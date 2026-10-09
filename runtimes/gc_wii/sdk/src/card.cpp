// CARD over the host's save image (ADR 0004, ADR 0007 §4).
//
// The image format is ours (format version 1), little-endian, written by value:
//   "MPTGCCRD" | u32 version | u16 size_mbits | u16 encoding | u64 serial | u32 file_count
//   then per file, in directory-slot order:
//   u32 slot | u8 game[4] | u8 company[2] | u8 banner_format | u8 permission | char name[32]
//   | u32 time | u32 icon_addr | u16 icon_format | u16 icon_speed | u32 comment_addr
//   | u16 block_count | block_count * 8192 data bytes
// Nothing else, no trailing bytes. File data is laid out contiguously in slot
// order on load, so an image always loads into the same arena bytes.
#include <algorithm>
#include <cstring>

#include "sdk_internal.h"

using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace {

constexpr char kMagic[8] = {'M', 'P', 'T', 'G', 'C', 'C', 'R', 'D'};
constexpr std::uint32_t kImageVersion = 1;
constexpr std::uint32_t kNoAddr = 0xFFFF'FFFFu;

CardState& card() { return st().card; }

std::uint16_t userBlocksFor(std::uint16_t mbits) { return static_cast<std::uint16_t>(mbits * 16u - kCardSystemBlocks); }

std::byte* blockData(std::uint32_t block) { return impl().cardData() + std::size_t{block} * kCardSectorSize; }

std::uint32_t secondsNow() { return static_cast<std::uint32_t>(st().now / OS_TIMER_CLOCK); }

std::size_t nameLen(const char* name) {
  std::size_t n = 0;
  while (n <= CARD_FILENAME_MAX && name[n]) ++n;
  return n;
}

bool sameName(const CardDirEntry& d, const char* name) {
  const std::size_t n = nameLen(name);
  if (n > CARD_FILENAME_MAX) return false;
  for (std::size_t i = 0; i < CARD_FILENAME_MAX; ++i) {
    const char c = i < n ? name[i] : '\0';
    if (d.file_name[i] != c) return false;
    if (c == '\0') return true;
  }
  return true;
}

bool ownedByGame(const CardDirEntry& d) {
  const auto& cc = impl().config.card;
  return std::memcmp(d.game_name, cc.game_code.data(), 4) == 0 && std::memcmp(d.company, cc.maker_code.data(), 2) == 0;
}

std::uint32_t usedBlocks() {
  std::uint32_t n = 0;
  for (const CardDirEntry& d : card().dir)
    if (d.used) n += d.block_count;
  return n;
}

// Packs every file to the front of the data area, in order of position.
void compact() {
  auto& c = card();
  std::int32_t order[CARD_MAX_FILE];
  int n = 0;
  for (int i = 0; i < CARD_MAX_FILE; ++i)
    if (c.dir[i].used) order[n++] = i;
  std::sort(order, order + n, [&](int a, int b) { return c.dir[a].start_block < c.dir[b].start_block; });
  std::uint16_t next = 0;
  for (int k = 0; k < n; ++k) {
    CardDirEntry& d = c.dir[order[k]];
    if (d.start_block != next)
      std::memmove(blockData(next), blockData(d.start_block), std::size_t{d.block_count} * kCardSectorSize);
    d.start_block = next;
    next = static_cast<std::uint16_t>(next + d.block_count);
  }
  std::memset(blockData(next), 0, std::size_t{c.user_blocks - next} * kCardSectorSize);
}

// First-fit contiguous run; compacts once if the free space is fragmented.
int allocBlocks(std::uint16_t count) {
  auto& c = card();
  for (int pass = 0; pass < 2; ++pass) {
    std::uint16_t start = 0;
    for (;;) {
      if (start + count > c.user_blocks) break;
      bool clash = false;
      for (const CardDirEntry& d : c.dir) {
        if (!d.used) continue;
        if (start < d.start_block + d.block_count && d.start_block < start + count) {
          start = static_cast<std::uint16_t>(d.start_block + d.block_count);
          clash = true;
          break;
        }
      }
      if (!clash) return start;
    }
    compact();
  }
  return -1;
}

s32 setResult(s32 chan, s32 result) {
  if (chan == CARD_SLOT_A) card().result = result;
  return result;
}

// Synchronous completion: errors are returned with no callback; success also
// schedules the callback (ADR 0007 §4).
s32 finishAsync(s32 chan, s32 result, CARDCallback cb) {
  setResult(chan, result);
  if (result >= CARD_RESULT_READY && cb)
    completeIo(Source::card, EventKind::card, nullptr, reinterpret_cast<void*>(cb),
               (static_cast<std::int64_t>(chan) << 32) | static_cast<std::uint32_t>(result));
  return result;
}

s32 checkSlot(s32 chan, bool need_mount) {
  if (chan != CARD_SLOT_A || !card().inserted) return CARD_RESULT_NOCARD;
  if (need_mount && !card().mounted) return CARD_RESULT_NOCARD;
  return CARD_RESULT_READY;
}

void committed() { ++card().generation; }

int findByName(const char* name) {
  for (int i = 0; i < CARD_MAX_FILE; ++i) {
    const CardDirEntry& d = card().dir[i];
    if (d.used && ownedByGame(d) && sameName(d, name)) return i;
  }
  return -1;
}

void fillInfo(CARDFileInfo* fi, s32 chan, int slot) {
  const CardDirEntry& d = card().dir[slot];
  fi->chan = chan;
  fi->fileNo = slot;
  fi->offset = 0;
  fi->length = static_cast<s32>(std::uint32_t{d.block_count} * kCardSectorSize);
  fi->iBlock = static_cast<u16>(d.start_block + kCardSystemBlocks);
  fi->__padding = 0;
}

const CardDirEntry* openEntry(const CARDFileInfo* fi) {
  if (!fi || fi->chan != CARD_SLOT_A || fi->fileNo < 0 || fi->fileNo >= CARD_MAX_FILE) return nullptr;
  const CardDirEntry& d = card().dir[fi->fileNo];
  return d.used ? &d : nullptr;
}

s32 doDelete(int slot) {
  CardDirEntry& d = card().dir[slot];
  std::memset(blockData(d.start_block), 0, std::size_t{d.block_count} * kCardSectorSize);
  d = CardDirEntry{};
  committed();
  return CARD_RESULT_READY;
}

// ---- Image (de)serialization ----------------------------------------------------------
struct Writer {
  std::vector<std::byte>& out;
  void bytes(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    out.insert(out.end(), b, b + n);
  }
  void u8(std::uint8_t v) { bytes(&v, 1); }
  void u16(std::uint16_t v) {
    const std::uint8_t b[2] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
    bytes(b, 2);
  }
  void u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i)));
  }
  void u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (8 * i)));
  }
};

