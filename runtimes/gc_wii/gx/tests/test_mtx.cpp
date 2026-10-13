// MTX golden values against aurora's MTX at the pin with patches 2 (ADR 0008 section 5 condition 2,
// and its amendment after P1-13 section 4: aurora's MTX is the only PS*/C_* implementation).
//
// Each group runs a deterministic set of inputs, including the hard cases for the PSVEC reciprocal
// square root (bit-exact frsqrte table plus explicit fmaf): values next to powers of two, a dense
// sweep around 1, subnormals, signed zeros, huge values, infinities and NaN. The outputs' bit
// patterns are hashed per group. The same hashes must come out on every platform and compiler.
//
// To re-record after an intended change (a pin bump or a patch to MTX), run with
// MPT_GX_MTX_PRINT=1 and paste the printed table into kGolden.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <dolphin/mtx.h>

#include <xxhash.h>

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Bits {
  std::vector<uint32_t> v;
  void add(float f) { v.push_back(std::bit_cast<uint32_t>(f)); }
  void add(const Vec& x) {
    add(x.x);
    add(x.y);
    add(x.z);
  }
  void add(const Quaternion& q) {
    add(q.x);
    add(q.y);
    add(q.z);
    add(q.w);
  }
  void add(const Mtx m) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) add(m[r][c]);
  }
  void add44(const Mtx44 m) {
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c) add(m[r][c]);
  }
  uint64_t hash() const { return XXH3_64bits(v.data(), v.size() * sizeof(uint32_t)); }
};

float f_from_bits(uint32_t b) { return std::bit_cast<float>(b); }

// Hard inputs for reciprocal square roots and magnitudes.
std::vector<float> hard_scalars() {
  std::vector<float> out;
  // Around powers of two, both directions, including the frsqrte table's even/odd exponent split.
  for (int e = -126; e <= 127; e += 7) {
    const float p = std::ldexp(1.0f, e);
    out.push_back(p);
    out.push_back(std::nextafter(p, 0.0f));
    out.push_back(std::nextafter(p, std::numeric_limits<float>::infinity()));
  }
  // Dense sweep of mantissas in [1, 4): covers every frsqrte table entry several times.
  for (uint32_t m = 0; m < (1u << 23); m += 4099) {
    out.push_back(f_from_bits(0x3F800000u | m));
    out.push_back(f_from_bits(0x40000000u | m));
  }
  // Subnormals, zeros, extremes, specials.
  for (uint32_t b : {0x00000001u, 0x00000002u, 0x0000FFFFu, 0x007FFFFFu, 0x00800000u, 0x80000000u, 0x00000000u,
                     0x7F7FFFFFu, 0x5F800000u, 0x1F800000u, 0x7F800000u, 0x7FC00000u, 0xBF800000u}) {
    out.push_back(f_from_bits(b));
  }
  return out;
}

std::vector<Vec> hard_vectors() {
  std::vector<Vec> out;
  const auto s = hard_scalars();
  for (size_t i = 0; i < s.size(); ++i) {
    const float a = s[i];
    const float b = s[(i * 7 + 3) % s.size()];
    const float c = s[(i * 13 + 5) % s.size()];
    out.push_back({a, 0.0f, 0.0f});
    out.push_back({a, b, -c});
    out.push_back({-a, b * 0.5f, c * 0.25f});
  }
  out.push_back({3.0f, 4.0f, 0.0f});
  out.push_back({-0.0f, 0.0f, -0.0f});
  out.push_back({1e19f, 1e19f, 1e19f});  // squares overflow
  return out;
}

uint64_t group_vec_rsqrt() {
  Bits b;
  for (const Vec& v : hard_vectors()) {
    Vec n{};
    PSVECNormalize(&v, &n);
    b.add(n);
    b.add(PSVECMag(&v));
    b.add(PSVECSquareMag(&v));
  }
  return b.hash();
}

uint64_t group_vec_distance() {
  Bits b;
  const auto vs = hard_vectors();
  for (size_t i = 0; i + 1 < vs.size(); ++i) {
    b.add(PSVECDistance(&vs[i], &vs[i + 1]));
    b.add(PSVECSquareDistance(&vs[i], &vs[i + 1]));
  }
  return b.hash();
}

uint64_t group_vec_c() {
  Bits b;
  const auto vs = hard_vectors();
  for (size_t i = 0; i + 1 < vs.size(); ++i) {
    Vec r{};
    C_VECNormalize(&vs[i], &r);
    b.add(r);
    b.add(C_VECMag(&vs[i]));
    C_VECCrossProduct(&vs[i], &vs[i + 1], &r);
    b.add(r);
    b.add(C_VECDotProduct(&vs[i], &vs[i + 1]));
    C_VECHalfAngle(&vs[i], &vs[i + 1], &r);
    b.add(r);
    C_VECReflect(&vs[i], &vs[i + 1], &r);
    b.add(r);
  }
  return b.hash();
}

