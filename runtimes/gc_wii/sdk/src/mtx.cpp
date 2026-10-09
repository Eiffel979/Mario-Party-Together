// Plain-C MTX/VEC/QUAT (ADR 0007 §4). The order of every floating-point
// operation is fixed by the source and by -ffp-contract=off, and the golden
// tests pin the results.
#include <cstring>

#include "mpt_gc/gc_math.h"
#include "mpt_gc/mtx.h"

#if !defined(__clang__)
#error "The GameCube SDK layer must be built with Clang (ADR 0008 §4)"
#endif

namespace {
constexpr f32 kPiF = 3.14159265358979323846f;
inline f32 sq(f32 v) { return v * v; }
}  // namespace

extern "C" {

// ---- 3x4 matrices -------------------------------------------------------------------

void C_MTXIdentity(Mtx m) {
  std::memset(m, 0, sizeof(Mtx));
  m[0][0] = m[1][1] = m[2][2] = 1.0f;
}

void C_MTXCopy(const Mtx src, Mtx dst) {
  if (src != dst) std::memmove(dst, src, sizeof(Mtx));
}

void C_MTXConcat(const Mtx a, const Mtx b, Mtx ab) {
  Mtx t;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 4; ++j) {
      f32 v = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
      if (j == 3) v = v + a[i][3];
      t[i][j] = v;
    }
  }
  std::memcpy(ab, t, sizeof(Mtx));
}

void C_MTXConcatArray(const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count) {
  for (u32 i = 0; i < count; ++i) C_MTXConcat(a, srcBase[i], dstBase[i]);
}

void C_MTXTranspose(const Mtx src, Mtx xPose) {
  Mtx t;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) t[i][j] = src[j][i];
  t[0][3] = t[1][3] = t[2][3] = 0.0f;
  std::memcpy(xPose, t, sizeof(Mtx));
}

u32 C_MTXInverse(const Mtx src, Mtx inv) {
  const f32 det = src[0][0] * src[1][1] * src[2][2] + src[0][1] * src[1][2] * src[2][0] +
                  src[0][2] * src[1][0] * src[2][1] - src[2][0] * src[1][1] * src[0][2] -
                  src[1][0] * src[0][1] * src[2][2] - src[0][0] * src[2][1] * src[1][2];
  if (det == 0.0f) return 0;
  const f32 r = 1.0f / det;
  Mtx t;
  t[0][0] = (src[1][1] * src[2][2] - src[2][1] * src[1][2]) * r;
  t[0][1] = -(src[0][1] * src[2][2] - src[2][1] * src[0][2]) * r;
  t[0][2] = (src[0][1] * src[1][2] - src[1][1] * src[0][2]) * r;
  t[1][0] = -(src[1][0] * src[2][2] - src[2][0] * src[1][2]) * r;
  t[1][1] = (src[0][0] * src[2][2] - src[2][0] * src[0][2]) * r;
  t[1][2] = -(src[0][0] * src[1][2] - src[1][0] * src[0][2]) * r;
  t[2][0] = (src[1][0] * src[2][1] - src[2][0] * src[1][1]) * r;
  t[2][1] = -(src[0][0] * src[2][1] - src[2][0] * src[0][1]) * r;
  t[2][2] = (src[0][0] * src[1][1] - src[1][0] * src[0][1]) * r;
  for (int i = 0; i < 3; ++i)
    t[i][3] = -t[i][0] * src[0][3] - t[i][1] * src[1][3] - t[i][2] * src[2][3];
  std::memcpy(inv, t, sizeof(Mtx));
  return 1;
}

u32 C_MTXInvXpose(const Mtx src, Mtx invX) {
  Mtx inv;
  if (!C_MTXInverse(src, inv)) return 0;
  C_MTXTranspose(inv, invX);
  return 1;
}

