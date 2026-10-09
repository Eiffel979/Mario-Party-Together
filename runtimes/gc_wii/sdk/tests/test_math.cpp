// Math goldens (E10, ADR 0008 §5): software fmaf (hard rounding cases and a
// comparison against hardware FMA where the CPU has it), no FMA contraction,
// saturating fast casts, trig accuracy, and a golden hash of trig and fmaf
// outputs that must be identical on every platform. MTX/VEC/QUAT goldens belong
// to P1-14 (aurora's MTX is the only implementation, ADR 0007 §6).
#include <bit>
#include <cmath>
#include <cstring>

#include "harness.h"
#include "mpt/core/rng.h"
#include "mpt/core/state.h"
#include "mpt_gc/gc_math.h"
#include "mpt_gc/os.h"

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <immintrin.h>

using namespace mpt;

namespace {

f32 F(std::uint32_t bits) { return std::bit_cast<f32>(bits); }
std::uint32_t B(f32 f) { return std::bit_cast<std::uint32_t>(f); }

// FMA3 is usable only if the CPU has it (CPUID.1:ECX.FMA[12]) and the OS saves
// the YMM state (CPUID.1:ECX.OSXSAVE[27], then XCR0 bits 1 and 2 via xgetbv).
__attribute__((target("xsave"))) std::uint64_t readXcr0() { return _xgetbv(0); }

bool cpuHasFma() {
  unsigned c = 0;
#if defined(_MSC_VER)
  int info[4];
  __cpuid(info, 1);
  c = static_cast<unsigned>(info[2]);
#else
  unsigned a, b, d;
  if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
#endif
  const bool fma = (c & (1u << 12)) != 0;
  const bool osxsave = (c & (1u << 27)) != 0;
  if (!fma || !osxsave) return false;
  return (readXcr0() & 0x6) == 0x6;  // XMM and YMM state enabled
}

__attribute__((target("fma"))) f32 hardwareFma(f32 a, f32 b, f32 c) {
  return _mm_cvtss_f32(_mm_fmadd_ss(_mm_set_ss(a), _mm_set_ss(b), _mm_set_ss(c)));
}

void hashF(StateHasher& h, f32 v) { h.update_u64(B(v)); }

// Reads a value the compiler cannot constant-fold.
template <typename T>
T opaque(T v) {
  volatile T x = v;
  return x;
}

}  // namespace

TEST_CASE("fmaf: hard rounding cases") {
  // (1 + 2^-12)^2 = 1 + 2^-11 + 2^-24: exactly halfway between two floats.
  const f32 a = F(0x3F800800);
  CHECK(B(mpt_fmaf(a, a, 0.0f)) == 0x3F801000u);           // tie to even
  CHECK(B(mpt_fmaf(a, a, F(0x21800000))) == 0x3F801001u);  // + 2^-60 breaks the tie upward
  CHECK(B(mpt_fmaf(a, a, -F(0x21800000))) == 0x3F801000u); // - 2^-60: just below the tie
  // The naive (float)((double)a * b + c) gets the +2^-60 case wrong (double rounding).
  CHECK(B(static_cast<f32>(static_cast<double>(a) * a + static_cast<double>(F(0x21800000)))) == 0x3F801000u);

  // The exact error of a rounded product.
  const f32 e = F(0x3F800001);  // 1 + 2^-23
  CHECK(B(mpt_fmaf(e, e, -F(0x3F800002))) == 0x28800000u);  // 2^-46

  // Subnormal results.
  CHECK(B(mpt_fmaf(std::ldexp(1.0f, -100), std::ldexp(1.0f, -49), 0.0f)) == 0x00000001u);  // 2^-149
  CHECK(B(mpt_fmaf(std::ldexp(1.5f, -100), std::ldexp(1.0f, -50), 0.0f)) == 0x00000001u);  // 0.75 ulp -> 1 ulp
  CHECK(B(mpt_fmaf(std::ldexp(1.0f, -100), std::ldexp(1.0f, -50), 0.0f)) == 0x00000000u);  // tie -> even (0)
  CHECK(B(mpt_fmaf(std::ldexp(1.0f, -100), std::ldexp(1.0f, -50), F(0x00000001))) == 0x00000002u);  // 1.5 ulp -> 2
  CHECK(B(mpt_fmaf(-std::ldexp(1.0f, -100), std::ldexp(1.0f, -50), 0.0f)) == 0x80000000u);  // -0

  // Signed zeros.
  CHECK(B(mpt_fmaf(-0.0f, 1.0f, 0.0f)) == 0x00000000u);
  CHECK(B(mpt_fmaf(-0.0f, 1.0f, -0.0f)) == 0x80000000u);
  CHECK(B(mpt_fmaf(2.0f, 3.0f, -6.0f)) == 0x00000000u);
  CHECK(B(mpt_fmaf(-2.0f, 3.0f, 6.0f)) == 0x00000000u);

  // Overflow, infinities and NaN.
  CHECK(std::isinf(mpt_fmaf(1e38f, 1e38f, 0.0f)));
  CHECK(B(mpt_fmaf(3.4e38f, 2.0f, -3.4e38f)) == B(3.4e38f));  // intermediate beyond FLT_MAX, result finite
  CHECK(std::isnan(mpt_fmaf(INFINITY, 0.0f, 1.0f)));
  CHECK(std::isnan(mpt_fmaf(INFINITY, 1.0f, -INFINITY)));
  CHECK(mpt_fmaf(1e38f, 10.0f, -INFINITY) == -INFINITY);
  CHECK(std::isnan(mpt_fmaf(NAN, 1.0f, 1.0f)));
}

