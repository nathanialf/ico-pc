/* rd_vu_skin_trace.c: the skin check's stamps and its search for the bytes
 * a wrong GPU copy holds (rd_vu_skin_check.h "the trace"; issue 123;
 * docs/port/VU_SKIN_CHECK.md).  Pure: no device, so the judge test runs
 * every verdict without one.
 *
 * Issue 123's v0.11.2 log (an Adreno 740 on Qualcomm's driver): a check
 * whose 403 vertices were all wrong, its fresh-offset variant wrong too,
 * the GPU's copy of the bone block differing at qwords 0..195 (every qword
 * the block uses) while the ring read back right after the fence.  Three
 * paths give that and the log could not tell them apart: the readback not
 * written by the replay (its rows and copy an earlier check's), the GPU
 * reading the ring as an earlier replay left it, or the GPU reading other
 * memory.  The stamps split the first two from the third; the search names
 * the block a wrong copy holds, or says it matches none. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_vu_skin_check.h"

void rd__skin_clear_stamp(uint32_t replay, float out[4])
{
    out[0] = (float)RD_SKIN_CLEAR_TAG;
    out[1] = (float)(replay & 0xFFu);
    out[2] = (float)((replay >> 8) & 0xFFu);
    out[3] = (float)((replay >> 16) & 0xFFu);
}

bool rd__skin_clear_stamp_read(const uint8_t texel[4], uint32_t *replay24)
{
    if (texel[0] != RD_SKIN_CLEAR_TAG) {
        return false;
    }
    *replay24 = (uint32_t)texel[1] | ((uint32_t)texel[2] << 8) | ((uint32_t)texel[3] << 16);
    return true;
}

int rd__skin_clear_stamp_state(const uint8_t texel[4], uint32_t replay, uint32_t *seen)
{
    uint32_t r = 0;
    *seen = 0;
    if (!rd__skin_clear_stamp_read(texel, &r)) {
        return RD_SKIN_STAMP_NONE;
    }
    *seen = r;
    return r == (replay & 0xFFFFFFu) ? RD_SKIN_STAMP_OK : RD_SKIN_STAMP_OLD;
}

int rd__skin_ring_stamp_state(const void *bytes16, uint32_t replay, uint32_t slot, uint32_t ringId,
                              uint32_t *seen)
{
    RdSkinRingStamp s;
    memcpy(&s, bytes16, sizeof(s));
    *seen = 0;
    if (s.magic != RD_SKIN_RING_STAMP_MAGIC) {
        return RD_SKIN_STAMP_NONE;
    }
    *seen = s.replay;
    return s.replay == replay && s.slot == slot && s.ringId == ringId ? RD_SKIN_STAMP_OK
                                                                      : RD_SKIN_STAMP_OLD;
}

static void stampWord(int state, uint32_t seen, const char *own, char *out, size_t size)
{
    if (state == RD_SKIN_STAMP_OK) {
        snprintf(out, size, "%s", own);
    } else if (state == RD_SKIN_STAMP_OLD) {
        snprintf(out, size, "replay %u's", seen);
    } else {
        snprintf(out, size, "no stamp");
    }
}

void rd__skin_stamp_words(int clearState, uint32_t clearSeen, int ringState, uint32_t ringSeen,
                          char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (clearState == RD_SKIN_STAMP_OK && ringState == RD_SKIN_STAMP_OK) {
        return;
    }
    char cw[32], rw[32];
    stampWord(clearState, clearSeen, "this replay's", cw, sizeof(cw));
    stampWord(ringState, ringSeen, "this replay's", rw, sizeof(rw));
    if (clearState != RD_SKIN_STAMP_OK) {
        /* the rows the GPU drew are not this replay's: nothing it copied is */
        snprintf(out, size,
                 "the readback was not written by this replay's copies (the probe target's "
                 "clear stamp: %s (bits 0..23); the ring's stamp: %s): the GPU did not run them, "
                 "or they were read before they landed",
                 cw, rw);
    } else {
        /* the rows are this replay's, the ring's stamp is not: the copy ran
         * and read the ring as another replay left it */
        snprintf(out, size,
                 "the copies ran (the clear stamp is this replay's) but the ring's stamp reads "
                 "%s: the GPU read the ring's memory as it was before this replay wrote it",
                 rw);
    }
}

/* a cheap 64-bit mix of the block's words (equal blocks, equal hashes;
 * the search compares the bytes after a hash agrees) */
