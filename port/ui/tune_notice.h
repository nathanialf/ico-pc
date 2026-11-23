/*
 * port/ui/tune_notice.h
 *
 * The notice that tells the player the game is timing its drawing choices
 * (docs/port/UI.md, "Popups"; docs/port/PIPELINES.md, "The zero-area cull
 * timed in play").  The renderer's in-play judge (port/render/rd_ab_run.h
 * rd_ab_notice) says whether a timing is running; the window's vsync
 * (ui_host.c ui_host_vsync, the thread the popups belong to) passes that,
 * the running trial's stage, a copy of the game pad's word and whether the
 * player is in play here, once a vsync.  The notice (popup.h
 * ui_popup_notice_show: its own panel, never in the queue) comes up when a
 * trial starts and slides out by itself when the timing has stopped
 * running for UI_TUNE_GRACE_MS (the judge chains a second subject's trial
 * one picture after the first one's verdict: no flicker between them),
 * after it has been fully shown for UI_TUNE_MIN_SHOWN_MS at least (a short
 * trial is no flash; the trial itself is never held up); it comes back at
 * the next trial.  A Triangle press in play hides it for the rest of the
 * run, a renderer made again included; the press is not taken from the
 * game.  Nothing is saved.
 *   title UI_STR_TUNE_TITLE "Finding the best settings"
 *   line  UI_STR_TUNE_BODY "You may see some stutter."
 *   block the Triangle button over UI_STR_TUNE_HIDE "Hide", at the side
 * One log line each time it is shown, when it leaves (the area's timing
 * ended, or the timing is done for the run with the reason) and when the
 * player hides it.
 */
#ifndef PORT_UI_TUNE_NOTICE_H
#define PORT_UI_TUNE_NOTICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* the judge's answer (port/render/rd_ab.h RD_AB_NOTICE_*, the same
   numbers: ui_host.c checks them) */
enum {
    UI_TUNE_JUDGE_NONE = 0,  /* no timing began */
    UI_TUNE_JUDGE_RUNNING,   /* a timing is running */
    UI_TUNE_JUDGE_IDLE,      /* between timings */
    UI_TUNE_JUDGE_DONE_CAP,  /* every trial of the run run */
    UI_TUNE_JUDGE_DONE_STOP, /* every trial stopped (a device loss, a hang) */
    UI_TUNE_JUDGE_DONE_GONE  /* every attempt off for the run */
};

/* the time fully shown before it may slide out, and the time the timing
   must have stopped running before it does (the judge's one picture
   between two subjects' trials, bridged) */
#define UI_TUNE_MIN_SHOWN_MS 2000
#define UI_TUNE_GRACE_MS 500

/* the game pad's Triangle in its logical word (port/input/pad_script.h
   ICO_PAD_TRIANGLE: ui_host.c checks it) */
#define UI_TUNE_TRIANGLE 0x0010u

/* what a step did */
enum { UI_TUNE_NOTHING = 0, UI_TUNE_SHOWN, UI_TUNE_HIDDEN, UI_TUNE_LEFT };

/* one vsync (ui_popup_vsync_hz a second): judge the judge's answer, stage
   the running trial's stage (the log; -1 not known), buttons a copy of the
   game pad's logical word now (any source: a pad, the key bound to
   Triangle, the touch button; only UI_TUNE_TRIANGLE is looked at), inPlay
   the game owns the pad (no menu, no photo mode: a menu's Triangle never
   hides it).  Hidden by a Triangle press edge in play while it is shown (a
   Triangle held from before it came up is no press).  Returns what it
   did. */
int ui_tune_notice_step(int judge, int stage, unsigned int buttons, int inPlay);
/* shown now */
int ui_tune_notice_up(void);
/* hidden by the player this run */
int ui_tune_notice_dismissed(void);
/* back to the run's start (tests) */
void ui_tune_notice_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_TUNE_NOTICE_H */
