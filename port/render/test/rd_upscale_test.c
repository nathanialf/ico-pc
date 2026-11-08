/* rd_upscale_test.c: the upscale point (port/render/rd_upscale.h,
 * docs/port/RENDER.md "The upscale point").
 *
 * Without a device:
 *   scales    rd__upscale_render_scale: d / ratio, floored at 1, the ratio
 *             clamped to 1..3, Auto's cap on the vertical scale (both axes
 *             together, the window's pixel shape kept), never below d / 3;
 *             the switch's ratio (rd__upscale_set, rd__upscale_ratio)
 * On a Vulkan device (exit 77 without one, after the CPU cases):
 *   marker    the reduction's frame carries RDC_UPSCALE_POINT in list 12
 *             right before the RDC_TARGET of DISPLAY, naming SCENE and
 *             DISPLAY; a state command that changes no state
 *   off       Enhanced 2x with a fade: the marker turned into an RDC_NOP
 *             and the frame replayed again gives DISPLAY and the present
 *             byte for byte (the marker is nothing without the split); no
 *             marker counted, no UPSCALED, the display scale the scene's
 *   test      the upscaler's test at Enhanced 3x, ratio 1.5 (FSR's
 *             Quality): SCENE at 2x (1024 x 1024), DISPLAY at 3x
 *             (1536 x 768), UPSCALED 1536 x 1536 and within 3 levels of
 *             SCENE upscaled bilinearly on the CPU at all but 0.1 % of its
 *             texels; one dispatch and one reduction remapped a frame; the
 *             fades fold off (it folds the same frame with the switch off)
 *   guard     ratio 2 at Enhanced 2x (the scene at 1x), a full-frame sprite
 *             over a black clear: with the jitter (0.25, 0.25) SCENE's
 *             column 0 equals column 1 and row 0 row 1 everywhere (the
 *             edge guard counted); with (-0.25, -0.25) no guard copy
 *   jitter    the phase count 8: each present that draws takes Halton
 *             (2, 3) at its index % 8; a re-shown present (a keep replay)
 *             takes none and is counted; off again, the jitter is cleared
 *   reset     the history reset is the replayed frame's own: four frames
 *             at a settled split, the third a camera cut, dispatched in
 *             order: the reset is given at the third alone (its marker's
 *             u[2] RD_UPSCALE_NO_HISTORY set by the attach, the others 0),
 *             and at the frame after rd__velocity_forget
 *   window    the "window" resolution at 4:3 in a 1024 x 768 output (the
 *             box 1024 x 768 over the 512 x 512 frame: the display scale 2
 *             x 1.5): the scene 1.5 / 1.5 = 1 down; Auto's cap 1 at ratio
 *             1.25 (the scene 1.2 uncapped) gives the scene 1 down and keeps
 *             DISPLAY at the window's 384 rows
 * Any validation error fails the test.  "rd_upscale_test off" runs the
 * scales, marker and off cases alone (rd_upscale_off_mali and
 * rd_upscale_off_tiler: the marker a NOP under a phone's limits and under
 * the tile-based device profile's list walkers, which no corpus dump with a
 * marker covers). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_jitter.h"
#include "rd_upscale.h"
#include "rd_velocity.h"
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

#define GS 512 /* the GS frame: 512 x 512 */
#define OUT_W 1024
#define OUT_H 768

static const uint8_t kBlack[4] = {0, 0, 0, 0x80}, kRed[4] = {0xFF, 0x10, 0x00, 0x80},
                     kGreen[4] = {0x00, 0xF0, 0x20, 0x80}, kBlue[4] = {0x08, 0x30, 0xFF, 0x80},
                     kGrey[4] = {0x80, 0x80, 0x80, 0x80}, kFill[4] = {0x60, 0xA0, 0x30, 0x80},
                     kFade[4] = {0x20, 0x10, 0x40, 0x50};

static RdTex s_tex;
static uint8_t s_texel[16 * 16 * 4];
static uint8_t s_scene[1024 * 1024 * 4];
static uint8_t s_up[1536 * 1536 * 4];
static uint8_t s_dispA[1536 * 768 * 4], s_dispB[1536 * 768 * 4];
static uint8_t s_outA[OUT_W * OUT_H * 4], s_outB[OUT_W * OUT_H * 4];

