/* rd_date_one_test.c: one DATE set-up for the shadow composites
 * (rd_date_one.c, rd_date_one.h) and the empty composites skipped
 * (rd_shadow_box.c rd__shadow_box_empty_skip; docs/port/RENDER_POST.md).
 *
 *   rd_date_one_test
 *
 * No device (the bound and the group rule, rd_date_one.h):
 *   b1  the retail stage data: shadowDepth 72 with shadowBlend 40, 80, 120 and
 *       shadowDepth 80 with 90, 100, 128 (the count's 0x80 through the three
 *       levels, each composite reading its level as Shadow.c does: SHADOW2
 *       with shadowBlend[3], SHADOW1 with [2], SHADOW0 with [1]): every
 *       composite keeps the alpha's top bit, the second and the third share;
 *   b2  the stage-setting debug menu's 128 x 128 (rd_date_one_alpha gives
 *       exactly 0x80 at every composite): the second and the third refused;
 *       128 with only shadowBlend[1] 128: the SHADOW0 composite writes 0x80
 *       but is the last one, so the two before share;
 *   b3  a DATM 1 composite, an FBA 1 composite and a composite in no
 *       composite form (AFAIL FB_ONLY: the box scan names it no composite, a
 *       command that ends the group) before another: none shares; a
 *       composite with no alpha written shares whatever its bound;
 *   b4  a draw between two composites, and a composite on another target,
 *       end the group; state between keeps it;
 *   b5  the 10 s clause and the --stats words from made-up records.
 * On a Vulkan device (77 without one, after the checks above), DATE by the
 * R8 snapshot (ICO_RD_DATE_PATH=snapshot), the fixed-function blend
 * (ICO_RD_BLEND=fixed) and the store plan's poison (ICO_RD_STORE_POISON=1),
 * 1x, a frame in the shadow's shape: list 0 writes SCENE's depth and an
 * alpha with the top bit over its left half; list 3 resets the count, draws
 * a rectangle's volume (two triangles in front), resolves it, blurs it into
 * SHADOW0 (one 2:1 sprite, vertex alpha shadowDepth) and composites SHADOW0
 * three times into SCENE under TEST 0x3400D (vertex alphas the blends);
 * list 4 a Z GEQUAL sprite:
 *   d1  shadowDepth 72, blends 40, 80, 120, one DATE set-up off against on
 *       (rd__date_one_force): SCENE byte for byte; snapshots 3 (1 the first,
 *       2 an alpha write since) against 1 (the first), 2 shared, 0 refused;
 *       render passes 4 fewer (two R8 passes and two SCENE passes); 2 R8
 *       draws fewer; the DATE cut's passes without the depth 2 off (the
 *       first two composites' runs) and 0 on (the run from the first
 *       composite goes on to list 4's Z GEQUAL sprite), none begun again;
 *   d2  shadowDepth 128, blends 128: on refuses both shares (3 snapshots, 0
 *       shared, 2 refused) and SCENE is off's byte for byte; forced past the
 *       bound (ICO_RD_DATE_ONE=force's mode) the two share and SCENE
 *       differs: the test can see a wrong share;
 *   d3  the same frame with no volume (the reset and the resolve alone: the
 *       volumes' box empty), the empty composites off against on
 *       (rd__shadow_box_empty_force; one DATE set-up off in both): SCENE byte
 *       for byte; 3 composites skipped, 3 with DATE; snapshots 3 against 0;
 *       5 render passes fewer (three R8 passes and the first two
 *       composites' SCENE passes; list 4's sprite begins the third's) and 3
 *       draws fewer (the R8 draws);
 * and no validation error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_one.h"
#include "rd_internal.h"
#include "rd_no_volume.h"
#include "rd_shadow_box.h"
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

/* the three composites of a stage: their levels' largest alpha from the
 * count's 0x80 through the levels' vertex alpha shadowDepth (SHADOW0, 1, 2
 * in turn), each composite reading its level as Shadow.c does (SHADOW2
 * first, with shadowBlend[3]) */
typedef struct Stage {
    uint32_t level[3]; /* SHADOW2, SHADOW1, SHADOW0 as composited */
    uint32_t amax[3];
} Stage;

