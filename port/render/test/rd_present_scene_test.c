/* rd_present_scene_test.c: the present from SCENE (port/render/rd_present_scene.c;
 * docs/port/RENDER_POST.md "The present from SCENE (off by default until
 * measured)"), CPU only, without a device (rd__init_record_only):
 *
 *   axis    rd__present_scene_axis on the reduction's own hardware pair
 *           (rd_post.c postReduction: corners a quarter pixel before pixel
 *           0, UV 0.5 there, one GS texel a pixel across and two a row down;
 *           rd_blur.c rd__blur_snap_verts's whole-texel snap) at the
 *           corpus's settings: the Pixel's Resolution Auto (848 x 448 GS at
 *           2364 x 1080, full height): SCENE's texel x + 1, y for DISPLAY's
 *           x, y (as measured on its dump); mirrored x - 1; a 16:9 window
 *           (688 x 448 at 1920 x 1080) x + 1; 4x x + 1; with the same Full
 *           pixel crop on both x + 1; Enhanced 2x refused (the PS2's quarter
 *           pixel is half a texel there: two columns blended); not full
 *           height refused (two SCENE rows a DISPLAY row); a pair that
 *           leaves a texel refused
 *   bound   the derivation's model: one fold step between levels (the
 *           brightness at 7/128, a fade at 64/128 and 121/128, toward
 *           white and black; the fixed-function blend's rounding r 64 and
 *           the exact blend's floor r 0; gs_math.hlsli gs_fold_ch) and the
 *           identity texture function (the tint 0x80): for every pair of
 *           levels and every weight of the filter in 1/32 (in 1/256 for the
 *           phone's brightness) the present of DISPLAY, the filter of the
 *           stepped texels rounded, against the present from SCENE, the
 *           step of the filter's rounded texel: at most one level apart;
 *           the four taps of a bilinear at random weights: at most one
 *           level; steps of 0/128 and 128/128: the same level always
 *   why     rd__present_scene_why: every reader alone keeps the reduction
 *           with its reason (the switch, the device, a pass window, scale
 *           1, MSAA, GS state as shader data, an upscaler, the display
 *           layer, the CRT filter, Downsampling, the doubled lines, the
 *           effects depth, a capture armed, the window's reduce, the border
 *           closed, its repeated columns, the form, the map, the bars, a
 *           touch after); none: taken
 *   settle  rd__present_scene_settle on frames recorded as the game
 *           records them (rd_post): a frame of play (SCENE drawn in list 0,
 *           the reduction in list 12): DISPLAY dropped (written whole by the
 *           reduction's black sprite before anything reads it); a keep
 *           frame (list 11's KEEP sprite reads DISPLAY): drawn (a read); the
 *           motion blur's held copy (rd_interp.c's copy of DISPLAY into
 *           DISPLAY_HELD at the frame's head): drawn (a read); the still
 *           re-present's empty keep frame (rd_reshow.c): kept (the present
 *           reads SCENE again); a frame drawing on SCENE alone: drawn
 *           (SCENE). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "test/hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_replay.h"
#include "../../test/ico_check.h"

/* ------------------------------------------------------------------ axis */

/* the reduction's hardware pair (rd_post.c reductionSprite, its mirrored
 * pair with mirror), S snapped as rd__blur_snap_verts does at SCENE scale
 * sx, in v */
static void reductionPair(int32_t W, int32_t H, float sx, bool mirror, RdScreenVtx v[2])
{
    memset(v, 0, 2 * sizeof(*v));
    const int32_t x0 = -W / 2 * 16 + 0x8000 - 4, y0 = -H / 4 * 16 + 0x8000 - 4;
    v[0].x = x0;
    v[0].y = y0;
    v[1].x = x0 + W * 16;
    v[1].y = y0 + H / 2 * 16;
    v[0].s = (float)(8 - (mirror ? 8 : 0));
    v[1].s = (float)(W * 16 + 8 - (mirror ? 8 : 0));
    v[0].t = 8.0f;
    v[1].t = (float)(H * 16 + 8);
    if (sx != floorf(sx)) {
        const float quarter = sx * 0.25f;
        float d = (floorf(quarter + 0.5f) - quarter) / sx * 16.0f;
        if (mirror) {
            d = -d;
        }
        v[0].s += d;
        v[1].s += d;
    }
}

/* the reduction of a W x H GS frame (SCENE tw x th texels, crop ox texels
 * each side on both targets) into DISPLAY (W x H / 2 GS, full height: th
 * texels, else th / 2): the offsets, false when refused */
