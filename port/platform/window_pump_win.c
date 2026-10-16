/*
 * port/platform/window_pump_win.c
 *
 * The window's messages recorded and the system kept awake on Windows
 * (window_pump_win.h). Only the Windows window build compiles it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include "diag_host.h"
#include "pump_watch.h"
#include "window_pump_win.h"

#define KEEP_AWAKE_EVERY_NS (30ull * 1000000000ull)

/* the game window's procedure before ours (SDL's) */
static WNDPROC s_next;
static HWND s_hwnd;

/* the posted message the hook just recorded: its dispatch to the game
   window's procedure is the same message, not recorded twice */
static struct {
    int valid;
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
} s_hooked;

static bool SDLCALL message_hook(void *userdata, MSG *msg)
{
    (void)userdata;
    if (pump_watch_msg(msg->message, (uint64_t)msg->wParam, (int64_t)msg->lParam,
                       SDL_GetTicksNS())) {
        s_hooked.valid = msg->hwnd == s_hwnd;
        s_hooked.msg = msg->message;
        s_hooked.wparam = msg->wParam;
        s_hooked.lparam = msg->lParam;
    }
    return true; /* SDL goes on with it as always */
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    const uint64_t t0 = SDL_GetTicksNS();
    char name[96];
    LRESULT r;
    uint64_t took;

    if (s_hooked.valid && s_hooked.msg == msg && s_hooked.wparam == wparam &&
        s_hooked.lparam == lparam) {
        s_hooked.valid = 0; /* the hook's message, dispatched */
    } else {
        (void)pump_watch_msg(msg, (uint64_t)wparam, (int64_t)lparam, t0);
    }
    pump_watch_msg_enter(msg, (uint64_t)wparam, (int64_t)lparam, t0);
    r = CallWindowProcW(s_next, hwnd, msg, wparam, lparam);
    took = pump_watch_msg_leave(SDL_GetTicksNS(), name, sizeof(name));
    if (took > PUMP_WATCH_SLOW_NS) {
        fprintf(stderr, "window: the window message %s took %.1f ms in SDL's window procedure\n",
                name, (double)took / 1e6);
    }
    return r;
}

void ico_pump_win_install(void *hwnd)
{
    HWND h = (HWND)hwnd;
    LONG_PTR prev;

    SDL_SetWindowsMessageHook(message_hook, NULL);
    if (h == NULL || h == s_hwnd) {
        return;
    }
    SetLastError(0);
    prev = SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)window_proc);
    if (prev == 0 && GetLastError() != 0) {
        fprintf(stderr, "window: the window's messages are not recorded (error %lu)\n",
                (unsigned long)GetLastError());
        return;
    }
    s_next = (WNDPROC)prev;
    s_hwnd = h;
    fprintf(stderr, "window: the window's messages are recorded for the event pump's lines\n");
}

void ico_pump_win_keep_awake(uint64_t now_ns, int active)
{
    static uint64_t s_last;
    static int s_logged;
    static int s_released;

    if (!active) {
        /* minimized or without focus: let the display and the computer
           sleep as they would for any other program, once */
        if (!s_released) {
            s_released = 1;
            s_last = 0;
            SetThreadExecutionState(ES_CONTINUOUS);
            ico_log_limited("window: keep awake released",
                            "window: the display may sleep while the game is minimized or in the "
                            "background");
        }
        return;
    }
    if (s_released) {
        s_released = 0;
        s_last = 0;
        ico_log_limited("window: keep awake again",
                        "window: the display and the computer are kept awake again");
    }
    if (s_last != 0 && now_ns - s_last < KEEP_AWAKE_EVERY_NS) {
        return;
    }
    s_last = now_ns;
    const EXECUTION_STATE was =
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    if (!s_logged) {
        s_logged = 1;
        fprintf(stderr,
                "window: the display and the computer are kept awake while the game runs%s\n",
                was != 0 ? "" : " (Windows refused: SetThreadExecutionState failed)");
    }
}
