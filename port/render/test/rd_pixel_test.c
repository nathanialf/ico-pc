/* rd_pixel_test.c: rd through the Vulkan RHI (lavapipe in the container).
 *
 *   order    the same pixel drawn from list 5 (recorded first) and list 1:
 *            list 5 wins, it replays later
 *   railing  (package P8) the stair railings' state, TEST 0x5160D (ATE
 *            GREATER 0x60, AFAIL FB_ONLY, Z GEQUAL) and ALPHA 0x44 with ABE
 *            and Z write, on a lattice texture whose holes have alpha 0,
 *            then an opaque sprite drawn later behind it (smaller GS Z):
 *            the holes show the later sprite and hold its Z, the wires
 *            keep their colour and their Z
 *   sprite   sprites in GS 12.4 window coordinates against the GS rule
 *            (pixel x covered when x0 <= x < x1, sampled at the integer):
 *            integer edges, half-pixel edges, the -4 corner nudge, a sprite
 *            covering no pixel centre, a one-pixel sprite; a textured
 *            sprite with the +8 UV nudge copies texels exactly; TEXA
 *            7F/81+AEM on an RGB24 source; rd_UVOffset
 *   texa     (package TEXA) an RGBA16 texture with A = 0, A = 1 and black
 *            texels under 7F/81+AEM, magnified 4x: bilinear is the GS
 *            order (TEXA per texel, then the 4-bit bilinear) with 0 LSB
 *            (sprite_texa_ps), nearest is the expanded texel with 0 LSB
 *            (sprite_ps); the planner's choice of entry
 *   font     (package R8) a 4x4 R8 coverage atlas (GS alpha units) drawn
 *            through rd_ScreenPrims under port/ui/font.c's state (font_ps):
 *            1:1 at texel centres the stored alpha is (c * va) >> 7, and
 *            1:1 and magnified 8x with bilinear filtering every byte equals
 *            the same atlas as RGBA8 (white, alpha c) through sprite_ps;
 *            the frame dumped and loaded has the texture as R8 with its
 *            texels and replays to the same bytes
 *   stq      (package RSMALL) a textured triangle strip with Q 1 to 0.25
 *            maps the texture perspective-correctly (U = f q1 / (q0 + f (q1 -
 *            q0)) at the fraction f across it), a strip with Q = 1 stays affine
 *   reduce   rd_Post(RD_POST_REDUCTION) on a synthetic 512x512 scene
 *            against a CPU reference of gsb_Reduction (the GS bilinear at
 *            the GS sample points, tint, border crop) exactly (R-POST: the
 *            reduction is drawn through the GS sprite model)
 *   keep     a keep frame (lists 11/12 only) draws DISPLAY back at 112/128
 *   exact    100 frames of RD_POST_COMPOSITE_FIX with exactInt into
 *            FEED128 equal the GS integer formula exactly every frame
 *   dump     a frame replayed, dumped, loaded and replayed again gives the
 *            same DISPLAY bytes; the dump is left for rd_replay_tool
 *   runs     (package PC) consecutive screen-prim commands under one state
 *            are one draw: six overlapping, differently coloured blended
 *            sprites drawn merged give the bytes of six sequential draws;
 *            two DATE sprites (the second retakes the snapshot), an AFAIL
 *            split and a scissor change each end a run (draws counted)
 *   aa1      PRIM.AA1 (package AA1) against a CPU coverage reference, in
 *            GS pixels at the integer sample points, As on the 0x80 scale
 *            (grey 0x80 LERPed over black writes As itself): a line with
 *            ABE 0 (the storm's) has coverage 1 - d on the two pixels per
 *            column nearest it, d the vertical distance; a triangle with
 *            ABE 1 and alpha 0x80 has its interior at 0x80 and a one-pixel
 *            fringe outside its top edge at 1 - d; As = the 16-bit coverage
 *            >> 9; within 1 LSB of As
 *   nodual   (package AN-E) every RdBlend 0..11 x PABE x FBA x DATE off/on x
 *            the alpha test (off, or GREATER 0x60 with AFAIL KEEP, FB_ONLY,
 *            ZB_ONLY, RGB_ONLY) x colour mask F, 7, 8 x Z ALWAYS, GEQUAL x Z
 *            write, two overlapping gouraud quads in one command (alpha
 *            0..0xFF, Z across the background's) over a noise background
 *            (RGB, alpha both sides of the MSB) in SCENE with its depth, and
 *            the Z-less half in DISPLAY: replayed with the two-pass blend
 *            fallback off and on (rd_SetNoDual), SCENE, its depth and
 *            DISPLAY equal byte for byte; the pipeline cache is cleared
 *            after it (its states are outside the reachable set)
 *   pipes    every pipeline created is in the enumerated reachable set,
 *            whose screen and post part has fewer than 250 keys (with package
 *            TEXA's sprite_texa_ps twins) and holds the colour
 *            mask 7 keys of the 2D draws under the dark volume's FBMSK and the
 *            STQ keys of the lightning (all of it,
 *            with the VU programs of wave 3, fewer than RD_PIPELINE_REACHABLE_MAX)
 *            (package AN-E: the set with the two-pass fallback too, also
 *            under RD_PIPELINE_REACHABLE_MAX; a created key is in the set of
 *            its mode)
 *
 *   aura     (package QUEEN) the mirage's mask as list 8 draws it into
 *            AURA_WORK with SCENE's depth (TEST 0x5346D, ALPHA 0x44, ABE, Z
 *            write): a mask quad behind a nearer opaque scene quad leaves
 *            AURA_WORK's alpha at the clear's 0, one at equal depth passes,
 *            also at the Queen's depths (GS Z 17M, quads 300, 100 and 17
 *            behind fail, at it and 17 in front pass);
 *            a textured quad with AFAIL RGB_ONLY (aref 0x46) and FB_ONLY
 *            (0x60) writes A = As where the alpha test passes and keeps the
 *            destination alpha (RGB_ONLY) or writes As (FB_ONLY) where it
 *            fails, RGB the GS lerp within 2 (As at most
 *            0x80; reported above it); in Original 1x, Enhanced 4x
 *            and Enhanced 4x with the full-height scene
 *
 * argv[1]: a writable directory.  Exit 0, 1 on a mismatch, 77 without a
 * Vulkan device.  Any validation error fails the test. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "rd_internal.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int pixFail(const char *what, int x, int y, const uint8_t *got, const int *want)
{
    if (failures < 40) {
        printf("FAIL %s (%d,%d): got %u %u %u %u, expected %d %d %d %d\n", what, x, y, got[0],
               got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    failures++;
    return 1;
}

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static RdScreenVtx vtx(int32_t x, int32_t y, uint32_t z, const uint8_t c[4], float s, float t)
{
    RdScreenVtx v;
    memset(&v, 0, sizeof(v));
    v.x = x;
    v.y = y;
    v.z = z;
    v.s = s;
    v.t = t;
    v.q = 1.0f;
    memcpy(v.rgba, c, 4);
    return v;
}

/* a sprite from (x0, y0) to (x1, y1) in 12.4 relative to the target's
 * top-left, for a target whose XYOFFSET is (2048 - w/2, 2048 - h/2) */
static void sprite(uint32_t tw, uint32_t th, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   const uint8_t c[4], int32_t u0, int32_t v0, int32_t u1, int32_t v1)
{
    const int32_t ox = (2048 - (int32_t)tw / 2) * 16, oy = (2048 - (int32_t)th / 2) * 16;
    RdScreenVtx v[2] = {vtx(ox + x0, oy + y0, 0, c, (float)u0, (float)v0),
                        vtx(ox + x1, oy + y1, 0, c, (float)u1, (float)v1)};
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
}

static void opaque2D(void)
{
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[512 * 512 * 4];
    if (!rd__ReadTarget(rd_Target(id), buf, sizeof(buf), w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        return NULL;
    }
    return buf;
}

/* ------------------------------------------------------------------ order */

static void testOrder(void)
{
    static const uint8_t red[4] = {200, 0, 0, 0x80}, blue[4] = {0, 0, 200, 0x80};
    static const uint8_t black[4] = {0, 0, 0, 0};
    rd_BeginFrame();
    rd_SelectList(5);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_TextureOff();
    sprite(256, 128, 0, 0, 16 * 16, 16 * 16, red, 0, 0, 0, 0);
    rd_SelectList(1);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK0), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_TextureOff();
    sprite(256, 128, 0, 0, 16 * 16, 16 * 16, blue, 0, 0, 0, 0);
    rd_EndFrame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK0, &w, &h);
    if (img) {
        CHECK(img[0] == 200 && img[2] == 0, "list 5 replays after list 1 (got %u,%u,%u)", img[0],
              img[1], img[2]);
    }
}

/* ---------------------------------------------------------------- railing */

/* Package P8: issue 9's lattice.  On the GS a hole texel (alpha 0) fails
 * the alpha test, FB_ONLY keeps its colour (blended to Cd by As = 0) and
 * drops its Z, so geometry drawn later behind the railing passes the Z
 * test there.  A Z write by the failing pass would leave the clear colour
 * in every hole.  Run with FBA off and on: the game's materials set FBA
 * (Packet.c), and the holes rest on the FB_ONLY pass with it. */
#define RAIL_Z 0x80000000u
#define WALL_Z 0x40000000u

static void testRailing(int fba)
{
    /* 16 x 16: wires (x % 4 == 0 or y % 4 == 0) white with alpha 0x80,
     * holes black with alpha 0 */
    static uint8_t lattice[16 * 16 * 4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint8_t *p = &lattice[(y * 16 + x) * 4];
            const int wire = x % 4 == 0 || y % 4 == 0;
            p[0] = p[1] = p[2] = wire ? 200 : 0;
            p[3] = wire ? 0x80 : 0;
        }
    }
    RdTex t = rd_CreateTexture(16, 16, lattice, RD_TEXA_80_80, "lattice");
    static const uint8_t clr[4] = {0, 0, 0, 0}, grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t wall[4] = {30, 160, 60, 0x80};
    const int32_t ox = (2048 - 256) * 16, oy = (2048 - 256) * 16;
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), clr, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    /* the railing: 16 x 16 texels (UVs in 1/16 texel) magnified 4x at
     * (64, 64), in world space like the game's 3D sprites */
    rd_SelectList(1);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_TestGs(0x5160D);
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_PABE(0);
    rd_FBA(fba);
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    {
        RdScreenVtx v[2] = {vtx(ox + 64 * 16, oy + 64 * 16, RAIL_Z, grey, 0.0f, 0.0f),
                            vtx(ox + 128 * 16, oy + 128 * 16, RAIL_Z, grey, 256.0f, 256.0f)};
        rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    /* the wall behind it, drawn later: opaque, Z GEQUAL, Z write */
    rd_SelectList(2);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_TestGs(RD_TEST_Z_GEQUAL);
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_FBA(0);
    rd_TextureOff();
    {
        RdScreenVtx v[2] = {vtx(ox + 48 * 16, oy + 48 * 16, WALL_Z, wall, 0.0f, 0.0f),
                            vtx(ox + 144 * 16, oy + 144 * 16, WALL_Z, wall, 0.0f, 0.0f)};
        rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_EndFrame(0);

    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_SCENE, &w, &h);
    static float depth[512 * 512];
    uint32_t dw = 0, dh = 0;
    const bool zOk =
        rd__ReadTargetDepth(rd_Target(RD_TARGET_SCENE), depth, sizeof(depth), &dw, &dh);
    CHECK(zOk && dw == w && dh == h, "railing: depth readback");
    if (!img || !zOk) {
        rd_DestroyTexture(t);
        return;
    }
    const float zScale = rd__TargetZScale(rd_Target(RD_TARGET_SCENE).id);
    const float railD = rd__GsDepth(RAIL_Z, zScale), wallD = rd__GsDepth(WALL_Z, zScale);
    /* the depth grows with GS Z (gs_z_to_depth, package QUEEN) */
    CHECK(wallD < railD, "railing: the wall is behind the railing (%g, %g)", (double)railD,
          (double)wallD);
    int holeBad = 0, wireBad = 0, holeZBad = 0, wireZBad = 0, holes = 0, wires = 0;
    /* pixel centres of the railing, texel (tx, ty) = ((x - 64) / 4, (y - 64) / 4) */
    for (uint32_t y = 64; y < 128; y++) {
        for (uint32_t x = 64; x < 128; x++) {
            const int tx = (int)(x - 64) / 4, ty = (int)(y - 64) / 4;
            const int wire = tx % 4 == 0 || ty % 4 == 0;
            const uint8_t *p = &img[(y * w + x) * 4];
            const float d = depth[y * w + x];
            if (wire) {
                wires++;
                wireBad += p[0] != 200 || p[1] != 200 || p[2] != 200;
                wireZBad += d != railD;
            } else {
                holes++;
                holeBad += p[0] != wall[0] || p[1] != wall[1] || p[2] != wall[2];
                holeZBad += d != wallD;
            }
        }
    }
    printf("  railing (FBA %d): %d hole pixels, %d show something other than the wall, %d hold "
           "other than its Z; %d wire pixels, %d off colour, %d off Z\n",
           fba, holes, holeBad, holeZBad, wires, wireBad, wireZBad);
    CHECK(holeBad == 0, "railing (FBA %d): %d of %d hole pixels do not show the wall drawn behind",
          fba, holeBad, holes);
    CHECK(holeZBad == 0, "railing (FBA %d): %d of %d hole pixels do not hold the wall's Z", fba,
          holeZBad, holes);
    CHECK(wireBad == 0 && wireZBad == 0, "railing (FBA %d): wires %d off colour, %d off Z", fba,
          wireBad, wireZBad);
    rd_DestroyTexture(t);
}

