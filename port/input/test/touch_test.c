/*
 * port/input/test/touch_test.c
 *
 * The touch overlay's mapper (touch.c) on the CPU: the layouts at three
 * phone sizes (one with a notch inset), the floating left stick (centre,
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

    ico_touch_step(t, g_l, &v, now);
    return v;
}

static void centre_of(const IcoTouchLayout *l, int z, float *x, float *y)
{
    *x = l->button[z].rect.x + 0.5f * l->button[z].rect.w;
    *y = l->button[z].rect.y + 0.5f * l->button[z].rect.h;
}

static int rect_inside(const IcoTouchRect *in, const IcoTouchRect *out)
{
    return in->x >= out->x - 0.01f && in->y >= out->y - 0.01f &&
           in->x + in->w <= out->x + out->w + 0.01f && in->y + in->h <= out->y + out->h + 0.01f;
}

static int buttons_overlap(const IcoTouchButton *a, const IcoTouchButton *b)
{
    if (a->round && b->round) {
        float ra = 0.5f * a->rect.w, rb = 0.5f * b->rect.w;
        float dx = (a->rect.x + ra) - (b->rect.x + rb), dy = (a->rect.y + ra) - (b->rect.y + rb);

        return dx * dx + dy * dy < (ra + rb) * (ra + rb);
    }
    return a->rect.x < b->rect.x + b->rect.w && b->rect.x < a->rect.x + a->rect.w &&
           a->rect.y < b->rect.y + b->rect.h && b->rect.y < a->rect.y + a->rect.h;
}

/* every size: inside the safe area, no two buttons overlapping, every pad
   bit once, the areas where touch.h says */
static void check_layout_common(const IcoTouchLayout *l)
{
    const IcoTouchRect *s = &l->safeRect;
    unsigned int bits = 0;
    int i, j;

    for (i = 0; i < ICO_TOUCH_BUTTONS; i++) {
        CHECK(rect_inside(&l->button[i].rect, s));
        CHECK((bits & l->button[i].pad) == 0);
        bits |= l->button[i].pad;
        for (j = i + 1; j < ICO_TOUCH_BUTTONS; j++) {
            if (buttons_overlap(&l->button[i], &l->button[j])) {
                fprintf(stderr, "  %ux%u size %d: zones %d and %d overlap\n", l->outW, l->outH,
                        l->size, i, j);
                CHECK(0);
            }
        }
    }
    CHECK(bits == 0xF9FFu); /* everything but L3 and R3 */
    CHECK(near_(l->stickArea.x, s->x, 0.01f));
    CHECK(near_(l->stickArea.w, 0.4f * s->w, 0.01f));
    CHECK(near_(l->stickArea.y + l->stickArea.h, s->y + s->h, 0.01f));
    CHECK(near_(l->stickArea.h, 0.7f * s->h, 0.01f));
    CHECK(near_(l->lookArea.x + l->lookArea.w, s->x + s->w, 0.01f));
    CHECK(near_(l->lookArea.w, 0.6f * s->w, 0.01f));
    CHECK(near_(l->lookArea.y, s->y, 0.01f));
    CHECK(near_(l->lookArea.h, 0.45f * s->h, 0.01f));
    CHECK(near_(l->runR, 0.94f * l->stickR, 0.01f));
    /* the stick's home ring fits in its area */
    CHECK(l->stickHomeX - l->stickR >= l->stickArea.x);
    CHECK(l->stickHomeY + l->stickR <= l->stickArea.y + l->stickArea.h);
    CHECK(l->stickHomeY - l->stickR >= l->stickArea.y);
    /* shoulders in the corners, L left of R; D-pad left half, above the
       stick's home; Start right of Select, centred */
    CHECK(l->button[ICO_TOUCH_B_L1].rect.x < s->x + 0.1f * s->w);
    CHECK(l->button[ICO_TOUCH_B_R1].rect.x + l->button[ICO_TOUCH_B_R1].rect.w > s->x + 0.9f * s->w);
    CHECK(l->button[ICO_TOUCH_B_L2].rect.y > l->button[ICO_TOUCH_B_L1].rect.y);
    CHECK(l->button[ICO_TOUCH_B_R2].rect.y > l->button[ICO_TOUCH_B_R1].rect.y);
    CHECK(l->dpadX < s->x + 0.4f * s->w && l->dpadY < l->stickHomeY - l->stickR);
    CHECK(near_(l->button[ICO_TOUCH_B_START].rect.x - (s->x + 0.5f * s->w),
                s->x + 0.5f * s->w -
                    (l->button[ICO_TOUCH_B_SELECT].rect.x + l->button[ICO_TOUCH_B_SELECT].rect.w),
                0.01f));
    /* the diamond: Cross below, Circle right, Square left, Triangle above */
    CHECK(l->button[ICO_TOUCH_B_CROSS].rect.y > l->button[ICO_TOUCH_B_TRIANGLE].rect.y);
    CHECK(l->button[ICO_TOUCH_B_CIRCLE].rect.x > l->button[ICO_TOUCH_B_SQUARE].rect.x);
    CHECK(
        near_(l->button[ICO_TOUCH_B_CROSS].rect.x, l->button[ICO_TOUCH_B_TRIANGLE].rect.x, 0.01f));
    CHECK(l->faceX > s->x + 0.7f * s->w && l->faceY > s->y + 0.6f * s->h);
}

