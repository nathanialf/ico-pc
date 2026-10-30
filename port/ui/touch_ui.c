/*
 * port/ui/touch_ui.c
 *
 * The touch overlay's drawing (touch_ui.h): shapes as untextured triangles
 * in output pixels, the game's button pictures as sprites in output pixels,
 * the words through the font's overlay mode; each control at its layout
 * edits' opacity (IcoTouchButton.alpha, stickAlpha, lookAlpha), and the
 * touch layout editor's marks while its page is open (IcoTouchEditView).
 */
#include "touch_ui.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "touch.h"
#include "touch_gap.h"
#include "touch_edit.h" /* the editor's keys (ICO_TOUCH_ED_*) */

#ifdef ICO_RD
#include <math.h>

#include "font.h"
#include "charFileManager.h" /* texFile */
#include "layout_ext.h"
#include "rd.h"
#include "strings.h"

/* seki/src/Texture.c: the rd texture of a texture table entry (0: none) */
extern unsigned int tex_HostTextureId(int idx);
extern char *tex_GetTextureName(int idx);
extern int tex_GetTextureNum(void);
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
    float alpha; /* the control being drawn: its edits' opacity (1 for the editor's marks) */
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
    float a = (float)rgba[3] * s_d.opacity * s_d.alpha;

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
    default:
        return -1;
    }
}

