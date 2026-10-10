/* rd_interp_test.c: presentation between ticks.
 *
 * Two synthetic frames with keyed draws, blended by rd__interp_frame.
 * Without a device (recording only):
 *   ends      alpha 0 gives the previous frame's draw data, alpha 1 the
 *             current frame's payload byte for byte
 *   half      a keyed UI sprite translated by 32 px lands 16 px over (within
 *             1/16 px, the 12.4 step), its colour half way; an unkeyed one
 *             keeps the current frame's
 *   snaps     a key missing from the previous frame, a vertex count change,
 *             a jump past the screen threshold, a camera cut (rd_camera_cut),
 *             a fully faded frame (the fade edge), a discarded frame between
 *             (gap), a camera turn past the threshold: the current data
 *   ordinal   the same key twice matches in order
 *   photo     two paused photo ticks whose cameras differ:
 *             the camera blends half way, and list 11's full-screen prims
 *             (brightness, letterbox, film noise) are all kept
 *   grid      a grid's vertices blend one by one, its STs and headers stay
 *   mesh      the VU block's matrices blend, the UV scroll across Texture.c's
 *             wrap by 2 blends the short way, a model origin moved further
 *             than the world threshold snaps
 *   particles a batch's particles blend; one moved further than four sizes
 *             keeps the current position
 *   shadow    a shadow volume's vertices blend; a triangle count change snaps
 *   prisms    Shadow.c's prisms regrouped by their tags: two ticks of
 *             equal counts whose faces changed sides blend prism by prism,
 *             every prism closed (its faces' signed areas net to 0) and half
 *             way; a prism of one tick only is drawn on the nearer tick's
 *             side of t = 0.5; alpha 1 is the current volume byte for byte
 *   fade      the fade sprite's level blends (the post sprite is keyed)
 *   text      a string's glyph quads keyed as port/ui/font.c keys them
 *             (the string's hash): moved and faded, every glyph blends half
 *             way (the alpha 0x80 -> 0 text at 0x40); another string is
 *             another key and is the current; the same string twice matches
 *             in order
 *   deferred  RDC_OVERLAY_TEXT items: the anchor, alpha and
 *             glow stretch blend half way; a jump past the screen threshold
 *             and a changed size are the current item; the fade op after
 *             them blends; alpha 0 and 1 are the two ticks' items
 *   morph     a morphing part as RegistPacket.c draws it: two meshes
 *             of one layout drawn in alternate frames under one key, the
 *             older rewritten (rd_update_vu_mesh) while the next frame
 *             records: the half-way frame draws a scratch mesh whose
 *             positions are half way between the two ticks' shapes (the
 *             older from the kept version), alpha 1 the current tick's
 *             shape, not the newer one; a mesh rewritten every frame the
 *             same; the replays use the kept streams
 *   feedback  (issue 28) a frame whose motion blur sprite reads
 *             DISPLAY copies DISPLAY into DISPLAY_HELD at the head of a
 *             tick's first present and back at the later ones', and its
 *             motion blur sprite is the recorded one in every present (the
 *             tick's FIX); a frame that writes FEED128 keeps every aura
 *             sprite in every present, and its head copies FEED128 into
 *             FEED_HELD in a tick's first present and back in the later
 *             ones; neither copy without the read or the write
 * On a device (skipped without one):
 *   pixels    replays of the blended frame at alpha 0 and 1 equal the
 *             previous and current frames' replays byte for byte; at 0.5 the
 *             translated sprite covers the half-way columns exactly
 *   mirage    a tick that pastes FEED128 over SCENE and
 *             copies SCENE back into it, presented twice (alpha 0.5 as the
 *             first present, 1 as a later one): the two SCENEs and
 *             FEED128s are byte-identical, and FEED128 advances once per tick
 *   present   in either preset, rd_present does nothing with interpolate
 *             off and presents with it on; a change of scale drops the
 *             history (the next pair snaps)
 * The shake of the glowing coffins before the Queen, without a device and
 * again with one:
 *   shine     a list-8 shine draw (the mirage's mask, into AURA_WORK) and
 *             the same object's list-0 draw, the camera turning 20 degrees
 *             in the tick: at alpha 0.25, 0.5 and 0.75 a point of the mask
 *             lands on the same point of the stone (within 1/16 GS pixel)
 *   swap      two emitters' particle batches of equal count in swapped
 *             order: keyed by emitter each blends half way from its own
 *   sine      Texture.c's sine scroll stepping 1.23 in a tick (amplitude
 *             0.8 at 7 Hz, 25 ticks a second), marked RD_VU_SCROLL_SINE_U:
 *             blended straight at alpha 0.25, 0.5 and 0.75 (unmarked, the
 *             linear scroll's unwrap put it half a repeat off at 0.25 and
 *             0.75); SET_UVOFFSET's quadword w marks V, the common block
 *             clears the mark
 *   lights    the boy's lights in the dark hall (two F12 dumps), two of
 *             which change slots between the ticks: on a skinned draw, a lit
 *             mesh and a lit grid, every light's direction and colour at
 *             alpha 0.5 lies between the two ticks' values of the same
 *             light, and the shading of normals all round between the two
 *             ticks' shading
 *   instances still instances under one key, the first gone: each blends
 *             with itself; one gone and one new: the two are paired, the
 *             new one drawn as the tick's; moving instances pair by
 *             ordinal; the pool's ripples (two F12 dumps' blocks) each stay
 *             at their place
 *   paired    a moved instance beside still ones under one key pairs with
 *             its own draw (no draw unmatched while one of its key is
 *             free); an emitter's batch count changing keeps every batch
 *             paired or drawn; the camera turning 28 degrees: a mesh of the
 *             tick before alone that left the picture is drawn at alpha
 *             0.25, 0.5 and 0.75 where the blended camera sees it, one still
 *             in the picture is not, nor one whose object and part the
 *             current tick draws; with a still camera such a draw leaves the
 *             frame byte for byte as it was
 *   grid STs  a grid sampling a target blends its STs half way; a grid
 *             with an image keeps the tick's
 *   rising    a skinned draw whose bone 0 rises 40 units and turns 30
 *             degrees in the tick (a shadow climbing out of its pool, its
 *             pivot 97 units from the bone's origin): at alpha 0.25, 0.5 and
 *             0.75 it blends as a rotation, the bone's origin stays between
 *             the ticks' heights and the skin's centroid on the line between
 *             its two places
 *   locked    the camera backs away 60 units and turns 8 degrees in the
 *             tick: at alpha 0.25 and 0.5 a part locked to the camera (node
 *             flag 2, RD_VU_VIEW_LOCKED: the 500 unit screen times its place
 *             in view space, as reg_setMMatrixPacket builds it), matched or
 *             the tick's alone, stays where the tick put it on the screen;
 *             a billboard (node flag 4, RD_VU_VIEW_FACING), matched or the
 *             tick's alone, is where the blended camera sees its world
 *             point and faces that camera (no turn about the view's axes);
 *             the same locked part of the tick alone recorded as a world
 *             object (the re-base before node flags reached it) is carried
 *             behind the eye or off the screen
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
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
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
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, key);
}

static void frameHead(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, TW, TH, 0);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
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

static const RdInterpStats *build(float alpha, int first)
{
    static RdInterpStats st;
    rd__interp_frame(rd__prev_frame(), rd__last_frame(), alpha, first, &st);
    return &st;
}

static const RdFrame *built(float alpha)
{
    return rd__interp_frame(rd__prev_frame(), rd__last_frame(), alpha, 1, NULL);
}

/* ----------------------------------------------------- screen sprites */

/* frame A: keyed sprite KA at x 0..32, KB at 10..20 twice (the ordinal),
 * an unkeyed sprite; frame B: KA at 32..64, KB at 20..30 and 30..40, the
 * unkeyed sprite elsewhere, a new key KC */
static void recordSprites(int second)
{
    static const uint8_t c0[4] = {200, 40, 0, 0x80}, c1[4] = {100, 80, 40, 0x40};
    static const uint8_t grey[4] = {90, 90, 90, 0x80};
    rd_begin_frame();
    frameHead();
    const int dx = second ? 32 : 0;
    sprite(dx, 8, dx + 32, 40, second ? c1 : c0, RD_KEY(&kObjA, 0, 0));
    sprite(10 + (second ? 10 : 0), 50, 20 + (second ? 10 : 0), 60, grey, RD_KEY(&kObjB, 0, 0));
    sprite(10 + (second ? 20 : 0), 70, 20 + (second ? 20 : 0), 80, grey, RD_KEY(&kObjB, 0, 0));
    sprite(second ? 100 : 120, 90, second ? 110 : 130, 100, grey, 0);
    if (second) {
        sprite(200, 8, 240, 40, grey, RD_KEY(&kObjC, 0, 0));
    }
    rd_end_frame(0);
}

static void testSprites(void)
{
    recordSprites(0);
    recordSprites(1);
    const RdFrame *prev = rd__prev_frame(), *cur = rd__last_frame();
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
    const RdInterpStats *st = build(0.0f, 1);
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
        rd_begin_frame();
        frameHead();
        /* KA: 2 vertices, then 4 (two sprites in one call) */
        RdScreenVtx v[4];
        memset(v, 0, sizeof(v));
        for (int i = 0; i < 4; i++) {
            v[i].x = OX + (i * 10 + k) * 16;
            v[i].y = OY + (i * 10) * 16;
            memcpy(v[i].rgba, grey, 4);
        }
        rd_screen_prims(RD_PRIM_SPRITES, v, k ? 4 : 2, RD_SPACE_UI, 0, RD_KEY(&kObjA, 1, 0));
        /* KD: 300 px to the right in the second frame */
        sprite(k ? 300 : 0, 0, k ? 310 : 10, 10, grey, RD_KEY(&kObjD, 0, 0));
        rd_end_frame(0);
    }
    const RdInterpStats *st = build(0.5f, 1);
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
        rd_begin_frame();
        frameHead();
        RdCamera cam;
        cameraYaw(&cam, what == 4 && k ? 40.0f : (k ? 5.0f : 0.0f), 0.0f);
        rd_set_camera(&cam);
        sprite(k * 8, 0, k * 8 + 10, 10, grey, RD_KEY(&kObjA, 2, 0));
        if (what == 1 && k) {
            rd_camera_cut();
        }
        if (what == 2 && !k) {
            RdPostParams pp;
            memset(&pp, 0, sizeof(pp));
            pp.rgba[3] = 0x80;
            rd_select_list(11);
            rd_post(RD_POST_FADE, &pp);
        }
        if (what == 3 && k) {
            rd_end_frame(0);
            rd_begin_frame();
            rd_discard_frame(); /* a frame dropped between */
            rd_begin_frame();
            frameHead();
            sprite(k * 8, 0, k * 8 + 10, 10, grey, RD_KEY(&kObjA, 2, 0));
        }
        rd_end_frame(what == 5 && k ? 1 : 0);
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
        const RdInterpStats *st = build(0.5f, 1);
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
            CHECK(rd__last_frame()->camera.cut == 1, "rd_camera_cut sets RdCamera.cut");
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
    return rd_create_vu_mesh(&md);
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
    rd_begin_frame();
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
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjE, 0, 32));
    d.vu.mem[19][0] = 2048.0f + (teleport && k ? 1000.0f : 0.0f); /* 10 m in the tick */
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjE, 1, 32));

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
    rd_draw_vu_grid(&gd, RD_KEY(&kObjE, 2, 0));

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
    rd_select_list(6);
    rd_draw_vu_particles(&pd, 0);

    /* a shadow volume: 2 triangles, then 3 with shadowTris */
    RdScreenVtx sv[9];
    int8_t sign[3] = {1, -1, 1};
    memset(sv, 0, sizeof(sv));
    for (int i = 0; i < 9; i++) {
        sv[i].x = OX + (i * 4 + k * 16) * 16;
        sv[i].y = OY + i * 16;
        sv[i].z = 1000u + (uint32_t)(k * 100);
    }
    rd_select_list(3);
    rd_shadow_tris(sv, sign, shadowTris && k ? 3 : 2, RD_KEY(&kObjE, 3, 0));
    rd_end_frame(0);
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
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->keyed == 5 && st->lerped == 4 && st->jump == 1 &&
              st->mismatch == 0 && st->shifted == 1,
          "mesh, grid and particles blend, the teleported mesh jumps, the shadow whose count "
          "changed is moved (S2) (keyed %u lerped %u jump %u mismatch %u shifted %u)",
          st->keyed, st->lerped, st->jump, st->mismatch, st->shifted);
    const RdFrame *f = built(0.5f);
    const RdFrame *cur = rd__last_frame();
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
    /* the particles (list 6, the key rd_draw_vu_particles makes) */
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
        /* the medians move from x 12, z 1000 (2 triangles) to x 32, z 1100
         * (3), y 3 to 4: cur's 9 vertices half way back, 10 pixels, half a
         * pixel and 50 */
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
    st = build(0.5f, 1);
    f = built(0.5f);
    CHECK(st->lerped == 5 && st->jump == 0 && st->mismatch == 0, "all five blend (lerped %u)",
          st->lerped);
    sc = findKey(f, 3, RD_KEY(&kObjE, 3, 0), 0);
    if (sc) {
        const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + sc->u[1]);
        CHECK(v[0].x == OX + 8 * 16 && v[0].z == 1050u, "shadow: half way (%d, %u)",
              (v[0].x - OX) / 16, v[0].z);
    }
    rd_destroy_vu_mesh(mesh);
}

/* ------------------------------------------------------- shadow prisms */

/* Shadow.c's volume for one caster triangle: emitVolumeStrip's ten
 * positions over the top cap (whole pixels from x0, y0) and the cap moved
 * by d, each triangle signed by the GS rule (faceZ x the running sign < 0:
 * RGBAQ 0x04), written as rd_shadow_tris takes it */
static const int kTestStrip[10] = {0, 1, 3, 4, 5, 1, 2, 0, 5, 3};

static double faceZOf(const double (*p)[2], int a, int b, int c)
{
    return (p[a][0] - p[b][0]) * (p[c][1] - p[b][1]) - (p[c][0] - p[b][0]) * (p[a][1] - p[b][1]);
}

static int emitPrism(const int (*top)[2], const int *d, float sgn, uint32_t z, RdScreenVtx *v,
                     int8_t *sign)
{
    double p[6][2];
    for (int k = 0; k < 3; k++) {
        p[k][0] = top[k][0];
        p[k][1] = top[k][1];
        p[k + 3][0] = top[k][0] + d[0];
        p[k + 3][1] = top[k][1] + d[1];
    }
    double fz[10];
    int plus = 0;
    for (int i = 0; i < 10; i++, sgn = -sgn) {
        static const int abc[10][3] = {{0},       {0}, {0, 1, 3}, {0},       {3, 4, 5},
                                       {4, 5, 1}, {0}, {1, 2, 0}, {2, 0, 5}, {0}};
        fz[i] = i < 2 ? 1.0
                      : (i == 3 || i == 6 || i == 9 ? -fz[i - 1]
                                                    : faceZOf(p, abc[i][0], abc[i][1], abc[i][2]));
        if (i < 2) {
            continue;
        }
        sign[i - 2] = fz[i] * sgn < 0.0 ? 1 : -1;
        plus += sign[i - 2] > 0;
        for (int k = 0; k < 3; k++) {
            RdScreenVtx *o = &v[(i - 2) * 3 + k];
            const int q = kTestStrip[i - 2 + k];
            memset(o, 0, sizeof(*o));
            o->x = OX + (int32_t)p[q][0] * 16;
            o->y = OY + (int32_t)p[q][1] * 16;
            o->z = z + (q >= 3 ? 40u : 0u);
            o->q = 1.0f;
        }
    }
    return plus;
}

/* a volume of the prisms listed (top-left corners x, extrusions d; the
 * strip sign is the caster triangle's, alternating with x / 50 as along a
 * caster strip); returns the increments */
static int recordPrisms(const int *xs, const int (*d)[2], int n, int dy, RdKey key)
{
    RdScreenVtx v[6 * 24];
    int8_t sign[6 * 8];
    int inc = 0;
    for (int i = 0; i < n; i++) {
        const int top[3][2] = {{xs[i], 20 + dy}, {xs[i] + 30, 20 + dy}, {xs[i], 50 + dy}};
        const float sgn = (xs[i] / 50) & 1 ? -1.0f : 1.0f;
        inc += emitPrism(top, d[i], sgn, 1000u, &v[i * 24], &sign[i * 8]);
    }
    rd_select_list(3);
    rd_shadow_tris(v, sign, (uint32_t)n * 8, key);
    return inc;
}

/* checks the volume of key k in f: every prism (eight triangles by tag)
 * closed, its signed areas summing to 0, and returns how many there are */
static int closedPrisms(const RdFrame *f, RdKey k, int *closed)
{
    const RdCmd *c = findKey(f, 3, k, 0);
    *closed = 0;
    if (!c || c->b[0] != RD_SHADOW_TRIS) {
        return -1;
    }
    const uint32_t n = (c->u[0] + c->u[3]) / 3;
    const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + c->u[1]);
    int64_t sum[8] = {0};
    for (uint32_t t = 0; t < n; t++) {
        const RdScreenVtx *a = &v[t * 3];
        const uint32_t tag = rd__shadow_tag(a);
        if (tag == 0 || tag > n || (tag - 1) / 8 >= 8) {
            return -1;
        }
        int64_t w = ((int64_t)a[1].x - a[0].x) * ((int64_t)a[2].y - a[0].y) -
                    ((int64_t)a[1].y - a[0].y) * ((int64_t)a[2].x - a[0].x);
        w = w < 0 ? -w : w;
        sum[(tag - 1) / 8] += t * 3 < c->u[0] ? w : -w;
    }
    for (uint32_t p = 0; p < n / 8; p++) {
        *closed += sum[p] == 0;
    }
    return (int)(n / 8);
}

static void prismFrame(const int *xs, const int (*d)[2], int n, int dy, int *inc)
{
    rd_begin_frame();
    frameHead();
    *inc = recordPrisms(xs, d, n, dy, RD_KEY(&kObjE, 9, 0));
    rd_end_frame(0);
}

