/* rd_crt_test.c: the CRT filter (package CRT; port/render/rd_crt.c,
 * port/shaders/crt.hlsl, docs/port/DISPLAY.md "CRT filter").
 *
 * Without a device:
 *   options   [video] crt, crt_mode, crt_strength and the five overrides
 *             from a config.toml, the defaults, the mode parser and names,
 *             out-of-range values, save and read back
 *   resolve   the modes' table, the overrides (a mask on the maskless
 *             mode, the cylindrical face keeping y flat), the mask's fade
 *             with the box height, rd_CrtSettings
 * On a Vulkan device (77 without one):
 *   off       rd_present_test's rich frame presented at 960 x 720: with the
 *             filter off its bytes are rd_present's recorded hash (llvmpipe),
 *             and a mode at strength 0 is the same bytes (nothing drawn)
 *   modes     each mode at 960 x 720 and 1920 x 1440: the hash of the
 *             present against the recorded constant (llvmpipe only: another
 *             driver's sampling and transcendental maths differ; logged
 *             elsewhere), and the same bytes from a second run
 *   outside   a 1280 x 720 output (the 4:3 box 960 x 720 in the middle):
 *             every pixel outside the box black, in every mode
 *   luma      the scanlines mode's mean luminance in the box within 20 %
 *             of the plain present's
 *   phosphors (package CRT2) the mask per output pixel, glow and
 *             curvature off: at a 1440 x 1080 box (2.81 output pixels a
 *             source pixel) under a white frame every box pixel is one
 *             channel only, the one rd__CrtMaskWeight gives its position,
 *             and each source pixel's columns run R, G, B in order; at a
 *             1536 x 1152 box (3 a pixel) a red frame in Trinitron lights
 *             only each pixel's first column, a white one R, G, B columns;
 *             Consumer TV's slot bridges are dark and the odd columns' half
 *             a line off the even ones'; the shadow mask's second row of
 *             dots is half a triad over; the scanlines mode has no column
 *             structure (the geometry at mask strength 1: pure stripes);
 *             at the modes' own strengths each stripe leaks (1 - strength)
 *             of the other channels (Trinitron, Consumer TV) and at 1440 x
 *             1080 every triad keeps its pixel's light; the gap columns,
 *             the leak and the gains (each triad's light integrated)
 *
 * Usage: rd_crt_test [dir]  (dir: where the scratch config goes)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "rd_internal.h"
#include "rhi.h"
#include "shader_consts.h"
#include "video_options.h"

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

/* ------------------------------------------------------------- options */