struct Reader {
  std::span<const std::byte> in;
  std::size_t pos = 0;
  bool ok = true;
  bool bytes(void* p, std::size_t n) {
    if (!ok || in.size() - pos < n) return ok = false;
    std::memcpy(p, in.data() + pos, n);
    pos += n;
    return true;
  }
  std::uint8_t u8() {
    std::uint8_t v = 0;
    bytes(&v, 1);
    return v;
  }
  std::uint16_t u16() {
    std::uint8_t b[2] = {};
    bytes(b, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
  }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t{u8()} << (8 * i);
    return v;
  }
  std::uint64_t u64() {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= std::uint64_t{u8()} << (8 * i);
    return v;
  }
};

}  // namespace

namespace mpt::gc::detail {

Result cardBoot(Sdk::Impl& I) {
  CardState& c = I.st->card;
  c = CardState{};
  const CardConfig& cfg = I.config.card;
  c.inserted = cfg.inserted ? 1 : 0;
  c.result = CARD_RESULT_READY;
  if (!cfg.image) {
    c.size_mbits = cfg.size_mbits;
    c.user_blocks = userBlocksFor(cfg.size_mbits);
    c.encoding = CARD_ENCODE_ANSI;
    c.serial = kStableCardSerial;
    return Result::success();
  }
  if (cfg.image->format_version != kImageVersion) return Result::failure("card image: unsupported format version");
  Reader r{cfg.image->data};
  char magic[8] = {};
  r.bytes(magic, 8);
  if (!r.ok || std::memcmp(magic, kMagic, 8) != 0) return Result::failure("card image: bad magic");
  if (r.u32() != kImageVersion) return Result::failure("card image: unsupported format version");
  c.size_mbits = r.u16();
  if (c.size_mbits != 4 && c.size_mbits != 8 && c.size_mbits != 16)
    return Result::failure("card image: bad card size");
  c.user_blocks = userBlocksFor(c.size_mbits);
  c.encoding = r.u16();
  if (c.encoding > CARD_ENCODE_SJIS) return Result::failure("card image: bad encoding");
  c.serial = r.u64();
  const std::uint32_t count = r.u32();
  if (!r.ok || count > CARD_MAX_FILE) return Result::failure("card image: bad file count");
  std::uint16_t next_block = 0;
  for (std::uint32_t k = 0; k < count; ++k) {
    const std::uint32_t slot = r.u32();
    if (!r.ok || slot >= CARD_MAX_FILE || c.dir[slot].used) return Result::failure("card image: bad directory slot");
    CardDirEntry& d = c.dir[slot];
    r.bytes(d.game_name, 4);
    r.bytes(d.company, 2);
    d.banner_format = r.u8();
    d.permission = r.u8();
    r.bytes(d.file_name, CARD_FILENAME_MAX);
    d.time = r.u32();
    d.icon_addr = r.u32();
    d.icon_format = r.u16();
    d.icon_speed = r.u16();
    d.comment_addr = r.u32();
    d.block_count = r.u16();
    if (!r.ok || d.file_name[0] == '\0' || d.block_count == 0) return Result::failure("card image: bad file entry");
    if (std::uint32_t{next_block} + d.block_count > c.user_blocks) return Result::failure("card image: card overfull");
    d.start_block = next_block;
    d.used = 1;
    if (!r.bytes(I.cardData() + std::size_t{next_block} * kCardSectorSize, std::size_t{d.block_count} * kCardSectorSize))
      return Result::failure("card image: truncated file data");
    next_block = static_cast<std::uint16_t>(next_block + d.block_count);
  }
  if (r.pos != cfg.image->data.size()) return Result::failure("card image: trailing bytes");
  return Result::success();
}

std::vector<std::byte> cardSerialize() {
  const CardState& c = card();
  std::vector<std::byte> out;
  Writer w{out};
  w.bytes(kMagic, 8);
  w.u32(kImageVersion);
  w.u16(c.size_mbits);
  w.u16(c.encoding);
  w.u64(c.serial);
  std::uint32_t count = 0;
  for (const CardDirEntry& d : c.dir) count += d.used;
  w.u32(count);
  for (std::uint32_t slot = 0; slot < CARD_MAX_FILE; ++slot) {
    const CardDirEntry& d = c.dir[slot];
    if (!d.used) continue;
    w.u32(slot);
    w.bytes(d.game_name, 4);
    w.bytes(d.company, 2);
    w.u8(d.banner_format);
    w.u8(d.permission);
    w.bytes(d.file_name, CARD_FILENAME_MAX);
    w.u32(d.time);
    w.u32(d.icon_addr);
    w.u16(d.icon_format);
    w.u16(d.icon_speed);
    w.u32(d.comment_addr);
    w.u16(d.block_count);
    w.bytes(blockData(d.start_block), std::size_t{d.block_count} * kCardSectorSize);
  }
  return out;
}

void cardHash(StateHasher& h) {
  const CardState& c = card();
  h.update_u64(c.inserted | (std::uint64_t{c.mounted} << 8) | (std::uint64_t{c.size_mbits} << 16) |
               (std::uint64_t{c.encoding} << 32) | (std::uint64_t{c.user_blocks} << 48));
  h.update_u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(c.result)));
  h.update_u64(c.serial);
  h.update_u64(c.generation);
  for (std::uint32_t slot = 0; slot < CARD_MAX_FILE; ++slot) {
    const CardDirEntry& d = c.dir[slot];
    if (!d.used) continue;
    h.update_u64(slot);
    h.update(std::as_bytes(std::span(d.game_name)));
    h.update(std::as_bytes(std::span(d.company)));
    h.update(std::as_bytes(std::span(d.file_name)));
    h.update_u64(d.banner_format | (std::uint64_t{d.permission} << 8) | (std::uint64_t{d.icon_format} << 16) |
                 (std::uint64_t{d.icon_speed} << 32));
    h.update_u64(d.time | (std::uint64_t{d.icon_addr} << 32));
    h.update_u64(d.comment_addr | (std::uint64_t{d.start_block} << 32) | (std::uint64_t{d.block_count} << 48));
    h.update({blockData(d.start_block), std::size_t{d.block_count} * kCardSectorSize});
  }
}