static void testPrisms(void)
{
    const RdKey key = RD_KEY(&kObjE, 9, 0);
    /* (1) the same two prisms, moved 8 pixels down, with extrusions turned
     * so that side faces change sides in both: the counts stay 8 and 8 but
     * the triangles sorted into increments no longer pair up */
    static const int xs[2] = {10, 60};
    static const int dPrev[2][2] = {{-30, -30}, {-30, -30}}, dCur[2][2] = {{-30, 10}, {-30, 10}};
    int incP, incC;
    prismFrame(xs, dPrev, 2, 0, &incP);
    prismFrame(xs, dCur, 2, 8, &incC);
    const RdFrame *pv = rd__prev_frame(), *cu = rd__last_frame();
    const RdCmd *pc = findKey(pv, 3, key, 0), *cc = findKey(cu, 3, key, 0);
    int differ = 0;
    if (pc && cc) {
        const RdScreenVtx *a = (const RdScreenVtx *)(const void *)(pv->payload + pc->u[1]);
        const RdScreenVtx *b = (const RdScreenVtx *)(const void *)(cu->payload + cc->u[1]);
        for (uint32_t i = 0; i < pc->u[0] && i < cc->u[0]; i += 3) {
            differ |= rd__shadow_tag(&a[i]) != rd__shadow_tag(&b[i]);
        }
    }
    CHECK(incP == incC && differ,
          "precondition: equal counts (%d, %d), the increments of different triangles", incP, incC);
    const RdInterpStats *st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    int closed = 0;
    int np = closedPrisms(f, key, &closed);
    CHECK(st->lerped >= 1 && np == 2 && closed == 2,
          "equal counts: 2 prisms, %d of %d closed (the faces' signed areas net to 0)", closed, np);
    const RdCmd *hc = findKey(f, 3, key, 0);
    if (hc) {
        /* every vertex half way: the top caps at y 20, 50 then 28, 58 (24,
         * 54), the bottom caps at -10, 20 then 38, 68 (14, 44) */
        const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + hc->u[1]);
        int ok = 1;
        for (uint32_t i = 0; i < hc->u[0] + hc->u[3]; i++) {
            const int y = (v[i].y - OY) / 16;
            ok &= (v[i].y - OY) % 16 == 0 && (y == 24 || y == 54 || y == 14 || y == 44);
        }
        CHECK(ok, "the prisms' vertices half way");
    }
    /* (2) a prism comes in cur (a triangle turned to the light): the pairs
     * blend, the new one is drawn from t = 0.5 (moved back by half the
     * volume's shift), not before */
    static const int xs3[3] = {10, 60, 110};
    static const int dSame[3][2] = {{10, 30}, {10, 30}, {10, 30}};
    static const int xs2[2] = {10, 110};
    prismFrame(xs2, dSame, 2, 0, &incP);
    prismFrame(xs3, dSame, 3, 8, &incC);
    build(0.25f, 1);
    f = built(0.25f);
    np = closedPrisms(f, key, &closed);
    CHECK(np == 2 && closed == 2, "a prism of cur only: at 0.25, %d prisms (2), %d closed", np,
          closed);
    f = built(0.75f);
    np = closedPrisms(f, key, &closed);
    CHECK(np == 3 && closed == 3, "a prism of cur only: at 0.75, %d prisms (3), %d closed", np,
          closed);
    /* (3) and one that goes: drawn until t = 0.5 */
    prismFrame(xs3, dSame, 3, 0, &incP);
    prismFrame(xs2, dSame, 2, 8, &incC);
    f = built(0.25f);
    np = closedPrisms(f, key, &closed);
    CHECK(np == 3 && closed == 3, "a prism of prev only: at 0.25, %d prisms (3), %d closed", np,
          closed);
    hc = findKey(f, 3, key, 0);
    if (hc) {
        /* the pairs at 0.25 of the 8 pixels (2), the lone prism moved with
         * the volume's shift as well */
        const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + hc->u[1]);
        int ok = 1;
        for (uint32_t i = 0; i < hc->u[0] + hc->u[3]; i++) {
            ok &= (v[i].y - OY) % 16 == 0 && ((v[i].y - OY) / 16 - 2) % 10 == 0;
        }
        CHECK(ok, "every vertex a quarter of the way");
    }
    f = built(0.75f);
    np = closedPrisms(f, key, &closed);
    CHECK(np == 2 && closed == 2, "a prism of prev only: at 0.75, %d prisms (2), %d closed", np,
          closed);
    /* alpha 1 is cur's volume byte for byte */
    f = built(1.0f);
    hc = findKey(f, 3, key, 0);
    cc = findKey(rd__last_frame(), 3, key, 0);
    CHECK(hc && cc && hc->u[0] == cc->u[0] && hc->u[3] == cc->u[3] &&
              memcmp(f->payload + hc->u[1], rd__last_frame()->payload + cc->u[1],
                     (cc->u[0] + cc->u[3]) * sizeof(RdScreenVtx)) == 0,
          "alpha 1: the current volume");
}

/* -------------------------------------------------- fade and feedback */

static void testFade(void)
{
    for (int k = 0; k < 2; k++) {
        rd_begin_frame();
        frameHead();
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.rgba[3] = k ? 0x20 : 0x40;
        rd_select_list(11);
        rd_post(RD_POST_FADE, &pp);
        rd_end_frame(0);
    }
    const RdInterpStats *st = build(0.5f, 1);
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

/* the frame of testFeedback: with display, a motion blur sprite reading
 * DISPLAY's RGB24 view (staticBlur.c MotionBlur's TEX0); with feed, an aura
 * sprite into FEED128; always one into AURA_WORK */
static void recordFeedback(int display, int feed)
{
    rd_begin_frame();
    frameHead();
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x20;
    pp.blend = RD_BLEND_LERP_FIX;
    pp.scalar[2] = 1.0f;
    if (display) {
        rd_select_list(7);
        rd_blend_func(RD_BLEND_LERP_FIX, 0x20);
        rd_texture(rd_target_texture(rd_target(RD_TARGET_DISPLAY), RD_VIEW_RGB24_TA0),
                   RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_post(RD_POST_MOTION_BLUR, &pp);
        rd_texture_off();
    }
    rd_select_list(8);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), (RdTarget){0}, 512, 512, 0);
    rd_post(RD_POST_AURA, &pp);
    if (feed) {
        rd_set_target(rd_target(RD_TARGET_FEED128), (RdTarget){0}, 128, 128, 0);
        rd_post(RD_POST_AURA, &pp);
    }
    rd_end_frame(0);
}

/* issue 28: the copies at the head of a present's first list:
 * DISPLAY <-> DISPLAY_HELD and FEED128 <-> FEED_HELD found (1 each at
 * most), and the number of RDC_COPYs in the frame */
static int headCopies(const RdFrame *f, int first, int *display, int *feed)
{
    const uint32_t disp = rd_target(RD_TARGET_DISPLAY).id;
    const uint32_t dispHeld = rd_target(RD_TARGET_DISPLAY_HELD).id;
    const uint32_t fd = rd_target(RD_TARGET_FEED128).id;
    const uint32_t fdHeld = rd_target(RD_TARGET_FEED_HELD).id;
    *display = *feed = 0;
    for (uint32_t i = 0; i < f->lists[0].count && i < 2; i++) {
        const RdCmd *c = &f->lists[0].cmds[i];
        if (c->type != RDC_COPY) {
            break;
        }
        RdCopyRec r;
        memcpy(&r, f->payload + c->u[2], sizeof(r));
        const int whole0 = r.srcX == 0 && r.srcY == 0 && r.dstX == 0 && r.dstY == 0;
        if (c->u[0] == (first ? disp : dispHeld) && c->u[1] == (first ? dispHeld : disp)) {
            *display += whole0 && r.w == 512 && r.h == 256;
        } else if (c->u[0] == (first ? fd : fdHeld) && c->u[1] == (first ? fdHeld : fd)) {
            *feed += whole0 && r.w == 128 && r.h == 128;
        }
    }
    int copies = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            copies += f->lists[l].cmds[i].type == RDC_COPY;
        }
    }
    return copies;
}

static void testFeedback(void)
{
    /* every present of a tick starts from the feedback inputs the tick
     * started from: a frame that reads DISPLAY (the motion blur's old
     * frame) copies all of DISPLAY into DISPLAY_HELD at the head of a
     * tick's first present and back at the later ones', one that writes
     * FEED128 the same with FEED_HELD; the motion blur sprite is the
     * recorded one in every present (the tick's FIX, no time factor) */
    static const struct {
        int display, feed;
    } kCases[] = {{1, 1}, {1, 0}, {0, 1}, {0, 0}};

    for (size_t k = 0; k < sizeof(kCases) / sizeof(kCases[0]); k++) {
        const int display = kCases[k].display, feed = kCases[k].feed;
        recordFeedback(display, feed);
        recordFeedback(display, feed);
        const RdFrame *cur = rd__last_frame();
        for (int first = 1; first >= 0; first--) {
            const RdFrame *f = rd__interp_frame(rd__prev_frame(), cur, 0.5f, first, NULL);
            if (!f) {
                CHECK(0, "feedback case %zu: the frame built", k);
                continue;
            }
            int blur = 0, same = 0, aura = 0;
            for (int l = 7; l <= 8; l++) {
                for (uint32_t i = 0; i < f->lists[l].count; i++) {
                    const RdCmd *c = &f->lists[l].cmds[i];
                    if (c->type != RDC_POST_STUB) {
                        continue;
                    }
                    if (c->b[0] == RD_POST_MOTION_BLUR) {
                        blur++;
                        same += memcmp(f->payload + c->u[1], cur->payload + c->u[1],
                                       sizeof(RdPostRec)) == 0;
                    }
                    aura += c->b[0] == RD_POST_AURA;
                }
            }
            CHECK(blur == display && same == display,
                  "case %zu, %s present: the motion blur sprite as recorded (%d of %d)", k,
                  first ? "a tick's first" : "a later", same, blur);
            CHECK(aura == 1 + feed, "case %zu, %s present: %d aura sprites (FEED128's kept)", k,
                  first ? "a tick's first" : "a later", aura);
            int d = 0, fd = 0;
            const int copies = headCopies(f, first, &d, &fd);
            CHECK(d == display && fd == feed && copies == display + feed,
                  "case %zu (DISPLAY read %d, FEED128 written %d), %s present: the head copies %s "
                  "%d, %s %d (%d copies)",
                  k, display, feed, first ? "a tick's first" : "a later",
                  first ? "DISPLAY into DISPLAY_HELD" : "DISPLAY_HELD back into DISPLAY", d,
                  first ? "FEED128 into FEED_HELD" : "FEED_HELD back into FEED128", fd, copies);
        }
    }
}

/* ---------------------------------------------------------- on a device */

static uint8_t *readWork0(void)
{
    static uint8_t buf[TW * TH * 4];
    uint32_t w, h;
    if (!rd__read_target(rd_target(RD_TARGET_WORK0), buf, sizeof(buf), &w, &h) || w != TW ||
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
    const RdFrame *prev = rd__prev_frame(), *cur = rd__last_frame();
    const uint8_t *p;
    rd__replay_frame(prev, 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[0], p, sizeof(img[0]));
    }
    rd__replay_frame(cur, 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[1], p, sizeof(img[1]));
    }
    /* KC (only in the current frame) is drawn at alpha 0 too: compare the
     * rows above it only where it is not: it sits at x 200..240 */
    rd__replay_frame(built(0.0f), 0, false);
    if ((p = readWork0()) != NULL) {
        memcpy(img[2], p, sizeof(img[2]));
    }
    rd__replay_frame(built(1.0f), 0, false);
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
    rd__replay_frame(built(0.5f), 0, false);
    if ((p = readWork0()) != NULL) {
        const int y = 20;
        const uint8_t *l = p + ((size_t)y * TW + 15) * 4, *a = p + ((size_t)y * TW + 16) * 4;
        const uint8_t *b = p + ((size_t)y * TW + 47) * 4, *r = p + ((size_t)y * TW + 48) * 4;
        CHECK(l[0] == 0 && a[0] == 150 && b[0] == 150 && r[0] == 0,
              "alpha 0.5: columns 16..47 drawn (15: %u, 16: %u, 47: %u, 48: %u)", l[0], a[0], b[0],
              r[0]);
    }
}

/* A mirage tick presented twice (alpha 0.5 as the tick's
 * first present, then alpha 1 as a later one).  The frame pastes
 * FEED128 over SCENE at half brightness through FEED128's alpha and copies
 * SCENE into FEED128 for the next tick (staticBlur.c's mode 2).  Both
 * presents must draw byte-identical SCENEs and leave the same FEED128, so
 * FEED128 advances once per tick, not once per present; and the next tick
 * starts from it. */
static void mirageSprite(RdTarget dst, uint32_t dw, uint32_t dh, RdTarget src, uint32_t sw,
                         uint32_t sh, const uint8_t c[4], int abe)
{
    rd_set_target(dst, (RdTarget){0}, dw, dh, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_pabe(abe ? 0 : 1);
    rd_blend_func(abe ? RD_BLEND_LERP_AS_ALT : RD_BLEND_LERP_FIX, 0);
    rd_abe(abe);
    rd_fba(0);
    rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    rd_sampler_wrap(RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(rd_target_texture(src, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - (int)dw * 8);
    p.rect[1] = (float)(0x8000 - (int)dh * 8);
    p.rect[2] = (float)(0x8000 + (int)dw * 8);
    p.rect[3] = (float)(0x8000 + (int)dh * 8);
    p.uv[0] = p.uv[1] = 8.0f;
    p.uv[2] = (float)(sw * 16);
    p.uv[3] = (float)(sh * 16);
    p.scalar[0] = (float)sw;
    p.scalar[1] = (float)sh;
    p.scalar[2] = 1.0f;
    p.exactInt = 1;
    memcpy(p.rgba, c, 4);
    rd_post(RD_POST_AURA, &p);
}

static void mirageTick(int n)
{
    static const uint8_t half[4] = {64, 64, 64, 128}, white[4] = {128, 128, 128, 128};
    const uint8_t bg[4] = {(uint8_t)(40 + n * 30), 120, 200, 0x80};
    const uint8_t fg[4] = {230, 50, 20, 0x60};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), bg, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    {
        RdScreenVtx v[2];
        memset(v, 0, sizeof(v));
        v[0].x = (2048 - 256 + 100) * 16;
        v[0].y = (2048 - 256 + 150) * 16;
        v[1].x = (2048 - 256 + 300) * 16;
        v[1].y = (2048 - 256 + 260) * 16;
        v[0].q = v[1].q = 1.0f;
        memcpy(v[0].rgba, fg, 4);
        memcpy(v[1].rgba, fg, 4);
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 0, 0);
    }
    rd_select_list(8);
    mirageSprite(rd_target(RD_TARGET_SCENE), 512, 512, rd_target(RD_TARGET_FEED128), 128, 128, half,
                 1);
    mirageSprite(rd_target(RD_TARGET_FEED128), 128, 128, rd_target(RD_TARGET_SCENE), 512, 512,
                 white, 0);
    rd_end_frame(0);
}

static int readTo(RdTargetId id, uint8_t *dst, size_t size)
{
    uint32_t w = 0, h = 0;
    return rd__read_target(rd_target(id), dst, size, &w, &h);
}

static void testMiragePresents(void)
{
    static uint8_t scene[2][512 * 512 * 4], feed[2][128 * 128 * 4], feedTick[128 * 128 * 4];
    const uint8_t interpolate = g_rd.settings.interpolate;
    g_rd.settings.interpolate = 1; /* the replays are the presents' */
    int sceneDiff = 0, feedDiff = 0, advanced = 0;
    for (int n = 0; n < 4; n++) {
        mirageTick(n);
        for (int k = 0; k < 2; k++) {
            const RdFrame *f =
                rd__interp_frame(rd__prev_frame(), rd__last_frame(), k ? 1.0f : 0.5f, k == 0, NULL);
            CHECK(f && rd__replay_frame(f, 0, false), "mirage tick %d present %d: replay", n, k);
            rhi_wait_idle();
            CHECK(readTo(RD_TARGET_SCENE, scene[k], sizeof(scene[k])) &&
                      readTo(RD_TARGET_FEED128, feed[k], sizeof(feed[k])),
                  "mirage tick %d present %d: readback", n, k);
        }
        if (n > 0) {
            advanced += memcmp(feed[0], feedTick, sizeof(feedTick)) != 0;
        }
        memcpy(feedTick, feed[1], sizeof(feedTick));
        int sd = 0, fd = 0;
        for (size_t i = 0; i < sizeof(scene[0]); i++) {
            sd += scene[0][i] != scene[1][i];
        }
        for (size_t i = 0; i < sizeof(feed[0]); i++) {
            fd += feed[0][i] != feed[1][i];
        }
        sceneDiff += sd;
        feedDiff += fd;
        CHECK(sd == 0, "mirage tick %d: the two presents' SCENEs differ in %d bytes", n, sd);
        CHECK(fd == 0, "mirage tick %d: the two presents leave FEED128s differing in %d bytes", n,
              fd);
    }
    g_rd.settings.interpolate = interpolate;
    CHECK(advanced == 3, "mirage: FEED128 advanced on %d of 3 tick changes", advanced);
    printf("  mirage presents (two per tick, 4 ticks): SCENE %d, FEED128 %d bytes differ "
           "between a tick's presents; FEED128 advanced on %d of 3 ticks\n",
           sceneDiff, feedDiff, advanced);
}

static void testPresent(void)
{
    RdSettings s = *rd_get_settings();
    CHECK(!rd_interpolation_active() && !rd_present(0.5f), "Original: rd_present does nothing");
    /* the Original preset interpolates too when asked */
    s.interpolate = 1;
    rd_set_settings(&s);
    recordSprites(0);
    CHECK(rd_interpolation_active(), "Original with interpolate");
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f; /* the GS size: no target is recreated */
    s.interpolate = 0;
    rd_set_settings(&s);
    recordSprites(0);
    CHECK(!rd_interpolation_active() && !rd_present(0.5f), "Enhanced, framerate original: none");
    s.interpolate = 1;
    rd_set_settings(&s);
    recordSprites(0);
    recordSprites(1);
    CHECK(rd_interpolation_active(), "Enhanced with interpolate");
    CHECK(rd_present(0.25f) && rd_present(0.75f), "rd_present presents");
    /* a change of scale recreates the targets: the pair across it snaps,
     * the next blends */
    s.sceneScale = 2.0f;
    rd_set_settings(&s);
    recordSprites(0);
    CHECK(build(0.5f, 1)->snap == RD_SNAP_HISTORY, "history dropped after a recreate");
    recordSprites(1);
    CHECK(build(0.5f, 1)->snap == RD_SNAP_NONE, "the next pair blends again");
    CHECK(rd_present(0.5f), "rd_present at 2x");
    s.preset = RD_PRESET_ORIGINAL;
    s.sceneScale = 0.0f;
    rd_set_settings(&s);
    recordSprites(0);
}

/* ------------------------------------------------------------ keyed text */

static RdKey textKeyOf(const char *s)
{
    uint64_t h = 0xCBF29CE484222325ull;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 0x100000001B3ull;
    }
    return h ? h : 1;
}

