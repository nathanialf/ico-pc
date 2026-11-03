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
 * them), and resolves it under the running disc's step
 * (rd__shadow_disc_step: 4, or 8 on the North American disc and the 2001
 * previews, which the reset and the resolve carry, so the one draw reads
 * that step's table).  The frame is replayed through the one draw and
 * through the seven draws, each from SCENE's clear (run: the frame has no
 * clear of its own), and the count at its GS size (the count target
 * at scale 1, the reduced count above it: rd__shadow_count_read) is read
 * back each time.  The one draw stays when its count equals the seven
 * draws' byte for byte and every band's centre holds the count's colour
 * (rd__shadow_table_texel of n mod 64 under the step, read inside the
 * frame's exact width: rd__frame_wexact); otherwise, or when
 * the test cannot run (no SCENE, no frame), the flag goes off
 * (rd__profile_self_test_off: "(self-test)") and the seven draws resolve
 * the count.  The one draw is replayed twice, its stencil read four
 * texels a gather and a texel a load (shadow_count_ps's g_mode.y,
 * g_rd.shadowLoad): the gathers stay where their count is the seven
 * draws' byte for byte and every centre right, else the loads where
 * theirs is (the result below is then the loads'), else the seven draws.
 * Where the stencil view takes no gather (RhiLimits.stencilGather false:
 * Direct3D) the gathers' replay is not run (their module would put a
 * gather on a view that has none) and the loads are the one draw's form:
 *   shadow count: the stencil read a texel a load, as the backend's
 *     stencil view takes no gather
 * MSAA is held off for the replays (the count's pass closes the epoch in
 * the game; here SCENE's colour is the count target).  The lines:
 *   shadow count: one draw (self-test: equal to the seven draws over
 *     688x448, 142 band centres ok) on SCENE 688x448 at 1376x896 texels,
 *     step 4
 *   shadow count: seven draws (self-test: one draw differs at 17 of
 *     308224 texels, the first (40,300): 00040480 for 04040480) on SCENE ...
 * and, where the one draw stays, one of
 *   shadow count: the stencil read four texels a gather, equal to the
 *     seven draws byte for byte
 *   shadow count: the stencil read a texel a load, as the gathers' count
 *     differs at N of M texels, the first (X,Y)
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

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h, uint32_t step)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    /* the reset and the resolve carry the step as the recorder writes it
     * (rd_internal.h RDC_SHADOW_RESET) */
    const uint8_t b0 = (uint8_t)(step == 8 ? 8 : 0);
    RdCmd *rc = NULL;
    bool ok = rd__st_target(f, 3, scene, w, h) && rd__st_push(f, 3, RDC_TEXTURE_OFF) &&
              rd__st_push1(f, 3, RDC_ABE, 0) && rd__st_test(f, 3, SB_TEST) &&
              rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF) &&
              (rc = rd__st_push(f, 3, RDC_SHADOW_RESET)) != NULL;
    if (rc) {
        rc->b[0] = b0;
    }
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
    RdCmd *rv = c ? rd__st_push(f, 3, RDC_SHADOW_RESOLVE) : NULL;
    if (rv) {
        rv->b[0] = b0;
    }
    return rv != NULL;
}

/* One replay of f, the count at its GS size into px (w * h * 4).  Each
 * replay starts SCENE from its clear (rd__clear_new_targets): the frame has
 * no clear of its own and reads SCENE's depth in list 3 (the volumes' Z
 * test), so both replays read the same depth, and neither reads one an
 * earlier replay left, which the store plan would take for a game frame's
 * carried read and keep SCENE's depth stores for the whole run
 * (rd_store_plan.c; docs/port/WORK_BUFFERS.md "After the replay") */
static bool run(const RdFrame *f, uint32_t scene, uint8_t *px, size_t size, uint32_t w, uint32_t h)
{
    uint32_t rw = 0, rh = 0;
    RdTargetRec *t = rd__target_rec(scene);
    if (t) {
        t->clearPending = 1;
    }
    return rd__replay_frame(f, 0, false) && rd__shadow_count_read(scene, px, size, &rw, &rh) &&
           rw == w && rh == h;
}

/* the band centres the one draw got right: band k's centre column, the
 * middle of each half.  A cropped frame (rd__frame_cropped: 624 wide at
 * 16:10, exact 614.40) leaves (w - wexact) / 2 columns at each side out of
 * the picture, and the last band's centre lands in them: the centre is the
 * middle of the band's columns inside the exact width, and a band with none
 * there is not read (its centres count as right: the byte-for-byte
 * comparison covers the columns) */
