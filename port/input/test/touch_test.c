/*
 * port/input/test/touch_test.c
 *
 * The touch overlay's mapper (touch.c) on the CPU: the layouts over a table
 * of screens (phones, a notch inset, foldables open, bent and split, a
 * tablet, monitors) at every size, the hinge rule's edges, the floating left stick (centre,
 * deflection, dead zone, the run ring), the D-pad, face buttons under two
 * fingers, a finger sliding off a button, cancel, the look pad's decay,
 * hiding after 5 s and with a gamepad, the fades, and the merged pad
 * through ico_input_vpad_to_frame with the stick fix off and on.
 */
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "touch.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define S 1000000000ull

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static int near_(float a, float b, float tol)
{
    return absf(a - b) <= tol;
}

static float sqrt_(float v)
{
    float x = v > 1.0f ? v : 1.0f;
    int i;

    if (v <= 0.0f) {
        return 0.0f;
    }
    for (i = 0; i < 40; i++) {
        x = 0.5f * (x + v / x);
    }
    return x;
}

static IcoTouchInsets no_insets(void)
{
    IcoTouchInsets in = {0.0f, 0.0f, 0.0f, 0.0f};

    return in;
}

/* events in output pixels */
static const IcoTouchLayout *g_l;

static void ev(IcoTouchState *t, uint64_t id, float px, float py, int kind, uint64_t now)
{
    ico_touch_event(t, id, px / (float)g_l->outW, py / (float)g_l->outH, kind, now);
}

static IcoVirtualPad step(IcoTouchState *t, uint64_t now)
{
    IcoVirtualPad v;

    ico_touch_step(t, g_l, &v);
    return v;
}

static void centre_of(const IcoTouchLayout *l, int z, float *x, float *y)
{
    *x = l->button[z].rect.x + 0.5f * l->button[z].rect.w;
    *y = l->button[z].rect.y + 0.5f * l->button[z].rect.h;
}

