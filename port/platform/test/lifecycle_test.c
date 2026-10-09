/*
 * port/platform/test/lifecycle_test.c
 *
 * The Android lifecycle's tables (window_lifecycle.h) on fake
 * operations that write down each call: the order of every event's steps,
 * the settings saved only when changed, a missing operation skipped, a
 * failed surface or save logged, unknown events refused, and SDL's event
 * types mapped.
 */
#include <stdio.h>
#include <string.h>

#include "window_lifecycle.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* the calls, one word each, space separated */
typedef struct Fake {
    char calls[512];
    int dirty;
    int recreateFails;
    int saveFails;
    char lastLog[128];
} Fake;

static void note(Fake *f, const char *word)
{
    size_t n = strlen(f->calls);

    snprintf(f->calls + n, sizeof(f->calls) - n, "%s%s", n ? " " : "", word);
}

static void f_idle(void *u)
{
    note(u, "idle");
}

static void f_release(void *u)
{
    note(u, "release");
}

static int f_recreate(void *u)
{
    note(u, "recreate");
    return ((Fake *)u)->recreateFails ? -1 : 0;
}

static void f_watchdog(void *u, int paused)
{
    note(u, paused ? "watchdog-pause" : "watchdog-resume");
}

static void f_audio(void *u, int paused)
{
    note(u, paused ? "audio-pause" : "audio-resume");
}

static void f_pace(void *u)
{
    note(u, "pace");
}

static int f_dirty(void *u)
{
    note(u, "dirty?");
    return ((Fake *)u)->dirty;
}

static int f_save(void *u)
{
    note(u, "save");
    return ((Fake *)u)->saveFails ? -1 : 0;
}

static void f_record(void *u)
{
    note(u, "record-close");
}

static void f_texture(void *u)
{
    note(u, "texture");
}

static void f_log(void *u, const char *line)
{
    Fake *f = u;

    note(f, "log");
    snprintf(f->lastLog, sizeof(f->lastLog), "%s", line);
}

static void f_flush(void *u)
{
    note(u, "flush");
}

static IcoLifecycleOps ops_for(Fake *f)
{
    IcoLifecycleOps o;

    memset(f, 0, sizeof(*f));
    o.user = f;
    o.gpu_wait_idle = f_idle;
    o.surface_release = f_release;
    o.surface_recreate = f_recreate;
    o.watchdog_pause = f_watchdog;
    o.audio_pause = f_audio;
    o.pace_reset = f_pace;
    o.config_dirty = f_dirty;
    o.config_save = f_save;
    o.record_close = f_record;
    o.texture_low_memory = f_texture;
    o.log = f_log;
    o.log_flush = f_flush;
    return o;
}

