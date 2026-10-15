/* The C ABI between the MP4 game module (mp4_game.dll / mp4_game.so, built with Clang) and the host
 * runtime (Mp4Runtime, P1-16, built with the host compiler). ADR 0008, amendment after P1-06.
 *
 * DRAFT v1 for architect review (P1-15 piece 7).
 *
 * Rules:
 * - The module exports exactly one symbol, mpt_mp4_get_api. Everything else is reached through the
 *   function table it returns, so a host can check the version before calling anything.
 * - Plain C: fixed-width integers, no C++ types, no struct passed by value, no ownership crossing the
 *   boundary. Memory the module returns stays owned by the module and is valid until the next call
 *   that names the same game (or destroy), unless a function says otherwise.
 * - Every struct starts with `struct_size`, which the caller sets to sizeof the struct it was compiled
 *   with. A later minor version only appends fields; the module reads and writes no more than
 *   struct_size bytes. A major change bumps MPT_MP4_ABI_VERSION and the module refuses other versions.
 * - Single-threaded: one game at a time per process (the SDK has one live instance), all calls from one
 *   thread. Calls are not reentrant (no call from inside the report callback).
 * - Determinism (ADR 0008): the module reads no wall clock, host RNG or thread timing. Its output is a
 *   function of the boot config, the disc files and the input frames.
 */
#ifndef MPT_MP4_GAME_ABI_H
#define MPT_MP4_GAME_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPT_MP4_ABI_VERSION 1u

#if defined(_WIN32)
#define MPT_MP4_EXPORT __declspec(dllexport)
#else
#define MPT_MP4_EXPORT __attribute__((visibility("default")))
#endif

typedef struct MptMp4Game MptMp4Game; /* opaque */

typedef enum MptMp4Status {
  MPT_MP4_OK = 0,
  MPT_MP4_ERR_VERSION = 1,     /* ABI version or struct_size not supported */
  MPT_MP4_ERR_ARGUMENT = 2,    /* a null or malformed argument */
  MPT_MP4_ERR_DISC = 3,        /* the extracted disc is missing, unreadable or not GMPE01_00 */
  MPT_MP4_ERR_SDK = 4,         /* the SDK, renderer or sound engine failed to start */
  MPT_MP4_ERR_STATE = 5,       /* a save state from another build, process or arena base */
  MPT_MP4_ERR_BUFFER = 6,      /* the caller's buffer is too small; the needed size is reported */
  MPT_MP4_ERR_BUSY = 7         /* a game already exists in this process */
} MptMp4Status;

typedef enum MptMp4StepResult {
  MPT_MP4_STEP_RUNNING = 0,
  MPT_MP4_STEP_HALTED = 1,          /* the game's main loop returned */
  MPT_MP4_STEP_RESET_REQUESTED = 2, /* the game called OSResetSystem */
  MPT_MP4_STEP_FAULTED = 3          /* see fault() */
} MptMp4StepResult;

/* An error message from create/load_state, written by the module (NUL-terminated, truncated). */
typedef struct MptMp4Error {
  uint32_t struct_size;
  char message[256];
} MptMp4Error;

typedef enum MptMp4VideoBackend {
  MPT_MP4_VIDEO_NULL = 0, /* no GPU work (replays, CI); video() reports no frame */
  MPT_MP4_VIDEO_GPU = 1   /* the GX layer's renderer; frames read back as RGBA8 (ADR 0002 rule 8) */
} MptMp4VideoBackend;

/* Boot flags: game behaviour fixed for the session (set by the host from the ruleset, ADR 0008). */
#define MPT_MP4_BOOT_SKIP_BOOT_SEQUENCE 0x1u          /* straight to mode select */
#define MPT_MP4_BOOT_UNLOCK_ALL_MINIGAMES 0x2u
#define MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY 0x4u

typedef struct MptMp4BootConfig {
  uint32_t struct_size;
  uint32_t abi_version;     /* MPT_MP4_ABI_VERSION */
  uint64_t seed;            /* RuntimeConfig::seed: derives the virtual clock's epoch */
  const char* disc_root;    /* UTF-8 path of the extracted disc (sys/, files/), VerifiedDump::build_dir */
  uint32_t flags;           /* MPT_MP4_BOOT_* */
  uint32_t video_backend;   /* MptMp4VideoBackend */
  uint32_t internal_scale;  /* render scale for the GPU backend, 1 = 640x480 */
  uint32_t audio_output;    /* 0 = null audio (everything game-visible still runs) */
  const uint8_t* save_media;  /* card image (SaveMedia::data) or NULL for a blank card */
  size_t save_media_size;
  uint32_t save_media_format; /* SaveMedia::format_version; must match card_format() when given */
  uint32_t reserved0;
  uintptr_t arena_base;     /* fixed SDK arena address for fresh-process save states, 0 = anywhere */
  const char* cache_dir;    /* UTF-8 pipeline cache directory for the GPU backend, or NULL */
} MptMp4BootConfig;

