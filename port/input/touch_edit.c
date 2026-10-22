/*
 * port/input/touch_edit.c
 *
 * The touch overlay's layout edits and the layout editor (touch_edit.h):
 * the config text of one control's edit and back, the rails, and the
 * editor's state machine (fingers queued by the device layer and drained
 * once a Main tick, then the gamepad word). Plain C: no SDL, no libm, no
 * locale (the numbers are read and written by hand), nothing printed.
 *
 * The toolbar's spot. Ten keys, the only place clear of every default
 * control on the smallest region fit() allows (touch.c: W >= 114 and
 * H >= 64 in q; SPLIT >= 58 left of the band and >= 40 right of it), the
 * offsets from place()'s table (L, T, Rt, B the region's edges, c its
 * centre x, all in q; the bounding boxes, touching counts as a clash):
 *
 *   FULL and TABLETOP: a 2 x 5 block of 6 x 7 keys, gap 1 (34 x 15), at
 *   x Rt-53..Rt-19, y T+13..T+28 (TABLETOP's T is the band's bottom):
 *     - Start c+1..c+13 and Select c-13..c-1 end at y T+11: 2 above;
 *     - R2 Rt-15..Rt-3 (y T+3..T+11): 4 right and 2 above;
 *     - R1 (Rt-10.5, B-43.5) radius 6.5, its square x Rt-17..Rt-4: 2
 *       right at any height (at H = 64 its y T+14..T+27 is beside the
 *       block);
 *     - Triangle (Rt-20, B-29.5) radius 5.5, its square from y B-35 =
 *       T+29 at H = 64: 1 below (more when H > 64); Circle, Cross and
 *       Square are lower still;
 *     - the D-pad's cross x L+17..L+41: Rt-53 = L+W-53 >= L+61, 20 right
 *       (it needs W > 94); L1/L2 end at L+15;
 *     - the stick's square at home (L+24, B-21), R 11: x L+13..L+35;
 *     - the look pad lies under the block by design (an area, not a
 *       control: the toolbar wins the hit test, as a button would).
 *   SPLIT: 2 columns x 5 rows of 5.5 x 7 keys, gap 1 (12 x 39), at
 *   x L+43..L+55, y T+13..T+52, left of the band:
 *     - the D-pad's cross ends at x L+41: 2 left;
 *     - L1/L2 end at x L+15; the stick's square at x L+35;
 *     - Select band.x-15..band.x-3 >= L+43 but at y T+3..T+11: 2 above;
 *       the block's right L+55 <= band.x-3 (the left side is >= 58);
 *     - its bottom T+52 <= B (H >= 64); nothing else is left of the band
 *       (Start, R2, R1, the faces and the look pad are right of it).
 *   The confirm strip of Reset takes the block's first row (FULL,
 *   TABLETOP: three keys of 32/3 x 7 with gaps of 1) or its first three
 *   rows (SPLIT: one 12 x 7 key each), so it is as clear.
 * test_toolbar_clear (test/touch_edit_test.c) asserts all of it over the
 * shape table and the smallest regions.
 *
 * The editor's rules: a finger down on the toolbar (or the strip) acts on
 * the key under it at once and does nothing more; on a control it selects
 * it (on nothing: deselects); moved ICO_TOUCH_EDIT_DRAG_MM q it drags the
 * control (clamped live as the layout clamps it), released it drops: the
 * anchors become the nearer edge on each axis, snapped to the grid's
 * multiples when Snap is on, and a drop whose shape covers another shown
 * control's centre springs back (refused). A second finger while the first
 * holds a control pinches: scale x (distance now / distance at the second
 * finger's down), clamped, snapped to 0.05 on the release. Size steps are
 * 0.1, opacity steps 10 %, on the selected control or with All on every
 * one. Every change pushes the work before it on the undo stack (8 deep,
 * the oldest dropped); Undo pops. The look pad is outside the overlap and
 * refuse rules (it lies under buttons by design). A cancel (focus lost)
 * or a posture change drops a drag or pinch (the control back where the
 * gesture found it) and keeps every other edit.
 */
#include "touch_edit.h"

#include <string.h>

const float ico_touch_edit_grids[3] = {2.0f, 4.0f, 8.0f};

static const char *const s_ctlNames[ICO_TOUCH_CTL_COUNT] = {
    "dpad", "cross", "circle", "square", "triangle", "r1",  "l1",
    "l2",   "r2",    "select", "start",  "stick",    "look"};
static const char *const s_postureNames[ICO_TOUCH_POSTURES] = {"full", "split", "tabletop"};

void ico_touch_edits_defaults(IcoTouchEdits *e)
{
    memset(e, 0, sizeof(*e));
    e->snap = 1;
    e->grid = ICO_TOUCH_EDIT_GRID_DEFAULT;
}

int ico_touch_ctl_hideable(int ctl)
{
    return ctl == ICO_TOUCH_CTL_SELECT || ctl == ICO_TOUCH_CTL_L1 || ctl == ICO_TOUCH_CTL_L2 ||
           ctl == ICO_TOUCH_CTL_R2 || ctl == ICO_TOUCH_CTL_LOOK;
}

const char *ico_touch_ctl_name(int ctl)
{
    return ctl >= 0 && ctl < ICO_TOUCH_CTL_COUNT ? s_ctlNames[ctl] : NULL;
}

