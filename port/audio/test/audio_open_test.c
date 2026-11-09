/*
 * port/audio/test/audio_open_test.c
 *
 * The start's device open (out_sdl.c, "the open"; audio_host.c
 * ico_audio_host_init, open_adopt, open_step) with the device open replaced
 * by a fake on the open's own thread (out_sdl.c open_hook): a device-less
 * SDL stream, handed back when the test opens its gate, or NULL (a device
 * that fails).  SDL's audio subsystem runs on the dummy driver, so no
 * device is touched.  The waits are shortened (the start's 2 s to 200 ms,
 * the shutdown's 2 s to 300 ms) so the test runs in about a second.
 *
 *   - opens at once: adopted inside the init, on the device's clock; the
 *     next vsync renders nothing (the SPU2's time stands), a device's take
 *     renders through the callback;
 *   - opens after five vsyncs: the init returns after the wait on the
 *     game's clock ("the device is opening", no stream, no callback, the
 *     Settings menu's move refused), each vsync renders its frames; once
 *     the fake returns, the next vsync adopts it before it renders: the
 *     device's clock, the watchdog's vsync reset to that vsync, nothing
 *     rendered on it;
 *   - fails at once and fails late: the game's clock, "no device", SDL's
 *     audio closed again;
 *   - never opens: 1201 vsyncs at 60 Hz on the game's clock write the
 *     still-waiting line twice (every 10 s of vsyncs); the shutdown returns
 *     after its bound, the thread left behind, which closes and frees what
 *     it opens once the gate opens.
 *
 * Every check is a state, a count or the SPU2's time, except the
 * shutdown's bound (between 300 ms and 300 ms plus 2 s).
 */
#define OPEN_WAIT_MS 200
#define OPEN_CLOSE_WAIT_MS 300

#include "../out_sdl.c"

#include "../audio_host.c"

#include "../../test/ico_check.h"
#include "../spu2.h"

#define HZ 60

static int gate;      /* 1: the fake open returns */
static int fake_fail; /* 1: it returns no stream */
static int hook_calls;
static int16_t take[PULL_MIN_FRAMES * CHANNELS];

static SDL_AudioStream *fake_open(SDL_AudioDeviceID *named)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, CHANNELS, RATE};

    __atomic_fetch_add(&hook_calls, 1, __ATOMIC_RELAXED);
    while (!__atomic_load_n(&gate, __ATOMIC_ACQUIRE)) {
        SDL_Delay(1);
    }
    *named = 0;
    if (__atomic_load_n(&fake_fail, __ATOMIC_ACQUIRE)) {
        SDL_SetError("the fake device refused");
        return NULL;
    }
    return SDL_CreateAudioStream(&spec, &spec);
}

/* a fresh start of the audio host with the fake's gate and outcome */
static void start(int open_now, int fail)
{
    __atomic_store_n(&fake_fail, fail, __ATOMIC_RELEASE);
    __atomic_store_n(&gate, open_now, __ATOMIC_RELEASE);
    started = 0; /* audio_host.c: once a run */
    ico_audio_host_init();
}

static void open_gate(void)
{
    __atomic_store_n(&gate, 1, __ATOMIC_RELEASE);
}

/* one vsync; the SPU2's frames it rendered */
static uint64_t vsync(void)
{
    const uint64_t t0 = spu2_time();

    ico_audio_host_vsync(HZ);
    return spu2_time() - t0;
}

/* a device's take of one period through the callback; the SPU2's frames
   it rendered */
static uint64_t device_take(void)
{
    const uint64_t t0 = spu2_time();
    const int got = SDL_GetAudioStreamData(stream, take, (int)sizeof(take));

    CHECK(got == (int)sizeof(take), "the take got %d bytes", got);
    return spu2_time() - t0;
}

static void check_closed(const char *what)
{
    CHECK(stream == NULL && pull_render == NULL && !subsystem && !audio_up &&
              !ico_audio_sdl_open_pending() && !sdl_open && !open_pending,
          "%s: still open (stream %p, render %d, subsystem %d, up %d, job %d, sdl_open %d, "
          "pending %d)",
          what, (void *)stream, pull_render != NULL, subsystem, audio_up,
          ico_audio_sdl_open_pending(), sdl_open, open_pending);
}

static void test_opens_at_once(void)
{
    const int calls0 = hook_calls;
    uint64_t n;

    start(1, 0);
    CHECK(hook_calls - calls0 == 1, "the open ran %d times", hook_calls - calls0);
    CHECK(open_mode == ICO_AUDIO_MODE_PULL,
          "the init picked mode %d (\"%s\"): the environment asks for the game's clock", open_mode,
          open_why);
    CHECK(!open_pending && sdl_open && mode == ICO_AUDIO_MODE_PULL,
          "at once: pending %d, open %d, mode %d (\"%s\")", open_pending, sdl_open, mode, mode_why);
    CHECK(stream != NULL && pull_render == device_render && subsystem,
          "at once: the stream or its callback missing");
    CHECK_STR(ico_audio_mode_text(), "audio on the device's clock");
    n = vsync();
    CHECK(n == 0, "the vsync rendered %llu frames on the device's clock", (unsigned long long)n);
    CHECK(pull_seen_vsync == vsync_index - 1, "the watchdog's vsync %u at vsync %u",
          pull_seen_vsync, vsync_index - 1);
    n = device_take();
    CHECK(n >= PULL_MIN_FRAMES, "the device's take rendered %llu frames", (unsigned long long)n);
    ico_audio_host_shutdown();
    check_closed("at once");
}

