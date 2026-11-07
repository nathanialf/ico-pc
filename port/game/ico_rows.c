/*
 * port/game/ico_rows.c
 *
 * Free movement and Ask Yorda for a hint (ico_rows.h).
 */
#include "ico_rows.h"

#include <stdio.h>

#include "config.h"
#include "options.h"

/* -1: not read yet; 0 or 1 */
static int s_free_movement = -1;
static int s_free_logged = -1;

static unsigned long s_steps;      /* snapshots seen */
static unsigned long s_presses;    /* presses counted */
static unsigned long s_taken;      /* presses the game has looked at */
static unsigned long s_press_step; /* the snapshot of the last press */
static unsigned long s_fired;
static int s_prev_down;

int ico_free_movement_row(void)
{
    if (s_free_movement < 0) {
        s_free_movement = ico_config_get_bool("developer.free_movement", 0) != 0;
    }
    return s_free_movement;
}

int ico_free_movement(void)
{
    const int on = ico_free_movement_row() && ico_opt_developer_mode();

    if (on != s_free_logged) {
        s_free_logged = on;
        fprintf(stderr, "developer: free movement %s (row %s, Developer mode %s)\n",
                on ? "on: L2 hands Ico to the sticks, L3 snaps him to the floor" : "off",
                s_free_movement ? "on" : "off", ico_opt_developer_mode() ? "on" : "off");
    }
    return on;
}

void ico_free_movement_set(int on)
{
    s_free_movement = on != 0;
}

void ico_rows_reload(void)
{
    s_free_movement = -1;
}

void ico_hint_ask_note(int down)
{
    s_steps++;
    if (down && !s_prev_down) {
        s_presses++;
        s_press_step = s_steps;
        if (s_presses <= 8 || s_presses % 100 == 0) {
            fprintf(stderr, "hint: the Ask Yorda for a hint button was pressed (%lu this run)\n",
                    s_presses);
        }
    }
    s_prev_down = down != 0;
}

int ico_hint_ask(void)
{
    if (s_presses == s_taken) {
        return 0;
    }
    s_taken = s_presses;
    if (s_steps - s_press_step > ICO_HINT_ASK_AGE) {
        return 0;
    }
    s_fired++;
    if (s_fired <= 8 || s_fired % 100 == 0) {
        fprintf(stderr, "hint: the waiting hint starts now, asked for (%lu this run)\n", s_fired);
    }
    return 1;
}

unsigned long ico_hint_ask_presses(void)
{
    return s_presses;
}

unsigned long ico_hint_ask_fired(void)
{
    return s_fired;
}

void ico_hint_ask_reset(void)
{
    s_steps = s_presses = s_taken = s_press_step = s_fired = 0;
    s_prev_down = 0;
}
