/* rd_interp_test.c: presentation between ticks (renderer wave 7, R7b;
 * docs/port/RENDER_API.md section 20).
 *
 * Two synthetic frames with keyed draws, blended by rd__InterpFrame.
 * Without a device (recording only):
 *   ends      alpha 0 gives the previous frame's draw data, alpha 1 the
 *             current frame's payload byte for byte
 *   half      a keyed UI sprite translated by 32 px lands 16 px over (within
 *             1/16 px, the 12.4 step), its colour half way; an unkeyed one
 *             keeps the current frame's
 *   snaps     a key missing from the previous frame, a vertex count change,
 *             a jump past the screen threshold, a camera cut (rd_CameraCut),
 *             a fully faded frame (the fade edge), a discarded frame between
 *             (gap), a camera turn past the threshold: the current data
 *   ordinal   the same key twice matches in order
 *   grid      a grid's vertices blend one by one, its STs and headers stay
 *   mesh      the VU block's matrices blend, the UV scroll across Texture.c's
 *             wrap by 2 blends the short way, a model origin moved further
 *             than the world threshold snaps
 *   particles a batch's particles blend; one moved further than four sizes
 *             keeps the current position
 *   shadow    a shadow volume's vertices blend; a triangle count change snaps
 *   fade      the fade sprite's level blends (the post sprite is keyed)
 *   text      (R7d) a string's glyph quads keyed as port/ui/font.c keys them
 *             (the string's hash): moved and faded, every glyph blends half
 *             way (the alpha 0x80 -> 0 text at 0x40); another string is
 *             another key and is the current; the same string twice matches
 *             in order
 *   morph     (R7d) a morphing part as RegistPacket.c draws it: two meshes
 *             of one layout drawn in alternate frames under one key, the
 *             older rewritten (rd_UpdateVuMesh) while the next frame
 *             records: the half-way frame draws a scratch mesh whose
 *             positions are half way between the two ticks' shapes (the
 *             older from the kept version), alpha 1 the current tick's
 *             shape, not the newer one; a mesh rewritten every frame the
 *             same; the replays use the kept streams
 *   feedback  rd__BlurFeedbackFix at dt 0.5: the LERP retention is a^0.5 (to
 *             the FIX's rounding), additive FIX x 0.5; the motion blur
 *             sprite of an interpolated frame carries dt; the aura's FEED128
 *             writes are dropped except in a tick's first present
 * On a device (skipped without one):
 *   pixels    replays of the blended frame at alpha 0 and 1 equal the
 *             previous and current frames' replays byte for byte; at 0.5 the
 *             translated sprite covers the half-way columns exactly
 *   present   rd_Present does nothing in the Original preset or with
 *             interpolate off, presents in Enhanced with it; a change of
 *             scale drops the history (the next pair snaps)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
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
/* WORK0: 256 x 128, XYOFFSET (2048 - 128, 2048 - 64) */
#define TW 256
#define TH 128
#define OX ((2048 - TW / 2) * 16)
#define OY ((2048 - TH / 2) * 16)

static const char kObjA, kObjB, kObjC, kObjD, kObjE;

static void opaque2D(void)
{
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
    rd_TextureOff();
}

/* a sprite x0..x1, y0..y1 in whole pixels of WORK0 */
static void sprite(int x0, int y0, int x1, int y1, const uint8_t c[4], RdKey key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[0].x = OX + x0 * 16;
    v[0].y = OY + y0 * 16;
    v[1].x = OX + x1 * 16;
    v[1].y = OY + y1 * 16;
    v[0].q = v[1].q = 1.0f;
    v[0].s = 3.0f;
    v[1].s = (float)(x0 + 7); /* differs between the frames: held at the current */
    memcpy(v[0].rgba, c, 4);
    memcpy(v[1].rgba, c, 4);
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, key);
}

static void frameHead(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    rd_SelectList(0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, TW, TH, 0);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK0), black, 0, 0);
    opaque2D();
}

/* the n-th command with key k in list l of f */
static const RdCmd *findKey(const RdFrame *f, int l, RdKey k, int nth)
{
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        if (c->keyLo == (uint32_t)k && c->keyHi == (uint32_t)(k >> 32) && nth-- == 0) {
            return c;
        }
    }
    return NULL;
}

static const RdScreenVtx *screenVtx(const RdFrame *f, const RdCmd *c)
{
    return c ? (const RdScreenVtx *)(const void *)(f->payload + c->u[0]) : NULL;
}

static const RdInterpStats *build(float alpha, float dt, int first)
{
    static RdInterpStats st;
    rd__InterpFrame(rd__PrevFrame(), rd__LastFrame(), alpha, dt, first, &st);
    return &st;
}

static const RdFrame *built(float alpha)
{
    return rd__InterpFrame(rd__PrevFrame(), rd__LastFrame(), alpha, 1.0f, 1, NULL);
}

/* ----------------------------------------------------- screen sprites */

/* frame A: keyed sprite KA at x 0..32, KB at 10..20 twice (the ordinal),
 * an unkeyed sprite; frame B: KA at 32..64, KB at 20..30 and 30..40, the
 * unkeyed sprite elsewhere, a new key KC */
static void recordSprites(int second)
{
    static const uint8_t c0[4] = {200, 40, 0, 0x80}, c1[4] = {100, 80, 40, 0x40};
    static const uint8_t grey[4] = {90, 90, 90, 0x80};
    rd_BeginFrame();
    frameHead();
    const int dx = second ? 32 : 0;
    sprite(dx, 8, dx + 32, 40, second ? c1 : c0, RD_KEY(&kObjA, 0, 0));
    sprite(10 + (second ? 10 : 0), 50, 20 + (second ? 10 : 0), 60, grey, RD_KEY(&kObjB, 0, 0));
    sprite(10 + (second ? 20 : 0), 70, 20 + (second ? 20 : 0), 80, grey, RD_KEY(&kObjB, 0, 0));
    sprite(second ? 100 : 120, 90, second ? 110 : 130, 100, grey, 0);
    if (second) {
        sprite(200, 8, 240, 40, grey, RD_KEY(&kObjC, 0, 0));
    }
    rd_EndFrame(0);
}

static void testSprites(void)
{
    recordSprites(0);
    recordSprites(1);
    const RdFrame *prev = rd__PrevFrame(), *cur = rd__LastFrame();
    CHECK(prev && cur && prev->number + 1 == cur->number, "two frames retained");
    if (!prev || !cur) {
        return;
    }
    /* alpha 1: the current frame's payload, byte for byte */
    const RdFrame *f = built(1.0f);
    CHECK(f && f->payloadSize == cur->payloadSize &&
              memcmp(f->payload, cur->payload, cur->payloadSize) == 0,
          "alpha 1: the current frame's payload");
    /* alpha 0: the previous frame's draws (the keyed ones that match) */
    const RdInterpStats *st = build(0.0f, 1.0f, 1);
    f = built(0.0f);
    const RdScreenVtx *a = screenVtx(f, findKey(f, 0, RD_KEY(&kObjA, 0, 0), 0));
    const RdScreenVtx *pa = screenVtx(prev, findKey(prev, 0, RD_KEY(&kObjA, 0, 0), 0));
    CHECK(a && pa && a[0].x == pa[0].x && a[1].x == pa[1].x && a[0].y == pa[0].y &&
              memcmp(a[0].rgba, pa[0].rgba, 4) == 0,
          "alpha 0: the previous frame's sprite");
    CHECK(st->snap == RD_SNAP_NONE && st->keyed == 4 && st->lerped == 3 && st->missing == 1,
          "alpha 0: 4 keyed, 3 blended, KC unmatched (snap %u keyed %u lerped %u missing %u)",
          st->snap, st->keyed, st->lerped, st->missing);
    /* half way */
    f = built(0.5f);
    a = screenVtx(f, findKey(f, 0, RD_KEY(&kObjA, 0, 0), 0));
    const RdScreenVtx *ca = screenVtx(cur, findKey(cur, 0, RD_KEY(&kObjA, 0, 0), 0));
    if (a && ca) {
        CHECK(abs(a[0].x - (OX + 16 * 16)) <= 1 && abs(a[1].x - (OX + 48 * 16)) <= 1,
              "alpha 0.5: x 16..48 px within 1/16 px (got %d..%d)", (a[0].x - OX), (a[1].x - OX));
        CHECK(a[0].rgba[0] == 150 && a[0].rgba[1] == 60 && a[0].rgba[2] == 20 &&
                  a[0].rgba[3] == 0x60,
              "alpha 0.5: colour half way (%u %u %u %u)", a[0].rgba[0], a[0].rgba[1], a[0].rgba[2],
              a[0].rgba[3]);
        CHECK(a[1].s == ca[1].s && a[0].s == ca[0].s, "UVs held at the current frame");
    }
    /* the ordinal: the second KB matches the second */
    const RdScreenVtx *b0 = screenVtx(f, findKey(f, 0, RD_KEY(&kObjB, 0, 0), 0));
    const RdScreenVtx *b1 = screenVtx(f, findKey(f, 0, RD_KEY(&kObjB, 0, 0), 1));
    CHECK(b0 && b1 && b0[0].x == OX + 15 * 16 && b1[0].x == OX + 20 * 16,
          "same key twice: matched in order (%d, %d)", b0 ? (b0[0].x - OX) / 16 : -1,
          b1 ? (b1[0].x - OX) / 16 : -1);
    /* unkeyed and unmatched: the current frame's */
    const RdScreenVtx *kc = screenVtx(f, findKey(f, 0, RD_KEY(&kObjC, 0, 0), 0));
    CHECK(kc && kc[0].x == OX + 200 * 16, "missing key snaps to the current");
    int unkeyedOk = 0;
    for (uint32_t i = 0; i < f->lists[0].count; i++) {
        const RdCmd *c = &f->lists[0].cmds[i];
        if (c->type == RDC_SCREEN && c->keyLo == 0 && c->keyHi == 0) {
            unkeyedOk = screenVtx(f, c)[0].x == OX + 100 * 16;
        }
    }
    CHECK(unkeyedOk, "an unkeyed sprite keeps the current frame's position");
}

