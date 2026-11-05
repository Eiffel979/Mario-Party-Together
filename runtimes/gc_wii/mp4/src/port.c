/* See mpt_mp4_port.h. Compiled with the game's headers (aurora's Dolphin headers, Party Board's
 * include/). The address table is adapted from Party Board's src/port/dolassets.cpp (CC0-1.0), reduced
 * to GMPE01_00, the only revision Phase 1 supports (plan D3). It holds addresses and sizes only: the
 * bytes come from the user's own main.dol at run time. */
#include <stdint.h>
#include <string.h>

#include "dolphin.h"
#include "game/dvd.h"
#include "game/memory.h"
#include "port/dolassets.h"
#include "port/port_version.h"
#include "port/settings.h"
#include "version.h"

#include "mpt_mp4_port.h"

MptMp4PortConfig mpt_mp4_port_config;

/* ---- Party Board settings and version queries -------------------------------------------------- */

CARDFileType partyboard_settings_card_file_type(void) { return CARD_RAWIMAGE; } /* unused: see card.c */
bool partyboard_settings_enableTurboKeybind(void) { return false; }
bool partyboard_settings_skipBootSequence(void) { return mpt_mp4_port_config.skip_boot_sequence != 0; }
bool partyboard_settings_unlock_all_minigames(void) { return mpt_mp4_port_config.unlock_all_minigames != 0; }
bool partyboard_settings_unlock_bowsers_gnarly_party(void) {
  return mpt_mp4_port_config.unlock_bowsers_gnarly_party != 0;
}

u8 partyboard_version_get_version(void) { return VERSION_NO_ENG0; }
BOOL partyboard_version_is_pal(void) { return FALSE; }
BOOL partyboard_version_is_usa(void) { return TRUE; }
BOOL partyboard_version_is_ntsc(void) { return TRUE; }

/* ---- DOL data reader ------------------------------------------------------------------------------ */

typedef struct DolBlob {
  uint32_t address; /* GMPE01_00 virtual address; 0 = not in this revision */
  uint32_t size;
} DolBlob;

static const DolBlob s_dol_blobs[DOL_INCLUDE_COUNT] = {
    [DOL_INCLUDE_ANK8X8_4B] = {0x8011FE00, 0x2000},
    [DOL_INCLUDE_ASCII8X8_1BPP] = {0x8012DCD7, 0x0800},
    [DOL_INCLUDE_HILITEDATA] = {0x8012C360, 0x0480},
    [DOL_INCLUDE_HILITEDATA2] = {0x8012C7E0, 0x0480},
    [DOL_INCLUDE_HILITEDATA3] = {0x8012CC60, 0x0480},
    [DOL_INCLUDE_HILITEDATA4] = {0x8012D0E0, 0x0480},
    [DOL_INCLUDE_REFMAPDATA0] = {0x801225A0, 0x1240},
    [DOL_INCLUDE_REFMAPDATA1] = {0x801237E0, 0x1100},
    [DOL_INCLUDE_REFMAPDATA2] = {0x801248E0, 0x2080},
    [DOL_INCLUDE_REFMAPDATA3] = {0x80126960, 0x2080},
    [DOL_INCLUDE_REFMAPDATA4] = {0x801289E0, 0x2080},
    [DOL_INCLUDE_TOONMAPDATA] = {0x8012AA60, 0x0880},
    [DOL_INCLUDE_TOONMAPDATA2] = {0x8012B2E0, 0x1080},
    [DOL_INCLUDE_COVEROPEN_EN] = {0x80132208, 0x1384},
    [DOL_INCLUDE_FATALERROR_EN] = {0x8013358C, 0x1384},
    [DOL_INCLUDE_LOADING_EN] = {0x80134910, 0x1384},
    [DOL_INCLUDE_NODISC_EN] = {0x80135C94, 0x1384},
    [DOL_INCLUDE_RETRYERROR_EN] = {0x80137018, 0x1384},
    [DOL_INCLUDE_WRONGDISC_EN] = {0x8013839C, 0x1384},
    /* the French, German, Italian and Spanish screens exist only on PAL discs */
};

/* bootDll.rel's Nintendo logo, read through the DVD layer into the DVD heap as on Party Board. */
#define NINTENDODATA_PATH "dll/bootDll.rel"
#define NINTENDODATA_OFFSET (0x2B80u + 0xA0u)
#define NINTENDODATA_SIZE 0x307Du

