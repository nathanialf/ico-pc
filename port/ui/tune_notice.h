/*
 * port/ui/tune_notice.h
 *
 * The standing notice that tells the player the game is timing its drawing
 * choices (docs/port/UI.md, "Notification popups"; docs/port/PIPELINES.md,
 * "The zero-area cull timed in play").  The renderer's in-play judge
 * (port/render/rd_ab_run.h rd_ab_notice) says whether timing is going on in
 * this run; the window's vsync (ui_host.c ui_host_vsync, the thread the
 * popups belong to) passes that, the game pad's Triangle and whether the
 * player is in play here once a vsync.  The notice (popup.h
 * ui_popup_notice_show: its own panel, never in the queue) comes up at the
 * run's first trial and stays until the judge has no timing left (all its
 * trials run, every trial stopped, every attempt off for the run) or the
 * player presses Triangle in play; after either it does not come back in
 * this run, a renderer made again included.  The press is not taken from
 * the game: the boy does what Triangle does.  Nothing is saved.
 *   title UI_STR_TUNE_TITLE "Finding the best settings"
 *   line  UI_STR_TUNE_BODY "You may see some stutter.", the Triangle button,
 *         UI_STR_TUNE_HIDE "Hide"
 * One log line when it is shown, when the player hides it and when it
 * leaves because the timing is done (with the reason).
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
    UI_TUNE_JUDGE_DUE,       /* timing is going on */
    UI_TUNE_JUDGE_DONE_CAP,  /* every trial of the run run */
    UI_TUNE_JUDGE_DONE_STOP, /* every trial stopped (a device loss, a hang) */
    UI_TUNE_JUDGE_DONE_GONE  /* every attempt off for the run */
};

/* the game pad's Triangle in its logical word (port/input/pad_script.h
   ICO_PAD_TRIANGLE: ui_host.c checks it) */
#define UI_TUNE_TRIANGLE 0x0010u

/* what a step did */
enum { UI_TUNE_NOTHING = 0, UI_TUNE_SHOWN, UI_TUNE_HIDDEN, UI_TUNE_DONE };

/* one vsync: judge the judge's answer, buttons a copy of the game pad's
   logical word now (any source: a pad, the key bound to Triangle, the touch
   button; only UI_TUNE_TRIANGLE is looked at), inPlay the game owns the pad
   (no menu, no photo mode: a menu's Triangle never hides it).  Hidden by a
   Triangle press edge in play while it is shown (a Triangle held from
   before it came up is no press); the word is the caller's copy, so the
   game reads the press as ever.  Returns what it did. */
int ui_tune_notice_step(int judge, unsigned int buttons, int inPlay);
/* shown now (not hidden, not done) */
int ui_tune_notice_up(void);
/* back to the run's start (tests) */
void ui_tune_notice_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_TUNE_NOTICE_H */