int ico_touch_ctl_from_name(const char *name)
{
    int i;

    for (i = 0; name != NULL && i < ICO_TOUCH_CTL_COUNT; i++) {
        if (strcmp(name, s_ctlNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *ico_touch_posture_name(int rule)
{
    return rule >= 0 && rule < ICO_TOUCH_POSTURES ? s_postureNames[rule] : NULL;
}

int ico_touch_posture_from_name(const char *name)
{
    int i;

    for (i = 0; name != NULL && i < ICO_TOUCH_POSTURES; i++) {
        if (strcmp(name, s_postureNames[i]) == 0) {
            return i;
        }
    }
    return -1;
}

unsigned int ico_touch_ctl_zones(int ctl)
{
    switch (ctl) {
    case ICO_TOUCH_CTL_DPAD:
        return (1u << ICO_TOUCH_B_UP) | (1u << ICO_TOUCH_B_DOWN) | (1u << ICO_TOUCH_B_LEFT) |
               (1u << ICO_TOUCH_B_RIGHT);
    case ICO_TOUCH_CTL_CROSS:
        return 1u << ICO_TOUCH_B_CROSS;
    case ICO_TOUCH_CTL_CIRCLE:
        return 1u << ICO_TOUCH_B_CIRCLE;
    case ICO_TOUCH_CTL_SQUARE:
        return 1u << ICO_TOUCH_B_SQUARE;
    case ICO_TOUCH_CTL_TRIANGLE:
        return 1u << ICO_TOUCH_B_TRIANGLE;
    case ICO_TOUCH_CTL_R1:
        return 1u << ICO_TOUCH_B_R1;
    case ICO_TOUCH_CTL_L1:
        return 1u << ICO_TOUCH_B_L1;
    case ICO_TOUCH_CTL_L2:
        return 1u << ICO_TOUCH_B_L2;
    case ICO_TOUCH_CTL_R2:
        return 1u << ICO_TOUCH_B_R2;
    case ICO_TOUCH_CTL_SELECT:
        return 1u << ICO_TOUCH_B_SELECT;
    case ICO_TOUCH_CTL_START:
        return 1u << ICO_TOUCH_B_START;
    default:
        return 0;
    }
}

/* --- numbers -------------------------------------------------------------- */

static long round_l(float v)
{
    return v >= 0.0f ? (long)(v + 0.5f) : -(long)(-v + 0.5f);
}

/* lo for a NaN */
static float clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) {
        return lo;
    }
    return v > hi ? hi : v;
}

/* v to the nearest multiple of g (g > 0; v >= 0) */
static float snap_to(float v, float g)
{
    return g > 0.0f ? (float)round_l(v / g) * g : v;
}

/* --- the text ------------------------------------------------------------- */

typedef struct Out {
    char *buf;
    unsigned size, n;
} Out;

static void put_c(Out *o, char c)
{
    if (o->n + 1 < o->size) {
        o->buf[o->n++] = c;
        o->buf[o->n] = '\0';
    }
}

static void put_s(Out *o, const char *s)
{
    while (*s != '\0') {
        put_c(o, *s++);
    }
}

static void put_u(Out *o, unsigned long v)
{
    char d[24];
    int n = 0;

    do {
        d[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0u && n < (int)sizeof(d));
    while (n > 0) {
        put_c(o, d[--n]);
    }
}

/* a token: its letter and v >= 0 to hundredths, the fraction always two
   digits (fixed) or without its trailing zeros */
static void put_num(Out *o, char key, float v, int fixed)
{
    long h = round_l(v * 100.0f);
    long f;

    if (h < 0) {
        h = 0;
    }
    f = h % 100;
    if (o->n > 0) {
        put_c(o, ' ');
    }
    put_c(o, key);
    put_u(o, (unsigned long)(h / 100));
    if (fixed || f != 0) {
        put_c(o, '.');
        put_c(o, (char)('0' + (int)(f / 10)));
        if (fixed || f % 10 != 0) {
            put_c(o, (char)('0' + (int)(f % 10)));
        }
    }
}

const char *ico_touch_edit_text(const IcoTouchCtlEdit *c, char *buf, unsigned size)
{
    Out o;
    long pct;
    int sized;

    if (buf == NULL || size == 0) {
        return buf;
    }
    o.buf = buf;
    o.size = size;
    o.n = 0;
    buf[0] = '\0';
    if (c == NULL || !c->set) {
        put_s(&o, "default");
        return buf;
    }
    if (c->ax > ICO_TOUCH_ANCHOR_R || c->ay > ICO_TOUCH_ANCHOR_B) {
        /* no position to write: hidden at the default place, else nothing */
        put_s(&o, !(c->alpha > 0.0f) ? "hidden" : "default");
        return buf;
    }
    /* a hidden control with a position keeps it ("l4 t6 o0"): shown again,
       it is where the player put it */
    put_num(&o, c->ax == ICO_TOUCH_ANCHOR_L ? 'l' : 'r', c->dx, 0);
    put_num(&o, c->ay == ICO_TOUCH_ANCHOR_T ? 't' : 'b', c->dy, 0);
    sized = c->w > 0.0f || c->h > 0.0f;
    if (c->w > 0.0f) {
        put_num(&o, 'w', c->w, 0);
    }
    if (c->h > 0.0f) {
        put_num(&o, 'h', c->h, 0);
    }
    if (!sized || round_l(c->scale * 100.0f) != 100) {
        put_num(&o, 's', c->scale, 1);
    }
    pct = round_l(c->alpha * 100.0f);
    if (pct > 100) {
        pct = 100;
    }
    if (o.n > 0) {
        put_c(&o, ' ');
    }
    put_c(&o, 'o');
    put_u(&o, (unsigned long)pct);
    return buf;
}

/* a plain decimal: digits, an optional point and digits; the whole string */
static int parse_num(const char *s, float *out)
{
    float v = 0.0f, scale = 1.0f;
    int digits = 0, ints = 0, point = 0;

    for (; *s != '\0'; s++) {
        if (*s >= '0' && *s <= '9') {
            if (point) {
                if (scale > 1e-6f) {
                    scale *= 0.1f;
                    v += (float)(*s - '0') * scale;
                }
            } else {
                if (++ints > 6) {
                    return -1; /* far out of every range */
                }
                v = v * 10.0f + (float)(*s - '0');
            }
            digits++;
        } else if (*s == '.' && !point) {
            point = 1;
        } else {
            return -1;
        }
    }
    if (digits == 0) {
        return -1;
    }
    *out = v;
    return 0;
}

#define RANGE_EPS 0.005f /* a value written to two decimals stays in range */

int ico_touch_edit_parse(IcoTouchCtlEdit *c, int ctl, const char *text)
{
    IcoTouchCtlEdit e;
    char tok[32];
    int haveX = 0, haveY = 0, haveS = 0, haveO = 0, haveW = 0, haveH = 0, ntok = 0, word = 0;
    const int look = ctl == ICO_TOUCH_CTL_LOOK;
    const char *p = text;

    if (c == NULL) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    if (text == NULL || ctl < 0 || ctl >= ICO_TOUCH_CTL_COUNT) {
        return -1;
    }
    memset(&e, 0, sizeof(e));
    e.scale = 1.0f;
    e.alpha = 1.0f;
    for (;;) {
        unsigned n = 0;
        float v = 0.0f;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            if (n + 1 >= sizeof(tok)) {
                return -1;
            }
            tok[n++] = *p++;
        }
        tok[n] = '\0';
        ntok++;
        if (strcmp(tok, "default") == 0) {
            word = 1;
            continue;
        }
        if (strcmp(tok, "hidden") == 0) {
            word = 2;
            continue;
        }
        if (parse_num(tok + 1, &v) != 0) {
            return -1;
        }
        switch (tok[0]) {
        case 'l':
        case 'r':
            if (haveX++ || v > ICO_TOUCH_EDIT_OFFSET_MAX) {
                return -1;
            }
            e.ax = tok[0] == 'l' ? ICO_TOUCH_ANCHOR_L : ICO_TOUCH_ANCHOR_R;
            e.dx = v;
            break;
        case 't':
        case 'b':
            if (haveY++ || v > ICO_TOUCH_EDIT_OFFSET_MAX) {
                return -1;
            }
            e.ay = tok[0] == 't' ? ICO_TOUCH_ANCHOR_T : ICO_TOUCH_ANCHOR_B;
            e.dy = v;
            break;
        case 's':
            if (haveS++ || v < ICO_TOUCH_EDIT_SCALE_MIN - RANGE_EPS ||
                v > ICO_TOUCH_EDIT_SCALE_MAX + RANGE_EPS) {
                return -1;
            }
            e.scale = v;
            break;
        case 'o':
            if (haveO++ || v > 100.0f) {
                return -1;
            }
            e.alpha = v / 100.0f;
            break;
        case 'w':
        case 'h':
            if (!look || (tok[0] == 'w' ? haveW++ : haveH++) ||
                v < ICO_TOUCH_EDIT_LOOK_MIN - RANGE_EPS || v > ICO_TOUCH_EDIT_OFFSET_MAX) {
                return -1;
            }
            if (tok[0] == 'w') {
                e.w = v;
            } else {
                e.h = v;
            }
            break;
        default:
            return -1;
        }
    }
    if (ntok == 0) {
        return -1;
    }
    if (word != 0) {
        if (ntok != 1) {
            return -1; /* "default" and "hidden" stand alone */
        }
        if (word == 2) {
            if (!ico_touch_ctl_hideable(ctl)) {
                return -1;
            }
            c->set = 1;
            c->ax = c->ay = ICO_TOUCH_ANCHOR_DEFAULT;
            c->scale = 1.0f;
            c->alpha = 0.0f;
        }
        return 0;
    }
    if (!haveX || !haveY) {
        return -1;
    }
    e.set = 1;
    ico_touch_edit_sanitize(&e, ctl);
    *c = e;
    return 0;
}

void ico_touch_edit_sanitize(IcoTouchCtlEdit *c, int ctl)
{
    if (c == NULL || !c->set) {
        return;
    }
    c->set = 1;
    if (c->ax > ICO_TOUCH_ANCHOR_R) {
        c->ax = ICO_TOUCH_ANCHOR_DEFAULT;
    }
    if (c->ay > ICO_TOUCH_ANCHOR_B) {
        c->ay = ICO_TOUCH_ANCHOR_DEFAULT;
    }
    c->dx = clampf(c->dx, 0.0f, ICO_TOUCH_EDIT_OFFSET_MAX);
    c->dy = clampf(c->dy, 0.0f, ICO_TOUCH_EDIT_OFFSET_MAX);
    if (!(c->scale >= 0.0f || c->scale < 0.0f)) {
        c->scale = 1.0f; /* not a number */
    }
    c->scale = clampf(c->scale, ICO_TOUCH_EDIT_SCALE_MIN, ICO_TOUCH_EDIT_SCALE_MAX);
    if (!(c->alpha >= 0.0f || c->alpha < 0.0f)) {
        c->alpha = 1.0f;
    }
    c->alpha =
        clampf(c->alpha, ico_touch_ctl_hideable(ctl) ? 0.0f : ICO_TOUCH_EDIT_ALPHA_MIN, 1.0f);
    if (ctl == ICO_TOUCH_CTL_LOOK) {
        c->w =
            c->w > 0.0f ? clampf(c->w, ICO_TOUCH_EDIT_LOOK_MIN, ICO_TOUCH_EDIT_OFFSET_MAX) : 0.0f;
        c->h =
            c->h > 0.0f ? clampf(c->h, ICO_TOUCH_EDIT_LOOK_MIN, ICO_TOUCH_EDIT_OFFSET_MAX) : 0.0f;
    } else {
        c->w = c->h = 0.0f;
    }
}

void ico_touch_edits_reset(IcoTouchEdits *e, int rule)
{
    if (rule < 0) {
        memset(e->ctl, 0, sizeof(e->ctl));
    } else if (rule < ICO_TOUCH_POSTURES) {
        memset(e->ctl[rule], 0, sizeof(e->ctl[rule]));
    }
    e->gen++;
}

int ico_touch_edits_any(const IcoTouchEdits *e, int rule)
{
    int r, c;

    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        if (rule >= 0 && r != rule) {
            continue;
        }
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            if (e->ctl[r][c].set) {
                return 1;
            }
        }
    }
    return 0;
}

