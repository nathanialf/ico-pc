/*
 * port/input/touch.c
 *
 * The touch overlay's input mapper (touch.h): the zones from the output's
 * size, density and fold, fingers to roles (a button, the left stick, the
 * look pad), the virtual pad they hold, and the overlay's visibility and
 * fade. No SDL and no libm (a Newton square root, as vpad.c).
 */
#include "touch.h"

#include <string.h>

enum { ROLE_PENDING = 0, ROLE_BUTTON, ROLE_STICK, ROLE_LOOK, ROLE_IGNORED };

/*
 * The layout, in q (output pixels per layout millimetre; touch.h). Every
 * control is a fixed multiple of q from an edge of the region R (L, T, Rt,
 * B its left, top, right and bottom; W, H its size in q; c its centre x),
 * with a margin of 3 from the edges and a gap of 2 between controls:
 *
 *   L2        (L+3, T+3)  12 x 8      R2   (Rt-15, T+3) 12 x 8
 *   L1        (L+3, T+13) 12 x 8      Select (c-13, T+3), Start (c+1, T+3),
 *   D-pad     centre (L+29, T+15),         12 x 8; SPLIT: Select ends 3
 *             keys 8 x 8: Up (L+25, T+3),  before the band, Start starts 3
 *             Left (L+17, T+11), Right     after it
 *             (L+33, T+11), Down (L+25, T+19)
 *   stick     home (L+24, B-21), R 11     faces centre (Rt-20, B-20), each
 *   R1        a disc, radius 6.5, centred  9.5 from it, radius 5.5
 *             (faceX+9.5, faceY-23.5): straight above Circle, on the right
 *             edge where the thumb's arc goes up (hold Yorda's hand, the
 *             most used button: larger than the faces, no reach across them)
 *
 * The stick's area starts below the D-pad (T+29) at the left edge, down to
 * the bottom: FULL/TABLETOP 0.4 of the width but at least 47 and at most up
 * to Square's left less the gap; SPLIT up to the band. The look pad is the
 * rest of the upper right: from the stick area's right (SPLIT: the band's
 * right) to the right edge, from the top down to the gap above Triangle
 * (faceY - 15 - 2); the buttons on it win the hit test.
 *
 * Why nothing overlaps, whatever the shape (the fit clamp keeps H >= 64 and
 * W >= 114, SPLIT left of the band >= 58 and right of it >= 40):
 *   - the upper-left block (y <= 27, x <= 41) against the stick's ring
 *     (y >= H-32): 27 + 2 <= H - 32 needs H >= 61;
 *   - R2's bottom (11) against R1's top (H-50) with the gap: H >= 63, the
 *     tightest, hence the budget of 64;
 *   - the D-pad's right (41) + 2 <= Select's left (c-13): W >= 112 (114);
 *   - Start's right (c+13) + 2 <= R2's left (W-15): W >= 60;
 *   - adjacent faces 9.5 * sqrt 2 = 13.43 apart >= 5.5 + 5.5 + 2 = 13;
 *   - R1 against Circle 23.5 apart, against Triangle sqrt(9.5^2 + 14^2) =
 *     16.92 apart, both >= 6.5 + 5.5 + 2 = 14;
 *   - every control is at least 3 inside the region (R1 4 from the right,
 *     Circle 5, Cross 5 from the bottom);
 *   - SPLIT, left of the band: 3 + 12 + 2 + 24 + 2 + 12 + 3 = 58 (L1, the
 *     D-pad, Select); right of it: Start and R2 need 3 + 12 + 2 + 12 + 3 =
 *     32, Square's left (Rt-35) 3 from the band needs 38 (40).
 */
#define Q_MARGIN 3.0f
#define Q_GAP 2.0f
#define Q_RECT_W 12.0f /* L1, L2, R2, Select, Start */
#define Q_RECT_H 8.0f
#define Q_DPAD_KEY 8.0f
#define Q_FACE_D 9.5f /* a face's centre from the diamond's */
#define Q_FACE_R 5.5f
#define Q_R1_R 6.5f
#define Q_STICK_MIN_W 47.0f
#define Q_FLOOR_MM 160.0f /* q is at least the short side over this */