void C_MTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA) {
  C_MTXIdentity(m);
  switch (axis) {
    case 'x':
    case 'X':
      m[1][1] = cosA;
      m[1][2] = -sinA;
      m[2][1] = sinA;
      m[2][2] = cosA;
      break;
    case 'y':
    case 'Y':
      m[0][0] = cosA;
      m[0][2] = sinA;
      m[2][0] = -sinA;
      m[2][2] = cosA;
      break;
    case 'z':
    case 'Z':
      m[0][0] = cosA;
      m[0][1] = -sinA;
      m[1][0] = sinA;
      m[1][1] = cosA;
      break;
    default:
      break;
  }
}

void C_MTXRotRad(Mtx m, char axis, f32 rad) { C_MTXRotTrig(m, axis, mpt_sinf(rad), mpt_cosf(rad)); }

void C_MTXRotAxisRad(Mtx m, const Vec* axis, f32 rad) {
  const f32 s = mpt_sinf(rad), c = mpt_cosf(rad), t = 1.0f - c;
  Vec v;
  C_VECNormalize(axis, &v);
  const f32 x = v.x, y = v.y, z = v.z;
  m[0][0] = t * x * x + c;
  m[0][1] = t * x * y - s * z;
  m[0][2] = t * x * z + s * y;
  m[0][3] = 0.0f;
  m[1][0] = t * x * y + s * z;
  m[1][1] = t * y * y + c;
  m[1][2] = t * y * z - s * x;
  m[1][3] = 0.0f;
  m[2][0] = t * x * z - s * y;
  m[2][1] = t * y * z + s * x;
  m[2][2] = t * z * z + c;
  m[2][3] = 0.0f;
}

void C_MTXTrans(Mtx m, f32 xT, f32 yT, f32 zT) {
  C_MTXIdentity(m);
  m[0][3] = xT;
  m[1][3] = yT;
  m[2][3] = zT;
}

void C_MTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT) {
  C_MTXCopy(src, dst);
  dst[0][3] = src[0][3] + xT;
  dst[1][3] = src[1][3] + yT;
  dst[2][3] = src[2][3] + zT;
}

void C_MTXScale(Mtx m, f32 xS, f32 yS, f32 zS) {
  std::memset(m, 0, sizeof(Mtx));
  m[0][0] = xS;
  m[1][1] = yS;
  m[2][2] = zS;
}

void C_MTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS) {
  const f32 s[3] = {xS, yS, zS};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 4; ++j) dst[i][j] = src[i][j] * s[i];
}

void C_MTXQuat(Mtx m, const Quaternion* q) {
  const f32 n = q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
  const f32 s = n == 0.0f ? 0.0f : 2.0f / n;
  const f32 xs = q->x * s, ys = q->y * s, zs = q->z * s;
  const f32 wx = q->w * xs, wy = q->w * ys, wz = q->w * zs;
  const f32 xx = q->x * xs, xy = q->x * ys, xz = q->x * zs;
  const f32 yy = q->y * ys, yz = q->y * zs, zz = q->z * zs;
  m[0][0] = 1.0f - (yy + zz);
  m[0][1] = xy - wz;
  m[0][2] = xz + wy;
  m[0][3] = 0.0f;
  m[1][0] = xy + wz;
  m[1][1] = 1.0f - (xx + zz);
  m[1][2] = yz - wx;
  m[1][3] = 0.0f;
  m[2][0] = xz - wy;
  m[2][1] = yz + wx;
  m[2][2] = 1.0f - (xx + yy);
  m[2][3] = 0.0f;
}

void C_MTXReflect(Mtx m, const Vec* p, const Vec* n) {
  const f32 d = p->x * n->x + p->y * n->y + p->z * n->z;
  const f32 nv[3] = {n->x, n->y, n->z};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) m[i][j] = (i == j ? 1.0f : 0.0f) - 2.0f * nv[i] * nv[j];
    m[i][3] = 2.0f * d * nv[i];
  }
}

