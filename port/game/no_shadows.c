/*
 * port/game/no_shadows.c
 *
 * No Shadows' counters and log lines (no_shadows.h).  The answer itself is
 * the journey's row, difficulty.c ico_no_shadows().
 */
#include "no_shadows.h"

#include <stdio.h>

#include "difficulty.h"
#include "../platform/state_registry.h"

/* the room whose Shadows are being counted, its counts, the ticks since the
   last one started */
static int s_room = -1;
static int s_held_placed;
static int s_held_pit;
static int s_quiet;
static int s_pending;

/* the refusals in the current room, and over the run */
static int s_drop_room[ICO_NOSH_DROP_COUNT];
static int s_drop_total[ICO_NOSH_DROP_COUNT];

/* lines printed per kind (the rate limit) and in all */
static unsigned s_told_room;
static unsigned s_told_left;
static unsigned s_told_forced[ICO_NOSH_FORCED_COUNT];
static int s_lines;

/* the first 8, then every 100th */
static int tell(unsigned *n)
{
    unsigned k = (*n)++;

    return k < 8 || k % 100 == 0;
}

static void flush_held(void)
{
    if (!s_pending) {
        return;
    }
    s_pending = 0;
    if (tell(&s_told_room)) {
        fprintf(stderr,
                "no shadows: room %d: %d Shadow(s) held back (%d placed in the room, %d waiting "
                "in its pits); they start as killed Shadows do, out of sight\n",
                s_room, s_held_placed + s_held_pit, s_held_placed, s_held_pit);
        s_lines++;
    }
    s_held_placed = s_held_pit = 0;
}

static void leave_room(void)
{
    int any = 0;

    flush_held();
    for (int i = 0; i < ICO_NOSH_DROP_COUNT; i++) {
        any |= s_drop_room[i] != 0;
    }
    if (any && s_room >= 0 && tell(&s_told_left)) {
        fprintf(stderr,
                "no shadows: room %d left: %d pit call(s) held at 0, %d direct call(s), %d quick "
                "call(s) and %d spider revive(s) refused\n",
                s_room, s_drop_room[ICO_NOSH_DROP_PIT], s_drop_room[ICO_NOSH_DROP_DIRECT],
                s_drop_room[ICO_NOSH_DROP_QUICK], s_drop_room[ICO_NOSH_DROP_UNLOCK]);
        s_lines++;
    }
    for (int i = 0; i < ICO_NOSH_DROP_COUNT; i++) {
        s_drop_room[i] = 0;
    }
    s_held_placed = s_held_pit = 0;
}

int ico_no_shadows_hold(int label, int was_dead)
{
    (void)label;
    if (!ico_no_shadows()) {
        return 0;
    }
    if (was_dead) {
        s_held_pit++;
    } else {
        s_held_placed++;
    }
    s_pending = 1;
    s_quiet = 0;
    return 1;
}

int ico_no_shadows_drop(int what, int n)
{
    if (!ico_no_shadows()) {
        return 0;
    }
    if (what >= 0 && what < ICO_NOSH_DROP_COUNT && n > 0) {
        s_drop_room[what] += n;
        s_drop_total[what] += n;
    }
    return 1;
}

int ico_no_shadows_tick(int stage)
{
    if (!ico_no_shadows()) {
        return 0;
    }
    if (stage != s_room) {
        /* the room's objects start in the frames of its load, before or after the
           first tick that sees its number: a count not yet printed is the new
           room's (the left room's was printed two ticks after its start) */
        int placed = s_held_placed;
        int pit = s_held_pit;
        int pending = s_pending;

        s_pending = 0;
        s_held_placed = s_held_pit = 0;
        leave_room();
        s_room = stage;
        s_held_placed = placed;
        s_held_pit = pit;
        s_pending = pending;
        s_quiet = 0;
        return 1;
    }
    if (s_pending && ++s_quiet >= 2) {
        flush_held();
    }
    return 1;
}

void ico_no_shadows_forced(int what, int a)
{
    if (what < 0 || what >= ICO_NOSH_FORCED_COUNT || !tell(&s_told_forced[what])) {
        return;
    }
    switch (what) {
    case ICO_NOSH_FORCED_GRAB:
        fprintf(stderr,
                "no shadows: the forced grab of Yorda by Shadow %d is skipped: she stays where "
                "she stands, and the hand and the idol door follow as after the fight\n",
                a);
        break;
    case ICO_NOSH_FORCED_FIGHT:
        fprintf(stderr,
                "no shadows: the final fight (room %d) ends with no Shadows: the post-fight "
                "scene follows its music's start\n",
                a);
        break;
    default:
        fprintf(stderr,
                "no shadows: the June 2001 demo's final fight (room %d, Shadows 2139 and 2140) "
                "ends with no Shadows: the post-fight scene follows its cutscene\n",
                a);
        break;
    }
    s_lines++;
}

/* the final fight's hold: begun (the music started), the ticks still to wait */
static int s_fight_begun;
static int s_fight_hold;

void ico_no_shadows_fight_reset(void)
{
    s_fight_begun = 0;
    s_fight_hold = 0;
}

void ico_no_shadows_fight_begun(int hold_ticks)
{
    s_fight_begun = 1;
    s_fight_hold = hold_ticks > 0 ? hold_ticks : 0;
}

int ico_no_shadows_fight_held(void)
{
    if (!ico_no_shadows()) {
        return 0;
    }
    if (!s_fight_begun) {
        return 1;
    }
    if (s_fight_hold > 0) {
        s_fight_hold--;
        return 1;
    }
    return 0;
}

/* FRESH: a state saved before the blocks existed loads, keeping this run's words (a waiter
   then holds until the fight's start runs again, which a room entered with the fight begun
   does at its first ticks) */
int ico_no_shadows_state_register(void)
{
    int rc = 0;

    rc |= ico_state_register("no_shadows.fight_begun", &s_fight_begun, sizeof s_fight_begun,
                             ICO_STATE_FRESH);
    rc |= ico_state_register("no_shadows.fight_hold", &s_fight_hold, sizeof s_fight_hold,
                             ICO_STATE_FRESH);
    return rc != 0 ? -1 : 0;
}

int ico_no_shadows_drop_count(int what)
{
    return what >= 0 && what < ICO_NOSH_DROP_COUNT ? s_drop_total[what] : 0;
}

int ico_no_shadows_lines(void)
{
    return s_lines;
}

void ico_no_shadows_reset(void)
{
    s_room = -1;
    s_held_placed = s_held_pit = 0;
    s_quiet = 0;
    s_pending = 0;
    for (int i = 0; i < ICO_NOSH_DROP_COUNT; i++) {
        s_drop_room[i] = s_drop_total[i] = 0;
    }
    s_told_room = s_told_left = 0;
    s_fight_begun = s_fight_hold = 0;
    for (int i = 0; i < ICO_NOSH_FORCED_COUNT; i++) {
        s_told_forced[i] = 0;
    }
    s_lines = 0;
}
