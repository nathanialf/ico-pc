/* rd_date_cut_test.c: DATE's passes without the depth their draws never
 * touch (rd_date_cut.c, rd_date_cut.h; docs/port/WORK_BUFFERS.md, "DATE's
 * passes without the depth").
 *
 *   rd_date_cut_test
 *
 * No device (the rule, rd_date_cut.h, on made-up commands):
 *   r1  the shadow's three composites on SCENE (DATE, Z ALWAYS, no Z write,
 *       alpha written) then a mesh draw on SCENE: the runs of the first two
 *       are depth-free (each ends at the next composite, whose snapshot is
 *       retaken), the third's ends undecided at the mesh draw;
 *   r2  a draw that tests the depth between two DATE draws: the first run
 *       ends undecided (its pass keeps the depth), the second closes at the
 *       replay's end;
 *   r3  a DATE draw after a run wrote no alpha (its snapshot is reused, no
 *       pass ends): in the run; a draw on another target ends it;
 *   r4  a post sprite, a copy or a clear in a run: undecided; a draw on
 *       another target after a run: depth-free;
 *   r5  no run opens at a DATE draw without a depth target, one that tests
 *       or writes the depth, or a PRIM.AA1 / depth-view draw (depthFree off);
 *   r6  the cut takes integer scales only (rd_date_cut_scale_ok: 1x, 2x, 3x
 *       yes; the PowerVR phone's 2.79 x 2.41, 3 x 2.5 and 0.5 no).
 * On a Vulkan device (77 without one, after the checks above), DATE by the
 * R8 snapshot:
 *   k   every screen-family key with the depth attached, its test ALWAYS,
 *       its write off and no stencil has its twin without the depth in
 *       rd__enumerate_reachable_screen (the keys a draw of a depth-free run
 *       takes, so the cut makes no pipeline during play);
 * and with the snapshot forced (ICO_RD_DATE_PATH=snapshot), the fixed-function blend
 * (ICO_RD_BLEND=fixed) and the store plan's poison (ICO_RD_STORE_POISON=1:
 * a depth load the plan leaves undefined clears to 0.5):
 *   d   a frame in the composites' shape (list 0 writes SCENE's depth over
 *       its left half, list 3 draws three DATE composites, list 4 a Z
 *       GEQUAL sprite that shows the depth in the colour), and two variants:
 *       a Z GEQUAL sprite right after the third composite in its pass, and
 *       one between the first two; each replayed with the cut off and on
 *       (rd__date_cut_force): SCENE's bytes the same; the scan's runs exactly
 *       the rule's, by ordinal (rd__date_cut_last_runs: [c1, c2) and [c2, c3),
 *       or [c2, c3) alone with the draw between; round 2's defect read every
 *       run from command 0, so list 0's draws lost their depth and the
 *       shadow count its stencil); one pass begun without the depth a run,
 *       none begun again; the same passes and 3 snapshots (1 the first, 2
 *       after an alpha write); the passes with a depth fewer by the runs (list
 *       0's and the third composite's keep it), the depth stores the store
 *       plan dropped the same; the depth and stencil bytes asked lower by 4 to
 *       5 bytes a texel each way of each cut pass; the 10 s text's
 *       "; DATE: 2.0 passes" (the first frame); no validation error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_cut.h"
#include "rd_internal.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------ the rule */

enum { T_SCENE = 1, T_WORK = 2 };

static RdDateCutCmd cmdState(void)
{
    RdDateCutCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = RD_DC_STATE;
    c.target = T_SCENE;
    return c;
}

/* a screen prim on target t: DATE, depth-free, alpha written, a depth target */
static RdDateCutCmd cmdScreen(uint32_t t, bool date, bool depthFree, bool alpha, bool hasDepth)
{
    RdDateCutCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = RD_DC_SCREEN;
    c.target = t;
    c.date = date;
    c.depthFree = depthFree;
    c.alpha = alpha;
    c.hasDepth = hasDepth;
    return c;
}