void cardDispatch(const Event& e) {
  const auto chan = static_cast<s32>(e.c >> 32);
  const auto result = static_cast<s32>(static_cast<std::uint32_t>(e.c));
  reinterpret_cast<CARDCallback>(e.b)(chan, result);
}

}  // namespace mpt::gc::detail

std::uint32_t mpt::gc::Sdk::card_image_format_version() { return kImageVersion; }

extern "C" {

void CARDInit(void) { boundary(); }

BOOL CARDProbe(s32 chan) {
  boundary();
  return chan == CARD_SLOT_A && card().inserted;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
  boundary();
  if (const s32 r = checkSlot(chan, false); r < 0) return r;
  if (memSize) *memSize = card().size_mbits;
  if (sectorSize) *sectorSize = static_cast<s32>(kCardSectorSize);
  return CARD_RESULT_READY;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
  (void)detachCallback;
  boundary();
  s32 r = checkSlot(chan, false);
  if (r >= 0 && !workArea) r = CARD_RESULT_FATAL_ERROR;
  if (r >= 0) card().mounted = 1;
  return finishAsync(chan, r, attachCallback);
}

s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
  return CARDMountAsync(chan, workArea, detachCallback, nullptr);
}

s32 CARDUnmount(s32 chan) {
  boundary();
  if (const s32 r = checkSlot(chan, false); r < 0) return setResult(chan, r);
  card().mounted = 0;
  return setResult(chan, CARD_RESULT_READY);
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback) {
  boundary();
  return finishAsync(chan, checkSlot(chan, true), callback);
}

