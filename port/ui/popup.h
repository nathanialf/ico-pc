/*
 * port/ui/popup.h
 *
 * Notification popups (achievements, photo mode, the model viewer, the
 * Graphics driver page, and the [dev] popup_test switch): a queue of
 * (title, body) shown one at a time, sliding in from the right edge of the
 * 4:3 picture, held, and sliding out, timed in vsyncs.
 *
 * Where they are drawn.  On the presentation overlay (port/render/rd.h
 * rd_set_present_overlay): the presenter calls ui_host.c's overlay function
 * at every present, which calls ui_popup_draw_overlay, so the popup is drawn on the output after the
 * box blit, at the output's resolution, outside the game's frame (never
 * reduced, never in DISPLAY's history, so a keep frame cannot show it
 * twice), never mirrored, and at each present's state of the queue.
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
int ui_popup_push(const char *title, const char *body);
/* One vsync passed: advances the current popup. */
void ui_popup_vsync(void);
/* whether a popup is showing or waiting (tests) */
int ui_popup_active(void);
/* Draws the current popup on the output (above): inside an rd overlay
   callback, ctx the callback's RdOverlayCtx (rd.h).  No-op without a
   popup, and without ICO_RD. */
struct RdOverlayCtx;
void ui_popup_draw_overlay(const struct RdOverlayCtx *ctx);
/* The developer trigger ([dev] popup_test): on, it queues the test popup
   when mainTick reaches 100 and every 150 ticks after (so a run's frame
   dumps catch one on any screen). */
void ui_popup_set_dev_test(int on);
void ui_popup_dev_tick(unsigned int mainTick);
/* empties the queue (tests) */
void ui_popup_reset(void);

/* The current popup's panel in the layout grid (font.h) at this vsync, for
   tests: x0, y0, x1, y1; 0 when none is showing. */
int ui_popup_panel(float rect[4]);
/* The current popup's title (tests); "" when none is showing. */
const char *ui_popup_title(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_POPUP_H */