/* n glyph sprites of 6 x 10 px from x, one rd_screen_prims call as font.c
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
    rd_screen_prims(RD_PRIM_SPRITES, v, (uint32_t)(2 * n), RD_SPACE_UI, 1, key);
}

/* "NEW GAME" fading out while it slides 16 px; "OPTIONS" twice (the halo
 * copies' ordinal); "LOAD" only in the first frame, "CONTINUE" only in the
 * second (a changed label) */
static void recordText(int second)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(11);
    glyphs(10 + (second ? 16 : 0), 20, 8, second ? 0x00 : 0x80, textKeyOf("NEW GAME"));
    glyphs(10, 40, 7, 0x40, textKeyOf("OPTIONS"));
    glyphs(12 + (second ? 4 : 0), 40, 7, 0x40, textKeyOf("OPTIONS"));
    if (second) {
        glyphs(10, 60, 8, 0x80, textKeyOf("CONTINUE"));
    } else {
        glyphs(10, 60, 4, 0x80, textKeyOf("LOAD"));
    }
    rd_end_frame(0);
}

static void testText(void)
{
    recordText(0);
    recordText(1);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->keyed == 4 && st->lerped == 3 && st->missing == 1,
          "text: three strings blend, the changed label is unmatched (keyed %u lerped %u "
          "missing %u)",
          st->keyed, st->lerped, st->missing);
    const RdFrame *f = built(0.5f), *cur = rd__last_frame();
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

/* --------------------------------------------------------- deferred text */

static void deferredItem(const char *str, float x, float y, uint8_t alpha, RdKey key)
{
    RdTextItem it;
    memset(&it, 0, sizeof(it));
    snprintf(it.utf8, sizeof(it.utf8), "%s", str);
    it.x = x;
    it.y = y;
    it.size = 27.0f;
    it.flags = 1 | 4; /* centred, capitals' middle (font.h) */
    it.rgba[0] = it.rgba[1] = it.rgba[2] = 0x80;
    it.rgba[3] = alpha;
    it.hasXf = 1;
    it.xf[2] = it.xf[3] = 1.0f + (alpha == 0x80 ? 0.0f : 0.5f); /* the glow's stretch */
    rd_deferred_text(&it, key);
}

/* "New Game" sliding 20 grid units right and fading out with its glow
 * stretching; "Options" jumping 400 units (snaps); "Load" with a different
 * size in the second frame (mismatch); a fade op after them in both */
static void recordDeferred(int second)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(11);
    deferredItem("New Game", 300.0f + (second ? 20.0f : 0.0f), 200.0f, second ? 0x00 : 0x80,
                 RD_KEY(&kObjD, 1, 0));
    deferredItem("Options", second ? 500.0f : 100.0f, 240.0f, 0x80, RD_KEY(&kObjD, 2, 0));
    RdTextItem it;
    memset(&it, 0, sizeof(it));
    snprintf(it.utf8, sizeof(it.utf8), "Load");
    it.size = second ? 20.0f : 27.0f;
    it.rgba[3] = 0x80;
    rd_deferred_text(&it, RD_KEY(&kObjD, 3, 0));
    /* a row of one tick only each */
    deferredItem(second ? "Extra" : "Gone", 320.0f, 280.0f, 0x80,
                 RD_KEY(&kObjD, second ? 4 : 5, 0));
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = second ? 0x40 : 0x00;
    rd_post(RD_POST_FADE, &pp);
    rd_end_frame(0);
}

static const RdTextItem *itemOf(const RdFrame *f, RdKey k)
{
    const RdCmd *c = findKey(f, 11, k, 0);
    return c && c->type == RDC_OVERLAY_TEXT
               ? (const RdTextItem *)(const void *)(f->payload + c->u[1])
               : NULL;
}

static void testDeferredText(void)
{
    recordDeferred(0);
    recordDeferred(1);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE && st->lerped >= 2 && st->jump >= 1 && st->mismatch >= 1,
          "deferred: items blend, a jump snaps, a changed size mismatches (lerped %u jump %u "
          "mismatch %u)",
          st->lerped, st->jump, st->mismatch);
    const RdFrame *f = built(0.5f), *cur = rd__last_frame(), *prev = rd__prev_frame();
    const RdTextItem *a = itemOf(f, RD_KEY(&kObjD, 1, 0));
    CHECK(a && a->x == 310.0f && a->y == 200.0f && a->rgba[3] == 0x40 && a->xf[2] == 1.25f &&
              strcmp(a->utf8, "New Game") == 0,
          "deferred: the anchor half way (x %.2f, 310), alpha 0x80 -> 0 at %u (0x40), the "
          "stretch at %.3f (1.25)",
          a ? a->x : -1.0f, a ? a->rgba[3] : 0, a ? a->xf[2] : 0.0f);
    const RdTextItem *o = itemOf(f, RD_KEY(&kObjD, 2, 0));
    CHECK(o && o->x == 500.0f, "deferred: a 400-unit jump is the current item (x %.1f)",
          o ? o->x : -1.0f);
    const RdTextItem *l = itemOf(f, RD_KEY(&kObjD, 3, 0));
    CHECK(l && l->size == 20.0f, "deferred: another size is the current item (%.1f)",
          l ? l->size : -1.0f);
    /* an item of one tick only fades with t, as its quads */
    const RdTextItem *ex = itemOf(f, RD_KEY(&kObjD, 4, 0)), *gone = itemOf(f, RD_KEY(&kObjD, 5, 0));
    CHECK(ex && ex->rgba[3] == 0x40 && gone && gone->rgba[3] == 0x40,
          "deferred: cur's new row at alpha %d, prev's gone row inserted at %d (0x40, 0x40)",
          ex ? ex->rgba[3] : -1, gone ? gone->rgba[3] : -1);
    /* the fade op after the items blends as the fade sprite does */
    const RdCmd *oc = NULL;
    for (uint32_t i = 0; i < f->lists[11].count; i++) {
        const RdCmd *c = &f->lists[11].cmds[i];
        if (c->type == RDC_OVERLAY_TEXT && c->b[0] == RD_OTEXT_OP) {
            oc = c;
        }
    }
    const RdTextOp *op = oc ? (const RdTextOp *)(const void *)(f->payload + oc->u[1]) : NULL;
    CHECK(op && oc->b[1] == RD_POST_FADE && op->rgba[3] == 0x20,
          "deferred: the fade op at 0x20 half way (%u)", op ? op->rgba[3] : 0);
    const RdFrame *f0 = built(0.0f);
    const RdTextItem *a0 = itemOf(f0, RD_KEY(&kObjD, 1, 0)),
                     *ap = itemOf(prev, RD_KEY(&kObjD, 1, 0));
    CHECK(a0 && ap && memcmp(a0, ap, sizeof(*a0)) == 0, "deferred: alpha 0 is the previous item");
    const RdFrame *f1 = built(1.0f);
    const RdTextItem *a1 = itemOf(f1, RD_KEY(&kObjD, 1, 0)),
                     *ac = itemOf(cur, RD_KEY(&kObjD, 1, 0));
    CHECK(a1 && ac && memcmp(a1, ac, sizeof(*a1)) == 0, "deferred: alpha 1 is the current item");
}

/* ------------------------------------------------------- morphing meshes */

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
    return rd_create_vu_mesh(&md);
}

static void morphUpdate(RdMesh m, float x)
{
    static float qw[10][4];
    morphStream(qw, x);
    CHECK(rd_update_vu_mesh(m, (const float (*)[4])qw), "the morph rewrote the mesh");
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
    rd_select_list(0);
    rd_draw_vu_mesh(m, &d, key);
}

/* vertex 1's x in the stream the n-th draw of key in f replays */
static float morphX(const RdFrame *f, RdKey key, int nth)
{
    const RdCmd *c = findKey(f, 0, key, nth);
    const RdMeshRec *m = c ? rd__mesh_rec(c->u[0]) : NULL;
    return m ? m->stream[RD_VU_QW_PRELIT][0] : -1.0f;
}

static void testMorph(void)
{
    RdSettings s = *rd_get_settings(), keep = s;
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.interpolate = 1;
    rd_set_settings(&s);
    const RdKey kTwin = RD_KEY(&kObjD, 5, 0), kOne = RD_KEY(&kObjD, 6, 0);
    /* the twins: A in odd frames, B in even ones, rewritten before the
     * draw (reg_setShape); "one": a single mesh rewritten every frame */
    RdMesh a = morphMesh(0.0f), b = morphMesh(0.0f), one = morphMesh(0.0f);
    CHECK(a.id && b.id && one.id, "the morph meshes");
    rd_begin_frame();
    frameHead();
    morphUpdate(a, 100.0f);
    morphDrawAt(a, kTwin);
    morphUpdate(one, 100.0f);
    morphDrawAt(one, kOne);
    rd_end_frame(0);
    rd_begin_frame();
    frameHead();
    morphUpdate(b, 120.0f);
    morphDrawAt(b, kTwin);
    morphUpdate(one, 120.0f);
    morphDrawAt(one, kOne);
    rd_end_frame(0);
    /* the next tick records: A and "one" take the third shape */
    rd_begin_frame();
    frameHead();
    morphUpdate(a, 200.0f);
    morphDrawAt(a, kTwin);
    morphUpdate(one, 200.0f);
    morphDrawAt(one, kOne);
    const RdInterpStats *st = build(0.5f, 1);
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
        CHECK(rd__replay_frame(built(0.5f), 0, false), "morph: the half-way frame replays");
    }
    rd_end_frame(0);
    rd_set_settings(&keep);
    rd_begin_frame();
    rd_end_frame(0);
    rd_destroy_vu_mesh(a);
    rd_destroy_vu_mesh(b);
    rd_destroy_vu_mesh(one);
}

/* ------------------------------------------------ rotation-aware blend */

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
    CHECK(rd__blend_affine(p, c, 0.5, NULL, o), "a 90 degree turn blends");
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
    CHECK(rd__blend_affine(p, c, 0.25, NULL, o), "turn with scale blends");
    const double ang2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
    CHECK(fabs(ang2 - (-10.0)) < 1e-9 && fabs(colLen(o, 0) - 2.25) < 1e-12,
          "scale lerped, rotation slerped (%.6f degrees, scale %.6f)", ang2, colLen(o, 0));
    turnZ(p, 170.0, 1.0, 0.0);
    turnZ(c, -170.0, 1.0, 0.0);
    CHECK(rd__blend_affine(p, c, 0.5, NULL, o) &&
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
        CHECK(rd__blend_affine(p, c, 0.5, x0, o), "about a pivot");
        const double jx = o[0] * 100.0 + o[12], jy = o[1] * 100.0 + o[13];
        const double a2 = atan2(o[1], o[0]) * 180.0 / 3.14159265358979323846;
        CHECK(fabs(jx - 100.0) < 1e-9 && fabs(jy) < 1e-9 && fabs(a2 - 45.0) < 1e-9,
              "the joint stays at (100, 0) half way (%.6f, %.6f), turned %.6f degrees", jx, jy, a2);
    }
    turnZ(c, 10.0, 1.0, 0.0);
    c[3] = 0.25; /* projective */
    CHECK(!rd__blend_affine(p, c, 0.5, NULL, o),
          "a projective matrix is left to the element-wise blend");
    turnZ(p, 0.0, 1.0, 0.0);
    turnZ(c, 10.0, 1.0, 0.0);
    c[10] = -1.0; /* a mirror */
    CHECK(!rd__blend_affine(p, c, 0.5, NULL, o),
          "opposite handedness is left to the element-wise blend");
}

/* a prelit mesh whose model to world turns deg about z, seen through a
 * world to screen S (a scale and a translation, w = 1); and a skinned draw
 * with one bone turning the same way */
static void recordTurn(RdMesh mesh, RdMesh skin, double deg)
{
    rd_begin_frame();
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
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjD, 0, 32));
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
    rd_draw_vu_mesh(skin, &d, RD_KEY(&kObjD, 1, 20));
    rd_end_frame(0);
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
    return rd_create_vu_mesh(&md);
}

static void testRotationDraws(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    recordTurn(mesh, skin, 0.0);
    recordTurn(mesh, skin, 90.0);
    const RdInterpStats *st = build(0.5f, 1);
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
    build(0.5f, 1);
    f = built(0.5f);
    const RdFrame *cur = rd__last_frame();
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

/* A shadow coming out of its pool: the root climbs up to 13
 * units a tick in EN1 START (shadow_spawn_test's rise) while the hips
 * turn.  A skinned draw whose bone 0 rises 40 units (Y down: 100 to 60) and
 * turns 30 degrees about x in the tick, its three vertices at (0, -100, 0),
 * (10, -100, 0) and (0, -90, 0), so the bone's pivot (their centroid) is
 * 97 units from its origin. */
static RdMesh makeRisingSkinMesh(void)
{
    static float qw[1 + 3 * 5][4];
    static const float pos[3][3] = {
        {0.0f, -100.0f, 0.0f}, {10.0f, -100.0f, 0.0f}, {0.0f, -90.0f, 0.0f}};
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u;
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        qw[1 + k * 5][0] = pos[k][0];
        qw[1 + k * 5][1] = pos[k][1];
        qw[1 + k * 5][2] = pos[k][2];
        qw[1 + k * 5][3] = 1.0f;
        qw[1 + k * 5 + 3][3] = k == 0 ? 0.0f : 1.0f; /* ST.w: the strip flag */
        const uint32_t addr = 16u;                   /* bone 0, weight 1 */
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
    return rd_create_vu_mesh(&md);
}

/* bone 0 of the rising draw: turned deg about x, its origin at (5, y, -20) */
static void risingBone(float (*b)[4], double deg, double y)
{
    const double a = deg * 3.14159265358979323846 / 180.0;
    memset(b, 0, 16 * sizeof(float));
    b[0][0] = 1.0f;
    b[1][1] = (float)cos(a);
    b[1][2] = (float)sin(a);
    b[2][1] = (float)-sin(a);
    b[2][2] = (float)cos(a);
    b[3][0] = 5.0f;
    b[3][1] = (float)y;
    b[3][2] = -20.0f;
    b[3][3] = 1.0f;
}

static void recordRise(RdMesh skin, double deg, double y)
{
    rd_begin_frame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 12);
    identity(d.vu.mem, 16);
    identity(d.vu.mem, 20);
    identity(d.vu.mem, 24);
    static float bone[4][4];
    risingBone(bone, deg, y);
    d.prog = RD_PROG_SKIN;
    d.code = 24;
    d.bones = (const float (*)[4])bone;
    d.boneQw = 4;
    rd_select_list(0);
    rd_draw_vu_mesh(skin, &d, RD_KEY(&kObjE, 2, 24));
    rd_end_frame(0);
}

/* p's image through the bone b (column-major rows: b[3] the origin) */
static void boneApply(const float (*b)[4], const double p[3], double o[3])
{
    for (int r = 0; r < 3; r++) {
        o[r] = b[0][r] * p[0] + b[1][r] * p[1] + b[2][r] * p[2] + b[3][r];
    }
}

static void testRisingBone(void)
{
    RdMesh skin = makeRisingSkinMesh();
    const double pivot[3] = {10.0 / 3.0, -290.0 / 3.0, 0.0};
    float bp[4][4], bc[4][4];
    double ip[3], ic[3];
    risingBone(bp, 0.0, 100.0);
    risingBone(bc, 30.0, 60.0);
    boneApply((const float (*)[4])bp, pivot, ip);
    boneApply((const float (*)[4])bc, pivot, ic);
    recordRise(skin, 0.0, 100.0);
    recordRise(skin, 30.0, 60.0);
    static const float alphas[3] = {0.25f, 0.5f, 0.75f};
    for (int i = 0; i < 3; i++) {
        const float t = alphas[i];
        const RdInterpStats *st = build(t, 1);
        CHECK(st->rotated == 1 && st->jump == 0,
              "alpha %.2f: the bone blends as a rotation, "
              "no jump (%u rotated, %u jumped)",
              t, st->rotated, st->jump);
        const RdFrame *f = built(t);
        const RdCmd *sk = findKey(f, 0, RD_KEY(&kObjE, 2, 24), 0);
        if (!sk) {
            CHECK(0, "alpha %.2f: the rising draw", t);
            continue;
        }
        const float (*b)[4] = (const float (*)[4])(
            const void *)(f->payload + sk->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
        double im[3];
        boneApply(b, pivot, im);
        const double want[3] = {ip[0] + (ic[0] - ip[0]) * t, ip[1] + (ic[1] - ip[1]) * t,
                                ip[2] + (ic[2] - ip[2]) * t};
        CHECK(b[3][1] <= 100.0f + 1e-3f && b[3][1] >= 60.0f - 1e-3f,
              "alpha %.2f: the bone's origin at Y %.4f, outside the ticks' 100 and 60", t, b[3][1]);
        CHECK(fabs(im[0] - want[0]) < 1e-3 && fabs(im[1] - want[1]) < 1e-3 &&
                  fabs(im[2] - want[2]) < 1e-3,
              "alpha %.2f: the skin's centroid at %.4f %.4f %.4f, want %.4f %.4f %.4f on the "
              "line between the ticks",
              t, im[0], im[1], im[2], want[0], want[1], want[2]);
        printf("rd_interp_test: rising bone at alpha %.2f: origin Y %.4f, centroid Y %.4f "
               "(ticks %.4f and %.4f)\n",
               t, b[3][1], im[1], ip[1], ic[1]);
    }
}

/* ---------------------------------------------------- the blended camera */

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

static const double kS6PointB[3] = {0.0, 0.0, 0.0}; /* B's origin: W = (-200, 0, 150) */

static int s_s6List; /* the list s6Draw draws in (8 for a shine packet) */

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
    rd_select_list(s_s6List);
    rd_draw_vu_mesh(mesh, &d, key);
}

/* one frame: the camera turned deg about the origin; mesh A (key part 0)
 * and, keyed by part bPart (0: not drawn), mesh B */
static void s6Frame(RdMesh mesh, double deg, int bPart)
{
    rd_begin_frame();
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
    rd_set_camera(&cam);
    s6Translate(wa, 0.0, 0.0, 0.0);
    s6Translate(wb, -200.0, 0.0, 150.0);
    s6Draw(mesh, v, wa, RD_KEY(&kObjS6, 0, 32));
    if (bPart) {
        s6Draw(mesh, v, wb, RD_KEY(&kObjS6, bPart, 32));
    }
    rd_end_frame(0);
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
    const RdInterpStats *st = build(0.5f, 1);
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
        const RdFrame *pf = rd__prev_frame(), *cf = rd__last_frame();
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
    st = build(0.5f, 1);
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
        /* without the blended camera, B would be cur's: at the 28 degree
         * camera */
        const RdFrame *cf = rd__last_frame();
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
    st = build(0.5f, 1);
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
     * blend (bit for bit) */
    s6Frame(mesh, 10.0, 0);
    s6Frame(mesh, 10.0, 1);
    st = build(0.5f, 1);
    f = built(0.5f);
    const RdFrame *cf = rd__last_frame();
    const float (*mo)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 1, 32), 0));
    const float (*mc)[4] = vuBlock(cf, findKey(cf, 0, RD_KEY(&kObjS6, 1, 32), 0));
    CHECK(st->rebased == 0 && mo && mc && memcmp(mo, mc, 36 * 16) == 0,
          "still camera: nothing re-based, the unmatched mesh is the tick's (%u)", st->rebased);
}

