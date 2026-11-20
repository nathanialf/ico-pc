/* rd_vu_share_test.c: shared vertices for clip-mode-none strips, the pure
 * parts, no device (port/render/rd_vu_share.c, rd_vu_share.h; issue 179).
 *
 *   rd_vu_share_test
 *
 *   t1  the tests' switch's word: unset, empty and 1 on, 0 and off off
 *   t2  a strip of four triangles: each value of the shared list names the
 *       same vertex as the original's in its place (kick - 2 + corner), a
 *       triangle's first value is its own (the provoking vertex: the flat
 *       outputs), every value is one the original list holds (a kick of
 *       the list), the values the hand-worked list gives, the batch's six
 *       distinct values (six invocations saved of twelve)
 *   t3  two batches (strips of 5 and 4 vertices): values never cross a
 *       batch, each batch's distinct count, the invariants of t2
 *   t4  no list where nothing or nothing safe is shared: three separate
 *       triangles (strip flags), a kick twice, a corner outside its batch,
 *       a corner out of order, batches not back to back, a sheet
 *   t5  a draw's verdict: shared with its saved count; kept on the
 *       original list (counted by why) for the switch's words, the form,
 *       a chain plane, the hide and later marks, a probe draw, a range off
 *       the batches; no candidate (not counted) for a skinned command, a
 *       draw the pre-pass did not record and a rule mode other than none;
 *       the joined mark changes nothing
 *   t6  the bytes on the device: the original's, then the shared list's
 *   t7  the counts and their texts: the run's stats line, the block's text
 *       taken back to zero */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_vu_share.h"
#include "shader_consts.h"

static int s_fail;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

/* a mesh record of n vertices, the kicks' triangles in the order given,
 * the batches (first vertex, vertex count, triangle count) back to back */
static RdMeshRec *mesh(uint32_t nv, const uint32_t *kicks, uint32_t nk, const uint32_t (*bat)[3],
                       uint32_t nb)
{
    RdMeshRec *m = calloc(1, sizeof(*m));
    m->vu = 1;
    m->vertexCount = nv;
    m->indexCount = nk * 3u;
    m->index = malloc((size_t)m->indexCount * 4u + 4u);
    for (uint32_t t = 0; t < nk; t++) {
        for (uint32_t c = 0; c < 3u; c++) {
            m->index[3u * t + c] = ICO_VU_INDEX(kicks[t], c);
        }
    }
    m->batchCount = nb;
    m->batches = calloc(nb, sizeof(RdVuBatchRec));
    uint32_t at = 0;
    for (uint32_t b = 0; b < nb; b++) {
        m->batches[b].firstVertex = bat[b][0];
        m->batches[b].vertexCount = bat[b][1];
        m->batches[b].firstIndex = at;
        m->batches[b].indexCount = bat[b][2] * 3u;
        at += bat[b][2] * 3u;
    }
    return m;
}

static void meshFree(RdMeshRec *m)
{
    rd__vu_share_free(m);
    free(m->index);
    free(m->batches);
    free(m->kickMark);
    free(m);
}

/* the invariants of every shared list: the same vertex at every place, a
 * triangle's first value its own, every value a kick of its batch's
 * triangles, no value crossing its batch */
static void invariants(const RdMeshRec *m, const char *what)
{
    CHECK(m->indexShared != NULL, "%s: a shared list", what);
    if (!m->indexShared) {
        return;
    }
    for (uint32_t b = 0; b < m->batchCount; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        for (uint32_t i = br->firstIndex; i < br->firstIndex + br->indexCount; i++) {
            const uint32_t o = m->index[i], s = m->indexShared[i];
            const uint32_t vo = (o >> 2) - 2u + (o & 3u), vs = (s >> 2) - 2u + (s & 3u);
            CHECK(vo == vs, "%s: place %u names vertex %u, not %u", what, i, vs, vo);
            CHECK((s & 3u) < 3u, "%s: place %u corner %u", what, i, s & 3u);
            if (i % 3u == 0u) {
                CHECK(s == o, "%s: triangle %u's first value %u, not its own %u", what, i / 3u, s,
                      o);
            }
            bool found = false;
            for (uint32_t j = br->firstIndex; j < br->firstIndex + br->indexCount; j += 3u) {
                found = found || (m->index[j] >> 2) == (s >> 2);
            }
            CHECK(found, "%s: place %u's value %u: no triangle of its batch has kick %u", what, i,
                  s, s >> 2);
        }
    }
}

static void t1(void)
{
    CHECK(rd_vu_share_word(NULL), "unset: on");
    CHECK(rd_vu_share_word(""), "empty: on");
    CHECK(rd_vu_share_word("1"), "1: on");
    CHECK(!rd_vu_share_word("0"), "0: off");
    CHECK(!rd_vu_share_word("off"), "off: off");
}

