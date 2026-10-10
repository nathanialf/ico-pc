/*
 * port/game/test/door_grace_test.c
 *
 * The doorway fix (port/game/door_grace.h, issue 53) on the CPU: the arm
 * and the grace with the option off and on, holding hands or not, a stage
 * start elsewhere, no arm, the frame count, the other way through the door,
 * and a copy of the exit check's guard (ico2/fumi/src/boyact.c,
 * CheckCollisionAttr) driven frame by frame through an arrival.
 */
#include <stdio.h>
#include "config.h"
#include "door_grace.h"
#include "options.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* The East Reflector (stage 21) and the stage below it (20): the ladder top's
   exit from 21 arrives in 20 beside 20's exit slot 3, which leads back to 21 */
#define STAGE_LEFT 21
#define STAGE_IN 20
#define GRACE 90

static void fresh(int on)
{
    ico_door_grace_reset();
    ico_opt_set_door_fix(on);
}

static void test_off(void)
{
    fresh(0);
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 0);
    CHECK(ico_door_grace_active() == 0);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 0);
}

static void test_on_holding(void)
{
    int i;

    fresh(1);
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 1);
    CHECK(ico_door_grace_active() == 1);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 1);
    /* only the door back to the stage left */
    CHECK(ico_door_grace_holds(5) == 0);
    CHECK(ico_door_grace_holds(-1) == 0);
    for (i = 0; i < GRACE - 1; i++) {
        ico_door_grace_tick();
    }
    CHECK(ico_door_grace_active() == 1);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 1);
    ico_door_grace_tick();
    CHECK(ico_door_grace_active() == 0);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 0);
    /* a tick after the end stays ended */
    ico_door_grace_tick();
    CHECK(ico_door_grace_active() == 0);
}

static void test_not_holding(void)
{
    fresh(1);
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 0);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 0);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 0);
}

static void test_other_stage(void)
{
    fresh(1);
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    /* a stage start that is not the exit's destination (a script's stage
       change in between) uses the arm up */
    CHECK(ico_door_grace_begin(7, GRACE) == 0);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 0);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 0);
    CHECK(ico_door_grace_active() == 0);
}

static void test_no_arm(void)
{
    fresh(1);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 0);
    CHECK(ico_door_grace_active() == 0);
    /* a stage start without an arm ends a grace still running */
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 1);
    CHECK(ico_door_grace_begin(STAGE_LEFT, GRACE) == 0);
    CHECK(ico_door_grace_active() == 0);
    /* no frames, no grace */
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    CHECK(ico_door_grace_begin(STAGE_IN, 0) == 0);
    CHECK(ico_door_grace_active() == 0);
}

static void test_rearm(void)
{
    fresh(1);
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    CHECK(ico_door_grace_begin(STAGE_IN, GRACE) == 1);
    /* back the other way: the grace now guards the door to 20 */
    ico_door_grace_arm(STAGE_IN, STAGE_LEFT, 1);
    CHECK(ico_door_grace_begin(STAGE_LEFT, GRACE) == 1);
    CHECK(ico_door_grace_holds(STAGE_IN) == 1);
    CHECK(ico_door_grace_holds(STAGE_LEFT) == 0);
}

/* --- the exit check's guard ---------------------------------------------
   A static copy of the guard logic of CheckCollisionAttr in
   ico2/fumi/src/boyact.c (the loop over the exit floors, the remembered
   floor w[4], the PC-port grace), with the floor test replaced by the one
   slot Ico stands on (0: none) and RequestStageChange by returning the
   slot that fires.  Stage 20's exit slots: 3 leads back to 21, 1 to 5. */
static int s_w4;

static int exit_dest(int slot)
{
    return slot == 3 ? STAGE_LEFT : slot == 1 ? 5 : -1;
}

static int guard_frame(int on_slot)
{
    int i;
    int flag = 1;

    if (ico_door_grace_active()) {
        ico_door_grace_tick();
    }
    for (i = 1; i < 16; i++) {
        if (on_slot == i) {
            flag = 0;
            if (exit_dest(i) >= 0 && ico_door_grace_holds(exit_dest(i))) {
                s_w4 = i;
                return 0;
            }
            if (s_w4 < 0) {
                s_w4 = i;
                return 0;
            }
            if (s_w4 != i) {
                return i;
            }
        }
    }
    if (flag) {
        s_w4 = 0xFF;
    }
    return 0;
}

/* an arrival in 20 from 21 holding hands: actBoyStart's reset and begin */
static void arrive(void)
{
    ico_door_grace_arm(STAGE_LEFT, STAGE_IN, 1);
    s_w4 = -1;
    ico_door_grace_begin(STAGE_IN, GRACE);
}

static void test_scenario(void)
{
    int f, fired;

    /* option on: off the floor on arrival, pushed onto slot 3 on frame 5,
       off on 20, on again on 40: no stage change during the grace */
    fresh(1);
    arrive();
    fired = 0;
    for (f = 0; f < 60; f++) {
        int on = (f >= 5 && f < 20) || f >= 40 ? 3 : 0;
        fired |= guard_frame(on);
    }
    CHECK(fired == 0);
    /* still on the floor when the grace ends: no change until a step off and
       back on, the game's own rule */
    for (; f < 120; f++) {
        fired |= guard_frame(3);
    }
    CHECK(fired == 0);
    CHECK(ico_door_grace_active() == 0);
    CHECK(guard_frame(0) == 0);
    CHECK(guard_frame(3) == 3);

    /* option on: off the floor when the grace ends, the door back works */
    fresh(1);
    arrive();
    for (f = 0; f < GRACE; f++) {
        CHECK(guard_frame(0) == 0);
    }
    CHECK(guard_frame(3) == 3);

    /* option on: another exit during the grace works as before */
    fresh(1);
    arrive();
    CHECK(guard_frame(0) == 0);
    CHECK(guard_frame(1) == 1);

    /* option off: the push on frame 5 sends them back (the original bug) */
    fresh(0);
    arrive();
    fired = 0;
    for (f = 0; f < 6 && fired == 0; f++) {
        int r = guard_frame(f >= 5 ? 3 : 0);
        if (r != 0) {
            CHECK(f == 5);
            fired = r;
        }
    }
    CHECK(fired == 3);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ico_config_reset("/nonexistent/door_grace_test.toml", "/nonexistent/door_grace_test.ini");
    ico_opt_reload();
    CHECK(ico_opt_door_fix() == 0);
    test_off();
    test_on_holding();
    test_not_holding();
    test_other_stage();
    test_no_arm();
    test_rearm();
    test_scenario();
    if (failures != 0) {
        fprintf(stderr, "door_grace_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("door_grace_test: ok\n");
    return 0;
}
