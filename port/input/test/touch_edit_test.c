/*
 * port/input/test/touch_edit_test.c
 *
 * The touch layout edits (touch.c's apply_edits, touch_edit.c) on the CPU:
 * an unedited layout is the default one byte for byte over the shape
 * table, the anchor arithmetic, the clamp into the region and out of the
 * hinge band, hidden controls, the moved stick's and the look pad's areas,
 * the flat crease seeing the edits, the config text both ways, and the
 * editor's state machine fed synthetic fingers and gamepad words (tap,
 * drag and snap, the refused drop, the overlap mask, pinch, the steppers,
 * All, the posture switch, the gamepad table, Undo, Done) and the
 * toolbar strip's spot (over the top row of controls by 2.5 q at most,
 * clear of every other) and the hint's.
 */
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "touch.h"
#include "touch_edit.h"
#include "touch_shapes.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static float absf(float v)
{
    return v < 0 ? -v : v;
}

static int near_(float a, float b, float tol)
{
    return absf(a - b) <= tol;
}

static IcoTouchInsets no_insets(void)
{
    IcoTouchInsets in = {0.0f, 0.0f, 0.0f, 0.0f};

    return in;
}

static IcoTouchEnv env_of(float pxPerMm, int orientation, int half, int sep, float x, float y,
                          float w, float h)
{
    IcoTouchEnv e;

    e.pxPerMm = pxPerMm;
    e.fold.orientation = orientation;
    e.fold.halfOpened = half;
    e.fold.separating = sep;
    e.fold.bounds.x = x;
    e.fold.bounds.y = y;
    e.fold.bounds.w = w;
    e.fold.bounds.h = h;
    return e;
}

static IcoTouchLayout shape_layout(const Shape *s, int size, const IcoTouchEdits *e)
{
    IcoTouchEnv env;

    env.pxPerMm = ico_touch_px_per_mm(s->dpi);
    env.fold = s->fold;
    return ico_touch_layout_edits(s->w, s->h, s->insets, size, &env, e);
}

static float right_of(const IcoTouchRect *r)
{
    return r->x + r->w;
}

static float bottom_of(const IcoTouchRect *r)
{
    return r->y + r->h;
}

static float cx_of(const IcoTouchLayout *l, int z)
{
    return l->button[z].rect.x + 0.5f * l->button[z].rect.w;
}

static float cy_of(const IcoTouchLayout *l, int z)
{
    return l->button[z].rect.y + 0.5f * l->button[z].rect.h;
}

static int rect_inside(const IcoTouchRect *in, const IcoTouchRect *out)
{
    return in->x >= out->x - 0.01f && in->y >= out->y - 0.01f &&
           in->x + in->w <= out->x + out->w + 0.01f && in->y + in->h <= out->y + out->h + 0.01f;
}

/* the rects share a point */
static int touches(const IcoTouchRect *a, const IcoTouchRect *b)
{
    return a->x <= b->x + b->w && b->x <= a->x + a->w && a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static int same_rect(IcoTouchRect a, IcoTouchRect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* the first field two layouts differ in, or NULL */
static const char *layout_diff(const IcoTouchLayout *a, const IcoTouchLayout *b)
{
    static char buf[64];
    int z;

    if (a->outW != b->outW || a->outH != b->outH) {
        return "outW/outH";
    }
    if (a->safe.left != b->safe.left || a->safe.top != b->safe.top ||
        a->safe.right != b->safe.right || a->safe.bottom != b->safe.bottom) {
        return "safe";
    }
    if (a->size != b->size || a->unit != b->unit || a->pxPerMm != b->pxPerMm) {
        return "size/unit/pxPerMm";
    }
    if (a->rule != b->rule) {
        return "rule";
    }
    if (!same_rect(a->safeRect, b->safeRect) || !same_rect(a->region, b->region) ||
        !same_rect(a->band, b->band)) {
        return "safeRect/region/band";
    }
    if (!same_rect(a->stickArea, b->stickArea) || a->stickR != b->stickR || a->runR != b->runR ||
        a->stickHomeX != b->stickHomeX || a->stickHomeY != b->stickHomeY) {
        return "stick";
    }
    if (!same_rect(a->lookArea, b->lookArea) || a->lookR != b->lookR) {
        return "look";
    }
    if (a->faceX != b->faceX || a->faceY != b->faceY || a->dpadX != b->dpadX ||
        a->dpadY != b->dpadY) {
        return "face/dpad centre";
    }
    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        const IcoTouchButton *p = &a->button[z], *q = &b->button[z];

        if (!same_rect(p->rect, q->rect) || p->round != q->round || p->pad != q->pad ||
            p->alpha != q->alpha) {
            snprintf(buf, sizeof(buf), "button %d", z);
            return buf;
        }
    }
    if (a->stickAlpha != b->stickAlpha || a->lookAlpha != b->lookAlpha) {
        return "stickAlpha/lookAlpha";
    }
    if (a->editGen != b->editGen) {
        return "editGen";
    }
    return NULL;
}

static void set_edit(IcoTouchEdits *e, int rule, int ctl, const char *text)
{
    if (ico_touch_edit_parse(&e->ctl[rule][ctl], ctl, text) != 0) {
        fprintf(stderr, "  parse %s \"%s\" failed\n", ico_touch_ctl_name(ctl), text);
        CHECK(0);
    }
}

/* An all-unset IcoTouchEdits, or none, gives ico_touch_layout_env's layout
   exactly (but editGen, which is the edits' generation), at every shape and
   size; edits of the other postures change nothing either. */
static void test_defaults(void)
{
    IcoTouchEdits e;
    int r, sz, p, z;

    for (r = 0; r < SHAPE_COUNT; r++) {
        for (sz = 0; sz < 3; sz++) {
            const Shape *s = &s_shapes[r];
            IcoTouchEnv env;
            IcoTouchLayout base, l;
            const char *d;

            env.pxPerMm = ico_touch_px_per_mm(s->dpi);
            env.fold = s->fold;
            base = ico_touch_layout_env(s->w, s->h, s->insets, sz, &env);
            for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
                CHECK(base.button[z].alpha == 1.0f);
            }
            CHECK(base.stickAlpha == 1.0f && base.lookAlpha == 1.0f && base.editGen == 0);

            l = shape_layout(s, sz, NULL);
            d = layout_diff(&l, &base);
            CHECK(d == NULL && memcmp(&l, &base, sizeof(l)) == 0);

            ico_touch_edits_defaults(&e);
            CHECK(e.snap == 1 && e.grid == 4.0f && e.gen == 0 && !ico_touch_edits_any(&e, -1));
            l = shape_layout(s, sz, &e);
            d = layout_diff(&l, &base);
            if (d != NULL) {
                fprintf(stderr, "  shape %d size %d: %s differs\n", r, sz, d);
            }
            CHECK(d == NULL && memcmp(&l, &base, sizeof(l)) == 0);

            e.gen = 9;
            l = shape_layout(s, sz, &e);
            CHECK(l.editGen == 9);
            l.editGen = 0;
            CHECK(layout_diff(&l, &base) == NULL && memcmp(&l, &base, sizeof(l)) == 0);

            /* the other postures' edits are not this one's */
            for (p = 0; p < ICO_TOUCH_POSTURES; p++) {
                if (p != base.rule) {
                    set_edit(&e, p, ICO_TOUCH_CTL_CROSS, "l30 t30 s1.40 o50");
                    set_edit(&e, p, ICO_TOUCH_CTL_LOOK, "hidden");
                    set_edit(&e, p, ICO_TOUCH_CTL_DPAD, "r40 b30");
                }
            }
            l = shape_layout(s, sz, &e);
            l.editGen = 0;
            d = layout_diff(&l, &base);
            if (d != NULL) {
                fprintf(stderr, "  shape %d size %d, other postures edited: %s differs\n", r, sz,
                        d);
            }
            CHECK(d == NULL);
        }
    }
}

/* the anchors: a Cross "r20 b10.5" is the default place on every screen
   (Rt-20q, B-10.5q); scale, the left and top edges, the look pad's edges */
static void check_cross_at(uint32_t w, uint32_t h, IcoTouchInsets in, float pxPerMm)
{
    IcoTouchEnv env = env_of(pxPerMm, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    IcoTouchLayout base = ico_touch_layout_env(w, h, in, ICO_TOUCH_MEDIUM, &env), l;
    IcoTouchEdits e;
    const IcoTouchRect *R;
    float q;

    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_CROSS, "r20 b10.5");
    l = ico_touch_layout_edits(w, h, in, ICO_TOUCH_MEDIUM, &env, &e);
    R = &l.region;
    q = l.unit;
    CHECK(l.rule == ICO_TOUCH_RULE_FULL && q == base.unit);
    CHECK(near_(cx_of(&l, ICO_TOUCH_B_CROSS), right_of(R) - 20.0f * q, 0.02f));
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_CROSS), bottom_of(R) - 10.5f * q, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.x, base.button[ICO_TOUCH_B_CROSS].rect.x, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.y, base.button[ICO_TOUCH_B_CROSS].rect.y, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.w, 11.0f * q, 0.01f));
    CHECK(l.button[ICO_TOUCH_B_CROSS].round && l.button[ICO_TOUCH_B_CROSS].pad == ICO_PAD_CROSS);
    CHECK(l.button[ICO_TOUCH_B_CROSS].alpha == 1.0f);

    /* scale 1.3: the disc 2 x 5.5 x 1.3 q across, the same centre */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_CROSS, "r20 b10.5 s1.30 o60");
    l = ico_touch_layout_edits(w, h, in, ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.w, 2.0f * 5.5f * 1.3f * q, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.h, 2.0f * 5.5f * 1.3f * q, 0.01f));
    CHECK(near_(cx_of(&l, ICO_TOUCH_B_CROSS), right_of(R) - 20.0f * q, 0.02f));
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_CROSS), bottom_of(R) - 10.5f * q, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].alpha, 0.6f, 1e-4f));
    /* the faces' centre stays the defaults' (the areas come from it) */
    CHECK(l.faceX == base.faceX && l.faceY == base.faceY);

    /* from the left and top edges: Select's centre, its 12 x 8 rect */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_SELECT, "l30 t20");
    l = ico_touch_layout_edits(w, h, in, ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(cx_of(&l, ICO_TOUCH_B_SELECT), R->x + 30.0f * q, 0.02f));
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_SELECT), R->y + 20.0f * q, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.w, 12.0f * q, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.h, 8.0f * q, 0.01f));
    CHECK(!l.button[ICO_TOUCH_B_SELECT].round &&
          l.button[ICO_TOUCH_B_SELECT].pad == ICO_PAD_SELECT);

    /* the look pad by its edges: "r0 t0 w40 h20" is the upper right corner */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_LOOK, "r0 t0 w40 h20 o70");
    l = ico_touch_layout_edits(w, h, in, ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(l.lookArea.x, right_of(R) - 40.0f * q, 0.02f));
    CHECK(near_(l.lookArea.y, R->y, 0.02f));
    CHECK(near_(l.lookArea.w, 40.0f * q, 0.02f) && near_(l.lookArea.h, 20.0f * q, 0.02f));
    CHECK(near_(l.lookAlpha, 0.7f, 1e-4f));
    CHECK(l.lookR == base.lookR);
}

