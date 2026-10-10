// DVD over the host file source. Synchronous; never reports errors (ADR 0007 §4).
#include <algorithm>
#include <cstring>
#include <vector>

#include "sdk_internal.h"

using namespace mpt::gc;
using namespace mpt::gc::detail;

namespace mpt::gc::detail {

namespace {

std::string lowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

const std::vector<DvdEntry>& entries() { return impl().dvd; }

std::int32_t findChild(std::uint32_t dir, const std::string& lower) {
  const auto& e = entries();
  for (std::uint32_t i = dir + 1; i < e[dir].end;) {
    if (e[i].lower == lower) return static_cast<std::int32_t>(i);
    i = e[i].is_dir ? e[i].end : i + 1;
  }
  return -1;
}

std::int32_t resolve(const char* path) {
  if (!path) return -1;
  const auto& e = entries();
  std::uint32_t cur = path[0] == '/' ? 0 : st().dvd_cwd;
  const std::string p(path);
  std::size_t start = 0;
  while (start <= p.size()) {
    std::size_t slash = p.find('/', start);
    if (slash == std::string::npos) slash = p.size();
    const std::string part = p.substr(start, slash - start);
    start = slash + 1;
    if (part.empty() || part == ".") continue;
    if (!e[cur].is_dir) return -1;
    if (part == "..") {
      cur = e[cur].parent;
      continue;
    }
    const std::int32_t child = findChild(cur, lowerAscii(part));
    if (child < 0) return -1;
    cur = static_cast<std::uint32_t>(child);
  }
  return static_cast<std::int32_t>(cur);
}

const DvdEntry& fileEntry(const DVDFileInfo* fi, const char* what) {
  const auto& e = entries();
  if (!fi || fi->startAddr >= e.size() || e[fi->startAddr].is_dir) fatal(std::string(what) + ": file not open");
  return e[fi->startAddr];
}

void fillFileInfo(DVDFileInfo* fi, std::uint32_t entry) {
  std::memset(fi, 0, sizeof *fi);
  fi->startAddr = entry;
  fi->length = static_cast<u32>(entries()[entry].size);
  fi->cb.state = DVD_STATE_END;
}

void doRead(DVDFileInfo* fi, void* addr, s32 length, s32 offset) {
  const DvdEntry& f = fileEntry(fi, "DVDRead");
  if (length < 0 || offset < 0) fatal("DVDRead: negative length or offset");
  auto* dst = static_cast<std::byte*>(addr);
  const std::uint64_t off = static_cast<std::uint64_t>(offset);
  const std::uint64_t len = static_cast<std::uint64_t>(length);
  const std::uint64_t avail = off < f.size ? std::min<std::uint64_t>(len, f.size - off) : 0;
  if (avail > 0) {
    const IFileSource* src = impl().config.files;
    if (!src || !src->read(fi->startAddr, off, {dst, static_cast<std::size_t>(avail)}))
      fatal("DVDRead: host read failed for " + f.name);
  }
  // Past the end of the file: the disc would return the next file's bytes; we return zeros.
  if (len > avail) std::memset(dst + avail, 0, static_cast<std::size_t>(len - avail));
  fi->cb.state = DVD_STATE_END;
  fi->cb.transferredSize = static_cast<u32>(length);
  fi->cb.length = static_cast<u32>(length);
  fi->cb.offset = static_cast<u32>(offset);
  fi->cb.addr = addr;
}

}  // namespace

void dvdBuildIndex(Sdk::Impl& I) {
  I.dvd.clear();
  DvdEntry root;
  root.is_dir = true;
  I.dvd.push_back(root);
  if (!I.config.files) {
    I.dvd[0].end = 1;
    return;
  }
  const auto src = I.config.files->entries();
  std::vector<std::uint32_t> stack{0};  // open directories
  for (std::size_t i = 1; i < src.size(); ++i) {
    const FileSourceEntry& fe = src[i];
    const std::size_t slash = fe.path.rfind('/');
    const std::string parent_path = slash == std::string::npos ? "" : fe.path.substr(0, slash);
    // Close directories that do not contain this entry.
    while (stack.size() > 1) {
      const std::uint32_t top = stack.back();
      const std::string& tp = src[top].path;
      if (parent_path == tp) break;
      I.dvd[top].end = static_cast<std::uint32_t>(i);
      stack.pop_back();
    }
    DvdEntry d;
    d.name = slash == std::string::npos ? fe.path : fe.path.substr(slash + 1);
    d.lower = lowerAscii(d.name);
    d.is_dir = fe.is_dir;
    d.size = fe.size;
    d.parent = stack.back();
    I.dvd.push_back(d);
    if (fe.is_dir) stack.push_back(static_cast<std::uint32_t>(i));
  }
  for (std::uint32_t open : stack) I.dvd[open].end = static_cast<std::uint32_t>(src.size());
}

void dvdDispatch(const Event& e) {
  if (e.kind == EventKind::dvd_read) {
    reinterpret_cast<DVDCallback>(e.b)(static_cast<s32>(e.c), static_cast<DVDFileInfo*>(e.a));
  } else {
    reinterpret_cast<DVDCBCallback>(e.b)(static_cast<s32>(e.c), static_cast<DVDCommandBlock*>(e.a));
  }
}

}  // namespace mpt::gc::detail

