/* ADR 0008, "Gate status after the implementation", decision point 1 (Party Board patch 0029):
 * SetRevMtx (src/game/EnvelopeExec.c, run by InitEnvelope) must not read uninitialised stack when the
 * first matrix it inverts is singular. Before the patch MTXInverse left its local untouched and the
 * MTXConcat after it copied whatever the stack held (on PC, host return addresses) into the model's
 * reverse matrices. A hand-made two-object model, no dump, no game data: object 0 is a null object
 * outside the hierarchy, whose matrix is all zero (singular), object 1 the mesh at the root. The stack
 * under the call is filled with a non-zero pattern first. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/EnvelopeExec.h"
#include "game/hsfformat.h"

/* hsfex.c's, called only by EnvelopeProc's paths, which this test does not run. */
void Hu3DMtxScaleGet(Mtx arg0, Vec *arg1) {
  (void)arg0;
  (void)arg1;
  fprintf(stderr, "envelope test: Hu3DMtxScaleGet reached\n");
  exit(2);
}

static int g_failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

/* Leaves a pattern in the stack below the caller's frame, where InitEnvelope's locals will lie. */
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
static void dirtyStack(void) {
  volatile unsigned char junk[16 * 1024];
  for (size_t i = 0; i < sizeof junk; ++i) junk[i] = (unsigned char)(0xA5 ^ (i * 7));
}

static int isZero(const Mtx m) {
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      if (m[r][c] != 0.0f) return 0;
  return 1;
}

static int isIdentity(const Mtx m) {
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c)
      if (m[r][c] != (r == c ? 1.0f : 0.0f)) return 0;
  return 1;
}

int main(void) {
  static HSFOBJECT objects[2];
  static HuVecF vertices[1];
  static Mtx matrices[4]; /* nMesh (0) + nObj (2) + nObj * meshes (2 * 1) */
  static HSFMATRIX matrix;
  static HSFDATA hsf;
  static char null_name[] = "null", mesh_name[] = "mesh";

  memset(objects, 0, sizeof objects);
  memset(matrices, 0, sizeof matrices);
  objects[0].name = null_name;
  objects[0].type = 0; /* not a mesh, not under the root: SetMtx never writes its matrix */
  objects[1].name = mesh_name;
  objects[1].type = 2;
  objects[1].mesh.vtxtop = vertices;
  objects[1].mesh.base.scale.x = objects[1].mesh.base.scale.y = objects[1].mesh.base.scale.z = 1.0f;
  matrix.base_idx = 0;
  matrix.count = 2;
  matrix.data = matrices;
  hsf.object = objects;
  hsf.objectNum = 2;
  hsf.root = &objects[1];
  hsf.matrix = &matrix;
  hsf.cenvNum = 1;
  hsf.skeletonNum = 0;

  dirtyStack();
  InitEnvelope(&hsf);

  CHECK(isZero(matrices[0]));     /* the null object's matrix: singular, untouched */
  CHECK(isIdentity(matrices[1])); /* the root mesh at the origin */
  /* Reverse matrices for the mesh: inverse(object 0) * mesh, where the inverse failed and the local
   * starts as zero, so a zero matrix rather than stack bytes; then inverse(mesh) * mesh. */
  CHECK(isZero(matrices[2]));
  CHECK(isIdentity(matrices[3]));

  printf("mp4_envelope: %d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