s32 CARDCheck(s32 chan) { return CARDCheckAsync(chan, nullptr); }

s32 CARDCheckEx(s32 chan, s32* xferBytes) {
  if (xferBytes) *xferBytes = 0;
  return CARDCheck(chan);
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
  boundary();
  s32 r = checkSlot(chan, true);
  if (r >= 0) {
    auto& c = card();
    for (CardDirEntry& d : c.dir) d = CardDirEntry{};
    std::memset(impl().cardData(), 0, kCardRegionBytes);
    c.serial = kStableCardSerial;  // ADR 0004 rule 7
    c.encoding = CARD_ENCODE_ANSI;
    committed();
  }
  return finishAsync(chan, r, callback);
}

s32 CARDFormat(s32 chan) { return CARDFormatAsync(chan, nullptr); }

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  s32 files = 0;
  for (const CardDirEntry& d : card().dir) files += d.used ? 0 : 1;
  if (byteNotUsed) *byteNotUsed = static_cast<s32>((card().user_blocks - usedBlocks()) * kCardSectorSize);
  if (filesNotUsed) *filesNotUsed = files;
  return setResult(chan, CARD_RESULT_READY);
}

s32 CARDGetSectorSize(s32 chan, u32* size) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return r;
  *size = kCardSectorSize;
  return CARD_RESULT_READY;
}

s32 CARDGetMemSize(s32 chan, u16* size) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return r;
  *size = card().size_mbits;
  return CARD_RESULT_READY;
}

s32 CARDGetEncoding(s32 chan, u16* encode) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return r;
  *encode = card().encoding;
  return CARD_RESULT_READY;
}

s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return r;
  *serialNo = card().serial;
  return CARD_RESULT_READY;
}

s32 CARDGetResultCode(s32 chan) {
  boundary();
  if (chan != CARD_SLOT_A) return CARD_RESULT_NOCARD;
  return card().result;
}

s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (nameLen(fileName) > CARD_FILENAME_MAX) return setResult(chan, CARD_RESULT_NAMETOOLONG);
  const int slot = findByName(fileName);
  if (slot < 0) return setResult(chan, CARD_RESULT_NOFILE);
  fillInfo(fileInfo, chan, slot);
  return setResult(chan, CARD_RESULT_READY);
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  if (!card().dir[fileNo].used) return setResult(chan, CARD_RESULT_NOFILE);
  fillInfo(fileInfo, chan, fileNo);
  return setResult(chan, CARD_RESULT_READY);
}