static const float s_scale[3] = {0.85f, 1.0f, 1.2f};

static float sqrtf_(float v)
{
    float x, prev;
    int i;

    if (!(v > 0.0f)) {
        return 0.0f;
    }
    x = v > 1.0f ? v : 1.0f;
    for (i = 0; i < 40; i++) { /* Newton from above: monotone, converges */
        prev = x;
        x = 0.5f * (x + v / x);
        if (x >= prev) {
            return prev;
        }
    }
    return x;
}

static IcoTouchRect rect(float x, float y, float w, float h)
{
    IcoTouchRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static void button(IcoTouchLayout *l, int z, IcoTouchRect r, int round, unsigned int pad)
{
    l->button[z].rect = r;
    l->button[z].round = round;
    l->button[z].pad = pad;
}

static void face(IcoTouchLayout *l, int z, float cx, float cy, float r, unsigned int pad)
{
    button(l, z, rect(cx - r, cy - r, 2.0f * r, 2.0f * r), 1, pad);
}

static float minf_(float a, float b)
{
    return a < b ? a : b;
}

static float maxf_(float a, float b)
{
    return a > b ? a : b;
}

/* the rects share a point (touching counts) */
static int touches(IcoTouchRect a, IcoTouchRect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

/* Every control in l->region at the unit q under l->rule (SPLIT reads
   l->band); the multiples are the table above. */
static void place(IcoTouchLayout *l, float q)
{
    const IcoTouchRect R = l->region;
    const float L = R.x, T = R.y, Rt = R.x + R.w, B = R.y + R.h, c = R.x + 0.5f * R.w;
    const float m = Q_MARGIN * q, g = Q_GAP * q, w = Q_RECT_W * q, h = Q_RECT_H * q;
    const float k = Q_DPAD_KEY * q, d = Q_FACE_D * q, r = Q_FACE_R * q;
    const int split = l->rule == ICO_TOUCH_RULE_SPLIT;
    float x0, sw;

    l->unit = q;

    /* the shoulders and R2: the rare ones, in the upper corners */
    button(l, ICO_TOUCH_B_L2, rect(L + m, T + m, w, h), 0, ICO_PAD_L2);
    button(l, ICO_TOUCH_B_L1, rect(L + m, T + 13.0f * q, w, h), 0, ICO_PAD_L1);
    button(l, ICO_TOUCH_B_R2, rect(Rt - 15.0f * q, T + m, w, h), 0, ICO_PAD_R2);

    /* the D-pad beside them, above the stick (menus) */
    l->dpadX = L + 29.0f * q;
    l->dpadY = T + 15.0f * q;
    button(l, ICO_TOUCH_B_UP, rect(l->dpadX - 0.5f * k, l->dpadY - 1.5f * k, k, k), 0, ICO_PAD_UP);
    button(l, ICO_TOUCH_B_DOWN, rect(l->dpadX - 0.5f * k, l->dpadY + 0.5f * k, k, k), 0,
           ICO_PAD_DOWN);
    button(l, ICO_TOUCH_B_LEFT, rect(l->dpadX - 1.5f * k, l->dpadY - 0.5f * k, k, k), 0,
           ICO_PAD_LEFT);
    button(l, ICO_TOUCH_B_RIGHT, rect(l->dpadX + 0.5f * k, l->dpadY - 0.5f * k, k, k), 0,
           ICO_PAD_RIGHT);

    /* Select and Start at the top centre, or each beside the hinge */
    if (split) {
        button(l, ICO_TOUCH_B_SELECT, rect(l->band.x - 15.0f * q, T + m, w, h), 0, ICO_PAD_SELECT);
        button(l, ICO_TOUCH_B_START, rect(l->band.x + l->band.w + m, T + m, w, h), 0,
               ICO_PAD_START);
    } else {
        button(l, ICO_TOUCH_B_SELECT, rect(c - 13.0f * q, T + m, w, h), 0, ICO_PAD_SELECT);
        button(l, ICO_TOUCH_B_START, rect(c + q, T + m, w, h), 0, ICO_PAD_START);
    }

    /* the face diamond in the lower right, R1 above Circle on the edge */
    l->faceX = Rt - 20.0f * q;
    l->faceY = B - 20.0f * q;
    face(l, ICO_TOUCH_B_CROSS, l->faceX, l->faceY + d, r, ICO_PAD_CROSS);
    face(l, ICO_TOUCH_B_CIRCLE, l->faceX + d, l->faceY, r, ICO_PAD_CIRCLE);
    face(l, ICO_TOUCH_B_SQUARE, l->faceX - d, l->faceY, r, ICO_PAD_SQUARE);
    face(l, ICO_TOUCH_B_TRIANGLE, l->faceX, l->faceY - d, r, ICO_PAD_TRIANGLE);
    face(l, ICO_TOUCH_B_R1, l->faceX + d, l->faceY - 23.5f * q, Q_R1_R * q, ICO_PAD_R1);

    /* the left stick: floating, its home in the lower left */
    l->stickR = ICO_TOUCH_STICK_MM * q;
    l->runR = ICO_TOUCH_RUN_RING * l->stickR;
    l->lookR = l->stickR;
    l->stickHomeX = L + 24.0f * q;
    l->stickHomeY = B - 21.0f * q;
    if (split) {
        sw = l->band.x - L;
    } else {
        sw = minf_(maxf_(0.4f * R.w, Q_STICK_MIN_W * q), l->faceX - d - r - g - L);
    }
    l->stickArea = rect(L, T + 29.0f * q, sw, B - (T + 29.0f * q));

    /* the look pad: the rest of the upper right */
    x0 = split ? l->band.x + l->band.w : l->stickArea.x + l->stickArea.w;
    l->lookArea = rect(x0, T, Rt - x0, (l->faceY - d - r - g) - T);
}

/* the largest q (at most q0) that fits the region under the rule */
static float fit(const IcoTouchLayout *l, float q0)
{
    const IcoTouchRect R = l->region;
    float q = minf_(q0, R.h / ICO_TOUCH_BUDGET_H);

    if (l->rule == ICO_TOUCH_RULE_SPLIT) {
        q = minf_(q, (l->band.x - R.x) / ICO_TOUCH_BUDGET_WL);
        q = minf_(q, (R.x + R.w - (l->band.x + l->band.w)) / ICO_TOUCH_BUDGET_WR);
    } else {
        q = minf_(q, R.w / ICO_TOUCH_BUDGET_W);
    }
    return q > 0.0f ? q : 0.0f;
}

/* any button, or the idle stick's square, shares a point with the rect */
static int controls_touch(const IcoTouchLayout *l, IcoTouchRect band)
{
    int z;

    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        if (touches(l->button[z].rect, band)) {
            return 1;
        }
    }
    return touches(rect(l->stickHomeX - l->stickR, l->stickHomeY - l->stickR, 2.0f * l->stickR,
                        2.0f * l->stickR),
                   band);
}

enum { FOLD_IGNORE = 0, FOLD_SPLIT, FOLD_TABLETOP, FOLD_FLAT };

/* The hinge rule: which way the fold lays the controls out, and its band
   (the bounds grown by ICO_TOUCH_FOLD_PAD_MM each side across the hinge,
   kPhys pixels per mm, cut to the safe rect S; along the hinge it spans S).
   Ignored with no fold, bounds missing S, or a side under a quarter of S. */
static int fold_apply(const IcoTouchEnv *env, IcoTouchRect S, float kPhys, IcoTouchRect *band)
{
    const float pad = ICO_TOUCH_FOLD_PAD_MM * kPhys;
    const float Sx1 = S.x + S.w, Sy1 = S.y + S.h;
    IcoTouchRect b;
    float a0, a1;

    if (env == NULL || !(S.w > 0.0f && S.h > 0.0f)) {
        return FOLD_IGNORE;
    }
    b = env->fold.bounds;
    if (!(b.w >= 0.0f)) {
        b.w = 0.0f;
    }
    if (!(b.h >= 0.0f)) {
        b.h = 0.0f;
    }
    if (!(b.x + b.w >= S.x && b.x <= Sx1 && b.y + b.h >= S.y && b.y <= Sy1)) {
        return FOLD_IGNORE; /* outside the safe rect (or not a number) */
    }
    if (env->fold.orientation == ICO_TOUCH_FOLD_VERTICAL) {
        a0 = maxf_(b.x - pad, S.x);
        a1 = minf_(b.x + b.w + pad, Sx1);
        if (a0 - S.x < 0.25f * S.w || Sx1 - a1 < 0.25f * S.w) {
            return FOLD_IGNORE;
        }
        *band = rect(a0, S.y, a1 - a0, S.h);
        return FOLD_SPLIT; /* flat or bent: a cluster each side */
    }
    if (env->fold.orientation == ICO_TOUCH_FOLD_HORIZONTAL) {
        a0 = maxf_(b.y - pad, S.y);
        a1 = minf_(b.y + b.h + pad, Sy1);
        if (a0 - S.y < 0.25f * S.h || Sy1 - a1 < 0.25f * S.h) {
            return FOLD_IGNORE;
        }
        *band = rect(S.x, a0, S.w, a1 - a0);
        return env->fold.halfOpened || env->fold.separating ? FOLD_TABLETOP : FOLD_FLAT;
    }
    return FOLD_IGNORE;
}

/* the region below a horizontal band */
static void tabletop(IcoTouchLayout *l, IcoTouchRect band)
{
    const float y = band.y + band.h;

    l->rule = ICO_TOUCH_RULE_TABLETOP;
    l->band = band;
    l->region = rect(l->safeRect.x, y, l->safeRect.w, l->safeRect.y + l->safeRect.h - y);
}

IcoTouchLayout ico_touch_layout_env(uint32_t outW, uint32_t outH, IcoTouchInsets safe, int size,
                                    const IcoTouchEnv *env)
{
    IcoTouchLayout l;
    IcoTouchRect band = {0.0f, 0.0f, 0.0f, 0.0f};
    float W = (float)outW, H = (float)outH;
    float minS, qPhys, q0;
    int fold;

    memset(&l, 0, sizeof(l));
    if (size < ICO_TOUCH_SMALL || size > ICO_TOUCH_LARGE) {
        size = ICO_TOUCH_MEDIUM;
    }
    if (!(safe.left >= 0.0f)) {
        safe.left = 0.0f;
    }
    if (!(safe.top >= 0.0f)) {
        safe.top = 0.0f;
    }
    if (!(safe.right >= 0.0f)) {
        safe.right = 0.0f;
    }
    if (!(safe.bottom >= 0.0f)) {
        safe.bottom = 0.0f;
    }
    if (safe.left + safe.right >= W || safe.top + safe.bottom >= H) {
        memset(&safe, 0, sizeof(safe));
    }
    l.outW = outW;
    l.outH = outH;
    l.safe = safe;
    l.size = size;
    l.safeRect = rect(safe.left, safe.top, W - safe.left - safe.right, H - safe.top - safe.bottom);

    /* the unit: the real millimetre when the density is known, else a
       phone's (an unknown screen's short side taken as 68 mm, close to the
       old look on a desktop); at least the short side over 160, which
       guards a wrong density on a big monitor; then the size's scale */
    minS = minf_(l.safeRect.w, l.safeRect.h);
    qPhys = env != NULL && env->pxPerMm > 0.0f ? env->pxPerMm : minS / ICO_TOUCH_FALLBACK_MM;
    l.pxPerMm = qPhys;
    q0 = maxf_(qPhys, minS / Q_FLOOR_MM) * s_scale[size];

    l.rule = ICO_TOUCH_RULE_FULL;
    l.region = l.safeRect;
    fold = fold_apply(env, l.safeRect, qPhys, &band);
    if (fold == FOLD_SPLIT) {
        l.rule = ICO_TOUCH_RULE_SPLIT;
        l.band = band;
    } else if (fold == FOLD_TABLETOP) {
        tabletop(&l, band);
    }
    place(&l, fit(&l, q0));
    if (fold == FOLD_FLAT && controls_touch(&l, band)) {
        /* a flat crease under a button: below it instead */
        tabletop(&l, band);
        place(&l, fit(&l, q0));
    }
    return l;
}

IcoTouchLayout ico_touch_layout(uint32_t outW, uint32_t outH, IcoTouchInsets safe, int size)
{
    return ico_touch_layout_env(outW, outH, safe, size, NULL);
}

float ico_touch_px_per_mm(float dpi)
{
    return dpi > 0.0f ? dpi / 25.4f : 0.0f;
}

static int in_rect(const IcoTouchRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* the button zone under (x, y) in pixels, or -1 */
static int hit(const IcoTouchLayout *l, float x, float y)
{
    int z;

    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *b = &l->button[z];

        if (b->round) {
            float r = 0.5f * b->rect.w;
            float dx = x - (b->rect.x + r), dy = y - (b->rect.y + r);

            if (dx * dx + dy * dy <= r * r) {
                return z;
            }
        } else if (in_rect(&b->rect, x, y)) {
            return z;
        }
    }
    return -1;
}

/* --- visibility ----------------------------------------------------------- */

static int any_down(const IcoTouchState *t)
{
    int i;

    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && !t->finger[i].up) {
            return 1;
        }
    }
    return 0;
}