/* ------------------------------------------------------------ DATE, flat */

/* Wave 2: TEST.DATE against the R8 snapshot, and PRIM.IIP 0.  WORK0 gets
 * alpha 0 on its left half and 0x80 on its right; a DATM=1 sprite over all
 * of it lands on the right half only, a DATM=0 one on the left only.  A flat
 * triangle takes its last vertex's colour. */
static void testDateFlat(void)
{
    static const uint8_t a0[4] = {10, 10, 10, 0}, a1[4] = {20, 20, 20, 0x80};
    static const uint8_t red[4] = {200, 0, 0, 0x80}, green[4] = {0, 200, 0, 0x80};
    static const uint8_t blue[4] = {0, 0, 200, 0x80};
    rd_BeginFrame();
    rd_SelectList(5);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_TextureOff();
    sprite(256, 128, 0, 0, 128 * 16, 64 * 16, a0, 0, 0, 0, 0);
    sprite(256, 128, 128 * 16, 0, 256 * 16, 64 * 16, a1, 0, 0, 0, 0);
    rd_TestGs(RD_TEST_RGBONLY_DATE1); /* DATE DATM 1, RGB-only AFAIL with ATE off */
    sprite(256, 128, 0, 0, 256 * 16, 32 * 16, red, 0, 0, 0, 0);
    rd_TestGs(RD_TEST_DATE0); /* DATE DATM 0 */
    sprite(256, 128, 0, 32 * 16, 256 * 16, 64 * 16, green, 0, 0, 0, 0);
    /* a flat triangle below: last vertex blue */
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_Gouraud(0);
    {
        const int32_t ox = (2048 - 128) * 16, oy = (2048 - 64) * 16;
        RdScreenVtx t[3] = {vtx(ox + 0, oy + 64 * 16, 0, red, 0, 0),
                            vtx(ox + 256 * 16, oy + 64 * 16, 0, green, 0, 0),
                            vtx(ox + 0, oy + 128 * 16, 0, blue, 0, 0)};
        rd_ScreenPrims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_UI, 1, 0);
    }
    rd_Gouraud(1);
    rd_EndFrame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK0, &w, &h);
    if (!img) {
        return;
    }
    const uint8_t *l0 = &img[(10 * w + 40) * 4], *r0 = &img[(10 * w + 200) * 4];
    const uint8_t *l1 = &img[(40 * w + 40) * 4], *r1 = &img[(40 * w + 200) * 4];
    const uint8_t *tri = &img[(70 * w + 20) * 4];
    CHECK(l0[0] == 10 && r0[0] == 200, "DATM 1 draws where the alpha MSB is set (%u, %u)", l0[0],
          r0[0]);
    CHECK(l1[1] == 200 && r1[1] == 20, "DATM 0 draws where it is clear (%u, %u)", l1[1], r1[1]);
    CHECK(tri[0] == 0 && tri[1] == 0 && tri[2] == 200, "flat triangle %u,%u,%u", tri[0], tri[1],
          tri[2]);
}

/* ------------------------------------------------------- screen-prim runs */

/* Package PC: consecutive screen-prim commands under the same state are one
 * draw (rd_replay.c doScreen).  The frame is replayed merged and with
 * merging off (rd__SetScreenMerge); WORK0 must be the same bytes, and the
 * draws are counted (RdPerfRecord screenCmds, screenDraws). */
static void runReplay(const RdFrame *f, bool merge, uint8_t *dst, uint32_t *cmds, uint32_t *draws)
{
    rd__SetScreenMerge(merge);
    rd__ReplayFrame(f, 0, false);
    rd__SetScreenMerge(true);
    *cmds = g_rdPerf.screenCmds;
    *draws = g_rdPerf.screenDraws;
    uint32_t w, h;
    const uint8_t *img = readTarget(RD_TARGET_WORK0, &w, &h);
    if (img) {
        memcpy(dst, img, (size_t)256 * 128 * 4);
    }
}

static void checkRun(const char *what, uint32_t wantCmds, uint32_t wantDraws)
{
    static uint8_t merged[256 * 128 * 4], seq[256 * 128 * 4];
    uint32_t c0, d0, c1, d1;
    memset(merged, 0, sizeof(merged));
    memset(seq, 0xFF, sizeof(seq));
    const RdFrame *f = rd__LastFrame();
    CHECK(f != NULL, "%s: no closed frame", what);
    if (!f) {
        return;
    }
    runReplay(f, true, merged, &c0, &d0);
    runReplay(f, false, seq, &c1, &d1);
    size_t diff = 0;
    for (size_t i = 0; i < sizeof(merged); i++) {
        diff += merged[i] != seq[i];
    }
    printf("  %s: %u screen-prim draws one per command, %u merged; %zu bytes differ\n", what, c0,
           d0, diff);
    CHECK(diff == 0, "%s: the merged replay differs from the sequential one in %zu bytes", what,
          diff);
    CHECK(c0 == wantCmds && d0 == wantDraws, "%s: %u draws merged into %u, expected %u into %u",
          what, c0, d0, wantCmds, wantDraws);
    CHECK(c1 == wantCmds && d1 == wantCmds, "%s: unmerged %u draws, %u recorded, expected %u", what,
          c1, d1, wantCmds);
}

/* a sprite of colour i (rgb from the index, alpha a) */
static void runSprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t i, uint8_t a)
{
    const uint8_t c[4] = {(uint8_t)(40 + 70 * (i % 3)), (uint8_t)(30 + 50 * (i % 5)),
                          (uint8_t)(220 - 35 * (i % 6)), a};
    sprite(256, 128, x0 * 16, y0 * 16, x1 * 16, y1 * 16, c, 0, 0, 0, 0);
}

static void testScreenRuns(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0};
    /* overlapping, differently coloured sprites under one state, blended
     * (LERP As: the result depends on the order), alpha above and below
     * 0x80: one run, one draw, the bytes of six draws */
    rd_BeginFrame();
    rd_SelectList(5);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK0), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_TextureOff();
    for (uint32_t i = 0; i < 6; i++) {
        runSprite(10 + (int32_t)i * 17, 8 + (int32_t)i * 9, 90 + (int32_t)i * 19,
                  70 + (int32_t)i * 7, i, (uint8_t)(0x30 + i * 0x18));
    }
    rd_EndFrame(0);
    checkRun("overlapping run", 6, 1);

    /* the boundaries: a run of two; two DATE sprites under the same state
     * (the first writes alpha, so the second retakes the snapshot); two
     * AFAIL FB_ONLY sprites (two passes each, never merged); two sprites,
     * a scissor change, two more */
    rd_BeginFrame();
    rd_SelectList(5);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK0), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_TextureOff();
    runSprite(0, 0, 120, 60, 0, 0x90);
    runSprite(60, 20, 200, 100, 1, 0x20);
    rd_TestGs(RD_TEST_DATE0);
    runSprite(20, 10, 160, 90, 2, 0xA0);
    runSprite(40, 30, 240, 120, 3, 0x50);
    rd_TestGs(RD_TEST_AT_GT64_FBONLY);
    runSprite(5, 40, 150, 110, 4, 0x60);
    runSprite(80, 0, 250, 80, 5, 0x70);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    runSprite(0, 50, 100, 128, 0, 0x40);
    runSprite(30, 70, 130, 128, 1, 0x88);
    rd_Scissor(16, 8, 200, 100);
    runSprite(0, 0, 256, 128, 2, 0x30);
    runSprite(50, 30, 220, 110, 3, 0x58);
    rd_EndFrame(0);
    checkRun("run boundaries", 2 + 2 + 4 + 4, 1 + 2 + 4 + 2);
}

/* -------------------------------------------------------------------- AA1 */

/* the coverage alpha of an edge pixel: the 16-bit coverage >> 9 (0..0x7F),
 * as PCSX2's GSDrawScanline takes it */
static int covAs(double cov)
{
    cov = cov < 0.0 ? 0.0 : (cov > 1.0 ? 1.0 : cov);
    return (int)floor(cov * 65535.0) >> 9;
}