TEST_CASE("fmaf matches hardware FMA on random and adversarial inputs") {
  if (!cpuHasFma()) {
    MESSAGE("CPU has no FMA3: hardware comparison skipped (hard cases above still run)");
    return;
  }
  DeterministicRng rng(12345);
  std::uint64_t mismatches = 0;
  for (int i = 0; i < 2'000'000; ++i) {
    const std::uint64_t r = rng.next();
    f32 a, b, c;
    switch (i % 4) {
      case 0:  // any bit patterns
        a = F(static_cast<std::uint32_t>(r));
        b = F(static_cast<std::uint32_t>(r >> 32));
        c = F(static_cast<std::uint32_t>(rng.next()));
        break;
      case 1: {  // cancellation: c close to -(a*b)
        a = F(0x3F000000u | (static_cast<std::uint32_t>(r) & 0x00FFFFFFu));
        b = F(0x3F000000u | (static_cast<std::uint32_t>(r >> 32) & 0x00FFFFFFu));
        const f32 p = a * b;
        c = -F(B(p) + static_cast<std::uint32_t>(rng.below(5)) - 2u);
        break;
      }
      case 2: {  // c tiny relative to a*b: rounding-boundary territory
        a = F(0x3F800000u | (static_cast<std::uint32_t>(r) & 0x007FFFFFu));
        b = F(0x3F800000u | (static_cast<std::uint32_t>(r >> 23) & 0x007FFFFFu));
        c = F((static_cast<std::uint32_t>(rng.below(60)) << 23) | (static_cast<std::uint32_t>(r >> 40) & 0x7FFFFFu)) *
            ((r >> 63) ? -1.0f : 1.0f);
        break;
      }
      default: {  // subnormal products
        a = F(static_cast<std::uint32_t>(0x0D000000u + (r & 0x00FFFFFFu)));
        b = F(static_cast<std::uint32_t>(0x0D000000u + ((r >> 24) & 0x00FFFFFFu)));
        c = F(static_cast<std::uint32_t>(rng.below(0x00800000u))) * (((r >> 50) & 1) ? -1.0f : 1.0f);
        break;
      }
    }
    const f32 sw = mpt_fmaf(a, b, c), hw = hardwareFma(a, b, c);
    const bool same = B(sw) == B(hw) || (std::isnan(sw) && std::isnan(hw));
    if (!same && mismatches++ < 5)
      MESSAGE("fmaf(" << B(a) << ", " << B(b) << ", " << B(c) << "): sw " << B(sw) << " hw " << B(hw));
  }
  CHECK(mismatches == 0);
}

extern "C" float mpt_gc_redirect_probe_c(float a, float b, float c);
extern "C" float mpt_gc_redirect_probe_std(float a, float b, float c);

TEST_CASE("fmaf and std::fmaf go through the redirect to mpt_fmaf") {
  // The double-rounding case: only a correctly rounded fma gives 0x3F801001.
  const f32 a = opaque(F(0x3F800800)), c = opaque(F(0x21800000));
  CHECK(B(mpt_gc_redirect_probe_c(a, a, c)) == 0x3F801001u);
  CHECK(B(mpt_gc_redirect_probe_std(a, a, c)) == 0x3F801001u);
}