static int in_area(const IcoTouchRect *r, float x, float y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static int rect_inside(const IcoTouchRect *in, const IcoTouchRect *out)
{
    return in->x >= out->x - 0.01f && in->y >= out->y - 0.01f &&
           in->x + in->w <= out->x + out->w + 0.01f && in->y + in->h <= out->y + out->h + 0.01f;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static float right_of(const IcoTouchRect *r)
{
    return r->x + r->w;
}

static float bottom_of(const IcoTouchRect *r)
{
    return r->y + r->h;
}

/* the gap between a disc (centre, radius) and a rect: 0 or less overlaps */
static float disc_rect_gap(float cx, float cy, float r, const IcoTouchRect *b)
{
    float dx = maxf(maxf(b->x - cx, cx - right_of(b)), 0.0f);
    float dy = maxf(maxf(b->y - cy, cy - bottom_of(b)), 0.0f);

    return sqrt_(dx * dx + dy * dy) - r;
}

/* the gap between two buttons, pixels (negative: they overlap) */
static float button_gap(const IcoTouchButton *a, const IcoTouchButton *b)
{
    float ra = 0.5f * a->rect.w, rb = 0.5f * b->rect.w;

    if (a->round && b->round) {
        float dx = (a->rect.x + ra) - (b->rect.x + rb), dy = (a->rect.y + ra) - (b->rect.y + rb);

        return sqrt_(dx * dx + dy * dy) - ra - rb;
    }
    if (a->round) {
        return disc_rect_gap(a->rect.x + ra, a->rect.y + ra, ra, &b->rect);
    }
    if (b->round) {
        return disc_rect_gap(b->rect.x + rb, b->rect.y + rb, rb, &a->rect);
    }
    return maxf(maxf(b->rect.x - right_of(&a->rect), a->rect.x - right_of(&b->rect)),
                maxf(b->rect.y - bottom_of(&a->rect), a->rect.y - bottom_of(&b->rect)));
}

static int is_dpad(int z)
{
    return z == ICO_TOUCH_B_UP || z == ICO_TOUCH_B_DOWN || z == ICO_TOUCH_B_LEFT ||
           z == ICO_TOUCH_B_RIGHT;
}

static float cx_of(const IcoTouchLayout *l, int z)
{
    return l->button[z].rect.x + 0.5f * l->button[z].rect.w;
}

static float cy_of(const IcoTouchLayout *l, int z)
{
    return l->button[z].rect.y + 0.5f * l->button[z].rect.h;
}

/* how far (x, y) is from a button's edge, pixels (negative: on it) */
static float point_gap(const IcoTouchButton *b, float x, float y)
{
    if (b->round) {
        float r = 0.5f * b->rect.w, dx = x - (b->rect.x + r), dy = y - (b->rect.y + r);

        return sqrt_(dx * dx + dy * dy) - r;
    }
    return disc_rect_gap(x, y, 0.0f, &b->rect);
}

/* A point in area at least 1q inside its edges and 1q from every button
   (a grid scan in quarter units); 0 when there is none. */
static int free_point(const IcoTouchLayout *l, IcoTouchRect area, float *x, float *y)
{
    const float q = l->unit, step = 0.25f * l->unit;
    float px, py;
    int z;

    for (py = area.y + q; py <= area.y + area.h - q; py += step) {
        for (px = area.x + q; px <= area.x + area.w - q; px += step) {
            for (z = 0; z < ICO_TOUCH_BUTTONS && point_gap(&l->button[z], px, py) >= q; z++) {}
            if (z == ICO_TOUCH_BUTTONS) {
                *x = px;
                *y = py;
                return 1;
            }
        }
    }
    return 0;
}

/* A free point of the look pad with room for the tests' moves: 2R from
   its left and right and R below its top, so a finger moved by up to 2R
   sideways or R up stays on the output (ico_touch_event clamps to it,
   which would shorten the move). */
static int look_point(const IcoTouchLayout *l, float *x, float *y)
{
    IcoTouchRect a = l->lookArea;

    a.x += 2.0f * l->lookR;
    a.w -= 4.0f * l->lookR;
    a.y += l->lookR;
    a.h -= l->lookR;
    return free_point(l, a, x, y);
}

/* the screen below the look pad between the stick's area and Square: no
   zone at all */
static IcoTouchRect gap_area(const IcoTouchLayout *l)
{
    IcoTouchRect r;

    r.x = right_of(&l->stickArea);
    r.y = bottom_of(&l->lookArea);
    r.w = l->button[ICO_TOUCH_B_SQUARE].rect.x - r.x;
    r.h = bottom_of(&l->region) - r.y;
    return r;
}

/* every shape and size: the controls inside the region, every pad bit once,
   no two buttons nearer than 2q (the D-pad's keys are one control), the
   arrangement touch.c's table gives, and the fold rule's sides */
static void check_layout_common(const IcoTouchLayout *l)
{
    const IcoTouchRect *R = &l->region;
    const IcoTouchButton *b = l->button;
    const float q = l->unit, c = R->x + 0.5f * R->w;
    unsigned int bits = 0;
    float hx0 = 0.0f, hx1 = 0.0f;
    int i, j, fails = failures;

    CHECK(q > 0.0f);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        CHECK(rect_inside(&b[i].rect, R));
        CHECK((bits & b[i].pad) == 0);
        bits |= b[i].pad;
        for (j = i + 1; j < ICO_TOUCH_BUTTONS; j++) {
            if (is_dpad(i) && is_dpad(j)) {
                continue;
            }
            if (button_gap(&b[i], &b[j]) < 2.0f * q - 0.01f) {
                fprintf(stderr, "  %ux%u size %d rule %d: zones %d and %d %.2f apart (q %.2f)\n",
                        l->outW, l->outH, l->size, l->rule, i, j, (double)button_gap(&b[i], &b[j]),
                        (double)q);
                CHECK(0);
            }
        }
    }
    CHECK(bits == 0xF9FFu); /* everything but L3 and R3 */
    CHECK(b[ICO_TOUCH_B_CROSS].round && b[ICO_TOUCH_B_R1].round && !b[ICO_TOUCH_B_UP].round &&
          !b[ICO_TOUCH_B_R2].round);
    CHECK(rect_inside(R, &l->safeRect));

    /* the stick: R = 11q, its home disc inside its area and clear of every
       button */
    CHECK(near_(l->stickR, ICO_TOUCH_STICK_MM * q, 0.01f));
    CHECK(near_(l->runR, ICO_TOUCH_RUN_RING * l->stickR, 0.01f));
    CHECK(near_(l->lookR, l->stickR, 0.01f));
    CHECK(rect_inside(&l->stickArea, R));
    CHECK(l->stickHomeX - l->stickR >= l->stickArea.x - 0.01f);
    CHECK(l->stickHomeX + l->stickR <= right_of(&l->stickArea) + 0.01f);
    CHECK(l->stickHomeY - l->stickR >= l->stickArea.y - 0.01f);
    CHECK(l->stickHomeY + l->stickR <= bottom_of(&l->stickArea) + 0.01f);
    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        IcoTouchButton home;

        home.rect.x = l->stickHomeX - l->stickR;
        home.rect.y = l->stickHomeY - l->stickR;
        home.rect.w = home.rect.h = 2.0f * l->stickR;
        home.round = 1;
        CHECK(button_gap(&home, &b[i]) >= 2.0f * q - 0.01f);
    }

    /* the fit clamp */
    CHECK(q <= R->h / ICO_TOUCH_BUDGET_H + 1e-3f);
    if (l->rule == ICO_TOUCH_RULE_SPLIT) {
        hx0 = l->band.x;
        hx1 = right_of(&l->band);
        CHECK(l->band.w >= 0.0f && hx0 > R->x && hx1 < right_of(R));
        CHECK(q <= (hx0 - R->x) / ICO_TOUCH_BUDGET_WL + 1e-3f);
        CHECK(q <= (right_of(R) - hx1) / ICO_TOUCH_BUDGET_WR + 1e-3f);
    } else {
        CHECK(q <= R->w / ICO_TOUCH_BUDGET_W + 1e-3f);
    }

    /* the arrangement: L2 above L1 on the left, R2 top right, Select left
       of Start, the diamond, R1 straight over Circle under R2, the D-pad
       beside the shoulders and above the stick's area */
    CHECK(bottom_of(&b[ICO_TOUCH_B_L2].rect) < b[ICO_TOUCH_B_L1].rect.y);
    CHECK(near_(b[ICO_TOUCH_B_L2].rect.x, R->x + 3.0f * q, 0.01f));
    CHECK(near_(b[ICO_TOUCH_B_L2].rect.y, R->y + 3.0f * q, 0.01f));
    CHECK(right_of(&b[ICO_TOUCH_B_L1].rect) < c);
    CHECK(near_(right_of(&b[ICO_TOUCH_B_R2].rect), right_of(R) - 3.0f * q, 0.01f));
    CHECK(near_(b[ICO_TOUCH_B_R2].rect.y, R->y + 3.0f * q, 0.01f));
    CHECK(b[ICO_TOUCH_B_R2].rect.x > c);
    CHECK(right_of(&b[ICO_TOUCH_B_SELECT].rect) < b[ICO_TOUCH_B_START].rect.x);
    CHECK(near_(b[ICO_TOUCH_B_SELECT].rect.y, R->y + 3.0f * q, 0.01f));
    CHECK(near_(b[ICO_TOUCH_B_START].rect.y, R->y + 3.0f * q, 0.01f));
    CHECK(cy_of(l, ICO_TOUCH_B_CROSS) > cy_of(l, ICO_TOUCH_B_CIRCLE));
    CHECK(near_(cy_of(l, ICO_TOUCH_B_CIRCLE), cy_of(l, ICO_TOUCH_B_SQUARE), 0.01f));
    CHECK(cy_of(l, ICO_TOUCH_B_TRIANGLE) < cy_of(l, ICO_TOUCH_B_CIRCLE));
    CHECK(cx_of(l, ICO_TOUCH_B_CIRCLE) > cx_of(l, ICO_TOUCH_B_CROSS));
    CHECK(cx_of(l, ICO_TOUCH_B_SQUARE) < cx_of(l, ICO_TOUCH_B_CROSS));
    CHECK(near_(cx_of(l, ICO_TOUCH_B_CROSS), cx_of(l, ICO_TOUCH_B_TRIANGLE), 0.01f));
    CHECK(near_(cx_of(l, ICO_TOUCH_B_CROSS), l->faceX, 0.01f));
    CHECK(near_(cx_of(l, ICO_TOUCH_B_R1), cx_of(l, ICO_TOUCH_B_CIRCLE), 0.01f));
    CHECK(b[ICO_TOUCH_B_R1].rect.y > bottom_of(&b[ICO_TOUCH_B_R2].rect));
    CHECK(bottom_of(&b[ICO_TOUCH_B_R1].rect) < b[ICO_TOUCH_B_CIRCLE].rect.y);
    CHECK(b[ICO_TOUCH_B_R1].rect.w > b[ICO_TOUCH_B_CIRCLE].rect.w);
    CHECK(b[ICO_TOUCH_B_LEFT].rect.x > right_of(&b[ICO_TOUCH_B_L1].rect));
    CHECK(bottom_of(&b[ICO_TOUCH_B_DOWN].rect) <= l->stickArea.y + 0.01f);
    CHECK(l->dpadX < c && l->dpadY < l->stickHomeY - l->stickR);

    /* the look pad: from the top, right of the stick's area, to the right
       edge, ending 2q above Triangle */
    CHECK(near_(l->lookArea.y, R->y, 0.01f));
    CHECK(l->lookArea.x >= right_of(&l->stickArea) - 0.01f);
    CHECK(near_(right_of(&l->lookArea), right_of(R), 0.01f));
    CHECK(bottom_of(&l->lookArea) <= b[ICO_TOUCH_B_TRIANGLE].rect.y - 2.0f * q + 0.01f);
    CHECK(near_(bottom_of(&l->stickArea), bottom_of(R), 0.01f));

    if (l->rule == ICO_TOUCH_RULE_SPLIT) {
        static const int left[] = {ICO_TOUCH_B_L1,    ICO_TOUCH_B_L2,   ICO_TOUCH_B_UP,
                                   ICO_TOUCH_B_DOWN,  ICO_TOUCH_B_LEFT, ICO_TOUCH_B_RIGHT,
                                   ICO_TOUCH_B_SELECT};
        static const int right[] = {ICO_TOUCH_B_R1,      ICO_TOUCH_B_R2,     ICO_TOUCH_B_START,
                                    ICO_TOUCH_B_CROSS,   ICO_TOUCH_B_CIRCLE, ICO_TOUCH_B_SQUARE,
                                    ICO_TOUCH_B_TRIANGLE};

        for (i = 0; i < (int)(sizeof(left) / sizeof(left[0])); i++) {
            CHECK(right_of(&b[left[i]].rect) <= hx0 + 0.01f);
        }
        for (i = 0; i < (int)(sizeof(right) / sizeof(right[0])); i++) {
            CHECK(b[right[i]].rect.x >= hx1 - 0.01f);
        }
        CHECK(l->lookArea.x >= hx1 - 0.01f);
        CHECK(right_of(&l->stickArea) <= hx0 + 0.01f);
        CHECK(near_(l->region.x, l->safeRect.x, 0.01f) && near_(l->region.w, l->safeRect.w, 0.01f));
    } else if (l->rule == ICO_TOUCH_RULE_TABLETOP) {
        const float y = bottom_of(&l->band);

        for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
            CHECK(b[i].rect.y >= y - 0.01f);
        }
        CHECK(near_(R->y, y, 0.01f) && l->stickArea.y >= y && l->lookArea.y >= y - 0.01f);
        CHECK(near_(bottom_of(R), bottom_of(&l->safeRect), 0.01f));
    } else {
        CHECK(l->rule == ICO_TOUCH_RULE_FULL);
        CHECK(l->band.x == 0.0f && l->band.y == 0.0f && l->band.w == 0.0f && l->band.h == 0.0f);
        CHECK(near_(R->x, l->safeRect.x, 0.01f) && near_(R->y, l->safeRect.y, 0.01f) &&
              near_(R->w, l->safeRect.w, 0.01f) && near_(R->h, l->safeRect.h, 0.01f));
        CHECK(near_(cx_of(l, ICO_TOUCH_B_START) - c, c - cx_of(l, ICO_TOUCH_B_SELECT), 0.01f));
    }

    /* a free point exists in the look pad and in the gap below it, and is
       in no zone (the tests below put fingers there) */
    {
        float x, y;
        IcoTouchRect g = gap_area(l);

        CHECK(free_point(l, l->lookArea, &x, &y));
        CHECK(free_point(l, g, &x, &y));
    }
    if (failures != fails) {
        fprintf(stderr, "  in the layout %ux%u size %d rule %d q %.3f\n", l->outW, l->outH, l->size,
                l->rule, (double)q);
    }
}

