/* P1-15 unit check: two REL-local symbols with the same name resolve independently, and linking an
 * overlay again restores its .data and zeroes its .bss (mpt_mp4_rel.h, tools/MptMp4Rel.cmake). */
#include <stdio.h>
#include <string.h>

#include "mpt_mp4_rel.h"

int mpt_rel_rel_a__shared_name(void);
int mpt_rel_rel_b__shared_name(void);
int mpt_rel_rel_b__counter_plus(int n);
extern int mpt_rel_rel_a__setup_calls;

static int failures = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

int main(void) {
  CHECK(mpt_mp4_rel_count == 2);
  CHECK(mpt_mp4_rel_init());
  CHECK(mpt_mp4_rel_link("no_such_rel") == NULL);

  const MptMp4RelInfo* a = mpt_mp4_rel_link("rel_a");
  const MptMp4RelInfo* b = mpt_mp4_rel_link("rel_b");
  CHECK(a != NULL && b != NULL && a != b);
  if (a == NULL || b == NULL) return 1;
  CHECK(strcmp(a->name, "rel_a") == 0);
  CHECK(a->object_setup != NULL);
  CHECK(b->object_setup == NULL);
  CHECK(a->data_hi > a->data_lo && b->data_hi > b->data_lo);
  CHECK(a->data_hi <= b->data_lo || b->data_hi <= a->data_lo); /* disjoint ranges */

  /* Same names, separate definitions. */
  CHECK(mpt_rel_rel_a__shared_name() == 600);
  CHECK(mpt_rel_rel_a__shared_name() == 701);
  CHECK(mpt_rel_rel_b__shared_name() == 5100);
  CHECK(mpt_rel_rel_b__counter_plus(1) == 52);
  mpt_mp4_rel_object_setup(a);
  CHECK(mpt_rel_rel_a__setup_calls == 1);

  /* A fresh link restores .data (counter, setup_calls) and zeroes .bss (scratch); B is untouched. */
  mpt_mp4_rel_unlink(a);
  CHECK(mpt_mp4_rel_link("rel_a") == a);
  CHECK(mpt_rel_rel_a__setup_calls == 0);
  CHECK(mpt_rel_rel_a__shared_name() == 600);
  CHECK(mpt_rel_rel_b__shared_name() == 5201);

  if (failures == 0) printf("rel isolation: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