static void writeFile(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

static char *readFile(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = 0;
    fclose(fp);
    return buf;
}

static void checkOptions(const char *dir)
{
    char toml[1024], ini[1024];
    snprintf(toml, sizeof(toml), "%s/rd_crt_config.toml", dir);
    snprintf(ini, sizeof(ini), "%s/rd_crt_none.ini", dir);
    remove(ini);

    writeFile(toml, "version = 1\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    IcoVideoOptions o;
    ico_video_get(&o);
    CHECK(o.crt == 0 && o.crtMode == ICO_CRT_CONSUMER && o.crtStrength == 1.0f &&
              o.crtScanlines < 0.0f && o.crtMask < 0.0f && o.crtHalation < 0.0f &&
              o.crtBloom < 0.0f && o.crtCurvature < 0.0f,
          "options: defaults (off, Consumer TV, 1.0, no overrides)");

    writeFile(toml, "version = 1\n[video]\ncrt = true\ncrt_mode = \"PVM\"\ncrt_strength = 0.5\n"
                    "crt_scanlines = 0.25\ncrt_mask = 2.0\ncrt_halation = 0\n"
                    "crt_bloom = -1\ncrt_curvature = 0.04\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.crt == 1 && o.crtMode == ICO_CRT_PVM && o.crtStrength == 0.5f &&
              o.crtScanlines == 0.25f && o.crtMask == 1.0f && o.crtHalation == 0.0f &&
              o.crtBloom < 0.0f && fabsf(o.crtCurvature - 0.04f) < 1e-6f,
          "options: every key read (mask clamped to 1, bloom -1 = the mode's)");

    writeFile(toml, "version = 1\n[video]\ncrt = true\ncrt_mode = \"vga\"\ncrt_strength = 3\n");
    ico_config_reset(toml, ini);
    ico_video_reload();
    ico_video_get(&o);
    CHECK(o.crtMode == ICO_CRT_CONSUMER && o.crtStrength == 1.0f,
          "options: an unknown mode is Consumer TV, the strength clamped");

    int m = -1;
    CHECK(ico_video_parse_crt_mode("trinitron", &m) == 0 && m == ICO_CRT_TRINITRON &&
              ico_video_parse_crt_mode("Scanlines", &m) == 0 && m == ICO_CRT_SCANLINES &&
              ico_video_parse_crt_mode("crt", &m) != 0 && m == ICO_CRT_SCANLINES,
          "options: the mode parser");
    CHECK(strcmp(ico_video_crt_mode_name(ICO_CRT_PVM), "pvm") == 0 &&
              strcmp(ico_video_crt_mode_name(ICO_CRT_CONSUMER), "consumer") == 0 &&
              strcmp(ico_video_crt_mode_name(9), "consumer") == 0,
          "options: the mode names");

    /* save and read back: the set keys written, an unset override absent */
    IcoVideoOptions p;
    ico_video_defaults(&p);
    p.crt = 1;
    p.crtMode = ICO_CRT_TRINITRON;
    p.crtStrength = 0.7f;
    p.crtHalation = 0.2f;
    ico_video_set(&p);
    writeFile(toml, "version = 1\n");
    ico_config_reset(toml, ini);
    ico_video_set(&p);
    CHECK(ico_video_save() == 0, "options: save");
    const char *text = readFile(toml);
    CHECK(text && strstr(text, "crt = true") && strstr(text, "crt_mode = \"trinitron\"") &&
              strstr(text, "crt_strength = 0.7") && strstr(text, "crt_halation = 0.2") &&
              !strstr(text, "crt_bloom") && !strstr(text, "crt_scanlines"),
          "options: saved keys");
    ico_config_reset(toml, ini);
    ico_video_reload();
    IcoVideoOptions q;
    ico_video_get(&q);
    CHECK(q.crt == 1 && q.crtMode == ICO_CRT_TRINITRON && fabsf(q.crtStrength - 0.7f) < 1e-6f &&
              fabsf(q.crtHalation - 0.2f) < 1e-6f && q.crtBloom < 0.0f,
          "options: read back");
    ico_video_defaults(&q);
    ico_video_set(&q);
    remove(toml);
}

static void checkResolve(void)
{
    RdCrtParams p;
    CHECK(!rd__CrtPreset(RD_CRT_OFF, &p) && !rd__CrtPreset(RD_CRT_MODE_COUNT, &p),
          "resolve: off and unknown have no parameters");
    CHECK(rd__CrtPreset(RD_CRT_SCANLINES, &p) && p.mask == RD_CRT_MASK_NONE &&
              p.scanline == 0.50f && p.curvX == 0.0f && p.halation == 0.0f,
          "resolve: scanlines: no mask, flat, no glow");
    CHECK(rd__CrtPreset(RD_CRT_CONSUMER, &p) && p.mask == RD_CRT_MASK_SLOT && p.curvX > 0.0f &&
              p.curvY > p.curvX && p.gammaIn > p.gammaOut,
          "resolve: consumer: slot mask, curved, gamma 2.4 in");
    CHECK(rd__CrtPreset(RD_CRT_TRINITRON, &p) && p.mask == RD_CRT_MASK_GRILLE && p.curvY == 0.0f,
          "resolve: trinitron: grille, cylindrical");
    RdCrtParams t;
    CHECK(rd__CrtPreset(RD_CRT_PVM, &p) && p.mask == RD_CRT_MASK_GRILLE && p.curvX == 0.0f &&
              rd__CrtPreset(RD_CRT_TRINITRON, &t) && p.maskStrength > t.maskStrength &&
              p.beamMin < t.beamMin,
          "resolve: pvm: grille with darker gaps than the Trinitron's, flat, sharper beam");
    CHECK(rd__CrtPreset(RD_CRT_SHADOW, &p) && p.mask == RD_CRT_MASK_DOTS,
          "resolve: shadow: dot triads");
    CHECK(rd__CrtGapColumns(2.8125f) == 0 && rd__CrtGapColumns(3.0f) == 0 &&
              rd__CrtGapColumns(4.0f) == 1 && rd__CrtGapColumns(5.625f) == 1 &&
              rd__CrtGapColumns(6.0f) == 2,
          "resolve: a gap column from 4 output pixels a source pixel, two from 6");
    CHECK(fabsf(rd__CrtMaskWeight(RD_CRT_MASK_GRILLE, 3.0f, 1.0f, 0.5f, 0.5f, 0, 0)) < 1e-6f &&
              fabsf(rd__CrtMaskWeight(RD_CRT_MASK_GRILLE, 3.0f, 0.4f, 0.5f, 0.5f, 0, 0) - 0.6f) <
                  1e-6f &&
              rd__CrtMaskWeight(RD_CRT_MASK_GRILLE, 3.0f, 0.4f, 0.5f, 0.5f, 0, 1) == 1.0f,
          "resolve: a stripe passes its own channel and leaks 1 - strength of the others");
    CHECK(
        fabsf(rd__CrtTriadGain(RD_CRT_MASK_GRILLE, 3.0f, 0.5f, 10, 0.25f, 0) - 1.5f) < 1e-5f &&
            rd__CrtTriadGain(RD_CRT_MASK_NONE, 3.0f, 0.5f, 10, 0.25f, 0) == 1.0f &&
            fabsf(rd__CrtTriadGain(RD_CRT_MASK_GRILLE, 3.0f, 1.0f, 10, 0.25f, 1) - 3.0f) < 1e-5f &&
            fabsf(rd__CrtTriadGain(RD_CRT_MASK_GRILLE, 4.0f, 1.0f, 10, 0.25f, 2) - 4.0f) < 1e-5f &&
            fabsf(rd__CrtRowGain(RD_CRT_MASK_SLOT, 0.5f) - 1.2f) < 1e-5f &&
            rd__CrtRowGain(RD_CRT_MASK_GRILLE, 0.5f) == 1.0f,
        "resolve: the gains keep the pixel's light");
    {
        /* each triad keeps its pixel's light: a channel's weight times the
           gains, averaged over the output columns its source pixel has and
           over the line, is 1 for every source pixel (at r 2.8125 they have
           2 or 3 columns), each mask, a few r and strengths */
        static const float rs[4] = {2.8125f, 3.0f, 4.5f, 6.0f};
        static const float gaps[2] = {0.4f, 0.6f};
        double worst = 0.0;
        for (int m = RD_CRT_MASK_GRILLE; m <= RD_CRT_MASK_DOTS; m++) {
            for (int i = 0; i < 4; i++) {
                for (int gi = 0; gi < 2; gi++) {
                    for (int sx = 0; sx < 32; sx++) {
                        for (int ch = 0; ch < 3; ch++) {
                            double sum = 0.0;
                            int cnt = 0;
                            const int n = 600;
                            for (int y = 0; y < n; y++) {
                                const float v = (y + 0.5f) / n;
                                const float g = rd__CrtTriadGain(m, rs[i], gaps[gi], sx, v, ch) *
                                                rd__CrtRowGain(m, gaps[gi]);
                                for (int x = (int)(sx * rs[i]) - 1;
                                     x <= (int)((sx + 1) * rs[i]) + 1; x++) {
                                    const float px = (x + 0.5f) / rs[i];
                                    if (x < 0 || (int)floorf(px) != sx) {
                                        continue;
                                    }
                                    sum += rd__CrtMaskWeight(m, rs[i], gaps[gi], px - (float)sx, v,
                                                             sx & 1, ch) *
                                           g;
                                    cnt++;
                                }
                            }
                            const double e = fabs(sum / cnt - 1.0);
                            worst = e > worst ? e : worst;
                        }
                    }
                }
            }
        }
        CHECK(worst < 0.01, "resolve: every triad keeps its pixel's light (worst %.4f)", worst);
    }

    RdSettings s;
    memset(&s, 0, sizeof(s));
    rd_CrtSettings(&s, RD_CRT_SCANLINES, 1.5f);
    CHECK(s.crtMode == RD_CRT_SCANLINES && s.crtStrength == 1.0f && s.crtScanlines < 0.0f &&
              s.crtCurvature < 0.0f,
          "resolve: rd_CrtSettings clamps and clears the overrides");
    s.crtMask = 0.4f;
    s.crtCurvature = 0.02f;
    CHECK(rd__CrtResolve(&s, &p) && p.mask == RD_CRT_MASK_GRILLE && p.maskStrength == 0.4f &&
              p.curvX == 0.02f && fabsf(p.curvY - 0.03f) < 1e-6f,
          "resolve: a mask on the maskless mode is a grille; curvature y half as much again");
    rd_CrtSettings(&s, RD_CRT_TRINITRON, 1.0f);
    s.crtCurvature = 0.05f;
    s.crtScanlines = 0.0f;
    CHECK(rd__CrtResolve(&s, &p) && p.curvX == 0.05f && p.curvY == 0.0f && p.scanline == 0.0f,
          "resolve: the cylindrical face stays flat vertically; 0 is an override");
    CHECK(rd__CrtMaskFade(720) == 0.0f && rd__CrtMaskFade(1080) == 1.0f &&
              rd__CrtMaskFade(1440) == 1.0f && fabsf(rd__CrtMaskFade(900) - 0.5f) < 1e-6f,
          "resolve: the mask fades from 1080 box lines to 720");
}

/* ------------------------------------------------ the frames (on a device) */

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
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

static void sprite(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const uint8_t c[4], int32_t u0,
                   int32_t v0, int32_t u1, int32_t v1)
{
    const int32_t o = (2048 - 256) * 16;
    RdScreenVtx v[2] = {vtx(o + x0, o + y0, c, (float)u0, (float)v0),
                        vtx(o + x1, o + y1, c, (float)u1, (float)v1)};
    rd_ScreenPrims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
}

static uint8_t s_scene[512 * 512 * 4];

/* rd_present_test's rich frame (rd_pixel_test's): noise and gradients, an
 * additive quad, fade, letterbox, brightness, the reduction */
static void makeNoiseScene(void)
{
    for (int i = 0; i < 512 * 512; i++) {
        uint32_t hsh = hash((uint32_t)i * 3 + 1);
        s_scene[i * 4 + 0] = (uint8_t)((i % 512) / 2 + (hsh & 31));
        s_scene[i * 4 + 1] = (uint8_t)((i / 512) / 2 + ((hsh >> 8) & 63));
        s_scene[i * 4 + 2] = (uint8_t)hsh;
        s_scene[i * 4 + 3] = (uint8_t)((hsh >> 24) & 0x7F) + 0x40;
    }
}

/* a flat frame of one colour (0..255 a channel) */
static void makeFlatScene(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 512 * 512; i++) {
        s_scene[i * 4 + 0] = r;
        s_scene[i * 4 + 1] = g;
        s_scene[i * 4 + 2] = b;
        s_scene[i * 4 + 3] = 0x80;
    }
}

static void recordFrame(RdTex t, int rich)
{
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t clr[4] = {0, 0, 0, 0};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), clr, 1, 0);
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
    rd_TestGs(RD_TEST_Z_ALWAYS);
    rd_ZWrite(0);
    rd_Blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_PABE(0);
    rd_FBA(0);
    rd_Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_Texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(0, 0, 512 * 16, 512 * 16, grey, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
    RdPostParams pp;
    if (rich) {
        rd_SelectList(2);
        static const uint8_t add[4] = {60, 30, 90, 0xFF};
        rd_TestGs(RD_TEST_Z_ALWAYS);
        rd_Blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
        rd_TextureOff();
        sprite(100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
        rd_SelectList(11);
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
    }
    rd_SelectList(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = rich ? 120 : 128;
    pp.rgba[2] = rich ? 110 : 128;
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);
}

#define OUT_MAX (1920u * 1440u * 4u)

static uint8_t s_out[OUT_MAX];

/* rd_Init with s, the frame, the present into s_out; false without a device */
static bool present(const RdSettings *s, int rich)
{
    if (!rd_Init(512, 512, s, NULL)) {
        return false;
    }
    RdTex t = rd_CreateTexture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordFrame(t, rich);
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)s->outputWidth * s->outputHeight * 4;
    bool ok = n <= OUT_MAX && rd__ReadPresent(s_out, n, &w, &h) && w == s->outputWidth &&
              h == s->outputHeight;
    CHECK(ok, "readback of the %ux%u present", s->outputWidth, s->outputHeight);
    rd_DestroyTexture(t);
    rd_Shutdown();
    return ok;
}

