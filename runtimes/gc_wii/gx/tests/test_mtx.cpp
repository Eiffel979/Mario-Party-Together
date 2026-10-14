// MTX golden values against aurora's MTX at the pin with patches 2 (ADR 0008 section 5 condition 2,
// and its amendment after P1-13 section 4: aurora's MTX is the only PS*/C_* implementation).
//
// Each group runs a deterministic set of inputs, including the hard cases for the PSVEC reciprocal
// square root (bit-exact frsqrte table plus explicit fmaf): values next to powers of two, a dense
// sweep around 1, subnormals, signed zeros, huge values, infinities and NaN. The outputs' bit
// patterns are hashed per group, and per chunk of 256 values. The same hashes must come out on every
// platform and compiler. On a mismatch the test names the diverging chunks and prints their inputs
// and outputs; MPT_GX_MTX_DUMP=<file> writes every group's raw values for diffing between machines.
//
// NaN results are hashed as one canonical quiet NaN. IEEE 754 leaves the sign and payload of a NaN
// result unspecified, and x86 returns the NaN of whichever operand the compiler put first, so they
// change with the compiler version and optimisation level (on the reference machine, -Od and -O2
// already disagree on 38 NaN signs in vec_c). Whether a result is NaN is still checked.
//
// The golden values are in mtx_golden.txt. To re-record after an intended change (a pin bump or a
// patch to MTX), run with MPT_GX_MTX_PRINT=1 and replace that file with the output.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <dolphin/mtx.h>

#include <xxhash.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Bits {
  static constexpr size_t kChunk = 256;
  std::vector<uint32_t> v;
  std::vector<uint32_t> value_case;  // index into labels, per value
  std::vector<std::string> labels;   // one per case: what went in
  void begin_case(std::string label) { labels.push_back(std::move(label)); }
  void add(float f) {
    uint32_t bits = std::bit_cast<uint32_t>(f);
    if (std::isnan(f)) bits = 0x7FC00000u;  // see the header comment
    v.push_back(bits);
    value_case.push_back(labels.empty() ? 0u : static_cast<uint32_t>(labels.size() - 1));
  }
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
  std::vector<uint64_t> chunk_hashes() const {
    std::vector<uint64_t> out;
    for (size_t i = 0; i < v.size(); i += kChunk) {
      out.push_back(XXH3_64bits(v.data() + i, std::min(kChunk, v.size() - i) * sizeof(uint32_t)));
    }
    return out;
  }
};

std::string hex(float f) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%08x", std::bit_cast<uint32_t>(f));
  return buf;
}
std::string hex(const Vec& v) { return "(" + hex(v.x) + " " + hex(v.y) + " " + hex(v.z) + ")"; }

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

Bits group_vec_rsqrt() {
  Bits b;
  for (const Vec& v : hard_vectors()) {
    b.begin_case("v=" + hex(v) + " -> PSVECNormalize xyz, PSVECMag, PSVECSquareMag");
    Vec n{};
    PSVECNormalize(&v, &n);
    b.add(n);
    b.add(PSVECMag(&v));
    b.add(PSVECSquareMag(&v));
  }
  return b;
}

Bits group_vec_distance() {
  Bits b;
  const auto vs = hard_vectors();
  for (size_t i = 0; i + 1 < vs.size(); ++i) {
    b.begin_case("a=" + hex(vs[i]) + " b=" + hex(vs[i + 1]) + " -> PSVECDistance, PSVECSquareDistance");
    b.add(PSVECDistance(&vs[i], &vs[i + 1]));
    b.add(PSVECSquareDistance(&vs[i], &vs[i + 1]));
  }
  return b;
}