static void testAa1(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0}, grey[4] = {0x80, 0x80, 0x80, 0x80};
    const int32_t ox = (2048 - 128) * 16, oy = (2048 - 128) * 16;
    /* the line (12.4, relative to the target) and the triangle */
    const int32_t la[2] = {20 * 16 + 5, 40 * 16 + 7}, lb[2] = {220 * 16 + 3, 90 * 16 + 12};
    const int32_t t0[2] = {30 * 16 + 5, 140 * 16 + 10}, t1[2] = {225 * 16 + 13, 160 * 16 + 3},
                  t2[2] = {120 * 16 + 2, 240 * 16 + 8};
    rd_BeginFrame();
    rd_SelectList(11);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK1), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D(); /* LERP As with ABE 0; WORLD prims, the space the game's AA1 draws use */
    rd_TextureOff();
    rd_AA1(1);
    {
        RdScreenVtx v[2] = {vtx(ox + la[0], oy + la[1], 0, grey, 0, 0),
                            vtx(ox + lb[0], oy + lb[1], 0, grey, 0, 0)};
        rd_ScreenPrims(RD_PRIM_LINES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_ABE(1);
    {
        RdScreenVtx v[3] = {vtx(ox + t0[0], oy + t0[1], 0, grey, 0, 0),
                            vtx(ox + t1[0], oy + t1[1], 0, grey, 0, 0),
                            vtx(ox + t2[0], oy + t2[1], 0, grey, 0, 0)};
        rd_ScreenPrims(RD_PRIM_TRIANGLES, v, 3, RD_SPACE_WORLD, 1, 0);
    }
    rd_AA1(0);
    rd_EndFrame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK1, &w, &h);
    if (!img) {
        return;
    }
    int bad = 0, maxd = 0, edge = 0;
    /* the line: every pixel of columns 22..218, rows 30..100 */
    for (int x = 22; x <= 218; x++) {
        const double yl =
            (la[1] + (double)(x * 16 - la[0]) * (lb[1] - la[1]) / (lb[0] - la[0])) / 16.0;
        for (int y = 30; y <= 100; y++) {
            const int want = covAs(1.0 - fabs(yl - y));
            const int got = img[(y * w + x) * 4];
            const int d = abs(got - want);
            maxd = d > maxd ? d : maxd;
            edge += want > 0;
            if (d > 1 && bad++ < 8) {
                printf("FAIL aa1 line (%d,%d): As %d, expected %d (line at y %.4f)\n", x, y, got,
                       want, yl);
            }
        }
    }
    printf("  aa1 line: %d edge pixels, max difference %d LSB of As\n", edge, maxd);
    CHECK(bad == 0 && edge > 300, "aa1 line: %d pixels over 1 LSB (%d edge pixels)", bad, edge);
    /* the triangle: columns 40..200 around its top edge t0-t1 (inside the
       other two edges there): 0 above the fringe, 1 - d in it, 0x80 below */
    bad = maxd = edge = 0;
    for (int x = 40; x <= 200; x++) {
        const double ye =
            (t0[1] + (double)(x * 16 - t0[0]) * (t1[1] - t0[1]) / (t1[0] - t0[0])) / 16.0;
        for (int y = (int)ye - 4; y <= (int)ye + 4; y++) {
            const int want = (double)y >= ye ? 0x80 : covAs(1.0 - (ye - y));
            const int got = img[(y * w + x) * 4];
            const int d = abs(got - want);
            maxd = d > maxd ? d : maxd;
            edge += want > 0 && want < 0x80;
            if (d > 1 && bad++ < 8) {
                printf("FAIL aa1 triangle (%d,%d): As %d, expected %d (edge at y %.4f)\n", x, y,
                       got, want, ye);
            }
        }
    }
    printf("  aa1 triangle edge: %d fringe pixels, max difference %d LSB of As\n", edge, maxd);
    CHECK(bad == 0 && edge > 140, "aa1 triangle: %d pixels over 1 LSB (%d fringe pixels)", bad,
          edge);
}

/* ----------------------------------------------------------------- sprite */

typedef struct SpriteCase {
    int32_t x0, y0, x1, y1; /* 12.4, relative to the target's top-left */
    uint8_t c[4];
} SpriteCase;

static int ceil16(int32_t v)
{
    return (v + 15) >> 4; /* v >= 0 */
}

static void testSprites(void)
{
    static const SpriteCase cases[] = {
        {10 * 16, 20 * 16, 30 * 16, 25 * 16, {255, 0, 0, 0x80}},                 /* integer edges */
        {40 * 16 + 8, 20 * 16 + 8, 50 * 16 + 8, 30 * 16 + 8, {0, 255, 0, 0x80}}, /* half */
        {60 * 16 - 4, 20 * 16 - 4, 70 * 16 - 4, 30 * 16 - 4, {0, 0, 255, 0x80}}, /* -4 nudge */
        {80 * 16 + 1, 20 * 16 + 1, 80 * 16 + 15, 20 * 16 + 15, {255, 255, 255, 0x80}}, /* none */
        {90 * 16, 20 * 16, 90 * 16 + 1, 20 * 16 + 1, {255, 255, 0, 0x80}},   /* one pixel */
        {100 * 16 + 8, 40 * 16, 101 * 16 + 8, 41 * 16, {0, 255, 255, 0x80}}, /* pixel 101 */
        {3 * 16 + 12, 50 * 16 + 4, 9 * 16 + 4, 57 * 16 + 12, {255, 0, 255, 0x80}},
    };
    const int nc = (int)(sizeof(cases) / sizeof(cases[0]));
    static const uint8_t black[4] = {0, 0, 0, 0}, grey[4] = {0x80, 0x80, 0x80, 0x80};

    /* 16x16 texture with distinct texels; RGB24 texture for TEXA */
    static uint8_t tex[16 * 16 * 4], tex24[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; i++) {
        uint32_t hsh = hash((uint32_t)i + 77);
        tex[i * 4 + 0] = (uint8_t)hsh;
        tex[i * 4 + 1] = (uint8_t)(hsh >> 8);
        tex[i * 4 + 2] = (uint8_t)(hsh >> 16);
        tex[i * 4 + 3] = (uint8_t)(hsh >> 24);
        int zero = (i % 5) == 0;
        tex24[i * 4 + 0] = zero ? 0 : (uint8_t)(hsh | 1);
        tex24[i * 4 + 1] = zero ? 0 : (uint8_t)(hsh >> 8);
        tex24[i * 4 + 2] = zero ? 0 : (uint8_t)(hsh >> 16);
        tex24[i * 4 + 3] = 0xEE; /* ignored for RGB24 */
    }
    RdTex t32 = rd_CreateTexture(16, 16, tex, RD_TEXA_80_80, "t32");
    RdTex t24 = rd_CreateTextureSrc(16, 16, tex24, RD_TEXSRC_RGB24, "t24");

    rd_BeginFrame();
    rd_SelectList(11);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK1), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D();
    rd_TextureOff();
    for (int i = 0; i < nc; i++) {
        sprite(256, 256, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, cases[i].c, 0, 0, 0,
               0);
    }
    /* textured 1:1 with the +8 UV nudge, nearest */
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t32, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(256, 256, 120 * 16, 100 * 16, 136 * 16, 116 * 16, grey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    /* RGB24 source under TEXA 7F/81+AEM */
    rd_TexA(RD_TEXA_7F_81_AEM);
    rd_Texture(t24, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(256, 256, 150 * 16, 100 * 16, 166 * 16, 116 * 16, grey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    /* UV offset: a quarter of the texture to the right, clamped */
    rd_TexA(RD_TEXA_80_80);
    rd_Texture(t32, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_UVOffset(0.25f, 0.0f);
    sprite(256, 256, 180 * 16, 100 * 16, 192 * 16, 101 * 16, grey, 8, 8, 12 * 16 + 8, 1 * 16 + 8);
    rd_UVOffset(0.0f, 0.0f);
    rd_EndFrame(0);

    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK1, &w, &h);
    if (!img) {
        return;
    }
    /* untextured cases: the whole 0..99 x 0..99 region against the rule */
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 110; x++) {
            int want[4] = {0, 0, 0, 0};
            for (int i = 0; i < nc; i++) {
                const SpriteCase *c = &cases[i];
                if (x >= ceil16(c->x0) && x < ceil16(c->x1) && y >= ceil16(c->y0) &&
                    y < ceil16(c->y1)) {
                    for (int k = 0; k < 4; k++) {
                        want[k] = c->c[k];
                    }
                }
            }
            const uint8_t *p = img + ((size_t)y * w + (size_t)x) * 4;
            if (p[0] != want[0] || p[1] != want[1] || p[2] != want[2] || p[3] != want[3]) {
                pixFail("GS sprite coverage", x, y, p, want);
            }
        }
    }
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *s = &tex[(y * 16 + x) * 4];
            const uint8_t *p = img + ((size_t)(100 + y) * w + (size_t)(120 + x)) * 4;
            int want[4] = {s[0], s[1], s[2], s[3]};
            if (memcmp(p, s, 4) != 0) {
                pixFail("textured sprite, +8 UV nudge", 120 + x, 100 + y, p, want);
            }
            const uint8_t *s24 = &tex24[(y * 16 + x) * 4];
            const uint8_t *p24 = img + ((size_t)(100 + y) * w + (size_t)(150 + x)) * 4;
            int a = (int)gs_texa_alpha(s24[0], s24[1], s24[2], 0, TEXA_7F_81_AEM, TEXFMT_RGB24);
            int want24[4] = {s24[0], s24[1], s24[2], (int)gs_tfx_mod((uint32_t)a, 0x80)};
            if (p24[0] != want24[0] || p24[1] != want24[1] || p24[2] != want24[2] ||
                p24[3] != want24[3]) {
                pixFail("RGB24 under TEXA 7F/81+AEM", 150 + x, 100 + y, p24, want24);
            }
        }
    }
    for (int x = 0; x < 12; x++) {
        const uint8_t *s = &tex[(x + 4) * 4];
        const uint8_t *p = img + ((size_t)100 * w + (size_t)(180 + x)) * 4;
        int want[4] = {s[0], s[1], s[2], s[3]};
        if (memcmp(p, s, 4) != 0) {
            pixFail("rd_UVOffset 0.25", 180 + x, 100, p, want);
        }
    }
    rd_DestroyTexture(t32);
    rd_DestroyTexture(t24);
}

/* ------------------------------------------------------------------ TEXA */

/* The GS texel of an RGBA16 texture (RGBA8 with the A bit in the alpha
 * byte) at (x, y), CLAMP, TEXA expanded */
static void texaTexel(const uint8_t *tex, int n, int x, int y, uint32_t mode, uint32_t o[4])
{
    x = x < 0 ? 0 : (x >= n ? n - 1 : x);
    y = y < 0 ? 0 : (y >= n ? n - 1 : y);
    const uint8_t *t = &tex[(y * n + x) * 4];
    o[0] = t[0];
    o[1] = t[1];
    o[2] = t[2];
    o[3] = gs_texa_alpha(t[0], t[1], t[2], t[3], mode, TEXFMT_RGBA16);
}

/* Package TEXA: an RGBA16 texture whose texels mix A = 0 and A = 1 and
 * black ones (AEM), drawn magnified 4x (UV 8 + 4 x in 12.4, the 4-bit
 * fractions 0, 4, 8 and 12) under TEXA 7F/81+AEM, MODULATE by 0x80 with
 * TCC RGBA: every pixel is the GS order (TEXA per texel, then the 4-bit
 * bilinear, floor of the sum >> 8), 0 LSB, through sprite_texa_ps; the
 * same sprite with nearest filtering (UV 10 + 4 x, off the texel edges) is
 * the expanded texel under it, 0 LSB,
 * through sprite_ps as before.  The planner gives sprite_texa_ps only to a
 * 24- or 16-bit texture under AEM with a linear filter. */