static RdDateCutCmd cmdKind(uint8_t kind, uint32_t t)
{
    RdDateCutCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.target = t;
    return c;
}

/* the runs a command sequence gives: free[] the depth-free [start, end)
 * pairs, the undecided count */
typedef struct Runs {
    uint32_t n, start[8], end[8], undecided;
} Runs;

static Runs runRule(const RdDateCutCmd *cmd, uint32_t n)
{
    Runs r;
    memset(&r, 0, sizeof(r));
    RdDateCutScan s;
    memset(&s, 0, sizeof(s));
    for (uint32_t i = 0; i <= n; i++) {
        uint32_t start = 0;
        const int did =
            i < n ? rd_date_cut_step(&s, &cmd[i], i, &start) : rd_date_cut_finish(&s, i, &start);
        if (did == RD_DC_STEP_FREE && r.n < 8u) {
            r.start[r.n] = start;
            r.end[r.n++] = i;
        } else if (did == RD_DC_STEP_UNDEC) {
            r.undecided++;
        }
    }
    return r;
}

static void testRule(void)
{
    /* r1: the composites, then the mesh draws of list 4 */
    const RdDateCutCmd r1[] = {
        cmdState(),
        cmdScreen(T_SCENE, true, true, true, true), /* 1: composite 1 */
        cmdState(),
        cmdState(),
        cmdScreen(T_SCENE, true, true, true, true), /* 4: composite 2 */
        cmdState(),
        cmdScreen(T_SCENE, true, true, true, true), /* 6: composite 3 */
        cmdState(),
        cmdKind(RD_DC_DRAW, T_SCENE), /* 8: list 4's mesh */
    };
    Runs r = runRule(r1, sizeof(r1) / sizeof(r1[0]));
    CHECK(r.n == 2 && r.start[0] == 1 && r.end[0] == 4 && r.start[1] == 4 && r.end[1] == 6 &&
              r.undecided == 1,
          "r1: %u runs ([%u,%u) [%u,%u)), %u undecided; 2 ([1,4) [4,6)) and 1 expected", r.n,
          r.start[0], r.end[0], r.start[1], r.end[1], r.undecided);

    /* r2: a draw that tests the depth between two DATE draws */
    const RdDateCutCmd r2[] = {
        cmdScreen(T_SCENE, true, true, true, true),   /* 0 */
        cmdScreen(T_SCENE, false, false, true, true), /* 1: Z GEQUAL */
        cmdScreen(T_SCENE, true, true, true, true),   /* 2 */
        cmdState(),
    };
    r = runRule(r2, sizeof(r2) / sizeof(r2[0]));
    CHECK(r.n == 1 && r.start[0] == 2 && r.end[0] == 4 && r.undecided == 1,
          "r2: %u runs (first [%u,%u)), %u undecided; [2,4) and 1 expected", r.n, r.start[0],
          r.end[0], r.undecided);

    /* r3: no alpha written before the second DATE draw: one run, ended by a
     * draw on another target */
    const RdDateCutCmd r3[] = {
        cmdScreen(T_SCENE, true, true, false, true),  /* 0: colour mask without A */
        cmdScreen(T_SCENE, true, true, true, true),   /* 1: the snapshot reused: in the run */
        cmdScreen(T_SCENE, false, true, true, true),  /* 2: a plain depth-free sprite */
        cmdScreen(T_WORK, false, false, true, false), /* 3: another target */
    };
    r = runRule(r3, sizeof(r3) / sizeof(r3[0]));
    CHECK(r.n == 1 && r.start[0] == 0 && r.end[0] == 3 && r.undecided == 0,
          "r3: %u runs (first [%u,%u)), %u undecided; [0,3) and 0 expected", r.n, r.start[0],
          r.end[0], r.undecided);

    /* r4: a post sprite in a run; then a run ended by a draw elsewhere */
    const RdDateCutCmd r4[] = {
        cmdScreen(T_SCENE, true, true, true, true), /* 0 */
        cmdKind(RD_DC_OTHER, T_SCENE),              /* 1: a post sprite */
        cmdScreen(T_SCENE, true, true, true, true), /* 2 */
        cmdKind(RD_DC_DRAW, T_WORK),                /* 3: a VU draw on another target */
        cmdScreen(T_SCENE, true, true, true, true), /* 4 */
        cmdKind(RD_DC_OTHER, T_WORK),               /* 5: a copy elsewhere */
    };
    r = runRule(r4, sizeof(r4) / sizeof(r4[0]));
    CHECK(r.n == 1 && r.start[0] == 2 && r.end[0] == 3 && r.undecided == 2,
          "r4: %u runs (first [%u,%u)), %u undecided; [2,3) and 2 expected", r.n, r.start[0],
          r.end[0], r.undecided);

    /* r5: nothing opens a run */
    const RdDateCutCmd r5[] = {
        cmdScreen(T_SCENE, true, true, true, false), /* no depth target */
        cmdScreen(T_SCENE, true, false, true, true), /* tests or writes the depth, AA1 */
        cmdScreen(T_SCENE, false, true, true, true), /* no DATE */
        cmdState(),
    };
    r = runRule(r5, sizeof(r5) / sizeof(r5[0]));
    CHECK(r.n == 0 && r.undecided == 0, "r5: %u runs, %u undecided; none expected", r.n,
          r.undecided);
    /* r6: the scales the cut takes (rd_date_cut_scale_ok): whole texels a
     * GS pixel only; the PowerVR phone's Resolution Auto (2364x1080 for
     * 848x448: 2.79 x 2.41), where lavapipe drew a composite one level apart
     * with and without the depth, is refused */
    CHECK(rd_date_cut_scale_ok(1.0f, 1.0f) && rd_date_cut_scale_ok(2.0f, 2.0f) &&
              rd_date_cut_scale_ok(3.0f, 3.0f),
          "r6: an integer scale refused");
    CHECK(!rd_date_cut_scale_ok(2364.0f / 848.0f, 1080.0f / 448.0f) &&
              !rd_date_cut_scale_ok(3.0f, 2.5f) && !rd_date_cut_scale_ok(0.5f, 0.5f),
          "r6: a fractional scale taken");
    printf("  the rule: r1-r6 checked\n");
}

