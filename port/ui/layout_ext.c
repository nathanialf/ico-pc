/*
 * port/ui/layout_ext.c
 *
 * The extension of texLayout and texProperty (layout_ext.h).
 */
#include "layout_ext.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "menu_font.h"
#include "menu_text.h"
#include "strings.h"
#include "ui_internal.h"

/* a label's bytes with its NUL: as long as the wrapped notes settings.c
   builds (setNote's 256), which the longer translations need (the Spanish
   mirror-screen note is 103 bytes) */
#define TEXT_MAX 256

/* The capitals' middle of a label, above its box's centre.  The game's menu
   rows (a 20-texel sheet rectangle in a 20-field-line box) have it at texel
   9.0 of 20 (menu_text.c's anchors: Options, pause, vibration, Continue,
   New Game outside English), a texel (2 y units) above the centre of the
   box display_texture draws, so a port row placed on the game's grid lines
   up with the game's rows. */
#define ROW_CAPS_ABOVE_CENTRE 2.0f

enum { ROW_TEXT = 0, ROW_GLYPH, ROW_RECT };

typedef struct ExtRow {
    int kind;              /* ROW_TEXT, ROW_GLYPH, ROW_RECT */
    int glyph;             /* ROW_GLYPH: LtExtGlyph */
    unsigned char rgba[4]; /* ROW_RECT: the colour at a full row colour */
    float fill;            /* ROW_RECT: the left part drawn, 0..1 */
    LtExtText text;
    char literal[TEXT_MAX];
    int hasLiteral;
    int base[4]; /* the last plain box (the glow maps from it) */
    int hasBase;
    int dim;  /* greyed (lt_ext_SetDim): the colour at half */
    int role; /* LT_POINTER_* (lt_ext_SetPointerRole) */
} ExtRow;

static LtProp s_layouts[LT_EXT_MAX_LAYOUTS];
static LtProperty s_props[LT_EXT_MAX_PROPERTIES];
static ExtRow s_rows[LT_EXT_MAX_PROPERTIES];
static int s_layoutCount;
static int s_propCount;
static int s_circleBack = 1; /* [game] circle_back's default */

void lt_ext_Reset(void)
{
    memset(s_layouts, 0, sizeof(s_layouts));
    memset(s_props, 0, sizeof(s_props));
    memset(s_rows, 0, sizeof(s_rows));
    s_layoutCount = 0;
    s_propCount = 0;
}

int lt_ext_AddLayout(const LtProp *layout)
{
    if (!layout || s_layoutCount == LT_EXT_MAX_LAYOUTS) {
        static int logged;
        if (layout && !logged) {
            logged = 1;
            fprintf(stderr, "ui: layout extension full (%d layouts); a page is missing\n",
                    LT_EXT_MAX_LAYOUTS);
        }
        return -1;
    }
    s_layouts[s_layoutCount] = *layout;
    return LT_GAME_LAYOUT_COUNT + s_layoutCount++;
}

static ExtRow *rowOf(int index)
{
    int i = index - LT_GAME_PROPERTY_COUNT;
    return i >= 0 && i < s_propCount ? &s_rows[i] : NULL;
}

static void copyText(ExtRow *r, const char *utf8)
{
    size_t n = strlen(utf8);
    if (n >= TEXT_MAX) {
        n = TEXT_MAX - 1;
        /* do not cut a UTF-8 sequence */
        while (n > 0 && ((unsigned char)utf8[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(r->literal, utf8, n);
    r->literal[n] = '\0';
    r->hasLiteral = 1;
}

int lt_ext_AddProperty(const LtProperty *row, const LtExtText *text)
{
    if (!row || s_propCount == LT_EXT_MAX_PROPERTIES) {
        static int logged;
        if (row && !logged) {
            logged = 1;
            fprintf(stderr, "ui: layout extension full (%d properties); rows are missing\n",
                    LT_EXT_MAX_PROPERTIES);
        }
        return -1;
    }
    LtProperty *p = &s_props[s_propCount];
    ExtRow *r = &s_rows[s_propCount];
    *p = *row;
    p->texData = 0;
    if (p->dispW == 0 && p->texW == 0) {
        p->dispW = 400;
    }
    if (p->dispH == 0 && p->texH == 0) {
        p->dispH = 40;
    }
    memset(r, 0, sizeof(*r));
    if (text) {
        r->text = *text;
        r->text.text = NULL;
        if (text->text) {
            copyText(r, text->text);
        }
    }
    return LT_GAME_PROPERTY_COUNT + s_propCount++;
}

int lt_ext_SetText(int index, const char *utf8)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return -1;
    }
    if (utf8) {
        copyText(r, utf8);
    } else {
        r->hasLiteral = 0;
    }
    return 0;
}

int lt_ext_SetSize(int index, float size)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return -1;
    }
    r->text.size = size;
    return 0;
}

