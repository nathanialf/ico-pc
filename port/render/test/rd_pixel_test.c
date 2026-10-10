/* rd_pixel_test.c: rd through the Vulkan RHI (lavapipe in the container).
 *
 *   order    the same pixel drawn from list 5 (recorded first) and list 1:
 *            list 5 wins, it replays later
 *   half     the frame head's half pixel on SCENE: a sprite of list 5,
 *            whose SCENE target the game set again with the screen offset
 *            alone, covers the rows of the same sprite in list 0; lists 8
 *            (no screen offset) and 11 (the UI) keep their own rows
 *   railing  the stair railings' state, TEST 0x5160D (ATE
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
 *            7F/81+AEM on an RGB24 source; rd_uv_offset
 *   texa     an RGBA16 texture with A = 0, A = 1 and black
 *            texels under 7F/81+AEM, magnified 4x: bilinear is the GS
 *            order (TEXA per texel, then the 4-bit bilinear) with 0 LSB
 *            (sprite_texa_ps), nearest is the expanded texel with 0 LSB
 *            (sprite_ps); the planner's choice of entry
 *   font     a 4x4 R8 coverage atlas (GS alpha units) drawn
 *            through rd_screen_prims under port/ui/font.c's state (font_ps):
 *            1:1 at texel centres the stored alpha is (c * va) >> 7, and
 *            1:1 and magnified 8x with bilinear filtering every byte equals
 *            the same atlas as RGBA8 (white, alpha c) through sprite_ps;
 *            the frame dumped and loaded has the texture as R8 with its
 *            texels and replays to the same bytes
 *   sheet    a coverage strip (rd_create_texture_sheet)
 *            through font_sheet_ps against sprite_ps drawing the CPU
 *            reference's texels (sheet_ref.c sheetref_texel) as RGBA8,
 *            magnified, the grey (TCC RGB) and the alpha (TCC RGBA) within 1,
 *            at scene scale 1 (Original) and 2, 3 (Enhanced); 1:1 at the
 *            texel centres every pixel is sheetref_texel's and the rim
 *            texels hold the style's rim level (English black, then French
 *            grey and no rim through rd_set_texture_sheet_style); the strip
 *            moved by whole pixels gives the same pixels (the grain is the
 *            texel's); on the presentation overlay magnified 4x within 2;
 *            the frame dumped and loaded keeps the format and the style and
 *            replays to the same bytes; a strip rasterised at 2 and 4
 *            texels a sheet texel (style.scale) drawn 1:1 is
 *            sheetref_texel's at that scale within 1, its rim reaches 6.5 S
 *            pixels across and 4.5 S down, its fill's grain is constant
 *            over each S x S cell, magnified it is within 1 of the
 *            reference, outside the letters it is the 1x strip of its sheet
 *            texels magnified S times within 1 (the rim is the sheets'
 *            look magnified), and its dump keeps the scale
 *   stq      a textured triangle strip with Q 1 to 0.25
 *            maps the texture perspective-correctly (U = f q1 / (q0 + f (q1 -
 *            q0)) at the fraction f across it), a strip with Q = 1 stays affine
 *   reduce   rd_post(RD_POST_REDUCTION) on a synthetic 512x512 scene
 *            against a CPU reference of gsb_Reduction (the GS bilinear at
 *            the GS sample points, tint, border crop) exactly (the
 *            reduction is drawn through the GS sprite model)
 *   keep     a keep frame (lists 11/12 only) draws DISPLAY back at 112/128
 *   exact    100 frames of RD_POST_COMPOSITE_FIX with exactInt into
 *            FEED128 equal the GS integer formula exactly every frame
 *   dump     a frame replayed, dumped, loaded and replayed again gives the
 *            same DISPLAY bytes; the dump is left for rd_replay_tool
 *   runs     consecutive screen-prim commands under one state
 *            are one draw: six overlapping, differently coloured blended
 *            sprites drawn merged give the bytes of six sequential draws;
 *            two DATE sprites (the second retakes the snapshot), an AFAIL
 *            split and a scissor change each end a run (draws counted)
 *   aa1      PRIM.AA1 against a CPU coverage reference, in
 *            GS pixels at the integer sample points, As on the 0x80 scale
 *            (grey 0x80 LERPed over black writes As itself): a line with
 *            ABE 0 (the storm's) has coverage 1 - d on the two pixels per
 *            column nearest it, d the vertical distance; a triangle with
 *            ABE 1 and alpha 0x80 has its interior at 0x80 and a one-pixel
 *            fringe outside its top edge at 1 - d; As = the 16-bit coverage
 *            >> 9; within 1 LSB of As
 *   nodual   every RdBlend 0..11 x PABE x FBA x DATE off/on x
 *            the alpha test (off, or GREATER 0x60 with AFAIL KEEP, FB_ONLY,
 *            ZB_ONLY, RGB_ONLY) x colour mask F, 7, 8 x Z ALWAYS, GEQUAL x Z
 *            write, two overlapping gouraud quads in one command (alpha
 *            0..0xFF, Z across the background's) over a noise background
 *            (RGB, alpha both sides of the MSB) in SCENE with its depth, and
 *            the Z-less half in DISPLAY: replayed with the two-pass blend
 *            fallback off and on (rd_set_no_dual), SCENE, its depth and
 *            DISPLAY equal byte for byte; the pipeline cache is cleared
 *            after it (its states are outside the reachable set)
 *   pipes    every pipeline created is in the enumerated reachable set,
 *            whose screen and post part has fewer than 250 keys (with the
 *            sprite_texa_ps twins) and holds the colour mask 7 keys of the
 *            2D draws (font_ps and font_sheet_ps too) under the dark
 *            volume's FBMSK and the STQ keys of the lightning (all of it,
 *            with the VU programs, fewer than RD_PIPELINE_REACHABLE_MAX)
 *            (the set with the two-pass fallback too, also under
 *            RD_PIPELINE_REACHABLE_MAX; a created key is in the set of its
 *            mode)
 *
 *   aura     the mirage's mask as list 8 draws it into
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
 *   vu paths one sloped prelit triangle off the 12.4 grid
 *            through each position path of vu_triangle_out (code 34, 32,
 *            36 uncut, 36 cut by the far plane), drawn with Z write and
 *            again over itself by each path with Z GEQUAL: no interior
 *            pixel fails between any two of 34, 32, 36 and 36 cut (issue
 *            25: the cut path's vertices with a GS position draw at it;
 *            the cut triangle with its winding reversed is reported only);
 *            32 over 34 along a diagonal pan in 1/64 GS pixel steps never
 *            fails; panned across, every path's edges move forward by at
 *            most one pixel a frame and 34's stay on 32's; in Original 1x,
 *            Original 4x, Enhanced 2.25x and Enhanced 4x (where 34 drew on
 *            the 12.4 grid and the others off it: 32 over 34 failed
 *            everywhere in 62 of 65 pan frames)
 *   vu seams (issue 26) in the same four renderers, over a grey clear at
 *            Z 0 with GEQUAL and Z write, the clear's pixels within one
 *            output pixel of a shared edge (parameter 0.1..0.9): none for
 *            S1 a code-32 triangle beside a GIF triangle (rd_screen_prims)
 *            at the VU's ftoi4 corners, the VU on either side; S2 two
 *            meshes under matrices 0.01 GS pixel apart whose shared
 *            vertices have equal ftoi4; S3 a code-36 strip whose first
 *            triangle is cut (v0 with GS Z < 0 and the -z flag, then with
 *            w < 0) beside its kicked second, along v1-v2, and the cut
 *            triangle still covers v0's side; S4 (reported against
 *            Original 1x) 8 x 8 quads of one mesh under a matrix each,
 *            turned 0.002 rad, whose shared corners differ by float noise.
 *            (before issue 26 Enhanced above 1x drew the VU off the 12.4
 *            grid, which opens S1 with the VU on P3's side and S2; before
 *            issue 25 the cut path's unsnapped corners opened S3 in every
 *            mode)
 *   vu overlap (issue 25, the near railing's shimmer) in the same four
 *            renderers: a lattice panel's front strip and then its back
 *            strips in one code-32 mesh (the same corners split along other
 *            diagonals, a corner's UV 2 texels off), under the railing's
 *            state (ATE GREATER 0x60, FB_ONLY, Z GEQUAL with Z write, ABE),
 *            seen obliquely at Z 30M..41M, the camera moving a little in 12
 *            steps: the back triangles are marked (ICO_VU_INDEX_LATER), and
 *            every step the one draw equals the front strip drawn and then
 *            the back strips with Z ALWAYS (the later face over the earlier
 *            at every pixel, an exact GS's picture); the faces drawn apart
 *            with GEQUAL differ from it in some steps (the rounding's
 *            picture: the earlier face wins in steps 1, 5, 9 and 10)
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
#include "sheet_ref.h"
#include "shader_consts.h"
#include "rd_mesh.h"
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
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
}

static void opaque2D(void)
{
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
}

static uint8_t *readTarget(RdTargetId id, uint32_t *w, uint32_t *h)
{
    static uint8_t buf[512 * 512 * 4];
    if (!rd__read_target(rd_target(id), buf, sizeof(buf), w, h)) {
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
    rd_begin_frame();
    rd_select_list(5);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_texture_off();
    sprite(256, 128, 0, 0, 16 * 16, 16 * 16, red, 0, 0, 0, 0);
    rd_select_list(1);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_texture_off();
    sprite(256, 128, 0, 0, 16 * 16, 16 * 16, blue, 0, 0, 0, 0);
    rd_end_frame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_WORK0, &w, &h);
    if (img) {
        CHECK(img[0] == 200 && img[2] == 0, "list 5 replays after list 1 (got %u,%u,%u)", img[0],
              img[1], img[2]);
    }
}

/* ------------------------------------------------------------ half */

/* The field's half pixel (issue 29, rd_replay.c sceneKeepsHalf): list 0's
 * SCENE target with the frame head's half offset (RD_TARGET_OFFSET |
 * RD_TARGET_HALF_Y), then SCENE set again with the screen offset alone in
 * list 5 (the shadow pass's gif_SetDrawEnviroment), in list 8 without the
 * screen offset (a full-target pass) and in list 11 (the UI).  The same
 * sprite, y from 20.25 to 30.25 pixels, covers rows 20..29 under the half
 * offset (the GS rule: row y when y0 <= y + 0.5 < y1) and 21..30 without:
 * list 5 lands on list 0's rows, lists 8 and 11 keep their own. */
static void halfRows(const uint8_t *img, uint32_t w, int x, int *first, int *last)
{
    *first = *last = -1;
    for (int y = 0; y < 64; y++) {
        if (img[((size_t)y * w + (size_t)x) * 4 + 3] != 0) {
            *first = *first < 0 ? y : *first;
            *last = y;
        }
    }
}

static void testHalfOffset(void)
{
    static const uint8_t clr[4] = {0, 0, 0, 0}, c[4] = {200, 100, 50, 0x80};
    static const int kList[4] = {0, 5, 8, 11};
    static const int kUse[4] = {RD_TARGET_OFFSET | RD_TARGET_HALF_Y, RD_TARGET_OFFSET, 0,
                                RD_TARGET_OFFSET};
    static const int kFirst[4] = {20, 20, 21, 21};
    const int32_t ox = (2048 - 256) * 16, oy = (2048 - 224) * 16;
    rd_begin_frame();
    for (int k = 0; k < 4; k++) {
        rd_select_list(kList[k]);
        if (k == 0) {
            rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 0, 0);
        }
        rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, 512, 448, kUse[k]);
        opaque2D();
        rd_texture_off();
        RdScreenVtx v[2] = {vtx(ox + (10 + 20 * k) * 16, oy + 20 * 16 + 4, 0, c, 0.0f, 0.0f),
                            vtx(ox + (20 + 20 * k) * 16, oy + 30 * 16 + 4, 0, c, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_end_frame(0);
    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_SCENE, &w, &h);
    if (!img) {
        return;
    }
    for (int k = 0; k < 4; k++) {
        int first, last;
        halfRows(img, w, 15 + 20 * k, &first, &last);
        CHECK(first == kFirst[k] && last == kFirst[k] + 9,
              "half offset: the sprite of list %d covers rows %d..%d (want %d..%d)", kList[k],
              first, last, kFirst[k], kFirst[k] + 9);
    }
}

/* ---------------------------------------------------------------- railing */

