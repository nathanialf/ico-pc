/* rd_crack_test.c: the crack filler above 1x (rd_crack_fill.c, issue 26).
 *
 *   rd_crack_test 2|4|1|frac|original [msaa|backdrop|fold]
 *
 * On a Vulkan device (77 without one, or without MSAA4 when msaa is
 * given), a 640 x 448 frame under Enhanced at scale 2, 4 or 1, at the
 * window's own 1800 x 1080 texels (frac: 2.8125 x 2.41) or under the
 * Original preset, recorded through the renderer's calls and replayed
 * twice (after a first replay that the fog's probe takes), the filler off
 * and then as the environment has it
 * (ICO_RD_CRACK_FILL), SCENE read back after the fog:
 *   cracks  two flat triangles that share a long diagonal edge, the second
 *           one's edge 1/16 GS pixel to the right of the first's (the
 *           form of DIVERGENCES.md VD-20: one world edge truncated through
 *           two node matrices): without the filler texels along the edge
 *           show the fog's colour of the clear (GS Z 0, the table's entry
 *           0) at 2x and above; with it none, each filled texel the colour
 *           of a written texel at most the reach away on its row or its
 *           column after the fog (the fill is drawn after lists 4-6), its
 *           depth unchanged (the fill writes no depth);
 *   slit    two sprites 1.5 GS pixels apart (3 texels at 2x, 6 at 4x, 4 at
 *           2.8125: wider than the reach): the slit (its texels half a
 *           texel in from its sides) keeps the clear's fog, filler or not;
 *   fold    below them a concave fold: two strips meeting in a vertical
 *           line, each strip's depth falling steeply towards it (35 % of
 *           the fold's depth a texel at 2x), so the texels along the fold
 *           lie behind both neighbours, each on one of the two planes:
 *           drawn, not a crack, never changed;
 * with backdrop, a far plane covers the frame first (GS Z 0x200000, its
 * own colour, Z written: a stage's backdrop) and the two triangles lie at
 * GS Z 0xA00000 and 0xA40000 (two parts of one surface): the cracks hold
 * the backdrop's depth and colour, not the clear's, and are filled the
 * same (the discontinuity form), while the slit keeps the backdrop;
 *   rest    every texel the first replay drew is the same in both, colour
 *           and depth: the pictures differ only at the cracks' colour;
 *   counts  the replay with the filler drew one fill pass and its count
 *           (rd__crack_fill_counts, every replay counted) equals the
 *           texels it changed;
 * at 1x, under Original, under MSAA or with ICO_RD_CRACK_FILL=0 no fill
 * pass is drawn and the two replays are byte-identical.  With msaa
 * (MSAA4; the frame has no shadow, so the epoch is still open at the fog
 * and the filler is not drawn: rd_crack_resolved_msaa2/4 fill a corpus
 * room after the resolve) the frame is replayed again without
 * anti-aliasing and without the filler: along the edge the MSAA picture
 * holds at most 5 % of the texels with the clear's depth (the fog's table
 * entry 0) that the frame without anti-aliasing holds (the covered-majority
 * resolve: a pixel takes the clear's depth only when more than half its
 * samples lay in the crack, not when one did), and the slit,
 * wider than a pixel, keeps the clear's fog on every texel.  With fold (the
 * fog's local read: a tile-based profile, ICO_VK_FAKE_LIMITS=moltenvk, or
 * ICO_RD_FOG_PATH=local; 77 without local read; the name is the old
 * fold's) the fog draws in the scene pass and the fill after lists 4-6:
 * the replay with the filler drew one fill there (RdCrackCounts.late), the
 * fog drew by the local read (RdPerfRecord.fogLocalDraws) in that replay,
 * and the replay has one render pass more than without the filler
 * (besides the count's: the fill's own, the scene pass not broken at the
 * fog); then the fog's path is set to the depth copy (the desktop's form:
 * the fog on the colour alone) and the frame replayed with the filler:
 * SCENE's colour and depth equal the local read's replay byte for byte.
 * With glow (list 7: the glow's cleanUpFB, two sprites into SCENE that
 * write its alpha alone) or aura (list 8: one colour into WORK0, pasted
 * over SCENE through (Cs - Cd) * As + Cd; the fx local read on) the fill
 * goes where rd__crack_fill_place puts it, after cleanUpFB with RGB alone
 * or as the first draw of the paste's pass, and the frame replayed with the
 * place pinned to the start of list 7 (rd__crack_fill_set_place, the
 * place the fill had before) has SCENE's colour and depth the same bytes and
 * more render passes (checkPlace).
 * The log lines are matched by test/rd_crack.cmake. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_late_pass.h"
#include "vk/rhi_vk.h"

/* rd_crack_fill.c's FILLSWAP and CRACKTAP (declared in rd_replay.h, which
 * needs the shaders' include path this test does not have) */
bool rd__crack_swap_selftest(void);
int rd__crack_swap_test(void);
void rd__crack_swap_force(int on);

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

#define GW 640
#define GH 448

/* the shapes in GS pixels from the target's top-left */
#define EDGE_X0 300.0 /* the shared edge from (EDGE_X0, TOP) to (LEFT, BOTTOM) */
#define LEFT 40.0
#define TOP 40.0
#define BOTTOM 400.0
#define BAND0 80.0 /* the rows measured along the edge, away from its ends */
#define BAND1 360.0
#define SLIT0 500.0 /* the slit between the sprites, [SLIT0, SLIT1) */
#define SLIT1 501.5

static const uint8_t kClear[4] = {30, 60, 90, 0x80};
static const uint8_t kTri1[4] = {200, 40, 40, 0x80};
static const uint8_t kTri2[4] = {40, 200, 40, 0x80};
static const uint8_t kRect[4] = {40, 40, 200, 0x80};
static const uint8_t kBack[4] = {120, 90, 30, 0x80};

/* the fold's band and its line (GS pixels), its depths */
#define FOLD_TOP 404.0
#define FOLD_BOTTOM 444.0
#define FOLD_X 320.0
#define FOLD_HALF 20.0
#define FOLD_Z_OUT 0x00F00000u
#define FOLD_Z_IN 0x00100000u

static bool s_backdrop; /* the far plane first (argv[2] backdrop) */

/* argv[2] held: the left crack's GS x (column 2, the border repeat's
 * source: texel 5's point at 2x) */
#define HELD_X 2.5

/* argv[2] glow or aura: after the fog the glow's cleanUpFB (list 7) or the
 * aura's paste (list 8) as staticBlur.c draws them */
enum { AFTER_NONE, AFTER_GLOW, AFTER_AURA, AFTER_HELD, AFTER_JOIN, AFTER_HELDGLOW, AFTER_DEFER };

static int s_after;

/* held, heldglow or defer: the late pass holds the fill (rd_late_pass.c) */
static bool held(void)
{
    return s_after == AFTER_HELD || s_after == AFTER_HELDGLOW || s_after == AFTER_DEFER;
}

/* the late pass's switch for them: the hold (and for defer the glow's paste
 * deferred) whatever the glow's plan (these frames hold no chain the light
 * plan reads) */
static int heldBits(void)
{
    return (int)((s_after == AFTER_DEFER ? RD_LATE_FILL | RD_LATE_DEFER : RD_LATE_FILL) |
                 RD_LATE_ANY_PLAN);
}

static RdScreenVtx vtx(double x, double y, uint32_t z, const uint8_t c[4])
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = (2048 - GW / 2) * 16 + (int32_t)(x * 16.0 + 0.5);
    v.y = (2048 - GH / 2) * 16 + (int32_t)(y * 16.0 + 0.5);
    v.z = z;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* rd_fog_selftest_test's table */
