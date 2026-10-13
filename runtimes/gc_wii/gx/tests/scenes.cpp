// Synthetic GX scenes for the GX layer's tests (P1-14, ADR 0007 section 6). Every byte here is
// generated in code; nothing comes from a game or a dump.
// All memory the GX front end can see (texture objects, texel data, TLUTs, display lists, copy destinations)
// lives in one arena at a fixed virtual address, so the FIFO stream (which carries host pointers, S1 D10) is
// identical between the null and real executables.
#include "scenes.hpp"

#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr uintptr_t kArenaBase = 0x200000000ull;
constexpr size_t kArenaSize = 32u << 20;
uint8_t* g_arena = nullptr;
size_t g_arenaUsed = 0;

void* arena_alloc(size_t size, size_t align = 32) {
  g_arenaUsed = (g_arenaUsed + align - 1) & ~(align - 1);
  void* p = g_arena + g_arenaUsed;
  g_arenaUsed += size;
  if (g_arenaUsed > kArenaSize) {
    std::fprintf(stderr, "arena exhausted\n");
    std::abort();
  }
  return p;
}
template <typename T> T* arena_new() { return static_cast<T*>(arena_alloc(sizeof(T))); }

// ---- GameCube texture tiling (big-endian), from a linear generator ----
using Gen = uint32_t (*)(int x, int y); // returns 0xRRGGBBAA, or an index / intensity in the low byte

void* tile_8bit(int w, int h, Gen g) { // I8, CI8, Z8: 8x4 blocks
  auto* out = static_cast<uint8_t*>(arena_alloc(w * h));
  size_t o = 0;
  for (int by = 0; by < h; by += 4)
    for (int bx = 0; bx < w; bx += 8)
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) out[o++] = static_cast<uint8_t>(g(bx + x, by + y) & 0xFF);
  return out;
}
void* tile_rgba8(int w, int h, Gen g) { // 4x4 blocks, AR plane then GB plane
  auto* out = static_cast<uint8_t*>(arena_alloc(w * h * 4));
  size_t o = 0;
  for (int by = 0; by < h; by += 4)
    for (int bx = 0; bx < w; bx += 4) {
      uint32_t c[16];
      for (int i = 0; i < 16; ++i) c[i] = g(bx + (i & 3), by + (i >> 2));
      for (int i = 0; i < 16; ++i) { out[o++] = c[i] & 0xFF; out[o++] = (c[i] >> 24) & 0xFF; }
      for (int i = 0; i < 16; ++i) { out[o++] = (c[i] >> 16) & 0xFF; out[o++] = (c[i] >> 8) & 0xFF; }
    }
  return out;
}
void* tile_16bit(int w, int h, uint32_t (*g)(int x, int y)) { // Z16, IA8, RGB565: 4x4 blocks, big-endian
  auto* out = static_cast<uint8_t*>(arena_alloc(w * h * 2));
  size_t o = 0;
  for (int by = 0; by < h; by += 4)
    for (int bx = 0; bx < w; bx += 4)
      for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
          const uint32_t v = g(bx + x, by + y);
          out[o++] = static_cast<uint8_t>(v >> 8);
          out[o++] = static_cast<uint8_t>(v);
        }
  return out;
}
uint16_t rgb565(uint32_t rgba) {
  const uint32_t r = (rgba >> 24) & 0xFF, gg = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF;
  return static_cast<uint16_t>(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3));
}

