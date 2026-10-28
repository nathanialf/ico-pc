/* rd_shadow_selftest.c: the shadow count's self-test (rd_internal.h
 * rd__shadow_selftest).
 *
 * The one draw (RdProfile.shadowOne; rd_replay.c shadowCountOne) reads the
 * depth-stencil's stencil through a view of its own (RHI_ASPECT_STENCIL),
 * a path each backend makes differently (Vulkan a stencil-aspect view,
 * Direct3D an SRV of the G8 plane, OpenGL a texture view in the
 * stencil-index mode) and a driver may get wrong.  So it is checked on the
 * device before the game draws with it, on SCENE at its real size and
 * scale: a frame of its own (never recorded, never presented) binds SCENE's
 * colour as the count target and SCENE's depth, resets the count and draws
 * volumes as rectangles of two triangles each, at sub-pixel edges:
 *   band k of SB_BANDS + 1 columns (k = 0 .. SB_BANDS) counts k in the top
 *   half and -k in the bottom half (rectangle k increments from band k's
 *   left edge to the right side over the whole height, and decrements twice
 *   over the bottom half),
 * so the stencil holds every value 0..63 (n mod 64, both signs, 64 among
 * them), and resolves it.  The frame is replayed through the one draw and
 * through the seven draws, and the count at its GS size (the count target
 * at scale 1, the reduced count above it: rd__shadow_count_read) is read
 * back each time.  The one draw stays when its count equals the seven
 * draws' byte for byte and every band's centre holds the count's colour
 * (rd__shadow_table_texel of n mod 64); otherwise, or when the test
 * cannot run (no SCENE, no frame), the flag goes off
 * (rd__profile_self_test_off: "(self-test)") and the seven draws resolve
 * the count.  MSAA is held off for both replays (the count's pass closes
 * the epoch in the game; here SCENE's colour is the count target).  One
 * line:
 *   shadow count: one draw (self-test: equal to the seven draws over
 *     688x448, 142 band centres ok) on SCENE 688x448 at 1376x896 texels
 *   shadow count: seven draws (self-test: one draw differs at 17 of
 *     308224 texels, the first (40,300): 00040480 for 04040480) on SCENE ...
 * ICO_RD_SHADOW_SABOTAGE=1 makes the one draw's table wrong at m = 1
 * (rd_shadow_one.c tableMake reads it through rd__shadow_sabotaged), so
 * the failure path runs (ctest rd_shadow_selftest_sabotage).
 * ICO_RD_SHADOW_ONE=1 ([dev] shadow_one) keeps the one draw whatever it
 * finds ("kept by [dev] shadow_one").  Where the profile has the seven
 * draws nothing runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"

#define SB_BANDS 70

/* GS TEST: ZTE, ZTST ALWAYS: every face counts */
#define SB_TEST RD_TEST_Z_ALWAYS

/* a GS pixel coordinate in 1/16 pixels on a w x h frame (XYOFFSET at
 * 2048 - w / 2) */
static int32_t gsX(uint32_t w, uint32_t sixteenths)
{
    return (2048 - (int32_t)(w / 2)) * 16 + (int32_t)sixteenths;
}

static int32_t gsY(uint32_t h, uint32_t sixteenths)
{
    return (2048 - (int32_t)(h / 2)) * 16 + (int32_t)sixteenths;
}

/* band k's left edge in 1/16 GS pixels (band 0 starts at 0): seven
 * sixteenths past a pixel edge, so a scaled count mixes bands in its
 * boxes */
static uint32_t bandEdge(uint32_t w, int k)
{
    return k == 0 ? 0u : (uint32_t)k * w * 16u / (SB_BANDS + 1u) + 7u;
}

/* the row between the halves, 1/16 GS pixels */
static uint32_t midRow(uint32_t h)
{
    return h * 8u + 5u;
}

