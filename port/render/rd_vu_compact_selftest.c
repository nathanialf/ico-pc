/* rd_vu_compact_selftest.c: the index compaction's own start-up test, for
 * its Mali-G52 form (the CPU writes every planned list first, pass K only
 * the fillers; rd_vu_compact.h says what and why; issue 179).
 *
 * v0.11.6's test passed on the phone and play then came back black: it
 * drew 60 vertices in 110 triangle slots on a 16-row record that play made
 * anew at 512 rows, every replay's lists where the replay before left the
 * same ones, and nothing read the lists back
 * (build-host/tmp/v01116/handbacks/TRI-COMPACT.md section 3).  This one
 * runs after the record's own test passed with the compute record:
 *   - the record made again at the play's size (rd_early_start_rows, as
 *     rd_vu_rec_early.c sizes it), kept for play, in host-visible memory;
 *   - a frame of four mesh commands on SCENE, Z GEQUAL with Z write,
 *     untextured, Gouraud, each the same prelit mesh of 64 strips of 128
 *     vertices (8064 triangles, index values over 32768): at the centre
 *     (every triangle kept), across the target's right edge (some kept,
 *     some left out, in the same workgroups), wholly right of it (every
 *     triangle left out: outside) and past the GS region's 4095 (every
 *     triangle left out: the region rule's drop);
 *   - the frame drawn twice without the compaction (the references, one in
 *     each frame slot), then six times with it back to back over both
 *     frame slots, the lists placed RD_VU_CMP_OWN_SHIFT values further each
 *     replay (never a whole texel), the first replay of each slot over
 *     plane memory nothing wrote;
 *   - each replay's lists and counts copied back after pass K
 *     (rd__vu_compact_check_copy) and checked against the CPU's own lists
 *     (rd_vu_cmp_check: every triangle as the CPU wrote it or the filler,
 *     the fillers as many as each workgroup counted, every triangle of the
 *     centre kept and of the two copies past the edge left out), each
 *     replay's SCENE byte for byte against the references, every recorded
 *     stamp of the test's pictures written (rd_health.h: v0.11.6's sign,
 *     "stamps unwritten after its work was signalled finished", fails it).
 * Its mark (port/platform/compact_mark.h) says "trying" (synced) before the
 * first replay with it, so a device loss or a hang here keeps it off on the
 * install at the next start; "counting" on a pass, "failed" on a refusal,
 * removed when the test could not check (tried again at the next start).
 *
 *   rd: VU index compaction: its own start-up test passed (the Mali-G52's
 *     form: 6 replays in both frame slots on the record at 512 rows, 4
 *     draws of 8064 triangles a replay, the lists from value 1048576 moved
 *     1237 values a replay; the lists read back: 193536 triangles read
 *     back: 72576 as the CPU wrote them, 120960 fillers where the GPU
 *     counted 120960 left out; the pictures byte for byte with it off,
 *     24318 of 286720 pixels drawn; no stamp of the test's 8 pictures
 *     unwritten) on SCENE 640x448 at 640x448 texels in 412.0 ms
 *   rd: VU index compaction: refused by its own start-up test (the lists:
 *     ... in replay 2 (frame slot 1), draw 3, the first at triangle 517:
 *     the keep stage's stores lost, the report's third cause) ...
 *   rd: VU index compaction: not checked by its own start-up test (...):
 *     off for the run, nothing written as failed, tried again at the next
 *     start
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_early_plan.h" /* rd_early_start_rows */
#include "rd_health.h"     /* the test's own pictures' stamps */
#include "rd_mesh.h"
#include "rd_velocity.h" /* rd__velocity_on */
#include "rd_vu_compact.h"
#include "rd_vu_compact_own.h"
#include "rd_vu_rec_early.h"
#include "rd_vu_record.h"
#include "rd_vu_skin_check.h" /* rd__vu_prepass_selftest_running */
#include "shader_consts.h"    /* ICO_VU_REC_W */

#define CT_COLS 64                          /* a strip's columns: two vertices each */
#define CT_BANDS 64                         /* the strips (batches), one under the other */
#define CT_VERTS (2 * CT_COLS)              /* a strip's vertices */
#define CT_TRIS (CT_BANDS * (CT_VERTS - 2)) /* 8064 */
#define CT_REPLAYS RD_VU_CMP_OWN_REPLAYS
#define CT_REFS 2
/* the values the lists move a replay (not a multiple of 4: each replay's
   lists straddle other texels than the replay before's) */
