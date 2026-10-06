/* font_edge_test.c: deferred text at the output's resolution (package DEF).
 *
 * port/ui's font.c and rd on a Vulkan device (lavapipe here; exit 77 without
 * one).  A frame of black SCENE with a row of text recorded in list 11 as
 * the menu rows record it (font.h ui_DrawTextDeferred) and the reduction in
 * list 12, presented with font.c's deferred text renderer installed and an
 * overlay callback that writes a second word through font.c's overlay mode
 * (as the popups do):
 *   edges     Enhanced, 1920 x 1080 (16:9) and 3840 x 2160: on rows through
 *             the stems of the deferred "H" and of the overlay "H", every
 *             edge has at most one pixel strictly between the background
 *             and the ink (the glyphs are rasterised at the shown size and
 *             drawn a texel a pixel); the quad path (--quad-text: no
 *             renderer) has more, which is what the deferral removes
 *   mirror    the mirror mode leaves the deferred text where it was (the
 *             output equal to the unmirrored one: the scene is black)
 *   original  the Original preset at 960 x 720: the present with a
 *             deferred row and a popup recorded is byte-identical to the
 *             same frame with the row drawn by ui_DrawText (no item), and to
 *             the frame with no renderer installed
 *   fold      Enhanced: a fade to black at 0x80 after the row hides it, at
 *             0x40 halves it; a full letterbox cuts the part of a row inside
 *             its band; a KEEP after the row drops it, a row after the KEEP
 *             of a keep frame (lists 11, 12) is drawn
 *   glow      Enhanced 1920 x 1080: the title's glow (additive, stretched)
 *             of an "I" is soft: across the stem its blue steps at most a
 *             third of its peak a pixel (package GHOST: drawn a texel a
 *             pixel it was a sharp second copy of the letters)
 *   faces     package GFONT: with a game face loaded (a synthetic one, built
 *             by game_font_build.c from a drawn sheet holding "I" and "L"),
 *             the edge check applies to Arimo, which stands in for the "H"
 *             the face lacks: still at most one pixel between on an edge at
 *             1080p; the game face's own "I" is its bitmap scaled
 *             bilinearly, soft by design (more than one pixel between), and
 *             is not held to it
 *
 * Usage: font_edge_test [dir]  (dir: where the PNGs go)
 * Exit 0, 1 on a failure, 77 without a device.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "game_font.h"
#include "rd_internal.h"
#include "ui_internal.h"
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

static char s_dir[1024];

static const uint8_t kWhite[4] = {0x80, 0x80, 0x80, 0x80};

/* the two words: the deferred one left of the centre, the overlay's right */
#define DEF_X 220.0f
#define OVL_X 420.0f
#define WORD_Y 226.0f
#define WORD_SIZE 40.0f

/* ------------------------------------------------------------ the frame */

typedef struct Frame {
    int deferred; /* the row through ui_DrawTextDeferred, else ui_DrawText */
    int row;      /* a row at all */
    const char *word;
    int fade;       /* 1 + the fade alpha after the row (0: none) */
    int letterbox;  /* 1 + the band level (0: none) */
    int keepBefore; /* KEEP before the row (a keep frame: lists 11, 12) */
    int keepAfter;  /* KEEP after the row */
    int glow;       /* the title's glow alone (1) or under the row (2; package GHOST) */
    uint8_t bg;     /* the scene's grey */
    float rowY;
} Frame;

static void scissorAll(void)
{
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
}

