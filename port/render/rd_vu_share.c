/* rd_vu_share.c: shared vertices for clip-mode-none strips: the shared
 * list's making, a draw's verdict, the counts and their texts
 * (rd_vu_share.h says what and why). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_share.h"
#include "shader_consts.h"

bool rd_vu_share_word(const char *e)
{
    return !(e != NULL && (strcmp(e, "0") == 0 || strcmp(e, "off") == 0));
}

bool rd__vu_share_on(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        s_on = rd_vu_share_word(getenv("ICO_RD_VU_SHARE")) ? 1 : 0;
    }
    return s_on != 0;
}

void rd__vu_share_free(RdMeshRec *m)
{
    if (m == NULL) {
        return;
    }
    free(m->indexShared);
    free(m->shareUnique);
    m->indexShared = NULL;
    m->shareUnique = NULL;
}

/* the form rd_vu_share.h names: batches back to back in index order, whole
 * triangles, vertex ranges in order and apart */
static bool batchesInOrder(const RdMeshRec *m)
{
    uint32_t at = 0, vEnd = 0;
    for (uint32_t b = 0; b < m->batchCount; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        if (br->firstIndex != at || br->indexCount % 3u != 0u || br->firstVertex < vEnd ||
            (uint64_t)br->firstVertex + br->vertexCount > m->vertexCount) {
            return false;
        }
        at += br->indexCount;
        vEnd = br->firstVertex + br->vertexCount;
    }
    return at == m->indexCount;
}

void rd__vu_share_build(RdMeshRec *m)
{
    rd__vu_share_free(m);
    if (m == NULL || !m->vu || m->index == NULL || m->batches == NULL || m->batchCount == 0 ||
        m->indexCount < 3u || m->indexCount % 3u != 0u || m->tiledFrom != 0u ||
        m->vertexCount < 3u || m->vertexCount >= ICO_VU_INDEX_KICK_LIMIT || !batchesInOrder(m)) {
        return;
    }
    uint32_t *shared = malloc((size_t)m->indexCount * 4u);
    uint32_t *unique = calloc(m->batchCount, sizeof(uint32_t));
    uint32_t *canon = malloc((size_t)m->vertexCount * 4u);
    uint8_t *kicked = calloc(m->vertexCount, 1);
    bool ok = shared && unique && canon && kicked;
    uint64_t distinct = 0;
    for (uint32_t b = 0; ok && b < m->batchCount; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        const uint32_t i0 = br->firstIndex, i1 = br->firstIndex + br->indexCount;
        const uint32_t v0 = br->firstVertex, v1 = br->firstVertex + br->vertexCount;
        /* the form, and the batch's kicks */
        for (uint32_t i = i0; ok && i < i1; i += 3u) {
            const uint32_t k = m->index[i] >> 2;
            ok = m->index[i] == ICO_VU_INDEX(k, 0u) && m->index[i + 1u] == ICO_VU_INDEX(k, 1u) &&
                 m->index[i + 2u] == ICO_VU_INDEX(k, 2u) && k >= 2u && k - 2u >= v0 && k < v1 &&
                 kicked[k] == 0u;
            if (ok) {
                kicked[k] = 1u;
            }
        }
        for (uint32_t v = v0; ok && v < v1; v++) {
            canon[v] = UINT32_MAX;
        }
        /* each vertex's value: its own triangle's corner 0 where that
           triangle is in the batch, else its first triangle's corner */
        uint32_t n = 0;
        for (uint32_t i = i0; ok && i < i1; i += 3u) {
            const uint32_t k = m->index[i] >> 2;
            for (uint32_t c = 0; c < 3u; c++) {
                const uint32_t v = k - 2u + c;
                if (canon[v] == UINT32_MAX) {
                    /* v + 2 < v1 keeps the kick in this batch's vertices */
                    canon[v] = v + 2u < v1 && kicked[v + 2u] ? ICO_VU_INDEX(v + 2u, 0u)
                                                             : ICO_VU_INDEX(k, c);
                    n++;
                }
                shared[i + c] = canon[v];
            }
        }
        unique[b] = n;
        distinct += n;
    }
    free(canon);
    free(kicked);
    if (!ok || distinct >= m->indexCount) {
        free(shared);
        free(unique);
        return;
    }
    m->indexShared = shared;
    m->shareUnique = unique;
}

uint64_t rd__mesh_index_bytes(const RdMeshRec *m)
{
    return (uint64_t)m->indexCount * 4u * (m->indexShared ? 2u : 1u);
}

void rd__mesh_write_indices(const RdMeshRec *m, uint8_t *dst)
{
    if (m->indexCount == 0u) {
        return;
    }
    memcpy(dst, m->index, (size_t)m->indexCount * 4u);
    if (m->indexShared) {
        memcpy(dst + (size_t)m->indexCount * 4u, m->indexShared, (size_t)m->indexCount * 4u);
    }
}

