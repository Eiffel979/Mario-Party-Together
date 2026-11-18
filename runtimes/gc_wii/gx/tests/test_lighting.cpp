// GXInitLightSpot against the Dolphin SDK's spot functions (aurora patch 0018). Synthetic values only.
//
// The spot attenuation is a0 + a1 * c + a2 * c^2, where c is the cosine between the light's direction
// and the direction to the vertex. The SDK picks the coefficients so that every function but RING2
// (which is 1 there) is 0 at the cutoff angle, and COS, COS2 and SHARP are 1 on the axis. aurora's
// SHARP lacked the division of a0 by (1 - cr)^2, which lit a sharp spot far outside its cutoff
// (Right Oar Left's lamp on the water), and its RING1 had the sign of a0 flipped.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <dolphin/gx.h>

#include <cmath>

extern "C" void GXGetLightAttnA(GXLightObj* light, float* a0, float* a1, float* a2);

namespace {
struct Spot {
  float a0, a1, a2;
  float at(float c) const { return a0 + a1 * c + a2 * c * c; }
  // Float rounding grows with the coefficients (about 1/(1 - cr)^2 for a narrow spot): checks scale with them.
  float tolerance() const { return 1e-5f * (std::fabs(a0) + std::fabs(a1) + std::fabs(a2)); }
};

Spot spot(float cutoff, GXSpotFn fn) {
  GXLightObj light{};
  GXInitLightSpot(&light, cutoff, fn);
  Spot s{};
  GXGetLightAttnA(&light, &s.a0, &s.a1, &s.a2);
  return s;
}

float cos_deg(float deg) { return static_cast<float>(std::cos(deg * 3.14159265358979 / 180.0)); }
}  // namespace

TEST_CASE("every spot function but RING2 is 0 at the cutoff") {
  for (const float cutoff : {10.f, 20.f, 37.5f, 60.f, 85.f}) {
    const float cr = cos_deg(cutoff);
    for (const GXSpotFn fn : {GX_SP_FLAT, GX_SP_COS, GX_SP_COS2, GX_SP_SHARP, GX_SP_RING1}) {
      CAPTURE(cutoff);
      CAPTURE(static_cast<int>(fn));
      const Spot s = spot(cutoff, fn);
      CHECK(std::fabs(s.at(cr)) <= s.tolerance());
    }
  }
}

TEST_CASE("COS, COS2 and SHARP are 1 on the axis; RING1 is 0 there and peaks between") {
  for (const float cutoff : {10.f, 20.f, 37.5f, 60.f, 85.f}) {
    CAPTURE(cutoff);
    const float cr = cos_deg(cutoff);
    for (const GXSpotFn fn : {GX_SP_COS, GX_SP_COS2, GX_SP_SHARP}) {
      CAPTURE(static_cast<int>(fn));
      const Spot s = spot(cutoff, fn);
      CHECK(std::fabs(s.at(1.f) - 1.f) <= s.tolerance());
    }
    const Spot ring1 = spot(cutoff, GX_SP_RING1);
    CHECK(std::fabs(ring1.at(1.f)) <= ring1.tolerance());
    CHECK(std::fabs(ring1.at(0.5f * (1.f + cr)) - 1.f) <= ring1.tolerance());
  }
}

TEST_CASE("a sharp spot is dark outside its cutoff") {
  // 37.5 degrees, as a boat's lamp uses: at 60 degrees off the axis the light must be off (<= 0).
  const Spot s = spot(37.5f, GX_SP_SHARP);
  CHECK(s.at(cos_deg(60.f)) <= 0.f);
  CHECK(s.at(cos_deg(20.f)) > 0.f);
  CHECK(s.at(cos_deg(20.f)) < 1.f);
}

TEST_CASE("the SDK's SHARP and RING1 coefficients") {
  const float cr = cos_deg(37.5f);
  const float d = (1.f - cr) * (1.f - cr);
  const Spot sharp = spot(37.5f, GX_SP_SHARP);
  CHECK(sharp.a0 == doctest::Approx(cr * (cr - 2.f) / d).epsilon(1e-4));
  CHECK(sharp.a1 == doctest::Approx(2.f / d).epsilon(1e-4));
  CHECK(sharp.a2 == doctest::Approx(-1.f / d).epsilon(1e-4));
  const Spot ring1 = spot(37.5f, GX_SP_RING1);
  CHECK(ring1.a0 == doctest::Approx(-4.f * cr / d).epsilon(1e-4));
  CHECK(ring1.a1 == doctest::Approx(4.f * (1.f + cr) / d).epsilon(1e-4));
  CHECK(ring1.a2 == doctest::Approx(-4.f / d).epsilon(1e-4));
}
