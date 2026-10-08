/*
 * port/ui/touch_ui.c
 *
 * The touch overlay's drawing (touch_ui.h): shapes as untextured triangles
 * in output pixels, the labels through the font's overlay mode.
 */
#include "touch_ui.h"

#include <stddef.h>
#include <stdint.h>

#include "touch.h"

#ifdef ICO_RD
#include <math.h>

#include "font.h"
#include "glyphs.h"
#include "rd.h"
#endif

static int (*s_source)(struct IcoTouchOverlay *out);

void ui_TouchSetSource(int (*fn)(struct IcoTouchOverlay *out))
{
    s_source = fn;
}

#ifdef ICO_RD

/* the layout-to-output mapping, for the labels (the shapes take theirs
   through glyphs.h) */
static struct {
    float kx, ky;
} s_d;

static void disc(float cx, float cy, float r, const uint8_t c[4])
{
    ui_GlyphDisc(cx, cy, r, c);
}

static void ring(float cx, float cy, float r, float th, const uint8_t c[4])
{
    ui_GlyphRing(cx, cy, r, th, c);
}

static void rect(float x0, float y0, float x1, float y1, const uint8_t c[4])
{
    ui_GlyphRect(x0, y0, x1, y1, c);
}

static void frame(float x0, float y0, float x1, float y1, float th, const uint8_t c[4])
{
    ui_GlyphFrame(x0, y0, x1, y1, th, c);
}

static UiBtnGlyph glyphOf(int zone)
{
    switch (zone) {
    case ICO_TOUCH_B_UP:
        return UI_BTN_UP;
    case ICO_TOUCH_B_DOWN:
        return UI_BTN_DOWN;
    case ICO_TOUCH_B_LEFT:
        return UI_BTN_LEFT;
    case ICO_TOUCH_B_RIGHT:
        return UI_BTN_RIGHT;
    case ICO_TOUCH_B_CROSS:
        return UI_BTN_CROSS;
    case ICO_TOUCH_B_CIRCLE:
        return UI_BTN_CIRCLE;
    case ICO_TOUCH_B_SQUARE:
        return UI_BTN_SQUARE;
    default:
        return UI_BTN_TRIANGLE;
    }
}

/* the PS2 symbols and the D-pad's arrows (glyphs.c), s the half size */
static void symbol(int zone, float cx, float cy, float s, float th, const uint8_t c[4])
{
    if (zone >= ICO_TOUCH_B_CROSS && zone <= ICO_TOUCH_B_TRIANGLE) {
        ui_GlyphSymbol(glyphOf(zone), cx, cy, s, th, c);
    }
}

static void arrow(int zone, float cx, float cy, float s, const uint8_t c[4])
{
    if (zone >= ICO_TOUCH_B_UP && zone <= ICO_TOUCH_B_RIGHT) {
        ui_GlyphArrow(glyphOf(zone), cx, cy, s, c);
    }
}

static const uint8_t *symbolColour(int zone)
{
    return ui_GlyphFaceColour(glyphOf(zone));
}

static const char *labelOf(int zone)
{
    switch (zone) {
    case ICO_TOUCH_B_L1:
        return "L1";
    case ICO_TOUCH_B_R1:
        return "R1";
    case ICO_TOUCH_B_L2:
        return "L2";
    case ICO_TOUCH_B_R2:
        return "R2";
    case ICO_TOUCH_B_SELECT:
        return "SELECT";
    case ICO_TOUCH_B_START:
        return "START";
    default:
        return NULL;
    }
}

/* the colours: a resting fill and outline, a held one brighter */
static const uint8_t kFill[4] = {0x60, 0x60, 0x64, 0x28}, kFillHeld[4] = {0x80, 0x80, 0x80, 0x58},
                     kEdge[4] = {0x80, 0x80, 0x80, 0x60}, kEdgeHeld[4] = {0x80, 0x80, 0x80, 0x80},
                     kMark[4] = {0x80, 0x80, 0x80, 0x70}, kRun[4] = {0x80, 0x80, 0x80, 0x38},
                     kRunLit[4] = {0x80, 0x78, 0x50, 0x70}, kKnob[4] = {0x80, 0x80, 0x80, 0x50};

static void drawButtons(const IcoTouchOverlay *o, float th)
{
    const IcoTouchLayout *l = &o->layout;

    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &l->button[z];
        const int held = (o->info.pressed >> z) & 1u;
        const float x0 = b->rect.x, y0 = b->rect.y, x1 = x0 + b->rect.w, y1 = y0 + b->rect.h;
        const float cx = x0 + b->rect.w * 0.5f, cy = y0 + b->rect.h * 0.5f;

        if (b->rect.w <= 0.0f || b->rect.h <= 0.0f) {
            continue;
        }
        if (b->round) {
            const float r = b->rect.w * 0.5f;
            disc(cx, cy, r, held ? kFillHeld : kFill);
            ring(cx, cy, r - th * 0.5f, th, held ? kEdgeHeld : kEdge);
            symbol(z, cx, cy, r * 0.42f, th * 1.3f, symbolColour(z));
        } else {
            rect(x0, y0, x1, y1, held ? kFillHeld : kFill);
            frame(x0, y0, x1, y1, th, held ? kEdgeHeld : kEdge);
            if (z <= ICO_TOUCH_B_RIGHT) {
                const float s = (b->rect.w < b->rect.h ? b->rect.w : b->rect.h) * 0.25f;
                arrow(z, cx, cy, s, held ? kEdgeHeld : kMark);
            }
        }
    }
}