#define RD_VU_CMP_OWN_SHIFT 1237u
/* the grid: 256 x 96 GS pixels about the frame's centre (cells of 4 x 1.5) */
#define CT_W 256.0f
#define CT_H 96.0f
#define CT_Z 2.0f
/* the copy past the GS region (GS X 4224 .. 4480, past 4095) */
#define CT_REGION_SHIFT 2304.0f

enum { CT_CENTRE, CT_EDGE, CT_PAST, CT_REGION };

static const char *const kDraw[RD_VU_CMP_OWN_DRAWS] = {"the centre", "across the edge",
                                                       "past the edge", "past the region"};
static const int kExpect[RD_VU_CMP_OWN_DRAWS] = {RD_VU_CMP_EXPECT_KEPT, RD_VU_CMP_EXPECT_ANY,
                                                 RD_VU_CMP_EXPECT_LEFT, RD_VU_CMP_EXPECT_LEFT};
static const uint8_t kClear[4] = {0, 0, 0, 0x80};

static float fbits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void qw(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

/* rd_vuprepass_selftest.c sceneBlock's VU memory (vu1_test.c buildScene):
   model to GS screen x' = 16 x + 2048 z, y' = 16 y + 2048 z, w' = z */
static void sceneBlock(RdVuBlock *b)
{
    float (*m)[4] = b->mem;
    memset(b, 0, sizeof(*b));
    qw(m[0], 0, 0, 0, 1);
    qw(m[1], 4095, 4095, 0, 16777215);
    qw(m[3], fbits(0x8000u), fbits(0x302EC000u), fbits(0x512u), 0);
    qw(m[4], 16, 0, 0, 0);
    qw(m[5], 0, 16, 0, 0);
    qw(m[6], 2048, 2048, 3145728, 1);
    qw(m[7], 0, 0, -3145728, 0);
    qw(m[8], 1024, 0, 0, 0);
    qw(m[9], 0, 1024, 0, 0);
    qw(m[10], 0, 0, 1048576, 0);
    qw(m[11], 2048, 2048, 1048576, 1);
    for (int i = 0; i < 4; i++) {
        m[12 + i][i] = 1.0f;
        m[24 + i][i] = 1.0f;
    }
    memcpy(m[16], m[4], 4 * sizeof(m[0]));
    qw(m[20], 0.015625f, 0, 0, 0);
    qw(m[21], 0, 0.015625f, 0, 0);
    qw(m[22], 0, 0, 2, 1);
    qw(m[23], 0, 0, -3, 0);
}

/* the mesh: CT_BANDS strips (a batch each: a GIF tag, NLOOP CT_VERTS,
   PRIM strip and Gouraud), the prelit layout (position, ST with the strip
   flag on each strip's first vertex, colour); strip b covers GS Y
   2048 - 48 + 1.5 b .. + 1.5, its columns GS X 2048 - 128 + 4 col */
static RdMesh makeMesh(void)
{
    const uint32_t qwN = CT_BANDS * (1u + CT_VERTS * RD_VU_QW_PRELIT);
    float (*q)[4] = calloc(qwN, sizeof(*q));
    RdVuBatchDesc *batch = calloc(CT_BANDS, sizeof(*batch));
    if (q == NULL || batch == NULL) {
        free(q);
        free(batch);
        return (RdMesh){0};
    }
    uint32_t at = 0;
    for (uint32_t b = 0; b < CT_BANDS; b++) {
        batch[b].firstQw = at;
        qw(q[at++], fbits(0x8000u | CT_VERTS), fbits(0x30064000u), fbits(0x512u), 0);
        for (uint32_t k = 0; k < CT_VERTS; k++) {
            const uint32_t col = k / 2u, row = k % 2u;
            const float gx = -0.5f * CT_W + 4.0f * (float)col;
            const float gy = -0.5f * CT_H + 1.5f * (float)(b + row);
            /* GS X = 2048 + 16 x / z */
            qw(q[at++], gx * CT_Z / 16.0f, gy * CT_Z / 16.0f, CT_Z, 1.0f);
            qw(q[at++], 0.1f * (float)col, (float)row, 1.0f, k == 0u ? 0.0f : 1.0f);
            qw(q[at++], (float)(40u + 3u * col), (float)(200u - 2u * b), (float)(60u + 80u * row),
               127.0f);
        }
    }
    const RdVuMeshDesc d = {(const float (*)[4])q,
                            at,
                            RD_VU_QW_PRELIT,
                            CT_BANDS,
                            batch,
                            0,
                            "rd vu index compaction own test",
                            NULL,
                            0,
                            0};
    const RdMesh m = rd_create_vu_mesh(&d);
    free(q);
    free(batch);
    return m;
}

/* a command of the mesh moved shiftX GS pixels right (x' = 16 x + (2048 +
   shiftX) z), code 32 under the region rule */
static bool pushMesh(RdFrame *f, RdMesh mesh, float shiftX, uint32_t key)
{
    uint8_t buf[sizeof(RdVuPayload) + sizeof(RdVuBlock)];
    RdVuPayload p;
    RdVuBlock vu;
    memset(&p, 0, sizeof(p));
    p.code = 32;
    p.clip = RD_VU_CLIP_REGION;
    p.prog = RD_PROG_PRELIT;
    p.batchCount = CT_BANDS;
    p.qwPerVertex = RD_VU_QW_PRELIT;
    sceneBlock(&vu);
    vu.mem[6][0] += shiftX;
    memcpy(buf, &p, sizeof(p));
    memcpy(buf + sizeof(p), &vu, sizeof(vu));
    const uint32_t off = rd__frame_payload(f, buf, (uint32_t)sizeof(buf));
    RdCmd *c = rd__st_push(f, 0, RDC_MESH);
    if (!c) {
        return false;
    }
    c->b[0] = p.prog;
    c->b[1] = p.code;
    c->b[2] = p.clip;
    c->u[0] = mesh.id;
    c->u[1] = off;
    c->u[2] = (uint32_t)sizeof(buf);
    c->u[3] = 0;
    c->keyLo = key;
    return true;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, RdMesh mesh)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1; /* the depth too, to GS Z 0 */
        c->u[1] = 0;
    }
    RdCmd *b = ok && c ? rd__st_push(f, 0, RDC_BLEND) : NULL;
    if (b) {
        b->b[1] = 0x80; /* ABE off */
    }
    ok = ok && b && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_test(f, 0, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
         rd__st_push1(f, 0, RDC_PABE, 0) && rd__st_push1(f, 0, RDC_COLCLAMP, 1) &&
         rd__st_push1(f, 0, RDC_SHADE, 1);
    /* the centre; across the right edge at 2048 + w / 2; wholly right of
       it by w / 2 - 128 GS pixels (w at least 320: 32 or more); past the
       GS region */
    const float shift[RD_VU_CMP_OWN_DRAWS] = {0.0f, 0.5f * (float)w, (float)w, CT_REGION_SHIFT};
    for (uint32_t i = 0; i < RD_VU_CMP_OWN_DRAWS && ok; i++) {
        ok = pushMesh(f, mesh, shift[i], i + 1u);
    }
    return ok;
}