static Stage stageOf(uint32_t depth, const uint32_t blend[4])
{
    Stage s;
    const uint32_t l0 = rd_date_one_alpha(0x80, depth, false);
    const uint32_t l1 = rd_date_one_alpha(l0, depth, false);
    const uint32_t l2 = rd_date_one_alpha(l1, depth, false);
    s.level[0] = l2;
    s.level[1] = l1;
    s.level[2] = l0;
    for (int i = 0; i < 3; i++) {
        s.amax[i] = rd_date_one_alpha(s.level[i], blend[3 - i], false);
    }
    return s;
}

static RdDateOneCmd cmdComposite(uint32_t target, bool keeps)
{
    RdDateOneCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = RD_D1_COMPOSITE;
    c.target = target;
    c.date = true;
    c.keeps = keeps;
    return c;
}

static RdDateOneCmd cmdKind(uint8_t kind)
{
    RdDateOneCmd c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.target = T_SCENE;
    return c;
}

/* the shares and refusals a sequence gives */
static void runRule(const RdDateOneCmd *cmd, uint32_t n, uint32_t *shared, uint32_t *refused,
                    uint32_t *lastPrev)
{
    RdDateOneScan s;
    memset(&s, 0, sizeof(s));
    *shared = *refused = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t prev = 0;
        const int did = rd_date_one_step(&s, &cmd[i], i, &prev);
        *shared += did == RD_D1_SHARED;
        *refused += did == RD_D1_REFUSED;
        if (did != RD_D1_NONE) {
            *lastPrev = prev;
        }
    }
}

/* a stage's three composites with state between, then list 4's draw */
static void stageRule(const char *name, uint32_t depth, const uint32_t blend[4],
                      uint32_t wantShared, uint32_t wantRefused)
{
    const Stage st = stageOf(depth, blend);
    RdDateOneCmd cmd[8];
    uint32_t n = 0;
    cmd[n++] = cmdKind(RD_D1_OTHER); /* the last level */
    for (int i = 0; i < 3; i++) {
        cmd[n++] =
            cmdComposite(T_SCENE, rd_date_one_keeps(true, 1, 0, false, 0xF, st.amax[i], false));
        cmd[n++] = cmdKind(RD_D1_STATE);
    }
    cmd[n++] = cmdKind(RD_D1_OTHER); /* list 4's mesh */
    uint32_t shared = 0, refused = 0, prev = 0;
    runRule(cmd, n, &shared, &refused, &prev);
    printf("  %s: levels %u/%u/%u, composites at most %u/%u/%u: %u shared, %u refused\n", name,
           st.level[0], st.level[1], st.level[2], st.amax[0], st.amax[1], st.amax[2], shared,
           refused);
    CHECK(shared == wantShared && refused == wantRefused, "%s: %u shared, %u refused; %u and %u",
          name, shared, refused, wantShared, wantRefused);
}