/* ------------------------------------------- parts that follow the camera */

static const char kObjLk;

/* a prelit draw of mesh with the common block of the camera v (world to
 * screen q v, the inverse view), the model matrices m16 (qw 16..19 and
 * 20..23) and m24, view RD_VU_VIEW_* */
static void lockDraw(RdMesh mesh, const double *q, const double *v, const double *m16,
                     const double *m24, uint8_t view, RdKey key)
{
    double s[16], iv[16];
    mul4(q, v, s);
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
    d.code = 34;
    d.clip = RD_VU_CLIP_NONE;
    d.view = view;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            d.vu.mem[4 + c][r] = (float)s[c * 4 + r];
            d.vu.mem[12 + c][r] = (float)iv[c * 4 + r];
            d.vu.mem[16 + c][r] = (float)m16[c * 4 + r];
            d.vu.mem[20 + c][r] = (float)m16[c * 4 + r];
            d.vu.mem[24 + c][r] = (float)m24[c * 4 + r];
        }
    }
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, key);
}

#define LOCK_ZOOM 512.0 /* the screen matrix's focal length (GsBase.c vs[0]) */

static const double kLockPlace[2][3] = {{10.0, 0.0, 30.0}, {-15.0, 5.0, 20.0}}; /* view space */

static const double kLockWorld[2][3] = {{100.0, 0.0, 300.0}, {-80.0, 20.0, 250.0}}; /* world */

/* the two ticks' cameras: the eye backs away 60 units, the view turns 8
 * degrees (under the cut thresholds, RD_INTERP_CAMERA_MOVE and _TURN) */
static void lockCamera(double t, double *v)
{
    const double eye[3] = {0.0, 0.0, -60.0 * t};
    s6View(8.0 * t, eye, v);
}

/* the screen matrix at focal length f (s6Proj's) */
static void lockProj(double f, double *p)
{
    const double keep = s6Focal;
    s6Focal = f;
    s6Proj(p);
    s6Focal = keep;
}

/* a billboard at the world point o seen through v: q T(v o) x 20 */
static void lockBillboard(const double *q, const double *v, const double o[3], double *m16,
                          double *x)
{
    double vo[3];
    for (int r = 0; r < 3; r++) {
        vo[r] = v[r] * o[0] + v[4 + r] * o[1] + v[8 + r] * o[2] + v[12 + r];
    }
    s6Translate(x, vo[0], vo[1], vo[2]);
    x[0] = x[5] = x[10] = 20.0;
    mul4(q, x, m16);
}

/* tick k (0, 1): the locked part (part 0) and the billboard (part 1) in
 * both ticks; in tick 1 a second locked part (2) and billboard (3) of that
 * tick only, and the first locked part again recorded as a world object of
 * that tick only (part 4: a matched pair of it would land within a fraction
 * of a pixel, the two ticks' errors cancelling in the blend; the tick's
 * alone is cur's object through the blended camera) */
static void lockFrame(RdMesh mesh, int k)
{
    rd_begin_frame();
    frameHead();
    double v[16], q[16], f[16];
    lockCamera((double)k, v);
    lockProj(LOCK_ZOOM, q);
    lockProj(500.0, f); /* +0x640: the 500 unit screen */
    RdCamera cam;
    memset(&cam, 0, sizeof(cam));
    for (int i = 0; i < 16; i++) {
        cam.view[i] = (float)v[i];
        cam.proj43[i] = (float)q[i];
    }
    cam.zoom = (float)LOCK_ZOOM;
    rd_set_camera(&cam);
    double l[16], m[16], x[16], stale[16];
    s6Translate(stale, 0.0, 0.0, 0.0); /* +0x80 x +0x40, another draw's */
    for (int n = 0; n < (k ? 2 : 1); n++) {
        s6Translate(l, kLockPlace[n][0], kLockPlace[n][1], kLockPlace[n][2]);
        mul4(f, l, m);
        lockDraw(mesh, q, v, m, stale, RD_VU_VIEW_LOCKED, RD_KEY(&kObjLk, n * 2, 0));
        if (n == 0 && k) {
            lockDraw(mesh, q, v, m, stale, RD_VU_VIEW_WORLD, RD_KEY(&kObjLk, 4, 0));
        }
        lockBillboard(q, v, kLockWorld[n], m, x);
        lockDraw(mesh, q, v, m, x, RD_VU_VIEW_FACING, RD_KEY(&kObjLk, n * 2 + 1, 0));
    }
    rd_end_frame(0);
}

/* the model origin through a block's qw 16..19: GS X, Y and w */
static void lockOrigin(const float (*m)[4], double out[3])
{
    const float *c = m[19];
    out[0] = c[0] / c[3];
    out[1] = c[1] / c[3];
    out[2] = c[3];
}

/* a billboard's turn about the view's y axis in degrees: its x axis (qw 16)
 * through the screen matrix q of focal length f is (f x + 2048 z, 0, z, z) */
static double lockTilt(const float (*m)[4], double f)
{
    const double z = m[16][3], x = (m[16][0] - 2048.0 * z) / f;
    return atan2(z, x) * 180.0 / 3.14159265358979323846;
}

static void testCameraLocked(void)
{
    RdMesh mesh = makeMesh();
    double f[16], q[16];
    lockProj(500.0, f);
    lockProj(LOCK_ZOOM, q);
    static const float kAlphas[2] = {0.25f, 0.5f};
    for (int a = 0; a < 2; a++) {
        const float t = kAlphas[a];
        lockFrame(mesh, 0);
        lockFrame(mesh, 1);
        const RdInterpStats *st = build(t, 1);
        CHECK(st->snap == RD_SNAP_NONE, "locked %.2f: the pair blends (snap %u)", (double)t,
              st->snap);
        const RdFrame *o = built(t);
        double vt[16];
        lockCamera((double)t, vt); /* the blended camera: the turn and the eye's step at t */
        for (int n = 0; n < 2; n++) {
            /* the locked part: where the tick put it */
            const float (*m)[4] = vuBlock(o, findKey(o, 0, RD_KEY(&kObjLk, n * 2, 0), 0));
            double l[16], fl[16], want[2], got[3];
            s6Translate(l, kLockPlace[n][0], kLockPlace[n][1], kLockPlace[n][2]);
            mul4(f, l, fl);
            s6Project(fl, (const double[3]){0.0, 0.0, 0.0}, want);
            if (m) {
                lockOrigin(m, got);
                CHECK(got[2] > 0.0 && hypot(got[0] - want[0], got[1] - want[1]) < 0.01,
                      "locked %.2f: the %s part at the tick's place on the screen (%.3f, %.3f; "
                      "want %.3f, %.3f; w %.2f)",
                      (double)t, n ? "tick's own" : "matched", got[0], got[1], want[0], want[1],
                      got[2]);
            } else {
                CHECK(0, "locked %.2f: locked part %d drawn", (double)t, n);
            }
            /* the billboard: its world point through the blended camera,
             * facing that camera */
            const float (*b)[4] = vuBlock(o, findKey(o, 0, RD_KEY(&kObjLk, n * 2 + 1, 0), 0));
            double qv[16], wp[2];
            mul4(q, vt, qv);
            s6Project(qv, kLockWorld[n], wp);
            if (b) {
                lockOrigin(b, got);
                const double tilt = lockTilt(b, LOCK_ZOOM);
                CHECK(hypot(got[0] - wp[0], got[1] - wp[1]) < 0.01 && fabs(tilt) < 1e-3,
                      "locked %.2f: the %s billboard at its world point (%.3f, want %.3f) facing "
                      "the blended camera (turned %.4f degrees)",
                      (double)t, n ? "tick's own" : "matched", got[0], wp[0], tilt);
            } else {
                CHECK(0, "locked %.2f: billboard %d drawn", (double)t, n);
            }
        }
        /* the same locked part, the tick's alone, taken for a world object
         * is carried with the blended camera: behind the eye at 0.25 (w
         * -14), at GS X 6224 at 0.5 */
        const float (*w)[4] = vuBlock(o, findKey(o, 0, RD_KEY(&kObjLk, 4, 0), 0));
        if (w) {
            double l[16], fl[16], want[2], got[3];
            s6Translate(l, kLockPlace[0][0], kLockPlace[0][1], kLockPlace[0][2]);
            mul4(f, l, fl);
            s6Project(fl, (const double[3]){0.0, 0.0, 0.0}, want);
            lockOrigin(w, got);
            CHECK(!(got[2] > 0.0) || hypot(got[0] - want[0], got[1] - want[1]) > 5.0,
                  "locked %.2f: as a world object the part leaves its place (%.3f, w %.2f; the "
                  "tick's %.3f)",
                  (double)t, got[0], got[2], want[0]);
        } else {
            CHECK(0, "locked %.2f: the world copy drawn", (double)t);
        }
    }
}

/* ------------------------------------------------------------ photo mode */

/* A paused photo tick as the game records it (port/game/photo_view.c):
 * the frame's camera turned deg about the origin, and in list 11 the
 * full-screen passes rd_post.c records as RD_SPACE_FULLSCREEN screen
 * prims: the brightness sprite (GsBase.c gsb_controlBrightness), the two
 * letterbox bars, the film noise */
static void photoTick(double deg)
{
    rd_begin_frame();
    frameHead();
    double eye[3], v[16], p[16];
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
    rd_set_camera(&cam);
    rd_select_list(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 0xFF;
    pp.rgba[3] = 8;
    rd_post(RD_POST_BRIGHTNESS, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x60;
    pp.lines = 58;
    rd_post(RD_POST_LETTERBOX, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 0x20;
    pp.scalar[0] = 4.0f;
    rd_post(RD_POST_FILM_NOISE, &pp);
    rd_end_frame(0);
}

static int countType(const RdFrame *f, int l, uint8_t type, uint8_t space)
{
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        n += c->type == type && (type != RDC_SCREEN || c->b[1] == space);
    }
    return n;
}

/* a camera's eye and its yaw (degrees, s6View's angle) */
static void eyeYaw(const RdCamera *c, double eye[3], double *yaw)
{
    const float *v = c->view;
    for (int j = 0; j < 3; j++) {
        eye[j] = -((double)v[j * 4 + 0] * v[12] + (double)v[j * 4 + 1] * v[13] +
                   (double)v[j * 4 + 2] * v[14]);
    }
    *yaw = atan2(-(double)v[2], (double)v[10]) * 180.0 / 3.14159265358979323846;
}

/* Photo mode is the paused game drawn from the photo camera every tick:
 * two such ticks whose cameras differ (20 degrees about the origin, the
 * eye 174 of 300 units) are blended like any pair, the camera half way,
 * and list 11's full-screen passes all survive rd__interp_frame (nothing
 * of the picture is dropped) */
static void testPhoto(void)
{
    photoTick(0.0);
    photoTick(20.0);
    const RdFrame *pf = rd__prev_frame(), *cf = rd__last_frame();
    const int want = countType(cf, 11, RDC_SCREEN, RD_SPACE_FULLSCREEN);
    CHECK(want == 4 && countType(pf, 11, RDC_SCREEN, RD_SPACE_FULLSCREEN) == 4,
          "photo: the tick records brightness, two letterbox bars and the noise (%d)", want);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->snap == RD_SNAP_NONE, "photo: the two ticks blend (snap %u)", st->snap);
    const RdFrame *f = built(0.5f);
    CHECK(f && countType(f, 11, RDC_SCREEN, RD_SPACE_FULLSCREEN) == want,
          "photo: list 11's full-screen prims all kept (%d of %d)",
          countType(f, 11, RDC_SCREEN, RD_SPACE_FULLSCREEN), want);
    CHECK(f && f->hasCamera, "photo: the blended frame has a camera");
    if (f && f->hasCamera && pf->hasCamera && cf->hasCamera) {
        double e0[3], e1[3], e[3], y0, y1, y;
        eyeYaw(&pf->camera, e0, &y0);
        eyeYaw(&cf->camera, e1, &y1);
        eyeYaw(&f->camera, e, &y);
        CHECK(memcmp(f->camera.view, pf->camera.view, sizeof(f->camera.view)) != 0 &&
                  memcmp(f->camera.view, cf->camera.view, sizeof(f->camera.view)) != 0,
              "photo: the camera is blended, not either tick's");
        CHECK(y > y0 + 1.0 && y < y1 - 1.0 && fabs(y - 0.5 * (y0 + y1)) < 1.0,
              "photo: the yaw half way (%.3f between %.3f and %.3f)", y, y0, y1);
        CHECK(e[0] > e0[0] && e[0] < e1[0] && fabs(e[1]) < 1e-3,
              "photo: the eye between the ticks' (x %.3f between %.3f and %.3f)", e[0], e0[0],
              e1[0]);
    }
    /* alpha 1 is the current tick's camera */
    f = built(1.0f);
    CHECK(f && memcmp(f->camera.view, cf->camera.view, sizeof(f->camera.view)) == 0,
          "photo: alpha 1 is the current tick's camera");
}

/* ----------------------------------------------------- the present clock */

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
        const float a = rd_present_clock_alpha(&c, now, tickAt, tick, gap);
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
    const float a = rd_present_clock_alpha(&c, 1000.0 + 2000 * gap + 500.0,
                                           1000.0 + 2000 * gap + 490.0, tick, gap);
    CHECK(c.resets == 2 && fabsf(a - (float)(10.0 / tick)) < 1e-5f,
          "present clock: a 500 ms pause resets it (alpha %.4f)", a);
}

/* ----------------------------------- unmatched draws, particles, lights */

static const char kObjU, kObjP1, kObjP2, kObjP3, kObjL;

/* the alpha of the (only) RDC_SCREEN draw with key k in list 0, -1: none */
static int alphaOf(const RdFrame *f, RdKey k)
{
    const RdCmd *c = findKey(f, 0, k, 0);
    return c && c->type == RDC_SCREEN ? screenVtx(f, c)[0].rgba[3] : -1;
}

static int drawsOf(const RdFrame *f, int l, RdKey k, uint8_t type)
{
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        n += c->type == type && c->keyLo == (uint32_t)k && c->keyHi == (uint32_t)(k >> 32);
    }
    return n;
}

/* the state at the first draw of key k in list l */
static int stateAt(const RdFrame *f, int l, RdKey k, RdStateBlock *out)
{
    RdStateBlock st = f->startState;
    for (int li = 0; li < RD_LIST_COUNT; li++) {
        for (uint32_t i = 0; i < f->lists[li].count; i++) {
            const RdCmd *c = &f->lists[li].cmds[i];
            rd__apply_state(&st, c);
            if (li == l && c->type != RDC_NOP && !rd__cmd_is_state(c->type) &&
                c->keyLo == (uint32_t)k && c->keyHi == (uint32_t)(k >> 32)) {
                *out = st;
                return 1;
            }
        }
    }
    return 0;
}

static void endStateOf(const RdFrame *f, RdStateBlock *out)
{
    *out = f->startState;
    rd__walk(f, (int)f->keep, out, NULL, NULL);
}

/* a 3-triangle volume at x0 in list 3 (untagged layout as rd_shadow_tris
 * records it) */
static void volume(int x0, RdKey key)
{
    RdScreenVtx sv[9];
    int8_t sign[3] = {1, -1, 1};
    memset(sv, 0, sizeof(sv));
    for (int i = 0; i < 9; i++) {
        sv[i].x = OX + (x0 + i * 4) * 16;
        sv[i].y = OY + i * 16;
        sv[i].z = 1000u;
    }
    rd_shadow_tris(sv, sign, 3, key);
}

/* prev (second 0): KM, a fading KP (ABE on, LERP As), an opaque KQ, and
 * volumes KS then KT; cur: KM, a fading KN, an opaque KR, volumes KS, KU */
static void recordUnmatched(int second)
{
    static const uint8_t grey[4] = {90, 90, 90, 0x80};
    rd_begin_frame();
    frameHead();
    sprite(second ? 4 : 0, 0, (second ? 4 : 0) + 10, 10, grey, RD_KEY(&kObjU, 0, 0)); /* KM */
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    sprite(20, 0, 30, 10, grey, RD_KEY(&kObjU, second ? 2 : 1, 0)); /* KN / KP: fade */
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    sprite(40, 0, 50, 10, grey, RD_KEY(&kObjU, second ? 4 : 3, 0)); /* KR / KQ: opaque */
    rd_select_list(3);
    rd_shadow_reset();
    volume(second ? 2 : 0, RD_KEY(&kObjU, 5, 0));  /* KS */
    volume(60, RD_KEY(&kObjU, second ? 7 : 6, 0)); /* KU / KT */
    rd_shadow_resolve();
    rd_end_frame(0);
}

