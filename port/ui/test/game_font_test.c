/* game_font_test.c: the game face's builder on a synthetic sheet (package
 * GFONT; docs/port/UI.md, "The font").  CPU only, no disc.
 *
 * A sheet is drawn with four block letters of known shapes (H, I, L, T at a
 * 10-texel capital height, the menu rows' 13.5-texel em), light ink with a
 * soft dark rim as the PAL sheets have it, in three rectangles:
 *   "H I L T"  the letters a space apart (one component a letter)
 *   "HILT"     two texels apart (one component a letter)
 *   "LITH"     the T touching the H (one component for the pair)
 * and handed to the builder with the transcribed words.  Checks:
 *   - every line aligned, one touching pair split, the four characters kept,
 *     each from the main class, seen as often as the sheet shows it;
 *   - each kept bitmap is the letter's: its ink cell's width is the shape's
 *     plus the antialiasing border, and the split T is as wide as a free one;
 *   - the advances: "HILT" set by the face measures the drawn word, a
 *     junction measures the drawn two texels within half a texel, and the
 *     space is the drawn word gap less the bearings;
 *   - the main cell is the letter's columns and the line's rows, the caps 10
 *     columns;
 *   - a sheet of the one word "HILT", built alone and set again from the atlas
 *     at the sheet's size, drawn dimmed as the sprite is (the alpha in black,
 *     the light added: MODULATE) against the sheet's sprite drawn the same
 *     way: within 2 levels on every texel within two of the letters' ink;
 *   - letters at the sheet's very top and bottom, in rectangles of their
 *     capitals' rows alone (the fitted glow reaching past them): built
 *     without reading outside the sheet's texels (run under ASan);
 *   - the blob loads (ui_GameFaceLoad), the face serves H I L T and falls
 *     back to Arimo for the rest, and a blob with a wrong version or size is
 *     refused.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "game_font.h"

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

enum { SW = 256, SH = 96, CAP = 10, STROKE = 2 };

static float s_ink[SH][SW];

/* the letters' shapes: width in texels, drawn with their left edge at x and
   their baseline at y */
static int letterW(char c)
{
    switch (c) {
    case 'H':
        return 7;
    case 'I':
        return 2;
    case 'L':
        return 6;
    case 'T':
        return 8;
    default:
        return 0;
    }
}

static void fill(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (x >= 0 && y >= 0 && x < SW && y < SH) {
                s_ink[y][x] = 1.0f;
            }
        }
    }
}

static void drawLetter(char c, int x, int base)
{
    const int top = base - CAP, w = letterW(c);
    switch (c) {
    case 'H':
        fill(x, top, x + STROKE, base);
        fill(x + w - STROKE, top, x + w, base);
        fill(x, top + 4, x + w, top + 6);
        break;
    case 'I':
        fill(x, top, x + STROKE, base);
        break;
    case 'L':
        fill(x, top, x + STROKE, base);
        fill(x, base - STROKE, x + w, base);
        break;
    case 'T':
        fill(x, top, x + w, top + STROKE);
        fill(x + 3, top, x + 5, base);
        break;
    }
}

/* draws a word: gaps[i] texels after letter i ("touch" = 0 makes the
   neighbours one component: the T's bar reaches the H's stem) */
static int drawWord(const char *w, int x, int base, const int *gaps)
{
    for (int i = 0; w[i]; i++) {
        if (w[i] == ' ') {
            continue;
        }
        drawLetter(w[i], x, base);
        x += letterW(w[i]) + gaps[i];
    }
    return x;
}

/* light ink with a dark rim: the rim's alpha a soft halo of the ink, the
   texel's light the ink (white) over it */
