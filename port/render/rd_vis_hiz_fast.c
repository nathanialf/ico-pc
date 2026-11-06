/* rd_vis_hiz_fast.c: what the occlusion test's collect keeps, so the same
 * occluders cost less (rd_vis.h; docs/port/RENDER.md, "Culling", the
 * occlusion test's cost).
 *
 * The collect cache.  rd_vis.c hizCollect reads every opaque mesh draw of
 * list 0 for its occluder triangles once a picture (rd__vis_hiz_mesh_ex).
 * Those triangles are a function of what the draw reads and nothing else:
 * the mesh's stream, index list, batches and marks (each new only with a
 * new stream serial, rd__mesh_stream_written: the creation, the morphs,
 * the interpolation's scratch copies and their blend, and the marks copied
 * with them), the sheet's tile bounds, the draw's batch range and clip
 * mode, VU memory 16..23 (the matrix, code 36's clip matrix), minArea (the
 * frame's size), the extended state (rd__vu_extended: a sheet's tiles) and
 * the pass (the full one reads *lbAll and every triangle's exactness too).
 * A draw whose key is byte for byte the same as one kept takes the kept
 * triangles: a still camera's picture, a picture made again (a pause, a
 * re-shown present), costs no vertex pass.  The key holds every input, so
 * a hit gives exactly what the pass would; the ids and pointers sit beside
 * the serial so a record made again in the same slot never matches.
 *
 * The scan memo.  An occluder whose texture is not uploaded yet takes the
 * alpha scan the upload will make from the CPU copy
 * (rd__vis_hiz_pending_scan: every texel of every level); the draws of
 * one texture in a picture scan it once (rd__vis_hiz_collect_begin starts
 * a picture's memo and the cache's age). */
#include <stdlib.h>
#include <string.h>

#include "rd_vis.h"
#include "xxh3.h"

/* ------------------------------------------------------- the collect cache */

/* every input of a draw's triangles (memset first: the padding compared
 * too) */
typedef struct CacheKey {
    uint64_t serial;
    const void *stream, *index, *batches, *marks;
    double minArea;
    float mem[8][4];
    uint32_t meshId, firstBatch, batchCount, vertexCount, qpv, indexCount, batchN, markCount;
    uint32_t tiledFrom, tiledIndexFrom;
    uint8_t clip, extended, full, pad;
} CacheKey;

typedef struct CacheEntry {
    CacheKey key;
    RdVisHizTri *t;
    uint32_t n, cap, lbAll, used; /* used: the collect that last read or wrote it, 0 none */
    int all;
} CacheEntry;

/* sets of CACHE_WAYS entries; a key goes to the set of its hash, into a
 * free way or the way read longest ago */
#define CACHE_SETS 512u
#define CACHE_WAYS 4u

static struct {
    CacheEntry *e;  /* CACHE_SETS * CACHE_WAYS, made on the first put */
    uint32_t total; /* the triangles kept over every entry */
    uint32_t now;   /* the collect (rd__vis_hiz_collect_begin) */
} s_cache;

static bool keyOf(CacheKey *k, uint32_t meshId, const RdMeshRec *m, const RdVuPayload *p,
                  const float (*mem)[4], double minArea, bool full)
{
    memset(k, 0, sizeof(*k));
    if (!m || m->streamSerial == 0) {
        return false; /* a record never written through the serial: no version to key */
    }
    k->serial = m->streamSerial;
    k->stream = m->stream;
    k->index = m->index;
    k->batches = m->batches;
    k->marks = m->kickMark;
    k->minArea = minArea;
    memcpy(k->mem, &mem[16], sizeof(k->mem));
    k->meshId = meshId;
    k->firstBatch = p->firstBatch;
    k->batchCount = p->batchCount;
    k->vertexCount = m->vertexCount;
    k->qpv = m->qwPerVertex;
    k->indexCount = m->indexCount;
    k->batchN = m->batchCount;
    k->markCount = m->markCount;
    k->tiledFrom = m->tiledFrom;
    k->tiledIndexFrom = m->tiledIndexFrom;
    k->clip = p->clip;
    k->extended = rd__vu_extended() ? 1u : 0u;
    k->full = full ? 1u : 0u;
    return true;
}

static CacheEntry *setOf(const CacheKey *k)
{
    return &s_cache.e[(xxh3_64(k, sizeof(*k)) % CACHE_SETS) * CACHE_WAYS];
}

static void drop(CacheEntry *e)
{
    s_cache.total -= e->n;
    e->n = 0;
    e->used = 0;
}