uint64_t group_mtx_trig() {
  Bits b;
  for (int i = -720; i <= 720; i += 7) {
    const float deg = static_cast<float>(i) * 0.5f;
    Mtx m;
    MTXRotDeg(m, 'x', deg);
    b.add(m);
    MTXRotRad(m, 'y', deg * 0.0174532925f);
    b.add(m);
    const Vec axis{0.267f, 0.534f, 0.801f};
    MTXRotAxisRad(m, &axis, deg * 0.0174532925f);
    b.add(m);
  }
  for (int i = 1; i < 179; i += 3) {
    Mtx44 p;
    MTXPerspective(p, static_cast<float>(i), 4.0f / 3.0f, 0.1f, 1000.0f);
    b.add44(p);
    Mtx l;
    MTXLightPerspective(l, static_cast<float>(i), 1.0f, 0.5f, -0.5f, 0.5f, 0.5f);
    b.add(l);
  }
  return b.hash();
}

uint64_t group_mtx_linear() {
  Bits b;
  Mtx a, c, inv, t;
  MTXIdentity(a);
  for (int i = 0; i < 64; ++i) {
    MTXRotDeg(t, 'z', static_cast<float>(i) * 11.25f);
    MTXConcat(t, a, c);
    MTXTrans(t, i * 0.5f, -i * 0.25f, i * 3.0f);
    MTXConcat(t, c, a);
    MTXScale(t, 1.0f + i * 0.01f, 1.0f, 0.5f + i * 0.02f);
    MTXConcat(a, t, c);
    b.add(c);
    b.add(static_cast<float>(MTXInverse(c, inv)));
    b.add(inv);
    MTXTranspose(c, t);
    b.add(t);
    const Vec cam{static_cast<float>(i), 10.0f, -5.0f}, up{0, 1, 0}, at{0, 0, 0};
    MTXLookAt(t, &cam, &up, &at);
    b.add(t);
    Vec v{1.0f, -2.0f, static_cast<float>(i)}, r{};
    MTXMultVec(c, &v, &r);
    b.add(r);
  }
  Mtx44 o;
  MTXOrtho(o, 0.0f, 480.0f, 0.0f, 640.0f, 0.0f, 8000.0f);
  b.add44(o);
  MTXFrustum(o, 1.0f, -1.0f, -1.33f, 1.33f, 1.0f, 100.0f);
  b.add44(o);
  return b.hash();
}

uint64_t group_quat() {
  Bits b;
  for (int i = 0; i < 90; ++i) {
    const float t = static_cast<float>(i) / 89.0f;
    Quaternion p{0.1f, 0.2f, 0.3f, 0.927f}, q{-0.5f, 0.5f, 0.5f, 0.5f}, r{};
    C_QUATNormalize(&p, &p);
    C_QUATSlerp(&p, &q, &r, t);
    b.add(r);
    const Vec axis{0.0f, 1.0f, 0.0f};
    C_QUATRotAxisRad(&r, &axis, t * 6.2831853f);
    b.add(r);
    Mtx m;
    C_MTXQuat(m, &r);
    b.add(m);
    Quaternion back{};
    C_QUATMtx(&back, m);
    b.add(back);
    C_QUATLogN(&back, &r);
    b.add(r);
    C_QUATExp(&r, &back);
    b.add(back);
  }
  return b.hash();
}

struct Golden {
  const char* name;
  uint64_t (*run)();
  uint64_t expected;
};

// Recorded on the reference machine (Windows, clang-cl 22.1.3) at aurora 5143394 with patches 0002,
// 0003 and 0013 and the math redirect. Linux CI must give the same values.
Golden kGolden[] = {
    {"vec_rsqrt", group_vec_rsqrt, 0x9841105f6a2e641aull},
    {"vec_distance", group_vec_distance, 0x6631a78045d8f6fbull},
    {"vec_c", group_vec_c, 0xac7d59123c73486bull},
    {"mtx_trig", group_mtx_trig, 0x8b91adbf6bd4ef25ull},
    {"mtx_linear", group_mtx_linear, 0xa1f002add80d51cbull},
    {"quat", group_quat, 0x26779f06e8569519ull},
};

}  // namespace

TEST_CASE("aurora MTX matches its golden values") {
  const bool print = std::getenv("MPT_GX_MTX_PRINT") != nullptr;
  for (const Golden& g : kGolden) {
    const uint64_t got = g.run();
    if (print) {
      std::printf("    {\"%s\", group_%s, 0x%016llxull},\n", g.name, g.name, static_cast<unsigned long long>(got));
      continue;
    }
    INFO("group " << g.name);
    CHECK(got == g.expected);
  }
}

TEST_CASE("PSVEC reciprocal square root spot values") {
  // Plain cases with known exact answers.
  const Vec v{3.0f, 4.0f, 0.0f};
  CHECK(PSVECMag(&v) == 5.0f);
  CHECK(PSVECSquareMag(&v) == 25.0f);
  const Vec z{-0.0f, 0.0f, -0.0f};
  CHECK(PSVECMag(&z) == 0.0f);
}