static void test_layouts(void)
{
    static const uint32_t size[3][2] = {{1920, 1080}, {2400, 1080}, {2340, 1080}};
    IcoTouchInsets in;
    IcoTouchLayout l;
    int r, sz;

    for (r = 0; r < 3; r++) {
        for (sz = 0; sz < 3; sz++) {
            in = no_insets();
            if (r == 1) {
                in.left = 100.0f;
            }
            l = ico_touch_layout(size[r][0], size[r][1], in, sz);
            check_layout_common(&l);
        }
    }

    /* 1920 x 1080 medium, by the numbers (u = 1080) */
    l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    CHECK(near_(l.unit, 1080.0f, 0.01f));
    CHECK(near_(l.stickR, 129.6f, 0.01f));
    CHECK(near_(l.runR, 121.824f, 0.01f));
    CHECK(near_(l.stickArea.x, 0.0f, 0.01f) && near_(l.stickArea.y, 324.0f, 0.01f));
    CHECK(near_(l.stickArea.w, 768.0f, 0.01f) && near_(l.stickArea.h, 756.0f, 0.01f));
    CHECK(near_(l.lookArea.x, 768.0f, 0.01f) && near_(l.lookArea.w, 1152.0f, 0.01f));
    CHECK(near_(l.lookArea.h, 486.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L1].rect.x, 43.2f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_R1].rect.x + l.button[ICO_TOUCH_B_R1].rect.w, 1876.8f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_CROSS].rect.y + l.button[ICO_TOUCH_B_CROSS].rect.h, 1036.8f,
                0.01f));
    CHECK(l.button[ICO_TOUCH_B_CROSS].round && !l.button[ICO_TOUCH_B_UP].round);

    /* sizes scale R */
    CHECK(near_(ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_SMALL).stickR, 129.6f * 0.85f,
                0.01f));
    CHECK(near_(ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_LARGE).stickR, 129.6f * 1.2f,
                0.01f));
    CHECK(ico_touch_layout(1920, 1080, no_insets(), 7).size == ICO_TOUCH_MEDIUM);

    /* 2400 x 1080 with a 100 px notch on the left: everything moves in */
    in = no_insets();
    in.left = 100.0f;
    l = ico_touch_layout(2400, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.x, 100.0f, 0.01f) && near_(l.safeRect.w, 2300.0f, 0.01f));
    CHECK(near_(l.stickArea.x, 100.0f, 0.01f) && near_(l.stickArea.w, 920.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L1].rect.x, 143.2f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_L2].rect.x, 143.2f, 0.01f));
    CHECK(near_(l.lookArea.x, 1020.0f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_SELECT].rect.x + l.button[ICO_TOUCH_B_SELECT].rect.w,
                1250.0f - 16.2f, 0.01f));
    CHECK(near_(l.button[ICO_TOUCH_B_R1].rect.x + l.button[ICO_TOUCH_B_R1].rect.w, 2356.8f, 0.01f));

    /* 2340 x 1080 */
    l = ico_touch_layout(2340, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    CHECK(near_(l.button[ICO_TOUCH_B_START].rect.x, 1170.0f + 16.2f, 0.01f));
    CHECK(near_(l.faceX, 2340.0f - 43.2f - 145.8f - 81.0f, 0.01f));

    /* insets that leave nothing are ignored; negative ones are zero */
    in.left = 1500.0f;
    in.right = 1000.0f;
    l = ico_touch_layout(2400, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.x, 0.0f, 0.01f) && near_(l.safeRect.w, 2400.0f, 0.01f));
    in = no_insets();
    in.top = -20.0f;
    l = ico_touch_layout(1920, 1080, in, ICO_TOUCH_MEDIUM);
    CHECK(near_(l.safeRect.y, 0.0f, 0.01f));
}