static void t2(void)
{
    static const uint32_t kicks[] = {2, 3, 4, 5};
    static const uint32_t bat[][3] = {{0, 6, 4}};
    RdMeshRec *m = mesh(6, kicks, 4, bat, 1);
    rd__vu_share_build(m);
    invariants(m, "t2");
    /* vertex 0..3 by their own triangles (kicks 2..5), 4 and 5 by the first
       triangle that has them (kick 4 corner 2, kick 5 corner 2) */
    static const uint32_t want[] = {8, 12, 16, 12, 16, 20, 16, 20, 18, 20, 18, 22};
    if (m->indexShared) {
        for (uint32_t i = 0; i < 12u; i++) {
            CHECK(m->indexShared[i] == want[i], "t2: place %u: %u, want %u", i, m->indexShared[i],
                  want[i]);
        }
        CHECK(m->shareUnique[0] == 6u, "t2: %u distinct, want 6", m->shareUnique[0]);
    }
    meshFree(m);
}

static void t3(void)
{
    /* batch 0: vertices 0..4 (kicks 2, 3, 4); batch 1: vertices 5..8 (kicks
       7, 8); a gap of none between them */
    static const uint32_t kicks[] = {2, 3, 4, 7, 8};
    static const uint32_t bat[][3] = {{0, 5, 3}, {5, 4, 2}};
    RdMeshRec *m = mesh(9, kicks, 5, bat, 2);
    rd__vu_share_build(m);
    invariants(m, "t3");
    if (m->shareUnique) {
        CHECK(m->shareUnique[0] == 5u && m->shareUnique[1] == 4u, "t3: %u and %u distinct",
              m->shareUnique[0], m->shareUnique[1]);
    }
    meshFree(m);
}

static void t4(void)
{
    /* three separate triangles: nothing shared */
    static const uint32_t k1[] = {2, 5, 8};
    static const uint32_t b1[][3] = {{0, 9, 3}};
    RdMeshRec *m = mesh(9, k1, 3, b1, 1);
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL && m->shareUnique == NULL, "t4: separate triangles: no list");
    meshFree(m);
    /* a kick twice */
    static const uint32_t k2[] = {2, 3, 3};
    static const uint32_t b2[][3] = {{0, 4, 3}};
    m = mesh(4, k2, 3, b2, 1);
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL, "t4: a kick twice: no list");
    meshFree(m);
    /* a corner outside its batch (kick 5 of batch 0, whose vertices are 0..4) */
    static const uint32_t k3[] = {2, 3, 5, 7};
    static const uint32_t b3[][3] = {{0, 5, 3}, {5, 4, 1}};
    m = mesh(9, k3, 4, b3, 2);
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL, "t4: a corner outside its batch: no list");
    meshFree(m);
    /* a corner out of order */
    static const uint32_t k4[] = {2, 3, 4};
    static const uint32_t b4[][3] = {{0, 5, 3}};
    m = mesh(5, k4, 3, b4, 1);
    m->index[4] = ICO_VU_INDEX(3u, 2u);
    m->index[5] = ICO_VU_INDEX(3u, 1u);
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL, "t4: corners out of order: no list");
    meshFree(m);
    /* batches not back to back */
    m = mesh(5, k4, 3, b4, 1);
    m->batches[0].firstIndex = 3u;
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL, "t4: a batch not at index 0: no list");
    meshFree(m);
    /* a sheet of the content table */
    m = mesh(5, k4, 3, b4, 1);
    m->tiledFrom = 5u;
    rd__vu_share_build(m);
    CHECK(m->indexShared == NULL, "t4: a sheet: no list");
    meshFree(m);
    /* the same mesh as a strip: a list (the cases above differ in one thing) */
    m = mesh(5, k4, 3, b4, 1);
    rd__vu_share_build(m);
    invariants(m, "t4 strip");
    meshFree(m);
}

