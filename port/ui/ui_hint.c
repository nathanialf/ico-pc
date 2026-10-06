/*
 * port/ui/ui_hint.c
 *
 * A line of button prompts (ui_hint.h).
 */
#include "ui_hint.h"

#include <string.h>

#include "font.h"
#include "strings.h"
#include "ui_list.h" /* ui_SettingsAddRow */

/* the words' box: 30 y units (15 field lines), as the Settings notes'; the
   capitals' middle is 2 y units above its centre (layout_ext.c), 6.5 field
   lines below its top */
#define TEXT_BOX_H 30
#define CAPS_MID_LINES 6.5f
/* in pixels at the words' size: a glyph to its word, an item to the next,
   the two glyphs of a pair */
#define GAP_GLYPH 5.0f
#define GAP_ITEM 24.0f
#define GAP_PAIR 2.0f

const UiHintItem ui_hint_gallery[UI_HINT_GAL_COUNT] = {
    {LT_GLYPH_L1, -1, UI_STR_HINT_PREV},
    {LT_GLYPH_CROSS, -1, UI_STR_HINT_PLAY},
    {LT_GLYPH_SQUARE, -1, UI_STR_HINT_STOP},
    {LT_GLYPH_R1, -1, UI_STR_HINT_NEXT},
    {LT_GLYPH_LEFT, LT_GLYPH_RIGHT, UI_STR_HINT_SECTION},
    {LT_GLYPH_TRIANGLE, -1, UI_STR_BACK}};
const UiHintItem ui_hint_mv_list[UI_HINT_MV_LIST_COUNT] = {
    {LT_GLYPH_CROSS, -1, UI_STR_MV_HINT_VIEW}, {LT_GLYPH_TRIANGLE, -1, UI_STR_BACK}};
const UiHintItem ui_hint_mv_sticks[UI_HINT_MV_STICKS_COUNT] = {{-1, -1, UI_STR_MV_HINT_TURN},
                                                               {-1, -1, UI_STR_MV_HINT_ZOOM}};
const UiHintItem ui_hint_mv_keys[UI_HINT_MV_KEYS_COUNT] = {
    {LT_GLYPH_CROSS, -1, UI_STR_MV_HINT_PLAY},
    {LT_GLYPH_SQUARE, -1, UI_STR_MV_LOOP},
    {LT_GLYPH_L1, LT_GLYPH_R1, UI_STR_MV_ANIMATION},
    {LT_GLYPH_TRIANGLE, -1, UI_STR_EXTRAS_MODELS}};

static void setShown(int row, int on)
{
    if (row < 0) {
        return;
    }
    LtProperty *p = lt_ext_Prop(row);
    p->defaultMask = on ? 0 : 1;
    p->masked = on ? 0 : 1;
}

void ui_HintBuild(UiHint *h, int y, float size, const UiHintItem *items, int n)
{
    memset(h, 0, sizeof(*h));
    h->y = y;
    h->size = size;
    h->n = n > UI_HINT_MAX ? UI_HINT_MAX : n;
    h->scale = 1.0f;
    for (int i = 0; i < h->n; i++) {
        h->item[i] = items[i];
        h->shown[i] = 1;
        h->glyphRow[i][0] = items[i].glyph >= 0 ? lt_ext_AddGlyph(items[i].glyph, 0, y, size) : -1;
        h->glyphRow[i][1] =
            items[i].glyph2 >= 0 ? lt_ext_AddGlyph(items[i].glyph2, 0, y, size) : -1;
        h->textRow[i] = ui_SettingsAddRow(0, y, 100, TEXT_BOX_H, 0, -1, items[i].strId, NULL, size,
                                          UI_ALIGN_LEFT);
    }
    ui_HintLayout(h);
}

void ui_HintShow(UiHint *h, int i, int on)
{
    if (i >= 0 && i < h->n) {
        h->shown[i] = on ? 1 : 0;
    }
}

void ui_HintSetStr(UiHint *h, int i, int strId)
{
    if (i >= 0 && i < h->n && h->item[i].strId != strId) {
        h->item[i].strId = strId;
        lt_ext_SetStr(h->textRow[i], strId);
    }
}

/* the line's width at scale k: glyphs, gaps and words */
static float lineWidth(const UiHint *h, float k)
{
    float w = 0.0f;
    int items = 0;
    for (int i = 0; i < h->n; i++) {
        if (!h->shown[i]) {
            continue;
        }
        for (int g = 0; g < 2; g++) {
            int gw;
            int glyph = g == 0 ? h->item[i].glyph : h->item[i].glyph2;
            if (glyph < 0) {
                continue;
            }
            lt_ext_GlyphBox(glyph, h->size * k, &gw, NULL);
            w += (float)gw + (g == 1 ? GAP_PAIR * k : 0.0f);
        }
        if (h->item[i].glyph >= 0) {
            w += GAP_GLYPH * k;
        }
        w += ui_MeasureText(h->size * k, ui_Str((UiStrId)h->item[i].strId));
        items++;
    }
    return items > 1 ? w + GAP_ITEM * k * (float)(items - 1) : w;
}

void ui_HintLayout(UiHint *h)
{
    float k = 1.0f;
    float w = lineWidth(h, 1.0f);
    if (w > UI_HINT_WIDTH) {
        k = UI_HINT_WIDTH / w;
        k = k < 0.6f ? 0.6f : k;
        w = lineWidth(h, k);
    }
    h->scale = k;
    float x = UI_GRID_CX - w * 0.5f;
    const float capsMid = (float)h->y + CAPS_MID_LINES;
    for (int i = 0; i < h->n; i++) {
        int on = h->shown[i];
        setShown(h->glyphRow[i][0], on);
        setShown(h->glyphRow[i][1], on);
        setShown(h->textRow[i], on);
        if (!on) {
            continue;
        }
        for (int g = 0; g < 2; g++) {
            int row = h->glyphRow[i][g];
            int glyph = g == 0 ? h->item[i].glyph : h->item[i].glyph2;
            if (row < 0) {
                continue;
            }
            int gw, gh;
            lt_ext_GlyphBox(glyph, h->size * k, &gw, &gh);
            if (g == 1) {
                x += GAP_PAIR * k;
            }
            LtProperty *p = lt_ext_Prop(row);
            p->dispX = (int)(x + 0.5f);
            /* the glyph's middle on the words' capitals (dispH is in y
               units, dispY in field lines: a field line is two) */
            p->dispY = (int)(capsMid - (float)gh * 0.25f + 0.5f);
            p->dispW = gw;
            p->dispH = gh;
            x += (float)gw;
        }
        if (h->item[i].glyph >= 0) {
            x += GAP_GLYPH * k;
        }
        const char *word = ui_Str((UiStrId)h->item[i].strId);
        float tw = ui_MeasureText(h->size * k, word);
        LtProperty *t = lt_ext_Prop(h->textRow[i]);
        lt_ext_SetSize(h->textRow[i], h->size * k);
        t->dispX = (int)(x + 0.5f);
        t->dispY = h->y;
        /* wide enough that the label is never shrunk again */
        t->dispW = (int)(tw + 0.5f) + 4;
        x += tw + GAP_ITEM * k;
    }
}
