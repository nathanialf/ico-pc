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
 * What it draws:
 *   - every button as a plain translucent disc (the face buttons and R1) or
 *     key (the rest), untextured triangles through rd_overlay_prims (rd.h);
 *   - on them the game's own button pictures (layout_ext.h LtExtGlyph: the
 *     four face buttons, L1 R1 L2 R2 and the arrows of Left and Right) as
 *     sprites in output pixels from the game's button sheets, fitted to the
 *     disc or key with the picture's proportions; the port draws no symbol
 *     of its own;
 *   - the words, with ui_draw_text placed in output pixels, on the zones
 *     with no picture (Up, Down, Select, Start), and on any zone whose
 *     sheet is not loaded yet (before a stage's textures are set up);
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