/* Byte-for-byte the layout of mpt::InputFrame (core/include/mpt/core/input.h, 136 bytes). */
typedef struct MptMp4Pad {
  uint32_t buttons;
  int16_t stick_x, stick_y, c_stick_x, c_stick_y;
  uint8_t trigger_l, trigger_r, connected, reserved;
  int16_t accel[3];
  int16_t gyro[3];
  int16_t pointer_x, pointer_y;
} MptMp4Pad;

typedef struct MptMp4InputFrame {
  uint64_t frame;
  MptMp4Pad pads[4];
} MptMp4InputFrame;

typedef struct MptMp4FrameRate {
  uint32_t numerator;
  uint32_t denominator;
} MptMp4FrameRate;

/* The last presented frame: width * height RGBA8 pixels, rows top to bottom, no padding.
 * pixels == NULL (width == height == 0) when no frame was presented (null backend). */
typedef struct MptMp4Video {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t reserved;
  const uint32_t* pixels;
} MptMp4Video;

/* Audio produced since the last call: interleaved s16 stereo. */
typedef struct MptMp4Audio {
  uint32_t struct_size;
  uint32_t sample_rate;
  uint32_t channels; /* 2 */
  uint32_t reserved;
  const int16_t* samples;
  size_t frame_count; /* samples has frame_count * channels values */
} MptMp4Audio;

/* Raw party state for the adapter's mirroring (mapped to content IDs on the host side). Values are
 * the game's own: character is the game's character index (-1 = none), board is GWSystem's board
 * number, overlay is the running overlay's index in the game's overlay list. */
typedef struct MptMp4PartyPlayer {
  int32_t character;
  int32_t is_cpu;
  int32_t coins;
  int32_t stars;
  int32_t space; /* current board space index, -1 when not on a board */
} MptMp4PartyPlayer;

typedef struct MptMp4Party {
  uint32_t struct_size;
  int32_t overlay;          /* running overlay index, -1 before the first */
  char overlay_name[32];    /* its name in the overlay list, e.g. "w01Dll" */
  int32_t board;
  int32_t turn;
  int32_t max_turns;
  MptMp4PartyPlayer players[4];
} MptMp4Party;

typedef void (*MptMp4ReportFn)(void* user, const char* text);

typedef struct MptMp4Api {
  uint32_t struct_size;
  uint32_t abi_version;
  const char* build_id; /* the module's build fingerprint (pins, patches, compiler); for save states */

  MptMp4Status (*create)(const MptMp4BootConfig* config, MptMp4Game** out, MptMp4Error* error);
  void (*destroy)(MptMp4Game* game);

  /* One VI field. input->frame must equal frame(game). */
  MptMp4StepResult (*step)(MptMp4Game* game, const MptMp4InputFrame* input);
  uint64_t (*frame)(const MptMp4Game* game);
  const char* (*fault)(const MptMp4Game* game); /* "" unless faulted */
  void (*frame_rate)(const MptMp4Game* game, MptMp4FrameRate* out);

  /* ADR 0008 section 8: SDK, sound engine and game state, by value. */
  uint64_t (*state_hash)(const MptMp4Game* game);
  /* Writes the state to buffer (or only reports its size when buffer is NULL). Between steps only.
   * Valid for the same build in the same process at the same arena base (ADR 0008 sections 10-11). */
  MptMp4Status (*save_state)(const MptMp4Game* game, uint8_t* buffer, size_t capacity, size_t* size);
  MptMp4Status (*load_state)(MptMp4Game* game, const uint8_t* data, size_t size, MptMp4Error* error);

  /* ADR 0004: the memory card. The generation counts committed writes. */
  uint64_t (*save_media_generation)(const MptMp4Game* game);
  MptMp4Status (*save_media)(const MptMp4Game* game, uint8_t* buffer, size_t capacity, size_t* size,
                             uint32_t* format);
  uint32_t (*card_format)(void);

  /* Presentation only, never game input. */
  void (*video)(MptMp4Game* game, MptMp4Video* out);
  void (*audio)(MptMp4Game* game, MptMp4Audio* out);
  void (*rumble)(const MptMp4Game* game, uint32_t out[4]);
  void (*set_report)(MptMp4Game* game, MptMp4ReportFn fn, void* user); /* OSReport text */

  void (*party)(const MptMp4Game* game, MptMp4Party* out);
} MptMp4Api;

/* The module's only export. Returns NULL if it does not implement `abi_version`. */
MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(uint32_t abi_version);
typedef const MptMp4Api* (*MptMp4GetApiFn)(uint32_t abi_version);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_GAME_ABI_H */