TEST_CASE("the compiler does not contract a*b+c into an FMA") {
  const f32 a = opaque(F(0x3F800800)), c = opaque(F(0x21800000));
  const f32 r = a * a + c;  // must round the product first
  CHECK(B(r) == B(static_cast<f32>(a * a) + c));
  CHECK(B(r) != B(mpt_fmaf(a, a, c)));
}

TEST_CASE("fast casts saturate, truncate and map NaN to zero") {
  auto s16 = [](f32 v) {
    ::s16 o;
    OSf32tos16(&v, &o);
    return o;
  };
  auto u8f = [](f32 v) {
    ::u8 o;
    OSf32tou8(&v, &o);
    return o;
  };
  auto s8f = [](f32 v) {
    ::s8 o;
    OSf32tos8(&v, &o);
    return o;
  };
  auto u16f = [](f32 v) {
    ::u16 o;
    OSf32tou16(&v, &o);
    return o;
  };
  CHECK(s16(40000.0f) == 32767);
  CHECK(s16(-40000.0f) == -32768);
  CHECK(s16(1.9f) == 1);
  CHECK(s16(-1.9f) == -1);
  CHECK(s16(NAN) == 0);
  CHECK(s16(INFINITY) == 32767);
  CHECK(s16(-INFINITY) == -32768);
  CHECK(u8f(-5.0f) == 0);
  CHECK(u8f(300.0f) == 255);
  CHECK(u8f(254.99f) == 254);
  CHECK(s8f(-128.5f) == -128);
  CHECK(s8f(127.9f) == 127);
  CHECK(u16f(-0.5f) == 0);
  CHECK(u16f(1e9f) == 65535);
  ::s16 in = -1234;
  f32 out = 0;
  OSs16tof32(&in, &out);
  CHECK(out == -1234.0f);
}

TEST_CASE("trig is accurate to within 2 ulp of the double reference") {
  int worst = 0;
  for (int i = -20000; i <= 20000; ++i) {
    const f32 x = static_cast<f32>(i) * 0.001f;
    const auto ulps = [](f32 got, double ref) {
      const f32 r = static_cast<f32>(ref);
      const std::int64_t d = static_cast<std::int64_t>(static_cast<std::int32_t>(B(got))) -
                             static_cast<std::int64_t>(static_cast<std::int32_t>(B(r)));
      return static_cast<int>(d < 0 ? -d : d);
    };
    // Near zeros of the function the float result is tiny; compare absolutely there.
    const double s = std::sin(static_cast<double>(x)), c = std::cos(static_cast<double>(x));
    if (std::fabs(s) > 1e-3) worst = std::max(worst, ulps(mpt_sinf(x), s));
    if (std::fabs(c) > 1e-3) worst = std::max(worst, ulps(mpt_cosf(x), c));
    if (std::fabs(c) > 1e-2) worst = std::max(worst, ulps(mpt_tanf(x), s / c));
    const f32 y = static_cast<f32>(i % 997) * 0.01f - 5.0f;
    const double a2 = std::atan2(static_cast<double>(y), static_cast<double>(x));
    if (std::fabs(a2) > 1e-3) worst = std::max(worst, ulps(mpt_atan2f(y, x), a2));
    if (i >= -1000 && i <= 1000) {
      const double ac = std::acos(static_cast<double>(x));
      worst = std::max(worst, ulps(mpt_acosf(x), ac));
    }
  }
  CHECK(worst <= 2);
  CHECK(mpt_sinf(0.0f) == 0.0f);
  CHECK(mpt_cosf(0.0f) == 1.0f);
  CHECK(std::isnan(mpt_sinf(INFINITY)));
  CHECK(std::isnan(mpt_acosf(1.5f)));
  CHECK(mpt_atan2f(0.0f, -1.0f) == static_cast<f32>(3.14159265358979323846));
}

TEST_CASE("golden: trig outputs") {
  StateHasher h;
  for (int i = -5000; i <= 5000; ++i) {
    const f32 x = static_cast<f32>(i) * 0.0123f;
    hashF(h, mpt_sinf(x));
    hashF(h, mpt_cosf(x));
    hashF(h, mpt_tanf(x));
    hashF(h, mpt_atan2f(x, 1.5f - x));
    hashF(h, mpt_acosf(static_cast<f32>(i) / 5000.0f));
    hashF(h, mpt_fmaf(x, 1.0f / 3.0f, -x));
  }
  hashF(h, mpt_sinf(1e6f));  // far outside the accurate range: still a fixed value
  hashF(h, mpt_cosf(-3e7f));
  CHECK(h.digest() == 13900187656750823100ull);  // golden: identical on every platform
}
