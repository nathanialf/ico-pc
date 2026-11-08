/* rd_velocity_test.c: the scene's motion vectors (rd_velocity.h,
 * docs/port/RENDER.md "Velocity").
 *
 * Without a device (recording only), the source rule on synthetic pairs:
 *   source    a prelit mesh moved 8 GS pixels in x between two ticks
 *             (interpolation off): the second tick's draw carries a
 *             VuPrevCB, the first tick's VU block, and its vertex 0 lands
 *             8 GS pixels to the left through it (the expected delta, -8 in
 *             x, "current to previous"); a draw of a key the tick before did
 *             not draw has none; a particle batch has none; the stats say so
 *   cut       a frame after rd_camera_cut starts a new history: no VuPrevCB,
 *             the stats' reset
 *   again     a frame already attached given again with nothing to pair
 *             (after rd__velocity_forget): every VU draw's RD_VU_PREV is
 *             cleared, the tick cameras' bit kept
 *   off       the switch off: a recorded frame's VU draws keep u[3] 0
 *   interp    two presents between the ticks (alpha 0.25 then 0.75): the
 *             second's VuPrevCB is the first's blended block (x 1794), its
 *             own block x 1798: the motion between the presents
 * On a device (skipped without one):
 *   pixels    the moving mesh into SCENE with depth writes at Original 1x:
 *             VELOCITY at a pixel inside it is (-8, 0) (w 1), outside it 0
 *             (w 0), on a mesh of the second tick only 0 (w 0); SCENE is
 *             the same bytes with the switch off; the frame is dumped as
 *             rd_velocity_moving.rddump for rd_replay_tool_velocity
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_velocity.h"

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

static const char kMoving, kNew;

static const uint8_t kGrey[4] = {0x40, 0x40, 0x40, 0x80};

/* the moving triangle (model space: GS pixels from SCENE's top-left), and
 * the second tick's own one */
static const float kTri[3][4] = {{60.0f, 50.0f, 1000000.0f, 1.0f},
                                 {400.0f, 60.0f, 1000000.0f, 1.0f},
                                 {100.0f, 400.0f, 1000000.0f, 1.0f}};
static const float kSmall[3][4] = {{420.0f, 420.0f, 1000000.0f, 1.0f},
                                   {500.0f, 425.0f, 1000000.0f, 1.0f},
                                   {430.0f, 500.0f, 1000000.0f, 1.0f}};

static RdMesh triMesh(const float (*pos)[4])
{
    float qw[1 + 3 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u; /* NLOOP 3, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        memcpy(qw[1 + k * 3], pos[k], sizeof(pos[k]));
        qw[1 + k * 3 + 1][2] = 1.0f;
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f; /* the strip flag */
        for (int i = 0; i < 4; i++) {
            qw[1 + k * 3 + 2][i] = i < 3 ? 200.0f : 127.0f;
        }
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

/* a code-32 draw moved by tx GS pixels (rd_pixel_test's seamVuDraw: the
 * model to screen matrix the identity in x, y from GS 1792, z scaled) */
static void triDraw(RdMesh m, double tx, RdKey key)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = 32;
    d.clip = RD_VU_CLIP_REGION;
    d.vu.mem[16][0] = 1.0f;
    d.vu.mem[17][1] = 1.0f;
    d.vu.mem[18][2] = 1.0f;
    d.vu.mem[19][0] = (float)(1792.0 + tx);
    d.vu.mem[19][1] = 1792.0f;
    d.vu.mem[19][3] = 1.0f;
    d.vu.mem[22][2] = 1.0f / 1020000.0f;
    d.vu.mem[23][3] = 1.0f;
    rd_draw_vu_mesh(m, &d, key);
}

/* one tick: the moving mesh at tx, the new one (withNew), a particle batch */
static void tick(RdMesh moving, RdMesh fresh, double tx, int withNew, int cut)
{
    rd_begin_frame();
    if (cut) {
        rd_camera_cut();
    }
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kGrey, 1, 0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    triDraw(moving, tx, RD_KEY(&kMoving, 0, 32));
    if (withNew) {
        triDraw(fresh, 0.0, RD_KEY(&kNew, 0, 32));
    }
    /* particles: never a VuPrevCB */
    static float p[6 + 2][4];
    memset(p, 0, sizeof(p));
    const int32_t n = 1;
    memcpy(&p[0][0], &n, 4);
    p[6][0] = 1.0f;
    p[6][3] = 1.0f;
    p[7][3] = 0.5f;
    RdVuParticleDraw pd;
    memset(&pd, 0, sizeof(pd));
    pd.qw = (const float (*)[4])p;
    pd.count = 1;
    pd.vu.mem[16][0] = pd.vu.mem[17][1] = pd.vu.mem[18][2] = 1.0f;
    pd.vu.mem[19][0] = pd.vu.mem[19][1] = 2048.0f;
    pd.vu.mem[19][3] = 1.0f;
    rd_select_list(6);
    rd_draw_vu_particles(&pd, RD_KEY(&kMoving, 9, 0));
    rd_end_frame(0);
}

static const RdCmd *findKey(const RdFrame *f, int l, RdKey k)
{
    for (uint32_t i = 0; f && i < f->lists[l].count; i++) {
        const RdCmd *c = &f->lists[l].cmds[i];
        if (c->keyLo == (uint32_t)k && c->keyHi == (uint32_t)(k >> 32)) {
            return c;
        }
    }
    return NULL;
}

static const float (*blockOf(const uint8_t *payload))[4]
{
    return (const float (*)[4])(const void *)(payload + sizeof(RdVuPayload));
}

/* model point p through a block's model to screen matrix (qw 16..19,
 * columns) divided: its GS x */
static double gsX(const float (*m)[4], const float p[4])
{
    double h[4] = {0.0, 0.0, 0.0, 0.0};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            h[r] += (double)m[16 + c][r] * p[c];
        }
    }
    return h[0] / h[3];
}