/* count change, jump */
static void testSpriteSnaps(void)
{
    static const uint8_t grey[4] = {90, 90, 90, 0x80};
    for (int k = 0; k < 2; k++) {
        rd_BeginFrame();
        frameHead();
        /* KA: 2 vertices, then 4 (two sprites in one call) */
        RdScreenVtx v[4];
        memset(v, 0, sizeof(v));
        for (int i = 0; i < 4; i++) {
            v[i].x = OX + (i * 10 + k) * 16;
            v[i].y = OY + (i * 10) * 16;
            memcpy(v[i].rgba, grey, 4);
        }
        rd_ScreenPrims(RD_PRIM_SPRITES, v, k ? 4 : 2, RD_SPACE_UI, 0, RD_KEY(&kObjA, 1, 0));
        /* KD: 300 px to the right in the second frame */
        sprite(k ? 300 : 0, 0, k ? 310 : 10, 10, grey, RD_KEY(&kObjD, 0, 0));
        rd_EndFrame(0);
    }
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->mismatch == 1 && st->jump == 1 && st->lerped == 0,
          "count change and jump snap (mismatch %u jump %u lerped %u)", st->mismatch, st->jump,
          st->lerped);
    const RdFrame *f = built(0.5f);
    const RdScreenVtx *d = screenVtx(f, findKey(f, 0, RD_KEY(&kObjD, 0, 0), 0));
    CHECK(d && d[0].x == OX + 300 * 16, "the jumped sprite is the current");
}

/* frame-level snaps: cut, fade edge, gap, camera turn; keep */
static void cameraYaw(RdCamera *cam, float deg, float x)
{
    memset(cam, 0, sizeof(*cam));
    const float r = deg * 3.14159265f / 180.0f;
    cam->view[0] = cosf(r);
    cam->view[2] = -sinf(r);
    cam->view[5] = 1.0f;
    cam->view[8] = sinf(r);
    cam->view[10] = cosf(r);
    cam->view[12] = x;
    cam->view[15] = 1.0f;
    cam->zoom = 1024.0f;
}

static void framePair(int what)
{
    static const uint8_t grey[4] = {90, 90, 90, 0x80};
    for (int k = 0; k < 2; k++) {
        rd_BeginFrame();
        frameHead();
        RdCamera cam;
        cameraYaw(&cam, what == 4 && k ? 40.0f : (k ? 5.0f : 0.0f), 0.0f);
        rd_SetCamera(&cam);
        sprite(k * 8, 0, k * 8 + 10, 10, grey, RD_KEY(&kObjA, 2, 0));
        if (what == 1 && k) {
            rd_CameraCut();
        }
        if (what == 2 && !k) {
            RdPostParams pp;
            memset(&pp, 0, sizeof(pp));
            pp.rgba[3] = 0x80;
            rd_SelectList(11);
            rd_Post(RD_POST_FADE, &pp);
        }
        if (what == 3 && k) {
            rd_EndFrame(0);
            rd_BeginFrame();
            rd_DiscardFrame(); /* a frame dropped between */
            rd_BeginFrame();
            frameHead();
            sprite(k * 8, 0, k * 8 + 10, 10, grey, RD_KEY(&kObjA, 2, 0));
        }
        rd_EndFrame(what == 5 && k ? 1 : 0);
    }
}

static void testFrameSnaps(void)
{
    static const struct {
        int what;
        uint32_t snap;
        const char *name;
    } cases[] = {{0, RD_SNAP_NONE, "none"},          {1, RD_SNAP_CUT, "cut"},
                 {2, RD_SNAP_FADE, "fade edge"},     {3, RD_SNAP_GAP, "gap"},
                 {4, RD_SNAP_CAMERA, "camera turn"}, {5, RD_SNAP_KEEP, "keep"}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        framePair(cases[i].what);
        const RdInterpStats *st = build(0.5f, 1.0f, 1);
        const RdFrame *f = built(0.5f);
        const RdScreenVtx *a = screenVtx(f, findKey(f, 0, RD_KEY(&kObjA, 2, 0), 0));
        const int x = a ? (a[0].x - OX) : -1;
        CHECK(st->snap == cases[i].snap, "%s: snap %u, expected %u", cases[i].name, st->snap,
              cases[i].snap);
        if (cases[i].snap == RD_SNAP_NONE) {
            CHECK(x == 4 * 16, "%s: sprite half way (x %d)", cases[i].name, x);
            /* the camera blends: 2.5 degrees */
            const float yaw = atan2f(-f->camera.view[2], f->camera.view[0]) * 180.0f / 3.14159265f;
            CHECK(fabsf(yaw - 2.5f) < 0.05f, "camera half way (%.3f degrees)", yaw);
        } else if (cases[i].what != 5) {
            CHECK(x == 8 * 16, "%s: the current sprite (x %d)", cases[i].name, x);
        }
        if (cases[i].what == 1) {
            CHECK(rd__LastFrame()->camera.cut == 1, "rd_CameraCut sets RdCamera.cut");
        }
    }
}

/* -------------------------------------------------------- VU payloads */

static RdMesh makeMesh(void)
{
    static float qw[1 + 3 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 10;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    return rd_CreateVuMesh(&md);
}

static void identity(float (*m)[4], int at)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            m[at + c][r] = c == r ? 1.0f : 0.0f;
        }
    }
}

