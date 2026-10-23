/*
 * port/platform/window_switch.h
 *
 * Options > Graphics > Apply: the renderer switched to another backend in
 * the process, the game going on (window_switch.c), and the Graphics page's
 * host (settings.h UiGraphicsHost).  The page asks; the switch itself runs
 * in the event pump (ico_window_switch_pump), between two steps of the game
 * and never inside a present.  window_host.c makes the window and the
 * renderer again (ico_window_reopen).
 *
 *   ico_window_switch_backend(name)
 *                               the switch now: the renderer shut down with
 *                               the game's state kept (rd_suspend), the
 *                               window made again with the backend's flags
 *                               in the same mode, size and place, rd_resume
 *                               and what the start does after rd_init.
 *                               0 switched (or name runs already); -1 name
 *                               did not start and the backend that ran is
 *                               back; -2 refused (a movie plays, photo mode,
 *                               the model viewer, the touch layout editor, a
 *                               build with one renderer, a name the build
 *                               does not link); -3 neither started (no
 *                               renderer: the game ends)
 *   ico_window_switch_running() the backend running, as the config key
 *                               [video] backend names it ("vulkan",
 *                               "opengl", "d3d12", "d3d11")
 *   ico_window_switch_install() the Graphics page's host
 *   ico_window_switch_pump()    a switch the page asked for, from
 *                               ico_window_pump; 0, or -1 when no renderer
 *                               is left (the pump ends the run)
 *   ico_window_reopen(backend)  window_host.c's part: 1 up, 0 not (no
 *                               window then; the next call makes one)
 */
#ifndef ICO_PLATFORM_WINDOW_SWITCH_H
#define ICO_PLATFORM_WINDOW_SWITCH_H

int ico_window_switch_backend(const char *name);
const char *ico_window_switch_running(void);
void ico_window_switch_install(void);
int ico_window_switch_pump(void);
int ico_window_reopen(const char *backend);

#endif /* ICO_PLATFORM_WINDOW_SWITCH_H */