static void testSource(RdMesh moving, RdMesh fresh)
{
    rd__velocity_set(true);
    tick(moving, fresh, 0.0, 0, 0);
    tick(moving, fresh, 8.0, 1, 0);
    const RdFrame *f = rd__last_frame();
    const RdCmd *c = findKey(f, 0, RD_KEY(&kMoving, 0, 32));
    const RdCmd *nc = findKey(f, 0, RD_KEY(&kNew, 0, 32));
    const RdCmd *pc = findKey(f, 6, RD_KEY(&kMoving, 9, 0));
    const uint8_t *prev = c ? rd__velocity_prev(f, c) : NULL;
    CHECK(prev != NULL, "source: the moved draw carries a VuPrevCB (u[3] %#x)", c ? c->u[3] : 0u);
    if (prev && c) {
        const float (*pb)[4] = blockOf(prev);
        const float (*cb)[4] = blockOf(f->payload + c->u[1]);
        CHECK(pb[19][0] == 1792.0f && cb[19][0] == 1800.0f,
              "source: VuPrevCB is the tick before's block (x %g, own %g)", (double)pb[19][0],
              (double)cb[19][0]);
        const double d = gsX(pb, kTri[0]) - gsX(cb, kTri[0]);
        CHECK(fabs(d + 8.0) < 1e-9, "source: vertex 0's delta current to previous -8 GS (%g)", d);
        CHECK((c->u[3] & RD_VU_PREV_OFF) % 8u == 0u && (c->u[3] & RD_VU_TICK_CAMERAS) == 0u,
              "source: u[3]'s offset 8-aligned, no tick cameras (%#x)", c->u[3]);
    }
    CHECK(nc && !rd__velocity_prev(f, nc) && (nc->u[3] & RD_VU_PREV) == 0u,
          "source: a draw with no pair has none");
    CHECK(pc && (pc->u[3] & RD_VU_PREV) == 0u, "source: a particle batch has none");
    const RdVelocityStats *st = rd__velocity_stats();
    CHECK(st->vuDraws == 2u && st->paired == 1u && st->moved == 1u && st->unpaired == 1u &&
              !st->reset,
          "source: stats (draws %u paired %u moved %u unpaired %u reset %u)", st->vuDraws,
          st->paired, st->moved, st->unpaired, st->reset);

    /* cut */
    tick(moving, fresh, 16.0, 1, 1);
    f = rd__last_frame();
    c = findKey(f, 0, RD_KEY(&kMoving, 0, 32));
    CHECK(c && !rd__velocity_prev(f, c) && rd__velocity_stats()->reset,
          "cut: a new history, no VuPrevCB");
    tick(moving, fresh, 24.0, 1, 0);
    f = rd__last_frame();
    c = findKey(f, 0, RD_KEY(&kMoving, 0, 32));
    CHECK(c && rd__velocity_prev(f, c) && !rd__velocity_stats()->reset,
          "cut: the frame after pairs");

    /* again: the frame just attached (its moving draw paired) given again
     * after the picture before was dropped: nothing pairs, so nothing may
     * keep the old VuPrevCB (the tick cameras' bit stays) */
    {
        RdFrame *g = (RdFrame *)rd__last_frame();
        RdCmd *gc = g ? (RdCmd *)findKey(g, 0, RD_KEY(&kMoving, 0, 32)) : NULL;
        CHECK(gc && (gc->u[3] & RD_VU_PREV), "again: the frame was attached (%#x)",
              gc ? gc->u[3] : 0u);
        if (gc) {
            gc->u[3] |= RD_VU_TICK_CAMERAS;
            rd__velocity_forget();
            rd__velocity_attach(g, 0);
            CHECK(gc->u[3] == RD_VU_TICK_CAMERAS && !rd__velocity_prev(g, gc) &&
                      rd__velocity_stats()->reset,
                  "again: a stale VuPrevCB kept (u[3] %#x)", gc->u[3]);
            for (int l = 0; l < RD_LIST_COUNT; l++) {
                for (uint32_t i = 0; i < g->lists[l].count; i++) {
                    const RdCmd *x = &g->lists[l].cmds[i];
                    if (x->type >= RDC_MESH && x->type <= RDC_PARTICLES) {
                        CHECK((x->u[3] & RD_VU_PREV) == 0u, "again: list %d draw %u u[3] %#x", l, i,
                              x->u[3]);
                    }
                }
            }
            gc->u[3] &= ~RD_VU_TICK_CAMERAS;
        }
    }

    /* off */
    rd__velocity_set(false);
    tick(moving, fresh, 32.0, 1, 0);
    f = rd__last_frame();
    c = findKey(f, 0, RD_KEY(&kMoving, 0, 32));
    nc = findKey(f, 0, RD_KEY(&kNew, 0, 32));
    uint32_t vu = 0;
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        for (uint32_t i = 0; i < f->lists[l].count; i++) {
            const RdCmd *x = &f->lists[l].cmds[i];
            if (x->type >= RDC_MESH && x->type <= RDC_PARTICLES) {
                CHECK(x->u[3] == 0u, "off: a VU draw's u[3] is 0 (%#x)", x->u[3]);
                vu++;
            }
        }
    }
    CHECK(c && nc && vu == 3u && rd__velocity_stats()->paired == 0u,
          "off: three VU draws, none with a VuPrevCB (%u)", vu);
}

