/*
 * port/ui/touch_ui.h
 *
 * The touch overlay's drawing: the buttons, the stick and
 * the look pad of port/input/touch.h on the output, from the copy the
 * device layer takes once per vsync (input_sdl.h ico_input_sdl_touch_overlay,
 * which the window host installs as the source).  Drawn by ui_host.c's
 * presentation overlay under the photo HUD and the popups, at the output's
 * resolution, only while the copy's opacity is above 0 (the fades, Touch
 * controls and Touch opacity).
 *
 * What it draws, every shape untextured triangles through rd_overlay_prims
 * (rd.h), the labels with ui_draw_text:
 *   - the face buttons as discs with the PS2 symbols as strokes (a cross, a
 *     ring, a square, a triangle) in their colours;
 *   - the D-pad as four keys with an arrow each, the shoulders (L1 R1 L2
 *     R2) and Start and Select as boxes with their names;
 *   - the left stick: its ring where it rests (or where the finger went
 *     down), the run ring inside it at ICO_TOUCH_RUN_RING of the radius
 *     (lit while the push is past it), and the knob under the finger;
 *   - the look pad while a finger is on it: a ring where it went down and
 *     a dot under the finger;
 *   - a held button or key brighter.
 */
#ifndef PORT_UI_TOUCH_UI_H
#define PORT_UI_TOUCH_UI_H

#ifdef __cplusplus
extern "C" {
#endif

struct RdOverlayCtx;
struct IcoTouchOverlay;

/* Where the drawing reads the overlay from: fn fills *out and returns 1
   when there is something to draw.  NULL (the default): nothing drawn. */
void ui_touch_set_source(int (*fn)(struct IcoTouchOverlay *out));

/* The overlay on the output (ui_host.c's overlay callback). */
void ui_touch_draw_overlay(const struct RdOverlayCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_TOUCH_UI_H */