/* 0..1 over ICO_TOUCH_FADE_NS from `from` */
static float ramp(uint64_t now, uint64_t from)
{
    if (now <= from) {
        return 0.0f;
    }
    if (now - from >= ICO_TOUCH_FADE_NS) {
        return 1.0f;
    }
    return (float)(now - from) / (float)ICO_TOUCH_FADE_NS;
}

static int touch_visible(const IcoTouchState *t, uint64_t now)
{
    return t->seen && (any_down(t) || now < t->lastNs + ICO_TOUCH_HIDE_NS);
}

/* the fade by touches alone (no gamepad) */
static float touch_opacity(const IcoTouchState *t, uint64_t now)
{
    uint64_t hide;

    if (!t->seen) {
        return 0.0f;
    }
    if (touch_visible(t, now)) {
        return ramp(now, t->shownNs);
    }
    hide = t->lastNs + ICO_TOUCH_HIDE_NS;
    return ramp(hide, t->shownNs) * (1.0f - ramp(now, hide));
}

int ico_touch_visible(const IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    return gamepads <= 0 && touch_visible(t, nowNs);
}

float ico_touch_opacity(const IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    float o = touch_opacity(t, nowNs);
    int known = t->gamepadsNs != 0 && (t->gamepads > 0) == (gamepads > 0);

    if (gamepads > 0) {
        /* fading out since the first pad connected, else gone */
        return known ? o * (1.0f - ramp(nowNs, t->gamepadsNs)) : 0.0f;
    }
    if (known) {
        o *= ramp(nowNs, t->gamepadsNs); /* back in since the last one left */
    }
    return o;
}

