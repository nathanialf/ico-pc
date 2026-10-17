/* rd_date_selftest.c: DATE's self-test (rd.h rd_date_selftest).
 *
 * DATE through the stencil bit (RdProfile.dateStencil; rd_replay_tiler.c
 * rd__replay_date_take) reads the colour attachment by local read after a
 * by-region barrier: a driver path the exact blend's rasterization-order
 * read does not take, whose results a device may get wrong (the Adreno
 * driver's attachment read returned wrong colours).  So the profile's
 * choice is checked on the device before the game draws with it, on SCENE
 * at its real size: a frame of its own (never recorded, never presented)
 * clears SCENE, draws a cell of destination alpha 0x80 over its left half
 * and one of alpha 0x00 over its right half, then a DATE sprite with DATM 1
 * over the top half and one with DATM 0 over the bottom half.  The GS draws
 * the DATM 1 sprite where the alpha's MSB is 1 and the DATM 0 one where it
 * is 0: top left and bottom right take the sprites' colours, the other two
 * keep the cells'.  The frame is replayed through the stencil bit and
 * through the R8 snapshot, and the four quadrants' centres are read back
 * each time and compared with that, every channel exactly.
 *   DATE: stencil path (self-test: stencil ok 4, snapshot ok 4) on SCENE ...
 *   DATE: snapshot path (self-test: stencil fail 2 at sample 1: 28C850FF
 *         for 28C85000, snapshot ok 4) on SCENE ...
 * The stencil path stays only with its four right; otherwise the profile's
 * flag goes off (rd__profile_self_test_off: "(self-test)" in the profile's
 * words) and the snapshot draws DATE.  ICO_RD_DATE_PATH=stencil ([dev]
 * date_path) keeps the stencil whatever it finds ("kept by [dev]
 * date_path").  Where the profile has the snapshot nothing runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

#define DT_SAMPLES 4

/* the cells (alpha MSB 1 left, 0 right), the sprites (DATM 1 top, DATM 0
 * bottom) and the clear, RGBA */
static const uint8_t kClear[4] = {10, 20, 30, 0x00};
static const uint8_t kCellMsb[4] = {200, 40, 40, 0x80};
static const uint8_t kCellZero[4] = {40, 200, 80, 0x00};
static const uint8_t kSpriteDatm1[4] = {250, 250, 10, 0x40};
static const uint8_t kSpriteDatm0[4] = {10, 250, 250, 0x60};

/* GS TEST words: ZTE, ZTST ALWAYS, DATE, DATM 1 (0x3C000) or DATM 0 */
#define DT_TEST_DATM1 0x3C000u
#define DT_TEST_DATM0 RD_TEST_DATE0

/* one screen command of n RdScreenVtx (sprites, full-frame space) */
static bool pushSprites(RdFrame *f, int list, const RdScreenVtx *v, uint32_t n)
{
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * sizeof(*v)));
    c->u[1] = n;
    return true;
}

/* a sprite from GS pixel (x0, y0) to (x1, y1) of a w x h frame */
static void sprite(RdScreenVtx v[2], uint32_t w, uint32_t h, uint32_t x0, uint32_t y0, uint32_t x1,
                   uint32_t y1, const uint8_t rgba[4])
{
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, 2 * sizeof(*v));
    v[0].x = (ox + (int32_t)x0) * 16;
    v[0].y = (oy + (int32_t)y0) * 16;
    v[1].x = (ox + (int32_t)x1) * 16;
    v[1].y = (oy + (int32_t)y1) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
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
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_push1(f, 0, RDC_ABE, 0) &&
         rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_SHADE, 0);
    RdScreenVtx cells[4], s1[2], s0[2];
    sprite(&cells[0], w, h, 0, 0, w / 2, h, kCellMsb);
    sprite(&cells[2], w, h, w / 2, 0, w, h, kCellZero);
    sprite(s1, w, h, 0, 0, w, h / 2, kSpriteDatm1);
    sprite(s0, w, h, 0, h / 2, w, h, kSpriteDatm0);
    ok = ok && pushSprites(f, 0, cells, 4);
    ok = ok && rd__st_test(f, 0, DT_TEST_DATM1) && pushSprites(f, 0, s1, 2);
    ok = ok && rd__st_test(f, 0, DT_TEST_DATM0) && pushSprites(f, 0, s0, 2);
    return ok;
}

/* the quadrants' centres in SCENE's texels: top left, top right, bottom
 * left, bottom right, and what the GS leaves there */
