/* CARD: the memory card in slot A, served from the host's SaveMedia image
 * (ADR 0004). Slot B is always empty. Every operation completes synchronously;
 * async callbacks run at the next SDK call boundary (ADR 0007 §4). The card
 * serial is a fixed constant stored in the image (ADR 0004 rule 7), and save
 * timestamps come from virtual time (rule 8).
 */
#ifndef MPT_GC_CARD_H
#define MPT_GC_CARD_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define CARD_SLOT_A 0
#define CARD_SLOT_B 1
#define CARD_FILENAME_MAX 32
#define CARD_MAX_FILE 127
#define CARD_ICON_MAX 8
#define CARD_READ_SIZE 512
#define CARD_COMMENT_SIZE 64
#define CARD_WORKAREA_SIZE (5 * 8 * 1024)

#define CARD_RESULT_UNLOCKED 1
#define CARD_RESULT_READY 0
#define CARD_RESULT_BUSY -1
#define CARD_RESULT_WRONGDEVICE -2
#define CARD_RESULT_NOCARD -3
#define CARD_RESULT_NOFILE -4
#define CARD_RESULT_IOERROR -5
#define CARD_RESULT_BROKEN -6
#define CARD_RESULT_EXIST -7
#define CARD_RESULT_NOENT -8
#define CARD_RESULT_INSSPACE -9
#define CARD_RESULT_NOPERM -10
#define CARD_RESULT_LIMIT -11
#define CARD_RESULT_NAMETOOLONG -12
#define CARD_RESULT_ENCODING -13
#define CARD_RESULT_CANCELED -14
#define CARD_RESULT_FATAL_ERROR -128

#define CARD_ENCODE_ANSI 0u
#define CARD_ENCODE_SJIS 1u

typedef void (*CARDCallback)(s32 chan, s32 result);

typedef struct CARDFileInfo {
  s32 chan;
  s32 fileNo;
  s32 offset;
  s32 length;
  u16 iBlock;
  u16 __padding;
} CARDFileInfo;

typedef struct CARDStat {
  char fileName[CARD_FILENAME_MAX];
  u32 length;
  u32 time; /* seconds since 2000-01-01 (virtual time) */
  u8 gameName[4];
  u8 company[2];
  u8 bannerFormat;
  u8 __padding;
  u32 iconAddr;
  u16 iconFormat;
  u16 iconSpeed;
  u32 commentAddr;
  u32 offsetBanner;
  u32 offsetBannerTlut;
  u32 offsetIcon[CARD_ICON_MAX];
  u32 offsetIconTlut;
  u32 offsetData;
} CARDStat;

void CARDInit(void);
BOOL CARDProbe(s32 chan);
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize);
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback);
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback);
s32 CARDUnmount(s32 chan);
s32 CARDCheck(s32 chan);
s32 CARDCheckAsync(s32 chan, CARDCallback callback);
s32 CARDCheckEx(s32 chan, s32* xferBytes);
s32 CARDFormat(s32 chan);
s32 CARDFormatAsync(s32 chan, CARDCallback callback);
s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed);
s32 CARDGetSectorSize(s32 chan, u32* size);
s32 CARDGetMemSize(s32 chan, u16* size);
s32 CARDGetEncoding(s32 chan, u16* encode);
s32 CARDGetSerialNo(s32 chan, u64* serialNo);
s32 CARDGetResultCode(s32 chan);
/* Files belong to the game and maker code in SdkConfig (the disc ID on the
 * console); CARDOpen only sees files with that code. */
s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo);
s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo);
s32 CARDClose(CARDFileInfo* fileInfo);
s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo);
s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback);
s32 CARDRead(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset);
s32 CARDReadAsync(CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback);
s32 CARDWrite(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset);
s32 CARDWriteAsync(CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, CARDCallback callback);
s32 CARDDelete(s32 chan, const char* fileName);
s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback);
s32 CARDFastDelete(s32 chan, s32 fileNo);
s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat);
s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat);
s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback);
s32 CARDRename(s32 chan, const char* oldName, const char* newName);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_CARD_H */