void ico_touch_set_gamepads(IcoTouchState *t, int gamepads, uint64_t nowNs)
{
    if ((gamepads > 0) != (t->gamepads > 0)) {
        t->gamepadsNs = nowNs != 0 ? nowNs : 1;
    }
    t->gamepads = gamepads > 0 ? gamepads : 0;
}

/* --- events --------------------------------------------------------------- */

static void release_all(IcoTouchState *t)
{
    memset(t->finger, 0, sizeof(t->finger));
    t->lookX = t->lookY = 0.0f;
    t->lx = t->ly = 0.0f;
    t->pressed = 0;
    t->stickActive = 0;
    t->lookActive = 0;
}

void ico_touch_reset(IcoTouchState *t, uint64_t nowNs)
{
    int gamepads = t->gamepads;
    uint64_t gamepadsNs = t->gamepadsNs;

    memset(t, 0, sizeof(*t));
    t->look_decay = ICO_TOUCH_LOOK_DECAY;
    t->gamepads = gamepads;
    t->gamepadsNs = gamepadsNs;
    t->seen = 1;
    t->lastNs = nowNs;
    t->shownNs = nowNs;
}

static IcoTouchFinger *find(IcoTouchState *t, uint64_t id)
{
    int i;

    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].id == id) {
            return &t->finger[i];
        }
    }
    return NULL;
}