/* --- the toolbar ---------------------------------------------------------- */

static IcoTouchRect mkrect(float x, float y, float w, float h)
{
    IcoTouchRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

void ico_touch_edit_toolbar(const IcoTouchLayout *l, IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT],
                            IcoTouchRect confirm[ICO_TOUCH_ED_CF_COUNT])
{
    const IcoTouchRect R = l->region;
    const float q = l->unit;
    int i;

    if (l->rule == ICO_TOUCH_RULE_SPLIT) {
        const float x0 = R.x + 43.0f * q, y0 = R.y + 13.0f * q;

        /* column 0 the actions, column 1 the steppers and All */
        for (i = 0; i < ICO_TOUCH_ED_TB_COUNT; i++) {
            tb[i] = mkrect(x0 + (float)(i / 5) * 6.5f * q, y0 + (float)(i % 5) * 8.0f * q, 5.5f * q,
                           7.0f * q);
        }
        for (i = 0; i < ICO_TOUCH_ED_CF_COUNT; i++) {
            confirm[i] = mkrect(x0, y0 + (float)i * 8.0f * q, 12.0f * q, 7.0f * q);
        }
    } else {
        const float x0 = R.x + R.w - 53.0f * q, y0 = R.y + 13.0f * q, kw = 32.0f / 3.0f * q;

        /* row 0 the actions, row 1 the steppers and All */
        for (i = 0; i < ICO_TOUCH_ED_TB_COUNT; i++) {
            tb[i] = mkrect(x0 + (float)(i % 5) * 7.0f * q, y0 + (float)(i / 5) * 8.0f * q, 6.0f * q,
                           7.0f * q);
        }
        for (i = 0; i < ICO_TOUCH_ED_CF_COUNT; i++) {
            confirm[i] = mkrect(x0 + (float)i * (kw + q), y0, kw, 7.0f * q);
        }
    }
}

/* --- shapes --------------------------------------------------------------- */

static int in_box(IcoTouchRect r, float x, float y)
{
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

/* the smallest rect holding a and b */
static IcoTouchRect span(IcoTouchRect a, IcoTouchRect b)
{
    float x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    float x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
    float y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;

    return mkrect(x0, y0, x1 - x0, y1 - y0);
}

/* the one button zone a control is, -1 for the D-pad, the stick, the look pad */
static int zone_of(int ctl)
{
    unsigned int m = ico_touch_ctl_zones(ctl);
    int z;

    if (ctl == ICO_TOUCH_CTL_DPAD) {
        return -1;
    }
    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        if (m == 1u << z) {
            return z;
        }
    }
    return -1;
}

static int is_round(int ctl)
{
    return ctl >= ICO_TOUCH_CTL_CROSS && ctl <= ICO_TOUCH_CTL_R1;
}

/* each control's bounding box on l: the D-pad's cross, a round button's
   square, the stick's 2 R square at home, the look pad's rect */
static IcoTouchRect ctl_box(const IcoTouchLayout *l, int ctl)
{
    const int z = zone_of(ctl);

    if (ctl == ICO_TOUCH_CTL_DPAD) {
        return span(span(l->button[ICO_TOUCH_B_UP].rect, l->button[ICO_TOUCH_B_DOWN].rect),
                    span(l->button[ICO_TOUCH_B_LEFT].rect, l->button[ICO_TOUCH_B_RIGHT].rect));
    }
    if (ctl == ICO_TOUCH_CTL_STICK) {
        return mkrect(l->stickHomeX - l->stickR, l->stickHomeY - l->stickR, 2.0f * l->stickR,
                      2.0f * l->stickR);
    }
    if (ctl == ICO_TOUCH_CTL_LOOK) {
        return l->lookArea;
    }
    return z >= 0 ? l->button[z].rect : mkrect(0.0f, 0.0f, 0.0f, 0.0f);
}

