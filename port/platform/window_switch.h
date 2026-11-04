/*
 * port/platform/window_switch.h
 *
 * Options > Graphics > Apply: the renderer switched to another backend, or
 * to another graphics card, in the process, the game going on
 * (window_switch.c), and the Graphics page's
 * host (settings.h UiGraphicsHost).  The page asks; the switch itself runs
 * in the event pump (ico_window_switch_pump), between two steps of the game
 * and never inside a present.  window_host.c makes the window and the
 * renderer again (ico_window_reopen).
 *
 *   ico_window_switch_backend(name, adapter, trial)
 *                               the switch now, to name on the card adapter
 *                               (rhi_set_adapter_wanted: "" automatic, NULL
 *                               the card asked for before): the renderer
 *                               shut down with
 *                               the game's state kept (rd_suspend), the
 *                               window made again with the backend's flags
 *                               in the same mode, size and place on the
 *                               same display, rd_resume, what the start
 *                               does after rd_init and rd_resume_frame;
 *                               trial: Apply's switch (no restart planned
 *                               after a crash or loss until Keep), else a
 *                               switch back (the start's choice again).
 *                               0 switched (or name runs already on that
 *                               card); -1 name did not start and the backend
 *                               that ran is back, on the card asked for
 *                               before; -2 refused (a movie plays, photo mode,
 *                               the model viewer, the touch layout editor, a
 *                               build with one renderer, a name the build
 *                               does not link); -3 neither started (no
 *                               renderer: the game ends)
 *   ico_window_switch_running() the backend running, as the config key
 *                               [video] backend names it ("vulkan",
 *                               "opengl", "d3d12", "d3d11")
 *   ico_window_switch_install() the Graphics page's host and its card names
 *                               (installed again after every switch)
 *   ico_window_switch_pump()    a switch the page asked for, from
 *                               ico_window_pump, under the switch's own
 *                               watchdog limit (diag_host.h
 *                               ico_diag_watchdog_switch); 0, or
 *                               -1 when no renderer is left (the pump ends
 *                               the run)
 *   ico_window_switch_end_text() after that -1: the box's text naming both
 *                               renderers (main_host.c shows it and saves
 *                               nothing); NULL otherwise
 *   ico_window_switch_started(backendExplicit, backend)
 *                               ico_window_open's choice once the renderer
 *                               is up (the player's, or Automatic's with
 *                               its fallback or restart backend)
 *   ico_window_switch_wait_hooks()
 *                               the renderer's wait hooks (rhi.h
 *                               RhiWaitHooks), set once before the first
 *                               rd_init: a long driver call (an OpenGL
 *                               shader compile on its own thread, a fence
 *                               wait) takes the window's events on this
 *                               thread meanwhile, is held to the GPU hang
 *                               limit, and each shader and pipeline made
 *                               is progress for the switch's limit; a
 *                               compile given up on makes the switch go
 *                               back with a box naming the shader
 *   ico_window_reopen(backend, backendExplicit)
 *                               window_host.c's part: 1 up, 0 not (no
 *                               window then; the next call makes one);
 *                               backendExplicit for the crash and loss
 *                               steps (ico_diag_set_startup_retry)
 */
#ifndef ICO_PLATFORM_WINDOW_SWITCH_H
#define ICO_PLATFORM_WINDOW_SWITCH_H

#include <stdbool.h>

int ico_window_switch_backend(const char *name, const char *adapter, int trial);
const char *ico_window_switch_running(void);
void ico_window_switch_install(void);
int ico_window_switch_pump(void);
const char *ico_window_switch_end_text(void);
void ico_window_switch_started(bool backendExplicit, const char *backend);
int ico_window_reopen(const char *backend, bool backendExplicit);
void ico_window_switch_wait_hooks(void);

#endif /* ICO_PLATFORM_WINDOW_SWITCH_H */