static void testTexa(void)
{
    enum { N = 16, S = 4 };

    static uint8_t tex[N * N * 4];
    for (int i = 0; i < N * N; i++) {
        const uint32_t hsh = hash((uint32_t)i + 991);
        const int black = (hsh >> 24) % 4 == 0;
        tex[i * 4 + 0] = black ? 0 : (uint8_t)((hsh & 0x1F) << 3 | 8);
        tex[i * 4 + 1] = black ? 0 : (uint8_t)(((hsh >> 5) & 0x1F) << 3);
        tex[i * 4 + 2] = black ? 0 : (uint8_t)(((hsh >> 10) & 0x1F) << 3);
        tex[i * 4 + 3] = (uint8_t)((hsh >> 16) & 1); /* the A bit */
    }
    static const uint8_t black[4] = {0, 0, 0, 0}, grey[4] = {0x80, 0x80, 0x80, 0x80};
    RdTex t16 = rd_CreateTextureSrc(N, N, tex, RD_TEXSRC_RGBA16, "texa rgba16");
    RdTex t32 = rd_CreateTexture(N, N, tex, RD_TEXA_80_80, "texa rgba32");

    /* the planner */
    {
        RdStateBlock st;
        rd__ResetStateBlock(&st);
        st.ds.texEnabled = 1;
        st.tex = t16.id;
        st.ds.texa = RD_TEXA_7F_81_AEM;
        st.ds.magFilter = st.ds.minFilter = RD_FILTER_LINEAR;
        RdDrawPass dp[2];
        rd__PlanScreenDraw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                           RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE_TEXA, "texa: RGBA16, AEM, bilinear: sprite_texa_ps");
        st.ds.magFilter = RD_FILTER_NEAREST;
        rd__PlanScreenDraw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                           RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE_TEXA, "texa: linear MIN alone: sprite_texa_ps");
        st.ds.minFilter = RD_FILTER_NEAREST;
        rd__PlanScreenDraw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                           RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: nearest: sprite_ps");
        st.ds.magFilter = st.ds.minFilter = RD_FILTER_LINEAR;
        st.ds.texa = RD_TEXA_80_80;
        rd__PlanScreenDraw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                           RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: TEXA 80/80: sprite_ps");
        st.ds.texa = RD_TEXA_80_80_AEM;
        st.tex = t32.id;
        rd__PlanScreenDraw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                           RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: RGBA32: sprite_ps");
    }

    rd_BeginFrame();
    rd_SelectList(11);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK1), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D();
    rd_TexA(RD_TEXA_7F_81_AEM);
    rd_Texture(t16, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    sprite(256, 256, 10 * 16, 10 * 16, (10 + N * S) * 16, (10 + N * S) * 16, grey, 8, 8, N * 16 + 8,
           N * 16 + 8);
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    /* UV 10 + 4 x: off the texel edges, where the nearest texel is unambiguous */
    sprite(256, 256, 100 * 16, 10 * 16, (100 + N * S) * 16, (10 + N * S) * 16, grey, 10, 10,
           N * 16 + 10, N * 16 + 10);
    rd_TexA(RD_TEXA_80_80);
    rd_EndFrame(0);

    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK1, &w, &h);
    if (img) {
        int bad = 0, badN = 0, maxd = 0, after = 0;
        for (int y = 0; y < N * S; y++) {
            for (int x = 0; x < N * S; x++) {
                /* the GS: u = 8 + 4 x (12.4), u - 8 split into texel and fraction */
                const int uu = 4 * x, vv = 4 * y;
                const int i0 = uu >> 4, j0 = vv >> 4;
                const uint32_t fu = (uint32_t)(uu & 15), fv = (uint32_t)(vv & 15);
                uint32_t a[4], b[4], c[4], d[4];
                texaTexel(tex, N, i0, j0, TEXA_7F_81_AEM, a);
                texaTexel(tex, N, i0 + 1, j0, TEXA_7F_81_AEM, b);
                texaTexel(tex, N, i0, j0 + 1, TEXA_7F_81_AEM, c);
                texaTexel(tex, N, i0 + 1, j0 + 1, TEXA_7F_81_AEM, d);
                int want[4], late[4];
                for (int k = 0; k < 4; k++) {
                    want[k] = (int)((a[k] * (16 - fu) * (16 - fv) + b[k] * fu * (16 - fv) +
                                     c[k] * (16 - fu) * fv + d[k] * fu * fv) >>
                                    8);
                    want[k] = (int)gs_tfx_mod((uint32_t)want[k], 0x80);
                }
                /* the order sprite_ps keeps: RGB and the A bit filtered, TEXA after */
                {
                    uint32_t ab[4] = {0, 0, 0, 0};
                    const int tx[4] = {i0, i0 + 1, i0, i0 + 1}, ty[4] = {j0, j0, j0 + 1, j0 + 1};
                    const uint32_t wt[4] = {(16 - fu) * (16 - fv), fu * (16 - fv), (16 - fu) * fv,
                                            fu * fv};
                    for (int q = 0; q < 4; q++) {
                        const int cx = tx[q] >= N ? N - 1 : tx[q], cy = ty[q] >= N ? N - 1 : ty[q];
                        for (int k = 0; k < 4; k++) {
                            ab[k] += tex[(cy * N + cx) * 4 + k] * wt[q];
                        }
                    }
                    for (int k = 0; k < 4; k++) {
                        late[k] = (int)((ab[k] + 128) >> 8);
                    }
                    late[3] =
                        (int)gs_texa_alpha((uint32_t)late[0], (uint32_t)late[1], (uint32_t)late[2],
                                           (uint32_t)late[3], TEXA_7F_81_AEM, TEXFMT_RGBA16);
                }
                after += late[3] != want[3];
                const uint8_t *p = img + ((size_t)(10 + y) * w + (size_t)(10 + x)) * 4;
                int dmax = 0;
                for (int k = 0; k < 4; k++) {
                    const int e = abs((int)p[k] - want[k]);
                    dmax = e > dmax ? e : dmax;
                }
                maxd = dmax > maxd ? dmax : maxd;
                if (dmax != 0 && bad++ < 8) {
                    pixFail("TEXA before the bilinear filter (RGBA16, 7F/81+AEM)", 10 + x, 10 + y,
                            p, want);
                }
                /* nearest: texel (10 + 4 x) >> 4, expanded */
                uint32_t t[4];
                texaTexel(tex, N, (10 + uu) >> 4, (10 + vv) >> 4, TEXA_7F_81_AEM, t);
                const int wn[4] = {(int)t[0], (int)t[1], (int)t[2], (int)gs_tfx_mod(t[3], 0x80)};
                const uint8_t *pn = img + ((size_t)(10 + y) * w + (size_t)(100 + x)) * 4;
                if ((pn[0] != wn[0] || pn[1] != wn[1] || pn[2] != wn[2] || pn[3] != wn[3]) &&
                    badN++ < 8) {
                    pixFail("RGBA16 nearest under 7F/81+AEM", 100 + x, 10 + y, pn, wn);
                }
            }
        }
        printf("  texa: %d pixels bilinear, max difference %d from the GS order (TEXA per "
               "texel); %d of them would differ in alpha with TEXA after the filter\n",
               N * S * N * S, maxd, after);
        CHECK(bad == 0, "texa: %d bilinear pixels differ from the GS order", bad);
        CHECK(after > 100, "texa: the texture exercises the order (%d pixels)", after);
        CHECK(badN == 0, "texa: %d nearest pixels differ", badN);
    }
    rd_DestroyTexture(t16);
    rd_DestroyTexture(t32);
}

/* ------------------------------------------------------------------- STQ */

/* A textured triangle strip receding in depth: the near edge has Q = 1, the
 * far edge Q = 0.25 (S = u * Q).  The GS interpolates S, T and Q linearly on
 * the screen and divides per pixel, so U at a fraction f along the quad is
 * f * q1 / (q0 + f * (q1 - q0)), not f.  Texel x of the 64-wide texture has
 * R = 4 x.  A second strip with every Q = 1 (UV mode: nothing changes) must
 * stay affine. */
static void testStq(void)
{
    static uint8_t tex[64 * 4 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t *p = &tex[(y * 64 + x) * 4];
            p[0] = (uint8_t)(x * 4);
            p[1] = (uint8_t)(y * 60);
            p[2] = 7;
            p[3] = 0x80;
        }
    }
    RdTex t = rd_CreateTexture(64, 4, tex, RD_TEXA_80_80, "stq");
    static const uint8_t black[4] = {0, 0, 0, 0}, white[4] = {0x80, 0x80, 0x80, 0x80};
    rd_BeginFrame();
    rd_SelectList(11);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK1), black, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_ABE(0);
    rd_PABE(0);
    rd_FBA(0);
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    const int32_t ox = (2048 - 128) * 16, oy = (2048 - 128) * 16;
    for (int strip = 0; strip < 2; strip++) {
        const float q1 = strip ? 1.0f : 0.25f;
        const int32_t y0 = (20 + 40 * strip) * 16, y1 = (50 + 40 * strip) * 16;
        RdScreenVtx v[4] = {vtx(ox + 16 * 16, oy + y0, 0, white, 0.0f, 0.0f),
                            vtx(ox + 16 * 16, oy + y1, 0, white, 0.0f, 1.0f),
                            vtx(ox + 144 * 16, oy + y0, 0, white, q1, 0.0f),
                            vtx(ox + 144 * 16, oy + y1, 0, white, q1, 1.0f)};
        v[2].q = v[3].q = q1;
        rd_ScreenPrims(RD_PRIM_TRIANGLE_STRIP, v, 4, RD_SPACE_WORLD, 0, 0);
    }
    rd_EndFrame(0);
    uint32_t w, h;
    const uint8_t *img = readTarget(RD_TARGET_WORK1, &w, &h);
    if (img) {
        for (int strip = 0; strip < 2; strip++) {
            const double q1 = strip ? 1.0 : 0.25;
            const int y = 35 + 40 * strip;
            for (int x = 20; x < 140; x += 3) {
                const double f = ((double)x + 0.5 - 16.0) / 128.0;
                const double u = f * q1 / (1.0 + f * (q1 - 1.0));
                const double want = u * 64.0;
                const uint8_t *p = img + ((size_t)y * w + (size_t)x) * 4;
                if (fabs((double)p[0] / 4.0 + 0.5 - want) > 1.6) {
                    const int wantv[4] = {(int)(want * 4.0), 0, 7, 0x80};
                    pixFail(strip ? "STQ with Q = 1 stays affine" : "perspective STQ", x, y, p,
                            wantv);
                }
            }
        }
    }
    rd_DestroyTexture(t);
}

/* ------------------------------------------------------------------ font */

static void fontState(void)
{
    rd_Blend(RD_BLEND_LERP_AS, 0, 1); /* port/ui/font.c setState */
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_FBA(0);
    rd_PABE(0);
    rd_ABE(1);
    rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
}

