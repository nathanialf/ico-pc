/*
 * port/game/test/zoom_toggle_test.c
 *
 * Zoom type's latch (port/game/zoom_toggle.h) on the CPU: Hold returns the
 * game's own R2 test tick for tick over pseudo-random ticks; Toggle's press
 * and release, the second press, a long hold, every clear (a script's zoom,
 * the source unable to deliver the bit, a new source, a new camera mode, a
 * stage start), R2 still held when control returns (a press), both switches
 * of the type, a copy of the camera arm's step choice (the game's steps under
 * Hold), the June 2001 look-around's test, and the save-state block (one FRESH block whose bytes copied back resume
 * the latch).
 */
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "options.h"
#include "state_registry.h"
#include "zoom_toggle.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define BOY ICO_ZOOM_SRC_BOY
#define PAD ICO_ZOOM_SRC_PAD
#define LOOK ICO_ZOOM_SRC_LOOK
#define PLAY 3 /* the game camera's mode (cameraMode 3) */
#define PATH 4 /* a cutscene's path camera */

/* one tick of the boy's pad in play: R2's bit as the pad gives it (now & 2) */
static int tick(int r2)
{
    return ico_zoom_held(r2 ? 2 : 0, 0, 1, BOY, PLAY);
}

static void fresh(int type)
{
    ico_zoom_reset();
    ico_opt_set_zoom_type(type);
    (void)tick(0); /* the first tick after a reset only records */
}

/* a small LCG, the same sequence every run */
static unsigned s_rng = 12345u;

static unsigned rnd(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (s_rng >> 16) & 0x7fffu;
}

static void test_hold_is_the_game(void)
{
    int i, same = 0;

    ico_zoom_reset();
    ico_opt_set_zoom_type(ICO_ZOOM_HOLD);
    for (i = 0; i < 2000; i++) {
        const int held = (rnd() & 1) ? 2 : 0;
        const int override = (rnd() % 7) == 0;
        const int live = (rnd() % 5) != 0;
        const int source = (int)(rnd() % 3);
        const int mode = (rnd() % 4) == 0 ? PATH : PLAY;
        const int r = ico_zoom_held(held, override, live, source, mode);

        same += r == held;
        CHECK(ico_zoom_wanted(held) == held);
    }
    CHECK(same == 2000);
}

static void test_toggle_press(void)
{
    int i;

    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(0) == 0);
    /* press, release: in, and stays in */
    CHECK(tick(1) == 1);
    CHECK(tick(0) == 1);
    for (i = 0; i < 30; i++) {
        CHECK(tick(0) == 1);
    }
    /* the second press: out */
    CHECK(tick(1) == 0);
    CHECK(tick(0) == 0);
    CHECK(tick(0) == 0);
    /* a level held 50 ticks toggles once */
    for (i = 0; i < 50; i++) {
        CHECK(tick(1) == 1);
    }
    CHECK(tick(0) == 1);
    for (i = 0; i < 50; i++) {
        CHECK(tick(1) == 0);
    }
    CHECK(tick(0) == 0);
}

