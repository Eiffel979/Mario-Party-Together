/* MTX/VEC/QUAT as plain C under strict FP (ADR 0007 §4, ADR 0008 §5).
 *
 * Every PS* entry point is the same plain-C code as its C_* twin: there is no
 * paired-single emulation and no SIMD whose results could differ between
 * hosts. The console's PS versions use fused multiply-add and reciprocal
 * square-root estimates, so these results do not bit-match a GameCube; that is
 * irrelevant to our determinism (mp4.md §9). Rotations use mpt_sinf/mpt_cosf.
 *
 * If P1-14 links aurora's MTX instead (ADR 0007 §6), these remain the fallback
 * named by ADR 0008 §5 for the three PSVEC* reciprocal-square-root functions.
 */
#ifndef MPT_GC_MTX_H
#define MPT_GC_MTX_H

#include "mpt_gc/types.h"

MPT_GC_EXTERN_C_BEGIN

typedef f32 Mtx[3][4];
typedef f32 (*MtxPtr)[4];
typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44Ptr)[4];
typedef struct Vec { f32 x, y, z; } Vec, *VecPtr;
typedef const Vec* CVecPtr;
typedef struct Quaternion { f32 x, y, z, w; } Quaternion, *QuaternionPtr;

#define MPT_GC_MTX_PAIR(ret, name, args) ret C_##name args; ret PS##name args;

/* 3x4 matrices */
MPT_GC_MTX_PAIR(void, MTXIdentity, (Mtx m))
MPT_GC_MTX_PAIR(void, MTXCopy, (const Mtx src, Mtx dst))
MPT_GC_MTX_PAIR(void, MTXConcat, (const Mtx a, const Mtx b, Mtx ab))
MPT_GC_MTX_PAIR(void, MTXConcatArray, (const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count))
MPT_GC_MTX_PAIR(void, MTXTranspose, (const Mtx src, Mtx xPose))
MPT_GC_MTX_PAIR(u32, MTXInverse, (const Mtx src, Mtx inv))
MPT_GC_MTX_PAIR(u32, MTXInvXpose, (const Mtx src, Mtx invX))
MPT_GC_MTX_PAIR(void, MTXRotRad, (Mtx m, char axis, f32 rad))
MPT_GC_MTX_PAIR(void, MTXRotTrig, (Mtx m, char axis, f32 sinA, f32 cosA))
MPT_GC_MTX_PAIR(void, MTXRotAxisRad, (Mtx m, const Vec* axis, f32 rad))
MPT_GC_MTX_PAIR(void, MTXTrans, (Mtx m, f32 xT, f32 yT, f32 zT))
MPT_GC_MTX_PAIR(void, MTXTransApply, (const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT))
MPT_GC_MTX_PAIR(void, MTXScale, (Mtx m, f32 xS, f32 yS, f32 zS))
MPT_GC_MTX_PAIR(void, MTXScaleApply, (const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS))
MPT_GC_MTX_PAIR(void, MTXQuat, (Mtx m, const Quaternion* q))
MPT_GC_MTX_PAIR(void, MTXReflect, (Mtx m, const Vec* p, const Vec* n))
MPT_GC_MTX_PAIR(void, MTXMultVec, (const Mtx m, const Vec* src, Vec* dst))
MPT_GC_MTX_PAIR(void, MTXMultVecArray, (const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count))
MPT_GC_MTX_PAIR(void, MTXMultVecSR, (const Mtx m, const Vec* src, Vec* dst))
MPT_GC_MTX_PAIR(void, MTXMultVecArraySR, (const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count))

void C_MTXLookAt(Mtx m, const Vec* camPos, const Vec* camUp, const Vec* target);
void C_MTXLightFrustum(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 scaleS, f32 scaleT, f32 transS, f32 transT);
void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, f32 scaleS, f32 scaleT, f32 transS, f32 transT);
void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, f32 scaleS, f32 scaleT, f32 transS, f32 transT);

/* 4x4 projection matrices */
void C_MTXFrustum(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f);
void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
MPT_GC_MTX_PAIR(void, MTX44Identity, (Mtx44 m))
MPT_GC_MTX_PAIR(void, MTX44Copy, (const Mtx44 src, Mtx44 dst))
MPT_GC_MTX_PAIR(void, MTX44Concat, (const Mtx44 a, const Mtx44 b, Mtx44 ab))
MPT_GC_MTX_PAIR(void, MTX44Transpose, (const Mtx44 src, Mtx44 xPose))
MPT_GC_MTX_PAIR(u32, MTX44Inverse, (const Mtx44 src, Mtx44 inv))
MPT_GC_MTX_PAIR(void, MTX44MultVec, (const Mtx44 m, const Vec* src, Vec* dst))

/* Vectors */
MPT_GC_MTX_PAIR(void, VECAdd, (const Vec* a, const Vec* b, Vec* ab))
MPT_GC_MTX_PAIR(void, VECSubtract, (const Vec* a, const Vec* b, Vec* a_b))
MPT_GC_MTX_PAIR(void, VECScale, (const Vec* src, Vec* dst, f32 scale))
MPT_GC_MTX_PAIR(void, VECNormalize, (const Vec* src, Vec* unit))
MPT_GC_MTX_PAIR(f32, VECSquareMag, (const Vec* v))
MPT_GC_MTX_PAIR(f32, VECMag, (const Vec* v))
MPT_GC_MTX_PAIR(f32, VECDotProduct, (const Vec* a, const Vec* b))
MPT_GC_MTX_PAIR(void, VECCrossProduct, (const Vec* a, const Vec* b, Vec* axb))
MPT_GC_MTX_PAIR(f32, VECSquareDistance, (const Vec* a, const Vec* b))
MPT_GC_MTX_PAIR(f32, VECDistance, (const Vec* a, const Vec* b))
void C_VECHalfAngle(const Vec* a, const Vec* b, Vec* half);
void C_VECReflect(const Vec* src, const Vec* normal, Vec* dst);

/* Quaternions */
MPT_GC_MTX_PAIR(void, QUATAdd, (const Quaternion* p, const Quaternion* q, Quaternion* r))
MPT_GC_MTX_PAIR(void, QUATSubtract, (const Quaternion* p, const Quaternion* q, Quaternion* r))
MPT_GC_MTX_PAIR(void, QUATMultiply, (const Quaternion* p, const Quaternion* q, Quaternion* pq))
MPT_GC_MTX_PAIR(void, QUATScale, (const Quaternion* q, Quaternion* r, f32 scale))
MPT_GC_MTX_PAIR(f32, QUATDotProduct, (const Quaternion* p, const Quaternion* q))
MPT_GC_MTX_PAIR(void, QUATNormalize, (const Quaternion* src, Quaternion* unit))
MPT_GC_MTX_PAIR(void, QUATInverse, (const Quaternion* src, Quaternion* inv))
MPT_GC_MTX_PAIR(void, QUATDivide, (const Quaternion* p, const Quaternion* q, Quaternion* r))
void C_QUATMtx(Quaternion* r, const Mtx m);
void C_QUATSlerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t);
void C_QUATRotAxisRad(Quaternion* r, const Vec* axis, f32 rad);
void C_QUATLerp(const Quaternion* p, const Quaternion* q, Quaternion* r, f32 t);

#undef MPT_GC_MTX_PAIR

MPT_GC_EXTERN_C_END

#endif /* MPT_GC_MTX_H */