static bool mapCase(int32_t W, int32_t H, uint32_t tw, uint32_t th, float ox, bool full,
                    bool mirror, int32_t narrow16, int32_t *dx, int32_t *dy)
{
    const float sx = ((float)tw + 2.0f * ox) / (float)W, sy = (float)th / (float)H;
    RdScreenVtx v[2];
    reductionPair(W, H, sx, mirror, v);
    v[0].x += narrow16;
    const uint32_t dth = full ? th : th / 2u;
    const double dsy = (double)dth / (double)(H / 2);
    return rd__present_scene_axis(v[0].x, v[1].x, v[0].s, v[1].s, 16 * (2048 - W / 2), sx, ox, sx,
                                  ox, tw, dx) &&
           rd__present_scene_axis(v[0].y, v[1].y, v[0].t, v[1].t, 16 * (2048 - H / 4), dsy, 0.0, sy,
                                  0.0, dth, dy);
}

static void checkAxis(void)
{
    int32_t dx = 99, dy = 99;
    CHECK(mapCase(848, 448, 2364, 1080, 0.0f, true, false, 0, &dx, &dy) && dx == 1 && dy == 0,
          "axis: the Pixel's 2364x1080: (%d, %d), (1, 0) expected", dx, dy);
    CHECK(mapCase(848, 448, 2364, 1080, 0.0f, true, true, 0, &dx, &dy) && dx == -1 && dy == 0,
          "axis: mirrored: (%d, %d), (-1, 0) expected", dx, dy);
    CHECK(mapCase(688, 448, 1920, 1080, 0.0f, true, false, 0, &dx, &dy) && dx == 1 && dy == 0,
          "axis: a 16:9 window: (%d, %d), (1, 0) expected", dx, dy);
    CHECK(mapCase(512, 448, 2048, 1792, 0.0f, true, false, 0, &dx, &dy) && dx == 1 && dy == 0,
          "axis: 4x: (%d, %d), (1, 0) expected", dx, dy);
    CHECK(mapCase(848, 448, 2340, 1080, 6.0f, true, false, 0, &dx, &dy) && dx == 1 && dy == 0,
          "axis: a Full pixel crop on both targets: (%d, %d), (1, 0) expected", dx, dy);
    CHECK(!mapCase(512, 448, 1024, 896, 0.0f, true, false, 0, &dx, &dy),
          "axis: 2x taken (its sample lies between two columns)");
    CHECK(!mapCase(848, 448, 2364, 1080, 0.0f, false, false, 0, &dx, &dy),
          "axis: half height taken (two SCENE rows a DISPLAY row)");
    CHECK(!mapCase(848, 448, 2364, 1080, 0.0f, true, false, 32, &dx, &dy),
          "axis: a pair leaving DISPLAY's first texels taken");
    printf("  axis: the reduction's map at the corpus's settings\n");
}

/* ----------------------------------------------------------------- bound */

/* g of one channel: one fold step (colour cs, factor k / 128, rounding r) */
static int stepOf(uint32_t cs, uint32_t k, int r, int c)
{
    const uint32_t s = cs | (cs << 8) | (cs << 16) | (k << 24);
    return (int)gs_fold_ch(s, 0u, (uint32_t)c, r);
}

/* the present of DISPLAY (the filter of the stepped texels, rounded) less
 * the present from SCENE (the step of the rounded filter) over every pair
 * of levels at weights w / 256, w = 0, step .. 256: the largest
 * difference; *differ the cases not equal */
static int boundOf(uint32_t cs, uint32_t k, int r, int step, long *differ, long *cases)
{
    int g[256];
    for (int c = 0; c < 256; c++) {
        g[c] = stepOf(cs, k, r, c);
    }
    int worst = 0;
    for (int a = 0; a < 256; a++) {
        for (int b = 0; b < 256; b++) {
            for (int w = 0; w <= 256; w += step) {
                const int old = (w * g[a] + (256 - w) * g[b] + 128) >> 8;
                const int now = g[(w * a + (256 - w) * b + 128) >> 8];
                const int d = old > now ? old - now : now - old;
                worst = d > worst ? d : worst;
                *differ += d != 0;
                (*cases)++;
            }
        }
    }
    return worst;
}