static void test_anchor_math(void)
{
    IcoTouchInsets in = {100.0f, 0.0f, 50.0f, 30.0f};
    IcoTouchEnv env = env_of(420.0f / 25.4f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    IcoTouchEdits e;
    IcoTouchLayout l;
    float q, k;

    check_cross_at(2340, 1080, no_insets(), 420.0f / 25.4f);
    check_cross_at(1812, 2176, no_insets(), 420.0f / 25.4f);
    check_cross_at(2560, 1600, no_insets(), 320.0f / 25.4f);
    check_cross_at(2400, 1080, in, 450.0f / 25.4f);
    check_cross_at(1812, 2176, in, 420.0f / 25.4f);

    /* the D-pad as one cross: its centre and the keys around it */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_DPAD, "l40 t20 s1.20");
    l = ico_touch_layout_edits(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    q = l.unit;
    k = 8.0f * 1.2f * q;
    CHECK(near_(l.dpadX, 40.0f * q, 0.02f) && near_(l.dpadY, 20.0f * q, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_UP].rect.x, l.dpadX - 0.5f * k, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_UP].rect.y, l.dpadY - 1.5f * k, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_DOWN].rect.y, l.dpadY + 0.5f * k, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_LEFT].rect.x, l.dpadX - 1.5f * k, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_RIGHT].rect.x, l.dpadX + 0.5f * k, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_RIGHT].rect.w, k, 0.01f));
    CHECK(l.button[ICO_TOUCH_B_LEFT].pad == ICO_PAD_LEFT && !l.button[ICO_TOUCH_B_LEFT].round);

    /* the stick: home from its anchors, R scaled, the look pad's R not */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_STICK, "l30 b25 s1.50");
    l = ico_touch_layout_edits(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(l.stickHomeX, 30.0f * q, 0.02f) && near_(l.stickHomeY, 1080.0f - 25.0f * q, 0.02f));
    CHECK(near_(l.stickR, 11.0f * 1.5f * q, 0.01f));
    CHECK(near_(l.runR, ICO_TOUCH_RUN_RING * l.stickR, 0.01f));
    CHECK(near_(l.lookR, 11.0f * q, 0.01f));

    /* R1 at 0.7 */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_R1, "r10.5 b43.5 s0.70");
    l = ico_touch_layout_edits(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(l.button[ICO_TOUCH_B_R1].rect.w, 2.0f * 6.5f * 0.7f * q, 0.01f));
    CHECK(near_(cx_of(&l, ICO_TOUCH_B_R1), 2340.0f - 10.5f * q, 0.02f));
    CHECK(l.editGen == e.gen);
}

/* every button, the stick's square and the look pad inside the region */
static void check_inside(const IcoTouchLayout *l)
{
    IcoTouchRect s;
    int z;

    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        CHECK(rect_inside(&l->button[z].rect, &l->region));
    }
    s.x = l->stickHomeX - l->stickR;
    s.y = l->stickHomeY - l->stickR;
    s.w = s.h = 2.0f * l->stickR;
    CHECK(rect_inside(&s, &l->region));
    CHECK(rect_inside(&l->lookArea, &l->region));
    CHECK(rect_inside(&l->stickArea, &l->region));
}

static void test_clamp_region_and_band(void)
{
    const float k420 = 420.0f / 25.4f;
    IcoTouchEnv env = env_of(k420, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    IcoTouchEdits e;
    IcoTouchLayout l;
    float q;

    /* past the edges: inside the region, against them */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_CROSS, "r0 b0");
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_SELECT, "l0 t0 s1.60");
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_STICK, "l0 b0 s1.60");
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_LOOK, "l500 t500 w500 h500");
    l = ico_touch_layout_edits(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    q = l.unit;
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_CROSS].rect), 2340.0f, 0.02f));
    CHECK(near_(bottom_of(&l.button[ICO_TOUCH_B_CROSS].rect), 1080.0f, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.x, 0.0f, 0.02f) &&
          near_(l.button[ICO_TOUCH_B_SELECT].rect.y, 0.0f, 0.02f));
    CHECK(near_(l.stickHomeX, 17.6f * q, 0.02f) && near_(l.stickHomeY, 1080.0f - 17.6f * q, 0.02f));
    /* a look pad larger than the region: the region */
    CHECK(near_(l.lookArea.x, 0.0f, 0.02f) && near_(l.lookArea.w, 2340.0f, 0.02f));
    CHECK(near_(l.lookArea.h, 1080.0f, 0.02f));
    check_inside(&l);
    /* the same edits on other screens: inside on every rebuild */
    l = ico_touch_layout_edits(1812, 2176, no_insets(), ICO_TOUCH_LARGE, &env, &e);
    check_inside(&l);
    l = ico_touch_layout_edits(960, 540, no_insets(), ICO_TOUCH_SMALL, NULL, &e);
    check_inside(&l);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_CROSS, "l500 t500");
    l = ico_touch_layout_edits(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_CROSS].rect), 2340.0f, 0.02f));
    CHECK(near_(bottom_of(&l.button[ICO_TOUCH_B_CROSS].rect), 1080.0f, 0.02f));

    /* SPLIT at 1104 of 2208 (the band 1104 -+ 2 mm): out of the band to the
       nearer side */
    env = env_of(k420, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 1104.0f, 0.0f, 0.0f, 1840.0f);
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_CTL_CROSS, "l60 b20");  /* centre 992 */
    set_edit(&e, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_CTL_CIRCLE, "l72 b20"); /* centre 1190.5 */
    set_edit(&e, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_CTL_STICK, "l50 b21");
    l = ico_touch_layout_edits(2208, 1840, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT && near_(l.band.x, 1104.0f - 2.0f * k420, 0.01f));
    CHECK(near_(right_of(&l.button[ICO_TOUCH_B_CROSS].rect), l.band.x, 0.02f));
    CHECK(near_(l.button[ICO_TOUCH_B_CIRCLE].rect.x, right_of(&l.band), 0.02f));
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_CROSS), 1840.0f - 20.0f * l.unit, 0.02f));
    /* the moved stick's area: 4.4 R around its home, cut to the region and
       to the band's left */
    CHECK(near_(l.stickHomeX, 50.0f * l.unit, 0.02f));
    CHECK(near_(l.stickArea.x, l.stickHomeX - 2.2f * l.stickR, 0.05f));
    CHECK(near_(l.stickArea.y, l.stickHomeY - 2.2f * l.stickR, 0.05f));
    CHECK(near_(right_of(&l.stickArea), l.band.x, 0.02f));
    CHECK(near_(bottom_of(&l.stickArea), 1840.0f, 0.02f));
    check_inside(&l);

    /* the smallest SPLIT (58 q left of the band, 40 right, q 10): a look pad
       wider than either side shrinks to the wider one */
    env = env_of(10.0f, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 600.0f, 0.0f, 0.0f, 640.0f);
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_CTL_LOOK, "l0 t0 w100 h30");
    l = ico_touch_layout_edits(1020, 640, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT && l.unit == 10.0f);
    CHECK(near_(l.band.x, 580.0f, 0.01f) && near_(right_of(&l.band), 620.0f, 0.01f));
    CHECK(near_(l.lookArea.x, 0.0f, 0.01f) && near_(l.lookArea.w, 580.0f, 0.01f));
    CHECK(near_(l.lookArea.y, 0.0f, 0.01f) && near_(l.lookArea.h, 300.0f, 0.01f));

    /* TABLETOP: the region starts below the band; a "t0" is its top */
    env = env_of(k420, ICO_TOUCH_FOLD_HORIZONTAL, 1, 0, 0.0f, 906.0f, 2176.0f, 0.0f);
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_CTL_CROSS, "r20 t0");
    l = ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP);
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.y, l.region.y, 0.02f));
    CHECK(l.button[ICO_TOUCH_B_CROSS].rect.y >= bottom_of(&l.band) - 0.01f);
    check_inside(&l);
}

static IcoVirtualPad finger_at(const IcoTouchLayout *l, float x, float y, float mx, float my)
{
    IcoTouchState t;
    IcoVirtualPad v;

    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, 1000000000ull);
    ico_touch_event(&t, 1, x / (float)l->outW, y / (float)l->outH, ICO_TOUCH_DOWN, 1000000000ull);
    ico_touch_event(&t, 1, (x + mx) / (float)l->outW, (y + my) / (float)l->outH, ICO_TOUCH_MOVE,
                    1000000000ull);
    ico_touch_step(&t, l, &v);
    return v;
}

static void test_hidden(void)
{
    static const int never[] = {ICO_TOUCH_CTL_DPAD,   ICO_TOUCH_CTL_CROSS,    ICO_TOUCH_CTL_CIRCLE,
                                ICO_TOUCH_CTL_SQUARE, ICO_TOUCH_CTL_TRIANGLE, ICO_TOUCH_CTL_R1,
                                ICO_TOUCH_CTL_START,  ICO_TOUCH_CTL_STICK};
    static const int may[] = {ICO_TOUCH_CTL_SELECT, ICO_TOUCH_CTL_L1, ICO_TOUCH_CTL_L2,
                              ICO_TOUCH_CTL_R2, ICO_TOUCH_CTL_LOOK};
    volatile float zero = 0.0f;
    IcoTouchEdits e;
    IcoTouchCtlEdit c;
    IcoTouchLayout l, base = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoVirtualPad v;
    int i;

    /* the rails: the protected eight cannot be hidden, the other five can */
    for (i = 0; i < (int)(sizeof(never) / sizeof(never[0])); i++) {
        memset(&c, 0x5A, sizeof(c));
        CHECK(!ico_touch_ctl_hideable(never[i]));
        CHECK(ico_touch_edit_parse(&c, never[i], "hidden") == -1 && c.set == 0);
    }
    for (i = 0; i < (int)(sizeof(may) / sizeof(may[0])); i++) {
        CHECK(ico_touch_ctl_hideable(may[i]));
        CHECK(ico_touch_edit_parse(&c, may[i], "hidden") == 0 && c.set && c.alpha == 0.0f);
        CHECK(c.ax == ICO_TOUCH_ANCHOR_DEFAULT && c.ay == ICO_TOUCH_ANCHOR_DEFAULT);
    }

    /* sanitize */
    memset(&c, 0, sizeof(c));
    c.set = 1;
    c.alpha = 0.1f;
    c.scale = 3.0f;
    c.dx = -3.0f;
    c.ax = 7;
    c.w = 30.0f;
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_CROSS);
    CHECK(c.alpha == ICO_TOUCH_EDIT_ALPHA_MIN && c.scale == ICO_TOUCH_EDIT_SCALE_MAX);
    CHECK(c.dx == 0.0f && c.ax == ICO_TOUCH_ANCHOR_DEFAULT && c.w == 0.0f);
    c.alpha = 0.0f;
    c.scale = 0.1f;
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_CROSS);
    CHECK(c.alpha == 0.25f && c.scale == ICO_TOUCH_EDIT_SCALE_MIN);
    c.alpha = 0.0f;
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_SELECT);
    CHECK(c.alpha == 0.0f);
    c.alpha = 1.5f;
    c.scale = zero / zero; /* not a number */
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_SELECT);
    CHECK(c.alpha == 1.0f && c.scale == 1.0f);
    c.w = 5.0f;
    c.h = 0.0f;
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_LOOK);
    CHECK(c.w == ICO_TOUCH_EDIT_LOOK_MIN && c.h == 0.0f);
    memset(&c, 0, sizeof(c));
    c.alpha = 0.0f; /* unset: left alone */
    ico_touch_edit_sanitize(&c, ICO_TOUCH_CTL_CROSS);
    CHECK(c.set == 0 && c.alpha == 0.0f);

    /* L2 hidden: drawn nowhere, its spot presses nothing; the rest as before */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_L2, "hidden");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    CHECK(l.button[ICO_TOUCH_B_L2].alpha == 0.0f);
    CHECK(same_rect(l.button[ICO_TOUCH_B_L2].rect, base.button[ICO_TOUCH_B_L2].rect));
    v = finger_at(&base, cx_of(&base, ICO_TOUCH_B_L2), cy_of(&base, ICO_TOUCH_B_L2), 0, 0);
    CHECK(v.buttons == ICO_PAD_L2);
    v = finger_at(&l, cx_of(&l, ICO_TOUCH_B_L2), cy_of(&l, ICO_TOUCH_B_L2), 0, 0);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.rx == 0.0f);
    l.button[ICO_TOUCH_B_L2].alpha = 1.0f;
    l.editGen = 0;
    CHECK(layout_diff(&l, &base) == NULL);

    /* the look pad hidden: a finger on it does nothing */
    v = finger_at(&base, 1100.0f, 300.0f, base.lookR, 0.0f);
    CHECK(near_(v.rx, 1.0f, 0.002f) && v.buttons == 0);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_LOOK, "hidden");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    CHECK(l.lookAlpha == 0.0f && same_rect(l.lookArea, base.lookArea));
    v = finger_at(&l, 1100.0f, 300.0f, l.lookR, 0.0f);
    CHECK(v.rx == 0.0f && v.ry == 0.0f && v.buttons == 0);

    /* the step skips a stick area at alpha 0 (the rails never give one; the
       layout's contract still holds) */
    l = base;
    v = finger_at(&l, l.stickHomeX, l.stickHomeY, 0.5f * l.stickR, 0.0f);
    CHECK(near_(v.lx, 0.5f, 0.002f));
    l.stickAlpha = 0.0f;
    v = finger_at(&l, l.stickHomeX, l.stickHomeY, 0.5f * l.stickR, 0.0f);
    CHECK(v.lx == 0.0f && v.ly == 0.0f);
}