void C_MTXMultVec(const Mtx m, const Vec* src, Vec* dst) {
  const Vec v = *src;
  dst->x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
  dst->y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
  dst->z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
}

void C_MTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
  for (u32 i = 0; i < count; ++i) C_MTXMultVec(m, &srcBase[i], &dstBase[i]);
}

void C_MTXMultVecSR(const Mtx m, const Vec* src, Vec* dst) {
  const Vec v = *src;
  dst->x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z;
  dst->y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z;
  dst->z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z;
}

void C_MTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count) {
  for (u32 i = 0; i < count; ++i) C_MTXMultVecSR(m, &srcBase[i], &dstBase[i]);
}

void C_MTXLookAt(Mtx m, const Vec* camPos, const Vec* camUp, const Vec* target) {
  Vec look, right, up;
  look.x = camPos->x - target->x;
  look.y = camPos->y - target->y;
  look.z = camPos->z - target->z;
  C_VECNormalize(&look, &look);
  C_VECCrossProduct(camUp, &look, &right);
  C_VECNormalize(&right, &right);
  C_VECCrossProduct(&look, &right, &up);
  const Vec rows[3] = {right, up, look};
  for (int i = 0; i < 3; ++i) {
    m[i][0] = rows[i].x;
    m[i][1] = rows[i].y;
    m[i][2] = rows[i].z;
    m[i][3] = -(camPos->x * rows[i].x + camPos->y * rows[i].y + camPos->z * rows[i].z);
  }
}

void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  std::memset(m, 0, sizeof(Mtx));
  const f32 w = 1.0f / (r - l), h = 1.0f / (t - b);
  m[0][0] = (2.0f * n * w) * scaleS;
  m[0][2] = ((r + l) * w) * scaleS - transS;
  m[1][1] = (2.0f * n * h) * scaleT;
  m[1][2] = ((t + b) * h) * scaleT - transT;
  m[2][2] = -1.0f;
}

void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  std::memset(m, 0, sizeof(Mtx));
  const f32 angle = fovY * 0.5f * (kPiF / 180.0f);
  const f32 cot = 1.0f / mpt_tanf(angle);
  m[0][0] = (cot / aspect) * scaleS;
  m[0][2] = -transS;
  m[1][1] = cot * scaleT;
  m[1][2] = -transT;
  m[2][2] = -1.0f;
}

void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT) {
  std::memset(m, 0, sizeof(Mtx));
  const f32 w = 1.0f / (r - l), h = 1.0f / (t - b);
  m[0][0] = 2.0f * w * scaleS;
  m[0][3] = -(r + l) * w * scaleS + transS;
  m[1][1] = 2.0f * h * scaleT;
  m[1][3] = -(t + b) * h * scaleT + transT;
  m[2][3] = 1.0f;
}

// ---- 4x4 matrices -------------------------------------------------------------------

void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
  std::memset(m, 0, sizeof(Mtx44));
  const f32 w = 1.0f / (r - l), h = 1.0f / (t - b), d = 1.0f / (f - n);
  m[0][0] = 2.0f * n * w;
  m[0][2] = (r + l) * w;
  m[1][1] = 2.0f * n * h;
  m[1][2] = (t + b) * h;
  m[2][2] = -n * d;
  m[2][3] = -(f * n) * d;
  m[3][2] = -1.0f;
}

void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f) {
  std::memset(m, 0, sizeof(Mtx44));
  const f32 angle = fovY * 0.5f * (kPiF / 180.0f);
  const f32 cot = 1.0f / mpt_tanf(angle);
  const f32 d = 1.0f / (f - n);
  m[0][0] = cot / aspect;
  m[1][1] = cot;
  m[2][2] = -n * d;
  m[2][3] = -(f * n) * d;
  m[3][2] = -1.0f;
}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f) {
  std::memset(m, 0, sizeof(Mtx44));
  const f32 w = 1.0f / (r - l), h = 1.0f / (t - b), d = 1.0f / (f - n);
  m[0][0] = 2.0f * w;
  m[0][3] = -(r + l) * w;
  m[1][1] = 2.0f * h;
  m[1][3] = -(t + b) * h;
  m[2][2] = -1.0f * d;
  m[2][3] = -f * d;
  m[3][3] = 1.0f;
}

