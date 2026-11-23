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

#include <stddef.h>

#define UI_POPUP_QUEUE 8
#define UI_POPUP_TEXT 384

/* The panel's sizes, in the layout grid (font.h: 640 units across the 4:3
   picture), every popup's and the standing notice's: the title's and the
   body's text sizes, the padding left and right and above and below, the
   gap between the title and the body, the narrowest panel and the margin
   from the picture's right edge.  The vertical padding holds the text's
   dark halo (4 field lines, 8 units, past the letters, the letters' weight
   a line more and the bilinear read a texel more: 11 let it spill), so it
   is not cut with the rest. */
#define UI_POPUP_TITLE_SIZE 22.0f
#define UI_POPUP_BODY_SIZE 18.0f
#define UI_POPUP_PAD_X 10.0f
#define UI_POPUP_PAD_Y 13.0f
#define UI_POPUP_GAP_Y 2.0f
#define UI_POPUP_MIN_W 160.0f
#define UI_POPUP_MARGIN_X 18.0f
/* the widest body line a panel holds (640 - 2 margins - 2 paddings) */
#define UI_POPUP_BODY_WIDTH (640.0f - 2.0f * UI_POPUP_MARGIN_X - 2.0f * UI_POPUP_PAD_X)
/* timing: 0.3 s in, 4 s held, 0.3 s out, counted in vsyncs at the rate
   ui_popup_set_vsync_hz gives (50 by default: 15, 200, 15; 60: 18, 240, 18) */
#define UI_POPUP_SLIDE_TENTHS 3
#define UI_POPUP_HOLD_SECONDS 4
#define UI_POPUP_DEFAULT_HZ 50

/* The vsync rate ui_popup_vsync is called at: the window host passes
   ico_host_vsync_hz() (50 on the European disc's PAL mode, 60 otherwise)
   before each call, so a change of the video mode is followed; 0 or less
   restores the default. */
void ui_popup_set_vsync_hz(int hz);
/* that rate (settings.c counts the Apply countdown with it) */
int ui_popup_vsync_hz(void);
/* the slide and the hold in vsyncs at that rate */
int ui_popup_slide_vsyncs(void);
int ui_popup_hold_vsyncs(void);

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
/* empties the queue and takes the standing notice down (tests) */
void ui_popup_reset(void);

/* The notice (port/ui/tune_notice.c "Finding the best settings"): one
   panel of its own, never an entry of the queue, so the queue's popups
   neither wait for it nor hide it (theirs is placed below it while it is
   up).  It slides in as a popup does, stays until hidden, then slides out.
   Two parts: its title and one line of text on the left, and at the
   panel's right side one stacked block, the Triangle button (the drawer's
   picture or key, ui_popup_set_button_drawer; without one the word
   UI_STR_BTN_TRIANGLE) over the hint word, centred on each other and on
   the panel's height, a thin divider between the parts.  Show while it is
   up does nothing; hide while it is not does nothing. */
void ui_popup_notice_show(const char *title, const char *body, const char *hint);
void ui_popup_notice_hide(void);
/* shown or sliding out; shown (not sliding out) */
int ui_popup_notice_active(void);
int ui_popup_notice_shown(void);
/* its panel (tests), as ui_popup_panel; its title, "" when none; its one
   line of text, "" when none */
int ui_popup_notice_panel(float rect[4]);
const char *ui_popup_notice_title(void);
const char *ui_popup_notice_line(void);
/* its parts (tests): the text's rectangle (x0, y0, x1, y1), the divider's
   x and the block's rectangle; 0 when it is not up */
int ui_popup_notice_parts(float text[4], float *divider, float block[4]);
/* The Triangle button in the notice's block, as the port draws its buttons
   elsewhere (photo_ui.c: the key bound to it in brackets after a key or the
   mouse was pressed last, else the disc's picture or the button set's
   art): fn(x, mid, size, draw) returns its width at size, drawn with its
   left edge at x and its middle on mid when draw (inside the overlay), 0
   when it has nothing (the word is drawn then).  NULL: the word. */
typedef float (*UiPopupButtonFn)(float x, float mid, float size, int draw);
void ui_popup_set_button_drawer(UiPopupButtonFn fn);

/* The current popup's panel in the layout grid (font.h) at this vsync, for
   tests: x0, y0, x1, y1; 0 when none is showing. */
int ui_popup_panel(float rect[4]);
/* The current popup's title (tests); "" when none is showing. */
const char *ui_popup_title(void);
/* Its body as shown (the Japanese and Korean line breaks in), "" when none
   is showing (tests). */
const char *ui_popup_body(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_POPUP_H */