/* ------------------------------------------------------------- CPU cases */

static int near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static void testScales(void)
{
    float x, y;
    rd__upscale_render_scale(3.0f, 3.0f, 1.5f, 0.0f, &x, &y);
    CHECK(near(x, 2.0f) && near(y, 2.0f), "3x over 1.5: %g x %g", (double)x, (double)y);
    rd__upscale_render_scale(2.0f, 2.0f, 2.0f, 0.0f, &x, &y);
    CHECK(x == 1.0f && y == 1.0f, "2x over 2: %g x %g", (double)x, (double)y);
    rd__upscale_render_scale(1.0f, 1.0f, 1.5f, 0.0f, &x, &y);
    CHECK(x == 1.0f && y == 1.0f, "1x over 1.5 is 1x (never below the GS size): %g x %g", (double)x,
          (double)y);
    rd__upscale_render_scale(2.4f, 2.4f, 1.0f, 0.0f, &x, &y);
    CHECK(near(x, 2.4f) && near(y, 2.4f), "ratio 1 (a native pass): %g x %g", (double)x, (double)y);
    rd__upscale_render_scale(6.0f, 6.0f, 9.0f, 0.0f, &x, &y);
    CHECK(near(x, 2.0f) && near(y, 2.0f), "a ratio over 3 is 3: %g x %g", (double)x, (double)y);
    rd__upscale_render_scale(3.0f, 3.0f, 0.5f, 0.0f, &x, &y);
    CHECK(near(x, 3.0f) && near(y, 3.0f), "a ratio under 1 is 1: %g x %g", (double)x, (double)y);
    /* Auto's cap: the vertical scale at most the cap, the shape kept */
    rd__upscale_render_scale(4.5f, 4.5f, 1.5f, 2.0f, &x, &y);
    CHECK(near(x, 2.0f) && near(y, 2.0f), "4.5x over 1.5 capped at 2: %g x %g", (double)x,
          (double)y);
    rd__upscale_render_scale(2.8125f, 2.41f, 1.5f, 1.0f, &x, &y);
    CHECK(near(y, 1.0f) && near(x, 2.8125f / 2.41f), "the window's shape under the cap: %g x %g",
          (double)x, (double)y);
    rd__upscale_render_scale(4.5f, 4.5f, 1.5f, 5.0f, &x, &y);
    CHECK(near(x, 3.0f) && near(y, 3.0f), "a cap above the scale changes nothing: %g x %g",
          (double)x, (double)y);
    /* never below d / 3 */
    rd__upscale_render_scale(6.0f, 6.0f, 1.5f, 1.0f, &x, &y);
    CHECK(near(x, 2.0f) && near(y, 2.0f), "6x capped at 1 stops at 6 / 3: %g x %g", (double)x,
          (double)y);
    /* the switch's ratio */
    rd__upscale_set(true, 1.7f);
    CHECK(near(rd__upscale_ratio(), 1.7f), "set 1.7: %g", (double)rd__upscale_ratio());
    rd__upscale_set(true, 0.25f);
    CHECK(rd__upscale_ratio() == 1.0f, "set 0.25 is 1: %g", (double)rd__upscale_ratio());
    rd__upscale_set(false, 1.5f);
    CHECK(rd__upscale_ratio() == 0.0f && !rd__upscale_wanted(), "off: ratio 0");
    printf("  scales: d / ratio, at least 1 and d / 3, Auto's cap with the shape kept\n");
}

/* ------------------------------------------------------------ the frames */