static void lutOf(int i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97 + 13);
    out[2] = (uint8_t)(255 - i);
    out[3] = (uint8_t)(0x10 + i * 0x70 / 255);
}

/* a staticBlur sprite over the w x h GS pixels of the target in force */
static void blurSprite(RdPostKind kind, uint32_t w, uint32_t h, const uint8_t rgba[4], uint32_t z,
                       float tw, float th)
{
    RdPostParams q;
    memset(&q, 0, sizeof(q));
    memcpy(q.rgba, rgba, 4);
    q.z = z;
    q.rect[0] = (float)(0x8000 - w * 8);
    q.rect[1] = (float)(0x8000 - h * 8);
    q.rect[2] = (float)(0x8000 + w * 8);
    q.rect[3] = (float)(0x8000 + h * 8);
    q.uv[0] = q.uv[1] = 8.0f;
    q.uv[2] = tw * 16.0f - 8.0f;
    q.uv[3] = th * 16.0f - 8.0f;
    q.scalar[0] = tw > 1.0f ? tw : 1024.0f;
    q.scalar[1] = th > 1.0f ? th : 1024.0f;
    q.scalar[2] = 1.0f;
    q.exactInt = 1;
    rd_post(kind, &q);
}

/* after the fog: s_after's sprites.  glow (staticBlur.c cleanUpFB, list 7):
 * into SCENE, untextured, Cs * As + Cd with RGBAQ 0, 0, 0, 0 under Z ALWAYS
 * (the alpha 0 everywhere, RGB kept), then 0, 0, 0, 192 at Z 1 under Z
 * GEQUAL, Z write off.  aura (list 8): WORK0 filled with one colour by a
 * sprite, then pasteFeedBackAreaToFB: WORK0 over SCENE, MODULATE, RGBAQ
 * 64, 64, 64, 128, (Cs - Cd) * As + Cd, Z ALWAYS, Z write off */