static void testUnmatched(void)
{
    const RdKey kM = RD_KEY(&kObjU, 0, 0), kP = RD_KEY(&kObjU, 1, 0), kN = RD_KEY(&kObjU, 2, 0);
    const RdKey kQ = RD_KEY(&kObjU, 3, 0), kR = RD_KEY(&kObjU, 4, 0), kT = RD_KEY(&kObjU, 6, 0);
    const RdKey kU = RD_KEY(&kObjU, 7, 0);
    recordUnmatched(0);
    recordUnmatched(1);
    const RdFrame *cur = rd__last_frame();
    RdStateBlock endCur, endOut, at;
    endStateOf(cur, &endCur);
    /* a quarter of the way: the tick before's draws mostly */
    const RdFrame *f = built(0.25f);
    CHECK(alphaOf(f, kN) == 0x20 && alphaOf(f, kP) == 0x60,
          "t 0.25: the new sprite fades in (alpha %d, want 0x20), the gone one fades out (alpha "
          "%d, want 0x60)",
          alphaOf(f, kN), alphaOf(f, kP));
    CHECK(alphaOf(f, kQ) == 0x80 && drawsOf(f, 0, kR, RDC_SCREEN) == 0,
          "t 0.25: an opaque sprite of the tick before is drawn whole, the new one not yet");
    CHECK(drawsOf(f, 3, kT, RDC_SHADOW_STRIP) == 1 && drawsOf(f, 3, kU, RDC_SHADOW_STRIP) == 0,
          "t 0.25: the volume of the tick before is drawn, the new one not yet (%d, %d)",
          drawsOf(f, 3, kT, RDC_SHADOW_STRIP), drawsOf(f, 3, kU, RDC_SHADOW_STRIP));
    CHECK(stateAt(f, 0, kP, &at) && at.ds.abe == 1 && stateAt(f, 0, kQ, &at) && at.ds.abe == 0,
          "the inserted sprites draw with their own tick's state");
    endStateOf(f, &endOut);
    CHECK(memcmp(&endOut, &endCur, sizeof(endCur)) == 0,
          "the insertions restore the state: the frame ends as the tick does");
    {
        /* the volume sits between the stencil reset and the resolve */
        int seenReset = 0, ok = 0;
        for (uint32_t i = 0; i < f->lists[3].count; i++) {
            const RdCmd *c = &f->lists[3].cmds[i];
            seenReset |= c->type == RDC_SHADOW_RESET;
            if (c->type == RDC_SHADOW_STRIP && c->keyLo == (uint32_t)kT) {
                ok = seenReset;
            }
            if (c->type == RDC_SHADOW_RESOLVE) {
                break;
            }
        }
        CHECK(ok, "the volume of the tick before is drawn inside the stencil pass");
    }
    /* three quarters */
    f = built(0.75f);
    CHECK(alphaOf(f, kN) == 0x60 && alphaOf(f, kP) == 0x20,
          "t 0.75: alpha in %d (want 0x60), out %d (want 0x20)", alphaOf(f, kN), alphaOf(f, kP));
    CHECK(drawsOf(f, 0, kQ, RDC_SCREEN) == 0 && alphaOf(f, kR) == 0x80,
          "t 0.75: the opaque sprite of the tick before is gone, the new one is drawn");
    CHECK(drawsOf(f, 3, kT, RDC_SHADOW_STRIP) == 0 && drawsOf(f, 3, kU, RDC_SHADOW_STRIP) == 1,
          "t 0.75: the new volume is drawn, the one of the tick before not");
    endStateOf(f, &endOut);
    CHECK(memcmp(&endOut, &endCur, sizeof(endCur)) == 0, "t 0.75: the frame ends as the tick does");
    /* the ends: alpha 0 has the tick before's unmatched draws whole and none
     * of the new; alpha 1 is the tick, byte for byte */
    f = built(0.0f);
    CHECK(alphaOf(f, kP) == 0x80 && alphaOf(f, kN) == 0 && alphaOf(f, kQ) == 0x80 &&
              drawsOf(f, 0, kR, RDC_SCREEN) == 0,
          "alpha 0: the tick before's sprites whole, the new ones invisible");
    f = built(1.0f);
    CHECK(f && f->payloadSize == cur->payloadSize &&
              memcmp(f->payload, cur->payload, cur->payloadSize) == 0 &&
              f->lists[0].count == cur->lists[0].count &&
              memcmp(f->lists[0].cmds, cur->lists[0].cmds, cur->lists[0].count * sizeof(RdCmd)) ==
                  0 &&
              f->lists[3].count == cur->lists[3].count,
          "alpha 1: the tick's lists and payload");
    /* the matched sprite still blends */
    f = built(0.5f);
    const RdScreenVtx *m = screenVtx(f, findKey(f, 0, kM, 0));
    CHECK(m && m[0].x == OX + 2 * 16, "the matched sprite blends as before");
}

/* two emitters' batches (2 and 3 particles) in list 6, keyed by emitter as
 * MicroCode.c keys prim_DispParticle's (RD_KEY(emitter, 18, 0)); with
 * insert, a third emitter's batch of 4 comes first.  byEmitter 0: the
 * batches keyed 0 (rd_mesh.c's list key, matched by order) */
static void particleBatch(const void *emitter, int n, float x, int byEmitter)
{
    static float p[6 + 2 * 4][4];
    memset(p, 0, sizeof(p));
    const int32_t cnt = n;
    memcpy(&p[0][0], &cnt, 4);
    for (int i = 0; i < n; i++) {
        p[6 + 2 * i][0] = x + (float)i;
        p[6 + 2 * i][3] = 1.0f;
        p[7 + 2 * i][3] = 0.5f;
    }
    RdVuParticleDraw pd;
    memset(&pd, 0, sizeof(pd));
    pd.qw = (const float (*)[4])p;
    pd.count = (uint32_t)n;
    identity(pd.vu.mem, 4);
    identity(pd.vu.mem, 16);
    pd.vu.mem[19][0] = 2048.0f;
    pd.vu.mem[19][1] = 2048.0f;
    pd.vu.mem[19][3] = 1.0f;
    rd_draw_vu_particles(&pd, byEmitter ? RD_KEY(emitter, 18, 0) : 0);
}

static void recordParticleOrder(int second, int byEmitter)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(6);
    if (second) {
        particleBatch(&kObjP3, 4, 500.0f, byEmitter); /* a new emitter, ahead of the others */
    }
    particleBatch(&kObjP1, 2, second ? 2.0f : 0.0f, byEmitter);
    particleBatch(&kObjP2, 3, second ? 12.0f : 10.0f, byEmitter);
    rd_end_frame(0);
}

/* the first particle's x of the n-th particle batch of list 6 */
static float particleX(const RdFrame *f, int nth)
{
    for (uint32_t i = 0; i < f->lists[6].count; i++) {
        const RdCmd *c = &f->lists[6].cmds[i];
        if (c->type == RDC_PARTICLES && nth-- == 0) {
            const float (*q)[4] = (const float (*)[4])(
                const void *)(f->payload + c->u[1] + sizeof(RdVuPayload) + sizeof(RdVuBlock));
            return q[6][0];
        }
    }
    return -1.0f;
}

static void testParticleOrder(void)
{
    /* before: by list order, the new batch pairs with the first and every
     * pair differs in count: all three snap */
    recordParticleOrder(0, 0);
    recordParticleOrder(1, 0);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->lerped == 0 && st->mismatch == 2 && st->missing == 1,
          "particles by list order: a batch inserted ahead snaps the others (lerped %u, "
          "mismatch %u, missing %u)",
          st->lerped, st->mismatch, st->missing);
    /* by emitter, the two batches keep their partners */
    recordParticleOrder(0, 1);
    recordParticleOrder(1, 1);
    st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    CHECK(st->lerped == 2 && st->mismatch == 0 && st->missing == 1,
          "particles by emitter: both batches blend, the new one is the tick's (lerped %u, "
          "mismatch %u, missing %u)",
          st->lerped, st->mismatch, st->missing);
    CHECK(particleX(f, 0) == 500.0f && particleX(f, 1) == 1.0f && particleX(f, 2) == 11.0f,
          "particles by emitter: half way (%g, %g, %g)", particleX(f, 0), particleX(f, 1),
          particleX(f, 2));
}

/* a lit mesh (normal_l) turning deg about z with one light along x: L1 =
 * Ln W, Ln's row 0 the light, rows 1 and 2 zero (no light), row 3 (0, 0,
 * 0, 1); L2 a colour and an ambient */
static void recordLitTurn(RdMesh mesh, double deg)
{
    rd_begin_frame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_LIT;
    d.code = 32;
    double w[16];
    turnZ(w, deg, 1.0, 10.0);
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 12);
    for (int cI = 0; cI < 4; cI++) {
        for (int r = 0; r < 4; r++) {
            d.vu.mem[16 + cI][r] = (float)w[cI * 4 + r];
            d.vu.mem[20 + cI][r] = (float)w[cI * 4 + r];
            d.vu.mem[24 + cI][r] = (float)w[cI * 4 + r];
        }
    }
    /* L1 = Ln W: row 0 = (1, 0, 0) W's 3 x 3, i.e. W's first row */
    memset(&d.vu.mem[28], 0, 8 * 16);
    for (int cI = 0; cI < 3; cI++) {
        d.vu.mem[28 + cI][0] = (float)w[cI * 4 + 0];
    }
    d.vu.mem[31][3] = 1.0f;
    d.vu.mem[32][0] = 0.8f; /* L2: light 0's colour */
    d.vu.mem[32][1] = 0.6f;
    d.vu.mem[32][2] = 0.4f;
    d.vu.mem[32][3] = 1.0f;
    d.vu.mem[35][0] = d.vu.mem[35][1] = d.vu.mem[35][2] = 0.25f; /* the ambient */
    d.vu.mem[35][3] = 1.0f;
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjL, 0, 32));
    rd_end_frame(0);
}

static void testLightTurn(void)
{
    RdMesh mesh = makeMesh();
    recordLitTurn(mesh, 0.0);
    recordLitTurn(mesh, 90.0);
    build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjL, 0, 32), 0));
    if (m) {
        /* the luminance of the normal facing the light is the length of L1's
         * row 0 (max0(L1 n), n a unit normal): 1 at either tick; element by
         * element (1, 0, 0) and (0, -1, 0) meet at 0.707 */
        const float x = m[28][0], y = m[29][0], z = m[30][0];
        const float lum = sqrtf(x * x + y * y + z * z);
        const float ew = sqrtf(0.5f * 0.5f + 0.5f * 0.5f);
        CHECK(fabsf(lum - 1.0f) < 0.01f && fabsf(x - 0.70710678f) < 1e-4f &&
                  fabsf(y + 0.70710678f) < 1e-4f,
              "a 90 degree turn: the light row at 45 degrees with luminance %.5f (element-wise "
              "%.5f)",
              lum, ew);
        printf("rd_interp_test: lit 90 degree turn at alpha 0.5: luminance %.6f (element-wise "
               "%.6f)\n",
               lum, ew);
        CHECK(m[28][1] == 0.0f && m[29][2] == 0.0f && m[31][3] == 1.0f && m[31][0] == 0.0f,
              "the unused lights stay zero, row 3 and column 3 as recorded");
        CHECK(m[32][0] == 0.8f && m[35][0] == 0.25f, "the colours and the ambient as recorded");
    } else {
        CHECK(0, "the lit draw");
    }
    rd_destroy_vu_mesh(mesh);
}

/* Morph limits.  A mesh rewritten twice before its draw in every frame
 * (two reg_setShape calls), twins (C in odd frames, D in even ones) each
 * rewritten twice in the frame before their draw, and RD_INTERP_MORPH_MANY
 * meshes rewritten every frame */
#define RD_INTERP_MORPH_MANY 200

static void morphLimitFrame(RdMesh a, RdMesh c, RdMesh d, const RdMesh *many, int n, float x,
                            int odd)
{
    rd_begin_frame();
    frameHead();
    morphUpdate(a, x - 5.0f); /* an intermediate shape no frame draws */
    morphUpdate(a, x);
    morphDrawAt(a, RD_KEY(&kObjD, 7, 0));
    /* the twin not drawn this frame takes the next frame's shape, twice */
    morphUpdate(odd ? d : c, x + 13.0f);
    morphUpdate(odd ? d : c, x + 20.0f);
    morphDrawAt(odd ? c : d, RD_KEY(&kObjD, 8, 0));
    for (int i = 0; i < n; i++) {
        morphUpdate(many[i], x + (float)i);
        morphDrawAt(many[i], RD_KEY(&kObjD, 9, i));
    }
}

static void testMorphLimits(void)
{
    RdSettings s = *rd_get_settings(), keep = s;
    s.preset = RD_PRESET_ENHANCED;
    s.sceneScale = 1.0f;
    s.interpolate = 1;
    rd_set_settings(&s);
    static RdMesh many[RD_INTERP_MORPH_MANY];
    RdMesh a = morphMesh(0.0f), c = morphMesh(0.0f), d = morphMesh(0.0f);
    for (int i = 0; i < RD_INTERP_MORPH_MANY; i++) {
        many[i] = morphMesh(0.0f);
    }
    /* the twins: D drawn in frame 1 with the shape set before it */
    morphUpdate(d, 100.0f);
    morphLimitFrame(a, c, d, many, RD_INTERP_MORPH_MANY, 100.0f, 0); /* draws D (100) */
    rd_end_frame(0);
    morphLimitFrame(a, c, d, many, RD_INTERP_MORPH_MANY, 120.0f, 1); /* draws C (120) */
    rd_end_frame(0);
    morphLimitFrame(a, c, d, many, RD_INTERP_MORPH_MANY, 200.0f, 0); /* records */
    const RdInterpStats *st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    /* frame 1 drew a at 100, D at 100 (set before it), frame 2 a at 120, C
     * at 100 + 20 (rewritten twice in frame 1) */
    CHECK(morphX(f, RD_KEY(&kObjD, 7, 0), 0) == 110.0f,
          "a double rewrite blends from the first tick's shape (%g, want 110)",
          morphX(f, RD_KEY(&kObjD, 7, 0), 0));
    CHECK(morphX(f, RD_KEY(&kObjD, 8, 0), 0) == 110.0f,
          "twins rewritten twice: from D's 100 to C's 120 (%g, want 110)",
          morphX(f, RD_KEY(&kObjD, 8, 0), 0));
    int ok = 1, last = -1;
    for (int i = 0; i < RD_INTERP_MORPH_MANY; i++) {
        const float x = morphX(f, RD_KEY(&kObjD, 9, i), 0);
        if (x != 110.0f + (float)i) {
            ok = 0;
            last = i;
        }
    }
    CHECK(ok && st->morph == RD_INTERP_MORPH_MANY + 2,
          "%d morph draws in a present all blend (%u streams; draw %d is not)",
          RD_INTERP_MORPH_MANY + 2, st->morph, last);
    f = built(0.0f);
    CHECK(morphX(f, RD_KEY(&kObjD, 7, 0), 0) == 100.0f &&
              morphX(f, RD_KEY(&kObjD, 9, RD_INTERP_MORPH_MANY - 1), 0) ==
                  100.0f + (float)(RD_INTERP_MORPH_MANY - 1),
          "alpha 0: the first tick's shapes");
    if (g_rd.hasDevice) {
        CHECK(rd__replay_frame(built(0.5f), 0, false), "morph limits: the half-way frame replays");
    }
    rd_end_frame(0);
    rd_set_settings(&keep);
    rd_begin_frame();
    rd_end_frame(0);
    rd_destroy_vu_mesh(a);
    rd_destroy_vu_mesh(c);
    rd_destroy_vu_mesh(d);
    for (int i = 0; i < RD_INTERP_MORPH_MANY; i++) {
        rd_destroy_vu_mesh(many[i]);
    }
}

/* ------------------------------------------------ shine, swap and sine */

/* (hypothesis 2) a glowing material in list 8 (the mirage's mask, drawn
 * into AURA_WORK, RegistPacket.c regGetShinePri: shine 2) and the stone of
 * the same object in list 0, the camera turning 20 degrees in the tick:
 * at alpha 0.25, 0.5 and 0.75 the mask lands where the stone does (the
 * FEED128 paste sprites are screen-sized and do not move, so the halo
 * follows the mask) */
static void shineFrame(RdMesh mesh, double deg)
{
    rd_begin_frame();
    frameHead();
    double eye[3], v[16], p[16], w[16];
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
    rd_set_camera(&cam);
    s6Translate(w, 0.0, 0.0, 0.0);
    s6Draw(mesh, v, w, RD_KEY(&kObjS6, 0, 0)); /* the stone, list 0 */
    rd_select_list(8);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), (RdTarget){0}, TW, TH, 0);
    s_s6List = 8;
    s6Draw(mesh, v, w, RD_KEY(&kObjS6, 0, 4)); /* the shine packet, list 8 */
    s_s6List = 0;
    rd_end_frame(0);
}

static void testShineFollows(void)
{
    RdMesh mesh = makeMesh();
    shineFrame(mesh, 0.0);
    shineFrame(mesh, 20.0);
    static const float kAlpha[3] = {0.25f, 0.5f, 0.75f};
    double worst = 0.0;
    for (int a = 0; a < 3; a++) {
        const RdInterpStats *st = build(kAlpha[a], 1);
        const RdFrame *f = built(kAlpha[a]);
        const float (*ms)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjS6, 0, 0), 0));
        const float (*mg)[4] = vuBlock(f, findKey(f, 8, RD_KEY(&kObjS6, 0, 4), 0));
        CHECK(st->snap == RD_SNAP_NONE && st->lerped == 2 && ms && mg,
              "shine at alpha %.2f: both draws blend (snap %u, lerped %u)", kAlpha[a], st->snap,
              st->lerped);
        if (!ms || !mg) {
            continue;
        }
        double ps[2], pg[2];
        s6ProjectF(ms, kS6PointA, ps);
        s6ProjectF(mg, kS6PointA, pg);
        const double d = hypot(ps[0] - pg[0], ps[1] - pg[1]);
        worst = d > worst ? d : worst;
        printf("  shine at alpha %.2f: stone at (%.3f, %.3f), mask at (%.3f, %.3f), %.5f GS "
               "pixels apart\n",
               kAlpha[a], ps[0], ps[1], pg[0], pg[1], d);
    }
    CHECK(worst <= 1.0 / 16.0, "shine: the list-8 mask %.4f GS pixels from the stone", worst);
    rd_destroy_vu_mesh(mesh);
}

/* (hypothesis 3) two emitters' batches of equal count in list 6 whose
 * order swaps between the ticks: keyed by emitter (MicroCode.c
 * mc_HostParticleKey) each blends with its own; by list order (key 0)
 * equal counts would pair the two emitters */
static void recordParticleSwap(int second, int byEmitter)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(6);
    if (second) {
        particleBatch(&kObjP2, 3, 12.0f, byEmitter);
        particleBatch(&kObjP1, 3, 2.0f, byEmitter);
    } else {
        particleBatch(&kObjP1, 3, 0.0f, byEmitter);
        particleBatch(&kObjP2, 3, 10.0f, byEmitter);
    }
    rd_end_frame(0);
}

static void testParticleSwap(void)
{
    recordParticleSwap(0, 1);
    recordParticleSwap(1, 1);
    const RdInterpStats *st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    CHECK(st->lerped == 2 && st->mismatch == 0 && st->missing == 0,
          "particles swapped, by emitter: both blend (lerped %u, mismatch %u, missing %u)",
          st->lerped, st->mismatch, st->missing);
    CHECK(particleX(f, 0) == 11.0f && particleX(f, 1) == 1.0f,
          "particles swapped, by emitter: each half way from its own (%g, %g)", particleX(f, 0),
          particleX(f, 1));
    recordParticleSwap(0, 0);
    recordParticleSwap(1, 0);
    build(0.5f, 1);
    f = built(0.5f);
    printf("  particles swapped, by list order (not the game's key): first particles at %g and "
           "%g (each from the other emitter: within four sizes they would blend across)\n",
           particleX(f, 0), particleX(f, 1));
}

