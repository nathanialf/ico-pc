/*
 * port/ui/tune_notice.c
 *
 * The standing notice "Finding the best settings" (tune_notice.h).
 */
#include "tune_notice.h"

#include <stdio.h>

#include "popup.h"
#include "strings.h"

enum { WAITING, UP, OVER };

static int s_phase = WAITING; /* the run's: OVER is for the rest of it */
static int s_held;            /* Triangle held at the last step */

void ui_tune_notice_reset(void)
{
    s_phase = WAITING;
    s_held = 0;
    ui_popup_notice_hide();
}

int ui_tune_notice_up(void)
{
    return s_phase == UP;
}

static const char *doneWhy(int judge)
{
    switch (judge) {
    case UI_TUNE_JUDGE_DONE_CAP:
        return "every trial of the run was run";
    case UI_TUNE_JUDGE_DONE_STOP:
        return "every trial was stopped (a device loss or the GPU's own sign of a reset)";
    default:
        return "every attempt is off for the rest of the run";
    }
}

int ui_tune_notice_step(int judge, unsigned int buttons, int inPlay)
{
    const int triangle = (buttons & UI_TUNE_TRIANGLE) != 0u;
    /* the press edge, from the last step's hold (a Triangle held as the
       notice comes up is not one) */
    const int press = triangle && !s_held;
    s_held = triangle != 0;
    if (s_phase == WAITING) {
        if (judge == UI_TUNE_JUDGE_DUE) {
            s_phase = UP;
            ui_popup_notice_show(ui_str(UI_STR_TUNE_TITLE), ui_str(UI_STR_TUNE_BODY),
                                 ui_str(UI_STR_TUNE_HIDE));
            fprintf(stderr,
                    "ui: notice \"%s\" shown: the in-play timing of the drawing choices began; "
                    "it stays until the timing is done or Triangle hides it\n",
                    ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
            return UI_TUNE_SHOWN;
        }
        if (judge >= UI_TUNE_JUDGE_DONE_CAP) {
            s_phase = OVER; /* done before it was shown: nothing to show */
        }
        return UI_TUNE_NOTHING;
    }
    if (s_phase != UP) {
        return UI_TUNE_NOTHING;
    }
    if (judge >= UI_TUNE_JUDGE_DONE_CAP) {
        s_phase = OVER;
        ui_popup_notice_hide();
        fprintf(stderr, "ui: notice \"%s\" leaves: the timing is done (%s)\n",
                ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE), doneWhy(judge));
        return UI_TUNE_DONE;
    }
    if (press && inPlay) {
        s_phase = OVER;
        ui_popup_notice_hide();
        fprintf(stderr,
                "ui: notice \"%s\" hidden by the player (Triangle in play; the press goes on to "
                "the game); not shown again this run\n",
                ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
        return UI_TUNE_HIDDEN;
    }
    return UI_TUNE_NOTHING;
}