void ico_touch_event(IcoTouchState *t, uint64_t finger, float nx, float ny, int kind,
                     uint64_t nowNs)
{
    IcoTouchFinger *f;
    int i;

    nx = ico_clamp01f(nx);
    ny = ico_clamp01f(ny);
    if (kind == ICO_TOUCH_CANCEL) {
        release_all(t);
        if (t->seen) {
            t->lastNs = nowNs;
        }
        return;
    }
    if (kind == ICO_TOUCH_DOWN) {
        if (!touch_visible(t, nowNs)) {
            /* show again, continuing a fade out that has not finished */
            uint64_t back = (uint64_t)(touch_opacity(t, nowNs) * (float)ICO_TOUCH_FADE_NS);

            t->shownNs = nowNs > back ? nowNs - back : 0;
        }
        t->seen = 1;
        t->lastNs = nowNs;
        f = find(t, finger);
        for (i = 0; f == NULL && i < ICO_TOUCH_FINGERS; i++) {
            if (!t->finger[i].used) {
                f = &t->finger[i];
            }
        }
        if (f == NULL) {
            return; /* more fingers than slots: this one does nothing */
        }
        memset(f, 0, sizeof(*f));
        f->id = finger;
        f->x0 = f->x = nx;
        f->y0 = f->y = ny;
        f->used = 1;
        f->role = ROLE_PENDING;
        f->zone = -1;
        return;
    }
    f = find(t, finger);
    if (f == NULL) {
        return;
    }
    t->lastNs = nowNs;
    f->x = nx;
    f->y = ny;
    if (kind == ICO_TOUCH_UP) {
        if (f->role == ROLE_PENDING) {
            f->up = 1; /* a tap between two steps: held for the next one */
        } else {
            memset(f, 0, sizeof(*f));
        }
    }
}