static void makeSheet(uint8_t *rgba)
{
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            float halo = 0.0f;
            for (int dy = -3; dy <= 3; dy++) {
                for (int dx = -3; dx <= 3; dx++) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < SW && yy < SH && s_ink[yy][xx] > 0.0f) {
                        const float d = sqrtf((float)(dx * dx + dy * dy));
                        const float a = 0.8f * (1.0f - d / 4.0f);
                        halo = a > halo ? a : halo;
                    }
                }
            }
            const float c = s_ink[y][x];
            const float A = c > halo ? c : halo;
            uint8_t *p = rgba + ((size_t)y * SW + (size_t)x) * 4;
            p[0] = p[1] = p[2] = (uint8_t)(A > 0.0f ? lrintf(255.0f * c / A) : 255);
            p[3] = (uint8_t)lrintf(255.0f * A);
        }
    }
}

/* The sheet's own texels: a word whose letters each appear once, built
   alone, set again from the atlas at the sheet's size and drawn as the
   sprite is (the alpha in black, the light added: MODULATE), dimmed as the
   layout dims an unselected row (the colour halved, the alpha kept) over a
   light background, against the sheet's sprite drawn the same way: every
   texel within two of the letters' ink within 2 levels of 255. */
static int checkSpriteExact(const uint8_t *sheet, int sw, int sh)
{
    UiGfBuilder *b = ui_GfBuilderNew();
    const int id = ui_GfBuilderSheet(b, "one.tm2");
    UiGfSource s;
    memset(&s, 0, sizeof(s));
    s.rgba = sheet;
    s.sheetW = sw;
    s.sheetH = sh;
    s.v = 21;
    s.w = 200;
    s.h = 20;
    s.em = 13.5f;
    s.capMid = 9.0f;
    s.pitch = 15.5f;
    s.text = "HILT";
    s.sheet = id;
    ui_GfBuilderAdd(b, &s);
    uint8_t *blob = NULL;
    size_t size = 0;
    const int ok = ui_GfBuilderFinish(b, 13.5f, &blob, &size, NULL) == 0;
    ui_GfBuilderFree(b);
    if (!ok) {
        return 255;
    }
    const int aw = blob[8] | (blob[9] << 8);
    const float space = 0.0f;
    (void)space;
    const uint32_t ng = (uint32_t)blob[24] | ((uint32_t)blob[25] << 8);
    const uint32_t nk = (uint32_t)blob[28] | ((uint32_t)blob[29] << 8);
    const uint32_t ns = (uint32_t)blob[32] | ((uint32_t)blob[33] << 8);
    const uint8_t *atlas =
        blob + UI_GF_HEADER + ng * UI_GF_GLYPH + nk * UI_GF_KERN + ns * UI_GF_SHEET_NAME;

    /* the composition on a canvas of the rectangle's size, the word's pen
       from 0; then the best whole-texel placement against the sheet */
    enum { CW = 200, CH = 20, VH = 64 };

    static float alpha[VH][CW + 40], light[VH][CW + 40], glow[VH][CW + 40];
    memset(alpha, 0, sizeof(alpha));
    memset(light, 0, sizeof(light));
    memset(glow, 0, sizeof(glow));
    const char *word = "HILT";
    int pen = 20; /* room for the left cap */
    for (int i = 0; word[i]; i++) {
        const uint8_t *g = blob + UI_GF_HEADER;
        while ((char)g[0] != word[i]) {
            g += UI_GF_GLYPH;
        }
        float adv, gdx, gdy;
        memcpy(&adv, g + 72, 4);
        memcpy(&gdx, g + 80, 4);
        memcpy(&gdy, g + 84, 4);
        {
            /* the fitted glow, overlapping: the transmittances multiply */
            const int v = (i == 0 ? 1 : 0) | (word[i + 1] ? 0 : 2);
            const int gx = g[92 + v * 4] | (g[93 + v * 4] << 8);
            const int gy = g[94 + v * 4] | (g[95 + v * 4] << 8);
            const int gw = g[88] | (g[89] << 8), gh = g[90] | (g[91] << 8);
            for (int y = 0; y < gh; y++) {
                for (int x = 0; x < gw; x++) {
                    const int cx = pen + (int)lrintf(gdx) + x, cy = 32 + (int)lrintf(gdy) + y;
                    if (cx >= 0 && cx < CW + 40 && cy >= 0 && cy < VH) {
                        const float v =
                            (float)atlas[(size_t)(gy + y) * (size_t)aw + (size_t)(gx + x)] / 255.0f;
                        glow[cy][cx] = 1.0f - (1.0f - glow[cy][cx]) * (1.0f - v);
                    }
                }
            }
        }
        for (int k = 0; k < 3; k++) {
            if ((k == 1 && i != 0) || (k == 2 && word[i + 1])) {
                continue; /* the caps at the word's ends only */
            }
            const uint8_t *r = g + 16 + k * 16;
            int ax = r[0] | (r[1] << 8), ay = r[2] | (r[3] << 8);
            int px = r[4] | (r[5] << 8), py = r[6] | (r[7] << 8);
            const int variant = (i == 0 ? 1 : 0) | (word[i + 1] ? 0 : 2);
            if (k == 0 && variant > 0) {
                const uint8_t *m = g + 108 + (variant - 1) * 8;
                ax = m[0] | (m[1] << 8);
                ay = m[2] | (m[3] << 8);
                px = m[4] | (m[5] << 8);
                py = m[6] | (m[7] << 8);
            }
            const int cw = r[8] | (r[9] << 8), ch = r[10] | (r[11] << 8);
            float top;
            memcpy(&top, r + 12, 4);
            const int inner = cw - 2;
            const int x0 = k == 0 ? pen : k == 1 ? pen - inner : pen + (int)lrintf(adv);
            for (int y = 0; y < ch - 2; y++) {
                for (int x = 0; x < inner; x++) {
                    const int cx = x0 + x, cy = 32 + (int)lrintf(top) + y;
                    if (cx < 0 || cx >= CW + 40 || cy < 0 || cy >= VH) {
                        continue;
                    }
                    alpha[cy][cx] =
                        (float)atlas[(size_t)(ay + 1 + y) * (size_t)aw + (size_t)(ax + 1 + x)] /
                        255.0f;
                    light[cy][cx] =
                        (float)atlas[(size_t)(py + 1 + y) * (size_t)aw + (size_t)(px + 1 + x)] /
                        255.0f;
                }
            }
        }
        pen += (int)lrintf(adv);
    }
    /* dimmed: the colour halved, the alpha kept; a light background.  The
       texels within two of the letters' ink (the letters, their antialiasing
       and the rim's dark edge: what the eye reads) are the sheet's: within 2
       levels; the far glow is the fitted one and is not compared */
    const float bg = 0.8f, col = 0.5f;
    int best = 255;
    for (int dy = 0; dy + CH <= VH; dy++) {
        for (int dx = -30; dx <= 30; dx++) {
            int worst = 0;
            for (int y = 0; y < CH; y++) {
                for (int x = 0; x < CW; x++) {
                    /* within two texels of the letters' ink */
                    int near = 0;
                    for (int v = -2; v <= 2 && !near; v++) {
                        for (int u = -2; u <= 2; u++) {
                            const int yy = 21 + y + v, xx = x + u;
                            if (u * u + v * v <= 4 && yy >= 0 && yy < sh && xx >= 0 && xx < sw &&
                                s_ink[yy][xx] > 0.1f) {
                                near = 1;
                                break;
                            }
                        }
                    }
                    if (!near) {
                        continue;
                    }
                    const uint8_t *t = sheet + ((size_t)(21 + y) * (size_t)sw + (size_t)x) * 4;
                    const float A = (float)t[3] / 255.0f, L = (float)t[0] / 255.0f;
                    const float sprite = bg * (1.0f - A) + col * L * A;
                    const int cx = x + dx;
                    const float a = cx >= 0 && cx < CW + 40 ? 1.0f - (1.0f - alpha[y + dy][cx]) *
                                                                         (1.0f - glow[y + dy][cx])
                                                            : 0.0f;
                    const float p = cx >= 0 && cx < CW + 40 ? light[y + dy][cx] : 0.0f;
                    const float atl = bg * (1.0f - a) + col * p;
                    const int d = abs((int)lrintf(sprite * 255.0f) - (int)lrintf(atl * 255.0f));
                    worst = d > worst ? d : worst;
                }
            }
            best = worst < best ? worst : best;
        }
    }
    free(blob);
    return best;
}