static void testRule(void)
{
    /* b1: the retail tuples (DATEPROOF's bound: at most 56 written over every
     * disc's stage data) */
    static const uint32_t kB72[4] = {0, 40, 80, 120}, kB80[4] = {0, 90, 100, 128};
    stageRule("b1 shadowDepth 72, blends 40/80/120", 72, kB72, 2, 0);
    stageRule("b1 shadowDepth 80, blends 90/100/128", 80, kB80, 2, 0);
    const Stage s72 = stageOf(72, kB72);
    CHECK(s72.level[2] == 72 && s72.amax[0] < 0x80 && s72.amax[1] < 0x80 && s72.amax[2] < 0x80,
          "b1: SHADOW0 %u (72 expected), composites %u/%u/%u under 0x80", s72.level[2], s72.amax[0],
          s72.amax[1], s72.amax[2]);
    /* b2: the debug menu's 128 x 128: (128 x 128) >> 7 = 0x80 at every level
     * and composite */
    static const uint32_t kB128[4] = {0, 128, 128, 128}, kB128a[4] = {0, 128, 80, 120};
    const Stage s128 = stageOf(128, kB128);
    CHECK(s128.amax[0] == 0x80 && s128.amax[1] == 0x80 && s128.amax[2] == 0x80,
          "b2: 128 x 128 writes %u/%u/%u: 0x80 expected", s128.amax[0], s128.amax[1], s128.amax[2]);
    stageRule("b2 shadowDepth 128, blends 128", 128, kB128, 0, 2);
    stageRule("b2 shadowDepth 128, shadowBlend[1] 128 alone", 128, kB128a, 2, 0);
    CHECK(rd_date_one_alpha(127, 129, false) == 128 && rd_date_one_alpha(200, 0, true) == 200 &&
              rd_date_one_alpha(255, 255, false) == 255,
          "b2: the bound rounds up (127 x 129 -> 128), DECAL takes the level, at most 255");
    /* b3: DATM 1 (date 2), FBA 1, not the composite form: none keeps */
    CHECK(!rd_date_one_keeps(true, 2, 0, false, 0xF, 0x10, false), "b3: DATM 1 kept");
    CHECK(!rd_date_one_keeps(true, 1, 1, false, 0xF, 0x10, false), "b3: FBA 1 kept");
    CHECK(!rd_date_one_keeps(false, 1, 0, false, 0xF, 0x10, false), "b3: no composite form kept");
    CHECK(!rd_date_one_keeps(true, 1, 0, true, 0xF, 0x10, false), "b3: COLCLAMP 0's wrap kept");
    CHECK(rd_date_one_keeps(true, 1, 0, false, 0x7, 0xFF, false),
          "b3: no alpha written, yet refused");
    CHECK(rd_date_one_keeps(true, 1, 0, false, 0xF, 0x80, true) &&
              !rd_date_one_keeps(true, 1, 0, false, 0xF, 0x80, false),
          "b3: force passes the bound, the bound refuses 0x80");
    {
        const RdDateOneCmd seq[] = {cmdComposite(T_SCENE, false), cmdKind(RD_D1_STATE),
                                    cmdComposite(T_SCENE, true)};
        uint32_t sh, rf, pv = 99;
        runRule(seq, 3, &sh, &rf, &pv);
        CHECK(sh == 0 && rf == 1 && pv == 0,
              "b3: after one that may set the bit: %u shared, %u "
              "refused (the one at %u)",
              sh, rf, pv);
        /* AFAIL FB_ONLY: the box scan names no composite: a command that ends it */
        const RdDateOneCmd fb[] = {cmdComposite(T_SCENE, true), cmdKind(RD_D1_OTHER),
                                   cmdComposite(T_SCENE, true)};
        runRule(fb, 3, &sh, &rf, &pv);
        CHECK(sh == 0 && rf == 0, "b3: a draw in no composite form between: %u shared", sh);
    }
    /* b4: a draw between, another target */
    {
        const RdDateOneCmd seq[] = {cmdComposite(T_SCENE, true), cmdKind(RD_D1_STATE),
                                    cmdComposite(T_SCENE, true), cmdKind(RD_D1_OTHER),
                                    cmdComposite(T_SCENE, true), cmdComposite(T_WORK, true),
                                    cmdComposite(T_SCENE, true)};
        uint32_t sh, rf, pv = 0;
        runRule(seq, 7, &sh, &rf, &pv);
        CHECK(sh == 1 && rf == 0 && pv == 0, "b4: %u shared (1 expected: 2 after 0), %u refused",
              sh, rf);
        RdDateOneCmd nd = cmdComposite(T_SCENE, true);
        nd.date = false; /* a composite without DATE: a draw like any other */
        const RdDateOneCmd s2[] = {cmdComposite(T_SCENE, true), nd, cmdComposite(T_SCENE, true)};
        runRule(s2, 3, &sh, &rf, &pv);
        CHECK(sh == 0 && rf == 0, "b4: a composite without DATE between: %u shared", sh);
    }
    printf("  the rule: b1-b4 checked\n");
}