/* Issue 9's lattice.  On the GS a hole texel (alpha 0) fails
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
    RdTex t = rd_create_texture(16, 16, lattice, RD_TEXA_80_80, "lattice");
    static const uint8_t clr[4] = {0, 0, 0, 0}, grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t wall[4] = {30, 160, 60, 0x80};
    const int32_t ox = (2048 - 256) * 16, oy = (2048 - 256) * 16;
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    /* the railing: 16 x 16 texels (UVs in 1/16 texel) magnified 4x at
     * (64, 64), in world space like the game's 3D sprites */
    rd_select_list(1);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(0x5160D);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_pabe(0);
    rd_fba(fba);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    {
        RdScreenVtx v[2] = {vtx(ox + 64 * 16, oy + 64 * 16, RAIL_Z, grey, 0.0f, 0.0f),
                            vtx(ox + 128 * 16, oy + 128 * 16, RAIL_Z, grey, 256.0f, 256.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    /* the wall behind it, drawn later: opaque, Z GEQUAL, Z write */
    rd_select_list(2);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_fba(0);
    rd_texture_off();
    {
        RdScreenVtx v[2] = {vtx(ox + 48 * 16, oy + 48 * 16, WALL_Z, wall, 0.0f, 0.0f),
                            vtx(ox + 144 * 16, oy + 144 * 16, WALL_Z, wall, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_end_frame(0);

    uint32_t w, h;
    uint8_t *img = readTarget(RD_TARGET_SCENE, &w, &h);
    static float depth[512 * 512];
    uint32_t dw = 0, dh = 0;
    const bool zOk =
        rd__read_target_depth(rd_target(RD_TARGET_SCENE), depth, sizeof(depth), &dw, &dh);
    CHECK(zOk && dw == w && dh == h, "railing: depth readback");
    if (!img || !zOk) {
        rd_destroy_texture(t);
        return;
    }
    const float zScale = rd__target_z_scale(rd_target(RD_TARGET_SCENE).id);
    const float railD = rd__gs_depth(RAIL_Z, zScale), wallD = rd__gs_depth(WALL_Z, zScale);
    /* the depth grows with GS Z (gs_z_to_depth) */
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
    rd_destroy_texture(t);
}

/* ------------------------------------------------------------ DATE, flat */

/* TEST.DATE against the R8 snapshot, and PRIM.IIP 0.  WORK0 gets
 * alpha 0 on its left half and 0x80 on its right; a DATM=1 sprite over all
 * of it lands on the right half only, a DATM=0 one on the left only.  A flat
 * triangle takes its last vertex's colour. */
static void testDateFlat(void)
{
    static const uint8_t a0[4] = {10, 10, 10, 0}, a1[4] = {20, 20, 20, 0x80};
    static const uint8_t red[4] = {200, 0, 0, 0x80}, green[4] = {0, 200, 0, 0x80};
    static const uint8_t blue[4] = {0, 0, 200, 0x80};
    rd_begin_frame();
    rd_select_list(5);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_texture_off();
    sprite(256, 128, 0, 0, 128 * 16, 64 * 16, a0, 0, 0, 0, 0);
    sprite(256, 128, 128 * 16, 0, 256 * 16, 64 * 16, a1, 0, 0, 0, 0);
    rd_test_gs(RD_TEST_RGBONLY_DATE1); /* DATE DATM 1, RGB-only AFAIL with ATE off */
    sprite(256, 128, 0, 0, 256 * 16, 32 * 16, red, 0, 0, 0, 0);
    rd_test_gs(RD_TEST_DATE0); /* DATE DATM 0 */
    sprite(256, 128, 0, 32 * 16, 256 * 16, 64 * 16, green, 0, 0, 0, 0);
    /* a flat triangle below: last vertex blue */
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_gouraud(0);
    {
        const int32_t ox = (2048 - 128) * 16, oy = (2048 - 64) * 16;
        RdScreenVtx t[3] = {vtx(ox + 0, oy + 64 * 16, 0, red, 0, 0),
                            vtx(ox + 256 * 16, oy + 64 * 16, 0, green, 0, 0),
                            vtx(ox + 0, oy + 128 * 16, 0, blue, 0, 0)};
        rd_screen_prims(RD_PRIM_TRIANGLES, t, 3, RD_SPACE_UI, 1, 0);
    }
    rd_gouraud(1);
    rd_end_frame(0);
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

/* Consecutive screen-prim commands under the same state are one
 * draw (rd_replay.c doScreen).  The frame is replayed merged and with
 * merging off (rd__set_screen_merge); WORK0 must be the same bytes, and the
 * draws are counted (RdPerfRecord screenCmds, screenDraws). */
static void runReplay(const RdFrame *f, bool merge, uint8_t *dst, uint32_t *cmds, uint32_t *draws)
{
    rd__set_screen_merge(merge);
    rd__replay_frame(f, 0, false);
    rd__set_screen_merge(true);
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
    const RdFrame *f = rd__last_frame();
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
    rd_begin_frame();
    rd_select_list(5);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_texture_off();
    for (uint32_t i = 0; i < 6; i++) {
        runSprite(10 + (int32_t)i * 17, 8 + (int32_t)i * 9, 90 + (int32_t)i * 19,
                  70 + (int32_t)i * 7, i, (uint8_t)(0x30 + i * 0x18));
    }
    rd_end_frame(0);
    checkRun("overlapping run", 6, 1);

    /* the boundaries: a run of two; two DATE sprites under the same state
     * (the first writes alpha, so the second retakes the snapshot); two
     * AFAIL FB_ONLY sprites (two passes each, never merged); two sprites,
     * a scissor change, two more */
    rd_begin_frame();
    rd_select_list(5);
    rd_clear_target(rd_target(RD_TARGET_WORK0), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK0), (RdTarget){0}, 256, 128, 0);
    opaque2D();
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_texture_off();
    runSprite(0, 0, 120, 60, 0, 0x90);
    runSprite(60, 20, 200, 100, 1, 0x20);
    rd_test_gs(RD_TEST_DATE0);
    runSprite(20, 10, 160, 90, 2, 0xA0);
    runSprite(40, 30, 240, 120, 3, 0x50);
    rd_test_gs(RD_TEST_AT_GT64_FBONLY);
    runSprite(5, 40, 150, 110, 4, 0x60);
    runSprite(80, 0, 250, 80, 5, 0x70);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    runSprite(0, 50, 100, 128, 0, 0x40);
    runSprite(30, 70, 130, 128, 1, 0x88);
    rd_scissor(16, 8, 200, 100);
    runSprite(0, 0, 256, 128, 2, 0x30);
    runSprite(50, 30, 220, 110, 3, 0x58);
    rd_end_frame(0);
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
    rd_begin_frame();
    rd_select_list(11);
    rd_clear_target(rd_target(RD_TARGET_WORK1), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D(); /* LERP As with ABE 0; WORLD prims, the space the game's AA1 draws use */
    rd_texture_off();
    rd_aa1(1);
    {
        RdScreenVtx v[2] = {vtx(ox + la[0], oy + la[1], 0, grey, 0, 0),
                            vtx(ox + lb[0], oy + lb[1], 0, grey, 0, 0)};
        rd_screen_prims(RD_PRIM_LINES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_abe(1);
    {
        RdScreenVtx v[3] = {vtx(ox + t0[0], oy + t0[1], 0, grey, 0, 0),
                            vtx(ox + t1[0], oy + t1[1], 0, grey, 0, 0),
                            vtx(ox + t2[0], oy + t2[1], 0, grey, 0, 0)};
        rd_screen_prims(RD_PRIM_TRIANGLES, v, 3, RD_SPACE_WORLD, 1, 0);
    }
    rd_aa1(0);
    rd_end_frame(0);
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
    RdTex t32 = rd_create_texture(16, 16, tex, RD_TEXA_80_80, "t32");
    RdTex t24 = rd_create_texture_src(16, 16, tex24, RD_TEXSRC_RGB24, "t24");

    rd_begin_frame();
    rd_select_list(11);
    rd_clear_target(rd_target(RD_TARGET_WORK1), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D();
    rd_texture_off();
    for (int i = 0; i < nc; i++) {
        sprite(256, 256, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, cases[i].c, 0, 0, 0,
               0);
    }
    /* textured 1:1 with the +8 UV nudge, nearest */
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t32, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(256, 256, 120 * 16, 100 * 16, 136 * 16, 116 * 16, grey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    /* RGB24 source under TEXA 7F/81+AEM */
    rd_tex_a(RD_TEXA_7F_81_AEM);
    rd_texture(t24, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(256, 256, 150 * 16, 100 * 16, 166 * 16, 116 * 16, grey, 8, 8, 16 * 16 + 8, 16 * 16 + 8);
    /* UV offset: a quarter of the texture to the right, clamped */
    rd_tex_a(RD_TEXA_80_80);
    rd_texture(t32, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_uv_offset(0.25f, 0.0f);
    sprite(256, 256, 180 * 16, 100 * 16, 192 * 16, 101 * 16, grey, 8, 8, 12 * 16 + 8, 1 * 16 + 8);
    rd_uv_offset(0.0f, 0.0f);
    rd_end_frame(0);

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
            pixFail("rd_uv_offset 0.25", 180 + x, 100, p, want);
        }
    }
    rd_destroy_texture(t32);
    rd_destroy_texture(t24);
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

/* An RGBA16 texture whose texels mix A = 0 and A = 1 and
 * black ones (AEM), drawn magnified 4x (UV 8 + 4 x in 12.4, the 4-bit
 * fractions 0, 4, 8 and 12) under TEXA 7F/81+AEM, MODULATE by 0x80 with
 * TCC RGBA: every pixel is the GS order (TEXA per texel, then the 4-bit
 * bilinear, floor of the sum >> 8), 0 LSB, through sprite_texa_ps; the
 * same sprite with nearest filtering (UV 10 + 4 x, off the texel edges) is
 * the expanded texel under it, 0 LSB,
 * through sprite_ps.  The planner gives sprite_texa_ps only to a
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
    RdTex t16 = rd_create_texture_src(N, N, tex, RD_TEXSRC_RGBA16, "texa rgba16");
    RdTex t32 = rd_create_texture(N, N, tex, RD_TEXA_80_80, "texa rgba32");

    /* the planner */
    {
        RdStateBlock st;
        rd__reset_state_block(&st);
        st.ds.texEnabled = 1;
        st.tex = t16.id;
        st.ds.texa = RD_TEXA_7F_81_AEM;
        st.ds.magFilter = st.ds.minFilter = RD_FILTER_LINEAR;
        RdDrawPass dp[2];
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE_TEXA, "texa: RGBA16, AEM, bilinear: sprite_texa_ps");
        st.ds.magFilter = RD_FILTER_NEAREST;
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE_TEXA, "texa: linear MIN alone: sprite_texa_ps");
        st.ds.minFilter = RD_FILTER_NEAREST;
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: nearest: sprite_ps");
        st.ds.magFilter = st.ds.minFilter = RD_FILTER_LINEAR;
        st.ds.texa = RD_TEXA_80_80;
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: TEXA 80/80: sprite_ps");
        st.ds.texa = RD_TEXA_80_80_AEM;
        st.tex = t32.id;
        rd__plan_screen_draw(&st, RD_PRIM_TRIANGLES, RD_SPACE_UI, RHI_FMT_RGBA8_UNORM,
                             RHI_FMT_UNKNOWN, dp);
        CHECK(dp[0].key.fs == RD_FS_SPRITE, "texa: RGBA32: sprite_ps");
    }

    rd_begin_frame();
    rd_select_list(11);
    rd_clear_target(rd_target(RD_TARGET_WORK1), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    opaque2D();
    rd_tex_a(RD_TEXA_7F_81_AEM);
    rd_texture(t16, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    sprite(256, 256, 10 * 16, 10 * 16, (10 + N * S) * 16, (10 + N * S) * 16, grey, 8, 8, N * 16 + 8,
           N * 16 + 8);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    /* UV 10 + 4 x: off the texel edges, where the nearest texel is unambiguous */
    sprite(256, 256, 100 * 16, 10 * 16, (100 + N * S) * 16, (10 + N * S) * 16, grey, 10, 10,
           N * 16 + 10, N * 16 + 10);
    rd_tex_a(RD_TEXA_80_80);
    rd_end_frame(0);

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
    rd_destroy_texture(t16);
    rd_destroy_texture(t32);
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
    RdTex t = rd_create_texture(64, 4, tex, RD_TEXA_80_80, "stq");
    static const uint8_t black[4] = {0, 0, 0, 0}, white[4] = {0x80, 0x80, 0x80, 0x80};
    rd_begin_frame();
    rd_select_list(11);
    rd_clear_target(rd_target(RD_TARGET_WORK1), black, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    const int32_t ox = (2048 - 128) * 16, oy = (2048 - 128) * 16;
    for (int strip = 0; strip < 2; strip++) {
        const float q1 = strip ? 1.0f : 0.25f;
        const int32_t y0 = (20 + 40 * strip) * 16, y1 = (50 + 40 * strip) * 16;
        RdScreenVtx v[4] = {vtx(ox + 16 * 16, oy + y0, 0, white, 0.0f, 0.0f),
                            vtx(ox + 16 * 16, oy + y1, 0, white, 0.0f, 1.0f),
                            vtx(ox + 144 * 16, oy + y0, 0, white, q1, 0.0f),
                            vtx(ox + 144 * 16, oy + y1, 0, white, q1, 1.0f)};
        v[2].q = v[3].q = q1;
        rd_screen_prims(RD_PRIM_TRIANGLE_STRIP, v, 4, RD_SPACE_WORLD, 0, 0);
    }
    rd_end_frame(0);
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
    rd_destroy_texture(t);
}

/* ------------------------------------------------------------------ font */

static void fontState(void)
{
    rd_blend(RD_BLEND_LERP_AS, 0, 1); /* port/ui/font.c setState */
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_fba(0);
    rd_pabe(0);
    rd_abe(1);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
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
    RdTex r8 = rd_create_texture_r8(4, 4, cov, "font r8");
    RdTex t32 = rd_create_texture(4, 4, rgba, RD_TEXA_80_80, "font rgba8");
    static const uint8_t bg[4] = {40, 80, 120, 0x80};
    static const uint8_t col[4] = {0x70, 0x50, 0x80, 0x60};
    rd_begin_frame();
    rd_select_list(11);
    rd_clear_target(rd_target(RD_TARGET_WORK1), bg, 0, 0);
    rd_set_target(rd_target(RD_TARGET_WORK1), (RdTarget){0}, 256, 256, 0);
    fontState();
    for (int k = 0; k < 2; k++) {
        rd_texture(k ? t32 : r8, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        /* 1:1, the +8 nudge: each pixel samples a texel centre */
        sprite(256, 256, (10 + 10 * k) * 16, 10 * 16, (14 + 10 * k) * 16, 14 * 16, col, 8, 8,
               4 * 16 + 8, 4 * 16 + 8);
        /* 8x: bilinear between the texels */
        sprite(256, 256, (40 + 40 * k) * 16, 40 * 16, (72 + 40 * k) * 16, 72 * 16, col, 0, 0,
               4 * 16, 4 * 16);
    }
    rd_end_frame(0);
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
        CHECK(rd_dump_frame(path), "rd_dump_frame (font)");
        RdFrame f;
        if (rd__load_frame(path, &f)) {
            int r8s = 0;
            for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
                const RdTexRec *t = &g_rd.textures[i];
                r8s += t->live && t->kind == RD_TEXKIND_IMAGE && t->format == RD_TEXEL_R8 &&
                       t->w == 4 && t->h == 4 && memcmp(t->pixels, cov, 16) == 0 &&
                       strcmp(t->name, "dump") == 0;
            }
            CHECK(r8s == 1, "the loaded dump has the R8 atlas (%d)", r8s);
            static const uint8_t junk[4] = {1, 2, 3, 4};
            rd_begin_frame();
            rd_select_list(0);
            rd_clear_target(rd_target(RD_TARGET_WORK1), junk, 0, 0);
            rd_end_frame(0);
            CHECK(rd__replay_frame(&f, (int)f.keep, false), "replay of the loaded font frame");
            uint8_t *again = readTarget(RD_TARGET_WORK1, &w, &h);
            CHECK(again && memcmp(again, first, (size_t)w * h * 4) == 0,
                  "font dump -> load -> replay: the same WORK1");
            rd__frame_free(&f);
        } else {
            CHECK(0, "rd__load_frame (font)");
        }
    }
    rd_destroy_texture(r8);
    rd_destroy_texture(t32);
}

/* ------------------------------------------------------------ sheet text
 * A coverage strip through font_sheet_ps against the
 * CPU reference (sheet_ref.c).  The reference texels as an RGBA8 texture
 * drawn by sprite_ps are what the sheet texture drawn by font_sheet_ps must
 * give: the shader rebuilds the same texels and blends them as the sampler
 * blends the RGBA8 ones.  The draws are port/ui/font.c's state (LERP with
 * ABE, the font keys of the reachable set); TCC RGB writes the sampled grey
 * itself (As is the vertex's 0x80), TCC RGBA the sampled alpha into A. */
#define SHEET_W 40
#define SHEET_H 20

static uint8_t *readScaled(RdTargetId id, uint32_t *w, uint32_t *h, float *sx, float *sy);
static void testSheetScaled(int S, const char *dir);

static const RdSheetStyle kSheetEn = {1, 0, 0xFF, 1, 0, 1};    /* the English sheets: black rim */
static const RdSheetStyle kSheetFr = {1, 62, 0xFF, 1, 0, 1};   /* French, Italian, Spanish: grey */
static const RdSheetStyle kSheetPlain = {0, 0, 0xFF, 1, 0, 1}; /* the dark inks: no rim */
static const RdSheetStyle kSheetFaint = {1, 62, 0xFF, 1, 21, 1}; /* a faint halo (a third) */

/* a few shapes more than the rim's reach inside the edges (x 7..32 with
 * SHEET_RX 6, y 5..14 with SHEET_RY 4), so no edge texel has a rim: the
 * RGBA8 reference clamps to its edge texels where the shader rebuilds
 * texels beyond the edge, and the two agree only where both are clear: a
 * solid block, a soft diagonal edge, a thin stroke of partial coverage, a
 * noisy patch.  (With the shapes 3 texels in, written for a smaller reach,
 * the magnified alpha was 64 to 78 off along the left and right edges.) */
_Static_assert(ICO_SHEET_RX <= 6 && ICO_SHEET_RY <= 4, "sheetCov's margins hold the rim's reach");

static uint8_t sheetCov(int x, int y)
{
    if (x < 7 || y < 5 || x >= SHEET_W - 7 || y >= SHEET_H - 5) {
        return 0;
    }
    if (x < 12) {
        return 0xFF;
    }
    if (x >= 14 && x < 22) {
        const int v = ((x - 14) * 2 - (y - 5)) * 40 + 128;
        return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    if (x == 23 || x == 24) {
        return x == 23 ? 160 : 60;
    }
    if (x >= 26 && y >= 6 && y < 14) {
        return (uint8_t)hash((uint32_t)(y * 64 + x) + 77u);
    }
    return 0;
}

static uint8_t s_sheetCov[SHEET_W * SHEET_H];

/* the reference texels of style st as an RGBA8 texture (white is not
 * assumed: the grey is the texel's colour) */
static RdTex sheetRefTexture(const RdSheetStyle *st)
{
    static uint8_t rgba[SHEET_W * SHEET_H * 4];
    for (int y = 0; y < SHEET_H; y++) {
        for (int x = 0; x < SHEET_W; x++) {
            uint8_t g, a;
            sheetref_texel(s_sheetCov, SHEET_W, SHEET_H, x, y, st, &g, &a);
            uint8_t *p = &rgba[(y * SHEET_W + x) * 4];
            p[0] = p[1] = p[2] = g;
            p[3] = a;
        }
    }
    return rd_create_texture(SHEET_W, SHEET_H, rgba, RD_TEXA_80_80, "sheet ref");
}

/* the strip at (x, y) GS pixels of SCENE, sw x sh pixels, the whole
 * texture (nudge: the +8 UV nudge, each pixel at a texel centre when 1:1) */
static void sheetDraw(RdTex t, RdTcc tcc, int x, int y, int sw, int sh, int nudge)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    const int n = nudge ? 8 : 0;
    rd_texture(t, RD_TEXFN_MODULATE, tcc);
    sprite(512, 512, x * 16, y * 16, (x + sw) * 16, (y + sh) * 16, grey, n, n, SHEET_W * 16 + n,
           SHEET_H * 16 + n);
}

static void sheetFrameBegin(void)
{
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_begin_frame();
    rd_select_list(1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 0, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), (RdTarget){0}, 512, 512, 0);
    fontState();
}

/* the worst channel difference between the sw x sh GS pixel rectangles at
 * (ax, ay) and (bx, by) of img (s target pixels a GS pixel), channel c0..c1 */
static int sheetWorst(const uint8_t *img, uint32_t w, int s, int ax, int ay, int bx, int by, int sw,
                      int sh, int c0, int c1)
{
    int worst = 0;
    for (int y = 0; y < sh * s; y++) {
        for (int x = 0; x < sw * s; x++) {
            const uint8_t *a = &img[((size_t)(ay * s + y) * w + (size_t)(ax * s + x)) * 4];
            const uint8_t *b = &img[((size_t)(by * s + y) * w + (size_t)(bx * s + x)) * 4];
            for (int c = c0; c <= c1; c++) {
                const int d = abs((int)a[c] - (int)b[c]);
                worst = d > worst ? d : worst;
            }
        }
    }
    return worst;
}

/* 1:1 at texel centres (scene scale 1): every pixel is sheetref_texel's */
static int sheetExact(const uint8_t *img, uint32_t w, int x0, int yRgb, int yA,
                      const RdSheetStyle *st, int *rim)
{
    int bad = 0;
    *rim = 0;
    for (int y = 0; y < SHEET_H; y++) {
        for (int x = 0; x < SHEET_W; x++) {
            uint8_t g, a;
            sheetref_texel(s_sheetCov, SHEET_W, SHEET_H, x, y, st, &g, &a);
            const uint8_t *pc = &img[((size_t)(yRgb + y) * w + (size_t)(x0 + x)) * 4];
            const uint8_t *pa = &img[((size_t)(yA + y) * w + (size_t)(x0 + x)) * 4];
            if (pc[0] != g || pc[1] != g || pc[2] != g || pa[3] != a) {
                const int want[4] = {g, g, g, a};
                const uint8_t got[4] = {pc[0], pc[1], pc[2], pa[3]};
                bad += pixFail("sheet 1:1 against sheetref_texel", x, y, got, want);
            }
            /* a rim texel: no coverage of its own, some within the rim */
            if (s_sheetCov[y * SHEET_W + x] == 0 && a != 0) {
                (*rim)++;
                if (g != st->rimLevel) {
                    const int want[4] = {st->rimLevel, st->rimLevel, st->rimLevel, a};
                    bad += pixFail("sheet rim texel at the style's rim level", x, y, pc, want);
                }
            }
        }
    }
    return bad;
}

/* the overlay: the strip and its reference magnified 4x on the output */
typedef struct SheetOv {
    RdTex sheet, ref;
    int calls;
} SheetOv;

#define SHEET_OV_X 20
#define SHEET_OV_Y 20
#define SHEET_OV_M 4

static void sheetOvQuad(RdTex t, int x, int y)
{
    static const uint8_t c[4] = {0x40, 0x40, 0x40, 0x80}; /* half: Cs * As stays within 2 */
    RdScreenVtx v[2] = {vtx(x * 16, y * 16, 0, c, 0.0f, 0.0f),
                        vtx((x + SHEET_W * SHEET_OV_M) * 16, (y + SHEET_H * SHEET_OV_M) * 16, 0, c,
                            (float)(SHEET_W * 16), (float)(SHEET_H * 16))};
    rd_overlay_prims(RD_PRIM_SPRITES, v, 2, t, RD_BLEND_LERP_AS);
}

static void sheetOvCallback(const RdOverlayCtx *ctx, void *user)
{
    SheetOv *o = user;
    o->calls++;
    sheetOvQuad(o->sheet, SHEET_OV_X, SHEET_OV_Y);
    sheetOvQuad(o->ref, SHEET_OV_X, SHEET_OV_Y + SHEET_H * SHEET_OV_M + 10);
}

static void testSheetOverlay(RdTex sheet, RdTex ref)
{
    static SheetOv ov;
    memset(&ov, 0, sizeof(ov));
    ov.sheet = sheet;
    ov.ref = ref;
    rd_set_present_overlay(sheetOvCallback, &ov);
    CHECK(rd_present_blank(), "sheet overlay: rd_present_blank");
    static uint8_t out[640 * 480 * 4];
    uint32_t ow = 0, oh = 0;
    const bool ok = rd_read_presented(out, &ow, &oh) && ow == 640 && oh == 480;
    rd_set_present_overlay(NULL, NULL);
    CHECK(ok && ov.calls == 1, "sheet overlay: the presented output (%ux%u, %d calls)", ow, oh,
          ov.calls);
    if (!ok) {
        return;
    }
    const int sw = SHEET_W * SHEET_OV_M, sh = SHEET_H * SHEET_OV_M;
    const int worst = sheetWorst(out, ow, 1, SHEET_OV_X, SHEET_OV_Y, SHEET_OV_X,
                                 SHEET_OV_Y + sh + 10, sw, sh, 0, 2);
    int inked = 0;
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            inked += out[((size_t)(SHEET_OV_Y + y) * ow + (size_t)(SHEET_OV_X + x)) * 4] != 0;
        }
    }
    printf("  sheet overlay: magnified %dx, worst %d off the RGBA8 reference, %d pixels inked\n",
           SHEET_OV_M, worst, inked);
    CHECK(worst <= 2, "sheet overlay magnified: worst %d off the RGBA8 reference (2 allowed)",
          worst);
    CHECK(inked > sw * sh / 4, "sheet overlay: %d pixels inked", inked);
}

/* scale: the scene's (1 in Original); dir: the dump's directory (scale 1) */
static void testSheetText(const char *mode, int scale, const char *dir)
{
    for (int y = 0; y < SHEET_H; y++) {
        for (int x = 0; x < SHEET_W; x++) {
            s_sheetCov[y * SHEET_W + x] = sheetCov(x, y);
        }
    }
    RdTex sheet = rd_create_texture_sheet(SHEET_W, SHEET_H, s_sheetCov, &kSheetEn, "sheet");
    RdTex ref = sheetRefTexture(&kSheetEn);
    const RdTexRec *sr = rd__tex_rec(sheet.id);
    CHECK(sr && sr->format == RD_TEXEL_SHEET && sr->sheet[0] == 64 && sr->sheet[1] == 0 &&
              sr->sheet[2] == 0xFF && sr->sheet[3] == 1,
          "%s: the sheet texture's record", mode);

    /* magnified 3 x 4: sheet and reference side by side, TCC RGB (grey)
     * on top, TCC RGBA (alpha) below; 1:1 at the texel centres; the strip
     * magnified 2x twice, the second moved by whole pixels (96, 45) */
    enum { MX = 3, MY = 4 };

    sheetFrameBegin();
    sheetDraw(sheet, RD_TCC_RGB, 8, 8, SHEET_W * MX, SHEET_H * MY, 0);
    sheetDraw(ref, RD_TCC_RGB, 136, 8, SHEET_W * MX, SHEET_H * MY, 0);
    sheetDraw(sheet, RD_TCC_RGBA, 8, 96, SHEET_W * MX, SHEET_H * MY, 0);
    sheetDraw(ref, RD_TCC_RGBA, 136, 96, SHEET_W * MX, SHEET_H * MY, 0);
    sheetDraw(sheet, RD_TCC_RGB, 264, 8, SHEET_W, SHEET_H, 1);
    sheetDraw(sheet, RD_TCC_RGBA, 264, 40, SHEET_W, SHEET_H, 1);
    sheetDraw(sheet, RD_TCC_RGBA, 264, 184, SHEET_W * 2, SHEET_H * 2, 0);
    sheetDraw(sheet, RD_TCC_RGBA, 264 + 96, 184 + 45, SHEET_W * 2, SHEET_H * 2, 0);
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float fsx = 1.0f, fsy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_SCENE, &w, &h, &fsx, &fsy);
    const int s = (int)lroundf(fsx);
    CHECK(s == scale && (int)lroundf(fsy) == scale, "%s: SCENE at %gx%g, %d expected", mode,
          (double)fsx, (double)fsy, scale);
    if (img && s == scale) {
        const int sw = SHEET_W * MX, sh = SHEET_H * MY;
        const int wRgb = sheetWorst(img, w, s, 8, 8, 136, 8, sw, sh, 0, 2);
        const int wA = sheetWorst(img, w, s, 8, 96, 136, 96, sw, sh, 3, 3);
        const int moved =
            sheetWorst(img, w, s, 264, 184, 264 + 96, 184 + 45, SHEET_W * 2, SHEET_H * 2, 0, 3);
        int inked = 0;
        for (int y = 0; y < sh * s; y++) {
            for (int x = 0; x < sw * s; x++) {
                inked += img[((size_t)(96 * s + y) * w + (size_t)(8 * s + x)) * 4 + 3] != 0;
            }
        }
        printf("  sheet text (%s): magnified worst %d (grey) %d (alpha) off the RGBA8 "
               "reference, %d pixels inked; moved by whole pixels %d off\n",
               mode, wRgb, wA, inked, moved);
        CHECK(wRgb <= 1 && wA <= 1,
              "%s: the sheet magnified is %d (grey) %d (alpha) off sprite_ps on the reference "
              "texels (1 allowed)",
              mode, wRgb, wA);
        CHECK(inked > sw * sh * s * s / 4, "%s: %d pixels inked", mode, inked);
        /* 3 allowed: the shader blends the four texels with float weights
         * from the interpolated UV, which lands a hair either side of a
         * half at a different screen position (measured 1 at 1x and 3x, 2
         * at 2x with 5 levels, 3 at 1x and 3x with 8, where an alpha tie
         * also moves the blended colour); a grain fixed to the screen
         * instead of the texel is a level (18 in alpha with 8 levels) or
         * more off */
        CHECK(moved <= 3,
              "%s: the strip moved by whole pixels differs by %d (the grain must move "
              "with it; 3 allowed)",
              mode, moved);
        if (scale == 1) {
            int rim = 0;
            const int bad = sheetExact(img, w, 264, 8, 40, &kSheetEn, &rim);
            CHECK(bad == 0 && rim > 0, "%s: 1:1 %d texels off sheetref_texel, %d rim texels", mode,
                  bad, rim);
        }
    }
    free(img);

    if (scale == 1) {
        /* the style follows rd_set_texture_sheet_style: the French rim level,
         * a faint rim (fewer rim texels), then no rim (the rim texels go
         * transparent) */
        static const RdSheetStyle *const styles[3] = {&kSheetFr, &kSheetFaint, &kSheetPlain};
        static const char *const names[3] = {"French", "faint", "plain"};
        int rimFull = 0;
        for (int k = 0; k < 3; k++) {
            rd_set_texture_sheet_style(sheet, styles[k]);
            sheetFrameBegin();
            sheetDraw(sheet, RD_TCC_RGB, 264, 8, SHEET_W, SHEET_H, 1);
            sheetDraw(sheet, RD_TCC_RGBA, 264, 40, SHEET_W, SHEET_H, 1);
            rd_end_frame(0);
            img = readScaled(RD_TARGET_SCENE, &w, &h, &fsx, &fsy);
            if (img) {
                int rim = 0;
                const int bad = sheetExact(img, w, 264, 8, 40, styles[k], &rim);
                CHECK(bad == 0 && (k == 2   ? rim == 0
                                   : k == 1 ? rim > 0 && rim < rimFull
                                            : rim > 0),
                      "%s: style %d: %d texels off sheetref_texel, %d rim texels", mode, k, bad,
                      rim);
                rimFull = k == 0 ? rim : rimFull;
                printf("  sheet style %s: %d rim texels, 1:1 %d off\n", names[k], rim, bad);
            }
            free(img);
        }
        rd_set_texture_sheet_style(sheet, &kSheetEn);
        rd_set_texture_sheet_style(ref, &kSheetFr); /* not a sheet: ignored */
        const RdTexRec *rr = rd__tex_rec(ref.id);
        CHECK(rr && rr->sheet[1] == 0, "rd_set_texture_sheet_style ignores an RGBA8 texture");

        testSheetOverlay(sheet, ref);

        testSheetScaled(2, dir);
        testSheetScaled(4, dir);

        /* the dump keeps the format and the style: load, replay, the same
         * bytes; the faint style's rim weight rides in the view word's bits
         * 10..15, which the loader once rejected */
        static const RdSheetStyle *const dumped[2] = {&kSheetFr, &kSheetFaint};
        static const uint8_t dumpedWeight[2] = {64, 21};
        for (int k = 0; k < 2; k++) {
            rd_set_texture_sheet_style(sheet, dumped[k]);
            sheetFrameBegin();
            sheetDraw(sheet, RD_TCC_RGBA, 8, 8, SHEET_W * MX, SHEET_H * MY, 0);
            rd_end_frame(0);
            img = readScaled(RD_TARGET_SCENE, &w, &h, &fsx, &fsy);
            char path[1024];
            snprintf(path, sizeof(path), "%s/rd_pixel_sheet%d.rddump", dir, k);
            CHECK(rd_dump_frame(path), "rd_dump_frame (sheet style %d)", k);
            RdFrame f;
            if (img && rd__load_frame(path, &f)) {
                int sheets = 0;
                for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
                    const RdTexRec *t = &g_rd.textures[i];
                    sheets += t->live && t->kind == RD_TEXKIND_IMAGE &&
                              t->format == RD_TEXEL_SHEET && t->w == SHEET_W && t->h == SHEET_H &&
                              memcmp(t->pixels, s_sheetCov, sizeof(s_sheetCov)) == 0 &&
                              t->sheet[0] == dumpedWeight[k] && t->sheet[1] == 62 &&
                              t->sheet[2] == 0xFF && t->sheet[3] == 1 &&
                              strcmp(t->name, "dump") == 0;
                }
                CHECK(sheets == 1, "the loaded dump has the sheet texture with style %d (%d)", k,
                      sheets);
                static const uint8_t junk[4] = {1, 2, 3, 4};
                rd_begin_frame();
                rd_select_list(0);
                rd_clear_target(rd_target(RD_TARGET_SCENE), junk, 0, 0);
                rd_end_frame(0);
                CHECK(rd__replay_frame(&f, (int)f.keep, false),
                      "replay of the loaded sheet frame (style %d)", k);
                uint32_t w2 = 0, h2 = 0;
                uint8_t *again = readScaled(RD_TARGET_SCENE, &w2, &h2, &fsx, &fsy);
                CHECK(again && w2 == w && h2 == h && memcmp(again, img, (size_t)w * h * 4) == 0,
                      "sheet dump -> load -> replay (style %d): the same SCENE", k);
                free(again);
                rd__frame_free(&f);
            } else {
                CHECK(0, "rd__load_frame (sheet style %d)", k);
            }
            free(img);
        }
    }
    rd_destroy_texture(sheet);
    rd_destroy_texture(ref);
}

/* A strip rasterised at S texels a sheet texel
 * (style.scale S): sheetCov's shapes S times finer, plus a band of noise at
 * the strip's own texels (detail finer than a sheet texel), drawn 1:1 at
 * the texel centres in a 1x scene: every pixel is sheetref_texel's at
 * scale S within 1; the rim beside the solid block reaches 6.5 S pixels
 * across (ICO_SHEET_RX sheet texels, the sheets' width, and the half
 * sheet texel of the magnified ramp) and 4.5 S down; the fill's grain is
 * the sheet texel's (with the plain style a flat partial patch is constant
 * over each S x S cell and changes between cells); magnified 1.5x the
 * strip is within 1 of sprite_ps on the reference's texels; outside the
 * letters the strip is the 1x strip of its
 * sheet texels' mean coverage magnified S times, grey and alpha within 1
 * (the rim at any scale is the sheets' look magnified, never a finer
 * rendition of it); the frame dumped and loaded keeps the scale and
 * replays to the same bytes.  With a directory the scaled strip and the
 * 1x strip magnified are written side by side over a grey as
 * rd_pixel_rim_s<S>.png (each pixel 4 x 4). */
#define SCALED_MAX 4

/* the coverage, and below it the rim (rd_sheet_rim) */
static uint8_t s_scaledCov[SHEET_W * SHEET_H * SCALED_MAX * SCALED_MAX * 2];
/* its sheet texels' coverage, the means of the S x S cells */
static uint8_t s_scaledCov1[SHEET_W * SHEET_H];

/* floor(v / d) */
static int floorDivI(int v, int d)
{
    return v >= 0 ? v / d : -((-v + d - 1) / d);
}

/* the sheet texel (x, y)'s coverage of s_scaledCov1, 0 outside it */
static int cov1At(int x, int y)
{
    return x < 0 || y < 0 || x >= SHEET_W || y >= SHEET_H ? 0 : s_scaledCov1[y * SHEET_W + x];
}

/* the W x H pixels at (ax, ay) (grey from the RGB draw's row ay, alpha
   from the RGBA draw's row aa) over a grey, each pixel 4 x 4, at column ox
   of out (ow wide) */
static void rimPngPut(const uint8_t *img, uint32_t w, int ax, int ay, int aa, int W, int H,
                      uint8_t *out, int ow, int ox)
{
    static const int bg[3] = {150, 140, 120};
    for (int y = 0; y < H * 4; y++) {
        for (int x = 0; x < W * 4; x++) {
            const uint8_t *pc = &img[((size_t)(ay + y / 4) * w + (size_t)(ax + x / 4)) * 4];
            const uint8_t *pa = &img[((size_t)(aa + y / 4) * w + (size_t)(ax + x / 4)) * 4];
            const int a = pa[3] > 128 ? 128 : pa[3];
            uint8_t *o = &out[((size_t)y * (size_t)ow + (size_t)(ox + x)) * 4];
            for (int c = 0; c < 3; c++) {
                o[c] = (uint8_t)(bg[c] + ((int)pc[c] - bg[c]) * a / 128);
            }
            o[3] = 255;
        }
    }
}

static uint8_t scaledCov(int x, int y, int S)
{
    const int sx = x / S, sy = y / S;
    /* a flat partial patch over sheet texels 26..32 x 6..8: the grain test
       (164: 4.5 of the 7 steps, so the Bayer threshold picks 4 or 5) */
    if (sx >= 26 && sx < 33 && sy >= 6 && sy < 9) {
        return 164;
    }
    /* the noisy patch at the strip's own texels */
    if (sx >= 26 && sx < 33 && sy >= 9 && sy < 14) {
        return (uint8_t)hash((uint32_t)(y * 512 + x) + 91u);
    }
    return sheetCov(sx, sy);
}

static void testSheetScaled(int S, const char *dir)
{
    const int W = SHEET_W * S, H = SHEET_H * S;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            s_scaledCov[y * W + x] = scaledCov(x, y, S);
        }
    }
    rd_sheet_rim(s_scaledCov, (uint32_t)W, (uint32_t)H, (uint32_t)S, 0, 0, W, H,
                 s_scaledCov + (size_t)W * (size_t)H);
    RdSheetStyle st = kSheetEn;
    st.scale = (uint8_t)S;
    RdTex sheet =
        rd_create_texture_sheet((uint32_t)W, (uint32_t)(2 * H), s_scaledCov, &st, "sheet S");
    static uint8_t rgba[SHEET_W * SHEET_H * SCALED_MAX * SCALED_MAX * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t g, a;
            sheetref_texel(s_scaledCov, (uint32_t)W, (uint32_t)H, x, y, &st, &g, &a);
            uint8_t *q = &rgba[(y * W + x) * 4];
            q[0] = q[1] = q[2] = g;
            q[3] = a;
        }
    }
    RdTex ref = rd_create_texture((uint32_t)W, (uint32_t)H, rgba, RD_TEXA_80_80, "sheet S ref");
    /* the same strip in the plain style (the fill's grain),
       and the 1x strip of its sheet texels' mean coverage */
    RdSheetStyle plain = kSheetPlain;
    plain.scale = (uint8_t)S;
    RdTex sheetPlain = rd_create_texture_sheet((uint32_t)W, (uint32_t)(2 * H), s_scaledCov, &plain,
                                               "sheet S plain");
    for (int y = 0; y < SHEET_H; y++) {
        for (int x = 0; x < SHEET_W; x++) {
            int sum = 0;
            for (int j = 0; j < S; j++) {
                for (int i = 0; i < S; i++) {
                    sum += s_scaledCov[(y * S + j) * W + x * S + i];
                }
            }
            s_scaledCov1[y * SHEET_W + x] = (uint8_t)((sum + S * S / 2) / (S * S));
        }
    }
    RdTex sheet1 = rd_create_texture_sheet(SHEET_W, SHEET_H, s_scaledCov1, &kSheetEn, "sheet S 1x");
    const RdTexRec *sr = rd__tex_rec(sheet.id);
    CHECK(sr && sr->sheetScale == S, "scaled sheet %d: the record's scale (%d)", S,
          sr ? sr->sheetScale : -1);

    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    /* 1:1 (nudged to the texel centres) RGB at (8, 8), RGBA below; 1.5x
       sheet and reference side by side, RGB then RGBA */
    const int yA = 8 + H + 4, yM = yA + H + 8, mw = W * 3 / 2, mh = H * 3 / 2;
    const int yMA = yM + mh + 8;
    sheetFrameBegin();
    for (int k = 0; k < 2; k++) {
        rd_texture(sheet, RD_TEXFN_MODULATE, k ? RD_TCC_RGBA : RD_TCC_RGB);
        sprite(512, 512, 8 * 16, (k ? yA : 8) * 16, (8 + W) * 16, ((k ? yA : 8) + H) * 16, grey, 8,
               8, W * 16 + 8, H * 16 + 8);
    }
    for (int k = 0; k < 4; k++) {
        const int x = k & 1 ? 8 + mw + 8 : 8, y = k & 2 ? yMA : yM;
        rd_texture(k & 1 ? ref : sheet, RD_TEXFN_MODULATE, k & 2 ? RD_TCC_RGBA : RD_TCC_RGB);
        sprite(512, 512, x * 16, y * 16, (x + mw) * 16, (y + mh) * 16, grey, 0, 0, W * 16, H * 16);
    }
    /* right of the 1:1 strip the 1x strip magnified S times, RGB
       then RGBA, nudged by half a pixel (8 / S sixteenths of a sheet
       texel: a pixel samples at its corner) so pixel x samples sheet
       texel position (x + 0.5) / S, where the scaled strip's texel x
       reads its rim; and the plain strip 1:1 (RGBA) */
    const int x1x = 8 + W + 8, xPl = x1x + W + 8, n1 = 8 / S;
    for (int k = 0; k < 2; k++) {
        rd_texture(sheet1, RD_TEXFN_MODULATE, k ? RD_TCC_RGBA : RD_TCC_RGB);
        sprite(512, 512, x1x * 16, (k ? yA : 8) * 16, (x1x + W) * 16, ((k ? yA : 8) + H) * 16, grey,
               n1, n1, SHEET_W * 16 + n1, SHEET_H * 16 + n1);
    }
    rd_texture(sheetPlain, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(512, 512, xPl * 16, yA * 16, (xPl + W) * 16, (yA + H) * 16, grey, 8, 8, W * 16 + 8,
           H * 16 + 8);
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float fsx = 1.0f, fsy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_SCENE, &w, &h, &fsx, &fsy);
    if (!img) {
        CHECK(0, "scaled sheet %d: SCENE", S);
    } else {
        int bad = 0, worst = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint8_t g, a;
                sheetref_texel(s_scaledCov, (uint32_t)W, (uint32_t)H, x, y, &st, &g, &a);
                const uint8_t *pc = &img[((size_t)(8 + y) * w + (size_t)(8 + x)) * 4];
                const uint8_t *pa = &img[((size_t)(yA + y) * w + (size_t)(8 + x)) * 4];
                const int d[4] = {abs(pc[0] - g), abs(pc[1] - g), abs(pc[2] - g), abs(pa[3] - a)};
                for (int c = 0; c < 4; c++) {
                    worst = d[c] > worst ? d[c] : worst;
                }
                if (d[0] > 1 || d[1] > 1 || d[2] > 1 || d[3] > 1) {
                    const int want[4] = {g, g, g, a};
                    const uint8_t got[4] = {pc[0], pc[1], pc[2], pa[3]};
                    bad += pixFail("scaled sheet 1:1 against sheetref_texel", x, y, got, want);
                }
            }
        }
        /* the rim's reach: the inked run left of the solid block (sheet
           texels 7..11 across, 5..14 down) on its middle row, and above
           it on its middle column */
        const int my = 10 * S, mx = 9 * S;
        int left = 0, up = 0;
        for (int x = 7 * S - 1; x >= 0 && img[((size_t)(yA + my) * w + (size_t)(8 + x)) * 4 + 3];
             x--) {
            left++;
        }
        for (int y = 5 * S - 1; y >= 0 && img[((size_t)(yA + y) * w + (size_t)(8 + mx)) * 4 + 3];
             y--) {
            up++;
        }
        /* the fill's grain (the plain strip: no rim): the flat patch's
           S x S cells (its top row of sheet texels) have one alpha each,
           and the cells differ (the Bayer entries of neighbouring sheet
           texels) */
        int mixed = 0, cells = 0, levels = 0, firstA = -1;
        for (int cx = 27; cx < 32; cx++) {
            const int cy = 6;
            const uint8_t a = img[((size_t)(yA + cy * S) * w + (size_t)(xPl + cx * S)) * 4 + 3];
            for (int y = 0; y < S; y++) {
                for (int x = 0; x < S; x++) {
                    mixed +=
                        img[((size_t)(yA + cy * S + y) * w + (size_t)(xPl + cx * S + x)) * 4 + 3] !=
                        a;
                }
            }
            firstA = firstA < 0 ? a : firstA;
            levels += a != firstA;
            cells++;
        }
        const int wRgb = sheetWorst(img, w, 1, 8, yM, 8 + mw + 8, yM, mw, mh, 0, 2);
        const int wA = sheetWorst(img, w, 1, 8, yMA, 8 + mw + 8, yMA, mw, mh, 3, 3);
        /* outside the letters (the four sheet texels a pixel's rim
           blends have no coverage) the scaled strip is the 1x strip
           magnified, grey and alpha */
        int out1x = 0, rim1x = 0, worst1x = 0;
        for (int y = 0; y < H; y++) {
            const int qy = floorDivI(2 * y + 1 - S, 2 * S);
            for (int x = 0; x < W; x++) {
                const int qx = floorDivI(2 * x + 1 - S, 2 * S);
                if (cov1At(qx, qy) || cov1At(qx + 1, qy) || cov1At(qx, qy + 1) ||
                    cov1At(qx + 1, qy + 1)) {
                    continue;
                }
                const uint8_t *sc = &img[((size_t)(8 + y) * w + (size_t)(8 + x)) * 4];
                const uint8_t *sa = &img[((size_t)(yA + y) * w + (size_t)(8 + x)) * 4];
                const uint8_t *oc = &img[((size_t)(8 + y) * w + (size_t)(x1x + x)) * 4];
                const uint8_t *oa = &img[((size_t)(yA + y) * w + (size_t)(x1x + x)) * 4];
                const int d[4] = {abs(sc[0] - oc[0]), abs(sc[1] - oc[1]), abs(sc[2] - oc[2]),
                                  abs(sa[3] - oa[3])};
                for (int c = 0; c < 4; c++) {
                    worst1x = d[c] > worst1x ? d[c] : worst1x;
                }
                if (d[0] > 1 || d[1] > 1 || d[2] > 1 || d[3] > 1) {
                    const int want[4] = {oc[0], oc[1], oc[2], oa[3]};
                    const uint8_t got[4] = {sc[0], sc[1], sc[2], sa[3]};
                    bad +=
                        pixFail("scaled sheet rim against the 1x strip magnified", x, y, got, want);
                }
                out1x++;
                rim1x += oa[3] != 0;
            }
        }
        const int reachX = (2 * ICO_SHEET_RX + 1) * S / 2, reachY = (2 * ICO_SHEET_RY + 1) * S / 2;
        printf("  scaled sheet %dx: 1:1 worst %d off sheetref_texel and the 1x strip magnified "
               "(%d over 1); rim %d px across (%d), %d down (%d); grain: %d texels unlike their "
               "cell's first, %d of %d cells another alpha; 1.5x worst %d (grey) %d (alpha) off "
               "the reference; outside the letters %d px (%d rim) worst %d off the 1x strip "
               "magnified\n",
               S, worst, bad, left, reachX, up, reachY, mixed, levels, cells, wRgb, wA, out1x,
               rim1x, worst1x);
        CHECK(bad == 0,
              "scaled sheet %d: 1:1 %d texels more than 1 off sheetref_texel or the 1x "
              "strip magnified",
              S, bad);
        CHECK(rim1x > 40 * S * S && worst1x <= 1,
              "scaled sheet %d: outside the letters %d rim pixels, %d off the 1x strip magnified",
              S, rim1x, worst1x);
        CHECK(left == reachX && up == reachY,
              "scaled sheet %d: the rim reaches %d across (%d) and %d down (%d)", S, left, reachX,
              up, reachY);
        CHECK(mixed == 0 && levels > 0,
              "scaled sheet %d: the grain is the sheet texel's (%d texels differ inside a cell, "
              "%d cells differ)",
              S, mixed, levels);
        CHECK(wRgb <= 1 && wA <= 1,
              "scaled sheet %d: magnified %d (grey) %d (alpha) off sprite_ps on the reference "
              "(1 allowed)",
              S, wRgb, wA);
    }
    if (img && dir) {
        char path[1024];
        /* the scaled strip (left) and the 1x strip magnified */
        const int ow = W * 4 * 2 + 16, oh = H * 4;
        uint8_t *png = calloc((size_t)ow * (size_t)oh, 4);
        if (png) {
            rimPngPut(img, w, 8, 8, yA, W, H, png, ow, 0);
            rimPngPut(img, w, x1x, 8, yA, W, H, png, ow, W * 4 + 16);
            snprintf(path, sizeof(path), "%s/rd_pixel_rim_s%d.png", dir, S);
            CHECK(rd_write_png(path, png, (uint32_t)ow, (uint32_t)oh, (uint32_t)ow * 4, 0),
                  "scaled sheet %d: %s", S, path);
            free(png);
        }
        snprintf(path, sizeof(path), "%s/rd_pixel_sheet_s%d.rddump", dir, S);
        CHECK(rd_dump_frame(path), "rd_dump_frame (scaled sheet %d)", S);
        RdFrame f;
        if (rd__load_frame(path, &f)) {
            int sheets = 0;
            for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
                const RdTexRec *t = &g_rd.textures[i];
                sheets += t->live && t->kind == RD_TEXKIND_IMAGE && t->format == RD_TEXEL_SHEET &&
                          t->w == (uint32_t)W && t->sheetScale == S && strcmp(t->name, "dump") == 0;
            }
            CHECK(sheets == 2,
                  "the loaded dump has the scaled sheet and its plain twin with scale %d (%d)", S,
                  sheets);
            static const uint8_t junk[4] = {1, 2, 3, 4};
            rd_begin_frame();
            rd_select_list(0);
            rd_clear_target(rd_target(RD_TARGET_SCENE), junk, 0, 0);
            rd_end_frame(0);
            CHECK(rd__replay_frame(&f, (int)f.keep, false), "replay of the scaled sheet frame");
            uint32_t w2 = 0, h2 = 0;
            uint8_t *again = readScaled(RD_TARGET_SCENE, &w2, &h2, &fsx, &fsy);
            CHECK(again && w2 == w && h2 == h && memcmp(again, img, (size_t)w * h * 4) == 0,
                  "scaled sheet %d dump -> load -> replay: the same SCENE", S);
            free(again);
            rd__frame_free(&f);
        } else {
            CHECK(0, "rd__load_frame (scaled sheet %d)", S);
        }
    }
    free(img);
    rd_destroy_texture(sheet);
    rd_destroy_texture(sheetPlain);
    rd_destroy_texture(sheet1);
    rd_destroy_texture(ref);
}

