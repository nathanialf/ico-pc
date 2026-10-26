/*
 * port/ui/ui_fps.h
 *
 * The Frame rate counter (Options > Display, [video] show_fps): the
 * presented frames per second as small text in the top-left corner of the
 * screen (the whole output, not the 4:3 picture), on the presenter's top
 * layer (ui_host.c hostOverlayTop): at the output's resolution, never
 * through the CRT filter, never mirrored.  One ui_draw_text per present, no
 * background.
 *
 * The number is made here from the window's calls (port/platform/fps_counter.h:
 * the presents over the last second or more, the 10 s log line's
 * "presents/s"): window_host.c's pace_log calls ui_fps_tick, its present
 * calls ui_fps_present.  Nothing is drawn until the first second has given a
 * number.  The counting runs whether or not the option is on.
 *
 * Where: the corner, a margin in (1.2 % of the output's height).  While the
 * touch controls' layout is there the corner is Left 2's, so the counter
 * stands on the top edge between the D-pad's Up key and Select, where the
 * default layout has no control (touch_ui.h ui_touch_top_gap), shrunk to
 * fit that stretch.  The text is 3.5 % of the output's height (at least 18
 * pixels).
 *
 * Log: "window: frame rate counter on" or "off" when the option changes
 * (and "on" at the start when it is on).
 */
#ifndef PORT_UI_UI_FPS_H
#define PORT_UI_UI_FPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RdOverlayCtx;

/* every pace of the window (window_host.c pace_log), now_ns on SDL's
   monotonic clock */
void ui_fps_tick(uint64_t now_ns);
/* a present made (window_host.c, with interpolation on) */
void ui_fps_present(void);

/* The counter on the output (ui_host.c's top overlay callback, when the
   option is on). */
void ui_fps_draw_overlay(const struct RdOverlayCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_UI_FPS_H */
