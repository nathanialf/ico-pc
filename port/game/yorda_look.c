/*
 * port/game/yorda_look.c
 *
 * Look at Yorda's press, toggle and end rules (yorda_look.h).
 */
#include "yorda_look.h"

#include <stdio.h>

#include "options.h"
#include "../platform/state_port.h"
#include "../platform/state_registry.h"

/* prev: L3's level the last tick saw; latch: a toggled look is on; stage:
   the stage the last tick ran in (-1: none yet); on: the last answer */
typedef struct YordaLookState {
    int prev;
    int latch;
    int stage;
    int on;
} YordaLookState;

static YordaLookState s_yl = {0, 0, -1, 0};
static unsigned long s_count;

/* the Main tick clock (yorda_look.h) and the tick of the last call */
static unsigned (*s_clock)(void);
static unsigned s_last;
static int s_haveLast;

void ico_yorda_look_set_clock(unsigned (*clock)(void))
{
    s_clock = clock;
    s_haveLast = 0;
}

/* FRESH: a state saved before the block existed loads with no look */
int ico_yorda_look_state_register(void)
{
    return ico_state_register("yorda_look.state", &s_yl, sizeof s_yl, ICO_STATE_FRESH) != 0 ? -1
                                                                                            : 0;
}

/* the log lines' rate: the first 8 looks of a run, then every 100th */
static int logNow(void)
{
    return s_count <= 8 || s_count % 100 == 0;
}

/* a toggled look ends: why, in the log */
static void endToggle(const char *why)
{
    if (s_yl.latch) {
        s_yl.latch = 0;
        if (logNow()) {
            fprintf(stderr, "yorda look: the look ended (%s)\n", why);
        }
    }
}

int ico_yorda_look(int l3, int live, int hand, int stage)
{
    const int level = l3 != 0;
    int type;
    int press;
    int look = 0;

    if (s_clock != NULL) {
        const unsigned now = s_clock();

        if (!s_haveLast || (now != s_last + 1u && now != s_last)) {
            /* the first tick after a pause, photo mode, a menu or a load:
               L3 already down is held, not pressed */
            s_yl.prev = level;
        }
        s_last = now;
        s_haveLast = 1;
    }
    press = level && !s_yl.prev;
    s_yl.prev = level;
    if (stage != s_yl.stage) {
        endToggle("a new stage");
        s_yl.stage = stage;
    }
    type = ico_opt_yorda_look();
    if (type == ICO_YORDA_LOOK_OFF) {
        s_yl.latch = 0;
    } else if (!live) {
        endToggle("no control");
    } else if (hand) {
        endToggle("her hand taken");
    } else if (type == ICO_YORDA_LOOK_HOLD) {
        s_yl.latch = 0;
        look = level;
    } else {
        if (press) {
            if (s_yl.latch) {
                endToggle("a second press");
            } else {
                s_yl.latch = 1;
            }
        }
        look = s_yl.latch;
    }
    if (look && !s_yl.on) {
        s_count++;
        if (logNow()) {
            fprintf(stderr, "yorda look: looking at Yorda (%s, %lu this run)\n",
                    ico_opt_yorda_look_name(type), s_count);
        }
    }
    s_yl.on = look;
    return look;
}

void ico_yorda_look_drop(void)
{
    endToggle("camera reset");
}

/* any load: a state of an earlier version has no block (FRESH: this run's
   bytes stay), and one of this version would bring back a look the player
   did not start in this run */
void ico_yorda_look_state_loaded(void)
{
    endToggle("a state loaded");
    s_yl.on = 0;
}

void ico_yorda_look_clear(void)
{
    s_yl.prev = 0;
    s_yl.latch = 0;
    s_yl.stage = -1;
    s_yl.on = 0;
    s_haveLast = 0;
}

unsigned long ico_yorda_look_count(void)
{
    return s_count;
}
