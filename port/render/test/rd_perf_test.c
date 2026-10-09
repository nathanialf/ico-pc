/* rd_perf_test (package P1): the
 * renderer's CPU cost per replay and its steady state.
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
 *             replay without its wait for the GPU (rhi_WaitFrame: on
 *             lavapipe that is the rasterisation) is under 20 ms; and
 *             (package PA) every replay, here and below, creates one
 *             uniform bind group per uniform layout (frame, draw, VU),
 *             however many draws; (package PB) a steady replay records
 *             exactly BARRIERS_REPLAY pipeline barriers (BARRIERS_RECORD
 *             below); (package PC) the screen-prim commands make
 *             SCREEN_CMDS draws one by one and SCREEN_DRAWS merged;
 *   record    200 frames recorded and closed as the game does (rd_EndFrame
 *             replays each): from the fifth on, when the frame ring and the
 *             temporary target pool are warm, nothing is created or
 *             destroyed either, and only the morphing mesh is uploaded.
 *
 * With a dump instead (the measurement tool of the package):
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
        s_mesh[i] = rd_CreateVuMesh(&d);
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
        s_tex[t] = rd_CreateTexture(64, 64, px, RD_TEXA_80_80, "perf texture");
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
    RdTarget scene = rd_Target(RD_TARGET_SCENE);

    rd_BeginFrame();
    /* list 0: the scene */
    rd_SelectList(0);
    rd_SetTarget(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_ClearTarget(scene, bg, 1, 0);
    rd_TestGs(0x50000);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
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
        rd_Texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        const int m = i % MESHES;
        if (m == 0 && i == 0) {
            CHECK(rd_UpdateVuMesh(s_mesh[0], (const float (*)[4])s_morph), "a morph"); /* a morph */
        }
        rd_DrawVuMesh(s_mesh[m], &d, RD_KEY(1, (uint32_t)i, 0));
    }
    /* world strips and UI sprites */
    rd_Texture(s_tex[1], RD_TEXFN_MODULATE, RD_TCC_RGBA);
    for (int i = 0; i < 20; i++) {
        RdScreenVtx s[8];
        for (int k = 0; k < 8; k++) {
            s[k] = vtx(20 + i * 20 + (k / 2) * 6, 40 + (k & 1) * 30, 0x1000000u + (uint32_t)i,
                       (float)k / 8.0f, (float)(k & 1), 100, 0x80);
        }
        rd_ScreenPrims(RD_PRIM_TRIANGLE_STRIP, s, 8, RD_SPACE_WORLD, 0, RD_KEY(2, (uint32_t)i, 0));
    }
    rd_SelectList(11);
    rd_SetTarget(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_TestGs(0x30000);
    for (int i = 0; i < 60; i++) {
        RdScreenVtx s[2] = {
            vtx(10 + (i % 12) * 40, 300 + (i / 12) * 20, 0xFFFFFFFFu, 0.0f, 0.0f, 128, 0x80),
            vtx(40 + (i % 12) * 40, 316 + (i / 12) * 20, 0xFFFFFFFFu, 64.0f * 16.0f, 64.0f * 16.0f,
                128, 0x80)};
        rd_Texture(s_tex[i & 3], RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_ScreenPrims(RD_PRIM_SPRITES, s, 2, RD_SPACE_UI, 1, RD_KEY(3, (uint32_t)i, 0));
    }
    /* list 4: a DATE draw after alpha writes (the specular passes' TEST) */
    rd_SelectList(4);
    rd_SetTarget(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_TestGs(0x5C000); /* DATE on, Z GEQUAL */
    for (int i = 0; i < 4; i++) {
        RdScreenVtx s[2] = {vtx(100 + i * 50, 100, 0x2000000u, 0, 0, 200, 0x80),
                            vtx(140 + i * 50, 160, 0x2000000u, 0, 0, 200, 0x80)};
        rd_TextureOff();
        rd_ScreenPrims(RD_PRIM_SPRITES, s, 2, RD_SPACE_WORLD, 0, 0);
    }
    /* list 3: the shadow count on its temporary target */
    rd_SelectList(3);
    RdTarget cnt = rd_ShadowCountTarget(W, H);
    rd_SetTarget(cnt, scene, W, H, 0);
    rd_ZWrite(0);
    rd_TestGs(0x30000);
    rd_ABE(0);
    rd_TextureOff();
    rd_ShadowReset();
    rd_TestGs(0x50000);
    rd_BlendFunc(RD_BLEND_CS_FIX_ADD_CD, 0x80);
    rd_ColClamp(0);
    {
        RdScreenVtx t[12];
        int8_t sign[4] = {1, 1, -1, 1};
        for (int k = 0; k < 12; k++) {
            t[k] = vtx(100 + (k % 3) * 60 + (k / 3) * 10, 100 + ((k % 3) == 2) * 80, 0x3000000u, 0,
                       0, 4, 0x80);
        }
        rd_ShadowTris(t, sign, 4, RD_KEY(4, 0, 0));
    }
    rd_ShadowResolve();
    rd_ColClamp(1);
    /* list 4 again: a block target with its own depth (a reflection) */
    rd_SelectList(4);
    RdTarget blk = rd_BlockTarget(0x2800, 256, 256, 1);
    rd_SetTarget(blk, blk, 256, 256, 0);
    rd_ClearTarget(blk, bg, 1, 0);
    rd_TestGs(0x50000);
    for (int i = 0; i < 8; i++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[4][0] = d.vu.mem[5][1] = d.vu.mem[6][2] = d.vu.mem[7][3] = 1.0f;
        d.vu.mem[7][0] = d.vu.mem[7][1] = 2048.0f;
        d.batchCount = 1;
        rd_DrawVuMesh(s_mesh[(i + 3) % MESHES], &d, RD_KEY(5, (uint32_t)i, 0));
    }
    rd_SetTarget(scene, scene, W, H, RD_TARGET_OFFSET);
    rd_Texture(rd_TargetTexture(blk, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    {
        RdScreenVtx s[2] = {vtx(300, 20, 0x2000000u, 0, 0, 128, 0x80),
                            vtx(428, 148, 0x2000000u, 256.0f * 16.0f, 256.0f * 16.0f, 128, 0x80)};
        rd_ScreenPrims(RD_PRIM_SPRITES, s, 2, RD_SPACE_WORLD, 1, 0);
    }
    /* list 12: the reduction */
    rd_SelectList(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = scene;
    pp.dst = rd_Target(RD_TARGET_DISPLAY);
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
    rd_Post(RD_POST_REDUCTION, &pp);
    (void)n;
}

/* ------------------------------------------------------------ the checks */

typedef struct Sum {
    unsigned n;
    double total, wait, upload, walk, bind, submit, present, gpu;
    double minTotal;
    uint64_t created, destroyed, allocs, waitIdles, texUploads, meshUploads, meshBytes, draws;
    /* package PA: bind groups created, per replay: all, and rd's uniform
     * and texture groups (the least and most of the uniform ones) */
    uint64_t groups, uniformGroups, textureGroups;
    uint32_t uniformMin, uniformMax;
    /* package PB: pipeline barriers recorded per replay (least, most) */
    uint32_t barrierMin, barrierMax;
    /* package PC: screen-prim draws per replay, one per command and merged
     * (least, most) */
    uint32_t screenCmdMin, screenCmdMax, screenDrawMin, screenDrawMax;
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
    while (rd_PerfPop(&r)) {
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
    if (px && !rd__ReadTarget(rd_Target(RD_TARGET_DISPLAY), px, cap, &w, &h)) {
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

/* Package PA: the uniform blocks take dynamic offsets, so every replay
 * creates one group per uniform layout it uses, whatever its draw count:
 * the frame (FrameCB), draw (DrawCB) and VU (DrawCB, VuCB, VuBoneCB over
 * the stream's buffer) layouts, all three in this frame, whose meshes live
 * in one arena chunk.  The other groups are texture groups, one per
 * (texture, sampler, DATE snapshot). */
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

/* Package PB: the pipeline barriers of a steady replay.  The Vulkan backend
 * orders same-state writes per resource: one list-opening barrier and one per pass or copy whose target
 * has a write pending, instead of a global barrier before every pass and
 * copy (ICO_VK_GLOBAL_BARRIERS=1, the _GLOBAL counts).  A backend
 * without counters (D3D12) reads 0 and is not checked.  v0.4.2 (N2): two
 * fewer in every mode (36, 39, 44, 48 before), the target clears now taken
 * as the next pass's load op instead of passes of their own.  The counts
 * are vkCmdPipelineBarrier calls: the image transitions recorded together
 * go out in one call, joined to the next pass's or copy's own barrier
 * (vk_cmd.c, vkr_FlushBarriers; 34 and 37 when each went out alone).  The
 * global mode records each transition in a call of its own, as before.
 * The shadow reset is the next pass's stencil clear, not a pass of its own,
 * so one fewer still; 18 and 21 are the steady-state counts this test
 * printed after those changes. */
#define BARRIERS_REPLAY 18
#define BARRIERS_RECORD 21
#define BARRIERS_REPLAY_GLOBAL 42
#define BARRIERS_RECORD_GLOBAL 46

static void checkBarriers(const char *what, const Sum *s, uint32_t tracked, uint32_t global)
{
    const char *env = getenv("ICO_VK_GLOBAL_BARRIERS");
    const uint32_t want = env && env[0] && env[0] != '0' ? global : tracked;
    if (s->n > 0 && s->barrierMax == 0) {
        printf("%s: the backend counts no barriers: not checked\n", what);
        return;
    }
    CHECK(s->n > 0 && s->barrierMin == want && s->barrierMax == want,
          "%s: %u..%u pipeline barriers a replay, %u expected", what, s->barrierMin, s->barrierMax,
          want);
}

/* Package PC: the screen-prim draws of a replay.  One per command: the 20
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

static int synthetic(void)
{
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    st.outputWidth = 640;
    st.outputHeight = 480;
    if (!rd_Init(W, H, &st, NULL)) {
        printf("no Vulkan device: skipped\n");
        return 77;
    }
    makeAssets();

    /* replay: one recording, 200 replays (rd_EndFrame replays it once) */
    recordFrame(0);
    rd_EndFrame(0);
    const RdFrame *f = rd__LastFrame();
    CHECK(f != NULL, "no closed frame");
    unsigned seen = 0;
    Sum first, rest;
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    for (int i = 0; i < 200 && f; i++) {
        rd__ReplayFrame(f, 0, true);
        drain(&first, &rest, 2, &seen);
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__ReplayFrame(f, 0, true); /* flush the last records' timestamps */
        drain(&first, &rest, 2, &seen);
    }
    /* the pixels: the first replay of a fresh frame (pooled targets cleared,
     * meshes uploaded) and the 200th (everything reused) agree */
    uint8_t *ref = NULL;
    {
        recordFrame(0);
        rd_EndFrame(0);
        ref = readDisplay();
        rd__ReplayFrame(rd__LastFrame(), 0, true);
        uint8_t *again = readDisplay();
        samePixels("replayed again", ref, again);
        free(again);
        RdPerfRecord r;
        while (rd_PerfPop(&r)) {}
    }
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
    CHECK(rest.n > 0 && (rest.total - rest.wait) / rest.n < 20.0,
          "replay: %.3f ms of CPU a replay (without the GPU wait), over 20 ms",
          rest.n ? (rest.total - rest.wait) / rest.n : 0.0);
    checkGroups("replay", &rest);
    checkBarriers("replay", &rest, BARRIERS_REPLAY, BARRIERS_REPLAY_GLOBAL);
    checkScreen("replay", &rest);

    /* record: 200 frames as the game makes them */
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    seen = 0;
    for (uint32_t i = 1; i <= 200; i++) {
        recordFrame(i);
        rd_EndFrame(0);
        drain(&first, &rest, 4, &seen);
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
        recordFrame(0);
        rd_EndFrame(0);
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
    CHECK(rest.n > 0 && (rest.total - rest.wait) / rest.n < 20.0,
          "record: %.3f ms of CPU a replay (without the GPU wait), over 20 ms",
          rest.n ? (rest.total - rest.wait) / rest.n : 0.0);
    checkGroups("record", &rest);
    checkBarriers("record", &rest, BARRIERS_RECORD, BARRIERS_RECORD_GLOBAL);
    checkScreen("record", &rest);

    /* v0.4.2 (Android): the reachable set, as the window creates it at
       start-up (rd_PrecreatePipelines): every key makes a pipeline.
       rd_perf_mali runs this on a Mali-G68's limits (ICO_VK_FAKE_LIMITS=
       mali), where a key past them would be refused by the limit's name */
    {
        static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
        const uint32_t n = rd__EnumerateReachable(keys, RD_PIPELINE_CACHE_MAX);
        uint32_t missing = 0;

        rd_PrecreatePipelines();
        for (uint32_t i = 0; i < n; i++) {
            if (!rd__GetPipeline(&keys[i]).id) {
                missing++;
            }
        }
        printf("reachable set: %u keys, %u without a pipeline\n", n, missing);
        CHECK(n > 0 && missing == 0, "reachable set: %u of %u keys without a pipeline", missing, n);
    }
    rd_Shutdown();
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
    if (!rd_Init(gw, gh, &st, NULL)) {
        printf("no Vulkan device: skipped\n");
        return 77;
    }
    rd__SetNotImplementedFatal(false);
    if (precreate) {
        rd_PrecreatePipelines(); /* as the window does after rd_Init */
    }
    RdFrame f;
    if (!rd__LoadFrame(path, &f)) {
        rd_Shutdown();
        return 1;
    }
    unsigned seen = 0;
    Sum first, rest;
    memset(&first, 0, sizeof(first));
    memset(&rest, 0, sizeof(rest));
    RdPerfRecord last;
    memset(&last, 0, sizeof(last));
    for (int i = 0; i < repeat + RHI_FRAMES_IN_FLIGHT + 1; i++) {
        rd__ReplayFrame(&f, (int)f.keep, true);
        RdPerfRecord r;
        while (rd_PerfPop(&r)) {
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
    rd__FrameFree(&f);
    rd_Shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        return dumpMode(argc, argv);
    }
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