/* a test screen: its pixels, insets, density and fold, and the rule it
   should give at Small, Medium and Large */
typedef struct Shape {
    uint32_t w, h;
    IcoTouchInsets insets;
    float dpi; /* 0 unknown */
    IcoTouchFold fold;
    int rule[3];
} Shape;

#define NOFOLD {ICO_TOUCH_FOLD_NONE, 0, 0, {0.0f, 0.0f, 0.0f, 0.0f}}
#define FULL3 {ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL}

static const Shape s_shapes[] = {
    {1920, 1080, {0, 0, 0, 0}, 0.0f, NOFOLD, FULL3},     /* desktop, density unknown */
    {2340, 1080, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},   /* a 19.5:9 phone */
    {2400, 1080, {100, 0, 0, 0}, 450.0f, NOFOLD, FULL3}, /* a notch on the left */
    {2176, 1812, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},   /* a foldable open */
    {1812, 2176, {0, 0, 0, 0}, 420.0f, NOFOLD, FULL3},   /* the same, portrait */
    {2560, 1600, {0, 0, 0, 0}, 320.0f, NOFOLD, FULL3},   /* a 16:10 tablet */
    {960, 540, {0, 0, 0, 0}, 0.0f, NOFOLD, FULL3},       /* a small window */
    {3840, 2160, {0, 0, 0, 0}, 96.0f, NOFOLD, FULL3},    /* a 4K monitor */
    {2208,
     1840,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_VERTICAL, 0, 0, {1104.0f, 0.0f, 0.0f, 1840.0f}},
     {ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT}}, /* flat, zero wide */
    {2176,
     1812,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_HORIZONTAL, 1, 1, {0.0f, 906.0f, 2176.0f, 0.0f}},
     {ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_RULE_TABLETOP}}, /* bent */
    {2176,
     1812,
     {0, 0, 0, 0},
     420.0f,
     {ICO_TOUCH_FOLD_HORIZONTAL, 0, 0, {0.0f, 906.0f, 2176.0f, 0.0f}},
     /* flat: the crease misses every button until Large puts R1 on it */
     {ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_FULL, ICO_TOUCH_RULE_TABLETOP}},
    {2784,
     1800,
     {0, 0, 0, 0},
     401.0f,
     {ICO_TOUCH_FOLD_VERTICAL, 0, 1, {1350.0f, 0.0f, 84.0f, 1800.0f}},
     {ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_RULE_SPLIT}}, /* two screens */
};

static IcoTouchLayout shape_layout(const Shape *s, int size)
{
    IcoTouchEnv env;

    env.pxPerMm = ico_touch_px_per_mm(s->dpi);
    env.fold = s->fold;
    return ico_touch_layout_env(s->w, s->h, s->insets, size, &env);
}

static IcoTouchEnv env_of(float dpi, int orientation, int half, int sep, float x, float y, float w,
                          float h)
{
    IcoTouchEnv e;

    e.pxPerMm = ico_touch_px_per_mm(dpi);
    e.fold.orientation = orientation;
    e.fold.halfOpened = half;
    e.fold.separating = sep;
    e.fold.bounds.x = x;
    e.fold.bounds.y = y;
    e.fold.bounds.w = w;
    e.fold.bounds.h = h;
    return e;
}

static void test_layouts(void)
{
    const float k420 = 420.0f / 25.4f;
    IcoTouchInsets in;
    IcoTouchLayout l;
    IcoTouchEnv e;
    int r, sz;

    for (r = 0; r < (int)(sizeof(s_shapes) / sizeof(s_shapes[0])); r++) {
        for (sz = 0; sz < 3; sz++) {
            l = shape_layout(&s_shapes[r], sz);
            if (l.rule != s_shapes[r].rule[sz]) {
                fprintf(stderr, "  shape %d size %d: rule %d, not %d\n", r, sz, l.rule,
                        s_shapes[r].rule[sz]);
                CHECK(0);
            }
            check_layout_common(&l);
        }
    }

    /* the density */
    CHECK(near_(ico_touch_px_per_mm(160.0f), 6.2992f, 1e-3f));
    CHECK(near_(ico_touch_px_per_mm(420.0f), 16.535f, 1e-3f));
    CHECK(ico_touch_px_per_mm(0.0f) == 0.0f && ico_touch_px_per_mm(-1.0f) == 0.0f);

    /* 2560 x 1600 at 320 dpi: real millimetres, nothing clamps */
    e = env_of(320.0f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    l = ico_touch_layout_env(2560, 1600, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(near_(l.unit, 12.598f, 1e-2f) && near_(l.pxPerMm, 12.598f, 1e-2f));
    CHECK(near_(l.stickR, 11.0f * 12.598f, 0.05f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.w, 11.0f * l.unit, 0.01f));
    CHECK(near_(ico_touch_layout_env(2560, 1600, no_insets(), ICO_TOUCH_SMALL, &e).unit,
                0.85f * l.unit, 0.01f));
    CHECK(near_(ico_touch_layout_env(2560, 1600, no_insets(), ICO_TOUCH_LARGE, &e).unit,
                1.2f * l.unit, 0.01f));

    /* 2340 x 1080 at 420 dpi: Medium real, Large capped by the height */
    e = env_of(420.0f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    l = ico_touch_layout_env(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(near_(l.unit, k420, 1e-3f) && l.rule == ICO_TOUCH_RULE_FULL);
    CHECK(near_(ico_touch_layout_env(2340, 1080, no_insets(), ICO_TOUCH_LARGE, &e).unit,
                1080.0f / 64.0f, 1e-3f));

    /* 2176 x 1812 at 420: Large capped by the width; portrait by the width
       at Medium too, Select still clear of the D-pad */
    CHECK(near_(ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_LARGE, &e).unit,
                2176.0f / 114.0f, 1e-3f));
    l = ico_touch_layout_env(1812, 2176, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(near_(l.unit, 1812.0f / 114.0f, 1e-3f));
    CHECK(l.button[ICO_TOUCH_B_SELECT].rect.x >=
          right_of(&l.button[ICO_TOUCH_B_RIGHT].rect) + 2.0f * l.unit);

    /* unknown density: the short side over 68 mm, the old look */
    l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    CHECK(near_(l.unit, 1080.0f / 68.0f, 1e-3f) && near_(l.pxPerMm, 1080.0f / 68.0f, 1e-3f));
    CHECK(near_(l.stickArea.x, 0.0f, 0.01f) && near_(l.stickArea.y, 29.0f * l.unit, 0.01f));
    CHECK(near_(l.stickArea.w, 768.0f, 0.01f)); /* 0.4 of the width */
    CHECK(near_(l.lookArea.x, 768.0f, 0.01f) && near_(l.lookArea.w, 1152.0f, 0.01f));
    CHECK(near_(l.lookArea.h, 1080.0f - 37.0f * l.unit, 0.01f));
    CHECK(near_(l.stickHomeX, 24.0f * l.unit, 0.01f) &&
          near_(l.stickHomeY, 1080.0f - 21.0f * l.unit, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L2].rect.x, 3.0f * l.unit, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L1].rect.y, 13.0f * l.unit, 0.01f));
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_R1].rect), 1920.0f - 4.0f * l.unit, 0.01f));
    CHECK(near_(bottom_of(&l.button[ICO_TOUCH_B_CROSS].rect), 1080.0f - 5.0f * l.unit, 0.01f));
    CHECK(near_(l.faceX, 1920.0f - 20.0f * l.unit, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.x, 960.0f - 13.0f * l.unit, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_START].rect.x, 960.0f + l.unit, 0.01f));
    {
        IcoTouchLayout n = ico_touch_layout_env(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL);

        CHECK(memcmp(&n, &l, sizeof(l)) == 0);
    }
    CHECK(ico_touch_layout(1920, 1080, no_insets(), 7).size == ICO_TOUCH_MEDIUM);

    /* a big monitor's density is too low for a thumb: the short side over
       160 instead */
    e = env_of(96.0f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    CHECK(near_(ico_touch_layout_env(3840, 2160, no_insets(), ICO_TOUCH_MEDIUM, &e).unit,
                2160.0f / 160.0f, 1e-3f));

    /* a vertical fold at 1104 of 2208 (zero wide): the clusters beside its
       4 mm band, Select ending 3q before it and Start 3q after */
    e = env_of(420.0f, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 1104.0f, 0.0f, 0.0f, 1840.0f);
    l = ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT && near_(l.unit, k420, 1e-3f));
    CHECK(near_(l.band.x, 1104.0f - 2.0f * k420, 0.01f) && near_(l.band.w, 4.0f * k420, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.x, 1104.0f - 17.0f * k420, 0.02f)); /* 823 */
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_SELECT].rect), 1104.0f - 5.0f * k420, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_START].rect.x, 1104.0f + 5.0f * k420, 0.02f)); /* 1187 */
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_START].rect), 1104.0f + 17.0f * k420, 0.02f));
    CHECK(near_(right_of(&l.stickArea), l.band.x, 0.01f));
    CHECK(near_(l.lookArea.x, right_of(&l.band), 0.01f));

    /* a horizontal fold half opened at 906 of 1812: everything below its
       band, the unit from the height left */
    e = env_of(420.0f, ICO_TOUCH_FOLD_HORIZONTAL, 1, 0, 0.0f, 906.0f, 2176.0f, 0.0f);
    l = ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP);
    CHECK(near_(l.region.y, 906.0f + 2.0f * k420, 0.01f));
    CHECK(near_(l.unit, (1812.0f - 906.0f - 2.0f * k420) / 64.0f, 1e-3f)); /* 13.64 */

    /* 2400 x 1080 with a 100 px notch on the left: everything moves in */
    in = no_insets();
    in.left = 100.0f;
    l = ico_touch_layout(2400, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.x, 100.0f, 0.01f) && near_(l.safeRect.w, 2300.0f, 0.01f));
    CHECK(near_(l.region.x, 100.0f, 0.01f));
    CHECK(near_(l.stickArea.x, 100.0f, 0.01f) && near_(l.stickArea.w, 920.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L1].rect.x, 100.0f + 3.0f * l.unit, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L2].rect.x, 100.0f + 3.0f * l.unit, 0.01f));
    CHECK(near_(l.lookArea.x, 1020.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.x, 1250.0f - 13.0f * l.unit, 0.01f));
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_R2].rect), 2400.0f - 3.0f * l.unit, 0.01f));

    /* insets that leave nothing are ignored; negative ones are zero */
    in.left = 1500.0f;
    in.right = 1000.0f;
    l = ico_touch_layout(2400, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.x, 0.0f, 0.01f) && near_(l.safeRect.w, 2400.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L1].rect.x, 3.0f * l.unit, 0.01f));
    in = no_insets();
    in.top = -20.0f;
    l = ico_touch_layout(1920, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.y, 0.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L2].rect.y, 3.0f * l.unit, 0.01f));
}