static void test_toggle_clears(void)
{
    /* a script's zoom takes over: out, and a press during it does nothing
       after it ends */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_zoom_held(0, 1, 1, BOY, PLAY) == 0);
    CHECK(ico_zoom_held(2, 1, 1, BOY, PLAY) == 0);
    CHECK(ico_zoom_held(2, 1, 1, BOY, PLAY) == 0);
    CHECK(tick(1) == 0); /* still held from the override: no press */
    CHECK(tick(0) == 0);
    CHECK(tick(1) == 1); /* a new press */

    /* the source cannot deliver the bit (no boy control, the pad off) */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_zoom_held(0, 0, 0, BOY, PLAY) == 0);
    CHECK(tick(0) == 0);

    /* while control is off the game zeroes the boy's pad (boyact.c, the four
       boy loops; the 2001 discs' iosPadRead with the pad off), so R2 still
       held when control returns is a press: the camera zooms in */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(ico_zoom_held(0, 0, 0, BOY, PLAY) == 0);
    CHECK(ico_zoom_held(0, 0, 0, BOY, PLAY) == 0);
    CHECK(tick(1) == 1);
    CHECK(tick(1) == 1 && tick(0) == 1);

    /* a new source: the boy leaves (the camera's own pad) */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_zoom_held(0, 0, 1, PAD, PLAY) == 0);
    CHECK(ico_zoom_held(0, 0, 1, PAD, PLAY) == 0);
    CHECK(ico_zoom_held(2, 0, 1, PAD, PLAY) == 1); /* a press on the new source */

    /* a new camera mode: a cutscene's path camera starts; the look-around
       toggles inside it, and ending it zooms out */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_zoom_held(0, 0, 1, LOOK, PATH) == 0);
    CHECK(ico_zoom_held(2, 0, 1, LOOK, PATH) == 1);
    CHECK(ico_zoom_held(0, 0, 1, LOOK, PATH) == 1);
    CHECK(tick(0) == 0);
    /* the mode alone, the source the same */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    CHECK(ico_zoom_held(0, 0, 1, BOY, PATH) == 0);
    CHECK(ico_zoom_held(0, 0, 1, BOY, PATH) == 0);

    /* a stage start */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    ico_zoom_reset();
    CHECK(ico_zoom_wanted(0) == 0);
    CHECK(tick(0) == 0);
    /* R2 held into the new stage: the first tick records, no press */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    ico_zoom_reset();
    CHECK(tick(1) == 0 && tick(1) == 0 && tick(0) == 0);
}

static void test_switches(void)
{
    /* Toggle to Hold while zoomed in: the latch clears; back to Toggle it
       stays out */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    ico_opt_set_zoom_type(ICO_ZOOM_HOLD);
    CHECK(ico_zoom_held(0, 0, 1, BOY, PLAY) == 0);
    CHECK(ico_zoom_held(2, 0, 1, BOY, PLAY) == 2);
    CHECK(ico_zoom_held(0, 0, 1, BOY, PLAY) == 0);
    ico_opt_set_zoom_type(ICO_ZOOM_TOGGLE);
    CHECK(tick(0) == 0 && ico_zoom_wanted(0) == 0);

    /* Hold to Toggle while R2 is held: no press */
    fresh(ICO_ZOOM_HOLD);
    CHECK(ico_zoom_held(2, 0, 1, BOY, PLAY) == 2);
    ico_opt_set_zoom_type(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 0);
    CHECK(tick(0) == 0);
    CHECK(tick(1) == 1);
}

/* A copy of the zoom block's step choice in camera-root.c's ntsc, aug6 and
   jun26 arms (the European body differs only in clearing zoomRequest after
   the chain).  The arms themselves need the whole game (GObj, the pads,
   Camctrl, gsb) and are not built into a CPU test; options_test finds the
   two lines below in camera-root.c four times each, so the copy is the
   arm's test as written:
       held = ico_zoom_held(p->now & 2, zoomRequest != 0,
       } else if (held) { */
static int arm_step(int now, int *zoomRequest, int boyControl, int ply)
{
    int step;
    const int held = ico_zoom_held(now & 2, *zoomRequest != 0, ply != 0 ? boyControl : 1,
                                   ply != 0 ? BOY : PAD, PLAY);

    if (*zoomRequest != 0) {
        step = 2;
        *zoomRequest = 0;
    } else if (held) {
        step = 1;
    } else {
        step = 0;
    }
    return step;
}

/* the game's own choice, before the port's hook */
static int game_step(int now, int *zoomRequest)
{
    int step;

    if (*zoomRequest != 0) {
        step = 2;
        *zoomRequest = 0;
    } else if (now & 2) {
        step = 1;
    } else {
        step = 0;
    }
    return step;
}

