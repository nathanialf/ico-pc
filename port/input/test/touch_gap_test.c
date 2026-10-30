/*
 * port/input/test/touch_gap_test.c
 *
 * The Frame rate counter's place among the player's actual touch controls
 * (touch_gap.c), on the CPU, over real layouts from ico_touch_layout_edits
 * at 1920x1080:
 *   (a) the default layout: the place the counter always had while the
 *       controls show (just right of the D-pad's Up key, on the keys' row,
 *       shrunk before Select);
 *   (b) Start moved to the top-left and drawn, with the left shoulder and
 *       the D-pad out of the way: right of Start's box and 2 q of air;
 *       with them in place (the reporter's layout): where the default has it;
 *   (c) that Start Hidden: the corner;
 *   (d) every control Off: the corner;
 *   (e) a text too wide for any gap on the row: the corner;
 *   (f) the left stick's resting ring counts while it is drawn.
 * Nothing here draws or links the renderer.
 */
#include <stdio.h>
#include <string.h>
#include "touch.h"
#include "touch_edit.h"
#include "touch_gap.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define OUT_W 1920
#define OUT_H 1080

static int near_(float a, float b)
{
    return (a < b ? b - a : a - b) <= 0.5f;
}

static IcoTouchLayout build(const IcoTouchEdits *e)
{
    const IcoTouchInsets in = {0.0f, 0.0f, 0.0f, 0.0f};
    const IcoTouchEnv env = {0};

    return ico_touch_layout_edits(OUT_W, OUT_H, in, ICO_TOUCH_MEDIUM, &env, e);
}

static void set(IcoTouchEdits *e, int ctl, const char *text)
{
    CHECK(ico_touch_edit_parse(&e->ctl[ICO_TOUCH_RULE_FULL][ctl], ctl, text) == 0);
}

/* the counter as ui_fps.c asks: the corner 0.8 q in, the text 2.4 q high and
   7.5 q wide (shrinkable to 4 q) */
typedef struct Place {
    int ret;
    float x0, x1, y0, cx, cy, nat, min;
} Place;

static Place ask(const IcoTouchLayout *l, float natQ)
{
    Place p;
    const float q = l->unit;

    p.cx = p.cy = 0.8f * q;
    p.nat = natQ * q;
    p.min = 4.0f * q;
    p.x0 = p.x1 = p.y0 = -1.0f;
    p.ret = ico_touch_fps_place(l, p.cx, p.cy, 2.4f * q, p.nat, p.min, &p.x0, &p.x1, &p.y0);
    return p;
}

static void test_default(void)
{
    IcoTouchEdits e;

    ico_touch_edits_defaults(&e);
    const IcoTouchLayout l = build(&e);
    const float q = l.unit;
    const Place p = ask(&l, 7.5f);
    const IcoTouchRect *up = &l.button[ICO_TOUCH_B_UP].rect;
    const IcoTouchRect *sel = &l.button[ICO_TOUCH_B_SELECT].rect;

    CHECK(p.ret == 2);
    /* the old fixed place: 35 q from the region's edge (the Up key ends at 33 q), 3 q down,
       Select's left less 2 q */
    CHECK(near_(p.x0, l.region.x + 35.0f * q));
    CHECK(near_(p.x0, up->x + up->w + 2.0f * q));
    CHECK(near_(p.y0, l.region.y + 3.0f * q));
    CHECK(near_(p.x1, sel->x - 2.0f * q));
}

