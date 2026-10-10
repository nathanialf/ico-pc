/*
 * port/ui/touch_ui.c
 *
 * The touch overlay's drawing (touch_ui.h): shapes as untextured triangles
 * in output pixels, the game's button pictures as sprites in output pixels,
 * the words through the font's overlay mode.
 */
#include "touch_ui.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "touch.h"

#ifdef ICO_RD
#include <math.h>

#include "font.h"
#include "layout_ext.h"
#include "rd.h"
#include "strings.h"

/* seki/src/Texture.c: the rd texture of a texture table entry (0: none) */
extern unsigned int tex_HostTextureId(int idx);
#endif

static int (*s_source)(struct IcoTouchOverlay *out);

void ui_touch_set_source(int (*fn)(struct IcoTouchOverlay *out))
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
        rd_overlay_prims(RD_PRIM_TRIANGLES, s_d.v, s_d.n, (RdTex){0}, RD_BLEND_LERP_AS);
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

/* The game's own picture for a zone (layout_ext.h LtExtGlyph), -1 for
   the zones that have none: Up, Down, Start and Select are words. */
static int glyphOf(int zone)
{
    switch (zone) {
    case ICO_TOUCH_B_CROSS:
        return LT_GLYPH_CROSS;
    case ICO_TOUCH_B_CIRCLE:
        return LT_GLYPH_CIRCLE;
    case ICO_TOUCH_B_SQUARE:
        return LT_GLYPH_SQUARE;
    case ICO_TOUCH_B_TRIANGLE:
        return LT_GLYPH_TRIANGLE;
    case ICO_TOUCH_B_L1:
        return LT_GLYPH_L1;
    case ICO_TOUCH_B_R1:
        return LT_GLYPH_R1;
    case ICO_TOUCH_B_L2:
        return LT_GLYPH_L2;
    case ICO_TOUCH_B_R2:
        return LT_GLYPH_R2;
    case ICO_TOUCH_B_LEFT:
        return LT_GLYPH_LEFT;
    case ICO_TOUCH_B_RIGHT:
        return LT_GLYPH_RIGHT;
    default:
        return -1;
    }
}