static int ctl_hidden(const IcoTouchLayout *l, int ctl)
{
    const int z = zone_of(ctl);

    if (ctl == ICO_TOUCH_CTL_DPAD) {
        return !(l->button[ICO_TOUCH_B_UP].alpha > 0.0f);
    }
    if (ctl == ICO_TOUCH_CTL_STICK) {
        return !(l->stickAlpha > 0.0f);
    }
    if (ctl == ICO_TOUCH_CTL_LOOK) {
        return !(l->lookAlpha > 0.0f);
    }
    return z >= 0 && !(l->button[z].alpha > 0.0f);
}

/* a disc (x, y the centre, w the radius) or a rect */
typedef struct Prim {
    int disc;
    float x, y, w, h;
} Prim;

/* a control's shape in its box: the D-pad two bars (its cross, the hole in
   the middle counted in), a round button or the stick a disc, the rest the
   rect */
static int prims(int ctl, IcoTouchRect b, Prim p[2])
{
    memset(p, 0, 2 * sizeof(Prim));
    if (ctl == ICO_TOUCH_CTL_DPAD) {
        const float k = b.w / 3.0f;

        p[0].x = b.x;
        p[0].y = b.y + k;
        p[0].w = b.w;
        p[0].h = k;
        p[1].x = b.x + k;
        p[1].y = b.y;
        p[1].w = k;
        p[1].h = b.h;
        return 2;
    }
    if (is_round(ctl) || ctl == ICO_TOUCH_CTL_STICK) {
        p[0].disc = 1;
        p[0].x = b.x + 0.5f * b.w;
        p[0].y = b.y + 0.5f * b.h;
        p[0].w = 0.5f * b.w;
        return 1;
    }
    p[0].x = b.x;
    p[0].y = b.y;
    p[0].w = b.w;
    p[0].h = b.h;
    return 1;
}

static int prim_has(const Prim *p, float x, float y)
{
    if (p->disc) {
        return (x - p->x) * (x - p->x) + (y - p->y) * (y - p->y) <= p->w * p->w;
    }
    return in_box(mkrect(p->x, p->y, p->w, p->h), x, y);
}

/* the two share a point */
static int prim_touch(const Prim *a, const Prim *b)
{
    if (a->disc && b->disc) {
        const float dx = a->x - b->x, dy = a->y - b->y, r = a->w + b->w;

        return dx * dx + dy * dy <= r * r;
    }
    if (a->disc || b->disc) {
        const Prim *d = a->disc ? a : b, *r = a->disc ? b : a;
        const float px = clampf(d->x, r->x, r->x + r->w), py = clampf(d->y, r->y, r->y + r->h);

        return (px - d->x) * (px - d->x) + (py - d->y) * (py - d->y) <= d->w * d->w;
    }
    return a->x <= b->x + b->w && b->x <= a->x + a->w && a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static int shapes_touch(int ca, IcoTouchRect a, int cb, IcoTouchRect b)
{
    Prim pa[2], pb[2];
    int na = prims(ca, a, pa), nb = prims(cb, b, pb), i, j;

    for (i = 0; i < na; i++) {
        for (j = 0; j < nb; j++) {
            if (prim_touch(&pa[i], &pb[j])) {
                return 1;
            }
        }
    }
    return 0;
}

/* 1 << ctl for each shown control whose shape shares a point with another
   shown control's (the look pad left out) */
static unsigned int overlap_mask(const IcoTouchLayout *l)
{
    IcoTouchRect b[ICO_TOUCH_CTL_COUNT];
    unsigned int m = 0;
    int i, j;

    for (i = 0; i < ICO_TOUCH_CTL_COUNT; i++) {
        b[i] = ctl_box(l, i);
    }
    for (i = 0; i < ICO_TOUCH_CTL_LOOK; i++) {
        if (ctl_hidden(l, i)) {
            continue;
        }
        for (j = i + 1; j < ICO_TOUCH_CTL_LOOK; j++) {
            if (!ctl_hidden(l, j) && shapes_touch(i, b[i], j, b[j])) {
                m |= (1u << i) | (1u << j);
            }
        }
    }
    return m;
}

/* the shape of ctl centred at (cx, cy) in a w x h box covers the centre of
   another shown control of l (the look pad on neither side) */
static int covers(const IcoTouchLayout *l, int ctl, float cx, float cy, float w, float h)
{
    Prim p[2];
    int n, i, o;

    if (ctl == ICO_TOUCH_CTL_LOOK) {
        return 0;
    }
    n = prims(ctl, mkrect(cx - 0.5f * w, cy - 0.5f * h, w, h), p);
    for (o = 0; o < ICO_TOUCH_CTL_LOOK; o++) {
        IcoTouchRect b;

        if (o == ctl || ctl_hidden(l, o)) {
            continue;
        }
        b = ctl_box(l, o);
        for (i = 0; i < n; i++) {
            if (prim_has(&p[i], b.x + 0.5f * b.w, b.y + 0.5f * b.h)) {
                return 1;
            }
        }
    }
    return 0;
}

/* --- placing an edit ------------------------------------------------------ */

static int edits_differ(const IcoTouchEdits *a, const IcoTouchEdits *b)
{
    int r, c;

    if (a->snap != b->snap || a->grid != b->grid) {
        return 1;
    }
    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            const IcoTouchCtlEdit *x = &a->ctl[r][c], *y = &b->ctl[r][c];

            if (x->set != y->set) {
                return 1;
            }
            if (x->set &&
                (x->ax != y->ax || x->ay != y->ay || x->dx != y->dx || x->dy != y->dy ||
                 x->scale != y->scale || x->alpha != y->alpha || x->w != y->w || x->h != y->h)) {
                return 1;
            }
        }
    }
    return 0;
}

/* where the edit puts ctl's centre on l (the layout's clamp; the box size
   in *w, *h) */
static void edit_centre(const IcoTouchLayout *l, int ctl, const IcoTouchCtlEdit *c, float *cx,
                        float *cy, float *w, float *h)
{
    const IcoTouchRect R = l->region, b = ctl_box(l, ctl);
    const float q = l->unit;
    const int look = ctl == ICO_TOUCH_CTL_LOOK;

    ico_touch_edit_box_size(l, ctl, c, w, h);
    *cx = b.x + 0.5f * b.w;
    *cy = b.y + 0.5f * b.h;
    if (c->set && c->ax == ICO_TOUCH_ANCHOR_L) {
        *cx = R.x + c->dx * q + (look ? 0.5f * *w : 0.0f);
    } else if (c->set && c->ax == ICO_TOUCH_ANCHOR_R) {
        *cx = R.x + R.w - c->dx * q - (look ? 0.5f * *w : 0.0f);
    }
    if (c->set && c->ay == ICO_TOUCH_ANCHOR_T) {
        *cy = R.y + c->dy * q + (look ? 0.5f * *h : 0.0f);
    } else if (c->set && c->ay == ICO_TOUCH_ANCHOR_B) {
        *cy = R.y + R.h - c->dy * q - (look ? 0.5f * *h : 0.0f);
    }
    ico_touch_edit_clamp(l, cx, cy, *w, *h);
}

/* ctl of e's posture rule placed with its w x h box centred at (cx, cy):
   clamped as the layout clamps, anchored to the nearer edge on each axis,
   the offsets snapped to the grid with snap; an unset control becomes set
   at scale 1, opacity 1 */
