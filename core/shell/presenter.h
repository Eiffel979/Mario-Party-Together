#pragma once

// Presents runtime frames on the shell's SDL_Renderer (P1-06 c, ADR 0002 rules 7-8,
// ADR 0010 §2). Draws the game frame letterboxed, then the overlay pass, then presents.

#include <cstdint>

#include <SDL3/SDL_render.h>

#include "letterbox.h"
#include "mpt/core/media.h"
#include "overlay.h"

namespace mpt::shell {

class Presenter {
 public:
  explicit Presenter(SDL_Renderer* renderer) : renderer_(renderer) {}
  ~Presenter();
  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;

  // Uploads a cpu_rgba8 frame (bytes R,G,B,A, stride = width). Other kinds keep the last
  // uploaded frame: Phase 1 runtimes always hand CPU pixels (ADR 0002 rule 8).
  void upload(const FrameOutput& frame);

  // Clears, draws the last uploaded frame letterboxed (16:9 when `widescreen`), runs the
  // overlay's render pass if any, and presents.
  void present(bool widescreen, IOverlay* overlay);

  // Forgets the frame (session ended): present() then draws only the background and UI.
  void clear_frame();

 private:
  SDL_Renderer* renderer_;
  SDL_Texture* texture_ = nullptr;
  std::uint32_t tex_w_ = 0;
  std::uint32_t tex_h_ = 0;
  bool has_frame_ = false;
};

}  // namespace mpt::shell