static RdScreenVtx vtx(int32_t x, int32_t y, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = x;
    v.y = y;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

static void opaque2D(void)
{
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
}

/* 12.4 GS window coordinates of the frame's (x, y), in 1/16 */
static int32_t gx(int32_t x16)
{
    return (2048 - GS / 2) * 16 + x16;
}

static int32_t gy(int32_t y16)
{
    return (2048 - GS / 2) * 16 + y16;
}

static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4], int32_t u0,
                   int32_t v0, int32_t u1, int32_t v1)
{
    const RdScreenVtx v[2] = {vtx(gx(x0), gy(y0), c, (float)u0, (float)v0),
                              vtx(gx(x1), gy(y1), c, (float)u1, (float)v1)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

static void reduction(void)
{
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    memcpy(pp.rgba, kGrey, 4);
    rd_post(RD_POST_REDUCTION, &pp);
}

/* a Gouraud triangle, a nearest and a bilinear textured sprite, list 11's
 * fade (fade alpha, 0 none), the reduction */
static void richFrame(uint8_t fade)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, GS, GS, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBlack, 0, 0);
    opaque2D();
    rd_texture_off();
    const RdScreenVtx t[3] = {vtx(gx(60 * 16 + 3), gy(40 * 16 + 5), kRed, 0, 0),
                              vtx(gx(450 * 16 + 9), gy(90 * 16 + 1), kGreen, 0, 0),
                              vtx(gx(120 * 16 + 14), gy(470 * 16 + 7), kBlue, 0, 0)};
    rd_screen_prims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_WORLD, 1, 0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(280 * 16, 40 * 16, 344 * 16, 104 * 16, kGrey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(s_tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(300 * 16 + 4, 250 * 16 + 4, 428 * 16 + 4, 378 * 16 + 4, kGrey, 8, 8, 16 * 16 + 8,
           16 * 16 + 8);
    rd_texture_off();
    if (fade) {
        rd_select_list(11);
        RdPostParams p;
        memset(&p, 0, sizeof(p));
        memcpy(p.rgba, kFade, 4);
        p.rgba[3] = fade;
        rd_post(RD_POST_FADE, &p);
    }
    reduction();
    rd_end_frame(0);
}

/* a full-frame untextured sprite over a black clear, then the reduction;
 * cut: after rd_camera_cut */
static void fillFrameCut(int cut)
{
    rd_begin_frame();
    if (cut) {
        rd_camera_cut();
    }
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, GS, GS, 0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kBlack, 0, 0);
    opaque2D();
    rd_texture_off();
    sprite(0, 0, GS * 16, GS * 16, kFill, 0, 0, 0, 0);
    reduction();
    rd_end_frame(0);
}

static void fillFrame(void)
{
    fillFrameCut(0);
}

static void settings(RdPreset preset, float scale, float cap)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = preset;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    s.sceneScale = scale;
    s.upscaleRenderCap = cap;
    rd_set_settings(&s);
}