void C_MTX44Identity(Mtx44 m) {
  std::memset(m, 0, sizeof(Mtx44));
  m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

void C_MTX44Copy(const Mtx44 src, Mtx44 dst) {
  if (src != dst) std::memmove(dst, src, sizeof(Mtx44));
}

void C_MTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab) {
  Mtx44 t;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
  std::memcpy(ab, t, sizeof(Mtx44));
}

void C_MTX44Transpose(const Mtx44 src, Mtx44 xPose) {
  Mtx44 t;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) t[i][j] = src[j][i];
  std::memcpy(xPose, t, sizeof(Mtx44));
}

// Gauss-Jordan elimination with partial pivoting.
u32 C_MTX44Inverse(const Mtx44 src, Mtx44 inv) {
  f32 a[4][4], r[4][4];
  std::memcpy(a, src, sizeof a);
  C_MTX44Identity(r);
  for (int c = 0; c < 4; ++c) {
    int p = c;
    f32 best = a[c][c] < 0 ? -a[c][c] : a[c][c];
    for (int i = c + 1; i < 4; ++i) {
      const f32 v = a[i][c] < 0 ? -a[i][c] : a[i][c];
      if (v > best) {
        best = v;
        p = i;
      }
    }
    if (best == 0.0f) return 0;
    if (p != c) {
      for (int j = 0; j < 4; ++j) {
        const f32 t1 = a[c][j];
        a[c][j] = a[p][j];
        a[p][j] = t1;
        const f32 t2 = r[c][j];
        r[c][j] = r[p][j];
        r[p][j] = t2;
      }
    }
    const f32 piv = 1.0f / a[c][c];
    for (int j = 0; j < 4; ++j) {
      a[c][j] = a[c][j] * piv;
      r[c][j] = r[c][j] * piv;
    }
    for (int i = 0; i < 4; ++i) {
      if (i == c) continue;
      const f32 k = a[i][c];
      for (int j = 0; j < 4; ++j) {
        a[i][j] = a[i][j] - k * a[c][j];
        r[i][j] = r[i][j] - k * r[c][j];
      }
    }
  }
  std::memcpy(inv, r, sizeof r);
  return 1;
}

void C_MTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst) {
  const Vec v = *src;
  const f32 x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
  const f32 y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
  const f32 z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
  const f32 w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
  const f32 rw = 1.0f / w;
  dst->x = x * rw;
  dst->y = y * rw;
  dst->z = z * rw;
}

// ---- Vectors -------------------------------------------------------------------------

void C_VECAdd(const Vec* a, const Vec* b, Vec* ab) {
  ab->x = a->x + b->x;
  ab->y = a->y + b->y;
  ab->z = a->z + b->z;
}

void C_VECSubtract(const Vec* a, const Vec* b, Vec* a_b) {
  a_b->x = a->x - b->x;
  a_b->y = a->y - b->y;
  a_b->z = a->z - b->z;
}

void C_VECScale(const Vec* src, Vec* dst, f32 scale) {
  dst->x = src->x * scale;
  dst->y = src->y * scale;
  dst->z = src->z * scale;
}

f32 C_VECSquareMag(const Vec* v) { return v->x * v->x + v->y * v->y + v->z * v->z; }

f32 C_VECMag(const Vec* v) { return mpt_sqrtf(C_VECSquareMag(v)); }

void C_VECNormalize(const Vec* src, Vec* unit) {
  const f32 mag = C_VECSquareMag(src);
  if (mag == 0.0f) {
    *unit = *src;
    return;
  }
  const f32 inv = 1.0f / mpt_sqrtf(mag);
  unit->x = src->x * inv;
  unit->y = src->y * inv;
  unit->z = src->z * inv;
}