/* Letters at the sheet's top and bottom edges, each in a rectangle of its
   capitals' rows alone: the fitted glow reaches rows past the rectangle,
   where the sheet has no texels to keep (the builder must not read them).
   Returns the glyphs built, -1 on a failed build. */
static int checkEdges(void)
{
    static uint8_t edge[SW * SH * 4];
    static const int close[] = {2, 2, 2, 2};
    memset(s_ink, 0, sizeof(s_ink));
    drawWord("HILT", 10, CAP, close); /* rows 0..CAP-1 */
    drawWord("HILT", 10, SH, close);  /* the last CAP rows */
    makeSheet(edge);
    UiGfBuilder *b = ui_GfBuilderNew();
    if (!b) {
        return -1;
    }
    const int id = ui_GfBuilderSheet(b, "edges.tm2");
    for (int r = 0; r < 2; r++) {
        UiGfSource s;
        memset(&s, 0, sizeof(s));
        s.rgba = edge;
        s.sheetW = SW;
        s.sheetH = SH;
        s.v = r ? SH - CAP : 0;
        s.w = 200;
        s.h = CAP;
        s.em = 13.5f;
        s.capMid = 0.5f * CAP;
        s.pitch = 15.5f;
        s.text = "HILT";
        s.sheet = id;
        CHECK(ui_GfBuilderAdd(b, &s) == 0, "edges: rectangle %d", r);
    }
    uint8_t *blob = NULL;
    size_t size = 0;
    UiGfStats st;
    memset(&st, 0, sizeof(st));
    const int ok = ui_GfBuilderFinish(b, 13.5f, &blob, &size, &st) == 0;
    ui_GfBuilderFree(b);
    free(blob);
    printf("game_font_test: letters at the sheet's top and bottom edges: %s, %d lines "
           "(%d unaligned), %d glyphs\n",
           ok ? "built" : "FAILED", st.lines, st.unaligned, st.glyphs);
    CHECK(ok && st.lines == 2 && st.unaligned == 0, "edges: both lines aligned and built");
    return ok ? st.glyphs : -1;
}