static void test_stick_look_areas(void)
{
    IcoTouchLayout base = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM), l;
    IcoTouchEdits e;
    IcoVirtualPad v;
    const float q = base.unit;
    float a;

    /* a moved stick: its area a 4.4 R square around its home, cut to the
       region; a finger there is the stick, deflection over the new R */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_STICK, "l40 b25 s1.50");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    a = 2.2f * l.stickR;
    CHECK(near_(l.stickR, 16.5f * q, 0.01f) && near_(l.lookR, base.lookR, 1e-4f));
    CHECK(near_(l.stickHomeX, 40.0f * q, 0.02f) && near_(l.stickHomeY, 1080.0f - 25.0f * q, 0.02f));
    CHECK(near_(l.stickArea.x, l.stickHomeX - a, 0.05f));
    CHECK(near_(l.stickArea.y, l.stickHomeY - a, 0.05f));
    CHECK(near_(right_of(&l.stickArea), l.stickHomeX + a, 0.05f));
    CHECK(near_(bottom_of(&l.stickArea), 1080.0f, 0.02f));
    v = finger_at(&l, l.stickHomeX + 0.3f * l.stickR, l.stickHomeY, 0.5f * l.stickR, 0.0f);
    CHECK(near_(v.lx, 0.5f, 0.002f) && v.buttons == 0);
    /* the look pad untouched by the stick's edit */
    CHECK(same_rect(l.lookArea, base.lookArea));
    /* a stick still at its default home (a size or opacity change only, the
       editor's anchors for that spot): place()'s wide catch area stays */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_STICK, "l24 b21 s1.50 o60");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    CHECK(near_(l.stickR, 16.5f * q, 0.01f) && l.stickAlpha == 0.6f);
    CHECK(same_rect(l.stickArea, base.stickArea));
    v = finger_at(&l, base.stickArea.x + 2.0f * q, base.stickArea.y + 2.0f * q, 0.5f * l.stickR,
                  0.0f);
    CHECK(near_(v.lx, 0.5f, 0.002f));

    /* a look pad of its own: (65q, 40q) is in no area by default, the look
       pad's once it is moved there */
    v = finger_at(&base, 65.0f * q, 40.0f * q, base.lookR, 0.0f);
    CHECK(v.rx == 0.0f && v.lx == 0.0f && v.buttons == 0);
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_LOOK, "l50 t30 w30 h20");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    CHECK(near_(l.lookArea.x, 50.0f * q, 0.02f) && near_(l.lookArea.y, 30.0f * q, 0.02f));
    CHECK(near_(l.lookArea.w, 30.0f * q, 0.02f) && near_(l.lookArea.h, 20.0f * q, 0.02f));
    CHECK(same_rect(l.stickArea, base.stickArea));
    v = finger_at(&l, 65.0f * q, 40.0f * q, l.lookR, 0.0f);
    CHECK(near_(v.rx, 1.0f, 0.002f) && v.lx == 0.0f && v.buttons == 0);
    /* the buttons still win over the areas: Square inside a look pad */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_LOOK, "r0 b0 w60 h40");
    l = ico_touch_layout_edits(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM, NULL, &e);
    v = finger_at(&l, cx_of(&l, ICO_TOUCH_B_SQUARE), cy_of(&l, ICO_TOUCH_B_SQUARE), 0.0f, 0.0f);
    CHECK(v.buttons == ICO_PAD_SQUARE && v.rx == 0.0f);
}

/* the flat crease's check sees the edited positions both ways */
static void test_flat_crease(void)
{
    IcoTouchEnv env =
        env_of(420.0f / 25.4f, ICO_TOUCH_FOLD_HORIZONTAL, 0, 0, 0.0f, 906.0f, 2176.0f, 0.0f);
    IcoTouchEdits e;
    IcoTouchLayout l;

    ico_touch_edits_defaults(&e);
    l = ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_FULL);
    /* Cross put on the crease: below it instead, with TABLETOP's own edits */
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_CROSS, "r20 t55");
    set_edit(&e, ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_CTL_CROSS, "r30 b10.5");
    l = ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP);
    CHECK(near_(cx_of(&l, ICO_TOUCH_B_CROSS), 2176.0f - 30.0f * l.unit, 0.02f));
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_CROSS), 1812.0f - 10.5f * l.unit, 0.02f));
    /* a hidden control on the crease does not count; a shown one does */
    ico_touch_edits_defaults(&e);
    {
        IcoTouchCtlEdit *c = &e.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_L2];

        c->set = 1;
        c->ax = ICO_TOUCH_ANCHOR_L;
        c->dx = 9.0f;
        c->ay = ICO_TOUCH_ANCHOR_T;
        c->dy = 55.0f;
        c->scale = 1.0f;
        c->alpha = 0.0f;
        CHECK(ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &env, &e).rule ==
              ICO_TOUCH_RULE_FULL);
        c->alpha = 0.5f;
        CHECK(ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_MEDIUM, &env, &e).rule ==
              ICO_TOUCH_RULE_TABLETOP);
    }
    /* Large puts R1 on the crease by default; moved off it, FULL stays */
    ico_touch_edits_defaults(&e);
    CHECK(ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_LARGE, &env, &e).rule ==
          ICO_TOUCH_RULE_TABLETOP);
    set_edit(&e, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_R1, "r10.5 t20");
    l = ico_touch_layout_edits(2176, 1812, no_insets(), ICO_TOUCH_LARGE, &env, &e);
    CHECK(l.rule == ICO_TOUCH_RULE_FULL);
    CHECK(near_(cy_of(&l, ICO_TOUCH_B_R1), 20.0f * l.unit, 0.02f));
}

static void check_round_trip(int ctl, const char *text)
{
    IcoTouchCtlEdit c;
    char buf[64];

    CHECK(ico_touch_edit_parse(&c, ctl, text) == 0);
    ico_touch_edit_text(&c, buf, sizeof(buf));
    if (strcmp(buf, text) != 0) {
        fprintf(stderr, "  %s: \"%s\" came back \"%s\"\n", ico_touch_ctl_name(ctl), text, buf);
        CHECK(0);
    }
}

static void check_rejected(int ctl, const char *text)
{
    IcoTouchCtlEdit c;

    memset(&c, 0x5A, sizeof(c));
    if (ico_touch_edit_parse(&c, ctl, text) != -1 || c.set != 0) {
        fprintf(stderr, "  %s: \"%s\" was not rejected\n", ico_touch_ctl_name(ctl), text);
        CHECK(0);
    }
}