/* the same at a scene scale of the Enhanced preset */
static void testSheetTextAt(const char *mode, int scale)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ENHANCED;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.sceneScale = (float)scale;
    if (!rd_init(512, 512, &s, NULL)) {
        CHECK(0, "rd_init (%s)", mode);
        return;
    }
    testSheetText(mode, scale, NULL);
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%s: %u validation errors", mode, verr);
    rd_shutdown();
}

/* ------------------------------------------------------------- reduction */

static uint8_t s_scene[512 * 512 * 4];

/* the synthetic scene: noise, drawn 1:1 into SCENE */
static void drawScene(RdTex t)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    opaque2D();
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
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
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    /* tints at or below 0x80 (the game's reduction colours are of this kind) */
    const uint8_t tint[3] = {100, 128, 90};
    rd_begin_frame();
    drawScene(t);
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    memcpy(pp.rgba, tint, 3);
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);

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
    rd_write_png(path, d, 512, 256, 512 * 4, 1);
    rd_destroy_texture(t);
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
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), col, 1, 0);
    rd_select_list(12);
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    rd_begin_frame();
    rd_select_list(0); /* not replayed in a keep frame */
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_select_list(11);
    RdPostParams kp;
    memset(&kp, 0, sizeof(kp));
    kp.dst = rd_target(RD_TARGET_SCENE);
    rd_post(RD_POST_KEEP, &kp);
    rd_select_list(12);
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(1);
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
    RdTex t = rd_create_texture(128, 128, NULL, RD_TEXA_80_80, "exact src");

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
        rd_update_texture(t, src);
        const int m = fr % 4;
        rd_begin_frame();
        rd_select_list(7);
        if (fr == 0) {
            rd_clear_target(rd_target(RD_TARGET_FEED128), init, 0, 0);
        }
        rd_set_target(rd_target(RD_TARGET_AA1), (RdTarget){0}, 128, 128, 0);
        opaque2D();
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        sprite(128, 128, 0, 0, 128 * 16, 128 * 16, grey, 8, 8, 128 * 16 + 8, 128 * 16 + 8);
        RdPostParams pp;
        memset(&pp, 0, sizeof(pp));
        pp.src = rd_target(RD_TARGET_AA1);
        pp.dst = rd_target(RD_TARGET_FEED128);
        pp.blend = (uint8_t)modes[m].eq;
        pp.fix = modes[m].fix;
        pp.exactInt = 1;
        rd_post(RD_POST_COMPOSITE_FIX, &pp);
        rd_end_frame(0);

        const uint32_t reg = rd__alpha_register((uint8_t)modes[m].eq);
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
    rd_destroy_texture(t);
}

