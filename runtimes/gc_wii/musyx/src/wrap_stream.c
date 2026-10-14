/* Compiles upstream stream.c unchanged and exposes its stream table to the
 * engine hash, which reads it by value. */
#include "stream.c"

#include "musyx_glue.h"

void mpt_musyx_hash_streams(const MptMusyxHasher* h) {
  h->put(h->self, nextPublicID);
  for (u32 i = 0; i < STREAM_MAX_SLOTS; ++i) {
    const STREAM_INFO* s = &streamInfo[i];
    h->put(h->self, s->state | ((uint64_t)s->type << 8) | ((uint64_t)s->prio << 16) | ((uint64_t)s->vol << 24) |
                        ((uint64_t)s->pan << 32) | ((uint64_t)s->span << 40) | ((uint64_t)s->auxa << 48) |
                        ((uint64_t)s->auxb << 56));
    if (!s->state) continue;
    h->put(h->self, s->stid);
    h->put(h->self, s->flags);
    h->put(h->self, s->size | ((uint64_t)s->bytes << 32));
    h->put(h->self, s->last | ((uint64_t)s->frq << 32));
    h->put(h->self, (uint64_t)s->voice | ((uint64_t)s->user << 32));
    h->put(h->self, s->studio);
    h->put(h->self, h->ptr(h->self, s->buffer));
    h->put(h->self, h->fn(h->self, (const void*)s->updateFunction));
  }
}