static void testFont(const char *dir)
{
    /* coverage in GS units: none, full, past full, values either side of
       the halves */
    static const uint8_t cov[16] = {0,  1,  2,  31,  32,  63,  64,  65,
                                    95, 96, 97, 127, 128, 129, 255, 77};
    uint8_t rgba[16 * 4];
    for (int i = 0; i < 16; i++) {
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0xFF;
        rgba[i * 4 + 3] = cov[i];
    }
    RdTex r8 = rd_CreateTextureR8(4, 4, cov, "font r8");
    RdTex t32 = rd_CreateTexture(4, 4, rgba, RD_TEXA_80_80, "font rgba8");
    static const uint8_t bg[4] = {40, 80, 120, 0x80};
    static const uint8_t col[4] = {0x70, 0x50, 0x80, 0x60};
    rd_BeginFrame();
    rd_SelectList(11);
    rd_ClearTarget(rd_Target(RD_TARGET_WORK1), bg, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    fontState();
    for (int k = 0; k < 2; k++) {
        rd_Texture(k ? t32 : r8, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        /* 1:1, the +8 nudge: each pixel samples a texel centre */
        sprite(256, 256, (10 + 10 * k) * 16, 10 * 16, (14 + 10 * k) * 16, 14 * 16, col, 8, 8,
               4 * 16 + 8, 4 * 16 + 8);
        /* 8x: bilinear between the texels */
        sprite(256, 256, (40 + 40 * k) * 16, 40 * 16, (72 + 40 * k) * 16, 72 * 16, col, 0, 0,
               4 * 16, 4 * 16);
    }
    rd_EndFrame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK1, &w, &h);
    if (img) {
        int bad = 0;
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                const uint8_t *a = &img[((size_t)(10 + y) * w + 10 + x) * 4];
                const uint8_t *b = &img[((size_t)(10 + y) * w + 20 + x) * 4];
                const int as = (int)gs_tfx_mod(cov[y * 4 + x], col[3]);
                if (a[3] != as || memcmp(a, b, 4) != 0) {
                    const int want[4] = {b[0], b[1], b[2], as};
                    bad += pixFail("R8 atlas 1:1 against RGBA8", 10 + x, 10 + y, a, want);
                }
            }
        }
        int worst[4] = {0, 0, 0, 0}, inked = 0, offA = 0;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                const uint8_t *a = &img[((size_t)(40 + y) * w + 40 + x) * 4];
                const uint8_t *b = &img[((size_t)(40 + y) * w + 80 + x) * 4];
                for (int c = 0; c < 4; c++) {
                    const int d = abs((int)a[c] - (int)b[c]);
                    worst[c] = d > worst[c] ? d : worst[c];
                }
                offA += a[3] != b[3];
                inked += a[3] != 0;
            }
        }
        CHECK(offA == 0 && worst[0] == 0 && worst[1] == 0 && worst[2] == 0,
              "R8 atlas magnified: %d alphas, worst %d %d %d %d off the RGBA8 path", offA, worst[0],
              worst[1], worst[2], worst[3]);
        CHECK(inked > 900, "R8 atlas magnified: %d pixels with alpha", inked);
        printf("  font: 1:1 %d off, magnified worst %d %d %d %d (%d alphas differ)\n", bad,
               worst[0], worst[1], worst[2], worst[3], offA);
        /* the dump keeps the format: load, replay, the same bytes */
        static uint8_t first[512 * 512 * 4];
        memcpy(first, img, (size_t)w * h * 4); /* readTarget's buffer is as large */
        char path[1024];
        snprintf(path, sizeof(path), "%s/rd_pixel_font.rddump", dir);
        CHECK(rd_DumpFrame(path), "rd_DumpFrame (font)");
        RdFrame f;
        if (rd__LoadFrame(path, &f)) {
            int r8s = 0;
            for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
                const RdTexRec *t = &g_rd.textures[i];
                r8s += t->live && t->kind == RD_TEXKIND_IMAGE && t->format == RD_TEXEL_R8 &&
                       t->w == 4 && t->h == 4 && memcmp(t->pixels, cov, 16) == 0 &&
                       strcmp(t->name, "dump") == 0;
            }
            CHECK(r8s == 1, "the loaded dump has the R8 atlas (%d)", r8s);
            static const uint8_t junk[4] = {1, 2, 3, 4};
            rd_BeginFrame();
            rd_SelectList(0);
            rd_ClearTarget(rd_Target(RD_TARGET_WORK1), junk, 0, 0);
            rd_EndFrame(0);
            CHECK(rd__ReplayFrame(&f, (int)f.keep, false), "replay of the loaded font frame");
            uint8_t *again = readTarget(RD_TARGET_WORK1, &w, &h);
            CHECK(again && memcmp(again, first, (size_t)w * h * 4) == 0,
                  "font dump -> load -> replay: the same WORK1");
            rd__FrameFree(&f);
        } else {
            CHECK(0, "rd__LoadFrame (font)");
        }
    }
    rd_DestroyTexture(r8);
    rd_DestroyTexture(t32);
}

/* ------------------------------------------------------------- reduction */

static uint8_t s_scene[512 * 512 * 4];

/* the synthetic scene: noise, drawn 1:1 into SCENE */
static void drawScene(RdTex t)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), clr, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(512, 512, 0, 0, 512 * 16, 512 * 16, grey, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
}

static void testReduction(const char *dir)
{
    for (int i = 0; i < 512 * 512; i++) {
        uint32_t hsh = hash((uint32_t)i * 3 + 1);
        /* smooth-ish gradients plus noise so the filter is exercised */
        s_scene[i * 4 + 0] = (uint8_t)((i % 512) / 2 + (hsh & 31));
        s_scene[i * 4 + 1] = (uint8_t)((i / 512) / 2 + ((hsh >> 8) & 63));
        s_scene[i * 4 + 2] = (uint8_t)hsh;
        s_scene[i * 4 + 3] = (uint8_t)((hsh >> 24) & 0x7F) + 0x40;
    }
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    /* tints at or below 0x80 (the game's reduction colours are of this kind) */
    const uint8_t tint[3] = {100, 128, 90};
    rd_BeginFrame();
    drawScene(t);
    rd_SelectList(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    memcpy(pp.rgba, tint, 3);
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);

    uint32_t w, h;
    uint8_t *sc = readTarget(RD_TARGET_SCENE, &w, &h);
    static uint8_t scene[512 * 512 * 4];
    if (!sc) {
        return;
    }
    memcpy(scene, sc, sizeof(scene));
    CHECK(memcmp(scene, s_scene, sizeof(scene)) == 0, "scene drawn 1:1 exactly");
    uint8_t *d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (!d) {
        return;
    }
    CHECK(w == 512 && h == 256, "DISPLAY is 512x256 (%ux%u)", w, h);
    int worst = 0;
    for (int py = 0; py < 256; py++) {
        for (int px = 0; px < 512; px++) {
            const uint8_t *p = d + ((size_t)py * 512 + (size_t)px) * 4;
            int want[4] = {0, 0, 0, 0};
            const int inside = px >= 2 && px <= 509 && py >= 8 && py <= 247;
            if (inside) {
                /* GS sample point (px, py): u = px + 0.75, v = 2py + 1 (texels);
                 * the GS bilinear between texels px, px+1 (4-bit weights 12
                 * and 4) and rows 2py, 2py+1 (8 and 8), the sum >> 8 */
                for (int k = 0; k < 4; k++) {
                    const int x0 = px, x1 = px + 1, y0 = 2 * py, y1 = 2 * py + 1;
                    const int v = 12 * 8 * scene[((size_t)y0 * 512 + x0) * 4 + k] +
                                  4 * 8 * scene[((size_t)y0 * 512 + x1) * 4 + k] +
                                  12 * 8 * scene[((size_t)y1 * 512 + x0) * 4 + k] +
                                  4 * 8 * scene[((size_t)y1 * 512 + x1) * 4 + k];
                    want[k] = (int)gs_tfx_mod((uint32_t)(v >> 8), k < 3 ? tint[k] : 0x80);
                }
            }
            int bad = 0;
            for (int k = 0; k < 4; k++) {
                int e = abs((int)p[k] - want[k]);
                if (e > worst) {
                    worst = e;
                }
                if (e > 0) {
                    bad = 1;
                }
            }
            if (bad) {
                pixFail("reduction", px, py, p, want);
            }
        }
    }
    printf("  reduction: worst channel error %d LSB\n", worst);
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_pixel_reduction.png", dir);
    rd_WritePng(path, d, 512, 256, 512 * 4, 1);
    rd_DestroyTexture(t);
}

/* ------------------------------------------------------------------ keep */

static void testKeep(void)
{
    /* a uniform scene, reduced; then a keep frame: KEEP + reduction at 128 */
    static const uint8_t col[4] = {200, 120, 40, 0x80}, clr[4] = {0, 0, 0, 0};
    static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    memcpy(pp.rgba, white, 3);
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), col, 1, 0);
    rd_SelectList(12);
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);
    rd_BeginFrame();
    rd_SelectList(0); /* not replayed in a keep frame */
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), clr, 1, 0);
    rd_SelectList(11);
    RdPostParams kp;
    memset(&kp, 0, sizeof(kp));
    kp.dst = rd_Target(RD_TARGET_SCENE);
    rd_Post(RD_POST_KEEP, &kp);
    rd_SelectList(12);
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(1);
    uint32_t w, h;
    uint8_t *d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (!d) {
        return;
    }
    /* far from the crop: DISPLAY = col * 112 >> 7; alpha is TA0 0x80 (DISPLAY read
     * as PSMCT24 under TEXA 80/80) times 128 */
    const uint8_t *p = d + ((size_t)128 * 512 + 256) * 4;
    int want[4] = {(200 * 112) >> 7, (120 * 112) >> 7, (40 * 112) >> 7, 0x80};
    int bad = 0;
    for (int k = 0; k < 4; k++) {
        bad |= abs((int)p[k] - want[k]) > 1;
    }
    if (bad) {
        pixFail("keep frame", 256, 128, p, want);
    }
}

/* ----------------------------------------------------------------- exact */

static void testExact(void)
{
    static uint8_t src[128 * 128 * 4];
    static int feed[128 * 128 * 4];
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t init[4] = {10, 20, 30, 40};
    for (int i = 0; i < 128 * 128; i++) {
        for (int k = 0; k < 4; k++) {
            feed[i * 4 + k] = init[k];
        }
    }
    RdTex t = rd_CreateTexture(128, 128, NULL, RD_TEXA_80_80, "exact src");

    static const struct {
        RdBlend eq;
        uint8_t fix;
    } modes[4] = {{RD_BLEND_LERP_FIX, 0x70},
                  {RD_BLEND_CD_SUB_CS_FIX, 0x10},
                  {RD_BLEND_CS_FIX_ADD_CD, 0x20},
                  {RD_BLEND_LERP_AS, 0}};

    int frameFails = 0;
    for (int fr = 0; fr < 100; fr++) {
        for (int i = 0; i < 128 * 128; i++) {
            uint32_t hsh = hash((uint32_t)(fr * 131071 + i));
            src[i * 4 + 0] = (uint8_t)hsh;
            src[i * 4 + 1] = (uint8_t)(hsh >> 8);
            src[i * 4 + 2] = (uint8_t)(hsh >> 16);
            src[i * 4 + 3] = (uint8_t)(hsh >> 24);
        }
        rd_UpdateTexture(t, src);
        const int m = fr % 4;
        rd_BeginFrame();
        rd_SelectList(7);
        if (fr == 0) {
            rd_ClearTarget(rd_Target(RD_TARGET_FEED128), init, 0, 0);
        }
        rd_SetTarget(rd_Target(RD_TARGET_AA1), (RdTarget){0}, 128, 128, 0);
        opaque2D();
        rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        sprite(128, 128, 0, 0, 128 * 16, 128 * 16, grey, 8, 8, 128 * 16 + 8, 128 * 16 + 8);
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.src = rd_Target(RD_TARGET_AA1);
        pp.dst = rd_Target(RD_TARGET_FEED128);
        pp.blend = (uint8_t)modes[m].eq;
        pp.fix = modes[m].fix;
        pp.exactInt = 1;
        rd_Post(RD_POST_COMPOSITE_FIX, &pp);
        rd_EndFrame(0);

        const uint32_t reg = rd__AlphaRegister((uint8_t)modes[m].eq);
        for (int i = 0; i < 128 * 128; i++) {
            const uint8_t *s = &src[i * 4];
            int *d = &feed[i * 4];
            int out[3];
            for (int k = 0; k < 3; k++) {
                out[k] = gs_blend_reg_ch(reg, s[k], d[k], s[3], d[3], modes[m].fix, 1);
            }
            d[0] = out[0];
            d[1] = out[1];
            d[2] = out[2];
            d[3] = s[3];
        }
        uint32_t w, h;
        uint8_t *g = readTarget(RD_TARGET_FEED128, &w, &h);
        if (!g) {
            return;
        }
        int bad = 0;
        for (int i = 0; i < 128 * 128 && !bad; i++) {
            for (int k = 0; k < 4; k++) {
                if (g[i * 4 + k] != feed[i * 4 + k]) {
                    if (frameFails < 5) {
                        pixFail("exact feedback blend", i % 128, i / 128, &g[i * 4], &feed[i * 4]);
                    }
                    bad = 1;
                    break;
                }
            }
        }
        frameFails += bad;
    }
    CHECK(frameFails == 0, "exact blend differed in %d of 100 frames", frameFails);
    printf("  exact feedback: 100 frames, %d differing\n", frameFails);
    rd_DestroyTexture(t);
}

/* ------------------------------------------------------------------ dump */