static int readTarget(RdTargetId id, uint8_t *dst, size_t size, uint32_t *w, uint32_t *h)
{
    if (!rd__read_target(rd_target(id), dst, size, w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return 0;
    }
    return 1;
}

static int readOut(uint8_t *dst)
{
    uint32_t w = 0, h = 0;
    if (!rd__read_present(dst, OUT_W * OUT_H * 4, &w, &h) || w != OUT_W || h != OUT_H) {
        CHECK(0, "present readback (%u x %u)", w, h);
        return 0;
    }
    return 1;
}

/* the last frame's RDC_UPSCALE_POINT in list 12, NULL none; *next the
 * command after it */
static RdCmd *markerOf(const RdFrame *f, const RdCmd **next)
{
    const RdCmdList *cl = &f->lists[12];
    for (uint32_t i = 0; i < cl->count; i++) {
        if (cl->cmds[i].type == RDC_UPSCALE_POINT) {
            *next = i + 1 < cl->count ? &cl->cmds[i + 1] : NULL;
            return &cl->cmds[i];
        }
    }
    *next = NULL;
    return NULL;
}

/* -------------------------------------------------------- device cases */

static void testMarker(void)
{
    settings(RD_PRESET_ORIGINAL, 1.0f, 0.0f);
    richFrame(0);
    const RdFrame *f = rd__last_frame();
    const RdCmd *next = NULL;
    const RdCmd *m = f ? markerOf(f, &next) : NULL;
    CHECK(m != NULL, "marker: none in list 12");
    if (!m) {
        return;
    }
    CHECK(m->u[0] == rd_target(RD_TARGET_SCENE).id && m->u[1] == rd_target(RD_TARGET_DISPLAY).id,
          "marker: names %u -> %u", m->u[0], m->u[1]);
    CHECK(next && next->type == RDC_TARGET && next->u[0] == rd_target(RD_TARGET_DISPLAY).id,
          "marker: not right before the reduction's DISPLAY target");
    CHECK(rd__cmd_is_state(m->type), "marker: not a state command");
    RdStateBlock a = f->startState, b = f->startState;
    CHECK(rd__apply_state(&b, m) && memcmp(&a, &b, sizeof(a)) == 0,
          "marker: rd__apply_state changed the state");
    printf(
        "  marker: RDC_UPSCALE_POINT SCENE -> DISPLAY before the reduction's target, no state\n");
}

static void testOff(void)
{
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    settings(RD_PRESET_ENHANCED, 2.0f, 0.0f);
    const RdUpscaleStats before = *rd__upscale_stats();
    richFrame(0x40);
    if (!readTarget(RD_TARGET_DISPLAY, s_dispA, sizeof(s_dispA), &w, &h) || !readOut(s_outA)) {
        return;
    }
    float dx = 0.0f, dy = 0.0f;
    rd__display_scale(&dx, &dy);
    CHECK(!rd__upscale_point_on() && dx == g_rd.sceneSx && dy == g_rd.sceneSy && dx == 2.0f,
          "off: the display scale %g x %g, the scene's %g x %g", (double)dx, (double)dy,
          (double)g_rd.sceneSx, (double)g_rd.sceneSy);
    CHECK(w == 1024 && h == 512, "off: DISPLAY %ux%u", w, h);
    /* the same frame with the marker a NOP: the same bytes */
    RdFrame *f = (RdFrame *)rd__last_frame();
    const RdCmd *next = NULL;
    RdCmd *m = f ? markerOf(f, &next) : NULL;
    CHECK(m != NULL, "off: no marker");
    if (!m) {
        return;
    }
    m->type = RDC_NOP;
    const bool ok = rd__replay_frame(f, 0, true);
    m->type = RDC_UPSCALE_POINT;
    CHECK(ok, "off: the replay with the marker a NOP");
    if (ok && readTarget(RD_TARGET_DISPLAY, s_dispB, sizeof(s_dispB), &w2, &h2) &&
        readOut(s_outB)) {
        CHECK(w2 == w && h2 == h && memcmp(s_dispA, s_dispB, (size_t)w * h * 4) == 0,
              "off: DISPLAY differs with the marker a NOP");
        CHECK(memcmp(s_outA, s_outB, sizeof(s_outA)) == 0,
              "off: the present differs with the marker a NOP");
    }
    const RdUpscaleStats *after = rd__upscale_stats();
    CHECK(after->points == before.points && after->dispatched == before.dispatched,
          "off: the marker counted (%u -> %u)", before.points, after->points);
    printf("  off: DISPLAY and the present byte for byte with the marker a NOP\n");
}

/* UPSCALED (uw x uh) against SCENE (sw x sh) upscaled bilinearly on the
 * CPU (the blit's sampling: texel centres at i + 1/2, the edges clamped):
 * the texels more than tol levels off in any channel */
static uint32_t bilinearOff(const uint8_t *sc, uint32_t sw, uint32_t sh, const uint8_t *up,
                            uint32_t uw, uint32_t uh, int tol, int *worst)
{
    uint32_t bad = 0;
    *worst = 0;
    for (uint32_t y = 0; y < uh; y++) {
        float fy = ((float)y + 0.5f) * (float)sh / (float)uh - 0.5f;
        fy = fy < 0.0f ? 0.0f : fy;
        int y0 = (int)fy;
        const float ty = fy - (float)y0;
        const int y1 = y0 + 1 < (int)sh ? y0 + 1 : (int)sh - 1;
        y0 = y0 < (int)sh ? y0 : (int)sh - 1;
        for (uint32_t x = 0; x < uw; x++) {
            float fx = ((float)x + 0.5f) * (float)sw / (float)uw - 0.5f;
            fx = fx < 0.0f ? 0.0f : fx;
            int x0 = (int)fx;
            const float tx = fx - (float)x0;
            const int x1 = x0 + 1 < (int)sw ? x0 + 1 : (int)sw - 1;
            x0 = x0 < (int)sw ? x0 : (int)sw - 1;
            int off = 0;
            for (int ch = 0; ch < 4; ch++) {
                const float a = sc[((size_t)y0 * sw + (size_t)x0) * 4 + ch];
                const float b = sc[((size_t)y0 * sw + (size_t)x1) * 4 + ch];
                const float c = sc[((size_t)y1 * sw + (size_t)x0) * 4 + ch];
                const float d = sc[((size_t)y1 * sw + (size_t)x1) * 4 + ch];
                const float v = (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
                const int e = abs((int)up[((size_t)y * uw + x) * 4 + ch] - (int)lrintf(v));
                off = e > off ? e : off;
            }
            *worst = off > *worst ? off : *worst;
            bad += off > tol;
        }
    }
    return bad;
}

static void testUpscale(void)
{
    uint32_t sw = 0, sh = 0, dw = 0, dh = 0, uw = 0, uh = 0;
    /* the fades fold with the switch off, for the comparison below */
    rd__upscale_set(false, 0.0f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f);
    richFrame(0x40);
    const uint32_t foldedOff = g_rdPerf.fadesFolded;

    rd__upscale_set(true, 1.5f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f); /* applied at the next frame, the split with it */
    const RdUpscaleStats before = *rd__upscale_stats();
    richFrame(0x40);
    const uint32_t foldedOn = g_rdPerf.fadesFolded;
    float dx = 0.0f, dy = 0.0f;
    rd__display_scale(&dx, &dy);
    CHECK(rd__upscale_point_on() && near(g_rd.sceneSx, 2.0f) && near(g_rd.sceneSy, 2.0f) &&
              dx == 3.0f && dy == 3.0f,
          "test: render scale %g x %g, display %g x %g", (double)g_rd.sceneSx, (double)g_rd.sceneSy,
          (double)dx, (double)dy);
    const RdUpscaleStats *st = rd__upscale_stats();
    CHECK(st->dispatched == before.dispatched + 1u && st->remapped == before.remapped + 1u &&
              st->refused == before.refused,
          "test: dispatched %u -> %u, remapped %u -> %u, refused %u -> %u", before.dispatched,
          st->dispatched, before.remapped, st->remapped, before.refused, st->refused);
    if (!readTarget(RD_TARGET_SCENE, s_scene, sizeof(s_scene), &sw, &sh) ||
        !readTarget(RD_TARGET_DISPLAY, s_dispA, sizeof(s_dispA), &dw, &dh) ||
        !rd__upscale_read(s_up, sizeof(s_up), &uw, &uh)) {
        CHECK(0, "test: readbacks");
        return;
    }
    CHECK(sw == 1024 && sh == 1024 && dw == 1536 && dh == 768 && uw == 1536 && uh == 1536,
          "test: SCENE %ux%u, DISPLAY %ux%u, UPSCALED %ux%u", sw, sh, dw, dh, uw, uh);
    if (sw == 1024 && sh == 1024 && uw == 1536 && uh == 1536) {
        int worst = 0;
        const uint32_t bad = bilinearOff(s_scene, sw, sh, s_up, uw, uh, 3, &worst);
        CHECK(bad <= uw * uh / 1000u,
              "test: UPSCALED is not SCENE upscaled: %u texels over 3 levels (worst %d)", bad,
              worst);
        printf("  test: UPSCALED 1536x1536 from SCENE 1024x1024, worst %d levels, %u over 3\n",
               worst, bad);
    }
    uint32_t litN = 0;
    for (size_t i = 0; i < (size_t)dw * dh; i++) {
        litN += (s_dispA[i * 4] | s_dispA[i * 4 + 1] | s_dispA[i * 4 + 2]) != 0;
    }
    CHECK(litN > (uint32_t)(dw * dh / 4), "test: DISPLAY drawn (%u lit texels)", litN);
    CHECK(foldedOn == 0, "test: the fades fold folded %u sprites under the upscaler", foldedOn);
    printf("  test: the fades fold %u sprites off, %u on (none under the upscaler)\n", foldedOff,
           foldedOn);
}

static void testGuard(void)
{
    uint32_t w = 0, h = 0;
    rd__upscale_set(true, 2.0f);
    settings(RD_PRESET_ENHANCED, 2.0f, 0.0f);
    rd__upscale_set_phases(0);
    rd__frame_jitter_set(0.25f, 0.25f);
    const RdUpscaleStats before = *rd__upscale_stats();
    fillFrame();
    CHECK(g_rd.sceneSx == 1.0f && g_rd.sceneSy == 1.0f, "guard: the scene at %g x %g",
          (double)g_rd.sceneSx, (double)g_rd.sceneSy);
    if (!readTarget(RD_TARGET_SCENE, s_scene, sizeof(s_scene), &w, &h)) {
        rd__frame_jitter_clear();
        return;
    }
    const RdUpscaleStats *st = rd__upscale_stats();
    CHECK(st->guardCols == before.guardCols + 1u && st->guardRows == before.guardRows + 1u,
          "guard: copies %u -> %u columns, %u -> %u rows", before.guardCols, st->guardCols,
          before.guardRows, st->guardRows);
    int badC = 0, badR = 0;
    for (uint32_t y = 0; y < h; y++) {
        badC += memcmp(&s_scene[(size_t)y * w * 4], &s_scene[((size_t)y * w + 1) * 4], 4) != 0;
    }
    for (uint32_t x = 0; x < w; x++) {
        badR += memcmp(&s_scene[(size_t)x * 4], &s_scene[((size_t)w + x) * 4], 4) != 0;
    }
    CHECK(badC == 0 && badR == 0,
          "guard: column 0 differs from column 1 at %d rows, row 0 from "
          "row 1 at %d columns",
          badC, badR);
    const uint8_t *c0 = &s_scene[((size_t)5 * w) * 4];
    printf("  guard: column 0 and row 0 their neighbours' (column 0 at row 5: %02x %02x %02x)\n",
           c0[0], c0[1], c0[2]);
    rd__frame_jitter_set(-0.25f, -0.25f);
    const RdUpscaleStats mid = *rd__upscale_stats();
    fillFrame();
    CHECK(rd__upscale_stats()->guardCols == mid.guardCols &&
              rd__upscale_stats()->guardRows == mid.guardRows,
          "guard: a jitter left and up copied");
    rd__frame_jitter_clear();
}

static void testJitter(void)
{
    rd__upscale_set(true, 1.5f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f);
    rd__upscale_set_phases(8);
    const uint32_t j0 = rd__upscale_stats()->jittered;
    for (uint32_t i = 0; i < 10; i++) {
        fillFrame();
        float g[2], want[2];
        rd__jitter_halton(i % 8u, want);
        CHECK(rd__frame_jitter_get(g) && g[0] == want[0] && g[1] == want[1],
              "jitter: present %u (%g, %g), want Halton %u (%g, %g)", i, (double)g[0], (double)g[1],
              i % 8u, (double)want[0], (double)want[1]);
    }
    CHECK(rd__upscale_stats()->jittered == j0 + 10u, "jitter: %u presents jittered",
          rd__upscale_stats()->jittered - j0);
    /* a re-shown present: no jitter taken, counted; the next present the
     * sequence's next term */
    const uint32_t r0 = rd__upscale_stats()->reshown;
    rd__upscale_replay_begin(1, true);
    float g[2], want[2];
    rd__jitter_halton(9u % 8u, want);
    CHECK(rd__frame_jitter_get(g) && g[0] == want[0] && rd__upscale_stats()->reshown == r0 + 1u,
          "jitter: the re-shown present moved the jitter");
    fillFrame();
    rd__jitter_halton(10u % 8u, want);
    CHECK(rd__frame_jitter_get(g) && g[0] == want[0] && g[1] == want[1],
          "jitter: after the re-shown present (%g, %g), want term 2", (double)g[0], (double)g[1]);
    /* off: the next replay clears the sequence's jitter */
    rd__upscale_set(false, 0.0f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f);
    fillFrame();
    CHECK(!rd__frame_jitter_get(g), "jitter: still set with the upscaler off");
    rd__upscale_set_phases(0);
    printf("  jitter: Halton (2, 3) at the present's index %% 8, a re-shown present none\n");
}

static void testReset(void)
{
    rd__upscale_set(true, 1.5f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f);
    fillFrame(); /* the split applied, the sizes settled */
    fillFrame();
    static const int kCut[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        const uint32_t r0 = rd__upscale_stats()->resets, d0 = rd__upscale_stats()->dispatched;
        fillFrameCut(kCut[i]);
        const RdUpscaleStats *st = rd__upscale_stats();
        const RdCmd *next = NULL;
        const RdCmd *m = rd__last_frame() ? markerOf(rd__last_frame(), &next) : NULL;
        CHECK(st->dispatched == d0 + 1u, "reset: frame %d not dispatched", i);
        CHECK(st->resets == r0 + (uint32_t)kCut[i],
              "reset: frame %d (cut %d) given %u resets, want %d", i, kCut[i], st->resets - r0,
              kCut[i]);
        CHECK(m && (m->u[2] & RD_UPSCALE_NO_HISTORY) == (kCut[i] ? RD_UPSCALE_NO_HISTORY : 0u),
              "reset: frame %d marker u[2] %u", i, m ? m->u[2] : 0u);
    }
    /* the first frame after the picture before is dropped: no cut, the
     * attach's mark alone */
    rd__velocity_forget();
    const uint32_t r0 = rd__upscale_stats()->resets;
    fillFrame();
    CHECK(rd__upscale_stats()->resets == r0 + 1u, "reset: none after rd__velocity_forget");
    fillFrame();
    CHECK(rd__upscale_stats()->resets == r0 + 1u, "reset: the frame after it reset too");
    rd__upscale_set(false, 0.0f);
    settings(RD_PRESET_ENHANCED, 3.0f, 0.0f);
    fillFrame();
    printf("  reset: at the cut frame alone and after the history was dropped\n");
}

static void testWindow(void)
{
    uint32_t dw = 0, dh = 0;
    /* the window: Enhanced, no scale, the output's box (1024 x 768 at 4:3
     * holds the 512 x 512 frame at 1.5) */
    rd__upscale_set(true, 1.5f);
    settings(RD_PRESET_ENHANCED, 0.0f, 0.0f);
    fillFrame();
    float dx = 0.0f, dy = 0.0f;
    rd__display_scale(&dx, &dy);
    CHECK(near(dy, 1.5f) && near(g_rd.sceneSy, 1.0f),
          "window: display %g x %g, scene %g x %g (want 1.5 and 1)", (double)dx, (double)dy,
          (double)g_rd.sceneSx, (double)g_rd.sceneSy);
    /* Auto's cap at ratio 1.25: the scene 1.2 is capped to 1, the
     * display stays the window's */
    rd__upscale_set(true, 1.25f);
    settings(RD_PRESET_ENHANCED, 0.0f, 1.0f);
    fillFrame();
    rd__display_scale(&dx, &dy);
    CHECK(near(dy, 1.5f) && near(g_rd.sceneSy, 1.0f),
          "window: Auto's cap 1: display %g x %g, scene %g x %g", (double)dx, (double)dy,
          (double)g_rd.sceneSx, (double)g_rd.sceneSy);
    if (readTarget(RD_TARGET_DISPLAY, s_dispA, sizeof(s_dispA), &dw, &dh)) {
        CHECK(dh == (uint32_t)lrintf(256.0f * dy), "window: DISPLAY %ux%u at the window's scale",
              dw, dh);
    }
    printf("  window: display %g x %g, the scene at Auto's cap 1\n", (double)dx, (double)dy);
    rd__upscale_set(false, 0.0f);
    settings(RD_PRESET_ORIGINAL, 1.0f, 0.0f);
    fillFrame();
}

int main(int argc, char **argv)
{
    const bool offOnly = argc > 1 && strcmp(argv[1], "off") == 0;
    testScales();
    for (int i = 0; i < 16 * 16; i++) {
        uint32_t v = (uint32_t)i * 2654435761u + 7u;
        v ^= v >> 15;
        v *= 0x846CA68Bu;
        v ^= v >> 16;
        s_texel[i * 4 + 0] = (uint8_t)v;
        s_texel[i * 4 + 1] = (uint8_t)(v >> 8);
        s_texel[i * 4 + 2] = (uint8_t)(v >> 16);
        s_texel[i * 4 + 3] = 0x80;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = OUT_W;
    s.outputHeight = OUT_H;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    if (!rd_init(GS, GS, &s, NULL)) {
        printf("rd_upscale_test: SKIP: no usable Vulkan device\n");
        return failures ? 1 : 77;
    }
    rd__set_not_implemented_fatal(false); /* counted: none may be replayed (below) */
    s_tex = rd_create_texture(16, 16, s_texel, RD_TEXA_80_80, "upscale test");
    testMarker();
    testOff();
    if (!offOnly) {
        testUpscale();
        testGuard();
        testJitter();
        testReset();
        testWindow();
    }
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%u validation errors", verr);
    rd_shutdown();
    if (failures) {
        printf("rd_upscale_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_upscale_test: ok%s\n", offOnly ? " (the off cases)" : "");
    return 0;
}