extern "C" {

void DVDInit(void) {
  boundary();
  st().dvd_initialized = 1;
}

s32 DVDConvertPathToEntrynum(const char* path) {
  boundary();
  return resolve(path);
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  boundary();
  const std::int32_t e = resolve(fileName);
  if (e < 0 || entries()[static_cast<std::size_t>(e)].is_dir) return FALSE;
  fillFileInfo(fileInfo, static_cast<std::uint32_t>(e));
  return TRUE;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  boundary();
  if (entrynum < 0 || static_cast<std::size_t>(entrynum) >= entries().size() ||
      entries()[static_cast<std::size_t>(entrynum)].is_dir)
    return FALSE;
  fillFileInfo(fileInfo, static_cast<std::uint32_t>(entrynum));
  return TRUE;
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
  boundary();
  (void)fileInfo;
  return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  (void)prio;
  boundary();
  doRead(fileInfo, addr, length, offset);
  return length;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  (void)prio;
  boundary();
  doRead(fileInfo, addr, length, offset);
  fileInfo->callback = callback;
  if (callback)
    completeIo(Source::dvd, EventKind::dvd_read, fileInfo, reinterpret_cast<void*>(callback), length);
  return TRUE;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
  boundary();
  return block ? block->state : DVD_STATE_END;
}

s32 DVDGetDriveStatus(void) {
  boundary();
  return DVD_STATE_END;
}

BOOL DVDCancel(DVDCommandBlock* block) {
  boundary();
  (void)block;
  return TRUE;
}

BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  boundary();
  if (callback) completeIo(Source::dvd, EventKind::dvd_cancel, block, reinterpret_cast<void*>(callback), 0);
  return TRUE;
}

BOOL DVDSetAutoInvalidation(BOOL autoInval) {
  boundary();
  auto& s = st();
  const BOOL old = s.dvd_auto_invalidation;
  s.dvd_auto_invalidation = autoInval ? 1 : 0;
  return old;
}

BOOL DVDChangeDir(const char* dirName) {
  boundary();
  const std::int32_t e = resolve(dirName);
  if (e < 0 || !entries()[static_cast<std::size_t>(e)].is_dir) return FALSE;
  st().dvd_cwd = static_cast<std::uint32_t>(e);
  return TRUE;
}

BOOL DVDGetCurrentDir(char* path, u32 maxlen) {
  boundary();
  std::string p;
  for (std::uint32_t cur = st().dvd_cwd; cur != 0; cur = entries()[cur].parent) p = "/" + entries()[cur].name + p;
  if (p.empty()) p = "/";
  if (p.size() + 1 > maxlen) return FALSE;
  std::memcpy(path, p.c_str(), p.size() + 1);
  return TRUE;
}

BOOL DVDOpenDir(const char* dirName, DVDDir* dir) {
  boundary();
  const std::int32_t e = resolve(dirName);
  if (e < 0 || !entries()[static_cast<std::size_t>(e)].is_dir) return FALSE;
  dir->entryNum = static_cast<u32>(e);
  dir->location = static_cast<u32>(e) + 1;
  dir->next = entries()[static_cast<std::size_t>(e)].end;
  return TRUE;
}

BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
  boundary();
  if (dir->location >= dir->next || dir->location >= entries().size()) return FALSE;
  const DvdEntry& d = entries()[dir->location];
  dirent->entryNum = dir->location;
  dirent->isDir = d.is_dir ? TRUE : FALSE;
  dirent->name = const_cast<char*>(d.name.c_str());
  dir->location = d.is_dir ? d.end : dir->location + 1;
  return TRUE;
}

BOOL DVDCloseDir(DVDDir* dir) {
  boundary();
  (void)dir;
  return TRUE;
}

}  // extern "C"