static void test_start_top_left(void)
{
    IcoTouchEdits e;

    /* (b) the shoulder and the D-pad hidden: Start alone in the corner */
    ico_touch_edits_defaults(&e);
    set(&e, ICO_TOUCH_CTL_L2, "hidden");
    set(&e, ICO_TOUCH_CTL_DPAD, "hidden");
    set(&e, ICO_TOUCH_CTL_START, "l10 t7");
    IcoTouchLayout l = build(&e);
    const float q = l.unit;
    Place p = ask(&l, 7.5f);
    const IcoTouchRect *st = &l.button[ICO_TOUCH_B_START].rect;

    CHECK(st->x < l.region.x + 6.0f * q &&
          st->y < l.region.y + 6.0f * q); /* it is up in the corner */
    CHECK(p.ret == 2);
    CHECK(near_(p.x0, st->x + st->w + 2.0f * q));
    CHECK(near_(p.y0, l.region.y + 3.0f * q));
    CHECK(p.x1 > p.x0 + p.nat - 0.5f);

    /* the reporter's layout: only Start moved; the shoulder and the D-pad
       still hold the row up to the Up key's end, as in the default */
    ico_touch_edits_defaults(&e);
    set(&e, ICO_TOUCH_CTL_START, "l10 t7");
    l = build(&e);
    p = ask(&l, 7.5f);
    CHECK(p.ret == 2);
    CHECK(near_(p.x0,
                l.button[ICO_TOUCH_B_UP].rect.x + l.button[ICO_TOUCH_B_UP].rect.w + 2.0f * l.unit));

    /* (c) Start Hidden at the same place: not drawn, so the corner */
    ico_touch_edits_defaults(&e);
    set(&e, ICO_TOUCH_CTL_L2, "hidden");
    set(&e, ICO_TOUCH_CTL_DPAD, "hidden");
    set(&e, ICO_TOUCH_CTL_START, "l10 t7 hidden");
    l = build(&e);
    CHECK(l.button[ICO_TOUCH_B_START].alpha == 0.0f);
    p = ask(&l, 7.5f);
    CHECK(p.ret == 1);
    CHECK(near_(p.x0, p.cx) && near_(p.y0, p.cy));
    /* Off is not drawn either */
    set(&e, ICO_TOUCH_CTL_START, "l10 t7 off");
    l = build(&e);
    p = ask(&l, 7.5f);
    CHECK(p.ret == 1 && near_(p.x0, p.cx) && near_(p.y0, p.cy));
}

static void test_all_off(void)
{
    IcoTouchEdits e;

    ico_touch_edits_defaults(&e);
    for (int c = 0; c < ICO_TOUCH_CTL_COUNT; c++) {
        set(&e, c, "off");
    }
    const IcoTouchLayout l = build(&e);
    const Place p = ask(&l, 7.5f);

    CHECK(p.ret == 1);
    CHECK(near_(p.x0, p.cx) && near_(p.y0, p.cy));
    CHECK(p.x1 == 0.0f);
}

static void test_no_room_and_stick(void)
{
    IcoTouchEdits e;
    IcoTouchLayout l;
    Place p;

    /* (e) a text wider than the whole row, even at its floor: the corner */
    ico_touch_edits_defaults(&e);
    l = build(&e);
    p = ask(&l, 7.5f);
    p.ret = ico_touch_fps_place(&l, p.cx, p.cy, 2.4f * l.unit, 400.0f * l.unit, 400.0f * l.unit,
                                &p.x0, &p.x1, &p.y0);
    CHECK(p.ret == 1);
    CHECK(near_(p.x0, p.cx) && near_(p.y0, p.cy) && p.x1 == 0.0f);

    /* (f) the left stick's resting ring up in the corner (moved there on the
       struct, every button out): a counted control while drawn, not at alpha 0 */
    ico_touch_edits_defaults(&e);
    l = build(&e);
    for (int i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        l.button[i].alpha = 0.0f;
    }
    l.stickHomeX = l.region.x + 14.0f * l.unit;
    l.stickHomeY = l.region.y + 8.0f * l.unit;
    p = ask(&l, 7.5f);
    CHECK(p.ret == 2);
    CHECK(near_(p.x0, l.stickHomeX + l.stickR + 2.0f * l.unit));
    l.stickAlpha = 0.0f;
    p = ask(&l, 7.5f);
    CHECK(p.ret == 1 && near_(p.x0, p.cx) && near_(p.y0, p.cy));

    CHECK(ico_touch_fps_place(NULL, 0, 0, 1, 1, 1, &p.x0, &p.x1, &p.y0) == 0);
}

int main(void)
{
    test_default();
    test_start_top_left();
    test_all_off();
    test_no_room_and_stick();
    if (failures) {
        fprintf(stderr, "touch_gap_test: %d failures\n", failures);
        return 1;
    }
    printf("touch_gap_test: ok\n");
    return 0;
}
