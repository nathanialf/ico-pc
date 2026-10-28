/* rd_perf_test: the renderer's CPU cost per replay and its steady state.
 *
 * A synthetic frame shaped like a game frame: SCENE cleared with its depth,
 * textured UI sprites and world triangle strips, 96 static mesh draws over
 * 12 meshes (one of them rewritten every frame, as reg_setShape morphs do),
 * a DATE draw after alpha writes, the shadow count (its scene-sized
 * temporary target, two volumes, the resolve), a block target with its own
 * depth (puddle.c's reflection) sampled back, and the reduction.
 *
 *   replay    the frame recorded once and replayed 200 times: after the
 *             first replay no buffer, texture or device memory is created
 *             or destroyed, no bind or fence wait-idle happens, no texture
 *             or static mesh is uploaded again, and the mean CPU time of a
 *             replay without its wait for the GPU (rhi_wait_frame: on
 *             lavapipe that is the rasterisation) is under 20 ms; and
 *             every replay, here and below, creates one
 *             uniform bind group per uniform layout (frame, draw, VU),
 *             however many draws; a steady replay records
 *             exactly BARRIERS_REPLAY pipeline barriers (BARRIERS_RECORD
 *             below); the screen-prim commands make
 *             SCREEN_CMDS draws one by one and SCREEN_DRAWS merged;
 *   record    200 frames recorded and closed as the game does (rd_end_frame
 *             replays each): from the fifth on, when the frame ring and the
 *             temporary target pool are warm, nothing is created or
 *             destroyed either, and only the morphing mesh is uploaded.
 *   Both run without the VU vertex pre-pass (rd__set_vu_prepass), whose
 *   pass and copies the barrier counts leave out;
 *   prepass   the replay's frame replayed 200 times with it (the records
 *             of the replays before the switch, still pending, dropped by
 *             replay number): after the phase's first two replays nothing
 *             is created or destroyed, every
 *             replay records one pre-pass with the same vertices, draws
 *             every mesh command through it (no draw the old way) and the
 *             same barriers, and the picture is the one drawn without it,
 *             byte for byte.  The reachable set below is made with it.
 *   passes    the record phase's last frame with every replay timed pass
 *             by pass ([dev] perf_passes, rd_perf.c rd__perf_pass): each
 *             timed record's pieces begin with the uploads (then the
 *             pre-pass where the device takes it) and end with the
 *             present; the pieces the pass ring also notes (render passes,
 *             copies, snapshots, exact blends, the uploads, the pre-pass,
 *             the present) are its entries of that replay one for one, in
 *             order, a render pass at the ring's size; the pieces' draws
 *             and pipeline binds add up to the record's, their GPU times
 *             are all there and add up to the record's GPU time (within
 *             10 percent and 0.5 ms), and the record's counts by kind are
 *             the pieces'; then without the key a replay is timed only
 *             RD_PERF_PASS_EVERY_MS after the last timed one, an untimed
 *             one has no pieces, and every replay still counts its pieces
 *             by kind.  Before any device: the CSV header and line and the
 *             window's two summary texts on fixed pieces, to the byte.
 *
 * With a dump instead (a measurement tool):
 *   rd_perf_test --dump FILE [--enhanced] [--resolution N] [--full-height]
 *                [--precreate] [--repeat N]
 * replays the dump N times (default 200) and prints the mean and the
 * minimum of every phase of rd.h's RdPerfRecord (the first replay apart);
 * --precreate makes the reachable pipeline set first, as the window does.
 *
 * Exit 77 without a Vulkan device. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_passring.h"

/* a sanitizer build (gcc defines __SANITIZE_*, clang answers __has_feature) */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define RD_PERF_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define RD_PERF_SANITIZED 1
#endif
#endif

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define W 512
#define H 448
#define MESHES 12
#define MESH_VERTS 300
#define QPV 3 /* normal_c: XYZ, ST (w = strip flag 1), RGBA */
#define DRAWS 96

static RdMesh s_mesh[MESHES];

static RdTex s_tex[4];

static float s_morph[1 + MESH_VERTS * QPV][4];

/* ------------------------------------------------------------ the frame */

static void meshQw(float (*qw)[4], uint32_t seed)
{
    memset(qw, 0, sizeof(float) * 4 * (1 + MESH_VERTS * QPV));
    uint32_t tag = MESH_VERTS | 0x8000u; /* NLOOP, EOP */
    memcpy(qw[0], &tag, 4);
    for (uint32_t v = 0; v < MESH_VERTS; v++) {
        float *p = qw[1 + v * QPV];
        p[0] = (float)((v * 37u + seed * 11u) % 400u) - 200.0f;
        p[1] = (float)((v * 53u + seed * 7u) % 300u) - 150.0f;
        p[2] = 1000.0f + (float)(v % 17u);
        p[3] = 1.0f;
        float *st = qw[1 + v * QPV + 1];
        st[0] = (float)(v % 8u) / 8.0f;
        st[1] = (float)(v % 5u) / 5.0f;
        st[2] = 1.0f;
        st[3] = 1.0f;
        float *c = qw[1 + v * QPV + 2];
        c[0] = c[1] = c[2] = 128.0f;
        c[3] = 128.0f;
    }
}

static void makeAssets(void)
{
    static float qw[1 + MESH_VERTS * QPV][4];
    for (int i = 0; i < MESHES; i++) {
        meshQw(qw, (uint32_t)i);
        RdVuBatchDesc b = {0};
        b.firstQw = 0;
        RdVuMeshDesc d;
        memset(&d, 0, sizeof(d));
        d.qw = (const float (*)[4])qw;
        d.qwCount = 1 + MESH_VERTS * QPV;
        d.qwPerVertex = QPV;
        d.batches = &b;
        d.batchCount = 1;
        d.debugName = "perf mesh";
        s_mesh[i] = rd_create_vu_mesh(&d);
    }
    meshQw(s_morph, 99);
    static uint8_t px[64 * 64 * 4];
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 64 * 64; i++) {
            px[i * 4 + 0] = (uint8_t)(i * (t + 1));
            px[i * 4 + 1] = (uint8_t)(i >> 4);
            px[i * 4 + 2] = (uint8_t)(t * 60);
            px[i * 4 + 3] = (uint8_t)((i & 1) ? 0x80 : 0x40);
        }
        s_tex[t] = rd_create_texture(64, 64, px, RD_TEXA_80_80, "perf texture");
    }
}

static RdScreenVtx vtx(int x, int y, uint32_t z, float s, float t, uint8_t c, uint8_t a)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - W / 2 + x) * 16;
    v.y = (2048 - H / 2 + y) * 16;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    v.rgba[0] = v.rgba[1] = v.rgba[2] = c;
    v.rgba[3] = a;
    return v;
}