/* The hinge rule's edges: folds it ignores, and a flat crease that would
   cross a button laying the controls out below it instead. */
static void test_fold_rules(void)
{
    const float k420 = 420.0f / 25.4f;
    IcoTouchLayout l, full;
    IcoTouchEnv e;
    IcoTouchInsets in;
    float y;

    e = env_of(420.0f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    full = ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(full.rule == ICO_TOUCH_RULE_FULL && near_(full.pxPerMm, k420, 1e-3f));

    /* a band touching the left edge: a side of nothing, ignored */
    e = env_of(420.0f, ICO_TOUCH_FOLD_VERTICAL, 1, 1, 0.0f, 0.0f, 10.0f, 1840.0f);
    l = ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_FULL && memcmp(&l, &full, sizeof(l)) == 0);
    /* a side under a quarter of the safe area (left of 0.2 of the width) */
    e = env_of(420.0f, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 0.2f * 2208.0f, 0.0f, 0.0f, 1840.0f);
    CHECK(ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e).rule ==
          ICO_TOUCH_RULE_FULL);
    /* at 0.3 of the width both sides are wide enough */
    e.fold.bounds.x = 0.3f * 2208.0f;
    l = ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT);
    check_layout_common(&l);
    /* bounds right of the output, and bounds in an inset only */
    e.fold.bounds.x = 3000.0f;
    CHECK(ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e).rule ==
          ICO_TOUCH_RULE_FULL);
    in = no_insets();
    in.left = 200.0f;
    e.fold.bounds.x = 100.0f;
    CHECK(ico_touch_layout_env(2208, 1840, in, ICO_TOUCH_MEDIUM, &e).rule == ICO_TOUCH_RULE_FULL);
    /* a horizontal band half opened at 0.2 of the height: ignored */
    e = env_of(420.0f, ICO_TOUCH_FOLD_HORIZONTAL, 1, 0, 0.0f, 0.2f * 1812.0f, 2176.0f, 0.0f);
    CHECK(ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e).rule ==
          ICO_TOUCH_RULE_FULL);
    /* an unknown orientation */
    e = env_of(420.0f, 7, 1, 1, 1000.0f, 0.0f, 0.0f, 1812.0f);
    CHECK(ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e).rule ==
          ICO_TOUCH_RULE_FULL);

    /* vertical, bent: split as when flat */
    e = env_of(420.0f, ICO_TOUCH_FOLD_VERTICAL, 1, 1, 1104.0f, 0.0f, 0.0f, 1840.0f);
    l = ico_touch_layout_env(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT);
    /* horizontal, flat but separating (two screens): below the band */
    e = env_of(420.0f, ICO_TOUCH_FOLD_HORIZONTAL, 0, 1, 0.0f, 900.0f, 2176.0f, 12.0f);
    l = ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP && near_(l.region.y, 912.0f + 2.0f * k420, 0.01f));
    check_layout_common(&l);

    /* flat at 906: the crease crosses only the stick and look areas */
    e = env_of(420.0f, ICO_TOUCH_FOLD_HORIZONTAL, 0, 0, 0.0f, 906.0f, 2176.0f, 0.0f);
    l = ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_FULL && near_(l.unit, k420, 1e-3f));
    CHECK(l.stickArea.y < 906.0f && bottom_of(&l.lookArea) > 906.0f);
    /* flat through R1's centre: below the band instead */
    y = cy_of(&l, ICO_TOUCH_B_R1);
    e.fold.bounds.y = y;
    l = ico_touch_layout_env(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP);
    CHECK(near_(l.region.y, y + 2.0f * k420, 0.01f));
    CHECK(near_(l.unit, (1812.0f - y - 2.0f * k420) / 64.0f, 1e-3f));
    check_layout_common(&l);
}