static void test_text_round_trip(void)
{
    IcoTouchEdits e;
    IcoTouchCtlEdit c, d;
    char buf[64], buf2[64];
    int p, k;

    /* names */
    for (k = 0; k < ICO_TOUCH_CTL_COUNT; k++) {
        CHECK(ico_touch_ctl_name(k) != NULL);
        CHECK(ico_touch_ctl_from_name(ico_touch_ctl_name(k)) == k);
    }
    CHECK(ico_touch_ctl_name(ICO_TOUCH_CTL_COUNT) == NULL && ico_touch_ctl_name(-1) == NULL);
    CHECK(ico_touch_ctl_from_name("nope") == -1 && ico_touch_ctl_from_name(NULL) == -1);
    CHECK(strcmp(ico_touch_ctl_name(ICO_TOUCH_CTL_DPAD), "dpad") == 0);
    CHECK(strcmp(ico_touch_ctl_name(ICO_TOUCH_CTL_LOOK), "look") == 0);
    for (p = 0; p < ICO_TOUCH_POSTURES; p++) {
        CHECK(ico_touch_posture_from_name(ico_touch_posture_name(p)) == p);
    }
    CHECK(strcmp(ico_touch_posture_name(ICO_TOUCH_RULE_TABLETOP), "tabletop") == 0);
    CHECK(ico_touch_posture_name(3) == NULL && ico_touch_posture_from_name("flat") == -1);

    /* every control in every posture: text -> parse -> the same edit and text */
    for (p = 0; p < ICO_TOUCH_POSTURES; p++) {
        for (k = 0; k < ICO_TOUCH_CTL_COUNT; k++) {
            memset(&c, 0, sizeof(c));
            c.set = 1;
            c.ax = (unsigned char)((k + p) & 1);
            c.ay = (unsigned char)((k >> 1) & 1);
            c.dx = 3.25f + (float)k;
            c.dy = 7.5f + (float)p;
            c.scale = 0.7f + 0.05f * (float)k;
            c.alpha = (float)(40 + 4 * k) / 100.0f;
            if (k == ICO_TOUCH_CTL_LOOK) {
                c.w = 30.5f + (float)p;
                c.h = 20.0f + (float)k;
            }
            ico_touch_edit_text(&c, buf, sizeof(buf));
            memset(&d, 0x5A, sizeof(d));
            CHECK(ico_touch_edit_parse(&d, k, buf) == 0);
            CHECK(d.set == 1 && d.ax == c.ax && d.ay == c.ay);
            CHECK(near_(d.dx, c.dx, 0.006f) && near_(d.dy, c.dy, 0.006f));
            CHECK(near_(d.scale, c.scale, 0.006f) && near_(d.alpha, c.alpha, 0.006f));
            CHECK(near_(d.w, c.w, 0.006f) && near_(d.h, c.h, 0.006f));
            ico_touch_edit_text(&d, buf2, sizeof(buf2));
            if (strcmp(buf, buf2) != 0) {
                fprintf(stderr, "  posture %d %s: \"%s\" then \"%s\"\n", p, ico_touch_ctl_name(k),
                        buf, buf2);
                CHECK(0);
            }
        }
    }

    /* the documented forms */
    check_round_trip(ICO_TOUCH_CTL_CROSS, "r20 b10.5 s1.00 o100");
    check_round_trip(ICO_TOUCH_CTL_LOOK, "l50 t0 w60 h30 o100");
    check_round_trip(ICO_TOUCH_CTL_LOOK, "l50 t0 w60 h30 s1.20 o80");
    check_round_trip(ICO_TOUCH_CTL_DPAD, "l12.25 t4 s0.75 o40");
    check_round_trip(ICO_TOUCH_CTL_L2, "hidden");
    check_round_trip(ICO_TOUCH_CTL_LOOK, "hidden");
    /* the optional tokens: s 1 and o 100 when absent; any spacing */
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, "  r20\tb10.5  ") == 0);
    CHECK(c.set && c.ax == ICO_TOUCH_ANCHOR_R && c.ay == ICO_TOUCH_ANCHOR_B && c.scale == 1.0f &&
          c.alpha == 1.0f && near_(c.dy, 10.5f, 1e-5f));
    CHECK(strcmp(ico_touch_edit_text(&c, buf, sizeof(buf)), "r20 b10.5 s1.00 o100") == 0);
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, "b10 o50 l3.5 s1.25") == 0);
    CHECK(strcmp(ico_touch_edit_text(&c, buf, sizeof(buf)), "l3.5 b10 s1.25 o50") == 0);
    /* "default" is unset; an opacity under the floor is the floor */
    memset(&c, 0x5A, sizeof(c));
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, "default") == 0 && c.set == 0);
    CHECK(strcmp(ico_touch_edit_text(&c, buf, sizeof(buf)), "default") == 0);
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, "r20 b10 o10") == 0 && c.alpha == 0.25f);
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_SELECT, "r20 b10 o10") == 0 &&
          near_(c.alpha, 0.1f, 1e-6f));
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_SELECT, "r20 b10 o0") == 0 && c.alpha == 0.0f);
    CHECK(strcmp(ico_touch_edit_text(&c, buf, sizeof(buf)), "r20 b10 s1.00 o0") ==
          0); /* place kept */
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_SELECT, "hidden") == 0 && c.alpha == 0.0f);
    CHECK(strcmp(ico_touch_edit_text(&c, buf, sizeof(buf)), "hidden") == 0);

    /* bad tokens, out of range, twice, missing anchors */
    check_rejected(ICO_TOUCH_CTL_CROSS, "");
    check_rejected(ICO_TOUCH_CTL_CROSS, "   ");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20");
    check_rejected(ICO_TOUCH_CTL_CROSS, "b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "s1.2 o50");
    check_rejected(ICO_TOUCH_CTL_CROSS, "x5 b2");
    check_rejected(ICO_TOUCH_CTL_CROSS, "R20 b2");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 junk");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r-5 b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r1e3 b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20,5 b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r. b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 l5 b10");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 t5");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 s1 s1");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 s1.70");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 s0.60");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 o101");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r501 b0");
    check_rejected(ICO_TOUCH_CTL_CROSS, "r20 b10 w30 h30");
    check_rejected(ICO_TOUCH_CTL_CROSS, "hidden");
    check_rejected(ICO_TOUCH_CTL_L2, "hidden r20");
    check_rejected(ICO_TOUCH_CTL_L2, "default hidden");
    check_rejected(ICO_TOUCH_CTL_LOOK, "l0 t0 w10 h30");
    check_rejected(ICO_TOUCH_CTL_LOOK, "l0 t0 w600 h30");
    check_rejected(ICO_TOUCH_CTL_LOOK, "l0 t0 w30 w30");
    check_rejected(-1, "r20 b10");
    check_rejected(ICO_TOUCH_CTL_COUNT, "r20 b10");
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, NULL) == -1 && c.set == 0);

    /* a short buffer: cut, still a string */
    CHECK(ico_touch_edit_parse(&c, ICO_TOUCH_CTL_CROSS, "r20 b10.5") == 0);
    ico_touch_edit_text(&c, buf, 4);
    CHECK(strlen(buf) == 3 && strncmp(buf, "r20", 3) == 0);

    /* any and reset */
    ico_touch_edits_defaults(&e);
    set_edit(&e, ICO_TOUCH_RULE_SPLIT, ICO_TOUCH_CTL_R1, "r10 t20");
    CHECK(ico_touch_edits_any(&e, -1) && ico_touch_edits_any(&e, ICO_TOUCH_RULE_SPLIT));
    CHECK(!ico_touch_edits_any(&e, ICO_TOUCH_RULE_FULL));
    e.snap = 0;
    e.grid = 8.0f;
    ico_touch_edits_reset(&e, ICO_TOUCH_RULE_FULL);
    CHECK(ico_touch_edits_any(&e, ICO_TOUCH_RULE_SPLIT) && e.gen == 1);
    ico_touch_edits_reset(&e, ICO_TOUCH_RULE_SPLIT);
    CHECK(!ico_touch_edits_any(&e, -1) && e.gen == 2 && e.snap == 0 && e.grid == 8.0f);
    set_edit(&e, ICO_TOUCH_RULE_TABLETOP, ICO_TOUCH_CTL_L1, "l3 t3");
    ico_touch_edits_reset(&e, -1);
    CHECK(!ico_touch_edits_any(&e, -1) && e.gen == 3);
}

/* --- the editor ----------------------------------------------------------- */

static uint32_t g_w = 1920, g_h = 1080;
static const IcoTouchEnv *g_env;
static IcoTouchLayout g_l;

/* the preview the device layer would build from the work */
static void preview(const IcoTouchEditor *ed)
{
    g_l = ico_touch_layout_edits(g_w, g_h, no_insets(), ICO_TOUCH_MEDIUM, g_env, &ed->work);
}

static int etick(IcoTouchEditor *ed, unsigned int pad)
{
    int r;

    preview(ed);
    r = ico_touch_edit_tick(ed, &g_l, pad);
    preview(ed);
    return r;
}

/* a finger event in output pixels */
static void fe(IcoTouchEditor *ed, uint64_t id, float x, float y, int kind)
{
    ico_touch_edit_event(ed, id, x / (float)g_w, y / (float)g_h, kind, 0);
}

static void tap(IcoTouchEditor *ed, float x, float y)
{
    fe(ed, 77, x, y, ICO_TOUCH_DOWN);
    fe(ed, 77, x, y, ICO_TOUCH_UP);
}

static void tap_key(IcoTouchEditor *ed, int key)
{
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT];

    ico_touch_edit_toolbar(&g_l, tb, cf);
    tap(ed, tb[key].x + 0.5f * tb[key].w, tb[key].y + 0.5f * tb[key].h);
    etick(ed, 0);
}

/* one size step the gamepad's way (R2 up, L2 down: the strip has no size
   keys), pressed and released */
static void size_step(IcoTouchEditor *ed, int dir)
{
    etick(ed, dir > 0 ? ICO_PAD_R2 : ICO_PAD_L2);
    etick(ed, 0);
}

static void reset_screen(uint32_t w, uint32_t h, const IcoTouchEnv *env)
{
    g_w = w;
    g_h = h;
    g_env = env;
}

static void test_snap_and_drop(void)
{
    IcoTouchEdits live;
    IcoTouchEditor ed;
    IcoTouchLayout base;
    IcoTouchCtlEdit *cross, *sel;
    float q, x, y, sx, sy, tx, ty;
    int i;

    reset_screen(1920, 1080, NULL);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    CHECK(ed.active && ed.selected == -1 && ed.state == ICO_TOUCH_ED_IDLE);
    CHECK(etick(&ed, 0) == ICO_TOUCH_ED_NONE);
    CHECK(ed.overlap == 0 && ed.undos == 0 && !ed.changed);
    base = g_l;
    q = base.unit;
    cross = &ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_CROSS];
    sel = &ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_SELECT];

    /* a tap selects, changes nothing */
    x = cx_of(&base, ICO_TOUCH_B_CROSS);
    y = cy_of(&base, ICO_TOUCH_B_CROSS);
    tap(&ed, x, y);
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_CROSS && !cross->set && ed.undos == 0);
    /* a 1 mm move is a tap */
    sx = cx_of(&base, ICO_TOUCH_B_SELECT);
    sy = cy_of(&base, ICO_TOUCH_B_SELECT);
    fe(&ed, 1, sx, sy, ICO_TOUCH_DOWN);
    fe(&ed, 1, sx + q, sy, ICO_TOUCH_MOVE);
    fe(&ed, 1, sx + q, sy, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_SELECT && !sel->set && ed.undos == 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE);
    /* empty screen deselects */
    tap(&ed, 1000.0f, 900.0f);
    etick(&ed, 0);
    CHECK(ed.selected == -1);

    /* a drag: follows the finger, snaps to the 4 q grid from the nearer
       edges on release (r 26.3 -> 28, b 13.65 -> 12) */
    fe(&ed, 2, x, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_CROSS);
    fe(&ed, 2, x - 100.0f, y - 50.0f, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_DRAG && ed.undos == 1 && cross->set);
    CHECK(near_(cx_of(&g_l, ICO_TOUCH_B_CROSS), x - 100.0f, 0.05f));
    CHECK(near_(cy_of(&g_l, ICO_TOUCH_B_CROSS), y - 50.0f, 0.05f));
    fe(&ed, 2, x - 100.0f, y - 50.0f, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && ed.undos == 1 && ed.refused == 0);
    CHECK(cross->ax == ICO_TOUCH_ANCHOR_R && near_(cross->dx, 28.0f, 1e-4f));
    CHECK(cross->ay == ICO_TOUCH_ANCHOR_B && near_(cross->dy, 12.0f, 1e-4f));
    CHECK(cross->scale == 1.0f && cross->alpha == 1.0f);
    CHECK(near_(cx_of(&g_l, ICO_TOUCH_B_CROSS), 1920.0f - 28.0f * q, 0.02f));
    CHECK(near_(cy_of(&g_l, ICO_TOUCH_B_CROSS), 1080.0f - 12.0f * q, 0.02f));
    CHECK(ed.changed);
    /* there it overlaps Square (amber) without covering its centre */
    etick(&ed, 0);
    CHECK((ed.overlap & (1u << ICO_TOUCH_CTL_CROSS)) &&
          (ed.overlap & (1u << ICO_TOUCH_CTL_SQUARE)));

    /* a drop on Start's centre springs back, the note shows for 60 ticks */
    tx = cx_of(&base, ICO_TOUCH_B_START);
    ty = cy_of(&base, ICO_TOUCH_B_START);
    fe(&ed, 3, sx, sy, ICO_TOUCH_DOWN);
    fe(&ed, 3, tx, ty, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_DRAG && ed.undos == 2 && sel->set);
    fe(&ed, 3, tx, ty, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.refused == ICO_TOUCH_EDIT_REFUSED && ed.undos == 1 && !sel->set);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && ed.selected == ICO_TOUCH_CTL_SELECT);
    CHECK(same_rect(g_l.button[ICO_TOUCH_B_SELECT].rect, base.button[ICO_TOUCH_B_SELECT].rect));
    CHECK(cross->set && near_(cross->dx, 28.0f, 1e-4f)); /* the earlier drop kept */
    for (i = 0; i < ICO_TOUCH_EDIT_REFUSED; i++) {
        etick(&ed, 0);
    }
    CHECK(ed.refused == 0);

    /* L1 onto the D-pad's left key but not its centre: allowed, both amber */
    x = cx_of(&base, ICO_TOUCH_B_L1);
    y = cy_of(&base, ICO_TOUCH_B_L1);
    fe(&ed, 4, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 4, x + 4.0f * q, y, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    fe(&ed, 4, x + 4.0f * q, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    etick(&ed, 0);
    CHECK(ed.refused == 0);
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_L1].ax == ICO_TOUCH_ANCHOR_L &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_L1].dx, 12.0f, 1e-4f));
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_L1].ay == ICO_TOUCH_ANCHOR_T &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_L1].dy, 16.0f, 1e-4f));
    CHECK((ed.overlap & (1u << ICO_TOUCH_CTL_DPAD)) && (ed.overlap & (1u << ICO_TOUCH_CTL_L1)));
    CHECK(!(ed.overlap & (1u << ICO_TOUCH_CTL_L2)) && !(ed.overlap & (1u << ICO_TOUCH_CTL_R1)));

    /* with Snap off a drop stays where it was let go */
    tap_key(&ed, ICO_TOUCH_ED_TB_SNAP);
    CHECK(ed.work.snap == 0);
    x = cx_of(&g_l, ICO_TOUCH_B_R2);
    y = cy_of(&g_l, ICO_TOUCH_B_R2);
    fe(&ed, 5, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 5, x - 3.3f * q, y + 0.7f * q, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    fe(&ed, 5, x - 3.3f * q, y + 0.7f * q, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_R2].ax == ICO_TOUCH_ANCHOR_R &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_R2].dx, 12.3f, 0.01f));
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_R2].ay == ICO_TOUCH_ANCHOR_T &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_R2].dy, 7.7f, 0.01f));

    /* a cancel mid-drag puts the control back, keeps the rest */
    x = cx_of(&g_l, ICO_TOUCH_B_TRIANGLE);
    y = cy_of(&g_l, ICO_TOUCH_B_TRIANGLE);
    fe(&ed, 6, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 6, x - 10.0f * q, y, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_DRAG && ed.work.ctl[0][ICO_TOUCH_CTL_TRIANGLE].set);
    ico_touch_edit_event(&ed, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, 0);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && !ed.work.ctl[0][ICO_TOUCH_CTL_TRIANGLE].set);
    CHECK(cross->set && ed.work.ctl[0][ICO_TOUCH_CTL_R2].set);
    fe(&ed, 6, x, y, ICO_TOUCH_UP); /* the cancelled finger's lift: nothing */
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && !ed.work.ctl[0][ICO_TOUCH_CTL_TRIANGLE].set);
    ico_touch_edit_end(&ed, 0);
}