static void recordFrame(const Frame *fr)
{
    const uint8_t bg[4] = {fr->bg, fr->bg, fr->bg, 0x80};
    rd_BeginFrame();
    if (!fr->keepBefore) {
        rd_SelectList(0);
        rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
        scissorAll();
    }
    rd_SelectList(11);
    scissorAll();
    RdPostParams pp;
    if (fr->keepBefore) {
        memset(&pp, 0, sizeof(pp));
        rd_Post(RD_POST_KEEP, &pp);
        scissorAll();
    }
    if (fr->row) {
        const unsigned flags = UI_ALIGN_CENTER | UI_VALIGN_MIDDLE;
        if (fr->glow == 2) {
            ui_DrawTextDeferred(DEF_X, fr->rowY, 27.0f, kWhite, fr->word, flags | UI_HALO, NULL);
        }
        if (fr->glow) {
            /* lt_glow_sprite's at its brightest: the row's box (400 x 38
               y units here) 4 wider and 8 taller, additive, blue */
            static const uint8_t blue[4] = {54, 80, 115, 127};
            const float bx = DEF_X - 200.0f, by = fr->rowY - 17.0f;
            const UiXform xf = {bx, by, 404.0f / 400.0f, 46.0f / 38.0f, -2.0f, -4.0f};
            ui_DrawTextDeferred(DEF_X, fr->rowY, fr->glow == 2 ? 27.0f : WORD_SIZE, blue, fr->word,
                                flags | UI_ADDITIVE, &xf);
        } else if (fr->deferred) {
            ui_DrawTextDeferred(DEF_X, fr->rowY, WORD_SIZE, kWhite, fr->word, flags, NULL);
        } else {
            ui_DrawText(DEF_X, fr->rowY, WORD_SIZE, kWhite, fr->word, flags);
        }
    }
    if (fr->keepAfter) {
        memset(&pp, 0, sizeof(pp));
        rd_Post(RD_POST_KEEP, &pp);
    }
    if (fr->fade) {
        memset(&pp, 0, sizeof(pp));
        pp.rgba[3] = (uint8_t)(fr->fade - 1);
        rd_Post(RD_POST_FADE, &pp);
    }
    if (fr->letterbox) {
        memset(&pp, 0, sizeof(pp));
        pp.fix = (uint8_t)(fr->letterbox - 1);
        rd_Post(RD_POST_LETTERBOX, &pp);
    }
    rd_SelectList(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(fr->keepBefore ? 1 : 0);
}

/* the overlay: a popup-like panel and the second word */
static int s_ovWord;

static void overlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_BeginOverlay(ctx);
    if (s_ovWord) {
        ui_DrawText(OVL_X, WORD_Y, WORD_SIZE, kWhite, "H", UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
    } else {
        static const uint8_t panel[4] = {20, 30, 40, 0x60};
        ui_DrawRect(400.0f, 380.0f, 620.0f, 440.0f, panel);
        ui_DrawText(410.0f, 400.0f, 18.0f, kWhite, "Achievement", UI_VALIGN_TOP);
    }
    ui_EndOverlay();
}

static RdSettings settingsOf(int enhanced, uint32_t w, uint32_t h, int mirror)
{
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = enhanced ? RD_PRESET_ENHANCED : RD_PRESET_ORIGINAL;
    s.outputWidth = w;
    s.outputHeight = h;
    s.aspect = enhanced ? (float)w / (float)h : 4.0f / 3.0f;
    s.mirror = (uint8_t)mirror;
    s.vsync = 1;
    return s;
}

/* rd up with s, the renderer and the overlay as asked, the frames recorded
 * and presented; the last output into a new buffer (NULL on failure) */
static uint8_t *present(const RdSettings *s, int renderer, int ovWord, const Frame *frames,
                        int nFrames)
{
    if (!rd_Init(512, 512, s, NULL)) {
        return NULL;
    }
    ui_FontForgetTextures();
    ui_SetScale(ui_ScaleFor((int)s->preset, s->outputHeight));
    ui_InstallDeferredText(renderer);
    s_ovWord = ovWord;
    rd_SetPresentOverlay(overlay, NULL);
    for (int i = 0; i < nFrames; i++) {
        recordFrame(&frames[i]);
    }
    uint8_t *px = malloc((size_t)s->outputWidth * s->outputHeight * 4);
    uint32_t w = 0, h = 0;
    if (!px || !rd_ReadPresented(px, &w, &h) || w != s->outputWidth || h != s->outputHeight) {
        CHECK(0, "the presented %ux%u output", s->outputWidth, s->outputHeight);
        free(px);
        px = NULL;
    }
    rd_SetPresentOverlay(NULL, NULL);
    ui_InstallDeferredText(0);
    ui_FontShutdown();
    CHECK(rhi_vk_ValidationErrorCount() == 0, "%u validation errors",
          rhi_vk_ValidationErrorCount());
    rd_Shutdown();
    return px;
}

static void writePng(const char *name, const uint8_t *px, uint32_t w, uint32_t h)
{
    char p[1100];
    snprintf(p, sizeof(p), "%s/%s", s_dir, name);
    rd_WritePng(p, px, w, h, w * 4, 0);
    printf("font_edge: %s\n", p);
}

/* -------------------------------------------------------------- edges */

static int lum(const uint8_t *px, uint32_t w, int x, int y)
{
    return px[((size_t)y * w + (size_t)x) * 4 + 1]; /* white on black: any channel */
}

/* the ink's bounding box (value > 128) inside [x0, x1) x [y0, y1) */
static bool inkBox(const uint8_t *px, uint32_t w, int x0, int y0, int x1, int y1, int b[4])
{
    b[0] = b[1] = 1 << 30;
    b[2] = b[3] = -1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (lum(px, w, x, y) > 128) {
                b[0] = x < b[0] ? x : b[0];
                b[1] = y < b[1] ? y : b[1];
                b[2] = x > b[2] ? x : b[2];
                b[3] = y > b[3] ? y : b[3];
            }
        }
    }
    return b[2] >= 0;
}