static void test_stick(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoTouchDrawInfo d;
    IcoVirtualPad v;
    const float R = l.stickR, cx = l.stickHomeX, cy = l.stickHomeY;
    float x, y;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, 1 * S);

    /* idle: the home ring */
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(!d.stickActive && near_(d.stickCX, l.stickHomeX, 0.01f) &&
          near_(d.knobY, l.stickHomeY, 0.01f));

    /* the finger down is the centre: no deflection yet */
    ev(&t, 7, cx, cy, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(d.stickActive && near_(d.stickCX, cx, 0.01f) && near_(d.stickCY, cy, 0.01f));
    CHECK(v.lx == 0.0f && v.ly == 0.0f && v.buttons == 0);

    /* half R to the right */
    ev(&t, 7, cx + 0.5f * R, cy, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.lx, 0.5f, 0.002f) && near_(v.ly, 0.0f, 0.002f));
    /* up is -1 (the pad's y grows downwards) */
    ev(&t, 7, cx, cy - 0.75f * R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.lx, 0.0f, 0.002f) && near_(v.ly, -0.75f, 0.002f));
    /* past R: clamped, the knob on the ring, the centre stays */
    ev(&t, 7, cx + 1.2f * R, cy - 1.6f * R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(near_(v.lx, 0.6f, 0.002f) && near_(v.ly, -0.8f, 0.002f));
    CHECK(near_(d.knobX, cx + 0.6f * R, 0.05f) && near_(d.knobY, cy - 0.8f * R, 0.05f));
    CHECK(near_(d.stickCX, cx, 0.01f) && near_(d.stickMag, 1.0f, 0.002f));
    /* the dead zone: under 5 % of R is centred, just past it is not rescaled */
    ev(&t, 7, cx + 0.04f * R, cy, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.lx == 0.0f && v.ly == 0.0f);
    d = ico_touch_draw_info(&t, &l);
    CHECK(d.stickActive && d.knobX > cx); /* the knob still follows */
    ev(&t, 7, cx + 0.06f * R, cy, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.lx, 0.06f, 0.002f));
    /* the run ring: the magnitude there is ICO_TOUCH_RUN_RING */
    ev(&t, 7, cx - l.runR, cy, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(ICO_TOUCH_RUN_RING == 0.94f);
    CHECK(near_(v.lx, -ICO_TOUCH_RUN_RING, 0.002f) && near_(d.stickMag, 0.94f, 0.002f));
    CHECK(near_(absf(d.knobX - d.stickCX), d.runR, 0.05f));

    /* a second finger in the stick's area does not take the stick */
    CHECK(in_area(&l.stickArea, cx + R, cy));
    ev(&t, 8, cx + R, cy, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(near_(d.stickCX, cx, 0.01f) && near_(v.lx, -0.94f, 0.002f));
    ev(&t, 8, cx + R, cy, ICO_TOUCH_UP, 1 * S);

    /* the stick follows a finger that leaves its area */
    ev(&t, 7, cx + 0.5f * R, cy - 3.0f * R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.ly < -0.9f && step(&t, 1 * S).buttons == 0);

    /* lifted: centred, home again; the next finger down is a new centre */
    ev(&t, 7, cx + 0.5f * R, cy, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(v.lx == 0.0f && v.ly == 0.0f && !d.stickActive);
    CHECK(near_(d.stickCX, l.stickHomeX, 0.01f));
    x = cx + 0.5f * R;
    y = cy - 0.5f * R;
    CHECK(in_area(&l.stickArea, x, y));
    ev(&t, 9, x, y, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 9, x, y + R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.ly, 1.0f, 0.002f) && near_(v.lx, 0.0f, 0.002f));
    d = ico_touch_draw_info(&t, &l);
    CHECK(near_(d.stickCX, x, 0.01f) && near_(d.stickCY, y, 0.01f));

    /* a finger outside every zone does nothing, even moved onto the look
       pad */
    memset(&t, 0, sizeof(t));
    CHECK(free_point(&l, gap_area(&l), &x, &y)); /* bottom middle */
    ev(&t, 1, x, y, ICO_TOUCH_DOWN, 1 * S);
    CHECK(free_point(&l, l.lookArea, &x, &y));
    ev(&t, 1, x, y, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.rx == 0.0f && v.ry == 0.0f);
}

static void test_dpad(void)
{
    static const int zone[4] = {ICO_TOUCH_B_UP, ICO_TOUCH_B_DOWN, ICO_TOUCH_B_LEFT,
                                ICO_TOUCH_B_RIGHT};
    static const unsigned int bit[4] = {ICO_PAD_UP, ICO_PAD_DOWN, ICO_PAD_LEFT, ICO_PAD_RIGHT};
    IcoTouchLayout l = ico_touch_layout(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoVirtualPad v;
    float x, y;
    int i;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    for (i = 0; i < 4; i++) {
        centre_of(&l, zone[i], &x, &y);
        ev(&t, 3, x, y, ICO_TOUCH_DOWN, 1 * S);
        v = step(&t, 1 * S);
        CHECK(v.buttons == bit[i]);
        CHECK(ico_touch_draw_info(&t, &l).pressed == 1u << zone[i]);
        CHECK(v.lx == 0.0f && v.rx == 0.0f); /* not the stick, not the look pad */
        ev(&t, 3, x, y, ICO_TOUCH_UP, 1 * S);
        CHECK(step(&t, 1 * S).buttons == 0);
    }
    /* the cluster's centre is between the rects: nothing */
    ev(&t, 3, l.dpadX, l.dpadY, ICO_TOUCH_DOWN, 1 * S);
    CHECK(step(&t, 1 * S).buttons == 0);
    /* rolling the thumb from up to right */
    centre_of(&l, ICO_TOUCH_B_UP, &x, &y);
    ev(&t, 3, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons == 0); /* went down on no button: stays nothing */
    ev(&t, 3, x, y, ICO_TOUCH_UP, 1 * S);
    ev(&t, 4, x, y, ICO_TOUCH_DOWN, 1 * S);
    CHECK(step(&t, 1 * S).buttons == ICO_PAD_UP);
    centre_of(&l, ICO_TOUCH_B_RIGHT, &x, &y);
    ev(&t, 4, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons == ICO_PAD_RIGHT);
    ev(&t, 4, x, y, ICO_TOUCH_UP, 1 * S);
    CHECK(step(&t, 1 * S).buttons == 0);

    /* a tap down and up between two steps is held for one step */
    centre_of(&l, ICO_TOUCH_B_DOWN, &x, &y);
    ev(&t, 5, x, y, ICO_TOUCH_DOWN, 2 * S);
    ev(&t, 5, x, y, ICO_TOUCH_UP, 2 * S);
    CHECK(step(&t, 2 * S).buttons == ICO_PAD_DOWN);
    CHECK(step(&t, 2 * S).buttons == 0);
}

static void test_buttons(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_LARGE);
    IcoTouchState t;
    IcoVirtualPad v;
    float x, y, x2, y2, gx, gy, px, py;
    int z;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    CHECK(free_point(&l, gap_area(&l), &gx, &gy)); /* no zone */
    CHECK(look_point(&l, &px, &py));               /* the look pad */

    /* every button by its centre */
    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        centre_of(&l, z, &x, &y);
        ev(&t, 1, x, y, ICO_TOUCH_DOWN, 1 * S);
        v = step(&t, 1 * S);
        CHECK(v.buttons == l.button[z].pad);
        CHECK(v.menu_buttons == l.button[z].pad); /* the glyphs are positions */
        ev(&t, 1, x, y, ICO_TOUCH_UP, 1 * S);
        CHECK(step(&t, 1 * S).buttons == 0);
    }
    /* a disc: the corner of Cross's bounding square is not Cross */
    ev(&t, 1, l.button[ICO_TOUCH_B_CROSS].rect.x + 2.0f,
       l.button[ICO_TOUCH_B_CROSS].rect.y + l.button[ICO_TOUCH_B_CROSS].rect.h - 2.0f,
       ICO_TOUCH_DOWN, 1 * S);
    CHECK(step(&t, 1 * S).buttons == 0);
    ev(&t, 1, 0, 0, ICO_TOUCH_UP, 1 * S);

    /* the stick and Cross at once */
    ev(&t, 10, l.stickHomeX, l.stickHomeY, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 10, l.stickHomeX + l.stickR, l.stickHomeY, ICO_TOUCH_MOVE, 1 * S);
    centre_of(&l, ICO_TOUCH_B_CROSS, &x, &y);
    ev(&t, 11, x, y, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == ICO_PAD_CROSS && near_(v.lx, 1.0f, 0.002f));
    /* Cross and Circle with two fingers, the stick still held */
    centre_of(&l, ICO_TOUCH_B_CIRCLE, &x2, &y2);
    ev(&t, 12, x2, y2, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == (ICO_PAD_CROSS | ICO_PAD_CIRCLE) && near_(v.lx, 1.0f, 0.002f));
    CHECK(ico_touch_draw_info(&t, &l).pressed ==
          ((1u << ICO_TOUCH_B_CROSS) | (1u << ICO_TOUCH_B_CIRCLE)));
    /* one lifts: the other stays */
    ev(&t, 11, x, y, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == ICO_PAD_CIRCLE && near_(v.lx, 1.0f, 0.002f));

    /* slide out: Circle's finger moves to empty screen, released; the stick
       finger is not affected */
    ev(&t, 12, gx, gy, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && near_(v.lx, 1.0f, 0.002f));
    /* and back onto a button (Square): a button finger presses what it is on */
    centre_of(&l, ICO_TOUCH_B_SQUARE, &x, &y);
    ev(&t, 12, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons == ICO_PAD_SQUARE);
    /* sliding onto the look pad does not turn it into a look finger */
    ev(&t, 12, px, py, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.rx == 0.0f && v.ry == 0.0f);

    /* a button over the look pad (R1) wins over the pad */
    centre_of(&l, ICO_TOUCH_B_R1, &x, &y);
    CHECK(x >= l.lookArea.x && y < l.lookArea.y + l.lookArea.h);
    ev(&t, 13, x, y, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 13, x - 2.0f * l.button[ICO_TOUCH_B_R1].rect.w, y, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.rx == 0.0f); /* moved off before the step: nothing */
    ev(&t, 13, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons & ICO_PAD_R1);

    /* cancel: everything released at once */
    ev(&t, 14, px, py, ICO_TOUCH_DOWN, 1 * S); /* the look pad */
    ev(&t, 14, px + l.lookR, py, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 1.0f, 0.002f) && near_(v.lx, 1.0f, 0.002f) && (v.buttons & ICO_PAD_R1));
    ico_touch_event(&t, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.ly == 0.0f && v.rx == 0.0f && v.ry == 0.0f);
    CHECK(ico_touch_draw_info(&t, &l).pressed == 0 && !ico_touch_draw_info(&t, &l).stickActive);
    /* the fingers are gone: their moves and lifts do nothing */
    ev(&t, 10, l.stickHomeX + l.stickR, l.stickHomeY + l.stickR, ICO_TOUCH_MOVE, 1 * S);
    ev(&t, 11, x, y, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.ly == 0.0f);

    /* more fingers than slots: the extra ones are ignored */
    centre_of(&l, ICO_TOUCH_B_START, &x, &y);
    for (z = 0; z < ICO_TOUCH_FINGERS; z++) {
        ev(&t, 100 + (uint64_t)z, gx, gy, ICO_TOUCH_DOWN, 1 * S);
    }
    ev(&t, 200, x, y, ICO_TOUCH_DOWN, 1 * S);
    CHECK(step(&t, 1 * S).buttons == 0);
    ico_touch_event(&t, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, 1 * S);
    ev(&t, 200, x, y, ICO_TOUCH_DOWN, 1 * S);
    CHECK(step(&t, 1 * S).buttons == ICO_PAD_START);
}