/* (hypothesis 4) Texture.c's sine scroll (tex_textureAnimation: uOfs =
 * ampU sin(frame pi scrlU / ticks per second)) is never wrapped, so a step
 * of more than 1 is real: amplitude 0.8 at 7 Hz on 25 ticks a second moves
 * up to 1.41 a tick.  Blended at alpha 0.25, 0.5 and 0.75 the offset must
 * equal the straight blend modulo 1 (the texture repeats every 1); a
 * linear scroll's wrap by 2 still blends the short way */
static void recordScroll(RdMesh mesh, float u, int sine)
{
    rd_begin_frame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 16);
    d.vu.mem[19][0] = 2048.0f;
    d.vu.mem[19][1] = 2048.0f;
    d.vu.mem[2][0] = u;
    d.vu.mem[2][1] = u;
#ifdef RD_VU_SCROLL_SINE_U
    d.scroll = (uint8_t)(sine ? RD_VU_SCROLL_SINE_U : 0);
#else
    (void)sine;
#endif
    rd_select_list(0);
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjE, 7, 32));
    rd_end_frame(0);
}

static double frac1(double x)
{
    return x - floor(x);
}

static void testSineScroll(void)
{
    RdMesh mesh = makeMesh();
    /* the largest step of the sine over a second of ticks */
    const double amp = 0.8, hz = 7.0, ticks = 25.0;
    double best = 0.0, p = 0.0, c = 0.0;
    for (int k = 0; k < 25; k++) {
        const double a = amp * sin(2.0 * 3.14159265358979 * hz * k / ticks);
        const double b = amp * sin(2.0 * 3.14159265358979 * hz * (k + 1) / ticks);
        if (fabs(b - a) > best) {
            best = fabs(b - a);
            p = a;
            c = b;
        }
    }
    static const float kAlpha[3] = {0.25f, 0.5f, 0.75f};
    for (int sine = 0; sine < 2; sine++) {
        recordScroll(mesh, (float)p, sine);
        recordScroll(mesh, (float)c, sine);
        double worst = 0.0;
        for (int a = 0; a < 3; a++) {
            const RdFrame *f = built(kAlpha[a]);
            const float (*m)[4] = vuBlock(f, findKey(f, 0, RD_KEY(&kObjE, 7, 32), 0));
            if (!m) {
                CHECK(0, "sine scroll: the draw");
                continue;
            }
            const double want =
                (double)(float)p + kAlpha[a] * ((double)(float)c - (double)(float)p);
            double d = fabs(frac1(m[2][0]) - frac1(want));
            d = d > 0.5 ? 1.0 - d : d;
            worst = d > worst ? d : worst;
            printf("  sine scroll %.4f -> %.4f (%s) at alpha %.2f: U %.4f, the sine's blend %.4f, "
                   "%.4f of the texture apart; linear V %.4f\n",
                   p, c, sine ? "marked sine" : "unmarked", kAlpha[a], m[2][0], want, d, m[2][1]);
        }
        if (sine) {
            CHECK(worst < 1e-4, "sine scroll: a step of %.3f blended %.4f of the texture off", best,
                  worst);
        }
    }
    rd_destroy_vu_mesh(mesh);
    /* the mark as the seki side sends it: Texture.c's SET_UVOFFSET quadword
     * with z (U) and w (V) nonzero for a sine axis, kept by the list until
     * the next SET_UVOFFSET or common block */
    rd_begin_frame();
    frameHead();
    rd_select_list(0);
    rd_vu_program(1);
    const uint32_t mark[4] = {0x3F000000u, 0u, 0u, 1u}; /* U 0.5, V 0, V a sine */
    float uvq[1][4];
    memcpy(uvq, mark, sizeof(uvq));
    rd_vu_call(2, (const float (*)[4])uvq, 1);
    rd_vu_call(32, NULL, 0);
    RdVuDraw d;
    CHECK(rd_vu_draw_from_state(&d) && d.scroll == RD_VU_SCROLL_SINE_V && d.vu.mem[2][0] == 0.5f,
          "SET_UVOFFSET's w marks V a sine scroll (scroll %u)", d.scroll);
    RdVuCommon common;
    memset(&common, 0, sizeof(common));
    rd_set_vu_common(&common);
    rd_vu_call(32, NULL, 0);
    CHECK(rd_vu_draw_from_state(&d) && d.scroll == 0, "the common block clears the mark (%u)",
          d.scroll);
    rd_end_frame(0);
}

/* ------------------------------------------------ the near lights' slots */

static const char kObjN, kObjR, kObjG;

/* The boy's lights in the dark hall: qw 28..35 of the VU block of boymodel
 * (SKINNED, key 016f5fd1fb600000, prog 5, code 24, list 0) in the F12 dumps
 * frame-20261009-083248-v13836 and -v13847.  Light 0 is the torch; lights
 * A (direction (+0.151, -0.531, +0.834), grey 0.252) and B ((+0.947,
 * -0.251, -0.200), colour (0.238, 0.205, 0.178) then (0.292, 0.252,
 * 0.219)) are in slots 1 and 2 in the first and in slots 2 and 1 in the
 * second (Light.c light_getNearLight orders them by strength). */
static const float kHallLights[2][8][4] = {
    {{-0.739586651f, 0.150836766f, 0.947048664f, 0.0f},
     {-0.526257157f, -0.531259656f, -0.251055896f, 0.0f},
     {-0.419600993f, 0.833673537f, -0.20017457f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.672120154f, 0.579868317f, 0.503431141f, 1.0f},
     {0.252478987f, 0.252478987f, 0.252478987f, 1.0f},
     {0.238166645f, 0.205477074f, 0.178391472f, 1.0f},
     {0.478431344f, 0.505882323f, 0.521568596f, 1.0f}},
    {{-0.739988029f, 0.946839809f, 0.150774419f, 0.0f},
     {-0.52592051f, -0.251541257f, -0.531289577f, 0.0f},
     {-0.419315368f, -0.200553343f, 0.833665729f, 0.0f},
     {0.0f, 0.0f, 0.0f, 1.0f},
     {0.822243273f, 0.709386289f, 0.615876257f, 1.0f},
     {0.29238373f, 0.252252609f, 0.219001129f, 1.0f},
     {0.252479583f, 0.252479583f, 0.252479583f, 1.0f},
     {0.478431344f, 0.505882323f, 0.521568596f, 1.0f}},
};

/* the hall's lights k on a skinned draw (prog 5, code 24, one bone at
 * rest), a lit mesh (normal_l) and a lit grid (code 22), still */
static void recordHallLights(RdMesh mesh, RdMesh skin, int k)
{
    rd_begin_frame();
    frameHead();
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 12);
    identity(d.vu.mem, 16);
    identity(d.vu.mem, 20);
    identity(d.vu.mem, 24);
    memcpy(d.vu.mem[28], kHallLights[k], sizeof(kHallLights[k]));
    static float bone[4][4];
    identity(bone, 0);
    d.prog = RD_PROG_SKIN_SPEC;
    d.code = 24;
    d.bones = (const float (*)[4])bone;
    d.boneQw = 4;
    rd_select_list(0);
    rd_draw_vu_mesh(skin, &d, RD_KEY(&kObjN, 0, 24));
    d.prog = RD_PROG_LIT;
    d.code = 32;
    d.bones = NULL;
    d.boneQw = 0;
    rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjN, 1, 32));
    /* one strip of 3 lit vertices: VIF qword, tag, colour, 3 x (pos,
     * normal, ST), MSCNT */
    static float g[3 * 3 + 4][4];
    memset(g, 0, sizeof(g));
    g[1][0] = 7.0f;
    g[2][0] = g[2][1] = g[2][2] = 128.0f;
    for (int v = 0; v < 3; v++) {
        g[3 + v * 3][0] = (float)(v * 10);
        g[3 + v * 3][3] = 1.0f;
        g[4 + v * 3][1] = 1.0f;
        g[5 + v * 3][0] = 0.5f;
    }
    RdVuGridDraw gd;
    memset(&gd, 0, sizeof(gd));
    gd.qw = (const float (*)[4])g;
    gd.strips = 1;
    gd.stripLen = 3;
    gd.lit = 1;
    gd.code = 22;
    gd.vu = d.vu;
    rd_draw_vu_grid(&gd, RD_KEY(&kObjN, 2, 22));
    rd_end_frame(0);
}

/* normal_l's and cluster's colour for the unit normal n (vu_skin.hlsl,
 * vu_lit.hlsl: c = L2 max0(L1 n) with the ambient column) */
/* the shader's lighting of normal n from eight rows: L1's four rows (qw
 * 28..31) then L2's four (qw 32..35); a VU block is passed as block + 28 */
static void lightShade(const float (*m)[4], const double n[3], double out[3])
{
    double l[3];
    for (int i = 0; i < 3; i++) {
        const double v = m[0][i] * n[0] + m[1][i] * n[1] + m[2][i] * n[2] + m[3][i];
        l[i] = v > 0.0 ? v : 0.0;
    }
    for (int ch = 0; ch < 3; ch++) {
        out[ch] = m[4][ch] * l[0] + m[5][ch] * l[1] + m[6][ch] * l[2] + m[7][ch];
    }
}

static bool within(double v, double a, double b, double tol)
{
    return v >= fmin(a, b) - tol && v <= fmax(a, b) + tol;
}

static void testLightSlots(void)
{
    RdMesh mesh = makeMesh(), skin = makeSkinMesh();
    recordHallLights(mesh, skin, 0);
    recordHallLights(mesh, skin, 1);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->lerped == 3 && st->lightPaired == 3,
          "the skinned draw, the lit mesh and the lit grid blend with their lights re-paired "
          "(lerped %u, re-paired %u)",
          st->lerped, st->lightPaired);
    const RdFrame *f = built(0.5f);
    const RdKey keys[3] = {RD_KEY(&kObjN, 0, 24), RD_KEY(&kObjN, 1, 32), RD_KEY(&kObjN, 2, 22)};
    const int same[3] = {0, 2, 1}; /* cur's slot i is prev's slot same[i] */
    const float (*p)[4] = kHallLights[0], (*c)[4] = kHallLights[1];
    for (int d = 0; d < 3; d++) {
        const float (*m)[4] = vuBlock(f, findKey(f, 0, keys[d], 0));
        if (!m) {
            CHECK(0, "the lit draw %d", d);
            continue;
        }
        int rows = 0;
        for (int i = 0; i < 3; i++) {
            for (int k = 0; k < 4; k++) {
                /* L1's row i (qw 28..31 element i) and L2's column i */
                rows += within(m[28 + k][i], p[k][same[i]], c[k][i], 1e-6) &&
                        within(m[32 + i][k], p[4 + same[i]][k], c[4 + i][k], 1e-6);
            }
        }
        CHECK(rows == 12,
              "draw %d: every light's direction and colour lies between the two ticks' values of "
              "the same light (%d of 12)",
              d, rows);
        /* the shading of normals all round stays between the two ticks' */
        double worst = 0.0;
        int out = 0;
        for (int a = 0; a < 360; a += 10) {
            for (int b = -80; b <= 80; b += 10) {
                const double ra = a * 3.14159265358979323846 / 180.0;
                const double rb = b * 3.14159265358979323846 / 180.0;
                const double n[3] = {cos(rb) * cos(ra), sin(rb), cos(rb) * sin(ra)};
                double sp[3], sc[3], so[3];
                lightShade(p, n, sp);
                lightShade(c, n, sc);
                lightShade(m + 28, n, so);
                for (int ch = 0; ch < 3; ch++) {
                    const double lo = fmin(sp[ch], sc[ch]);
                    if (!within(so[ch], sp[ch], sc[ch], 1e-4 + 1e-3 * fmax(sp[ch], sc[ch]))) {
                        out++;
                        if (lo > 0.0 && (lo - so[ch]) / lo > worst) {
                            worst = (lo - so[ch]) / lo;
                        }
                    }
                }
            }
        }
        CHECK(out == 0,
              "draw %d: the half-way shading stays between the two ticks' (%d channels out, the "
              "darkest %.1f %% under)",
              d, out, worst * 100.0);
        if (d == 0) {
            printf("rd_interp_test: hall lights at alpha 0.5: %d shaded channels outside the two "
                   "ticks' range\n",
                   out);
        }
    }
    /* recorded in the same order: nothing to re-pair */
    recordHallLights(mesh, skin, 1);
    recordHallLights(mesh, skin, 1);
    st = build(0.5f, 1);
    CHECK(st->lightPaired == 0, "lights in the same slots stay (%u)", st->lightPaired);
    rd_destroy_vu_mesh(mesh);
    rd_destroy_vu_mesh(skin);
}

/* ------------------------------------------------- same-key instances */

/* a still prelit instance at world x (S identity: the origin is qw 19) */
static void instanceAt(RdMesh mesh, float x, RdKey key)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 34;
    identity(d.vu.mem, 4);
    identity(d.vu.mem, 12);
    identity(d.vu.mem, 16);
    identity(d.vu.mem, 20);
    identity(d.vu.mem, 24);
    d.vu.mem[19][0] = d.vu.mem[23][0] = d.vu.mem[27][0] = x;
    rd_draw_vu_mesh(mesh, &d, key);
}

static void instanceFrame(RdMesh mesh, const float *xs, int n)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(5);
    for (int i = 0; i < n; i++) {
        instanceAt(mesh, xs[i], RD_KEY(&kObjR, 0, 34));
    }
    rd_end_frame(0);
}

/* the blended x of the n-th instance */
static float instanceX(const RdFrame *f, int nth)
{
    const float (*m)[4] = vuBlock(f, findKey(f, 5, RD_KEY(&kObjR, 0, 34), nth));
    return m ? m[19][0] : -1.0e9f;
}

/* The pool's ripples (hamon, list 5, key 016f5fc525f00100, prog 0 code 34)
 * in the F12 dumps frame-20261009-083502-v21878 and -083503-v21889, six
 * ticks apart: the common block's world to screen (qw 4..7) and each
 * instance's model to screen (qw 16..19).  The ripples stand still and grow;
 * in the second the oldest (world x 971.5) is gone and a new one of scale 0
 * (x 1093.8) is the last, so by ordinal each would blend from the place of
 * the one before it (12 to 55 units away). */
static const float kPoolS[2][16] = {
    {1981.80237f, 1517.66699f, -3606.70361f, 0.880751312f, 816.943115f, 1501.67151f, -1633.49951f,
     0.398898005f, -92.4732056f, 439.468201f, -1044.38696f, 0.255037636f, -818977.938f, -836864.25f,
     1.074736e+09f, -240.81897f},
    {1983.39221f, 1529.83594f, -3628.23999f, 0.886010468f, 809.374817f, 1495.43274f, -1618.36658f,
     0.395202547f, -121.36026f, 418.298096f, -992.057922f, 0.242258981f, -831610.0f, -847413.25f,
     1.07476006e+09f, -246.694855f},
};

static const float kPoolM[2][5][16] = {
    {{1874.01575f, 902.704102f, -2145.25732f, 0.523868442f, 1126.32007f, 2070.35547f, -2252.10693f,
      0.549960971f, 2003.4657f, 1996.24194f, -4744.02686f, 1.15848386f, 1844667.25f, 1969408.75f,
      1.06980672e+09f, 962.892334f},
     {2001.3844f, 1097.10242f, -2607.24097f, 0.63668412f, 1055.20361f, 1939.6322f, -2109.90771f,
      0.515236139f, 1606.77881f, 1726.98633f, -4104.14648f, 1.00222611f, 1899899.75f, 1997198.5f,
      1.06974067e+09f, 979.019531f},
     {1818.97156f, 1047.61047f, -2489.62451f, 0.60796237f, 912.326721f, 1677.00183f, -1824.22168f,
      0.445472032f, 1266.85815f, 1425.10852f, -3386.74048f, 0.827036619f, 1915560.0f, 2004579.5f,
      1.06972314e+09f, 983.302979f},
     {1005.3338f, 583.225281f, -1386.02258f, 0.338464528f, 498.67334f, 916.640991f, -997.110596f,
      0.243492842f, 678.531738f, 769.373413f, -1828.39978f, 0.446492314f, 2005382.75f, 2056413.0f,
      1.0696e+09f, 1013.38379f},
     {262.342651f, 144.094528f, -342.437622f, 0.0836227238f, 132.216156f, 243.034348f, -264.36972f,
      0.0645586699f, 215.981873f, 232.509155f, -552.553101f, 0.134932593f, 2043516.5f, 2078757.5f,
      1.06954688e+09f, 1026.35083f}},
    {{2117.1814f, 1176.86133f, -2791.10693f, 0.681583822f, 1092.07373f, 2017.7583f, -2183.63062f,
      0.533239126f, 1659.13611f, 1802.86584f, -4275.77246f, 1.04413688f, 1884069.0f, 1994665.25f,
      1.06975322e+09f, 975.963318f},
     {2022.44604f, 1179.13367f, -2796.49609f, 0.682899952f, 992.304871f, 1833.42139f, -1984.14001f,
      0.484523863f, 1357.4093f, 1545.72791f, -3665.93042f, 0.895214379f, 1900002.75f, 2002328.75f,
      1.06973504e+09f, 980.40155f},
     {1357.06116f, 796.834229f, -1889.81409f, 0.461489618f, 657.341858f, 1214.53064f, -1314.37256f,
      0.320967704f, 888.8255f, 1020.53302f, -2420.35034f, 0.591045737f, 1990854.5f, 2055398.5f,
      1.06960915e+09f, 1011.13702f},
     {773.140381f, 430.522247f, -1021.04938f, 0.249338627f, 398.079865f, 735.507996f, -795.971313f,
      0.194374934f, 597.141785f, 649.852905f, -1541.22546f, 0.376364827f, 2029406.25f, 2078254.0f,
      1.06955494e+09f, 1024.3739f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2083245.0f,
      2109330.5f, 1.06948128e+09f, 1042.37207f}},
};

static void rippleFrame(RdMesh mesh, int k)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(5);
    for (int i = 0; i < 5; i++) {
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 34;
        identity(d.vu.mem, 12);
        memcpy(d.vu.mem[4], kPoolS[k], sizeof(kPoolS[k]));
        for (int a = 16; a < 28; a += 4) {
            memcpy(d.vu.mem[a], kPoolM[k][i], sizeof(kPoolM[k][i]));
        }
        rd_draw_vu_mesh(mesh, &d, RD_KEY(&kObjR, 1, 34));
    }
    rd_end_frame(0);
}