/* The blobs are copied into module storage at attach, so no host pointer reaches game state: the game
 * keeps pointers to them, and module data has a fixed address at a fixed module base (ADR 0008
 * section 11). This file's data is part of the game-core state range. Offsets are 32-byte aligned. */
#define MPT_DOL_BLOB_BYTES 0x15000u /* 0x14DA0 used by GMPE01_00 */
static unsigned char s_blob_storage[MPT_DOL_BLOB_BYTES] __attribute__((aligned(32)));
static uint32_t s_blob_offset[DOL_INCLUDE_COUNT]; /* 0 = not attached; offsets start at 32 */

static uint32_t be32(const unsigned char* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* The DOL header: 7 text and 11 data sections, as file offsets (0x00), load addresses (0x48) and
 * sizes (0x90), all big-endian. Returns the blob's bytes, or NULL if no section holds all of it. */
static const unsigned char* dol_find(const unsigned char* dol, size_t size, uint32_t address, uint32_t length) {
  if (size < 0x100) {
    return NULL;
  }
  for (int i = 0; i < 18; ++i) {
    const uint32_t off = be32(dol + 0x00 + 4 * i);
    const uint32_t addr = be32(dol + 0x48 + 4 * i);
    const uint32_t len = be32(dol + 0x90 + 4 * i);
    if (len == 0 || address < addr || address - addr > len || len - (address - addr) < length) {
      continue;
    }
    const uint64_t file_off = (uint64_t)off + (address - addr);
    if (file_off + length > size) {
      return NULL;
    }
    return dol + file_off;
  }
  return NULL;
}

static void set_error(char* error, size_t error_size, const char* message) {
  if (error != NULL && error_size > 0) {
    size_t n = strlen(message);
    if (n > error_size - 1) n = error_size - 1;
    memcpy(error, message, n);
    error[n] = '\0';
  }
}

int mpt_mp4_dol_attach(const unsigned char* dol, size_t size, char* error, size_t error_size) {
  uint32_t at = 32;
  for (int i = 0; i < DOL_INCLUDE_COUNT; ++i) {
    s_blob_offset[i] = 0;
    if (s_dol_blobs[i].address == 0) {
      continue;
    }
    const unsigned char* found = dol_find(dol, size, s_dol_blobs[i].address, s_dol_blobs[i].size);
    if (found == NULL) {
      set_error(error, error_size, "main.dol does not have GMPE01_00's layout (a data blob is outside every section)");
      return 0;
    }
    if (at + s_dol_blobs[i].size > MPT_DOL_BLOB_BYTES) {
      set_error(error, error_size, "DOL blob storage is too small");
      return 0;
    }
    memcpy(s_blob_storage + at, found, s_dol_blobs[i].size);
    s_blob_offset[i] = at;
    at = (at + s_dol_blobs[i].size + 31u) & ~31u;
  }
  return 1;
}

void mpt_mp4_dol_detach(void) { memset(s_blob_offset, 0, sizeof s_blob_offset); }

void InitializeDol(void) {} /* mpt_mp4_dol_attach replaces it */

/* The game only reads the blobs. */
void* GetDolIncludeData(int index) {
  if (index < 0 || index >= DOL_INCLUDE_COUNT || s_blob_offset[index] == 0) {
    OSPanic(__FILE__, __LINE__, "DOL data %d is not available in GMPE01_00", index);
  }
  return s_blob_storage + s_blob_offset[index];
}

void* GetRelIncludeData(int index) {
  if (index != REL_INCLUDE_NINTENDODATA) {
    OSPanic(__FILE__, __LINE__, "REL data %d is not available", index);
  }
  DVDFileInfo file;
  if (!DVDOpen(NINTENDODATA_PATH, &file)) {
    OSPanic(__FILE__, __LINE__, "cannot open %s", NINTENDODATA_PATH);
  }
  void* buffer = HuMemDirectMalloc(HEAP_DVD, NINTENDODATA_SIZE);
  if (DVDReadPrio(&file, buffer, (s32)NINTENDODATA_SIZE, (s32)NINTENDODATA_OFFSET, 2) < 0) {
    OSPanic(__FILE__, __LINE__, "cannot read %s", NINTENDODATA_PATH);
  }
  DVDClose(&file);
  return buffer;
}