/* ------------------------------------------------------ the start-up set */

static void testKeys(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_REACHABLE_MAX];
    const uint32_t n = rd__enumerate_reachable_screen(keys, RD_PIPELINE_REACHABLE_MAX);
    CHECK(n > 0 && n < RD_PIPELINE_REACHABLE_MAX, "the screen family: %u keys", n);
    uint32_t pairs = 0, missing = 0;
    for (uint32_t i = 0; i < n && i < RD_PIPELINE_REACHABLE_MAX; i++) {
        const RdPipeKeyInt *k = &keys[i];
        if (k->depthFmt != RHI_FMT_D32F_S8 || k->gs.ztst != RD_ZTST_ALWAYS ||
            k->gs.zwrite != RD_ZWRITE_OFF || k->gs.stencil != RD_STENCIL_OFF ||
            k->dst != RHI_DST_READ_NONE) {
            continue;
        }
        RdPipeKeyInt t = *k;
        t.depthFmt = RHI_FMT_UNKNOWN;
        bool found = false;
        for (uint32_t j = 0; j < n && j < RD_PIPELINE_REACHABLE_MAX && !found; j++) {
            found = rd__pipe_key_equal(&keys[j], &t);
        }
        pairs += found;
        if (!found && missing++ < 4u) {
            printf("  key without its no-depth twin: program %u blend %u vs %u fs %u mask %x\n",
                   k->gs.program, k->gs.blend, k->vs, k->fs, k->gs.colorMask);
        }
    }
    CHECK(missing == 0 && pairs > 0,
          "%u screen keys with an unused depth lack their twin (%u have it)", missing, pairs);
    printf("  the start-up set: %u screen keys with an unused depth, each with its twin\n", pairs);
}

