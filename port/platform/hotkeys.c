/*
 * port/platform/hotkeys.c
 *
 * The window build's diagnostic keys (hotkeys.h).
 */
#include "hotkeys.h"
#include <stdio.h>
#include <string.h>

IcoHotkey ico_hotkey_for(unsigned int key, int repeat)
{
    if (repeat) {
        return ICO_HOTKEY_NONE;
    }
    switch (key) {
    case ICO_HOTKEY_KEY_F12:
        return ICO_HOTKEY_FRAME_DUMP;
    case ICO_HOTKEY_KEY_F11:
        return ICO_HOTKEY_STATS_FAST;
    default:
        return ICO_HOTKEY_NONE;
    }
}

unsigned long long ico_stats_period_ns(unsigned long long now, unsigned long long fast_until)
{
    return now < fast_until ? ICO_STATS_FAST_PERIOD_NS : ICO_STATS_PERIOD_NS;
}

unsigned long long ico_stats_fast_toggle(unsigned long long now, unsigned long long fast_until)
{
    return now < fast_until ? 0 : now + ICO_STATS_FAST_SPAN_NS;
}

void ico_frame_dump_paths(const char *dir, const char *stamp, unsigned int vsync, char *dump,
                          size_t dump_size, char *png, size_t png_size)
{
    const size_t n = strlen(dir);
#ifdef _WIN32
    const char *sep = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "\\";
#else
    const char *sep = n > 0 && dir[n - 1] == '/' ? "" : "/";
#endif

    snprintf(dump, dump_size, "%s%sframe-%s-v%u.rddump", dir, sep, stamp, vsync);
    snprintf(png, png_size, "%s%sframe-%s-v%u.png", dir, sep, stamp, vsync);
}
