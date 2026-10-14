/* rd_crt_test.c: the CRT filter (port/render/rd_crt.c,
 * port/shaders/crt.hlsl).
 *
 * Without a device:
 *   options   [video] crt, crt_mode, crt_strength and the five overrides
 *             from a config.toml, the defaults, the mode parser and names,
 *             out-of-range values, save and read back
 *   resolve   the modes' table, the overrides (a mask on the maskless
 *             mode, the cylindrical face keeping y flat), the mask's fade
 *             with the box height, rd_crt_settings
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
 *   phosphors the mask per output pixel, glow and
 *             curvature off: at a 1440 x 1080 box (2.81 output pixels a
 *             source pixel) under a grey frame every box pixel is one
 *             channel only, the one rd__crt_mask_weight gives its position,
 *             and each source pixel's columns run R, G, B in order; at a
 *             1536 x 1152 box (3 a pixel) a red frame in Trinitron lights
 *             only each pixel's first column, a grey one R, G, B columns;
 *             Consumer TV's slot bridges are dark and the odd columns' half
 *             a line off the even ones'; the shadow mask's second row of
 *             dots is half a triad over; the scanlines mode has no column
 *             structure (the geometry at mask strength 1: pure stripes);
 *             at the modes' own strengths each stripe leaks (1 - strength)
 *             of the other channels (Trinitron, Consumer TV) and at 1440 x
 *             1080 every triad keeps its pixel's light; the gap columns,
 *             the leak and the gains (each triad's light integrated)
 *   light     white and light colours (pink, sky, cream) in
 *             each mode as played (glow, curvature, vignette) at a 1440 x
 *             1080 box: no channel brighter than its input in the mode's
 *             linear light, the hue kept, the mask still in a white
 *   top layer rd_set_present_overlay_top under the scanlines:
 *             its ctx the output, its solid quad drawn after the filter
 *             (every pixel its flat colour), the main overlay's quad still
 *             in the filtered picture (its rows vary)
 *   wide mask Full pixel off in the game (the border mask) on a 688 frame
 *             at 16:9: the bands 2 of the grid's 682 columns and 8 of 256
 *             lines, black, the on picture inside them (at 4:3, the full
 *             pixel check: no mask, plain off's present byte for byte)
 *   wide crop a 688 frame at 16:9: the grid 682 columns, the 3 a
 *             side past the exact width never in the box (white there, all
 *             black), columns 3 and 684 at its edges; under Trinitron's
 *             grille at strength 1 a row's columns light the channels of the
 *             4:3 run's at the same 3 output pixels a source pixel
 * Without a device, also:
 *   wide grid the grid at 512, 688 (16:9), 1376 (32:9), 2560 (60:9): the
 *             columns cropped with Full pixel on and off (0, 3, 5, 0)
 *             and the grid's width, the films'
 *             512, and the filter's box of the grid's shape: output pixels
 *             a source pixel the 4:3 run's at 768 and 1152 lines (the 4:3
 *             box itself at 512)
 *   highlights the CPU model of crt_ps over a flat field
 *             (rd__crt_beam, the mask's weights and gains, rd__crt_strength_at,
 *             rd__crt_triad_top, rd__crt_gain_fade, rd__crt_glow_mix,
 *             rd__crt_shoulder): white, light colours and a mid grey in
 *             every mode at 2.81, 3 and 2 output pixels a source pixel
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
    CHECK(!rd__crt_preset(RD_CRT_OFF, &p) && !rd__crt_preset(RD_CRT_MODE_COUNT, &p),
          "resolve: off and unknown have no parameters");
    CHECK(rd__crt_preset(RD_CRT_SCANLINES, &p) && p.mask == RD_CRT_MASK_NONE &&
              p.scanline == 0.50f && p.curvX == 0.0f && p.halation == 0.0f,
          "resolve: scanlines: no mask, flat, no glow");
    CHECK(rd__crt_preset(RD_CRT_CONSUMER, &p) && p.mask == RD_CRT_MASK_SLOT && p.curvX > 0.0f &&
              p.curvY > p.curvX && p.gammaIn > p.gammaOut,
          "resolve: consumer: slot mask, curved, gamma 2.4 in");
    CHECK(rd__crt_preset(RD_CRT_TRINITRON, &p) && p.mask == RD_CRT_MASK_GRILLE && p.curvY == 0.0f,
          "resolve: trinitron: grille, cylindrical");
    RdCrtParams t;
    CHECK(rd__crt_preset(RD_CRT_PVM, &p) && p.mask == RD_CRT_MASK_GRILLE && p.curvX == 0.0f &&
              rd__crt_preset(RD_CRT_TRINITRON, &t) && p.maskStrength > t.maskStrength &&
              p.beamMin < t.beamMin,
          "resolve: pvm: grille with darker gaps than the Trinitron's, flat, sharper beam");
    CHECK(rd__crt_preset(RD_CRT_SHADOW, &p) && p.mask == RD_CRT_MASK_DOTS,
          "resolve: shadow: dot triads");
    CHECK(rd__crt_gap_columns(2.8125f) == 0 && rd__crt_gap_columns(3.0f) == 0 &&
              rd__crt_gap_columns(4.0f) == 1 && rd__crt_gap_columns(5.625f) == 1 &&
              rd__crt_gap_columns(6.0f) == 2,
          "resolve: a gap column from 4 output pixels a source pixel, two from 6");
    CHECK(fabsf(rd__crt_mask_weight(RD_CRT_MASK_GRILLE, 3.0f, 1.0f, 0.5f, 0.5f, 0, 0)) < 1e-6f &&
              fabsf(rd__crt_mask_weight(RD_CRT_MASK_GRILLE, 3.0f, 0.4f, 0.5f, 0.5f, 0, 0) - 0.6f) <
                  1e-6f &&
              rd__crt_mask_weight(RD_CRT_MASK_GRILLE, 3.0f, 0.4f, 0.5f, 0.5f, 0, 1) == 1.0f,
          "resolve: a stripe passes its own channel and leaks 1 - strength of the others");
    CHECK(fabsf(rd__crt_triad_gain(RD_CRT_MASK_GRILLE, 3.0f, 0.5f, 10, 0.25f, 0) - 1.5f) < 1e-5f &&
              rd__crt_triad_gain(RD_CRT_MASK_NONE, 3.0f, 0.5f, 10, 0.25f, 0) == 1.0f &&
              fabsf(rd__crt_triad_gain(RD_CRT_MASK_GRILLE, 3.0f, 1.0f, 10, 0.25f, 1) - 3.0f) <
                  1e-5f &&
              fabsf(rd__crt_triad_gain(RD_CRT_MASK_GRILLE, 4.0f, 1.0f, 10, 0.25f, 2) - 4.0f) <
                  1e-5f &&
              fabsf(rd__crt_row_gain(RD_CRT_MASK_SLOT, 0.5f) - 1.2f) < 1e-5f &&
              rd__crt_row_gain(RD_CRT_MASK_GRILLE, 0.5f) == 1.0f,
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
                                const float g = rd__crt_triad_gain(m, rs[i], gaps[gi], sx, v, ch) *
                                                rd__crt_row_gain(m, gaps[gi]);
                                for (int x = (int)(sx * rs[i]) - 1;
                                     x <= (int)((sx + 1) * rs[i]) + 1; x++) {
                                    const float px = (x + 0.5f) / rs[i];
                                    if (x < 0 || (int)floorf(px) != sx) {
                                        continue;
                                    }
                                    sum += rd__crt_mask_weight(m, rs[i], gaps[gi], px - (float)sx,
                                                               v, sx & 1, ch) *
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
    rd_crt_settings(&s, RD_CRT_SCANLINES, 1.5f);
    CHECK(s.crtMode == RD_CRT_SCANLINES && s.crtStrength == 1.0f && s.crtScanlines < 0.0f &&
              s.crtCurvature < 0.0f,
          "resolve: rd_crt_settings clamps and clears the overrides");
    s.crtMask = 0.4f;
    s.crtCurvature = 0.02f;
    CHECK(rd__crt_resolve(&s, &p) && p.mask == RD_CRT_MASK_GRILLE && p.maskStrength == 0.4f &&
              p.curvX == 0.02f && fabsf(p.curvY - 0.03f) < 1e-6f,
          "resolve: a mask on the maskless mode is a grille; curvature y half as much again");
    rd_crt_settings(&s, RD_CRT_TRINITRON, 1.0f);
    s.crtCurvature = 0.05f;
    s.crtScanlines = 0.0f;
    CHECK(rd__crt_resolve(&s, &p) && p.curvX == 0.05f && p.curvY == 0.0f && p.scanline == 0.0f,
          "resolve: the cylindrical face stays flat vertically; 0 is an override");
    CHECK(rd__crt_mask_fade(720) == 0.0f && rd__crt_mask_fade(1080) == 1.0f &&
              rd__crt_mask_fade(1440) == 1.0f && fabsf(rd__crt_mask_fade(900) - 0.5f) < 1e-6f,
          "resolve: the mask fades from 1080 box lines to 720");
}

/* -------------------------------------------------------- the highlights */