/* mesh: k = 0 / 1; teleport moves KE's origin 1000 units */
static void recordVu(RdMesh mesh, int k, int teleport, int shadowTris)
{
    rd_BeginFrame();
    frameHead();
    /* a prelit mesh: inverse view identity, model to view translated */
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    identity(d.vu.mem, 4); /* world to screen */
    identity(d.vu.mem, 12);
    identity(d.vu.mem, 16);
    identity(d.vu.mem, 24);
    d.vu.mem[19][0] = 2048.0f + (float)(k * 20);
    d.vu.mem[19][1] = 2048.0f;
    d.vu.mem[27][0] = (float)(k * 100); /* 1 m in the tick */
    d.vu.mem[2][0] = k ? -0.9f : 0.9f;  /* the scroll wrapped by 2 */
    d.vu.mem[2][1] = k ? 0.5f : 0.25f;
    d.vu.mem[30][2] = k ? 1.0f : 0.0f; /* a light term */
    d.vu.mem[3][0] = k ? 7.0f : 3.0f;  /* the GIF tag qword: never blended */
    rd_SelectList(0);
    rd_DrawVuMesh(mesh, &d, RD_KEY(&kObjE, 0, 32));
    d.vu.mem[19][0] = 2048.0f + (teleport && k ? 1000.0f : 0.0f); /* 10 m in the tick */
    rd_DrawVuMesh(mesh, &d, RD_KEY(&kObjE, 1, 32));

    /* a grid: 2 strips of 3 unlit vertices (VIF qw, tag, colour, 3 x (pos,
     * ST), MSCNT) */
    static float g[2 * (3 * 2 + 4)][4];
    memset(g, 0, sizeof(g));
    for (int s = 0; s < 2; s++) {
        const int base = s * 10;
        g[base + 1][0] = 7.0f;              /* the tag */
        g[base + 2][0] = k ? 64.0f : 32.0f; /* the colour: held */
        for (int v = 0; v < 3; v++) {
            g[base + 3 + v * 2][0] = (float)(v * 10 + s * 100 + k * 8);
            g[base + 3 + v * 2][1] = (float)(k * 4 * (v + 1));
            g[base + 3 + v * 2][3] = 1.0f;
            g[base + 4 + v * 2][0] = k ? 0.75f : 0.25f; /* ST: held */
        }
    }
    RdVuGridDraw gd;
    memset(&gd, 0, sizeof(gd));
    gd.qw = (const float (*)[4])g;
    gd.strips = 2;
    gd.stripLen = 3;
    gd.code = 20;
    identity(gd.vu.mem, 4);
    identity(gd.vu.mem, 16);
    gd.vu.mem[19][0] = 2048.0f;
    gd.vu.mem[19][1] = 2048.0f;
    rd_DrawVuGrid(&gd, RD_KEY(&kObjE, 2, 0));

    /* particles: 2, the second moves 100 x its size */
    static float p[6 + 4][4];
    memset(p, 0, sizeof(p));
    const int32_t n = 2;
    memcpy(&p[0][0], &n, 4);
    p[6][0] = (float)(k * 2);
    p[6][3] = 1.0f;
    p[7][3] = 0.5f;
    p[8][0] = (float)(k * 100);
    p[8][3] = 1.0f;
    p[9][3] = 0.5f;
    RdVuParticleDraw pd;
    memset(&pd, 0, sizeof(pd));
    pd.qw = (const float (*)[4])p;
    pd.count = 2;
    identity(pd.vu.mem, 4);
    identity(pd.vu.mem, 16);
    pd.vu.mem[19][0] = 2048.0f;
    pd.vu.mem[19][1] = 2048.0f;
    pd.vu.mem[19][3] = 1.0f;
    rd_SelectList(6);
    rd_DrawVuParticles(&pd, 0);

    /* a shadow volume: 2 triangles, then 3 with shadowTris */
    RdScreenVtx sv[9];
    int8_t sign[3] = {1, -1, 1};
    memset(sv, 0, sizeof(sv));
    for (int i = 0; i < 9; i++) {
        sv[i].x = OX + (i * 4 + k * 16) * 16;
        sv[i].y = OY + i * 16;
        sv[i].z = 1000u + (uint32_t)(k * 100);
    }
    rd_SelectList(3);
    rd_ShadowTris(sv, sign, shadowTris && k ? 3 : 2, RD_KEY(&kObjE, 3, 0));
    rd_EndFrame(0);
}

static const float (*vuBlock(const RdFrame *f, const RdCmd *c))[4]
{
    return c ? (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(RdVuPayload))
             : NULL;
}

static void testVu(void)
{
    RdMesh mesh = makeMesh();
    CHECK(mesh.id != 0, "the test mesh");
    recordVu(mesh, 0, 0, 0);
    recordVu(mesh, 1, 1, 1);
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->keyed == 5 && st->lerped == 4 && st->jump == 1 &&
              st->mismatch == 0 && st->shifted == 1,
          "mesh, grid and particles blend, the teleported mesh jumps, the shadow whose count "
          "changed is moved (S2) (keyed %u lerped %u jump %u mismatch %u shifted %u)",
          st->keyed, st->lerped, st->jump, st->mismatch, st->shifted);
    const RdFrame *f = built(0.5f);
    const RdFrame *cur = rd__LastFrame();
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjE, 0, 32), 0));
    if (m) {
        CHECK(m[27][0] == 50.0f && m[19][0] == 2058.0f && m[30][2] == 0.5f,
              "the model and light matrices half way (%g %g %g)", m[27][0], m[19][0], m[30][2]);
        /* 0.9 -> -0.9 is 0.9 -> 1.1 wrapped: half way is 1.0, the same
         * offset as -1.0 */
        CHECK(fabsf(fabsf(m[2][0]) - 1.0f) < 1e-6f && m[2][1] == 0.375f,
              "the UV scroll the short way across the wrap (%g, %g)", m[2][0], m[2][1]);
        CHECK(m[3][0] == 7.0f, "qw 3 (the GIF tag) is the current frame's");
    }
    const float (*mt)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjE, 1, 32), 0));
    CHECK(mt && mt[19][0] == 3048.0f, "the teleported mesh is the current");
    /* the grid: VU block, then the stream */
    const RdCmd *gc = findKey(f, 0, RD_KEY(&kObjE, 2, 0), 0);
    if (gc) {
        const float (*s)[4] = (const float (*)[4])(
            const void *)(f->payload + gc->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        int ok = 1;
        for (int sI = 0; sI < 2; sI++) {
            for (int v = 0; v < 3; v++) {
                const float *pos = s[sI * 10 + 3 + v * 2];
                ok &= pos[0] == (float)(v * 10 + sI * 100 + 4) && pos[1] == (float)(2 * (v + 1));
                ok &= s[sI * 10 + 4 + v * 2][0] == 0.75f;
            }
            ok &= s[sI * 10 + 2][0] == 64.0f && s[sI * 10 + 1][0] == 7.0f;
        }
        CHECK(ok, "grid: every vertex half way, STs, colours and tags the current");
    }
    /* the particles (list 6, the key rd_DrawVuParticles makes) */
    const RdCmd *pc = NULL;
    for (uint32_t i = 0; i < f->lists[6].count; i++) {
        if (f->lists[6].cmds[i].type == RDC_PARTICLES) {
            pc = &f->lists[6].cmds[i];
        }
    }
    if (pc) {
        const float (*s)[4] = (const float (*)[4])(
            const void *)(f->payload + pc->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        CHECK(s[6][0] == 1.0f && s[8][0] == 100.0f,
              "particles: the first half way, the far one the current (%g, %g)", s[6][0], s[8][0]);
    } else {
        CHECK(0, "the particle batch");
    }
    const RdCmd *sc = findKey(f, 3, RD_KEY(&kObjE, 3, 0), 0);
    const RdCmd *scc = findKey(cur, 3, RD_KEY(&kObjE, 3, 0), 0);
    if (sc && scc && sc->u[0] + sc->u[3] == 9) {
        /* S2: the medians move from x 12, z 1000 (2 triangles) to x 32, z
         * 1100 (3), y 3 to 4: cur's 9 vertices half way back, 10 pixels, half a pixel
         * and 50 */
        const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + sc->u[1]);
        const RdScreenVtx *w = (const RdScreenVtx *)(const void *)(cur->payload + scc->u[1]);
        int ok = 1;
        for (int i = 0; i < 9; i++) {
            ok &= v[i].x == w[i].x - 10 * 16 && v[i].y == w[i].y - 8 && v[i].z == w[i].z - 50u;
        }
        CHECK(ok,
              "shadow: the triangle count changed, the current volume moved half way back "
              "by the shift of the medians (%d, %u)",
              (v[0].x - w[0].x) / 16, w[0].z - v[0].z);
    } else {
        CHECK(0, "shadow: the current volume's triangles");
    }
    /* the shadow blends when the topology is the same */
    recordVu(mesh, 0, 0, 0);
    recordVu(mesh, 1, 0, 0);
    st = build(0.5f, 1.0f, 1);
    f = built(0.5f);
    CHECK(st->lerped == 5 && st->jump == 0 && st->mismatch == 0, "all five blend (lerped %u)",
          st->lerped);
    sc = findKey(f, 3, RD_KEY(&kObjE, 3, 0), 0);
    if (sc) {
        const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + sc->u[1]);
        CHECK(v[0].x == OX + 8 * 16 && v[0].z == 1050u, "shadow: half way (%d, %u)",
              (v[0].x - OX) / 16, v[0].z);
    }
    rd_DestroyVuMesh(mesh);
}

/* -------------------------------------------------- fade and feedback */