/* the word on a zone that has no picture on screen (yet) */
static const char *labelOf(int zone)
{
    switch (zone) {
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
/* the editor's: the grid's dots, the hinge band, a key of the toolbar (lit:
   All or Snap on), the backing of its words, and the amber of an overlap */
static const uint8_t kDot[4] = {0x80, 0x80, 0x80, 0x30}, kBand[4] = {0x40, 0x40, 0x44, 0x30},
                     kKey[4] = {0x14, 0x14, 0x18, 0x60}, kKeyLit[4] = {0x50, 0x50, 0x58, 0x70},
                     kWarn[4] = {0x80, 0x60, 0x10, 0x80};

/* the faintest a shown control is drawn while the editor is open, so every
   one can be seen and picked however faint it is in play */
#define EDIT_ALPHA_FLOOR 0.35f
/* a key that does nothing now (Size and Opacity with nothing selected) */
#define KEY_DIM 0.4f
/* an Off control's dashed outline and word against a Hidden one's (which
   still takes touches) */
#define OFF_DIM 0.5f

/* The opacity a control is drawn at: its edits' alpha, at most 1, at least
   EDIT_ALPHA_FLOOR while editing; -1 for a Hidden or Off one (alpha 0:
   nothing in play, a dashed outline and the word Hidden or Off while
   editing, an Off one's dimmer). */
static float shownAlpha(const IcoTouchOverlay *o, float a)
{
    if (!(a > 0.0f)) {
        return -1.0f;
    }
    a = a > 1.0f ? 1.0f : a;
    return o->edit.active && a < EDIT_ALPHA_FLOOR ? EDIT_ALPHA_FLOOR : a;
}

/* a rect's border, th thick, in dashes three th long */
static void dashedFrame(float x0, float y0, float x1, float y1, float th, const uint8_t c[4])
{
    const float dash = 3.0f * th;

    if (!(dash > 0.0f) || !(x1 - x0 > 2.0f * th) || !(y1 - y0 > 2.0f * th)) {
        return;
    }
    for (float x = x0; x < x1; x += 2.0f * dash) {
        const float xe = x + dash < x1 ? x + dash : x1;
        rect(x, y0, xe, y0 + th, c);
        rect(x, y1 - th, xe, y1, c);
    }
    for (float y = y0 + th + dash; y < y1 - th; y += 2.0f * dash) {
        const float ye = y + dash < y1 - th ? y + dash : y1 - th;
        rect(x0, y, x0 + th, ye, c);
        rect(x1 - th, y, x1, ye, c);
    }
}

/* a ring in dashes: every other of its segments */
static void dashedRing(float cx, float cy, float r, float th, const uint8_t c[4])
{
    const float r0 = r - th * 0.5f > 0.0f ? r - th * 0.5f : 0.0f, r1 = r + th * 0.5f;

    for (int i = 0; i < SEGMENTS; i += 2) {
        quad(cx + r0 * s_d.cosT[i], cy + r0 * s_d.sinT[i], cx + r1 * s_d.cosT[i],
             cy + r1 * s_d.sinT[i], cx + r1 * s_d.cosT[i + 1], cy + r1 * s_d.sinT[i + 1],
             cx + r0 * s_d.cosT[i + 1], cy + r0 * s_d.sinT[i + 1], c);
    }
}

/* The D-pad as one plus-shaped cross: the four keys' rectangles joined
   through the square between them, filled and outlined in the other
   buttons' colours, the pressed arm brighter.  The pieces do not overlap,
   so the translucent fill and edge are even. */
static void drawDpad(const IcoTouchOverlay *o, float th)
{
    const IcoTouchButton *u = &o->layout.button[ICO_TOUCH_B_UP],
                         *d = &o->layout.button[ICO_TOUCH_B_DOWN],
                         *l = &o->layout.button[ICO_TOUCH_B_LEFT],
                         *r = &o->layout.button[ICO_TOUCH_B_RIGHT];
    const float x0 = l->rect.x, x1 = u->rect.x, x2 = u->rect.x + u->rect.w,
                x3 = r->rect.x + r->rect.w, y0 = u->rect.y, y1 = l->rect.y,
                y2 = l->rect.y + l->rect.h, y3 = d->rect.y + d->rect.h;
    const int hu = (o->info.pressed >> ICO_TOUCH_B_UP) & 1u,
              hd = (o->info.pressed >> ICO_TOUCH_B_DOWN) & 1u,
              hl = (o->info.pressed >> ICO_TOUCH_B_LEFT) & 1u,
              hr = (o->info.pressed >> ICO_TOUCH_B_RIGHT) & 1u;
    const uint8_t *eu = hu ? kEdgeHeld : kEdge, *ed = hd ? kEdgeHeld : kEdge,
                  *el = hl ? kEdgeHeld : kEdge, *er = hr ? kEdgeHeld : kEdge;

    const float a = shownAlpha(o, u->alpha);

    if (!(x1 < x2) || !(x0 < x1) || !(x2 < x3) || !(y0 < y1) || !(y1 < y2) || !(y2 < y3) ||
        2.0f * th >= x2 - x1 || 2.0f * th >= y2 - y1) {
        return;
    }
    if (a < 0.0f) {
        /* Hidden or Off: only the editor shows where it is, each key dashed */
        if (o->edit.active) {
            s_d.alpha = u->active ? 1.0f : OFF_DIM;
            dashedFrame(x1, y0, x2, y1, th, kEdge);
            dashedFrame(x1, y2, x2, y3, th, kEdge);
            dashedFrame(x0, y1, x1, y2, th, kEdge);
            dashedFrame(x2, y1, x3, y2, th, kEdge);
            s_d.alpha = 1.0f;
        }
        return;
    }
    s_d.alpha = a;
    rect(x1, y0, x2, y1, hu ? kFillHeld : kFill);
    rect(x1, y2, x2, y3, hd ? kFillHeld : kFill);
    rect(x0, y1, x1, y2, hl ? kFillHeld : kFill);
    rect(x2, y1, x3, y2, hr ? kFillHeld : kFill);
    rect(x1, y1, x2, y2, kFill);
    /* up arm */
    rect(x1, y0, x2, y0 + th, eu);
    rect(x1, y0 + th, x1 + th, y1, eu);
    rect(x2 - th, y0 + th, x2, y1, eu);
    /* left arm */
    rect(x0, y1, x1, y1 + th, el);
    rect(x0, y1 + th, x0 + th, y2 - th, el);
    rect(x0, y2 - th, x1, y2, el);
    /* right arm */
    rect(x2, y1, x3, y1 + th, er);
    rect(x3 - th, y1 + th, x3, y2 - th, er);
    rect(x2, y2 - th, x3, y2, er);
    /* down arm */
    rect(x1, y2, x1 + th, y3 - th, ed);
    rect(x2 - th, y2, x2, y3 - th, ed);
    rect(x1, y3 - th, x2, y3, ed);
    /* the four inner corners, where two arms meet */
    rect(x1, y1, x1 + th, y1 + th, hu || hl ? kEdgeHeld : kEdge);
    rect(x2 - th, y1, x2, y1 + th, hu || hr ? kEdgeHeld : kEdge);
    rect(x1, y2 - th, x1 + th, y2, hd || hl ? kEdgeHeld : kEdge);
    rect(x2 - th, y2 - th, x2, y2, hd || hr ? kEdgeHeld : kEdge);
    s_d.alpha = 1.0f;
}

static void drawButtons(const IcoTouchOverlay *o, float th)
{
    const IcoTouchLayout *l = &o->layout;

    for (int z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &l->button[z];
        const int held = (o->info.pressed >> z) & 1u;
        const float x0 = b->rect.x, y0 = b->rect.y, x1 = x0 + b->rect.w, y1 = y0 + b->rect.h;
        const float cx = x0 + b->rect.w * 0.5f, cy = y0 + b->rect.h * 0.5f;

        const float a = shownAlpha(o, b->alpha);

        if (b->rect.w <= 0.0f || b->rect.h <= 0.0f || z <= ICO_TOUCH_B_RIGHT) {
            continue;
        }
        if (a < 0.0f) {
            /* Hidden or Off: only the editor shows where it is (Off dimmer) */
            if (o->edit.active) {
                s_d.alpha = b->active ? 1.0f : OFF_DIM;
                if (b->round) {
                    dashedRing(cx, cy, b->rect.w * 0.5f - th * 0.5f, th, kEdge);
                } else {
                    dashedFrame(x0, y0, x1, y1, th, kEdge);
                }
                s_d.alpha = 1.0f;
            }
            continue;
        }
        s_d.alpha = a;
        if (b->round) {
            const float r = b->rect.w * 0.5f;
            disc(cx, cy, r, held ? kFillHeld : kFill);
            ring(cx, cy, r - th * 0.5f, th, held ? kEdgeHeld : kEdge);
        } else {
            rect(x0, y0, x1, y1, held ? kFillHeld : kFill);
            frame(x0, y0, x1, y1, th, held ? kEdgeHeld : kEdge);
        }
        s_d.alpha = 1.0f;
    }
}

static void drawSticks(const IcoTouchOverlay *o, float th)
{
    const IcoTouchDrawInfo *d = &o->info;
    const IcoTouchLayout *l = &o->layout;
    const float sa = shownAlpha(o, l->stickAlpha), la = shownAlpha(o, l->lookAlpha);

    if (o->edit.active) {
        /* the editor: the stick where it rests (the step's state is not
           kept while editing), the look pad's area outlined (it draws
           nothing else until a finger is on it) */
        const IcoTouchRect *a = &l->lookArea;
        if (l->stickR > 0.0f && sa >= 0.0f) {
            s_d.alpha = sa;
            disc(l->stickHomeX, l->stickHomeY, l->stickR, kFill);
            ring(l->stickHomeX, l->stickHomeY, l->stickR - th * 0.5f, th, kEdge);
            ring(l->stickHomeX, l->stickHomeY, l->runR, th * 0.6f, kRun);
            disc(l->stickHomeX, l->stickHomeY, l->stickR * 0.38f, kKnob);
        } else if (l->stickR > 0.0f) {
            /* Hidden or Off: its ring dashed (Off dimmer) */
            s_d.alpha = l->stickActive ? 1.0f : OFF_DIM;
            dashedRing(l->stickHomeX, l->stickHomeY, l->stickR - th * 0.5f, th, kEdge);
        }
        if (a->w > 2.0f * th && a->h > 2.0f * th) {
            s_d.alpha = la >= 0.0f ? la : l->lookActive ? 1.0f : OFF_DIM;
            if (la >= 0.0f) {
                frame(a->x, a->y, a->x + a->w, a->y + a->h, th * 0.6f, kRun);
            } else {
                dashedFrame(a->x, a->y, a->x + a->w, a->y + a->h, th, kEdge);
            }
        }
        s_d.alpha = 1.0f;
        return;
    }

    const float R = d->stickR;
    const int running = d->stickActive && R > 0.0f && d->stickMag * R >= d->runR - 0.5f;

    if (R > 0.0f && sa >= 0.0f) {
        s_d.alpha = sa;
        disc(d->stickCX, d->stickCY, R, d->stickActive ? kFillHeld : kFill);
        ring(d->stickCX, d->stickCY, R - th * 0.5f, th, d->stickActive ? kEdgeHeld : kEdge);
        ring(d->stickCX, d->stickCY, d->runR, th * 0.6f, running ? kRunLit : kRun);
        disc(d->knobX, d->knobY, R * 0.38f, d->stickActive ? kEdgeHeld : kKnob);
    }
    if (d->lookActive && la >= 0.0f) {
        const float r = l->lookR > 0.0f ? l->lookR * 0.35f : R * 0.35f;
        s_d.alpha = la;
        ring(d->lookCX, d->lookCY, r, th, kEdge);
        disc(d->lookFX, d->lookFY, r * 0.5f, kKnob);
    }
    s_d.alpha = 1.0f;
}

/* Whether table slot no holds the sheet the glyph is cut from.  A glyph
   names its sheet's texture by slot number (lt_ext_glyph_texture), and a
   slot is reloaded with another texture, or freed, when the game changes
   between a stage and the menus; until the sheet is back the slot holds
   something else, and drawing the glyph's rectangle from it would show
   that texture.  The slot's name is compared with the sheet's file name
   (the texFile row the glyph's texProperty row points at, without its
   directory and extension, as layout_texture.c names textures). */
static int sheetLoaded(int glyph, int no)
{
    const int row = lt_ext_glyph_source(glyph, NULL);
    const char *want, *have, *dot;
    size_t len;

    if (row < 0 || no < 0 || no >= tex_GetTextureNum()) {
        return 0;
    }
    want = texFile[texProperty[row].texFileNo].path;
    have = tex_GetTextureName(no);
    if (want == NULL || have == NULL) {
        return 0;
    }
    if ((dot = strrchr(want, '/')) != NULL) {
        want = dot + 1;
    }
    dot = strrchr(want, '.');
    len = dot != NULL ? (size_t)(dot - want) : strlen(want);
    return len > 0 && strncmp(have, want, len) == 0 && have[len] == '\0';
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

        tex[z] = no >= 0 && sheetLoaded(g, no) ? tex_HostTextureId(no) : 0u;
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

            const float ca = shownAlpha(o, b->alpha);

            if (tex[z] != tex[first] || b->rect.w <= 0.0f || b->rect.h <= 0.0f) {
                continue;
            }
            done |= 1u << z;
            if (ca < 0.0f) {
                continue; /* Hidden or Off: no picture (the editor writes the word) */
            }
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
            float al = (float)(held ? 0x80 : 0x70) * s_d.opacity * ca;
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
        const float ca = shownAlpha(o, b->alpha);
        /* a Hidden or Off one's word is Hidden or Off while editing (none in
           play; the D-pad's is written once, below) */
        const char *name = ca < 0.0f ? (o->edit.active && z > ICO_TOUCH_B_RIGHT
                                            ? ui_str(b->active ? UI_STR_TED_HIDDEN : UI_STR_TED_OFF)
                                            : NULL)
                           : ((withGlyph >> z) & 1u) ? NULL
                                                     : labelOf(z);
        const int held = (o->info.pressed >> z) & 1u;
        float a = (held ? 128.0f : 104.0f) * s_d.opacity *
                  (ca >= 0.0f  ? ca
                   : b->active ? 1.0f
                               : OFF_DIM);
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
    {
        /* the D-pad's word, Hidden or Off, across its middle row of keys
           while editing */
        const IcoTouchButton *u = &o->layout.button[ICO_TOUCH_B_UP],
                             *lk = &o->layout.button[ICO_TOUCH_B_LEFT],
                             *rk = &o->layout.button[ICO_TOUCH_B_RIGHT];
        const float wPx = (rk->rect.x + rk->rect.w - lk->rect.x) * s_d.kx,
                    hBox = lk->rect.h * s_d.ky;
        if (o->edit.active && !(u->alpha > 0.0f) && wPx > 0.0f && hBox > 0.0f) {
            const char *name = ui_str(u->active ? UI_STR_TED_HIDDEN : UI_STR_TED_OFF);
            const float a = 104.0f * s_d.opacity * (u->active ? 1.0f : OFF_DIM);
            uint8_t c[4] = {0x80, 0x80, 0x80, (uint8_t)(a > 128.0f ? 128.0f : a + 0.5f)};
            float size = 0.45f * hBox * 448.0f / bh, gx, gy;
            for (int i = 0;
                 i < 24 && size > 4.0f && ui_measure_text(size, name) * W / 640.0f > 0.85f * wPx;
                 i++) {
                size *= 0.92f;
            }
            pxToGrid(ctx, (lk->rect.x + (rk->rect.x + rk->rect.w - lk->rect.x) * 0.5f) * s_d.kx,
                     (lk->rect.y + lk->rect.h * 0.5f) * s_d.ky, &gx, &gy);
            ui_draw_text(gx, gy, size, c, name, UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
        }
    }
    ui_end_overlay();
}

/* --- the touch layout editor's marks (the Edit layout page) ---------------
 * Drawn only while o->edit.active, every place from the snapshot: the
 * layout's region, band and rects, the view's toolbar, confirm strip and
 * control boxes (port/input/touch_edit.c computes them; nothing here
 * places a key). */

/* the hinge band hatched: its area faint, stripes across it at 45 degrees */
static void drawBand(const IcoTouchLayout *l, float th)
{
    const IcoTouchRect *b = &l->band;
    const float x0 = b->x, y0 = b->y, x1 = b->x + b->w, y1 = b->y + b->h;

    if (!(b->w > 0.0f) || !(b->h > 0.0f)) {
        return;
    }
    rect(x0, y0, x1, y1, kBand);
    if (b->h >= b->w) {
        /* a vertical band: each stripe falls b->w from left to right */
        for (float y = y0 + b->w; y + th <= y1; y += 4.0f * th) {
            quad(x0, y, x1, y - b->w, x1, y - b->w + th, x0, y + th, kRun);
        }
    } else {
        for (float x = x0; x + b->h + th <= x1; x += 4.0f * th) {
            quad(x, y1, x + b->h, y0, x + b->h + th, y0, x + th, y1, kRun);
        }
    }
}

/* Snap on: a faint dot where a control's centre can land, every grid step
   counted from the edge it is anchored to (the nearer edge of the region on
   each axis, as the editor anchors a drop), so what is seen is what
   snaps; carried on outward to the bounds' edges (under the camera hole);
   none in the hinge band (carried along the hinge to the bounds) */
#define GRID_DOTS 256

/* the dots of one axis: the region's [lo, lo + len] with the bounds'
   [blo, blo + blen] around it */
static int gridLine(float lo, float len, float blo, float blen, float step, float *out)
{
    const float mid = lo + len * 0.5f;
    int n = 0;

    for (int i = (int)ceilf((blo - lo) / step - 0.001f);
         n < GRID_DOTS / 2 && lo + (float)i * step <= mid; i++) {
        if (lo + (float)i * step >= blo - 0.001f) {
            out[n++] = lo + (float)i * step;
        }
    }
    for (int i = (int)ceilf((lo + len - (blo + blen)) / step - 0.001f);
         n < GRID_DOTS && lo + len - (float)i * step > mid; i++) {
        if (lo + len - (float)i * step <= blo + blen + 0.001f) {
            out[n++] = lo + len - (float)i * step;
        }
    }
    return n;
}

static void drawGrid(const IcoTouchOverlay *o, float th)
{
    const IcoTouchLayout *l = &o->layout;
    const IcoTouchRect *r = &l->region, *B = &l->bounds;
    const IcoTouchRect bandRect = ico_touch_clamp_band(l);
    const IcoTouchRect *b = &bandRect;
    const float step = o->edit.grid * l->unit, d = th * 0.5f;
    float xs[GRID_DOTS], ys[GRID_DOTS];

    if (!o->edit.snap || !(step >= 2.0f * th) || !(r->w > 0.0f) || !(r->h > 0.0f)) {
        return;
    }
    const int nx = gridLine(r->x, r->w, B->x, B->w, step, xs),
              ny = gridLine(r->y, r->h, B->y, B->h, step, ys);
    for (int i = 0; i < nx; i++) {
        for (int j = 0; j < ny; j++) {
            if (b->w > 0.0f && b->h > 0.0f && xs[i] >= b->x && xs[i] <= b->x + b->w &&
                ys[j] >= b->y && ys[j] <= b->y + b->h) {
                continue;
            }
            rect(xs[i] - d, ys[j] - d, xs[i] + d, ys[j] + d, kDot);
        }
    }
}

/* the strip's two readouts: display only, no frame */
static int readout(int k)
{
    return k == ICO_TOUCH_ED_TB_SIZE_VAL || k == ICO_TOUCH_ED_TB_OP_VAL;
}

/* whether a toolbar cell acts (or, a readout, reads) now: the opacity keys
   and both readouts need a control selected, or All on; Undo a step to
   take back */
static int keyLive(const IcoTouchEditView *v, int k)
{
    if (k >= ICO_TOUCH_ED_TB_SIZE_VAL && k <= ICO_TOUCH_ED_TB_OP_UP) {
        return v->selected >= 0 || v->all;
    }
    if (k == ICO_TOUCH_ED_TB_UNDO) {
        return v->undos > 0;
    }
    return 1;
}

static int keyLit(const IcoTouchEditView *v, int k)
{
    return (k == ICO_TOUCH_ED_TB_ALL && v->all) || (k == ICO_TOUCH_ED_TB_SNAP && v->snap);
}

/* the confirm strip's key under the gamepad's cursor, lit */
static int confirmLit(const IcoTouchEditView *v, int k)
{
    return v->confirmSel == k;
}

/* Reset asks: its three choices lie over Done .. All, the rest of the
   strip shows dimmed */
static int confirming(const IcoTouchEditView *v)
{
    return v->state == ICO_TOUCH_ED_CONFIRM_RESET;
}

static int rectShown(const IcoTouchRect *r)
{
    return r->w > 0.0f && r->h > 0.0f;
}

/* the toolbar strip's cells' height: the unit the editor's words are sized
   by; and the strip's lowest edge (the selection's name keeps below it) */
static float keyHeight(const IcoTouchEditView *v, float *bottom)
{
    float h = 0.0f;

    *bottom = 0.0f;
    for (int i = 0; i < ICO_TOUCH_ED_TB_COUNT; i++) {
        if (rectShown(&v->tb[i])) {
            h = v->tb[i].h;
            if (v->tb[i].y + v->tb[i].h > *bottom) {
                *bottom = v->tb[i].y + v->tb[i].h;
            }
        }
    }
    return h;
}

/* the line at the bottom (IcoTouchEditView.hint): the refused or overlap
   note while it applies, else the hint (none while Reset asks) */
static const char *hintText(const IcoTouchEditView *v)
{
    if (v->refused > 0) {
        return ui_str(UI_STR_TED_COVERED);
    }
    if (v->overlap != 0) {
        return ui_str(UI_STR_TED_OVERLAP);
    }
    return confirming(v) ? NULL : ui_str(UI_STR_TED_HINT);
}

/* the font's overlay grid of the output's box (font.h: 640 units across
   the 4:3 width, 448 down), as drawLabels measures */
typedef struct TextGrid {
    const RdOverlayCtx *ctx;
    float W, bh;
} TextGrid;

static TextGrid textGrid(const RdOverlayCtx *ctx)
{
    TextGrid g;
    const float bw = (float)ctx->box.w;

    g.ctx = ctx;
    g.bh = (float)ctx->box.h;
    g.W = bw < g.bh * (4.0f / 3.0f) ? bw : g.bh * (4.0f / 3.0f);
    return g;
}

/* a line's size (grid y units) for capitals hOut output pixels tall,
   shrunk to fit wOut output pixels */
static float fitSize(const TextGrid *g, float hOut, float wOut, const char *t)
{
    float size = hOut * 448.0f / g->bh;

    for (int i = 0; i < 24 && size > 4.0f && ui_measure_text(size, t) * g->W / 640.0f > wOut; i++) {
        size *= 0.92f;
    }
    return size;
}

/* one line centred on layout pixel (px, py), hPx tall at most and wPx wide,
   at alpha a (0..1) */
static void textAt(const TextGrid *g, float px, float py, float hPx, float wPx, float a,
                   const char *t)
{
    uint8_t c[4] = {0x80, 0x80, 0x80, 0};
    const float al = 128.0f * a;
    float gx, gy;

    if (t == NULL || t[0] == '\0') {
        return;
    }
    c[3] = (uint8_t)(al > 128.0f ? 128.0f : al < 0.0f ? 0.0f : al + 0.5f);
    pxToGrid(g->ctx, px * s_d.kx, py * s_d.ky, &gx, &gy);
    ui_draw_text(gx, gy, fitSize(g, hPx * s_d.ky, wPx * s_d.kx, t), c, t,
                 UI_ALIGN_CENTER | UI_VALIGN_MIDDLE);
}

/* a table string with one %d (UI_STR_VAL_MM) and its number */
static const char *intText(UiStrId id, int v, char *buf, size_t size)
{
    const char *t = ui_str(id);
    const char *at = strstr(t, "%d");

    if (at != NULL) {
        snprintf(buf, size, "%.*s%d%s", (int)(at - t), t, v, at + 2);
    } else {
        snprintf(buf, size, "%d %s", v, t);
    }
    return buf;
}

/* a strip cell's words, one line: its name, with its value where it has
   one ("Snap: On", "Grid: 4 mm"); the readouts the selected control's
   size and opacity ("Size 120 %", "75 %", Hidden or Off at 0), "All" with All
   lit, "-" with nothing selected */
static void keyWords(const IcoTouchEditView *v, int k, char *out, size_t n)
{
    static const UiStrId kName[ICO_TOUCH_ED_TB_COUNT] = {
        UI_STR_TED_DONE,    UI_STR_TED_UNDO,   UI_STR_TED_RESET, UI_STR_TED_SNAP,
        UI_STR_TED_GRID,    UI_STR_TED_ALL,    UI_STR_TED_SIZE,  UI_STR_TED_OPACITY,
        UI_STR_TED_OPACITY, UI_STR_TED_OPACITY};
    const int sel = v->selected >= 0;
    char val[48];

    out[0] = '\0';
    switch (k) {
    case ICO_TOUCH_ED_TB_SNAP:
        snprintf(out, n, "%s: %s", ui_str(UI_STR_TED_SNAP),
                 ui_str(v->snap ? UI_STR_ON : UI_STR_OFF));
        return;
    case ICO_TOUCH_ED_TB_GRID:
        snprintf(out, n, "%s: %s", ui_str(UI_STR_TED_GRID),
                 intText(UI_STR_VAL_MM, (int)(v->grid + 0.5f), val, sizeof(val)));
        return;
    case ICO_TOUCH_ED_TB_SIZE_VAL:
        if (v->all) {
            snprintf(val, sizeof(val), "%s", ui_str(UI_STR_TED_ALL));
        } else if (sel) {
            snprintf(val, sizeof(val), "%d %%", (int)(v->selScale * 100.0f + 0.5f));
        } else {
            snprintf(val, sizeof(val), "-");
        }
        snprintf(out, n, "%s %s", ui_str(UI_STR_TED_SIZE), val);
        return;
    case ICO_TOUCH_ED_TB_OP_VAL:
        if (v->all) {
            snprintf(out, n, "%s", ui_str(UI_STR_TED_ALL));
        } else if (sel && !(v->selAlpha > 0.0f)) {
            snprintf(out, n, "%s", ui_str(v->selActive ? UI_STR_TED_HIDDEN : UI_STR_TED_OFF));
        } else if (sel) {
            snprintf(out, n, "%d %%", (int)(v->selAlpha * 100.0f + 0.5f));
        } else {
            snprintf(out, n, "-");
        }
        return;
    case ICO_TOUCH_ED_TB_OP_DN:
    case ICO_TOUCH_ED_TB_OP_UP:
        snprintf(out, n, "%s %s", ui_str(UI_STR_TED_OPACITY),
                 k == ICO_TOUCH_ED_TB_OP_DN ? "-" : "+");
        return;
    default:
        snprintf(out, n, "%s", k >= 0 && k < ICO_TOUCH_ED_TB_COUNT ? ui_str(kName[k]) : "");
        return;
    }
}

/* a control's name: the selection's caption, the look pad's word */
static const char *ctlName(int ctl)
{
    static const UiStrId kStr[ICO_TOUCH_CTL_COUNT] = {
        UI_STR_BTN_DPAD,     UI_STR_BTN_CROSS,  UI_STR_BTN_CIRCLE, UI_STR_BTN_SQUARE,
        UI_STR_BTN_TRIANGLE, UI_STR_NONE,       UI_STR_NONE,       UI_STR_NONE,
        UI_STR_NONE,         UI_STR_BTN_SELECT, UI_STR_BTN_START,  UI_STR_TED_STICK,
        UI_STR_TED_LOOK};
    static const char *const kShoulder[4] = {"R1", "L1", "L2", "R2"};

    if (ctl >= ICO_TOUCH_CTL_R1 && ctl <= ICO_TOUCH_CTL_R2) {
        return kShoulder[ctl - ICO_TOUCH_CTL_R1];
    }
    return ctl >= 0 && ctl < ICO_TOUCH_CTL_COUNT ? ui_str(kStr[ctl]) : "";
}

/* The marks over the controls: the overlapping controls' outlines in
   amber, the selection's in white, the toolbar strip along the top (its
   keys framed; the two readouts unframed on a fainter backing; Undo dims
   with nothing to undo, the opacity keys and the readouts with nothing to
   act on; while Reset asks its three choices over Done .. All, the cursor
   key lit, and the rest of the strip dimmed) and the hint's backing at the
   bottom as shapes, then their words, the selection's name and the look
   pad's. */
static void drawEditor(const RdOverlayCtx *ctx, const IcoTouchOverlay *o, float th)
{
    static const UiStrId kConfirm[ICO_TOUCH_ED_CF_COUNT] = {
        UI_STR_TED_THIS_SCREEN, UI_STR_TED_ALL_SCREENS, UI_STR_TED_CANCEL};
    const IcoTouchEditView *v = &o->edit;
    const int conf = confirming(v);
    float stripBottom;
    const float kh = keyHeight(v, &stripBottom);
    const float lineH = kh > 0.0f ? kh * 0.36f : o->layout.unit * 2.0f;
    const int fontOk = ctx->box.w != 0 && ctx->box.h != 0 && ui_font_init();
    const TextGrid g = textGrid(ctx);
    const int sel =
        v->selected >= 0 && v->selected < ICO_TOUCH_CTL_COUNT && rectShown(&v->ctl[v->selected]);
    const char *hint = hintText(v);
    const IcoTouchRect *hr = &v->hint;
    float hintW = 0.0f, hintSize = 0.0f;

    s_d.alpha = 1.0f;
    for (int c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
        const IcoTouchRect *b = &v->ctl[c];
        if (((v->overlap >> c) & 1u) && rectShown(b)) {
            frame(b->x - th, b->y - th, b->x + b->w + th, b->y + b->h + th, th, kWarn);
        }
    }
    if (sel) {
        const IcoTouchRect *b = &v->ctl[v->selected];
        frame(b->x - 2.0f * th, b->y - 2.0f * th, b->x + b->w + 2.0f * th, b->y + b->h + 2.0f * th,
              th, kEdgeHeld);
    }
    /* the strip: cells side by side, each drawn a frame's width in */
    for (int k = 0; k < ICO_TOUCH_ED_TB_COUNT; k++) {
        const IcoTouchRect *r = &v->tb[k];
        if (!rectShown(r) || (conf && k <= ICO_TOUCH_ED_TB_ALL)) {
            continue;
        }
        const float in = th * 0.5f;
        s_d.alpha = !conf && keyLive(v, k) ? 1.0f : KEY_DIM;
        if (readout(k)) {
            s_d.alpha *= 0.6f;
            rect(r->x + in, r->y + in, r->x + r->w - in, r->y + r->h - in, kKey);
            continue;
        }
        rect(r->x + in, r->y + in, r->x + r->w - in, r->y + r->h - in,
             !conf && keyLit(v, k) ? kKeyLit : kKey);
        frame(r->x + in, r->y + in, r->x + r->w - in, r->y + r->h - in, th * 0.6f, kEdge);
    }
    if (conf) {
        s_d.alpha = 1.0f;
        for (int k = 0; k < ICO_TOUCH_ED_CF_COUNT; k++) {
            const IcoTouchRect *r = &v->confirm[k];
            const float in = th * 0.5f;
            if (!rectShown(r)) {
                continue;
            }
            const int lit = confirmLit(v, k);
            rect(r->x + in, r->y + in, r->x + r->w - in, r->y + r->h - in, lit ? kKeyLit : kKey);
            frame(r->x + in, r->y + in, r->x + r->w - in, r->y + r->h - in, th * 0.6f,
                  lit ? kEdgeHeld : kEdge);
        }
    }
    s_d.alpha = 1.0f;
    if (fontOk && hint != NULL && rectShown(hr) && s_d.kx > 0.0f) {
        /* the hint in its box at the bottom, shrunk to its width, on a
           backing as wide as its words */
        hintSize = fitSize(&g, hr->h * 0.6f * s_d.ky, hr->w * s_d.kx, hint);
        hintW = ui_measure_text(hintSize, hint) * g.W / 640.0f / s_d.kx;
        if (hintW > hr->w) {
            hintW = hr->w;
        }
        const float cx = hr->x + hr->w * 0.5f;
        rect(cx - hintW * 0.5f - th, hr->y, cx + hintW * 0.5f + th, hr->y + hr->h, kKey);
    }
    flush();
    if (!fontOk) {
        return;
    }
    ui_begin_overlay(ctx);
    for (int k = 0; k < ICO_TOUCH_ED_TB_COUNT; k++) {
        const IcoTouchRect *r = &v->tb[k];
        char words[96];

        if (!rectShown(r) || (conf && k <= ICO_TOUCH_ED_TB_ALL)) {
            continue;
        }
        keyWords(v, k, words, sizeof(words));
        textAt(&g, r->x + r->w * 0.5f, r->y + r->h * 0.5f, r->h * 0.4f, r->w * 0.88f,
               !conf && keyLive(v, k) ? 1.0f : KEY_DIM, words);
    }
    for (int k = 0; conf && k < ICO_TOUCH_ED_CF_COUNT; k++) {
        const IcoTouchRect *r = &v->confirm[k];
        if (rectShown(r)) {
            textAt(&g, r->x + r->w * 0.5f, r->y + r->h * 0.5f, r->h * 0.4f, r->w * 0.88f, 1.0f,
                   ui_str(kConfirm[k]));
        }
    }
    if (hintW > 0.0f) {
        textAt(&g, hr->x + hr->w * 0.5f, hr->y + hr->h * 0.5f, hr->h * 0.6f, hr->w, 1.0f, hint);
    }
    if (sel) {
        /* the selection's name over it, or under it when the strip (or the
           region's top) leaves no room over it */
        const IcoTouchRect *b = &v->ctl[v->selected];
        const float top = stripBottom > o->layout.region.y ? stripBottom : o->layout.region.y;
        float y = b->y - 2.0f * th - lineH;
        if (y - lineH * 0.6f < top) {
            y = b->y + b->h + 2.0f * th + lineH;
        }
        textAt(&g, b->x + b->w * 0.5f, y, lineH, (float)o->layout.outW, 1.0f, ctlName(v->selected));
    }
    {
        /* the look pad's word, or Hidden or Off, in its area: nothing else
           shows it */
        const IcoTouchRect *a = &o->layout.lookArea;
        if (rectShown(a)) {
            const float la = shownAlpha(o, o->layout.lookAlpha);
            const int on = o->layout.lookActive;
            textAt(&g, a->x + a->w * 0.5f, a->y + a->h * 0.5f, lineH, a->w * 0.9f,
                   la >= 0.0f ? la
                   : on       ? 1.0f
                              : OFF_DIM,
                   la >= 0.0f ? ctlName(ICO_TOUCH_CTL_LOOK)
                              : ui_str(on ? UI_STR_TED_HIDDEN : UI_STR_TED_OFF));
        }
    }
    {
        /* the stick's word when it is Hidden or Off, in its dashed ring */
        const IcoTouchLayout *l = &o->layout;
        if (l->stickR > 0.0f && !(l->stickAlpha > 0.0f)) {
            textAt(&g, l->stickHomeX, l->stickHomeY, lineH, 1.6f * l->stickR,
                   l->stickActive ? 1.0f : OFF_DIM,
                   ui_str(l->stickActive ? UI_STR_TED_HIDDEN : UI_STR_TED_OFF));
        }
    }
    ui_end_overlay();
}

void ui_touch_draw_overlay(const struct RdOverlayCtx *ctx)
{
    IcoTouchOverlay o;

    if (!ctx || !s_source || !s_source(&o) || !(o.opacity > 0.0f || o.edit.active) ||
        o.layout.outW == 0 || o.layout.outH == 0 || ctx->outW == 0 || ctx->outH == 0) {
        return;
    }
    tables();
    s_d.n = 0;
    s_d.opacity = o.opacity > 1.0f ? 1.0f : o.opacity;
    s_d.alpha = 1.0f;
    if (o.edit.active) {
        /* the editor shows every control at full strength (the device
           layer's snapshot says 1 then: a gamepad in Auto would fade it) */
        s_d.opacity = 1.0f;
    }
    /* the zones were built for the window's pixels; the output is the
       same size but for a swapchain still catching up with a resize */
    s_d.kx = (float)ctx->outW / (float)o.layout.outW;
    s_d.ky = (float)ctx->outH / (float)o.layout.outH;
    s_d.maxX = (float)(ctx->outW < 4095u ? ctx->outW : 4095u) * 16.0f;
    s_d.maxY = (float)(ctx->outH < 4095u ? ctx->outH : 4095u) * 16.0f;
    float th = o.layout.unit * 0.4f;
    th = th < 2.0f ? 2.0f : th;
    if (o.edit.active) {
        drawBand(&o.layout, th);
        drawGrid(&o, th);
    }
    drawSticks(&o, th);
    drawButtons(&o, th);
    drawDpad(&o, th);
    flush();
    const uint32_t withGlyph = drawGlyphs(&o);
    drawLabels(ctx, &o, withGlyph);
    if (o.edit.active) {
        drawEditor(ctx, &o, th);
    }
}

int ui_touch_top_gap(const struct RdOverlayCtx *ctx, float margin, float bandH, float natW,
                     float minW, float *x0, float *x1, float *y0)
{
    IcoTouchOverlay o;

    if (!ctx || !s_source || !s_source(&o) || o.layout.outW == 0 || o.layout.outH == 0 ||
        ctx->outW == 0 || ctx->outH == 0 || !(o.layout.unit > 0.0f)) {
        return 0;
    }
    const float kx = (float)ctx->outW / (float)o.layout.outW;
    const float ky = (float)ctx->outH / (float)o.layout.outH;
    float lx0, lx1, ly0;

    /* the player's actual layout (edits applied): touch_gap.c */
    if (!ico_touch_fps_place(&o.layout, margin / kx, margin / ky, bandH / ky, natW / kx, minW / kx,
                             &lx0, &lx1, &ly0)) {
        return 0;
    }
    *x0 = lx0 * kx;
    *x1 = lx1 * kx;
    *y0 = ly0 * ky;
    return 1;
}

#else

void ui_touch_draw_overlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

int ui_touch_top_gap(const struct RdOverlayCtx *ctx, float margin, float bandH, float natW,
                     float minW, float *x0, float *x1, float *y0)
{
    (void)ctx;
    (void)margin;
    (void)bandH;
    (void)natW;
    (void)minW;
    (void)x0;
    (void)x1;
    (void)y0;
    return 0;
}

#endif