/* ------------------------------------------------------- on a device */

static void frameBegin(RdFrame *f, uint32_t number, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->number = number;
    f->gsW = w;
    f->gsH = h;
}

/* a sprite over columns x0..x1 of a w x h frame at GS Z z, colour rgba */
static bool sprite(RdFrame *f, int list, uint32_t w, uint32_t h, uint32_t x0, uint32_t x1,
                   uint32_t z, const uint8_t rgba[4])
{
    RdScreenVtx v[2];
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, sizeof(v));
    v[0].x = (ox + (int32_t)x0) * 16;
    v[0].y = oy * 16;
    v[1].x = (ox + (int32_t)x1) * 16;
    v[1].y = (oy + (int32_t)h) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].q = 1.0f;
        v[k].z = z;
        memcpy(v[k].rgba, rgba, 4);
    }
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)sizeof(v));
    c->u[1] = 2;
    return true;
}

static bool state(RdFrame *f, int list, uint64_t test, uint8_t zwrite, bool blend)
{
    RdCmd *a = rd__st_push(f, list, RDC_ALPHA);
    if (a) {
        a->b[0] = RD_BLEND_LERP_AS;
        a->b[1] = 0;
    }
    return a && rd__st_push(f, list, RDC_TEXTURE_OFF) &&
           rd__st_push1(f, list, RDC_ABE, blend ? 1 : 0) && rd__st_test(f, list, test) &&
           rd__st_push1(f, list, RDC_ZWRITE, zwrite);
}

/* the frames' variants: the composites alone; a Z GEQUAL sprite right after
 * the third composite in list 3 (the same pass: its run ends undecided);
 * a Z GEQUAL sprite between the first two (the first run undecided, its
 * pass keeps the depth) */
enum { V_PLAIN, V_DEPTH_AFTER, V_DEPTH_BETWEEN, V_COUNT };

/* the composites' ordinals (the store plan's: list 0's commands, then list
 * 3's index; lists 1 and 2 are empty) */
typedef struct Ords {
    uint32_t comp[3];
} Ords;

/* the composites' shape: list 0 clears SCENE and writes its depth (Z 0x1000)
 * and an alpha with the MSB over the left half; list 3 three DATE DATM 0
 * composites over the frame (alpha 0x30, 0x50, 0x90: the right half takes
 * them); list 4 a Z GEQUAL sprite at Z 0x800 (drawn where the depth is
 * under it: the right half, unless the depth was lost); the variant's Z
 * GEQUAL sprite at Z 0x800 in list 3 */