static const char *const kModeName[RD_CRT_MODE_COUNT] = {"off",       "scanlines", "consumer",
                                                         "trinitron", "pvm",       "shadow"};

/* The CPU model of crt_ps over a flat field (every source pixel rgb, the
 * filter at strength 1, no curvature, the vignette's centre): the beam of
 * the line and its two neighbours, the mask with its gains and their fade,
 * the glow (a flat field's glow is the field), the shoulder.  out the mean
 * colour of the box in the output's linear light (decoded with the mode's
 * gamma out), flat the fraction of output pixels whose three channels are
 * all 0.98 or more (a white with no mask left in it). */
static void flatModel(const RdCrtParams *p, const uint8_t rgb[3], float r, float fade,
                      double out[3], double *flat)
{
    float lin[3];
    for (int c = 0; c < 3; c++) {
        lin[c] = powf(rgb[c] / 255.0f, p->gammaIn);
    }
    const float ms = p->mask != RD_CRT_MASK_NONE ? rd__crt_strength_at(p->maskStrength, lin) : 0.0f;
    double sum[3] = {0, 0, 0};
    long n = 0, white = 0;
    const int lines = 120;
    for (int sx = 0; sx < 32; sx++) {
        for (int y = 0; y < lines; y++) {
            const float v = (y + 0.5f) / (float)lines, d = v - 0.5f;
            for (int x = (int)floorf(sx * r) - 1; x <= (int)ceilf((sx + 1) * r) + 1; x++) {
                const float fx = (x + 0.5f) / r;
                if (x < 0 || (int)floorf(fx) != sx) {
                    continue;
                }
                float col[3], gain[3], top[3];
                for (int c = 0; c < 3; c++) {
                    const float beam = rd__crt_beam(lin[c], d, p->beamMin, p->beamMax) +
                                       rd__crt_beam(lin[c], d + 1.0f, p->beamMin, p->beamMax) +
                                       rd__crt_beam(lin[c], 1.0f - d, p->beamMin, p->beamMax);
                    col[c] = lin[c] + (beam - lin[c]) * p->scanline;
                    gain[c] = rd__crt_triad_gain(p->mask, r, ms, sx, v, c) *
                              rd__crt_row_gain(p->mask, ms);
                    top[c] = rd__crt_triad_top(p->mask, r, ms, sx, v, c);
                }
                if (p->mask != RD_CRT_MASK_NONE && fade > 0.0f) {
                    const float t = rd__crt_gain_fade(col, gain, top, fade);
                    for (int c = 0; c < 3; c++) {
                        const float m =
                            rd__crt_mask_weight(p->mask, r, ms, fx - (float)sx, v, sx & 1, c) *
                            (1.0f + (gain[c] - 1.0f) * t);
                        col[c] *= 1.0f + (m - 1.0f) * fade;
                    }
                }
                rd__crt_glow_mix(col, lin, lin, p->halation, p->bloom, 1.0f);
                rd__crt_shoulder(col, 1.0f);
                int all = 1;
                for (int c = 0; c < 3; c++) {
                    col[c] = col[c] > 1.0f ? 1.0f : col[c];
                    /* through the output's 8 bits and back */
                    const float q = roundf(powf(col[c], 1.0f / p->gammaOut) * 255.0f) / 255.0f;
                    sum[c] += powf(q, p->gammaOut);
                    all &= q >= 0.98f;
                }
                white += all;
                n++;
            }
        }
    }
    for (int c = 0; c < 3; c++) {
        out[c] = sum[c] / (double)n;
    }
    *flat = (double)white / (double)n;
}

static double lumaOf(const double c[3])
{
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
}

/* White and light colours through every mode's model: no channel comes out
 * brighter than it went in (in the mode's own linear light, so Consumer
 * TV's gamma 2.4 in is part of the input), a hue keeps its channels'
 * ratios, the mask stays in a white (no flat white; at 1080 lines and up,
 * where the mask is drawn), a white is not left dim, and a mid grey keeps
 * its light. */