static void recordFrame(uint32_t n)
{
    static const uint8_t bg[4] = {16, 24, 32, 0x80};
    RdTarget scene = rd_target(RD_TARGET_SCENE);

    rd_begin_frame();
    /* list 0: the scene */
    rd_select_list(0);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_clear_target(scene, bg, 1, 0);
    rd_test_gs(0x50000);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    for (int i = 0; i < DRAWS; i++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        /* identity-ish world to screen: GS window around 2048 */
        d.vu.mem[4][0] = 1.0f;
        d.vu.mem[5][1] = 1.0f;
        d.vu.mem[6][2] = 1.0f;
        d.vu.mem[7][0] = 2048.0f;
        d.vu.mem[7][1] = 2048.0f;
        d.vu.mem[7][3] = 1.0f;
        d.batchCount = 1;
        rd_texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        const int m = i % MESHES;
        if (m == 0 && i == 0) {
            CHECK(rd_update_vu_mesh(s_mesh[0], (const float (*)[4])s_morph),
                  "a morph"); /* a morph */
        }
        rd_draw_vu_mesh(s_mesh[m], &d, RD_KEY(1, (uint32_t)i, 0));
    }
    /* world strips and UI sprites */
    rd_texture(s_tex[1], RD_TEXFN_MODULATE, RD_TCC_RGBA);
    for (int i = 0; i < 20; i++) {
        RdScreenVtx s[8];
        for (int k = 0; k < 8; k++) {
            s[k] = vtx(20 + i * 20 + (k / 2) * 6, 40 + (k & 1) * 30, 0x1000000u + (uint32_t)i,
                       (float)k / 8.0f, (float)(k & 1), 100, 0x80);
        }
        rd_screen_prims(RD_PRIM_TRIANGLE_STRIP, s, 8, RD_SPACE_WORLD, 0, RD_KEY(2, (uint32_t)i, 0));
    }
    rd_select_list(11);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_test_gs(0x30000);
    for (int i = 0; i < 60; i++) {
        RdScreenVtx s[2] = {
            vtx(10 + (i % 12) * 40, 300 + (i / 12) * 20, 0xFFFFFFFFu, 0.0f, 0.0f, 128, 0x80),
            vtx(40 + (i % 12) * 40, 316 + (i / 12) * 20, 0xFFFFFFFFu, 64.0f * 16.0f, 64.0f * 16.0f,
                128, 0x80)};
        rd_texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_UI, 1, RD_KEY(3, (uint32_t)i, 0));
    }
    /* list 4: a DATE draw after alpha writes (the specular passes' TEST) */
    rd_select_list(4);
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_test_gs(0x5C000); /* DATE on, Z GEQUAL */
    for (int i = 0; i < 4; i++) {
        RdScreenVtx s[2] = {vtx(100 + i * 50, 100, 0x2000000u, 0, 0, 200, 0x80),
                            vtx(140 + i * 50, 160, 0x2000000u, 0, 0, 200, 0x80)};
        rd_texture_off();
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_WORLD, 0, 0);
    }
    /* list 3: the shadow count on its temporary target */
    rd_select_list(3);
    RdTarget cnt = rd_shadow_count_target(W, H);
    rd_set_target(cnt, scene, W, H, 0);
    rd_z_write(0);
    rd_test_gs(0x30000);
    rd_abe(0);
    rd_texture_off();
    rd_shadow_reset();
    rd_test_gs(0x50000);
    rd_blend_func(RD_BLEND_CS_FIX_ADD_CD, 0x80);
    rd_col_clamp(0);
    {
        RdScreenVtx t[12];
        int8_t sign[4] = {1, 1, -1, 1};
        for (int k = 0; k < 12; k++) {
            t[k] = vtx(100 + (k % 3) * 60 + (k / 3) * 10, 100 + ((k % 3) == 2) * 80, 0x3000000u, 0,
                       0, 4, 0x80);
        }
        rd_shadow_tris(t, sign, 4, RD_KEY(4, 0, 0));
    }
    rd_shadow_resolve();
    rd_col_clamp(1);
    /* list 4 again: a block target with its own depth (a reflection) */
    rd_select_list(4);
    RdTarget blk = rd_block_target(0x2800, 256, 256, 1);
    rd_set_target(blk, blk, 256, 256, 0);
    rd_clear_target(blk, bg, 1, 0);
    rd_test_gs(0x50000);
    for (int i = 0; i < 8; i++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[4][0] = d.vu.mem[5][1] = d.vu.mem[6][2] = d.vu.mem[7][3] = 1.0f;
        d.vu.mem[7][0] = d.vu.mem[7][1] = 2048.0f;
        d.batchCount = 1;
        rd_draw_vu_mesh(s_mesh[(i + 3) % MESHES], &d, RD_KEY(5, (uint32_t)i, 0));
    }
    rd_set_target(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_texture(rd_target_texture(blk, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    {
        RdScreenVtx s[2] = {vtx(300, 20, 0x2000000u, 0, 0, 128, 0x80),
                            vtx(428, 148, 0x2000000u, 256.0f * 16.0f, 256.0f * 16.0f, 128, 0x80)};
        rd_screen_prims(RD_PRIM_SPRITES, s, 2, RD_SPACE_WORLD, 1, 0);
    }
    /* list 12: the reduction */
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = scene;
    pp.dst = rd_target(RD_TARGET_DISPLAY);
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
    rd_post(RD_POST_REDUCTION, &pp);
    (void)n;
}

/* ------------------------------------------------------------ the checks */

typedef struct Sum {
    unsigned n;
    double total, wait, upload, walk, bind, submit, present, gpu;
    double minTotal;
    uint64_t created, destroyed, allocs, waitIdles, texUploads, meshUploads, meshBytes, draws;
    /* bind groups created, per replay: all, and rd's uniform
     * and texture groups (the least and most of the uniform ones) */
    uint64_t groups, uniformGroups, textureGroups;
    uint32_t uniformMin, uniformMax;
    /* pipeline barriers recorded per replay (least, most) */
    uint32_t barrierMin, barrierMax;
    /* screen-prim draws per replay, one per command and merged
     * (least, most) */
    uint32_t screenCmdMin, screenCmdMax, screenDrawMin, screenDrawMax;
    /* the VU vertex pre-pass (rd.h RdPerfRecord.vuPre*): its passes, draws
     * and the draws drawn the old way, summed; its vertices a replay
     * (least, most) */
    uint64_t prePasses, preDraws, preFallbacks;
    uint32_t preVertMin, preVertMax;
} Sum;

static void add(Sum *s, const RdPerfRecord *r)
{
    if (s->n == 0 || r->totalMs < s->minTotal) {
        s->minTotal = r->totalMs;
    }
    s->n++;
    s->total += r->totalMs;
    s->wait += r->waitMs;
    s->upload += r->uploadMs;
    s->walk += r->walkMs;
    s->bind += r->bindMs;
    s->submit += r->submitMs;
    s->present += r->presentMs;
    s->gpu += r->gpuMs;
    s->created += r->buffersCreated + r->texturesCreated;
    s->destroyed += r->buffersDestroyed + r->texturesDestroyed;
    s->allocs += r->memoryAllocs;
    s->waitIdles += r->waitIdles;
    s->texUploads += r->textureUploads;
    s->meshUploads += r->meshUploads;
    s->meshBytes += r->meshUploadBytes;
    s->draws += r->draws;
    if (s->n == 1 || r->uniformGroups < s->uniformMin) {
        s->uniformMin = r->uniformGroups;
    }
    if (s->n == 1 || r->uniformGroups > s->uniformMax) {
        s->uniformMax = r->uniformGroups;
    }
    if (s->n == 1 || r->barriers < s->barrierMin) {
        s->barrierMin = r->barriers;
    }
    if (s->n == 1 || r->barriers > s->barrierMax) {
        s->barrierMax = r->barriers;
    }
    if (s->n == 1 || r->screenCmds < s->screenCmdMin) {
        s->screenCmdMin = r->screenCmds;
    }
    if (s->n == 1 || r->screenCmds > s->screenCmdMax) {
        s->screenCmdMax = r->screenCmds;
    }
    if (s->n == 1 || r->screenDraws < s->screenDrawMin) {
        s->screenDrawMin = r->screenDraws;
    }
    if (s->n == 1 || r->screenDraws > s->screenDrawMax) {
        s->screenDrawMax = r->screenDraws;
    }
    s->groups += r->bindGroups;
    s->uniformGroups += r->uniformGroups;
    s->textureGroups += r->textureGroups;
    s->prePasses += r->vuPrePasses;
    s->preDraws += r->vuPreDraws;
    s->preFallbacks += r->vuPreFallbacks;
    if (s->n == 1 || r->vuPreVertices < s->preVertMin) {
        s->preVertMin = r->vuPreVertices;
    }
    if (s->n == 1 || r->vuPreVertices > s->preVertMax) {
        s->preVertMax = r->vuPreVertices;
    }
}

static void print(const char *what, const Sum *s)
{
    const double n = s->n ? (double)s->n : 1.0;
    printf("%s: %u replays, CPU %.3f ms mean (min %.3f): wait %.3f upload %.3f walk %.3f bind "
           "%.3f submit %.3f present %.3f; GPU %.3f ms; per replay %.1f draws, %.2f textures, "
           "%.2f meshes (%.1f KB) uploaded; %llu created, %llu destroyed, %llu memory "
           "allocations, %llu wait-idles\n",
           what, s->n, s->total / n, s->minTotal, s->wait / n, s->upload / n, s->walk / n,
           s->bind / n, s->submit / n, s->present / n, s->gpu / n, (double)s->draws / n,
           (double)s->texUploads / n, (double)s->meshUploads / n, (double)s->meshBytes / n / 1024.0,
           (unsigned long long)s->created, (unsigned long long)s->destroyed,
           (unsigned long long)s->allocs, (unsigned long long)s->waitIdles);
    printf("%s: bind groups per replay %.2f: uniform %.2f (%u..%u), texture %.2f; barriers "
           "%u..%u; screen-prim draws %u..%u one per command, %u..%u merged\n",
           what, (double)s->groups / n, (double)s->uniformGroups / n, s->uniformMin, s->uniformMax,
           (double)s->textureGroups / n, s->barrierMin, s->barrierMax, s->screenCmdMin,
           s->screenCmdMax, s->screenDrawMin, s->screenDrawMax);
}

/* every finished record into first (the first `skip`) or rest */
static void drain(Sum *first, Sum *rest, unsigned skip, unsigned *seen)
{
    RdPerfRecord r;
    while (rd_perf_pop(&r)) {
        add(*seen < skip ? first : rest, &r);
        (*seen)++;
    }
}

/* drain for a phase that starts with a switch: a record is pushed
   RHI_FRAMES_IN_FLIGHT replays after its own (rd_perf.c
   rd__perf_collect_gpu), so the first ones popped after the switch are the
   replays before it; those (replay number at or below after) are dropped
   and counted in *stale, and the first skip of the phase's own go to first */
static void drainAfter(Sum *first, Sum *rest, unsigned skip, unsigned *seen, uint32_t after,
                       unsigned *stale)
{
    RdPerfRecord r;
    while (rd_perf_pop(&r)) {
        if (r.replay <= after) {
            (*stale)++;
            continue;
        }
        add(*seen < skip ? first : rest, &r);
        (*seen)++;
    }
}

/* DISPLAY's pixels (W x H/2 RGBA8) into a new buffer; NULL on failure */
static uint8_t *readDisplay(void)
{
    const size_t cap = (size_t)W * H * 4;
    uint8_t *px = malloc(cap);
    uint32_t w = 0, h = 0;
    if (px && !rd__read_target(rd_target(RD_TARGET_DISPLAY), px, cap, &w, &h)) {
        free(px);
        px = NULL;
    }
    return px;
}

static void samePixels(const char *what, const uint8_t *a, const uint8_t *b)
{
    CHECK(a && b, "%s: no readback", what);
    if (a && b) {
        size_t diff = 0;
        for (size_t i = 0; i < (size_t)W * (H / 2) * 4; i++) {
            diff += a[i] != b[i];
        }
        CHECK(diff == 0, "%s: %zu bytes of DISPLAY differ from the first replay's", what, diff);
    }
}

/* The exact blend's counters (rd.h RdPerfRecord, issue 103) on the frame
 * replayed under each blend path the device has: none under the
 * fixed-function blend; destination-read draws (the frame's LERP meshes)
 * and neither storage passes nor switches under the attachment read; under
 * the storage read storage passes and switches, and with the storage pass
 * merge (g_rd.passMerge) the same DISPLAY bytes as without it, no more
 * passes and no more switches (a device with the interlock or ROV; not
 * lavapipe, which has none: that part is skipped there).  The frame has no
 * glow: no glow passes. */
static RdPerfRecord replayOnce(const RdFrame *f)
{
    rd__replay_frame(f, 0, true);
    const RdPerfRecord r = g_rdPerf; /* the record the replay just closed */
    while (rd_perf_pop(NULL)) {}
    return r;
}

static void checkExactCounters(const RdFrame *f)
{
    const RdBlendExact was = rd_blend_exact();
    const bool merge = g_rd.passMerge;
    if (rd_set_blend_exact(RD_BLEND_EXACT_OFF)) {
        const RdPerfRecord r = replayOnce(f);
        CHECK(r.dstDraws == 0 && r.storagePasses == 0 && r.dstSwitches == 0,
              "fixed-function: %u destination-read draws, %u storage passes, %u switches",
              r.dstDraws, r.storagePasses, r.dstSwitches);
        CHECK(r.glowPasses == 0, "fixed-function: %u glow passes in a frame without the glow",
              r.glowPasses);
        printf("fixed-function: %u snapshots copied, %u swapped\n", r.snapshots, r.snapSwaps);
    }
    if (rd_set_blend_exact(RD_BLEND_EXACT_ATTACHMENT)) {
        const RdPerfRecord r = replayOnce(f);
        printf("attachment read: %u destination-read draws\n", r.dstDraws);
        CHECK(r.dstDraws > 0 && r.storagePasses == 0 && r.dstSwitches == 0,
              "attachment read: %u destination-read draws, %u storage passes, %u switches",
              r.dstDraws, r.storagePasses, r.dstSwitches);
    }
    if (rd_set_blend_exact(RD_BLEND_EXACT_STORAGE)) {
        g_rd.passMerge = false;
        const RdPerfRecord off = replayOnce(f);
        uint8_t *a = readDisplay();
        g_rd.passMerge = true;
        const RdPerfRecord on = replayOnce(f);
        uint8_t *b = readDisplay();
        printf("storage read: %u passes, %u storage passes, %u switches, %u destination-read draws "
               "without the merge; %u, %u, %u, %u with it\n",
               off.renderPasses, off.storagePasses, off.dstSwitches, off.dstDraws, on.renderPasses,
               on.storagePasses, on.dstSwitches, on.dstDraws);
        CHECK(off.dstDraws > 0 && off.storagePasses > 0 && off.dstSwitches > 0,
              "storage read: %u destination-read draws, %u storage passes, %u switches",
              off.dstDraws, off.storagePasses, off.dstSwitches);
        CHECK(on.dstSwitches <= off.dstSwitches && on.renderPasses <= off.renderPasses &&
                  on.dstDraws >= off.dstDraws,
              "storage pass merge: %u switches, %u passes, %u destination-read draws against "
              "%u, %u, %u without",
              on.dstSwitches, on.renderPasses, on.dstDraws, off.dstSwitches, off.renderPasses,
              off.dstDraws);
        samePixels("storage pass merge", a, b);
        free(a);
        free(b);
    }
    g_rd.passMerge = merge;
    rd_set_blend_exact(was);
    rd__replay_frame(f, 0, true); /* the frame as it was, for what follows */
    while (rd_perf_pop(NULL)) {}
}

/* The uniform blocks take dynamic offsets, so every replay
 * creates one group per uniform layout it uses, whatever its draw count:
 * the frame (FrameCB), draw (DrawCB) and VU (DrawCB, VuCB, VuBoneCB over
 * the stream's buffer) layouts, all three in this frame, whose meshes live
 * in one arena chunk.  The other groups are texture groups, one per
 * (texture, sampler, DATE snapshot), and under the exact blend one per pass
 * that draws with the destination read (rd_replay.c dstBind: the target as
 * the input attachment or storage texture). */
#define UNIFORM_LAYOUTS 3

static void checkGroups(const char *what, const Sum *s)
{
    CHECK(s->n > 0 && s->uniformMin == UNIFORM_LAYOUTS && s->uniformMax == UNIFORM_LAYOUTS,
          "%s: %u..%u uniform bind groups a replay, %d expected (one per layout)", what,
          s->uniformMin, s->uniformMax, UNIFORM_LAYOUTS);
    CHECK(s->groups == s->uniformGroups + s->textureGroups,
          "%s: %llu bind groups, %llu uniform + %llu texture", what, (unsigned long long)s->groups,
          (unsigned long long)s->uniformGroups, (unsigned long long)s->textureGroups);
}

/* The pipeline barriers of a steady replay.  The Vulkan backend orders
 * same-state writes per resource: one list-opening barrier and one per
 * pass or copy whose target has a write pending, instead of a global
 * barrier before every pass and copy (ICO_VK_GLOBAL_BARRIERS=1, the
 * _GLOBAL counts).  A backend without counters (D3D12) reads 0 and is not
 * checked.  A target clear is the next pass's load op and the shadow reset
 * the next pass's stencil clear, not passes of their own.  The counts are
 * vkCmdPipelineBarrier calls: the image transitions recorded together go
 * out in one call, joined to the next pass's or copy's own barrier
 * (vk_cmd.c, vkr_flush_barriers); the global mode records each transition
 * in a call of its own.  18 and 21 are the steady-state counts this test
 * prints with the fixed-function blend.
 *
 * Under the exact blend's attachment read (rd_dst_read.c) a pending clear
 * of a readable target is a pass of its own before the pass that draws
 * (the load op's clear is not ordered against the shaders' reads of the
 * attachment), and the pass after it starts behind a barrier: two in this
 * frame (SCENE with its depth, the reflection's block target), so 20 and
 * 23.  No draw adds one: the rasterization-order access orders the reads
 * (the by-region barriers the validation layer needs before a
 * destination-read draw are RhiStats.orderBarriers, not counted here).
 * The storage path (interlock, ROV) is not measured on the devices the
 * tests run on (lavapipe has no interlock): not checked. */
#define BARRIERS_REPLAY 18
#define BARRIERS_RECORD 21
#define BARRIERS_ATTACHMENT_CLEARS 2
#define BARRIERS_REPLAY_GLOBAL 42
#define BARRIERS_RECORD_GLOBAL 46

/* The CPU budget a replay (20 ms without the GPU wait) is the renderer's own
 * walk on Vulkan and D3D12, whose drivers shade on the GPU behind the fence
 * wait.  On OpenGL the walk's draw calls hold the driver's vertex shading
 * too (llvmpipe shades inside glDraw*: 0.43 us a vertex here), so the budget
 * would count the software GPU's work: not checked there, said once. */
static bool cpuBudgetApplies(const char *what)
{
#ifdef RD_PERF_SANITIZED
    /* a sanitizer build runs the walk two to three times slower, and CI runs
       four tests at once: the budget is the release build's (the asan job's
       rd_perf_attachment measured 21.96 ms against 20 ms in the v0.9.1 run) */
    printf("%s: the CPU budget is not checked under a sanitizer\n", what);
    return false;
#endif
    if (rhi_backend() == RHI_BACKEND_OPENGL) {
        printf("%s: the CPU budget is not checked on OpenGL (the driver shades inside the "
               "draw calls)\n",
               what);
        return false;
    }
    return true;
}

static void checkBarriers(const char *what, const Sum *s, uint32_t tracked, uint32_t global)
{
    const char *env = getenv("ICO_VK_GLOBAL_BARRIERS");
    const bool globalMode = env && env[0] && env[0] != '0';
    uint32_t want = globalMode ? global : tracked;
    if (s->n > 0 && s->barrierMax == 0) {
        printf("%s: the backend counts no barriers: not checked\n", what);
        return;
    }
    if (rd_blend_exact() == RD_BLEND_EXACT_STORAGE) {
        printf("%s: the exact blend's storage path: barriers not checked\n", what);
        return;
    }
    if (rd_blend_exact() == RD_BLEND_EXACT_ATTACHMENT) {
        want += BARRIERS_ATTACHMENT_CLEARS; /* the global mode's too (43, 47) */
        if (strcmp(what, "record") == 0) {
            want += 1; /* v0.9.4: the shadow count in one draw reads the stencil
                          through a view: one transition more under the
                          attachment read with the record on (the count the
                          run prints; 24) */
        }
    }
    CHECK(s->n > 0 && s->barrierMin == want && s->barrierMax == want,
          "%s: %u..%u pipeline barriers a replay, %u expected", what, s->barrierMin, s->barrierMax,
          want);
}

/* The screen-prim draws of a replay.  One per command: the 20
 * world strips, the 60 UI sprites, the 4 DATE sprites and the reflection
 * sprite, 85.  Merged (rd_replay.c doScreen: consecutive commands under the
 * same state, pipeline, texture, scissor and DATE snapshot): the strips are
 * one draw; the UI sprites change texture every sprite and stay 60; each
 * DATE sprite writes alpha, so the next retakes the snapshot, 4; 66. */
#define SCREEN_CMDS 85
#define SCREEN_DRAWS 66

static void checkScreen(const char *what, const Sum *s)
{
    CHECK(s->n > 0 && s->screenCmdMin == SCREEN_CMDS && s->screenCmdMax == SCREEN_CMDS,
          "%s: %u..%u screen-prim draws one per command, %d expected", what, s->screenCmdMin,
          s->screenCmdMax, SCREEN_CMDS);
    CHECK(s->n > 0 && s->screenDrawMin == SCREEN_DRAWS && s->screenDrawMax == SCREEN_DRAWS,
          "%s: %u..%u screen-prim draws merged, %d expected", what, s->screenDrawMin,
          s->screenDrawMax, SCREEN_DRAWS);
}

/* rd_set_pipeline_progress: one call with 0 before the first pipeline, the
   calls while the batches compile and between them, done never going down
   and below the total until the one last call at the total, which is the
   size of the reachable set; cleared, no more calls.  A nonzero return
   stops the set after the batch in flight, without the last call. */
typedef struct PipeProgress {
    uint32_t calls, firstDone, lastDone, lastTotal, atTotal, stopAfter;
    int backwards, totalChanged;
} PipeProgress;

static int pipeProgress(void *ctx, uint32_t done, uint32_t total)
{
    PipeProgress *p = ctx;

    if (p->calls == 0) {
        p->firstDone = done;
        p->lastTotal = total;
    } else {
        p->backwards += done < p->lastDone;
        p->totalChanged += total != p->lastTotal;
    }
    p->atTotal += done == total;
    p->lastDone = done;
    p->calls++;
    return p->stopAfter != 0 && p->calls >= p->stopAfter;
}

static void checkPipelineProgress(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
    PipeProgress p;

    /* stopped at the first call after the first one: one batch made, the
       set's end not reported, and the next call makes the rest */
    rd__pipeline_cache_clear();
    memset(&p, 0, sizeof(p));
    p.stopAfter = 2;
    rd_set_pipeline_progress(pipeProgress, &p);
    const uint32_t made = rd_precreate_pipelines();
    CHECK(made > 0 && made <= 32 && made < n,
          "pipeline progress: a stopped set made %u of %u (want one batch, at most 32)", made, n);
    CHECK(p.atTotal == 0 && p.lastDone < n, "pipeline progress: a stopped set reported %u of %u",
          p.lastDone, n);

    memset(&p, 0, sizeof(p));
    rd_precreate_pipelines();
    CHECK(p.calls >= 2, "pipeline progress: %u calls for %u pipelines", p.calls, n);
    CHECK(p.firstDone == 0, "pipeline progress: the first call had %u done", p.firstDone);
    CHECK(p.lastDone == n && p.lastTotal == n && p.atTotal == 1,
          "pipeline progress: ended at %u of %u (want %u), %u calls at the total", p.lastDone,
          p.lastTotal, n, p.atTotal);
    CHECK(p.backwards == 0 && p.totalChanged == 0,
          "pipeline progress: went back %d times, total "
          "changed %d times",
          p.backwards, p.totalChanged);
    CHECK(rd__pipeline_count() >= n, "pipeline progress: %u pipelines cached for a set of %u",
          rd__pipeline_count(), n);
    CHECK(rd_precreate_longest_ms() >= 0.0f, "pipeline progress: the longest compile %.3f ms",
          (double)rd_precreate_longest_ms());
    const uint32_t calls = p.calls;
    rd_set_pipeline_progress(NULL, NULL);
    rd_precreate_pipelines();
    CHECK(p.calls == calls, "pipeline progress: called %u times after it was cleared", p.calls);
}

/* ---------------------------------------------- the pieces' texts (pure) */

static void checkPassTexts(void)
{
    char buf[1024];
    rd_perf_pass_csv_header(buf, sizeof(buf));
    CHECK(strcmp(buf, "replay,frame,pass,kind,list,effect,target,width,height,draws,"
                      "pipeline_binds,gpu_us,cpu_us\n") == 0,
          "csv header: %s", buf);
    const RdPerfPass p[4] = {
        {RD_PERF_PASS_RENDER, RD_TARGET_SCENE, 0, -1, 848, 448, 420, 300, 4120.0f, 900.0f, 0.0f, 0,
         0, 0},
        {RD_PERF_PASS_RENDER, RD_TARGET_WORK1, 7, RD_PERF_POST_GLOW, 256, 128, 1, 1, 60.0f, 5.0f,
         0.0f, 0, 0, 0},
        {RD_PERF_PASS_RENDER, RD_TARGET_WORK1, 7, RD_PERF_POST_GLOW, 256, 128, 1, 1, 40.0f, 5.0f,
         0.0f, 0, 0, 0},
        {RD_PERF_PASS_DATE, RD_TARGET_SCENE, 3, -1, 848, 448, 1, 1, 30.0f, 2.0f, 0.0f, 0, 0, 0},
    };
    rd_perf_pass_csv_line(12, 34, 1, &p[1], buf, sizeof(buf));
    CHECK(strcmp(buf, "12,34,1,render pass,7,glow,WORK1,256,128,1,1,60.0,5.0\n") == 0,
          "csv line: %s", buf);
    static RdPerfPassAgg agg;
    rd_perf_pass_agg_reset(&agg);
    rd_perf_pass_agg_add(&agg, p, 4);
    rd_perf_pass_agg_add(&agg, p, 4);
    rd_perf_pass_agg_text(&agg, 5, buf, sizeof(buf));
    CHECK(strcmp(buf, "list 0 SCENE 848x448 420 draws 4.12 ms, glow WORK1 256x128 2.0 passes 2 "
                      "draws 0.10 ms, DATE snapshot SCENE 848x448 1 draws 0.03 ms (3 of 4.0 "
                      "passes a replay, 2 timed replays; passes under 0.05 ms: 2.0 a replay, "
                      "0.07 ms)") == 0,
          "top passes: %s", buf);
    /* a busy clock (OpenGL): the entry's busy time after its span, the
       others as before */
    RdPerfPass busy[4];
    memcpy(busy, p, sizeof(busy));
    busy[0].gpuBusyUs = 1300.0f;
    busy[0].busyValid = 1;
    busy[0].entryBinds = 2100;
    busy[0].entrySkipped = 1800;
    rd_perf_pass_agg_reset(&agg);
    rd_perf_pass_agg_add(&agg, busy, 4);
    rd_perf_pass_agg_text(&agg, 1, buf, sizeof(buf));
    CHECK(strcmp(buf, "list 0 SCENE 848x448 420 draws 4.12 ms (busy 1.30) (1 of 4.0 passes a "
                      "replay, 1 timed replays; passes under 0.05 ms: 2.0 a replay, 0.07 ms; group "
                      "entries 2100 a timed replay, 1800 already bound; GPU busy 1.30 ms, span "
                      "4.12 ms a timed replay)") == 0,
          "top passes with a busy time: %s", buf);
    rd_perf_pass_agg_reset(&agg);
    rd_perf_pass_agg_add(&agg, p, 4);
    rd_perf_pass_agg_add(&agg, p, 4);
    double kinds[RD_PERF_PASS_KIND_COUNT] = {0}, effects[RD_PERF_POST_COUNT] = {0};
    kinds[RD_PERF_PASS_RENDER] = 6.0;
    kinds[RD_PERF_PASS_DATE] = 2.0;
    kinds[RD_PERF_PASS_UPLOAD] = 2.0;
    effects[RD_PERF_POST_GLOW] = 4.0;
    rd_perf_pass_kinds_text(kinds, effects, 2.0, buf, sizeof(buf));
    CHECK(strcmp(buf, "5.0: render passes 3.0 (lists 1.0, glow 2.0), uploads 1.0, DATE snapshot "
                      "1.0") == 0,
          "passes by kind: %s", buf);
    /* cut to the buffer, never past it */
    char small[24];
    memset(small, 'x', sizeof(small));
    const int n = rd_perf_pass_agg_text(&agg, 5, small, 20);
    CHECK(n == 19 && small[19] == '\0' && small[20] == 'x', "top passes cut: %d", n);
}

/* ------------------------------------------------ the pieces on the device */

/* the pass ring's kind as a piece's (-1: none) */
static int ringPiece(uint8_t kind)
{
    switch (kind) {
    case RD_PASSK_PASS:
        return RD_PERF_PASS_RENDER;
    case RD_PASSK_COPY:
        return RD_PERF_PASS_COPY;
    case RD_PASSK_SNAP:
        return RD_PERF_PASS_SNAP;
    case RD_PASSK_EXACT:
        return RD_PERF_PASS_EXACT;
    case RD_PASSK_UPLOAD:
        return RD_PERF_PASS_UPLOAD;
    case RD_PASSK_PRESENT:
        return RD_PERF_PASS_PRESENT;
    case RD_PASSK_VU_PRE:
    case RD_PASSK_VU_TRI:
        return RD_PERF_PASS_PREPASS;
    default:
        return -1;
    }
}

static bool ringKind(int piece)
{
    return piece == RD_PERF_PASS_RENDER || piece == RD_PERF_PASS_COPY ||
           piece == RD_PERF_PASS_SNAP || piece == RD_PERF_PASS_EXACT ||
           piece == RD_PERF_PASS_UPLOAD || piece == RD_PERF_PASS_PRESENT ||
           piece == RD_PERF_PASS_PREPASS;
}

/* one timed record's pieces against the record and the pass ring */
static void checkPieces(const RdPerfRecord *r, const RdPerfPass *p, uint32_t n, bool prepass)
{
    CHECK(n > 0 && n == r->passCount, "passes: replay %u: %u pieces, the record says %u", r->replay,
          n, r->passCount);
    if (n == 0) {
        return;
    }
    CHECK(p[0].kind == RD_PERF_PASS_UPLOAD, "passes: replay %u begins with %s", r->replay,
          rd_perf_pass_kind_name(p[0].kind));
    CHECK(!prepass || (n > 1 && p[1].kind == RD_PERF_PASS_PREPASS),
          "passes: replay %u: no pre-pass second", r->replay);
    CHECK(p[n - 1].kind == RD_PERF_PASS_PRESENT, "passes: replay %u ends with %s", r->replay,
          rd_perf_pass_kind_name(p[n - 1].kind));
    /* the pass ring's entries of the replay, one for one, in order */
    uint32_t k = 0, matched = 0, entries = 0;
    for (uint32_t i = 0; i < rd_passring_count(); i++) {
        const RdPassRec *e = rd_passring_get(i);
        if (e->replay != r->replay) {
            continue;
        }
        const int want = ringPiece(e->kind);
        if (want < 0) {
            continue;
        }
        entries++;
        /* the next piece the ring notes (a render pass without a target,
           the shadow count's reduction, is not in the ring) */
        while (k < n && (!ringKind(p[k].kind) || (p[k].kind == RD_PERF_PASS_RENDER &&
                                                  p[k].target == RD_PERF_TARGET_NONE))) {
            k++;
        }
        if (k == n) {
            break;
        }
        CHECK(p[k].kind == want, "passes: replay %u entry %u is a %s, piece %u a %s", r->replay,
              entries, rd_perf_pass_kind_name(want), k, rd_perf_pass_kind_name(p[k].kind));
        if (want == RD_PERF_PASS_RENDER) {
            CHECK(p[k].w == e->cw && p[k].h == e->ch,
                  "passes: replay %u piece %u is %ux%u, the ring's pass %ux%u", r->replay, k,
                  (unsigned)p[k].w, (unsigned)p[k].h, e->cw, e->ch);
        }
        matched += p[k].kind == want;
        k++;
    }
    for (; k < n; k++) {
        CHECK(!ringKind(p[k].kind) ||
                  (p[k].kind == RD_PERF_PASS_RENDER && p[k].target == RD_PERF_TARGET_NONE),
              "passes: replay %u piece %u (%s) has no ring entry", r->replay, k,
              rd_perf_pass_kind_name(p[k].kind));
    }
    CHECK(entries > 0 && matched == entries, "passes: replay %u: %u of %u ring entries matched",
          r->replay, matched, entries);
    /* the counters and times add up */
    uint64_t draws = 0, binds = 0;
    double gpu = 0.0;
    uint32_t untimed = 0, kinds[RD_PERF_PASS_KIND_COUNT] = {0};
    for (uint32_t i = 0; i < n; i++) {
        draws += p[i].draws;
        binds += p[i].pipelineBinds;
        if (p[i].gpuUs < 0.0f) {
            untimed++;
        } else {
            gpu += p[i].gpuUs;
        }
        CHECK(p[i].cpuUs >= 0.0f, "passes: replay %u piece %u CPU %.1f us", r->replay, i,
              (double)p[i].cpuUs);
        kinds[p[i].kind]++;
    }
    CHECK(draws == r->draws && binds == r->pipelineBinds,
          "passes: replay %u: pieces %llu draws and %llu pipeline binds, the record %u and %u",
          r->replay, (unsigned long long)draws, (unsigned long long)binds, r->draws,
          r->pipelineBinds);
    for (int t = 0; t < RD_PERF_PASS_KIND_COUNT; t++) {
        CHECK(kinds[t] == r->passKinds[t], "passes: replay %u: %u pieces of %s, counted %u",
              r->replay, kinds[t], rd_perf_pass_kind_name(t), (unsigned)r->passKinds[t]);
    }
    /* the busy clock (rhi.h rhi_read_timestamps_busy): on OpenGL each timed
       piece's busy time within its span and the timed record's within its
       GPU time; no other backend has one (checkPieces sees timed records
       only) */
    const bool gl = rhi_backend() == RHI_BACKEND_OPENGL;
    for (uint32_t i = 0; i < n; i++) {
        CHECK(p[i].busyValid == (gl && p[i].gpuUs >= 0.0f) &&
                  (!p[i].busyValid ||
                   (p[i].gpuBusyUs >= 0.0f && p[i].gpuBusyUs <= p[i].gpuUs + 0.01f)),
              "passes: replay %u piece %u busy %d %.1f us of %.1f us", r->replay, i,
              (int)p[i].busyValid, (double)p[i].gpuBusyUs, (double)p[i].gpuUs);
    }
    CHECK(r->gpuBusyValid == (gl && r->gpuValid) &&
              (!r->gpuBusyValid || (r->gpuBusyMs >= 0.0 && r->gpuBusyMs <= r->gpuMs + 1e-6)),
          "passes: replay %u busy %d %.3f ms of %.3f ms", r->replay, (int)r->gpuBusyValid,
          r->gpuBusyMs, r->gpuMs);
    if (r->gpuValid) {
        const double total = r->gpuMs * 1000.0;
        CHECK(untimed == 0, "passes: replay %u: %u pieces without a GPU time", r->replay, untimed);
        CHECK(gpu <= total * 1.1 + 500.0 && gpu >= total * 0.9 - 500.0,
              "passes: replay %u: the pieces' GPU %.1f us against the record's %.1f us", r->replay,
              gpu, total);
    }
}

static void checkPasses(const RdFrame *f, bool prepass)
{
    /* every record and pieces of the replays before: drained */
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__replay_frame(f, 0, true);
    }
    RdPerfRecord r;
    const RdPerfPass *p;
    /* the drain's last RHI_FRAMES_IN_FLIGHT replays are still in flight
     * (their records pop RHI_FRAMES_IN_FLIGHT replays later, untimed): the
     * loop below passes over those by their replay number */
    uint32_t lastDrained = 0;
    while (rd_perf_pop(&r)) {
        rd_perf_passes_of(r.replay, &p);
        lastDrained = r.replay > lastDrained ? r.replay : lastDrained;
    }
    rd__set_perf_passes(true);
    uint32_t timed = 0, records = 0;
    for (int i = 0; i < 8 + RHI_FRAMES_IN_FLIGHT; i++) {
        rd__replay_frame(f, 0, true);
        while (rd_perf_pop(&r)) {
            const uint32_t n = rd_perf_passes_of(r.replay, &p);
            if (!r.passTimed && r.replay <= lastDrained + RHI_FRAMES_IN_FLIGHT + 1) {
                continue; /* a drain replay's record, popped late */
            }
            records++;
            timed += r.passTimed;
            checkPieces(&r, p, n, prepass);
        }
    }
    printf("passes: every replay: %u of %u records timed pass by pass\n", timed, records);
    CHECK(records > 0 && timed == records, "passes: %u of %u records timed", timed, records);
    /* sampled: a timed replay RD_PERF_PASS_EVERY_MS after the last */
    rd__set_perf_passes(false);
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__replay_frame(f, 0, true);
    }
    while (rd_perf_pop(&r)) {
        rd_perf_passes_of(r.replay, &p);
    }
    double lastTimed = -1.0;
    uint32_t sampled = 0, untimed = 0;
    for (int i = 0; i < 40; i++) {
        rd__replay_frame(f, 0, true);
        while (rd_perf_pop(&r)) {
            const uint32_t n = rd_perf_passes_of(r.replay, &p);
            CHECK(r.passKinds[RD_PERF_PASS_UPLOAD] == 1 && r.passKinds[RD_PERF_PASS_PRESENT] == 1,
                  "passes: replay %u counted %u uploads and %u presents", r.replay,
                  (unsigned)r.passKinds[RD_PERF_PASS_UPLOAD],
                  (unsigned)r.passKinds[RD_PERF_PASS_PRESENT]);
            if (!r.passTimed) {
                untimed++;
                CHECK(n == 0 && r.passCount == 0, "passes: untimed replay %u has %u pieces",
                      r.replay, n);
                continue;
            }
            CHECK(lastTimed < 0.0 || r.startMs - lastTimed >= RD_PERF_PASS_EVERY_MS,
                  "passes: replays timed %.1f ms apart", r.startMs - lastTimed);
            lastTimed = r.startMs;
            sampled++;
            checkPieces(&r, p, n, prepass);
        }
    }
    printf("passes: sampled: %u timed, %u not\n", sampled, untimed);
    /* without the list and effect stamps ([dev] perf_list_stamps = false):
       no list or effect time, the GPU total and the pieces' times kept */
    rd__set_perf_list_stamps(false);
    rd__set_perf_passes(true);
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__replay_frame(f, 0, true); /* the records with the stamps drained */
    }
    while (rd_perf_pop(&r)) {
        rd_perf_passes_of(r.replay, &p);
    }
    uint32_t bare = 0;
    for (int i = 0; i < 4 + RHI_FRAMES_IN_FLIGHT; i++) {
        rd__replay_frame(f, 0, true);
        while (rd_perf_pop(&r)) {
            const uint32_t n = rd_perf_passes_of(r.replay, &p);
            if (!r.gpuValid) {
                continue;
            }
            double lists = 0.0;
            for (int l = 0; l < RD_LIST_COUNT; l++) {
                lists += r.gpuListMs[l];
            }
            bare++;
            CHECK(lists == 0.0 && r.gpuMs > 0.0,
                  "passes: without list stamps replay %u: lists %.3f ms, GPU %.3f ms", r.replay,
                  lists, r.gpuMs);
            checkPieces(&r, p, n, prepass);
        }
    }
    rd__set_perf_list_stamps(true);
    rd__set_perf_passes(false);
    printf("passes: without list stamps: %u replays checked\n", bare);
    CHECK(bare > 0, "passes: no replay checked without list stamps");
}