static void test_look(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoTouchDrawInfo d;
    IcoVirtualPad v;
    float R = l.lookR, lx = 0.0f, ly = 0.0f;

    g_l = &l;
    CHECK(look_point(&l, &lx, &ly));
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, 1 * S);
    CHECK(near_(t.look_decay, 0.80f, 1e-6f));

    ev(&t, 1, lx, ly, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.rx == 0.0f && v.ry == 0.0f && v.lx == 0.0f);
    ev(&t, 1, lx + 0.5f * R, ly - 0.25f * R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 0.5f, 0.002f) && near_(v.ry, -0.25f, 0.002f));
    d = ico_touch_draw_info(&t, &l);
    CHECK(d.lookActive && near_(d.lookCX, lx, 0.01f) && near_(d.lookFX, lx + 0.5f * R, 0.01f));
    /* held still: the deflection holds (a stick, not mouse motion) */
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 0.5f, 0.002f));
    /* clamped to the unit circle */
    ev(&t, 1, lx - 2.0f * R, ly, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -1.0f, 0.002f) && near_(v.ry, 0.0f, 0.002f));
    /* a second finger on the pad does nothing */
    ev(&t, 2, lx, ly, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -1.0f, 0.002f));
    ev(&t, 2, lx, ly, ICO_TOUCH_UP, 1 * S);
    /* released: decays like the mouse look, 0.8 per step */
    ev(&t, 1, lx - 2.0f * R, ly, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -0.8f, 0.002f));
    CHECK(!ico_touch_draw_info(&t, &l).lookActive);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -0.64f, 0.002f));
    {
        int i;

        for (i = 0; i < 60; i++) {
            v = step(&t, 1 * S);
        }
    }
    CHECK(v.rx == 0.0f && v.ry == 0.0f);
    /* the bindings' mouse_decay carried over */
    t.look_decay = 0.5f;
    ev(&t, 3, lx, ly, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 3, lx + R, ly, ICO_TOUCH_MOVE, 1 * S);
    step(&t, 1 * S);
    ev(&t, 3, lx + R, ly, ICO_TOUCH_UP, 1 * S);
    CHECK(near_(step(&t, 1 * S).rx, 0.5f, 0.002f));
    /* a zeroed state centres at once */
    memset(&t, 0, sizeof(t));
    ev(&t, 3, lx, ly, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 3, lx + R, ly, ICO_TOUCH_MOVE, 1 * S);
    CHECK(near_(step(&t, 1 * S).rx, 1.0f, 0.002f));
    ev(&t, 3, lx + R, ly, ICO_TOUCH_UP, 1 * S);
    CHECK(step(&t, 1 * S).rx == 0.0f);
}