static void checkHighlights(void)
{
    static const uint8_t kIn[6][3] = {
        {255, 255, 255}, /* white */
        {255, 204, 204}, /* pink */
        {204, 229, 255}, /* sky */
        {255, 255, 204}, /* cream */
        {230, 230, 230}, /* light grey */
        {0x60, 0x60, 0x60},
    };
    static const float rs[3] = {2.8125f, 3.0f, 2.0f}; /* 1440, 1536, 1024 a 512 grid */
    double worstOver = 0.0, worstHue = 0.0, worstFlat = 0.0, dimmest = 9.0, worstMid = 0.0;
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdCrtParams p;
        rd__crt_preset((RdCrtMode)m, &p);
        for (int k = 0; k < 3; k++) {
            const float fade = k == 2 ? 0.0f : 1.0f;
            for (int i = 0; i < 6; i++) {
                double out[3], flat;
                flatModel(&p, kIn[i], rs[k], fade, out, &flat);
                double lin[3];
                for (int c = 0; c < 3; c++) {
                    lin[c] = pow(kIn[i][c] / 255.0, p.gammaIn);
                    const double over = out[c] / lin[c] - 1.0;
                    worstOver = over > worstOver ? over : worstOver;
                }
                for (int c = 1; c < 3; c++) {
                    const double e = fabs((out[c] / out[0]) / (lin[c] / lin[0]) - 1.0);
                    worstHue = e > worstHue ? e : worstHue;
                }
                if (i == 0) {
                    printf("  highlights: %-9s r %.2f fade %.0f: white out %.3f %.3f %.3f, %.1f %% "
                           "flat white\n",
                           kModeName[m], rs[k], fade, out[0], out[1], out[2], 100.0 * flat);
                    dimmest = lumaOf(out) < dimmest ? lumaOf(out) : dimmest;
                    if (p.mask != RD_CRT_MASK_NONE && fade > 0.0f) {
                        worstFlat = flat > worstFlat ? flat : worstFlat;
                    }
                }
                if (i == 5) {
                    const double e = fabs(lumaOf(out) / lumaOf(lin) - 1.0);
                    worstMid = e > worstMid ? e : worstMid;
                }
            }
        }
    }
    printf("  highlights: worst over %.2f %%, hue %.2f %%, flat white %.1f %%, dimmest white "
           "%.3f, mid grey %.2f %%\n",
           100.0 * worstOver, 100.0 * worstHue, 100.0 * worstFlat, dimmest, 100.0 * worstMid);
    CHECK(worstOver < 0.01, "highlights: no channel brighter than its input (worst %.3f)",
          worstOver);
    CHECK(worstHue < 0.03, "highlights: light colours keep their hue (worst %.3f)", worstHue);
    CHECK(worstFlat < 0.02, "highlights: the mask stays in a white (worst %.3f flat)", worstFlat);
    CHECK(dimmest > 0.7, "highlights: a white is not left dim (%.3f)", dimmest);
    CHECK(worstMid < 0.04, "highlights: a mid grey keeps its light (worst %.3f)", worstMid);
    float a[3] = {1.0f, 0.5f, 0.25f};
    rd__crt_shoulder(a, 1.0f);
    float b[3] = {1.6f, 0.8f, 0.4f};
    rd__crt_shoulder(b, 1.0f);
    float g[3] = {0.5f, 0.5f, 0.5f};
    rd__crt_shoulder(g, 1.0f);
    CHECK(a[0] < 1.0f && a[0] > 0.95f && fabsf(a[1] / a[0] - 0.5f) < 1e-5f && b[0] < 1.0f &&
              fabsf(b[2] / b[0] - 0.25f) < 1e-5f && g[0] == 0.5f,
          "highlights: the shoulder rolls the brightest channel off below 1, keeps the ratios, "
          "leaves the mid-tones");
    const float col[3] = {0.8f, 0.5f, 0.2f}, gain[3] = {1.5f, 1.5f, 1.5f}, one[3] = {1, 1, 1};
    const float t = rd__crt_gain_fade(col, gain, one, 1.0f);
    /* a triad short of its red stripe: red's top is the leak (0.6), its
       gain 1 / 0.6, so red never passes its level and does not hold the
       others back */
    const float shortG[3] = {1.0f / 0.6f, 1.25f, 1.25f}, shortT[3] = {0.6f, 1.0f, 1.0f};
    CHECK(fabsf(0.8f * (1.0f + 0.5f * t) - 1.0f) < 1e-5f &&
              rd__crt_gain_fade((const float[3]){0.2f, 0.2f, 0.2f}, gain, one, 1.0f) == 1.0f &&
              rd__crt_gain_fade((const float[3]){1.0f, 0.2f, 0.2f}, gain, one, 1.0f) == 0.0f &&
              rd__crt_gain_fade((const float[3]){0.7f, 0.7f, 0.7f}, shortG, shortT, 1.0f) == 1.0f,
          "highlights: the gains stop where the brightest lit stripe reaches 1");
    CHECK(rd__crt_triad_top(RD_CRT_MASK_GRILLE, 3.0f, 0.4f, 10, 0.25f, 0) == 1.0f &&
              rd__crt_triad_top(RD_CRT_MASK_NONE, 3.0f, 0.4f, 10, 0.25f, 0) == 1.0f,
          "highlights: a whole triad's top is its own stripe");
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
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, 0);
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
    rd_begin_frame();
    rd_select_list(0);
    rd_clear_target(rd_target(RD_TARGET_SCENE), clr, 1, 0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(t, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    sprite(0, 0, 512 * 16, 512 * 16, grey, 8, 8, 512 * 16 + 8, 512 * 16 + 8);
    RdPostParams pp;
    if (rich) {
        rd_select_list(2);
        static const uint8_t add[4] = {60, 30, 90, 0xFF};
        rd_test_gs(RD_TEST_Z_ALWAYS);
        rd_blend(RD_BLEND_CS_AS_ADD_CD, 0x80, 1);
        rd_texture_off();
        sprite(100 * 16, 100 * 16, 300 * 16, 200 * 16, add, 0, 0, 0, 0);
        rd_select_list(11);
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
    }
    rd_select_list(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 128;
    pp.rgba[1] = rich ? 120 : 128;
    pp.rgba[2] = rich ? 110 : 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

#define OUT_MAX (1920u * 1440u * 4u)

static uint8_t s_out[OUT_MAX];

/* the RdBlendExact the last present() drew with (rd_dst_read.c): kGold
 * is the fixed-function blend's (rd_crt_fixed), kGoldExact the attachment
 * read's */
static int s_presentExact;

/* rd_init with s, the frame, the present into s_out; false without a device */
static bool present(const RdSettings *s, int rich)
{
    if (!rd_init(512, 512, s, NULL)) {
        return false;
    }
    RdTex t = rd_create_texture(512, 512, s_scene, RD_TEXA_80_80, "scene");
    recordFrame(t, rich);
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)s->outputWidth * s->outputHeight * 4;
    bool ok = n <= OUT_MAX && rd__read_present(s_out, n, &w, &h) && w == s->outputWidth &&
              h == s->outputHeight;
    CHECK(ok, "readback of the %ux%u present", s->outputWidth, s->outputHeight);
    s_presentExact = (int)rd_blend_exact();
    rd_destroy_texture(t);
    rd_shutdown();
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

/* rd_present_test.c GOLD_PRESENT: the rich frame's 960 x 720 present;
 * GOLD_PRESENT_EXACT under the exact blend's attachment read */
#define GOLD_OFF 0x9089548e05b4f69eull
#define GOLD_OFF_EXACT 0xea894ace64f1d717ull

/* The presents of the rich frame through each mode (llvmpipe, LLVM 19.1.7,
 * this file's frame; the phosphors per output pixel, the leak and the
 * per-triad gain; the flat face counted by column edges, which moves PVM's
 * columns on a stripe's edge at 1920 x 1440; the highlights: the glow
 * mixed in, the gains' fade, the eased strength, the shoulder):
 * [mode - 1][0] 960 x 720, [1] 1920 x 1440; recorded again for the GS
 * bilinear as PCSX2 filters (gs_bilerp: the reduction's DISPLAY changed) */
static const uint64_t kGold[RD_CRT_MODE_COUNT - 1][2] = {
    {0x3fe07f0f8ee07719ull, 0x40b965bc9c07ed48ull}, /* scanlines */
    {0x6ed5d89b6db79b69ull, 0xfb155a427b37158dull}, /* consumer */
    {0x6a621fbc9e86b891ull, 0x839c2f37bbd43b15ull}, /* trinitron */
    {0xd90eb34208079b34ull, 0x8f6664aef9c91ceeull}, /* pvm */
    {0xfa878669b0bacab0ull, 0x44a618ae9e1fa647ull}, /* shadow */
};

/* the same under the exact GS blend's attachment read (rd_dst_read.c;
 * llvmpipe, LLVM 19.1.7, Mesa 25.0.7); kGold stays the fixed-function
 * blend's (rd_crt_fixed) */
static const uint64_t kGoldExact[RD_CRT_MODE_COUNT - 1][2] = {
    {0x41c3b784485e8d78ull, 0x858c33d2f4f17880ull}, /* scanlines */
    {0x0de74e2323838d33ull, 0x7b0075fbb1fd4d32ull}, /* consumer */
    {0x3320bceefdf5a5a1ull, 0x662f2640a74d26c7ull}, /* trinitron */
    {0x656bc79c70138558ull, 0x070e74f71159ce17ull}, /* pvm */
    {0xd7a5053db7e17860ull, 0x651df7c27e4ea4e7ull}, /* shadow */
};

static double boxLuma(uint32_t w, uint32_t h)
{
    RhiRect b;
    rd__present_box(w, h, 4.0f / 3.0f, &b);
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
    if (s_llvmpipe && s_presentExact == RD_BLEND_EXACT_ATTACHMENT) {
        CHECK(off == GOLD_OFF_EXACT, "off: the rd_present baseline's bytes (exact blend)");
    } else if (s_llvmpipe && !s_presentExact) {
        CHECK(off == GOLD_OFF, "off: the rd_present baseline's bytes");
    }
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdSettings z = s;
        rd_crt_settings(&z, (RdCrtMode)m, 0.0f);
        if (present(&z, 1)) {
            CHECK(fnv(s_out, 960 * 720 * 4) == off, "off: %s at strength 0 is the plain present",
                  kModeName[m]);
        }
    }
    RdSettings o = s;
    rd_crt_settings(&o, RD_CRT_OFF, 1.0f);
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
            rd_crt_settings(&s, (RdCrtMode)m, 1.0f);
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
            if (s_llvmpipe && s_presentExact == RD_BLEND_EXACT_ATTACHMENT) {
                CHECK(h1 == kGoldExact[m - 1][k], "modes: %s %ux%u the recorded hash (exact blend)",
                      kModeName[m], sizes[k][0], sizes[k][1]);
            } else if (s_llvmpipe && !s_presentExact) {
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
    rd__present_box(w, h, 4.0f / 3.0f, &b);
    CHECK(b.x == 160 && b.w == 960, "outside: the box is columns 160..1119");
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdSettings s = outputSettings(w, h);
        rd_crt_settings(&s, (RdCrtMode)m, 1.0f);
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
        rd_crt_settings(&s, RD_CRT_SCANLINES, 1.0f);
        if (!present(&s, 1)) {
            return;
        }
        const double crt = boxLuma(w, h);
        printf("  luma: %ux%u off %.2f scanlines %.2f (%.1f %%)\n", w, h, off, crt,
               100.0 * crt / off);
        CHECK(crt > off * 0.8 && crt < off * 1.2, "luma: scanlines within 20 %% at %ux%u", w, h);
    }
}

/* Full pixel under the filter: a flat frame through the tube at 1920 x 1080
 * (pillars).  Off, the reduction's border is dark at the box edge; on, the
 * edge pixels are as lit as the centre and the pillars stay black.  Off in
 * the game (the border mask, mode 2) at 4:3, where the frame is its exact
 * width: no mask (rd_present.c rd__frame_cropped), the present the plain
 * off one's byte for byte, the GS's own border as in v0.7.4 (the mask on
 * a cropped frame: checkWideMask). */
static void checkFullPixel(void)
{
    const uint32_t w = 1920, h = 1080;
    uint8_t edge[3][4][3], mid[3][3];
    static uint8_t gsOut[1920 * 1080 * 4];
    RhiRect b;
    rd__present_box(w, h, 4.0f / 3.0f, &b);
    static const char *const kMode[3] = {"off", "on", "off (border mask)"};
    for (int on = 0; on < 3; on++) {
        makeFlatScene(0xC0, 0xC0, 0xC0);
        RdSettings s = outputSettings(w, h);
        rd_crt_settings(&s, RD_CRT_SCANLINES, 1.0f);
        s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
        s.crtScanlines = 0.0f;
        s.fullPixel = (uint8_t)(on == 1);
        s.borderMask = (uint8_t)(on == 2);
        if (!present(&s, 0)) {
            return;
        }
        CHECK(rhi_vk_validation_error_count() == 0, "full pixel %s: %u validation errors",
              kMode[on], rhi_vk_validation_error_count());
        const uint32_t cx = (uint32_t)b.x + b.w / 2, cy = (uint32_t)b.y + b.h / 2;
        const uint32_t xs[4] = {(uint32_t)b.x, (uint32_t)b.x + b.w - 1, cx, cx};
        const uint32_t ys[4] = {cy, cy, (uint32_t)b.y, (uint32_t)b.y + b.h - 1};
        for (int k = 0; k < 4; k++) {
            memcpy(edge[on][k], &s_out[((size_t)ys[k] * w + xs[k]) * 4], 3);
        }
        memcpy(mid[on], &s_out[((size_t)cy * w + cx) * 4], 3);
        if (on == 0) {
            memcpy(gsOut, s_out, sizeof(gsOut));
        } else if (on == 2) {
            CHECK(memcmp(gsOut, s_out, sizeof(gsOut)) == 0,
                  "full pixel off (border mask) at 4:3: the present is plain off's byte for byte, "
                  "the GS's own border, no mask");
        }
        /* the pillars beside the box */
        CHECK(!(s_out[((size_t)cy * w + b.x - 1) * 4] | s_out[((size_t)cy * w + b.x + b.w) * 4]),
              "full pixel %s: the pillars stay black", kMode[on]);
    }
    static const char *const kSide[4] = {"left", "right", "top", "bottom"};
    for (int k = 0; k < 4; k++) {
        CHECK(edge[0][k][0] < mid[0][0] / 8, "full pixel off: the %s edge is dark (%u of %u)",
              kSide[k], edge[0][k][0], mid[0][0]);
        CHECK(edge[1][k][0] * 4 >= mid[1][0] * 3, "full pixel on: the %s edge is lit (%u of %u)",
              kSide[k], edge[1][k][0], mid[1][0]);
    }
}

/* --------------------------------------------------------- the phosphors */

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
    rd_crt_settings(&s, mode, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    s.crtMask = mask; /* < 0: the mode's own strength */
    if (flat) {
        s.crtScanlines = 0.0f;
    }
    s_w = w;
    rd__present_box(w, h, 4.0f / 3.0f, &s_box);
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
        if (rd__crt_mask_weight(p->mask, r, p->maskStrength, f, v, odd, c) == 1.0f) {
            lit = c;
        }
    }
    return lit;
}

static void checkPhosphors(void)
{
    RdCrtParams p;
    /* 1440 x 1080 (1920 x 1080), Trinitron, a grey: every box pixel one
       channel, the expected one, each source pixel's columns R, G, B in
       order.  The geometry's frames are 0x80 (linear 0.22), below the
       highlights where the mask's strength eases (rd__crt_strength_at),
       under a flat beam (a grey's beam leaves the lines' edges dark) */
    if (phosphors(RD_CRT_TRINITRON, 1920, 1080, 0x80, 0x80, 0x80, 1, 1.0f)) {
        rd__crt_preset(RD_CRT_TRINITRON, &p);
        p.maskStrength = 1.0f;
        uint32_t impure = 0, wrong = 0, order = 0, checked = 0;
        /* away from the rounded corners (0.02 of the height) and the
           frame's dark top lines (under a white the beam of the next line
           lit them; a grey's flat beam does not) */
        for (uint32_t y = 37; y + 30 < s_box.h; y += 7) {
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
        printf("  phosphors: 1440x1080 trinitron grey: %u pixels checked, %u impure, %u not the "
               "expected channel, %u out of R, G, B order\n",
               checked, impure, wrong, order);
        CHECK(impure == 0 && wrong == 0 && order == 0 && checked > 100000,
              "phosphors: 1440x1080: every box pixel one channel of its source pixel, in order");
    }
    /* 1536 x 1152, Trinitron, red: only each pixel's first column lights */
    if (phosphors(RD_CRT_TRINITRON, 1536, 1152, 0x80, 0, 0, 1, 1.0f)) {
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
    /* 1536 x 1152, Trinitron and PVM, grey: columns R, G, B */
    for (int m = RD_CRT_TRINITRON; m <= RD_CRT_PVM; m++) {
        if (!phosphors((RdCrtMode)m, 1536, 1152, 0x80, 0x80, 0x80, 1, 1.0f)) {
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
        CHECK(stripes, "phosphors: 1536x1152 %s: a grey pixel lights R, G, B columns",
              kModeName[m]);
    }
    /* Consumer TV at its own strength, a mid grey (no clipping), the beam
       flat: the bridges (the last third of a line, half a line later in odd
       columns) are darker than the slots by (1 - strength) in linear light,
       at the expected rows; each stripe its channel in full and the other
       two leaking (1 - strength) */
    if (phosphors(RD_CRT_CONSUMER, 1536, 1152, 0x60, 0x60, 0x60, 1, -1.0f)) {
        rd__crt_preset(RD_CRT_CONSUMER, &p);
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
        rd__crt_preset(RD_CRT_TRINITRON, &p);
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
    /* Shadow mask, grey: the first half of a line R, G, B; the second the
       row of dots half a triad over (the expected channel everywhere) */
    if (phosphors(RD_CRT_SHADOW, 1536, 1152, 0x80, 0x80, 0x80, 1, 1.0f)) {
        rd__crt_preset(RD_CRT_SHADOW, &p);
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

/* -------------------------------------------- the highlights on a device */

/* White and light colours through each mode as the player sees it (its
 * glow, curvature and vignette on) at 1920 x 1080 (the 1440 x 1080 box,
 * the mask drawn in full): over the box's middle half, no channel's mean
 * is brighter than the input (in the mode's linear light, a 2 % margin
 * for the 8 bits), a light colour keeps its channels' ratios, and in a
 * white under a mask few pixels are a flat white (the stripes stay). */
static void checkLight(void)
{
    static const uint8_t kIn[4][3] = {
        {255, 255, 255}, {255, 204, 204}, {204, 229, 255}, {255, 255, 204}};
    for (int m = RD_CRT_SCANLINES; m < RD_CRT_MODE_COUNT; m++) {
        RdCrtParams p;
        rd__crt_preset((RdCrtMode)m, &p);
        for (int i = 0; i < 4; i++) {
            makeFlatScene(kIn[i][0], kIn[i][1], kIn[i][2]);
            RdSettings s = outputSettings(1920, 1080);
            rd_crt_settings(&s, (RdCrtMode)m, 1.0f);
            s_w = 1920;
            rd__present_box(1920, 1080, 4.0f / 3.0f, &s_box);
            if (!present(&s, 0)) {
                return;
            }
            double sum[3] = {0, 0, 0};
            uint32_t n = 0, flat = 0;
            for (uint32_t y = s_box.h / 4; y < s_box.h * 3 / 4; y++) {
                for (uint32_t x = s_box.w / 4; x < s_box.w * 3 / 4; x++) {
                    const uint8_t *c = at(x, y);
                    for (int k = 0; k < 3; k++) {
                        sum[k] += pow(c[k] / 255.0, p.gammaOut);
                    }
                    flat += c[0] >= 250 && c[1] >= 250 && c[2] >= 250;
                    n++;
                }
            }
            double out[3], lin[3], over = 0.0, hue = 0.0;
            for (int k = 0; k < 3; k++) {
                out[k] = sum[k] / n;
                lin[k] = pow(kIn[i][k] / 255.0, p.gammaIn);
                const double o = out[k] / lin[k] - 1.0;
                over = o > over ? o : over;
            }
            for (int k = 1; k < 3; k++) {
                const double e = fabs((out[k] / out[0]) / (lin[k] / lin[0]) - 1.0);
                hue = e > hue ? e : hue;
            }
            printf("  light: %-9s %3u %3u %3u -> %.3f %.3f %.3f (in %.3f %.3f %.3f), %.1f %% flat "
                   "white\n",
                   kModeName[m], kIn[i][0], kIn[i][1], kIn[i][2], out[0], out[1], out[2], lin[0],
                   lin[1], lin[2], 100.0 * flat / n);
            CHECK(over < 0.02, "light: %s %u %u %u: no channel brighter than its input (%.3f)",
                  kModeName[m], kIn[i][0], kIn[i][1], kIn[i][2], over);
            CHECK(hue < 0.04, "light: %s %u %u %u: the hue kept (%.3f)", kModeName[m], kIn[i][0],
                  kIn[i][1], kIn[i][2], hue);
            if (i == 0 && p.mask != RD_CRT_MASK_NONE) {
                CHECK(flat < n / 20, "light: %s: the mask stays in a white (%.1f %% flat white)",
                      kModeName[m], 100.0 * flat / n);
            }
        }
    }
}

/* -------------------------------------------- the touch controls' layer */

/* one layer's callback: its ctx kept, one solid quad drawn at fractions
 * of its frame (fx0, fy0)..(fx1, fy1) in colour c */
typedef struct LayerTest {
    RdOverlayCtx ctx;
    int calls;
    float fx0, fy0, fx1, fy1;
    uint8_t c[4];
} LayerTest;

static void layerQuad(const RdOverlayCtx *ctx, void *user)
{
    LayerTest *t = user;
    t->ctx = *ctx;
    t->calls++;
    const float w = (float)ctx->outW, h = (float)ctx->outH;
    RdScreenVtx v[2] = {vtx((int32_t)(t->fx0 * w) * 16, (int32_t)(t->fy0 * h) * 16, t->c, 0, 0),
                        vtx((int32_t)(t->fx1 * w) * 16, (int32_t)(t->fy1 * h) * 16, t->c, 0, 0)};
    rd_overlay_prims(RD_PRIM_SPRITES, v, 2, (RdTex){0}, RD_BLEND_LERP_AS);
}

/* the scanlines on a black frame, a grey quad on the main overlay (inside
 * the filtered picture) and a red one on the top layer (the touch
 * controls): the red is drawn on the output after the filter, every pixel
 * its flat colour, while the grey's rows vary with the scanlines */
static void checkTopLayer(void)
{
    const uint32_t w = 960, h = 720;
    makeFlatScene(0, 0, 0);
    RdSettings s = outputSettings(w, h);
    rd_crt_settings(&s, RD_CRT_SCANLINES, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    LayerTest m = {.fx0 = 0.25f, .fy0 = 0.25f, .fx1 = 0.5f, .fy1 = 0.5f, .c = {64, 64, 64, 0x80}};
    LayerTest t = {.fx0 = 0.6f, .fy0 = 0.6f, .fx1 = 0.8f, .fy1 = 0.8f, .c = {255, 0, 0, 0x80}};
    RhiRect b;
    rd__present_box(w, h, 4.0f / 3.0f, &b);
    rd_set_present_overlay(layerQuad, &m);
    rd_set_present_overlay_top(layerQuad, &t);
    const bool ok = present(&s, 0);
    rd_set_present_overlay(NULL, NULL);
    rd_set_present_overlay_top(NULL, NULL);
    if (!ok) {
        return;
    }
    CHECK(m.calls == 1 && t.calls == 1, "top layer: one call each (%d, %d)", m.calls, t.calls);
    CHECK(t.ctx.outW == w && t.ctx.outH == h && t.ctx.box.x == b.x && t.ctx.box.y == b.y &&
              t.ctx.box.w == b.w && t.ctx.box.h == b.h,
          "top layer: its ctx is the output (%ux%u, box %d,%d %ux%u)", t.ctx.outW, t.ctx.outH,
          t.ctx.box.x, t.ctx.box.y, t.ctx.box.w, t.ctx.box.h);
    CHECK(m.ctx.outW != w && m.ctx.box.x == 0 && m.ctx.box.w == m.ctx.outW,
          "top layer: the main overlay's ctx is still the grid (%ux%u)", m.ctx.outW, m.ctx.outH);
    /* the red quad, output pixels (its own frame is the output) */
    const uint32_t tx0 = (uint32_t)(0.6f * w), ty0 = (uint32_t)(0.6f * h);
    const uint32_t tx1 = (uint32_t)(0.8f * w), ty1 = (uint32_t)(0.8f * h);
    uint32_t notRed = 0;
    for (uint32_t y = ty0; y < ty1; y++) {
        for (uint32_t x = tx0; x < tx1; x++) {
            const uint8_t *p = &s_out[((size_t)y * w + x) * 4];
            notRed += !(p[0] == 255 && p[1] == 0 && p[2] == 0);
        }
    }
    CHECK(notRed == 0, "top layer: %u pixels of the red quad are not its flat red (filtered?)",
          notRed);
    /* the grey quad: a quarter of the box from its corner, inset away from
     * the edges' glow; each row's mean */
    const uint32_t mx0 = (uint32_t)b.x + b.w / 4 + 8, mx1 = (uint32_t)b.x + b.w / 2 - 8;
    const uint32_t my0 = (uint32_t)b.y + b.h / 4 + 8, my1 = (uint32_t)b.y + b.h / 2 - 8;
    double lo = 1e9, hi = 0.0;
    for (uint32_t y = my0; y < my1; y++) {
        double sum = 0.0;
        for (uint32_t x = mx0; x < mx1; x++) {
            sum += s_out[((size_t)y * w + x) * 4];
        }
        const double mean = sum / (double)(mx1 - mx0);
        lo = mean < lo ? mean : lo;
        hi = mean > hi ? mean : hi;
    }
    printf("  top layer: the main overlay's grey rows %.1f .. %.1f\n", lo, hi);
    CHECK(hi > 30.0, "top layer: the main overlay's quad is in the picture (rows up to %.1f)", hi);
    /* the scanlines' modulation of a flat grey (measured 56 .. 71 on
       lavapipe: a 15-level swing) */
    CHECK(hi - lo > 8.0,
          "top layer: the main overlay's quad goes through the scanlines (rows %.1f .. %.1f)", lo,
          hi);
}

/* ------------------------------------------------------ the native frame
 *
 * The grid of a frame wider than 512: with Full pixel on and off the
 * native frame's columns its exact width shows, the (gsW - wExact) / 2
 * past it at each side rounded to whole GS pixels and left out (off masks
 * the game's border over the tube's box), in a box of the grid's shape,
 * so a source pixel spans as many output pixels as in the 4:3 run; none
 * of this at 512. */
static void checkWideGrid(void)
{
    static const struct {
        uint32_t w;
        float we;
        uint32_t crop, vw;
    } kW[] = {
        {512, 512.0f, 0, 512},
        {688, 384.0f * 16.0f / 9.0f, 3, 682},   /* 16:9: 2.67 a side */
        {1376, 384.0f * 32.0f / 9.0f, 5, 1366}, /* 32:9: 5.33 a side */
        {2560, 384.0f * 60.0f / 9.0f, 0, 2560}, /* 60:9: the frame is the exact width */
    };

    for (size_t j = 0; j < 2 * sizeof(kW) / sizeof(kW[0]); j++) {
        /* Full pixel on and off (the game's: the border mask): the exact
           width either way */
        const size_t i = j / 2;
        const int on = (j % 2) == 0;
        if (!rd__init_record_only(kW[i].w, 512)) {
            CHECK(0, "wide grid: no record-only context at %u", kW[i].w);
            return;
        }
        g_rd.settings.fullPixel = (uint8_t)on;
        g_rd.settings.borderMask = (uint8_t)!on;
        g_rd.frameWExact = kW[i].we;
        const uint32_t wantCrop = kW[i].crop, wantW = kW[i].vw;
        uint32_t vw = 0, vh = 0, fw = 0, fh = 0;
        rd__crt_grid(&vw, &vh);
        CHECK(rd__crt_crop() == wantCrop && vw == wantW && vh == 256,
              "wide grid %u (exact %.2f, full pixel %s): %u columns cropped a side, a %ux%u grid "
              "(want %u, %ux256)",
              kW[i].w, (double)kW[i].we, on ? "on" : "off", rd__crt_crop(), vw, vh, wantCrop,
              wantW);
        /* the films keep the 4:3 tube whatever the frame's width */
        rd__crt_film_grid(448, &fw, &fh);
        CHECK(fw == 512 && fh == 224, "wide grid %u: the films' grid %ux%u (want 512x224)", kW[i].w,
              fw, fh);
        /* the mask period: output pixels a source pixel, the 4:3 run's
         * (2 and 3 at 768 and 1152 lines) */
        static const uint32_t kH[2] = {768, 1152};
        for (int k = 0; k < 2; k++) {
            RhiRect b, b43;
            rd__crt_box(8192, kH[k], &b);
            rd__present_box(8192, kH[k], 4.0f / 3.0f, &b43);
            CHECK(b.h == kH[k] && b.w * 512u == b43.w * vw,
                  "wide grid %u: box %ux%u, %.4f output pixels a source pixel (the 4:3 run's "
                  "%.4f)",
                  kW[i].w, b.w, b.h, (double)b.w / (double)vw, (double)b43.w / 512.0);
            if (kW[i].w == 512) {
                CHECK(b.x == b43.x && b.y == b43.y && b.w == b43.w && b.h == b43.h,
                      "wide grid 512: the filter's box is the 4:3 box");
            }
        }
        rd_shutdown();
    }
}

/* A W-wide frame at the aspect through the filter: the clear colour grey
 * (0..255), white columns cols, the reduction (full pixel as in s) */
static bool presentWide(const RdSettings *s, uint32_t W, float aspect, uint8_t grey,
                        const int *cols, int ncols)
{
    if (!rd_init(512, 512, s, NULL)) {
        return false;
    }
    rd_reset_scene(W, 512);
    rd_begin_frame();
    RdFrameHead hd;
    memset(&hd, 0, sizeof(hd));
    hd.rgba[0] = hd.rgba[1] = hd.rgba[2] = grey;
    hd.rgba[3] = 0x80;
    hd.gsW = W;
    hd.gsH = 512;
    hd.aspect = aspect;
    hd.wExact = 384.0f * aspect;
    hd.clear = 1;
    rd_frame_head(&hd);
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), W, 512, 1);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_pabe(0);
    rd_fba(0);
    rd_texture_off();
    static const uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x80};
    const int32_t ox = (2048 - (int32_t)W / 2) * 16, oy = (2048 - 256) * 16;
    for (int i = 0; i < ncols; i++) {
        RdScreenVtx v[2] = {vtx(ox + cols[i] * 16, oy, white, 0.0f, 0.0f),
                            vtx(ox + (cols[i] + 1) * 16, oy + 512 * 16, white, 0.0f, 0.0f)};
        rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_WORLD, 1, 0);
    }
    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
    uint32_t w = 0, h = 0;
    const size_t n = (size_t)s->outputWidth * s->outputHeight * 4;
    const bool ok = n <= OUT_MAX && rd__read_present(s_out, n, &w, &h) && w == s->outputWidth &&
                    h == s->outputHeight;
    CHECK(ok, "readback of the %ux%u wide present", s->outputWidth, s->outputHeight);
    CHECK(rhi_vk_validation_error_count() == 0, "wide present: %u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    return ok;
}

/* the lit channel of each column of row y of the box (-1: none or more
 * than one) */
static void rowChannels(const RhiRect *b, uint32_t outW, uint32_t y, int *ch)
{
    for (uint32_t x = 0; x < b->w; x++) {
        const uint8_t *c = &s_out[((size_t)(b->y + y) * outW + b->x + x) * 4];
        int n = 0, k1 = -1;
        for (int k = 0; k < 3; k++) {
            if (c[k] > 1) {
                n++;
                k1 = k;
            }
        }
        ch[x] = n == 1 ? k1 : -1;
    }
}

/* Full pixel off in the game (the border mask) on a cropped frame under the
 * filter: a flat 688 frame at 16:9 (the grid 682 columns) through the tube
 * at 1920 x 1080 with Full pixel on and with the mask.  The tube's box at
 * the centre row and column is the on run's byte for byte inside the mask
 * and black in its bands, 2 of the grid's 682 columns a side and 8 of its
 * 256 lines top and bottom (512 lines), within a pixel. */
static void checkWideMask(void)
{
    const uint32_t W = 688, w = 1920, h = 1080;
    const float aspect = 16.0f / 9.0f;
    static uint8_t row[2][1920][3], col[2][1080][3];
    RhiRect b;
    rd__present_box(w, h, 682.0f / 384.0f, &b);
    const uint32_t cx = (uint32_t)b.x + b.w / 2, cy = (uint32_t)b.y + b.h / 2;
    for (int k = 0; k < 2; k++) {
        RdSettings s = outputSettings(w, h);
        s.aspect = aspect;
        rd_crt_settings(&s, RD_CRT_SCANLINES, 1.0f);
        s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
        s.crtScanlines = 0.0f;
        s.fullPixel = (uint8_t)(k == 0);
        s.borderMask = (uint8_t)(k == 1);
        if (!presentWide(&s, W, aspect, 0xC0, NULL, 0)) {
            return;
        }
        for (uint32_t x = 0; x < w; x++) {
            memcpy(row[k][x], &s_out[((size_t)cy * w + x) * 4], 3);
        }
        for (uint32_t y = 0; y < h; y++) {
            memcpy(col[k][y], &s_out[((size_t)y * w + cx) * 4], 3);
        }
    }
    static const uint8_t zero[3] = {0, 0, 0};
    uint32_t l = 0, r = 0, t = 0, bo = 0;
    while (l < b.w / 2 && !memcmp(row[1][b.x + l], zero, 3)) {
        l++;
    }
    while (r < b.w / 2 && !memcmp(row[1][b.x + b.w - 1 - r], zero, 3)) {
        r++;
    }
    while (t < b.h / 2 && !memcmp(col[1][b.y + t], zero, 3)) {
        t++;
    }
    while (bo < b.h / 2 && !memcmp(col[1][b.y + b.h - 1 - bo], zero, 3)) {
        bo++;
    }
    const float side = 2.0f * (float)b.w / 682.0f, rows = (float)b.h * 16.0f / 512.0f;
    printf("  wide border mask: %u left, %u right, %u top, %u bottom (%.3f, %.3f)\n", l, r, t, bo,
           (double)side, (double)rows);
    CHECK(fabsf((float)l - side) <= 1.0f && fabsf((float)r - side) <= 1.0f &&
              fabsf((float)t - rows) <= 1.0f && fabsf((float)bo - rows) <= 1.0f,
          "wide border mask: the bands are 2 of 682 columns and 8 of 256 lines at the grid's "
          "scale (%u %u %u %u; %.3f, %.3f)",
          l, r, t, bo, (double)side, (double)rows);
    uint32_t bad = 0;
    for (uint32_t x = (uint32_t)b.x + l; x < (uint32_t)b.x + b.w - r; x++) {
        bad += memcmp(row[1][x], row[0][x], 3) != 0;
    }
    for (uint32_t y = (uint32_t)b.y + t; y < (uint32_t)b.y + b.h - bo; y++) {
        bad += memcmp(col[1][y], col[0][y], 3) != 0;
    }
    CHECK(bad == 0, "wide border mask: inside the mask the on picture (%u differ)", bad);
}

/* A 688 frame at 16:9 through the filter: the grid is 682 columns (3 a
 * side cropped); white columns 0..2 and 686..687 never reach the box, 3
 * and 684 do, at its edges.  Column 685 is cropped too, but the reduction
 * samples DISPLAY x at SCENE x + 0.75 (rd_blur.c: texels x and x + 1 at
 * 12/16 and 4/16, the PS2's bias), so a quarter of it reaches DISPLAY 684,
 * the box's last column: alone it lights only that column, dimmer than 684
 * lights it (a crop one column short would show it at full).  Under
 * Trinitron's grille at strength 1 the
 * columns of the box's row light the same channels as the 4:3 run's, the
 * same period (3 output pixels a source pixel at 1152 lines) */
static void checkWideCrop(void)
{
    const uint32_t W = 688, ow = 2048, oh = 1152;
    const float aspect = 16.0f / 9.0f;
    RdSettings s = outputSettings(ow, oh);
    s.aspect = aspect;
    s.fullPixel = 1; /* the reduction draws every column */
    rd_crt_settings(&s, RD_CRT_TRINITRON, 1.0f);
    s.crtHalation = s.crtBloom = s.crtCurvature = 0.0f;
    s.crtScanlines = 0.0f;
    s.crtMask = 1.0f;
    static const int kOut[5] = {0, 1, 2, 686, 687}, kIn[2] = {3, 684};
    if (!presentWide(&s, W, aspect, 0, kOut, 5)) {
        return;
    }
    uint32_t vw = 0, vh = 0;
    rd__crt_last_pass(&vw, &vh);
    CHECK(vw == 682 && vh == 256, "wide crop: the filter's grid %ux%u (want 682x256)", vw, vh);
    uint32_t lit = 0;
    for (size_t i = 0; i < (size_t)ow * oh; i++) {
        lit += (s_out[i * 4] | s_out[i * 4 + 1] | s_out[i * 4 + 2]) != 0;
    }
    CHECK(lit == 0, "wide crop: the columns past the exact width are left out (%u pixels lit)",
          lit);
    /* 685's quarter in DISPLAY 684 against 684 itself: the brightest
     * channel each lights and where (the box's last source pixel: its last
     * 3 output pixels, 2046 / 682 a source pixel) */
    RhiRect eb;
    rd__present_box(ow, oh, 682.0f / 384.0f, &eb);
    uint8_t peak[2] = {0, 0};
    uint32_t outside = 0;
    for (int k = 0; k < 2; k++) {
        static const int kEdge[2] = {685, 684};
        if (!presentWide(&s, W, aspect, 0, &kEdge[k], 1)) {
            return;
        }
        for (size_t i = 0; i < (size_t)ow * oh; i++) {
            const uint8_t *c = &s_out[i * 4];
            const uint8_t m =
                c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            const uint32_t x = (uint32_t)(i % ow);
            peak[k] = m > peak[k] ? m : peak[k];
            outside += k == 0 && m != 0 && (x + 3 < eb.x + eb.w || x >= eb.x + eb.w);
        }
    }
    CHECK(outside == 0 && peak[0] > 0 && 2u * peak[0] < peak[1],
          "wide crop: column 685 reaches the box only as the reduction's quarter in its last "
          "column (%u pixels elsewhere, peak %u against 684's %u)",
          outside, peak[0], peak[1]);
    if (!presentWide(&s, W, aspect, 0, kIn, 2)) {
        return;
    }
    RhiRect b;
    rd__present_box(ow, oh, 682.0f / 384.0f, &b);
    const uint32_t y = b.h / 2;
    int edge[2] = {0, 0};
    for (uint32_t x = 0; x < 3; x++) {
        const uint8_t *l = &s_out[((size_t)(b.y + y) * ow + b.x + x) * 4];
        const uint8_t *r = &s_out[((size_t)(b.y + y) * ow + b.x + b.w - 1 - x) * 4];
        edge[0] |= l[0] | l[1] | l[2];
        edge[1] |= r[0] | r[1] | r[2];
    }
    CHECK(b.w == 2046 && edge[0] && edge[1],
          "wide crop: columns 3 and 684 at the box's edges (box %u wide, lit %d %d)", b.w, edge[0],
          edge[1]);
    /* the mask against the 4:3 run, a grey frame */
    static int ch43[1536], chW[2046];
    if (!presentWide(&s, W, aspect, 0x80, NULL, 0)) {
        return;
    }
    rowChannels(&b, ow, y, chW);
    if (!phosphors(RD_CRT_TRINITRON, 1536, 1152, 0x80, 0x80, 0x80, 1, 1.0f)) {
        return;
    }
    CHECK(s_box.w == 1536 && s_box.h == 1152, "wide crop: the 4:3 run's box %ux%u", s_box.w,
          s_box.h);
    rowChannels(&s_box, 1536, y, ch43);
    /* the 4:3 run's first and last 3 source pixels are the reduction's
     * border (full pixel off there); the wide one's are cropped */
    uint32_t same = 0, diff = 0;
    for (uint32_t x = 9; x + 9 < 1536; x++) {
        same += ch43[x] >= 0 && ch43[x] == chW[x];
        diff += ch43[x] != chW[x];
    }
    printf("  wide crop: 688 at 16:9, the row's channels as the 4:3 run's: %u same, %u not\n", same,
           diff);
    CHECK(diff == 0 && same > 1500,
          "wide crop: the mask's columns are the 4:3 run's (%u same, %u differ)", same, diff);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    const char *dir = argc > 1 ? argv[1] : ".";
    checkOptions(dir);
    checkResolve();
    checkHighlights();
    checkWideGrid();
    RdSettings st = outputSettings(64, 48);
    if (!rd_init(512, 512, &st, NULL)) {
        if (failures) {
            printf("rd_crt_test: %d failures\n", failures);
            return 1;
        }
        printf("rd_crt_test: options ok; SKIP the pixel checks: no usable device\n");
        return 77;
    }
    s_llvmpipe = strstr(rhi_adapter_name(), "llvmpipe") != NULL;
    printf("rd_crt_test: adapter %s\n", rhi_adapter_name());
    rd_shutdown();
    checkOff();
    checkModes();
    checkOutside();
    checkLuma();
    checkFullPixel();
    checkPhosphors();
    checkLight();
    checkTopLayer();
    checkWideCrop();
    checkWideMask();
    if (failures) {
        printf("rd_crt_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_crt_test: ok\n");
    return 0;
}