static bool buildComposites(RdFrame *f, uint32_t number, uint32_t scene, uint32_t w, uint32_t h,
                            int variant, Ords *o)
{
    static const uint8_t kLeft[4] = {0x40, 0x40, 0x40, 0x80};
    static const uint8_t kComp[3][4] = {
        {0x80, 0x20, 0x20, 0x30}, {0x20, 0x80, 0x20, 0x50}, {0x20, 0x20, 0x80, 0x90}};
    static const uint8_t kMid[4] = {0x60, 0x10, 0x10, 0x20};
    static const uint8_t kLate[4] = {0x10, 0x60, 0x60, 0x80};
    frameBegin(f, number, w, h);
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = ok ? rd__st_push(f, 0, RDC_CLEAR) : NULL;
    if (c) {
        static const uint8_t kClear[4] = {0x08, 0x10, 0x18, 0x00};
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && state(f, 0, RD_TEST_Z_GEQUAL, RD_ZWRITE_ON, false) &&
         sprite(f, 0, w, h, 0, w / 2, 0x1000, kLeft);
    ok = ok && rd__st_target(f, 3, scene, w, h) &&
         state(f, 3, RD_TEST_AT_GT0_DATE0, RD_ZWRITE_OFF, true);
    for (int i = 0; i < 3 && ok; i++) {
        o->comp[i] = f->lists[0].count + f->lists[3].count;
        ok = sprite(f, 3, w, h, 0, w, 0, kComp[i]) && rd__st_push1(f, 3, RDC_ABE, 1);
        const bool mid =
            (variant == V_DEPTH_BETWEEN && i == 0) || (variant == V_DEPTH_AFTER && i == 2);
        if (ok && mid) {
            ok = state(f, 3, RD_TEST_Z_GEQUAL, RD_ZWRITE_ON, false) &&
                 sprite(f, 3, w, h, 0, w, 0x800, kMid) &&
                 state(f, 3, RD_TEST_AT_GT0_DATE0, RD_ZWRITE_OFF, true);
        }
    }
    ok = ok && rd__st_target(f, 4, scene, w, h) &&
         state(f, 4, RD_TEST_Z_GEQUAL, RD_ZWRITE_ON, false) &&
         sprite(f, 4, w, h, 0, w, 0x800, kLate);
    return ok;
}

static uint8_t *readScene(uint32_t *w, uint32_t *h)
{
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t) {
        return NULL;
    }
    const size_t n = (size_t)t->tw * t->th * 4u;
    uint8_t *px = malloc(n);
    if (px && !rd__read_target(rd_target(RD_TARGET_SCENE), px, n, w, h)) {
        free(px);
        px = NULL;
    }
    return px;
}

typedef struct Took {
    RdPerfRecord r;
    uint8_t *px;
    uint32_t w, h;
} Took;

static bool replayOnce(const RdFrame *f, int cut, Took *out)
{
    rd__date_cut_force(cut);
    /* two replays: the second's record is the steady one (the first may
     * clear new targets) */
    bool ok = rd__replay_frame(f, 0, false) && rd__replay_frame(f, 0, false);
    out->r = g_rdPerf;
    out->px = ok ? readScene(&out->w, &out->h) : NULL;
    printf("  cut %d: %u passes, %u DATE snapshots (%u first, %u after an alpha write), %u runs, "
           "%u passes without the depth, %u begun again, depth %llu/%llu bytes loaded/stored\n",
           cut, out->r.renderPasses, out->r.dateSnapshots,
           out->r.dateRetakeWhy[RD_DATE_RETAKE_FIRST], out->r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN],
           out->r.dateCutRuns, out->r.dateCutPasses, out->r.dateCutAgain,
           (unsigned long long)out->r.depthLoadBytes, (unsigned long long)out->r.depthStoreBytes);
    return ok && out->px != NULL;
}

/* one variant with the cut off and on: the expectations from the rule
 * (rd_date_cut.h): a run opens at each composite (DATE, Z ALWAYS, no Z
 * write, alpha written) and is depth-free when the next composite comes with
 * nothing but state between (its snapshot retaken: the pass ends); a Z
 * GEQUAL sprite in a run ends it undecided; list 4's Z GEQUAL sprite ends
 * the third one undecided.  So: V_PLAIN and V_DEPTH_AFTER the runs [c1, c2)
 * and [c2, c3); V_DEPTH_BETWEEN [c2, c3) alone.  Each depth-free run is one
 * pass begun without the depth (its first draw follows a snapshot, which
 * ended the pass before); the passes and snapshots are the cut off's; the
 * passes with a depth fewer by those (list 0's and the third composite's
 * keep it: the count's stencil and the late Z test read it); the depth and
 * stencil bytes fewer by 4 to 5 a texel each way of each */
