/*
 * port/game/test/yorda_look_test.c
 *
 * Look at Yorda's press, toggle and end rules (port/game/yorda_look.h) on
 * the CPU: Off answers 0 over pseudo-random input; Hold looks while L3 is
 * held; Toggle starts with a press and ends with the next, a held press
 * does not repeat; a toggled look ends without control, with her hand
 * taken, on a new stage and with a Camera reset (camera_reset.h, the real
 * call), and an L3 held through any of those is no press afterwards; the
 * Main tick clock (a pause keeps the toggle, L3 down at the first tick back
 * is held); a copy of the arms' use (boyact.c subBoyCollision: the head and
 * the camera); the save-state block (one FRESH block whose bytes copied
 * back resume the toggle).
 */
#include <stdio.h>
#include <string.h>

#include "camera_reset.h"
#include "config.h"
#include "options.h"
#include "state_registry.h"
#include "yorda_look.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define L3 0x200
#define R3 0x400

static unsigned s_rng = 2266u;

static unsigned rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) & 0x7fffu;
}

/* one tick under control, no hand, stage 3 */
static int tick(int l3)
{
    return ico_yorda_look(l3 ? L3 : 0, 1, 0, 3);
}

static void fresh(int type)
{
    ico_yorda_look_clear();
    ico_opt_set_yorda_look(type);
}

static void test_off(void)
{
    int i, none = 0;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_OFF);
    for (i = 0; i < 2000; i++) {
        const int l3 = (rnd() & 1) ? L3 : 0;
        const int live = (rnd() % 4) != 0;
        const int hand = (rnd() % 5) == 0;
        const int stage = (int)(rnd() % 3);

        none += ico_yorda_look(l3, live, hand, stage) == 0;
    }
    CHECK(none == 2000);
    CHECK(ico_yorda_look_count() == n0);
    /* a toggle left on when the row goes Off: gone, and not back on Toggle */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_OFF);
    CHECK(tick(1) == 0);
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(1) == 0 && tick(0) == 0);
}

static void test_hold(void)
{
    int i;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_HOLD);
    CHECK(tick(0) == 0);
    for (i = 0; i < 30; i++) {
        CHECK(tick(1) == 1);
    }
    CHECK(tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 1); /* one look started */
    CHECK(tick(1) == 1 && tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 2);
    /* not under control or holding her hand: no look while held */
    CHECK(ico_yorda_look(L3, 0, 0, 3) == 0);
    CHECK(ico_yorda_look(L3, 1, 1, 3) == 0);
    CHECK(tick(1) == 1); /* Hold follows the button: control back, still held */
}

static void test_toggle(void)
{
    int i;
    const unsigned long n0 = ico_yorda_look_count();

    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0);
    CHECK(tick(1) == 1); /* a press starts it */
    for (i = 0; i < 30; i++) {
        CHECK(tick(1) == 1); /* held: no repeat */
    }
    for (i = 0; i < 30; i++) {
        CHECK(tick(0) == 1); /* let go: it stays on */
    }
    CHECK(tick(1) == 0); /* the second press ends it */
    CHECK(tick(1) == 0 && tick(0) == 0);
    CHECK(ico_yorda_look_count() == n0 + 1);
    CHECK(tick(1) == 1 && ico_yorda_look_count() == n0 + 2);
}

/* the ends of a toggled look, and an L3 held through each is no press */
static void test_ends(void)
{
    /* no control (a cutscene, the cut to her capture, Free movement) */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1 && tick(0) == 1);
    CHECK(ico_yorda_look(0, 0, 0, 3) == 0);
    CHECK(tick(0) == 0); /* not back with control */
    CHECK(tick(1) == 1);
    CHECK(ico_yorda_look(L3, 0, 0, 3) == 0);
    CHECK(tick(1) == 0); /* held through it: no press */
    CHECK(tick(0) == 0 && tick(1) == 1);
    /* her hand taken; a press while holding it starts nothing */
    CHECK(ico_yorda_look(0, 1, 1, 3) == 0);
    CHECK(tick(0) == 0);
    CHECK(ico_yorda_look(L3, 1, 1, 3) == 0);
    CHECK(ico_yorda_look(0, 1, 1, 3) == 0 && tick(0) == 0);
    /* a new stage */
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_yorda_look(0, 1, 0, 4) == 0);
    CHECK(ico_yorda_look(0, 1, 0, 4) == 0);
    CHECK(ico_yorda_look(L3, 1, 0, 4) == 1);
    /* a stage change on the tick of a press: the press starts a new look */
    CHECK(ico_yorda_look(0, 1, 0, 4) == 1);
    CHECK(ico_yorda_look(L3, 1, 0, 5) == 1);
    CHECK(ico_yorda_look(0, 1, 0, 5) == 1);
}

static void test_camera_reset(void)
{
    float sx = 0.0f, sz = 0.0f;

    /* Camera reset on: R3 ends a toggled look (camera_reset.c) */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    ico_cam_reset_clear();
    ico_opt_set_camera_reset(1);
    CHECK(tick(0) == 0 && tick(1) == 1 && tick(0) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0);
    CHECK(ico_cam_reset(R3, 1, &sx, &sz) == 1);
    CHECK(tick(0) == 0);
    /* Hold: L3 held keeps looking (the player's own input) */
    ico_opt_set_yorda_look(ICO_YORDA_LOOK_HOLD);
    CHECK(tick(1) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0 && ico_cam_reset(R3, 1, &sx, &sz) == 1);
    CHECK(tick(1) == 1);
    /* Camera reset off: R3 changes nothing */
    ico_opt_set_camera_reset(0);
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    CHECK(ico_cam_reset(0, 1, &sx, &sz) == 0 && ico_cam_reset(R3, 1, &sx, &sz) == 0);
    CHECK(tick(0) == 1);
    ico_cam_reset_clear();
}