static void test_visibility(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    uint64_t T = 10 * S, L;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    /* never touched: hidden */
    CHECK(!ico_touch_visible(&t, 0, T) && ico_touch_opacity(&t, 0, T) == 0.0f);

    /* a reset shows it for 5 s, fading in over 300 ms */
    ico_touch_reset(&t, T);
    CHECK(ico_touch_visible(&t, 0, T));
    CHECK(near_(ico_touch_opacity(&t, 0, T), 0.0f, 1e-4f));
    CHECK(near_(ico_touch_opacity(&t, 0, T + 150 * MS), 0.5f, 1e-3f));
    CHECK(near_(ico_touch_opacity(&t, 0, T + 300 * MS), 1.0f, 1e-4f));
    CHECK(ico_touch_visible(&t, 0, T + 4999 * MS));
    CHECK(!ico_touch_visible(&t, 0, T + 5 * S));
    /* fading out over the next 300 ms */
    CHECK(near_(ico_touch_opacity(&t, 0, T + 5 * S + 150 * MS), 0.5f, 1e-3f));
    CHECK(ico_touch_opacity(&t, 0, T + 5 * S + 300 * MS) == 0.0f);
    CHECK(ico_touch_opacity(&t, 0, T + 60 * S) == 0.0f);

    /* a finger down shows it; held, it stays however long */
    T = 100 * S;
    ev(&t, 1, 300.0f, 700.0f, ICO_TOUCH_DOWN, T);
    step(&t, T);
    CHECK(ico_touch_visible(&t, 0, T));
    CHECK(near_(ico_touch_opacity(&t, 0, T + 150 * MS), 0.5f, 1e-3f));
    CHECK(ico_touch_visible(&t, 0, T + 30 * S));
    CHECK(ico_touch_opacity(&t, 0, T + 30 * S) == 1.0f);
    /* lifted at L: 5 s more */
    L = T + 30 * S;
    ev(&t, 1, 300.0f, 700.0f, ICO_TOUCH_UP, L);
    step(&t, L);
    CHECK(ico_touch_visible(&t, 0, L + 4 * S));
    CHECK(!ico_touch_visible(&t, 0, L + 5 * S + 1));
    /* a move or a lift of another finger counts as a finger event too:
       touching again half way through the fade out continues from there */
    CHECK(near_(ico_touch_opacity(&t, 0, L + 5 * S + 150 * MS), 0.5f, 1e-3f));
    ev(&t, 2, 1300.0f, 300.0f, ICO_TOUCH_DOWN, L + 5 * S + 150 * MS);
    CHECK(ico_touch_visible(&t, 0, L + 5 * S + 150 * MS));
    CHECK(near_(ico_touch_opacity(&t, 0, L + 5 * S + 150 * MS), 0.5f, 1e-3f));
    CHECK(near_(ico_touch_opacity(&t, 0, L + 5 * S + 225 * MS), 0.75f, 1e-3f));
    CHECK(ico_touch_opacity(&t, 0, L + 5 * S + 300 * MS) == 1.0f);
    ev(&t, 2, 1300.0f, 300.0f, ICO_TOUCH_UP, L + 6 * S);
    step(&t, L + 6 * S);

    /* a gamepad hides it, at once for visible(), fading for opacity */
    T = L + 7 * S;
    ev(&t, 3, 1300.0f, 300.0f, ICO_TOUCH_DOWN, T);
    step(&t, T);
    CHECK(ico_touch_visible(&t, 0, T + S) && !ico_touch_visible(&t, 1, T + S));
    CHECK(ico_touch_opacity(&t, 1, T + S) == 0.0f); /* no hotplug noted: gone */
    ico_touch_set_gamepads(&t, 1, T + S);
    CHECK(near_(ico_touch_opacity(&t, 1, T + S + 150 * MS), 0.5f, 1e-3f));
    CHECK(ico_touch_opacity(&t, 1, T + S + 300 * MS) == 0.0f);
    ico_touch_set_gamepads(&t, 2, T + 2 * S); /* a second pad: no new fade */
    CHECK(ico_touch_opacity(&t, 2, T + 2 * S) == 0.0f);
    /* touches still map, unseen, while a gamepad is connected */
    {
        float x, y;
        IcoVirtualPad v;

        centre_of(&l, ICO_TOUCH_B_CROSS, &x, &y);
        ev(&t, 4, x, y, ICO_TOUCH_DOWN, T + 2 * S);
        v = step(&t, T + 2 * S);
        CHECK(v.buttons == ICO_PAD_CROSS && !ico_touch_visible(&t, 2, T + 2 * S));
        ev(&t, 4, x, y, ICO_TOUCH_UP, T + 2 * S);
        step(&t, T + 2 * S);
    }
    /* unplugged with a finger still down: fades back in */
    ico_touch_set_gamepads(&t, 0, T + 3 * S);
    CHECK(ico_touch_visible(&t, 0, T + 3 * S));
    CHECK(near_(ico_touch_opacity(&t, 0, T + 3 * S + 150 * MS), 0.5f, 1e-3f));
    CHECK(ico_touch_opacity(&t, 0, T + 3 * S + 300 * MS) == 1.0f);
    /* a reset keeps the gamepad count */
    ico_touch_set_gamepads(&t, 1, T + 4 * S);
    ico_touch_reset(&t, T + 5 * S);
    CHECK(t.gamepads == 1 && !ico_touch_visible(&t, 1, T + 5 * S));
    CHECK(ico_touch_opacity(&t, 1, T + 6 * S) == 0.0f);
    /* cancel counts as the last finger event */
    memset(&t, 0, sizeof(t));
    ev(&t, 1, 300.0f, 700.0f, ICO_TOUCH_DOWN, 50 * S);
    ico_touch_event(&t, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, 52 * S);
    CHECK(ico_touch_visible(&t, 0, 56 * S) && !ico_touch_visible(&t, 0, 57 * S));
}

/* fumi/ios/pad.c iosPadNormalizeStick on a frame's stick bytes: the
   magnitude the game reads (0 in its dead zone, 1 from 120 after the
   off-axis divisor; the game truncates the angle to whole degrees, this
   uses vpad.c's continuous one) */
static float game_magnitude(unsigned char bx, unsigned char by)
{
    float x = (float)bx - 127.5f, y = (float)by - 127.5f;
    float len = sqrt_(x * x + y * y);

    if (len <= 48.0f) {
        return 0.0f;
    }
    len /= ico_input_stick_fix_scale(x, y);
    if (len < 48.0f) {
        len = 48.0f;
    }
    if (len >= 120.0f) {
        return 1.0f;
    }
    return (len - 48.0f) / 72.0f;
}