static void testVariant(uint32_t scene, const RdTargetRec *t, uint32_t w, uint32_t h, int variant)
{
    static const char *const kName[V_COUNT] = {"the composites", "a depth draw after the third",
                                               "a depth draw between the first two"};
    static RdFrame f;
    Ords o;
    memset(&o, 0, sizeof(o));
    CHECK(buildComposites(&f, 1u + (uint32_t)variant, scene, w, h, variant, &o),
          "%s: the frame built", kName[variant]);
    uint32_t wantStart[2], wantEnd[2], wantN = 0;
    if (variant != V_DEPTH_BETWEEN) {
        wantStart[wantN] = o.comp[0];
        wantEnd[wantN++] = o.comp[1];
    }
    wantStart[wantN] = o.comp[1];
    wantEnd[wantN++] = o.comp[2];
    Took off, on;
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    printf("  %s (composites at %u, %u, %u):\n", kName[variant], o.comp[0], o.comp[1], o.comp[2]);
    CHECK(replayOnce(&f, 0, &off), "%s: replayed with the cut off", kName[variant]);
    CHECK(rd__date_cut_last_runs(NULL, NULL, 0) == 0, "%s: the cut off found runs", kName[variant]);
    CHECK(replayOnce(&f, 1, &on), "%s: replayed with the cut on", kName[variant]);
    uint32_t start[4] = {0}, end[4] = {0};
    const uint32_t n = rd__date_cut_last_runs(start, end, 4);
    bool same = n == wantN;
    for (uint32_t i = 0; i < n && i < 4 && same; i++) {
        same = start[i] == wantStart[i] && end[i] == wantEnd[i];
    }
    CHECK(same, "%s: %u runs ([%u,%u) [%u,%u)), %u expected ([%u,%u) [%u,%u))", kName[variant], n,
          start[0], end[0], start[1], end[1], wantN, wantStart[0], wantEnd[0],
          wantN > 1 ? wantStart[1] : 0u, wantN > 1 ? wantEnd[1] : 0u);
    if (off.px && on.px) {
        CHECK(off.w == on.w && off.h == on.h &&
                  memcmp(off.px, on.px, (size_t)off.w * off.h * 4u) == 0,
              "%s: SCENE differs with the cut on", kName[variant]);
    }
    CHECK(off.r.dateCutPasses == 0 && off.r.dateCutRuns == 0, "%s: the cut off cut %u passes",
          kName[variant], off.r.dateCutPasses);
    CHECK(on.r.dateCutRuns == wantN && on.r.dateCutPasses == wantN && on.r.dateCutAgain == 0,
          "%s: the cut on: %u runs, %u passes, %u begun again; %u, %u and 0 expected",
          kName[variant], on.r.dateCutRuns, on.r.dateCutPasses, on.r.dateCutAgain, wantN, wantN);
    CHECK(on.r.renderPasses == off.r.renderPasses && on.r.dateSnapshots == off.r.dateSnapshots &&
              on.r.dateSnapshots == 3,
          "%s: passes %u and %u, snapshots %u and %u (the same, 3 snapshots expected)",
          kName[variant], off.r.renderPasses, on.r.renderPasses, off.r.dateSnapshots,
          on.r.dateSnapshots);
    CHECK(on.r.storeDepthPasses + wantN == off.r.storeDepthPasses,
          "%s: passes with the depth %u off, %u on: %u fewer expected", kName[variant],
          off.r.storeDepthPasses, on.r.storeDepthPasses, wantN);
    CHECK(on.r.storeDepthDropped == off.r.storeDepthDropped,
          "%s: depth stores dropped %u off, %u on (the store plan's the same)", kName[variant],
          off.r.storeDepthDropped, on.r.storeDepthDropped);
    CHECK(on.r.dateRetakeWhy[RD_DATE_RETAKE_FIRST] == 1 &&
              on.r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN] == 2 && on.r.dateSnapReused == 0,
          "%s: the snapshots' reasons: %u first, %u written, %u reused (1, 2, 0 expected)",
          kName[variant], on.r.dateRetakeWhy[RD_DATE_RETAKE_FIRST],
          on.r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN], on.r.dateSnapReused);
    const uint64_t texels = (uint64_t)t->tw * t->th * wantN;
    const uint64_t lessLoad = off.r.depthLoadBytes - on.r.depthLoadBytes;
    const uint64_t lessStore = off.r.depthStoreBytes - on.r.depthStoreBytes;
    CHECK(on.r.dateCutTexels == texels, "%s: %llu texels counted, %llu expected", kName[variant],
          (unsigned long long)on.r.dateCutTexels, (unsigned long long)texels);
    CHECK(off.r.depthLoadBytes > on.r.depthLoadBytes && lessLoad >= texels * 4u &&
              lessLoad <= texels * 5u,
          "%s: depth and stencil loads %llu -> %llu bytes: %llu less, %llu to %llu expected",
          kName[variant], (unsigned long long)off.r.depthLoadBytes,
          (unsigned long long)on.r.depthLoadBytes, (unsigned long long)lessLoad,
          (unsigned long long)(texels * 4u), (unsigned long long)(texels * 5u));
    CHECK(off.r.depthStoreBytes > on.r.depthStoreBytes && lessStore >= texels * 4u &&
              lessStore <= texels * 5u,
          "%s: depth and stencil stores %llu -> %llu bytes: %llu less, %llu to %llu expected",
          kName[variant], (unsigned long long)off.r.depthStoreBytes,
          (unsigned long long)on.r.depthStoreBytes, (unsigned long long)lessStore,
          (unsigned long long)(texels * 4u), (unsigned long long)(texels * 5u));
    if (variant == V_PLAIN) {
        /* the 10 s text: the records popped with their presents */
        rd__date_cut_force(1);
        for (int i = 0; i < RHI_FRAMES_IN_FLIGHT + 1; i++) {
            (void)rd__replay_frame(&f, 0, true);
        }
        while (rd_perf_pop(NULL)) {}
        (void)rd_perf_backend_text();
        for (int i = 0; i < 4 + RHI_FRAMES_IN_FLIGHT; i++) {
            (void)rd__replay_frame(&f, 0, true);
            while (rd_perf_pop(NULL)) {}
        }
        const char *text = rd_perf_backend_text();
        printf("  10 s text: %s\n", text);
        CHECK(strstr(text, "; DATE: 2.0 passes a replay begun without the depth") != NULL &&
                  strstr(text, "an alpha write since 2.0") != NULL,
              "the 10 s text: %s", text);
    }
    free(off.px);
    free(on.px);
    rd__date_cut_force(-1);
    rd__frame_free(&f);
}

