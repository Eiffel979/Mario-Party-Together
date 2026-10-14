// Adapted from RmlUi's RmlUi_Renderer_SDL.cpp (RmlUi 6.2, MIT License; notice in
// render_sdl.h and ui/README.md).

#include "render_sdl.h"

#include <cstdio>

namespace mpt::ui {

void SdlRenderInterface::begin_frame() {
  SDL_GetRenderDrawBlendMode(renderer_, &saved_blend_);
  saved_clip_enabled_ = SDL_RenderClipEnabled(renderer_);
  if (saved_clip_enabled_) SDL_GetRenderClipRect(renderer_, &saved_clip_);
  // RmlUi hands vertex colours and textures with premultiplied alpha.
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
  SDL_SetRenderClipRect(renderer_, nullptr);
  scissor_enabled_ = false;
}

void SdlRenderInterface::end_frame() {
  SDL_SetRenderClipRect(renderer_, saved_clip_enabled_ ? &saved_clip_ : nullptr);
  SDL_SetRenderDrawBlendMode(renderer_, saved_blend_);
}

Rml::CompiledGeometryHandle SdlRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                                Rml::Span<const int> indices) {
  auto* geometry = new Geometry;
  geometry->vertices.resize(vertices.size());
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Rml::Vertex& v = vertices[i];
    SDL_Vertex& out = geometry->vertices[i];
    out.position = {v.position.x, v.position.y};
    out.tex_coord = {v.tex_coord.x, v.tex_coord.y};
    out.color = {v.colour.red / 255.f, v.colour.green / 255.f, v.colour.blue / 255.f, v.colour.alpha / 255.f};
  }
  geometry->indices.assign(indices.begin(), indices.end());
  return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void SdlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                        Rml::TextureHandle texture) {
  const auto* geometry = reinterpret_cast<const Geometry*>(handle);
  scratch_ = geometry->vertices;
  for (SDL_Vertex& v : scratch_) {
    v.position.x += translation.x;
    v.position.y += translation.y;
  }
  SDL_RenderGeometry(renderer_, reinterpret_cast<SDL_Texture*>(texture), scratch_.data(),
                     static_cast<int>(scratch_.size()), geometry->indices.data(),
                     static_cast<int>(geometry->indices.size()));
  ++draw_calls_;
}

void SdlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
  delete reinterpret_cast<Geometry*>(handle);
}

Rml::TextureHandle SdlRenderInterface::LoadTexture(Rml::Vector2i& /*texture_dimensions*/, const Rml::String& source) {
  // ADR 0010 Review 4: no image assets in Phase 1.
  std::fprintf(stderr, "ui: image files are not supported (%s)\n", source.c_str());
  return {};
}

Rml::TextureHandle SdlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                       Rml::Vector2i dimensions) {
  if (dimensions.x <= 0 || dimensions.y <= 0 ||
      source.size() != static_cast<std::size_t>(dimensions.x) * static_cast<std::size_t>(dimensions.y) * 4) {
    return {};
  }
  SDL_Texture* texture =
      SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, dimensions.x, dimensions.y);
  if (!texture) return {};
  SDL_UpdateTexture(texture, nullptr, source.data(), dimensions.x * 4);
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
  return reinterpret_cast<Rml::TextureHandle>(texture);
}

void SdlRenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
  SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(texture));
}

void SdlRenderInterface::EnableScissorRegion(bool enable) {
  scissor_enabled_ = enable;
  SDL_SetRenderClipRect(renderer_, enable ? &scissor_ : nullptr);
}

void SdlRenderInterface::SetScissorRegion(Rml::Rectanglei region) {
  scissor_ = SDL_Rect{region.Left(), region.Top(), region.Width(), region.Height()};
  if (scissor_enabled_) SDL_SetRenderClipRect(renderer_, &scissor_);
}

}  // namespace mpt::ui