bool rd__vis_hiz_cache_get(uint32_t meshId, const RdMeshRec *m, const RdVuPayload *p,
                           const float (*mem)[4], double minArea, bool full, uint32_t cmd,
                           RdVisHizList *out, uint32_t *lbAll, int *all)
{
    CacheKey k;
    if (!s_cache.e || !keyOf(&k, meshId, m, p, mem, minArea, full)) {
        return false;
    }
    CacheEntry *set = setOf(&k), *e = NULL;
    for (uint32_t w = 0; w < CACHE_WAYS && !e; w++) {
        if (set[w].used && memcmp(&set[w].key, &k, sizeof(k)) == 0) {
            e = &set[w];
        }
    }
    if (!e) {
        return false;
    }
    const uint32_t first = out->n;
    for (uint32_t i = 0; i < e->n; i++) {
        RdVisHizTri t = e->t[i];
        t.cmd = cmd;
        t.sel = 1;
        if (!rd__vis_hiz_list_push(out, &t)) {
            out->n = first;
            return false; /* the pass then runs, and finds no memory itself */
        }
    }
    e->used = s_cache.now;
    *lbAll = e->lbAll;
    *all = e->all;
    return true;
}

void rd__vis_hiz_cache_put(uint32_t meshId, const RdMeshRec *m, const RdVuPayload *p,
                           const float (*mem)[4], double minArea, bool full,
                           const RdVisHizList *out, uint32_t first, uint32_t lbAll, int all)
{
    CacheKey k;
    if (all < 0 || first > out->n || !keyOf(&k, meshId, m, p, mem, minArea, full)) {
        return;
    }
    const uint32_t n = out->n - first;
    if (n > RD_VIS_HIZ_CACHE_TRIS) {
        return;
    }
    if (!s_cache.e) {
        s_cache.e = calloc((size_t)CACHE_SETS * CACHE_WAYS, sizeof(CacheEntry));
        if (!s_cache.e) {
            return;
        }
    }
    /* the way: the key's own, a free one, or the one read longest ago */
    CacheEntry *set = setOf(&k), *e = NULL;
    for (uint32_t w = 0; w < CACHE_WAYS && !e; w++) {
        if (set[w].used && memcmp(&set[w].key, &k, sizeof(k)) == 0) {
            e = &set[w];
        }
    }
    for (uint32_t w = 0; w < CACHE_WAYS && !e; w++) {
        e = set[w].used ? NULL : &set[w];
    }
    if (!e) {
        e = &set[0];
        for (uint32_t w = 1; w < CACHE_WAYS; w++) {
            e = set[w].used < e->used ? &set[w] : e;
        }
    }
    if (e->used) {
        drop(e);
    }
    if (s_cache.total + n > RD_VIS_HIZ_CACHE_TRIS) {
        /* past the size: the entries no draw of this collect read go */
        for (uint32_t i = 0; i < CACHE_SETS * CACHE_WAYS; i++) {
            if (s_cache.e[i].used && s_cache.e[i].used != s_cache.now) {
                drop(&s_cache.e[i]);
            }
        }
        if (s_cache.total + n > RD_VIS_HIZ_CACHE_TRIS) {
            return; /* this collect's own are more: not kept */
        }
    }
    if (n > e->cap) {
        RdVisHizTri *g = realloc(e->t, (size_t)n * sizeof(RdVisHizTri));
        if (!g) {
            return;
        }
        e->t = g;
        e->cap = n;
    }
    if (n) {
        memcpy(e->t, out->t + first, (size_t)n * sizeof(RdVisHizTri));
    }
    s_cache.total += n;
    e->key = k;
    e->n = n;
    e->lbAll = lbAll;
    e->all = all;
    e->used = s_cache.now ? s_cache.now : 1u;
}

void rd__vis_hiz_cache_clear(void)
{
    if (s_cache.e) {
        for (uint32_t i = 0; i < CACHE_SETS * CACHE_WAYS; i++) {
            free(s_cache.e[i].t);
        }
        free(s_cache.e);
    }
    memset(&s_cache, 0, sizeof(s_cache));
}

/* ---------------------------------------------------------- the scan memo */

#define SCAN_SLOTS 256u

typedef struct ScanEntry {
    uint32_t id, stamp;
    bool have;
    RdTexAlphaScan scan;
} ScanEntry;

static struct {
    ScanEntry e[SCAN_SLOTS];
    uint32_t now;
} s_scan;

void rd__vis_hiz_collect_begin(void)
{
    if (++s_scan.now == 0u) {
        memset(s_scan.e, 0, sizeof(s_scan.e));
        s_scan.now = 1u;
    }
    if (++s_cache.now == 0u) {
        /* every entry's age from 1 again (a wrap after 2^32 collects) */
        for (uint32_t i = 0; s_cache.e && i < CACHE_SETS * CACHE_WAYS; i++) {
            s_cache.e[i].used = s_cache.e[i].used ? 1u : 0u;
        }
        s_cache.now = 2u;
    }
}

bool rd__vis_hiz_scan_memo(uint32_t id, RdTexAlphaScan *out)
{
    if (s_scan.now == 0u) {
        return rd__vis_hiz_pending_scan(id, out); /* no collect begun */
    }
    ScanEntry *e = &s_scan.e[(id * 2654435761u) >> 24];
    if (e->stamp == s_scan.now && e->id == id) {
        *out = e->scan;
        return e->have;
    }
    memset(out, 0, sizeof(*out));
    const bool have = rd__vis_hiz_pending_scan(id, out);
    e->id = id;
    e->stamp = s_scan.now;
    e->have = have;
    e->scan = *out;
    return have;
}