/* ------------------------------------------------------------------ dump */

static void recordRichFrame(RdTex t)
{
    rd_begin_frame();
    drawScene(t);
    rd_select_list(2);
    /* additive with As up to 0xFF (DF_PREMUL), then a LERP_FIX quad */
    static const uint8_t add[4] = {60, 30, 90, 0xFF};
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
    rd_texture_off();
    sprite(512, 512, 100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
    rd_select_list(11);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 0x40;
    pp.rgba[0] = 0x20;
    rd_post(RD_POST_FADE, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.fix = 0x60;
    rd_post(RD_POST_LETTERBOX, &pp);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[3] = 5;
    rd_post(RD_POST_BRIGHTNESS, &pp);
    rd_select_list(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = 120;
    pp.rgba[2] = 110;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

static void testDump(const char *dir)
{
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
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
    CHECK(rd_dump_frame(path), "rd_dump_frame");
    RdFrame f;
    if (!rd__load_frame(path, &f)) {
        CHECK(0, "rd__load_frame");
        return;
    }
    /* scribble on the targets, then replay the loaded frame */
    static const uint8_t junk[4] = {1, 2, 3, 4};
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), junk, 1, 0x1234);
    rd_clear_target(rd_target(RD_TARGET_DISPLAY), junk, 0, 0);
    rd_end_frame(0);
    CHECK(rd__replay_frame(&f, (int)f.keep, false), "replay of the loaded frame");
    d = readTarget(RD_TARGET_DISPLAY, &w, &h);
    if (d) {
        size_t diff = 0;
        for (size_t i = 0; i < sizeof(a); i++) {
            diff += a[i] != d[i];
        }
        CHECK(diff == 0, "dump -> load -> replay: %zu bytes differ", diff);
        snprintf(path, sizeof(path), "%s/rd_pixel_frame.png", dir);
        rd_write_png(path, d, w, h, w * 4, 1);
    }
    rd__frame_free(&f);
    rd_destroy_texture(t);
}

/* ---------------------------------------------------------------- present */

static void testPresent(void)
{
    static uint8_t out[640 * 480 * 4];
    uint32_t w = 0, h = 0;
    if (!rd__read_present(out, sizeof(out), &w, &h)) {
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

/* ---------------------------------------------------------------- nodual */

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
    rd_screen_prims(RD_PRIM_TRIANGLES, v, 12, RD_SPACE_WORLD, 1, 0);
}

/* one frame: blends b0 .. b0 + nb - 1 over every other combination */
static void ndFrame(RdTex noise, int b0, int nb)
{
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_begin_frame();
    rd_select_list(5);
    for (int t = 0; t < 2; t++) {
        const RdTargetId id = t ? RD_TARGET_DISPLAY : RD_TARGET_SCENE;
        const uint32_t gw = 512, gh = t ? 256 : 512;
        const int32_t ox = (2048 - (int32_t)gw / 2) * 16, oy = (2048 - (int32_t)gh / 2) * 16;
        rd_clear_target(rd_target(id), clr, !t, 0);
        rd_set_target(rd_target(id), t ? (RdTarget){0} : rd_target(id), gw, gh, 1);
        /* the background: noise texels 1:1, Z write at ND_SCENE_BG_Z */
        rd_color_mask(0);
        rd_test_gs(RD_TEST_Z_ALWAYS);
        rd_z_write(1);
        rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
        rd_pabe(0);
        rd_fba(0);
        rd_gouraud(1);
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
        rd_texture(noise, RD_TEXFN_DECAL, RD_TCC_RGBA);
        {
            static const uint8_t white[4] = {0x80, 0x80, 0x80, 0x80};
            RdScreenVtx v[2] = {vtx(ox, oy, ND_SCENE_BG_Z, white, 0.0f, 0.0f),
                                vtx(ox + (int32_t)gw * 16, oy + (int32_t)gh * 16, ND_SCENE_BG_Z,
                                    white, (float)gw * 16.0f, (float)gh * 16.0f)};
            rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
        }
        rd_texture_off();
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
                                    rd_blend((RdBlend)b, (uint8_t)(b == 3 ? 0x50 : 0xA0), 1);
                                    rd_pabe(pabe);
                                    rd_fba(fba);
                                    rd_color_mask(kNdMasks[m]);
                                    rd_test_gs(ndTest(date, af, zz & 1));
                                    rd_z_write((zz >> 1) & 1);
                                    ndQuads(gw, gh, x, y, cell * 31u + (uint32_t)b);
                                }
                            }
                        }
                    }
                }
            }
        }
        rd_color_mask(0);
    }
    rd_end_frame(0);
}