static void testFade(void)
{
    for (int k = 0; k < 2; k++) {
        rd_BeginFrame();
        frameHead();
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.rgba[3] = k ? 0x20 : 0x40;
        rd_SelectList(11);
        rd_Post(RD_POST_FADE, &pp);
        rd_EndFrame(0);
    }
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->lerped == 1, "a partial fade interpolates");
    const RdFrame *f = built(0.5f);
    int found = 0;
    for (uint32_t i = 0; i < f->lists[11].count; i++) {
        const RdCmd *c = &f->lists[11].cmds[i];
        if (c->type == RDC_SCREEN && (c->keyLo || c->keyHi)) {
            found = 1;
            CHECK(screenVtx(f, c)[0].rgba[3] == 0x30, "the fade level half way (%u)",
                  screenVtx(f, c)[0].rgba[3]);
        }
    }
    CHECK(found, "the fade sprite is keyed");
}

static void testFeedback(void)
{
    /* rd__BlurFeedbackFix: a = (128 - FIX) / 128 per tick; dt 0.5 keeps
     * a^0.5 to the rounding of the FIX (half a step of 1/128) */
    for (int fix = 8; fix <= 120; fix += 16) {
        const uint8_t f2 = rd__BlurFeedbackFix(RD_BLEND_LERP_FIX, (uint8_t)fix, 0.5f);
        const double a = (128.0 - fix) / 128.0, a2 = (128.0 - f2) / 128.0;
        CHECK(fabs(a2 - sqrt(a)) <= 0.5 / 128.0 + 1e-9,
              "FIX %d at dt 0.5: %u keeps %.4f, a^0.5 %.4f", fix, f2, a2, sqrt(a));
        CHECK(rd__BlurFeedbackFix(RD_BLEND_LERP_FIX, (uint8_t)fix, 1.0f) == fix,
              "dt 1: FIX unchanged");
    }
    CHECK(rd__BlurFeedbackFix(RD_BLEND_CS_FIX_ADD_CD, 0x40, 0.5f) == 0x20, "additive: FIX x dt");
    /* the frame: a motion blur sprite and two aura sprites, one into FEED128 */
    for (int k = 0; k < 2; k++) {
        rd_BeginFrame();
        frameHead();
        rd_SelectList(7);
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.fix = 0x20;
        pp.blend = RD_BLEND_LERP_FIX;
        rd_Post(RD_POST_MOTION_BLUR, &pp);
        rd_SelectList(8);
        rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), (RdTarget){0}, 512, 512, 0);
        rd_Post(RD_POST_AURA, &pp);
        rd_SetTarget(rd_Target(RD_TARGET_FEED128), (RdTarget){0}, 128, 128, 0);
        rd_Post(RD_POST_AURA, &pp);
        rd_EndFrame(0);
    }
    for (int first = 1; first >= 0; first--) {
        const RdFrame *f =
            rd__InterpFrame(rd__PrevFrame(), rd__LastFrame(), 0.5f, 0.5f, first, NULL);
        float dt = 0.0f;
        int aura = 0;
        for (int l = 7; l <= 8; l++) {
            for (uint32_t i = 0; i < f->lists[l].count; i++) {
                const RdCmd *c = &f->lists[l].cmds[i];
                if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_MOTION_BLUR) {
                    RdPostRec r;
                    memcpy(&r, f->payload + c->u[1], sizeof(r));
                    dt = r.scalar[2];
                }
                aura += c->type == RDC_POST_STUB && c->b[0] == RD_POST_AURA;
            }
        }
        CHECK(dt == 0.5f, "the motion blur sprite stands for dt 0.5 (%g)", dt);
        CHECK(aura == (first ? 2 : 1), "%s present: %d aura sprites (FEED128's %s)",
              first ? "a tick's first" : "a later", aura, first ? "kept" : "dropped");
    }
}

/* ---------------------------------------------------------- on a device */

static uint8_t *readWork0(void)
{
    static uint8_t buf[TW * TH * 4];
    uint32_t w, h;
    if (!rd__ReadTarget(rd_Target(RD_TARGET_WORK0), buf, sizeof(buf), &w, &h) || w != TW ||
        h != TH) {
        CHECK(0, "WORK0 readback");
        return NULL;
    }
    return buf;
}

static void testPixels(void)
{
    static uint8_t img[4][TW * TH * 4];
    recordSprites(0);
    recordSprites(1);
    const RdFrame *prev = rd__PrevFrame(), *cur = rd__LastFrame();
    const uint8_t *p;
    rd__ReplayFrame(prev, 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[0], p, sizeof(img[0]));
    }
    rd__ReplayFrame(cur, 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[1], p, sizeof(img[1]));
    }
    /* KC (only in the current frame) is drawn at alpha 0 too: compare the
     * rows above it only where it is not: it sits at x 200..240 */
    rd__ReplayFrame(built(0.0f), 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[2], p, sizeof(img[2]));
    }
    rd__ReplayFrame(built(1.0f), 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[3], p, sizeof(img[3]));
    }
    int diff0 = 0, diff1 = 0;
    for (int y = 0; y < TH; y++) {
        for (int x = 0; x < TW; x++) {
            const size_t o = ((size_t)y * TW + x) * 4;
            const int inKC = x >= 200 && x < 240 && y >= 8 && y < 40;
            /* the unkeyed sprite is the current frame's at alpha 0 too */
            const int inUnkeyed = x >= 100 && x < 130 && y >= 90 && y < 100;
            if (!inKC && !inUnkeyed) {
                diff0 += memcmp(img[2] + o, img[0] + o, 4) != 0;
            }
            diff1 += memcmp(img[3] + o, img[1] + o, 4) != 0;
        }
    }
    CHECK(diff0 == 0, "alpha 0 replays as the previous frame (%d pixels differ)", diff0);
    CHECK(diff1 == 0, "alpha 1 replays as the current frame (%d pixels differ)", diff1);
    /* alpha 0.5: KA covers x 16..47 exactly */
    rd__ReplayFrame(built(0.5f), 0, false);
    if ((p = readWork0()) != NULL) {
        const int y = 20;
        const uint8_t *l = p + ((size_t)y * TW + 15) * 4, *a = p + ((size_t)y * TW + 16) * 4;
        const uint8_t *b = p + ((size_t)y * TW + 47) * 4, *r = p + ((size_t)y * TW + 48) * 4;
        CHECK(l[0] == 0 && a[0] == 150 && b[0] == 150 && r[0] == 0,
              "alpha 0.5: columns 16..47 drawn (15: %u, 16: %u, 47: %u, 48: %u)", l[0], a[0], b[0],
              r[0]);
    }
}

static void testPresent(void)
{
    RdSettings s = *rd_GetSettings();
    CHECK(!rd_InterpolationActive() && !rd_Present(0.5f), "Original: rd_Present does nothing");
    /* F2: the Original preset interpolates too when asked */
    s.interpolate = 1;
    rd_SetSettings(&s);
    recordSprites(0);
    CHECK(rd_InterpolationActive(), "Original with interpolate");
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f; /* the GS size: no target is recreated */
    s.interpolate = 0;
    rd_SetSettings(&s);
    recordSprites(0);
    CHECK(!rd_InterpolationActive() && !rd_Present(0.5f), "Enhanced, framerate original: none");
    s.interpolate = 1;
    rd_SetSettings(&s);
    recordSprites(0);
    recordSprites(1);
    CHECK(rd_InterpolationActive(), "Enhanced with interpolate");
    CHECK(rd_Present(0.25f) && rd_Present(0.75f), "rd_Present presents");
    /* a change of scale recreates the targets: the pair across it snaps,
     * the next blends */
    s.sceneScale = 2.0f;
    rd_SetSettings(&s);
    recordSprites(0);
    CHECK(build(0.5f, 1.0f, 1)->snap == RD_SNAP_HISTORY, "history dropped after a recreate");
    recordSprites(1);
    CHECK(build(0.5f, 1.0f, 1)->snap == RD_SNAP_NONE, "the next pair blends again");
    CHECK(rd_Present(0.5f), "rd_Present at 2x");
    s.preset = RD_PRESET_ORIGINAL;
    s.sceneScale = 0.0f;
    rd_SetSettings(&s);
    recordSprites(0);
}

/* ------------------------------------------------------ keyed text (R7d) */

static RdKey textKeyOf(const char *s)
{
    uint64_t h = 0xCBF29CE484222325ull;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 0x100000001B3ull;
    }
    return h ? h : 1;
}

/* n glyph sprites of 6 x 10 px from x, one rd_ScreenPrims call as font.c
 * makes it */
