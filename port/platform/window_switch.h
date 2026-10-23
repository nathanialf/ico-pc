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
 *   ico_window_switch_backend(name, trial)
 *                               the switch now: the renderer shut down with
 *                               the game's state kept (rd_suspend), the
 *                               window made again with the backend's flags
 *                               in the same mode, size and place on the
 *                               same display, rd_resume, what the start
 *                               does after rd_init and rd_resume_frame;
 *                               trial: Apply's switch (no restart planned
 *                               after a crash or loss until Keep), else a
 *                               switch back (the start's choice again).
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
 *                               ico_window_pump, the watchdog paused; 0, or
 *                               -1 when no renderer is left (the pump ends
 *                               the run)
 *   ico_window_switch_end_text() after that -1: the box's text naming both
 *                               renderers (main_host.c shows it and saves
 *                               nothing); NULL otherwise
 *   ico_window_switch_started(backendExplicit, backend)
 *                               ico_window_open's choice once the renderer
 *                               is up (the player's, or Automatic's with
 *                               its fallback or restart backend)
 *   ico_window_reopen(backend, backendExplicit)
 *                               window_host.c's part: 1 up, 0 not (no
 *                               window then; the next call makes one);
 *                               backendExplicit for the crash and loss
 *                               steps (ico_diag_set_startup_retry)
 */
#ifndef ICO_PLATFORM_WINDOW_SWITCH_H
#define ICO_PLATFORM_WINDOW_SWITCH_H

#include <stdbool.h>

int ico_window_switch_backend(const char *name, int trial);
const char *ico_window_switch_running(void);
void ico_window_switch_install(void);
int ico_window_switch_pump(void);
const char *ico_window_switch_end_text(void);
void ico_window_switch_started(bool backendExplicit, const char *backend);
int ico_window_reopen(const char *backend, bool backendExplicit);

#endif /* ICO_PLATFORM_WINDOW_SWITCH_H */