int lt_ext_SetDim(int index, int dim)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return -1;
    }
    r->dim = dim != 0;
    return 0;
}

int lt_ext_RowDim(int index)
{
    ExtRow *r = rowOf(index);
    return r ? r->dim : 0;
}

int lt_ext_SetPointerRole(int index, int role)
{
    ExtRow *r = rowOf(index);
    if (!r || role < LT_POINTER_AUTO || role > LT_POINTER_STEP) {
        return -1;
    }
    r->role = role;
    return 0;
}

int lt_ext_PointerRole(int index)
{
    ExtRow *r = rowOf(index);
    return r ? r->role : LT_POINTER_AUTO;
}

int lt_ext_SetStr(int index, int strId)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return -1;
    }
    r->text.strId = strId;
    r->hasLiteral = 0;
    return 0;
}

const char *lt_ext_RowText(int index)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return "";
    }
    return r->hasLiteral ? r->literal : ui_Str((UiStrId)r->text.strId);
}

float lt_ext_RowSize(int index)
{
    ExtRow *r = rowOf(index);
    if (!r) {
        return 0.0f;
    }
    return r->text.size > 0.0f ? r->text.size : UI_MENU_TEXT_SIZE;
}

/* The glyphs' sources in the PAL tables (texProperty rows, checked against
   the boot ELF's table: rows 182 and 184 are the save prompts' Cross and
   Triangle beside OK (181) and Back (183), 343 and 344 the key config
   screen's Square and Circle, 349, 346, 348 and 347 its L1, R1, L2 and R2
   labels, 301 and 302 the Options screen's value arrows) and the height
   each has beside the game's 27-unit labels (dispH, y units; the width is
   the rectangle's, a pixel a texel). */
static const struct {
    short row, u, v, w, h, dispH;
} kGlyph[LT_GLYPH_COUNT] = {
    {182, 32, 30, 32, 30, 30},   /* Cross, text/buttons.tm2 */
    {344, 0, 30, 32, 30, 30},    /* Circle */
    {343, 32, 0, 32, 30, 30},    /* Square */
    {184, 0, 0, 32, 30, 30},     /* Triangle */
    {349, 420, 240, 40, 15, 30}, /* L1, menu_PAL_02 */
    {346, 340, 240, 40, 15, 30}, /* R1 */
    {348, 460, 240, 40, 15, 30}, /* L2 */
    {347, 380, 240, 40, 15, 30}, /* R2 */
    {301, 490, 130, 20, 20, 40}, /* Left, menu_PAL_01 */
    {302, 490, 150, 20, 20, 40}, /* Right */
};

int lt_ext_GlyphSource(int glyph, int uvwh[4])
{
    if (glyph < 0 || glyph >= LT_GLYPH_COUNT) {
        return -1;
    }
    if (uvwh) {
        uvwh[0] = kGlyph[glyph].u;
        uvwh[1] = kGlyph[glyph].v;
        uvwh[2] = kGlyph[glyph].w;
        uvwh[3] = kGlyph[glyph].h;
    }
    return kGlyph[glyph].row;
}

void lt_ext_GlyphBox(int glyph, float size, int *w, int *h)
{
    float k = (size > 0.0f ? size : UI_MENU_TEXT_SIZE) / UI_MENU_TEXT_SIZE;
    if (glyph < 0 || glyph >= LT_GLYPH_COUNT) {
        glyph = 0;
    }
    if (w) {
        *w = (int)((float)kGlyph[glyph].w * k + 0.5f);
    }
    if (h) {
        *h = (int)((float)kGlyph[glyph].dispH * k + 0.5f);
    }
}