static void glyphs(int x, int y, int n, uint8_t alpha, RdKey key)
{
    RdScreenVtx v[16];
    memset(v, 0, sizeof(v));
    for (int i = 0; i < n && i < 8; i++) {
        RdScreenVtx *a = &v[2 * i], *c = &v[2 * i + 1];
        a->x = OX + (x + i * 8) * 16;
        a->y = OY + y * 16;
        c->x = a->x + 6 * 16;
        c->y = a->y + 10 * 16;
        a->s = (float)(i * 96);
        c->s = a->s + 96.0f;
        a->q = c->q = 1.0f;
        a->rgba[0] = c->rgba[0] = 128;
        a->rgba[1] = c->rgba[1] = 128;
        a->rgba[2] = c->rgba[2] = 128;
        a->rgba[3] = c->rgba[3] = alpha;
    }
    rd_ScreenPrims(RD_PRIM_SPRITES, v, (uint32_t)(2 * n), RD_SPACE_UI, 1, key);
}

/* "NEW GAME" fading out while it slides 16 px; "OPTIONS" twice (the halo
 * copies' ordinal); "LOAD" only in the first frame, "CONTINUE" only in the
 * second (a changed label) */
static void recordText(int second)
{
    rd_BeginFrame();
    frameHead();
    rd_SelectList(11);
    glyphs(10 + (second ? 16 : 0), 20, 8, second ? 0x00 : 0x80, textKeyOf("NEW GAME"));
    glyphs(10, 40, 7, 0x40, textKeyOf("OPTIONS"));
    glyphs(12 + (second ? 4 : 0), 40, 7, 0x40, textKeyOf("OPTIONS"));
    if (second) {
        glyphs(10, 60, 8, 0x80, textKeyOf("CONTINUE"));
    } else {
        glyphs(10, 60, 4, 0x80, textKeyOf("LOAD"));
    }
    rd_EndFrame(0);
}

static void testText(void)
{
    recordText(0);
    recordText(1);
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->keyed == 4 && st->lerped == 3 && st->missing == 1,
          "text: three strings blend, the changed label is unmatched (keyed %u lerped %u "
          "missing %u)",
          st->keyed, st->lerped, st->missing);
    const RdFrame *f = built(0.5f), *cur = rd__LastFrame();
    const RdScreenVtx *a = screenVtx(f, findKey(f, 11, textKeyOf("NEW GAME"), 0));
    int ok = a != NULL;
    for (int i = 0; ok && i < 8; i++) {
        ok &= a[2 * i].x == OX + (18 + i * 8) * 16 && a[2 * i + 1].x == a[2 * i].x + 6 * 16;
        ok &= a[2 * i].rgba[3] == 0x40 && a[2 * i + 1].rgba[3] == 0x40;
        ok &= a[2 * i].s == (float)(i * 96);
    }
    CHECK(ok, "text: every glyph 8 px over, alpha 0x80 -> 0 at 0x40, STs the current");
    const RdScreenVtx *o1 = screenVtx(f, findKey(f, 11, textKeyOf("OPTIONS"), 1));
    CHECK(o1 && o1[0].x == OX + 14 * 16, "text: the second copy matches the second (x %d)",
          o1 ? (o1[0].x - OX) / 16 : -1);
    const RdCmd *nc = findKey(f, 11, textKeyOf("CONTINUE"), 0);
    const RdCmd *ncc = findKey(cur, 11, textKeyOf("CONTINUE"), 0);
    CHECK(nc && ncc &&
              memcmp(f->payload + nc->u[0], cur->payload + ncc->u[0], 16 * sizeof(RdScreenVtx)) ==
                  0,
          "text: a new label is the current frame's");
}

/* ------------------------------------------------- morphing meshes (R7d) */

/* the creation stream of a 3-vertex prelit batch with vertex 1 at x */
static void morphStream(float (*qw)[4], float x)
{
    memset(qw, 0, 10 * 16);
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 3][0] = (float)(k * 10);
        qw[1 + k * 3][3] = 1.0f;
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        qw[1 + k * 3 + 2][0] = 128.0f;               /* the colour */
    }
    qw[1 + 3][0] = x;
}

static RdMesh morphMesh(float x)
{
    static float qw[10][4];
    morphStream(qw, x);
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 10;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    return rd_CreateVuMesh(&md);
}

static void morphUpdate(RdMesh m, float x)
{
    static float qw[10][4];
    morphStream(qw, x);
    rd_UpdateVuMesh(m, (const float (*)[4])qw);
}

static void morphDrawAt(RdMesh m, RdKey key)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 16);
    d.vu.mem[19][0] = 2048.0f;
    d.vu.mem[19][1] = 2048.0f;
    rd_SelectList(0);
    rd_DrawVuMesh(m, &d, key);
}

/* vertex 1's x in the stream the n-th draw of key in f replays */
static float morphX(const RdFrame *f, RdKey key, int nth)
{
    const RdCmd *c = findKey(f, 0, key, nth);
    const RdMeshRec *m = c ? rd__MeshRec(c->u[0]) : NULL;
    return m ? m->stream[RD_VU_QW_PRELIT][0] : -1.0f;
}

static void testMorph(void)
{
    RdSettings s = *rd_GetSettings(), keep = s;
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.interpolate = 1;
    rd_SetSettings(&s);
    const RdKey kTwin = RD_KEY(&kObjD, 5, 0), kOne = RD_KEY(&kObjD, 6, 0);
    /* the twins: A in odd frames, B in even ones, rewritten before the
     * draw (reg_setShape); "one": a single mesh rewritten every frame */
    RdMesh a = morphMesh(0.0f), b = morphMesh(0.0f), one = morphMesh(0.0f);
    CHECK(a.id && b.id && one.id, "the morph meshes");
    rd_BeginFrame();
    frameHead();
    morphUpdate(a, 100.0f);
    morphDrawAt(a, kTwin);
    morphUpdate(one, 100.0f);
    morphDrawAt(one, kOne);
    rd_EndFrame(0);
    rd_BeginFrame();
    frameHead();
    morphUpdate(b, 120.0f);
    morphDrawAt(b, kTwin);
    morphUpdate(one, 120.0f);
    morphDrawAt(one, kOne);
    rd_EndFrame(0);
    /* the next tick records: A and "one" take the third shape */
    rd_BeginFrame();
    frameHead();
    morphUpdate(a, 200.0f);
    morphDrawAt(a, kTwin);
    morphUpdate(one, 200.0f);
    morphDrawAt(one, kOne);
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->lerped == 2 && st->mismatch == 0 && st->morph == 2,
          "morph: the twins match across their two meshes, both streams blended (lerped %u "
          "mismatch %u morph %u)",
          st->lerped, st->mismatch, st->morph);
    const RdFrame *f = built(0.5f);
    CHECK(morphX(f, kTwin, 0) == 110.0f && morphX(f, kOne, 0) == 110.0f,
          "morph: half way between the ticks' shapes (%g, %g)", morphX(f, kTwin, 0),
          morphX(f, kOne, 0));
    f = built(1.0f);
    CHECK(morphX(f, kTwin, 0) == 120.0f && morphX(f, kOne, 0) == 120.0f,
          "morph: alpha 1 is the current tick's shape, not the newer (%g, %g)", morphX(f, kTwin, 0),
          morphX(f, kOne, 0));
    f = built(0.0f);
    CHECK(morphX(f, kTwin, 0) == 100.0f && morphX(f, kOne, 0) == 100.0f,
          "morph: alpha 0 is the previous tick's shape (%g, %g)", morphX(f, kTwin, 0),
          morphX(f, kOne, 0));
    if (g_rd.hasDevice) {
        CHECK(rd__ReplayFrame(built(0.5f), 0, false), "morph: the half-way frame replays");
    }
    rd_EndFrame(0);
    rd_SetSettings(&keep);
    rd_BeginFrame();
    rd_EndFrame(0);
    rd_DestroyVuMesh(a);
    rd_DestroyVuMesh(b);
    rd_DestroyVuMesh(one);
}

/* ------------------------------------------- S2: rotation-aware blend */

/* column-major 4 x 4 (double): a turn of deg about z, uniform scale sc,
 * translation (tx, 0, 0) */
static void turnZ(double *m, double deg, double sc, double tx)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    memset(m, 0, 16 * sizeof(double));
    m[0] = cos(a) * sc;
    m[1] = sin(a) * sc;
    m[4] = -sin(a) * sc;
    m[5] = cos(a) * sc;
    m[10] = sc;
    m[12] = tx;
    m[15] = 1.0;
}

static double colLen(const double *m, int c)
{
    return sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2]);
}