static void t5(void)
{
    static const uint32_t kicks[] = {2, 3, 4, 5};
    static const uint32_t bat[][3] = {{0, 6, 4}};
    RdMeshRec *m = mesh(6, kicks, 4, bat, 1);
    rd__vu_share_build(m);
    const uint32_t none = ICO_VU_CLIP_NONE | ICO_VU_TRI_CHAIN | ICO_VU_TRI_HIDE;
    uint32_t saved = 99;
    int w = rd__vu_share_draw(m, RDC_MESH, none, 0, ICO_VU_CLIP_NONE, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_YES && saved == 6u, "t5: shared: %d, saved %u", w, saved);
    /* no candidate */
    w = rd__vu_share_draw(m, RDC_SKINNED, none, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == -1 && saved == 0u, "t5: skinned: %d", w);
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, false, 0, 1, 0, 12, &saved);
    CHECK(w == -1, "t5: not recorded: %d", w);
    w = rd__vu_share_draw(m, RDC_MESH, ICO_VU_CLIP_REGION | ICO_VU_TRI_CHAIN, 0, 0, true, 0, 1, 0,
                          12, &saved);
    CHECK(w == -1, "t5: rule mode region: %d", w);
    w = rd__vu_share_draw(m, RDC_MESH, ICO_VU_CLIP_SCISSOR, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == -1, "t5: rule mode scissor: %d", w);
    /* kept */
    w = rd__vu_share_draw(m, RDC_MESH, none, 3u, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_CHAIN && saved == 0u, "t5: chain plane: %d", w);
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, ICO_VU_PROBE, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_OTHER, "t5: probe: %d", w);
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, true, 0, 1, 3, 12, &saved);
    CHECK(w == RD_VU_SHARE_OTHER, "t5: a range off the batches: %d", w);
    m->kickMark = calloc(6, 1);
    m->markCount = 6;
    m->kickMark[4] = ICO_VU_MARK_JOINED | ICO_VU_MARK_FLIP;
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_YES, "t5: joined and flip marks: %d", w);
    m->kickMark[5] = ICO_VU_MARK_LATER;
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_MARKS, "t5: later mark: %d", w);
    m->kickMark[5] = 0;
    m->kickMark[2] = ICO_VU_MARK_HIDE;
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_MARKS, "t5: hide mark: %d", w);
    m->kickMark[2] = 0;
    rd__vu_share_free(m);
    w = rd__vu_share_draw(m, RDC_MESH, none, 0, 0, true, 0, 1, 0, 12, &saved);
    CHECK(w == RD_VU_SHARE_FORM, "t5: no list: %d", w);
    meshFree(m);
}

static void t6(void)
{
    static const uint32_t kicks[] = {2, 3, 4, 5};
    static const uint32_t bat[][3] = {{0, 6, 4}};
    RdMeshRec *m = mesh(6, kicks, 4, bat, 1);
    CHECK(rd__mesh_index_bytes(m) == 48u, "t6: no list: %llu bytes",
          (unsigned long long)rd__mesh_index_bytes(m));
    rd__vu_share_build(m);
    CHECK(rd__mesh_index_bytes(m) == 96u, "t6: with the list: %llu bytes",
          (unsigned long long)rd__mesh_index_bytes(m));
    uint8_t buf[96];
    memset(buf, 0xEE, sizeof(buf));
    rd__mesh_write_indices(m, buf);
    CHECK(memcmp(buf, m->index, 48) == 0, "t6: the original first");
    CHECK(m->indexShared && memcmp(buf + 48, m->indexShared, 48) == 0, "t6: the shared list after");
    meshFree(m);
}

static void t7(void)
{
    rd__vu_share_reset();
    rd__vu_share_note(100u, false, 0u);
    rd__vu_share_note(40u, true, 30u);
    rd__vu_share_kept(RD_VU_SHARE_CHAIN);
    rd__vu_share_kept(RD_VU_SHARE_MARKS);
    rd__vu_share_kept(RD_VU_SHARE_YES); /* not a reason: not counted */
    uint64_t t, sv, rc, dr;
    rd__vu_share_totals(&t, &sv, &rc, &dr);
    CHECK(t == 40u && sv == 30u && rc == 140u && dr == 1u, "t7: totals %llu %llu %llu %llu",
          (unsigned long long)t, (unsigned long long)sv, (unsigned long long)rc,
          (unsigned long long)dr);
    char buf[512];
    const int n = rd__vu_share_stats_text(buf, sizeof(buf));
    CHECK(n > 0 && (size_t)n < sizeof(buf), "t7: stats %d bytes", n);
    CHECK(strstr(buf, "VU shared vertices on: 40 triangles in 1 draws, 30 vertex invocations "
                      "saved of 420") == buf,
          "t7: stats \"%s\"", buf);
    CHECK(strstr(buf, "chain plane 1, hide or later marks 1,") != NULL, "t7: reasons \"%s\"", buf);
    const size_t w = rd_vu_share_text(buf, sizeof(buf), 2u);
    CHECK(w > 0 && strcmp(buf, "; shared vertices: 20 triangles, 15 vertex invocations saved (7 % "
                               "of the draws')") == 0,
          "t7: block \"%s\"", buf);
    (void)rd_vu_share_text(buf, sizeof(buf), 1u);
    CHECK(strstr(buf, ": 0 triangles, 0 vertex invocations saved (0 %") != NULL,
          "t7: block taken back to zero: \"%s\"", buf);
    char small[8];
    const size_t ws = rd_vu_share_text(small, sizeof(small), 1u);
    CHECK(ws == sizeof(small) - 1u && strlen(small) == ws, "t7: a short buffer cut: %u",
          (unsigned)ws);
    rd__vu_share_reset();
}

int main(void)
{
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
    if (s_fail) {
        printf("rd_vu_share_test: %d failures\n", s_fail);
        return 1;
    }
    printf("rd_vu_share_test: ok\n");
    return 0;
}