/* along row y from x0 to x1: the edges (runs between background, 0, and
   ink, ink or more) and the widest run of pixels strictly between; returns
   the number of edges */
static int rowEdges(const uint8_t *px, uint32_t w, int y, int x0, int x1, int ink, int *worst)
{
    int edges = 0, run = 0;
    *worst = 0;
    int last = -1; /* 0 background, 1 ink */
    for (int x = x0; x <= x1; x++) {
        const int v = lum(px, w, x, y);
        if (v == 0 || v >= ink) {
            const int now = v >= ink;
            if (last >= 0 && now != last) {
                edges++;
            }
            if (last >= 0 && run > *worst) {
                *worst = run;
            }
            run = 0;
            last = now;
        } else {
            run++;
        }
    }
    return edges;
}

/* the H in [x0, x1): rows a fifth, three tenths, seven tenths and four
   fifths down its ink, each with four edges (two stems: background to the
   row's brightest and back, twice); returns the widest run of pixels
   strictly between on an edge.  strict: the ink is 255 */
static int checkH(const char *what, const uint8_t *px, uint32_t w, uint32_t h, int x0, int x1,
                  int strict)
{
    int b[4];
    if (!inkBox(px, w, x0, 0, x1, (int)h, b)) {
        CHECK(0, "%s: no ink", what);
        return 99;
    }
    int worstAll = 0;
    const int hgt = b[3] - b[1];
    for (int k = 0; k < 4; k++) {
        static const float at[4] = {0.2f, 0.3f, 0.7f, 0.8f};
        const int y = b[1] + (int)((float)hgt * at[k]);
        int ink = 0;
        for (int x = b[0]; x <= b[2]; x++) {
            ink = lum(px, w, x, y) > ink ? lum(px, w, x, y) : ink;
        }
        CHECK(!strict || ink == 255, "%s: row %d peaks at %d (255)", what, y, ink);
        int worst = 0;
        const int edges = rowEdges(px, w, y, b[0] - 3, b[2] + 3, ink, &worst);
        CHECK(edges == 4, "%s: row %d has %d edges (two stems: 4)", what, y, edges);
        worstAll = worst > worstAll ? worst : worstAll;
    }
    printf("font_edge: %s: ink %dx%d at (%d,%d), at most %d pixel(s) between on an edge\n", what,
           b[2] - b[0] + 1, hgt + 1, b[0], b[1], worstAll);
    return worstAll;
}

/* the grid x of a word to an output column (font.h ui_BeginOverlay) */
static int colOf(const RdSettings *s, float gx)
{
    RhiRect box;
    rd__PresentBox(s->outputWidth, s->outputHeight, s->aspect, &box);
    const float w43 = fminf((float)box.w, (float)box.h * 4.0f / 3.0f);
    return (int)((float)box.x + ((float)box.w - w43) * 0.5f + gx * w43 / 640.0f);
}