static void set_place(IcoTouchEdits *e, int rule, const IcoTouchLayout *l, int ctl, float cx,
                      float cy, float w, float h, int snap)
{
    IcoTouchCtlEdit *c = &e->ctl[rule][ctl];
    const IcoTouchRect R = l->region;
    const float q = l->unit;
    const int look = ctl == ICO_TOUCH_CTL_LOOK;
    const float ex = look ? 0.5f * w : 0.0f, ey = look ? 0.5f * h : 0.0f;
    float dl, dr, dt, db;

    if (!(q > 0.0f)) {
        return;
    }
    ico_touch_edit_clamp(l, &cx, &cy, w, h);
    if (!c->set) {
        memset(c, 0, sizeof(*c));
        c->set = 1;
        c->scale = 1.0f;
        c->alpha = 1.0f;
    }
    if (look) {
        c->w = w / (q * c->scale);
        c->h = h / (q * c->scale);
    }
    dl = (cx - ex - R.x) / q;
    dr = (R.x + R.w - cx - ex) / q;
    dt = (cy - ey - R.y) / q;
    db = (R.y + R.h - cy - ey) / q;
    c->ax = dl <= dr ? ICO_TOUCH_ANCHOR_L : ICO_TOUCH_ANCHOR_R;
    c->dx = dl <= dr ? dl : dr;
    c->ay = dt <= db ? ICO_TOUCH_ANCHOR_T : ICO_TOUCH_ANCHOR_B;
    c->dy = dt <= db ? dt : db;
    if (snap) {
        c->dx = snap_to(c->dx < 0.0f ? 0.0f : c->dx, e->grid);
        c->dy = snap_to(c->dy < 0.0f ? 0.0f : c->dy, e->grid);
    }
    ico_touch_edit_sanitize(c, ctl);
}

/* an unset control of e set where l shows it (no snap) */
static void ensure_set(IcoTouchEdits *e, int rule, const IcoTouchLayout *l, int ctl)
{
    if (!e->ctl[rule][ctl].set) {
        const IcoTouchRect b = ctl_box(l, ctl);

        set_place(e, rule, l, ctl, b.x + 0.5f * b.w, b.y + 0.5f * b.h, b.w, b.h, 0);
    }
}

/* --- the undo stack and changes ------------------------------------------- */

static void push_undo(IcoTouchEditor *ed)
{
    if (ed->undos >= ICO_TOUCH_EDIT_UNDO) {
        memmove(&ed->undo[0], &ed->undo[1], sizeof(ed->undo[0]) * (ICO_TOUCH_EDIT_UNDO - 1));
        ed->undos = ICO_TOUCH_EDIT_UNDO - 1;
    }
    ed->undo[ed->undos++] = ed->work;
}

static int pop_undo(IcoTouchEditor *ed)
{
    unsigned int g = ed->work.gen;

    if (ed->undos <= 0) {
        return 0;
    }
    ed->work = ed->undo[--ed->undos];
    ed->work.gen = g + 1; /* newer than every layout built so far */
    return 1;
}

/* the work becomes *next when it differs: the old work pushed first */
static void commit(IcoTouchEditor *ed, const IcoTouchEdits *next)
{
    unsigned int g = ed->work.gen;

    if (!edits_differ(next, &ed->work)) {
        return;
    }
    push_undo(ed);
    ed->work = *next;
    ed->work.gen = g + 1;
    ed->lastChange = 1;
}

/* a change inside a gesture that pushed at its start */
static void touched(IcoTouchEditor *ed)
{
    ed->work.gen++;
    ed->lastChange = 1;
}

/* the controls a size or opacity step acts on: every one with All, else
   the selected one */
static int targets(const IcoTouchEditor *ed, int out[ICO_TOUCH_CTL_COUNT])
{
    int i;

    if (ed->all) {
        for (i = 0; i < ICO_TOUCH_CTL_COUNT; i++) {
            out[i] = i;
        }
        return ICO_TOUCH_CTL_COUNT;
    }
    if (ed->selected >= 0 && ed->selected < ICO_TOUCH_CTL_COUNT) {
        out[0] = ed->selected;
        return 1;
    }
    return 0;
}

static void step_size(IcoTouchEditor *ed, const IcoTouchLayout *l, int dir)
{
    IcoTouchEdits next = ed->work;
    int t[ICO_TOUCH_CTL_COUNT], n = targets(ed, t), i;

    for (i = 0; i < n; i++) {
        IcoTouchCtlEdit *c = &next.ctl[ed->rule][t[i]];
        long p;

        ensure_set(&next, ed->rule, l, t[i]);
        p = round_l(c->scale * 100.0f) + 10 * dir;
        p = p < 70 ? 70 : p > 160 ? 160 : p;
        c->scale = (float)p / 100.0f;
        ico_touch_edit_sanitize(c, t[i]);
    }
    commit(ed, &next);
}

static void step_alpha(IcoTouchEditor *ed, const IcoTouchLayout *l, int dir)
{
    IcoTouchEdits next = ed->work;
    int t[ICO_TOUCH_CTL_COUNT], n = targets(ed, t), i;

    for (i = 0; i < n; i++) {
        IcoTouchCtlEdit *c = &next.ctl[ed->rule][t[i]];
        const long lo = ico_touch_ctl_hideable(t[i]) ? 0 : 25;
        long p;

        ensure_set(&next, ed->rule, l, t[i]);
        p = round_l(c->alpha * 100.0f) + 10 * dir;
        p = p < lo ? lo : p > 100 ? 100 : p;
        c->alpha = (float)p / 100.0f;
        ico_touch_edit_sanitize(c, t[i]);
    }
    commit(ed, &next);
}

/* the gamepad's nudge: the selected control one grid step along (sx, sy),
   across a vertical band in one step when the band stops it */
static void nudge(IcoTouchEditor *ed, const IcoTouchLayout *l, int sx, int sy)
{
    IcoTouchEdits next = ed->work;
    const int ctl = ed->selected;
    const IcoTouchRect band = l->band;
    float cx, cy, w, h, nx, ny;

    if (ctl < 0 || ctl >= ICO_TOUCH_CTL_COUNT || !(l->unit > 0.0f)) {
        return;
    }
    ensure_set(&next, ed->rule, l, ctl);
    edit_centre(l, ctl, &next.ctl[ed->rule][ctl], &cx, &cy, &w, &h);
    nx = cx + (float)sx * next.grid * l->unit;
    ny = cy + (float)sy * next.grid * l->unit;
    set_place(&next, ed->rule, l, ctl, nx, ny, w, h, next.snap);
    edit_centre(l, ctl, &next.ctl[ed->rule][ctl], &nx, &ny, &w, &h);
    if (sx != 0 && band.w > 0.0f && band.y <= l->region.y &&
        band.y + band.h >= l->region.y + l->region.h && (sx > 0 ? nx <= cx : nx >= cx)) {
        /* held at the hinge: to its far side */
        nx = sx > 0 ? band.x + band.w + 0.5f * w : band.x - 0.5f * w;
        if ((sx > 0) == (cx < band.x + 0.5f * band.w)) {
            set_place(&next, ed->rule, l, ctl, nx, cy, w, h, next.snap);
            edit_centre(l, ctl, &next.ctl[ed->rule][ctl], &nx, &ny, &w, &h);
        }
    }
    if (covers(l, ctl, nx, ny, w, h)) {
        ed->refused = ICO_TOUCH_EDIT_REFUSED;
        return;
    }
    commit(ed, &next);
}