static void checkBound(void)
{
    static const uint32_t kStep[] = {7u, 64u, 121u};
    static const uint32_t kColour[] = {255u, 0u};
    long differ = 0, cases = 0;
    int worst = 0;
    for (int r = 0; r <= 64; r += 64) {
        for (size_t i = 0; i < sizeof(kStep) / sizeof(kStep[0]); i++) {
            for (size_t c = 0; c < sizeof(kColour) / sizeof(kColour[0]); c++) {
                const int b = boundOf(kColour[c], kStep[i], r, 8, &differ, &cases);
                CHECK(b <= 1, "bound: step (%u at %u/128, r %d): %d levels", kColour[c], kStep[i],
                      r, b);
                worst = b > worst ? b : worst;
            }
        }
    }
    /* the phone's brightness (white at 7/128, the fixed-function rounding) at
     * every weight of an 8-bit filter */
    const int phone = boundOf(255u, 7u, 64, 1, &differ, &cases);
    CHECK(phone <= 1, "bound: the brightness at 7/128: %d levels", phone);
    /* steps of 0/128 and 128/128 store one value or the texel: no change */
    long same = 0, sameCases = 0;
    CHECK(boundOf(255u, 0u, 64, 4, &same, &sameCases) == 0 && same == 0,
          "bound: a step of 0/128 changed a level");
    CHECK(boundOf(40u, 128u, 64, 4, &same, &sameCases) == 0 && same == 0,
          "bound: a step of 128/128 changed a level");
    /* the bilinear's four taps at random 8-bit weights */
    uint32_t seed = 12345u;
    int worst2 = 0;
    long differ2 = 0;
    int g[256];
    for (int c = 0; c < 256; c++) {
        g[c] = stepOf(255u, 7u, 64, c);
    }
    for (int n = 0; n < 1000000; n++) {
        int t[4];
        for (int k = 0; k < 4; k++) {
            seed = seed * 1664525u + 1013904223u;
            t[k] = (int)(seed >> 24);
        }
        seed = seed * 1664525u + 1013904223u;
        const int wx = (int)((seed >> 8) % 257u), wy = (int)((seed >> 20) % 257u);
        const long top = (long)(256 - wx) * g[t[0]] + (long)wx * g[t[1]];
        const long bot = (long)(256 - wx) * g[t[2]] + (long)wx * g[t[3]];
        const int old = (int)(((256 - wy) * top + wy * bot + 32768) >> 16);
        const long topS = (long)(256 - wx) * t[0] + (long)wx * t[1];
        const long botS = (long)(256 - wx) * t[2] + (long)wx * t[3];
        const int now = g[(int)(((256 - wy) * topS + wy * botS + 32768) >> 16)];
        const int d = old > now ? old - now : now - old;
        worst2 = d > worst2 ? d : worst2;
        differ2 += d != 0;
    }
    CHECK(worst2 <= 1, "bound: the bilinear's four taps: %d levels", worst2);
    printf("  bound: one step between levels: at most %d level (%ld of %ld cases a level apart); "
           "the bilinear at most %d (%ld of 1000000)\n",
           worst > phone ? worst : phone, differ, cases, worst2, differ2);
}

/* ------------------------------------------------------------------- why */

static void checkWhy(void)
{
    RdPresentSceneIn ok;
    memset(&ok, 0, sizeof(ok));
    ok.on = ok.device = ok.scaled = ok.borderOpen = ok.formOk = ok.mapOk = true;
    CHECK(rd__present_scene_why(&ok) == RD_PS_WHY_NONE, "why: the plain case not taken");
    RdPresentSceneIn in = ok;
    in.on = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_OFF, "why: the switch off");
    in = ok;
    in.device = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_DEVICE, "why: no device");
    in = ok;
    in.window = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_WINDOW, "why: a pass window");
    in = ok;
    in.scaled = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_SCALE1, "why: scale 1 (Original)");
    in = ok;
    in.msaa = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_MSAA, "why: MSAA");
    in = ok;
    in.uber = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_UBER, "why: GS state as shader data");
    in = ok;
    in.upscaler = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_UPSCALER, "why: FSR 1.0 or a temporal upscaler");
    in = ok;
    in.layer = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_LAYER, "why: the display layer");
    static const int kPresent[] = {RD_PS_WHY_CRT,     RD_PS_WHY_DOWNSAMPLE, RD_PS_WHY_LINES,
                                   RD_PS_WHY_DEPTH,   RD_PS_WHY_CAPTURE,    RD_PS_WHY_REDUCE,
                                   RD_PS_WHY_UPSCALER};
    for (size_t i = 0; i < sizeof(kPresent) / sizeof(kPresent[0]); i++) {
        in = ok;
        in.presentWhy = kPresent[i];
        CHECK(rd__present_scene_why(&in) == kPresent[i], "why: the present's %s",
              rd__present_scene_why_text(kPresent[i]));
    }
    in = ok;
    in.borderOpen = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_BORDER, "why: the border closed");
    in = ok;
    in.strips = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_STRIPS, "why: the repeated columns");
    in = ok;
    in.formOk = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_FORM, "why: the form");
    in = ok;
    in.mapOk = false;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_RESAMPLE, "why: the map");
    in = ok;
    in.bars = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_BARS, "why: the bars");
    in = ok;
    in.after = true;
    CHECK(rd__present_scene_why(&in) == RD_PS_WHY_AFTER, "why: a touch after the reduction");
    for (int k = 0; k < RD_PS_WHY_COUNT; k++) {
        const char *t = rd__present_scene_why_text(k);
        CHECK(t && t[0] && strcmp(t, "?") != 0, "why: reason %d has no words", k);
    }
    printf("  why: each reader keeps the reduction with its reason\n");
}