static void testInterp(RdMesh moving, RdMesh fresh)
{
    rd__velocity_set(false);
    tick(moving, fresh, 0.0, 0, 0);
    tick(moving, fresh, 8.0, 0, 0);
    rd__velocity_set(true); /* a new history */
    const RdFrame *a = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.25f, 1, NULL);
    const RdCmd *c = a ? findKey(a, 0, RD_KEY(&kMoving, 0, 32)) : NULL;
    CHECK(c && !rd__velocity_prev(a, c) && rd__velocity_stats()->reset,
          "interp: the first present begins the history");
    const RdFrame *b = rd__interp_frame(rd__prev_frame(), rd__last_frame(), 0.75f, 0, NULL);
    c = b ? findKey(b, 0, RD_KEY(&kMoving, 0, 32)) : NULL;
    const uint8_t *prev = c ? rd__velocity_prev(b, c) : NULL;
    CHECK(prev != NULL && !rd__velocity_stats()->reset, "interp: the second present pairs");
    if (prev && c) {
        const float (*pb)[4] = blockOf(prev);
        const float (*cb)[4] = blockOf(b->payload + c->u[1]);
        CHECK(fabsf(pb[19][0] - 1794.0f) < 1e-3f && fabsf(cb[19][0] - 1798.0f) < 1e-3f,
              "interp: VuPrevCB the present before's blend (%g at 0.25, own %g at 0.75)",
              (double)pb[19][0], (double)cb[19][0]);
    }
    rd__velocity_set(false);
}

/* VELOCITY's texel (x, y): rgba */
static bool velAt(const float *v, uint32_t w, uint32_t h, uint32_t x, uint32_t y, float out[4])
{
    if (!v || x >= w || y >= h) {
        return false;
    }
    memcpy(out, &v[((size_t)y * w + x) * 4], 4 * sizeof(float));
    return true;
}

