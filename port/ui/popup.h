/*
 * port/ui/popup.h
 *
 * Notification popups (achievements from package 6E; a developer test
 * now): a queue of (title, body) shown one at a time, sliding in from the
 * right edge of the 4:3 picture, held, and sliding out, timed in vsyncs
 * (docs/port/UI.md, "Popups").
 *
 * Where they are drawn.  The design is an overlay the presenter draws
 * after it has scaled DISPLAY to the output (output resolution, after the
 * mirror flip, outside the game's fade and never in DISPLAY's history).
 * rd has no entry point for that yet (docs/port/UI.md, "Requested rd
 * API"), so ui_PopupRecord draws the popup into list 12 of the open rd
 * frame instead: after list 11 (the game's UI and fade), before the
 * reduction, UI-tagged (pre-flipped in mirror mode).  It is therefore at
 * the scene's resolution and reaches DISPLAY, so a keep frame (pause)
 * shows the last popup frame under the live one; window_host.c calls it
 * once per game frame.
 */
#ifndef PORT_UI_POPUP_H
#define PORT_UI_POPUP_H

#ifdef __cplusplus
extern "C" {
#endif

#define UI_POPUP_QUEUE 8
#define UI_POPUP_TEXT 128
/* timing, in vsyncs (50 Hz PAL: 0.3 s in, 4 s held, 0.3 s out) */
#define UI_POPUP_SLIDE_VSYNCS 15
#define UI_POPUP_HOLD_VSYNCS 200

/* Queues a popup (copied; UTF-8, '\n' allowed in body); 0, or -1 when the
   queue is full. */
int ui_PopupPush(const char *title, const char *body);
/* One vsync passed: advances the current popup. */
void ui_PopupVsync(void);
/* whether a popup is showing or waiting */
int ui_PopupActive(void);
/* Records the current popup into the open rd frame's list 12 (above);
   restores the current list.  No-op without a popup or an open frame. */
void ui_PopupRecord(void);
/* The developer trigger ([dev] popup_test): on, it queues the test popup
   when mainTick reaches 100 and every 150 ticks after (so a run's frame
   dumps catch one on any screen). */
void ui_PopupSetDevTest(int on);
void ui_PopupDevTick(unsigned int mainTick);
/* empties the queue (tests) */
void ui_PopupReset(void);

/* The current popup's panel in the layout grid (font.h) at this vsync, for
   tests: x0, y0, x1, y1; 0 when none is showing. */
int ui_PopupPanel(float rect[4]);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_POPUP_H */