/* --- the step ------------------------------------------------------------- */

void ico_touch_step(IcoTouchState *t, const IcoTouchLayout *l, IcoVirtualPad *out)
{
    float W = (float)l->outW, H = (float)l->outH;
    float decay = t->look_decay;
    int stick = -1, look = -1;
    int i;

    memset(out, 0, sizeof(*out));
    if (!(decay >= 0.0f)) {
        decay = 0.0f;
    }
    if (decay > 0.99f) {
        decay = 0.99f;
    }
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].role == ROLE_STICK) {
            stick = i;
        } else if (t->finger[i].used && t->finger[i].role == ROLE_LOOK) {
            look = i;
        }
    }

    /* new fingers: by where they went down */
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        IcoTouchFinger *f = &t->finger[i];
        float x0 = f->x0 * W, y0 = f->y0 * H;

        if (!f->used || f->role != ROLE_PENDING) {
            continue;
        }
        if (hit(l, x0, y0) >= 0) {
            f->role = ROLE_BUTTON;
        } else if (!f->up && stick < 0 && in_rect(&l->stickArea, x0, y0)) {
            f->role = ROLE_STICK;
            stick = i;
        } else if (!f->up && look < 0 && in_rect(&l->lookArea, x0, y0)) {
            f->role = ROLE_LOOK;
            look = i;
        } else {
            f->role = ROLE_IGNORED;
        }
    }

    /* buttons: whatever is under each button finger now */
    t->pressed = 0;
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        IcoTouchFinger *f = &t->finger[i];

        if (f->used && f->role == ROLE_BUTTON) {
            f->zone = (signed char)hit(l, f->x * W, f->y * H);
            if (f->zone >= 0) {
                t->pressed |= 1u << f->zone;
            }
        }
    }
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        if (t->pressed & (1u << i)) {
            out->buttons |= l->button[i].pad;
        }
    }
    /* the glyphs are pictures of positions, so the menus see the same word */
    out->menu_buttons = out->buttons;

    /* the left stick */
    t->lx = t->ly = 0.0f;
    t->stickActive = stick >= 0;
    if (stick >= 0) {
        const IcoTouchFinger *f = &t->finger[stick];
        float dx = (f->x - f->x0) * W, dy = (f->y - f->y0) * H;
        float d = sqrtf_(dx * dx + dy * dy);
        float R = l->stickR > 0.0f ? l->stickR : 1.0f;

        t->stickCX = f->x0 * W;
        t->stickCY = f->y0 * H;
        if (d > R) {
            dx *= R / d;
            dy *= R / d;
        }
        t->stickFX = t->stickCX + dx;
        t->stickFY = t->stickCY + dy;
        if (d >= ICO_TOUCH_STICK_DEADZONE * R) {
            t->lx = dx / R;
            t->ly = dy / R;
        }
    }
    out->lx = t->lx;
    out->ly = t->ly;

    /* the look pad: the offset while held, decaying after */
    t->lookActive = look >= 0;
    if (look >= 0) {
        const IcoTouchFinger *f = &t->finger[look];
        float dx = (f->x - f->x0) * W, dy = (f->y - f->y0) * H;
        float d = sqrtf_(dx * dx + dy * dy);
        float R = l->lookR > 0.0f ? l->lookR : 1.0f;

        t->lookCX = f->x0 * W;
        t->lookCY = f->y0 * H;
        t->lookFX = f->x * W;
        t->lookFY = f->y * H;
        if (d > R) {
            dx *= R / d;
            dy *= R / d;
        }
        t->lookX = dx / R;
        t->lookY = dy / R;
    } else {
        t->lookX *= decay;
        t->lookY *= decay;
        if (t->lookX * t->lookX + t->lookY * t->lookY < 1e-6f) {
            t->lookX = t->lookY = 0.0f;
        }
    }
    out->rx = t->lookX;
    out->ry = t->lookY;

    /* taps that already lifted have had their step */
    for (i = 0; i < ICO_TOUCH_FINGERS; i++) {
        if (t->finger[i].used && t->finger[i].up) {
            memset(&t->finger[i], 0, sizeof(t->finger[i]));
        }
    }
}