static int synthetic(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.outputWidth = 640;
    st.outputHeight = 480;
    if (!rd_init(W, H, &st, NULL)) {
        printf("no Vulkan device: skipped\n");
        return 77;
    }
    makeAssets();
    /* the counts below are those of a replay without the vertex pre-pass
     * (the prepass phase after them measures it) */
    const bool prepassDevice = g_rd.vuPrepass;
    rd__set_vu_prepass(false);

    /* replay: one recording, 200 replays (rd_end_frame replays it once) */
    recordFrame(0);
    rd_end_frame(0);
    const RdFrame *f = rd__last_frame();
    CHECK(f != NULL, "no closed frame");
    unsigned seen = 0;
    Sum first, rest;
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    for (int i = 0; i < 200 && f; i++) {
        rd__replay_frame(f, 0, true);
        drain(&first, &rest, 2, &seen);
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__replay_frame(f, 0, true); /* flush the last records' timestamps */
        drain(&first, &rest, 2, &seen);
    }
    /* the pixels: the first replay of a fresh frame (pooled targets cleared,
     * meshes uploaded) and the 200th (everything reused) agree */
    uint8_t *ref = NULL;
    {
        recordFrame(0);
        rd_end_frame(0);
        ref = readDisplay();
        rd__replay_frame(rd__last_frame(), 0, true);
        uint8_t *again = readDisplay();
        samePixels("replayed again", ref, again);
        free(again);
        RdPerfRecord r;
        while (rd_perf_pop(&r)) {}
    }
    checkExactCounters(rd__last_frame());
    print("replay (first 2)", &first);
    print("replay (steady)", &rest);
    CHECK(rest.created == 0 && rest.destroyed == 0 && rest.allocs == 0,
          "replay: %llu created, %llu destroyed, %llu allocations after the first",
          (unsigned long long)rest.created, (unsigned long long)rest.destroyed,
          (unsigned long long)rest.allocs);
    CHECK(rest.waitIdles == 0, "replay: %llu wait-idles", (unsigned long long)rest.waitIdles);
    CHECK(rest.texUploads == 0, "replay: textures uploaded again (%llu)",
          (unsigned long long)rest.texUploads);
    CHECK(rest.meshUploads == 0, "replay: static meshes uploaded again (%llu)",
          (unsigned long long)rest.meshUploads);
    if (cpuBudgetApplies("replay")) {
        CHECK(rest.n > 0 && (rest.total - rest.wait) / rest.n < 20.0,
              "replay: %.3f ms of CPU a replay (without the GPU wait), over 20 ms",
              rest.n ? (rest.total - rest.wait) / rest.n : 0.0);
    }
    checkGroups("replay", &rest);
    checkBarriers("replay", &rest, BARRIERS_REPLAY, BARRIERS_REPLAY_GLOBAL);
    checkScreen("replay", &rest);

    /* prepass: the same frame with the VU vertex pre-pass (rd_replay_vu.c
     * rd__vu_prepass), where the device takes it */
    if (prepassDevice) {
        const uint32_t barriersOff = rest.barrierMax;
        /* the records still pending are replays without the pre-pass: they
           are dropped, so the first pre-pass replay's creations (the record's
           textures and buffers) land in the first two */
        const uint32_t lastOff = g_rdPerf.replay;
        unsigned stale = 0;
        rd__set_vu_prepass(true);
        memset(&first, 0, sizeof(first));
        memset(&rest, 0, sizeof(rest));
        seen = 0;
        const RdFrame *pf = rd__last_frame();
        for (int i = 0; i < 200 && pf; i++) {
            rd__replay_frame(pf, 0, true);
            drainAfter(&first, &rest, 2, &seen, lastOff, &stale);
        }
        for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
            rd__replay_frame(pf, 0, true);
            drainAfter(&first, &rest, 2, &seen, lastOff, &stale);
        }
        uint8_t *withPre = readDisplay();
        samePixels("the VU vertex pre-pass", ref, withPre);
        free(withPre);
        printf("prepass: %u records from before the switch dropped\n", stale);
        CHECK(stale <= RHI_FRAMES_IN_FLIGHT,
              "prepass: %u records from before the switch (at most %d pending)", stale,
              RHI_FRAMES_IN_FLIGHT);
        print("prepass (first 2)", &first);
        print("prepass (steady)", &rest);
        printf("prepass: %u..%u vertices a replay, %.1f draws a replay, %llu draws the old way; "
               "barriers %u..%u against %u without\n",
               rest.preVertMin, rest.preVertMax, rest.n ? (double)rest.preDraws / rest.n : 0.0,
               (unsigned long long)rest.preFallbacks, rest.barrierMin, rest.barrierMax,
               barriersOff);
        CHECK(rest.created == 0 && rest.destroyed == 0 && rest.allocs == 0,
              "prepass: %llu created, %llu destroyed, %llu allocations after the first two",
              (unsigned long long)rest.created, (unsigned long long)rest.destroyed,
              (unsigned long long)rest.allocs);
        CHECK(
            rest.n > 0 && rest.prePasses >= rest.n && rest.prePasses <= 2u * rest.n &&
                rest.preVertMin > 0 && rest.preVertMin == rest.preVertMax &&
                rest.preDraws >= rest.n &&
                rest.preDraws <=
                    (uint64_t)DRAWS * 5u *
                        rest.n, /* batched (G1): one draw a batch, at most the old one an entry and stage */
            "prepass: %llu passes in %u replays, %u..%u vertices, %llu draws",
            (unsigned long long)rest.prePasses, rest.n, rest.preVertMin, rest.preVertMax,
            (unsigned long long)rest.preDraws);
        CHECK(rest.preFallbacks == 0, "prepass: %llu mesh draws drawn the old way",
              (unsigned long long)rest.preFallbacks);
        if (rest.barrierMax == 0) {
            printf("prepass: the backend counts no barriers: not checked\n");
        } else {
            CHECK(rest.n > 0 && rest.barrierMin == rest.barrierMax && rest.barrierMin > barriersOff,
                  "prepass: %u..%u barriers a replay (%u without the pre-pass)", rest.barrierMin,
                  rest.barrierMax, barriersOff);
        }
        checkGroups("prepass", &rest);
        checkScreen("prepass", &rest);
        rd__set_vu_prepass(false);
    }

    /* record: 200 frames as the game makes them */
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    seen = 0;
    for (uint32_t i = 1; i <= 200; i++) {
        recordFrame(i);
        rd_end_frame(0);
        drain(&first, &rest, 4, &seen);
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        recordFrame(0);
        rd_end_frame(0);
        drain(&first, &rest, 4, &seen);
    }
    {
        uint8_t *last = readDisplay();
        samePixels("200 frames recorded", ref, last);
        free(last);
    }
    free(ref);
    print("record (first 4)", &first);
    print("record (steady)", &rest);
    CHECK(rest.created == 0 && rest.destroyed == 0 && rest.allocs == 0,
          "record: %llu created, %llu destroyed, %llu allocations in steady state",
          (unsigned long long)rest.created, (unsigned long long)rest.destroyed,
          (unsigned long long)rest.allocs);
    CHECK(rest.waitIdles == 0, "record: %llu wait-idles", (unsigned long long)rest.waitIdles);
    CHECK(rest.texUploads == 0, "record: textures uploaded again (%llu)",
          (unsigned long long)rest.texUploads);
    CHECK(rest.n > 0 && rest.meshUploads <= rest.n,
          "record: %llu mesh uploads in %u frames (only the morph should upload)",
          (unsigned long long)rest.meshUploads, rest.n);
    if (cpuBudgetApplies("record")) {
        CHECK(rest.n > 0 && (rest.total - rest.wait) / rest.n < 20.0,
              "record: %.3f ms of CPU a replay (without the GPU wait), over 20 ms",
              rest.n ? (rest.total - rest.wait) / rest.n : 0.0);
    }
    checkGroups("record", &rest);
    checkBarriers("record", &rest, BARRIERS_RECORD, BARRIERS_RECORD_GLOBAL);
    checkScreen("record", &rest);

    /* passes: the record phase's last frame timed pass by pass, with the
       pre-pass where the device takes it */
    rd__set_vu_prepass(prepassDevice);
    if (rhi_timestamps_supported()) {
        checkPasses(rd__last_frame(), prepassDevice);
    } else {
        printf("passes: no GPU timestamps: skipped\n");
    }

    /* the reachable set, as the window creates it at
       start-up (rd_precreate_pipelines): every key makes a pipeline, the
       vertex pre-pass's among them where the device takes it.
       rd_perf_mali runs this on a Mali-G68's limits (ICO_VK_FAKE_LIMITS=
       mali), where a key past them would be refused by the limit's name */
    rd__set_vu_prepass(prepassDevice);
    {
        static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
        const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
        uint32_t missing = 0;

        checkPipelineProgress();
        rd_precreate_pipelines();
        for (uint32_t i = 0; i < n; i++) {
            if (!rd__get_pipeline(&keys[i]).id) {
                missing++;
            }
        }
        printf("reachable set: %u keys, %u without a pipeline\n", n, missing);
        CHECK(n > 0 && missing == 0, "reachable set: %u of %u keys without a pipeline", missing, n);
    }
    rd_shutdown();
    return 0;
}

