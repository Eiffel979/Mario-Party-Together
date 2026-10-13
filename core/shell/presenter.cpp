#include "presenter.h"

namespace mpt::shell {

Presenter::~Presenter() {
  if (texture_) SDL_DestroyTexture(texture_);
}

void Presenter::upload(const FrameOutput& frame) {
  if (frame.kind != FrameOutput::Kind::cpu_rgba8 || frame.width == 0 || frame.height == 0) return;
  if (frame.pixels.size() < static_cast<std::size_t>(frame.width) * frame.height) return;  // malformed: keep the old frame

  if (!texture_ || tex_w_ != frame.width || tex_h_ != frame.height) {
    if (texture_) SDL_DestroyTexture(texture_);
    // SDL_PIXELFORMAT_RGBA32 is bytes R,G,B,A in memory on every host (ADR 0002 rule 7).
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                 static_cast<int>(frame.width), static_cast<int>(frame.height));
    if (!texture_) return;
    // Integer upscaling of a low-resolution frame looks best with nearest sampling when
    // internal_scale is 1; linear is fine once the runtime renders at a higher scale.
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    tex_w_ = frame.width;
    tex_h_ = frame.height;
  }
  SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.width * 4));
  has_frame_ = true;
}

void Presenter::present(bool widescreen, IOverlay* overlay) {
  int w = 0;
  int h = 0;
  SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
  SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
  SDL_RenderClear(renderer_);
  if (has_frame_ && texture_) {
    const PixelRect r = letterbox(w, h, display_aspect(tex_w_, tex_h_, widescreen));
    const SDL_FRect dst{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
                        static_cast<float>(r.h)};
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
  }
  if (overlay) overlay->render(renderer_, w, h);
  SDL_RenderPresent(renderer_);
}

void Presenter::clear_frame() { has_frame_ = false; }

}  // namespace mpt::shell