f32 C_VECDotProduct(const Vec* a, const Vec* b) { return a->x * b->x + a->y * b->y + a->z * b->z; }

void C_VECCrossProduct(const Vec* a, const Vec* b, Vec* axb) {
  const Vec t{a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x};
  *axb = t;
}

f32 C_VECSquareDistance(const Vec* a, const Vec* b) { return sq(a->x - b->x) + sq(a->y - b->y) + sq(a->z - b->z); }

f32 C_VECDistance(const Vec* a, const Vec* b) { return mpt_sqrtf(C_VECSquareDistance(a, b)); }

void C_VECHalfAngle(const Vec* a, const Vec* b, Vec* half) {
  Vec na{-a->x, -a->y, -a->z}, nb{-b->x, -b->y, -b->z};
  C_VECNormalize(&na, &na);
  C_VECNormalize(&nb, &nb);
  C_VECAdd(&na, &nb, half);
  if (C_VECDotProduct(half, half) > 0.0f) C_VECNormalize(half, half);
}

void C_VECReflect(const Vec* src, const Vec* normal, Vec* dst) {
  Vec ui{-src->x, -src->y, -src->z}, un;
  C_VECNormalize(&ui, &ui);
  C_VECNormalize(normal, &un);
  const f32 c = C_VECDotProduct(&ui, &un);
  dst->x = 2.0f * c * un.x - ui.x;
  dst->y = 2.0f * c * un.y - ui.y;
  dst->z = 2.0f * c * un.z - ui.z;
  C_VECNormalize(dst, dst);
}

// ---- Quaternions ------------------------------------------------------------------------

void C_QUATAdd(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  r->x = p->x + q->x;
  r->y = p->y + q->y;
  r->z = p->z + q->z;
  r->w = p->w + q->w;
}

void C_QUATSubtract(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  r->x = p->x - q->x;
  r->y = p->y - q->y;
  r->z = p->z - q->z;
  r->w = p->w - q->w;
}

void C_QUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq) {
  const Quaternion a = *p, b = *q;
  pq->x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
  pq->y = a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z;
  pq->z = a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x;
  pq->w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
}

void C_QUATScale(const Quaternion* q, Quaternion* r, f32 scale) {
  r->x = q->x * scale;
  r->y = q->y * scale;
  r->z = q->z * scale;
  r->w = q->w * scale;
}

f32 C_QUATDotProduct(const Quaternion* p, const Quaternion* q) {
  return p->x * q->x + p->y * q->y + p->z * q->z + p->w * q->w;
}

void C_QUATNormalize(const Quaternion* src, Quaternion* unit) {
  const f32 mag = C_QUATDotProduct(src, src);
  if (mag >= 0.00001f) {
    C_QUATScale(src, unit, 1.0f / mpt_sqrtf(mag));
  } else {
    unit->x = unit->y = unit->z = unit->w = 0.0f;
  }
}

void C_QUATInverse(const Quaternion* src, Quaternion* inv) {
  const f32 mag = C_QUATDotProduct(src, src);
  const f32 n = mag == 0.0f ? 1.0f : 1.0f / mag;
  inv->x = -src->x * n;
  inv->y = -src->y * n;
  inv->z = -src->z * n;
  inv->w = src->w * n;
}

void C_QUATDivide(const Quaternion* p, const Quaternion* q, Quaternion* r) {
  Quaternion qi;
  C_QUATInverse(q, &qi);
  C_QUATMultiply(&qi, p, r);
}