static void test_arm(void)
{
    int i, same = 0, zr;

    /* Hold: the arm's steps are the game's over random pads, script zooms,
       boy control and boy presence */
    ico_zoom_reset();
    ico_opt_set_zoom_type(ICO_ZOOM_HOLD);
    for (i = 0; i < 2000; i++) {
        const int now = (int)(rnd() & 0xffff);
        const int req = (rnd() % 9) == 0;
        const int ctl = (rnd() % 5) != 0;
        const int ply = (rnd() % 4) != 0;
        int z1 = req, z2 = req;
        const int a = arm_step(now, &z1, ctl, ply);
        const int g = game_step(now, &z2);

        same += a == g && z1 == z2;
    }
    CHECK(same == 2000);

    /* Toggle: a press and release zoom in (step 1) and stay in; a script's
       zoom is step 2 and zooms out after; the next press zooms in again */
    ico_zoom_reset();
    ico_opt_set_zoom_type(ICO_ZOOM_TOGGLE);
    zr = 0;
    CHECK(arm_step(0, &zr, 1, 1) == 0);
    CHECK(arm_step(2, &zr, 1, 1) == 1);
    CHECK(arm_step(0, &zr, 1, 1) == 1);
    CHECK(arm_step(0, &zr, 1, 1) == 1);
    zr = 1;
    CHECK(arm_step(0, &zr, 1, 1) == 2 && zr == 0);
    CHECK(arm_step(0, &zr, 1, 1) == 0);
    CHECK(arm_step(2, &zr, 1, 1) == 1);
    /* the second press: out */
    CHECK(arm_step(0, &zr, 1, 1) == 1);
    CHECK(arm_step(2, &zr, 1, 1) == 0);
    CHECK(arm_step(0, &zr, 1, 1) == 0);
}

static void test_wanted(void)
{
    /* the June 2001 look-around: Hold reads R2, Toggle the latch */
    fresh(ICO_ZOOM_HOLD);
    CHECK(ico_zoom_wanted(2) == 2 && ico_zoom_wanted(0) == 0);
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(ico_zoom_wanted(2) == 0);
    CHECK(tick(1) == 1);
    CHECK(ico_zoom_wanted(0) == 1 && ico_zoom_wanted(2) == 1);
    CHECK(tick(0) == 1 && tick(1) == 0);
    CHECK(ico_zoom_wanted(2) == 0);
}

static void test_block(void)
{
    const IcoStateEntry *e = NULL;
    unsigned char saved[64];
    int i, n;

    ico_state_registry_reset();
    CHECK(ico_zoom_state_register() == 0);
    n = ico_state_registry_count();
    CHECK(n == 1);
    for (i = 0; i < n; i++) {
        if (strcmp(ico_state_registry_entry(i)->name, "zoom_toggle.state") == 0) {
            e = ico_state_registry_entry(i);
        }
    }
    CHECK(e != NULL);
    if (e == NULL) {
        return;
    }
    CHECK(e->flags == ICO_STATE_FRESH);
    CHECK(e->n == 4 * sizeof(int) && e->n <= sizeof saved);
    /* a second registration of the name is refused */
    CHECK(ico_zoom_state_register() != 0);

    /* zoomed in, saved; out again; the bytes copied back: in, and the next
       press zooms out */
    fresh(ICO_ZOOM_TOGGLE);
    CHECK(tick(1) == 1 && tick(0) == 1);
    memcpy(saved, e->p, e->n);
    CHECK(tick(1) == 0 && tick(0) == 0);
    memcpy(e->p, saved, e->n);
    CHECK(ico_zoom_wanted(0) == 1);
    CHECK(tick(0) == 1);
    CHECK(tick(1) == 0);
    ico_state_registry_reset();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_config_reset("/nonexistent/zoom_toggle_test.toml", "/nonexistent/zoom_toggle_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_zoom_type() == ICO_ZOOM_HOLD); /* the default */
    test_hold_is_the_game();
    test_toggle_press();
    test_toggle_clears();
    test_switches();
    test_arm();
    test_wanted();
    test_block();
    if (failures != 0) {
        fprintf(stderr, "zoom_toggle_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("zoom_toggle_test: ok\n");
    return 0;
}