static void testDevice(uint32_t w, uint32_t h)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *t = rd__target_rec(scene);
    CHECK(t && t->withDepth && t->depth.id, "SCENE with a depth");
    CHECK(!g_rd.profile.dateStencil && g_rd.dstMode == RD_BLEND_EXACT_OFF,
          "DATE by the snapshot (%d) and the fixed-function blend (%u) in force",
          (int)!g_rd.profile.dateStencil, (unsigned)g_rd.dstMode);
    if (!t || !t->withDepth) {
        return;
    }
    for (int v = 0; v < V_COUNT; v++) {
        testVariant(scene, t, w, h, v);
    }
}

static void setEnv(const char *k, const char *v)
{
#ifdef _WIN32
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

int main(void)
{
    printf("rd_date_cut_test\n");
    testRule();
    setEnv("ICO_RD_DATE_PATH", "snapshot");
    setEnv("ICO_RD_BLEND", "fixed");
    setEnv("ICO_RD_STORE_POISON", "1");
    const uint32_t w = 640, h = 448;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 16.0f / 9.0f;
    st.sceneScale = 2.0f;
    st.fullPixel = 1;
    st.outputWidth = 1280;
    st.outputHeight = 720;
    if (!rd_init(w, h, &st, NULL)) {
        if (failures) {
            printf("rd_date_cut_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_date_cut_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    testKeys();
    testDevice(w, h);
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_date_cut_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_date_cut_test: ok\n");
    return 0;
}