/* a block's model origin in the world, S^-1 (qw 19), in double */
static bool worldOriginOf(const float (*m)[4], double out[3])
{
    double a[4][8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            a[r][c] = m[4 + c][r];
            a[r][4 + c] = r == c ? 1.0 : 0.0;
        }
    }
    for (int i = 0; i < 4; i++) {
        int piv = i;
        for (int r = i + 1; r < 4; r++) {
            piv = fabs(a[r][i]) > fabs(a[piv][i]) ? r : piv;
        }
        for (int c = 0; c < 8; c++) {
            const double t = a[i][c];
            a[i][c] = a[piv][c];
            a[piv][c] = t;
        }
        if (!(fabs(a[i][i]) > 1e-30)) {
            return false;
        }
        const double d = a[i][i];
        for (int c = 0; c < 8; c++) {
            a[i][c] /= d;
        }
        for (int r = 0; r < 4; r++) {
            if (r != i) {
                const double f = a[r][i];
                for (int c = 0; c < 8; c++) {
                    a[r][c] -= f * a[i][c];
                }
            }
        }
    }
    double w[4];
    for (int r = 0; r < 4; r++) {
        w[r] = 0.0;
        for (int k = 0; k < 4; k++) {
            w[r] += a[r][4 + k] * m[19][k];
        }
    }
    for (int j = 0; j < 3; j++) {
        out[j] = w[j] / w[3];
    }
    return fabs(w[3]) > 1e-12;
}

static void testInstances(void)
{
    RdMesh mesh = makeMesh();
    /* three still instances, the first gone in the tick: each blends with
     * itself (by ordinal, B from A's place and C from B's) */
    const float abc[3] = {0.0f, 40.0f, 80.0f}, bc[2] = {40.0f, 80.0f};
    instanceFrame(mesh, abc, 3);
    instanceFrame(mesh, bc, 2);
    const RdInterpStats *st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    CHECK(st->lerped == 2 && st->placed == 2 && instanceX(f, 0) == 40.0f &&
              instanceX(f, 1) == 80.0f,
          "a still instance gone: the others blend with themselves (x %.2f %.2f, ordinal 20 60; "
          "lerped %u, placed %u)",
          (double)instanceX(f, 0), (double)instanceX(f, 1), st->lerped, st->placed);
    /* the first gone and a new one at 120 in the same tick: the two are
     * paired (no draw unmatched beside a free one of its key), and as they
     * are not each other's nearest (80 is nearer to 120) the new one is the
     * tick's, not blended from the one that went */
    const float bcd[3] = {40.0f, 80.0f, 120.0f};
    instanceFrame(mesh, abc, 3);
    instanceFrame(mesh, bcd, 3);
    st = build(0.5f, 1);
    f = built(0.5f);
    CHECK(st->lerped == 2 && st->missing == 0 && st->apart == 1 && st->jump == 1 &&
              instanceX(f, 0) == 40.0f && instanceX(f, 1) == 80.0f && instanceX(f, 2) == 120.0f,
          "one gone, one new: paired apart, the new one is the tick's (x %.2f %.2f %.2f; lerped "
          "%u, unmatched %u, apart %u)",
          (double)instanceX(f, 0), (double)instanceX(f, 1), (double)instanceX(f, 2), st->lerped,
          st->missing, st->apart);
    /* moving instances (none at a place of the tick before): by ordinal */
    const float ab[2] = {0.0f, 40.0f}, ab2[2] = {10.0f, 50.0f};
    instanceFrame(mesh, ab, 2);
    instanceFrame(mesh, ab2, 2);
    st = build(0.5f, 1);
    f = built(0.5f);
    CHECK(st->lerped == 2 && st->placed == 0 && fabsf(instanceX(f, 0) - 5.0f) < 1e-4f &&
              fabsf(instanceX(f, 1) - 45.0f) < 1e-4f,
          "moving instances pair by ordinal (x %.2f %.2f, placed %u)", (double)instanceX(f, 0),
          (double)instanceX(f, 1), st->placed);

    /* the pool's ripples, the two dumps' blocks as two ticks */
    rippleFrame(mesh, 0);
    rippleFrame(mesh, 1);
    st = build(0.5f, 1);
    f = built(0.5f);
    const RdFrame *cur = rd__last_frame();
    CHECK(st->lerped == 4 && st->missing == 0 && st->apart == 1,
          "ripples: 4 blend, the new one is paired apart and the tick's (%u, %u, %u)", st->lerped,
          st->missing, st->apart);
    for (int i = 0; i < 5; i++) {
        const float (*m)[4] = vuBlock(f, findKey(f, 5, RD_KEY(&kObjR, 1, 34), i));
        const float (*mc)[4] = vuBlock(cur, findKey(cur, 5, RD_KEY(&kObjR, 1, 34), i));
        double o[3], oc[3];
        if (!m || !mc || !worldOriginOf(m, o) || !worldOriginOf(mc, oc)) {
            CHECK(0, "ripple %d", i);
            continue;
        }
        const double dx = o[0] - oc[0], dy = o[1] - oc[1], dz = o[2] - oc[2];
        const double off = sqrt(dx * dx + dy * dy + dz * dz);
        CHECK(off < 0.5, "ripple %d stays at its place (%.2f %.2f %.2f, %.3f units off)", i, o[0],
              o[1], o[2], off);
    }
    rd_destroy_vu_mesh(mesh);
}

/* ------------------------------------------- wading in shallow water */

/* The wading splash of an issue report's F12 dump (shallow water, v0.4.5:
 * frame-20261009-210725-v10576), list 5, prog 0 code 34: the common block
 * (qw 4..15) and each draw's model matrices (qw 16..27).  Each step enters
 * a stage animation (frameDependSequence.c, EntryStageMultiBgaManager) of
 * four hamon parts and two splash_mini parts, each still in the world, and
 * two splash_shibuki parts (the spray), whose height changes with its age.
 * kWadeRipple: the seven draws of hamon part 1 (key 02c68798d4a00100,
 * 5:83 the oldest .. 5:516 the newest), then hamon part 2 of the newest
 * step (5:527), the newcomer standing at the other foot.  kWadeSpray: the
 * two draws of splash_shibuki part 0 (5:481 older, 5:564 newer). */
static const float kWadeCommon[48] = {
    -1121.67017f,  -1244.37891f,  2848.18701f,  -0.695522785f, 869.721313f,
    1369.32825f,   -1739.03101f,  0.42466861f,  1550.11084f,   1036.69629f,
    -2372.83423f,  0.579442382f,  2129370.5f,   2169264.5f,    1.06853274e+09f,
    1274.01587f,   1499.99988f,   0.0f,         0.0f,          0.0f,
    0.0f,          1749.99963f,   0.0f,         0.0f,          0.0f,
    0.0f,          -268435424.0f, 0.0f,         2048.0f,       2048.0f,
    268435440.0f,  1.0f,          0.640049934f, 0.0f,          0.768271923f,
    0.0f,          0.326260954f,  0.905308127f, -0.271809101f, 0.0f,
    -0.695522785f, 0.42466861f,   0.579442382f, 0.0f,          1795.4209f,
    180.634338f,   -175.596252f,  1.0f};
static const float kWadeRipple[8][48] = {
    {347.324036f,   719.612122f,  -1647.07849f,   0.40221402f,   1199.08533f,   1887.8938f,
     -2397.60327f,  0.585490882f, -2097.0f,       -1646.61633f,  3768.84497f,   -0.920346022f,
     705930.5f,     685821.375f,  1.0724569e+09f, 315.736511f,   -0.317606807f, -0.0594983548f,
     0.40222013f,   0.40221402f,  0.0f,           0.393604845f,  0.585499823f,  0.585490882f,
     -0.14142096f,  0.136144131f, -0.920360029f,  -0.920346022f, 39.5348511f,   22.3959351f,
     311.741211f,   315.736511f,  -1.00715363f,   -0.188673511f, 0.40221402f,   0.0f,
     0.0f,          1.24814892f,  0.585490882f,   0.0f,          -0.4484559f,   0.431722701f,
     -0.920346022f, 0.0f,         125.367798f,    71.019104f,    315.736511f,   1.0f}, /* 5:83 */
    {-1782.68713f,  -1301.0415f,   2977.87866f,   -0.727193296f, 1103.1875f,
     1736.90784f,   -2205.85278f,  0.538665771f,  -770.812256f,  -1005.64978f,
     2301.77368f,   -0.562089503f, 834080.25f,    784713.375f,   1.07223053e+09f,
     371.010376f,   -0.195596889f, 0.107571602f,  -0.727204382f, -0.727193296f,
     0.0f,          0.362125963f,  0.538673997f,  0.538665771f,  0.253564626f,
     0.0831482708f, -0.562098026f, -0.562089503f, 49.5006714f,   14.2194214f,
     367.016052f,   371.010376f,   -0.620251596f, 0.341117173f,  -0.727193296f,
     0.0f,          0.0f,          1.14832711f,   0.538665771f,  0.0f,
     0.804071486f,  0.263669074f,  -0.562089503f, 0.0f,          156.969971f,
     45.0908203f,   371.010376f,   1.0f}, /* 5:153 */
    {-1609.94812f,  -1333.26379f,   3051.63037f,   -0.745203316f, 916.755798f,
     1443.38147f,   -1833.07776f,   0.447634667f,  61.5209961f,   -289.200989f,
     661.935425f,   -0.161643624f,  949634.0f,     899977.625f,   1.07196672e+09f,
     435.435303f,   -0.0558477864f, 0.11023578f,   -0.745214701f, -0.745203316f,
     0.0f,          0.30092898f,    0.447641492f,  0.447634667f,  0.261711359f,
     0.0239114687f, -0.161646098f,  -0.161643624f, 38.5751343f,   4.6892395f,
     431.441833f,   435.435303f,    -0.177097321f, 0.349565476f,  -0.745203316f,
     0.0f,          0.0f,           0.954267144f,  0.447634667f,  0.0f,
     0.829905331f,  0.075824976f,   -0.161643624f, 0.0f,          122.324585f,
     14.8699951f,   435.435303f,    1.0f}, /* 5:223 */
    {-1509.2395f,   -1263.84436f,   2892.74048f,   -0.706402659f, 860.446533f,
     1354.72559f,   -1720.48584f,   0.420139909f,  124.900818f,   -215.625793f,
     493.533569f,   -0.120520145f,  975835.625f,   929564.75f,    1.07189901e+09f,
     451.972473f,   -0.0416845679f, 0.104496107f,  -0.706413448f, -0.706402659f,
     0.0f,          0.282445222f,   0.420146316f,  0.420139909f,  0.247817338f,
     0.0178281926f, -0.120521963f,  -0.120520145f, 33.4640503f,   2.24293518f,
     447.979248f,   451.972473f,    -0.132184744f, 0.331364572f,  -0.706402659f,
     0.0f,          0.0f,           0.895653844f,  0.420139909f,  0.0f,
     0.785846353f,  0.0565344691f,  -0.120520145f, 0.0f,          106.116821f,
     7.11254883f,   451.972473f,    1.0f}, /* 5:293 */
    {-903.861755f,   -852.6521f,     1951.58618f,   -0.476574272f, 593.585571f,
     934.567749f,    -1186.89014f,   0.289836705f,  531.089417f,   246.73703f,
     -564.742065f,   0.137909099f,   1026227.0f,    1002995.5f,    1.07173094e+09f,
     493.015198f,    0.0481081903f,  0.0704982504f, -0.476581514f, -0.476574272f,
     0.0f,           0.194846973f,   0.289841115f,  0.289836705f,  0.165767685f,
     -0.0204005018f, 0.137911215f,   0.137909099f,  11.0213013f,   -3.82836914f,
     489.022583f,    493.015198f,    0.152554482f,  0.223554969f,  -0.476574272f,
     0.0f,           0.0f,           0.617873609f,  0.289836705f,  0.0f,
     0.525661051f,   -0.0646914169f, 0.137909099f,  0.0f,          34.9493408f,
     -12.1400757f,   493.015198f,    1.0f}, /* 5:363 */
    {78.5832901f,
     35.6086693f,
     -81.5026398f,
     0.019902816f,
     85.4004517f,
     134.458298f,
     -170.760483f,
     0.0416994393f,
     143.676834f,
     134.984589f,
     -308.958435f,
     0.0754471645f,
     908974.0f,
     924762.0f,
     1.07191002e+09f,
     449.288025f,
     0.0252148826f,
     -0.00294416607f,
     0.0199031197f,
     0.019902816f,
     0.0f,
     0.0280330591f,
     0.0417000726f,
     0.0416994393f,
     -0.00722596329f,
     -0.0111606801f,
     0.0754483119f,
     0.0754471645f,
     -7.44515991f,
     2.64006042f,
     445.294739f,
     449.288025f,
     0.0799581856f,
     -0.00933615956f,
     0.019902816f,
     0.0f,
     0.0f,
     0.0888948217f,
     0.0416994393f,
     0.0f,
     -0.0229140408f,
     -0.0353913084f,
     0.0754471645f,
     0.0f,
     -23.6090698f,
     8.37176514f,
     449.288025f,
     1.0f}, /* 5:433 */
    {0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        864130.875f, 890281.125f,  1.07198893e+09f,
     430.015564f, 0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        -11.0271912f, 5.4909668f,
     426.022095f, 430.015564f, 0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         -34.9679565f,
     17.4122314f, 430.015564f, 1.0f}, /* 5:516 */
    {0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        767149.375f, 807323.125f,  1.07217882e+09f,
     383.647766f, 0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        -12.3739624f, 12.3500366f,
     379.653503f, 383.647766f, 0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         0.0f,
     0.0f,        0.0f,        0.0f,        0.0f,         -39.2387695f,
     39.1628418f, 383.647766f, 1.0f}, /* 5:527 */
};
static const float kWadeSpray[2][48] = {
    {318.628021f,   0.0f,         0.0f,          0.0f,           0.0f,
     371.7789f,     0.0f,         0.0f,          1384.34912f,    1384.34912f,
     -2768.04297f,  0.675951719f, 901064.0f,     913585.875f,    1.07192422e+09f,
     445.817566f,   0.212418661f, 0.0f,          0.0f,           0.0f,
     0.0f,          0.21244511f,  0.0f,          0.0f,           0.0f,
     0.0f,          0.675962031f, 0.675951719f,  -7.98022127f,   0.315177649f,
     441.82431f,    445.817566f,  -0.456220448f, -0.0615602806f, 0.131234139f,
     0.0f,          0.0f,         0.235959664f,  0.110685699f,   0.0f,
     -0.145000711f, 0.193795905f, -0.413133919f, 0.0f,           -39.9914551f,
     30.4671021f,   405.1521f,    1.0f}, /* 5:481 */
    {236.521667f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     275.984772f,
     0.0f,
     0.0f,
     1039.68848f,
     1039.68848f,
     -2078.88477f,
     0.507660389f,
     853293.438f,
     874987.625f,
     1.07200902e+09f,
     425.103516f,
     0.157681093f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     0.157705605f,
     0.0f,
     0.0f,
     0.0f,
     0.0f,
     0.507668078f,
     0.507660389f,
     -11.5456839f,
     2.50036454f,
     421.109955f,
     425.103516f,
     -0.128535137f,
     -0.00351484679f,
     0.00749294832f,
     0.0f,
     0.0f,
     0.153612673f,
     0.0720577687f,
     0.0f,
     -0.0082766749f,
     0.0545847788f,
     -0.116363779f,
     0.0f,
     -38.9780273f,
     40.565979f,
     383.623474f,
     1.0f}, /* 5:564 */
};

static const char kObjW, kObjWs;

/* one wading draw; dy moves it dy world units along y (qw 19 + dy S's y
 * column: the only place the pairing reads) */
static void wadeDraw(RdMesh mesh, const float *inst, float dy, RdKey key)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 34;
    memcpy(d.vu.mem[4], kWadeCommon, sizeof(kWadeCommon));
    memcpy(d.vu.mem[16], inst, 48 * sizeof(float));
    for (int k = 0; k < 4; k++) {
        d.vu.mem[19][k] += dy * kWadeCommon[4 + k];
    }
    rd_draw_vu_mesh(mesh, &d, key);
}

/* ripples first .. first + n - 1, then the newcomer when asked */
static void wadeRipples(RdMesh mesh, int first, int n, int newcomer)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(5);
    for (int i = first; i < first + n; i++) {
        wadeDraw(mesh, kWadeRipple[i], 0.0f, RD_KEY(&kObjW, 1, 34));
    }
    if (newcomer) {
        wadeDraw(mesh, kWadeRipple[7], 0.0f, RD_KEY(&kObjW, 1, 34));
    }
    rd_end_frame(0);
}

/* the spray draws picked by which (0 older, 1 newer, 2 a newcomer: the
 * newest step's other foot), each lifted by dy */
static void wadeSpray(RdMesh mesh, const int *which, int n, float dy)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(5);
    for (int i = 0; i < n; i++) {
        wadeDraw(mesh, which[i] < 2 ? kWadeSpray[which[i]] : kWadeRipple[7],
                 which[i] < 2 ? dy : 0.0f, RD_KEY(&kObjWs, 0, 34));
    }
    rd_end_frame(0);
}

