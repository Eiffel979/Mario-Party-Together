#pragma once

// RmlUi render interface on the shell's SDL_Renderer (ADR 0010 §2.3-2.4).
//
// Adapted from RmlUi's RmlUi_Renderer_SDL.cpp (RmlUi 6.2, MIT License,
// Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors,
// Copyright (c) 2019-2026 The RmlUi Team, and contributors). See ui/README.md for the
// full notice.
//
// It implements geometry, generated textures and scissor clipping only. Clip masks,
// layers, filters, shaders and transforms are not implemented, so ui/ RCSS must not
// use them (ADR 0010 §2.4). Image files are not loaded: Phase 1 has no image assets.

#include <cstdint>
#include <vector>

#include <RmlUi/Core/RenderInterface.h>
#include <SDL3/SDL_render.h>

namespace mpt::ui {

class SdlRenderInterface final : public Rml::RenderInterface {
 public:
  explicit SdlRenderInterface(SDL_Renderer* renderer) : renderer_(renderer) {}

  // Around one RmlUi render pass: saves and restores the renderer state the UI changes
  // (draw blend mode, clip rect), so the shell's presenter is unaffected.
  void begin_frame();
  void end_frame();

  Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                              Rml::Span<const int> indices) override;
  void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                      Rml::TextureHandle texture) override;
  void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

  Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
  void ReleaseTexture(Rml::TextureHandle texture) override;

  void EnableScissorRegion(bool enable) override;
  void SetScissorRegion(Rml::Rectanglei region) override;

  std::uint64_t draw_calls() const { return draw_calls_; }  // for the bring-up test

 private:
  struct Geometry {
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
  };

  SDL_Renderer* renderer_;
  std::vector<SDL_Vertex> scratch_;
  SDL_Rect scissor_{};
  bool scissor_enabled_ = false;
  SDL_BlendMode saved_blend_ = SDL_BLENDMODE_NONE;
  bool saved_clip_enabled_ = false;
  SDL_Rect saved_clip_{};
  std::uint64_t draw_calls_ = 0;
};

}  // namespace mpt::ui