IcoTouchDrawInfo ico_touch_draw_info(const IcoTouchState *t, const IcoTouchLayout *l)
{
    IcoTouchDrawInfo d;

    memset(&d, 0, sizeof(d));
    d.pressed = t->pressed;
    d.stickActive = t->stickActive;
    d.stickR = l->stickR;
    d.runR = l->runR;
    if (t->stickActive) {
        d.stickCX = t->stickCX;
        d.stickCY = t->stickCY;
        d.knobX = t->stickFX;
        d.knobY = t->stickFY;
    } else {
        d.stickCX = d.knobX = l->stickHomeX;
        d.stickCY = d.knobY = l->stickHomeY;
    }
    d.stickMag = sqrtf_(t->lx * t->lx + t->ly * t->ly);
    d.lookActive = t->lookActive;
    d.lookCX = t->lookCX;
    d.lookCY = t->lookCY;
    d.lookFX = t->lookFX;
    d.lookFY = t->lookFY;
    d.rx = t->lookX;
    d.ry = t->lookY;
    return d;
}

int ico_touch_accepts(int mode, int gamepads)
{
    return mode == ICO_TOUCH_MODE_ALWAYS || (mode == ICO_TOUCH_MODE_AUTO && gamepads <= 0);
}

int ico_touch_update(IcoTouchState *t, const IcoTouchLayout *l, int mode, int gamepads,
                     IcoVirtualPad *v, uint64_t nowNs)
{
    IcoVirtualPad tv;

    ico_touch_step(t, l, &tv);
    if (!ico_touch_accepts(mode, gamepads)) {
        return 0;
    }
    ico_vpad_merge(v, &tv);
    return 1;
}

float ico_touch_mode_opacity(const IcoTouchState *t, int mode, int gamepads, uint64_t nowNs)
{
    if (mode == ICO_TOUCH_MODE_ALWAYS) {
        return 1.0f;
    }
    if (mode != ICO_TOUCH_MODE_AUTO) {
        return 0.0f;
    }
    return ico_touch_opacity(t, gamepads, nowNs);
}
