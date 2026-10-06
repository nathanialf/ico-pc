/*
 * port/game/credits.c
 *
 * Extras > Credits and the staff roll's port credit (ico_credits.h;
 * docs/port/EXTRAS.md, "Credits"; docs/port/UI.md, "Staff roll").  Port
 * code only: the game's side of the playback is the engine in
 * credits_live.c, installed by the program.
 */
#include "ico_credits.h"

#include <stdio.h>

#include "achievements.h"
#include "config.h"

/* --- the roll's port credit ------------------------------------------------
 * In the roll's own forms (staffRollNameData, read from the PAL ELF): a
 * section heading is "{R}< Game Design > ", its name "{R}Fumito Ueda ",
 * four blank lines (" ") between them, and a run of blank lines before the
 * next heading (twelve between "Fumito Ueda" and "< Planners >").  The
 * colour code ({#FFFFFF80}) is set once by the first line and holds; {R}
 * sets the right alignment again after the closing lines' {C}.  ASCII only:
 * the roll draws with font_Print's bitmap font. */
static char s_blank[] = " ";
static char s_heading[] = "{R}< Decompilation and PC Port > ";
static char s_name[] = "{R}Nathanial Fine ";

static char *s_lines[] = {
    s_blank,   s_blank, s_blank, s_blank, s_blank, s_blank,
    s_blank,   s_blank, s_blank, s_blank, s_blank, s_blank, /* 0..11 */
    s_heading,                                              /* 12 */
    s_blank,   s_blank, s_blank, s_blank,                   /* 13..16 */
    s_name,                                                 /* 17 */
};

#define PORT_LINES ((int)(sizeof(s_lines) / sizeof(s_lines[0])))

_Static_assert(ICO_ROLL_PORT_HEADING < PORT_LINES && ICO_ROLL_PORT_NAME == PORT_LINES - 1,
               "the heading and the name are where ico_credits.h says");

int ico_roll_port_count(void)
{
    return PORT_LINES;
}

char **ico_roll_port_line(int k)
{
    if (k < 0 || k >= PORT_LINES) {
        return NULL;
    }
    if (k == ICO_ROLL_PORT_HEADING) {
        fprintf(stderr, "staff roll: the port credit is posted (%s)\n",
                ico_credits_active() ? "Extras > Credits" : "the ending");
    }
    return &s_lines[k];
}

void ico_roll_started(int discLines)
{
    fprintf(stderr, "staff roll: start, %d lines from the disc and %d of the port's (%s)\n",
            discLines, PORT_LINES, ico_credits_active() ? "Extras > Credits" : "the ending");
}

/* --- the playback -------------------------------------------------------- */

/* the game's empty layout, the one the scripts switch to for a scene */
#define LAYOUT_NONE 55

static int s_active;
static const IcoCreditsEngine *s_engine;

int ico_credits_active(void)
{
    return s_active;
}

void ico_credits_set_active(int on)
{
    s_active = on != 0;
}

void ico_credits_set_engine(const IcoCreditsEngine *e)
{
    s_engine = e;
}

int ico_credits_unlocked(void)
{
    IcoAchStats st;
    int i;

    if (ico_config_get_bool("dev.unlock_credits", 0)) {
        return 1;
    }
    i = ico_ach_find("finish");
    if (i >= 0 && ico_ach_state(i) == ICO_ACH_UNLOCKED) {
        return 1;
    }
    ico_ach_stats(&st);
    return st.clears > 0;
}

int ico_credits_start(void)
{
    if (s_active) {
        fprintf(stderr, "credits: failed: a playback is already running\n");
        return -1;
    }
    if (s_engine == NULL || s_engine->begin == NULL) {
        fprintf(stderr, "credits: failed: no engine in this program\n");
        return -1;
    }
    fprintf(stderr, "credits: enter\n");
    if (s_engine->begin() != 0) {
        return -1; /* the engine logged why */
    }
    return LAYOUT_NONE;
}