int rd__vu_share_draw(const RdMeshRec *m, uint8_t type, uint32_t rule, uint32_t chain,
                      uint32_t flags, bool recorded, uint32_t first, uint32_t last,
                      uint32_t firstIndex, uint32_t end, uint32_t *saved)
{
    *saved = 0;
    if (m == NULL || type != RDC_MESH || !recorded ||
        (rule & ICO_VU_CLIP_MASK) != ICO_VU_CLIP_NONE) {
        return -1;
    }
    if (!rd__vu_share_on()) {
        return RD_VU_SHARE_OFF;
    }
    if (m->indexShared == NULL || m->shareUnique == NULL) {
        return RD_VU_SHARE_FORM;
    }
    if (chain != 0u) {
        return RD_VU_SHARE_CHAIN;
    }
    if ((flags & ICO_VU_PROBE) != 0u || m->tiledFrom != 0u || first >= last ||
        last > m->batchCount || m->batches[first].firstIndex != firstIndex ||
        m->batches[last - 1u].firstIndex + m->batches[last - 1u].indexCount != end) {
        return RD_VU_SHARE_OTHER;
    }
    /* the hide and later marks of the range's kicks (a triangle the verdict
       may cull or whose depth the later bias moves) */
    if (m->kickMark) {
        for (uint32_t i = firstIndex; i < end; i += 3u) {
            const uint32_t k = m->index[i] >> 2;
            if (k < m->markCount &&
                (m->kickMark[k] & (ICO_VU_MARK_HIDE | ICO_VU_MARK_LATER)) != 0u) {
                return RD_VU_SHARE_MARKS;
            }
        }
    }
    uint32_t distinct = 0;
    for (uint32_t b = first; b < last; b++) {
        distinct += m->shareUnique[b];
    }
    const uint32_t count = end - firstIndex;
    *saved = count > distinct ? count - distinct : 0u;
    return RD_VU_SHARE_YES;
}

/* The counts: the run's (the tool's stats) and the window's block (taken
   by rd_vu_share_text from another thread: atomics) */
static uint64_t s_tris, s_saved, s_rec, s_draws;
static uint64_t s_kept[RD_VU_SHARE_WHY_COUNT];
static uint64_t s_bTris, s_bSaved, s_bRec;

void rd__vu_share_note(uint32_t tris, bool shared, uint32_t saved)
{
    __atomic_fetch_add(&s_rec, tris, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_bRec, tris, __ATOMIC_RELAXED);
    if (shared) {
        __atomic_fetch_add(&s_draws, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_tris, tris, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_saved, saved, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_bTris, tris, __ATOMIC_RELAXED);
        __atomic_fetch_add(&s_bSaved, saved, __ATOMIC_RELAXED);
    }
}

void rd__vu_share_kept(int why)
{
    if (why > RD_VU_SHARE_YES && why < RD_VU_SHARE_WHY_COUNT) {
        __atomic_fetch_add(&s_kept[why], 1u, __ATOMIC_RELAXED);
    }
}

void rd__vu_share_totals(uint64_t *sharedTris, uint64_t *saved, uint64_t *recordedTris,
                         uint64_t *sharedDraws)
{
    *sharedTris = __atomic_load_n(&s_tris, __ATOMIC_RELAXED);
    *saved = __atomic_load_n(&s_saved, __ATOMIC_RELAXED);
    *recordedTris = __atomic_load_n(&s_rec, __ATOMIC_RELAXED);
    *sharedDraws = __atomic_load_n(&s_draws, __ATOMIC_RELAXED);
}

void rd__vu_share_reset(void)
{
    s_tris = s_saved = s_rec = s_draws = 0;
    s_bTris = s_bSaved = s_bRec = 0;
    memset(s_kept, 0, sizeof(s_kept));
}

int rd__vu_share_stats_text(char *out, size_t cap)
{
    uint64_t t, sv, rc, dr;
    rd__vu_share_totals(&t, &sv, &rc, &dr);
    const double all = 3.0 * (double)rc;
    return snprintf(
        out, cap,
        "VU shared vertices %s: %llu triangles in %llu draws, %llu vertex invocations "
        "saved of %.0f (%.1f %%); kept on the original list: off %llu, form %llu, "
        "chain plane %llu, hide or later marks %llu, other %llu, compaction or "
        "program entry %llu",
        rd__vu_share_on() ? "on" : "off (ICO_RD_VU_SHARE=0)", (unsigned long long)t,
        (unsigned long long)dr, (unsigned long long)sv, all,
        all > 0.0 ? 100.0 * (double)sv / all : 0.0, (unsigned long long)s_kept[RD_VU_SHARE_OFF],
        (unsigned long long)s_kept[RD_VU_SHARE_FORM], (unsigned long long)s_kept[RD_VU_SHARE_CHAIN],
        (unsigned long long)s_kept[RD_VU_SHARE_MARKS],
        (unsigned long long)s_kept[RD_VU_SHARE_OTHER],
        (unsigned long long)s_kept[RD_VU_SHARE_ENTRY]);
}

/* 61k, 4.1k, 550: the VU line's words (window_scene.c count_words) */
static void words(double v, char *out, size_t cap)
{
    if (v >= 1e4) {
        snprintf(out, cap, "%.0fk", v / 1e3);
    } else if (v >= 1e3) {
        snprintf(out, cap, "%.1fk", v / 1e3);
    } else {
        snprintf(out, cap, "%.0f", v);
    }
}

size_t rd_vu_share_text(char *out, size_t cap, unsigned n)
{
    if (cap == 0) {
        return 0;
    }
    const uint64_t t = __atomic_exchange_n(&s_bTris, 0u, __ATOMIC_RELAXED);
    const uint64_t sv = __atomic_exchange_n(&s_bSaved, 0u, __ATOMIC_RELAXED);
    const uint64_t rc = __atomic_exchange_n(&s_bRec, 0u, __ATOMIC_RELAXED);
    const double d = n ? (double)n : 1.0;
    char a[24], b[24];
    words((double)t / d, a, sizeof(a));
    words((double)sv / d, b, sizeof(b));
    const int w = snprintf(out, cap,
                           "; shared vertices: %s triangles, %s vertex invocations saved (%.0f %% "
                           "of the draws')",
                           a, b, rc ? 100.0 * (double)sv / (3.0 * (double)rc) : 0.0);
    if (w < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)w < cap ? (size_t)w : cap - 1;
}