static void testNoDual(void)
{
    enum { FRAMES = 3, BLENDS_PER = RD_BLEND_COUNT / FRAMES };

    static uint8_t noiseTex[64 * 64 * 4];
    for (uint32_t i = 0; i < 64 * 64; i++) {
        const uint32_t h = hash(i + 0x5EEDu);
        memcpy(&noiseTex[i * 4], &h, 4);
    }
    RdTex noise = rd_create_texture(64, 64, noiseTex, RD_TEXA_80_80, "nodual noise");
    static uint8_t scene[2][FRAMES][512 * 512 * 4], disp[2][FRAMES][512 * 256 * 4];
    static float depth[2][FRAMES][512 * 512];
    const bool was = rd_no_dual();
    uint32_t noDualKeys = 0;
    for (int mode = 0; mode < 2; mode++) {
        CHECK(rd_set_no_dual(mode != 0), "rd_set_no_dual(%d) refused", mode);
        for (int f = 0; f < FRAMES; f++) {
            ndFrame(noise, f * BLENDS_PER, BLENDS_PER);
            uint32_t w = 0, h = 0, dw = 0, dh = 0;
            CHECK(rd__read_target(rd_target(RD_TARGET_SCENE), scene[mode][f], sizeof(scene[0][0]),
                                  &w, &h) &&
                      w == 512 && h == 512,
                  "nodual: SCENE readback");
            CHECK(rd__read_target_depth(rd_target(RD_TARGET_SCENE), depth[mode][f],
                                        sizeof(depth[0][0]), &dw, &dh) &&
                      dw == 512 && dh == 512,
                  "nodual: SCENE depth readback");
            CHECK(rd__read_target(rd_target(RD_TARGET_DISPLAY), disp[mode][f], sizeof(disp[0][0]),
                                  &w, &h) &&
                      w == 512 && h == 256,
                  "nodual: DISPLAY readback");
        }
        if (mode) {
            for (uint32_t i = 0; i < rd__pipeline_count(); i++) {
                noDualKeys += rd__pipeline_key_at(i)->gs.nodual;
            }
        }
    }
    rd_set_no_dual(was);
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
    rd_destroy_texture(noise);
    /* its states are outside the reachable set: the pipes cell checks the
     * other cells' pipelines */
    rhi_wait_idle();
    rd__pipeline_cache_clear();
}

/* -------------------------------------------------------------- pipelines */