static RdSettings outputSettings(uint32_t w, uint32_t h)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = w;
    s.outputHeight = h;
    s.aspect = 4.0f / 3.0f;
    s.vsync = 1;
    return s;
}

static bool s_llvmpipe;

/* rd_present_test.c GOLD_PRESENT: the rich frame's 960 x 720 present */
#define GOLD_OFF 0xedb088b74a237351ull

static const char *const kModeName[RD_CRT_MODE_COUNT] = {"off",       "scanlines", "consumer",
                                                         "trinitron", "pvm",       "shadow"};

/* The presents of the rich frame through each mode (llvmpipe, LLVM 19.1.7,
 * this file's frame; package CRT2's phosphors per output pixel, FIX0's leak
 * and per-triad gain): [mode - 1][0]
 * 960 x 720, [1] 1920 x 1440 */
static const uint64_t kGold[RD_CRT_MODE_COUNT - 1][2] = {
    {0xb63d6f980571913bull, 0x06ebfa2d69b427ceull}, /* scanlines */
    {0xe8f8c1f74162712aull, 0xcc091535d9b3bdfcull}, /* consumer */
    {0xf69cf28414987833ull, 0x4482b83647d616ceull}, /* trinitron */
    {0xe5dcb80cb41b8402ull, 0x70ac35d4c4ec372cull}, /* pvm */
    {0x633134534e10b694ull, 0x16355a63829491f3ull}, /* shadow */
};