/* ---------------------------------------------------------------- settle */

/* a frame as the game records one: SCENE cleared and a sprite drawn in list
 * 0; with keepRead list 11's KEEP sprite reading DISPLAY; with reduction
 * list 12's reduction; with sceneOnly no reduction */
static const RdFrame *record(bool keepRead, bool reduction)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 448,
                  RD_TARGET_OFFSET);
    static const uint8_t grey[4] = {60, 70, 80, 0x80};
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    RdPostParams p;
    if (keepRead) {
        rd_select_list(11);
        memset(&p, 0, sizeof(p));
        p.dst = rd_target(RD_TARGET_SCENE);
        rd_post(RD_POST_KEEP, &p);
    }
    if (reduction) {
        rd_select_list(12);
        memset(&p, 0, sizeof(p));
        static const uint8_t tint[4] = {0x80, 0x80, 0x80, 0x80};
        memcpy(p.rgba, tint, 4);
        rd_post(RD_POST_REDUCTION, &p);
    }
    rd_end_frame(0);
    return rd__last_frame();
}

static void checkSettle(void)
{
    const uint32_t disp = rd_target(RD_TARGET_DISPLAY).id, scene = rd_target(RD_TARGET_SCENE).id;
    int why = -1;
    const RdFrame *play = record(false, true);
    CHECK(play && rd__present_scene_settle(play, 0, disp, scene, &why) == RD_PS_SETTLE_DROP,
          "settle: a frame of play does not drop the debt (why %d)", why);
    const RdFrame *keep = record(true, true);
    CHECK(keep && rd__present_scene_settle(keep, 0, disp, scene, &why) == RD_PS_SETTLE_DRAW &&
              why == RD_PS_WHY_READ,
          "settle: a keep frame does not draw DISPLAY first (why %d)", why);
    const RdFrame *sceneOnly = record(false, false);
    CHECK(sceneOnly &&
              rd__present_scene_settle(sceneOnly, 0, disp, scene, &why) == RD_PS_SETTLE_DRAW &&
              why == RD_PS_WHY_SCENE,
          "settle: a frame drawing on SCENE alone does not draw DISPLAY (why %d)", why);
    /* the motion blur's held copy: rd_interp.c's copy of DISPLAY into
     * DISPLAY_HELD at the head of a frame of play */
    static RdFrame held;
    memset(&held, 0, sizeof(held));
    rd__reset_state_block(&held.startState);
    held.number = 30;
    RdCmd *c = rd__st_push(&held, 0, RDC_COPY);
    CHECK(c != NULL, "settle: the held copy's frame built");
    if (c) {
        c->u[0] = disp;
        c->u[1] = rd_target(RD_TARGET_DISPLAY_HELD).id;
    }
    CHECK(rd__present_scene_settle(&held, 0, disp, scene, &why) == RD_PS_SETTLE_DRAW &&
              why == RD_PS_WHY_READ,
          "settle: the held copy does not draw DISPLAY first (why %d)", why);
    rd__frame_free(&held);
    /* the still re-present: the empty keep frame (lists 11 and 12 empty) */
    static RdFrame empty;
    memset(&empty, 0, sizeof(empty));
    rd__reset_state_block(&empty.startState);
    empty.number = 31;
    empty.keep = 1;
    CHECK(rd__present_scene_settle(&empty, 1, disp, scene, &why) == RD_PS_SETTLE_KEEP,
          "settle: the empty keep frame does not keep the debt (why %d)", why);
    printf("  settle: play drops, a keep frame and the held copy draw, the re-present keeps\n");
}

int main(void)
{
    checkAxis();
    checkBound();
    checkWhy();
    if (!rd__init_record_only(512, 448)) {
        printf("rd_present_scene_test: no context\n");
        return 1;
    }
    checkSettle();
    rd_shutdown();
    return ico_check_summary("rd_present_scene_test");
}
