/* rd_hash.c: the per-draw lookups' hash (rd_hash.h).
 *
 * Every draw that binds a DrawCB looks its 144 bytes up in the uniform
 * blocks' cache (rd_replay.c uniformOffset), and every draw looks its
 * pipeline key up in the pipeline index (rd_pipeline.c keyHash).  Both
 * hashed with FNV-1a a byte at a time: each byte a multiply on the one
 * before, about 576 dependent cycles for a DrawCB at 4 a step
 * (build-host/tmp/v0115/design/OPENING_SPLIT.md C.2, an estimate).  Here
 * the same bytes go 8 at a time (memcpy loads: any alignment, no aliasing),
 * each word a multiply and a shift on the one before: 18 steps for a DrawCB.
 * The hash picks a slot only; equality is still the bytes' (memcmp,
 * keyEqual), so the values every draw reads are the same, and only which
 * blocks share a slot (which repeats are written to the ring once more)
 * moves. */
#include <string.h>

#include "rd_hash.h"

#define K_STEP 0xFF51AFD7ED558CCDull
#define K_MIX 0xC4CEB9FE1A85EC53ull

uint32_t rd__hash_words(const void *p, size_t n, uint32_t seed)
{
    const unsigned char *b = p;
    uint64_t h = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed << 32) ^ (uint64_t)n;
    size_t i = 0;
    for (; i + 8u <= n; i += 8u) {
        uint64_t w;
        memcpy(&w, b + i, 8u);
        h = (h ^ w) * K_STEP;
        h ^= h >> 32;
    }
    if (i < n) {
        uint64_t w = 0;
        memcpy(&w, b + i, n - i);
        h = (h ^ w) * K_STEP;
        h ^= h >> 32;
    }
    h ^= h >> 33;
    h *= K_MIX;
    h ^= h >> 33;
    return (uint32_t)h;
}