// ---- common state ----
void base_state(bool ortho = true) {
  Mtx44 p;
  if (ortho) {
    MTXOrtho(p, 0.f, 480.f, 0.f, 640.f, 0.f, 10.f);
    GXSetProjection(p, GX_ORTHOGRAPHIC);
  } else {
    MTXPerspective(p, 60.f, 640.f / 480.f, 1.f, 100.f);
    GXSetProjection(p, GX_PERSPECTIVE);
  }
  Mtx m;
  MTXIdentity(m);
  GXLoadPosMtxImm(m, GX_PNMTX0);
  GXLoadNrmMtxImm(m, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);
  GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
  GXSetScissor(0, 0, 640, 480);
  GXSetCullMode(GX_CULL_NONE);
  GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetFog(GX_FOG_NONE, 0.f, 0.f, 0.f, 0.f, GXColor{0, 0, 0, 0});
  GXSetNumIndStages(0);
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
  for (int s = 0; s < 4; ++s) {
    GXSetTevDirect(static_cast<GXTevStageID>(s));
    GXSetTevSwapMode(static_cast<GXTevStageID>(s), GX_TEV_SWAP0, GX_TEV_SWAP0);
  }
  GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(0);
  GXSetNumTevStages(1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

void quad_c(float x0, float y0, float x1, float y1, float z, GXColor c0, GXColor c1, GXColor c2, GXColor c3) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(x0, y0, z); GXColor4u8(c0.r, c0.g, c0.b, c0.a);
  GXPosition3f32(x1, y0, z); GXColor4u8(c1.r, c1.g, c1.b, c1.a);
  GXPosition3f32(x1, y1, z); GXColor4u8(c2.r, c2.g, c2.b, c2.a);
  GXPosition3f32(x0, y1, z); GXColor4u8(c3.r, c3.g, c3.b, c3.a);
  GXEnd();
}
void quad(float x0, float y0, float x1, float y1, GXColor c, float z = -1.f) { quad_c(x0, y0, x1, y1, z, c, c, c, c); }

// Textured quad: POS + TEX0 (+ CLR0 white).
void tex_vtx_fmt() {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}
void quad_t(float x0, float y0, float x1, float y1, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1,
            float z = -1.f) {
  GXBegin(GX_QUADS, GX_VTXFMT1, 4);
  GXPosition3f32(x0, y0, z); GXColor4u8(255, 255, 255, 255); GXTexCoord2f32(s0, t0);
  GXPosition3f32(x1, y0, z); GXColor4u8(255, 255, 255, 255); GXTexCoord2f32(s1, t0);
  GXPosition3f32(x1, y1, z); GXColor4u8(255, 255, 255, 255); GXTexCoord2f32(s1, t1);
  GXPosition3f32(x0, y1, z); GXColor4u8(255, 255, 255, 255); GXTexCoord2f32(s0, t1);
  GXEnd();
}
void tex_stage0(GXTexObj* obj, GXTexMapID map = GX_TEXMAP0) {
  GXLoadTexObj(obj, map);
  GXSetNumTexGens(1);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, map, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
}

constexpr GXColor kRed{255, 0, 0, 255}, kGreen{0, 255, 0, 255}, kBlue{0, 0, 255, 255}, kWhite{255, 255, 255, 255},
    kYellow{255, 255, 0, 255}, kGrey{128, 128, 128, 255};

// ================= scenes =================

void scene_clear_and_quads() {
  GXSetCopyClear(GXColor{0, 0, 100, 255}, GX_MAX_Z24);
  base_state();
  quad(40, 40, 200, 200, kRed);
  quad(240, 40, 400, 200, kGreen);
  quad(440, 40, 600, 200, kBlue);
  quad_c(40, 260, 600, 440, -1.f, kRed, kGreen, kBlue, kWhite); // gouraud gradient
}

void scene_tev_swap_konst_s10() {
  GXSetCopyClear(GXColor{20, 20, 20, 255}, GX_MAX_Z24);
  base_state();
  // 1) Konst colour: stage output = KONST (k0 = orange).
  GXSetTevKColor(GX_KCOLOR0, GXColor{255, 128, 0, 255});
  GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  quad(40, 40, 300, 220, kWhite);
  // 2) Swap table: rasterised colour with R and B swapped (red vertex colour -> blue output).
  GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_BLUE, GX_CH_GREEN, GX_CH_RED, GX_CH_ALPHA);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  quad(340, 40, 600, 220, kRed);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  // 3) S10 colour register: C0 = (-128, 300, 64). out = clamp(RASC + C0) over a grey ramp.
  GXSetTevColorS10(GX_TEVREG0, GXColorS10{-128, 300, 64, 255});
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  // d = C0 alone first (left), then RASC + C0 via a=RASC,d=C0 with b=c=0 -> a*(1-c)+b*c+d = RASC + C0 (right).
  quad(40, 260, 300, 440, kGrey);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_RASC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
  quad_c(340, 260, 600, 440, -1.f, GXColor{0, 0, 0, 255}, kWhite, kWhite, GXColor{0, 0, 0, 255});
}