static void checkEdgesAt(uint32_t w, uint32_t h)
{
    const RdSettings s = settingsOf(1, w, h, 0);
    const Frame fr = {.deferred = 1, .row = 1, .word = "H", .rowY = WORD_Y};
    uint8_t *px = present(&s, 1, 1, &fr, 1);
    uint8_t *quads = present(&s, 0, 1, &fr, 1);
    if (!px || !quads) {
        free(px);
        free(quads);
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "font_edge_%u.png", h);
    writePng(name, px, w, h);
    snprintf(name, sizeof(name), "font_edge_%u_quads.png", h);
    writePng(name, quads, w, h);
    const int mid = colOf(&s, (DEF_X + OVL_X) * 0.5f);
    char what[64];
    snprintf(what, sizeof(what), "%u deferred H", h);
    const int d = checkH(what, px, w, h, 0, mid, 1);
    snprintf(what, sizeof(what), "%u overlay H", h);
    const int o = checkH(what, px, w, h, mid, (int)w, 1);
    snprintf(what, sizeof(what), "%u quad-path H", h);
    const int q = checkH(what, quads, w, h, 0, mid, 0);
    CHECK(d <= 1, "%u: the deferred H has %d pixels between on an edge (at most 1)", h, d);
    CHECK(o <= 1, "%u: the overlay H has %d pixels between on an edge (at most 1)", h, o);
    CHECK(q > 1, "%u: the quad path (scene, reduced, scaled) is soft: %d", h, q);
    /* the mirror leaves the deferred text where it is (the scene is black,
       so the whole output is the same) */
    const RdSettings sm = settingsOf(1, w, h, 1);
    uint8_t *mir = present(&sm, 1, 1, &fr, 1);
    CHECK(mir && memcmp(mir, px, (size_t)w * h * 4) == 0,
          "%u: the mirrored present equals the unmirrored one", h);
    free(mir);
    free(px);
    free(quads);
}

/* ------------------------------------------------------------ original */

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t hsh = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        hsh ^= p[i];
        hsh *= 0x100000001b3ull;
    }
    return hsh;
}

static void checkOriginal(void)
{
    const RdSettings s = settingsOf(0, 960, 720, 0);
    const Frame def = {
        .deferred = 1, .row = 1, .word = "New Game", .fade = 1 + 0x30, .rowY = WORD_Y};
    Frame plain = def;
    plain.deferred = 0;
    uint8_t *a = present(&s, 1, 0, &def, 1);
    uint8_t *b = present(&s, 1, 0, &plain, 1);
    uint8_t *c = present(&s, 0, 0, &def, 1);
    if (a && b && c) {
        const size_t n = (size_t)960 * 720 * 4;
        printf("font_edge: Original present %016llx (deferred row), %016llx (plain), %016llx "
               "(no renderer)\n",
               (unsigned long long)fnv(a, n), (unsigned long long)fnv(b, n),
               (unsigned long long)fnv(c, n));
        CHECK(memcmp(a, b, n) == 0, "Original: a deferred row presents as the plain quads");
        CHECK(memcmp(a, c, n) == 0, "Original: the renderer changes nothing");
        writePng("font_edge_original.png", a, 960, 720);
    }
    free(a);
    free(b);
    free(c);
}

/* ---------------------------------------------------------------- fold */

/* the brightest pixel in [x0, x1) */
static int peak(const uint8_t *px, uint32_t w, uint32_t h, int x0, int x1, int y0, int y1)
{
    int m = 0;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < (int)h; y++) {
        for (int x = x0; x < x1; x++) {
            const int v = lum(px, w, x, y);
            m = v > m ? v : m;
        }
    }
    return m;
}

static void checkFold(void)
{
    const uint32_t w = 1280, h = 720;
    const RdSettings s = settingsOf(1, w, h, 0);
    const int mid = colOf(&s, (DEF_X + OVL_X) * 0.5f);
    Frame fr = {.deferred = 1, .row = 1, .word = "H", .rowY = WORD_Y};
    uint8_t *px = present(&s, 1, 0, &fr, 1);
    const int full = px ? peak(px, w, h, 0, mid, 0, (int)h) : -1;
    free(px);
    CHECK(full == 255, "fold: the row alone peaks at %d (255)", full);
    fr.fade = 1 + 0x80;
    px = present(&s, 1, 0, &fr, 1);
    const int black = px ? peak(px, w, h, 0, mid, 0, (int)h) : -1;
    free(px);
    CHECK(black == 0, "fold: under a fade to black at 0x80 the row peaks at %d (0)", black);
    fr.fade = 1 + 0x40;
    px = present(&s, 1, 0, &fr, 1);
    const int half = px ? peak(px, w, h, 0, mid, 0, (int)h) : -1;
    free(px);
    CHECK(half >= 126 && half <= 129, "fold: under a fade at 0x40 the row peaks at %d (128)", half);
    /* a row across the top band's edge (58 of 512 lines, grid y 2 + 58 *
       448 / 512 = 52.75): the letterbox at 0x80 cuts the part above */
    fr.fade = 0;
    fr.letterbox = 1 + 0x80;
    fr.rowY = 52.75f;
    px = present(&s, 1, 0, &fr, 1);
    if (px) {
        RhiRect box;
        rd__PresentBox(w, h, s.aspect, &box);
        const int edge = box.y + (int)lrintf(58.0f * (float)box.h / 512.0f);
        const int above = peak(px, w, h, 0, mid, 0, edge);
        const int below = peak(px, w, h, 0, mid, edge, (int)h);
        CHECK(above == 0 && below == 255,
              "fold: the letterbox cuts the row at line %d (above %d, below %d; 0, 255)", edge,
              above, below);
        writePng("font_edge_letterbox.png", px, w, h);
    }
    free(px);
    /* KEEP after the row: gone; a keep frame's row after its KEEP: drawn */
    fr.letterbox = 0;
    fr.rowY = WORD_Y;
    fr.keepAfter = 1;
    px = present(&s, 1, 0, &fr, 1);
    const int kept = px ? peak(px, w, h, 0, mid, 0, (int)h) : -1;
    free(px);
    CHECK(kept == 0, "fold: a row before a KEEP is not drawn (peak %d)", kept);
    Frame two[2] = {{.deferred = 1, .row = 0, .word = "H", .rowY = WORD_Y},
                    {.deferred = 1, .row = 1, .word = "H", .keepBefore = 1, .rowY = WORD_Y}};
    px = present(&s, 1, 0, two, 2);
    const int keepFrame = px ? peak(px, w, h, 0, mid, 0, (int)h) : -1;
    free(px);
    CHECK(keepFrame == 255, "fold: a keep frame's row after its KEEP is drawn (peak %d)",
          keepFrame);
}