static void test_pinch_and_steppers(void)
{
    IcoTouchEdits live;
    IcoTouchEditor ed;
    IcoTouchEditView v;
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT];
    IcoTouchCtlEdit *cross, *tri;
    float q, x, y;
    int i;

    reset_screen(1920, 1080, NULL);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    q = g_l.unit;
    cross = &ed.work.ctl[0][ICO_TOUCH_CTL_CROSS];
    x = cx_of(&g_l, ICO_TOUCH_B_CROSS);
    y = cy_of(&g_l, ICO_TOUCH_B_CROSS);

    /* pinch: 100 px apart, then 140: 1.0 -> 1.4 */
    fe(&ed, 1, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 2, x + 100.0f, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_PINCH && ed.selected == ICO_TOUCH_CTL_CROSS && ed.undos == 1);
    CHECK(cross->set && cross->scale == 1.0f);
    fe(&ed, 2, x + 140.0f, y, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(near_(cross->scale, 1.4f, 1e-3f));
    fe(&ed, 2, x + 140.0f, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && near_(cross->scale, 1.4f, 1e-5f));
    fe(&ed, 1, x, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(near_(g_l.button[ICO_TOUCH_B_CROSS].rect.w, 2.0f * 5.5f * 1.4f * q, 0.01f));
    CHECK(near_(cx_of(&g_l, ICO_TOUCH_B_CROSS), x, 0.05f) &&
          near_(cy_of(&g_l, ICO_TOUCH_B_CROSS), y, 0.05f));
    /* snapped to 0.05 on release: 1.4 x 1.17 = 1.638 -> clamp 1.6 */
    fe(&ed, 1, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 2, x + 100.0f, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    fe(&ed, 2, x + 300.0f, y, ICO_TOUCH_MOVE);
    fe(&ed, 2, x + 300.0f, y, ICO_TOUCH_UP);
    fe(&ed, 1, x, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(cross->scale == ICO_TOUCH_EDIT_SCALE_MAX && ed.undos == 2);
    fe(&ed, 1, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 2, x + 100.0f, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    fe(&ed, 2, x + 92.0f, y, ICO_TOUCH_MOVE); /* 1.6 x 0.92 = 1.472 -> 1.45 */
    fe(&ed, 2, x + 92.0f, y, ICO_TOUCH_UP);
    fe(&ed, 1, x, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(near_(cross->scale, 1.45f, 1e-5f) && ed.undos == 3);

    /* the size steps (the gamepad's R2/L2): 0.1 within 0.7..1.6, a step
       that changes nothing pushes no undo */
    size_step(&ed, 1);
    CHECK(near_(cross->scale, 1.55f, 1e-5f) && ed.undos == 4);
    size_step(&ed, 1);
    CHECK(near_(cross->scale, 1.6f, 1e-5f) && ed.undos == 5);
    size_step(&ed, 1);
    CHECK(near_(cross->scale, 1.6f, 1e-5f) && ed.undos == 5);
    size_step(&ed, -1);
    CHECK(near_(cross->scale, 1.5f, 1e-5f) && ed.selected == ICO_TOUCH_CTL_CROSS);

    /* opacity: 10 % steps, Cross stops at 25 % */
    for (i = 0; i < 10; i++) {
        tap_key(&ed, ICO_TOUCH_ED_TB_OP_DN);
    }
    CHECK(cross->alpha == 0.25f);
    tap_key(&ed, ICO_TOUCH_ED_TB_OP_UP);
    CHECK(near_(cross->alpha, 0.35f, 1e-5f));
    CHECK(near_(g_l.button[ICO_TOUCH_B_CROSS].alpha, 0.35f, 1e-5f));
    ico_touch_edit_view(&ed, &g_l, &v);
    CHECK(v.active && v.rule == ICO_TOUCH_RULE_FULL && v.selected == ICO_TOUCH_CTL_CROSS);
    CHECK(near_(v.selScale, 1.5f, 1e-5f) && near_(v.selAlpha, 0.35f, 1e-5f));
    CHECK(v.snap == 1 && v.grid == 4.0f && v.hidden == 0 && v.undos == ed.undos && v.changed);
    ico_touch_edit_toolbar(&g_l, tb, cf);
    CHECK(same_rect(v.tb[0], tb[0]) &&
          same_rect(v.tb[ICO_TOUCH_ED_TB_COUNT - 1], tb[ICO_TOUCH_ED_TB_COUNT - 1]) &&
          same_rect(v.confirm[2], cf[2]));
    CHECK(same_rect(v.ctl[ICO_TOUCH_CTL_LOOK], g_l.lookArea));
    CHECK(same_rect(v.ctl[ICO_TOUCH_CTL_SELECT], g_l.button[ICO_TOUCH_B_SELECT].rect));
    CHECK(near_(v.ctl[ICO_TOUCH_CTL_DPAD].w, 24.0f * q, 0.01f));
    CHECK(near_(v.ctl[ICO_TOUCH_CTL_STICK].w, 2.0f * g_l.stickR, 0.01f));

    /* Select reaches Hidden */
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_SELECT), cy_of(&g_l, ICO_TOUCH_B_SELECT));
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_SELECT);
    for (i = 0; i < 10; i++) {
        tap_key(&ed, ICO_TOUCH_ED_TB_OP_DN);
    }
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_SELECT].alpha == 0.0f);
    CHECK(g_l.button[ICO_TOUCH_B_SELECT].alpha == 0.0f);
    ico_touch_edit_view(&ed, &g_l, &v);
    CHECK(v.hidden == (1u << ICO_TOUCH_CTL_SELECT) && v.selAlpha == 0.0f);
    /* hidden, it is still there to select and bring back */
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_SELECT), cy_of(&g_l, ICO_TOUCH_B_SELECT));
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_SELECT);

    /* All: the size and opacity keys act on every control */
    tap_key(&ed, ICO_TOUCH_ED_TB_ALL);
    CHECK(ed.all == 1);
    size_step(&ed, -1);
    CHECK(near_(cross->scale, 1.4f, 1e-5f));
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_L1].set &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_L1].scale, 0.9f, 1e-5f));
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_LOOK].set &&
          near_(ed.work.ctl[0][ICO_TOUCH_CTL_LOOK].scale, 0.9f, 1e-5f));
    CHECK(near_(ed.work.ctl[0][ICO_TOUCH_CTL_SELECT].scale, 0.9f, 1e-5f));
    tap_key(&ed, ICO_TOUCH_ED_TB_OP_UP);
    CHECK(near_(ed.work.ctl[0][ICO_TOUCH_CTL_SELECT].alpha, 0.1f, 1e-5f));
    CHECK(near_(cross->alpha, 0.45f, 1e-5f));
    CHECK(ed.work.ctl[0][ICO_TOUCH_CTL_STICK].alpha == 1.0f);
    for (i = 0; i < 12; i++) {
        tap_key(&ed, ICO_TOUCH_ED_TB_OP_DN);
    }
    for (i = 0; i < ICO_TOUCH_CTL_COUNT; i++) {
        CHECK(ed.work.ctl[0][i].alpha == (ico_touch_ctl_hideable(i) ? 0.0f : 0.25f));
    }
    tap_key(&ed, ICO_TOUCH_ED_TB_ALL);
    CHECK(ed.all == 0);
    ico_touch_edit_end(&ed, 0);

    /* a control the file hid ("hidden": no place of its own) brought back
       with Opacity +: it gains its default place and writes it with the
       new opacity, not "default" (which would reload at 100 %) */
    ico_touch_edits_defaults(&live);
    CHECK(ico_touch_edit_parse(&live.ctl[0][ICO_TOUCH_CTL_SELECT], ICO_TOUCH_CTL_SELECT,
                               "hidden") == 0);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_SELECT), cy_of(&g_l, ICO_TOUCH_B_SELECT));
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_SELECT);
    tap_key(&ed, ICO_TOUCH_ED_TB_OP_UP);
    {
        const IcoTouchCtlEdit *sel = &ed.work.ctl[0][ICO_TOUCH_CTL_SELECT];
        char buf[64];

        CHECK(sel->set && sel->ax <= ICO_TOUCH_ANCHOR_R && sel->ay <= ICO_TOUCH_ANCHOR_B);
        CHECK(near_(sel->alpha, 0.1f, 1e-5f));
        ico_touch_edit_text(sel, buf, sizeof(buf));
        CHECK(strcmp(buf, "default") != 0 && strstr(buf, " o10") != NULL);
        CHECK(near_(g_l.button[ICO_TOUCH_B_SELECT].alpha, 0.1f, 1e-5f));
        {
            const IcoTouchLayout d = ico_touch_layout(g_w, g_h, no_insets(), ICO_TOUCH_MEDIUM);

            CHECK(near_(cx_of(&g_l, ICO_TOUCH_B_SELECT), cx_of(&d, ICO_TOUCH_B_SELECT), 0.6f));
        }
    }
    ico_touch_edit_end(&ed, 0);

    /* a pinch from a fresh two-finger touch: the first finger's down on an
       unselected control selects it at once (no tap first), a second
       finger anywhere scales it (100 px apart, then 120: 1.2) */
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    CHECK(ed.selected == -1);
    tri = &ed.work.ctl[0][ICO_TOUCH_CTL_TRIANGLE];
    x = cx_of(&g_l, ICO_TOUCH_B_TRIANGLE);
    y = cy_of(&g_l, ICO_TOUCH_B_TRIANGLE);
    fe(&ed, 8, x, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_TRIANGLE && ed.state == ICO_TOUCH_ED_IDLE && !tri->set);
    fe(&ed, 9, x - 100.0f, y, ICO_TOUCH_DOWN);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_PINCH && ed.selected == ICO_TOUCH_CTL_TRIANGLE);
    fe(&ed, 9, x - 120.0f, y, ICO_TOUCH_MOVE);
    fe(&ed, 9, x - 120.0f, y, ICO_TOUCH_UP);
    fe(&ed, 8, x, y, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && ed.selected == ICO_TOUCH_CTL_TRIANGLE);
    CHECK(tri->set && near_(tri->scale, 1.2f, 1e-5f));
    CHECK(near_(g_l.button[ICO_TOUCH_B_TRIANGLE].rect.w, 2.0f * 5.5f * 1.2f * q, 0.01f) &&
          ed.undos == 1);
    ico_touch_edit_end(&ed, 0);
}