int main(void)
{
    static uint8_t sheet[SW * SH * 4];
    /* rows 20 texels high: the capitals' middle 9.5, the baseline 14.5 */
    static const int spaced[] = {7, 7, 7, 7, 7, 7, 7};
    static const int close[] = {2, 2, 2, 2};
    static const int touch[] = {2, 2, 0, 2}; /* L I T|H: T touches H */
    drawWord("H I L T", 10, 15, spaced);
    drawWord("HILT", 10, 35, close);
    drawWord("LITH", 10, 55, touch);
    drawWord("HILT", 10, 75, close);
    makeSheet(sheet);
    {
        /* a sheet of one word, for the sprite check */
        static uint8_t one[SW * SH * 4];
        memset(s_ink, 0, sizeof(s_ink));
        drawWord("HILT", 10, 35, close);
        makeSheet(one);
        const int d = checkSpriteExact(one, SW, SH);
        printf("game_font_test: \"HILT\" from the atlas, dimmed, against the sheet's sprite: "
               "at most %d levels apart\n",
               d);
        CHECK(d <= 2, "the atlas's word is the sheet's sprite within 2 levels (%d)", d);
        /* the four-rectangle sheet again */
        memset(s_ink, 0, sizeof(s_ink));
        drawWord("H I L T", 10, 15, spaced);
        drawWord("HILT", 10, 35, close);
        drawWord("LITH", 10, 55, touch);
        drawWord("HILT", 10, 75, close);
        makeSheet(sheet);
    }

    CHECK(checkEdges() == 4, "edges: the four characters");
    /* the four-rectangle sheet again */
    memset(s_ink, 0, sizeof(s_ink));
    drawWord("H I L T", 10, 15, spaced);
    drawWord("HILT", 10, 35, close);
    drawWord("LITH", 10, 55, touch);
    drawWord("HILT", 10, 75, close);
    makeSheet(sheet);

    UiGfBuilder *b = ui_GfBuilderNew();
    CHECK(b != NULL, "builder");
    if (!b) {
        return 1;
    }
    const int sh = ui_GfBuilderSheet(b, "synthetic.tm2");
    static const char *const words[4] = {"H I L T", "HILT", "LITH", "HILT"};
    for (int r = 0; r < 4; r++) {
        UiGfSource s;
        memset(&s, 0, sizeof(s));
        s.rgba = sheet;
        s.sheetW = SW;
        s.sheetH = SH;
        s.u = 0;
        s.v = r * 20 + 1;
        s.w = 200;
        s.h = 20;
        s.em = 13.5f;
        s.capMid = 9.0f;
        s.pitch = 15.5f;
        s.text = words[r];
        s.sheet = sh;
        s.lang = 0;
        CHECK(ui_GfBuilderAdd(b, &s) == 0, "rectangle %d", r);
    }
    uint8_t *blob = NULL;
    size_t size = 0;
    UiGfStats st;
    CHECK(ui_GfBuilderFinish(b, 13.5f, &blob, &size, &st) == 0, "finish");
    ui_GfBuilderFree(b);
    printf("game_font_test: %d lines (%d exact, %d aligned, %d splits, %d unaligned), %d letters, "
           "%d glyphs, %d kerning pairs, %zu bytes\n",
           st.lines, st.exact, st.aligned, st.splits, st.unaligned, st.instances, st.glyphs,
           st.kerns, size);
    CHECK(st.lines == 4 && st.unaligned == 0, "every line aligned");
    CHECK(st.exact == 3 && st.aligned == 1 && st.splits == 1, "one touching pair split");
    CHECK(st.instances == 16 && st.glyphs == 4, "16 letters, 4 characters");
    if (!blob) {
        return 1;
    }

    CHECK(ui_FontInit(), "Arimo");
    CHECK(ui_GameFaceLoad(blob, size), "the blob loads");
    uint32_t cps[8];
    CHECK(ui_GameFaceChars(cps, 8) == 4 && cps[0] == 'H' && cps[1] == 'I' && cps[2] == 'L' &&
              cps[3] == 'T',
          "the characters H I L T");
    for (int i = 0; i < 4; i++) {
        const char *name = NULL;
        int lang = -1, count = 0;
        CHECK(ui_GameFaceSource(cps[i], &name, &lang, &count) && name &&
                  strcmp(name, "synthetic.tm2") == 0 && lang == 0 && count == 4,
              "%c from the sheet, 4 times (%d)", (char)cps[i], count);
    }
    /* the cells (the blob's glyph records) */
    {
        const uint8_t *g = blob + UI_GF_HEADER;
        for (int i = 0; i < 4; i++, g += UI_GF_GLYPH) {
            const uint32_t cp = (uint32_t)g[0] | ((uint32_t)g[1] << 8);
            const int iw = g[12] | (g[13] << 8), ih = g[14] | (g[15] << 8);
            const int mw = g[24] | (g[25] << 8), mh = g[26] | (g[27] << 8);
            const int cw = g[40] | (g[41] << 8);
            /* the main cell: the letter's columns between its bearings, the
               line's rows (the rectangle's 20, and the fitted glow's reach
               past them), a texel of apron round; the caps CAP_W wide */
            CHECK(mw >= letterW((char)cp) + 2 && mw <= letterW((char)cp) + 6 && mh >= 22 &&
                      mh <= 60,
                  "%c: the main cell %d x %d", (char)cp, mw, mh);
            CHECK(cw == 10 + 2, "%c: the caps 10 texels wide (%d)", (char)cp, cw);
            /* the ink: the shape, a texel of antialiasing a side at most, a
               border */
            CHECK(iw >= letterW((char)cp) + 2 && iw <= letterW((char)cp) + 4,
                  "%c: ink cell %d wide for a %d-texel shape", (char)cp, iw, letterW((char)cp));
            CHECK(ih >= CAP + 2 && ih <= CAP + 4, "%c: ink cell %d high", (char)cp, ih);
        }
    }
    /* the advances, at the main size (27 y units: a texel an x unit) */
    {
        const float w = ui_MeasureText(27.0f, "HILT");
        /* the drawn word's ink is 29 texels; the measure adds the outer
           bearings, which only the T|H junction of "LITH" (cut: no gap)
           determines */
        CHECK(w >= 28.5f && w <= 31.5f, "\"HILT\" measures %.2f (29 drawn)", w);
        /* the I|L junction is the drawn two texels: "HILT" less "HI" and "LT"
           is the gap less the bearings either side of it, 0 */
        const float j = w - ui_MeasureText(27.0f, "HI") - ui_MeasureText(27.0f, "LT");
        CHECK(fabsf(j) < 0.5f, "I|L as drawn: %.2f", j);
        const float sp = ui_MeasureText(27.0f, "H I") - ui_MeasureText(27.0f, "HI");
        /* the drawn word gap is 7, the bearings across it 2 */
        CHECK(fabsf(sp - 5.0f) < 0.5f, "the space: %.2f (7 less the bearings, 5)", sp);
        CHECK(ui_MeasureText(54.0f, "HILT") > 1.9f * w && ui_MeasureText(54.0f, "HILT") < 2.1f * w,
              "twice the size, twice the width");
    }
    CHECK(ui_FontFaceOf('H') == UI_FACE_GAME && ui_FontFaceOf(' ') == UI_FACE_GAME &&
              ui_FontFaceOf('x') == UI_FACE_ARIMO && ui_FontFaceOf(0x4E2D) == -1,
          "H and the space from the game face, x from Arimo, U+4E2D from neither");
    {
        /* a mixed string: the fallback letter is logged once */
        const float m = ui_MeasureText(27.0f, "HxH");
        CHECK(m > ui_MeasureText(27.0f, "HH"), "x takes Arimo's room");
        uint32_t fb[4];
        CHECK(ui_FontFallbackSeen(fb, 4) == 1 && fb[0] == 'x', "x logged as a fallback once");
        ui_MeasureText(27.0f, "xx");
        CHECK(ui_FontFallbackSeen(NULL, 0) == 1, "not twice");
    }
    /* no game face (no disc to cut it from): Arimo alone */
    ui_GameFaceUnload();
    CHECK(ui_FontFaceOf('H') == UI_FACE_ARIMO, "no game face: H from Arimo");
    CHECK(ui_GameFaceLoad(blob, size) && ui_FontFaceOf('H') == UI_FACE_GAME,
          "loaded again: H from the game face");
    /* refused blobs */
    {
        uint8_t *bad = malloc(size);
        if (bad) {
            memcpy(bad, blob, size);
            bad[4] = (uint8_t)(UI_GF_VERSION + 1);
            CHECK(!ui_GameFaceLoad(bad, size) && !ui_GameFaceLoaded(), "another version");
            CHECK(!ui_GameFaceLoad(blob, size - 1), "a short blob");
            free(bad);
        }
        CHECK(ui_GameFaceLoad(blob, size) && ui_GameFaceLoaded(), "and the good one again");
    }
    free(blob);
    ui_GameFaceUnload();
    printf("game_font_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