static void test_stick(void)
{
    IcoTouchLayout l = ico_touch_layout(1920, 1080, no_insets(), ICO_TOUCH_MEDIUM);
    IcoTouchState t;
    IcoTouchDrawInfo d;
    IcoVirtualPad v;
    float R = l.stickR, cx = 300.0f, cy = 700.0f;

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
    ev(&t, 8, 600.0f, 900.0f, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(near_(d.stickCX, cx, 0.01f) && near_(v.lx, -0.94f, 0.002f));
    ev(&t, 8, 600.0f, 900.0f, ICO_TOUCH_UP, 1 * S);

    /* the stick follows a finger that leaves its area */
    ev(&t, 7, cx + 0.5f * R, 100.0f, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.ly < -0.9f && step(&t, 1 * S).buttons == 0);

    /* lifted: centred, home again; the next finger down is a new centre */
    ev(&t, 7, cx + 0.5f * R, cy, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    d = ico_touch_draw_info(&t, &l);
    CHECK(v.lx == 0.0f && v.ly == 0.0f && !d.stickActive);
    CHECK(near_(d.stickCX, l.stickHomeX, 0.01f));
    ev(&t, 9, 500.0f, 800.0f, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 9, 500.0f, 800.0f + R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.ly, 1.0f, 0.002f) && near_(v.lx, 0.0f, 0.002f));
    d = ico_touch_draw_info(&t, &l);
    CHECK(near_(d.stickCX, 500.0f, 0.01f) && near_(d.stickCY, 800.0f, 0.01f));

    /* a finger outside every zone does nothing */
    memset(&t, 0, sizeof(t));
    ev(&t, 1, 1100.0f, 900.0f, ICO_TOUCH_DOWN, 1 * S); /* bottom middle */
    ev(&t, 1, 1300.0f, 700.0f, ICO_TOUCH_MOVE, 1 * S);
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
    float x, y, x2, y2;
    int z;

    g_l = &l;
    memset(&t, 0, sizeof(t));

    /* every button by its centre */
    for (z = 0; z < ICO_TOUCH_BUTTONS; z++) {
        centre_of(&l, z, &x, &y);
        ev(&t, 1, x, y, ICO_TOUCH_DOWN, 1 * S);
        v = step(&t, 1 * S);
        CHECK(v.buttons == l.button[z].pad);
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
    ev(&t, 10, 250.0f, 800.0f, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 10, 250.0f + l.stickR, 800.0f, ICO_TOUCH_MOVE, 1 * S);
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
    ev(&t, 12, 1300.0f, 800.0f, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && near_(v.lx, 1.0f, 0.002f));
    /* and back onto a button (Square): a button finger presses what it is on */
    centre_of(&l, ICO_TOUCH_B_SQUARE, &x, &y);
    ev(&t, 12, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons == ICO_PAD_SQUARE);
    /* sliding onto the look pad does not turn it into a look finger */
    ev(&t, 12, 1300.0f, 300.0f, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.rx == 0.0f && v.ry == 0.0f);

    /* a button over the look pad (R1) wins over the pad */
    centre_of(&l, ICO_TOUCH_B_R1, &x, &y);
    CHECK(x >= l.lookArea.x && y < l.lookArea.y + l.lookArea.h);
    ev(&t, 13, x, y, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 13, x - 400.0f, y, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.rx == 0.0f); /* moved off before the step: nothing */
    ev(&t, 13, x, y, ICO_TOUCH_MOVE, 1 * S);
    CHECK(step(&t, 1 * S).buttons & ICO_PAD_R1);

    /* cancel: everything released at once */
    ev(&t, 14, 1400.0f, 200.0f, ICO_TOUCH_DOWN, 1 * S); /* the look pad */
    ev(&t, 14, 1400.0f + l.lookR, 200.0f, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 1.0f, 0.002f) && near_(v.lx, 1.0f, 0.002f) && (v.buttons & ICO_PAD_R1));
    ico_touch_event(&t, 0, 0.0f, 0.0f, ICO_TOUCH_CANCEL, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.ly == 0.0f && v.rx == 0.0f && v.ry == 0.0f);
    CHECK(ico_touch_draw_info(&t, &l).pressed == 0 && !ico_touch_draw_info(&t, &l).stickActive);
    /* the fingers are gone: their moves and lifts do nothing */
    ev(&t, 10, 250.0f + l.stickR, 900.0f, ICO_TOUCH_MOVE, 1 * S);
    ev(&t, 11, x, y, ICO_TOUCH_UP, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.buttons == 0 && v.lx == 0.0f && v.ly == 0.0f);

    /* more fingers than slots: the extra ones are ignored */
    centre_of(&l, ICO_TOUCH_B_START, &x, &y);
    for (z = 0; z < ICO_TOUCH_FINGERS; z++) {
        ev(&t, 100 + (uint64_t)z, 1100.0f, 900.0f, ICO_TOUCH_DOWN, 1 * S);
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
    float R = l.lookR;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, 1 * S);
    CHECK(near_(t.look_decay, 0.80f, 1e-6f));

    ev(&t, 1, 1200.0f, 250.0f, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(v.rx == 0.0f && v.ry == 0.0f && v.lx == 0.0f);
    ev(&t, 1, 1200.0f + 0.5f * R, 250.0f - 0.25f * R, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 0.5f, 0.002f) && near_(v.ry, -0.25f, 0.002f));
    d = ico_touch_draw_info(&t, &l);
    CHECK(d.lookActive && near_(d.lookCX, 1200.0f, 0.01f) &&
          near_(d.lookFX, 1200.0f + 0.5f * R, 0.01f));
    /* held still: the deflection holds (a stick, not mouse motion) */
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, 0.5f, 0.002f));
    /* clamped to the unit circle */
    ev(&t, 1, 1200.0f - 2.0f * R, 250.0f, ICO_TOUCH_MOVE, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -1.0f, 0.002f) && near_(v.ry, 0.0f, 0.002f));
    /* a second finger on the pad does nothing */
    ev(&t, 2, 1500.0f, 300.0f, ICO_TOUCH_DOWN, 1 * S);
    v = step(&t, 1 * S);
    CHECK(near_(v.rx, -1.0f, 0.002f));
    ev(&t, 2, 1500.0f, 300.0f, ICO_TOUCH_UP, 1 * S);
    /* released: decays like the mouse look, 0.8 per step */
    ev(&t, 1, 1200.0f - 2.0f * R, 250.0f, ICO_TOUCH_UP, 1 * S);
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
    ev(&t, 3, 1200.0f, 250.0f, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 3, 1200.0f + R, 250.0f, ICO_TOUCH_MOVE, 1 * S);
    step(&t, 1 * S);
    ev(&t, 3, 1200.0f + R, 250.0f, ICO_TOUCH_UP, 1 * S);
    CHECK(near_(step(&t, 1 * S).rx, 0.5f, 0.002f));
    /* a zeroed state centres at once */
    memset(&t, 0, sizeof(t));
    ev(&t, 3, 1200.0f, 250.0f, ICO_TOUCH_DOWN, 1 * S);
    ev(&t, 3, 1200.0f + R, 250.0f, ICO_TOUCH_MOVE, 1 * S);
    CHECK(near_(step(&t, 1 * S).rx, 1.0f, 0.002f));
    ev(&t, 3, 1200.0f + R, 250.0f, ICO_TOUCH_UP, 1 * S);
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
    float R = l.stickR, x, y;

    g_l = &l;
    memset(&t, 0, sizeof(t));
    ico_touch_reset(&t, S);

    /* the bindings' pad: Start held, a small keyboard push left (walk) */
    memset(&bind, 0, sizeof(bind));
    bind.buttons = ICO_PAD_START;
    bind.lx = -0.3f;

    /* touch: the stick full down-right at 45 degrees, Cross */
    ev(&t, 1, 400.0f, 700.0f, ICO_TOUCH_DOWN, S);
    ev(&t, 1, 400.0f + 2.0f * R, 700.0f + 2.0f * R, ICO_TOUCH_MOVE, S);
    centre_of(&l, ICO_TOUCH_B_CROSS, &x, &y);
    ev(&t, 2, x, y, ICO_TOUCH_DOWN, S);
    ico_touch_step(&t, &l, &tv, S);
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
    ev(&t, 1, 400.0f + 2.0f * R, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 255 && f.ly == 128 && game_magnitude(f.lx, f.ly) == 1.0f);
    ico_input_vpad_to_frame(&v, 1, 0, &f);
    CHECK(f.lx == 255 && f.ly == 128);
    ev(&t, 1, 400.0f + l.runR, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 247 && f.ly == 128 && game_magnitude(f.lx, f.ly) >= 0.99f);
    ev(&t, 1, 400.0f + 0.6f * R, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(f.lx == 204 && near_(game_magnitude(f.lx, f.ly), 0.3958f, 0.002f));
    ev(&t, 1, 400.0f + 0.94f * R, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    ico_input_vpad_to_frame(&tv, 0, 0, &f);
    CHECK(game_magnitude(f.lx, f.ly) >= 0.99f);
    ev(&t, 1, 400.0f + 0.92f * R, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    ico_input_vpad_to_frame(&tv, 0, 0, &f);
    CHECK(game_magnitude(f.lx, f.ly) < 0.99f);

    /* the touch stick in its dead zone loses to the keyboard's push */
    ev(&t, 1, 400.0f + 0.03f * R, 700.0f, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
    v = bind;
    ico_vpad_merge(&v, &tv);
    ico_input_vpad_to_frame(&v, 0, 0, &f);
    CHECK(near_(v.lx, -0.3f, 1e-6f) && f.lx == ico_input_quantise(-0.3f) && f.ly == 128);

    /* the look pad merges into the right stick */
    ev(&t, 3, 1500.0f, 200.0f, ICO_TOUCH_DOWN, S);
    ev(&t, 3, 1500.0f, 200.0f - R, ICO_TOUCH_MOVE, S);
    ico_touch_step(&t, &l, &tv, S);
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
    const float R = l.stickR, cx = 420.0f, cy = 760.0f;
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
            ico_touch_step(&t, &l, &tv, S);
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
    ev(&t, 2, 400.0f, 700.0f, ICO_TOUCH_DOWN, S);
    ev(&t, 2, 400.0f + l.stickR, 700.0f, ICO_TOUCH_MOVE, S);
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
    ev(&t, 2, 400.0f, 700.0f, ICO_TOUCH_UP, S);

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
