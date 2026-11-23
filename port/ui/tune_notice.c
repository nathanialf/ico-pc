/*
 * port/ui/tune_notice.c
 *
 * The notice "Finding the best settings" (tune_notice.h).
 */
#include "tune_notice.h"

#include <stdio.h>

#include "popup.h"
#include "strings.h"

enum { WAITING, UP, OVER };

static int s_phase = WAITING; /* OVER is for the rest of the run */
static int s_held;            /* Triangle held at the last step */
static int s_dismissed;       /* hidden by the player */
static unsigned int s_upVsyncs, s_idleVsyncs;

void ui_tune_notice_reset(void)
{
    s_phase = WAITING;
    s_held = 0;
    s_dismissed = 0;
    s_upVsyncs = s_idleVsyncs = 0;
    ui_popup_notice_hide();
}

int ui_tune_notice_up(void)
{
    return s_phase == UP;
}

int ui_tune_notice_dismissed(void)
{
    return s_dismissed;
}

static const char *doneWhy(int judge)
{
    switch (judge) {
    case UI_TUNE_JUDGE_DONE_CAP:
        return "every trial of the run was run";
    case UI_TUNE_JUDGE_DONE_STOP:
        return "every trial was stopped (a device loss or the GPU's own sign of a reset)";
    case UI_TUNE_JUDGE_DONE_GONE:
        return "every attempt is off for the rest of the run";
    default:
        return "every attempt was timed once, its answer kept for the run";
    }
}

/* ms in vsyncs at the popups' rate */
static unsigned int vsyncsOf(int ms)
{
    return (unsigned int)((ui_popup_vsync_hz() * ms + 999) / 1000);
}

int ui_tune_notice_step(int judge, int stage, unsigned int buttons, int inPlay)
{
    /* the press edge, from the last step's hold (a Triangle held as the
       notice comes up is not one) */
    const int triangle = (buttons & UI_TUNE_TRIANGLE) != 0u;
    const int press = triangle && !s_held;
    s_held = triangle;
    const int done = judge >= UI_TUNE_JUDGE_DONE_CAP;
    if (s_phase == WAITING) {
        if (judge == UI_TUNE_JUDGE_RUNNING) {
            s_phase = UP;
            s_upVsyncs = s_idleVsyncs = 0;
            ui_popup_notice_show(ui_str(UI_STR_TUNE_TITLE), ui_str(UI_STR_TUNE_BODY),
                                 ui_str(UI_STR_TUNE_HIDE));
            if (stage >= 0) {
                fprintf(stderr, "ui: notice \"%s\" shown: the timing of stage %d began\n",
                        ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE), stage);
            } else {
                fprintf(stderr, "ui: notice \"%s\" shown: a timing began\n",
                        ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
            }
            return UI_TUNE_SHOWN;
        }
        if (done) {
            s_phase = OVER; /* nothing more to time this run */
        }
        return UI_TUNE_NOTHING;
    }
    if (s_phase != UP) {
        return UI_TUNE_NOTHING;
    }
    if (press && inPlay) {
        s_phase = OVER;
        s_dismissed = 1;
        ui_popup_notice_hide();
        fprintf(stderr,
                "ui: notice \"%s\" hidden by the player (Triangle in play; the press goes on to "
                "the game); not shown again this run\n",
                ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
        return UI_TUNE_HIDDEN;
    }
    s_upVsyncs++;
    s_idleVsyncs = judge == UI_TUNE_JUDGE_RUNNING ? 0u : s_idleVsyncs + 1u;
    /* fully shown for its minimum (after its slide in), and the timing
       stopped running for the grace (or done for the run) */
    const int shownLong =
        s_upVsyncs >= (unsigned int)ui_popup_slide_vsyncs() + vsyncsOf(UI_TUNE_MIN_SHOWN_MS);
    if (!shownLong || judge == UI_TUNE_JUDGE_RUNNING ||
        (!done && s_idleVsyncs < vsyncsOf(UI_TUNE_GRACE_MS))) {
        return UI_TUNE_NOTHING;
    }
    ui_popup_notice_hide();
    if (done) {
        s_phase = OVER;
        fprintf(stderr, "ui: notice \"%s\" left: the timing is done for the run (%s)\n",
                ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE), doneWhy(judge));
    } else {
        s_phase = WAITING; /* back at the next timing */
        fprintf(stderr, "ui: notice \"%s\" left: the timing of this area ended\n",
                ui_str_in(UI_LANG_EN, UI_STR_TUNE_TITLE));
    }
    return UI_TUNE_LEFT;
}