static void testRotationBlend(void)
{
    double p[16], c[16], o[16];
    turnZ(p, 0.0, 1.0, 0.0);
    turnZ(c, 90.0, 1.0, 100.0);
    CHECK(rd__BlendAffine(p, c, 0.5, NULL, o), "a 90 degree turn blends");
    const double ang = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
    CHECK(fabs(ang - 45.0) < 1e-9 && fabs(colLen(o, 0) - 1.0) < 1e-12 &&
              fabs(colLen(o, 1) - 1.0) < 1e-12 && fabs(colLen(o, 2) - 1.0) < 1e-12 &&
              fabs(o[12] - 50.0) < 1e-12,
          "90 degrees at alpha 0.5: %.9f degrees, axes %.12f %.12f, x %.3f (element-wise: "
          "axes 0.707)",
          ang, colLen(o, 0), colLen(o, 1), o[12]);
    printf("rd_interp_test: 90 degrees at alpha 0.5: %.6f degrees, axes %.9f %.9f %.9f\n", ang,
           colLen(o, 0), colLen(o, 1), colLen(o, 2));
    turnZ(p, -30.0, 2.0, 0.0);
    turnZ(c, 50.0, 3.0, 0.0);
    CHECK(rd__BlendAffine(p, c, 0.25, NULL, o), "turn with scale blends");
    const double ang2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
    CHECK(fabs(ang2 - (-10.0)) < 1e-9 && fabs(colLen(o, 0) - 2.25) < 1e-12,
          "scale lerped, rotation slerped (%.6f degrees, scale %.6f)", ang2, colLen(o, 0));
    turnZ(p, 170.0, 1.0, 0.0);
    turnZ(c, -170.0, 1.0, 0.0);
    CHECK(rd__BlendAffine(p, c, 0.5, NULL, o) &&
              fabs(fabs(atan2(o[1], o[0])) * 180.0 / 3.14159265358979323846 - 180.0) < 1e-9,
          "the short way round (170 to -170 is 180 half way)");
    /* about a pivot: a limb turning 90 degrees about its joint at (100, 0,
     * 0) keeps the joint where it is half way (about the origin it would
     * leave it by 29 units) */
    {
        const double x0[3] = {100.0, 0.0, 0.0};
        turnZ(p, 0.0, 1.0, 0.0);
        turnZ(c, 90.0, 1.0, 100.0); /* R x + (100, -100, 0): R (x - x0) + x0 */
        c[13] = -100.0;
        CHECK(rd__BlendAffine(p, c, 0.5, x0, o), "about a pivot");
        const double jx = o[0] * 100.0 + o[12], jy = o[1] * 100.0 + o[13];
        const double a2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
        CHECK(fabs(jx - 100.0) < 1e-9 && fabs(jy) < 1e-9 && fabs(a2 - 45.0) < 1e-9,
              "the joint stays at (100, 0) half way (%.6f, %.6f), turned %.6f degrees", jx, jy, a2);
    }
    turnZ(c, 10.0, 1.0, 0.0);
    c[3] = 0.25; /* projective */
    CHECK(!rd__BlendAffine(p, c, 0.5, NULL, o),
          "a projective matrix is left to the element-wise blend");
    turnZ(p, 0.0, 1.0, 0.0);
    turnZ(c, 10.0, 1.0, 0.0);
    c[10] = -1.0; /* a mirror */
    CHECK(!rd__BlendAffine(p, c, 0.5, NULL, o),
          "opposite handedness is left to the element-wise blend");
}

/* a prelit mesh whose model to world turns deg about z, seen through a
 * world to screen S (a scale and a translation, w = 1); and a skinned draw
 * with one bone turning the same way */
static void recordTurn(RdMesh mesh, RdMesh skin, double deg)
{
    rd_BeginFrame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    double s[16], w[16], m[16];
    memset(s, 0, sizeof(s));
    s[0] = 2.0;
    s[5] = 2.0;
    s[10] = 1.0;
    s[12] = 2048.0;
    s[13] = 2048.0;
    s[15] = 1.0;
    turnZ(w, deg, 1.0, 10.0);
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += s[k * 4 + r] * w[cI * 4 + k];
            }
            m[cI * 4 + r] = v;
        }
    }
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            d.vu.mem[4 + cI][r] = (float)s[cI * 4 + r];
            d.vu.mem[16 + cI][r] = (float)m[cI * 4 + r];
            d.vu.mem[20 + cI][r] = (float)m[cI * 4 + r]; /* the clip matrix: the same here */
            d.vu.mem[24 + cI][r] = (float)w[cI * 4 + r]; /* model to view: view identity */
        }
    }
    identity(d.vu.mem, 12);
    rd_SelectList(0);
    rd_DrawVuMesh(mesh, &d, RD_KEY(&kObjD, 0, 32));
    /* skinned: one bone */
    static float bone[4][4];
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            bone[cI][r] = (float)w[cI * 4 + r];
        }
    }
    d.prog = RD_PROG_SKIN;
    d.code = 20;
    d.bones = (const float (*)[4])bone;
    d.boneQw = 4;
    rd_DrawVuMesh(skin, &d, RD_KEY(&kObjD, 1, 20));
    rd_EndFrame(0);
}

/* a cluster (skinned) layout mesh of one strip of 3 vertices */
static RdMesh makeSkinMesh(void)
{
    static float qw[1 + 3 * 5][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 5 + 3][3] = k == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        const uint32_t addr = 16u;                   /* bone 0, weight 1: its pivot is the origin */
        memcpy(&qw[1 + k * 5 + 2][0], &addr, 4);
        qw[1 + k * 5 + 2][1] = 1.0f;
        memcpy(&qw[1 + k * 5 + 2][2], &addr, 4);
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 16;
    md.qwPerVertex = RD_VU_QW_SKIN;
    md.batchCount = 1;
    md.batches = &bd;
    return rd_CreateVuMesh(&md);
}

static void testRotationDraws(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 90.0);
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->lerped == 2 && st->rotated == 2, "mesh and skinned draw blend as rotations (%u, %u)",
          st->lerped, st->rotated);
    const RdFrame *f = built(0.5f);
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    if (m) {
        /* S (2, 2, 1) times a 45 degree turn: the x axis (sqrt 2, sqrt 2) */
        const float ax = m[16][0], ay = m[16][1];
        const float len = sqrtf(ax * ax + ay * ay);
        CHECK(fabsf(len - 2.0f) < 1e-4f && fabsf(ax - ay) < 1e-4f &&
                  fabsf(m[19][0] - 2068.0f) < 1e-3f,
              "model to screen: 45 degrees, length %.5f (element-wise 1.414), origin x %.3f", len,
              m[19][0]);
        const float bx = m[24][0], by = m[24][1];
        CHECK(fabsf(sqrtf(bx * bx + by * by) - 1.0f) < 1e-5f && fabsf(bx - by) < 1e-5f,
              "model to view: 45 degrees, no scale change (%.5f, %.5f)", bx, by);
    } else {
        CHECK(0, "the turning mesh");
    }
    const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    if (sk) {
        const RdVuPayload *h = (const RdVuPayload *)(const void *)(f->payload + sk->u[1]);
        const float (*b)[4] = (const float (*)[4])(
            const void *)(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        CHECK(h->boneQw == 4 &&
                  fabsf(sqrtf(b[0][0] * b[0][0] + b[0][1] * b[0][1]) - 1.0f) < 1e-5f &&
                  fabsf(b[0][0] - b[0][1]) < 1e-5f && fabsf(b[3][0] - 10.0f) < 1e-5f,
              "bone: 45 degrees, no scale change (%.5f, %.5f)", b[0][0], b[0][1]);
    } else {
        CHECK(0, "the skinned draw");
    }
    /* a turn of 150 degrees in a tick is a flip: the tick's matrices */
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 150.0);
    build(0.5f, 1.0f, 1);
    f = built(0.5f);
    const RdFrame *cur = rd__LastFrame();
    const float (*mf)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjD, 0, 32), 0));
    const float (*mc)[4] = vuBlock(cur, findKey(cur, 0, RD_KEY(&kObjD, 0, 32), 0));
    CHECK(mf && mc && memcmp(mf[16], mc[16], 12 * 16) == 0,
          "a 150 degree turn in a tick keeps the tick's model matrices");
    sk = findKey(f, 0, RD_KEY(&kObjD, 1, 20), 0);
    const RdCmd *skc = findKey(cur, 0, RD_KEY(&kObjD, 1, 20), 0);
    CHECK(sk && skc &&
              memcmp(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock),
                     cur->payload + skc->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock), 64) == 0,
          "a 150 degree turn in a tick keeps the tick's bone");
}

/* ------------------------------------------- S6: the blended camera */