static void drawSticks(const IcoTouchOverlay *o, float th)
{
    const IcoTouchDrawInfo *d = &o->info;
    const float R = d->stickR;
    const int running = d->stickActive && R > 0.0f && d->stickMag * R >= d->runR - 0.5f;

    if (R > 0.0f) {
        disc(d->stickCX, d->stickCY, R, d->stickActive ? kFillHeld : kFill);
        ring(d->stickCX, d->stickCY, R - th * 0.5f, th, d->stickActive ? kEdgeHeld : kEdge);
        ring(d->stickCX, d->stickCY, d->runR, th * 0.6f, running ? kRunLit : kRun);
        disc(d->knobX, d->knobY, R * 0.38f, d->stickActive ? kEdgeHeld : kKnob);
    }
    if (d->lookActive) {
        const float r = o->layout.lookR > 0.0f ? o->layout.lookR * 0.35f : R * 0.35f;
        ring(d->lookCX, d->lookCY, r, th, kEdge);
        disc(d->lookFX, d->lookFY, r * 0.5f, kKnob);
    }
}

/* the names on the shoulders and on Start and Select, in the font's
   overlay grid (font.h: x' = left + gx W / 640, y' = box.y + (gy - 2)
   box.h / 448) */
static void drawLabels(const RdOverlayCtx *ctx, const IcoTouchOverlay *o)
{
    const float bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float W = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    const float left = (float)ctx->box.x + (bw - W) * 0.5f, top = (float)ctx->box.y;
    const float sx = W / 640.0f, sy = bh / 448.0f;

    if (ctx->box.w == 0 || ctx->box.h == 0 || !ui_FontInit()) {
        return;
    }
    ui_BeginOverlay(ctx);
    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &o->layout.button[z];
        const char *name = labelOf(z);
        const int held = (o->info.pressed >> z) & 1u;
        float a = (held ? 128.0f : 104.0f) * o->opacity;
        uint8_t c[4] = {0x80, 0x80, 0x80, 0};

        if (name == NULL || b->rect.w <= 0.0f || b->rect.h <= 0.0f) {
            continue;
        }
        c[3] = (uint8_t)(a > 128.0f ? 128.0f : a + 0.5f);
        /* the box's middle and a height of about 45 % of it, in output
           pixels, then in the grid */
        const float px = (b->rect.x + b->rect.w * 0.5f) * s_d.kx;
        const float py = (b->rect.y + b->rect.h * 0.5f) * s_d.ky;
        const float hPx = b->rect.h * s_d.ky * (name[1] == '\0' || name[2] == '\0' ? 0.5f : 0.38f);
        ui_DrawText((px - left) / sx, (py - top) / sy + 2.0f, hPx / sy, c, name,
                    UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
    }
    ui_EndOverlay();
}

void ui_TouchDrawOverlay(const struct RdOverlayCtx *ctx)
{
    IcoTouchOverlay o;

    if (!ctx || !s_source || !s_source(&o) || !(o.opacity > 0.0f) || o.layout.outW == 0 ||
        o.layout.outH == 0 || ctx->outW == 0 || ctx->outH == 0) {
        return;
    }
    UiGlyphXf xf;
    xf.opacity = o.opacity > 1.0f ? 1.0f : o.opacity;
    xf.ox = xf.oy = 0.0f;
    /* the zones were built for the window's pixels; the output is the
       same size but for a swapchain still catching up with a resize */
    s_d.kx = (float)ctx->outW / (float)o.layout.outW;
    s_d.ky = (float)ctx->outH / (float)o.layout.outH;
    xf.kx = s_d.kx;
    xf.ky = s_d.ky;
    xf.maxX = (float)(ctx->outW < 4095u ? ctx->outW : 4095u) * 16.0f;
    xf.maxY = (float)(ctx->outH < 4095u ? ctx->outH : 4095u) * 16.0f;
    ui_GlyphBegin(&xf);
    float th = o.layout.unit * 0.006f;
    th = th < 2.0f ? 2.0f : th;
    drawSticks(&o, th);
    drawButtons(&o, th);
    ui_GlyphFlush();
    drawLabels(ctx, &o);
}

#else

void ui_TouchDrawOverlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

#endif