static void recordRichFrame(RdTex t)
{
    rd_BeginFrame();
    drawScene(t);
    rd_SelectList(2);
    /* additive with As up to 0xFF (DF_PREMUL), then a LERP_FIX quad */
    static const uint8_t add[4] = {60, 30, 90, 0xFF};
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
    rd_TextureOff();
    sprite(512, 512, 100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
    rd_SelectList(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 0x40;
    pp.rgba[0] = 0x20;
    rd_Post(RD_POST_FADE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x60;
    rd_Post(RD_POST_LETTERBOX, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 5;
    rd_Post(RD_POST_BRIGHTNESS, &pp);
    rd_SelectList(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = 120;
    pp.rgba[2] = 110;
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);
}

static void testDump(const char *dir)
{
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordRichFrame(t);
    uint32_t w, h;
    static uint8_t a[512 * 256 * 4];
    uint8_t *d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (!d) {
        return;
    }
    memcpy(a, d, sizeof(a));
    char path[1024];
    snprintf(path, sizeof(path), "%s/rd_pixel_frame.rddump", dir);
    CHECK(rd_DumpFrame(path), "rd_DumpFrame");
    RdFrame f;
    if (!rd__LoadFrame(path, &f)) {
        CHECK(0, "rd__LoadFrame");
        return;
    }
    /* scribble on the targets, then replay the loaded frame */
    static const uint8_t junk[4] = {1, 2, 3, 4};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), junk, 1, 0x1234);
    rd_ClearTarget(rd_Target(RD_TARGET_DISPLAY), junk, 0, 0);
    rd_EndFrame(0);
    CHECK(rd__ReplayFrame(&f, (int)f.keep, false), "replay of the loaded frame");
    d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (d) {
        size_t diff = 0;
        for (size_t i = 0; i < sizeof(a); i++) {
            diff += a[i] != d[i];
        }
        CHECK(diff == 0, "dump -> load -> replay: %zu bytes differ", diff);
        snprintf(path, sizeof(path), "%s/rd_pixel_frame.png", dir);
        rd_WritePng(path, d, w, h, w * 4, 1);
    }
    rd__FrameFree(&f);
    rd_DestroyTexture(t);
}

/* ---------------------------------------------------------------- present */

static void testPresent(void)
{
    static uint8_t out[640 * 480 * 4];
    uint32_t w = 0, h = 0;
    if (!rd__ReadPresent(out, sizeof(out), &w, &h)) {
        CHECK(0, "presenter output");
        return;
    }
    CHECK(w == 640 && h == 480, "presenter output size");
    uint32_t dw, dh;
    uint8_t *d = readTarget(RD_TARGET_DISPLAY, &dw, &dh);
    if (!d) {
        return;
    }
    /* 640x480 output, 4:3 box = whole output; 512x512 lines (doubled) scaled
     * by 1.25 / 0.9375.  Output pixel (320, 240) samples lines texel
     * (256.2, 256.5) -> DISPLAY row 128 (doubled rows 256, 257 both 128):
     * horizontal lerp between columns 255/256 (0.3/0.7)... check against a
     * loose bound: within the two neighbours' range */
    const uint8_t *p = out + ((size_t)240 * 640 + 320) * 4;
    const uint8_t *q0 = d + ((size_t)128 * dw + 255) * 4, *q1 = d + ((size_t)128 * dw + 256) * 4;
    for (int k = 0; k < 3; k++) {
        int lo = q0[k] < q1[k] ? q0[k] : q1[k], hi = q0[k] < q1[k] ? q1[k] : q0[k];
        CHECK(p[k] + 1 >= lo && p[k] <= hi + 1, "presented pixel channel %d %u outside [%d,%d]", k,
              p[k], lo, hi);
    }
}

/* ------------------------------------------------- nodual (package AN-E) */

#define ND_CELL 8
#define ND_AFAILS 5 /* ATE off, then AFAIL KEEP, FB_ONLY, ZB_ONLY, RGB_ONLY under ATE GREATER */
#define ND_SCENE_BG_Z 0x40000000u

static const uint32_t kNdMasks[3] = {0x00000000u, 0xFF000000u, 0x00FFFFFFu}; /* F, 7, 8 */

/* the TEST word of combination (date, afail, ztst) */
static uint64_t ndTest(int date, int af, int zge)
{
    uint64_t t = (1u << 16) | ((uint64_t)(zge ? RD_ZTST_GEQUAL : RD_ZTST_ALWAYS) << 17);
    if (af > 0) {
        t |= 1u | ((uint64_t)RD_ATST_GREATER << 1) | (0x60u << 4) | ((uint64_t)(af - 1) << 12);
    }
    if (date) {
        t |= (1u << 14) | (1u << 15);
    }
    return t;
}

/* two overlapping quads in one triangle command over the cell at (x, y)
 * (pixels) of a target gw wide, gh high: alpha 0..0xFF across them, Z
 * across the background's */
static void ndQuads(uint32_t gw, uint32_t gh, int x, int y, uint32_t seed)
{
    const int32_t ox = (2048 - (int32_t)gw / 2) * 16, oy = (2048 - (int32_t)gh / 2) * 16;
    const int32_t x0 = ox + x * 16, y0 = oy + y * 16, x1 = x0 + ND_CELL * 16,
                  y1 = y0 + ND_CELL * 16;
    const int32_t xm = x0 + 3 * 16 + 8, ym = y0 + 2 * 16 + 8;
    uint8_t c[8][4];
    for (int i = 0; i < 8; i++) {
        const uint32_t h = hash(seed * 8u + (uint32_t)i);
        c[i][0] = (uint8_t)h;
        c[i][1] = (uint8_t)(h >> 8);
        c[i][2] = (uint8_t)(h >> 16);
    }
    static const uint8_t kA[8] = {0x00, 0xFF, 0x40, 0x90, 0xC0, 0x10, 0x80, 0x7F};
    for (int i = 0; i < 8; i++) {
        c[i][3] = kA[i];
    }
    const uint32_t zLo = 0x20000000u, zHi = 0x60000000u;
    RdScreenVtx v[12] = {/* A: the whole cell, Z rising left to right */
                         vtx(x0, y0, zLo, c[0], 0, 0), vtx(x1, y0, zHi, c[1], 0, 0),
                         vtx(x0, y1, zLo, c[2], 0, 0), vtx(x1, y0, zHi, c[1], 0, 0),
                         vtx(x1, y1, zHi, c[3], 0, 0), vtx(x0, y1, zLo, c[2], 0, 0),
                         /* B: its lower right part, Z falling left to right */
                         vtx(xm, ym, zHi, c[4], 0, 0), vtx(x1, ym, zLo, c[5], 0, 0),
                         vtx(xm, y1, zHi, c[6], 0, 0), vtx(x1, ym, zLo, c[5], 0, 0),
                         vtx(x1, y1, ND_SCENE_BG_Z, c[7], 0, 0), vtx(xm, y1, zHi, c[6], 0, 0)};
    rd_ScreenPrims(RD_PRIM_TRIANGLES, v, 12, RD_SPACE_WORLD, 1, 0);
}

/* one frame: blends b0 .. b0 + nb - 1 over every other combination */
static void ndFrame(RdTex noise, int b0, int nb)
{
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_BeginFrame();
    rd_SelectList(5);
    for (int t = 0; t < 2; t++) {
        const RdTargetId id = t ? RD_TARGET_DISPLAY : RD_TARGET_SCENE;
        const uint32_t gw = 512, gh = t ? 256 : 512;
        const int32_t ox = (2048 - (int32_t)gw / 2) * 16, oy = (2048 - (int32_t)gh / 2) * 16;
        rd_ClearTarget(rd_Target(id), clr, !t, 0);
        rd_SetTarget(rd_Target(id), t ? (RdTarget){0} : rd_Target(id), gw, gh, 1);
        /* the background: noise texels 1:1, Z write at ND_SCENE_BG_Z */
        rd_ColorMask(0);
        rd_TestGs(RD_TEST_Z_ALWAYS);
        rd_ZWrite(1);
        rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
        rd_PABE(0);
        rd_FBA(0);
        rd_Gouraud(1);
        rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
        rd_Texture(noise, RD_TEXFN_DECAL, RD_TCC_RGBA);
        {
            static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
            RdScreenVtx v[2] = {vtx(ox, oy, ND_SCENE_BG_Z, white, 0.0f, 0.0f),
                                vtx(ox + (int32_t)gw * 16, oy + (int32_t)gh * 16, ND_SCENE_BG_Z,
                                    white, (float)gw * 16.0f, (float)gh * 16.0f)};
            rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
        }
        rd_TextureOff();
        uint32_t cell = 0;
        for (int b = b0; b < b0 + nb; b++) {
            for (int pabe = 0; pabe < 2; pabe++) {
                for (int fba = 0; fba < 2; fba++) {
                    for (int date = 0; date < 2; date++) {
                        for (int af = 0; af < ND_AFAILS; af++) {
                            for (int m = 0; m < 3; m++) {
                                for (int zz = 0; zz < (t ? 1 : 4); zz++, cell++) {
                                    const int x = (int)(cell % 64) * ND_CELL;
                                    const int y = (int)(cell / 64) * ND_CELL;
                                    rd_Blend((RdBlend)b, (uint8_t)(b == 3 ? 0x50 : 0xA0), 1);
                                    rd_PABE(pabe);
                                    rd_FBA(fba);
                                    rd_ColorMask(kNdMasks[m]);
                                    rd_TestGs(ndTest(date, af, zz & 1));
                                    rd_ZWrite((zz >> 1) & 1);
                                    ndQuads(gw, gh, x, y, cell * 31u + (uint32_t)b);
                                }
                            }
                        }
                    }
                }
            }
        }
        rd_ColorMask(0);
    }
    rd_EndFrame(0);
}

static void testNoDual(void)
{
    enum { FRAMES = 3, BLENDS_PER = RD_BLEND_COUNT / FRAMES };

    static uint8_t noiseTex[64 * 64 * 4];
    for (uint32_t i = 0; i < 64 * 64; i++) {
        const uint32_t h = hash(i + 0x5EEDu);
        memcpy(&noiseTex[i * 4], &h, 4);
    }
    RdTex noise = rd_CreateTexture(64, 64, noiseTex, RD_TEXA_80_80, "nodual noise");
    static uint8_t scene[2][FRAMES][512 * 512 * 4], disp[2][FRAMES][512 * 256 * 4];
    static float depth[2][FRAMES][512 * 512];
    const bool was = rd_NoDual();
    uint32_t noDualKeys = 0;
    for (int mode = 0; mode < 2; mode++) {
        CHECK(rd_SetNoDual(mode != 0), "rd_SetNoDual(%d) refused", mode);
        for (int f = 0; f < FRAMES; f++) {
            ndFrame(noise, f * BLENDS_PER, BLENDS_PER);
            uint32_t w = 0, h = 0, dw = 0, dh = 0;
            CHECK(rd__ReadTarget(rd_Target(RD_TARGET_SCENE), scene[mode][f], sizeof(scene[0][0]),
                                 &w, &h) &&
                      w == 512 && h == 512,
                  "nodual: SCENE readback");
            CHECK(rd__ReadTargetDepth(rd_Target(RD_TARGET_SCENE), depth[mode][f],
                                      sizeof(depth[0][0]), &dw, &dh) &&
                      dw == 512 && dh == 512,
                  "nodual: SCENE depth readback");
            CHECK(rd__ReadTarget(rd_Target(RD_TARGET_DISPLAY), disp[mode][f], sizeof(disp[0][0]),
                                 &w, &h) &&
                      w == 512 && h == 256,
                  "nodual: DISPLAY readback");
        }
        if (mode) {
            for (uint32_t i = 0; i < rd__PipelineCount(); i++) {
                noDualKeys += rd__PipelineKeyAt(i)->gs.nodual;
            }
        }
    }
    rd_SetNoDual(was);
    CHECK(noDualKeys > 0, "nodual: no *_nodual pipeline was created");
    int bad = 0, badZ = 0, badD = 0, shown = 0;
    for (int f = 0; f < FRAMES; f++) {
        for (uint32_t i = 0; i < 512 * 512; i++) {
            const uint32_t cell = (i / 512 / ND_CELL) * 64 + (i % 512) / ND_CELL;
            const uint32_t perBlend = 2 * 2 * 2 * ND_AFAILS * 3 * 4;
            const int mc = memcmp(&scene[0][f][i * 4], &scene[1][f][i * 4], 4) != 0;
            const int mz = memcmp(&depth[0][f][i], &depth[1][f][i], sizeof(float)) != 0;
            bad += mc;
            badZ += mz;
            if ((mc || mz) && shown < 12) {
                const uint32_t k = cell % perBlend;
                const uint8_t *a = &scene[0][f][i * 4], *b = &scene[1][f][i * 4];
                printf("FAIL nodual SCENE (%u,%u) blend %u pabe %u fba %u date %u afail %u mask "
                       "%u zge %u zw %u: %u %u %u %u (Z %g) one pass, %u %u %u %u (Z %g) two\n",
                       i % 512, i / 512, f * BLENDS_PER + cell / perBlend, k / 240, (k / 120) % 2,
                       (k / 60) % 2, (k / 12) % 5, (k / 4) % 3, k % 2, (k / 2) % 2, a[0], a[1],
                       a[2], a[3], (double)depth[0][f][i], b[0], b[1], b[2], b[3],
                       (double)depth[1][f][i]);
                shown++;
            }
        }
        for (uint32_t i = 0; i < 512 * 256; i++) {
            const int md = memcmp(&disp[0][f][i * 4], &disp[1][f][i * 4], 4) != 0;
            badD += md;
            if (md && shown < 12) {
                const uint32_t cell = (i / 512 / ND_CELL) * 64 + (i % 512) / ND_CELL;
                const uint32_t perBlend = 2 * 2 * 2 * ND_AFAILS * 3, k = cell % perBlend;
                printf("FAIL nodual DISPLAY (%u,%u) blend %u pabe %u fba %u date %u afail %u "
                       "mask %u\n",
                       i % 512, i / 512, f * BLENDS_PER + cell / perBlend, k / 60, (k / 30) % 2,
                       (k / 15) % 2, (k / 3) % 5, k % 3);
                shown++;
            }
        }
    }
    printf("  nodual: %d cells x 2 modes, %u two-pass pipelines; %d SCENE, %d depth, %d DISPLAY "
           "pixels differ\n",
           RD_BLEND_COUNT * 2 * 2 * 2 * ND_AFAILS * 3 * 4, noDualKeys, bad, badZ, badD);
    CHECK(bad == 0 && badZ == 0 && badD == 0,
          "nodual: the two-pass fallback differs from the dual-source draw (%d, %d, %d pixels)",
          bad, badZ, badD);
    rd_DestroyTexture(noise);
    /* its states are outside the reachable set: the pipes cell checks the
     * other cells' pipelines */
    rhi_WaitIdle();
    rd__PipelineCacheClear();
}

/* -------------------------------------------------------------- pipelines */

static void testPipelines(void)
{
    static RdPipeKeyInt keys[512], keysNd[512];
    /* package AN-E: both sets, with dual-source blending (keys) and with the
     * two-pass fallback (keysNd); the created keys are in the set of the
     * mode the test runs in */
    const bool was = rd_NoDual();
    rd_SetNoDual(false);
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    rd_SetNoDual(true);
    const uint32_t nNd = rd__EnumerateReachable(keysNd, 512);
    rd_SetNoDual(was);
    const RdPipeKeyInt *mine = was ? keysNd : keys;
    const uint32_t nMine = was ? nNd : n;
    const uint32_t c = rd__PipelineCount();
    printf("  pipelines: %u created, %u reachable (%u screen and post), %u with the two-pass "
           "fallback\n",
           c, n, ns, nNd);
    CHECK(nNd < RD_PIPELINE_REACHABLE_MAX,
          "reachable pipelines with the two-pass fallback %u >= %d", nNd,
          RD_PIPELINE_REACHABLE_MAX);
    CHECK(ns < 250, "reachable screen and post pipelines %u >= 250", ns);
    /* the keys a frame with the dark volume's FBMSK (colour mask 7 until the
     * next FRAME write) and the lightning's perspective STQ would otherwise
     * create at run time */
    {
        RdStateBlock s;
        rd__ResetStateBlock(&s);
        s.ds.test = rd_TestFromGs(RD_TEST_Z_ALWAYS);
        s.ds.zwrite = RD_ZWRITE_OFF;
        s.ds.abe = 1;
        s.ds.blend = RD_BLEND_LERP_AS;
        s.ds.colorMask = 0x7;
        for (int dz = 0; dz < 2; dz++) {
            RdDrawPass dp[2];
            const RhiFormat depth = dz ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
            for (int prim = 0; prim < 2; prim++) {
                const int np = rd__PlanScreenDraw(&s, prim ? RD_PRIM_LINES : RD_PRIM_TRIANGLES,
                                                  RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, depth, dp);
                for (int i = 0; i < np; i++) {
                    int found = 0;
                    for (uint32_t j = 0; j < ns; j++) {
                        found |= rd__PipeKeyEqual(&dp[i].key, &keys[j]);
                    }
                    CHECK(dp[i].key.gs.colorMask == 0x7 && found,
                          "UI draw under colour mask 7 (lines %d, depth %d) is not enumerated",
                          prim, dz);
                    dp[i].key.fs = RD_FS_FONT;
                    found = 0;
                    for (uint32_t j = 0; j < ns; j++) {
                        found |= rd__PipeKeyEqual(&dp[i].key, &keys[j]);
                    }
                    CHECK(prim || found,
                          "font draw under colour mask 7 (depth %d) is not enumerated", dz);
                }
            }
            s.ds.colorMask = 0xF;
            s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL);
            const int np = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                              RHI_FMT_RGBA8_UNORM, depth, dp);
            for (int i = 0; i < np; i++) {
                int found = 0;
                CHECK(rd__StqPass(&dp[i]), "a plain world sprite pass takes the STQ shaders");
                for (uint32_t j = 0; j < ns; j++) {
                    found |= rd__PipeKeyEqual(&dp[i].key, &keys[j]);
                }
                CHECK(found, "STQ world triangle pass (depth %d) is not enumerated", dz);
            }
            s.ds.colorMask = 0x7;
            s.ds.test = rd_TestFromGs(RD_TEST_Z_ALWAYS);
        }
    }
    CHECK(n < RD_PIPELINE_REACHABLE_MAX,
          "reachable pipelines %u >= %d (wave 3: with the VU programs)", n,
          RD_PIPELINE_REACHABLE_MAX);
    for (uint32_t i = 0; i < c; i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < nMine && j < 512; j++) {
            found |= rd__PipeKeyEqual(k, &mine[j]);
        }
        CHECK(found,
              "created pipeline %u (prog %u blend %u vs %u fmt %u/%u z %u/%u mask %x nodual %u) is "
              "not in the enumerated set",
              i, k->gs.program, k->gs.blend, k->vs, k->colorFmt, k->depthFmt, k->gs.ztst,
              k->gs.zwrite, k->gs.colorMask, k->gs.nodual);
    }
}