/* --- the editor ----------------------------------------------------------- */

void ico_touch_edit_begin(IcoTouchEditor *ed, IcoTouchEdits *live, int rule)
{
    memset(ed, 0, sizeof(*ed));
    ed->live = live;
    if (live != NULL) {
        ed->work = *live;
    } else {
        ico_touch_edits_defaults(&ed->work);
    }
    ed->rule = rule >= 0 && rule < ICO_TOUCH_POSTURES ? rule : ICO_TOUCH_RULE_FULL;
    ed->selected = -1;
    ed->grab = -1;
    ed->confirmSel = ICO_TOUCH_ED_CF_CANCEL;
    ed->active = 1;
}

void ico_touch_edit_event(IcoTouchEditor *ed, uint64_t finger, float nx, float ny, int kind,
                          uint64_t nowNs)
{
    IcoTouchEditEvent *ev;

    if (!ed->active) {
        return;
    }
    if (kind == ICO_TOUCH_CANCEL) {
        ed->qn = 0; /* what came before it is moot */
    }
    if (ed->qn >= ICO_TOUCH_ED_QUEUE) {
        /* a full queue keeps its last slot for a lift or a cancel, so no
           finger stays down for ever */
        if (kind == ICO_TOUCH_MOVE || kind == ICO_TOUCH_DOWN) {
            return;
        }
        ed->qn = ICO_TOUCH_ED_QUEUE - 1;
    }
    ev = &ed->queue[ed->qn++];
    ev->id = finger;
    ev->nx = nx;
    ev->ny = ny;
    ev->kind = kind;
    ev->ns = nowNs;
}

/* the gesture forgotten; with revert, a drag or pinch that pushed is undone */
static void cancel_gesture(IcoTouchEditor *ed, int revert)
{
    if (revert && ed->pushed &&
        (ed->state == ICO_TOUCH_ED_DRAG || ed->state == ICO_TOUCH_ED_PINCH)) {
        pop_undo(ed);
    }
    if (ed->state == ICO_TOUCH_ED_DRAG || ed->state == ICO_TOUCH_ED_PINCH) {
        ed->state = ICO_TOUCH_ED_IDLE;
    }
    ed->fingers = 0;
    ed->grab = -1;
    ed->consumed = 0;
    ed->moved = 0;
    ed->pushed = 0;
}

static int finger_slot(const IcoTouchEditor *ed, uint64_t id)
{
    int i;

    for (i = 0; i < ed->fingers && i < 2; i++) {
        if (ed->finger[i] == id) {
            return i;
        }
    }
    return -1;
}

static float fdist(float ax, float ay, float bx, float by)
{
    float d2 = (ax - bx) * (ax - bx) + (ay - by) * (ay - by), x, prev;
    int i;

    if (!(d2 > 0.0f)) {
        return 0.0f;
    }
    x = d2 > 1.0f ? d2 : 1.0f;
    for (i = 0; i < 40; i++) { /* Newton from above, as touch.c's sqrtf_ */
        prev = x;
        x = 0.5f * (x + d2 / x);
        if (x >= prev) {
            return prev;
        }
    }
    return x;
}

/* the control under (x, y): the nearest centre among the boxes holding it,
   the look pad only when no other box does */
static int pick(const IcoTouchEditor *ed, float x, float y)
{
    float best = 0.0f;
    int i, found = -1;

    for (i = 0; i < ICO_TOUCH_CTL_LOOK; i++) {
        const IcoTouchRect b = ed->ctl[i];
        const float dx = x - (b.x + 0.5f * b.w), dy = y - (b.y + 0.5f * b.h);

        if (b.w > 0.0f && in_box(b, x, y) && (found < 0 || dx * dx + dy * dy < best)) {
            found = i;
            best = dx * dx + dy * dy;
        }
    }
    if (found < 0 && in_box(ed->ctl[ICO_TOUCH_CTL_LOOK], x, y)) {
        found = ICO_TOUCH_CTL_LOOK;
    }
    return found;
}

static int key_at(const IcoTouchRect *keys, int n, float x, float y)
{
    int i;

    for (i = 0; i < n; i++) {
        if (in_box(keys[i], x, y)) {
            return i;
        }
    }
    return -1;
}

static int confirm_act(IcoTouchEditor *ed, int k)
{
    IcoTouchEdits next = ed->work;

    if (k == ICO_TOUCH_ED_CF_THIS || k == ICO_TOUCH_ED_CF_ALL) {
        ico_touch_edits_reset(&next, k == ICO_TOUCH_ED_CF_THIS ? ed->rule : -1);
        commit(ed, &next);
    }
    ed->state = ICO_TOUCH_ED_IDLE;
    ed->confirmSel = ICO_TOUCH_ED_CF_CANCEL;
    return ICO_TOUCH_ED_NONE;
}

static int key_act(IcoTouchEditor *ed, const IcoTouchLayout *l, int k)
{
    IcoTouchEdits next = ed->work;
    int i;

    switch (k) {
    case ICO_TOUCH_ED_TB_DONE:
        return ICO_TOUCH_ED_LEAVE;
    case ICO_TOUCH_ED_TB_UNDO:
        pop_undo(ed);
        break;
    case ICO_TOUCH_ED_TB_RESET:
        ed->state = ICO_TOUCH_ED_CONFIRM_RESET;
        ed->confirmSel = ICO_TOUCH_ED_CF_CANCEL;
        break;
    case ICO_TOUCH_ED_TB_SNAP:
        next.snap = !next.snap;
        commit(ed, &next);
        break;
    case ICO_TOUCH_ED_TB_GRID:
        for (i = 0; i < 3 && ico_touch_edit_grids[i] != next.grid; i++) {}
        next.grid = ico_touch_edit_grids[i < 3 ? (i + 1) % 3 : 1];
        commit(ed, &next);
        break;
    case ICO_TOUCH_ED_TB_SIZE_DN:
    case ICO_TOUCH_ED_TB_SIZE_UP:
        step_size(ed, l, k == ICO_TOUCH_ED_TB_SIZE_UP ? 1 : -1);
        break;
    case ICO_TOUCH_ED_TB_OP_DN:
    case ICO_TOUCH_ED_TB_OP_UP:
        step_alpha(ed, l, k == ICO_TOUCH_ED_TB_OP_UP ? 1 : -1);
        break;
    case ICO_TOUCH_ED_TB_ALL:
        ed->all = !ed->all;
        break;
    default:
        break;
    }
    return ICO_TOUCH_ED_NONE;
}

/* the drop of the grabbed control where the work has it: snapped with
   Snap, refused (the gesture's push undone) when it covers another's
   centre */
static void drop(IcoTouchEditor *ed, const IcoTouchLayout *l)
{
    const int ctl = ed->grab;
    float cx, cy, w, h;

    if (ctl < 0) {
        return;
    }
    edit_centre(l, ctl, &ed->work.ctl[ed->rule][ctl], &cx, &cy, &w, &h);
    set_place(&ed->work, ed->rule, l, ctl, cx, cy, w, h, ed->work.snap);
    edit_centre(l, ctl, &ed->work.ctl[ed->rule][ctl], &cx, &cy, &w, &h);
    if (covers(l, ctl, cx, cy, w, h)) {
        if (ed->pushed) {
            pop_undo(ed);
        }
        ed->refused = ICO_TOUCH_EDIT_REFUSED;
    } else {
        touched(ed);
    }
}