static void quad(RdScreenVtx *o, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    const int32_t xs[6] = {x0, x1, x0, x0, x1, x1}, ys[6] = {y0, y0, y1, y1, y0, y1};
    for (int i = 0; i < 6; i++) {
        memset(&o[i], 0, sizeof(o[i]));
        o[i].x = xs[i];
        o[i].y = ys[i];
        o[i].q = 1.0f;
    }
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 3, scene, w, h) && rd__st_push(f, 3, RDC_TEXTURE_OFF) &&
              rd__st_push1(f, 3, RDC_ABE, 0) && rd__st_test(f, 3, SB_TEST) &&
              rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF) && rd__st_push(f, 3, RDC_SHADOW_RESET);
    /* SB_BANDS increments, then twice as many decrements */
    const uint32_t inc = SB_BANDS * 6u, dec = SB_BANDS * 12u;
    RdScreenVtx *v = calloc(inc + dec, sizeof(*v));
    if (!v) {
        return false;
    }
    const int32_t right = gsX(w, w * 16u), top = gsY(h, 0), bottom = gsY(h, h * 16u),
                  mid = gsY(h, midRow(h));
    for (int k = 1; k <= SB_BANDS; k++) {
        const int32_t left = gsX(w, bandEdge(w, k));
        quad(&v[(k - 1) * 6], left, top, right, bottom);
        quad(&v[inc + (k - 1) * 12], left, mid, right, bottom);
        quad(&v[inc + (k - 1) * 12 + 6], left, mid, right, bottom);
    }
    const uint32_t off = rd__frame_payload(f, v, (inc + dec) * (uint32_t)sizeof(*v));
    free(v);
    RdCmd *c = ok ? rd__st_push(f, 3, RDC_SHADOW_STRIP) : NULL;
    if (c) {
        c->b[0] = RD_SHADOW_TRIS;
        c->u[0] = inc;
        c->u[1] = off;
        c->u[2] = (inc + dec) * (uint32_t)sizeof(RdScreenVtx);
        c->u[3] = dec;
    }
    return c && rd__st_push(f, 3, RDC_SHADOW_RESOLVE);
}

/* One replay of f, the count at its GS size into px (w * h * 4) */
static bool run(const RdFrame *f, uint32_t scene, uint8_t *px, size_t size, uint32_t w, uint32_t h)
{
    uint32_t rw = 0, rh = 0;
    return rd__replay_frame(f, 0, false) && rd__shadow_count_read(scene, px, size, &rw, &rh) &&
           rw == w && rh == h;
}

/* the band centres the one draw got right: band k's centre column, the
 * middle of each half */
static int centresOk(const uint8_t *px, uint32_t w, uint32_t h, int *bad)
{
    int ok = 0;
    *bad = -1;
    for (int k = 0; k <= SB_BANDS; k++) {
        const uint32_t x0 = bandEdge(w, k) / 16u + 1u;
        const uint32_t x1 = (k == SB_BANDS ? w * 16u : bandEdge(w, k + 1)) / 16u;
        const uint32_t x = (x0 + x1) / 2u;
        for (int half = 0; half < 2; half++) {
            const uint32_t y = half ? h * 3u / 4u : h / 4u;
            const int n = half ? -k : k;
            uint8_t want[4];
            rd__shadow_table_texel((uint32_t)n & RD_SHADOW_STENCIL_MASK, want);
            if (memcmp(&px[((size_t)y * w + x) * 4u], want, 4) == 0) {
                ok++;
            } else if (*bad < 0) {
                *bad = half ? -k - 1000 : k;
            }
        }
    }
    return ok;
}