static void mul4(const double *a, const double *b, double *o)
{
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            double v = 0.0;
            for (int k = 0; k < 4; k++) {
                v += a[k * 4 + r] * b[c * 4 + k];
            }
            o[c * 4 + r] = v;
        }
    }
}

#define S6_RADIUS 500.0

/* The view of a camera that has turned deg (yaw) with its eye on a circle
 * of S6_RADIUS about the origin, looking at it, the eye at eye; written
 * from the rotation and the eye: x_v = R (x - eye). */
static void s6View(double deg, const double eye[3], double *v)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    memset(v, 0, 16 * sizeof(double));
    v[0] = cos(a);
    v[2] = -sin(a);
    v[5] = 1.0;
    v[8] = sin(a);
    v[10] = cos(a);
    v[15] = 1.0;
    for (int r = 0; r < 3; r++) {
        v[12 + r] = -(v[r] * eye[0] + v[4 + r] * eye[1] + v[8 + r] * eye[2]);
    }
}

static void s6OrbitEye(double deg, double eye[3])
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    eye[0] = S6_RADIUS * sin(a);
    eye[1] = 0.0;
    eye[2] = -S6_RADIUS * cos(a);
}

/* the focal length of the tick's projection (GsBase.c's zoom eases) */
static double s6Focal = 500.0;

/* a GS screen matrix: x = 2048 + f x_v / z_v, w = z_v (invertible) */
static void s6Proj(double *p)
{
    memset(p, 0, 16 * sizeof(double));
    p[0] = s6Focal;
    p[5] = s6Focal;
    p[8] = 2048.0;
    p[9] = 2048.0;
    p[10] = 1.0;
    p[11] = 1.0;
    p[14] = 1.0;
}

static void s6Translate(double *w, double x, double y, double z)
{
    memset(w, 0, 16 * sizeof(double));
    w[0] = w[5] = w[10] = w[15] = 1.0;
    w[12] = x;
    w[13] = y;
    w[14] = z;
}

/* the GS X and Y of the model point x through a model to screen matrix */
static void s6Project(const double *m, const double x[3], double out[2])
{
    double h[4];
    for (int r = 0; r < 4; r++) {
        h[r] = m[r] * x[0] + m[4 + r] * x[1] + m[8 + r] * x[2] + m[12 + r];
    }
    out[0] = h[0] / h[3];
    out[1] = h[1] / h[3];
}

static void s6ProjectF(const float (*m)[4], const double x[3], double out[2])
{
    double d[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            d[c * 4 + r] = m[16 + c][r];
        }
    }
    s6Project(d, x, out);
}

static const char kObjS6;

static const double kS6PointA[3] = {300.0, 0.0, 0.0}; /* in A's model space (W = I) */
static const double kS6PointB[3] = {0.0, 0.0, 0.0};   /* B's origin: W = (-200, 0, 150) */

/* a prelit static mesh with model to world w through the camera v */
static void s6Draw(RdMesh mesh, const double *v, const double *w, RdKey key)
{
    double p[16], s[16], m[16], vw[16];
    s6Proj(p);
    mul4(p, v, s);
    mul4(s, w, m);
    mul4(v, w, vw);
    /* the inverse view: R^T and the eye */
    double iv[16];
    memset(iv, 0, sizeof(iv));
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            iv[c * 4 + r] = v[r * 4 + c];
        }
    }
    for (int r = 0; r < 3; r++) {
        iv[12 + r] = -(iv[r] * v[12] + iv[4 + r] * v[13] + iv[8 + r] * v[14]);
    }
    iv[15] = 1.0;
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            d.vu.mem[4 + c][r] = (float)s[c * 4 + r];
            d.vu.mem[12 + c][r] = (float)iv[c * 4 + r];
            d.vu.mem[16 + c][r] = (float)m[c * 4 + r];
            d.vu.mem[20 + c][r] = (float)m[c * 4 + r];
            d.vu.mem[24 + c][r] = (float)vw[c * 4 + r];
        }
    }
    rd_SelectList(0);
    rd_DrawVuMesh(mesh, &d, key);
}

/* one frame: the camera turned deg about the origin; mesh A (key part 0)
 * and, keyed by part bPart (0: not drawn), mesh B */
static void s6Frame(RdMesh mesh, double deg, int bPart)
{
    rd_BeginFrame();
    frameHead();
    double eye[3], v[16], p[16], wa[16], wb[16];
    s6OrbitEye(deg, eye);
    s6View(deg, eye, v);
    s6Proj(p);
    RdCamera cam;
    memset(&cam, 0, sizeof(cam));
    for (int k = 0; k < 16; k++) {
        cam.view[k] = (float)v[k];
        cam.proj43[k] = (float)p[k];
    }
    cam.zoom = 500.0f;
    rd_SetCamera(&cam);
    s6Translate(wa, 0.0, 0.0, 0.0);
    s6Translate(wb, -200.0, 0.0, 150.0);
    s6Draw(mesh, v, wa, RD_KEY(&kObjS6, 0, 32));
    if (bPart) {
        s6Draw(mesh, v, wb, RD_KEY(&kObjS6, bPart, 32));
    }
    rd_EndFrame(0);
}

/* where a static point lands half way: the camera turned 14 degrees, its
 * eye half way along the line between the two ticks' eyes, the focal
 * length focal */
static void s6Expected(const double *w, const double x[3], double focal, double out[2])
{
    double e0[3], e1[3], eye[3], v[16], p[16], s[16], m[16];
    s6OrbitEye(0.0, e0);
    s6OrbitEye(28.0, e1);
    for (int k = 0; k < 3; k++) {
        eye[k] = 0.5 * (e0[k] + e1[k]);
    }
    s6View(14.0, eye, v);
    const double keep = s6Focal;
    s6Focal = focal;
    s6Proj(p);
    s6Focal = keep;
    mul4(p, v, s);
    mul4(s, w, m);
    s6Project(m, x, out);
}