static double boxLuma(uint32_t w, uint32_t h)
{
    RhiRect b;
    rd__PresentBox(w, h, 4.0f / 3.0f, &b);
    double sum = 0.0;
    for (uint32_t y = (uint32_t)b.y; y < (uint32_t)b.y + b.h; y++) {
        for (uint32_t x = (uint32_t)b.x; x < (uint32_t)b.x + b.w; x++) {
            const uint8_t *p = &s_out[((size_t)y * w + x) * 4];
            sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
        }
    }
    return sum / ((double)b.w * b.h);
}

static void checkOff(void)
{
    makeNoiseScene();
    RdSettings s = outputSettings(960, 720);
    if (!present(&s, 1)) {
        return;
    }
    const uint64_t off = fnv(s_out, 960 * 720 * 4);
    printf("  off: present %016llx\n", (unsigned long long)off);
    if (s_llvmpipe) {
        CHECK(off == GOLD_OFF, "off: the rd_present baseline's bytes");
    }
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdSettings z = s;
        rd_CrtSettings(&z, (RdCrtMode)m, 0.0f);
        if (present(&z, 1)) {
            CHECK(fnv(s_out, 960 * 720 * 4) == off, "off: %s at strength 0 is the plain present",
                  kModeName[m]);
        }
    }
    RdSettings o = s;
    rd_CrtSettings(&o, RD_CRT_OFF, 1.0f);
    if (present(&o, 1)) {
        CHECK(fnv(s_out, 960 * 720 * 4) == off, "off: mode off at strength 1 is the plain present");
    }
}

