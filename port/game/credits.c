/*
 * port/game/credits.c
 *
 * Extras > Credits and the staff roll's port credit (ico_credits.h).  Port
 * code only: the game's side of the playback is the engine in
 * credits_live.c, installed by the program.
 */
#include "ico_credits.h"

#include <stdio.h>

#include "achievements.h"
#include "config.h"
#include "../core/region.h" /* ico_region_cur */

/* --- the roll's port credit ------------------------------------------------
 * In the roll's own forms (staffRollNameData, read from the PAL ELF): a
 * section heading is "{R}< Game Design > ", its name "{R}Fumito Ueda ",
 * four blank lines (" ") between them, and a run of blank lines before the
 * next heading (twelve between "Fumito Ueda" and "< Planners >"); two blank
 * lines between the names of one heading, as the disc's "< Special Thanks >"
 * has.  The colour code ({#FFFFFF80}) is set once by the first line and
 * holds; {R} sets the right alignment again after the closing lines' {C}.
 * ASCII 0x20-0x7E without the backtick only: the roll draws with
 * font_Print's bitmap font (DisplayFont.c).  Four blocks: the port's
 * credit, then the translators, the contributors and the special thanks
 * (the last under the disc's own heading text). */
static const IcoCreditsEngine *s_engine; /* the program's (ico_credits_set_engine) */

static char s_blank[] = " ";
static char s_heading[] = "{R}< Decompilation and PC Port > ";
static char s_name[] = "{R}Nathanial Fine ";
static char s_translation[] = "{R}< Translation > ";
static char s_bobttz[] = "{R}bob-ttz ";
static char s_cobra838[] = "{R}cobra838 ";
static char s_ichagas7[] = "{R}ichagas7 ";
static char s_contributors[] = "{R}< Contributors > ";
static char s_katsifolis[] = "{R}katsifolis ";
static char s_braito4[] = "{R}braito4 ";
static char s_neetgon[] = "{R}neetgon ";
static char s_kclough[] = "{R}KClough ";
static char s_thanks[] = "{R}< Special Thanks > ";
static char s_sadorigami[] = "{R}Sad Origami ";

/* the table: each non-blank line at its index; every other index is a blank
   line (port_lines_fill) */
static char *s_lines[ICO_ROLL_PORT_LAST + 1] = {
    [ICO_ROLL_PORT_HEADING] = s_heading, /* after 12 blanks */
    [ICO_ROLL_PORT_NAME] = s_name,       /* after 4 */
    [30] = s_translation,                /* after 12 */
    [35] = s_bobttz,                     /* after 4 */
    [38] = s_cobra838,                   /* after 2 */
    [41] = s_ichagas7,                   /* after 2 */
    [54] = s_contributors,               /* after 12 */
    [59] = s_katsifolis,                 /* after 4 */
    [62] = s_braito4,                    /* after 2 */
    [65] = s_neetgon,                    /* after 2 */
    [68] = s_kclough,                    /* after 2 */
    [81] = s_thanks,                     /* after 12 */
    [ICO_ROLL_PORT_LAST] = s_sadorigami, /* after 4: the roll's last line */
};

#define PORT_LINES ((int)(sizeof(s_lines) / sizeof(s_lines[0])))

_Static_assert(ICO_ROLL_PORT_HEADING < ICO_ROLL_PORT_NAME &&
                   ICO_ROLL_PORT_NAME < ICO_ROLL_PORT_LAST && ICO_ROLL_PORT_LAST == PORT_LINES - 1,
               "the heading, the name and the last line are where ico_credits.h says");

static void port_lines_fill(void)
{
    static int filled;

    if (!filled) {
        for (int k = 0; k < PORT_LINES; k++) {
            if (s_lines[k] == NULL) {
                s_lines[k] = s_blank;
            }
        }
        filled = 1;
    }
}

int ico_roll_port_count(void)
{
    return PORT_LINES;
}

char **ico_roll_port_line(int k)
{
    if (k < 0 || k >= PORT_LINES) {
        return NULL;
    }
    port_lines_fill();
    if (k == ICO_ROLL_PORT_HEADING) {
        fprintf(stderr, "credits: the port credit is posted (%s)\n",
                ico_credits_active() ? "Extras > Credits" : "the ending");
    } else if (k == ICO_ROLL_PORT_LAST) {
        fprintf(stderr, "credits: the port's last line is posted (%s)\n",
                ico_credits_active() ? "Extras > Credits" : "the ending");
    }
    return &s_lines[k];
}

void ico_roll_started(int discLines)
{
    port_lines_fill();
    fprintf(stderr, "credits: roll start, %d lines from the disc and %d of the port's (%s)\n",
            discLines, PORT_LINES, ico_credits_active() ? "Extras > Credits" : "the ending");
    if (s_engine != NULL && s_engine->roll != NULL) {
        s_engine->roll(); /* the song's place (credits_live.c) */
    }
}

/* --- the playback -------------------------------------------------------- */

/* the game's empty layout, the one the scripts switch to for a scene (55,
   52 on the Japanese and Korean discs: port/core/region.h) */
#define LAYOUT_NONE (ico_region_cur()->layout_scene)

int ico_credits_stage(void)
{
    return ico_region_cur()->stage_credits;
}

int ico_credits_title_stage(void)
{
    return ico_region_cur()->stage_title;
}

static int s_active;

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
