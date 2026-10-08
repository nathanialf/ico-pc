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
#include "rd.h"
#endif

static int (*s_source)(struct IcoTouchOverlay *out);

void ui_TouchSetSource(int (*fn)(struct IcoTouchOverlay *out))
{
    s_source = fn;
}

#ifdef ICO_RD

#define SEGMENTS 32
#define BATCH 960 /* vertices, a multiple of 3 */

static struct {
    RdScreenVtx v[BATCH];
    uint32_t n;
    float kx, ky;     /* layout pixels to output pixels */
    float maxX, maxY; /* the output, 12.4 */
    float opacity;
    float cosT[SEGMENTS + 1], sinT[SEGMENTS + 1];
    int tables;
} s_d;

static void flush(void)
{
    if (s_d.n > 0) {
        rd_OverlayPrims(RD_PRIM_TRIANGLES, s_d.v, s_d.n, (RdTex){0}, RD_BLEND_LERP_AS);
        s_d.n = 0;
    }
}

static int32_t fix16(float p, float max16)
{
    float f = p * 16.0f;

    f = f < 0.0f ? 0.0f : f > max16 ? max16 : f;
    return (int32_t)lrintf(f);
}

/* one vertex at layout pixel (x, y); rgba in GS units, the alpha before
   the overlay's opacity */
static void vtx(float x, float y, const uint8_t rgba[4])
{
    RdScreenVtx *v = &s_d.v[s_d.n++];
    float a = (float)rgba[3] * s_d.opacity;

    v->x = fix16(x * s_d.kx, s_d.maxX);
    v->y = fix16(y * s_d.ky, s_d.maxY);
    v->z = 0;
    v->s = v->t = 0.0f;
    v->q = 1.0f;
    v->rgba[0] = rgba[0];
    v->rgba[1] = rgba[1];
    v->rgba[2] = rgba[2];
    v->rgba[3] = (uint8_t)(a < 0.0f ? 0.0f : a > 128.0f ? 128.0f : a + 0.5f);
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, const uint8_t c[4])
{
    if (s_d.n + 3 > BATCH) {
        flush();
    }
    vtx(x0, y0, c);
    vtx(x1, y1, c);
    vtx(x2, y2, c);
}

static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                 const uint8_t c[4])
{
    tri(x0, y0, x1, y1, x2, y2, c);
    tri(x0, y0, x2, y2, x3, y3, c);
}

static void rect(float x0, float y0, float x1, float y1, const uint8_t c[4])
{
    quad(x0, y0, x1, y0, x1, y1, x0, y1, c);
}

/* a rect's border, th thick, inside it */
static void frame(float x0, float y0, float x1, float y1, float th, const uint8_t c[4])
{
    rect(x0, y0, x1, y0 + th, c);
    rect(x0, y1 - th, x1, y1, c);
    rect(x0, y0 + th, x0 + th, y1 - th, c);
    rect(x1 - th, y0 + th, x1, y1 - th, c);
}

static void tables(void)
{
    if (!s_d.tables) {
        for (int i = 0; i <= SEGMENTS; i++) {
            const float a = 6.28318531f * (float)i / (float)SEGMENTS;
            s_d.cosT[i] = cosf(a);
            s_d.sinT[i] = sinf(a);
        }
        s_d.tables = 1;
    }
}

static void disc(float cx, float cy, float r, const uint8_t c[4])
{
    for (int i = 0; i < SEGMENTS; i++) {
        tri(cx, cy, cx + r * s_d.cosT[i], cy + r * s_d.sinT[i], cx + r * s_d.cosT[i + 1],
            cy + r * s_d.sinT[i + 1], c);
    }
}

/* a ring of radius r (its middle), th thick */
static void ring(float cx, float cy, float r, float th, const uint8_t c[4])
{
    const float r0 = r - th * 0.5f > 0.0f ? r - th * 0.5f : 0.0f, r1 = r + th * 0.5f;

    for (int i = 0; i < SEGMENTS; i++) {
        quad(cx + r0 * s_d.cosT[i], cy + r0 * s_d.sinT[i], cx + r1 * s_d.cosT[i],
             cy + r1 * s_d.sinT[i], cx + r1 * s_d.cosT[i + 1], cy + r1 * s_d.sinT[i + 1],
             cx + r0 * s_d.cosT[i + 1], cy + r0 * s_d.sinT[i + 1], c);
    }
}