/* the Main tick clock (ico_yorda_look_set_clock) */
static unsigned s_tick;

static unsigned fake_clock(void)
{
    return s_tick;
}

static void test_clock(void)
{
    fresh(ICO_YORDA_LOOK_TOGGLE);
    ico_yorda_look_set_clock(fake_clock);
    s_tick = 200;
    CHECK(tick(0) == 0);
    s_tick = 201;
    CHECK(tick(1) == 1);
    s_tick = 202;
    CHECK(tick(0) == 1);
    /* a pause or photo mode (the boy's thread does not run for 50 ticks):
       the look is still on after it, and L3 down at the first tick back is
       held, not a press */
    s_tick = 253;
    CHECK(tick(1) == 1);
    s_tick = 254;
    CHECK(tick(0) == 1);
    s_tick = 255;
    CHECK(tick(1) == 0); /* a real press ends it */
    /* the same with the look off: L3 down at the first tick back starts
       nothing */
    s_tick = 300;
    CHECK(tick(1) == 0);
    s_tick = 301;
    CHECK(tick(0) == 0);
    s_tick = 302;
    CHECK(tick(1) == 1);
    /* a second call in the same tick is not a new stretch */
    CHECK(tick(1) == 1);
    /* a load rewinds the clock */
    s_tick = 40;
    CHECK(tick(0) == 1);
    s_tick = 41;
    CHECK(tick(1) == 0);
    ico_yorda_look_set_clock(NULL);
}

/* A copy of the arms' use (boyact.c, the four subBoyCollision copies:
   options_test finds the call four times).  With Yorda on the stage the boy
   turns his head to her when the call looks (call), the second player's R1
   asks (p2) or the look asks; the camera follows when she was off the screen
   as the call began (inScreen 0) or the look asks, and the stage's script
   allows the call camera (camFlag).  Without her the camera turns toward her
   other stage when R1 or the look asks.  No call: no wish, no mail. */
typedef struct {
    int head, camera, other;
} Arm;

static Arm arm(int look, int girl, int call, int p2, int inScreen, int camFlag, int r1)
{
    Arm a = {0, 0, 0};

    if (girl && (call || p2 || look)) {
        a.head = 1;
        a.camera = (inScreen == 0 || look) && camFlag;
    }
    if (!girl && (r1 || look)) {
        a.other = 1;
    }
    return a;
}

static void test_arm(void)
{
    Arm a;
    int i, same = 0;

    /* off: the arms decide as the game does, over every input */
    for (i = 0; i < 64; i++) {
        const int girl = i & 1, call = (i >> 1) & 1, p2 = (i >> 2) & 1, in = (i >> 3) & 1,
                  cf = (i >> 4) & 1, r1 = (i >> 5) & 1;
        const Arm g = arm(0, girl, call, p2, in, cf, r1);

        same += g.head == (girl && (call || p2)) &&
                g.camera == (girl && (call || p2) && in == 0 && cf) && g.other == (!girl && r1);
    }
    CHECK(same == 64);
    /* the look: head and camera, also with her already on the screen */
    a = arm(1, 1, 0, 0, 1, 1, 0);
    CHECK(a.head && a.camera && !a.other);
    /* the script's call camera off: the head only */
    a = arm(1, 1, 0, 0, 0, 0, 0);
    CHECK(a.head && !a.camera);
    /* during a call with her on the screen: the look moves the camera */
    a = arm(1, 1, 1, 0, 1, 1, 0);
    CHECK(a.head && a.camera);
    /* she is not on the stage: toward her other stage, as R1 */
    a = arm(1, 0, 0, 0, 0, 1, 0);
    CHECK(!a.head && a.other);
}

static void test_block(void)
{
    const IcoStateEntry *e = NULL;
    unsigned char saved[32];
    int i;

    ico_state_registry_reset();
    CHECK(ico_yorda_look_state_register() == 0);
    CHECK(ico_state_registry_count() == 1);
    for (i = 0; i < ico_state_registry_count(); i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "yorda_look.state") == 0) {
            e = ico_state_registry_entry(i);
        }
    }
    CHECK(e != NULL);
    if (e == NULL) {
        return;
    }
    CHECK(e->flags == ICO_STATE_FRESH && e->n == 4 * sizeof(int) && e->n <= sizeof saved);
    CHECK(ico_yorda_look_state_register() != 0);
    /* toggled on, saved; ended; copied back: on, and L3 held is no press */
    fresh(ICO_YORDA_LOOK_TOGGLE);
    CHECK(tick(0) == 0 && tick(1) == 1);
    memcpy(saved, e->p, e->n);
    CHECK(tick(0) == 1 && tick(1) == 0 && tick(0) == 0);
    memcpy(e->p, saved, e->n);
    CHECK(tick(1) == 1 && tick(0) == 1);
    ico_state_registry_reset();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_config_reset("/nonexistent/yorda_look_test.toml", "/nonexistent/yorda_look_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_yorda_look() == ICO_YORDA_LOOK_OFF); /* the default */
    test_off();
    test_hold();
    test_toggle();
    test_ends();
    test_camera_reset();
    test_clock();
    test_arm();
    test_block();
    if (failures != 0) {
        fprintf(stderr, "yorda_look_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("yorda_look_test: ok\n");
    return 0;
}