/* the word on a zone that has no picture on screen (yet) */
static const char *labelOf(int zone)
{
    switch (zone) {
    case ICO_TOUCH_B_UP:
        return ui_str(UI_STR_PHOTO_UP);
    case ICO_TOUCH_B_DOWN:
        return ui_str(UI_STR_PHOTO_DOWN);
    case ICO_TOUCH_B_LEFT:
        return ui_str(UI_STR_TOUCH_LEFT);
    case ICO_TOUCH_B_RIGHT:
        return ui_str(UI_STR_TOUCH_RIGHT);
    case ICO_TOUCH_B_CROSS:
        return ui_str(UI_STR_BTN_CROSS);
    case ICO_TOUCH_B_CIRCLE:
        return ui_str(UI_STR_BTN_CIRCLE);
    case ICO_TOUCH_B_SQUARE:
        return ui_str(UI_STR_BTN_SQUARE);
    case ICO_TOUCH_B_TRIANGLE:
        return ui_str(UI_STR_BTN_TRIANGLE);
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
                     kRun[4] = {0x80, 0x80, 0x80, 0x38}, kRunLit[4] = {0x80, 0x78, 0x50, 0x70},
                     kKnob[4] = {0x80, 0x80, 0x80, 0x50};

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
        } else {
            rect(x0, y0, x1, y1, held ? kFillHeld : kFill);
            frame(x0, y0, x1, y1, th, held ? kEdgeHeld : kEdge);
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

/* the game's button pictures on the discs and keys, each zone that has one
   (glyphOf) and whose sheet is loaded.  The sprites are in 12.4 output
   pixels as the menus' glyphs are (font.c ovEmit), the texel rectangle half
   a texel in on each side as display_texture samples it; zones on one
   texture go in one draw.  Returns the mask of the zones drawn. */
static uint32_t drawGlyphs(const IcoTouchOverlay *o)
{
    unsigned tex[ICO_TOUCH_BUTTONS];
    uint32_t drawn = 0, done = 0;

    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const int g = glyphOf(z);
        const int no = g >= 0 ? lt_ext_glyph_texture(g) : -1;

        tex[z] = no >= 0 ? tex_HostTextureId(no) : 0u;
    }
    for (int first = 0; first < ICO_TOUCH_BUTTONS; first++) {
        RdScreenVtx v[2 * ICO_TOUCH_BUTTONS];
        uint32_t n = 0;

        if (!tex[first] || ((done >> first) & 1u)) {
            continue;
        }
        for (int z = first; z < ICO_TOUCH_BUTTONS; z++) {
            const IcoTouchButton *b = &o->layout.button[z];
            const int held = (o->info.pressed >> z) & 1u;
            int uvwh[4], gw = 0, gh = 0;

            if (tex[z] != tex[first] || b->rect.w <= 0.0f || b->rect.h <= 0.0f) {
                continue;
            }
            done |= 1u << z;
            if (lt_ext_glyph_source(glyphOf(z), uvwh) < 0) {
                continue;
            }
            lt_ext_glyph_box(glyphOf(z), 27.0f, &gw, &gh);
            if (gw <= 0 || gh <= 0) {
                continue;
            }
            /* the grid's x unit is 14/15 of its y unit on screen */
            const float a = (float)gw * 14.0f / (15.0f * (float)gh);
            float bw, bh;
            if (b->round) {
                bw = bh = b->rect.w * 0.78f;
            } else if (z <= ICO_TOUCH_B_RIGHT) {
                bw = b->rect.w * 0.6f;
                bh = b->rect.h * 0.6f;
            } else {
                bw = b->rect.w * 0.8f;
                bh = b->rect.h * 0.7f;
            }
            /* bw and bh are in layout pixels; the fit is made in output
               pixels so the glyph keeps its proportions when kx != ky */
            bw *= s_d.kx;
            bh *= s_d.ky;
            const float w = bw < bh * a ? bw : bh * a, h = w / a;
            const float cx = (b->rect.x + b->rect.w * 0.5f) * s_d.kx;
            const float cy = (b->rect.y + b->rect.h * 0.5f) * s_d.ky;
            float al = (float)(held ? 0x80 : 0x70) * s_d.opacity;
            RdScreenVtx *p = &v[n++], *q = &v[n++];

            al = al > 128.0f ? 128.0f : al;
            memset(p, 0, sizeof(*p) * 2);
            p->x = fix16(cx - w * 0.5f, s_d.maxX);
            p->y = fix16(cy - h * 0.5f, s_d.maxY);
            q->x = fix16(cx + w * 0.5f, s_d.maxX);
            q->y = fix16(cy + h * 0.5f, s_d.maxY);
            p->s = ((float)uvwh[0] + 0.5f) * 16.0f;
            p->t = ((float)uvwh[1] + 0.5f) * 16.0f;
            q->s = ((float)(uvwh[0] + uvwh[2]) - 0.5f) * 16.0f;
            q->t = ((float)(uvwh[1] + uvwh[3]) - 0.5f) * 16.0f;
            p->q = q->q = 1.0f;
            p->rgba[0] = p->rgba[1] = p->rgba[2] = q->rgba[0] = q->rgba[1] = q->rgba[2] = 0x80;
            p->rgba[3] = q->rgba[3] = (uint8_t)(al + 0.5f);
            drawn |= 1u << z;
        }
        if (n > 0) {
            rd_overlay_prims(RD_PRIM_SPRITES, v, n, (RdTex){tex[first]}, RD_BLEND_LERP_AS);
        }
    }
    return drawn;
}

/* an output pixel to the font's overlay grid: the inverse of
   font.h's x' = left + gx W / 640, y' = box.y + (gy - 2) box.h / 448 */
static void pxToGrid(const RdOverlayCtx *ctx, float px, float py, float *gx, float *gy)
{
    const float bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float W = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    const float left = (float)ctx->box.x + (bw - W) * 0.5f;

    *gx = (px - left) * 640.0f / W;
    *gy = (py - (float)ctx->box.y) * 448.0f / bh + 2.0f;
}

/* the words: on the zones without a picture (Up, Down, Start, Select; the
   others while their sheet is not loaded), centred on the zone, about 45 %
   of its short side tall and shrunk to fit 85 % of its width */
static void drawLabels(const RdOverlayCtx *ctx, const IcoTouchOverlay *o, uint32_t withGlyph)
{
    const float bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float W = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);

    if (ctx->box.w == 0 || ctx->box.h == 0 || !ui_font_init()) {
        return;
    }
    ui_begin_overlay(ctx);
    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &o->layout.button[z];
        const char *name = ((withGlyph >> z) & 1u) ? NULL : labelOf(z);
        const int held = (o->info.pressed >> z) & 1u;
        float a = (held ? 128.0f : 104.0f) * o->opacity;
        uint8_t c[4] = {0x80, 0x80, 0x80, 0};

        if (name == NULL || b->rect.w <= 0.0f || b->rect.h <= 0.0f) {
            continue;
        }
        c[3] = (uint8_t)(a > 128.0f ? 128.0f : a + 0.5f);
        const float wPx = b->rect.w * s_d.kx, hBox = b->rect.h * s_d.ky;
        const float px = (b->rect.x + b->rect.w * 0.5f) * s_d.kx;
        const float py = (b->rect.y + b->rect.h * 0.5f) * s_d.ky;
        float size = 0.45f * (wPx < hBox ? wPx : hBox) * 448.0f / bh; /* grid y units */
        for (int i = 0;
             i < 24 && size > 4.0f && ui_measure_text(size, name) * W / 640.0f > 0.85f * wPx; i++) {
            size *= 0.92f;
        }
        float gx, gy;
        pxToGrid(ctx, px, py, &gx, &gy);
        ui_draw_text(gx, gy, size, c, name, UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
    }
    ui_end_overlay();
}

void ui_touch_draw_overlay(const struct RdOverlayCtx *ctx)
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
    float th = o.layout.unit * 0.4f;
    th = th < 2.0f ? 2.0f : th;
    drawSticks(&o, th);
    drawButtons(&o, th);
    flush();
    const uint32_t withGlyph = drawGlyphs(&o);
    drawLabels(ctx, &o, withGlyph);
}

#else

void ui_touch_draw_overlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

#endif