static double dist3d(const double *a, const double *b)
{
    const double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

/* the origin of the n-th draw of key k in list 5 of f */
static bool wadeOrigin(const RdFrame *f, RdKey k, int nth, double out[3])
{
    const float (*m)[4] = vuBlock(f, findKey(f, 5, k, nth));
    return m && worldOriginOf(m, out);
}

static void testWading(void)
{
    RdMesh mesh = makeMesh();
    const RdKey kr = RD_KEY(&kObjW, 1, 34), ks = RD_KEY(&kObjWs, 0, 34);

    /* the ripples: the oldest gone and a new one in the tick; by ordinal
     * each would blend from the place of the one before it */
    wadeRipples(mesh, 0, 7, 0);
    const RdFrame *prevF = rd__last_frame();
    double gap = 1.0e9;
    for (int i = 0; i + 1 < 7; i++) {
        double a[3], b[3];
        if (wadeOrigin(prevF, kr, i, a) && wadeOrigin(prevF, kr, i + 1, b)) {
            gap = fmin(gap, dist3d(a, b));
        }
    }
    CHECK(gap > 10.0 && gap < 300.0,
          "the dump's ripples stand %.1f or more units apart, under the jump limit", gap);
    wadeRipples(mesh, 1, 6, 1);
    const RdInterpStats *st = build(0.5f, 1);
    const RdFrame *f = built(0.5f);
    const RdFrame *cur = rd__last_frame();
    CHECK(st->lerped == 6 && st->missing == 0 && st->apart == 1,
          "wading ripples: the 6 that stay blend, the new one is paired apart and the tick's (%u, "
          "%u, %u)",
          st->lerped, st->missing, st->apart);
    for (int i = 0; i < 7; i++) {
        double o[3], oc[3];
        if (!wadeOrigin(f, kr, i, o) || !wadeOrigin(cur, kr, i, oc)) {
            CHECK(0, "wading ripple %d", i);
            continue;
        }
        CHECK(dist3d(o, oc) < 0.5, "wading ripple %d stays at its place (%.2f %.2f %.2f, %.3f off)",
              i, o[0], o[1], o[2], dist3d(o, oc));
    }
    const float (*mn)[4] = vuBlock(f, findKey(f, 5, kr, 6));
    const float (*mc)[4] = vuBlock(cur, findKey(cur, 5, kr, 6));
    CHECK(mn && mc && memcmp(mn[16], mc[16], 12 * 16) == 0,
          "the new ripple is the tick's draw, not blended");

    /* the spray rises (a 1 unit step: its height changes with its age, the
     * rate is not in one dump): no draw stays at its place, so the pairing
     * keeps their order and leaves out the draws that keep the pairs
     * nearest */
    double older[3], newer[3], newer2[3];
    {
        const int both[2] = {0, 1}, one[1] = {1};
        wadeSpray(mesh, both, 2, 0.0f);
        const bool okP = wadeOrigin(rd__last_frame(), ks, 0, older) &&
                         wadeOrigin(rd__last_frame(), ks, 1, newer);
        wadeSpray(mesh, one, 1, -1.0f);
        const bool okC = wadeOrigin(rd__last_frame(), ks, 0, newer2);
        st = build(0.5f, 1);
        f = built(0.5f);
        double o[3];
        const bool okB = wadeOrigin(f, ks, 0, o);
        const double mid[3] = {(newer[0] + newer2[0]) * 0.5, (newer[1] + newer2[1]) * 0.5,
                               (newer[2] + newer2[2]) * 0.5};
        CHECK(okP && okC && okB && st->lerped == 1 && dist3d(o, mid) < 0.05 &&
                  dist3d(older, newer) > 10.0,
              "the older spray gone: the newer blends with itself (%.3f from its half way; the "
              "older stood %.1f away; lerped %u)",
              okB ? dist3d(o, mid) : -1.0, dist3d(older, newer), st->lerped);
    }
    /* a new spray drawn first (the ring of 30 wrapped) and the two rising */
    {
        const int both[2] = {0, 1}, three[3] = {2, 0, 1};
        wadeSpray(mesh, both, 2, 0.0f);
        wadeSpray(mesh, three, 3, -1.0f);
        st = build(0.5f, 1);
        f = built(0.5f);
        cur = rd__last_frame();
        int ok = 0;
        for (int i = 1; i < 3; i++) {
            double o[3], oc[3];
            ok += wadeOrigin(f, ks, i, o) && wadeOrigin(cur, ks, i, oc) && dist3d(o, oc) < 0.51 &&
                  dist3d(o, oc) > 0.49;
        }
        const float (*a)[4] = vuBlock(f, findKey(f, 5, ks, 0));
        const float (*b)[4] = vuBlock(cur, findKey(cur, 5, ks, 0));
        CHECK(st->lerped == 2 && st->missing == 1 && ok == 2 && a && b &&
                  memcmp(a[16], b[16], 12 * 16) == 0,
              "a new spray drawn first is the tick's, the two rising blend with themselves "
              "(lerped %u, unmatched %u, %d of 2 half a unit from the tick's)",
              st->lerped, st->missing, ok);
    }
    rd_destroy_vu_mesh(mesh);
}

/* -------------------------------------------- screen-space grid STs */

/* two unlit grids, one sampling WORK1 (the pool's surface sampling the
 * scene's copy), one an image; their STs u0 + v * 0.1 */
static void gridStFrame(RdTex image, float u0)
{
    rd_begin_frame();
    frameHead();
    static float g[3 * 2 + 4][4];
    memset(g, 0, sizeof(g));
    g[1][0] = 7.0f;
    g[2][0] = 128.0f;
    for (int v = 0; v < 3; v++) {
        g[3 + v * 2][0] = (float)(v * 10);
        g[3 + v * 2][3] = 1.0f;
        g[4 + v * 2][0] = u0 + (float)v * 0.1f;
        g[4 + v * 2][1] = u0;
        g[4 + v * 2][2] = 1.0f;
    }
    RdVuGridDraw gd;
    memset(&gd, 0, sizeof(gd));
    gd.qw = (const float (*)[4])g;
    gd.strips = 1;
    gd.stripLen = 3;
    gd.code = 20;
    identity(gd.vu.mem, 4);
    identity(gd.vu.mem, 16);
    rd_select_list(4);
    rd_texture(rd_target_texture(rd_target(RD_TARGET_WORK1), RD_VIEW_RGBA), RD_TEXFN_MODULATE,
               RD_TCC_RGBA);
    rd_draw_vu_grid(&gd, RD_KEY(&kObjG, 0, 20));
    rd_texture(image, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_draw_vu_grid(&gd, RD_KEY(&kObjG, 1, 20));
    rd_texture_off();
    rd_end_frame(0);
}

static const float (*gridStream(const RdFrame *f, RdKey k))[4]
{
    const RdCmd *c = findKey(f, 4, k, 0);
    return c ? (const float (*)[4])(const void *)(f->payload + c->u[1] + sizeof(RdVuPayload) +
                                                  sizeof(RdVuBlock))
             : NULL;
}

static void testGridScreenSt(void)
{
    static const uint8_t px[4 * 4 * 4] = {0};
    RdTex image = rd_create_texture(4, 4, px, RD_TEXA_7F_81_AEM, "grid image");
    gridStFrame(image, 0.25f);
    gridStFrame(image, 0.75f);
    const RdInterpStats *st = build(0.5f, 1);
    CHECK(st->lerped == 2 && st->gridSt == 1, "both grids blend, one with its STs (%u, %u)",
          st->lerped, st->gridSt);
    const RdFrame *f = built(0.5f);
    const float (*a)[4] = gridStream(f, RD_KEY(&kObjG, 0, 20));
    const float (*b)[4] = gridStream(f, RD_KEY(&kObjG, 1, 20));
    if (a && b) {
        int mid = 0, held = 0;
        for (int v = 0; v < 3; v++) {
            mid += fabsf(a[4 + v * 2][0] - (0.5f + (float)v * 0.1f)) < 1e-6f &&
                   fabsf(a[4 + v * 2][1] - 0.5f) < 1e-6f;
            held += b[4 + v * 2][0] == 0.75f + (float)v * 0.1f && b[4 + v * 2][1] == 0.75f;
        }
        CHECK(mid == 3, "the grid sampling a target: STs half way (%d of 3; s %.4f)", mid,
              (double)a[4][0]);
        CHECK(held == 3, "the grid with an image keeps the tick's STs (%d of 3; s %.4f)", held,
              (double)b[4][0]);
    } else {
        CHECK(0, "the two grids");
    }
    rd_destroy_texture(image);
}

/* ------------------------------------------ every draw paired or kept */

/* an emitter's n batches of 2 particles in list 6, keyed by the emitter
 * (MicroCode.c: one key for all its batches), moved dx */
static void batchFrame(int n, float dx)
{
    rd_begin_frame();
    frameHead();
    rd_select_list(6);
    for (int b = 0; b < n; b++) {
        particleBatch(&kObjP1, 2, (float)(b * 10) + dx, 1);
    }
    rd_end_frame(0);
}

static int typeCount(const RdFrame *f, int l, uint8_t type)
{
    int n = 0;
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        n += f->lists[l].cmds[i].type == type;
    }
    return n;
}

static const char kObjAl;

/* the s6 camera turned deg; mesh A (W = I) and, when b is not NULL, mesh B
 * (key part 2) with its origin at world b; with bOther, a draw of B's
 * object and part under another ordinal byte at A's place */
static void aloneFrame(RdMesh mesh, double deg, const double *b, int bOther)
{
    rd_begin_frame();
    frameHead();
    double eye[3], v[16], p[16], w[16];
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
    rd_set_camera(&cam);
    s6Translate(w, 0.0, 0.0, 0.0);
    s6Draw(mesh, v, w, RD_KEY(&kObjAl, 0, 32));
    if (b) {
        s6Translate(w, b[0], b[1], b[2]);
        s6Draw(mesh, v, w, RD_KEY(&kObjAl, 2, 32));
    }
    if (bOther) {
        s6Translate(w, 0.0, 0.0, 0.0);
        s6Draw(mesh, v, w, RD_KEY(&kObjAl, 2, 33));
    }
    rd_end_frame(0);
}

/* where the blended camera at t of the turn 0 -> 28 degrees (the rotation
 * slerped: the yaw 28 t; the eye on the line between the ticks' eyes) puts
 * the world point x */
static void aloneExpected(double t, const double x[3], double out[2])
{
    double e0[3], e1[3], eye[3], v[16], p[16], s[16], w[16], m[16];
    s6OrbitEye(0.0, e0);
    s6OrbitEye(28.0, e1);
    for (int k = 0; k < 3; k++) {
        eye[k] = (1.0 - t) * e0[k] + t * e1[k];
    }
    s6View(28.0 * t, eye, v);
    s6Proj(p);
    mul4(p, v, s);
    s6Translate(w, x[0], x[1], x[2]);
    mul4(s, w, m);
    s6Project(m, kS6PointB, out);
}

/* a frame's lists and payload, for a byte comparison */
typedef struct FrameCopy {
    uint32_t count[RD_LIST_COUNT];
    RdCmd *cmds[RD_LIST_COUNT];
    uint8_t *payload;
    uint32_t payloadSize;
} FrameCopy;

static void frameCopy(const RdFrame *f, FrameCopy *c)
{
    memset(c, 0, sizeof(*c));
    for (int l = 0; f && l < RD_LIST_COUNT; l++) {
        c->count[l] = f->lists[l].count;
        c->cmds[l] = malloc((size_t)f->lists[l].count * sizeof(RdCmd) + 1);
        if (c->cmds[l] && f->lists[l].count) {
            memcpy(c->cmds[l], f->lists[l].cmds, (size_t)f->lists[l].count * sizeof(RdCmd));
        }
    }
    if (f) {
        c->payloadSize = f->payloadSize;
        c->payload = malloc((size_t)f->payloadSize + 1);
        if (c->payload && f->payloadSize) {
            memcpy(c->payload, f->payload, f->payloadSize);
        }
    }
}

static bool frameSame(const FrameCopy *c, const RdFrame *f)
{
    if (!f || !c->payload || c->payloadSize != f->payloadSize ||
        memcmp(c->payload, f->payload, f->payloadSize) != 0) {
        return false;
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        if (!c->cmds[l] || c->count[l] != f->lists[l].count ||
            (c->count[l] &&
             memcmp(c->cmds[l], f->lists[l].cmds, (size_t)c->count[l] * sizeof(RdCmd)) != 0)) {
            return false;
        }
    }
    return true;
}

static void frameCopyFree(FrameCopy *c)
{
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        free(c->cmds[l]);
    }
    free(c->payload);
    memset(c, 0, sizeof(*c));
}

static void testPairedOrKept(void)
{
    RdMesh mesh = makeMesh();
    RdInterpStats st;
    const RdFrame *f;

    /* (a) three instances under one key, two still and the third moved 10
     * units: its origins fail sameOrigin, yet the two are each other's
     * nearest and pair (before: the moved one unmatched, the one it was
     * free) */
    {
        const float before[3] = {0.0f, 40.0f, 80.0f}, after[3] = {0.0f, 40.0f, 90.0f};
        instanceFrame(mesh, before, 3);
        instanceFrame(mesh, after, 3);
        f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
        uint32_t why = 0;
        for (int k = 0; k < RD_UNMATCHED_COUNT; k++) {
            why += st.unmatchedWhy[k];
        }
        CHECK(f && st.lerped == 3 && st.missing == 0 && st.apart == 0 && why == 0 &&
                  fabsf(instanceX(f, 2) - 85.0f) < 1e-3f && instanceX(f, 0) == 0.0f &&
                  instanceX(f, 1) == 40.0f,
              "a moved instance beside still ones pairs with its own (x %.3f, want 85; lerped "
              "%u, unmatched %u, apart %u)",
              f ? (double)instanceX(f, 2) : -1.0, st.lerped, st.missing, st.apart);
    }

    /* (b) an emitter's batch count changes: every batch of cur is paired or
     * drawn as the tick's, and the reason of an unmatched one is the count */
    batchFrame(2, 0.0f);
    batchFrame(3, 1.0f);
    f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
    CHECK(f && st.lerped == 2 && st.missing == 1 && st.unmatchedWhy[RD_UNMATCHED_FEWER] == 1 &&
              st.unmatchedWhy[RD_UNMATCHED_UNPLACED] == 0 && typeCount(f, 6, RDC_PARTICLES) == 3,
          "batches 2 -> 3: two blend, the third is the tick's for its count (lerped %u, "
          "unmatched %u, fewer %u, drawn %d)",
          st.lerped, st.missing, st.unmatchedWhy[RD_UNMATCHED_FEWER],
          f ? typeCount(f, 6, RDC_PARTICLES) : -1);
    batchFrame(3, 0.0f);
    batchFrame(2, 1.0f);
    f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
    CHECK(f && st.lerped == 2 && st.missing == 0 && st.prevKept == 0 &&
              typeCount(f, 6, RDC_PARTICLES) == 2,
          "batches 3 -> 2: both blend, the key is drawn in cur so nothing of the tick before is "
          "added (lerped %u, unmatched %u, kept %u, drawn %d)",
          st.lerped, st.missing, st.prevKept, f ? typeCount(f, 6, RDC_PARTICLES) : -1);

    /* (c) the camera turns 28 degrees and leaves B (at x 250, z 100: GS x
     * 2256 at 0 degrees, 2332 at 28, past the picture's 2304) behind: B is
     * of prev alone, outside cur's picture, and is drawn through the
     * blended camera at every t */
    const RdKey kb = RD_KEY(&kObjAl, 2, 32);
    const double bOut[3] = {250.0, 0.0, 100.0}, bIn[3] = {-200.0, 0.0, 150.0};
    aloneFrame(mesh, 0.0, bOut, 0);
    aloneFrame(mesh, 28.0, NULL, 0);
    for (int i = 1; i <= 3; i++) {
        const double t = 0.25 * i;
        f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), (float)t, 1, &st);
        const RdCmd *c = f ? findKey(f, 0, kb, 0) : NULL;
        const RdCmd *a = f ? findKey(f, 0, RD_KEY(&kObjAl, 0, 32), 0) : NULL;
        const float (*m)[4] = c ? vuBlock(f, c) : NULL;
        double got[2] = {0.0, 0.0}, want[2];
        if (m) {
            s6ProjectF(m, kS6PointB, got);
        }
        aloneExpected(t, bOut, want);
        CHECK(m && a && c > a && st.prevKept == 1 && st.prevHeld == 0 &&
                  drawsOf(f, 0, kb, RDC_MESH) == 1 &&
                  hypot(got[0] - want[0], got[1] - want[1]) < 0.01,
              "t %.2f: B of the tick before alone, outside the picture, drawn after A at %.3f "
              "%.3f (want %.3f %.3f; kept %u, held %u)",
              t, got[0], got[1], want[0], want[1], st.prevKept, st.prevHeld);
    }

    /* (d) B still inside cur's picture (x -200, z 150: GS x 1975 at 28
     * degrees): the game dropped it for another reason, it is not drawn;
     * nor is B outside the picture while cur draws its object and part
     * under another ordinal byte */
    aloneFrame(mesh, 0.0, bIn, 0);
    aloneFrame(mesh, 28.0, NULL, 0);
    f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
    CHECK(f && st.prevKept == 0 && drawsOf(f, 0, kb, RDC_MESH) == 0,
          "B of the tick before alone inside the picture is not drawn (kept %u, drawn %d)",
          st.prevKept, f ? drawsOf(f, 0, kb, RDC_MESH) : -1);
    aloneFrame(mesh, 0.0, bOut, 0);
    aloneFrame(mesh, 28.0, NULL, 1);
    f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
    CHECK(f && st.prevKept == 0 && drawsOf(f, 0, kb, RDC_MESH) == 0,
          "B of the tick before is not drawn beside its object and part (kept %u, drawn %d)",
          st.prevKept, f ? drawsOf(f, 0, kb, RDC_MESH) : -1);

    /* (e) a still camera: B of the tick before alone (x 350, z -100: past
     * the picture's right edge at 10 degrees) changes nothing; the frame is
     * byte for byte the one built without B */
    {
        const double bFar[3] = {350.0, 0.0, -100.0};
        FrameCopy ref;
        aloneFrame(mesh, 10.0, NULL, 0);
        aloneFrame(mesh, 10.0, NULL, 0);
        frameCopy(rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st), &ref);
        aloneFrame(mesh, 10.0, bFar, 0);
        aloneFrame(mesh, 10.0, NULL, 0);
        f = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.5f, 1, &st);
        CHECK(frameSame(&ref, f) && st.prevKept == 0 && st.prevHeld == 0,
              "still camera: the unpaired draw of the tick before changes nothing (kept %u, held "
              "%u)",
              st.prevKept, st.prevHeld);
        frameCopyFree(&ref);
    }
    rd_destroy_vu_mesh(mesh);
}

static void runCpu(void)
{
    testRotationBlend();
    testRotationDraws();
    testRisingBone();
    testCameraBlend();
    testCameraLocked();
    testPhoto();
    testPresentClock();
    testSprites();
    testSpriteSnaps();
    testFrameSnaps();
    testVu();
    testPrisms();
    testFade();
    testFeedback();
    testText();
    testDeferredText();
    testMorph();
    testUnmatched();
    testParticleOrder();
    testLightTurn();
    testMorphLimits();
    testShineFollows();
    testParticleSwap();
    testSineScroll();
    testLightSlots();
    testInstances();
    testWading();
    testGridScreenSt();
    testPairedOrKept();
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    if (!rd__init_record_only(512, 512)) {
        printf("rd_interp_test: no context\n");
        return 1;
    }
    rd__set_not_implemented_fatal(false);
    runCpu();
    rd_shutdown();

    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    if (!rd_init(512, 512, &s, NULL)) {
        printf("rd_interp_test: no usable device: the pixel and present cases skipped\n");
    } else {
        rd__set_not_implemented_fatal(false);
        runCpu(); /* again, with the frames replayed as they close */
        testPixels();
        testMiragePresents();
        testPresent();
        CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
        const uint32_t verr = rhi_vk_validation_error_count();
        CHECK(verr == 0, "%u validation errors", verr);
        rd_shutdown();
    }
    if (failures) {
        printf("rd_interp_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_interp_test: ok\n");
    return 0;
}