static void test_posture_switch(void)
{
    const float k420 = 420.0f / 25.4f;
    IcoTouchEnv full = env_of(k420, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    IcoTouchEnv split = env_of(k420, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 1104.0f, 0.0f, 0.0f, 1840.0f);
    IcoTouchEnv flat = env_of(k420, ICO_TOUCH_FOLD_HORIZONTAL, 0, 0, 0.0f, 906.0f, 2176.0f, 0.0f);
    IcoTouchEdits live;
    IcoTouchEditor ed;
    float x, y;

    reset_screen(2208, 1840, &full);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    CHECK(g_l.rule == ICO_TOUCH_RULE_FULL && ed.rule == ICO_TOUCH_RULE_FULL);
    /* a finished edit in FULL, then a drag under way */
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_L1), cy_of(&g_l, ICO_TOUCH_B_L1));
    etick(&ed, 0);
    size_step(&ed, 1);
    CHECK(near_(ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_L1].scale, 1.1f, 1e-5f));
    x = cx_of(&g_l, ICO_TOUCH_B_CROSS);
    y = cy_of(&g_l, ICO_TOUCH_B_CROSS);
    fe(&ed, 1, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 1, x - 100.0f, y - 100.0f, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_DRAG && ed.undos == 2);
    /* the phone unfolds into SPLIT: the drag dropped (Cross back), the L1
       edit kept, the editor on SPLIT now */
    g_env = &split;
    etick(&ed, 0);
    CHECK(g_l.rule == ICO_TOUCH_RULE_SPLIT && ed.rule == ICO_TOUCH_RULE_SPLIT);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && ed.fingers == 0 && ed.undos == 1);
    CHECK(!ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_CROSS].set);
    CHECK(near_(ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_L1].scale, 1.1f, 1e-5f));
    fe(&ed, 1, x - 100.0f, y - 100.0f, ICO_TOUCH_UP); /* that finger: nothing */
    etick(&ed, 0);
    CHECK(!ico_touch_edits_any(&ed.work, ICO_TOUCH_RULE_SPLIT));
    /* SPLIT's own edits */
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_CROSS), cy_of(&g_l, ICO_TOUCH_B_CROSS));
    etick(&ed, 0);
    CHECK(ed.selected == ICO_TOUCH_CTL_CROSS);
    size_step(&ed, 1);
    CHECK(near_(ed.work.ctl[ICO_TOUCH_RULE_SPLIT][ICO_TOUCH_CTL_CROSS].scale, 1.1f, 1e-5f));
    CHECK(!ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_CROSS].set);
    /* folded back: FULL again, both kept */
    g_env = &full;
    etick(&ed, 0);
    CHECK(ed.rule == ICO_TOUCH_RULE_FULL);
    CHECK(ed.work.ctl[ICO_TOUCH_RULE_SPLIT][ICO_TOUCH_CTL_CROSS].set);
    CHECK(near_(g_l.button[ICO_TOUCH_B_L1].rect.w, 12.0f * 1.1f * g_l.unit, 0.01f));
    ico_touch_edit_end(&ed, 1);
    CHECK(live.ctl[ICO_TOUCH_RULE_SPLIT][ICO_TOUCH_CTL_CROSS].set);

    /* a drag onto a flat crease would lay everything out below it: the
       change is undone and refused, the posture stays */
    reset_screen(2176, 1812, &flat);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    CHECK(g_l.rule == ICO_TOUCH_RULE_FULL);
    x = cx_of(&g_l, ICO_TOUCH_B_CROSS);
    y = cy_of(&g_l, ICO_TOUCH_B_CROSS);
    fe(&ed, 2, x, y, ICO_TOUCH_DOWN);
    fe(&ed, 2, x, 906.0f, ICO_TOUCH_MOVE);
    etick(&ed, 0);
    CHECK(ed.state == ICO_TOUCH_ED_DRAG && g_l.rule == ICO_TOUCH_RULE_TABLETOP);
    etick(&ed, 0);
    CHECK(ed.rule == ICO_TOUCH_RULE_FULL && ed.refused == ICO_TOUCH_EDIT_REFUSED);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && ed.undos == 0);
    CHECK(!ed.work.ctl[ICO_TOUCH_RULE_FULL][ICO_TOUCH_CTL_CROSS].set);
    CHECK(g_l.rule == ICO_TOUCH_RULE_FULL);
    fe(&ed, 2, x, 906.0f, ICO_TOUCH_UP);
    etick(&ed, 0);
    CHECK(ed.rule == ICO_TOUCH_RULE_FULL && !ico_touch_edits_any(&ed.work, -1));
    ico_touch_edit_end(&ed, 0);
}

static void test_gamepad_drive(void)
{
    IcoTouchEdits live;
    IcoTouchEditor ed;
    IcoTouchCtlEdit *cross;
    int i;

    reset_screen(1920, 1080, NULL);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    cross = &ed.work.ctl[0][ICO_TOUCH_CTL_CROSS];

    /* Cross with nothing selected toggles All */
    etick(&ed, ICO_PAD_CROSS);
    CHECK(ed.all == 1);
    etick(&ed, ICO_PAD_CROSS); /* held: no second toggle */
    CHECK(ed.all == 1);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_CROSS);
    CHECK(ed.all == 0);
    etick(&ed, 0);

    /* R1 / L1 cycle the selection */
    etick(&ed, ICO_PAD_R1);
    CHECK(ed.selected == ICO_TOUCH_CTL_DPAD);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_R1);
    CHECK(ed.selected == ICO_TOUCH_CTL_CROSS);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_L1);
    CHECK(ed.selected == ICO_TOUCH_CTL_DPAD);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_L1);
    CHECK(ed.selected == ICO_TOUCH_CTL_LOOK);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_R1);
    CHECK(ed.selected == ICO_TOUCH_CTL_DPAD);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_R1);
    CHECK(ed.selected == ICO_TOUCH_CTL_CROSS);
    etick(&ed, 0);

    /* the D-pad nudges one grid step (snapped): r20 b10.5 -> r16 b12 */
    etick(&ed, ICO_PAD_RIGHT);
    CHECK(cross->set && cross->ax == ICO_TOUCH_ANCHOR_R && near_(cross->dx, 16.0f, 1e-4f));
    CHECK(cross->ay == ICO_TOUCH_ANCHOR_B && near_(cross->dy, 12.0f, 1e-4f) && ed.undos == 1);
    /* held: repeats after ICO_TOUCH_EDIT_REPEAT_DELAY ticks, then every
       ICO_TOUCH_EDIT_REPEAT */
    for (i = 1; i < ICO_TOUCH_EDIT_REPEAT_DELAY; i++) {
        etick(&ed, ICO_PAD_RIGHT);
    }
    CHECK(near_(cross->dx, 16.0f, 1e-4f));
    etick(&ed, ICO_PAD_RIGHT);
    CHECK(near_(cross->dx, 12.0f, 1e-4f));
    for (i = 1; i < ICO_TOUCH_EDIT_REPEAT; i++) {
        etick(&ed, ICO_PAD_RIGHT);
    }
    CHECK(near_(cross->dx, 12.0f, 1e-4f));
    etick(&ed, ICO_PAD_RIGHT);
    CHECK(near_(cross->dx, 8.0f, 1e-4f) && ed.undos == 3);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_DOWN);
    CHECK(near_(cross->dy, 8.0f, 1e-4f));
    etick(&ed, 0);
    etick(&ed, ICO_PAD_UP);
    CHECK(near_(cross->dy, 12.0f, 1e-4f) && ed.refused == 0);
    etick(&ed, 0);
    /* one more up would cover Circle's centre: refused, not moved */
    etick(&ed, ICO_PAD_UP);
    CHECK(near_(cross->dy, 12.0f, 1e-4f) && ed.refused == ICO_TOUCH_EDIT_REFUSED);
    etick(&ed, 0);

    /* L2 / R2 size, Square / Cross opacity */
    etick(&ed, ICO_PAD_L2);
    CHECK(near_(cross->scale, 0.9f, 1e-5f));
    etick(&ed, 0);
    etick(&ed, ICO_PAD_R2);
    CHECK(near_(cross->scale, 1.0f, 1e-5f));
    etick(&ed, 0);
    etick(&ed, ICO_PAD_SQUARE);
    CHECK(near_(cross->alpha, 0.9f, 1e-5f));
    etick(&ed, 0);
    etick(&ed, ICO_PAD_CROSS);
    CHECK(near_(cross->alpha, 1.0f, 1e-5f) && ed.all == 0);
    etick(&ed, 0);

    /* Start: the Reset strip on Cancel; right to This screen, Cross picks */
    etick(&ed, ICO_PAD_START);
    CHECK(ed.state == ICO_TOUCH_ED_CONFIRM_RESET && ed.confirmSel == ICO_TOUCH_ED_CF_CANCEL);
    etick(&ed, 0);
    etick(&ed, ICO_PAD_RIGHT);
    CHECK(ed.confirmSel == ICO_TOUCH_ED_CF_THIS && near_(cross->dx, 8.0f, 1e-4f));
    etick(&ed, 0);
    etick(&ed, ICO_PAD_CROSS);
    CHECK(ed.state == ICO_TOUCH_ED_IDLE && !cross->set);
    etick(&ed, 0);
    /* Circle in the strip backs out of it */
    etick(&ed, ICO_PAD_START);
    etick(&ed, 0);
    CHECK(etick(&ed, ICO_PAD_CIRCLE) == ICO_TOUCH_ED_NONE && ed.state == ICO_TOUCH_ED_IDLE);
    etick(&ed, 0);
    /* Select: Undo (the reset taken back) */
    etick(&ed, ICO_PAD_SELECT);
    CHECK(cross->set && near_(cross->dx, 8.0f, 1e-4f) && near_(cross->dy, 12.0f, 1e-4f));
    etick(&ed, 0);
    /* Triangle or Circle: Done */
    CHECK(etick(&ed, ICO_PAD_TRIANGLE) == ICO_TOUCH_ED_LEAVE);
    etick(&ed, 0);
    CHECK(etick(&ed, ICO_PAD_CIRCLE) == ICO_TOUCH_ED_LEAVE);
    ico_touch_edit_end(&ed, 0);
}