/* ------------------------------------------------------- a dump, timed */

static int dumpMode(int argc, char **argv)
{
    const char *path = NULL;
    int repeat = 200, precreate = 0;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.outputWidth = 960;
    st.outputHeight = 720;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--enhanced") == 0) {
            st.preset = RD_PRESET_ENHANCED;
            st.aspect = 4.0f / 3.0f;
        } else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            st.sceneScale = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--full-height") == 0) {
            st.fullHeightScene = 1;
        } else if (strcmp(argv[i], "--precreate") == 0) {
            precreate = 1;
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = atoi(argv[++i]);
        } else {
            printf("unknown argument %s\n", argv[i]);
            return 1;
        }
    }
    /* the dump's GS size (rd_replay_tool's peek: bytes 24..31 of the header) */
    FILE *probe = path ? fopen(path, "rb") : NULL;
    uint8_t hdr[32];
    if (!probe || fread(hdr, 1, sizeof(hdr), probe) != sizeof(hdr) ||
        memcmp(hdr, RD_DUMP_MAGIC, 8) != 0) {
        printf("%s: no such dump, or not an rd dump\n", path ? path : "(none)");
        if (probe) {
            fclose(probe);
        }
        return 1;
    }
    fclose(probe);
    const uint32_t gw = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8) | ((uint32_t)hdr[26] << 16) |
                        ((uint32_t)hdr[27] << 24);
    const uint32_t gh = (uint32_t)hdr[28] | ((uint32_t)hdr[29] << 8) | ((uint32_t)hdr[30] << 16) |
                        ((uint32_t)hdr[31] << 24);
    if (!rd_init(gw, gh, &st, NULL)) {
        printf("no Vulkan device: skipped\n");
        return 77;
    }
    rd__set_not_implemented_fatal(false);
    if (precreate) {
        rd_precreate_pipelines(); /* as the window does after rd_init */
    }
    RdFrame f;
    if (!rd__load_frame(path, &f)) {
        rd_shutdown();
        return 1;
    }
    unsigned seen = 0;
    Sum first, rest;
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    RdPerfRecord last;
    memset(&last, 0, sizeof(last));
    for (int i = 0; i < repeat + RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__replay_frame(&f, (int)f.keep, true);
        RdPerfRecord r;
        while (rd_perf_pop(&r)) {
            add(seen < 1 ? &first : &rest, &r);
            if (seen > 0) {
                last = r;
            }
            seen++;
        }
    }
    printf("%s (frame %u, %ux%u):\n", path, f.number, f.gsW, f.gsH);
    print("  first replay", &first);
    print("  steady", &rest);
    printf("  last: %u draws, %u passes, %u pipeline binds, %u bind group binds, %u bind groups "
           "(%u uniform, %u texture), "
           "%u barriers, %u copies, %llu KB uploaded (%llu KB meshes), %u DATE snapshots, %u "
           "fence waits (%.3f ms), screen prims %u draws merged into %u\n",
           last.draws, last.renderPasses, last.pipelineBinds, last.bindGroupBinds, last.bindGroups,
           last.uniformGroups, last.textureGroups, last.barriers, last.copies,
           (unsigned long long)(last.uploadBytes / 1024),
           (unsigned long long)(last.meshUploadBytes / 1024), last.dateSnapshots, last.fenceWaits,
           last.fenceWaitMs, last.screenCmds, last.screenDraws);
    if (last.gpuValid) {
        printf("  GPU %.3f ms: uploads %.3f, lists", last.gpuMs, last.gpuUploadMs);
        for (int l = 0; l < 13; l++) {
            printf(" %.3f", last.gpuListMs[l]);
        }
        printf(", present %.3f\n", last.gpuPresentMs);
    }
    rd__frame_free(&f);
    rd_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (argc > 1) {
        return dumpMode(argc, argv);
    }
    checkPassTexts(); /* pure: before any device */
    const int rc = synthetic();
    if (rc != 0) {
        return rc;
    }
    if (s_fail) {
        printf("rd_perf: %d failure(s)\n", s_fail);
        return 1;
    }
    printf("rd_perf: ok\n");
    return 0;
}