static void checkModes(void)
{
    static const uint32_t sizes[2][2] = {{960, 720}, {1920, 1440}};
    makeNoiseScene();
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        for (int k = 0; k < 2; k++) {
            RdSettings s = outputSettings(sizes[k][0], sizes[k][1]);
            rd_CrtSettings(&s, (RdCrtMode)m, 1.0f);
            const size_t n = (size_t)sizes[k][0] * sizes[k][1] * 4;
            if (!present(&s, 1)) {
                continue;
            }
            const uint64_t h1 = fnv(s_out, n);
            if (!present(&s, 1)) {
                continue;
            }
            const uint64_t h2 = fnv(s_out, n);
            printf("  modes: %-9s %ux%u %016llx\n", kModeName[m], sizes[k][0], sizes[k][1],
                   (unsigned long long)h1);
            CHECK(h1 == h2, "modes: %s %ux%u deterministic", kModeName[m], sizes[k][0],
                  sizes[k][1]);
            if (s_llvmpipe) {
                CHECK(h1 == kGold[m - 1][k], "modes: %s %ux%u the recorded hash", kModeName[m],
                      sizes[k][0], sizes[k][1]);
            }
        }
    }
}

static void checkOutside(void)
{
    makeNoiseScene();
    const uint32_t w = 1280, h = 720;
    RhiRect b;
    rd__PresentBox(w, h, 4.0f / 3.0f, &b);
    CHECK(b.x == 160 && b.w == 960, "outside: the box is columns 160..1119");
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdSettings s = outputSettings(w, h);
        rd_CrtSettings(&s, (RdCrtMode)m, 1.0f);
        if (!present(&s, 1)) {
            continue;
        }
        uint32_t lit = 0, inside = 0;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                const uint8_t *p = &s_out[((size_t)y * w + x) * 4];
                const int in = (int32_t)x >= b.x && x < (uint32_t)b.x + b.w;
                if (!in && (p[0] | p[1] | p[2])) {
                    lit++;
                }
                if (in && (p[0] | p[1] | p[2])) {
                    inside++;
                }
            }
        }
        CHECK(lit == 0, "outside: %s: %u lit pixels outside the box", kModeName[m], lit);
        CHECK(inside > b.w * b.h / 2, "outside: %s: the box is drawn (%u lit)", kModeName[m],
              inside);
    }
}

static void checkLuma(void)
{
    makeNoiseScene();
    for (int k = 0; k < 2; k++) {
        const uint32_t w = k ? 1920 : 960, h = k ? 1440 : 720;
        RdSettings s = outputSettings(w, h);
        if (!present(&s, 1)) {
            return;
        }
        const double off = boxLuma(w, h);
        rd_CrtSettings(&s, RD_CRT_SCANLINES, 1.0f);
        if (!present(&s, 1)) {
            return;
        }
        const double crt = boxLuma(w, h);
        printf("  luma: %ux%u off %.2f scanlines %.2f (%.1f %%)\n", w, h, off, crt,
               100.0 * crt / off);
        CHECK(crt > off * 0.8 && crt < off * 1.2, "luma: scanlines within 20 %% at %ux%u", w, h);
    }
}

/* ------------------------------------------ the phosphors (package CRT2) */

static uint32_t s_w; /* the last present's output width */
static RhiRect s_box;

/* a flat frame (r, g, b) through mode at a w x h output, the glow, the
 * curvature (and so the corners) off, the mask at strength mask (< 0: the
 * mode's); the beam flat when flat is set (crt_scanlines 0) */
