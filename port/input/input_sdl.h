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
 */
#ifndef ICO_PORT_INPUT_INPUT_SDL_H
#define ICO_PORT_INPUT_INPUT_SDL_H

#include <SDL3/SDL.h>

void ico_input_sdl_init(const char *config_path);
void ico_input_sdl_event(const SDL_Event *e);
void ico_input_sdl_set_capture(int on);
void ico_input_sdl_update(void);
void ico_input_sdl_shutdown(void);

#endif /* ICO_PORT_INPUT_INPUT_SDL_H */