#define CHECK_CALLS(f, want)                                                                       \
    do {                                                                                           \
        if (strcmp((f).calls, (want)) != 0) {                                                      \
            printf("FAIL %s:%d: calls \"%s\", expected \"%s\"\n", __FILE__, __LINE__, (f).calls,   \
                   (want));                                                                        \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    Fake f;
    IcoLifecycleOps o;

    /* the background: idle before the surface goes, the watchdog and the
       sound stopped, the log on the disc last */
    o = ops_for(&f);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_WILL_ENTER_BACKGROUND, &o) == 6);
    CHECK_CALLS(f, "log idle release watchdog-pause audio-pause flush");
    CHECK(strcmp(f.lastLog, "lifecycle: will enter background") == 0);

    /* the foreground: the surface first, then the watchdog, the sound and
       the pacer's deadline */
    o = ops_for(&f);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_DID_ENTER_FOREGROUND, &o) == 5);
    CHECK_CALLS(f, "recreate watchdog-resume audio-resume pace log");
    CHECK(strcmp(f.lastLog, "lifecycle: did enter foreground") == 0);

    /* no surface yet: said in the log, the rest still runs */
    o = ops_for(&f);
    f.recreateFails = 1;
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_DID_ENTER_FOREGROUND, &o) == 5);
    CHECK_CALLS(f, "recreate log watchdog-resume audio-resume pace log");

    /* terminating with unsaved settings: saved, the recording closed,
       then the flush */
    o = ops_for(&f);
    f.dirty = 1;
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_TERMINATING, &o) == 4);
    CHECK_CALLS(f, "log dirty? save record-close flush");

    /* nothing changed: no save */
    o = ops_for(&f);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_TERMINATING, &o) == 3);
    CHECK_CALLS(f, "log dirty? record-close flush");

    /* a failed save is logged */
    o = ops_for(&f);
    f.dirty = 1;
    f.saveFails = 1;
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_TERMINATING, &o) == 4);
    CHECK_CALLS(f, "log dirty? save log record-close flush");

    /* low memory */
    o = ops_for(&f);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_LOW_MEMORY, &o) == 3);
    CHECK_CALLS(f, "texture log flush");
    CHECK(strcmp(f.lastLog, "lifecycle: low memory") == 0);

    /* a missing operation is skipped and not counted */
    o = ops_for(&f);
    o.surface_release = NULL;
    o.audio_pause = NULL;
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_WILL_ENTER_BACKGROUND, &o) == 4);
    CHECK_CALLS(f, "log idle watchdog-pause flush");
    o = ops_for(&f);
    o.config_dirty = NULL; /* nothing says the settings changed: no save */
    f.dirty = 1;
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_TERMINATING, &o) == 3);
    CHECK_CALLS(f, "log record-close flush");

    /* outside the table */
    o = ops_for(&f);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_NONE, &o) == -1);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_COUNT, &o) == -1);
    CHECK(ico_lifecycle_on((IcoLifecycleEvent)-1, &o) == -1);
    CHECK(ico_lifecycle_on(ICO_LIFECYCLE_LOW_MEMORY, NULL) == -1);
    CHECK_CALLS(f, "");
    CHECK(ico_lifecycle_steps(ICO_LIFECYCLE_NONE) == NULL);
    CHECK(strcmp(ico_lifecycle_name(ICO_LIFECYCLE_COUNT), "none") == 0);

    /* every table ends, and every step in it is a known one */
    for (int e = ICO_LIFECYCLE_NONE + 1; e < ICO_LIFECYCLE_COUNT; e++) {
        const IcoLifecycleStep *s = ico_lifecycle_steps((IcoLifecycleEvent)e);
        int n = 0;

        CHECK(s != NULL);
        while (s != NULL && s[n] != ICO_LC_END && n < 32) {
            CHECK(s[n] > ICO_LC_END && s[n] < ICO_LC_STEP_COUNT);
            n++;
        }
        CHECK(n > 0 && n < 32);
    }

    /* SDL3's event types (SDL_events.h: SDL_EVENT_QUIT 0x100, then
       TERMINATING, LOW_MEMORY, WILL_ENTER_BACKGROUND, DID_ENTER_BACKGROUND,
       WILL_ENTER_FOREGROUND, DID_ENTER_FOREGROUND) */
    CHECK(ico_lifecycle_from_sdl(0x101u) == ICO_LIFECYCLE_TERMINATING);
    CHECK(ico_lifecycle_from_sdl(0x102u) == ICO_LIFECYCLE_LOW_MEMORY);
    CHECK(ico_lifecycle_from_sdl(0x103u) == ICO_LIFECYCLE_WILL_ENTER_BACKGROUND);
    CHECK(ico_lifecycle_from_sdl(0x104u) == ICO_LIFECYCLE_NONE);
    CHECK(ico_lifecycle_from_sdl(0x105u) == ICO_LIFECYCLE_NONE);
    CHECK(ico_lifecycle_from_sdl(0x106u) == ICO_LIFECYCLE_DID_ENTER_FOREGROUND);
    CHECK(ico_lifecycle_from_sdl(0x100u) == ICO_LIFECYCLE_NONE);
    CHECK(ico_lifecycle_from_sdl(0x300u) == ICO_LIFECYCLE_NONE);

    if (fails == 0) {
        printf("lifecycle_test: all checks passed\n");
    }
    return fails ? 1 : 0;
}
