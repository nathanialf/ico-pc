/*
 * port/platform/window_pump_win.h
 *
 * The window's messages on Windows, for the event pump's long-call lines
 * (issue 105; pump_watch.h), and the display and the system kept awake:
 *
 *   - SDL's message hook (SDL_SetWindowsMessageHook) records each posted
 *     message the pump takes for any window of the main thread (SDL's
 *     hidden device-notification windows included); a window procedure in
 *     front of SDL's on the game's window records the sent ones (the
 *     system's WM_SYSCOMMAND, WM_POWERBROADCAST, WM_DISPLAYCHANGE,
 *     WM_ACTIVATE... never pass the hook) and times every message's
 *     handling, logging one over 1 s ("window: the window message <name>
 *     took N ms in SDL's window procedure"). Both run on the main thread
 *     and let everything through unchanged;
 *   - SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED |
 *     ES_SYSTEM_REQUIRED) from the main thread, at the first pump and every
 *     30 s after: SDL_DisableScreenSaver sets the display flag alone
 *     (SDL_windowsvideo.c WIN_SuspendScreenSaver), which keeps the display
 *     on but not the system out of its idle sleep.
 *
 * Elsewhere both are empty.
 */
#ifndef ICO_PLATFORM_WINDOW_PUMP_WIN_H
#define ICO_PLATFORM_WINDOW_PUMP_WIN_H

#include <stdint.h>

#ifdef _WIN32
/* hwnd: the game window's HWND (SDL_PROP_WINDOW_WIN32_HWND_POINTER); again
   for a window made again (a new HWND is subclassed, the same one is not) */
void ico_pump_win_install(void *hwnd);
/* each pump, with the pump's monotonic time */
void ico_pump_win_keep_awake(uint64_t now_ns);
#else
static inline void ico_pump_win_install(void *hwnd)
{
    (void)hwnd;
}

static inline void ico_pump_win_keep_awake(uint64_t now_ns)
{
    (void)now_ns;
}
#endif

#endif
