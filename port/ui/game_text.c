/*
 * port/ui/game_text.c
 *
 * The game's remaining text through the port font (package TXT;
 * docs/port/UI.md, "Subtitles" and "Staff roll"): the subtitles jimaku.c
 * streams as pictures, and the staff roll staffroll.c prints with
 * DisplayFont.c's bitmap font.  Both are drawn with ui_DrawTextDeferred, so
 * the Enhanced preset composites them at the output's resolution
 * (RENDER_API.md, "The deferred text pass"); [game] classic_menu_text
 * (ui_MenuTextClassic) gives the game's own drawing back.
 */
#include <stdint.h>
#include <string.h>

#include "font.h"
#include "layout_ext.h"
#include "menu_text.h"
#include "strings.h"
#include "subtitles.h"
#include "ui_internal.h"

#ifdef ICO_RD
#include "rd.h"
#define GT_KEY(obj, part) RD_KEY((obj), (part), 0)
#else
#define GT_KEY(obj, part) ((uint64_t)(uintptr_t)(obj) << 16 ^ (uint64_t)(part) << 8)
#endif

int lt_ext_PortText(void)
{
    return !ui_MenuTextClassic();
}

/* ---------------------------------------------------------- the subtitles */

const UiSubtitle *lt_ext_SubtitleFind(int set, int block)
{
    if (ui_MenuTextClassic() || block < 0) {
        return NULL;
    }
    return ui_SubtitleFind(ui_GetLanguage(), set != 0, block);
}

#define SUB_LINE_BYTES 160

void lt_ext_DrawSubtitle(const UiSubtitle *s, const void *key, const int box[4], const int uv[4],
                         const unsigned char rgba[4])
{
    if (!s || !s->text || uv[2] <= 0 || uv[3] <= 0) {
        return;
    }
    ui__Sync();
    const UiSubtitleFace *face = ui_SubtitleFace(ui_GetLanguage());
    /* row 434's sprite: the strip's left half, texels (uv) onto the box,
       as ui_MenuTextDraw maps a menu row; the right half (row 435) goes on
       at the same scale, so strip x 256..512 lands where row 435 draws */
    const float bx = (float)box[0] / 16.0f + UI_GRID_CX;
    const float by = (float)box[1] / 8.0f + UI_GRID_CY;
    const float sx = ((float)box[2] / 16.0f) * 16.0f / (float)uv[2];
    const float sy = ((float)box[3] / 8.0f) * 16.0f / (float)uv[3];
    const float ox = bx - (float)uv[0] / 16.0f * sx;
    const float oy = by - (float)uv[1] / 16.0f * sy;

    char lines[2][SUB_LINE_BYTES];
    int n = 0;
    for (const char *p = s->text; n < 2;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= SUB_LINE_BYTES) {
            len = SUB_LINE_BYTES - 1;
        }
        memcpy(lines[n], p, len);
        lines[n][len] = '\0';
        n++;
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    /* the sheets' em; a line wider than the room about its centre (the
       strip with 32 texels either side, inside the 4:3 picture) sets the
       block smaller, down to 60 %: Arimo is wider than the sheets' faces */
    float size = face->em * sy;
    float k = 1.0f;
    for (int i = 0; i < n; i++) {
        const float xc = s->x[i];
        const float half = (xc + 32.0f < (float)UI_SUB_STRIP_W + 32.0f - xc)
                               ? xc + 32.0f
                               : (float)UI_SUB_STRIP_W + 32.0f - xc;
        const float room = 2.0f * half * sx;
        const float w = ui_MeasureText(size, lines[i]);
        if (w > room && room > 0.0f && room / w < k) {
            k = room / w;
        }
    }
    size *= k < 0.6f ? 0.6f : k;
    size = (float)(int)(size + 0.5f);
    if (size < 1.0f) {
        size = 1.0f;
    }
    const unsigned flags = UI_KEEP_STATE | UI_VALIGN_MIDDLE | UI_ALIGN_CENTER | UI_HALO;
    const uint8_t col[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    for (int i = 0; i < n; i++) {
        /* one line: the lower slot; two: both */
        const float yt = face->y[n == 1 ? 1 : i];
        const uint64_t owner = ui_SetDrawKey(GT_KEY(key, i));
        ui_DrawTextDeferred(ox + s->x[i] * sx, oy + yt * sy, size, col, lines[i], flags, NULL);
        ui_SetDrawKey(owner);
    }
}

/* ---------------------------------------------------------- the staff roll */

/* the bitmap font's cell (DisplayFont.c, font.tm2): 20 texels square, drawn
   font_GetWidth() * 640 / ScreenWidth GS lines tall; its capitals ("H"),
   measured at half coverage, run from texel 2.5 to 17.2 */
#define ROLL_CELL 20.0f
#define ROLL_CAP_TOP 2.5f
#define ROLL_CAP_BOTTOM 17.2f
/* staffroll.c's rollLines */
#define ROLL_LINES 300
#define ROLL_BYTES 256

static unsigned char s_rollKey[ROLL_LINES];

/* str as font_Print shows it, UTF-8: the {...} codes skipped, the cells
   that hold other signs than their ASCII code mapped ('@' the copyright
   sign, '\' the yen sign), a byte with no cell ('`', DEL, >= 0x80) a space;
   returns whether anything but spaces is left */
static int rollText(const char *str, char *out, size_t cap)
{
    size_t n = 0;
    int brace = 0, ink = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        const unsigned c = *p;
        const char *rep = NULL;
        char one[2] = {0, 0};
        if (c == '{') {
            brace = 1;
            continue;
        }
        if (c == '}') {
            brace = 0;
            continue;
        }
        if (brace) {
            continue;
        }
        if (c == '@') {
            rep = "\xC2\xA9"; /* U+00A9 */
        } else if (c == '\\') {
            rep = "\xC2\xA5"; /* U+00A5 */
        } else if (c < 0x20 || c == '`' || c >= 0x7F) {
            one[0] = ' ';
            rep = one;
        } else {
            one[0] = (char)c;
            rep = one;
        }
        const size_t len = strlen(rep);
        if (n + len + 1 > cap) {
            break;
        }
        memcpy(out + n, rep, len);
        n += len;
        ink |= rep[0] != ' ';
    }
    out[n] = '\0';
    return ink;
}

