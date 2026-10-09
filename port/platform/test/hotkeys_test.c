/*
 * port/platform/test/hotkeys_test.c
 *
 * The window build's diagnostic keys (hotkeys.h): F12 is the
 * frame dump and F11 the fast stats lines, a held key's repeats do
 * nothing, no other key is taken (Escape, Alt+Enter and the bindable keys
 * stay with window_host.c and the binding layer), the stats period and
 * F11's toggle, and F12's file names. window_host.c checks the key codes
 * against SDL3's SDLK_F11 and SDLK_F12 at compile time and dispatches on
 * ico_hotkey_for.
 */
#include <stdio.h>
#include <string.h>

#include "hotkeys.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* SDL3's values (SDL_keycode.h), spelled out so the test needs no SDL */
#define K_ESCAPE 0x1bu
#define K_RETURN 0x0du
#define K_F1 0x4000003au
#define K_F4 0x4000003du
#define K_F10 0x40000043u
#define K_PRINTSCREEN 0x40000046u
#define K_A 0x61u

int main(void)
{
    const unsigned long long s = 1000000000ull;
    unsigned long long until;
    char dump[256], png[256];

    /* the two keys, once per press */
    CHECK(ico_hotkey_for(ICO_HOTKEY_KEY_F12, 0) == ICO_HOTKEY_FRAME_DUMP);
    CHECK(ico_hotkey_for(ICO_HOTKEY_KEY_F11, 0) == ICO_HOTKEY_STATS_FAST);
    CHECK(ico_hotkey_for(ICO_HOTKEY_KEY_F12, 1) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(ICO_HOTKEY_KEY_F11, 1) == ICO_HOTKEY_NONE);
    /* SDL3: SDLK_F11/F12 = scancode 68/69 | SDLK_SCANCODE_MASK (1 << 30) */
    CHECK(ICO_HOTKEY_KEY_F11 == (68u | (1u << 30)));
    CHECK(ICO_HOTKEY_KEY_F12 == (69u | (1u << 30)));
    /* nothing else (Escape is the pad's Start in play, Triangle elsewhere:
       window_host.c escape_event) */
    CHECK(ico_hotkey_for(K_ESCAPE, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_RETURN, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_F1, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_F4, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_F10, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_PRINTSCREEN, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(K_A, 0) == ICO_HOTKEY_NONE);
    CHECK(ico_hotkey_for(0, 0) == ICO_HOTKEY_NONE);

    /* the stats period: 10 s, 1 s for 30 s after F11, F11 again stops it */
    CHECK(ico_stats_period_ns(5 * s, 0) == 10 * s);
    until = ico_stats_fast_toggle(100 * s, 0);
    CHECK(until == 130 * s);
    CHECK(ico_stats_period_ns(100 * s, until) == 1 * s);
    CHECK(ico_stats_period_ns(129 * s, until) == 1 * s);
    CHECK(ico_stats_period_ns(130 * s, until) == 10 * s);
    CHECK(ico_stats_fast_toggle(110 * s, until) == 0);
    /* after it ran out, F11 starts a new 30 s */
    CHECK(ico_stats_fast_toggle(140 * s, until) == 170 * s);

    /* F12's files */
    CHECK(ico_frame_dump_paths("/p/dumps", "20261005-073732", 1234, dump, sizeof(dump), png,
                               sizeof(png)) == 0);
#ifdef _WIN32
    CHECK(strcmp(dump, "/p/dumps\\frame-20261005-073732-v1234.rddump") == 0);
#else
    CHECK(strcmp(dump, "/p/dumps/frame-20261005-073732-v1234.rddump") == 0);
    CHECK(strcmp(png, "/p/dumps/frame-20261005-073732-v1234.png") == 0);
#endif
    ico_frame_dump_paths("/p/dumps/", "x", 1, dump, sizeof(dump), png, sizeof(png));
    CHECK(strcmp(dump, "/p/dumps/frame-x-v1.rddump") == 0);
    /* a path that does not fit: refused, both empty, not cut short */
    {
        char small[24];

        CHECK(ico_frame_dump_paths("/p/dumps", "x", 1, small, sizeof(small), png, sizeof(png)) ==
              -1);
        CHECK(small[0] == '\0' && png[0] == '\0');
    }

    printf("hotkeys_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