static bool phosphors(RdCrtMode mode, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b,
                      int flat, float mask)
{
    makeFlatScene(r, g, b);
    RdSettings s = outputSettings(w, h);
    rd_CrtSettings(&s, mode, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    s.crtMask = mask; /* < 0: the mode's own strength */
    if (flat) {
        s.crtScanlines = 0.0f;
    }
    s_w = w;
    rd__PresentBox(w, h, 4.0f / 3.0f, &s_box);
    return present(&s, 0);
}

static const uint8_t *at(uint32_t bx, uint32_t by)
{
    return &s_out[((size_t)(s_box.y + by) * s_w + s_box.x + bx) * 4];
}

/* the box pixel (bx, by)'s position in its source pixel and line, as
 * crt_ps computes it (curvature off): source x, f, v, the column's parity */
static void posOf(uint32_t bx, uint32_t by, float *f, float *v, int *odd)
{
    const float sx = ((float)bx + 0.5f) / (float)s_box.w * 512.0f;
    const float sy = ((float)by + 0.5f) / (float)s_box.h * 256.0f;
    *f = sx - floorf(sx);
    *v = sy - floorf(sy);
    *odd = (int)floorf(sx) & 1;
}

/* the channel the mask lights at a box pixel (0..2), -1 in a gap or a
 * slot's bridge, -2 too near a stripe's edge to tell (float rounding) */
static int litChannel(RdCrtParams *p, uint32_t bx, uint32_t by)
{
    float f, v;
    int odd;
    posOf(bx, by, &f, &v, &odd);
    const float r = (float)s_box.w / 512.0f;
    const float u =
        (p->mask == RD_CRT_MASK_DOTS && v >= 0.5f ? f + 1.0f / 3.0f - floorf(f + 1.0f / 3.0f) : f) *
        3.0f;
    if (fabsf(u - roundf(u)) < 1e-3f || fabsf(v - 0.5f) < 1e-3f) {
        return -2;
    }
    int lit = -1;
    for (int c = 0; c < 3; c++) {
        if (rd__CrtMaskWeight(p->mask, r, p->maskStrength, f, v, odd, c) == 1.0f) {
            lit = c;
        }
    }
    return lit;
}

static void checkPhosphors(void)
{
    RdCrtParams p;
    /* 1440 x 1080 (1920 x 1080), Trinitron, white: every box pixel one
       channel, the expected one, each source pixel's columns R, G, B in
       order */
    if (phosphors(RD_CRT_TRINITRON, 1920, 1080, 0xFF, 0xFF, 0xFF, 0, 1.0f)) {
        rd__CrtPreset(RD_CRT_TRINITRON, &p);
        p.maskStrength = 1.0f;
        uint32_t impure = 0, wrong = 0, order = 0, checked = 0;
        /* away from the rounded corners (0.02 of the height) */
        for (uint32_t y = 30; y + 30 < s_box.h; y += 7) {
            int prevCh = -1, prevSx = -1;
            for (uint32_t x = 0; x < s_box.w; x++) {
                const uint8_t *c = at(x, y);
                int n = 0, ch = -1;
                for (int k = 0; k < 3; k++) {
                    if (c[k] > 1) {
                        n++;
                        ch = k;
                    }
                }
                impure += n > 1;
                const int want = litChannel(&p, x, y);
                const int sx = (int)(((float)x + 0.5f) / (float)s_box.w * 512.0f);
                /* the reduction's 2-pixel black border is no phosphor */
                if (want >= -1 && sx >= 3 && sx < 509) {
                    wrong += n != 1 || ch != want;
                    checked++;
                }
                if (n == 1) {
                    order += sx == prevSx && ch <= prevCh;
                    prevCh = ch;
                    prevSx = sx;
                }
            }
        }
        printf("  phosphors: 1440x1080 trinitron white: %u pixels checked, %u impure, %u not the "
               "expected channel, %u out of R, G, B order\n",
               checked, impure, wrong, order);
        CHECK(impure == 0 && wrong == 0 && order == 0 && checked > 100000,
              "phosphors: 1440x1080: every box pixel one channel of its source pixel, in order");
    }
    /* 1536 x 1152, Trinitron, red: only each pixel's first column lights */
    if (phosphors(RD_CRT_TRINITRON, 1536, 1152, 0xFF, 0, 0, 0, 1.0f)) {
        int pure = 1;
        for (uint32_t y = 300; y < 340; y++) {
            for (uint32_t x = 3 * 200; x < 3 * 220; x++) {
                const uint8_t *c = at(x, y);
                pure &= x % 3 == 0 ? c[0] > 100 && c[1] <= 1 && c[2] <= 1
                                   : c[0] <= 1 && c[1] <= 1 && c[2] <= 1;
            }
        }
        CHECK(pure, "phosphors: 1536x1152 trinitron: a red pixel lights only its R column");
    }
    /* 1536 x 1152, Trinitron and PVM, white: columns R, G, B */
    for (int m = RD_CRT_TRINITRON; m <= RD_CRT_PVM; m++) {
        if (!phosphors((RdCrtMode)m, 1536, 1152, 0xFF, 0xFF, 0xFF, 0, 1.0f)) {
            continue;
        }
        int stripes = 1;
        for (uint32_t y = 300; y < 340; y++) {
            for (uint32_t x = 3 * 200; x < 3 * 220; x++) {
                const uint8_t *c = at(x, y);
                for (int k = 0; k < 3; k++) {
                    stripes &= (int)(x % 3) == k ? c[k] > 100 : c[k] <= 1;
                }
            }
        }
        CHECK(stripes, "phosphors: 1536x1152 %s: a white pixel lights R, G, B columns",
              kModeName[m]);
    }
    /* Consumer TV at its own strength, a mid grey (no clipping), the beam
       flat: the bridges (the last third of a line, half a line later in odd
       columns) are darker than the slots by (1 - strength) in linear light,
       at the expected rows; each stripe its channel in full and the other
       two leaking (1 - strength) */
    if (phosphors(RD_CRT_CONSUMER, 1536, 1152, 0x60, 0x60, 0x60, 1, -1.0f)) {
        rd__CrtPreset(RD_CRT_CONSUMER, &p);
        const float want = powf(1.0f - p.maskStrength, 1.0f / 2.2f);
        uint32_t bad = 0, bridges = 0, n = 0;
        for (uint32_t y = 300; y < 360; y++) {
            for (uint32_t x = 3 * 200; x < 3 * 204; x++) {
                float f, v;
                int odd;
                posOf(x, y, &f, &v, &odd);
                const float vv = v + (odd ? 0.5f : 0.0f) - floorf(v + (odd ? 0.5f : 0.0f));
                if (fabsf(vv - 2.0f / 3.0f) < 0.05f || vv > 0.97f || vv < 0.03f) {
                    continue; /* a row on a bridge's edge */
                }
                const int k = (int)(x % 3);
                const uint8_t lit = at(x - x % 3 + (uint32_t)k, y)[k];
                /* the same stripe in the other parity's column: the
                   neighbour source pixel, 3 output pixels over */
                const int bridge = vv >= 2.0f / 3.0f;
                const uint8_t other = at(x + 3, y)[k];
                float fo, vo;
                int oddo;
                posOf(x + 3, y, &fo, &vo, &oddo);
                const float vvo = vo + (oddo ? 0.5f : 0.0f) - floorf(vo + (oddo ? 0.5f : 0.0f));
                if (fabsf(vvo - 2.0f / 3.0f) < 0.05f || vvo > 0.97f || vvo < 0.03f) {
                    continue;
                }
                const int bridgeO = vvo >= 2.0f / 3.0f;
                n++;
                bridges += bridge;
                if (bridge != bridgeO) {
                    /* one in a bridge, one in a slot: the bridge darker by
                       (1 - strength)^(1 / 2.2) */
                    const float ratio =
                        bridge ? (float)lit / (float)other : (float)other / (float)lit;
                    bad += fabsf(ratio - want) > 0.05f;
                } else {
                    bad += abs((int)lit - (int)other) > 3;
                }
                /* the next stripe's channel leaks through this stripe */
                const float leak = (float)at(x, y)[(k + 1) % 3] / (float)at(x, y)[k];
                bad += fabsf(leak - want) > 0.05f;
            }
        }
        printf("  phosphors: consumer: %u pixels, %u in bridges, %u off\n", n, bridges, bad);
        CHECK(bad == 0 && bridges > n / 5 && bridges < n / 2,
              "phosphors: consumer: dark bridges, staggered half a line between columns");
    }
    /* Trinitron at its own strength (the leak), a mid grey, the beam flat,
       at 1536 x 1152 (3 output pixels a source pixel): each stripe shows
       its channel in full and the other two at (1 - strength) of it in
       linear light */
    if (phosphors(RD_CRT_TRINITRON, 1536, 1152, 0x60, 0x60, 0x60, 1, -1.0f)) {
        rd__CrtPreset(RD_CRT_TRINITRON, &p);
        const float want = powf(1.0f - p.maskStrength, 1.0f / 2.2f);
        float lo = 9.0f, hi = 0.0f;
        uint32_t own = 0;
        for (uint32_t y = 300; y < 340; y++) {
            for (uint32_t x = 3 * 200; x < 3 * 220; x++) {
                const uint8_t *c = at(x, y);
                const int k = (int)(x % 3);
                own += c[k] < c[(k + 1) % 3] || c[k] < c[(k + 2) % 3];
                for (int j = 1; j < 3; j++) {
                    const float ratio = (float)c[(k + j) % 3] / (float)c[k];
                    lo = ratio < lo ? ratio : lo;
                    hi = ratio > hi ? ratio : hi;
                }
            }
        }
        printf("  phosphors: trinitron at strength %.2f: the leak %.3f..%.3f (want %.3f)\n",
               p.maskStrength, lo, hi, want);
        CHECK(own == 0 && fabsf(lo - want) < 0.03f && fabsf(hi - want) < 0.03f,
              "phosphors: trinitron at its own strength: the other channels leak (1 - strength)");
    }
    /* Trinitron at its own strength at 1440 x 1080 (2.81 output pixels a
       source pixel: triads of 2 and 3 columns), a mid grey, the beam flat:
       every source pixel's columns keep its light, channel by channel, so
       the triads short of a stripe do not tint in bands */
    if (phosphors(RD_CRT_TRINITRON, 1920, 1080, 0x60, 0x60, 0x60, 1, -1.0f)) {
        const double in = pow(0x60 / 255.0, 2.2);
        double worst = 0.0;
        for (uint32_t y = 500; y < 580; y += 3) {
            double sum[3] = {0, 0, 0};
            int cnt = 0, cur = -1;
            /* whole source pixels only: from the first one starting after
               column 520 */
            const int first = (int)((520.5f) / (float)s_box.w * 512.0f) + 1;
            for (uint32_t x = 520; x < 920; x++) {
                const int sx = (int)(((float)x + 0.5f) / (float)s_box.w * 512.0f);
                if (sx < first) {
                    continue;
                }
                if (sx != cur) {
                    if (cnt > 0) {
                        for (int k = 0; k < 3; k++) {
                            const double e = fabs(sum[k] / cnt / in - 1.0);
                            worst = e > worst ? e : worst;
                        }
                    }
                    sum[0] = sum[1] = sum[2] = 0.0;
                    cnt = 0;
                    cur = sx;
                }
                const uint8_t *c = at(x, y);
                for (int k = 0; k < 3; k++) {
                    sum[k] += pow(c[k] / 255.0, 2.2);
                }
                cnt++;
            }
        }
        printf("  phosphors: 1440x1080 trinitron: each triad's light within %.1f %%\n",
               100.0 * worst);
        CHECK(worst < 0.04, "phosphors: 1440x1080: every triad keeps its pixel's light (%.3f)",
              worst);
    }
    /* Shadow mask, white: the first half of a line R, G, B; the second the
       row of dots half a triad over (the expected channel everywhere) */
    if (phosphors(RD_CRT_SHADOW, 1536, 1152, 0xFF, 0xFF, 0xFF, 0, 1.0f)) {
        rd__CrtPreset(RD_CRT_SHADOW, &p);
        p.maskStrength = 1.0f;
        uint32_t wrong = 0, shifted = 0;
        for (uint32_t y = 300; y < 340; y++) {
            for (uint32_t x = 3 * 200; x < 3 * 220; x++) {
                const int want = litChannel(&p, x, y);
                if (want < 0) {
                    continue;
                }
                const uint8_t *c = at(x, y);
                for (int k = 0; k < 3; k++) {
                    wrong += k == want ? c[k] <= 100 : c[k] > 1;
                }
                shifted += want != (int)(x % 3);
            }
        }
        CHECK(wrong == 0 && shifted > 0,
              "phosphors: shadow: two rows of dots, the second half a triad over (%u wrong, %u "
              "shifted)",
              wrong, shifted);
    }
    /* Scanlines: no mask: every pixel of a row of a flat frame the same
       grey; the beam brightest mid-line */
    if (phosphors(RD_CRT_SCANLINES, 1536, 1152, 0xFF, 0xFF, 0xFF, 0, -1.0f)) {
        int flat = 1;
        for (uint32_t y = 300; y < 340; y++) {
            for (uint32_t x = 600; x < 660; x++) {
                const uint8_t *c = at(x, y), *c0 = at(600, y);
                flat &= c[0] == c0[0] && c[1] == c0[1] && c[2] == c0[2] && c[0] == c[1];
            }
        }
        CHECK(flat, "phosphors: scanlines: no column structure");
    }
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    checkOptions(dir);
    checkResolve();
    RdSettings st = outputSettings(64, 48);
    if (!rd_Init(512, 512, &st, NULL)) {
        if (failures) {
            printf("rd_crt_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_crt_test: options ok; SKIP the pixel checks: no usable device\n");
        return 77;
    }
    s_llvmpipe = strstr(rhi_AdapterName(), "llvmpipe") != NULL;
    printf("rd_crt_test: adapter %s\n", rhi_AdapterName());
    rd_Shutdown();
    checkOff();
    checkModes();
    checkOutside();
    checkLuma();
    checkPhosphors();
    if (failures) {
        printf("rd_crt_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_crt_test: ok\n");
    return 0;
}
