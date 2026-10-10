/* DVD over the host-provided file source (the files asset-import extracted
 * into VerifiedDump::build_dir). Reads complete synchronously inside the call;
 * async callbacks run at the next SDK call boundary (or before return, if the
 * SDK is configured that way). The drive never reports errors (ADR 0007 §4).
 */
#ifndef MPT_GC_DVD_H
#define MPT_GC_DVD_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

#define DVD_STATE_FATAL_ERROR -1
#define DVD_STATE_END 0
#define DVD_STATE_BUSY 1
#define DVD_STATE_CANCELED 10

#define DVD_RESULT_GOOD 0
#define DVD_RESULT_FATAL_ERROR -1
#define DVD_RESULT_CANCELED -3

typedef struct DVDCommandBlock DVDCommandBlock;
typedef void (*DVDCBCallback)(s32 result, DVDCommandBlock* block);

struct DVDCommandBlock {
  DVDCommandBlock* next;
  DVDCommandBlock* prev;
  u32 command;
  s32 state;
  u32 offset;
  u32 length;
  void* addr;
  u32 currTransferSize;
  u32 transferredSize;
  void* id;
  DVDCBCallback callback;
  void* userData;
};

typedef struct DVDFileInfo DVDFileInfo;
typedef void (*DVDCallback)(s32 result, DVDFileInfo* fileInfo);

struct DVDFileInfo {
  DVDCommandBlock cb;
  u32 startAddr; /* entry number of the file (we have no disc offsets) */
  u32 length;
  DVDCallback callback;
};

typedef struct DVDDir {
  u32 entryNum;
  u32 location;
  u32 next;
} DVDDir;

typedef struct DVDDirEntry {
  u32 entryNum;
  BOOL isDir;
  char* name;
} DVDDirEntry;

void DVDInit(void);
s32 DVDConvertPathToEntrynum(const char* path);
BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo);
BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo);
BOOL DVDClose(DVDFileInfo* fileInfo);
/* Returns the number of bytes read (`length`). Bytes past the end of the file read as zero. */
s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio);
BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio);
#define DVDRead(fi, addr, len, off) DVDReadPrio((fi), (addr), (len), (off), 2)
#define DVDReadAsync(fi, addr, len, off, cb) DVDReadAsyncPrio((fi), (addr), (len), (off), (cb), 2)
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block);
#define DVDGetFileInfoStatus(fi) DVDGetCommandBlockStatus(&(fi)->cb)
s32 DVDGetDriveStatus(void);
BOOL DVDCancel(DVDCommandBlock* block);
BOOL DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback);
BOOL DVDSetAutoInvalidation(BOOL autoInval);
BOOL DVDChangeDir(const char* dirName);
BOOL DVDGetCurrentDir(char* path, u32 maxlen);
BOOL DVDOpenDir(const char* dirName, DVDDir* dir);
BOOL DVDReadDir(DVDDir* dir, DVDDirEntry* dirent);
BOOL DVDCloseDir(DVDDir* dir);

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_DVD_H */