static void testPipelines(void)
{
    static RdPipeKeyInt keys[512], keysNd[512];
    /* both sets, with dual-source blending (keys) and with the
     * two-pass fallback (keysNd); the created keys are in the set of the
     * mode the test runs in */
    const bool was = rd_no_dual();
    rd_set_no_dual(false);
    const uint32_t ns = rd__enumerate_reachable_screen(keys, 512);
    const uint32_t n = rd__enumerate_reachable(keys, 512);
    rd_set_no_dual(true);
    const uint32_t nNd = rd__enumerate_reachable(keysNd, 512);
    rd_set_no_dual(was);
    const RdPipeKeyInt *mine = was ? keysNd : keys;
    const uint32_t nMine = was ? nNd : n;
    const uint32_t c = rd__pipeline_count();
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
        rd__reset_state_block(&s);
        s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
        s.ds.zwrite = RD_ZWRITE_OFF;
        s.ds.abe = 1;
        s.ds.blend = RD_BLEND_LERP_AS;
        s.ds.colorMask = 0x7;
        for (int dz = 0; dz < 2; dz++) {
            RdDrawPass dp[2];
            const RhiFormat depth = dz ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
            for (int prim = 0; prim < 2; prim++) {
                const int np = rd__plan_screen_draw(&s, prim ? RD_PRIM_LINES : RD_PRIM_TRIANGLES,
                                                    RD_SPACE_UI, RHI_FMT_RGBA8_UNORM, depth, dp);
                for (int i = 0; i < np; i++) {
                    int found = 0;
                    for (uint32_t j = 0; j < ns; j++) {
                        found |= rd__pipe_key_equal(&dp[i].key, &keys[j]);
                    }
                    CHECK(dp[i].key.gs.colorMask == 0x7 && found,
                          "UI draw under colour mask 7 (lines %d, depth %d) is not enumerated",
                          prim, dz);
                    dp[i].key.fs = RD_FS_FONT;
                    found = 0;
                    for (uint32_t j = 0; j < ns; j++) {
                        found |= rd__pipe_key_equal(&dp[i].key, &keys[j]);
                    }
                    CHECK(prim || found,
                          "font draw under colour mask 7 (depth %d) is not enumerated", dz);
                    /* the menus' sheet text in the same state */
                    dp[i].key.fs = RD_FS_FONT_SHEET;
                    found = 0;
                    for (uint32_t j = 0; j < ns; j++) {
                        found |= rd__pipe_key_equal(&dp[i].key, &keys[j]);
                    }
                    CHECK(prim || found,
                          "sheet text draw under colour mask 7 (depth %d) is not enumerated", dz);
                }
            }
            s.ds.colorMask = 0xF;
            s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
            const int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD,
                                                RHI_FMT_RGBA8_UNORM, depth, dp);
            for (int i = 0; i < np; i++) {
                int found = 0;
                CHECK(rd__stq_pass(&dp[i]), "a plain world sprite pass takes the STQ shaders");
                for (uint32_t j = 0; j < ns; j++) {
                    found |= rd__pipe_key_equal(&dp[i].key, &keys[j]);
                }
                CHECK(found, "STQ world triangle pass (depth %d) is not enumerated", dz);
            }
            s.ds.colorMask = 0x7;
            s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
        }
    }
    CHECK(n < RD_PIPELINE_REACHABLE_MAX, "reachable pipelines %u >= %d (with the VU programs)", n,
          RD_PIPELINE_REACHABLE_MAX);
    for (uint32_t i = 0; i < c; i++) {
        const RdPipeKeyInt *k = rd__pipeline_key_at(i);
        int found = 0;
        for (uint32_t j = 0; j < nMine && j < 512; j++) {
            found |= rd__pipe_key_equal(k, &mine[j]);
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
 * The inputs of staticBlur.c's mirage (feedback mode 2) as
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
    const RdTargetRec *t = rd__target_rec(rd_target(id).id);
    if (!t) {
        CHECK(0, "target %d has no record", (int)id);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)t->tw * t->th * 4);
    if (!buf || !rd__read_target(rd_target(id), buf, (size_t)t->tw * t->th * 4, w, h)) {
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
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
}

/* auraInspireBefore's clear of AURA_WORK as staticBlur.c's host path
 * records it: Z test off, Z write off, PABE 1, ALPHA mode 2 without ABE, an
 * untextured RD_POST_AURA sprite over the screen in colour 0 */
static void auraClear(void)
{
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_pabe(1);
    rd_blend_func(RD_BLEND_LERP_FIX, 0);
    rd_texture_off();
    rd_abe(0);
    rd_gouraud(0);
    RdPostParams p;
    memset(&p, 0, sizeof(p));
    p.rect[0] = (float)(0x8000 - 256 * 16);
    p.rect[1] = (float)(0x8000 - 256 * 16);
    p.rect[2] = (float)(0x8000 + 256 * 16);
    p.rect[3] = (float)(0x8000 + 256 * 16);
    p.scalar[2] = 1.0f;
    p.exactInt = 1;
    rd_post(RD_POST_AURA, &p);
}

/* (a): the mask behind the scene */
static void testAuraDepth(const char *mode, RdTex white)
{
    static const uint8_t grey[4] = {0x60, 0x60, 0x60, 0x80}, red[4] = {200, 30, 30, 0x80};
    static const uint8_t shine[4] = {0x80, 0x80, 0x80, 0x7F}; /* the queen's vertex colour */
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    /* list 1: the face, opaque, nearer, Z write */
    rd_select_list(1);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    auraQuad(100, 100, 200, 200, AURA_Z_NEAR, red);
    /* list 8: the clear, then the shine material: farther (rows 50..150),
     * and at the face's depth (rows 160..190) */
    rd_select_list(8);
    auraClear();
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_test_gs(0x5346D); /* ATE GREATER 0x46, AFAIL RGB_ONLY, Z GEQUAL */
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_texture(white, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    auraQuad(50, 50, 250, 150, AURA_Z_FAR, shine);
    auraQuad(50, 160, 250, 190, AURA_Z_NEAR, shine);
    rd_end_frame(0);

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
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), grey, 1, 0);
    rd_select_list(1);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    auraQuad(40, 40, 260, 240, face, skin);
    rd_select_list(8);
    auraClear();
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_test_gs(0x5346D);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_sampler(RD_FILTER_LINEAR, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_texture(white, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    for (int k = 0; k < 5; k++) {
        auraQuad(50 + k * 40, 50, 80 + k * 40, 230, (uint32_t)((int32_t)face + kDz[k]), shine);
    }
    rd_end_frame(0);
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
    RdTex t = rd_create_texture(16, 16, tex, RD_TEXA_80_80, "aura alpha");
    static const uint8_t bg[4] = {10, 200, 30, 0x55}, vc[4] = {0x80, 0x80, 0x80, 0x80};
    rd_begin_frame();
    rd_select_list(8);
    rd_clear_target(rd_target(RD_TARGET_AURA_WORK), bg, 0, 0);
    rd_set_target(rd_target(RD_TARGET_AURA_WORK), rd_target(RD_TARGET_SCENE), 512, 512, 0);
    rd_test_gs(fbOnly ? 0x3160D : 0x3346D); /* Z ALWAYS */
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    auraQuad(64, 64, 80, 80, 0, vc); /* 1:1 */
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_AURA_WORK, &w, &h, &sx, &sy);
    if (!img) {
        rd_destroy_texture(t);
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
    rd_destroy_texture(t);
}

static void testAuraMask(const char *mode)
{
    static uint8_t whiteTx[16 * 16 * 4];
    memset(whiteTx, 0xFF, sizeof(whiteTx));
    for (int i = 0; i < 16 * 16; i++) {
        whiteTx[i * 4 + 3] = 0x80;
    }
    RdTex white = rd_create_texture(16, 16, whiteTx, RD_TEXA_80_80, "aura white");
    testAuraDepth(mode, white);
    testAuraDepthNear(mode, white);
    testAuraAlpha(mode, 0);
    testAuraAlpha(mode, 1);
    rd_destroy_texture(white);
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
    if (!rd_init(512, 512, &s, NULL)) {
        CHECK(0, "rd_init (%s)", mode);
        return;
    }
    testAuraMask(mode);
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%s: %u validation errors", mode, verr);
    rd_shutdown();
}

/* ------------------------------------------------------------ VU paths */
/* One prelit triangle through the four position paths of
 * vu_triangle_out: code 34 (RD_VU_CLIP_NONE: vu_gs_position), code 32
 * (REGION: vu_vtx_position), code 36 uncut (SCISSOR: vu_vtx_position) and
 * code 36 with vertex 1 past the clip space's far plane (SCISSOR, cut:
 * vu_cut_position, which is vu_vtx_position for these vertices since issue
 * 25); VP_36R is the cut one with its winding reversed (vertices 0, 2, 1).
 * The vertices sit off the 12.4 grid and the triangle slopes in Z
 * (thousands of GS Z units a pixel around 16M, the game's 3D range). */
enum { VP_34, VP_32, VP_36, VP_36H, VP_36R, VP_COUNT };

static const char *const kVpName[VP_COUNT] = {"34", "32", "36", "36 cut", "36 cut reversed"};

static RdMesh s_vpMesh[2][2]; /* [reversed][red, green] */

static void vpMeshes(void)
{
    static const float kPos[3][4] = {{40.3f, 30.7f, 1000000.3f, 1.0f},
                                     {200.6f, 60.2f, 1040000.7f, 1.0f},
                                     {90.9f, 210.45f, 980000.1f, 1.0f}};
    static const int kOrder[2][3] = {{0, 1, 2}, {0, 2, 1}};
    static float qw[2][2][1 + 3 * 3][4];
    for (int r = 0; r < 2; r++) {
        for (int i = 0; i < 2; i++) {
            memset(qw[r][i], 0, sizeof(qw[r][i]));
            const uint32_t tag = 0x8003u; /* NLOOP 3, EOP */
            memcpy(&qw[r][i][0][0], &tag, 4);
            for (int k = 0; k < 3; k++) {
                memcpy(qw[r][i][1 + k * 3], kPos[kOrder[r][k]], sizeof(kPos[0]));
                qw[r][i][1 + k * 3 + 1][2] = 1.0f;
                /* the strip flag on vertex 0 */
                qw[r][i][1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f;
                qw[r][i][1 + k * 3 + 2][0] = i == 0 ? 200.0f : 30.0f;
                qw[r][i][1 + k * 3 + 2][1] = i == 0 ? 30.0f : 200.0f;
                qw[r][i][1 + k * 3 + 2][2] = 30.0f;
                qw[r][i][1 + k * 3 + 2][3] = 127.0f;
            }
            const RdVuBatchDesc bd = {0, 0, 0};
            RdVuMeshDesc md;
            memset(&md, 0, sizeof(md));
            md.qw = (const float (*)[4])qw[r][i];
            md.qwCount = 10;
            md.qwPerVertex = RD_VU_QW_PRELIT;
            md.batchCount = 1;
            md.batches = &bd;
            s_vpMesh[r][i] = rd_create_vu_mesh(&md);
        }
    }
}

static const char kVpKey;

/* the triangle by path, red or green, moved (tx, ty) GS pixels; the
 * model-to-screen matrix puts model (0, 0) at the 512 target's top-left */
static void vpDraw(int path, int green, double tx, double ty)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = path == VP_34 ? 34 : path == VP_32 ? 32 : 36;
    d.clip = path == VP_34   ? RD_VU_CLIP_NONE
             : path == VP_32 ? RD_VU_CLIP_REGION
                             : RD_VU_CLIP_SCISSOR;
    for (int c = 0; c < 4; c++) {
        d.vu.mem[16 + c][c] = 1.0f;
    }
    d.vu.mem[19][0] = (float)(1792.0 + tx);
    d.vu.mem[19][1] = (float)(1792.0 + ty);
    /* clip space (mem[20..23]): x = y = 0, w = 1, z = pos.z / 1.02M: vertex
     * 1 (1.04M) past z = w for VP_36H and VP_36R, nothing flagged otherwise */
    const int cut = path == VP_36H || path == VP_36R;
    d.vu.mem[23][3] = 1.0f;
    d.vu.mem[22][2] = cut ? 1.0f / 1020000.0f : 0.0f;
    rd_draw_vu_mesh(s_vpMesh[path == VP_36R][green], &d, RD_KEY(&kVpKey, green, path));
}

static void vpState(int zwrite)
{
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(zwrite);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
}

static const uint8_t kVpGrey[4] = {0x40, 0x40, 0x40, 0x80};

static int vpIsBg(const uint8_t *p)
{
    return p[0] == kVpGrey[0] && p[1] == kVpGrey[1] && p[2] == kVpGrey[2];
}

/* red base by path a with Z write, green by path b over it with GEQUAL:
 * the base's interior pixels (no background within 2 pixels) left red
 * failed b's depth test against a's depth */
static void vpPair(int a, int b, double tx, double ty, int *interior, int *failed)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kVpGrey, 1, 0);
    vpState(1);
    vpDraw(a, 0, tx, ty);
    vpState(0);
    vpDraw(b, 1, tx, ty);
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_SCENE, &w, &h, &sx, &sy);
    *interior = *failed = 0;
    if (!img) {
        return;
    }
    for (uint32_t y = 2; y + 2 < h; y++) {
        for (uint32_t x = 2; x + 2 < w; x++) {
            int edge = 0;
            for (int dy = -2; dy <= 2 && !edge; dy++) {
                for (int dx = -2; dx <= 2 && !edge; dx++) {
                    edge = vpIsBg(&img[((size_t)(y + dy) * w + (x + dx)) * 4]);
                }
            }
            if (edge) {
                continue;
            }
            const uint8_t *p = &img[((size_t)y * w + x) * 4];
            (*interior)++;
            *failed += p[0] > p[1];
        }
    }
    free(img);
}

/* each row's first and last covered pixel (-1: none) of one path at a
 * horizontal shift of tx GS pixels; the row count */
#define VP_ROWS 2048

static int vpEdges(int path, double tx, int *left, int *right)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kVpGrey, 1, 0);
    vpState(1);
    vpDraw(path, 0, tx, 0.0);
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_SCENE, &w, &h, &sx, &sy);
    if (!img) {
        return 0;
    }
    h = h > VP_ROWS ? VP_ROWS : h;
    for (uint32_t y = 0; y < h; y++) {
        left[y] = right[y] = -1;
        for (uint32_t x = 0; x < w; x++) {
            if (!vpIsBg(&img[((size_t)y * w + x) * 4])) {
                left[y] = left[y] < 0 ? (int)x : left[y];
                right[y] = (int)x;
            }
        }
    }
    free(img);
    return (int)h;
}

/* every pair of paths, one drawn over the other; then a camera panning
 * 1/64 GS pixel a frame across one GS pixel: per row, each path's edges
 * move forward only, by at most one output pixel a frame, and 34's edges
 * stay within one output pixel of 32's.  check34: whether code 34 is to
 * match the other paths (1x; Enhanced above 1x once fixed); the cut path
 * is checked like the others since issue 25's first fix (vu_cut_position;
 * the shimmer itself was the case of testVuOverlap), its reversed twin
 * reported, not checked */