int lt_ext_AddGlyph(int glyph, int x, int y, float size)
{
    if (glyph < 0 || glyph >= LT_GLYPH_COUNT) {
        return -1;
    }
    LtProperty r;
    memset(&r, 0, sizeof(r));
    r.word0 = r.word4 = r.word8 = r.wordC = -1;
    r.ownerItem = -1;
    r.up = r.down = r.left = r.right = -1;
    r.rightItem = r.leftItem = r.downItem = r.upItem = -1;
    r.word40 = 1;
    r.dispX = x;
    r.dispY = y;
    lt_ext_GlyphBox(glyph, size, &r.dispW, &r.dispH);
    r.texU = kGlyph[glyph].u;
    r.texV = kGlyph[glyph].v;
    r.texW = kGlyph[glyph].w;
    r.texH = kGlyph[glyph].h;
    /* never looked up (no stage range reaches a port row); a value of its
       own, so the fade-cancel check pairs no two glyph rows */
    r.texFileNo = 0x7000 + s_propCount;
    int i = lt_ext_AddProperty(&r, NULL);
    if (i >= 0) {
        ExtRow *e = &s_rows[i - LT_GAME_PROPERTY_COUNT];
        e->kind = ROW_GLYPH;
        e->glyph = glyph;
    }
    return i;
}

int lt_ext_IsGlyphRow(const LtProperty *e)
{
    return lt_ext_IsPortProp(e) && s_rows[e - s_props].kind == ROW_GLYPH;
}

int lt_ext_GlyphTexNo(const LtProperty *e)
{
    if (!lt_ext_IsGlyphRow(e)) {
        return -1;
    }
    return lt_ext_GlyphTexture(s_rows[e - s_props].glyph);
}

int lt_ext_GlyphTexture(int g)
{
    if (g < 0 || g >= LT_GLYPH_COUNT) {
        return -1;
    }
    const LtProperty *src = &texProperty[kGlyph[g].row];
    if (src->texU != kGlyph[g].u || src->texV != kGlyph[g].v || src->texW != kGlyph[g].w ||
        src->texH != kGlyph[g].h || src->texNo < 0) {
        return -1;
    }
    return src->texNo;
}

int lt_ext_AddRect(int x, int y, int w, int h, const unsigned char rgba[4])
{
    LtProperty r;
    memset(&r, 0, sizeof(r));
    r.word0 = r.word4 = r.word8 = r.wordC = -1;
    r.ownerItem = -1;
    r.up = r.down = r.left = r.right = -1;
    r.rightItem = r.leftItem = r.downItem = r.upItem = -1;
    r.word40 = 1;
    r.dispX = x;
    r.dispY = y;
    r.dispW = w > 0 ? w : 1;
    r.dispH = h > 0 ? h : 1;
    r.texFileNo = 0x7000 + s_propCount; /* as a glyph row's */
    int i = lt_ext_AddProperty(&r, NULL);
    if (i >= 0) {
        ExtRow *e = &s_rows[i - LT_GAME_PROPERTY_COUNT];
        e->kind = ROW_RECT;
        memcpy(e->rgba, rgba, 4);
        e->fill = 1.0f;
    }
    return i;
}

int lt_ext_SetFill(int index, float fill)
{
    ExtRow *r = rowOf(index);
    if (!r || r->kind != ROW_RECT) {
        return -1;
    }
    r->fill = fill < 0.0f ? 0.0f : fill > 1.0f ? 1.0f : fill;
    return 0;
}

float lt_ext_RowFill(int index)
{
    ExtRow *r = rowOf(index);
    return r && r->kind == ROW_RECT ? r->fill : 0.0f;
}

/* a rect row: its box as dispX..dispY gave it (display_texture's half-texel
   inset taken back), the fill's part, the colour times the row's */
static void drawRect(const ExtRow *r, const int box[4], const unsigned char rgba[4])
{
    const float x0 = (float)(box[0] - 4) / 16.0f + UI_GRID_CX;
    const float y0 = (float)(box[1] - 4) / 8.0f + UI_GRID_CY;
    const float w = (float)(box[2] + 16) / 16.0f;
    const float h = (float)(box[3] + 16) / 8.0f;
    unsigned char c[4];
    for (int k = 0; k < 4; k++) {
        unsigned v = (unsigned)r->rgba[k] * rgba[k] / 0x80u;
        c[k] = (unsigned char)(v > 255 ? 255 : v);
    }
    if (r->fill <= 0.0f) {
        return;
    }
    ui_DrawRect(x0, y0, x0 + w * r->fill, y0 + h, c);
}