/* ---------------------------------------------------------------- glow */

/* package GHOST: the selected row's glow (lt_glow_sprite: the row stretched,
   additive) is the game's sheet stretched, a soft picture.  Deferred, it is
   rasterised at the sheets' density and magnified (font.h UI_GLOW_SCALE):
   across the stem of an "I" its blue rises and falls over several pixels.
   Drawn a texel a pixel like the label, it rose in about one pixel, a sharp
   stretched copy of the letters beside the label's: the ghost. */
static void checkGlow(void)
{
    const uint32_t w = 1920, h = 1080;
    const RdSettings s = settingsOf(1, w, h, 0);
    const int mid = colOf(&s, (DEF_X + OVL_X) * 0.5f);
    const Frame fr = {.deferred = 1, .row = 1, .glow = 1, .word = "I", .rowY = WORD_Y};
    uint8_t *px = present(&s, 1, 0, &fr, 1);
    if (!px) {
        return;
    }
    writePng("font_edge_glow.png", px, w, h);
    /* the glow's extent: blue above 0 */
    int b[4] = {1 << 30, 1 << 30, -1, -1};
    for (int y = 0; y < (int)h; y++) {
        for (int x = 0; x < mid; x++) {
            if (px[((size_t)y * w + (size_t)x) * 4 + 2] > 0) {
                b[0] = x < b[0] ? x : b[0];
                b[1] = y < b[1] ? y : b[1];
                b[2] = x > b[2] ? x : b[2];
                b[3] = y > b[3] ? y : b[3];
            }
        }
    }
    CHECK(b[2] >= 0, "glow: drawn");
    if (b[2] >= 0) {
        const int y = (b[1] + b[3]) / 2;
        int peakB = 0, step = 0;
        for (int x = b[0] - 2; x <= b[2] + 2; x++) {
            const int v = px[((size_t)y * w + (size_t)x) * 4 + 2];
            const int u = px[((size_t)y * w + (size_t)x - 1) * 4 + 2];
            peakB = v > peakB ? v : peakB;
            step = abs(v - u) > step ? abs(v - u) : step;
        }
        printf("font_edge: glow I: %d px wide, blue peaks at %d, steps at most %d a pixel\n",
               b[2] - b[0] + 1, peakB, step);
        CHECK(peakB >= 64, "glow: the blue peaks at %d (at least 64)", peakB);
        CHECK(step * 3 <= peakB,
              "glow: across the stem the blue steps %d in a pixel, more than a third of its "
              "peak %d: a sharp copy, not a glow",
              step, peakB);
    }
    free(px);
    /* to look at: a title row, its halo and its glow over the fog's grey */
    const Frame title = {
        .deferred = 1, .row = 1, .glow = 2, .bg = 0x90, .word = "Continue", .rowY = WORD_Y};
    px = present(&s, 1, 0, &title, 1);
    if (px) {
        writePng("font_edge_glow_title.png", px, w, h);
    }
    free(px);
}

/* ------------------------------------------------------------ faces */