static void testLines(void)
{
    RdPerfRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.dateOneShared = 2;
    rec.dateOneTexels = 2u * 512u * 448u;
    rd__date_one_pop(&rec);
    rec.dateOneShared = 0;
    rec.dateOneRefused = 2;
    rd__date_one_pop(&rec);
    char out[256];
    size_t at = 0;
    out[0] = '\0';
    rd__date_one_text(out, sizeof out, &at);
    printf("  10 s: %s\n", out);
    CHECK(strcmp(out, "; DATE one set-up: 1.0 composites a replay shared the set-up before them, "
                      "1.0 kept their own") == 0,
          "b5: the 10 s clause: %s", out);
    at = 0;
    out[0] = '\0';
    rd__date_one_text(out, sizeof out, &at);
    CHECK(at == 0 && out[0] == '\0', "b5: the clause again with nothing counted: %s", out);
    rd__date_one_force(1);
    rec.dateOneShared = 2;
    rec.dateOneRefused = 0;
    rd__date_one_stats(&rec, out, sizeof out);
    printf("  --stats: %s\n", out);
    CHECK(strcmp(out, "DATE one set-up on: 2 shared (458752 texels), 0 kept their own") == 0,
          "b5: the stats words: %s", out);
    rd__date_one_force(-1);
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

static bool target(RdFrame *f, int list, uint32_t color, uint32_t depth, uint32_t w, uint32_t h)
{
    RdCmd *c = rd__st_push(f, list, RDC_TARGET);
    if (c) {
        c->u[0] = color;
        c->u[1] = depth;
        c->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        c->b[0] = RD_TARGET_OFFSET;
    }
    return c != NULL;
}

/* a sprite over GS pixels x0..x1, y0..y1 of a w x h target at GS Z z,
 * colour rgba, UVs (uvFixed, texels) u0..u1, v0..v1 when uv */
static bool sprite(RdFrame *f, int list, uint32_t w, uint32_t h, uint32_t x0, uint32_t y0,
                   uint32_t x1, uint32_t y1, uint32_t z, const uint8_t rgba[4], bool uv,
                   uint32_t u1, uint32_t v1)
{
    RdScreenVtx v[2];
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, sizeof(v));
    v[0].x = (ox + (int32_t)x0) * 16;
    v[0].y = (oy + (int32_t)y0) * 16;
    v[1].x = (ox + (int32_t)x1) * 16;
    v[1].y = (oy + (int32_t)y1) * 16;
    v[1].s = uv ? (float)(u1 * 16u) : 0.0f;
    v[1].t = uv ? (float)(v1 * 16u) : 0.0f;
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
    c->b[2] = uv ? 1 : 0;
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
    return a && rd__st_push1(f, list, RDC_ABE, blend ? 1 : 0) && rd__st_test(f, list, test) &&
           rd__st_push1(f, list, RDC_ZWRITE, zwrite);
}