int lt_ext_DrawRollLine(int i, const char *str, float x, float y, int align,
                        const unsigned char col[4], unsigned int color)
{
    if (ui_MenuTextClassic()) {
        return 0;
    }
    char text[ROLL_BYTES * 2];
    if (!str || i < 0 || i >= ROLL_LINES || !rollText(str, text, sizeof(text))) {
        return 1;
    }
    ui__Sync();
    const UiGsFrame *fr = ui_GetGsFrame();
    const float sw = (float)(fr->screenW > 0 ? fr->screenW : 512);
    const float sh = (float)(fr->screenH > 0 ? fr->screenH : 512);
    /* font_Print's cell height, GS lines (DisplayFont.c drawOne: fontw *
       640 / ScreenWidth, integer) */
    const float fh = (float)(20 * 640 / (fr->screenW > 0 ? fr->screenW : 512));
    const float lineScale = fh / ROLL_CELL; /* GS lines a texel */
    const float yUnits = 448.0f / sh;       /* y units a GS line */
    const float xUnits = 640.0f / sw;       /* x units a GS pixel */
    /* the GS point font_Print draws at (drawOne: 2048 - ScreenWidth / 2 +
       cx, 2048 - ScreenHeight / 4 + y), into the grid (font.h) */
    const float capMid = y + 0.5f * (ROLL_CAP_TOP + ROLL_CAP_BOTTOM) * lineScale;
    const float gy = UI_GRID_CY + (2048.0f - sh / 4.0f + capMid - fr->centerY) * yUnits;
    float gx;
    unsigned flags = UI_VALIGN_MIDDLE;
    /* font_Print: centred on x * ScreenWidth / 640 + ScreenWidth / 2, or 4
       GS pixels in from the left or right edge, x across */
    switch (align) {
    case 1:
        gx = UI_GRID_CX + (2048.0f - fr->centerX) * xUnits - 320.0f + x + 4.0f * xUnits;
        flags |= UI_ALIGN_LEFT;
        break;
    case 2:
        gx = UI_GRID_CX + (2048.0f - fr->centerX) * xUnits + 320.0f + x - 4.0f * xUnits;
        flags |= UI_ALIGN_RIGHT;
        break;
    default:
        gx = UI_GRID_CX + (2048.0f - fr->centerX) * xUnits + x;
        flags |= UI_ALIGN_CENTER;
        break;
    }
    /* the em: the capitals' height less 0.7 texel, over Arimo's 0.688 */
    float size = (ROLL_CAP_BOTTOM - ROLL_CAP_TOP - 0.7f) / 0.688f * lineScale * yUnits;
    size = (float)(int)(size + 0.5f);
    /* font_Print's colour: the packed colour scaled by the line's */
    uint8_t rgba[4];
    rgba[0] = (uint8_t)((color >> 24) * col[0] / 255u);
    rgba[1] = (uint8_t)(((color >> 16) & 0xFFu) * col[1] / 255u);
    rgba[2] = (uint8_t)(((color >> 8) & 0xFFu) * col[2] / 255u);
    rgba[3] = (uint8_t)(((color & 0xFFu) * col[3]) >> 7);
    if (rgba[3] == 0) {
        return 1;
    }
    const uint64_t owner = ui_SetDrawKey(GT_KEY(&s_rollKey[i], 0));
    ui_DrawTextDeferred(gx, gy, size, rgba, text, flags, NULL);
    ui_SetDrawKey(owner);
    return 1;
}