/* ------------------------------------------------------ the mirage mask
 *
 * Package QUEEN: the inputs of staticBlur.c's mirage (feedback mode 2) as
 * the F12 dumps of the Queen in the model viewer record them.  List 8 draws
 * the "shine" materials into AURA_WORK with SCENE's depth bound, TEST ATE
 * GREATER 0x46 AFAIL RGB_ONLY (or 0x60 FB_ONLY), Z GEQUAL, Z write, ALPHA
 * 0x44 with ABE; AURA_WORK's alpha is the mask the paste lerps the previous
 * frame over SCENE by.  On the GS:
 *   depth   a mask draw behind a nearer opaque scene draw fails the Z test
 *           and leaves the mask at the clear's 0; at equal depth it passes
 *   alpha   a texel that passes the alpha test writes A = As (FBA off) even
 *           under the lerp (the GS blends RGB only); one that fails writes
 *           RGB and keeps the destination alpha (RGB_ONLY) or writes RGB
 *           and A = As without Z (FB_ONLY)
 * Each read back at every GS pixel centre (the target's texels per GS pixel
 * at a scale), against the GS rule. */
#define AURA_Z_NEAR 0x00C00000u
#define AURA_Z_FAR 0x00400000u

static uint8_t *readScaled(RdTargetId id, uint32_t *w, uint32_t *h, float *sx, float *sy)
{
    const RdTargetRec *t = rd__TargetRec(rd_Target(id).id);
    if (!t) {
        CHECK(0, "target %d has no record", (int)id);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)t->tw * t->th * 4);
    if (!buf || !rd__ReadTarget(rd_Target(id), buf, (size_t)t->tw * t->th * 4, w, h)) {
        CHECK(0, "readback of target %d", (int)id);
        free(buf);
        return NULL;
    }
    *sx = (float)*w / (float)t->w;
    *sy = (float)*h / (float)t->h;
    return buf;
}

/* the texel at GS pixel (x, y)'s centre */
static const uint8_t *gsPixel(const uint8_t *img, uint32_t w, float sx, float sy, int x, int y)
{
    const uint32_t tx = (uint32_t)(((float)x + 0.5f) * sx), ty = (uint32_t)(((float)y + 0.5f) * sy);
    return &img[((size_t)ty * w + tx) * 4];
}

/* a world-space sprite (x0, y0)-(x1, y1) in GS pixels of a 512 x 512
 * target, UVs 0..16 texels (12.4) */
static void auraQuad(int x0, int y0, int x1, int y1, uint32_t z, const uint8_t c[4])
{
    const int32_t ox = (2048 - 256) * 16, oy = (2048 - 256) * 16;
    RdScreenVtx v[2] = {vtx(ox + x0 * 16, oy + y0 * 16, z, c, 0.0f, 0.0f),
                        vtx(ox + x1 * 16, oy + y1 * 16, z, c, 256.0f, 256.0f)};
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

/* auraInspireBefore's clear of AURA_WORK as staticBlur.c's host path
 * records it: Z test off, Z write off, PABE 1, ALPHA mode 2 without ABE, an
 * untextured RD_POST_AURA sprite over the screen in colour 0 */
static void auraClear(void)
{
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_PABE(1);
    rd_BlendFunc(RD_BLEND_LERP_FIX, 0);
    rd_TextureOff();
    rd_ABE(0);
    rd_Gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - 256 * 16);
    p.rect[1] = (float)(0x8000 - 256 * 16);
    p.rect[2] = (float)(0x8000 + 256 * 16);
    p.rect[3] = (float)(0x8000 + 256 * 16);
    p.scalar[2] = 1.0f;
    p.exactInt = 1;
    rd_Post(RD_POST_AURA, &p);
}