/* a stroke from (x0, y0) to (x1, y1), th thick */
static void line(float x0, float y0, float x1, float y1, float th, const uint8_t c[4])
{
    float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);

    if (len <= 0.0f) {
        return;
    }
    dx *= 0.5f * th / len;
    dy *= 0.5f * th / len;
    quad(x0 - dy, y0 + dx, x1 - dy, y1 + dx, x1 + dy, y1 - dx, x0 + dy, y0 - dx, c);
}

/* the PS2 symbols, s the half size */
static void symbol(int zone, float cx, float cy, float s, float th, const uint8_t c[4])
{
    switch (zone) {
    case ICO_TOUCH_B_CROSS:
        line(cx - s, cy - s, cx + s, cy + s, th, c);
        line(cx - s, cy + s, cx + s, cy - s, th, c);
        break;
    case ICO_TOUCH_B_CIRCLE:
        ring(cx, cy, s, th, c);
        break;
    case ICO_TOUCH_B_SQUARE:
        frame(cx - s * 0.85f, cy - s * 0.85f, cx + s * 0.85f, cy + s * 0.85f, th, c);
        break;
    case ICO_TOUCH_B_TRIANGLE: {
        const float top = cy - s, base = cy + s * 0.7f, half = s * 1.0f;
        line(cx, top, cx + half, base, th, c);
        line(cx + half, base, cx - half, base, th, c);
        line(cx - half, base, cx, top, th, c);
        break;
    }
    default:
        break;
    }
}

/* a D-pad key's arrow, pointing away from the cluster's centre */
static void arrow(int zone, float cx, float cy, float s, const uint8_t c[4])
{
    switch (zone) {
    case ICO_TOUCH_B_UP:
        tri(cx, cy - s, cx + s, cy + s * 0.6f, cx - s, cy + s * 0.6f, c);
        break;
    case ICO_TOUCH_B_DOWN:
        tri(cx, cy + s, cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, c);
        break;
    case ICO_TOUCH_B_LEFT:
        tri(cx - s, cy, cx + s * 0.6f, cy - s, cx + s * 0.6f, cy + s, c);
        break;
    case ICO_TOUCH_B_RIGHT:
        tri(cx + s, cy, cx - s * 0.6f, cy + s, cx - s * 0.6f, cy - s, c);
        break;
    default:
        break;
    }
}

/* the face buttons' colours, GS units (0x80 = 1.0) */
static const uint8_t *symbolColour(int zone)
{
    static const uint8_t cross[4] = {0x50, 0x6C, 0x80, 0x80}, circle[4] = {0x80, 0x48, 0x48, 0x80},
                         square[4] = {0x80, 0x5C, 0x78, 0x80},
                         triangle[4] = {0x40, 0x80, 0x68, 0x80};

    switch (zone) {
    case ICO_TOUCH_B_CROSS:
        return cross;
    case ICO_TOUCH_B_CIRCLE:
        return circle;
    case ICO_TOUCH_B_SQUARE:
        return square;
    default:
        return triangle;
    }
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
    tables();
    s_d.n = 0;
    s_d.opacity = o.opacity > 1.0f ? 1.0f : o.opacity;
    /* the zones were built for the window's pixels; the output is the
       same size but for a swapchain still catching up with a resize */
    s_d.kx = (float)ctx->outW / (float)o.layout.outW;
    s_d.ky = (float)ctx->outH / (float)o.layout.outH;
    s_d.maxX = (float)(ctx->outW < 4095u ? ctx->outW : 4095u) * 16.0f;
    s_d.maxY = (float)(ctx->outH < 4095u ? ctx->outH : 4095u) * 16.0f;
    float th = o.layout.unit * 0.006f;
    th = th < 2.0f ? 2.0f : th;
    drawSticks(&o, th);
    drawButtons(&o, th);
    flush();
    drawLabels(ctx, &o);
}

#else

void ui_TouchDrawOverlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

#endif