void scene_fog() {
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state(false);
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetFog(GX_FOG_PERSP_LIN, 5.f, 40.f, 1.f, 100.f, GXColor{200, 200, 255, 255});
  // Five equal-size red quads at increasing depth: should fade to the fog colour.
  // Five red quads, each covering the same screen area at increasing depth: they fade to the fog colour.
  for (int i = 0; i < 5; ++i) {
    const float z = -(4.f + 9.f * i);
    const float k = -z * 0.57735f; // tan(30 deg): half-height of the view at depth z
    const float cx = (-0.8f + 0.4f * i) * k * (640.f / 480.f);
    quad(cx - 0.12f * k, -0.2f * k, cx + 0.12f * k, 0.2f * k, kRed, z);
  }
}

void scene_lights() {
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state(false);
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT2, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT2, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
  auto* spot = arena_new<GXLightObj>();
  GXInitLightPos(spot, -3.f, 0.f, -6.f);
  GXInitLightDir(spot, 0.f, 0.f, -1.f);
  GXInitLightColor(spot, GXColor{255, 255, 200, 255});
  GXInitLightSpot(spot, 30.f, GX_SP_COS);
  GXInitLightDistAttn(spot, 5.f, 0.6f, GX_DA_MEDIUM);
  GXLoadLightObjImm(spot, GX_LIGHT0);
  auto* spec = arena_new<GXLightObj>();
  GXInitSpecularDir(spec, -0.3f, 0.3f, -1.f);
  GXInitLightAttn(spec, 0.f, 0.f, 1.f, 16.f, 0.f, -15.f);
  GXInitLightColor(spec, GXColor{255, 255, 255, 255});
  GXLoadLightObjImm(spec, GX_LIGHT1);
  GXSetNumChans(1);
  GXSetChanAmbColor(GX_COLOR0A0, GXColor{20, 20, 40, 255});
  GXSetChanMatColor(GX_COLOR0A0, GXColor{255, 255, 255, 255});
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  // A tessellated bumpy grid, drawn twice: left with the spot light (diffuse), right with specular.
  auto grid = [](float cx) {
    const int n = 24;
    for (int j = 0; j < n; ++j) {
      GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT2, (n + 1) * 2);
      for (int i = 0; i <= n; ++i) {
        for (int k = 0; k < 2; ++k) {
          const float u = static_cast<float>(i) / n * 2.f - 1.f;
          const float v = static_cast<float>(j + k) / n * 2.f - 1.f;
          float nx = u * 0.6f, ny = v * 0.6f, nz = 1.f;
          const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
          GXPosition3f32(cx + u * 2.5f, v * 2.5f, -8.f);
          GXNormal3f32(nx / l, ny / l, nz / l);
        }
      }
      GXEnd();
    }
  };
  GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0, GX_DF_CLAMP, GX_AF_SPOT);
  grid(-3.f);
  GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT1, GX_DF_NONE, GX_AF_SPEC);
  grid(3.f);
}