s32 CARDClose(CARDFileInfo* fileInfo) {
  boundary();
  if (!fileInfo || fileInfo->chan != CARD_SLOT_A) return CARD_RESULT_NOCARD;
  fileInfo->chan = -1;
  return CARD_RESULT_READY;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
  boundary();
  s32 r = checkSlot(chan, true);
  if (r < 0) return setResult(chan, r);
  const std::size_t len = nameLen(fileName);
  if (len > CARD_FILENAME_MAX) return setResult(chan, CARD_RESULT_NAMETOOLONG);
  if (len == 0 || size == 0 || size % kCardSectorSize != 0) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  if (findByName(fileName) >= 0) return setResult(chan, CARD_RESULT_EXIST);
  int slot = -1;
  for (int i = 0; i < CARD_MAX_FILE && slot < 0; ++i)
    if (!card().dir[i].used) slot = i;
  if (slot < 0) return setResult(chan, CARD_RESULT_NOENT);
  const auto blocks = static_cast<std::uint16_t>(size / kCardSectorSize);
  if (usedBlocks() + blocks > card().user_blocks) return setResult(chan, CARD_RESULT_INSSPACE);
  const int start = allocBlocks(blocks);
  if (start < 0) return setResult(chan, CARD_RESULT_INSSPACE);
  CardDirEntry& d = card().dir[slot];
  d = CardDirEntry{};
  d.used = 1;
  const auto& cc = impl().config.card;
  std::memcpy(d.game_name, cc.game_code.data(), 4);
  std::memcpy(d.company, cc.maker_code.data(), 2);
  std::memcpy(d.file_name, fileName, len);
  d.time = secondsNow();
  d.icon_addr = kNoAddr;
  d.comment_addr = kNoAddr;
  d.start_block = static_cast<std::uint16_t>(start);
  d.block_count = blocks;
  std::memset(blockData(d.start_block), 0, std::size_t{blocks} * kCardSectorSize);
  committed();
  if (fileInfo) fillInfo(fileInfo, chan, slot);
  return finishAsync(chan, CARD_RESULT_READY, callback);
}

s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo) {
  return CARDCreateAsync(chan, fileName, size, fileInfo, nullptr);
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback) {
  boundary();
  const s32 chan = fileInfo ? fileInfo->chan : CARD_SLOT_B;
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  const CardDirEntry* d = openEntry(fileInfo);
  if (!d) return setResult(chan, CARD_RESULT_NOFILE);
  if (length < 0 || offset < 0 || length % CARD_READ_SIZE || offset % CARD_READ_SIZE)
    return setResult(chan, CARD_RESULT_FATAL_ERROR);
  if (std::int64_t{offset} + length > std::int64_t{d->block_count} * kCardSectorSize)
    return setResult(chan, CARD_RESULT_LIMIT);
  std::memcpy(addr, blockData(d->start_block) + offset, static_cast<std::size_t>(length));
  return finishAsync(chan, CARD_RESULT_READY, callback);
}

s32 CARDRead(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  return CARDReadAsync(fileInfo, addr, length, offset, nullptr);
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, CARDCallback callback) {
  boundary();
  const s32 chan = fileInfo ? fileInfo->chan : CARD_SLOT_B;
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (!openEntry(fileInfo)) return setResult(chan, CARD_RESULT_NOFILE);
  CardDirEntry& d = card().dir[fileInfo->fileNo];
  if (length < 0 || offset < 0 || length % static_cast<s32>(kCardSectorSize) ||
      offset % static_cast<s32>(kCardSectorSize))
    return setResult(chan, CARD_RESULT_FATAL_ERROR);
  if (std::int64_t{offset} + length > std::int64_t{d.block_count} * kCardSectorSize)
    return setResult(chan, CARD_RESULT_LIMIT);
  std::memcpy(blockData(d.start_block) + offset, addr, static_cast<std::size_t>(length));
  d.time = secondsNow();
  committed();
  return finishAsync(chan, CARD_RESULT_READY, callback);
}

s32 CARDWrite(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset) {
  return CARDWriteAsync(fileInfo, addr, length, offset, nullptr);
}

s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (nameLen(fileName) > CARD_FILENAME_MAX) return setResult(chan, CARD_RESULT_NAMETOOLONG);
  const int slot = findByName(fileName);
  if (slot < 0) return setResult(chan, CARD_RESULT_NOFILE);
  return finishAsync(chan, doDelete(slot), callback);
}

s32 CARDDelete(s32 chan, const char* fileName) { return CARDDeleteAsync(chan, fileName, nullptr); }