static void testVuPaths(const char *mode, int check34)
{
    vpMeshes();
    for (int a = 0; a < VP_COUNT; a++) {
        for (int b = 0; b < VP_COUNT; b++) {
            int interior = 0, failed = 0;
            vpPair(a, b, 0.0, 0.0, &interior, &failed);
            printf("  vu paths (%s): %s then %s over it: %d of %d interior pixels fail GEQUAL\n",
                   mode, kVpName[a], kVpName[b], failed, interior);
            if (a != VP_36R) {
                CHECK(interior > 1000, "vu paths (%s): %s drew %d interior pixels", mode,
                      kVpName[a], interior);
            }
            const int checked =
                a != VP_36R && b != VP_36R && (check34 || (a != VP_34 && b != VP_34));
            if (checked) {
                CHECK(failed == 0,
                      "vu paths (%s): %s over %s: %d of %d interior pixels fail the depth test",
                      mode, kVpName[b], kVpName[a], failed, interior);
            }
        }
    }
    /* code 32 over 34 (a reflection pass over a code-34 material pass)
     * along a diagonal pan (1/64 GS pixel across, 0.61/64 down a frame):
     * the frames where it fails anywhere, the fewest and most pixels */
    int panFail = 0, panMax = 0, panMin = 1 << 30;
    for (int s = 0; s <= 64; s++) {
        int interior = 0, failed = 0;
        vpPair(VP_34, VP_32, (double)s / 64.0, (double)s * 0.61 / 64.0, &interior, &failed);
        panFail += failed > 0;
        panMax = failed > panMax ? failed : panMax;
        panMin = failed < panMin ? failed : panMin;
    }
    printf("  vu paths (%s): 32 over 34 panned in 1/64 GS pixel steps: %d of 65 frames fail "
           "GEQUAL, %d to %d pixels\n",
           mode, panFail, panMin, panMax);
    if (check34) {
        CHECK(panFail == 0, "vu paths (%s): 32 over 34 fails in %d frames of the pan", mode,
              panFail);
    }
    static int l[VP_36H][VP_ROWS], r[VP_36H][VP_ROWS], pl[VP_36H][VP_ROWS], pr[VP_36H][VP_ROWS];
    int back[VP_36H] = {0}, maxStep[VP_36H] = {0}, maxSep = 0, rows = 0;
    for (int s = 0; s <= 64; s++) {
        for (int p = 0; p < VP_36H; p++) {
            rows = vpEdges(p, (double)s / 64.0, l[p], r[p]);
            for (int y = 0; s > 0 && y < rows; y++) {
                if (l[p][y] < 0 || pl[p][y] < 0) {
                    continue;
                }
                const int dl = l[p][y] - pl[p][y], dr = r[p][y] - pr[p][y];
                back[p] += (dl < 0) + (dr < 0);
                maxStep[p] = dl > maxStep[p] ? dl : maxStep[p];
                maxStep[p] = dr > maxStep[p] ? dr : maxStep[p];
            }
            memcpy(pl[p], l[p], sizeof(l[p]));
            memcpy(pr[p], r[p], sizeof(r[p]));
        }
        for (int y = 0; y < rows; y++) {
            if (l[VP_34][y] >= 0 && l[VP_32][y] >= 0) {
                const int dl = abs(l[VP_34][y] - l[VP_32][y]), dr = abs(r[VP_34][y] - r[VP_32][y]);
                maxSep = dl > maxSep ? dl : maxSep;
                maxSep = dr > maxSep ? dr : maxSep;
            }
        }
    }
    for (int p = 0; p < VP_36H; p++) {
        printf("  vu paths (%s): %s panned one GS pixel in 1/64 steps: edges step at most %d "
               "pixels, %d steps back\n",
               mode, kVpName[p], maxStep[p], back[p]);
        CHECK(back[p] == 0, "vu paths (%s): %s's edges moved back %d times", mode, kVpName[p],
              back[p]);
        CHECK(maxStep[p] <= 1, "vu paths (%s): %s's edges jumped %d pixels", mode, kVpName[p],
              maxStep[p]);
    }
    printf("  vu paths (%s): 34's edges and 32's up to %d pixels apart\n", mode, maxSep);
    if (check34) {
        CHECK(maxSep == 0, "vu paths (%s): 34's edges %d pixels from 32's", mode, maxSep);
    }
    for (int r = 0; r < 2; r++) {
        rd_destroy_vu_mesh(s_vpMesh[r][0]);
        rd_destroy_vu_mesh(s_vpMesh[r][1]);
    }
}

/* ------------------------------------------------------------ VU seams */
/* (issue 26) edges two draws share, against the frame's clear: the GS
 * draws a vertex where ftoi4 put it, so an edge two primitives share is
 * drawn by both from the same 12.4 values and leaves no pixel between
 * them.  A pixel the clear shows along it (grey, Z 0) is a crack, which in
 * the game the fog pass paints at full fog (the bright dots of issue 26).
 * Model space is GS pixels from the 512 target's top-left, as in vpDraw. */
static const char kSeamKey;
static const uint8_t kSeamRed[4] = {200, 30, 30, 0x80}, kSeamGreen[4] = {30, 200, 30, 0x80};

/* ftoi4 as vu_ftoi4 computes it, of a float the VU holds */
static int32_t seamFtoi4(float x)
{
    const float v = x * 16.0f;
    if (v >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (v <= -2147483648.0f) {
        return INT32_MIN;
    }
    return (int32_t)v;
}

/* a prelit mesh of one batch: n vertices (x, y, z, w), a strip starting at
 * each vertex start marks (the strip flag) */
static RdMesh seamMesh(const float (*pos)[4], const uint8_t *start, int n, const uint8_t c[4])
{
    float (*qw)[4] = calloc(1 + (size_t)n * 3, sizeof(*qw));
    if (!qw) {
        CHECK(0, "seam mesh: no memory");
        return (RdMesh){0};
    }
    const uint32_t tag = 0x8000u | (uint32_t)n; /* NLOOP n, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < n; k++) {
        memcpy(qw[1 + k * 3], pos[k], sizeof(pos[k]));
        qw[1 + k * 3 + 1][2] = 1.0f;
        qw[1 + k * 3 + 1][3] = start[k] ? 0.0f : 1.0f;
        for (int i = 0; i < 4; i++) {
            qw[1 + k * 3 + 2][i] = i < 3 ? (float)c[i] : 127.0f;
        }
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + (uint32_t)n * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    const RdMesh m = rd_create_vu_mesh(&md);
    free(qw);
    CHECK(m.id != 0, "seam mesh: rd_create_vu_mesh");
    return m;
}

/* one draw of a seam mesh by code 32 (REGION) or 36 (SCISSOR): the model
 * to screen matrix turns by (cs, sn) = (cos, sin) about model (0, 0) and
 * moves by (tx, ty) GS pixels; clip space as vpDraw's (x = y = 0, w =
 * pos.w, z = pos.z / 1.02M) */
static void seamVuDraw(RdMesh m, int code, double cs, double sn, double tx, double ty, int ord)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = (uint8_t)code;
    d.clip = code == 36 ? RD_VU_CLIP_SCISSOR : RD_VU_CLIP_REGION;
    d.vu.mem[16][0] = (float)cs;
    d.vu.mem[16][1] = (float)sn;
    d.vu.mem[17][0] = (float)-sn;
    d.vu.mem[17][1] = (float)cs;
    d.vu.mem[18][2] = 1.0f;
    d.vu.mem[19][0] = (float)(1792.0 + tx);
    d.vu.mem[19][1] = (float)(1792.0 + ty);
    d.vu.mem[19][3] = 1.0f;
    d.vu.mem[22][2] = 1.0f / 1020000.0f;
    d.vu.mem[23][3] = 1.0f;
    rd_draw_vu_mesh(m, &d, RD_KEY(&kSeamKey, code, ord));
}

/* the GS vertex (12.4 X, Y and Z) of model point p under the identity
 * matrix moved by (tx, ty), as the VU computes it (each product and sum in
 * float: x * 1 + y * 0 + z * 0 + T is x + T) */
static RdScreenVtx seamGsVtx(const float p[4], double tx, double ty, const uint8_t c[4])
{
    const float hx = p[0] + (float)(1792.0 + tx), hy = p[1] + (float)(1792.0 + ty);
    return vtx(seamFtoi4(hx), seamFtoi4(hy), (uint32_t)seamFtoi4(p[2]), c, 0.0f, 0.0f);
}

/* the background pixels within one output pixel of the segments (x0, y0,
 * x1, y1 in GS pixels from the target's top-left) at parameters t0..t1
 * along each, each pixel counted once; *nearOut: all pixels so near.  A
 * pixel's centre is GS x / sx (FrameCB g_origin.zw: GS integers on texel
 * centres) */
static int seamCracks(const uint8_t *img, uint32_t w, uint32_t h, float sx, float sy,
                      const double (*seg)[4], int nseg, double t0, double t1, int *nearOut)
{
    int cracks = 0, near = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            int hit = 0;
            for (int i = 0; i < nseg && !hit; i++) {
                const double ax = seg[i][0] * sx, ay = seg[i][1] * sy;
                const double dx = seg[i][2] * sx - ax, dy = seg[i][3] * sy - ay;
                const double t =
                    (((double)x - ax) * dx + ((double)y - ay) * dy) / (dx * dx + dy * dy);
                if (t < t0 || t > t1) {
                    continue;
                }
                const double ex = ax + t * dx - (double)x, ey = ay + t * dy - (double)y;
                hit = ex * ex + ey * ey <= 1.0;
            }
            if (hit) {
                near++;
                cracks += vpIsBg(&img[((size_t)y * w + x) * 4]);
            }
        }
    }
    *nearOut = near;
    return cracks;
}

static void seamBegin(void)
{
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kVpGrey, 1, 0);
    vpState(1);
}

/* ends the frame begun by seamBegin: seamCracks of the scene (-1: no
 * readback); probe (GS pixels, NULL: none): *probeBg whether that pixel
 * shows the clear */
static int seamEnd(const double (*seg)[4], int nseg, double t0, double t1, int *near,
                   const double *probe, int *probeBg)
{
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    float sx = 1.0f, sy = 1.0f;
    uint8_t *img = readScaled(RD_TARGET_SCENE, &w, &h, &sx, &sy);
    *near = 0;
    if (!img) {
        return -1;
    }
    const int n = seamCracks(img, w, h, sx, sy, seg, nseg, t0, t1, near);
    if (probe) {
        const double px = probe[0] * sx + 0.5, py = probe[1] * sy + 0.5;
        *probeBg = px < 0.0 || py < 0.0 || px >= (double)w || py >= (double)h ||
                   vpIsBg(&img[((size_t)py * w + (size_t)px) * 4]);
    }
    free(img);
    return n;
}

/* S1-S4 in the bound renderer (mode: its name) */
static void testVuSeams(const char *mode)
{
    /* a quad split along P1-P2 (its normal points to P3's side); off the
     * 12.4 grid, Z below the far plane of seamVuDraw's clip space */
    static const float kP[4][4] = {{60.37f, 50.81f, 1000000.3f, 1.0f},
                                   {300.62f, 90.23f, 1010000.7f, 1.0f},
                                   {120.91f, 330.47f, 990000.1f, 1.0f},
                                   {380.13f, 310.69f, 1005000.9f, 1.0f}};
    static const uint8_t kStart3[3] = {1, 0, 0}, kStart4[4] = {1, 0, 0, 0};
    const double edge[1][4] = {{kP[1][0], kP[1][1], kP[2][0], kP[2][1]}}; /* within 1/16 GS pixel */
    int near = 0, n;

    /* S1: a code-32 triangle beside a GIF triangle (rd_screen_prims) at the
     * VU's ftoi4 corners; the VU on either side of the shared edge */
    for (int side = 0; side < 2; side++) {
        const int vu0 = side == 0 ? 0 : 1, gif0 = side == 0 ? 1 : 0;
        const RdMesh m = seamMesh(&kP[vu0], kStart3, 3, kSeamRed);
        seamBegin();
        seamVuDraw(m, 32, 1.0, 0.0, 0.0, 0.0, side);
        RdScreenVtx g[3];
        for (int k = 0; k < 3; k++) {
            g[k] = seamGsVtx(kP[gif0 + k], 0.0, 0.0, kSeamGreen);
        }
        rd_screen_prims(RD_PRIM_TRIANGLES, g, 3, RD_SPACE_WORLD, 1, 0);
        n = seamEnd(edge, 1, 0.1, 0.9, &near, NULL, NULL);
        printf("  vu seams (%s): S1 code 32 on P%d's side of a GIF triangle: %d cracks in %d "
               "pixels along the edge\n",
               mode, side == 0 ? 0 : 3, n, near);
        CHECK(n == 0 && near > 100, "vu seams (%s): S1 side %d: %d cracks (%d pixels near)", mode,
              side, n, near);
        rd_destroy_vu_mesh(m);
    }

    /* S2: two meshes under matrices 0.01 GS pixel apart whose shared
     * vertices have the same ftoi4 values (16 x frac(X, Y) = n + 0.5) */
    {
        static const float kQ[4][4] = {{60.37f, 50.81f, 1000000.3f, 1.0f},
                                       {300.65625f, 90.21875f, 1010000.7f, 1.0f},
                                       {120.90625f, 330.46875f, 990000.1f, 1.0f},
                                       {380.13f, 310.69f, 1005000.9f, 1.0f}};
        const double qEdge[1][4] = {{kQ[1][0], kQ[1][1], kQ[2][0], kQ[2][1]}};
        for (int k = 1; k <= 2; k++) {
            const RdScreenVtx v0 = seamGsVtx(kQ[k], 0.0, 0.0, kSeamRed);
            const RdScreenVtx v1 = seamGsVtx(kQ[k], 0.01, 0.01, kSeamRed);
            CHECK(v0.x == v1.x && v0.y == v1.y,
                  "vu seams: S2 vertex %d's ftoi4 differs under the two matrices", k);
        }
        const RdMesh ma = seamMesh(&kQ[0], kStart3, 3, kSeamRed);
        const RdMesh mb = seamMesh(&kQ[1], kStart3, 3, kSeamGreen);
        seamBegin();
        seamVuDraw(ma, 32, 1.0, 0.0, 0.0, 0.0, 0);
        seamVuDraw(mb, 32, 1.0, 0.0, 0.01, 0.01, 1);
        n = seamEnd(qEdge, 1, 0.1, 0.9, &near, NULL, NULL);
        printf("  vu seams (%s): S2 two meshes 0.01 GS pixel apart: %d cracks in %d pixels\n", mode,
               n, near);
        CHECK(n == 0 && near > 100, "vu seams (%s): S2: %d cracks (%d pixels near)", mode, n, near);
        rd_destroy_vu_mesh(ma);
        rd_destroy_vu_mesh(mb);
    }

    /* S3: a code-36 strip v0..v3 whose first triangle is cut (v0 flagged
     * -z) and whose second is kicked; v0 on P3's side, so the cut triangle
     * off the grid would leave the gap.  v0 with GS Z < 0 (w 1), then with
     * w < 0 (GS Z > 0 then: only w tells).  The probe: 8 GS pixels from
     * the edge's middle into v0's side, which the cut triangle covers */
    for (int c = 0; c < 2; c++) {
        float strip[4][4];
        memcpy(strip[0], kP[3], sizeof(strip[0]));
        memcpy(strip[1], kP[1], sizeof(strip[1]));
        memcpy(strip[2], kP[2], sizeof(strip[2]));
        memcpy(strip[3], kP[0], sizeof(strip[3]));
        strip[0][2] = -2000000.0f; /* clip z < -w: the -z flag */
        strip[0][3] = c == 0 ? 1.0f : -1.0f;
        const RdMesh m = seamMesh((const float (*)[4])strip, kStart4, 4, kSeamRed);
        seamBegin();
        seamVuDraw(m, 36, 1.0, 0.0, 0.0, 0.0, 2 + c);
        const double probe[2] = {(kP[1][0] + kP[2][0]) * 0.5 + 8.0 * 0.8,
                                 (kP[1][1] + kP[2][1]) * 0.5 + 8.0 * 0.6};
        int probeBg = 1;
        n = seamEnd(edge, 1, 0.1, 0.9, &near, probe, &probeBg);
        printf("  vu seams (%s): S3 code 36 cut beside kicked, v0 %s: %d cracks in %d pixels, "
               "v0's side %s\n",
               mode, c == 0 ? "GS Z < 0" : "w < 0", n, near, probeBg ? "empty" : "covered");
        CHECK(n == 0 && near > 100, "vu seams (%s): S3 (%s): %d cracks (%d pixels near)", mode,
              c == 0 ? "GS Z < 0" : "w < 0", n, near);
        CHECK(!probeBg, "vu seams (%s): S3 (%s): the cut triangle left v0's side empty", mode,
              c == 0 ? "GS Z < 0" : "w < 0");
        rd_destroy_vu_mesh(m);
    }

    /* S4: 8 x 8 quads of one mesh, each drawn under its own matrix (the
     * grid's turn by 0.002 rad and the quad's corner, in double): shared
     * corners differ by float noise only.  Reported against Original 1x */
    {
        static int s_s4Ref = -1;
        const double qs = 40.0, ang = 0.002, cs = cos(ang), sn = sin(ang), bx = 96.3, by = 80.7;
        const float quad[4][4] = {{0.0f, 0.0f, 1000000.3f, 1.0f},
                                  {(float)qs, 0.0f, 1000000.3f, 1.0f},
                                  {0.0f, (float)qs, 1000000.3f, 1.0f},
                                  {(float)qs, (float)qs, 1000000.3f, 1.0f}};
        const RdMesh m = seamMesh(quad, kStart4, 4, kSeamRed);
        seamBegin();
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                const double mx = i * qs, my = j * qs;
                seamVuDraw(m, 32, cs, sn, bx + cs * mx - sn * my, by + sn * mx + cs * my,
                           j * 8 + i);
            }
        }
        double lines[14][4];
        for (int k = 1; k < 8; k++) {
            const double a = k * qs, e = 8.0 * qs;
            double *v = lines[k - 1], *u = lines[6 + k];
            v[0] = bx + cs * a; /* model (a, 0) to (a, 8 qs) */
            v[1] = by + sn * a;
            v[2] = bx + cs * a - sn * e;
            v[3] = by + sn * a + cs * e;
            u[0] = bx - sn * a; /* model (0, a) to (8 qs, a) */
            u[1] = by + cs * a;
            u[2] = bx + cs * e - sn * a;
            u[3] = by + sn * e + cs * a;
        }
        n = seamEnd((const double (*)[4])lines, 14, 0.02, 0.98, &near, NULL, NULL);
        if (strcmp(mode, "Original 1x") == 0) {
            s_s4Ref = n;
        }
        printf("  vu seams (%s): S4 8 x 8 quads, a matrix each: %d cracks in %d pixels along the "
               "inner edges (Original 1x: %d)\n",
               mode, n, near, s_s4Ref);
        CHECK(near > 1000, "vu seams (%s): S4 saw %d pixels near the inner edges", mode, near);
        rd_destroy_vu_mesh(m);
    }
}