/* An index that names no row gets a scratch row instead of memory outside
   the tables: its readers see zeros, its writers write nowhere that
   matters (re-zeroed on every such lookup).  -1 is the tables' "none" (a
   layout with no current item: display_texture compares each row with
   &LT_PROP(curItem)), and a failed add's result, which logged already; any
   other index is logged once. */
static LtProp s_scratchLayout;
static LtProperty s_scratchProp;

LtProp *lt_ext_Layout(int index)
{
    int i = index - LT_GAME_LAYOUT_COUNT;
    if (i >= 0 && i < s_layoutCount) {
        return &s_layouts[i];
    }
    if (index >= 0 && index < LT_GAME_LAYOUT_COUNT) {
        return &texLayout[index];
    }
    static int logged;
    if (index != -1 && !logged) {
        logged = 1;
        fprintf(stderr, "ui: layout index %d names no layout; a scratch row used\n", index);
    }
    memset(&s_scratchLayout, 0, sizeof(s_scratchLayout));
    return &s_scratchLayout;
}

LtProperty *lt_ext_Prop(int index)
{
    int i = index - LT_GAME_PROPERTY_COUNT;
    if (i >= 0 && i < s_propCount) {
        return &s_props[i];
    }
    if (index >= 0 && index < LT_GAME_PROPERTY_COUNT) {
        return &texProperty[index];
    }
    static int logged;
    if (index != -1 && !logged) {
        logged = 1;
        fprintf(stderr, "ui: property index %d names no row; a scratch row used\n", index);
    }
    memset(&s_scratchProp, 0, sizeof(s_scratchProp));
    return &s_scratchProp;
}

int lt_ext_PropIndex(const LtProperty *e)
{
    if (e >= s_props && e < s_props + s_propCount) {
        return LT_GAME_PROPERTY_COUNT + (int)(e - s_props);
    }
    if (e >= texProperty && e < texProperty + LT_GAME_PROPERTY_COUNT) {
        return (int)(e - texProperty);
    }
    return -1;
}

int lt_ext_IsPortProp(const LtProperty *e)
{
    return e >= s_props && e < s_props + s_propCount;
}

int lt_ext_LayoutCount(void)
{
    return s_layoutCount;
}

int lt_ext_PropCount(void)
{
    return s_propCount;
}