/* the pinch's scale from the fingers now */
static void pinch_update(IcoTouchEditor *ed)
{
    IcoTouchCtlEdit *c = &ed->work.ctl[ed->rule][ed->grab];
    const float d = fdist(ed->fx[0], ed->fy[0], ed->fx[1], ed->fy[1]);

    c->scale = clampf(ed->scale0 * d / ed->d0, ICO_TOUCH_EDIT_SCALE_MIN, ICO_TOUCH_EDIT_SCALE_MAX);
    touched(ed);
}

static int handle_event(IcoTouchEditor *ed, const IcoTouchLayout *l, const IcoTouchEditEvent *ev)
{
    const float x = clampf(ev->nx, 0.0f, 1.0f) * (float)l->outW;
    const float y = clampf(ev->ny, 0.0f, 1.0f) * (float)l->outH;
    const int s = finger_slot(ed, ev->id);

    switch (ev->kind) {
    case ICO_TOUCH_DOWN:
        if (ed->fingers == 0) {
            ed->finger[0] = ev->id;
            ed->x0[0] = ed->fx[0] = x;
            ed->y0[0] = ed->fy[0] = y;
            ed->fingers = 1;
            ed->grab = -1;
            ed->consumed = 0;
            ed->moved = 0;
            ed->pushed = 0;
            if (ed->state == ICO_TOUCH_ED_CONFIRM_RESET) {
                const int k = key_at(ed->confirm, ICO_TOUCH_ED_CF_COUNT, x, y);

                ed->consumed = 1;
                return confirm_act(ed, k >= 0 ? k : ICO_TOUCH_ED_CF_CANCEL);
            }
            if (in_box(span(ed->tb[0], ed->tb[ICO_TOUCH_ED_TB_COUNT - 1]), x, y)) {
                /* the toolbar wins over any control under it */
                const int k = key_at(ed->tb, ICO_TOUCH_ED_TB_COUNT, x, y);

                ed->consumed = 1;
                return k >= 0 ? key_act(ed, l, k) : ICO_TOUCH_ED_NONE;
            }
            ed->selected = ed->grab = pick(ed, x, y);
            if (ed->grab >= 0) {
                const IcoTouchRect b = ed->ctl[ed->grab];

                ed->cx0 = b.x + 0.5f * b.w;
                ed->cy0 = b.y + 0.5f * b.h;
            }
        } else if (ed->fingers == 1 && s < 0 && ed->grab >= 0 && !ed->consumed &&
                   (ed->state == ICO_TOUCH_ED_IDLE || ed->state == ICO_TOUCH_ED_DRAG)) {
            /* a second finger: the pinch */
            ed->finger[1] = ev->id;
            ed->x0[1] = ed->fx[1] = x;
            ed->y0[1] = ed->fy[1] = y;
            ed->fingers = 2;
            if (!ed->pushed) {
                push_undo(ed);
                ed->pushed = 1;
            }
            ensure_set(&ed->work, ed->rule, l, ed->grab);
            ed->scale0 = ed->work.ctl[ed->rule][ed->grab].scale;
            ed->d0 = fdist(ed->fx[0], ed->fy[0], x, y);
            if (ed->d0 < 1.0f) {
                ed->d0 = 1.0f;
            }
            ed->state = ICO_TOUCH_ED_PINCH;
        }
        return ICO_TOUCH_ED_NONE;
    case ICO_TOUCH_MOVE:
    case ICO_TOUCH_UP:
        if (s < 0) {
            return ICO_TOUCH_ED_NONE;
        }
        ed->fx[s] = x;
        ed->fy[s] = y;
        if (ed->state == ICO_TOUCH_ED_PINCH) {
            pinch_update(ed);
            if (ev->kind == ICO_TOUCH_UP) {
                IcoTouchCtlEdit *c = &ed->work.ctl[ed->rule][ed->grab];

                /* snapped to 0.05, then the drop rules if it was dragged */
                c->scale = clampf((float)round_l(c->scale * 20.0f) / 20.0f,
                                  ICO_TOUCH_EDIT_SCALE_MIN, ICO_TOUCH_EDIT_SCALE_MAX);
                if (ed->moved) {
                    drop(ed, l);
                } else {
                    touched(ed);
                }
                ed->state = ICO_TOUCH_ED_IDLE;
                /* the other finger does nothing more */
                ed->finger[0] = ed->finger[1 - s];
                ed->x0[0] = ed->x0[1 - s];
                ed->y0[0] = ed->y0[1 - s];
                ed->fx[0] = ed->fx[1 - s];
                ed->fy[0] = ed->fy[1 - s];
                ed->fingers = 1;
                ed->consumed = 1;
                ed->pushed = 0;
            }
            return ICO_TOUCH_ED_NONE;
        }
        if (s != 0) {
            return ICO_TOUCH_ED_NONE;
        }
        if (ed->grab >= 0 && !ed->consumed) {
            if (!ed->moved &&
                fdist(x, y, ed->x0[0], ed->y0[0]) >= ICO_TOUCH_EDIT_DRAG_MM * l->unit) {
                ed->moved = 1;
                ed->state = ICO_TOUCH_ED_DRAG;
                push_undo(ed);
                ed->pushed = 1;
            }
            if (ed->state == ICO_TOUCH_ED_DRAG) {
                float w, h;

                ico_touch_edit_box_size(l, ed->grab, &ed->work.ctl[ed->rule][ed->grab], &w, &h);
                set_place(&ed->work, ed->rule, l, ed->grab, ed->cx0 + x - ed->x0[0],
                          ed->cy0 + y - ed->y0[0], w, h, 0);
                touched(ed);
            }
        }
        if (ev->kind == ICO_TOUCH_UP) {
            if (ed->state == ICO_TOUCH_ED_DRAG) {
                drop(ed, l);
                ed->state = ICO_TOUCH_ED_IDLE;
            }
            ed->fingers = 0;
            ed->grab = -1;
            ed->consumed = 0;
            ed->moved = 0;
            ed->pushed = 0;
        }
        return ICO_TOUCH_ED_NONE;
    case ICO_TOUCH_CANCEL:
        cancel_gesture(ed, 1);
        return ICO_TOUCH_ED_NONE;
    default:
        return ICO_TOUCH_ED_NONE;
    }
}

#define PAD_DIRS (ICO_PAD_UP | ICO_PAD_DOWN | ICO_PAD_LEFT | ICO_PAD_RIGHT)