static void test_merged_frame(void)
{
    IcoTouchLayout l = ico_touch_layout(2400, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoVirtualPad bind, tv, v;
    IcoPadFrame f;
    const float R = l.stickR, hx = l.stickHomeX, hy = l.stickHomeY;
    float x, y, px = 0.0f, py = 0.0f;

    g_l = &l;
    CHECK(look_point(&l, &px, &py));
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, S);

    /* the bindings' pad: Start held, a small keyboard push left (walk) */
    memset(&bind, 0, sizeof(bind));
    bind.buttons = ICO_PAD_START;
    bind.lx = -0.3f;

    /* touch: the stick full down-right at 45 degrees, Cross */
    ev(&t, 1, hx, hy, ICO_TOUCH_DOWN, S);
    ev(&t, 1, hx + R, hy + R, ICO_TOUCH_MOVE, S); /* 1.41 R: clamped, on the output */
    centre_of(&l, ICO_TOUCH_B_CROSS, &x, &y);
    ev(&t, 2, x, y, ICO_TOUCH_DOWN, S);
    ico_touch_step(&t, &l, &tv);
    CHECK(tv.buttons == ICO_PAD_CROSS);
    CHECK(near_(tv.lx, 0.70711f, 0.002f) && near_(tv.ly, 0.70711f, 0.002f));

    v = bind;
    ico_vpad_merge(&v, &tv);
    CHECK(v.buttons == (ICO_PAD_START | ICO_PAD_CROSS));
    CHECK(near_(v.lx, tv.lx, 1e-6f) && near_(v.ly, tv.ly, 1e-6f)); /* the longer stick */
    CHECK(v.rx == 0.0f && v.ry == 0.0f);

    /* stick fix off: (0.707 + 1) * 127.5 = 218; the game divides a diagonal
       by 1.2, so a full diagonal push walks, as on a round gamepad gate */
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.buttons == (ICO_PAD_START | ICO_PAD_CROSS));
    CHECK(f.lx == 218 && f.ly == 218 && f.rx == 128 && f.ry == 128);
    CHECK(game_magnitude(f.lx, f.ly) < 0.99f);
    /* stick fix on: scaled by 1.2 -> 236, the game reads 1.0 (runs) */
    ico_input_vpad_to_frame(&v, 1, 0, &f);
    CHECK(f.lx == 236 && f.ly == 236);
    CHECK(game_magnitude(f.lx, f.ly) == 1.0f);
    /* mirror negates the touch stick's X like any other source */
    ico_input_vpad_to_frame(&v, 0, 1, &f);
    CHECK(f.lx == 37 && f.ly == 218);

    /* along an axis, fix or not: a full push is 255 and runs; the run ring
       (0.94 R) is a deflection of 0.94 -> 247, which the game reads as 0.99
       or more (it runs: the ring is drawn where running starts); 0.6 R is
       204, about 0.40 (it walks); 0.92 R still walks */
    ev(&t, 1, hx + 2.0f * R, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 255 && f.ly == 128 && game_magnitude(f.lx, f.ly) == 1.0f);
    ico_input_vpad_to_frame(&v, 1, 0, &f);
    CHECK(f.lx == 255 && f.ly == 128);
    ev(&t, 1, hx + l.runR, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 247 && f.ly == 128 && game_magnitude(f.lx, f.ly) >= 0.99f);
    ev(&t, 1, hx + 0.6f * R, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 204 && near_(game_magnitude(f.lx, f.ly), 0.3958f, 0.002f));
    ev(&t, 1, hx + 0.94f * R, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    ico_input_vpad_to_frame(&tv, 0, 0, &f);
    CHECK(game_magnitude(f.lx, f.ly) >= 0.99f);
    ev(&t, 1, hx + 0.92f * R, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    ico_input_vpad_to_frame(&tv, 0, 0, &f);
    CHECK(game_magnitude(f.lx, f.ly) < 0.99f);

    /* the touch stick in its dead zone loses to the keyboard's push */
    ev(&t, 1, hx + 0.03f * R, hy, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(near_(v.lx, -0.3f, 1e-6f) && f.lx == ico_input_quantise(-0.3f) && f.ly == 128);

    /* the look pad merges into the right stick */
    ev(&t, 3, px, py, ICO_TOUCH_DOWN, S);
    ev(&t, 3, px, py - R, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.rx == 128 && f.ry == 0);
}

/* Android issue 19: the touch stick in every direction (1 degree
   steps) and at every reach (inside the dead zone to past the ring) gives
   the bytes a round gamepad stick deflected as far gives: the same frame
   from ico_input_vpad_to_frame, stick fix off and on, so Ico walks and runs
   on the phone where he does with a pad. Off, no byte pair is further than
   128 from the centre (the PS2's circle) and the game's magnitude never
   passes 1. No libm: the directions are a rotation repeated 360 times. */
static void test_stick_every_direction(void)
{
    static const float reach[] = {0.03f, 0.3f, 0.5f, 0.6f, 0.92f, 0.94f, 1.0f, 1.2f, 1.5f};
    IcoTouchLayout l = ico_touch_layout(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoVirtualPad tv, pad;
    IcoPadFrame f, g;
    const float R = l.stickR, cx = l.stickHomeX, cy = l.stickHomeY;
    const double c1 = 0.99984769515639124, s1 = 0.017452406437283512; /* 1 degree */
    double c = 1.0, s = 0.0;
    int a, k, fix, diff = 0, outside = 0, over = 0;

    g_l = &l;
    for (a = 0; a < 360; a++) {
        for (k = 0; k < (int)(sizeof(reach) / sizeof(reach[0])); k++) {
            const float r = reach[k];
            const float dx = (float)(c * r * R), dy = (float)(s * r * R);
            const float m = r > 1.0f ? 1.0f : r;

            memset(&t, 0, sizeof(t));
            ico_touch_reset(&t, S);
            ev(&t, 1, cx, cy, ICO_TOUCH_DOWN, S);
            ev(&t, 1, cx + dx, cy + dy, ICO_TOUCH_MOVE, S);
            ico_touch_step(&t, &l, &tv);
            /* the round pad deflected as far, with the touch dead zone */
            memset(&pad, 0, sizeof(pad));
            if (r >= ICO_TOUCH_STICK_DEADZONE) {
                pad.lx = (float)(c * m);
                pad.ly = (float)(s * m);
            }
            for (fix = 0; fix < 2; fix++) {
                ico_input_vpad_to_frame(&tv, fix, 0, &f);
                ico_input_vpad_to_frame(&pad, fix, 0, &g);
                if (absf((float)f.lx - (float)g.lx) > 1.0f ||
                    absf((float)f.ly - (float)g.ly) > 1.0f) {
                    if (diff++ < 5) {
                        fprintf(stderr,
                                "FAIL %d degrees at %.2f R (fix %d): touch %u %u, pad %u %u\n", a,
                                (double)r, fix, f.lx, f.ly, g.lx, g.ly);
                    }
                }
                if (!fix) {
                    const float x = (float)f.lx - 127.5f, y = (float)f.ly - 127.5f;

                    if (x * x + y * y > 128.5f * 128.5f) {
                        outside++;
                    }
                    if (game_magnitude(f.lx, f.ly) > 1.0f) {
                        over++;
                    }
                }
            }
        }
        {
            const double nc = c * c1 - s * s1;

            s = s * c1 + c * s1;
            c = nc;
        }
    }
    CHECK(diff == 0);
    CHECK(outside == 0);
    CHECK(over == 0);
    printf("touch stick: 360 directions x %d reaches match a round pad's bytes\n",
           (int)(sizeof(reach) / sizeof(reach[0])));
}

/* The device layer's gate (ico_touch_update): with a gamepad connected in
   Auto the overlay is hidden and the touches are dropped, so nothing unseen
   is pressed; Always maps them with a gamepad too; Off never. A tap made
   while dropped is not replayed once the gamepad goes. */
static void test_gamepad_drop(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoVirtualPad v;
    float x, y;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, S);
    CHECK(ico_touch_accepts(ICO_TOUCH_MODE_AUTO, 0) && !ico_touch_accepts(ICO_TOUCH_MODE_AUTO, 1));
    CHECK(ico_touch_accepts(ICO_TOUCH_MODE_ALWAYS, 0) &&
          ico_touch_accepts(ICO_TOUCH_MODE_ALWAYS, 2));
    CHECK(!ico_touch_accepts(ICO_TOUCH_MODE_OFF, 0) && !ico_touch_accepts(ICO_TOUCH_MODE_OFF, 1));

    /* Auto, one gamepad: Cross held and the stick pushed give nothing */
    ico_touch_set_gamepads(&t, 1, S);
    centre_of(&l, ICO_TOUCH_B_CROSS, &x, &y);
    ev(&t, 1, x, y, ICO_TOUCH_DOWN, S);
    ev(&t, 2, l.stickHomeX, l.stickHomeY, ICO_TOUCH_DOWN, S);
    ev(&t, 2, l.stickHomeX + l.stickR, l.stickHomeY, ICO_TOUCH_MOVE, S);
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_AUTO, 1, &v, S) == 0);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.ly == 0.0f && v.rx == 0.0f && v.ry == 0.0f);
    CHECK(ico_touch_mode_opacity(&t, ICO_TOUCH_MODE_AUTO, 1, 2 * S) == 0.0f);
    /* the same fingers in Always: merged, drawn at full opacity */
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_ALWAYS, 1, &v, S) == 1);
    CHECK(v.buttons == ICO_PAD_CROSS && near_(v.lx, 1.0f, 0.002f));
    CHECK(ico_touch_mode_opacity(&t, ICO_TOUCH_MODE_ALWAYS, 1, 2 * S) == 1.0f);
    /* Off: nothing, never drawn */
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_OFF, 0, &v, S) == 0 && v.buttons == 0);
    CHECK(ico_touch_mode_opacity(&t, ICO_TOUCH_MODE_OFF, 0, 2 * S) == 0.0f);
    ev(&t, 1, x, y, ICO_TOUCH_UP, S);
    ev(&t, 2, l.stickHomeX, l.stickHomeY, ICO_TOUCH_UP, S);

    /* a tap while dropped: down and up between two steps, not replayed
       after the gamepad goes */
    ev(&t, 3, x, y, ICO_TOUCH_DOWN, 3 * S);
    ev(&t, 3, x, y, ICO_TOUCH_UP, 3 * S);
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_AUTO, 1, &v, 3 * S) == 0 && v.buttons == 0);
    ico_touch_set_gamepads(&t, 0, 4 * S);
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_AUTO, 0, &v, 4 * S) == 1 && v.buttons == 0);
    /* Auto without a gamepad maps, and the overlay shows with its fade */
    ev(&t, 4, x, y, ICO_TOUCH_DOWN, 5 * S);
    memset(&v, 0, sizeof(v));
    CHECK(ico_touch_update(&t, &l, ICO_TOUCH_MODE_AUTO, 0, &v, 5 * S) == 1 &&
          v.buttons == ICO_PAD_CROSS);
    CHECK(near_(ico_touch_mode_opacity(&t, ICO_TOUCH_MODE_AUTO, 0, 6 * S),
                ico_touch_opacity(&t, 0, 6 * S), 1e-6f) &&
          ico_touch_mode_opacity(&t, ICO_TOUCH_MODE_AUTO, 0, 6 * S) > 0.99f);
}

int main(void)
{
    test_layouts();
    test_fold_rules();
    test_stick();
    test_dpad();
    test_buttons();
    test_look();
    test_visibility();
    test_merged_frame();
    test_stick_every_direction();
    test_gamepad_drop();
    if (failures != 0) {
        fprintf(stderr, "touch_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("touch_test: ok\n");
    return 0;
}
