// Deterministic float math (ADR 0008 §5). Only IEEE add, subtract, multiply,
// divide, sqrt and float<->double conversions are used, all correctly rounded
// under round-to-nearest, so results are identical on every conforming host
// built with -ffp-contract=off. No host libm transcendental is ever called.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "mpt_gc/gc_math.h"

#if !defined(__clang__)
#error "The GameCube SDK layer must be built with Clang (ADR 0008 §4)"
#endif
#if defined(__FAST_MATH__)
#error "The SDK math library must not be built with fast-math"
#endif

namespace {

std::uint64_t bitsOf(double d) {
  std::uint64_t u;
  std::memcpy(&u, &d, sizeof u);
  return u;
}

double fromBits(std::uint64_t u) {
  double d;
  std::memcpy(&d, &u, sizeof d);
  return d;
}

constexpr double kPi = 3.14159265358979311600e+00;
constexpr double kPiHalf = 1.57079632679489655800e+00;
constexpr double kPiSixth = 5.23598775598298815659e-01;
constexpr double kSqrt3 = 1.73205080756887719318e+00;
constexpr double kTwoOverPi = 6.36619772367581382433e-01;
// pi/2 split into three parts (the first two with 33 significant bits) for
// Cody-Waite reduction.
constexpr double kPio2_1 = 1.57079632673412561417e+00;
constexpr double kPio2_2 = 6.07710050630396597660e-11;
constexpr double kPio2_3 = 2.02226624879595063154e-21;

// sin and cos on [-pi/4, pi/4] by Taylor series (truncation error < 1e-17).
double sinPoly(double r) {
  const double r2 = r * r;
  double t = -1.0 / 1307674368000.0;  // -1/15!
  t = t * r2 + 1.0 / 6227020800.0;    // 1/13!
  t = t * r2 - 1.0 / 39916800.0;
  t = t * r2 + 1.0 / 362880.0;
  t = t * r2 - 1.0 / 5040.0;
  t = t * r2 + 1.0 / 120.0;
  t = t * r2 - 1.0 / 6.0;
  return r + r * r2 * t;
}

double cosPoly(double r) {
  const double r2 = r * r;
  double t = 1.0 / 20922789888000.0;  // 1/16!
  t = t * r2 - 1.0 / 87178291200.0;
  t = t * r2 + 1.0 / 479001600.0;
  t = t * r2 - 1.0 / 3628800.0;
  t = t * r2 + 1.0 / 40320.0;
  t = t * r2 - 1.0 / 720.0;
  t = t * r2 + 1.0 / 24.0;
  t = t * r2 - 0.5;
  return 1.0 + t * r2;
}

// x = k*pi/2 + r with |r| <= ~pi/4. Exact enough for |x| < 2^20 * pi/2; beyond
// that accuracy degrades, but the result is still a fixed function of x.
double reduce(double x, std::int64_t& k) {
  const double fk = std::floor(x * kTwoOverPi + 0.5);
  k = static_cast<std::int64_t>(fk);
  return ((x - fk * kPio2_1) - fk * kPio2_2) - fk * kPio2_3;
}

double sinD(double x, bool cosine) {
  std::int64_t k = 0;
  const double r = reduce(x, k);
  const int q = static_cast<int>((k + (cosine ? 1 : 0)) & 3);
  switch (q) {
    case 0:
      return sinPoly(r);
    case 1:
      return cosPoly(r);
    case 2:
      return -sinPoly(r);
    default:
      return -cosPoly(r);
  }
}

// atan on [0, tan(pi/12)] by its alternating series (error < 1e-17).
double atanSeries(double t) {
  const double t2 = t * t;
  double s = 0.0;
  for (int n = 27; n >= 3; n -= 2) s = (((n / 2) % 2) ? -1.0 : 1.0) / n + s * t2;
  return t + t * t2 * s;
}

double atanD(double x) {
  if (std::isnan(x)) return x;
  const bool neg = x < 0;
  double t = neg ? -x : x;
  double base = 0.0;
  bool inv = false;
  if (t > 1.0) {
    t = 1.0 / t;
    inv = true;
  }
  if (t > 0.26794919243112270) {  // tan(pi/12)
    t = (t * kSqrt3 - 1.0) / (t + kSqrt3);
    base = kPiSixth;
  }
  double r = base + atanSeries(t);
  if (inv) r = kPiHalf - r;
  return neg ? -r : r;
}

double atan2D(double y, double x) {
  if (std::isnan(x) || std::isnan(y)) return x + y;
  const bool yneg = std::signbit(y);
  if (std::isinf(x) && std::isinf(y)) {
    const double a = x > 0 ? kPi / 4 : 3 * kPi / 4;
    return yneg ? -a : a;
  }
  if (x == 0.0) {
    if (y == 0.0) return std::signbit(x) ? (yneg ? -kPi : kPi) : y;
    return yneg ? -kPiHalf : kPiHalf;
  }
  const double a = atanD(y / x);
  if (x > 0) return a;
  return yneg ? a - kPi : a + kPi;
}

}  // namespace

extern "C" {

// Correctly rounded fusedMultiplyAdd for binary32. a*b is exact in binary64;
// the sum is rounded to binary64 with round-to-odd (an exact two-sum gives the
// error term), and a round-to-odd result with 53 >= 24 + 2 bits rounds to the
// correctly rounded binary32 value (Boldo and Melquiond).
f32 mpt_fmaf(f32 a, f32 b, f32 c) {
  const double p = static_cast<double>(a) * static_cast<double>(b);
  const double dc = static_cast<double>(c);
  if (!std::isfinite(p) || !std::isfinite(dc)) return static_cast<f32>(p + dc);
  const double s = p + dc;
  const double bv = s - p;
  const double err = (p - (s - bv)) + (dc - bv);
  if (err == 0.0) return static_cast<f32>(s);
  std::uint64_t u = bitsOf(s);
  if ((u & 1u) == 0) {
    // Move one ulp toward the exact value so the last bit is odd.
    const bool same_sign = std::signbit(s) == std::signbit(err);
    u = same_sign ? u + 1 : u - 1;
  }
  return static_cast<f32>(fromBits(u));
}

f32 mpt_sinf(f32 x) {
  if (!std::isfinite(x)) return x - x;
  return static_cast<f32>(sinD(x, false));
}

f32 mpt_cosf(f32 x) {
  if (!std::isfinite(x)) return x - x;
  return static_cast<f32>(sinD(x, true));
}

f32 mpt_tanf(f32 x) {
  if (!std::isfinite(x)) return x - x;
  return static_cast<f32>(sinD(x, false) / sinD(x, true));
}

f32 mpt_atan2f(f32 y, f32 x) { return static_cast<f32>(atan2D(y, x)); }

f32 mpt_acosf(f32 x) {
  const double d = x;
  if (std::isnan(d) || d > 1.0 || d < -1.0) return std::numeric_limits<f32>::quiet_NaN();
  return static_cast<f32>(atan2D(std::sqrt((1.0 - d) * (1.0 + d)), d));
}

f32 mpt_sqrtf(f32 x) { return std::sqrt(x); }

}  // extern "C"