bool rd__shadow_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice || !g_rd.profile.shadowOne) {
        return true; /* the seven draws resolve the count: nothing to check */
    }
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    RdTargetRec *t = rd__target_rec(scene);
    /* an early return leaves the one draw unverified: off, as on a
     * difference, unless [dev] shadow_one set it */
    const bool kept = (g_rd.profile.devSet & RD_PROFILE_SHADOW_ONE) != 0;
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM) {
        rd__log("shadow count: self-test: no SCENE target to run it on%s",
                kept ? "; kept by [dev] shadow_one" : "");
        if (!kept) {
            rd__profile_self_test_off(RD_PROFILE_SHADOW_ONE);
        }
        return false;
    }
    const uint32_t w = t->w, h = t->h;
    const size_t size = (size_t)w * h * 4u;
    uint8_t *one = malloc(size), *seven = malloc(size);
    static RdFrame f;
    if (!one || !seven || !buildFrame(&f, scene, w, h)) {
        free(one);
        free(seven);
        rd__frame_free(&f);
        rd__log("shadow count: self-test: could not build its frame%s",
                kept ? "; kept by [dev] shadow_one" : "");
        if (!kept) {
            rd__profile_self_test_off(RD_PROFILE_SHADOW_ONE);
        }
        return false;
    }
    if (rd__shadow_sabotaged()) {
        rd__log("shadow count: ICO_RD_SHADOW_SABOTAGE: the one draw's table is wrong at m = 1");
    }
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* SCENE's colour is this count's target */
    const bool doneOne = run(&f, scene, one, size, w, h);
    const bool tookOne = g_rdPerf.shadowOne > 0; /* the replay's own record (rd__perf_begin) */
    /* a stencil view the backend could not make turned the one draw off
     * inside that replay (rd__shadow_one_draw): it stays off */
    const bool viewLost = !rhi_limits()->stencilView;
    g_rd.profile.shadowOne = false; /* the seven draws' replay */
    const bool doneSeven = run(&f, scene, seven, size, w, h) && g_rdPerf.shadowSeven > 0;
    g_rd.profile.shadowOne = !viewLost;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    rd__shadow_count_forget(); /* the count's target was SCENE */
    t->clearPending = 1;       /* the next replay starts SCENE from its clear */

    size_t diff = 0, first = 0;
    for (size_t i = 0; doneOne && doneSeven && i < (size_t)w * h; i++) {
        if (memcmp(&one[i * 4u], &seven[i * 4u], 4) != 0) {
            if (!diff++) {
                first = i;
            }
        }
    }
    int bad = -1;
    const int centres = doneOne ? centresOk(one, w, h, &bad) : 0;
    const int want = 2 * (SB_BANDS + 1);
    const bool passed = doneOne && doneSeven && tookOne && diff == 0 && centres == want;
    char res[192];
    if (!doneOne || !doneSeven) {
        snprintf(res, sizeof res, "error (%s replay or readback)",
                 doneOne ? "seven draws'" : "one draw's");
    } else if (viewLost) {
        snprintf(res, sizeof res, "not run (the backend could not make a stencil view)");
    } else if (!tookOne) {
        snprintf(res, sizeof res, "not run (no resolve took the one draw)");
    } else if (diff) {
        const uint8_t *a = &one[first * 4u], *b = &seven[first * 4u];
        snprintf(res, sizeof res,
                 "one draw differs at %zu of %zu texels, the first (%zu,%zu): %02X%02X%02X%02X for "
                 "%02X%02X%02X%02X",
                 diff, (size_t)w * h, first % w, first / w, a[0], a[1], a[2], a[3], b[0], b[1],
                 b[2], b[3]);
    } else if (centres != want) {
        snprintf(res, sizeof res,
                 "equal to the seven draws, %d of %d band centres off the count's "
                 "colour, the first band %d",
                 want - centres, want, bad);
    } else {
        snprintf(res, sizeof res, "equal to the seven draws over %ux%u, %d band centres ok", w, h,
                 centres);
    }
    if (!passed && !kept) {
        rd__profile_self_test_off(RD_PROFILE_SHADOW_ONE);
    }
    rd__log("shadow count: %s (self-test: %s)%s on SCENE %ux%u at %ux%u texels",
            g_rd.profile.shadowOne ? "one draw" : "seven draws", res,
            !passed && kept && !viewLost ? "; kept by [dev] shadow_one" : "", w, h, t->tw, t->th);
    free(one);
    free(seven);
    return passed;
}
