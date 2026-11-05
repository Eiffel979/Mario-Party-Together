/* The C ABI between the MP4 game module (mp4_game.dll / mp4_game.so, built with Clang) and the host
 * runtime (Mp4Runtime, P1-16, built with the host compiler). ADR 0008, amendment after P1-06.
 *
 * Version 1.0: the v1 draft as approved by architect, with the required changes of the ADR 0008 ABI
 * review (2026-10-19).
 *
 * Rules:
 * - The module exports exactly one symbol, mpt_mp4_get_api. Everything else is reached through the
 *   function table it returns, so a host can check the version before calling anything.
 * - Plain C: fixed-width integers, no C++ types, no struct passed by value, no ownership crossing the
 *   boundary.
 * - Lifetimes (ADR 0002 rule 6): memory the module returns (video pixels, audio samples, fault text,
 *   the build ID) stays owned by the module and valid until the next step, load_state or destroy of
 *   that game. Query functions never invalidate anything, and calling one twice gives the same answer.
 * - Versioning: MPT_MP4_ABI_VERSION is the major version; the module refuses any other. Within major
 *   version 1, structs with `struct_size` only grow at the end. The caller sets struct_size to the size
 *   it was compiled with, which must be at least the struct's v1.0 size (the MPT_MP4_*_SIZE_V1_0
 *   constants). The module reads and writes no more than struct_size bytes, and fields past the
 *   caller's size take their v1.0 defaults. The module sets MptMp4Api::struct_size; a host checks that
 *   it covers every function it calls, and entries added in a minor version may be NULL. Structs
 *   without struct_size are frozen for major version 1 (asserted below).
 * - Single-threaded: one game at a time per process (the SDK has one live instance), all calls from one
 *   thread. Calls are not reentrant (no call from inside the report callback).
 * - Determinism (ADR 0008): the module reads no wall clock, host RNG or thread timing. Its output is a
 *   function of the seed, the gameplay boot flags, the save media, the disc files and the input frames.
 *   Presentation and debug settings never change the state hash.
 */
#ifndef MPT_MP4_GAME_ABI_H
#define MPT_MP4_GAME_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define MPT_MP4_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define MPT_MP4_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
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
  MPT_MP4_ERR_STATE = 5,       /* a save state from another build, layout or arena/module base */
  MPT_MP4_ERR_BUFFER = 6,      /* the caller's buffer is too small; the needed size is reported */
  MPT_MP4_ERR_BUSY = 7         /* a game already exists in this process */
} MptMp4Status;