/* ------------------------------------------------- coplanar overlaps */
/* (issue 25) the near railing's lattice as the game draws it: one code-32
 * mesh holding a panel's front strip and then its back strips, the same
 * five corners (the quad and a corner on its right edge whose UV is off the
 * panel's mapping, by 2 texels here as in the dump) split along other
 * diagonals, under the railing's state (ATE GREATER 0x60, AFAIL FB_ONLY, Z
 * GEQUAL with Z write, ABE LERP As), seen obliquely with the game's Z range
 * (30M to 41M) by a camera moving a little each step.  In GS arithmetic
 * (ftoi4 per vertex) the two triangulations' depths differ by hundreds of
 * units with a sign that changes from step to step: the earlier face wins
 * GEQUAL in steps 1, 5, 9 and 10, the later one elsewhere.  The single draw
 * must give the picture of the faces with the later one passing over the
 * earlier at every pixel (the reference: the front strip, then the back
 * strips with Z ALWAYS), every step; the two faces drawn apart with
 * GEQUAL, the rounding's picture, show that the steps do make the earlier
 * face win.  Nearest filtering, so every passing texel is opaque and the
 * order of a fringe under a later wire does not enter. */
static const char kOvKey;

#define OV_TEX 32

/* the lattice: diagonal wires 3 texels wide every 16, the middle texel
 * opaque (its colour from the texel's place, so the two faces' copies 2
 * texels apart differ), the outer two a dark fringe at 0x40 (fails the
 * alpha test, FB_ONLY blends it), the holes alpha 0 */
static RdTex ovTexture(void)
{
    static uint8_t tex[OV_TEX * OV_TEX * 4];
    for (int y = 0; y < OV_TEX; y++) {
        for (int x = 0; x < OV_TEX; x++) {
            uint8_t *p = &tex[(y * OV_TEX + x) * 4];
            const int a = (x + y) % 16, b = (x - y + 2 * OV_TEX) % 16;
            const int core = a == 1 || b == 1, edge = a == 0 || a == 2 || b == 0 || b == 2;
            if (core) {
                p[0] = (uint8_t)(90 + x * 5);
                p[1] = (uint8_t)(100 + y * 4);
                p[2] = (uint8_t)(150 + (x * 3 + y) % 60);
                p[3] = 0x80;
            } else if (edge) {
                p[0] = 20;
                p[1] = 24;
                p[2] = 30;
                p[3] = 0x40;
            } else {
                p[0] = p[1] = p[2] = p[3] = 0;
            }
        }
    }
    return rd_create_texture(OV_TEX, OV_TEX, tex, RD_TEXA_80_80, "overlap lattice");
}

/* a textured prelit mesh of one batch: corners k[0..n) of the panel, a
 * strip starting at each vertex start marks */
static RdMesh ovMesh(const int *k, const uint8_t *start, int n)
{
    /* P0, P1, P2 (the quad's top left, top right, bottom left), Pm (on the
     * right edge, 0.62 down; its V 0.555, 2 texels off), P4 (bottom right) */
    static const float kPos[5][2] = {
        {-40.0f, -30.0f}, {40.0f, -30.0f}, {-40.0f, 30.0f}, {40.0f, 7.2f}, {40.0f, 30.0f}};
    static const float kUv[5][2] = {
        {0.0f, 0.0f}, {1.25f, 0.0f}, {0.0f, 1.0f}, {1.25f, 0.555f}, {1.25f, 1.0f}};
    float qw[1 + 12 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8000u | (uint32_t)n; /* NLOOP n, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int i = 0; i < n && i < 12; i++) {
        float *pos = qw[1 + i * 3], *st = qw[2 + i * 3], *rgba = qw[3 + i * 3];
        pos[0] = kPos[k[i]][0];
        pos[1] = kPos[k[i]][1];
        pos[3] = 1.0f;
        st[0] = kUv[k[i]][0];
        st[1] = kUv[k[i]][1];
        st[2] = 1.0f;
        st[3] = start[i] ? 0.0f : 1.0f; /* the strip flag */
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 128.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + (uint32_t)n * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    const RdMesh m = rd_create_vu_mesh(&md);
    CHECK(m.id != 0, "overlap mesh: rd_create_vu_mesh");
    return m;
}

/* one code-32 draw at camera step s: model (x, y, 0) at view X = x + 0.2 y
 * + cx, Y = y + cy, depth D = 200 + 1.2 x + 0.45 y + dD (focal 300), GS Z =
 * 16 (1M + 220M / D) */
static void ovDraw(RdMesh m, int s, int ord)
{
    const double f = 300.0, kx = 1.2, ky = 0.45, sh = 0.2, za = 1.0e6, zb = 2.2e8;
    const double cx = 0.0131 * s, cy = 0.0077 * s, d = 200.0 + 0.0093 * s;
    RdVuDraw dr;
    memset(&dr, 0, sizeof(dr));
    dr.prog = RD_PROG_PRELIT;
    dr.code = 32;
    dr.clip = RD_VU_CLIP_REGION;
    const double col[4][4] = {{f + 2048.0 * kx, 2048.0 * kx, za * kx, kx},
                              {f * sh + 2048.0 * ky, f + 2048.0 * ky, za * ky, ky},
                              {0.0, 0.0, 0.0, 0.0},
                              {f * cx + 2048.0 * d, f * cy + 2048.0 * d, za * d + zb, d}};
    for (int c = 0; c < 4; c++) {
        for (int e = 0; e < 4; e++) {
            dr.vu.mem[16 + c][e] = (float)col[c][e];
        }
    }
    rd_draw_vu_mesh(m, &dr, RD_KEY(&kOvKey, s, ord));
}

static void ovState(RdTex t, uint32_t test)
{
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(test);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 1);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
}

#define OV_TEST_GEQUAL 0x5160Du /* the railing's TEST: ATE GREATER 0x60, FB_ONLY, Z GEQUAL */
#define OV_TEST_ALWAYS 0x3160Du /* the same with Z ALWAYS */

/* one frame: mesh a under test ta, then (b.id: ) mesh b under tb; SCENE */
static uint8_t *ovFrame(RdTex t, RdMesh a, uint32_t ta, RdMesh b, uint32_t tb, int s, uint32_t *w,
                        uint32_t *h)
{
    float sx = 1.0f, sy = 1.0f;
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), kVpGrey, 1, 0);
    ovState(t, ta);
    ovDraw(a, s, 0);
    if (b.id) {
        ovState(t, tb);
        ovDraw(b, s, 1);
    }
    rd_end_frame(0);
    return readScaled(RD_TARGET_SCENE, w, h, &sx, &sy);
}

/* pixels whose RGB differs by more than 1 */
static int ovDiff(const uint8_t *x, const uint8_t *y, uint32_t w, uint32_t h)
{
    int n = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *p = &x[i * 4], *q = &y[i * 4];
        n += abs(p[0] - q[0]) > 1 || abs(p[1] - q[1]) > 1 || abs(p[2] - q[2]) > 1;
    }
    return n;
}

static void testVuOverlap(const char *mode)
{
    /* the game's order: the front strip, then the back strips */
    static const int kAll[12] = {0, 1, 2, 3, 4, 2, 3, 0, 1, 4, 3, 2};
    static const uint8_t kAllStart[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const RdTex t = ovTexture();
    const RdMesh all = ovMesh(kAll, kAllStart, 12);
    const RdMesh front = ovMesh(kAll, kAllStart, 5);
    const RdMesh back = ovMesh(kAll + 5, kAllStart + 5, 7);
    const RdMesh none = {0};
    /* the marks: the back strips' three triangles (kicks 7, 8 and 11) in the
     * one mesh, none in either face alone */
    const RdMeshRec *ma = rd__mesh_rec(all.id), *mf = rd__mesh_rec(front.id),
                    *mb = rd__mesh_rec(back.id);
    if (ma && mf && mb) {
        int marks = 0, right = 0;
        for (uint32_t i = 0; i < ma->indexCount; i++) {
            const uint32_t ix = rd__mesh_draw_index(ma)[i], kick = (ix & ICO_VU_INDEX_MASK) / 4;
            const int later = (ix & ICO_VU_INDEX_LATER) != 0;
            marks += later;
            right += later == (kick == 7 || kick == 8 || kick == 11);
        }
        CHECK(marks == 9 && right == (int)ma->indexCount && !mf->drawIndex && !mb->drawIndex,
              "vu overlap: %d marked indices (%d as expected of %u), front %s, back %s", marks,
              right, ma->indexCount, mf->drawIndex ? "marked" : "clear",
              mb->drawIndex ? "marked" : "clear");
    } else {
        CHECK(0, "vu overlap: mesh records");
    }
    int bad = 0, worst = 0, earlierWins = 0, covered = 0;
    for (int s = 0; s < 12; s++) {
        uint32_t w = 0, h = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
        uint8_t *one = ovFrame(t, all, OV_TEST_GEQUAL, none, 0, s, &w, &h);
        uint8_t *ref = ovFrame(t, front, OV_TEST_GEQUAL, back, OV_TEST_ALWAYS, s, &w2, &h2);
        uint8_t *gs = ovFrame(t, front, OV_TEST_GEQUAL, back, OV_TEST_GEQUAL, s, &w3, &h3);
        if (one && ref && gs && w == w2 && h == h2 && w == w3 && h == h3) {
            const int d = ovDiff(one, ref, w, h), g = ovDiff(gs, ref, w, h);
            int drawn = 0;
            for (size_t i = 0; i < (size_t)w * h; i++) {
                drawn += !vpIsBg(&ref[i * 4]);
            }
            covered = drawn > covered ? drawn : covered;
            printf("  vu overlap (%s): step %2d: the one draw differs from the later face over "
                   "the earlier in %d pixels; the faces drawn apart with GEQUAL in %d\n",
                   mode, s, d, g);
            bad += d > 0;
            worst = d > worst ? d : worst;
            earlierWins += g > 0;
        } else {
            CHECK(0, "vu overlap (%s): step %d: readback", mode, s);
        }
        free(one);
        free(ref);
        free(gs);
    }
    printf("  vu overlap (%s): %d of 12 steps differ (at most %d pixels); the earlier face wins "
           "GEQUAL in %d steps\n",
           mode, bad, worst, earlierWins);
    CHECK(covered > 1000, "vu overlap (%s): the panel covers %d pixels", mode, covered);
    CHECK(earlierWins > 0,
          "vu overlap (%s): no step makes the earlier face win (the test "
          "geometry no longer z-fights)",
          mode);
    CHECK(bad == 0,
          "vu overlap (%s): %d steps where the later face does not pass over the "
          "earlier (at most %d pixels)",
          mode, bad, worst);
    rd_destroy_vu_mesh(all);
    rd_destroy_vu_mesh(front);
    rd_destroy_vu_mesh(back);
    rd_destroy_texture(t);
}

static void testVuPathsAt(const char *mode, RdPreset preset, float scale, int check34)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = preset;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    s.sceneScale = scale;
    if (!rd_init(512, 512, &s, NULL)) {
        CHECK(0, "rd_init (%s)", mode);
        return;
    }
    testVuPaths(mode, check34);
    testVuSeams(mode);   /* issue 26 */
    testVuOverlap(mode); /* issue 25 */
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%s: %u validation errors", mode, verr);
    rd_shutdown();
}

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    const char *dir = argc > 1 ? argv[1] : ".";
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = 4.0f / 3.0f;
    if (!rd_init(512, 512, &s, NULL)) {
        printf("SKIP rd_pixel_test: no usable Vulkan device\n");
        return 77;
    }
    printf("rd_pixel_test: adapter %s%s\n", rhi_adapter_name(),
           rd_no_dual() ? " (two-pass blend fallback)" : "");
    testNoDual(); /* first: it clears the pipeline cache */
    testOrder();
    testHalfOffset();
    testRailing(0);
    testRailing(1); /* as the game's materials draw it */
    testDateFlat();
    testScreenRuns();
    testSprites();
    testTexa();
    testFont(dir);
    testSheetText("Original 1x", 1, dir);
    testStq();
    testAa1();
    testReduction(dir);
    testPresent();
    testKeep();
    testExact();
    testDump(dir);
    testPipelines();
    testAuraMask("Original 1x");
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%u validation errors", verr);
    CHECK(rd__not_implemented_count() == 0, "no stubbed command replayed");
    rd_shutdown();
    testAuraMaskAt("Enhanced 4x", 4.0f, 0);
    testAuraMaskAt("Enhanced 4x, full height", 4.0f, 1);
    testSheetTextAt("Enhanced 2x", 2);
    testSheetTextAt("Enhanced 3x", 3);
    testVuPathsAt("Original 1x", RD_PRESET_ORIGINAL, 1.0f, 1);
    testVuPathsAt("Original 4x", RD_PRESET_ORIGINAL, 4.0f, 1);
    testVuPathsAt("Enhanced 2.25x", RD_PRESET_ENHANCED, 2.25f, 1); /* issues 25, 26 */
    testVuPathsAt("Enhanced 4x", RD_PRESET_ENHANCED, 4.0f, 1);
    if (failures) {
        printf("rd_pixel_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pixel_test: ok\n");
    return 0;
}