/* the replays' pictures: SCENE copied into a texture each (read back after
   them all: no wait between the replays) */
static struct {
    RhiTexture read[CT_REFS + CT_REPLAYS];
    RhiState state[CT_REFS + CT_REPLAYS];
    uint32_t slot[CT_REFS + CT_REPLAYS];
    RhiRect rect;
} s_ct;

static bool runOnce(const RdFrame *f, RdTargetRec *t, int k)
{
    if (!rd__replay_frame(f, 0, false) || rhi_device_lost()) {
        return false;
    }
    s_ct.slot[k] = rd__ring_slot();
    if (!s_ct.read[k].id) {
        s_ct.read[k] = rhi_create_texture(&(RhiTextureDesc){s_ct.rect.w, s_ct.rect.h, 1, t->format,
                                                            RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                            "rd vu index compaction own test", 0});
        s_ct.state[k] = RHI_STATE_UNDEFINED;
        if (!s_ct.read[k].id) {
            return false;
        }
    }
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_ct.read[k], &s_ct.state[k], RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, t->color, s_ct.rect, s_ct.read[k], 0, 0);
    rhi_end_commands(cl);
    rhi_submit(cl);
    return true;
}

/* the lists of every replay against the CPU's: into r, the first
   problem's words into where */
static bool checkLists(RdVuCmpCheck *r, char *where, size_t whereSize, char *unchecked,
                       size_t uncheckedSize)
{
    rd_vu_cmp_check_reset(r);
    bool ok = true;
    for (int k = 0; k < CT_REPLAYS; k++) {
        const RdVuCmpOwnCopy *o = rd__vu_cmp_own_copy(k);
        if (o == NULL || o->n != RD_VU_CMP_OWN_DRAWS) {
            snprintf(unchecked, uncheckedSize,
                     "pass K planned %u of its %u draws in replay %d of %d", o != NULL ? o->n : 0u,
                     RD_VU_CMP_OWN_DRAWS, k + 1, CT_REPLAYS);
            return false;
        }
        if (!o->copied || !o->rb.id) {
            snprintf(unchecked, uncheckedSize,
                     "replay %d of %d's lists were not copied back (a command not dispatched, or "
                     "no buffer for the copy)",
                     k + 1, CT_REPLAYS);
            return false;
        }
        uint32_t *m = rhi_map_buffer(o->rb);
        if (m == NULL) {
            snprintf(unchecked, uncheckedSize, "replay %d's lists could not be read back", k + 1);
            return false;
        }
        for (uint32_t i = 0; i < o->n; i++) {
            const RdVuCmpCall *c = &o->calls[i];
            uint32_t *got = m + (c->at - o->from);
            const uint32_t *texels = m + ((size_t)(o->counts + c->group) * 4u - o->from);
            const uint32_t *src = o->src[i] + c->first;
            if (k == 0 && i == CT_PAST && rd__vu_cmp_own_sabotage_readback()) {
                /* the tests' readback sabotage: the first filler's corner 0
                   as the CPU wrote it (a filler write lost in part) */
                for (uint32_t t = 0; t < c->count / 3u; t++) {
                    if (got[3u * t] == got[3u * t + 1u] && got[3u * t] != src[3u * t] + c->raise) {
                        got[3u * t] = src[3u * t] + c->raise;
                        break;
                    }
                }
            }
            const uint32_t first = r->first;
            if (!rd_vu_cmp_check(r, got, src, c->count, c->raise, texels, kExpect[i]) && ok) {
                ok = false;
                snprintf(where, whereSize,
                         "in replay %d of %d (frame slot %u), draw %u (%s), the first at triangle "
                         "%u",
                         k + 1, CT_REPLAYS, s_ct.slot[CT_REFS + k], i + 1u, kDraw[i],
                         first == UINT32_MAX ? r->first : first);
            }
        }
        rhi_unmap_buffer(o->rb);
    }
    return ok;
}