Bits group_vec_c() {
  Bits b;
  const auto vs = hard_vectors();
  for (size_t i = 0; i + 1 < vs.size(); ++i) {
    b.begin_case("a=" + hex(vs[i]) + " b=" + hex(vs[i + 1]) +
                 " -> Normalize(a) xyz, Mag(a), Cross xyz, Dot, HalfAngle xyz, Reflect xyz");
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
  return b;
}

Bits group_mtx_trig() {
  Bits b;
  for (int i = -720; i <= 720; i += 7) {
    const float deg = static_cast<float>(i) * 0.5f;
    b.begin_case("deg=" + hex(deg) + " -> RotDeg x, RotRad y, RotAxisRad (12 each)");
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
    b.begin_case("fovy=" + std::to_string(i) + " -> Perspective (16), LightPerspective (12)");
    Mtx44 p;
    MTXPerspective(p, static_cast<float>(i), 4.0f / 3.0f, 0.1f, 1000.0f);
    b.add44(p);
    Mtx l;
    MTXLightPerspective(l, static_cast<float>(i), 1.0f, 0.5f, -0.5f, 0.5f, 0.5f);
    b.add(l);
  }
  return b;
}

Bits group_mtx_linear() {
  Bits b;
  Mtx a, c, inv, t;
  MTXIdentity(a);
  for (int i = 0; i < 64; ++i) {
    b.begin_case("step " + std::to_string(i) + " -> Concat, Inverse result, Inverse, Transpose, LookAt, MultVec");
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
  b.begin_case("Ortho, Frustum");
  Mtx44 o;
  MTXOrtho(o, 0.0f, 480.0f, 0.0f, 640.0f, 0.0f, 8000.0f);
  b.add44(o);
  MTXFrustum(o, 1.0f, -1.0f, -1.33f, 1.33f, 1.0f, 100.0f);
  b.add44(o);
  return b;
}

Bits group_quat() {
  Bits b;
  for (int i = 0; i < 90; ++i) {
    const float t = static_cast<float>(i) / 89.0f;
    b.begin_case("t=" + hex(t) + " -> Slerp, RotAxisRad, MTXQuat (12), QUATMtx, LogN, Exp");
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
  return b;
}

struct Group {
  const char* name;
  Bits (*run)();
};
const Group kGroups[] = {
    {"vec_rsqrt", group_vec_rsqrt},   {"vec_distance", group_vec_distance}, {"vec_c", group_vec_c},
    {"mtx_trig", group_mtx_trig},     {"mtx_linear", group_mtx_linear},     {"quat", group_quat},
};

struct GoldenGroup {
  uint64_t hash = 0;
  size_t count = 0;
  std::vector<uint64_t> chunks;
};

// mtx_golden.txt: "group <name> <hash> <value count>" then "chunk <name> <index> <hash>" lines.
std::map<std::string, GoldenGroup> load_golden() {
  std::map<std::string, GoldenGroup> out;
  std::ifstream in(MPT_GX_MTX_GOLDEN_FILE);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string kind, name;
    if (!(ls >> kind >> name)) continue;
    if (kind == "group") {
      ls >> std::hex >> out[name].hash >> std::dec >> out[name].count;
    } else if (kind == "chunk") {
      size_t index = 0;
      uint64_t h = 0;
      ls >> std::dec >> index >> std::hex >> h;
      auto& chunks = out[name].chunks;
      if (chunks.size() <= index) chunks.resize(index + 1);
      chunks[index] = h;
    }
  }
  return out;
}

void report_divergence(const char* name, const Bits& got, const GoldenGroup& want) {
  std::printf("group %s: got %016llx over %zu values, golden %016llx over %zu values\n", name,
              static_cast<unsigned long long>(got.hash()), got.v.size(), static_cast<unsigned long long>(want.hash),
              want.count);
  const auto chunks = got.chunk_hashes();
  int shown = 0;
  for (size_t c = 0; c < chunks.size(); ++c) {
    if (c < want.chunks.size() && chunks[c] == want.chunks[c]) continue;
    std::printf("  chunk %zu (values %zu..%zu) differs\n", c, c * Bits::kChunk,
                std::min(got.v.size(), (c + 1) * Bits::kChunk) - 1);
    if (shown++ >= 2) continue;  // print the values of the first two diverging chunks only
    uint32_t lastCase = UINT32_MAX;
    for (size_t i = c * Bits::kChunk; i < std::min(got.v.size(), (c + 1) * Bits::kChunk); ++i) {
      if (got.value_case[i] != lastCase) {
        lastCase = got.value_case[i];
        std::printf("    case %u: %s\n", lastCase, got.labels.empty() ? "" : got.labels[lastCase].c_str());
      }
      std::printf("      [%zu] %08x %.9g\n", i, got.v[i], static_cast<double>(std::bit_cast<float>(got.v[i])));
    }
  }
}

void dump_raw(const char* path) {
  FILE* f = std::fopen(path, "w");
  if (f == nullptr) return;
  for (const Group& g : kGroups) {
    const Bits b = g.run();
    for (size_t i = 0; i < b.v.size(); ++i) {
      std::fprintf(f, "%s %zu %08x %s\n", g.name, i, b.v[i], b.labels.empty() ? "" : b.labels[b.value_case[i]].c_str());
    }
  }
  std::fclose(f);
}

}  // namespace

TEST_CASE("aurora MTX matches its golden values") {
  if (const char* dump = std::getenv("MPT_GX_MTX_DUMP")) {
    dump_raw(dump);
  }
  if (std::getenv("MPT_GX_MTX_PRINT") != nullptr) {
    // Goldens come only from the pinned aurora (ADR 0009 section 1).
    REQUIRE_MESSAGE(MPT_GX_AURORA_PINNED, "refusing to record goldens from an unpinned aurora checkout");
    std::printf("# Golden values for test_mtx.cpp (see its header). Recorded on the reference machine.\n");
    for (const Group& g : kGroups) {
      const Bits b = g.run();
      std::printf("group %s %016llx %zu\n", g.name, static_cast<unsigned long long>(b.hash()), b.v.size());
      const auto chunks = b.chunk_hashes();
      for (size_t c = 0; c < chunks.size(); ++c) {
        std::printf("chunk %s %zu %016llx\n", g.name, c, static_cast<unsigned long long>(chunks[c]));
      }
    }
    return;
  }
  const auto golden = load_golden();
  REQUIRE_MESSAGE(!golden.empty(), "cannot read " MPT_GX_MTX_GOLDEN_FILE);
  for (const Group& g : kGroups) {
    const Bits b = g.run();
    const auto it = golden.find(g.name);
    REQUIRE_MESSAGE(it != golden.end(), "no golden for group " << g.name);
    const bool same = b.hash() == it->second.hash && b.v.size() == it->second.count;
    if (!same) report_divergence(g.name, b, it->second);
    INFO("group " << g.name);
    CHECK(same);
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