/* a game face of two letters drawn on a sheet: I (2 x 10 texels) and L */
static int loadSyntheticFace(void)
{
    enum { W = 128, H = 20 };

    static uint8_t sheet[W * H * 4];
    memset(sheet, 0, sizeof(sheet));
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = sheet + ((size_t)y * W + (size_t)x) * 4;
            p[0] = p[1] = p[2] = 255;
            const int inI = x >= 10 && x < 12 && y >= 5 && y < 15;
            const int inL =
                (x >= 20 && x < 22 && y >= 5 && y < 15) || (x >= 20 && x < 26 && y >= 13 && y < 15);
            p[3] = (inI || inL) ? 255 : 0;
        }
    }
    UiGfBuilder *b = ui_GfBuilderNew();
    if (!b) {
        return 0;
    }
    UiGfSource src;
    memset(&src, 0, sizeof(src));
    src.rgba = sheet;
    src.sheetW = W;
    src.sheetH = H;
    src.w = W;
    src.h = H;
    src.em = 13.5f;
    src.capMid = 10.0f;
    src.pitch = 15.5f;
    src.text = "I L";
    src.sheet = ui_GfBuilderSheet(b, "synthetic.tm2");
    ui_GfBuilderAdd(b, &src);
    uint8_t *blob = NULL;
    size_t size = 0;
    const int ok =
        ui_GfBuilderFinish(b, 13.5f, &blob, &size, NULL) == 0 && ui_GameFaceLoad(blob, size);
    free(blob);
    ui_GfBuilderFree(b);
    return ok;
}

static void checkFaces(void)
{
    CHECK(loadSyntheticFace(), "the synthetic game face loads");
    CHECK(ui_FontFaceOf('I') == UI_FACE_GAME && ui_FontFaceOf('H') == UI_FACE_ARIMO,
          "I from the game face, H from Arimo");
    const uint32_t w = 1920, h = 1080;
    const RdSettings s = settingsOf(1, w, h, 0);
    const int mid = colOf(&s, (DEF_X + OVL_X) * 0.5f);
    /* the Arimo fallback: as sharp as before */
    const Frame fh = {.deferred = 1, .row = 1, .word = "H", .rowY = WORD_Y};
    uint8_t *px = present(&s, 1, 0, &fh, 1);
    if (px) {
        writePng("font_edge_1080_fallback.png", px, w, h);
        const int d = checkH("1080 deferred H (Arimo, the game face loaded)", px, w, h, 0, mid, 1);
        CHECK(d <= 1, "the Arimo fallback has %d pixels between on an edge (at most 1)", d);
    }
    free(px);
    /* the game face's I: drawn, soft (its 2-texel stem scaled about 4.8
       times vertically, bilinearly), not held to the edge check */
    const Frame fi = {.deferred = 1, .row = 1, .word = "I", .rowY = WORD_Y};
    px = present(&s, 1, 0, &fi, 1);
    if (px) {
        writePng("font_edge_1080_game.png", px, w, h);
        int b[4];
        CHECK(inkBox(px, w, 0, 0, mid, (int)h, b), "the game face's I is drawn");
        if (inkBox(px, w, 0, 0, mid, (int)h, b)) {
            const int y = (b[1] + b[3]) / 2;
            int ink = 0, worst = 0;
            for (int x = b[0]; x <= b[2]; x++) {
                ink = lum(px, w, x, y) > ink ? lum(px, w, x, y) : ink;
            }
            rowEdges(px, w, y, b[0] - 3, b[2] + 3, ink, &worst);
            printf("font_edge: 1080 game-face I: ink %dx%d, %d pixel(s) between on an edge (soft "
                   "by design)\n",
                   b[2] - b[0] + 1, b[3] - b[1] + 1, worst);
            CHECK(worst > 1, "the game face is its bitmap scaled bilinearly: %d between", worst);
        }
    }
    free(px);
    ui_GameFaceUnload();
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    RdSettings probe = settingsOf(0, 64, 48, 0);
    if (!rd_Init(512, 512, &probe, NULL)) {
        printf("font_edge_test: SKIP: no usable Vulkan device\n");
        return 77;
    }
    printf("font_edge_test: adapter %s\n", rhi_AdapterName());
    rd_Shutdown();
    checkEdgesAt(1920, 1080);
    checkEdgesAt(3840, 2160);
    checkOriginal();
    checkFold();
    checkGlow();
    checkFaces();
    if (failures) {
        printf("font_edge_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("font_edge_test: ok\n");
    return 0;
}