static void recordAfter(RdTarget scene)
{
    static const uint8_t kNone[4] = {0, 0, 0, 0}, kMark[4] = {0, 0, 0, 192};
    static const uint8_t kFeed[4] = {90, 160, 30, 0x20}, kPaste[4] = {64, 64, 64, 128};
    /* the sprites' state recorded in their own list, as staticBlur.c sets
     * it: lists 7 and 8 open with state of their own (this frame's dump:
     * TEST, ZWRITE on, FBA and TEXA at their heads), so state recorded in
     * list 4 after the fog never reached the sprites: they drew with Z
     * write on, which no room's glow or aura paste does */
    const int list = s_after == AFTER_AURA || s_after == AFTER_JOIN ? 8 : 7;
    rd_select_list(list);
    rd_texture_off();
    rd_pabe(0);
    rd_fba(0);
    rd_z_write(0);
    rd_gouraud(0);
    if (s_after == AFTER_GLOW || held()) {
        rd_set_target(scene, scene, GW, GH, 0);
        rd_abe(1);
        rd_blend_func(RD_BLEND_CS_AS_ADD_CD, 0);
        rd_test_gs(RD_TEST_Z_ALWAYS);
        blurSprite(RD_POST_FLARE, GW, GH, kNone, 0, 0.0f, 0.0f);
        rd_test_gs(RD_TEST_Z_GEQUAL);
        blurSprite(RD_POST_FLARE, GW, GH, kMark, 1, 0.0f, 0.0f);
        if (s_after == AFTER_GLOW) {
            return;
        }
        /* held: a sprite into WORK0 (another target after the glow's alpha:
         * the fill's snapshot there), the glow's mask (SCENE's alpha into
         * WORK2, MODULATE by RGBAQ 0, 0, 0, 128: SCENE's border repeat just
         * before it, the frame 640 GS pixels wide), then list 8's paste of
         * WORK0 over SCENE, the aura's form, which takes the held fill */
        static const uint8_t kMask[4] = {0, 0, 0, 128};
        const RdTarget work0 = rd_target(RD_TARGET_WORK0), work2 = rd_target(RD_TARGET_WORK2);
        rd_set_target(work0, (RdTarget){0}, 256, 128, 0);
        rd_texture_off();
        rd_abe(0);
        rd_test_gs(RD_TEST_Z_ALWAYS);
        blurSprite(RD_POST_FLARE, 256, 128, kFeed, 0, 0.0f, 0.0f);
        rd_set_target(work2, (RdTarget){0}, GW, GH, 0);
        rd_texture(rd_target_texture(scene, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
        blurSprite(RD_POST_FLARE, GW, GH, kMask, 0, (float)GW, (float)GH);
        if (s_after == AFTER_HELDGLOW || s_after == AFTER_DEFER) {
            /* heldglow: a lamp's stand-in (a sprite into WORK2 under a Z
             * GEQUAL test of SCENE's depth, no Z write), then the glow's
             * paste: WORK0 added over SCENE through Cs * FIX + Cd, FIX 128,
             * no Z test (the fx plan's premultiplied add), which takes the
             * held fill */
            static const uint8_t kLamp[4] = {200, 180, 60, 0x80}, kGlow[4] = {128, 128, 128, 128};
            rd_set_target(work2, scene, GW, GH, 0);
            rd_texture_off();
            rd_abe(0);
            rd_z_write(0);
            rd_test_gs(RD_TEST_Z_GEQUAL);
            RdScreenVtx lamp[2] = {vtx(120.0, 60.0, 0x00100000u, kLamp),
                                   vtx(200.0, 120.0, 0x00100000u, kLamp)};
            rd_screen_prims(RD_PRIM_SPRITES, lamp, 2, RD_SPACE_WORLD, 1, 0);
            rd_set_target(scene, scene, GW, GH, 0);
            rd_texture(rd_target_texture(work0, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
            rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
            rd_test_gs(RD_TEST_Z_ALWAYS);
            rd_abe(1);
            rd_blend_func(RD_BLEND_CS_FIX_ADD_CD, 128);
            blurSprite(RD_POST_FLARE, GW, GH, kGlow, 0, 256.0f, 128.0f);
            if (s_after == AFTER_HELDGLOW) {
                return;
            }
            /* defer: the aura's paste after the glow's (the paste deferred to
             * its pass, the fill held on to it) */
        }
        rd_select_list(8);
        rd_texture_off();
        rd_pabe(0);
        rd_fba(0);
        rd_z_write(0);
        rd_gouraud(0);
        rd_test_gs(RD_TEST_Z_ALWAYS);
        rd_set_target(scene, scene, GW, GH, 0);
        rd_texture(rd_target_texture(work0, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
        rd_abe(1);
        rd_blend_func(RD_BLEND_LERP_AS_ALT, 0);
        blurSprite(RD_POST_AURA, GW, GH, kPaste, 0, 256.0f, 128.0f);
        return;
    }
    const RdTarget work = rd_target(RD_TARGET_WORK0);
    rd_set_target(work, (RdTarget){0}, 256, 128, 0);
    rd_abe(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    blurSprite(RD_POST_AURA, 256, 128, kFeed, 0, 0.0f, 0.0f);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_texture(rd_target_texture(work, RD_VIEW_RGBA), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_sampler_filter(RD_FILTER_LINEAR, RD_FILTER_LINEAR);
    rd_abe(1);
    rd_blend_func(RD_BLEND_LERP_AS_ALT, 0);
    blurSprite(RD_POST_AURA, GW, GH, kPaste, 0, 256.0f, 128.0f);
    if (s_after == AFTER_JOIN) {
        /* list 11 after the paste (the paused frame's: no feedback read
         * between): two commands of an untextured blended sprite into SCENE
         * under Z ALWAYS without a Z write, SCENE's depth bound */
        static const uint8_t kMenu[4] = {180, 170, 40, 0x40};
        rd_select_list(11);
        rd_set_target(scene, scene, GW, GH, 0);
        rd_texture_off();
        rd_pabe(0);
        rd_fba(0);
        rd_z_write(0);
        rd_test_gs(RD_TEST_Z_ALWAYS);
        rd_abe(1);
        rd_blend_func(RD_BLEND_LERP_AS, 0);
        /* two commands (each joins: rd_late_pass.c counts a command), right of
         * the slit, away from the cracks and the border's columns */
        RdScreenVtx m[2] = {vtx(605.0, 50.0, 0, kMenu), vtx(620.0, 90.0, 0, kMenu)};
        rd_screen_prims(RD_PRIM_SPRITES, m, 2, RD_SPACE_WORLD, 1, 0);
        RdScreenVtx n[2] = {vtx(610.0, 100.0, 0, kMenu), vtx(630.0, 130.0, 0, kMenu)};
        rd_screen_prims(RD_PRIM_SPRITES, n, 2, RD_SPACE_WORLD, 1, 0);
    }
}

static void recordFrame(void)
{
    static uint8_t lut[256 * 4];
    const RdTarget scene = rd_target(RD_TARGET_SCENE);
    rd_begin_frame();
    rd_select_list(0);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_clear_target(scene, kClear, 1, 0);
    rd_texture_off();
    rd_abe(0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(1);
    rd_fba(0);
    rd_gouraud(0);
    if (s_backdrop) {
        /* a stage's far plane over the whole frame, Z written */
        RdScreenVtx b[2] = {vtx(0.0, 0.0, 0x00200000u, kBack), vtx(GW, GH, 0x00200000u, kBack)};
        rd_screen_prims(RD_PRIM_SPRITES, b, 2, RD_SPACE_WORLD, 1, 0);
    }
    /* the crack: the second triangle's shared edge one 12.4 step right;
       over the backdrop the two parts of one surface lie near each other */
    const double d = 1.0 / 16.0;
    const uint32_t z1 = s_backdrop ? 0x00A00000u : 0x00600000u;
    const uint32_t z2 = s_backdrop ? 0x00A40000u : 0x00A00000u;
    RdScreenVtx t[6] = {vtx(LEFT, TOP, z1, kTri1),       vtx(EDGE_X0, TOP, z1, kTri1),
                        vtx(LEFT, BOTTOM, z1, kTri1),    vtx(EDGE_X0 + d, TOP, z2, kTri2),
                        vtx(EDGE_X0, BOTTOM, z2, kTri2), vtx(LEFT + d, BOTTOM, z2, kTri2)};
    rd_screen_prims(RD_PRIM_TRIANGLES, t, 6, RD_SPACE_WORLD, 1, 0);
    /* the fold: two strips sharing the line FOLD_X (the same vertices),
       each strip's depth falling linearly towards it */
    const double fl = FOLD_X - FOLD_HALF, fr = FOLD_X + FOLD_HALF;
    RdScreenVtx fo[12] = {
        vtx(fl, FOLD_TOP, FOLD_Z_OUT, kTri1),       vtx(FOLD_X, FOLD_TOP, FOLD_Z_IN, kTri1),
        vtx(fl, FOLD_BOTTOM, FOLD_Z_OUT, kTri1),    vtx(FOLD_X, FOLD_TOP, FOLD_Z_IN, kTri1),
        vtx(FOLD_X, FOLD_BOTTOM, FOLD_Z_IN, kTri1), vtx(fl, FOLD_BOTTOM, FOLD_Z_OUT, kTri1),
        vtx(FOLD_X, FOLD_TOP, FOLD_Z_IN, kTri2),    vtx(fr, FOLD_TOP, FOLD_Z_OUT, kTri2),
        vtx(FOLD_X, FOLD_BOTTOM, FOLD_Z_IN, kTri2), vtx(fr, FOLD_TOP, FOLD_Z_OUT, kTri2),
        vtx(fr, FOLD_BOTTOM, FOLD_Z_OUT, kTri2),    vtx(FOLD_X, FOLD_BOTTOM, FOLD_Z_IN, kTri2)};
    rd_screen_prims(RD_PRIM_TRIANGLES, fo, 12, RD_SPACE_WORLD, 1, 0);
    /* the slit */
    RdScreenVtx s[4] = {vtx(400.0, TOP, 0x00800000u, kRect), vtx(SLIT0, BOTTOM, 0x00800000u, kRect),
                        vtx(SLIT1, TOP, 0x00800000u, kRect),
                        vtx(600.0, BOTTOM, 0x00800000u, kRect)};
    rd_screen_prims(RD_PRIM_SPRITES, s, 4, RD_SPACE_WORLD, 1, 0);
    if (held()) {
        /* a crack 1/16 GS pixel wide at GS x HELD_X in column 2 (the border
         * repeat's source on the left): two sprites meeting there, one 12.4
         * step apart, so the texel whose point lies on HELD_X (texel 5 at
         * 2x) is covered by neither */
        RdScreenVtx e[4] = {vtx(0.0, TOP, 0x00600000u, kTri1),
                            vtx(HELD_X, BOTTOM, 0x00600000u, kTri1),
                            vtx(HELD_X + 1.0 / 16.0, TOP, 0x00A00000u, kTri2),
                            vtx(20.0, BOTTOM, 0x00A00000u, kTri2)};
        rd_screen_prims(RD_PRIM_SPRITES, e, 4, RD_SPACE_WORLD, 1, 0);
    }
    /* ZFog.c fogHostDraw's state and record */
    rd_select_list(4);
    rd_set_target(scene, scene, GW, GH, 0);
    rd_pabe(0);
    rd_blend_func(RD_BLEND_LERP_AS, 128);
    rd_texture(rd_target_texture(scene, RD_VIEW_DEPTH), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_z_write(0);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_sampler_filter(RD_FILTER_NEAREST, RD_FILTER_NEAREST);
    rd_abe(1);
    rd_gouraud(0);
    for (int i = 0; i < 256; i++) {
        lutOf(i, &lut[i * 4]);
    }
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.lut = lut;
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = pp.rgba[3] = 0x80;
    pp.z = 0xFFFFFF;
    pp.rect[0] = (float)(0x8000 - GW * 8);
    pp.rect[1] = (float)(0x8000 - GH * 8);
    pp.rect[2] = (float)(0x8000 + GW * 8);
    pp.rect[3] = (float)(0x8000 + GH * 8);
    pp.uv[0] = pp.uv[1] = 8.0f;
    pp.uv[2] = (float)(8 + GW * 16);
    pp.uv[3] = (float)(8 + GH * 16);
    rd_post(RD_POST_FOG, &pp);
    if (s_after != AFTER_NONE) {
        recordAfter(scene);
    }
    rd_end_frame(0); /* replayed here: interpolation is off */
}

typedef struct Picture {
    uint8_t *px;
    float *z; /* NULL where the depth does not read back (D24S8) */
    uint32_t w, h;
    uint32_t fills; /* RdPerfRecord.crackFillPasses of its replay */
    /* its render passes, the count's among them (crackCountPasses), and
     * the fog's draws by the local read */
    uint32_t passes, countPasses, fogLocal;
    /* the late pass (rd_late_pass.c): fills held into the aura's paste
     * pass, held fills drawn early in a pass of their own, list 11's draws
     * joined without the depth and the passes they joined */
    uint32_t held, glowHeld, early, joined, joinPasses, deferred, deferEarly;
    /* FILLSWAP: snapshots by the swap, copied ones */
    uint32_t swaps, swapCopied;
} Picture;

static bool replay(bool fill, Picture *p)
{
    rd__crack_fill_set_enabled(fill);
    recordFrame();
    p->fills = g_rdPerf.crackFillPasses;
    p->passes = g_rdPerf.passKinds[RD_PERF_PASS_RENDER];
    p->countPasses = g_rdPerf.crackCountPasses;
    p->fogLocal = g_rdPerf.fogLocalDraws;
    p->held = g_rdPerf.latePassHeld;
    p->glowHeld = g_rdPerf.latePassGlowHeld;
    p->deferred = g_rdPerf.latePassDeferred;
    p->deferEarly = g_rdPerf.latePassDeferEarly;
    p->early = g_rdPerf.latePassFlushed;
    p->joined = g_rdPerf.latePassJoined;
    p->joinPasses = g_rdPerf.latePassJoinPasses;
    p->swaps = g_rdPerf.crackSwaps;
    p->swapCopied = g_rdPerf.crackSwapCopied;
    rhi_wait_idle();
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t) {
        return false;
    }
    const size_t n = (size_t)t->tw * t->th;
    p->px = malloc(n * 4);
    p->z = malloc(n * sizeof(float));
    if (!p->px || !p->z) {
        return false;
    }
    uint32_t zw = 0, zh = 0;
    const bool ok = rd__read_target(rd_target(RD_TARGET_SCENE), p->px, n * 4, &p->w, &p->h);
    if (!rhi_limits()->depthReadback ||
        !rd__read_target_depth(rd_target(RD_TARGET_SCENE), p->z, n * sizeof(float), &zw, &zh) ||
        zw != p->w || zh != p->h) {
        free(p->z);
        p->z = NULL;
    }
    return ok && p->w == t->tw && p->h == t->th;
}

/* the texel's GS position (FrameCB g_origin.zw: GS integers on texel
 * centres, rd_pixel_test's seamCracks) */
static double gsX(uint32_t x, float sx)
{
    return (double)x / sx;
}

/* glow and aura: the fill where rd__crack_fill_place puts it (on) against
 * the same frame with the place pinned to the start of list 7 (the
 * place it had before): SCENE's colour and depth byte for byte, the place's
 * counter, and the passes: glow two fewer on the fog's local read
 * (cleanUpFB's two sprites in the fog's pass, not a pass each after the
 * fill's), at least one fewer otherwise (the first sprite in the fog's pass
 * without the depth); aura one fewer (the fill the first draw of the
 * paste's pass, not a pass of its own), the snapshot's copy the same */
static void checkPlace(const Picture *on, RdCrackCounts c0, RdCrackCounts c1, bool active)
{
    const bool glow = s_after == AFTER_GLOW;
    CHECK(active, "the filler does not draw here");
    const uint64_t placed = glow ? c1.afterAlpha - c0.afterAlpha : c1.pasted - c0.pasted;
    /* the counts just before the pinned replay: the replays between c1 and
     * here (the general shader's on the same frame, unpinned) place the
     * fill too and are counted in c1..now */
    rd__crack_fill_wait();
    RdCrackCounts cp;
    rd__crack_fill_counts(&cp);
    rd__crack_fill_set_place(1);
    Picture pin;
    memset(&pin, 0, sizeof(pin));
    const bool ok = replay(true, &pin) && pin.w == on->w && pin.h == on->h;
    rd__crack_fill_set_place(-1);
    rd__crack_fill_wait();
    RdCrackCounts c2;
    rd__crack_fill_counts(&c2);
    CHECK(ok, "SCENE readback with the place pinned");
    size_t dPx = 0, dZ = 0;
    for (size_t i = 0; ok && i < (size_t)on->w * on->h; i++) {
        dPx += memcmp(&pin.px[i * 4], &on->px[i * 4], 4) != 0;
        dZ += on->z && pin.z && memcmp(&pin.z[i], &on->z[i], sizeof(float)) != 0;
    }
    const int fewer = (int)pin.passes - (int)on->passes;
    const bool local = on->fogLocal > 0;
    printf("  %s: %llu fills %s; at the start of list 7 instead: %zu texels' colour and %zu "
           "depths differ, %u render passes against %u (%d fewer), fill passes %u and %u, %llu "
           "placed so then\n",
           glow ? "glow" : "aura", (unsigned long long)placed,
           glow ? "after the glow's alpha" : "in the aura's paste pass", dPx, dZ, pin.passes,
           on->passes, fewer, pin.fills, on->fills,
           (unsigned long long)(glow ? c2.afterAlpha - cp.afterAlpha : c2.pasted - cp.pasted));
    CHECK(placed == 1, "%llu fills placed %s", (unsigned long long)placed,
          glow ? "after the glow's alpha" : "in the aura's paste pass");
    CHECK((glow ? c2.afterAlpha - cp.afterAlpha : c2.pasted - cp.pasted) == 0,
          "the pinned replay placed the fill later");
    CHECK(dPx == 0 && dZ == 0, "the place against list 7's start: %zu colours, %zu depths differ",
          dPx, dZ);
    CHECK(pin.fills == 1, "the pinned replay drew %u fill passes", pin.fills);
    if (glow) {
        CHECK(local ? fewer == 2 : fewer >= 1, "%d render passes fewer than pinned (%s)", fewer,
              local ? "2 on the fog's local read" : "at least 1");
    } else {
        CHECK(fewer == 1, "%d render passes fewer than pinned, not 1", fewer);
    }
    free(pin.px);
    free(pin.z);
}

/* held: the fill held to the aura's paste pass (rd_late_pass.c) against
 * the same frame with the late pass off (the fill in a pass of its own
 * where it was due, before SCENE's border repeat, which then copies the
 * filled source column): SCENE's colour and depth byte for byte, one render
 * pass fewer, one fill held and none drawn early (the late pass off: none
 * held, the fill's own pass); the crack in GS column 2 filled (the band's
 * middle row) and every copy the repeat laid of it over the outer columns
 * that texel's bytes (the held fill's draws at their source texel) */
static void checkHeld(const Picture *on, bool active)
{
    CHECK(active, "the filler does not draw here");
    rd__late_pass_force(0);
    Picture un;
    memset(&un, 0, sizeof(un));
    const bool ok = replay(true, &un) && un.w == on->w && un.h == on->h;
    rd__late_pass_force(heldBits());
    CHECK(ok, "SCENE readback with the late pass off");
    size_t dPx = 0, dZ = 0;
    for (size_t i = 0; ok && i < (size_t)on->w * on->h; i++) {
        dPx += memcmp(&un.px[i * 4], &on->px[i * 4], 4) != 0;
        dZ += on->z && un.z && memcmp(&un.z[i], &on->z[i], sizeof(float)) != 0;
    }
    const int fewer = (int)un.passes - (int)on->passes;
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const double sx = t ? (double)t->sx : 1.0, sy = t ? (double)t->sy : 1.0;
    const uint32_t w = on->w;
    const uint32_t x = (uint32_t)(HELD_X * sx + 0.5), y = (uint32_t)((TOP + BOTTOM) * 0.5 * sy);
    const uint32_t a2 = (uint32_t)ceil(2.0 * sx - 1e-4), a3 = (uint32_t)ceil(3.0 * sx - 1e-4);
    const uint8_t *ref = &on->px[((size_t)1 * w + 1) * 4];
    const uint8_t *src = ok && y < on->h ? &on->px[((size_t)y * w + x) * 4] : ref;
    int copies = 0, badCopies = 0;
    for (int32_t d = (int32_t)x - (int32_t)(a3 - a2); ok && a3 > a2 && d >= 0;
         d -= (int32_t)(a3 - a2)) {
        copies++;
        badCopies += memcmp(&on->px[((size_t)y * w + (size_t)d) * 4], src, 4) != 0;
    }
    printf("  held: %u fills held, %u drawn early (off: %u, %u); against the late pass off %zu "
           "texels' colour and %zu depths differ, %u render passes against %u (%d fewer), fill "
           "passes %u and %u; the crack at texel %u of row %u %s, %d copies over the outer "
           "columns, %d not its bytes\n",
           on->held, on->early, un.held, un.early, dPx, dZ, on->passes, un.passes, fewer, on->fills,
           un.fills, x, y, memcmp(src, ref, 4) != 0 ? "filled" : "the clear's fog", copies,
           badCopies);
    const uint32_t glowWant = s_after == AFTER_HELDGLOW ? 1u : 0u;
    const uint32_t deferWant = s_after == AFTER_DEFER ? 1u : 0u;
    CHECK(on->held == 1 && on->early == 0 && on->glowHeld == glowWant,
          "held %u (%u in the glow's paste pass), drawn early %u: 1 (%u) and 0 expected", on->held,
          on->glowHeld, on->early, glowWant);
    CHECK(on->deferred == deferWant && on->deferEarly == 0 && un.deferred == 0,
          "glow pastes deferred %u (early %u; off %u): %u expected", on->deferred, on->deferEarly,
          un.deferred, deferWant);
    CHECK(un.held == 0 && un.early == 0, "the late pass off held %u, drew %u early", un.held,
          un.early);
    CHECK(dPx == 0 && dZ == 0,
          "the held fill against the late pass off: %zu colours, %zu depths "
          "differ",
          dPx, dZ);
    CHECK(fewer == 1 + (int)deferWant, "%d render passes fewer than the late pass off, not %d",
          fewer, 1 + (int)deferWant);
    CHECK(on->fills == 0 && un.fills == 1, "fill passes %u held and %u off, 0 and 1 expected",
          on->fills, un.fills);
    CHECK(x >= a2 && x < a3, "the crack's texel %u not in column 2's texels [%u, %u)", x, a2, a3);
    CHECK(memcmp(src, ref, 4) != 0, "the crack at texel %u of row %u not filled", x, y);
    CHECK(copies >= 1 && badCopies == 0, "%d of %d copies of the filled texel differ", badCopies,
          copies);
    free(un.px);
    free(un.z);
}

/* join: the aura's frame with two list 11 sprites after the paste (Z
 * ALWAYS, no Z write): with the late pass's join they are drawn in the
 * paste's pass without SCENE's depth (the fill its first draw, as aura);
 * against the join off (rd__late_pass_force(RD_LATE_FILL)): SCENE's colour
 * and depth byte for byte, one render pass fewer, 2 draws joined in 1 pass,
 * none with the join off */
static void checkJoin(const Picture *on)
{
    rd__late_pass_force((int)RD_LATE_FILL);
    Picture un;
    memset(&un, 0, sizeof(un));
    const bool ok = replay(true, &un) && un.w == on->w && un.h == on->h;
    rd__late_pass_force((int)RD_LATE_ALL);
    CHECK(ok, "SCENE readback with the join off");
    size_t dPx = 0, dZ = 0;
    for (size_t i = 0; ok && i < (size_t)on->w * on->h; i++) {
        dPx += memcmp(&un.px[i * 4], &on->px[i * 4], 4) != 0;
        dZ += on->z && un.z && memcmp(&un.z[i], &on->z[i], sizeof(float)) != 0;
    }
    const int fewer = (int)un.passes - (int)on->passes;
    printf("  join: %u list 11 draws in %u passes without SCENE's depth (off: %u, %u); against "
           "the join off %zu texels' colour and %zu depths differ, %u render passes against %u "
           "(%d fewer)\n",
           on->joined, on->joinPasses, un.joined, un.joinPasses, dPx, dZ, on->passes, un.passes,
           fewer);
    CHECK(on->joined == 2 && on->joinPasses == 1, "%u draws joined in %u passes: 2 and 1 expected",
          on->joined, on->joinPasses);
    CHECK(un.joined == 0 && un.joinPasses == 0, "the join off joined %u draws", un.joined);
    CHECK(dPx == 0 && dZ == 0, "joined against the join off: %zu colours, %zu depths differ", dPx,
          dZ);
    CHECK(fewer == 1, "%d render passes fewer than the join off, not 1", fewer);
    free(un.px);
    free(un.z);
}

/* one replay of the frame with the swap forced (-1 the environment's) and
 * the general shader forced or not: the counts' change */
static bool workReplay(int swap, bool general, Picture *p, uint64_t *pixels)
{
    rd__crack_swap_force(swap);
    rd__crack_fill_force_general(general);
    RdCrackCounts a, b;
    rd__crack_fill_wait();
    rd__crack_fill_counts(&a);
    const bool ok = replay(true, p);
    rd__crack_fill_wait();
    rd__crack_fill_counts(&b);
    rd__crack_swap_force(-1);
    rd__crack_fill_force_general(false);
    *pixels = b.pixels - a.pixels;
    return ok;
}

static void workDiff(const Picture *a, const Picture *b, size_t *dPx, size_t *dZ)
{
    *dPx = *dZ = 0;
    for (size_t i = 0; i < (size_t)a->w * a->h; i++) {
        *dPx += memcmp(&a->px[i * 4], &b->px[i * 4], 4) != 0;
        *dZ += a->z && b->z && memcmp(&a->z[i], &b->z[i], sizeof(float)) != 0;
    }
}

/* FILLSWAP (rd_crack_fill.c), wherever the filler draws: the swap's
 * self-test on this device (passed: rd_crack_test runs no rd_fog_selftest,
 * so the swap is off until it is run here), then the frame with the swap
 * forced on against forced off: SCENE's colour and depth byte for byte
 * against each other and against the frame as first drawn (on: the swap not
 * yet tested), the same render passes, fill passes and count; the snapshot
 * taken by the swap once (where the late pass holds the fill: none, copied
 * for that reason), copied once with the swap off.  At a reach of 1 on both
 * axes the general shader forced with the swap on: the same bytes. */
static void checkWork(const Picture *on, uint32_t rx, uint32_t ry)
{
    const bool passed = rd__crack_swap_selftest();
    CHECK(passed && rd__crack_swap_test() == 1, "the swap's self-test: %d (1 passed)",
          rd__crack_swap_test());
    Picture a, b, g;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&g, 0, sizeof(g));
    uint64_t pa = 0, pb = 0, pg = 0;
    const bool okA = workReplay(1, false, &a, &pa) && a.w == on->w && a.h == on->h;
    const bool okB = workReplay(0, false, &b, &pb) && b.w == on->w && b.h == on->h;
    const bool one = rx <= 1u && ry <= 1u;
    const bool okG = !one || (workReplay(1, true, &g, &pg) && g.w == on->w && g.h == on->h);
    CHECK(okA && okB && okG, "SCENE readbacks with the swap forced");
    if (!(okA && okB && okG)) {
        free(a.px), free(a.z), free(b.px), free(b.z), free(g.px), free(g.z);
        return;
    }
    size_t dPx, dZ, oPx, oZ, gPx = 0, gZ = 0;
    workDiff(&a, &b, &dPx, &dZ);
    workDiff(on, &b, &oPx, &oZ);
    if (one) {
        workDiff(&g, &b, &gPx, &gZ);
    }
    const uint32_t swapWant = held() ? 0u : 1u;
    printf("  swap on: %u snapshots by the swap, %u copied (off: %u, %u); %zu texels' colour and "
           "%zu depths differ (against the first: %zu, %zu), render passes %u and %u, fill passes "
           "%u and %u, counted %llu and %llu pixels\n",
           a.swaps, a.swapCopied, b.swaps, b.swapCopied, dPx, dZ, oPx, oZ, a.passes, b.passes,
           a.fills, b.fills, (unsigned long long)pa, (unsigned long long)pb);
    CHECK(dPx == 0 && dZ == 0 && oPx == 0 && oZ == 0,
          "the swap on against off: %zu colours, %zu depths differ "
          "(against the first replay %zu, %zu)",
          dPx, dZ, oPx, oZ);
    CHECK(a.swaps == swapWant && a.swapCopied == 1u - swapWant,
          "%u snapshots by the swap, %u copied: %u and %u expected", a.swaps, a.swapCopied,
          swapWant, 1u - swapWant);
    CHECK(b.swaps == 0 && b.swapCopied == 1, "the swap off: %u swapped, %u copied", b.swaps,
          b.swapCopied);
    CHECK(a.passes == b.passes && a.fills == b.fills && pa == pb,
          "render passes %u and %u, fill passes %u and %u, counts %llu and %llu", a.passes,
          b.passes, a.fills, b.fills, (unsigned long long)pa, (unsigned long long)pb);
    if (one) {
        printf("  the general shader with the swap on: %u snapshots by the swap; %zu texels' "
               "colour and %zu depths differ, counted %llu pixels\n",
               g.swaps, gPx, gZ, (unsigned long long)pg);
        CHECK(gPx == 0 && gZ == 0 && g.swaps == swapWant && pg == pb,
              "the general shader with the swap on: %zu colours, %zu depths differ, %u swapped, "
              "%llu pixels",
              gPx, gZ, g.swaps, (unsigned long long)pg);
    }
    free(a.px), free(a.z), free(b.px), free(b.z), free(g.px), free(g.z);
}

int main(int argc, char **argv)
{
    printf("rd_crack_test\n");
    if (argc < 2) {
        printf("usage: rd_crack_test 2|4|1|frac|original "
               "[msaa|backdrop|fold|glow|aura|held|heldglow|defer|join]\n");
        return 2;
    }
    const bool original = strcmp(argv[1], "original") == 0;
    const bool frac = strcmp(argv[1], "frac") == 0;
    const bool msaa = argc > 2 && strcmp(argv[2], "msaa") == 0;
    s_backdrop = argc > 2 && strcmp(argv[2], "backdrop") == 0;
    const bool inPass = argc > 2 && strcmp(argv[2], "fold") == 0;
    s_after = argc > 2 && strcmp(argv[2], "glow") == 0       ? AFTER_GLOW
              : argc > 2 && strcmp(argv[2], "aura") == 0     ? AFTER_AURA
              : argc > 2 && strcmp(argv[2], "held") == 0     ? AFTER_HELD
              : argc > 2 && strcmp(argv[2], "heldglow") == 0 ? AFTER_HELDGLOW
              : argc > 2 && strcmp(argv[2], "defer") == 0    ? AFTER_DEFER
              : argc > 2 && strcmp(argv[2], "join") == 0     ? AFTER_JOIN
                                                             : AFTER_NONE;
    if (held()) {
        /* the late pass's hold whatever the glow's plan (this frame holds
         * no chain the light plan reads; rd_late_pass.c) */
        rd__late_pass_force(heldBits());
    } else if (s_after == AFTER_JOIN) {
        rd__late_pass_force((int)RD_LATE_ALL); /* the tile class's default, said */
    }
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = original ? RD_PRESET_ORIGINAL : RD_PRESET_ENHANCED;
    st.aspect = 4.0f / 3.0f;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.sceneScale = original || frac ? (original ? 1.0f : 0.0f) : (float)atof(argv[1]);
    st.sceneWidth = frac ? 1800u : 0u;
    st.sceneHeight = frac ? 1080u : 0u;
    st.antiAlias = msaa ? (uint8_t)RD_AA_MSAA4 : (uint8_t)RD_AA_OFF;
    if (!rd_init(GW, GH, &st, NULL)) {
        printf("rd_crack_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    if (msaa && (rd_aa_supported_mask() & (1u << RD_AA_MSAA4)) == 0u) {
        printf("rd_crack_test: SKIP: MSAA4 is not offered here\n");
        rd_shutdown();
        return 77;
    }
    if (inPass && !rhi_limits()->localRead) {
        printf("rd_crack_test: SKIP: no local read here\n");
        rd_shutdown();
        return 77;
    }
    if (inPass && !(g_rd.fogPath == RD_FOG_LOCAL && g_rd.profile.fogLocal)) {
        printf("rd_crack_test: the fog's path is %s, not the local read (profile fog local %d)\n",
               rd__fog_path_name(g_rd.fogPath), (int)g_rd.profile.fogLocal);
        rd_shutdown();
        return 1;
    }
    rd__crack_fill_count_all(true);
    RdCrackCounts c0;
    rd__crack_fill_counts(&c0);
    const bool envOn = c0.enabled; /* ICO_RD_CRACK_FILL, else the profile's default */
    /* the first fogged replay is the fog probe's (its own read, the exact
     * table entry; under MSAA the resolved depth): one before the two
     * compared, which then draw the fog alike */
    rd__crack_fill_set_enabled(false);
    recordFrame();
    rd_begin_frame(); /* logs the probe */
    rd_end_frame(0);
    rd__crack_fill_counts(&c0);
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    CHECK(t != NULL, "no SCENE");
    if (!t) {
        rd_shutdown();
        return 1;
    }
    const float sx = t->sx, sy = t->sy;
    const uint32_t rx = rd__crack_fill_reach(sx), ry = rd__crack_fill_reach(sy);
    const bool active = envOn && !original && !msaa && (sx > 1.0f || sy > 1.0f);
    printf("  SCENE %ux%u texels, scale %gx%g, reach %ux%u; the filler %s%s\n", t->tw, t->th,
           (double)sx, (double)sy, rx, ry,
           active ? "draws" : (envOn ? "on, not drawn here" : "off (the switch or the profile)"),
           s_backdrop ? "; over a backdrop" : "");

    Picture off, on;
    memset(&off, 0, sizeof(off));
    memset(&on, 0, sizeof(on));
    const bool okOff = replay(false, &off);
    const bool okOn = replay(envOn, &on);
    CHECK(okOff && okOn, "SCENE readbacks");
    rd__crack_fill_wait();
    RdCrackCounts c1;
    rd__crack_fill_counts(&c1);
    if (!(okOff && okOn)) {
        rd_shutdown();
        return 1;
    }
    const uint32_t w = off.w, h = off.h;
    /* the clear's fog (with backdrop the backdrop's): every texel nothing
       but the clear or the backdrop drew (corner texel 1, 1), its depth */
    const uint8_t *ref = &off.px[((size_t)1 * w + 1) * 4];
    const float refZ = off.z ? off.z[(size_t)1 * w + 1] : 0.0f;
    int cracksOff = 0, cracksOn = 0, near = 0, slitOff = 0, slitOn = 0, slitAll = 0;
    int changed = 0, changedDrawn = 0, badFrom = 0, badDepth = 0, fold = 0;
    /* held: SCENE's outer two GS columns a side are the border repeat's
     * copies of columns 2 and W - 3 (their colour, not their depth), the
     * filled source's with the filler on: checkHeld compares them; the
     * texels here are the repeat's own columns (rd_blur_border.c
     * columnTexel: texel t's point at GS t / sx) */
    const uint32_t bx0 = held() ? (uint32_t)ceil(2.0 * sx - 1e-4) : 0u;
    const uint32_t bx1 = held() ? (uint32_t)ceil((GW - 2) * (double)sx - 1e-4) : w;
    for (uint32_t y = 0; y < h; y++) {
        const double gy = gsX(y, sy);
        const double ex = EDGE_X0 - (EDGE_X0 - LEFT) * (gy - TOP) / (BOTTOM - TOP);
        for (uint32_t x = 0; x < w; x++) {
            if (x < bx0 || x >= bx1) {
                continue;
            }
            const size_t i = (size_t)y * w + x;
            const uint8_t *a = &off.px[i * 4], *b = &on.px[i * 4];
            const bool clearA = memcmp(a, ref, 4) == 0, clearB = memcmp(b, ref, 4) == 0;
            const double gx = gsX(x, sx);
            const bool band = gy >= BAND0 && gy <= BAND1;
            /* the fold's texels within a texel of its line, away from its ends */
            fold += gy > FOLD_TOP + 2.0 && gy < FOLD_BOTTOM - 2.0 && gx > FOLD_X - 1.0 / sx &&
                    gx < FOLD_X + 1.0 / sx;
            if (band && gx > ex - 2.0 / sx && gx < ex + 2.0 / sx) {
                near++;
                cracksOff += clearA;
                cracksOn += clearB;
            }
            /* half a texel in from the slit's sides: unwritten whichever
               side of a texel its centre is taken at */
            if (band && gx > SLIT0 + 0.5 / sx && gx < SLIT1 - 0.5 / sx) {
                slitAll++;
                slitOff += clearA;
                slitOn += clearB;
            }
            const bool zDiff = off.z && on.z && off.z[i] != on.z[i];
            if (memcmp(a, b, 4) == 0 && !zDiff) {
                continue;
            }
            changed++;
            changedDrawn += !clearA || (off.z && off.z[i] != refZ);
            /* a filled texel: a written texel within the reach on its row
               or its column has its colour; its depth is unchanged */
            bool from = false;
            for (int k = 1; k <= (int)RD_CRACK_REACH_MAX && !from; k++) {
                const int64_t nb[4][2] = {{(int64_t)x - k, y},
                                          {(int64_t)x + k, y},
                                          {x, (int64_t)y - k},
                                          {x, (int64_t)y + k}};
                for (int j = 0; j < 4 && !from; j++) {
                    const uint32_t r = j < 2 ? rx : ry;
                    if ((uint32_t)k > r || nb[j][0] < 0 || nb[j][1] < 0 || nb[j][0] >= (int64_t)w ||
                        nb[j][1] >= (int64_t)h) {
                        continue;
                    }
                    const size_t q = (size_t)nb[j][1] * w + (size_t)nb[j][0];
                    /* after the glow the crack's alpha is its own (cleanUpFB's
                       Z-tested mark): its RGB the neighbour's */
                    if (memcmp(&off.px[q * 4], ref, 4) != 0 &&
                        memcmp(&on.px[q * 4], b, s_after == AFTER_GLOW || held() ? 3 : 4) == 0) {
                        from = true;
                    }
                }
            }
            badFrom += !from;
            badDepth += zDiff;
        }
    }
    printf("  cracks along the edge: %d texels of the clear's fog without the filler, %d with it, "
           "of %d near the edge\n",
           cracksOff, cracksOn, near);
    printf("  the slit: %d of %d texels the clear's fog without, %d with\n", slitOff, slitAll,
           slitOn);
    printf(
        "  %d texels changed (%d the first replay drew; %d not a neighbour's colour, %d with "
        "their depth changed%s); fill passes %u and %u; counted %llu pixels over %llu replays (filled %llu)\n",
        changed, changedDrawn, badFrom, badDepth, off.z ? "" : ", no depth readback", off.fills,
        on.fills, (unsigned long long)(c1.pixels - c0.pixels),
        (unsigned long long)(c1.counted - c0.counted), (unsigned long long)(c1.filled - c0.filled));
    CHECK(near > 100, "the edge's band holds %d texels", near);
    CHECK(fold > 20, "the fold's line holds %d texels", fold);
    if (off.z) {
        CHECK(s_backdrop ? refZ > 0.0f : refZ == 0.0f, "the corner's depth %g", (double)refZ);
    }
    /* the slit is only measured where the filler draws (reach > 0): at 1x
       and under Original there is nothing to keep it from */
    CHECK(!active || (slitAll > 0 && slitOff == slitAll && slitOn == slitAll),
          "the slit: %d and %d of %d texels the clear's fog", slitOff, slitOn, slitAll);
    CHECK(changedDrawn == 0, "%d texels the first replay drew changed", changedDrawn);
    CHECK(off.fills == 0, "the replay with the filler off drew %u fill passes", off.fills);
    if (active) {
        CHECK(cracksOff > 0, "no crack without the filler at %gx%g", (double)sx, (double)sy);
        CHECK(cracksOn == 0, "%d cracks with the filler", cracksOn);
        CHECK(badFrom == 0 && badDepth == 0,
              "%d filled texels not a neighbour's colour, %d with their depth changed", badFrom,
              badDepth);
        /* in the aura's paste pass: no pass of its own */
        CHECK(on.fills == (s_after == AFTER_AURA || held() || s_after == AFTER_JOIN ? 0u : 1u),
              "%u fill passes", on.fills);
        CHECK(c1.counted - c0.counted == 1 && c1.filled - c0.filled == 1 &&
                  c1.pixels - c0.pixels == (uint64_t)changed,
              "counted %llu pixels over %llu replays (%llu filled), %d texels changed",
              (unsigned long long)(c1.pixels - c0.pixels),
              (unsigned long long)(c1.counted - c0.counted),
              (unsigned long long)(c1.filled - c0.filled), changed);
        CHECK(c1.rx == rx && c1.ry == ry, "the reach %ux%u, %ux%u expected", c1.rx, c1.ry, rx, ry);
    } else {
        CHECK(on.fills == 0 && changed == 0 && c1.filled == c0.filled,
              "not drawn here: %u fill passes, %d texels changed, %llu replays filled", on.fills,
              changed, (unsigned long long)(c1.filled - c0.filled));
        if (msaa) {
            CHECK(c1.skipped > c0.skipped, "the MSAA replay not counted as left unfilled");
        }
    }
    if (active && !msaa && rx <= 1u && ry <= 1u) {
        /* a reach of 1 on both axes draws crack_fill1_ps (the four
           neighbours first); the general crack_fill_ps on the same frame
           gives every byte of colour and depth the same, and the same
           count */
        rd__crack_fill_force_general(true);
        Picture gen;
        memset(&gen, 0, sizeof(gen));
        RdCrackCounts g0, g1;
        rd__crack_fill_wait();
        rd__crack_fill_counts(&g0);
        const bool okGen = replay(true, &gen) && gen.w == w && gen.h == h;
        rd__crack_fill_wait();
        rd__crack_fill_counts(&g1);
        rd__crack_fill_force_general(false);
        CHECK(okGen, "SCENE readback with the general shader");
        size_t dPx = 0, dZ = 0;
        for (size_t i = 0; okGen && i < (size_t)w * h; i++) {
            dPx += memcmp(&gen.px[i * 4], &on.px[i * 4], 4) != 0;
            dZ += on.z && gen.z && memcmp(&gen.z[i], &on.z[i], sizeof(float)) != 0;
        }
        printf("  the general shader on the same frame: %zu texels' colour and %zu depths differ "
               "from the reach-1 shader's; counted %llu and %llu pixels\n",
               dPx, dZ, (unsigned long long)(c1.pixels - c0.pixels),
               (unsigned long long)(g1.pixels - g0.pixels));
        CHECK(dPx == 0 && dZ == 0, "reach-1 against general: %zu colours, %zu depths differ", dPx,
              dZ);
        CHECK(g1.counted - g0.counted == 1 && g1.pixels - g0.pixels == c1.pixels - c0.pixels,
              "the general shader counted %llu pixels over %llu replays, the reach-1 %llu",
              (unsigned long long)(g1.pixels - g0.pixels),
              (unsigned long long)(g1.counted - g0.counted),
              (unsigned long long)(c1.pixels - c0.pixels));
        free(gen.px);
        free(gen.z);
    }
    if (inPass) {
        /* the fog in the scene pass, the fill after lists 4-6: one render
           pass more than without the filler (the fill's own), the count's
           apart */
        const int extra = (int)on.passes - (int)on.countPasses - (int)off.passes;
        printf("  late: %llu of %llu fills after lists 4-6; render passes %u without the "
               "filler, %u with it (%u the count's); fog draws by the local read %u and %u\n",
               (unsigned long long)(c1.late - c0.late), (unsigned long long)(c1.filled - c0.filled),
               off.passes, on.passes, on.countPasses, off.fogLocal, on.fogLocal);
        CHECK(active, "the filler does not draw here");
        CHECK(c1.late - c0.late == 1, "%llu fills after lists 4-6",
              (unsigned long long)(c1.late - c0.late));
        CHECK(off.fogLocal > 0 && on.fogLocal > 0, "fog draws by the local read: %u and %u",
              off.fogLocal, on.fogLocal);
        CHECK(extra == 1, "%d render passes more with the filler, not 1", extra);
        /* the desktop's form: the fog's depth copy (the fog on the colour
           alone), a replay for the switch first */
        rd__set_fog_path(RD_FOG_COPY);
        Picture cp;
        memset(&cp, 0, sizeof(cp));
        bool okCp = replay(true, &cp);
        free(cp.px);
        free(cp.z);
        memset(&cp, 0, sizeof(cp));
        RdCrackCounts c2;
        rd__crack_fill_counts(&c2);
        okCp = replay(true, &cp) && okCp && cp.w == w && cp.h == h;
        rd__crack_fill_wait();
        RdCrackCounts c3;
        rd__crack_fill_counts(&c3);
        CHECK(okCp, "SCENE readback on the depth copy");
        const size_t n = (size_t)w * h;
        size_t diffPx = 0, diffZ = 0;
        for (size_t i = 0; okCp && i < n; i++) {
            diffPx += memcmp(&cp.px[i * 4], &on.px[i * 4], 4) != 0;
            diffZ += on.z && cp.z && memcmp(&cp.z[i], &on.z[i], sizeof(float)) != 0;
        }
        printf("  the depth copy's replay: %u fills (%llu after lists 4-6), fog draws by the "
               "local read %u; %zu texels' colour and %zu depths differ from the local read's "
               "replay\n",
               cp.fills, (unsigned long long)(c3.late - c2.late), cp.fogLocal, diffPx, diffZ);
        CHECK(cp.fills == 1 && c3.late - c2.late == 1 && cp.fogLocal == 0,
              "the depth copy's replay: %u fills, %llu after lists 4-6, %u local fog draws",
              cp.fills, (unsigned long long)(c3.late - c2.late), cp.fogLocal);
        CHECK(diffPx == 0 && diffZ == 0,
              "the local read's replay against the depth copy's: %zu colours, %zu depths differ",
              diffPx, diffZ);
        free(cp.px);
        free(cp.z);
    }
    if (held()) {
        checkHeld(&on, active);
    } else if (s_after == AFTER_JOIN) {
        checkJoin(&on);
    } else if (s_after != AFTER_NONE) {
        checkPlace(&on, c0, c1, active);
    }
    if (active && !msaa) {
        checkWork(&on, rx, ry);
    }
    if (msaa) {
        /* the same frame without anti-aliasing and without the filler (a
           replay for the switch first) against the MSAA picture: the
           texels along the edge with the clear's depth, or without a depth
           readback its fog's colour */
        st.antiAlias = (uint8_t)RD_AA_OFF;
        rd_set_settings(&st);
        Picture aa;
        memset(&aa, 0, sizeof(aa));
        bool okAa = replay(false, &aa);
        free(aa.px);
        free(aa.z);
        memset(&aa, 0, sizeof(aa));
        okAa = replay(false, &aa) && okAa && aa.w == w && aa.h == h;
        CHECK(okAa, "SCENE readback without anti-aliasing");
        const bool depth = off.z && aa.z;
        const uint8_t *refAa = okAa ? &aa.px[((size_t)1 * w + 1) * 4] : ref;
        const float refAaZ = depth ? aa.z[(size_t)1 * w + 1] : 0.0f;
        int clearMs = 0, clearAa = 0;
        for (uint32_t y = 0; okAa && y < h; y++) {
            const double gy = gsX(y, sy);
            const double ex = EDGE_X0 - (EDGE_X0 - LEFT) * (gy - TOP) / (BOTTOM - TOP);
            if (gy < BAND0 || gy > BAND1) {
                continue;
            }
            for (uint32_t x = 0; x < w; x++) {
                const double gx = gsX(x, sx);
                if (gx <= ex - 2.0 / sx || gx >= ex + 2.0 / sx) {
                    continue;
                }
                const size_t i = (size_t)y * w + x;
                clearMs += depth ? off.z[i] == refZ : memcmp(&off.px[i * 4], ref, 4) == 0;
                clearAa += depth ? aa.z[i] == refAaZ : memcmp(&aa.px[i * 4], refAa, 4) == 0;
            }
        }
        printf("  MSAA4 against no anti-aliasing (the filler off): %d and %d texels along the edge "
               "with the clear's %s\n",
               clearMs, clearAa, depth ? "depth" : "fog colour");
        CHECK(clearAa > 0, "no crack without anti-aliasing at %gx%g", (double)sx, (double)sy);
        CHECK(clearMs * 20 <= clearAa,
              "MSAA4: %d texels along the edge with the clear's %s, more than 5 %% of the %d "
              "without anti-aliasing",
              clearMs, depth ? "depth" : "fog colour", clearAa);
        CHECK(slitAll > 0 && slitOff == slitAll,
              "MSAA4: the slit keeps the clear's fog on %d of %d texels", slitOff, slitAll);
        free(aa.px);
        free(aa.z);
    }
    /* the reach: ceil(scale / 2) above 1x, at most 3 */
    CHECK(rd__crack_fill_reach(1.0f) == 0u && rd__crack_fill_reach(0.5f) == 0u &&
              rd__crack_fill_reach(1.154f) == 1u && rd__crack_fill_reach(2.0f) == 1u &&
              rd__crack_fill_reach(2.8125f) == 2u && rd__crack_fill_reach(4.0f) == 2u &&
              rd__crack_fill_reach(6.0f) == 3u && rd__crack_fill_reach(16.0f) == 3u,
          "rd__crack_fill_reach");
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    free(off.px);
    free(off.z);
    free(on.px);
    free(on.z);
    rd_shutdown();
    if (failures) {
        printf("rd_crack_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_crack_test: ok\n");
    return 0;
}