/* the cause the check points at, for the reporter's log (TRI-COMPACT.md's
   ranks) */
static const char *causeOf(const RdVuCmpCheck *r)
{
    if (r->wrong) {
        return "a list other than the one written read back (stale or lost: the report's second "
               "cause, or the CPU's lists not seen by the GPU)";
    }
    if (r->mixed || r->missing) {
        return "the keep stage's stores lost (the report's third cause; the picture stays right: "
               "an unwritten filler leaves its triangle as it was)";
    }
    if (r->extra || r->badCounts) {
        return "the keep stage's counts not its fillers";
    }
    return "the triangles left out not those the test's geometry gives";
}

void rd__vu_compact_own_selftest(RdTargetRec *t)
{
    if (!rd__vu_cmp_own_wanted()) {
        return; /* another form, or off before it: the record's test decided */
    }
    const double t0 = rd__now_ms();
    char why[1024] = "";
    char unchecked[256] = "";
    if (t->w < 320u || t->h < (uint32_t)CT_H) {
        snprintf(unchecked, sizeof unchecked,
                 "SCENE %ux%u is narrower than its frame needs (320 x 96 GS pixels)", t->w, t->h);
    }
    RdFrame f;
    memset(&f, 0, sizeof(f));
    memset(&s_ct, 0, sizeof(s_ct));
    s_ct.rect = (RhiRect){0, 0, t->tw, t->th};
    const RdMesh mesh = unchecked[0] == '\0' ? makeMesh() : (RdMesh){0};
    if (unchecked[0] == '\0' &&
        (!mesh.id || !buildFrame(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h, mesh))) {
        snprintf(unchecked, sizeof unchecked, "could not build its frame");
    }
    rd__vu_cmp_own_begin();
    if (unchecked[0] == '\0' && !rd__vu_cmp_own_trying()) {
        snprintf(unchecked, sizeof unchecked, "its mark could not be written");
    }
    const size_t size = (size_t)t->tw * t->th * 4u;
    uint8_t *px = unchecked[0] == '\0' ? malloc(size * (CT_REFS + CT_REPLAYS)) : NULL;
    if (unchecked[0] == '\0' && px == NULL) {
        snprintf(unchecked, sizeof unchecked, "no memory for its pictures");
    }
    uint32_t rows = 0, stBad = 0, stN = 0, startAt = 0, drawn = 0;
    int result = 2; /* not checked */
    RdVuCmpCheck chk;
    rd_vu_cmp_check_reset(&chk);
    if (unchecked[0] == '\0') {
        /* the record at the play's size, in host-visible memory from here
           (rd__vu_compact_host_record): made again, kept for play */
        const uint32_t before = rd__vu_rec_rows();
        uint32_t want =
            rd_early_start_rows(rd_interpolation_active(), rd__velocity_on(), rd__vu_rec_rows_min(),
                                rd__vu_rec_rows_max(), ICO_VU_REC_W);
        want = want > before ? want : before;
        rd__vu_rec_release();
        if (want != 0u) {
            (void)rd__vu_rec_presize(want);
        }
        const bool wasBlocked = g_rd.msaaBlocked;
        g_rd.msaaBlocked = true; /* the MSAA epoch is not what is checked */
        /* the pipelines first, on the workers (rd_vuprepass_selftest.c) */
        rd__vu_cmp_own_hold(false);
        rd__replay_set_window(UINT32_MAX, 0);
        rd__pipeline_gather(true);
        (void)rd__replay_frame(&f, 0, false);
        rd__pipeline_gather(false);
        rd__replay_set_window(0, 0);
        rhi_wait_idle();
        if (!rhi_device_lost()) {
            bool stopped = false;
            (void)rd__pipeline_create_gathered(&stopped);
        }
        /* the references, one in each frame slot */
        bool okRef = true;
        for (int k = 0; k < CT_REFS && okRef; k++) {
            okRef = runOnce(&f, t, k);
        }
        /* with it: back to back, the lists moved every replay, each one's
           lists and counts copied back; the test's own pictures' stamps */
        rd__health_selftest_begin();
        rd__vu_cmp_own_hold(true);
        bool okWith = okRef;
        uint32_t fallbacks = 0;
        for (int k = 0; k < CT_REPLAYS && okWith; k++) {
            rd__vu_cmp_own_shift((uint32_t)k * RD_VU_CMP_OWN_SHIFT);
            rd__vu_cmp_own_arm(k);
            okWith = runOnce(&f, t, CT_REFS + k);
            fallbacks += g_rdPerf.vuPreFallbacks;
            if (k == 0) {
                startAt = rd__vu_compact_far_at();
                rows = rd__vu_rec_rows();
            }
        }
        rd__vu_cmp_own_arm(-1);
        /* the last two replays' records are collected by the two after them
           (rd_perf.c: a slot's at its next use): two more, nothing read */
        for (int k = 0; k < RHI_FRAMES_IN_FLIGHT && okWith; k++) {
            okWith = rd__replay_frame(&f, 0, false) && !rhi_device_lost();
        }
        stBad = rd__health_selftest_end(&stN);
        rd__vu_cmp_own_hold(false);
        g_rd.msaaBlocked = wasBlocked;
        rhi_wait_idle();
        /* the eight pictures read back one a frame of their own: a frame
           holds at most 8 command lists (VKR_MAX_CMD_LISTS), and each read
           takes one; the GPU is idle, so a new frame waits for nothing */
        bool okRead = okWith && !rhi_device_lost();
        for (int k = 0; k < CT_REFS + CT_REPLAYS && okRead; k++) {
            okRead = rd__begin_own_frame(64u * 1024u) &&
                     rd__read_rhi_texture(s_ct.read[k], &s_ct.state[k], s_ct.rect.w, s_ct.rect.h,
                                          px + (size_t)k * size, size);
        }
        char where[256] = "";
        if (!okRef || !okWith || rhi_device_lost()) {
            result = 0;
            snprintf(why, sizeof why, "the device failed in its replays %s it",
                     !okRef ? "without" : "with");
        } else if (!okRead) {
            snprintf(unchecked, sizeof unchecked, "its pictures could not be read back");
        } else if (fallbacks != 0u) {
            snprintf(unchecked, sizeof unchecked,
                     "%u draws of its replays took the old way (not through the record)",
                     fallbacks);
        } else if (!checkLists(&chk, where, sizeof where, unchecked, sizeof unchecked)) {
            if (unchecked[0] == '\0') {
                result = 0;
                char text[512];
                rd_vu_cmp_check_text(text, sizeof text, &chk);
                snprintf(why, sizeof why, "the lists: %s %s: %s", text, where, causeOf(&chk));
            }
        } else {
            /* the pictures: the references alike, every replay with it as
               they are */
            for (size_t i = 0; i < size / 4u; i++) {
                drawn += memcmp(px + i * 4u, kClear, 4) != 0;
            }
            int bad = -1;
            for (int k = 0; k < CT_REPLAYS && bad < 0; k++) {
                bad = memcmp(px, px + (size_t)(CT_REFS + k) * size, size) != 0 ? CT_REFS + k : -1;
            }
            if (drawn == 0u) {
                snprintf(unchecked, sizeof unchecked, "its frame drew nothing without it");
            } else if (memcmp(px, px + size, size) != 0) {
                snprintf(unchecked, sizeof unchecked,
                         "its two pictures without it differ (frame slots %u and %u)", s_ct.slot[0],
                         s_ct.slot[1]);
            } else if (bad >= 0) {
                const uint8_t *a = px + (size_t)bad * size;
                size_t first = 0, diff = 0;
                for (size_t i = 0; i < size; i++) {
                    if (a[i] != px[i] && diff++ == 0) {
                        first = i;
                    }
                }
                const size_t tx = first / 4u;
                const uint8_t *g = a + tx * 4u, *w = px + tx * 4u;
                result = 0;
                snprintf(why, sizeof why,
                         "the pictures differ: in replay %d of %d (frame slot %u), %zu of %zu "
                         "bytes, the first at texel %u,%u: %u,%u,%u,%u want %u,%u,%u,%u; the "
                         "lists read back right: the draws did not draw them, the report's first "
                         "or second cause",
                         bad - CT_REFS + 1, CT_REPLAYS, s_ct.slot[bad], diff, size,
                         (unsigned)(tx % t->tw), (unsigned)(tx / t->tw), g[0], g[1], g[2], g[3],
                         w[0], w[1], w[2], w[3]);
            } else if (stBad != 0u) {
                result = 0;
                snprintf(why, sizeof why,
                         "the GPU's own sign of a reset in %u of the test's %u pictures (recorded "
                         "stamps unwritten after the work was signalled finished, or the clock "
                         "restarted with work queued: v0.11.6's sign on the Mali-G52); the lists "
                         "and the pictures right",
                         stBad, stN);
            } else {
                result = 1;
            }
        }
    }
    if (result == 2 && unchecked[0] == '\0') {
        snprintf(unchecked, sizeof unchecked, "no verdict");
    }
    /* the frame, the mesh, the copies and the pictures; SCENE from its
       clear at the next replay */
    rd__vu_cmp_own_end();
    rd__frame_free(&f);
    if (mesh.id) {
        rd_destroy_vu_mesh(mesh);
    }
    for (int k = 0; k < CT_REFS + CT_REPLAYS; k++) {
        if (s_ct.read[k].id) {
            rhi_destroy_texture(s_ct.read[k]);
        }
    }
    rd__vu_cmp_own_free();
    free(px);
    t->clearPending = 1;
    const double ms = rd__now_ms() - t0;
    if (result == 1) {
        char text[512];
        rd_vu_cmp_check_text(text, sizeof text, &chk);
        rd__log("VU index compaction: its own start-up test passed (the Mali-G52's form: %d "
                "replays in both frame slots on the record at %u rows, %u draws of %u triangles "
                "a replay, the lists from value %u moved %u values a replay; the lists read "
                "back: %s; the pictures byte for byte with it off, %u of %zu pixels drawn; no "
                "stamp of the test's %u pictures unwritten) on SCENE %ux%u at %ux%u texels in "
                "%.1f ms",
                CT_REPLAYS, rows, RD_VU_CMP_OWN_DRAWS, (unsigned)CT_TRIS, startAt,
                RD_VU_CMP_OWN_SHIFT, text, drawn, size / 4u, stN, t->w, t->h, t->tw, t->th, ms);
        rd__vu_cmp_own_verdict(1, NULL);
    } else if (result == 0) {
        rd__log("VU index compaction: refused by its own start-up test (%s) on SCENE %ux%u at "
                "%ux%u texels in %.1f ms: off for the run, its mark says failed",
                why, t->w, t->h, t->tw, t->th, ms);
        rd__vu_cmp_own_verdict(0, why);
    } else {
        rd__log("VU index compaction: not checked by its own start-up test (%s) on SCENE %ux%u "
                "in %.1f ms: off for the run, nothing written as failed, tried again at the next "
                "start",
                unchecked, t->w, t->h, ms);
        rd__vu_cmp_own_verdict(2, unchecked);
    }
}
