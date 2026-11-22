/*
 * port/ui/tune_notice.h
 *
 * The pop-up that tells the player the game is timing its drawing choices
 * (docs/port/UI.md, "Notification popups"; docs/port/PIPELINES.md, "The
 * zero-area cull timed in play"): the renderer's in-play judge
 * (port/render/rd_ab_run.h rd_ab_notice_take) says once a run, when it
 * starts its first trial, and the window's vsync (ui_host.c
 * ui_host_vsync, the thread the popup queue belongs to) shows it: title
 * UI_STR_TUNE_TITLE ("Finding the best settings"), body UI_STR_TUNE_BODY.
 * Behind a popup already showing it waits its turn; a full queue drops it,
 * not retried.  One log line when it is queued.
 */
#ifndef PORT_UI_TUNE_NOTICE_H
#define PORT_UI_TUNE_NOTICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* due: the judge's notice taken this vsync.  0 nothing to show, 1 queued
   (and logged), -1 the queue was full (dropped) */
int ui_tune_notice(int due);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_TUNE_NOTICE_H */
