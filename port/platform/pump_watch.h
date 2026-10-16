/*
 * port/platform/pump_watch.h
 *
 * What the window's event pump went through, for the lines that explain
 * a long pump (issue 105: Windows held the pump for 50 s at a time while
 * the game idled, right after a controller went away). Plain C, no SDL and
 * no windows.h, so port/platform/test/pump_watch_test.c checks it on every
 * build; window_host.c and window_pump_win.c feed it:
 *
 *   - the part of the pump that runs now (pump_watch_part: "the system's
 *     messages and SDL's own work (SDL_PollEvent)", "the controllers'
 *     update (SDL_UpdateJoysticks)", ...), so the watchdog can say where a
 *     pump that has not returned is;
 *   - the last window messages the program received (Windows: the message
 *     hook sees the posted ones, the window procedure the sent ones),
 *     coalesced (a repeat of the newest only counts), the noisy ones
 *     (mouse moves, raw input, hit tests) left out, and the message whose
 *     handling is running now (pump_watch_msg_enter/leave);
 *   - the last SDL events the pump took (pump_watch_event);
 *   - the controllers that arrived or left, with their name, GUID and the
 *     SDL driver that found them (pump_watch_pad), so the slow-pump line
 *     names the device whose change came before the stall;
 *   - SDL's own log lines (pump_watch_sdl_log: window_host.c installs an
 *     SDL_SetLogOutputFunction with the INPUT and SYSTEM categories at
 *     VERBOSE), the last PUMP_WATCH_SDL_LINES of them, printed with a slow
 *     pump's line and in the report file (report.h), so the joystick
 *     drivers' own steps (a HIDAPI device added or removed, a DirectInput
 *     warning) sit beside the stall in time.
 *
 * Times are the caller's monotonic nanoseconds. The main thread writes;
 * pump_watch_describe also runs on the watchdog's thread while the main
 * thread is held, and reads plain words (a torn entry prints a wrong name
 * at worst).
 */
#ifndef ICO_PLATFORM_PUMP_WATCH_H
#define ICO_PLATFORM_PUMP_WATCH_H

#include <stddef.h>
#include <stdint.h>

/* a part of the pump, or one message's handling, longer than this is logged */
#define PUMP_WATCH_SLOW_NS 1000000000ull
#define PUMP_WATCH_MSGS 16
#define PUMP_WATCH_EVENTS 8
#define PUMP_WATCH_PADS 4
#define PUMP_WATCH_SDL_LINES 64
#define PUMP_WATCH_SDL_TEXT 200

/* Back to the empty state (tests). */
void pump_watch_reset(void);

/* The part of the pump running from now (a static string; NULL: none). */
void pump_watch_part(const char *part);
const char *pump_watch_part_now(void);

/* A window message received (msg, its wParam and lParam). Returns 0 when
   it is one of the noisy ones and was not recorded. */
int pump_watch_msg(uint32_t msg, uint64_t wparam, int64_t lparam, uint64_t now_ns);
/* Its handling starts and ends (nested handling keeps a short stack).
   leave returns how long it took; name (may be NULL) gets its name. */
void pump_watch_msg_enter(uint32_t msg, uint64_t wparam, int64_t lparam, uint64_t now_ns);
uint64_t pump_watch_msg_leave(uint64_t now_ns, char *name, size_t size);

/* An SDL event the pump took (a static string naming it). */
void pump_watch_event(const char *name, uint64_t now_ns);

/* A controller arrived (added 1) or left (0): SDL's instance id, its name,
   GUID text (SDL_GUIDToString) and driver (pump_watch_guid_driver). A
   removal looks the id's arrival up for the name when name is NULL. */
void pump_watch_pad(int added, uint32_t id, const char *name, const char *guid, const char *driver,
                    uint64_t now_ns);
/* The newest arrival or removal as `"Xbox Controller" (GUID 0300...,
   XInput)` ("" when none was recorded). */
void pump_watch_pad_newest(char *out, size_t size);
/* The newest arrival or removal's time, 0 when none was recorded. */
uint64_t pump_watch_pad_last_ns(void);
/* "controllers before it: removed "Xbox Controller" (GUID 0300..., XInput)
   50.3 s ago, added ..." (newest first), "" when none was recorded. */
void pump_watch_pads_recent(char *out, size_t size, uint64_t now_ns);
/* The SDL driver a joystick GUID names (SDL_joystick.c: byte 14 is the
   driver's signature): 'x' "XInput", 'h' "HIDAPI", 'r' "RawInput", 'w'
   "Windows.Gaming.Input", 'g' "GameInput", 'v' "virtual", 'm' "MFi",
   0 "DirectInput" on Windows (windows != 0), else "the system's"
   (evdev, IOKit, Android); another byte "driver 'c'" in a static buffer. */
const char *pump_watch_guid_driver(const unsigned char guid[16], int windows);

/* One line of SDL's own log (any thread): SDL's category and priority
   numbers (SDL_log.h), the text (cut at PUMP_WATCH_SDL_TEXT - 1). */
void pump_watch_sdl_log(int category, int priority, const char *text, uint64_t now_ns);
/* The lines recorded so far (also those the ring no longer holds). */
uint32_t pump_watch_sdl_count(void);
/* The ring's lines newer than since_ns (0: all it holds), oldest first,
   each "<prefix>12.3 s ago INPUT debug: text\n" (the age at now_ns); the
   number of lines written. A line that does not fit is left out with
   the ones after it. */
int pump_watch_sdl_text(char *out, size_t size, const char *prefix, uint64_t since_ns,
                        uint64_t now_ns);
/* SDL's category and priority names ("INPUT", "debug"; "custom", "?"). */
const char *pump_watch_sdl_category(int category);
const char *pump_watch_sdl_priority(int priority);

/* A message's name: "WM_SYSCOMMAND SC_MONITORPOWER off(0x0112)",
   "WM_DEVICECHANGE DBT_DEVICEREMOVECOMPLETE(0x0219)", "message 0xc0f3"... */
void pump_watch_msg_name(uint32_t msg, uint64_t wparam, int64_t lparam, char *out, size_t size);

/* "last messages before it: WM_TIMER(0x0113) x3 0.4 s ago, ...; SDL events
   before it: gamepad removed 50.3 s ago, ..." (the newest first; "none"
   for an empty list; the message part only once one was recorded, so a
   build without the hook says nothing about messages; then "; " and
   pump_watch_pads_recent once a controller was recorded). */
void pump_watch_recent(char *out, size_t size, uint64_t now_ns);

/* For the watchdog while a pump has not returned: the part running, the
   message being handled and for how long, then pump_watch_recent. */
void pump_watch_describe(char *out, size_t size, uint64_t now_ns);

#endif