void lt_ext_DrawRow(const LtProperty *e, const int box[4], const unsigned char rgba[4], int glow)
{
    if (!lt_ext_IsPortProp(e)) {
        return;
    }
    ui__Sync();
    ExtRow *r = &s_rows[e - s_props];
    if (r->kind == ROW_GLYPH) {
        return; /* a glyph without its texture (tables not PAL): nothing */
    }
    if (r->kind == ROW_RECT) {
        if (!glow) {
            const uint64_t owner = ui_SetDrawKey(((uint64_t)(uintptr_t)e << 2) ^ 1u);
            drawRect(r, box, rgba);
            ui_SetDrawKey(owner);
        }
        return;
    }
    unsigned char grey[4];
    if (r->dim) {
        /* a locked row (Settings > Extras): the colour at half, the alpha kept */
        grey[0] = (unsigned char)(rgba[0] / 2);
        grey[1] = (unsigned char)(rgba[1] / 2);
        grey[2] = (unsigned char)(rgba[2] / 2);
        grey[3] = rgba[3];
        rgba = grey;
    }
    const char *text = r->hasLiteral ? r->literal : ui_Str((UiStrId)r->text.strId);
    if (!glow || !r->hasBase) {
        memcpy(r->base, box, sizeof(r->base));
        r->hasBase = 1;
    }
    /* the base box in the grid: x 1/16 px from the centre, y 1/16 field
       line from the centre (two y units a field line) */
    const float bx = (float)r->base[0] / 16.0f + UI_GRID_CX;
    const float by = (float)r->base[1] / 8.0f + UI_GRID_CY;
    const float bw = (float)r->base[2] / 16.0f;
    const float bh = (float)r->base[3] / 8.0f;
    const float y = by + bh * 0.5f - ROW_CAPS_ABOVE_CENTRE;
    float x;
    unsigned flags = UI_KEEP_STATE | UI_VALIGN_MIDDLE;
    switch (r->text.align) {
    case UI_ALIGN_CENTER:
        x = bx + bw * 0.5f;
        flags |= UI_ALIGN_CENTER;
        break;
    case UI_ALIGN_RIGHT:
        x = bx + bw;
        flags |= UI_ALIGN_RIGHT;
        break;
    default:
        x = bx;
        break;
    }
    float size = r->text.size > 0.0f ? r->text.size : UI_MENU_TEXT_SIZE;
    /* a label wider than its box (a long option, another language) is set
       smaller to fit, down to 60 %; the widest line of a multi-line label
       counts */
    if (bw > 0.0f) {
        float w = ui_MeasureMenuText(size, text);
        if (w > bw) {
            float k = bw / w;
            size *= k < 0.6f ? 0.6f : k;
        }
    }
    /* the row's draws are keyed by the row and the pass (the label,
       the glow), so a row that moves or fades blends between ticks */
    const uint64_t owner =
        ui_SetDrawKey(((uint64_t)(uintptr_t)e << 2) ^ (uint64_t)(glow ? 2u : 1u));
    /* the menus' text in the sheets' look, light ink
       (menu_font.h), into the scene list at 1x in every preset */
    if (!glow) {
        ui_DrawMenuText(x, y, size, rgba, text, flags, UI_INK_LIGHT, NULL);
        ui_SetDrawKey(owner);
        return;
    }
    /* the glow sprite stretches the row's box: the same map for the text */
    UiXform xf;
    xf.originX = bx;
    xf.originY = by;
    xf.scaleX = bw > 0.0f ? ((float)box[2] / 16.0f) / bw : 1.0f;
    xf.scaleY = bh > 0.0f ? ((float)box[3] / 8.0f) / bh : 1.0f;
    xf.offsetX = ((float)box[0] / 16.0f + UI_GRID_CX) - bx;
    xf.offsetY = ((float)box[1] / 8.0f + UI_GRID_CY) - by;
    /* the glow sprite's blend (ALPHA 0x48), which the packet holds */
    ui_DrawMenuText(x, y, size, rgba, text, flags | UI_ADDITIVE, UI_INK_LIGHT, &xf);
    ui_SetDrawKey(owner);
}

int lt_ext_IsTextRow(const LtProperty *e)
{
    if (lt_ext_IsGlyphRow(e)) {
        return lt_ext_GlyphTexNo(e) < 0;
    }
    return lt_ext_IsPortProp(e) || ui_MenuTextItemOf(e) != NULL;
}

void lt_ext_DrawTextRow(const LtProperty *e, const int box[4], const int uv[4],
                        const unsigned char rgba[4], int glow)
{
    if (lt_ext_IsPortProp(e)) {
        lt_ext_DrawRow(e, box, rgba, glow);
        return;
    }
    const UiMenuTextItem *it = ui_MenuTextItemOf(e);
    if (!it) {
        return;
    }
    ui__Sync();
    /* keyed by the row and the pass (the words, the glow), as the port's
       rows are, so the presenter blends a row that moves or fades */
    const uint64_t owner =
        ui_SetDrawKey(((uint64_t)(uintptr_t)e << 2) ^ (uint64_t)(glow ? 2u : 1u));
    ui_MenuWordDraw(it, (int)ui_GetLanguage(), box, uv, rgba, glow);
    ui_SetDrawKey(owner);
}

int lt_ext_BackButtons(void)
{
    return LT_PAD_TRIANGLE | (s_circleBack ? LT_PAD_CIRCLE : 0);
}

void lt_ext_SetCircleBack(int on)
{
    s_circleBack = on != 0;
}

int lt_ext_CircleBack(void)
{
    return s_circleBack;
}

int lt_ext_SetRectColor(int index, const unsigned char rgba[4])
{
    ExtRow *r = rowOf(index);
    if (!r || r->kind != ROW_RECT || !rgba) {
        return -1;
    }
    memcpy(r->rgba, rgba, 4);
    return 0;
}

int lt_ext_RectColor(int index, unsigned char rgba[4])
{
    ExtRow *r = rowOf(index);
    if (!r || r->kind != ROW_RECT || !rgba) {
        return -1;
    }
    memcpy(rgba, r->rgba, 4);
    return 0;
}
