/* font_edge_test.c: the port's plain text at the output's resolution.
 *
 * port/ui's font.c and rd on a Vulkan device (lavapipe here; exit 77 without
 * one).  A frame of black SCENE with a row of text recorded in list 11 by
 * ui_DrawText (the plain glyph path: the scene's quads) and the reduction in
 * list 12, presented with an overlay callback that writes a second word
 * through font.c's overlay mode (as the touch labels do):
 *   edges     Enhanced, 1920 x 1080 (16:9) and 3840 x 2160: on rows through
 *             the stems of the overlay "H", every edge has at most one pixel
 *             strictly between the background and the ink (the glyphs are
 *             rasterised at the shown size and drawn a texel a pixel); the
 *             scene's "H" (reduced and scaled with the scene) has more
 *   mirror    the mirror mode leaves the overlay's text where it was (with
 *             the scene black, the output equal to the unmirrored one)
 * v0.4.2 (package F-B): the deferred menu rows, their halo, fold and glow
 * checks and the game face's are gone with the deferral and the face; the
 * menus' text is the sheet strips (menu_font.h; ui_test, menu_text_test).
 *
 * Usage: font_edge_test [dir]  (dir: where the PNGs go)
 * Exit 0, 1 on a failure, 77 without a device.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
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

/* the two words: the scene's left of the centre, the overlay's right */
#define DEF_X 220.0f
#define OVL_X 420.0f
#define WORD_Y 226.0f
#define WORD_SIZE 40.0f

/* ------------------------------------------------------------ the frame */

typedef struct Frame {
    int row; /* the scene's row at all */
    const char *word;
    float rowY;
} Frame;

static void scissorAll(void)
{
    rd_SetTarget(rd_Target(RD_TARGET_SCENE), rd_Target(RD_TARGET_SCENE), 512, 512, 1);
}

static void recordFrame(const Frame *fr)
{
    static const uint8_t bg[4] = {0, 0, 0, 0x80};
    rd_BeginFrame();
    rd_SelectList(0);
    rd_ClearTarget(rd_Target(RD_TARGET_SCENE), bg, 1, 0);
    scissorAll();
    rd_SelectList(11);
    scissorAll();
    if (fr->row) {
        ui_DrawText(DEF_X, fr->rowY, WORD_SIZE, kWhite, fr->word,
                    UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
    }
    RdPostParams pp;
    rd_SelectList(12);
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = pp.rgba[1] = pp.rgba[2] = 128;
    rd_Post(RD_POST_REDUCTION, &pp);
    rd_EndFrame(0);
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

/* rd up with s and the overlay as asked, the frames recorded and
 * presented; the last output into a new buffer (NULL on failure) */
static uint8_t *present(const RdSettings *s, int ovWord, const Frame *frames, int nFrames)
{
    if (!rd_Init(512, 512, s, NULL)) {
        return NULL;
    }
    ui_FontForgetTextures();
    ui_SetScale(ui_ScaleFor((int)s->preset, s->outputHeight));
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
    const Frame fr = {.row = 1, .word = "H", .rowY = WORD_Y};
    uint8_t *px = present(&s, 1, &fr, 1);
    if (!px) {
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "font_edge_%u.png", h);
    writePng(name, px, w, h);
    const int mid = colOf(&s, (DEF_X + OVL_X) * 0.5f);
    char what[64];
    snprintf(what, sizeof(what), "%u overlay H", h);
    const int o = checkH(what, px, w, h, mid, (int)w, 1);
    snprintf(what, sizeof(what), "%u scene H", h);
    const int q = checkH(what, px, w, h, 0, mid, 0);
    CHECK(o <= 1, "%u: the overlay H has %d pixels between on an edge (at most 1)", h, o);
    CHECK(q > 1, "%u: the scene's H (reduced, scaled) is soft: %d", h, q);
    /* the mirror leaves the overlay's text where it is (the scene black and
       empty, so the whole output is the same) */
    const Frame none = {.row = 0, .word = "H", .rowY = WORD_Y};
    uint8_t *plain = present(&s, 1, &none, 1);
    const RdSettings sm = settingsOf(1, w, h, 1);
    uint8_t *mir = present(&sm, 1, &none, 1);
    CHECK(plain && mir && memcmp(mir, plain, (size_t)w * h * 4) == 0,
          "%u: the mirrored present equals the unmirrored one", h);
    free(mir);
    free(plain);
    free(px);
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
    if (failures) {
        printf("font_edge_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("font_edge_test: ok\n");
    return 0;
}