uint64_t rd__skin_block_hash(const void *bytes, uint32_t n)
{
    const uint8_t *p = bytes;
    uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
    uint32_t i = 0;
    for (; i + 8u <= n; i += 8u) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = (h ^ w) * 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    for (; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

void rd__skin_trace_note(RdSkinTrace *t, const RdSkinTraceEntry *e)
{
    t->e[t->next] = *e;
    t->next = (t->next + 1u) % RD_SKIN_TRACE_KEEP;
    if (t->count < RD_SKIN_TRACE_KEEP) {
        t->count++;
    }
}

/* the ring's block at an aligned offset equal to gpu: the first found (the
 * draw's own offset first, then in order), within used and scanMax */
static bool ringFind(const uint8_t *ring, uint64_t used, uint64_t scanMax, uint64_t align,
                     uint64_t own, const uint8_t *gpu, uint32_t bytes, uint64_t *at,
                     uint64_t *scanned)
{
    *scanned = 0;
    const uint64_t lim = used < scanMax ? used : scanMax;
    if (!ring || lim < bytes || align == 0 || bytes < 16u) {
        return false;
    }
    if (own % align == 0 && own + bytes <= used && memcmp(ring + own, gpu, bytes) == 0) {
        *at = own;
        *scanned = bytes;
        return true;
    }
    for (uint64_t o = 0; o + bytes <= lim; o += align) {
        *scanned = o + bytes;
        /* the first 16 bytes before the whole block: the ring's memory may
         * be uncached on the device (a read is slow) */
        if (memcmp(ring + o, gpu, 16) == 0 && memcmp(ring + o, gpu, bytes) == 0) {
            *at = o;
            return true;
        }
    }
    return false;
}

void rd__skin_copy_find(const RdSkinCopyFind *in, RdSkinCopyFound *out)
{
    memset(out, 0, sizeof(*out));
    const uint32_t nq = in->bytes / 16u;
    bool zero = true;
    for (uint32_t q = 0; q < nq; q++) {
        static const uint8_t z[16];
        const bool qz = memcmp(in->gpu + q * 16u, z, 16) == 0;
        out->zeroQw += qz;
        zero = zero && qz;
        out->keptQw += in->kept && memcmp(in->gpu + q * 16u, in->kept + q * 16u, 16) == 0;
        for (uint32_t l = 0; l < 4u; l++) {
            float f;
            memcpy(&f, in->gpu + q * 16u + l * 4u, 4);
            out->nonFinite += !isfinite(f);
        }
    }
    if (in->prev && memcmp(in->prev, in->gpu, in->bytes) == 0) {
        out->kind = RD_SKIN_FOUND_PREV;
        out->replay = in->prevReplay;
        out->slot = in->slot;
        return;
    }
    if (zero) {
        out->kind = RD_SKIN_FOUND_ZERO;
        return;
    }
    if (in->trace && in->trace->count) {
        const uint64_t h = rd__skin_block_hash(in->gpu, in->bytes);
        /* newest first */
        for (uint32_t k = 0; k < in->trace->count; k++) {
            const uint32_t i = (in->trace->next + RD_SKIN_TRACE_KEEP - 1u - k) % RD_SKIN_TRACE_KEEP;
            const RdSkinTraceEntry *e = &in->trace->e[i];
            if (e->hash != h || (e->replay == in->replay && e->off == in->off)) {
                continue; /* the check's own block is the bytes kept */
            }
            out->kind = RD_SKIN_FOUND_TRACE;
            out->replay = e->replay;
            out->slot = e->slot;
            out->off = e->off;
            out->meshId = e->meshId;
            out->list = e->list;
            out->index = e->index;
            memcpy(out->mesh, e->mesh, sizeof(out->mesh));
            out->sameOff = e->off == in->off;
            return;
        }
    }
    /* the check's own slot first, then the other */
    for (uint32_t k = 0; k < 2u; k++) {
        const uint32_t s = k == 0 ? in->slot % 2u : (in->slot + 1u) % 2u;
        uint64_t at = 0, scanned = 0;
        const bool hit = ringFind(in->ring[s], in->ringUsed[s], in->scanMax ? in->scanMax : ~0ull,
                                  in->align, in->off, in->gpu, in->bytes, &at, &scanned);
        out->scanned += scanned;
        if (hit) {
            out->kind = RD_SKIN_FOUND_RING;
            out->slot = s;
            out->off = at;
            out->replay = in->ringReplay[s];
            out->sameOff = at == in->off;
            return;
        }
    }
}

void rd__skin_copy_words(const RdSkinCopyFound *f, uint32_t bytes, char *out, size_t size)
{
    if (size == 0) {
        return;
    }
    const uint32_t nq = bytes / 16u;
    char tail[192];
    snprintf(tail, sizeof(tail),
             "; %u of %u qwords zero, %u of %u qwords as kept, %u of %u lanes not finite; %llu KB "
             "of the rings searched",
             f->zeroQw, nq, f->keptQw, nq, f->nonFinite, nq * 4u,
             (unsigned long long)(f->scanned >> 10));
    switch (f->kind) {
    case RD_SKIN_FOUND_PREV:
        snprintf(out, size,
                 "the bytes the slot's readback held from the check of replay %u (not written "
                 "again)%s",
                 f->replay, tail);
        break;
    case RD_SKIN_FOUND_ZERO:
        snprintf(out, size, "all zeros%s", tail);
        break;
    case RD_SKIN_FOUND_TRACE:
        snprintf(out, size,
                 "the block checked in replay %u (frame slot %u, ring offset %llu%s; mesh %s #%u, "
                 "command %u:%u): another replay's block%s",
                 f->replay, f->slot, (unsigned long long)f->off,
                 f->sameOff ? ", the draw's own offset" : "", f->mesh, f->meshId, f->list, f->index,
                 tail);
        break;
    case RD_SKIN_FOUND_RING:
        snprintf(out, size,
                 "the block at ring offset %llu of frame slot %u's ring as it is now (replay "
                 "%u's%s)%s",
                 (unsigned long long)f->off, f->slot, f->replay,
                 f->sameOff ? ", the draw's own offset" : "", tail);
        break;
    default:
        snprintf(out, size, "no block the check kept or the rings hold: other memory%s", tail);
        break;
    }
}