void scene_ci8_tlut() {
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state();
  tex_vtx_fmt();
  // 32x32 CI8: four colour bands plus a checker of indices 4/5.
  void* texels = tile_8bit(32, 32, [](int x, int y) -> uint32_t {
    if (y < 16) return static_cast<uint32_t>(x / 8);
    return 4u + static_cast<uint32_t>(((x / 4) + (y / 4)) & 1);
  });
  auto* pal = static_cast<uint16_t*>(arena_alloc(256 * 2));
  const uint32_t colours[6] = {0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0xFFFF00FF, 0xFFFFFFFF, 0x000000FF};
  for (int i = 0; i < 256; ++i) {
    const uint16_t c = rgb565(colours[i < 6 ? i : 0]);
    pal[i] = static_cast<uint16_t>((c >> 8) | (c << 8)); // big-endian
  }
  auto* tlut = arena_new<GXTlutObj>();
  GXInitTlutObj(tlut, pal, GX_TL_RGB565, 256);
  GXLoadTlut(tlut, GX_TLUT0);
  auto* tex = arena_new<GXTexObj>();
  GXInitTexObjCI(tex, texels, 32, 32, GX_TF_C8, GX_REPEAT, GX_REPEAT, GX_FALSE, GX_TLUT0);
  GXInitTexObjLOD(tex, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  tex_stage0(tex);
  quad_t(64, 48, 576, 432, 0, 0, 2, 2);
}

uint8_t* g_dlBuf = nullptr;
uint32_t g_dlSize = 0;
void scene_display_list() {
  GXSetCopyClear(GXColor{30, 0, 30, 255}, GX_MAX_Z24);
  base_state();
  if (g_dlBuf == nullptr) {
    g_dlBuf = static_cast<uint8_t*>(arena_alloc(8192));
  }
  GXBeginDisplayList(g_dlBuf, 8192);
  quad(0, 0, 120, 120, kYellow);
  quad_c(20, 20, 100, 100, -1.f, kRed, kGreen, kBlue, kWhite);
  g_dlSize = GXEndDisplayList();
  for (int i = 0; i < 4; ++i) {
    Mtx m;
    MTXTrans(m, 40.f + 150.f * i, 60.f + 60.f * (i & 1), 0.f);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    GXCallDisplayList(g_dlBuf, g_dlSize);
  }
}

void* g_copyFull = nullptr;
void* g_copyPart = nullptr;
void scene_copy_tex() {
  GXSetCopyClear(GXColor{0, 60, 0, 255}, GX_MAX_Z24);
  base_state();
  // Draw a source pattern in the top half.
  quad(0, 0, 160, 120, kRed);
  quad(40, 30, 120, 90, kBlue);
  quad(320, 0, 400, 60, kYellow);
  quad(340, 10, 380, 50, kRed);
  // Full copy of (0,0,160,120) without clear.
  if (g_copyFull == nullptr) {
    g_copyFull = arena_alloc(GXGetTexBufferSize(160, 120, GX_TF_RGBA8, GX_FALSE, 0));
    g_copyPart = arena_alloc(GXGetTexBufferSize(80, 60, GX_TF_RGBA8, GX_FALSE, 0));
  }
  GXSetTexCopySrc(0, 0, 160, 120);
  GXSetTexCopyDst(160, 120, GX_TF_RGBA8, GX_FALSE);
  GXCopyTex(g_copyFull, GX_FALSE);
  // Partial-rectangle copy of (320,0,80,60) WITH clear: only that rectangle may be cleared.
  GXSetTexCopySrc(320, 0, 80, 60);
  GXSetTexCopyDst(80, 60, GX_TF_RGBA8, GX_FALSE);
  GXCopyTex(g_copyPart, GX_TRUE);
  GXPixModeSync();
  // Re-sample both copies as textures in the bottom half.
  tex_vtx_fmt();
  auto* t0 = arena_new<GXTexObj>();
  GXInitTexObj(t0, g_copyFull, 160, 120, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GXInitTexObjLOD(t0, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  tex_stage0(t0);
  quad_t(0, 240, 320, 480);
  auto* t1 = arena_new<GXTexObj>();
  GXInitTexObj(t1, g_copyPart, 80, 60, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GXInitTexObjLOD(t1, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  tex_stage0(t1);
  quad_t(320, 240, 640, 480);
}

void scene_indirect() {
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state();
  tex_vtx_fmt();
  // Base: 64x64 I8 checker. Indirect map: 32x32 RGBA8 sine offsets.
  void* base = tile_8bit(64, 64, [](int x, int y) -> uint32_t { return (((x / 8) + (y / 8)) & 1) ? 255u : 40u; });
  void* ind = tile_rgba8(32, 32, [](int x, int y) -> uint32_t {
    const auto s = static_cast<uint32_t>(128 + 100 * std::sin(y * 0.4));
    const auto t = static_cast<uint32_t>(128 + 100 * std::cos(x * 0.4));
    return (s << 24) | (t << 16) | (128u << 8) | s;
  });
  auto* tb = arena_new<GXTexObj>();
  GXInitTexObj(tb, base, 64, 64, GX_TF_I8, GX_REPEAT, GX_REPEAT, GX_FALSE);
  GXInitTexObjLOD(tb, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  auto* ti = arena_new<GXTexObj>();
  GXInitTexObj(ti, ind, 32, 32, GX_TF_RGBA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
  GXInitTexObjLOD(ti, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GXLoadTexObj(tb, GX_TEXMAP0);
  GXLoadTexObj(ti, GX_TEXMAP1);
  GXSetNumTexGens(2);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetNumIndStages(1);
  GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP1);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
  f32 im[2][3] = {{0.5f, 0.f, 0.f}, {0.f, 0.5f, 0.f}};
  GXSetIndTexMtx(GX_ITM_0, im, -2);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
  // Left: warp. Right: tile.
  GXSetTevIndWarp(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_TRUE, GX_FALSE, GX_ITM_0);
  quad_t(20, 60, 310, 420, 0, 0, 4, 4);
  GXSetTevIndTile(GX_TEVSTAGE0, GX_INDTEXSTAGE0, 16, 16, 16, 16, GX_ITF_8, GX_ITM_0, GX_ITB_NONE, GX_ITBA_OFF);
  quad_t(330, 60, 620, 420, 0, 0, 4, 4);
  GXSetTevDirect(GX_TEVSTAGE0);
  GXSetNumIndStages(0);
}

void scene_ztexture() {
  // Patch 7. A red quad far away, its depth replaced by a Z16 texture plus bias: left half
  // 0x7F8000 (in front of the green bar), right half 0x807FFF (behind it). Then a green bar at mid
  // depth with LEQUAL. Correct: green only on the right half. Without z textures: green everywhere.
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state();
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  tex_vtx_fmt();
  void* z16 = tile_16bit(64, 64, [](int x, int) -> uint32_t { return x < 32 ? 0x0000u : 0xFFFFu; });
  auto* tz = arena_new<GXTexObj>();
  GXInitTexObj(tz, z16, 64, 64, GX_TF_Z16, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GXInitTexObjLOD(tz, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GXLoadTexObj(tz, GX_TEXMAP0);
  GXSetNumTexGens(1);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevKColor(GX_KCOLOR0, kRed);
  GXSetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetZCompLoc(GX_FALSE);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z16, 0x7F8000);
  quad_t(64, 64, 576, 416, 0, 0, 1, 1, -9.9f);
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
  GXSetZCompLoc(GX_TRUE);
  base_state();
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  quad(32, 200, 608, 280, kGreen, -5.f);
}

void* g_depthCopy = nullptr;
void scene_depth_copy_ztexture() {
  // m419's pattern: copy EFB depth as Z24X8, then draw with GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8)
  // sampling that copy in the last TEV stage. Left half of the EFB holds a near quad, right half far.
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  base_state();
  GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
  quad(0, 0, 320, 480, kGrey, -2.f);   // near (depth 0.2)
  quad(320, 0, 640, 480, kGrey, -8.f); // far (depth 0.8)
  if (g_depthCopy == nullptr) {
    g_depthCopy = arena_alloc(GXGetTexBufferSize(640, 480, GX_TF_Z24X8, GX_FALSE, 0));
  }
  GXSetTexCopySrc(0, 0, 640, 480);
  GXSetTexCopyDst(640, 480, GX_TF_Z24X8, GX_FALSE);
  GXCopyTex(g_depthCopy, GX_TRUE);
  GXPixModeSync();
  // White full-screen quad whose depth comes from the copy; then a yellow bar at depth 0.5 with LEQUAL.
  // Correct: yellow only on the right half (where the copied depth is 0.8).
  base_state();
  GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
  tex_vtx_fmt();
  auto* tz = arena_new<GXTexObj>();
  GXInitTexObj(tz, g_depthCopy, 640, 480, GX_TF_Z24X8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GXInitTexObjLOD(tz, GX_NEAR, GX_NEAR, 0.f, 0.f, 0.f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GXLoadTexObj(tz, GX_TEXMAP1);
  GXSetNumTexGens(1);
  GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GXSetNumTevStages(2);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR_NULL);
  GXSetTevOp(GX_TEVSTAGE1, GX_PASSCLR);
  GXSetZCompLoc(GX_FALSE);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
  quad_t(0, 0, 640, 480, 0, 0, 1, 1, -1.f);
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
  GXSetZCompLoc(GX_TRUE);
  base_state();
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  quad(32, 200, 608, 280, kYellow, -5.f);
}
} // namespace

const std::vector<Scene>& scenes() {
  // Probe points (logical pixels) whose colours the golden-probe test checks on the real renderer.
  static const std::vector<Scene> s = {
      {"clear_and_quads", scene_clear_and_quads, {{120, 120}, {320, 120}, {520, 120}, {20, 20}, {320, 350}}},
      {"tev_swap_konst_s10", scene_tev_swap_konst_s10, {{170, 130}, {470, 130}, {170, 350}, {470, 350}, {20, 20}}},
      {"fog", scene_fog, {{64, 240}, {192, 240}, {320, 240}, {448, 240}, {576, 240}}},
      {"lights_spot_specular", scene_lights, {{164, 240}, {476, 240}, {100, 140}, {20, 20}}},
      {"ci8_tlut", scene_ci8_tlut, {{96, 80}, {160, 80}, {224, 80}, {288, 80}, {72, 152}, {88, 152}}},
      {"display_list", scene_display_list, {{45, 65}, {100, 120}, {295, 125}, {20, 300}}},
      {"copy_tex_partial", scene_copy_tex, {{80, 60}, {360, 30}, {330, 5}, {160, 360}, {480, 360}}},
      {"indirect_warp_tile", scene_indirect, {{100, 150}, {200, 300}, {400, 150}, {500, 300}}},
      {"ztexture", scene_ztexture, {{160, 240}, {480, 240}, {160, 100}}},
      {"depth_copy_ztexture", scene_depth_copy_ztexture, {{160, 240}, {480, 240}, {160, 100}}},
  };
  return s;
}

void arena_init() {
#ifdef _WIN32
  g_arena = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(kArenaBase), kArenaSize,
                                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
  void* p = mmap(reinterpret_cast<void*>(kArenaBase), kArenaSize, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  g_arena = p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
#endif
  if (g_arena != reinterpret_cast<uint8_t*>(kArenaBase)) {
    std::fprintf(stderr, "could not reserve the arena at its fixed address\n");
    std::abort();
  }
  g_arenaUsed = 0;
}
void arena_reset() {
  std::memset(g_arena, 0, kArenaSize);
  g_arenaUsed = 0;
  g_dlBuf = nullptr;
  g_copyFull = g_copyPart = nullptr;
  g_depthCopy = nullptr;
}