s32 CARDFastDelete(s32 chan, s32 fileNo) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  if (!card().dir[fileNo].used) return setResult(chan, CARD_RESULT_NOFILE);
  return setResult(chan, doDelete(fileNo));
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  const CardDirEntry& d = card().dir[fileNo];
  if (!d.used) return setResult(chan, CARD_RESULT_NOFILE);
  std::memset(stat, 0, sizeof *stat);
  std::memcpy(stat->fileName, d.file_name, CARD_FILENAME_MAX);
  stat->length = std::uint32_t{d.block_count} * kCardSectorSize;
  stat->time = d.time;
  std::memcpy(stat->gameName, d.game_name, 4);
  std::memcpy(stat->company, d.company, 2);
  stat->bannerFormat = d.banner_format;
  stat->iconAddr = d.icon_addr;
  stat->iconFormat = d.icon_format;
  stat->iconSpeed = d.icon_speed;
  stat->commentAddr = d.comment_addr;
  // Image offsets, laid out after iconAddr as the SDK does: banner (+ TLUT),
  // the icons, one shared TLUT for C8 icons, then the data.
  std::uint32_t off = d.icon_addr;
  if (off == kNoAddr) {
    stat->offsetBanner = stat->offsetBannerTlut = stat->offsetIconTlut = kNoAddr;
    for (u32& o : stat->offsetIcon) o = kNoAddr;
    stat->offsetData = d.comment_addr == kNoAddr ? 0 : d.comment_addr + CARD_COMMENT_SIZE;
    return setResult(chan, CARD_RESULT_READY);
  }
  switch (d.banner_format & 3u) {
    case 1:  // C8
      stat->offsetBanner = off;
      off += 96 * 32;
      stat->offsetBannerTlut = off;
      off += 512;
      break;
    case 2:  // RGB5A3
      stat->offsetBanner = off;
      off += 96 * 32 * 2;
      stat->offsetBannerTlut = kNoAddr;
      break;
    default:
      stat->offsetBanner = stat->offsetBannerTlut = kNoAddr;
      break;
  }
  bool c8 = false;
  for (int i = 0; i < CARD_ICON_MAX; ++i) {
    const unsigned fmt = (d.icon_format >> (2 * i)) & 3u;
    if (fmt == 1) {
      stat->offsetIcon[i] = off;
      off += 32 * 32;
      c8 = true;
    } else if (fmt == 2) {
      stat->offsetIcon[i] = off;
      off += 32 * 32 * 2;
    } else {
      stat->offsetIcon[i] = kNoAddr;
    }
  }
  stat->offsetIconTlut = c8 ? off : kNoAddr;
  if (c8) off += 512;
  stat->offsetData = off;
  return setResult(chan, CARD_RESULT_READY);
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  CardDirEntry& d = card().dir[fileNo];
  if (!d.used) return setResult(chan, CARD_RESULT_NOFILE);
  d.banner_format = stat->bannerFormat;
  d.icon_addr = stat->iconAddr;
  d.icon_format = stat->iconFormat;
  d.icon_speed = stat->iconSpeed;
  d.comment_addr = stat->commentAddr;
  d.time = secondsNow();
  stat->time = d.time;
  committed();
  return finishAsync(chan, CARD_RESULT_READY, callback);
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat) { return CARDSetStatusAsync(chan, fileNo, stat, nullptr); }

s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
  boundary();
  if (const s32 r = checkSlot(chan, true); r < 0) return setResult(chan, r);
  const std::size_t len = nameLen(newName);
  if (nameLen(oldName) > CARD_FILENAME_MAX || len > CARD_FILENAME_MAX)
    return setResult(chan, CARD_RESULT_NAMETOOLONG);
  if (len == 0) return setResult(chan, CARD_RESULT_FATAL_ERROR);
  const int slot = findByName(oldName);
  if (slot < 0) return setResult(chan, CARD_RESULT_NOFILE);
  if (findByName(newName) >= 0) return setResult(chan, CARD_RESULT_EXIST);
  CardDirEntry& d = card().dir[slot];
  std::memset(d.file_name, 0, CARD_FILENAME_MAX);
  std::memcpy(d.file_name, newName, len);
  d.time = secondsNow();
  committed();
  return setResult(chan, CARD_RESULT_READY);
}

}  // extern "C"