static bool texture(RdFrame *f, int list, uint32_t tex)
{
    RdCmd *c = rd__st_push(f, list, RDC_TEXTURE);
    if (c) {
        c->u[0] = tex;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    RdCmd *wr = c ? rd__st_push(f, list, RDC_WRAP) : NULL;
    if (wr) {
        wr->b[0] = RD_WRAP_CLAMP;
        wr->b[1] = RD_WRAP_CLAMP;
    }
    return wr != NULL;
}

/* the frame: list 0 SCENE cleared, its left half drawn at Z 0x1000 with
 * alpha 0x80 (the top bit); list 3 the count on count (a w x h target with
 * no depth, SCENE's depth for the stencil): the reset, a rectangle's volume
 * when volume (two triangles, both counted up, at Z 0x2000: in front), the
 * resolve; SHADOW0 drawn from the count (2:1, vertex alpha depth); the three
 * composites of SHADOW0 into SCENE under 0x3400D, vertex alphas blend[0..2]
 * and three vertex colours (the force run's wrong share shows); list 4 a Z
 * GEQUAL sprite over SCENE */
static bool buildFrame(RdFrame *f, uint32_t number, uint32_t scene, uint32_t count, uint32_t w,
                       uint32_t h, bool volume, uint8_t depth, const uint8_t blend[3])
{
    static const uint8_t kLeft[4] = {0x40, 0x40, 0x40, 0x80};
    static const uint8_t kLate[4] = {0x10, 0x60, 0x60, 0x80};
    static const uint8_t kRgb[3][3] = {{0x80, 0x80, 0x80}, {0xFF, 0xFF, 0xFF}, {0x40, 0xC0, 0x40}};
    const uint32_t s0 = rd_target(RD_TARGET_SHADOW0).id;
    const RdTargetRec *l0 = rd__target_rec(s0);
    if (!l0) {
        return false;
    }
    frameBegin(f, number, w, h);
    bool ok = target(f, 0, scene, scene, w, h);
    RdCmd *c = ok ? rd__st_push(f, 0, RDC_CLEAR) : NULL;
    if (c) {
        static const uint8_t kClear[4] = {0x08, 0x10, 0x18, 0x00};
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_push(f, 0, RDC_TEXTURE_OFF) &&
         state(f, 0, RD_TEST_Z_GEQUAL, RD_ZWRITE_ON, false) &&
         sprite(f, 0, w, h, 0, 0, w / 2, h, 0x1000, kLeft, false, 0, 0);
    /* the count */
    ok = ok && target(f, 3, count, scene, w, h) && rd__st_push(f, 3, RDC_TEXTURE_OFF) &&
         state(f, 3, RD_TEST_Z_GEQUAL, RD_ZWRITE_OFF, false) && rd__st_push(f, 3, RDC_SHADOW_RESET);
    if (ok && volume) {
        const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
        const int32_t x0 = (ox + (int32_t)w / 4) * 16, x1 = (ox + 3 * (int32_t)w / 4) * 16;
        const int32_t y0 = (oy + (int32_t)h / 4) * 16, y1 = (oy + 3 * (int32_t)h / 4) * 16;
        const int32_t xs[6] = {x0, x1, x0, x0, x1, x1}, ys[6] = {y0, y0, y1, y1, y0, y1};
        RdScreenVtx q[6];
        memset(q, 0, sizeof(q));
        for (int i = 0; i < 6; i++) {
            q[i].x = xs[i];
            q[i].y = ys[i];
            q[i].z = 0x2000;
            q[i].q = 1.0f;
            rd__set_shadow_tag(&q[i], (uint32_t)(i / 3 + 1));
        }
        RdCmd *sc = rd__st_push(f, 3, RDC_SHADOW_STRIP);
        ok = sc != NULL;
        if (sc) {
            sc->b[0] = RD_SHADOW_TRIS;
            sc->u[0] = 6;
            sc->u[1] = rd__frame_payload(f, q, (uint32_t)sizeof(q));
            sc->u[2] = (uint32_t)sizeof(q);
            sc->u[3] = 0;
        }
    }
    ok = ok && rd__st_push(f, 3, RDC_SHADOW_RESOLVE);
    /* the level: SHADOW0 from the count, the whole level */
    const uint8_t lv[4] = {0x80, 0x80, 0x80, depth};
    ok = ok && target(f, 3, s0, 0, l0->w, l0->h) &&
         texture(f, 3, rd_target_texture((RdTarget){count}, RD_VIEW_RGBA).id) &&
         state(f, 3, RD_TEST_Z_ALWAYS, RD_ZWRITE_OFF, false) &&
         sprite(f, 3, l0->w, l0->h, 0, 0, l0->w, l0->h, 0, lv, true, w, h);
    /* the composites */
    ok = ok && target(f, 3, scene, scene, w, h) &&
         texture(f, 3, rd_target_texture((RdTarget){s0}, RD_VIEW_RGBA).id) &&
         state(f, 3, RD_TEST_AT_GT0_DATE0, RD_ZWRITE_OFF, true);
    for (int i = 0; i < 3 && ok; i++) {
        const uint8_t cv[4] = {kRgb[i][0], kRgb[i][1], kRgb[i][2], blend[i]};
        ok = sprite(f, 3, w, h, 0, 0, w, h, 0, cv, true, l0->w, l0->h) &&
             rd__st_push1(f, 3, RDC_ABE, 1);
    }
    ok = ok && target(f, 4, scene, scene, w, h) && rd__st_push(f, 4, RDC_TEXTURE_OFF) &&
         state(f, 4, RD_TEST_Z_GEQUAL, RD_ZWRITE_ON, false) &&
         sprite(f, 4, w, h, 0, 0, w, h, 0x800, kLate, false, 0, 0);
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

/* the frame replayed twice (the second's record the steady one) with one
 * DATE set-up in mode one (rd__date_one_force) and the empty composites in
 * mode empty (rd__shadow_box_empty_force) */
static bool replayOnce(const RdFrame *f, int one, int empty, Took *out)
{
    rd__date_one_force(one);
    rd__shadow_box_empty_force(empty);
    bool ok = rd__replay_frame(f, 0, false) && rd__replay_frame(f, 0, false);
    out->r = g_rdPerf;
    out->px = ok ? readScene(&out->w, &out->h) : NULL;
    printf("    one %d, empty %d: %u passes, %u draws, %u DATE snapshots (%u first, %u after an "
           "alpha write), %u shared, %u refused, %u empty skipped (%u with DATE), %u passes "
           "without the depth, %u begun again\n",
           one, empty, out->r.renderPasses, out->r.draws, out->r.dateSnapshots,
           out->r.dateRetakeWhy[RD_DATE_RETAKE_FIRST], out->r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN],
           out->r.dateOneShared, out->r.dateOneRefused, out->r.emptySkipped,
           out->r.emptySkippedDate, out->r.dateCutPasses, out->r.dateCutAgain);
    return ok && out->px != NULL;
}

static bool samePx(const Took *a, const Took *b)
{
    return a->px && b->px && a->w == b->w && a->h == b->h &&
           memcmp(a->px, b->px, (size_t)a->w * a->h * 4u) == 0;
}

static void testDevice(uint32_t w, uint32_t h)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const RdTargetRec *t = rd__target_rec(scene);
    CHECK(t && t->withDepth && t->depth.id && t->sx == 1.0f && t->sy == 1.0f,
          "SCENE %ux%u with a depth at 1x", t ? t->tw : 0u, t ? t->th : 0u);
    CHECK(!g_rd.profile.dateStencil && g_rd.dstMode == RD_BLEND_EXACT_OFF,
          "DATE by the snapshot (%d) and the fixed-function blend (%u) in force",
          (int)!g_rd.profile.dateStencil, (unsigned)g_rd.dstMode);
    const uint32_t count = rd_temp_target(w, h, 0, 1).id;
    CHECK(count != 0, "the count target made");
    if (!t || !t->withDepth || !count) {
        return;
    }
    static RdFrame f;
    /* d1: the retail stage */
    static const uint8_t kRetail[3] = {40, 80, 120}, kDebug[3] = {128, 128, 128};
    printf("  d1: shadowDepth 72, blends 40/80/120:\n");
    CHECK(buildFrame(&f, 1, scene, count, w, h, true, 72, kRetail), "d1: the frame built");
    Took off, on;
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    CHECK(replayOnce(&f, 0, 1, &off) && replayOnce(&f, 1, 1, &on), "d1: replayed");
    CHECK(samePx(&off, &on), "d1: SCENE differs with one DATE set-up");
    CHECK(off.r.dateSnapshots == 3 && off.r.dateRetakeWhy[RD_DATE_RETAKE_FIRST] == 1 &&
              off.r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN] == 2 && off.r.dateOneShared == 0,
          "d1 off: %u snapshots (%u first, %u written), %u shared: 3 (1, 2) and 0 expected",
          off.r.dateSnapshots, off.r.dateRetakeWhy[RD_DATE_RETAKE_FIRST],
          off.r.dateRetakeWhy[RD_DATE_RETAKE_WRITTEN], off.r.dateOneShared);
    CHECK(on.r.dateSnapshots == 1 && on.r.dateRetakeWhy[RD_DATE_RETAKE_FIRST] == 1 &&
              on.r.dateOneShared == 2 && on.r.dateOneRefused == 0 && on.r.dateSnapReused == 2,
          "d1 on: %u snapshots, %u shared, %u refused, %u reused: 1, 2, 0, 2 expected",
          on.r.dateSnapshots, on.r.dateOneShared, on.r.dateOneRefused, on.r.dateSnapReused);
    CHECK(on.r.renderPasses + 4u == off.r.renderPasses && on.r.draws + 2u == off.r.draws,
          "d1: passes %u -> %u, draws %u -> %u: 4 and 2 fewer expected", off.r.renderPasses,
          on.r.renderPasses, off.r.draws, on.r.draws);
    CHECK(off.r.dateCutPasses == 2 && on.r.dateCutPasses == 0 && off.r.dateCutAgain == 0 &&
              on.r.dateCutAgain == 0,
          "d1: the DATE cut's passes %u off, %u on (2 and 0), begun again %u/%u",
          off.r.dateCutPasses, on.r.dateCutPasses, off.r.dateCutAgain, on.r.dateCutAgain);
    CHECK(on.r.dateOneTexels == 2ull * t->tw * t->th, "d1: %llu texels shared, %llu expected",
          (unsigned long long)on.r.dateOneTexels, 2ull * t->tw * t->th);
    free(off.px);
    free(on.px);
    rd__frame_free(&f);

    /* d2: the debug menu's 128 x 128 */
    printf("  d2: shadowDepth 128, blends 128:\n");
    CHECK(buildFrame(&f, 2, scene, count, w, h, true, 128, kDebug), "d2: the frame built");
    Took forced;
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    memset(&forced, 0, sizeof(forced));
    CHECK(replayOnce(&f, 0, 1, &off) && replayOnce(&f, 1, 1, &on) && replayOnce(&f, 2, 1, &forced),
          "d2: replayed");
    CHECK(samePx(&off, &on), "d2: SCENE differs with the shares refused");
    CHECK(on.r.dateSnapshots == 3 && on.r.dateOneShared == 0 && on.r.dateOneRefused == 2 &&
              off.r.dateSnapshots == 3,
          "d2 on: %u snapshots (off %u), %u shared, %u refused: 3, 0, 2 expected",
          on.r.dateSnapshots, off.r.dateSnapshots, on.r.dateOneShared, on.r.dateOneRefused);
    CHECK(forced.r.dateOneShared == 2 && forced.r.dateSnapshots == 1,
          "d2 forced: %u shared, %u snapshots: 2 and 1 expected", forced.r.dateOneShared,
          forced.r.dateSnapshots);
    CHECK(forced.px && off.px && !samePx(&off, &forced),
          "d2: SCENE the same with the bound ignored: the test cannot see a wrong share");
    free(off.px);
    free(on.px);
    free(forced.px);
    rd__frame_free(&f);

    /* d3: no volume, the empty composites */
    printf("  d3: no volume, the empty composites:\n");
    CHECK(buildFrame(&f, 3, scene, count, w, h, false, 72, kRetail), "d3: the frame built");
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    CHECK(replayOnce(&f, 0, 0, &off) && replayOnce(&f, 0, 1, &on), "d3: replayed");
    CHECK(samePx(&off, &on), "d3: SCENE differs with the empty composites skipped");
    CHECK(off.r.emptySkipped == 0 && on.r.emptySkipped == 3 && on.r.emptySkippedDate == 3,
          "d3: %u skipped off, %u (%u with DATE) on: 0 and 3 (3) expected", off.r.emptySkipped,
          on.r.emptySkipped, on.r.emptySkippedDate);
    CHECK(off.r.dateSnapshots == 3 && on.r.dateSnapshots == 0,
          "d3: snapshots %u off, %u on: 3 and 0 expected", off.r.dateSnapshots, on.r.dateSnapshots);
    CHECK(on.r.renderPasses + 5u == off.r.renderPasses && on.r.draws + 3u == off.r.draws,
          "d3: passes %u -> %u, draws %u -> %u: 5 and 3 fewer expected", off.r.renderPasses,
          on.r.renderPasses, off.r.draws, on.r.draws);
    free(off.px);
    free(on.px);
    rd__frame_free(&f);
    rd__date_one_force(-1);
    rd__shadow_box_empty_force(-1);
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
    printf("rd_date_one_test\n");
    testRule();
    testLines();
    setEnv("ICO_RD_DATE_PATH", "snapshot");
    setEnv("ICO_RD_BLEND", "fixed");
    setEnv("ICO_RD_STORE_POISON", "1");
    const uint32_t w = 512, h = 448;
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.sceneScale = 1.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    if (!rd_init(w, h, &st, NULL)) {
        if (failures) {
            printf("rd_date_one_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_date_one_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    testDevice(w, h);
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_date_one_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_date_one_test: ok\n");
    return 0;
}