static void test_undo(void)
{
    IcoTouchEdits live;
    IcoTouchEditor ed;
    IcoTouchCtlEdit *cross;
    int i;

    reset_screen(1920, 1080, NULL);
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    cross = &ed.work.ctl[0][ICO_TOUCH_CTL_CROSS];
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_CROSS), cy_of(&g_l, ICO_TOUCH_B_CROSS));
    etick(&ed, 0);
    /* nine changes: 1.1 .. 1.6, then 1.5, 1.4, 1.3; the stack keeps eight */
    for (i = 0; i < 6; i++) {
        size_step(&ed, 1);
    }
    for (i = 0; i < 3; i++) {
        size_step(&ed, -1);
    }
    CHECK(near_(cross->scale, 1.3f, 1e-5f) && ed.undos == ICO_TOUCH_EDIT_UNDO);
    tap_key(&ed, ICO_TOUCH_ED_TB_UNDO);
    CHECK(near_(cross->scale, 1.4f, 1e-5f) && ed.undos == ICO_TOUCH_EDIT_UNDO - 1);
    for (i = 1; i < ICO_TOUCH_EDIT_UNDO; i++) {
        tap_key(&ed, ICO_TOUCH_ED_TB_UNDO);
    }
    CHECK(near_(cross->scale, 1.1f, 1e-5f) && ed.undos == 0);
    tap_key(&ed, ICO_TOUCH_ED_TB_UNDO); /* nothing left */
    CHECK(near_(cross->scale, 1.1f, 1e-5f) && cross->set);
    CHECK(near_(g_l.button[ICO_TOUCH_B_CROSS].rect.w, 2.0f * 5.5f * 1.1f * g_l.unit, 0.01f));
    /* Snap and Grid are changes too */
    tap_key(&ed, ICO_TOUCH_ED_TB_SNAP);
    CHECK(ed.work.snap == 0 && ed.undos == 1);
    tap_key(&ed, ICO_TOUCH_ED_TB_GRID);
    CHECK(ed.work.grid == 8.0f);
    tap_key(&ed, ICO_TOUCH_ED_TB_GRID);
    CHECK(ed.work.grid == 2.0f);
    tap_key(&ed, ICO_TOUCH_ED_TB_GRID);
    CHECK(ed.work.grid == 4.0f && ed.undos == 4);
    tap_key(&ed, ICO_TOUCH_ED_TB_UNDO);
    CHECK(ed.work.grid == 2.0f);
    /* Reset -> This screen, then Undo */
    tap_key(&ed, ICO_TOUCH_ED_TB_RESET);
    CHECK(ed.state == ICO_TOUCH_ED_CONFIRM_RESET);
    {
        IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT];

        ico_touch_edit_toolbar(&g_l, tb, cf);
        tap(&ed, cf[ICO_TOUCH_ED_CF_THIS].x + 1.0f, cf[ICO_TOUCH_ED_CF_THIS].y + 1.0f);
        etick(&ed, 0);
        CHECK(ed.state == ICO_TOUCH_ED_IDLE && !cross->set && ed.work.grid == 2.0f);
        tap_key(&ed, ICO_TOUCH_ED_TB_UNDO);
        CHECK(cross->set && near_(cross->scale, 1.1f, 1e-5f));
        /* the strip's Cancel, and a tap off it, change nothing */
        tap_key(&ed, ICO_TOUCH_ED_TB_RESET);
        tap(&ed, cf[ICO_TOUCH_ED_CF_CANCEL].x + 1.0f, cf[ICO_TOUCH_ED_CF_CANCEL].y + 1.0f);
        etick(&ed, 0);
        CHECK(ed.state == ICO_TOUCH_ED_IDLE && cross->set);
        tap_key(&ed, ICO_TOUCH_ED_TB_RESET);
        tap(&ed, 1000.0f, 900.0f);
        etick(&ed, 0);
        CHECK(ed.state == ICO_TOUCH_ED_IDLE && cross->set);
        /* All screens */
        ed.work.ctl[ICO_TOUCH_RULE_SPLIT][ICO_TOUCH_CTL_L1] = *cross;
        tap_key(&ed, ICO_TOUCH_ED_TB_RESET);
        tap(&ed, cf[ICO_TOUCH_ED_CF_ALL].x + 1.0f, cf[ICO_TOUCH_ED_CF_ALL].y + 1.0f);
        etick(&ed, 0);
        CHECK(!ico_touch_edits_any(&ed.work, -1));
    }
    ico_touch_edit_end(&ed, 0);
}

static int edits_equal(const IcoTouchEdits *a, const IcoTouchEdits *b)
{
    int r, c;

    if (a->snap != b->snap || a->grid != b->grid || a->gen != b->gen) {
        return 0;
    }
    for (r = 0; r < ICO_TOUCH_POSTURES; r++) {
        for (c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
            const IcoTouchCtlEdit *x = &a->ctl[r][c], *y = &b->ctl[r][c];

            if (x->set != y->set || x->ax != y->ax || x->ay != y->ay || x->dx != y->dx ||
                x->dy != y->dy || x->scale != y->scale || x->alpha != y->alpha || x->w != y->w ||
                x->h != y->h) {
                return 0;
            }
        }
    }
    return 1;
}

static void test_done(void)
{
    IcoTouchEdits live, copy;
    IcoTouchEditor ed;
    IcoTouchEditView v;
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT];
    unsigned int g;
    float q;

    reset_screen(1920, 1080, NULL);
    ico_touch_edits_defaults(&live);
    live.gen = 5;
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    CHECK(ed.live == &live && edits_equal(&ed.work, &live));
    etick(&ed, 0);
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_CROSS), cy_of(&g_l, ICO_TOUCH_B_CROSS));
    etick(&ed, 0);
    size_step(&ed, 1);
    CHECK(ed.changed && !live.ctl[0][ICO_TOUCH_CTL_CROSS].set);
    /* end(1): work -> live, a generation newer than both */
    g = ed.work.gen > live.gen ? ed.work.gen : live.gen;
    ico_touch_edit_end(&ed, 1);
    CHECK(!ed.active && live.ctl[0][ICO_TOUCH_CTL_CROSS].set);
    CHECK(near_(live.ctl[0][ICO_TOUCH_CTL_CROSS].scale, 1.1f, 1e-5f) && live.gen == g + 1);
    CHECK(etick(&ed, ICO_PAD_TRIANGLE) == ICO_TOUCH_ED_NONE);
    ico_touch_edit_view(&ed, &g_l, &v);
    CHECK(!v.active);
    /* end(0): live untouched */
    copy = live;
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    tap(&ed, cx_of(&g_l, ICO_TOUCH_B_CROSS), cy_of(&g_l, ICO_TOUCH_B_CROSS));
    etick(&ed, 0);
    size_step(&ed, 1);
    CHECK(near_(ed.work.ctl[0][ICO_TOUCH_CTL_CROSS].scale, 1.2f, 1e-5f));
    ico_touch_edit_end(&ed, 0);
    CHECK(!ed.active && edits_equal(&live, &copy));

    /* the toolbar wins over a control put under it (R2 at l40 t7, its top
       under Snap and Grid), the part of R2 below the strip selects it, the
       Done key leaves (over L2: the toolbar wins there too) */
    q = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM).unit;
    ico_touch_edits_defaults(&live);
    set_edit(&live, ICO_TOUCH_RULE_FULL, ICO_TOUCH_CTL_R2, "l40 t7");
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    ico_touch_edit_toolbar(&g_l, tb, cf);
    CHECK(near_(cx_of(&g_l, ICO_TOUCH_B_R2), 40.0f * q, 0.02f));
    CHECK(g_l.button[ICO_TOUCH_B_R2].rect.y < bottom_of(&tb[ICO_TOUCH_ED_TB_SNAP]));
    CHECK(tb[ICO_TOUCH_ED_TB_SNAP].x < 40.0f * q &&
          right_of(&tb[ICO_TOUCH_ED_TB_SNAP]) > 40.0f * q);
    tap(&ed, 40.0f * q, 3.0f * q);
    CHECK(etick(&ed, 0) == ICO_TOUCH_ED_NONE && ed.selected == -1 && ed.work.snap == 0);
    tap(&ed, 40.0f * q, 9.0f * q);
    CHECK(etick(&ed, 0) == ICO_TOUCH_ED_NONE && ed.selected == ICO_TOUCH_CTL_R2);
    tap(&ed, 1000.0f, 900.0f);
    etick(&ed, 0);
    CHECK(ed.selected == -1);
    /* a value cell is the toolbar's and does nothing */
    tap_key(&ed, ICO_TOUCH_ED_TB_SIZE_VAL);
    CHECK(ed.selected == -1 && ed.undos == 1 && ed.state == ICO_TOUCH_ED_IDLE);
    CHECK(touches(&tb[ICO_TOUCH_ED_TB_DONE], &g_l.button[ICO_TOUCH_B_L2].rect));
    tap(&ed, tb[ICO_TOUCH_ED_TB_DONE].x + 0.5f * tb[ICO_TOUCH_ED_TB_DONE].w,
        tb[ICO_TOUCH_ED_TB_DONE].y + 0.5f * tb[ICO_TOUCH_ED_TB_DONE].h);
    CHECK(etick(&ed, 0) == ICO_TOUCH_ED_LEAVE && ed.selected == -1);
    ico_touch_edit_end(&ed, 1);
    CHECK(live.ctl[0][ICO_TOUCH_CTL_R2].set &&
          near_(live.ctl[0][ICO_TOUCH_CTL_R2].dx, 40.0f, 1e-4f));
}

/* each default control's bounding box (the look pad left out: the toolbar
   lies over it by design) */
static int control_boxes(const IcoTouchLayout *l, IcoTouchRect out[ICO_TOUCH_BUTTONS + 1])
{
    int z, n = 0;

    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        out[n++] = l->button[z].rect;
    }
    out[n].x = l->stickHomeX - l->stickR;
    out[n].y = l->stickHomeY - l->stickR;
    out[n].w = out[n].h = 2.0f * l->stickR;
    return n + 1;
}

/* the rects share some area (touching is not overlapping), beyond eps */
static int overlaps_by(const IcoTouchRect *a, const IcoTouchRect *b, float eps)
{
    return a->x < b->x + b->w - eps && b->x < a->x + a->w - eps && a->y < b->y + b->h - eps &&
           b->y < a->y + a->h - eps;
}

/* the controls of the default top row (y T+3..T+11) the strip may cover */
static int top_row(int z)
{
    return z == ICO_TOUCH_B_L2 || z == ICO_TOUCH_B_UP || z == ICO_TOUCH_B_SELECT ||
           z == ICO_TOUCH_B_START || z == ICO_TOUCH_B_R2;
}

/* The strip's rules on a default layout: every cell inside the region,
   3 q in from its sides (SPLIT: 3 q clear of the band, wholly on one side),
   at least ICO_TOUCH_ED_CELL_MIN q wide, the cells side by side without
   overlapping; the confirm strip over the first cells; the top row's
   controls covered 2.5 q at most with 5.5 q below the strip to grab, no
   other control touched (SPLIT's second row: Start and R2 only); the hint
   inside the region along its bottom, touching no control and no cell.
   Returns 1 when the strip has a second row. */