static void samples(const RdTargetRec *t, uint32_t xs[DT_SAMPLES], uint32_t ys[DT_SAMPLES],
                    const uint8_t *want[DT_SAMPLES])
{
    xs[0] = xs[2] = t->tw / 4;
    xs[1] = xs[3] = t->tw - 1 - t->tw / 4;
    ys[0] = ys[1] = t->th / 4;
    ys[2] = ys[3] = t->th - 1 - t->th / 4;
    want[0] = kSpriteDatm1; /* MSB 1, DATM 1: drawn */
    want[1] = kCellZero;    /* MSB 0, DATM 1: kept */
    want[2] = kCellMsb;     /* MSB 1, DATM 0: kept */
    want[3] = kSpriteDatm0; /* MSB 0, DATM 0: drawn */
}

static struct {
    RhiTexture tex;
    RhiState state;
} s_read;

/* One replay of f; *ok the samples equal to the GS's, *bad the first that
 * is not (-1 none) and got its RGBA.  False when the replay or the readback
 * could not be done. */
static bool run(const RdFrame *f, RdTargetRec *t, int *ok, int *bad, uint8_t got[4])
{
    if (!rd__replay_frame(f, 0, false)) {
        return false;
    }
    if (!s_read.tex.id) {
        s_read.tex = rhi_create_texture(&(RhiTextureDesc){DT_SAMPLES, 1, 1, t->format,
                                                          RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                          "rd DATE self-test", 0});
        s_read.state = RHI_STATE_UNDEFINED;
        if (!s_read.tex.id) {
            return false;
        }
    }
    uint32_t xs[DT_SAMPLES], ys[DT_SAMPLES];
    const uint8_t *want[DT_SAMPLES];
    samples(t, xs, ys, want);
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_read.tex, &s_read.state, RHI_STATE_COPY_DST);
    for (uint32_t k = 0; k < DT_SAMPLES; k++) {
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){(int32_t)xs[k], (int32_t)ys[k], 1, 1},
                             s_read.tex, (int32_t)k, 0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    uint8_t px[DT_SAMPLES * 4];
    if (!rd__read_rhi_texture(s_read.tex, &s_read.state, DT_SAMPLES, 1, px, sizeof(px))) {
        return false;
    }
    *ok = 0;
    *bad = -1;
    for (int k = 0; k < DT_SAMPLES; k++) {
        if (memcmp(&px[k * 4], want[k], 4) == 0) {
            (*ok)++;
        } else if (*bad < 0) {
            *bad = k;
            memcpy(got, &px[k * 4], 4);
        }
    }
    return true;
}

/* "ok 4", "fail 2 at sample 1: 28C850FF for 28C85000" or "error" */
static void result(char *out, size_t size, bool done, int ok, int bad, const uint8_t got[4],
                   const RdTargetRec *t)
{
    if (!done) {
        snprintf(out, size, "error");
        return;
    }
    if (bad < 0) {
        snprintf(out, size, "ok %d", ok);
        return;
    }
    uint32_t xs[DT_SAMPLES], ys[DT_SAMPLES];
    const uint8_t *want[DT_SAMPLES];
    samples(t, xs, ys, want);
    snprintf(out, size, "fail %d at sample %d: %02X%02X%02X%02X for %02X%02X%02X%02X",
             DT_SAMPLES - ok, bad, got[0], got[1], got[2], got[3], want[bad][0], want[bad][1],
             want[bad][2], want[bad][3]);
}

bool rd_date_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice || !g_rd.profile.dateStencil) {
        return true; /* the snapshot draws DATE: nothing to check */
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM) {
        rd__log("DATE: self-test: no SCENE target to run it on");
        return false;
    }
    static RdFrame f;
    if (!buildFrame(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h)) {
        rd__frame_free(&f);
        rd__log("DATE: self-test: could not build its frame");
        return false;
    }
    int okS = 0, badS = -1, okN = 0, badN = -1;
    uint8_t gotS[4] = {0}, gotN[4] = {0};
    const bool doneS = run(&f, t, &okS, &badS, gotS);
    g_rd.profile.dateStencil = false; /* the snapshot's replay */
    const bool doneN = run(&f, t, &okN, &badN, gotN);
    g_rd.profile.dateStencil = true;
    rd__frame_free(&f);
    if (s_read.tex.id) {
        rhi_destroy_texture(s_read.tex);
    }
    s_read.tex = (RhiTexture){0};
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    const bool passed = doneS && badS < 0;
    const bool kept = (g_rd.profile.devSet & RD_PROFILE_DATE_STENCIL) != 0;
    char rs[96], rn[96];
    result(rs, sizeof rs, doneS, okS, badS, gotS, t);
    result(rn, sizeof rn, doneN, okN, badN, gotN, t);
    if (!passed && !kept) {
        rd__profile_self_test_off(RD_PROFILE_DATE_STENCIL);
    }
    rd__log("DATE: %s path (self-test: stencil %s, snapshot %s)%s on SCENE %ux%u at %ux%u texels",
            g_rd.profile.dateStencil ? "stencil" : "snapshot", rs, rn,
            !passed && kept ? "; kept by [dev] date_path" : "", t->w, t->h, t->tw, t->th);
    return passed;
}