typedef enum MptMp4StepResult {
  MPT_MP4_STEP_RUNNING = 0,
  MPT_MP4_STEP_HALTED = 1,          /* the game's main loop returned */
  MPT_MP4_STEP_RESET_REQUESTED = 2, /* reserved: OSResetSystem is a soft reset inside the module */
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

/* Gameplay boot flags: Party Board's options, which change the game's trajectory. They are
 * determinism inputs (ADR 0002 rule 2) and part of the state hash. In Phase 1 Mp4Runtime always
 * passes MPT_MP4_PHASE1_GAMEPLAY_FLAGS (none: the console's behaviour); they are never user settings.
 * Later the unlocks become ruleset rules recorded in the replay (ADR 0002 review). */
#define MPT_MP4_BOOT_SKIP_BOOT_SEQUENCE 0x1u          /* straight to mode select */
#define MPT_MP4_BOOT_UNLOCK_ALL_MINIGAMES 0x2u
#define MPT_MP4_BOOT_UNLOCK_BOWSERS_GNARLY_PARTY 0x4u
#define MPT_MP4_BOOT_GAMEPLAY_MASK 0xFFu
#define MPT_MP4_PHASE1_GAMEPLAY_FLAGS 0x0u
/* Debug flags: never part of the state hash. Trap CPU reads of GXCopyTex destinations (ADR 0007
 * section 5); a read faults the step. */
#define MPT_MP4_BOOT_DEBUG_COPY_TRAP 0x100u

/* video_backend, internal_scale, audio_output, cache_dir and arena_base are presentation or placement
 * settings and never change the state hash. Strings are copied during create. */
typedef struct MptMp4BootConfig {
  uint32_t struct_size;
  uint32_t abi_version;     /* MPT_MP4_ABI_VERSION */
  uint64_t seed;            /* RuntimeConfig::seed: derives the virtual clock's epoch */
  const char* disc_root;    /* UTF-8 path of the extracted disc (sys/, files/), VerifiedDump::build_dir */
  uint32_t flags;           /* MPT_MP4_BOOT_* */
  uint32_t video_backend;   /* MptMp4VideoBackend */
  uint32_t internal_scale;  /* render scale for the GPU backend, 1 = 640x480 */
  uint32_t audio_output;    /* 0 = null audio (everything game-visible still runs) */
  const uint8_t* save_media;  /* card image (SaveMedia::data) or NULL for a blank card; copied */
  size_t save_media_size;
  uint32_t save_media_format; /* SaveMedia::format_version; must match card_format() when given */
  uint32_t reserved0;
  uintptr_t arena_base;     /* fixed SDK arena address for fresh-process save states, 0 = anywhere */
  const char* cache_dir;    /* UTF-8 pipeline cache directory for the GPU backend, or NULL */
} MptMp4BootConfig;

/* Byte-for-byte the layout of mpt::InputFrame (core/include/mpt/core/input.h, 136 bytes). Stick value
 * -32768 is folded to -32767 by the module (ADR 0002 amendment 12). Frozen for major version 1. */
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

/* The last step's presented frame: width * height RGBA8 pixels, rows top to bottom, no padding.
 * pixels == NULL (width == height == 0) when no frame was presented (null backend). */
typedef struct MptMp4Video {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t reserved;
  const uint32_t* pixels;
} MptMp4Video;

/* The audio the last step produced: interleaved s16 stereo. The same on every call until the next step. */
typedef struct MptMp4Audio {
  uint32_t struct_size;
  uint32_t sample_rate;
  uint32_t channels; /* 2 */
  uint32_t reserved;
  const int16_t* samples;
  size_t frame_count; /* samples has frame_count * channels values */
} MptMp4Audio;

/* Raw party state for the adapter's mirroring, which maps it to content IDs and PartyPhase on the
 * host side. Values are the game's own: character is the game's character index (-1 = none), board is
 * GWSystem's board number, overlay is the running overlay's index in the game's overlay list. Which game
 * variables back the flags is documented in docs/titles/mp4.md (party reads). Frozen for major 1. */
typedef struct MptMp4PartyPlayer {
  int32_t character;
  int32_t is_cpu;
  int32_t coins;
  int32_t stars;
  int32_t space; /* current board space index, -1 when not on a board */
} MptMp4PartyPlayer;

#define MPT_MP4_PARTY_MODE 0x1u          /* party mode (not story or minigame modes) */
#define MPT_MP4_PARTY_IN_SETUP 0x2u      /* the party setup overlay is running */
#define MPT_MP4_PARTY_IN_PROGRESS 0x4u   /* a board is in the overlay history: its turns or minigames */
#define MPT_MP4_PARTY_TURNS_OVER 0x8u    /* the last turn has ended (end-of-party sequence) */
#define MPT_MP4_PARTY_RESULTS_FINAL 0x10u /* turns over and the board has exited: results are final */

typedef struct MptMp4Party {
  uint32_t struct_size;
  int32_t overlay;          /* running overlay index, -1 before the first */
  char overlay_name[32];    /* its name in the overlay list, e.g. "w01Dll" */
  uint32_t flags;           /* MPT_MP4_PARTY_* */
  int32_t board;
  int32_t turn;
  int32_t max_turns;
  MptMp4PartyPlayer players[4];
} MptMp4Party;

typedef void (*MptMp4ReportFn)(void* user, const char* text);

typedef struct MptMp4Api {
  uint32_t struct_size; /* set by the module */
  uint32_t abi_version;
  const char* build_id; /* the module's full build fingerprint (pins, patches, compiler); in save states */

  MptMp4Status (*create)(const MptMp4BootConfig* config, MptMp4Game** out, MptMp4Error* error);
  void (*destroy)(MptMp4Game* game);

  /* One VI field. input->frame must equal frame(game). OSResetSystem is a deterministic soft reset
   * inside the module: the game reboots on the same card, and frame numbers continue. */
  MptMp4StepResult (*step)(MptMp4Game* game, const MptMp4InputFrame* input);
  uint64_t (*frame)(const MptMp4Game* game); /* after load_state, the state's frame */
  const char* (*fault)(const MptMp4Game* game); /* "" unless faulted */
  void (*frame_rate)(const MptMp4Game* game, MptMp4FrameRate* out);

  /* ADR 0008 section 8: SDK, sound engine, game state and the gameplay boot flags, by value. */
  uint64_t (*state_hash)(const MptMp4Game* game);
  /* Writes the state to buffer (or only reports its size when buffer is NULL). Between steps only. The
   * blob carries the full build ID and its own layout version (state_format()); load_state rejects
   * any other. Valid for the same build and platform, at the same arena and module base (ADR 0008
   * sections 10-11). It holds the card image and its generation (ADR 0004 rule 9). */
  MptMp4Status (*save_state)(const MptMp4Game* game, uint8_t* buffer, size_t capacity, size_t* size);
  MptMp4Status (*load_state)(MptMp4Game* game, const uint8_t* data, size_t size, MptMp4Error* error);
  uint32_t (*state_format)(void); /* for SaveState::format_version */

  /* ADR 0004: the memory card. The generation counts committed writes. */
  uint64_t (*save_media_generation)(const MptMp4Game* game);
  MptMp4Status (*save_media)(const MptMp4Game* game, uint8_t* buffer, size_t capacity, size_t* size,
                             uint32_t* format);
  uint32_t (*card_format)(void);

  /* Presentation only, never game input. */
  void (*video)(const MptMp4Game* game, MptMp4Video* out);
  void (*audio)(const MptMp4Game* game, MptMp4Audio* out);
  void (*rumble)(const MptMp4Game* game, uint32_t out[4]);
  void (*set_report)(MptMp4Game* game, MptMp4ReportFn fn, void* user); /* OSReport text; host-side */

  /* Cheap and pure: never changes game state (ADR 0002 rule 9). */
  void (*party)(const MptMp4Game* game, MptMp4Party* out);
} MptMp4Api;

/* v1.0 sizes (x86-64). A caller's struct_size must be at least these. */
#define MPT_MP4_ERROR_SIZE_V1_0 260u
#define MPT_MP4_BOOT_CONFIG_SIZE_V1_0 80u
#define MPT_MP4_VIDEO_SIZE_V1_0 24u
#define MPT_MP4_AUDIO_SIZE_V1_0 32u
#define MPT_MP4_PARTY_SIZE_V1_0 136u
#define MPT_MP4_API_SIZE_V1_0 160u

MPT_MP4_STATIC_ASSERT(sizeof(void*) == 8, "the MP4 game module ABI is defined for 64-bit hosts");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Error) == MPT_MP4_ERROR_SIZE_V1_0, "MptMp4Error v1.0");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4BootConfig) == MPT_MP4_BOOT_CONFIG_SIZE_V1_0, "MptMp4BootConfig v1.0");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Video) == MPT_MP4_VIDEO_SIZE_V1_0, "MptMp4Video v1.0");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Audio) == MPT_MP4_AUDIO_SIZE_V1_0, "MptMp4Audio v1.0");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Party) == MPT_MP4_PARTY_SIZE_V1_0, "MptMp4Party v1.0");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Api) == MPT_MP4_API_SIZE_V1_0, "MptMp4Api v1.0");
/* Frozen for major version 1. */
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4Pad) == 32, "MptMp4Pad is frozen");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4InputFrame) == 136, "MptMp4InputFrame is frozen");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4FrameRate) == 8, "MptMp4FrameRate is frozen");
MPT_MP4_STATIC_ASSERT(sizeof(MptMp4PartyPlayer) == 20, "MptMp4PartyPlayer is frozen");

/* The module's only export. Returns NULL if it does not implement `abi_version`. */
MPT_MP4_EXPORT const MptMp4Api* mpt_mp4_get_api(uint32_t abi_version);
typedef const MptMp4Api* (*MptMp4GetApiFn)(uint32_t abi_version);

#ifdef __cplusplus
}
#endif

#endif /* MPT_MP4_GAME_ABI_H */