static int check_toolbar_clear(const IcoTouchLayout *l, const char *what)
{
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT], boxes[ICO_TOUCH_BUTTONS + 1];
    IcoTouchRect hint;
    const IcoTouchRect *R = &l->region, *b = &l->band;
    const float q = l->unit, eps = 0.02f;
    const float row1 = R->y + 5.5f * q;
    const int split = l->rule == ICO_TOUCH_RULE_SPLIT;
    int n = control_boxes(l, boxes), i, j, fails = failures, twoRows = 0;

    ico_touch_edit_toolbar(l, tb, cf);
    ico_touch_edit_hint_rect(l, &hint);
    for (i = 0; i < ICO_TOUCH_ED_TB_COUNT; i++) {
        const int second = tb[i].y > row1 - eps;

        twoRows |= second;
        CHECK(tb[i].w >= ICO_TOUCH_ED_CELL_MIN * q - eps && near_(tb[i].h, 5.0f * q, eps));
        CHECK(rect_inside(&tb[i], R));
        CHECK(tb[i].x >= R->x + 3.0f * q - eps && right_of(&tb[i]) <= R->x + R->w - 3.0f * q + eps);
        CHECK(near_(tb[i].y, R->y + (second ? 6.0f : 0.5f) * q, eps));
        CHECK(!second || split);
        if (split) {
            CHECK(right_of(&tb[i]) <= b->x - 3.0f * q + eps ||
                  tb[i].x >= b->x + b->w + 3.0f * q - eps);
        }
        for (j = i + 1; j < ICO_TOUCH_ED_TB_COUNT; j++) {
            CHECK(!overlaps_by(&tb[i], &tb[j], eps));
        }
        for (j = 0; j < n; j++) {
            const int z = j < ICO_TOUCH_BUTTONS ? j : -1;

            if (second) {
                /* SPLIT's second row lies over Start and R2 (the right
                   side's top row) and nothing else */
                if (z != ICO_TOUCH_B_START && z != ICO_TOUCH_B_R2 && touches(&tb[i], &boxes[j])) {
                    fprintf(stderr, "  %s: second-row cell %d touches control box %d\n", what, i,
                            j);
                    CHECK(0);
                }
            } else if (z >= 0 && top_row(z)) {
                if (overlaps_by(&tb[i], &boxes[j], eps)) {
                    /* 2.5 q covered at most, 5.5 q left below */
                    CHECK(bottom_of(&tb[i]) - boxes[j].y <= 2.5f * q + eps);
                    CHECK(bottom_of(&boxes[j]) - bottom_of(&tb[i]) >= 5.5f * q - eps);
                }
            } else if (touches(&tb[i], &boxes[j])) {
                fprintf(stderr, "  %s: cell %d touches control box %d\n", what, i, j);
                CHECK(0);
            }
        }
        CHECK(!touches(&tb[i], &hint));
    }
    if (twoRows) {
        /* only where the right side cannot hold its cells in one row */
        CHECK(split && b->x + b->w + 3.0f * q < R->x + R->w - 3.0f * q);
        CHECK((R->x + R->w - 3.0f * q) - (b->x + b->w + 3.0f * q) <
              (float)(ICO_TOUCH_ED_TB_COUNT - 6) * ICO_TOUCH_ED_CELL_MIN * q + eps);
    } else {
        /* one row: every top-row control keeps its 5.5 q */
        for (j = 0; j < ICO_TOUCH_BUTTONS; j++) {
            if (top_row(j)) {
                CHECK(bottom_of(&boxes[j]) - row1 >= 5.5f * q - eps);
            }
        }
    }
    for (i = 0; i < ICO_TOUCH_ED_CF_COUNT; i++) {
        CHECK(near_(cf[i].y, tb[ICO_TOUCH_ED_TB_DONE].y, eps) && near_(cf[i].h, 5.0f * q, eps));
        CHECK(cf[i].x >= tb[ICO_TOUCH_ED_TB_DONE].x - eps &&
              right_of(&cf[i]) <= right_of(&tb[ICO_TOUCH_ED_TB_ALL]) + eps);
        CHECK(i == 0 || near_(cf[i].x, right_of(&cf[i - 1]), eps));
    }
    /* the hint: the bottom, clear of every control */
    CHECK(rect_inside(&hint, R) && near_(hint.y, R->y + R->h - 4.0f * q, eps) &&
          near_(hint.h, 3.0f * q, eps));
    CHECK(hint.w >= 44.0f * q - eps);
    if (split) {
        CHECK(right_of(&hint) <= b->x - 3.0f * q + eps || hint.x >= b->x + b->w + 3.0f * q - eps);
    } else {
        CHECK(near_(hint.x, R->x + 35.0f * q, eps) &&
              near_(right_of(&hint), R->x + R->w - 35.0f * q, eps));
    }
    for (j = 0; j < n; j++) {
        if (touches(&hint, &boxes[j])) {
            fprintf(stderr, "  %s: the hint touches control box %d\n", what, j);
            CHECK(0);
        }
    }
    if (failures != fails) {
        fprintf(stderr, "  in %s: %ux%u size %d rule %d q %.3f\n", what, l->outW, l->outH, l->size,
                l->rule, (double)l->unit);
    }
    return twoRows;
}

static void test_toolbar_clear(void)
{
    IcoTouchEnv env;
    IcoTouchLayout l;
    IcoTouchRect tb[ICO_TOUCH_ED_TB_COUNT], cf[ICO_TOUCH_ED_CF_COUNT], hint;
    IcoTouchEditor ed;
    IcoTouchEditView v;
    IcoTouchEdits live;
    int r, sz, seen = 0;

    for (r = 0; r < SHAPE_COUNT; r++) {
        for (sz = 0; sz < 3; sz++) {
            l = shape_layout(&s_shapes[r], sz, NULL);
            seen |= 1 << l.rule;
            check_toolbar_clear(&l, "shape");
        }
    }
    CHECK(seen == 7); /* all three postures */

    /* the smallest regions fit() allows, q = 10 */
    env = env_of(10.0f, ICO_TOUCH_FOLD_NONE, 0, 0, 0, 0, 0, 0);
    l = ico_touch_layout_env(1140, 640, no_insets(), ICO_TOUCH_MEDIUM, &env);
    CHECK(l.rule == ICO_TOUCH_RULE_FULL && l.unit == 10.0f);
    CHECK(check_toolbar_clear(&l, "FULL 114 x 64") == 0);
    ico_touch_edit_toolbar(&l, tb, cf);
    /* ten cells of 10.8 q from L+3 to Rt-3 */
    CHECK(near_(tb[ICO_TOUCH_ED_TB_DONE].x, 30.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_DONE].y, 5.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_DONE].w, 108.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_DONE].h, 50.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_ALL].x, 30.0f + 5.0f * 108.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].x, 30.0f + 6.0f * 108.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_OP_VAL].x, 30.0f + 8.0f * 108.0f, 0.01f));
    CHECK(near_(right_of(&tb[ICO_TOUCH_ED_TB_OP_UP]), 1110.0f, 0.01f));
    /* the confirm strip: three 21.6 q cells over Done .. All */
    CHECK(near_(cf[0].x, 30.0f, 0.01f) && near_(cf[1].w, 216.0f, 0.01f));
    CHECK(near_(right_of(&cf[2]), right_of(&tb[ICO_TOUCH_ED_TB_ALL]), 0.01f));
    ico_touch_edit_hint_rect(&l, &hint);
    CHECK(near_(hint.x, 350.0f, 0.01f) && near_(hint.w, 440.0f, 0.01f));
    CHECK(near_(hint.y, 600.0f, 0.01f) && near_(hint.h, 30.0f, 0.01f));

    env = env_of(10.0f, ICO_TOUCH_FOLD_HORIZONTAL, 1, 0, 0.0f, 700.0f, 1140.0f, 0.0f);
    l = ico_touch_layout_env(1140, 1360, no_insets(), ICO_TOUCH_MEDIUM, &env);
    CHECK(l.rule == ICO_TOUCH_RULE_TABLETOP && l.unit == 10.0f && near_(l.region.h, 640.0f, 0.01f));
    CHECK(check_toolbar_clear(&l, "TABLETOP 114 x 64") == 0);
    ico_touch_edit_toolbar(&l, tb, cf);
    CHECK(near_(tb[ICO_TOUCH_ED_TB_DONE].y, 720.0f + 5.0f, 0.01f));
    ico_touch_edit_hint_rect(&l, &hint);
    CHECK(near_(hint.y, 1320.0f, 0.01f));

    /* SPLIT at its minimum, 58 | 40: six cells of 52/6 q on the left, the
       right side's 34 q in one row of four (8.5 q) over Start and R2 */
    env = env_of(10.0f, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 600.0f, 0.0f, 0.0f, 640.0f);
    l = ico_touch_layout_env(1020, 640, no_insets(), ICO_TOUCH_MEDIUM, &env);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT && l.unit == 10.0f && near_(l.band.x, 580.0f, 0.01f));
    CHECK(near_(l.band.x + l.band.w, 620.0f, 0.01f));
    CHECK(check_toolbar_clear(&l, "SPLIT 58 | 40 x 64") == 0);
    ico_touch_edit_toolbar(&l, tb, cf);
    CHECK(near_(tb[ICO_TOUCH_ED_TB_DONE].x, 30.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_DONE].w, 520.0f / 6.0f, 0.01f));
    CHECK(near_(right_of(&tb[ICO_TOUCH_ED_TB_ALL]), 550.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].x, 650.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].y, 5.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].w, 85.0f, 0.01f));
    CHECK(near_(right_of(&tb[ICO_TOUCH_ED_TB_OP_UP]), 990.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_OP_UP].y, 5.0f, 0.01f));
    CHECK(touches(&tb[ICO_TOUCH_ED_TB_SIZE_VAL], &l.button[ICO_TOUCH_B_START].rect) &&
          touches(&tb[ICO_TOUCH_ED_TB_OP_UP], &l.button[ICO_TOUCH_B_R2].rect));
    CHECK(near_(cf[0].x, 30.0f, 0.01f) && near_(right_of(&cf[2]), 550.0f, 0.01f) &&
          near_(cf[2].y, 5.0f, 0.01f));
    ico_touch_edit_hint_rect(&l, &hint);
    CHECK(near_(hint.x, 30.0f, 0.01f) && near_(hint.w, 520.0f, 0.01f) &&
          near_(hint.y, 600.0f, 0.01f));

    /* SPLIT 58 | 58: the right side's cells 13 q wide */
    env = env_of(10.0f, ICO_TOUCH_FOLD_VERTICAL, 0, 0, 600.0f, 0.0f, 0.0f, 640.0f);
    l = ico_touch_layout_env(1200, 640, no_insets(), ICO_TOUCH_MEDIUM, &env);
    CHECK(l.rule == ICO_TOUCH_RULE_SPLIT && l.unit == 10.0f);
    CHECK(check_toolbar_clear(&l, "SPLIT 58 | 58 x 64") == 0);
    ico_touch_edit_toolbar(&l, tb, cf);
    CHECK(near_(tb[ICO_TOUCH_ED_TB_OP_UP].y, 5.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].w, 130.0f, 0.01f));

    /* the second row, on a right side narrower than fit() allows (28 q,
       a layout built by hand: only the strip's geometry is read): its 22 q
       span takes two rows of two 11 q cells */
    memset(&l, 0, sizeof(l));
    l.rule = ICO_TOUCH_RULE_SPLIT;
    l.unit = 10.0f;
    l.region.w = 900.0f;
    l.region.h = 640.0f;
    l.band.x = 580.0f;
    l.band.w = 40.0f;
    l.band.h = 640.0f;
    ico_touch_edit_toolbar(&l, tb, cf);
    CHECK(near_(right_of(&tb[ICO_TOUCH_ED_TB_ALL]), 550.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].x, 650.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].y, 5.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_SIZE_VAL].w, 110.0f, 0.01f));
    CHECK(near_(right_of(&tb[ICO_TOUCH_ED_TB_OP_DN]), 870.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_OP_DN].y, 5.0f, 0.01f));
    CHECK(near_(tb[ICO_TOUCH_ED_TB_OP_VAL].x, 650.0f, 0.01f) &&
          near_(tb[ICO_TOUCH_ED_TB_OP_VAL].y, 60.0f, 0.01f) &&
          near_(bottom_of(&tb[ICO_TOUCH_ED_TB_OP_UP]), 110.0f, 0.01f) &&
          near_(right_of(&tb[ICO_TOUCH_ED_TB_OP_UP]), 870.0f, 0.01f));

    /* the view carries the cells and the hint */
    g_w = 1140;
    g_h = 640;
    g_env = NULL;
    ico_touch_edits_defaults(&live);
    ico_touch_edit_begin(&ed, &live, ICO_TOUCH_RULE_FULL);
    etick(&ed, 0);
    ico_touch_edit_view(&ed, &g_l, &v);
    ico_touch_edit_toolbar(&g_l, tb, cf);
    ico_touch_edit_hint_rect(&g_l, &hint);
    for (r = 0; r < ICO_TOUCH_ED_TB_COUNT; r++) {
        CHECK(same_rect(v.tb[r], tb[r]));
    }
    CHECK(same_rect(v.hint, hint) && hint.w > 0.0f);
    ico_touch_edit_end(&ed, 0);
}

int main(void)
{
    test_defaults();
    test_anchor_math();
    test_clamp_region_and_band();
    test_hidden();
    test_stick_look_areas();
    test_flat_crease();
    test_text_round_trip();
    test_snap_and_drop();
    test_pinch_and_steppers();
    test_posture_switch();
    test_gamepad_drive();
    test_undo();
    test_done();
    test_toolbar_clear();
    if (failures != 0) {
        fprintf(stderr, "touch_edit_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("touch_edit_test: ok\n");
    return 0;
}