static void test_opens_late(void)
{
    unsigned int v;
    uint64_t n;
    int i;

    start(0, 0);
    CHECK(open_pending && !sdl_open && mode == ICO_AUDIO_MODE_SYNC &&
              strcmp(mode_why, "the device is opening") == 0,
          "late: pending %d, open %d, mode %d (\"%s\")", open_pending, sdl_open, mode, mode_why);
    CHECK(stream == NULL && pull_render == NULL && !subsystem,
          "late: a stream or a callback before the adoption");
    CHECK(ico_audio_sdl_open_pending(), "late: no open under way");
    CHECK_STR(ico_audio_mode_text(), "audio on the game's clock (the device is opening)");
    CHECK(ico_audio_sdl_reopen("") == -1, "the move was not refused while opening");
    {
        char names[2][ICO_AUDIO_DEVICE_NAME_MAX];

        CHECK(ico_audio_sdl_devices(names, 2) == 0, "devices listed while opening");
    }
    for (i = 0; i < 5; i++) {
        const int want = ico_audio_host_frames(HZ, vsync_index);

        n = vsync();
        CHECK(n == (uint64_t)want, "vsync %d while opening rendered %llu frames, want %d", i,
              (unsigned long long)n, want);
        CHECK(open_pending && mode == ICO_AUDIO_MODE_SYNC, "vsync %d: adopted early", i);
    }
    open_gate();
    CHECK(ico_audio_sdl_open_wait_ms(5000), "the fake open did not return");
    CHECK(open_pending && stream == NULL, "adopted before a vsync");
    v = vsync_index;
    n = vsync();
    CHECK(!open_pending && sdl_open && mode == ICO_AUDIO_MODE_PULL,
          "after the adoption: pending %d, open %d, mode %d (\"%s\")", open_pending, sdl_open, mode,
          mode_why);
    CHECK(pull_seen_vsync == v, "the watchdog's vsync %u, want %u", pull_seen_vsync, v);
    CHECK(n == 0, "the adopting vsync rendered %llu frames", (unsigned long long)n);
    CHECK(stream != NULL && pull_render == device_render, "no callback after the adoption");
    n = device_take();
    CHECK(n >= PULL_MIN_FRAMES, "the take after the adoption rendered %llu frames",
          (unsigned long long)n);
    ico_audio_host_shutdown();
    check_closed("late");
}

static void test_fails(void)
{
    uint64_t n;

    start(1, 1);
    CHECK(!open_pending && !sdl_open && mode == ICO_AUDIO_MODE_SYNC &&
              strcmp(mode_why, "no device") == 0,
          "fails: pending %d, open %d, mode %d (\"%s\")", open_pending, sdl_open, mode, mode_why);
    CHECK_STR(ico_audio_mode_text(), "audio on the game's clock (no device)");
    check_closed("fails");
    n = vsync();
    CHECK(n > 0, "no render on the game's clock after a failed open");
    ico_audio_host_shutdown();

    /* failing after the wait: the game's clock all along */
    start(0, 1);
    CHECK(open_pending && mode == ICO_AUDIO_MODE_SYNC, "fails late: not pending");
    vsync();
    open_gate();
    CHECK(ico_audio_sdl_open_wait_ms(5000), "the failing open did not return");
    n = vsync();
    CHECK(!open_pending && !sdl_open && mode == ICO_AUDIO_MODE_SYNC &&
              strcmp(mode_why, "no device") == 0 && n > 0,
          "fails late: pending %d, open %d, mode %d (\"%s\"), %llu frames", open_pending, sdl_open,
          mode, mode_why, (unsigned long long)n);
    check_closed("fails late");
    ico_audio_host_shutdown();
}

static void test_never_opens(void)
{
    const int out0 = __atomic_load_n(&open_threads_out, __ATOMIC_ACQUIRE);
    uint64_t rendered = 0, want = 0;
    Uint64 t0, ms;
    int i;

    start(0, 0);
    for (i = 0; i < 20 * HZ + 1; i++) {
        want += (uint64_t)ico_audio_host_frames(HZ, vsync_index);
        rendered += vsync();
    }
    CHECK(open_lines == 2, "%u still-waiting lines in 20 s of vsyncs, want 2", open_lines);
    CHECK(rendered == want, "%llu frames rendered while waiting, want %llu",
          (unsigned long long)rendered, (unsigned long long)want);
    CHECK(open_pending && mode == ICO_AUDIO_MODE_SYNC && stream == NULL, "never: adopted");
    t0 = SDL_GetTicks();
    ico_audio_host_shutdown();
    ms = SDL_GetTicks() - t0;
    CHECK(ms + 10 >= OPEN_CLOSE_WAIT_MS && ms < OPEN_CLOSE_WAIT_MS + 2000,
          "the shutdown took %llu ms (bound %d ms)", (unsigned long long)ms, OPEN_CLOSE_WAIT_MS);
    check_closed("never");
    /* the thread left behind closes what it opens and frees its job */
    open_gate();
    for (i = 0; i < 5000 && __atomic_load_n(&open_threads_out, __ATOMIC_ACQUIRE) == out0; i++) {
        SDL_Delay(1);
    }
    CHECK(__atomic_load_n(&open_threads_out, __ATOMIC_ACQUIRE) == out0 + 1,
          "the thread left behind did not return");
}

int main(void)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(0)) {
        fprintf(stderr, "audio_open_test: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    open_hook = fake_open;
    ico_audio_set_volume(1.0);

    test_opens_at_once();
    test_opens_late();
    test_fails();
    test_never_opens();

    SDL_Quit();
    return ico_check_summary("audio_open_test");
}
