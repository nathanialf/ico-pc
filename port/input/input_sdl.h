/*
 * port/input/input_sdl.h
 *
 * The device layer on SDL3 (window build only): keyboard, mouse and gamepad
 * events into a raw snapshot, the binding step once per vsync, and the pad's
 * rumble back out to the gamepads. window_host.c drives it.
 *
 *   ico_input_sdl_init(path)    load the config.toml at path (may be missing
 *                               or NULL: defaults), open the gamepads that
 *                               are plugged in, and plug the live pad into
 *                               the libpad port. SDL_INIT_GAMEPAD must be on.
 *   ico_input_sdl_event(e)      one SDL event (keys, mouse, gamepad hotplug,
 *                               focus loss clears everything held)
 *   ico_input_sdl_set_capture(on)  the mouse is captured (relative mode):
 *                               its motion drives the right stick; off, the
 *                               stick decays to centre
 *   ico_input_sdl_update()      once per vsync: snapshot -> bindings ->
 *                               virtual pad; rumble to the gamepads
 *   ico_input_sdl_shutdown()
 *
 * The touch overlay (touch.h): a direct touch screen's fingers drive a
 * virtual pad merged with the bindings' (dropped while a gamepad is
 * connected unless [input] touch_mode is "always").
 *   ico_input_sdl_set_touch_layout(w, h, sx, sy, sw, sh)  the output's size
 *                               and its safe area, window pixels (sw 0:
 *                               the whole output); window_host.c calls it
 *                               on open and when the size or safe area
 *                               changes (the Touch size row is followed
 *                               here)
 *   ico_input_sdl_touch_present()  1 once a direct touch screen is known
 *                               (at start, or its first touch): the
 *                               Settings rows show
 *   ico_input_sdl_touch_overlay(out)  the copy the overlay draws from,
 *                               taken at the last update; 0 when there is
 *                               nothing to draw
 */
#ifndef ICO_PORT_INPUT_INPUT_SDL_H
#define ICO_PORT_INPUT_INPUT_SDL_H

#include <SDL3/SDL.h>
#include "touch.h"

void ico_input_sdl_init(const char *config_path);
void ico_input_sdl_event(const SDL_Event *e);
void ico_input_sdl_set_capture(int on);
void ico_input_sdl_update(void);
void ico_input_sdl_shutdown(void);
void ico_input_sdl_set_touch_layout(int w, int h, int sx, int sy, int sw, int sh);
int ico_input_sdl_touch_present(void);
int ico_input_sdl_touch_overlay(IcoTouchOverlay *out);

#endif /* ICO_PORT_INPUT_INPUT_SDL_H */
