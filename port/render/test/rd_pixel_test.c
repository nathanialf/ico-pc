/* rd_pixel_test.c: rd through the Vulkan RHI (lavapipe in the container).
 *
 *   order    the same pixel drawn from list 5 (recorded first) and list 1:
 *            list 5 wins, it replays later
 *   sprite   sprites in GS 12.4 window coordinates against the GS rule
 *            (pixel x covered when x0 <= x < x1, sampled at the integer):
 *            integer edges, half-pixel edges, the -4 corner nudge, a sprite
 *            covering no pixel centre, a one-pixel sprite; a textured
 *            sprite with the +8 UV nudge copies texels exactly; TEXA
 *            7F/81+AEM on an RGB24 source; rd_UVOffset
 *   font     (package R8) a 4x4 R8 coverage atlas (GS alpha units) drawn
 *            through rd_ScreenPrims under port/ui/font.c's state (font_ps):
 *            1:1 at texel centres the stored alpha is (c * va) >> 7, and
 *            1:1 and magnified 8x with bilinear filtering every byte equals
 *            the same atlas as RGBA8 (white, alpha c) through sprite_ps;
 *            the frame dumped and loaded has the texture as R8 with its
 *            texels and replays to the same bytes
 *   reduce   rd_Post(RD_POST_REDUCTION) on a synthetic 512x512 scene
 *            against a CPU reference of gsb_Reduction (bilinear at the GS
 *            sample points, tint, border crop) within 1 LSB
 *   keep     a keep frame (lists 11/12 only) draws DISPLAY back at 112/128
 *   exact    100 frames of RD_POST_COMPOSITE_FIX with exactInt into
 *            FEED128 equal the GS integer formula exactly every frame
 *   dump     a frame replayed, dumped, loaded and replayed again gives the
 *            same DISPLAY bytes; the dump is left for rd_replay_tool
 *   pipes    every pipeline created is in the enumerated reachable set,
 *            whose screen and post part has fewer than 100 keys (all of it,
 *            with the VU programs of wave 3, fewer than RD_PIPELINE_REACHABLE_MAX)
 *
 * argv[1]: a writable directory.  Exit 0, 1 on a mismatch, 77 without a
 * Vulkan device.  Any validation error fails the test. */
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
    /* tints at or below 0x80: a 1-LSB filter difference stays 1 LSB after the
     * modulate (the game's reduction colours are of this kind) */
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
                 * bilinear between texels px, px+1 (0.75/0.25) and rows 2py,
                 * 2py+1 (0.5/0.5) */
                for (int k = 0; k < 4; k++) {
                    const int x0 = px, x1 = px + 1, y0 = 2 * py, y1 = 2 * py + 1;
                    double v = 0.375 * scene[((size_t)y0 * 512 + x0) * 4 + k] +
                               0.125 * scene[((size_t)y0 * 512 + x1) * 4 + k] +
                               0.375 * scene[((size_t)y1 * 512 + x0) * 4 + k] +
                               0.125 * scene[((size_t)y1 * 512 + x1) * 4 + k];
                    int t8 = (int)(v + 0.5);
                    want[k] = (int)gs_tfx_mod((uint32_t)t8, k < 3 ? tint[k] : 0x80);
                }
            }
            int bad = 0;
            for (int k = 0; k < 4; k++) {
                int e = abs((int)p[k] - want[k]);
                if (e > worst) {
                    worst = e;
                }
                if (e > (inside ? 1 : 0)) {
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

/* -------------------------------------------------------------- pipelines */

static void testPipelines(void)
{
    static RdPipeKeyInt keys[512];
    const uint32_t ns = rd__EnumerateReachableScreen(keys, 512);
    const uint32_t n = rd__EnumerateReachable(keys, 512);
    const uint32_t c = rd__PipelineCount();
    printf("  pipelines: %u created, %u reachable (%u screen and post)\n", c, n, ns);
    CHECK(ns < 100, "reachable screen and post pipelines %u >= 100", ns);
    CHECK(n < RD_PIPELINE_REACHABLE_MAX,
          "reachable pipelines %u >= %d (wave 3: with the VU programs)", n,
          RD_PIPELINE_REACHABLE_MAX);
    for (uint32_t i = 0; i < c; i++) {
        const RdPipeKeyInt *k = rd__PipelineKeyAt(i);
        int found = 0;
        for (uint32_t j = 0; j < n && j < 512; j++) {
            found |= rd__PipeKeyEqual(k, &keys[j]);
        }
        CHECK(found,
              "created pipeline %u (prog %u blend %u vs %u fmt %u/%u z %u/%u mask %x) is "
              "not in the enumerated set",
              i, k->gs.program, k->gs.blend, k->vs, k->colorFmt, k->depthFmt, k->gs.ztst,
              k->gs.zwrite, k->gs.colorMask);
    }
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
    printf("rd_pixel_test: adapter %s\n", rhi_AdapterName());
    testOrder();
    testDateFlat();
    testSprites();
    testFont(dir);
    testReduction(dir);
    testPresent();
    testKeep();
    testExact();
    testDump(dir);
    testPipelines();
    const uint32_t verr = rhi_vk_ValidationErrorCount();
    CHECK(verr == 0, "%u validation errors", verr);
    CHECK(rd__NotImplementedCount() == 0, "no stubbed command replayed");
    rd_Shutdown();
    if (failures) {
        printf("rd_pixel_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pixel_test: ok\n");
    return 0;
}
