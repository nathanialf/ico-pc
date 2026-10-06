/*
 * port/ui/model_overlay.c
 *
 * The model viewer's text on the presentation overlay (model_overlay.h).
 */
#include "model_overlay.h"

#include <stddef.h>

#include "font.h"

/* the panel, in the layout grid (font.h), as the popup's (popup.c) */
#define NAME_SIZE 26.0f
#define LINE_SIZE 19.0f
#define PAD_X 14.0f
#define PAD_Y 9.0f
#define GAP_Y 4.0f
#define LEFT 16.0f
#define TOP 16.0f

typedef struct Lines {
    float nameBase, animBase, frameBase, bottom, right;
} Lines;

static void place(const char *name, const char *anim, const char *frame, Lines *l)
{
    float na, nd, la, ld;
    ui_FontMetrics(NAME_SIZE, &na, &nd, NULL);
    ui_FontMetrics(LINE_SIZE, &la, &ld, NULL);
    float w = ui_MeasureText(NAME_SIZE, name);
    float y = TOP + PAD_Y + na;
    l->nameBase = y;
    y += nd;
    l->animBase = l->frameBase = y;
    if (anim[0]) {
        float wa = ui_MeasureText(LINE_SIZE, anim);
        w = wa > w ? wa : w;
        y += GAP_Y + la;
        l->animBase = y;
        y += ld;
    }
    if (frame[0]) {
        float wf = ui_MeasureText(LINE_SIZE, frame);
        w = wf > w ? wf : w;
        y += GAP_Y + la;
        l->frameBase = y;
        y += ld;
    }
    l->bottom = y + PAD_Y;
    l->right = LEFT + w + 2.0f * PAD_X;
}

void ui_ModelOverlayPanel(const char *name, const char *anim, const char *frame, float rect[4])
{
    Lines l;
    place(name ? name : "", anim ? anim : "", frame ? frame : "", &l);
    rect[0] = LEFT;
    rect[1] = TOP;
    rect[2] = l.right;
    rect[3] = l.bottom;
}

void ui_ModelOverlayDraw(const struct RdOverlayCtx *ctx, const char *name, const char *anim,
                         const char *frame)
{
    static const uint8_t kPanel[4] = {6, 6, 9, 0x5C};
    static const uint8_t kName[4] = {0x80, 0x7C, 0x70, 0x80};
    static const uint8_t kText[4] = {0x70, 0x6E, 0x66, 0x80};
    Lines l;
    if (!ctx || !ui_FontInit()) {
        return;
    }
    name = name ? name : "";
    anim = anim ? anim : "";
    frame = frame ? frame : "";
    ui_BeginOverlay(ctx);
    place(name, anim, frame, &l);
    ui_DrawRect(LEFT, TOP, l.right, l.bottom, kPanel);
    ui_DrawText(LEFT + PAD_X, l.nameBase, NAME_SIZE, kName, name, UI_VALIGN_BASELINE);
    if (anim[0]) {
        ui_DrawText(LEFT + PAD_X, l.animBase, LINE_SIZE, kText, anim, UI_VALIGN_BASELINE);
    }
    if (frame[0]) {
        ui_DrawText(LEFT + PAD_X, l.frameBase, LINE_SIZE, kText, frame, UI_VALIGN_BASELINE);
    }
    ui_EndOverlay();
}