static void testPixels(RdMesh moving, RdMesh fresh, const char *dir)
{
    /* the picture with the switch off */
    rd__velocity_set(false);
    tick(moving, fresh, 0.0, 0, 0);
    tick(moving, fresh, 8.0, 1, 0);
    uint32_t w = 0, h = 0;
    const size_t cap = (size_t)4096 * 4096 * 4;
    uint8_t *off = malloc(cap), *on = malloc(cap);
    float *v = malloc(cap * sizeof(float));
    if (!off || !on || !v) {
        CHECK(0, "pixels: no memory");
        free(off);
        free(on);
        free(v);
        return;
    }
    const bool okOff = rd__read_target(rd_target(RD_TARGET_SCENE), off, cap, &w, &h);
    rd__velocity_set(true);
    tick(moving, fresh, 0.0, 0, 0);
    tick(moving, fresh, 8.0, 1, 0);
    uint32_t w2 = 0, h2 = 0;
    const bool okOn = rd__read_target(rd_target(RD_TARGET_SCENE), on, cap, &w2, &h2);
    CHECK(okOff && okOn && w == w2 && h == h2 && memcmp(off, on, (size_t)w * h * 4) == 0,
          "pixels: SCENE the same bytes with the switch on and off (%ux%u)", w, h);
    uint32_t vw = 0, vh = 0;
    const bool okV = rd__velocity_read(v, cap, &vw, &vh);
    const RdVelocityStats *st = rd__velocity_stats();
    CHECK(okV && vw == w && vh == h, "pixels: VELOCITY read back at SCENE's size (%ux%u)", vw, vh);
    CHECK(st->drawn >= 1u && st->noRecord == 0u,
          "pixels: the moving draw drawn into VELOCITY (drawn %u, left at 0 %u)", st->drawn,
          st->noRecord);
    float t[4];
    if (okV && velAt(v, vw, vh, 150, 150, t)) {
        printf("  pixels: VELOCITY at (150, 150): %g %g %g %g\n", (double)t[0], (double)t[1],
               (double)t[2], (double)t[3]);
        CHECK(fabsf(t[0] + 8.0f) < 0.05f && fabsf(t[1]) < 0.05f && t[3] == 1.0f,
              "pixels: inside the moving mesh (-8, 0) w 1: %g %g w %g", (double)t[0], (double)t[1],
              (double)t[3]);
    }
    if (okV && velAt(v, vw, vh, 5, 5, t)) {
        CHECK(t[0] == 0.0f && t[1] == 0.0f && t[3] == 0.0f,
              "pixels: outside every mesh 0 (%g %g w %g)", (double)t[0], (double)t[1],
              (double)t[3]);
    }
    if (okV && velAt(v, vw, vh, 450, 440, t)) {
        CHECK(t[0] == 0.0f && t[1] == 0.0f && t[3] == 0.0f,
              "pixels: the new mesh (no pair) 0 (%g %g w %g)", (double)t[0], (double)t[1],
              (double)t[3]);
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_velocity_moving.rddump", dir);
    CHECK(rd_dump_frame(path), "pixels: rd_dump_frame (%s)", path);
    rd__velocity_set(false);
    free(off);
    free(on);
    free(v);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    rd__set_not_implemented_fatal(true);
    if (!rd__init_record_only(512, 512)) {
        printf("rd_velocity_test: no context\n");
        return 1;
    }
    RdMesh moving = triMesh(kTri), fresh = triMesh(kSmall);
    CHECK(moving.id && fresh.id, "the meshes");
    testSource(moving, fresh);
    testInterp(moving, fresh);
    rd_destroy_vu_mesh(moving);
    rd_destroy_vu_mesh(fresh);
    rd_shutdown();
    printf("rd_velocity_test: source rule %s\n", failures ? "FAILED" : "ok");

    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    if (!rd_init(512, 512, &s, NULL)) {
        printf("rd_velocity_test: no usable device: the pixels skipped\n");
        return failures ? 1 : 0;
    }
    moving = triMesh(kTri);
    fresh = triMesh(kSmall);
    testPixels(moving, fresh, dir);
    rd_destroy_vu_mesh(moving);
    rd_destroy_vu_mesh(fresh);
    rd_shutdown();
    printf("rd_velocity_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