static void testCameraBlend(void)
{
    RdMesh mesh = makeMesh();
    double wa[16], wb[16], want[2], got[2], mean[2];
    s6Translate(wa, 0.0, 0.0, 0.0);
    s6Translate(wb, -200.0, 0.0, 150.0);

    /* the camera turns 28 degrees about the target in the tick (30 is the
     * cut threshold, RD_INTERP_CAMERA_TURN; the eye moves 242 of 300) */
    s6Frame(mesh, 0.0, 0);
    s6Frame(mesh, 28.0, 0);
    const RdInterpStats *st = build(0.5f, 1.0f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->lerped == 1 && st->rebased == 1,
          "camera turn: the static mesh blends through the blended camera (snap %u, lerped %u, "
          "rebased %u)",
          st->snap, st->lerped, st->rebased);
    const RdFrame *f = built(0.5f);
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 0, 32), 0));
    if (m) {
        s6Expected(wa, kS6PointA, 500.0, want);
        s6ProjectF(m, kS6PointA, got);
        /* the element-wise mean of the two ticks' model to screen */
        const RdFrame *pf = rd__PrevFrame(), *cf = rd__LastFrame();
        const float (*mp)[4] = vuBlock(pf, findKey(pf, 0, RD_KEY(&kObjS6, 0, 32), 0));
        const float (*mc)[4] = vuBlock(cf, findKey(cf, 0, RD_KEY(&kObjS6, 0, 32), 0));
        double mm[16];
        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 4; r++) {
                mm[c * 4 + r] = 0.5 * ((double)mp[16 + c][r] + (double)mc[16 + c][r]);
            }
        }
        s6Project(mm, kS6PointA, mean);
        const double err = hypot(got[0] - want[0], got[1] - want[1]);
        const double off = hypot(mean[0] - want[0], mean[1] - want[1]);
        CHECK(err < 0.01 && off > 2.0,
              "camera turn: the point at the 14 degree projection (%.3f, want %.3f: off by %.4f; "
              "the element-wise mean %.3f is %.2f away)",
              got[0], want[0], err, mean[0], off);
        /* the inverse view is the blended camera's (rigid: the eye half way) */
        double e0[3], e1[3];
        s6OrbitEye(0.0, e0);
        s6OrbitEye(28.0, e1);
        CHECK(fabs(m[15][0] - 0.5 * (e0[0] + e1[0])) < 1e-3 &&
                  fabs(m[15][2] - 0.5 * (e0[2] + e1[2])) < 1e-3,
              "camera turn: the inverse view's eye half way (%.3f, %.3f)", m[15][0], m[15][2]);
        const float yaw = atan2f(-f->camera.view[2], f->camera.view[0]) * 180.0f / 3.14159265f;
        CHECK(fabsf(yaw - 14.0f) < 1e-3f, "camera turn: RdCamera.view half way (%.4f)", yaw);
    } else {
        CHECK(0, "camera turn: the mesh");
    }

    /* two static meshes, B new in cur (unmatched): both through the same
     * camera, each where the blended camera puts it */
    s6Frame(mesh, 0.0, 0);
    s6Frame(mesh, 28.0, 1);
    st = build(0.5f, 1.0f, 1);
    CHECK(st->lerped == 1 && st->missing == 1 && st->rebased == 2 && st->rebasedCur == 1,
          "unmatched neighbour: one blended, one unmatched, both re-based (%u, %u, %u, %u)",
          st->lerped, st->missing, st->rebased, st->rebasedCur);
    f = built(0.5f);
    const float (*ma)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 0, 32), 0));
    const float (*mb)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 1, 32), 0));
    if (ma && mb) {
        double ga[2], gb[2], wa2[2], wb2[2];
        s6ProjectF(ma, kS6PointA, ga);
        s6ProjectF(mb, kS6PointB, gb);
        s6Expected(wa, kS6PointA, 500.0, wa2);
        s6Expected(wb, kS6PointB, 500.0, wb2);
        float d = 0.0f;
        for (int q = 4; q < 16; q++) {
            for (int r = 0; r < 4; r++) {
                d = fmaxf(d, fabsf(ma[q][r] - mb[q][r]) / (1.0f + fabsf(ma[q][r])));
            }
        }
        CHECK(d < 1e-5f, "unmatched neighbour: the same camera block (qw 4..15) (%g)", (double)d);
        CHECK(hypot(ga[0] - wa2[0], ga[1] - wa2[1]) < 0.01 &&
                  hypot(gb[0] - wb2[0], gb[1] - wb2[1]) < 0.01,
              "unmatched neighbour: A at %.3f (want %.3f), B at %.3f (want %.3f)", ga[0], wa2[0],
              gb[0], wb2[0]);
        /* without S6, B was cur's: at the 28 degree camera */
        const RdFrame *cf = rd__LastFrame();
        const float (*mbc)[4] = vuBlock(cf, findKey(cf, 0, RD_KEY(&kObjS6, 1, 32), 0));
        double gc[2];
        s6ProjectF(mbc, kS6PointB, gc);
        CHECK(hypot(gc[0] - wb2[0], gc[1] - wb2[1]) > 2.0,
              "unmatched neighbour: the tick's B (%.3f) is away from the half-way one", gc[0]);
    } else {
        CHECK(0, "unmatched neighbour: the meshes");
    }

    /* the same while the zoom eases (focal length 500 to 520): the
     * unmatched mesh takes the half-way projection (510) with its
     * neighbour */
    s6Frame(mesh, 0.0, 0);
    s6Focal = 520.0;
    s6Frame(mesh, 28.0, 1);
    s6Focal = 500.0;
    st = build(0.5f, 1.0f, 1);
    f = built(0.5f);
    ma = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 0, 32), 0));
    mb = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 1, 32), 0));
    if (ma && mb) {
        double ga[2], gb[2], wa2[2], wb2[2];
        s6ProjectF(ma, kS6PointA, ga);
        s6ProjectF(mb, kS6PointB, gb);
        s6Expected(wa, kS6PointA, 510.0, wa2);
        s6Expected(wb, kS6PointB, 510.0, wb2);
        CHECK(st->rebasedCur == 1 && hypot(ga[0] - wa2[0], ga[1] - wa2[1]) < 0.01 &&
                  hypot(gb[0] - wb2[0], gb[1] - wb2[1]) < 0.01,
              "zoom: A at %.3f (want %.3f), unmatched B at %.3f (want %.3f)", ga[0], wa2[0], gb[0],
              wb2[0]);
    } else {
        CHECK(0, "zoom: the meshes");
    }

    /* a still camera: nothing is re-based, the blocks are the element-wise
     * blend as before (bit for bit) */
    s6Frame(mesh, 10.0, 0);
    s6Frame(mesh, 10.0, 1);
    st = build(0.5f, 1.0f, 1);
    f = built(0.5f);
    const RdFrame *cf = rd__LastFrame();
    const float (*mo)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 1, 32), 0));
    const float (*mc)[4] = vuBlock(cf, findKey(cf, 0, RD_KEY(&kObjS6, 1, 32), 0));
    CHECK(st->rebased == 0 && mo && mc && memcmp(mo, mc, 36 * 16) == 0,
          "still camera: nothing re-based, the unmatched mesh is the tick's (%u)", st->rebased);
}

/* ------------------------------------------------- S2: the present clock */

static void testPresentClock(void)
{
    /* presents every 8.34 ms (two a 60 Hz refresh, mailbox), each measured
     * with up to +-3 ms of jitter (the step and the sleeps before it);
     * ticks every 33.37 ms.  The alpha steps between presents of one tick
     * should be 0.25; the raw measured alpha's jitter passes straight on. */
    RdPresentClock c;
    memset(&c, 0, sizeof(c));
    const double gap = 8.3417, tick = 33.3667;
    uint32_t seed = 12345u;
    double sumRaw = 0.0, sumClk = 0.0;
    int n = 0, monotonic = 1;
    float lastA = -1.0f;
    double lastTick = -1.0;
    double prevRaw = 0.0, prevClk = 0.0;
    for (int i = 0; i < 2000; i++) {
        const double truth = 1000.0 + i * gap;
        seed = seed * 1664525u + 1013904223u;
        const double jit = ((double)(seed >> 8) / 16777216.0 - 0.5) * 6.0;
        const double now = truth + jit;
        const double tickAt = floor((truth - 1000.0) / tick) * tick + 1000.0 - 0.5;
        const float a = rd_PresentClockAlpha(&c, now, tickAt, tick, gap);
        double raw = (now - tickAt) / tick;
        raw = raw < 0.0 ? 0.0 : (raw > 0.999 ? 0.999 : raw);
        if (tickAt == lastTick && i > 100) {
            /* the step within a tick, against the ideal gap / tick */
            const double ideal = gap / tick;
            sumRaw += (raw - prevRaw - ideal) * (raw - prevRaw - ideal);
            sumClk += (a - prevClk - ideal) * (a - prevClk - ideal);
            n++;
            monotonic &= a >= lastA;
        }
        prevRaw = raw;
        prevClk = a;
        lastA = a;
        lastTick = tickAt;
    }
    const double rmsRaw = sqrt(sumRaw / n), rmsClk = sqrt(sumClk / n);
    CHECK(rmsClk < rmsRaw / 3.0 && monotonic,
          "present clock: alpha step error rms %.4f (measured time %.4f), monotonic within a tick",
          rmsClk, rmsRaw);
    printf("rd_interp_test: present clock: alpha step error rms %.4f, measured time %.4f\n", rmsClk,
           rmsRaw);
    CHECK(c.resets == 1, "present clock: no reset after the first present (%u)", c.resets);
    /* a pause (the window dragged, a load): the clock restarts at the
     * measured time */
    const float a = rd_PresentClockAlpha(&c, 1000.0 + 2000 * gap + 500.0,
                                         1000.0 + 2000 * gap + 490.0, tick, gap);
    CHECK(c.resets == 2 && fabsf(a - (float)(10.0 / tick)) < 1e-5f,
          "present clock: a 500 ms pause resets it (alpha %.4f)", a);
}

static void runCpu(void)
{
    testRotationBlend();
    testRotationDraws();
    testCameraBlend();
    testPresentClock();
    testSprites();
    testSpriteSnaps();
    testFrameSnaps();
    testVu();
    testFade();
    testFeedback();
    testText();
    testMorph();
}

int main(void)
{
    if (!rd__InitRecordOnly(512, 512)) {
        printf("rd_interp_test: no context\n");
        return 1;
    }
    rd__SetNotImplementedFatal(false);
    runCpu();
    rd_Shutdown();

    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    if (!rd_Init(512, 512, &s, NULL)) {
        printf("rd_interp_test: no usable device: the pixel and present cases skipped\n");
    } else {
        rd__SetNotImplementedFatal(false);
        runCpu(); /* again, with the frames replayed as they close */
        testPixels();
        testPresent();
        CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
        const uint32_t verr = rhi_vk_ValidationErrorCount();
        CHECK(verr == 0, "%u validation errors", verr);
        rd_Shutdown();
    }
    if (failures) {
        printf("rd_interp_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_interp_test: ok\n");
    return 0;
}
