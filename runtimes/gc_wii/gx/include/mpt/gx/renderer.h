#pragma once

// The GX renderer behind aurora's GX front end (ADR 0007 section 6).
//
// The front end (GX API, FIFO, command processor, MTX) is game code: it lives in the game module
// and its CPU-side state is game-visible. The renderer is presentation only. Exactly one renderer
// library is linked, chosen by MPT_GX_BACKEND:
//   - null: the command processor still parses every FIFO byte, but nothing is drawn and no GPU
//     or Dawn is involved. end_frame() returns no image.
//   - dawn: aurora's WebGPU renderer on a headless Dawn device (no window, no surface). end_frame()
//     reads the EFB back as cpu_rgba8 (ADR 0002 rules 7 and 8).
// Both give the same game-visible state; the null-against-real test checks it.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mpt::gx {

enum class GpuApi {
  automatic,  // Windows: D3D12, then Vulkan. Linux: Vulkan.
  d3d12,
  vulkan,
  d3d11,
  dawn_null,  // Dawn's Null adapter: full device and pipeline path, no rasterization.
};

struct RendererConfig {
  uint32_t logical_width = 640;  // the console framebuffer; vi::configured_fb_size reports it
  uint32_t logical_height = 480;
  uint32_t internal_scale = 1;   // the EFB is internal_scale x the logical size
  GpuApi api = GpuApi::automatic;
  // Accept a software adapter (Mesa lavapipe, SwiftShader). Off for players; CI machines without a
  // GPU turn it on. Dawn's Null adapter is always accepted when asked for by name.
  bool allow_software_adapter = false;
  // Pipeline and Dawn blob caches. User data (contents come from game TEV state): the shell passes
  // its user cache directory, tests a temporary one. Empty: no persistent cache.
  std::string cache_dir;
};

// One frame's picture: stride = width, bytes R, G, B, A.
struct FrameImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual void begin_frame() = 0;
  // Drains the FIFO through the command processor. The dawn renderer then renders the frame and,
  // if `out` is not null, reads the EFB back into it (synchronously).
  virtual void end_frame(FrameImage* out) = 0;
  // Before loadState: drops renderer caches keyed by object IDs and copy destinations.
  virtual void reset_for_load_state() = 0;
  virtual std::string_view description() const = 0;
};

// Implemented by the linked renderer library. Only one renderer may exist at a time.
std::unique_ptr<Renderer> create_renderer(const RendererConfig& config, std::string& error);
bool renderer_draws();  // false for the null renderer

// Save-state access to the front end's texture/TLUT object ID counters (game state, never hashed).
struct ObjectIdCounters {
  uint32_t next_tex_obj_id;
  uint32_t next_tlut_obj_id;
};
ObjectIdCounters object_id_counters();
void set_object_id_counters(const ObjectIdCounters& counters);

// The front end's game-visible state outside aurora's GX state: the draw-sync callback the game
// registered (MP4's performance meter reads the clock in it) and the last draw-sync token. It changes
// what the game does, so a title saves it with its state (an SdkConfig state region), hashes it and
// clears it at boot: a runtime that loads a state never ran the game's init that registered the
// callback (P1-17, conformance save/load). Plain data at a fixed address in the module.
std::span<std::byte> game_visible_state();
void reset_game_visible_state();

// Debug hook for the copy-destination trap (ADR 0007 section 5): called by GXCopyTex with each
// destination and its CPU-side size in bytes. nullptr (the default) turns it off.
using CopyTexObserver = void (*)(void* dest, uint32_t size);
void set_copy_tex_observer(CopyTexObserver observer);

}  // namespace mpt::gx