/* (a): the mask behind the scene */
static void testAuraDepth(const char *mode, RdTex white)
{
    static const uint8_t grey[4] = {0x60, 0x60, 0x60, 0x80}, red[4] = {200, 30, 30, 0x80};
    static const uint8_t shine[4] = {0x80, 0x80, 0x80, 0x7F}; /* the queen's vertex colour */
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    /* list 1: the face, opaque, nearer, Z write */
    rd_SelectList(1);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_TestGs(RD_TEST_Z_GEQUAL);
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
    rd_TextureOff();
    auraQuad(100, 100, 200, 200, AURA_Z_NEAR, red);
    /* list 8: the clear, then the shine material: farther (rows 50..150),
     * and at the face's depth (rows 160..190) */
    rd_SelectList(8);
    auraClear();
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_TestGs(0x5346D); /* ATE GREATER 0x46, AFAIL RGB_ONLY, Z GEQUAL */
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_PABE(0);
    rd_FBA(0);
    rd_Gouraud(1);
    rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_Texture(white, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    auraQuad(50, 50, 250, 150, AURA_Z_FAR, shine);
    auraQuad(50, 160, 250, 190, AURA_Z_NEAR, shine);
    rd_EndFrame(0);

    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_AURA_WORK, &w, &h, &sx, &sy);
    if (!img) {
        return;
    }
    int behind = 0, behindBad = 0, beside = 0, besideBad = 0, equal = 0, equalBad = 0, out = 0,
        outBad = 0;
    for (int y = 40; y < 200; y++) {
        for (int x = 40; x < 260; x++) {
            const uint8_t a = gsPixel(img, w, sx, sy, x, y)[3];
            const int inFar = x >= 50 && x < 250 && y >= 50 && y < 150;
            const int inEq = x >= 50 && x < 250 && y >= 160 && y < 190;
            const int inFace = x >= 100 && x < 200 && y >= 100 && y < 200;
            if (inFar && inFace) {
                behind++;
                behindBad += a != 0;
            } else if (inFar) {
                beside++;
                besideBad += a != 0x7F;
            } else if (inEq) {
                equal++;
                equalBad += a != 0x7F;
            } else {
                out++;
                outBad += a != 0;
            }
        }
    }
    printf("  aura depth (%s): mask behind the face %d of %d set, beside it %d of %d unset, at "
           "its depth %d of %d unset, outside %d of %d set\n",
           mode, behindBad, behind, besideBad, beside, equalBad, equal, outBad, out);
    CHECK(behindBad == 0, "aura depth (%s): %d of %d mask pixels behind the nearer scene draw set",
          mode, behindBad, behind);
    CHECK(besideBad == 0 && equalBad == 0,
          "aura depth (%s): mask pixels that pass the Z test unset: %d beside, %d at equal depth",
          mode, besideBad, equalBad);
    CHECK(outBad == 0, "aura depth (%s): %d mask pixels set outside the draws", mode, outBad);
    free(img);
}

/* (a), at the Queen's depths: the model viewer's dumps put her face (list
 * 0/1) at GS Z 16.9M..17.2M and the veil, mist and hair layers of list 8
 * within tens to hundreds of Z units of it, in front and behind (the
 * vertices of mesh ...0910 lie 17..34 in front of the face's, ...0300 lie
 * -360..+4809 from the veil's; rd_replay_tool --list --no-device prints
 * every draw's Z range).  The GS compares the integers: a mask quad 17,
 * 100 or 300 behind the face fails, one 17 in front or at its Z passes. */
static void testAuraDepthNear(const char *mode, RdTex white)
{
    static const uint8_t grey[4] = {0x60, 0x60, 0x60, 0x80}, skin[4] = {220, 210, 200, 0x80};
    static const uint8_t shine[4] = {0x80, 0x80, 0x80, 0x7F};
    static const int32_t kDz[5] = {-300, -100, -17, 0, 17};
    const uint32_t face = 17000000u;
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), grey, 1, 0);
    rd_SelectList(1);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_TestGs(RD_TEST_Z_GEQUAL);
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
    rd_TextureOff();
    auraQuad(40, 40, 260, 240, face, skin);
    rd_SelectList(8);
    auraClear();
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_TestGs(0x5346D);
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_PABE(0);
    rd_FBA(0);
    rd_Gouraud(1);
    rd_Sampler(RD_FILTER_LINEAR, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_Texture(white, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    for (int k = 0; k < 5; k++) {
        auraQuad(50 + k * 40, 50, 80 + k * 40, 230, (uint32_t)((int32_t)face + kDz[k]), shine);
    }
    rd_EndFrame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_AURA_WORK, &w, &h, &sx, &sy);
    if (!img) {
        return;
    }
    for (int k = 0; k < 5; k++) {
        const int want = kDz[k] >= 0 ? 0x7F : 0;
        int bad = 0;
        for (int y = 50; y < 230; y++) {
            for (int x = 50 + k * 40; x < 80 + k * 40; x++) {
                bad += gsPixel(img, w, sx, sy, x, y)[3] != want;
            }
        }
        printf("  aura depth near (%s): a mask quad %+d from the face's Z %u: %d of %d pixels "
               "off the GS (%s)\n",
               mode, kDz[k], face, bad, 30 * 180, want ? "passes" : "fails");
        CHECK(bad == 0, "aura depth near (%s): a mask quad %+d from the face's Z: %d pixels %s",
              mode, kDz[k], bad,
              want ? "fail the GEQUAL it passes on the GS" : "pass the GEQUAL it fails on the GS");
    }
    free(img);
}

/* (b): the mask's alpha per texel under RGB_ONLY 0x46 and FB_ONLY 0x60 */
static const uint8_t kAuraAlphas[16] = {0x00, 0x10, 0x30, 0x45, 0x46, 0x47, 0x50, 0x5F,
                                        0x60, 0x61, 0x70, 0x7F, 0x80, 0x90, 0xC0, 0xFF};

static int gsLerp(int cs, int cd, int as)
{
    int v = (((cs - cd) * as) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void testAuraAlpha(const char *mode, int fbOnly)
{
    static uint8_t tex[16 * 16 * 4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            uint8_t *p = &tex[(y * 16 + x) * 4];
            const uint32_t hsh = hash((uint32_t)(y * 16 + x) * 77u + 5u);
            p[0] = (uint8_t)hsh;
            p[1] = (uint8_t)(hsh >> 8);
            p[2] = (uint8_t)(hsh >> 16);
            p[3] = kAuraAlphas[(x + y) & 15];
        }
    }
    RdTex t = rd_CreateTexture(16, 16, tex, RD_TEXA_80_80, "aura alpha");
    static const uint8_t bg[4] = {10, 200, 30, 0x55}, vc[4] = {0x80, 0x80, 0x80, 0x80};
    rd_BeginFrame();
    rd_SelectList(8);
    rd_ClearTarget(rd_Target(RD_TARGET_AURA_WORK), bg, 0, 0);
    rd_SetTarget(rd_Target(RD_TARGET_AURA_WORK), rd_Target(RD_TARGET_SCENE), 512, 512, 0);
    rd_TestGs(fbOnly ? 0x3160D : 0x3346D); /* Z ALWAYS */
    rd_ZWrite(1);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_PABE(0);
    rd_FBA(0);
    rd_Gouraud(1);
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    auraQuad(64, 64, 80, 80, 0, vc); /* 1:1 */
    rd_EndFrame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_AURA_WORK, &w, &h, &sx, &sy);
    if (!img) {
        rd_DestroyTexture(t);
        return;
    }
    const int aref = fbOnly ? 0x60 : 0x46;
    int alphaBad = 0, rgbMax = 0, rgbOver = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const uint8_t *s = &tex[(y * 16 + x) * 4];
            const uint8_t *g = gsPixel(img, w, sx, sy, 64 + x, 64 + y);
            const int as = (s[3] * 0x80) >> 7, pass = as > aref;
            const int wantA = pass || fbOnly ? as : bg[3];
            if (g[3] != wantA) {
                const int want[4] = {gsLerp(s[0], bg[0], as), gsLerp(s[1], bg[1], as),
                                     gsLerp(s[2], bg[2], as), wantA};
                pixFail(fbOnly ? "aura alpha FB_ONLY" : "aura alpha RGB_ONLY", x, y, g, want);
                alphaBad++;
            }
            for (int k = 0; k < 3; k++) {
                const int d = abs((int)g[k] - gsLerp(s[k], bg[k], as));
                if (as <= 0x80) {
                    rgbMax = d > rgbMax ? d : rgbMax;
                } else {
                    rgbOver = d > rgbOver ? d : rgbOver;
                }
            }
        }
    }
    /* RGB is reported, not asserted, where As passes 0x80 (the GS
     * overshoots there; the hardware blend's factor stops at 1.0): the mask
     * is the alpha, and the queen's texels are at most 0x80 */
    printf("  aura alpha (%s, %s 0x%02x): %d of 256 texels with the wrong alpha, RGB within %d "
           "(As <= 0x80), %d where As > 0x80\n",
           mode, fbOnly ? "FB_ONLY" : "RGB_ONLY", aref, alphaBad, rgbMax, rgbOver);
    CHECK(alphaBad == 0, "aura alpha (%s, %s): %d texels' alpha is not the GS's", mode,
          fbOnly ? "FB_ONLY" : "RGB_ONLY", alphaBad);
    /* the hardware blend's lerp (not blend_int) is within 2 of the GS's */
    CHECK(rgbMax <= 2, "aura alpha (%s): RGB off the GS lerp by %d", mode, rgbMax);
    free(img);
    rd_DestroyTexture(t);
}

static void testAuraMask(const char *mode)
{
    static uint8_t whiteTx[16 * 16 * 4];
    memset(whiteTx, 0xFF, sizeof(whiteTx));
    for (int i = 0; i < 16 * 16; i++) {
        whiteTx[i * 4 + 3] = 0x80;
    }
    RdTex white = rd_CreateTexture(16, 16, whiteTx, RD_TEXA_80_80, "aura white");
    testAuraDepth(mode, white);
    testAuraDepthNear(mode, white);
    testAuraAlpha(mode, 0);
    testAuraAlpha(mode, 1);
    rd_DestroyTexture(white);
}

/* the mask tests again in a renderer of other display options: the
 * Enhanced preset at 4x (the resolution window) and with the full-height
 * scene */
static void testAuraMaskAt(const char *mode, float scale, int fullHeight)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.sceneScale = scale;
    s.fullHeightScene = (uint8_t)fullHeight;
    if (!rd_Init(512, 512, &s, NULL)) {
        CHECK(0, "rd_Init (%s)", mode);
        return;
    }
    testAuraMask(mode);
    const uint32_t verr = rhi_vk_ValidationErrorCount();
    CHECK(verr == 0, "%s: %u validation errors", mode, verr);
    rd_Shutdown();
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    if (!rd_Init(512, 512, &s, NULL)) {
        printf("SKIP rd_pixel_test: no usable Vulkan device\n");
        return 77;
    }
    printf("rd_pixel_test: adapter %s%s\n", rhi_AdapterName(),
           rd_NoDual() ? " (two-pass blend fallback)" : "");
    testNoDual(); /* package AN-E: first, it clears the pipeline cache */
    testOrder();
    testRailing(0);
    testRailing(1); /* as the game's materials draw it */
    testDateFlat();
    testScreenRuns();
    testSprites();
    testTexa();
    testFont(dir);
    testStq();
    testAa1();
    testReduction(dir);
    testPresent();
    testKeep();
    testExact();
    testDump(dir);
    testPipelines();
    testAuraMask("Original 1x"); /* package QUEEN */
    const uint32_t verr = rhi_vk_ValidationErrorCount();
    CHECK(verr == 0, "%u validation errors", verr);
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    testAuraMaskAt("Enhanced 4x", 4.0f, 0);
    testAuraMaskAt("Enhanced 4x, full height", 4.0f, 1);
    if (failures) {
        printf("rd_pixel_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pixel_test: ok\n");
    return 0;
}
