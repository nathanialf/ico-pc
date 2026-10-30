/*
 * port/platform/hotkeys.h
 *
 * The window build's diagnostic keys, kept free of SDL so a unit test checks them:
 *
 *   F12   the frame shown, for a bug report, in <pref>/dumps: an rd dump of
 *         the last closed frame and one of the tick before (when it is the
 *         tick right before) zipped with a copy of the log,
 *         frame-<time>-v<vsync>.zip (dump_zip.h; the loose .rddump is kept
 *         only when the zip fails), the whole DISPLAY frame as
 *         frame-<time>-v<vsync>.png, and the picture as presented as
 *         frame-<time>-present.png; the paths are logged (window_host.c,
 *         frame_dump)
 *   Shift+F12  in photo mode, the aspect sweep: the paused frame re-rendered
 *         at 4:3, 16:9, 32:9, 48:9 and 60:9 into one zip,
 *         <pref>/dumps/sweep-<time>.zip holding <aspect>.rddump and
 *         <aspect>.png for each (window_host.c; PC only, there is no key
 *         on Android)
 *   F11   the window: stats lines every second for 30 s instead of every
 *         10 s; again turns it off
 *   F5    save states: save to slot 1 (window_host.c dispatches)
 *   F9    save states: load slot 1
 *
 * None of these keys is bindable (port/input/keys.def has F1 to F4 only), and
 * a held key's repeats do nothing.
 */
#ifndef ICO_PLATFORM_HOTKEYS_H
#define ICO_PLATFORM_HOTKEYS_H

#include <stddef.h>

/* SDL3's SDLK_F5, SDLK_F9, SDLK_F11 and SDLK_F12 (SDL_SCANCODE_TO_KEYCODE of
   scancodes 62, 66, 68 and 69); window_host.c checks them against SDL's at
   compile time. */
#define ICO_HOTKEY_KEY_F5 0x4000003Eu
#define ICO_HOTKEY_KEY_F9 0x40000042u
#define ICO_HOTKEY_KEY_F11 0x40000044u
#define ICO_HOTKEY_KEY_F12 0x40000045u

typedef enum IcoHotkey {
    ICO_HOTKEY_NONE,
    ICO_HOTKEY_FRAME_DUMP,   /* F12 */
    ICO_HOTKEY_STATS_FAST,   /* F11 */
    ICO_HOTKEY_ASPECT_SWEEP, /* Shift+F12 */
    ICO_HOTKEY_STATE_SAVE,   /* F5: save states, slot 1 (window_host.c dispatches) */
    ICO_HOTKEY_STATE_LOAD    /* F9: slot 1 */
} IcoHotkey;

/* The action of a key-down event (SDL keycode, whether it is a repeat), no
   modifier held. */
IcoHotkey ico_hotkey_for(unsigned int key, int repeat);
/* The same with Shift held or not: Shift+F12 is the aspect sweep, Shift
   changes nothing for the other keys. */
IcoHotkey ico_hotkey_for_mod(unsigned int key, int repeat, int shift);

/* The aspect sweep's files in dir: sweep-<stamp>.zip, and the prefix of the
   loose files dump_zip.h ico_sweep_zip takes in (dir/sweep-<stamp>-).  0, or
   -1 when a path does not fit (both then ""). */
int ico_sweep_paths(const char *dir, const char *stamp, char *prefix, size_t prefix_size, char *zip,
                    size_t zip_size);

/* The stats line's period: ICO_STATS_FAST_PERIOD_NS while now is before
   fast_until, else ICO_STATS_PERIOD_NS. */
#define ICO_STATS_PERIOD_NS 10000000000ull
#define ICO_STATS_FAST_PERIOD_NS 1000000000ull
#define ICO_STATS_FAST_SPAN_NS 30000000000ull
unsigned long long ico_stats_period_ns(unsigned long long now, unsigned long long fast_until);
/* F11: the new fast_until (now + the span, or 0 when it was running). */
unsigned long long ico_stats_fast_toggle(unsigned long long now, unsigned long long fast_until);

/* F12's three files in dir: frame-<stamp>-v<vsync>.rddump, .png and .zip
   (the zip holds the rddump, the tick before's and the log, dump_zip.h). 0, or -1 when any
   path does not fit (all three then ""). */
int ico_frame_dump_paths(const char *dir, const char *stamp, unsigned int vsync, char *dump,
                         size_t dump_size, char *png, size_t png_size, char *zip, size_t zip_size);

/* F12's fourth file: the picture as presented (the window's pixels, the
   port's own UI left out), frame-<stamp>-present.png in dir; the .png above
   is the whole DISPLAY frame (wider than the picture at 16:9).  0, or -1
   when the path does not fit (png then ""). */
int ico_frame_present_path(const char *dir, const char *stamp, char *png, size_t png_size);

#endif /* ICO_PLATFORM_HOTKEYS_H */