void C_QUATMtx(Quaternion* r, const Mtx m) {
  const f32 tr = m[0][0] + m[1][1] + m[2][2];
  if (tr > 0.0f) {
    f32 s = mpt_sqrtf(tr + 1.0f);
    r->w = s * 0.5f;
    s = 0.5f / s;
    r->x = (m[2][1] - m[1][2]) * s;
    r->y = (m[0][2] - m[2][0]) * s;
    r->z = (m[1][0] - m[0][1]) * s;
    return;
  }
  int i = 0;
  if (m[1][1] > m[0][0]) i = 1;
  if (m[2][2] > m[i][i]) i = 2;
  const int nxt[3] = {1, 2, 0};
  const int j = nxt[i], k = nxt[j];
  f32 s = mpt_sqrtf((m[i][i] - (m[j][j] + m[k][k])) + 1.0f);
  f32 q[3];
  q[i] = s * 0.5f;
  if (s != 0.0f) s = 0.5f / s;
  r->w = (m[k][j] - m[j][k]) * s;
  q[j] = (m[i][j] + m[j][i]) * s;
  q[k] = (m[i][k] + m[k][i]) * s;
  r->x = q[0];
  r->y = q[1];
  r->z = q[2];
}

void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
  f32 c = C_QUATDotProduct(p, q);
  f32 sign = 1.0f;
  if (c < 0.0f) {
    c = -c;
    sign = -1.0f;
  }
  f32 tp, tq;
  if (c <= 0.99999f) {
    const f32 theta = mpt_acosf(c);
    const f32 s = mpt_sinf(theta);
    tp = mpt_sinf((1.0f - t) * theta) / s;
    tq = mpt_sinf(t * theta) / s;
  } else {
    tp = 1.0f - t;
    tq = t;
  }
  tq = tq * sign;
  r->x = tp * p->x + tq * q->x;
  r->y = tp * p->y + tq * q->y;
  r->z = tp * p->z + tq * q->z;
  r->w = tp * p->w + tq * q->w;
}

void C_QUATLerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t) {
  r->x = p->x + t * (q->x - p->x);
  r->y = p->y + t * (q->y - p->y);
  r->z = p->z + t * (q->z - p->z);
  r->w = p->w + t * (q->w - p->w);
}

void C_QUATRotAxisRad(Quaternion* r, const Vec* axis, f32 rad) {
  Vec n;
  C_VECNormalize(axis, &n);
  const f32 half = rad * 0.5f;
  const f32 s = mpt_sinf(half);
  r->x = n.x * s;
  r->y = n.y * s;
  r->z = n.z * s;
  r->w = mpt_cosf(half);
}

// ---- PS entry points: the same plain-C code -------------------------------------------------
#define MPT_PS_VOID(name, params, args) \
  void PS##name params { C_##name args; }
#define MPT_PS_RET(ret, name, params, args) \
  ret PS##name params { return C_##name args; }

