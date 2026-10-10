/*
 * port/platform/hotkeys.h
 *
 * The window build's diagnostic keys, kept free of SDL so a unit test checks them:
 *
 *   F12   the frame shown: an rd dump of the last closed frame and a PNG of
 *         the picture, <pref>/dumps/frame-<time>-v<vsync>.rddump and .png,
 *         both paths logged (window_host.c, rd_dump_on_demand)
 *   F11   the window: stats lines every second for 30 s instead of every
 *         10 s; again turns it off
 *
 * Neither key is bindable (port/input/keys.def has F1 to F4 only), and a held
 * key's repeats do nothing.
 */
#ifndef ICO_PLATFORM_HOTKEYS_H
#define ICO_PLATFORM_HOTKEYS_H

#include <stddef.h>

/* SDL3's SDLK_F11 and SDLK_F12 (SDL_SCANCODE_TO_KEYCODE of scancodes 68
   and 69); window_host.c checks them against SDL's at compile time. */
#define ICO_HOTKEY_KEY_F11 0x40000044u
#define ICO_HOTKEY_KEY_F12 0x40000045u

typedef enum IcoHotkey {
    ICO_HOTKEY_NONE,
    ICO_HOTKEY_FRAME_DUMP, /* F12 */
    ICO_HOTKEY_STATS_FAST  /* F11 */
} IcoHotkey;

/* The action of a key-down event (SDL keycode, whether it is a repeat). */
IcoHotkey ico_hotkey_for(unsigned int key, int repeat);

/* The stats line's period: ICO_STATS_FAST_PERIOD_NS while now is before
   fast_until, else ICO_STATS_PERIOD_NS. */
#define ICO_STATS_PERIOD_NS 10000000000ull
#define ICO_STATS_FAST_PERIOD_NS 1000000000ull
#define ICO_STATS_FAST_SPAN_NS 30000000000ull
unsigned long long ico_stats_period_ns(unsigned long long now, unsigned long long fast_until);
/* F11: the new fast_until (now + the span, or 0 when it was running). */
unsigned long long ico_stats_fast_toggle(unsigned long long now, unsigned long long fast_until);

/* F12's three files in dir: frame-<stamp>-v<vsync>.rddump, .png and .zip
   (the zip holds the rddump and the log, dump_zip.h). 0, or -1 when any
   path does not fit (all three then ""). */
int ico_frame_dump_paths(const char *dir, const char *stamp, unsigned int vsync, char *dump,
                         size_t dump_size, char *png, size_t png_size, char *zip, size_t zip_size);

#endif /* ICO_PLATFORM_HOTKEYS_H */