static int centresOk(const uint8_t *px, uint32_t w, uint32_t h, uint32_t step, float wexact,
                     int *bad)
{
    int ok = 0;
    *bad = -1;
    const float crop = wexact < (float)w ? ((float)w - wexact) / 2.0f : 0.0f;
    const uint32_t cropUp = (uint32_t)crop + ((float)(uint32_t)crop < crop ? 1u : 0u);
    const uint32_t lo = cropUp, hi = w - cropUp;
    for (int k = 0; k <= SB_BANDS; k++) {
        uint32_t x0 = bandEdge(w, k) / 16u + 1u;
        uint32_t x1 = (k == SB_BANDS ? w * 16u : bandEdge(w, k + 1)) / 16u;
        x0 = x0 < lo ? lo : x0;
        x1 = x1 > hi ? hi : x1;
        if (x0 >= x1) {
            ok += 2;
            continue;
        }
        const uint32_t x = (x0 + x1) / 2u;
        for (int half = 0; half < 2; half++) {
            const uint32_t y = half ? h * 3u / 4u : h / 4u;
            const int n = half ? -k : k;
            uint8_t want[4];
            rd__shadow_table_texel((uint32_t)n & RD_SHADOW_STENCIL_MASK, step, want);
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
    uint8_t *one = malloc(size), *load = malloc(size), *seven = malloc(size);
    static RdFrame f;
    const uint32_t step = rd__shadow_disc_step();
    if (!one || !load || !seven || !buildFrame(&f, scene, w, h, step)) {
        free(one);
        free(load);
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
    /* the one draw twice: the stencil four texels a gather, then a texel a
     * load (shadow_count_ps's g_mode.y), each against the seven draws */
    const bool gatherAsked = rhi_limits()->stencilGather;
    g_rd.shadowLoad = false;
    const bool doneOne = gatherAsked && run(&f, scene, one, size, w, h);
    const bool tookOne = doneOne && g_rdPerf.shadowOne > 0; /* the replay's own record */
    g_rd.shadowLoad = true;
    const bool doneLoad = rhi_limits()->stencilView && run(&f, scene, load, size, w, h);
    const bool tookLoad = doneLoad && g_rdPerf.shadowOne > 0;
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

    size_t diff = 0, first = 0, diffLoad = 0, firstLoad = 0;
    for (size_t i = 0; doneOne && doneSeven && i < (size_t)w * h; i++) {
        if (memcmp(&one[i * 4u], &seven[i * 4u], 4) != 0) {
            if (!diff++) {
                first = i;
            }
        }
    }
    for (size_t i = 0; doneLoad && doneSeven && i < (size_t)w * h; i++) {
        if (memcmp(&load[i * 4u], &seven[i * 4u], 4) != 0) {
            if (!diffLoad++) {
                firstLoad = i;
            }
        }
    }
    int bad = -1, badLoad = -1;
    /* the frame's exact width, read after the replays (they set gsW) */
    const float wexact = rd__frame_wexact();
    const int centres = doneOne ? centresOk(one, w, h, step, wexact, &bad) : 0;
    const int centresLoad = doneLoad ? centresOk(load, w, h, step, wexact, &badLoad) : 0;
    const int want = 2 * (SB_BANDS + 1);
    const bool gatherOk = doneOne && doneSeven && tookOne && diff == 0 && centres == want;
    const bool loadOk = doneLoad && doneSeven && tookLoad && diffLoad == 0 && centresLoad == want;
    /* the gathers where they are right; else the loads (the result below
     * is then the loads' own), else the seven draws */
    g_rd.shadowLoad = !gatherOk;
    const bool passed = gatherOk || loadOk;
    const bool useLoad = !gatherOk && (loadOk || (doneLoad && !doneOne));
    const uint8_t *px = useLoad ? load : one;
    const size_t nDiff = useLoad ? diffLoad : diff, at = useLoad ? firstLoad : first;
    const int nCentres = useLoad ? centresLoad : centres, nBad = useLoad ? badLoad : bad;
    const bool done = useLoad ? doneLoad : doneOne, took = useLoad ? tookLoad : tookOne;
    char res[192];
    if (!done || !doneSeven) {
        snprintf(res, sizeof res, "error (%s replay or readback)",
                 done ? "seven draws'" : "one draw's");
    } else if (viewLost) {
        snprintf(res, sizeof res, "not run (the backend could not make a stencil view)");
    } else if (!took) {
        snprintf(res, sizeof res, "not run (no resolve took the one draw)");
    } else if (nDiff) {
        const uint8_t *a = &px[at * 4u], *b = &seven[at * 4u];
        snprintf(res, sizeof res,
                 "one draw differs at %zu of %zu texels, the first (%zu,%zu): %02X%02X%02X%02X for "
                 "%02X%02X%02X%02X",
                 nDiff, (size_t)w * h, at % w, at / w, a[0], a[1], a[2], a[3], b[0], b[1], b[2],
                 b[3]);
    } else if (nCentres != want) {
        snprintf(res, sizeof res,
                 "equal to the seven draws, %d of %d band centres off the count's "
                 "colour, the first band %d",
                 want - nCentres, want, nBad);
    } else {
        snprintf(res, sizeof res, "equal to the seven draws over %ux%u, %d band centres ok", w, h,
                 nCentres);
    }
    if (!passed && !kept) {
        rd__profile_self_test_off(RD_PROFILE_SHADOW_ONE);
    }
    rd__log("shadow count: %s (self-test: %s)%s on SCENE %ux%u at %ux%u texels, step %u",
            g_rd.profile.shadowOne ? "one draw" : "seven draws", res,
            !passed && kept && !viewLost ? "; kept by [dev] shadow_one" : "", w, h, t->tw, t->th,
            step);
    if (passed) {
        /* how the one draw reads the stencil, and the gathers' outcome */
        char why[160] = "the gathers' count off at a band centre";
        if (!gatherAsked) {
            snprintf(why, sizeof why, "the backend's stencil view takes no gather");
        } else if (!doneOne || !tookOne) {
            snprintf(why, sizeof why, "the gathers' count %s",
                     !doneOne ? "not read back" : "not drawn");
        } else if (diff) {
            snprintf(why, sizeof why,
                     "the gathers' count differs at %zu of %zu texels, the first (%zu,%zu)", diff,
                     (size_t)w * h, first % w, first / w);
        }
        if (gatherOk) {
            rd__log("shadow count: the stencil read four texels a gather, equal to the seven "
                    "draws byte for byte");
        } else {
            rd__log("shadow count: the stencil read a texel a load, as %s", why);
        }
    }
    free(one);
    free(load);
    free(seven);
    return passed;
}