MPT_PS_VOID(MTXIdentity, (Mtx m), (m))
MPT_PS_VOID(MTXCopy, (const Mtx s, Mtx d), (s, d))
MPT_PS_VOID(MTXConcat, (const Mtx a, const Mtx b, Mtx ab), (a, b, ab))
MPT_PS_VOID(MTXConcatArray, (const Mtx a, const Mtx* s, Mtx* d, u32 n), (a, s, d, n))
MPT_PS_VOID(MTXTranspose, (const Mtx s, Mtx x), (s, x))
MPT_PS_RET(u32, MTXInverse, (const Mtx s, Mtx i), (s, i))
MPT_PS_RET(u32, MTXInvXpose, (const Mtx s, Mtx i), (s, i))
MPT_PS_VOID(MTXRotRad, (Mtx m, char a, f32 r), (m, a, r))
MPT_PS_VOID(MTXRotTrig, (Mtx m, char a, f32 s, f32 c), (m, a, s, c))
MPT_PS_VOID(MTXRotAxisRad, (Mtx m, const Vec* a, f32 r), (m, a, r))
MPT_PS_VOID(MTXTrans, (Mtx m, f32 x, f32 y, f32 z), (m, x, y, z))
MPT_PS_VOID(MTXTransApply, (const Mtx s, Mtx d, f32 x, f32 y, f32 z), (s, d, x, y, z))
MPT_PS_VOID(MTXScale, (Mtx m, f32 x, f32 y, f32 z), (m, x, y, z))
MPT_PS_VOID(MTXScaleApply, (const Mtx s, Mtx d, f32 x, f32 y, f32 z), (s, d, x, y, z))
MPT_PS_VOID(MTXQuat, (Mtx m, const Quaternion* q), (m, q))
MPT_PS_VOID(MTXReflect, (Mtx m, const Vec* p, const Vec* n), (m, p, n))
MPT_PS_VOID(MTXMultVec, (const Mtx m, const Vec* s, Vec* d), (m, s, d))
MPT_PS_VOID(MTXMultVecArray, (const Mtx m, const Vec* s, Vec* d, u32 n), (m, s, d, n))
MPT_PS_VOID(MTXMultVecSR, (const Mtx m, const Vec* s, Vec* d), (m, s, d))
MPT_PS_VOID(MTXMultVecArraySR, (const Mtx m, const Vec* s, Vec* d, u32 n), (m, s, d, n))
MPT_PS_VOID(MTX44Identity, (Mtx44 m), (m))
MPT_PS_VOID(MTX44Copy, (const Mtx44 s, Mtx44 d), (s, d))
MPT_PS_VOID(MTX44Concat, (const Mtx44 a, const Mtx44 b, Mtx44 ab), (a, b, ab))
MPT_PS_VOID(MTX44Transpose, (const Mtx44 s, Mtx44 x), (s, x))
MPT_PS_RET(u32, MTX44Inverse, (const Mtx44 s, Mtx44 i), (s, i))
MPT_PS_VOID(MTX44MultVec, (const Mtx44 m, const Vec* s, Vec* d), (m, s, d))
MPT_PS_VOID(VECAdd, (const Vec* a, const Vec* b, Vec* r), (a, b, r))
MPT_PS_VOID(VECSubtract, (const Vec* a, const Vec* b, Vec* r), (a, b, r))
MPT_PS_VOID(VECScale, (const Vec* s, Vec* d, f32 k), (s, d, k))
MPT_PS_VOID(VECNormalize, (const Vec* s, Vec* u), (s, u))
MPT_PS_RET(f32, VECSquareMag, (const Vec* v), (v))
MPT_PS_RET(f32, VECMag, (const Vec* v), (v))
MPT_PS_RET(f32, VECDotProduct, (const Vec* a, const Vec* b), (a, b))
MPT_PS_VOID(VECCrossProduct, (const Vec* a, const Vec* b, Vec* r), (a, b, r))
MPT_PS_RET(f32, VECSquareDistance, (const Vec* a, const Vec* b), (a, b))
MPT_PS_RET(f32, VECDistance, (const Vec* a, const Vec* b), (a, b))
MPT_PS_VOID(QUATAdd, (const Quaternion* p, const Quaternion* q, Quaternion* r), (p, q, r))
MPT_PS_VOID(QUATSubtract, (const Quaternion* p, const Quaternion* q, Quaternion* r), (p, q, r))
MPT_PS_VOID(QUATMultiply, (const Quaternion* p, const Quaternion* q, Quaternion* r), (p, q, r))
MPT_PS_VOID(QUATScale, (const Quaternion* q, Quaternion* r, f32 s), (q, r, s))
MPT_PS_RET(f32, QUATDotProduct, (const Quaternion* p, const Quaternion* q), (p, q))
MPT_PS_VOID(QUATNormalize, (const Quaternion* s, Quaternion* u), (s, u))
MPT_PS_VOID(QUATInverse, (const Quaternion* s, Quaternion* i), (s, i))
MPT_PS_VOID(QUATDivide, (const Quaternion* p, const Quaternion* q, Quaternion* r), (p, q, r))

#undef MPT_PS_VOID
#undef MPT_PS_RET

}  // extern "C"
