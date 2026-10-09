/* Basic types used by the GameCube/Wii SDK replacement layer's C API.
 *
 * These declarations are our own, written from the public Dolphin SDK API
 * surface (function names, argument order, documented constants). Game code
 * in P1-15 compiles against the pinned aurora Dolphin headers instead (ADR
 * 0007 §6). Our implementations therefore treat most SDK objects (OSThread,
 * OSAlarm, OSMessageQueue, OSThreadQueue, OSMutex) as opaque keys and keep
 * their state in the SDK's own tables, so the game-side struct layout does not
 * matter. Structs whose fields the SDK must write (PADStatus, DVDFileInfo,
 * CARDFileInfo, CARDStat, OSCalendarTime, ARQRequest) follow the public SDK
 * layout; P1-15 adds static_asserts against the pinned headers.
 */
#ifndef MPT_GC_TYPES_H
#define MPT_GC_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
typedef int BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifdef __cplusplus
#define MPT_GC_EXTERN_C_BEGIN extern "C" {
#define MPT_GC_EXTERN_C_END }
#else
#define MPT_GC_EXTERN_C_BEGIN
#define MPT_GC_EXTERN_C_END
#endif

#endif /* MPT_GC_TYPES_H */
