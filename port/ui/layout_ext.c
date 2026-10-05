/*
 * port/ui/layout_ext.c
 *
 * The extension of texLayout and texProperty (layout_ext.h).
 */
#include "layout_ext.h"

#include <string.h>

#include "font.h"
#include "strings.h"
#include "ui_internal.h"

#define TEXT_MAX 96

typedef struct ExtRow {
    LtExtText text;
    char literal[TEXT_MAX];
    int hasLiteral;
    int base[4]; /* the last plain box (the glow maps from it) */
    int hasBase;
} ExtRow;

static LtProp s_layouts[LT_EXT_MAX_LAYOUTS];
static LtProperty s_props[LT_EXT_MAX_PROPERTIES];
static ExtRow s_rows[LT_EXT_MAX_PROPERTIES];
static int s_layoutCount;
static int s_propCount;

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

LtProp *lt_ext_Layout(int index)
{
    int i = index - LT_GAME_LAYOUT_COUNT;
    if (i >= 0 && i < s_layoutCount) {
        return &s_layouts[i];
    }
    return &texLayout[index];
}

LtProperty *lt_ext_Prop(int index)
{
    int i = index - LT_GAME_PROPERTY_COUNT;
    if (i >= 0 && i < s_propCount) {
        return &s_props[i];
    }
    return &texProperty[index];
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
    /* 6C: a label wider than its box (a long option, another language) is
       set smaller to fit, down to 60 % (docs/port/UI.md, open item 7);
       the widest line of a multi-line label counts */
    if (bw > 0.0f) {
        float w = ui_MeasureText(size, text);
        if (w > bw) {
            float k = bw / w;
            size *= k < 0.6f ? 0.6f : k;
        }
    }
    /* R7d: the row's draws are keyed by the row and the pass (the label,
       the glow), so a row that moves or fades blends between ticks */
    const uint64_t owner =
        ui_SetDrawKey(((uint64_t)(uintptr_t)e << 2) ^ (uint64_t)(glow ? 2u : 1u));
    if (!glow) {
        ui_DrawText(x, by + bh * 0.5f, size, rgba, text, flags | UI_HALO);
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
    ui_DrawTextXf(x, by + bh * 0.5f, size, rgba, text, flags, &xf);
    ui_SetDrawKey(owner);
}