static int handle_pad(IcoTouchEditor *ed, const IcoTouchLayout *l, unsigned int held)
{
    const unsigned int pressed = held & ~ed->padHeld;
    unsigned int dirs = held & PAD_DIRS;

    if (ed->state == ICO_TOUCH_ED_DRAG || ed->state == ICO_TOUCH_ED_PINCH) {
        ed->padTicks = 0;
        return ICO_TOUCH_ED_NONE; /* the fingers have it */
    }
    if (ed->state == ICO_TOUCH_ED_CONFIRM_RESET) {
        if (pressed & ICO_PAD_LEFT) {
            ed->confirmSel = (ed->confirmSel + ICO_TOUCH_ED_CF_COUNT - 1) % ICO_TOUCH_ED_CF_COUNT;
        }
        if (pressed & ICO_PAD_RIGHT) {
            ed->confirmSel = (ed->confirmSel + 1) % ICO_TOUCH_ED_CF_COUNT;
        }
        if (pressed & ICO_PAD_CROSS) {
            return confirm_act(ed, ed->confirmSel);
        }
        if (pressed & (ICO_PAD_CIRCLE | ICO_PAD_TRIANGLE | ICO_PAD_START)) {
            return confirm_act(ed, ICO_TOUCH_ED_CF_CANCEL);
        }
        return ICO_TOUCH_ED_NONE;
    }
    if (pressed & (ICO_PAD_TRIANGLE | ICO_PAD_CIRCLE)) {
        return ICO_TOUCH_ED_LEAVE;
    }
    if (pressed & ICO_PAD_START) {
        ed->state = ICO_TOUCH_ED_CONFIRM_RESET;
        ed->confirmSel = ICO_TOUCH_ED_CF_CANCEL;
        return ICO_TOUCH_ED_NONE;
    }
    if (pressed & ICO_PAD_SELECT) {
        pop_undo(ed);
    }
    if (pressed & ICO_PAD_R1) {
        ed->selected = ed->selected < 0 ? 0 : (ed->selected + 1) % ICO_TOUCH_CTL_COUNT;
    }
    if (pressed & ICO_PAD_L1) {
        ed->selected = ed->selected <= 0 ? ICO_TOUCH_CTL_COUNT - 1 : ed->selected - 1;
    }
    if (pressed & ICO_PAD_L2) {
        step_size(ed, l, -1);
    }
    if (pressed & ICO_PAD_R2) {
        step_size(ed, l, 1);
    }
    if (pressed & ICO_PAD_SQUARE) {
        step_alpha(ed, l, -1);
    }
    if (pressed & ICO_PAD_CROSS) {
        if (ed->selected < 0) {
            ed->all = !ed->all;
        } else {
            step_alpha(ed, l, 1);
        }
    }
    /* the D-pad nudges: at once, then repeating while held */
    if (dirs == 0) {
        ed->padTicks = 0;
    } else {
        int fire;

        if (pressed & PAD_DIRS) {
            ed->padTicks = 0;
            fire = 1;
        } else {
            ed->padTicks++;
            fire = ed->padTicks >= ICO_TOUCH_EDIT_REPEAT_DELAY &&
                   (ed->padTicks - ICO_TOUCH_EDIT_REPEAT_DELAY) % ICO_TOUCH_EDIT_REPEAT == 0;
        }
        if (fire) {
            nudge(ed, l, (dirs & ICO_PAD_RIGHT ? 1 : 0) - (dirs & ICO_PAD_LEFT ? 1 : 0),
                  (dirs & ICO_PAD_DOWN ? 1 : 0) - (dirs & ICO_PAD_UP ? 1 : 0));
        }
    }
    return ICO_TOUCH_ED_NONE;
}

int ico_touch_edit_tick(IcoTouchEditor *ed, const IcoTouchLayout *l, unsigned int padHeld)
{
    int ret = ICO_TOUCH_ED_NONE, i, wasChange;

    if (!ed->active || l == NULL) {
        ed->qn = 0;
        ed->padHeld = padHeld;
        return ICO_TOUCH_ED_NONE;
    }
    if (ed->refused > 0) {
        ed->refused--;
    }
    wasChange = ed->lastChange;
    ed->lastChange = 0;
    for (i = 0; i < ICO_TOUCH_CTL_COUNT; i++) {
        ed->ctl[i] = ctl_box(l, i);
    }
    ico_touch_edit_toolbar(l, ed->tb, ed->confirm);
    if (l->rule != ed->rule) {
        if (wasChange && ed->rule == ICO_TOUCH_RULE_FULL && l->rule == ICO_TOUCH_RULE_TABLETOP) {
            /* the last change put a control on a flat crease (touch.c lays
               the controls out below it then): undone, refused */
            cancel_gesture(ed, 0);
            pop_undo(ed);
            ed->refused = ICO_TOUCH_EDIT_REFUSED;
            ed->qn = 0;
            ed->padHeld = padHeld;
            return ICO_TOUCH_ED_NONE;
        }
        /* the phone folded: that posture's own arrangement from now */
        cancel_gesture(ed, 1);
        if (ed->state == ICO_TOUCH_ED_CONFIRM_RESET) {
            ed->state = ICO_TOUCH_ED_IDLE;
        }
        ed->rule = l->rule >= 0 && l->rule < ICO_TOUCH_POSTURES ? l->rule : ICO_TOUCH_RULE_FULL;
    }
    for (i = 0; i < ed->qn && ret == ICO_TOUCH_ED_NONE; i++) {
        ret = handle_event(ed, l, &ed->queue[i]);
    }
    ed->qn = 0;
    if (ret == ICO_TOUCH_ED_NONE) {
        ret = handle_pad(ed, l, padHeld);
    }
    ed->padHeld = padHeld;
    ed->overlap = overlap_mask(l);
    ed->changed = ed->live != NULL ? (unsigned int)edits_differ(&ed->work, ed->live) : 1u;
    return ret;
}

void ico_touch_edit_end(IcoTouchEditor *ed, int keep)
{
    if (keep && ed->live != NULL) {
        const unsigned int g = ed->work.gen > ed->live->gen ? ed->work.gen : ed->live->gen;

        *ed->live = ed->work;
        ed->live->gen = g + 1;
    }
    ed->active = 0;
    ed->state = ICO_TOUCH_ED_IDLE;
    ed->fingers = 0;
    ed->qn = 0;
}

void ico_touch_edit_view(const IcoTouchEditor *ed, const IcoTouchLayout *l, IcoTouchEditView *out)
{
    int i;

    memset(out, 0, sizeof(*out));
    out->selected = -1;
    if (!ed->active || l == NULL) {
        return;
    }
    out->active = 1;
    out->rule = ed->rule;
    out->state = ed->state;
    out->selected = ed->selected;
    out->all = ed->all;
    out->snap = ed->work.snap;
    out->grid = ed->work.grid;
    out->overlap = overlap_mask(l);
    for (i = 0; i < ICO_TOUCH_CTL_COUNT; i++) {
        out->ctl[i] = ctl_box(l, i);
        if (ctl_hidden(l, i)) {
            out->hidden |= 1u << i;
        }
    }
    out->refused = ed->refused;
    out->selScale = out->selAlpha = 1.0f;
    if (ed->selected >= 0 && ed->selected < ICO_TOUCH_CTL_COUNT && ed->rule >= 0 &&
        ed->rule < ICO_TOUCH_POSTURES) {
        const IcoTouchCtlEdit *c = &ed->work.ctl[ed->rule][ed->selected];

        if (c->set) {
            out->selScale = c->scale;
            out->selAlpha = c->alpha;
        }
    }
    ico_touch_edit_toolbar(l, out->tb, out->confirm);
    out->confirmSel = ed->confirmSel;
    out->undos = ed->undos;
    out->changed = (int)ed->changed;
}
