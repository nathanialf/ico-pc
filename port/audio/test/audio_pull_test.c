/*
 * port/audio/test/audio_pull_test.c
 *
 * out_sdl.c's pull path (the output on the device's clock) on a real SDL
 * stream without a device, over the real SPU2.  SDL runs a stream's
 * get-callback from SDL_GetAudioStreamData, so the "device" is a thread
 * that takes 480 frames at a time; the audio lock is out_sdl.c's own SDL
 * mutex (lock_create), as ico_audio_sdl_open installs it.
 *
 *   - bit for bit: a scene of timed register writes rendered on one thread,
 *     then the same writes pushed by a writer thread under the audio lock
 *     while a puller thread renders through pull_cb, the puller fenced
 *     never to render past a write not pushed yet (the writer moves the
 *     fence under the lock): the same frames;
 *   - free-running: 10 s of audio pulled while the writer pushes the
 *     scene's writes stamped with spu2_time() under the lock, as the
 *     driver's serve does: every pull full, the stamps never going back,
 *     the frames rendered and the silence equal to what was taken plus what
 *     is left queued, no lock miss and the lead left queued (the lock's
 *     wait bound raised to 500 ms here, so a writer the system preempts
 *     while it holds the lock delays a pull instead of making a miss);
 *   - the ramp: with the lock held by another thread, the first pull plays
 *     the lead and the second a 96-frame linear ramp from the last frame to
 *     0, then silence; two misses, the silence counted; once the lock is
 *     free the next sound ramps in over 96 frames; the lead in
 *     ico_audio_sdl_queued_ms and a cushion of 0;
 *   - the 10 s line's numbers (ico_audio_sdl_summary_text) with the
 *     driver's ticks and longest gap, and the choice of the mode
 *     (ico_audio_pick_mode: the dump, the fixed clock, [dev] audio_sync,
 *     audio=0 and the headless build on the game's clock);
 *   - the bound on the SPU2's lead over the game: 2 s of pulls with the
 *     game's frames frozen render exactly 120 ms (LEAD_MAX_FRAMES), then a
 *     96-frame ramp from the last frame to 0 and silence, all of it counted
 *     as held back (the 10 s line says so); the game stepping 1000 frames
 *     leaves it held (99 ms ahead, over the 80 ms it resumes at) and turns
 *     the trim on (the stream at ICO_AUDIO_TRIM slower); 1000 more resume
 *     the sound, ramped in; the game a second on, level again, turns the
 *     trim off.  Elsewhere every pull steps the game by the frames it takes
 *     (pull), so the bound never holds there.
 *
 * Every check is a count, a fence or a sample, never a wall-clock time, so
 * scheduler delays (a loaded machine, ctest -j) change how long the test
 * takes, not what it finds; the one wall-clock bound, the lock's wait in
 * the free-running part, is 500 ms.  A failing check prints its values;
 * each part prints a line of its counts.
 */
#include "../out_sdl.c"

#include "../../test/ico_check.h"
#include "../adpcm.h"
#include "../spu2.h"

#define PULL_FRAMES 480
#define SCENE_FRAMES 96000 /* 2 s */
#define SCENE_EVENTS 64
/* the most one pull renders here: the take and the lead (pull_cb) */
#define PULL_BOUND 2048
/* how far ahead of the render the writer pushes */
#define WRITER_AHEAD 8192

typedef struct Ev {
    uint64_t time;
    int core;
    unsigned reg;
    uint16_t value;
} Ev;

static Ev scene[SCENE_EVENTS];
static int16_t ref[SCENE_FRAMES * CHANNELS];
static int16_t cap[SCENE_FRAMES * CHANNELS];
static int16_t dev[PULL_FRAMES * 4 * CHANNELS];

/* what the render captures (capture_render, under the lock) */
static int16_t *capture;
static int capture_at;

/* the writer's fence, under the lock: no render past it */
static uint64_t fence;

static void capture_render(int16_t *frames, int count)
{
    spu2_render(frames, count);
    if (capture != NULL && capture_at < SCENE_FRAMES) {
        const int n = count < SCENE_FRAMES - capture_at ? count : SCENE_FRAMES - capture_at;

        memcpy(capture + capture_at * CHANNELS, frames, (size_t)n * BYTES_PER_FRAME);
        capture_at += n;
    }
}

static uint64_t time_locked(void)
{
    uint64_t t;

    ico_audio_lock();
    t = spu2_time();
    ico_audio_unlock();
    return t;
}

/* a device's take, the game stepping as many frames first */
static int pull(int16_t *out)
{
    ico_audio_sdl_game_step(PULL_FRAMES);
    return SDL_GetAudioStreamData(stream, out, PULL_FRAMES * BYTES_PER_FRAME);
}

/* a device's take with the game's frames standing still */
static int pull_frozen(int16_t *out)
{
    return SDL_GetAudioStreamData(stream, out, PULL_FRAMES * BYTES_PER_FRAME);
}

/* --- the scene ------------------------------------------------------------------ */

static void W(int core, unsigned reg, uint16_t v)
{
    spu2_write_reg(core, reg, v, spu2_time());
}

static void W2(int core, unsigned reg, uint32_t hw)
{
    W(core, reg, (uint16_t)(hw >> 16));
    W(core, reg + 2, (uint16_t)(hw & 0xFFFF));
}

/* core 1's voices dry to the output, core 0 through core 1's external
   input, master volumes full, no reverb */
static void basic_mix(void)
{
    W(0, SPU2_R_MMIX, SPU2_MMIX_VOICE_DRY_L | SPU2_MMIX_VOICE_DRY_R);
    W(1, SPU2_R_MMIX,
      SPU2_MMIX_VOICE_DRY_L | SPU2_MMIX_VOICE_DRY_R | SPU2_MMIX_SIN_DRY_L | SPU2_MMIX_SIN_DRY_R);
    W(0, SPU2_R_MVOLL, 0x3FFF);
    W(0, SPU2_R_MVOLR, 0x3FFF);
    W(1, SPU2_R_MVOLL, 0x3FFF);
    W(1, SPU2_R_MVOLR, 0x3FFF);
    W(1, SPU2_R_AVOLL, 0x7FFF);
    W(1, SPU2_R_AVOLR, 0x7FFF);
}

/* voice v of core c: sample at `ssa`, an envelope that attacks at once and
   holds, both volumes `vol` (the voices' dry masks are set by the caller) */
static void voice_setup(int c, int v, uint32_t ssa, uint16_t pitch, uint16_t vol)
{
    W2(c, SPU2_R_VA(v) + SPU2_VA_SSA, ssa);
    W(c, SPU2_R_VP(v) + SPU2_VP_PITCH, pitch);
    W(c, SPU2_R_VP(v) + SPU2_VP_ADSR1, 0x000F);
    W(c, SPU2_R_VP(v) + SPU2_VP_ADSR2, 0x1FC0);
    W(c, SPU2_R_VP(v) + SPU2_VP_VOLL, vol);
    W(c, SPU2_R_VP(v) + SPU2_VP_VOLR, vol);
}

/* sound RAM: a block of constant 28672 looping on itself at byte 0x10000,
   and eight blocks of varied nibbles looping over themselves at 0x10100 */
static void scene_ram(void)
{
    uint8_t b[16 * 8];
    uint32_t r = 99;
    int i;

    memset(b, 0x77, 16);
    b[0] = 0x00;
    b[1] = ADPCM_FLAG_LOOP_START | ADPCM_FLAG_LOOP_REPEAT | ADPCM_FLAG_LOOP_END;
    spu2_dma_write(0x10000, b, 16);
    for (i = 0; i < (int)sizeof(b); i++) {
        r = r * 1103515245u + 12345u;
        b[i] = (uint8_t)(r >> 16);
    }
    for (i = 0; i < 8; i++) {
        b[16 * i] = (uint8_t)((i % 5) << 4 | 2); /* filter, shift */
        b[16 * i + 1] = (uint8_t)(i == 0   ? ADPCM_FLAG_LOOP_START
                                  : i == 7 ? ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT
                                           : 0);
    }
    spu2_dma_write(0x10100, b, sizeof(b));
}

/* the SPU2 powered on, the RAM and the mix, eight voices on core 1 and four
   on core 0 set up (not keyed), at time 0 */
static void scene_setup(void)
{
    int v;

    spu2_reset();
    scene_ram();
    basic_mix();
    for (v = 0; v < 8; v++) {
        voice_setup(1, v, (v & 1) ? 0x8000 : 0x8080, (uint16_t)(0x0800 + v * 0x200),
                    (uint16_t)(0x0800 + v * 0x100));
    }
    for (v = 0; v < 4; v++) {
        voice_setup(0, v, 0x8080, (uint16_t)(0x1000 + v * 0x300), 0x1800);
    }
    W(1, SPU2_R_VMIXL, 0x00FF);
    W(1, SPU2_R_VMIXR, 0x00FF);
    W(0, SPU2_R_VMIXL, 0x000F);
    W(0, SPU2_R_VMIXR, 0x000F);
}

/* SCENE_EVENTS writes from t0, one every `spacing` frames with a jitter
   under half of it (so in time order, no two at once): key-ons, pitches,
   volumes, key-offs and release rates on the twelve voices */
static void scene_events(uint64_t t0, uint64_t spacing)
{
    uint32_t r = 12345;
    int i;

    for (i = 0; i < SCENE_EVENTS; i++) {
        Ev *e = &scene[i];
        int core;
        int v;

        r = r * 1103515245u + 12345u;
        core = (int)(r >> 20) & 1;
        v = (int)(r >> 8) % (core ? 8 : 4);
        e->time = t0 + (uint64_t)i * spacing + (r >> 16) % (spacing / 2);
        e->core = core;
        switch (i % 5) {
        case 0:
            e->reg = SPU2_R_KON;
            e->value = (uint16_t)(1u << v);
            break;
        case 1:
            e->reg = SPU2_R_VP(v) + SPU2_VP_PITCH;
            e->value = (uint16_t)(0x0400 + (r & 0x1FFF));
            break;
        case 2:
            e->reg = SPU2_R_VP(v) + SPU2_VP_VOLL;
            e->value = (uint16_t)(r & 0x3FFF);
            break;
        case 3:
            e->reg = SPU2_R_KOFF;
            e->value = (uint16_t)(1u << v);
            break;
        default:
            e->reg = SPU2_R_VP(v) + SPU2_VP_ADSR2;
            e->value = (uint16_t)(0x1FC0 | (r & 0x1F));
            break;
        }
    }
}

/* the stream emptied and the pull path's state fresh, as an open */
static void restart(void)
{
    SDL_ClearAudioStream(stream);
    pull_setup(PULL_FRAMES);
}

/* --- bit for bit ------------------------------------------------------------------- */

static int writer_fenced(void *u)
{
    int i;

    (void)u;
    for (i = 0; i < SCENE_EVENTS; i++) {
        const Ev *e = &scene[i];

        while (e->time > time_locked() + WRITER_AHEAD) {
            SDL_DelayPrecise(20000);
        }
        ico_audio_lock();
        spu2_write_reg(e->core, e->reg, e->value, e->time);
        fence = i + 1 < SCENE_EVENTS ? scene[i + 1].time : UINT64_MAX;
        ico_audio_unlock();
    }
    return 0;
}

static int puller_fenced(void *u)
{
    int16_t out[PULL_FRAMES * CHANNELS];
    int *short_pulls = u;

    for (;;) {
        uint64_t t, f;

        ico_audio_lock();
        t = spu2_time();
        f = fence;
        ico_audio_unlock();
        if (t >= SCENE_FRAMES) {
            return 0;
        }
        if (f != UINT64_MAX && t + PULL_BOUND > f) {
            SDL_DelayPrecise(20000);
            continue;
        }
        if (pull(out) != PULL_FRAMES * BYTES_PER_FRAME) {
            (*short_pulls)++;
        }
    }
}

static void test_bit_for_bit(void)
{
    SDL_Thread *w;
    SDL_Thread *p;
    int short_pulls = 0;
    int i;

    scene_events(600, 1400);
    /* on one thread: every write queued at its time, then the render */
    scene_setup();
    for (i = 0; i < SCENE_EVENTS; i++) {
        spu2_write_reg(scene[i].core, scene[i].reg, scene[i].value, scene[i].time);
    }
    for (i = 0; i < SCENE_FRAMES; i += 1000) {
        spu2_render(ref + i * CHANNELS, 1000);
    }
    {
        int loud = 0;

        for (i = 0; i < SCENE_FRAMES * CHANNELS; i++) {
            loud |= ref[i] != 0;
        }
        CHECK(loud, "the scene sounds");
    }

    /* two threads: the writer pushes under the lock, the puller renders */
    scene_setup();
    restart();
    fence = scene[0].time;
    capture = cap;
    capture_at = 0;
    w = SDL_CreateThread(writer_fenced, "writer", NULL);
    p = SDL_CreateThread(puller_fenced, "puller", &short_pulls);
    CHECK(w != NULL && p != NULL, "threads: %s", SDL_GetError());
    SDL_WaitThread(w, NULL);
    SDL_WaitThread(p, NULL);
    capture = NULL;
    CHECK(short_pulls == 0, "%d pulls short", short_pulls);
    CHECK(capture_at == SCENE_FRAMES, "captured %d frames", capture_at);
    for (i = 0; i < SCENE_FRAMES * CHANNELS && cap[i] == ref[i]; i++) {}
    CHECK(i == SCENE_FRAMES * CHANNELS, "frame %d differs: %d, want %d", i / CHANNELS,
          i < SCENE_FRAMES * CHANNELS ? cap[i] : 0, i < SCENE_FRAMES * CHANNELS ? ref[i] : 0);
    printf("audio_pull_test: bit for bit: %d of %d frames equal; %llu misses on the way\n",
           i / CHANNELS, SCENE_FRAMES, (unsigned long long)P_LOAD(P.misses));
}

/* --- free-running ----------------------------------------------------------------- */

static int free_done;      /* the puller finished (atomic) */
static int free_backwards; /* a stamp before the last one */
static int free_pushed;

static int writer_free(void *u)
{
    uint64_t last = 0;
    int i;

    (void)u;
    for (i = 0; i < SCENE_EVENTS; i++) {
        const Ev *e = &scene[i];

        while (time_locked() < e->time && !__atomic_load_n(&free_done, __ATOMIC_ACQUIRE)) {
            SDL_DelayPrecise(500000);
        }
        {
            uint64_t t;

            ico_audio_lock();
            t = spu2_time(); /* the serve's stamp */
            spu2_write_reg(e->core, e->reg, e->value, t);
            ico_audio_unlock();
            if (t < last) {
                free_backwards++;
            }
            last = t;
            free_pushed++;
        }
    }
    return 0;
}

static int puller_free(void *u)
{
    int16_t out[PULL_FRAMES * CHANNELS];
    int *short_pulls = u;
    int i;

    for (i = 0; i < 1000; i++) {
        if (pull(out) != PULL_FRAMES * BYTES_PER_FRAME) {
            (*short_pulls)++;
        }
        SDL_DelayPrecise(200000);
    }
    __atomic_store_n(&free_done, 1, __ATOMIC_RELEASE);
    return 0;
}

static void test_free_running(void)
{
    SDL_Thread *w;
    SDL_Thread *p;
    int short_pulls = 0;
    uint64_t frames0, silence0, misses0, callbacks0;
    int queued;

    scene_events(600, 7000); /* the last write near 9.4 s */
    scene_setup();
    restart();
    /* the lock's wait bound (a quarter period) at 500 ms: this part checks
       that two threads keep the SPU2 whole and the counts exact, not the
       bound (the ramp part makes its misses by holding the lock); with the
       real 2.5 ms a writer preempted inside its lock on a loaded machine
       could make a miss */
    period_ns = 2000000000u;
    frames0 = P_LOAD(P.frames);
    silence0 = P_LOAD(P.silence);
    misses0 = P_LOAD(P.misses);
    callbacks0 = P_LOAD(P.callbacks);
    w = SDL_CreateThread(writer_free, "writer", NULL);
    p = SDL_CreateThread(puller_free, "puller", &short_pulls);
    CHECK(w != NULL && p != NULL, "threads: %s", SDL_GetError());
    SDL_WaitThread(p, NULL);
    SDL_WaitThread(w, NULL);
    queued = SDL_GetAudioStreamQueued(stream) / BYTES_PER_FRAME;
    CHECK(short_pulls == 0, "%d pulls short", short_pulls);
    CHECK(free_pushed == SCENE_EVENTS, "%d writes pushed", free_pushed);
    CHECK(free_backwards == 0, "%d stamps went back", free_backwards);
    CHECK(P_LOAD(P.callbacks) - callbacks0 == 1000, "%llu callbacks, want 1000",
          (unsigned long long)(P_LOAD(P.callbacks) - callbacks0));
    /* what was put (rendered or silence) is what was taken plus the rest */
    CHECK(P_LOAD(P.frames) - frames0 + (P_LOAD(P.silence) - silence0) ==
              (uint64_t)1000 * PULL_FRAMES + (uint64_t)queued,
          "rendered %llu, silence %llu, taken %d, queued %d",
          (unsigned long long)(P_LOAD(P.frames) - frames0),
          (unsigned long long)(P_LOAD(P.silence) - silence0), 1000 * PULL_FRAMES, queued);
    CHECK(queued == lead_frames, "the lead left queued: %d frames, want %d", queued, lead_frames);
    CHECK(time_locked() == P_LOAD(P.frames) - frames0,
          "the SPU2's time %llu is not the %llu frames rendered", (unsigned long long)time_locked(),
          (unsigned long long)(P_LOAD(P.frames) - frames0));
    CHECK(P_LOAD(P.misses) == misses0 && P_LOAD(P.silence) == silence0,
          "%llu misses, %llu frames of silence, want none (the longest wait %.2f ms)",
          (unsigned long long)(P_LOAD(P.misses) - misses0),
          (unsigned long long)(P_LOAD(P.silence) - silence0), P_LOAD(P.wait_run_ns) / 1e6);
    printf("audio_pull_test: free-running: %llu callbacks, %llu frames rendered, %d writes, "
           "%llu misses, the longest lock wait since the start %.2f ms\n",
           (unsigned long long)(P_LOAD(P.callbacks) - callbacks0),
           (unsigned long long)(P_LOAD(P.frames) - frames0), free_pushed,
           (unsigned long long)(P_LOAD(P.misses) - misses0), P_LOAD(P.wait_run_ns) / 1e6);
}

/* --- the ramp ------------------------------------------------------------------------ */

static int puller_two(void *u)
{
    (void)u;
    pull(dev);
    pull(dev + PULL_FRAMES * CHANNELS);
    return 0;
}

static void test_ramp(void)
{
    SDL_Thread *p;
    uint64_t misses0, silence0;
    int16_t dc[CHANNELS];
    int i;
    int ok;

    /* one voice of constant level */
    scene_setup();
    W2(1, SPU2_R_VA(0) + SPU2_VA_SSA, 0x8000);
    W(1, SPU2_R_VP(0) + SPU2_VP_PITCH, 0x1000);
    W(1, SPU2_R_VP(0) + SPU2_VP_VOLL, 0x3FFF);
    W(1, SPU2_R_VP(0) + SPU2_VP_VOLR, 0x3FFF);
    W(1, SPU2_R_VMIXL, 0x0001);
    W(1, SPU2_R_VMIXR, 0x0001);
    W(0, SPU2_R_VMIXL, 0);
    W(0, SPU2_R_VMIXR, 0);
    W(1, SPU2_R_KON, 1);
    restart();
    for (i = 0; i < 20; i++) {
        pull(dev);
    }
    dc[0] = dev[2 * (PULL_FRAMES - 1)];
    dc[1] = dev[2 * (PULL_FRAMES - 1) + 1];
    CHECK(dc[0] > 20000 && dc[1] > 20000, "a steady level: %d %d", dc[0], dc[1]);
    for (ok = 1, i = 0; i < PULL_FRAMES && ok; i++) {
        ok = dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "the level is not constant: frame %d is %d %d, want %d %d", i - 1, dev[2 * (i - 1)],
          dev[2 * (i - 1) + 1], dc[0], dc[1]);
    CHECK(ico_audio_sdl_queued_ms() == PULL_FRAMES * 1000 / RATE, "the lead queued: %d ms",
          ico_audio_sdl_queued_ms());
    CHECK(ico_audio_sdl_cushion_ms() == 0, "a cushion of %d ms, want 0",
          ico_audio_sdl_cushion_ms());

    /* the lock held by this thread while another pulls twice */
    misses0 = P_LOAD(P.misses);
    silence0 = P_LOAD(P.silence);
    memset(dev, 0x55, sizeof(dev));
    ico_audio_lock();
    p = SDL_CreateThread(puller_two, "puller", NULL);
    CHECK(p != NULL, "thread: %s", SDL_GetError());
    SDL_WaitThread(p, NULL);
    ico_audio_unlock();
    CHECK(P_LOAD(P.misses) - misses0 == 2, "%llu misses, want 2",
          (unsigned long long)(P_LOAD(P.misses) - misses0));
    CHECK(P_LOAD(P.silence) - silence0 == PULL_FRAMES, "%llu frames of silence, want %d",
          (unsigned long long)(P_LOAD(P.silence) - silence0), PULL_FRAMES);
    for (ok = 1, i = 0; i < PULL_FRAMES && ok; i++) {
        ok = dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "the first pull does not play the lead: frame %d is %d %d, want %d %d", i - 1,
          dev[2 * (i - 1)], dev[2 * (i - 1) + 1], dc[0], dc[1]);
    for (ok = 1, i = 0; i < RAMP_FRAMES; i++) {
        const int16_t *f = dev + (PULL_FRAMES + i) * CHANNELS;

        if (f[0] != dc[0] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES ||
            f[1] != dc[1] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES) {
            ok = 0;
            CHECK(0, "ramp frame %d: %d %d", i, f[0], f[1]);
            break;
        }
    }
    CHECK(ok && dev[(PULL_FRAMES + RAMP_FRAMES - 1) * CHANNELS] == 0,
          "the second does not ramp from the last frame to 0 (its frame %d is %d)", RAMP_FRAMES - 1,
          dev[(PULL_FRAMES + RAMP_FRAMES - 1) * CHANNELS]);
    for (ok = 1, i = PULL_FRAMES + RAMP_FRAMES; i < 2 * PULL_FRAMES && ok; i++) {
        ok = dev[2 * i] == 0 && dev[2 * i + 1] == 0;
    }
    CHECK(ok, "no silence after the ramp: frame %d is %d %d", i - 1 - PULL_FRAMES, dev[2 * (i - 1)],
          dev[2 * (i - 1) + 1]);

    /* the lock free again: the sound ramps in, the level where it was */
    pull(dev);
    for (ok = 1, i = 0; i < RAMP_FRAMES && ok; i++) {
        const int raw0 = dc[0] < 0 ? -dc[0] : dc[0];

        ok = (dev[2 * i] < 0 ? -dev[2 * i] : dev[2 * i]) <= raw0 * i / RAMP_FRAMES + 1 &&
             dev[2 * i] == dc[0] * i / RAMP_FRAMES && dev[2 * i + 1] == dc[1] * i / RAMP_FRAMES;
    }
    CHECK(ok, "the ramp in: frame %d is %d %d, want %d %d", i - 1, dev[2 * (i - 1)],
          dev[2 * (i - 1) + 1], dc[0] * (i - 1) / RAMP_FRAMES, dc[1] * (i - 1) / RAMP_FRAMES);
    for (ok = 1, i = RAMP_FRAMES; i < PULL_FRAMES && ok; i++) {
        ok = dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "after the ramp in: frame %d is %d %d, want %d %d", i - 1, dev[2 * (i - 1)],
          dev[2 * (i - 1) + 1], dc[0], dc[1]);
    printf("audio_pull_test: ramp: level %d %d, %llu misses, %llu frames of silence while the "
           "lock was held\n",
           dc[0], dc[1], (unsigned long long)(P_LOAD(P.misses) - misses0),
           (unsigned long long)(P_LOAD(P.silence) - silence0));
}

/* one row of the mode table (ico_audio_pick_mode), its arguments in the
   message on a failure */
static void mode_case(int window_build, const char *enable, int dumping, int repeatable,
                      const char *sync, int want, const char *want_why)
{
    const char *why = "(unset)";
    const int got = ico_audio_pick_mode(window_build, enable, dumping, repeatable, sync, &why);

    CHECK(got == want && strcmp(why, want_why) == 0,
          "ico_audio_pick_mode(%d, %s, %d, %d, %s): mode %d (\"%s\"), want %d (\"%s\")",
          window_build, enable != NULL ? enable : "NULL", dumping, repeatable,
          sync != NULL ? sync : "NULL", got, why, want, want_why);
}

/* --- the 10 s line and the mode ---------------------------------------------------------- */

static void test_summary_and_mode(void)
{
    char line[640];
    unsigned long long callbacks, frames, gaveup, late, silence_ms, held_ms, ticks, gap, over,
        bound;
    int ahead_ms = -1;
    double waited = -1.0, slowed = -1.0;
    uint64_t base;
    int i;

    /* the window so far, discarded */
    pull(dev);
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1, "no 10 s line on the pull path");
    /* three pulls with the lead queued (480 frames each), five driver
       ticks with one gap of 250 ms */
    for (i = 0; i < 3; i++) {
        pull(dev);
    }
    base = 1000000000u;
    ico_audio_lock();
    ico_audio_tick_note(base);
    ico_audio_tick_note(base + 16700000u);
    ico_audio_tick_note(base + 33400000u);
    ico_audio_tick_note(base + 283400000u);
    ico_audio_tick_note(base + 300100000u);
    ico_audio_unlock();
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1, "no 10 s line on the pull path");
    printf("audio_pull_test: %s\n", line);
    CHECK(sscanf(line,
                 "audio: %llu callbacks, %llu frames; waited %lf ms at most for the driver (%llu "
                 "gave up), %llu callbacks late, %llu ms of silence (%llu ms held back behind the "
                 "game); ahead of the game by %d ms at most, slowed for %lf s; the driver ticked "
                 "%llu times, longest gap %llu ms (%llu over %llu ms)",
                 &callbacks, &frames, &waited, &gaveup, &late, &silence_ms, &held_ms, &ahead_ms,
                 &slowed, &ticks, &gap, &over, &bound) == 13,
          "the line's shape: %s", line);
    CHECK(callbacks == 3 && frames == 3 * PULL_FRAMES, "%llu callbacks, %llu frames", callbacks,
          frames);
    /* the game steps what each pull takes: the take and the lead ahead, no
       hold, no trim */
    CHECK(held_ms == 0 && ahead_ms == 2 * PULL_FRAMES * 1000 / RATE && slowed == 0.0,
          "%llu ms held back, %d ms ahead, slowed for %.1f s: want 0, %d, 0.0", held_ms, ahead_ms,
          slowed, 2 * PULL_FRAMES * 1000 / RATE);
    /* uncontended: the first try takes the lock, so the wait is 0 */
    CHECK(gaveup == 0 && silence_ms == 0 && waited == 0.0,
          "%llu gave up, %llu ms of silence, waited %.2f ms: want 0, 0, 0.00", gaveup, silence_ms,
          waited);
    CHECK(gap == 250 && over == 1 && bound == 200, "gap %llu, %llu over %llu", gap, over, bound);
    /* the ticks since the start: these five and none before */
    CHECK(ticks == 5, "%llu ticks", ticks);
    /* the next window's longest gap starts again */
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1, "no 10 s line on the pull path");
    CHECK(strstr(line, "longest gap 0 ms (0 over 200 ms)") != NULL, "%s", line);

    /* the mode */
    mode_case(1, NULL, 0, 0, NULL, ICO_AUDIO_MODE_PULL, "");
    mode_case(1, NULL, 0, 0, "0", ICO_AUDIO_MODE_PULL, "");
    mode_case(1, NULL, 1, 0, NULL, ICO_AUDIO_MODE_SYNC, "audio_dump");
    mode_case(1, NULL, 0, 0, "1", ICO_AUDIO_MODE_SYNC, "[dev] audio_sync");
    mode_case(1, "0", 0, 0, NULL, ICO_AUDIO_MODE_SYNC, "audio=0");
    mode_case(1, "1", 0, 0, NULL, ICO_AUDIO_MODE_PULL, "");
    mode_case(1, NULL, 0, 1, NULL, ICO_AUDIO_MODE_SYNC, "fixed clock");
    mode_case(0, NULL, 0, 0, NULL, ICO_AUDIO_MODE_SYNC, "headless build");
    CHECK(ico_audio_pick_mode(1, NULL, 0, 0, NULL, NULL) == ICO_AUDIO_MODE_PULL,
          "without a reason pointer: not the pull path");
}

/* --- the bound on the SPU2's lead ------------------------------------------------------ */

static int16_t held_out[200 * PULL_FRAMES * CHANNELS];

static int frame_is(const int16_t *f, int l, int r)
{
    return f[0] == l && f[1] == r;
}

static void test_hold(void)
{
    char line[640];
    uint64_t frames0, silence0, held0, f0;
    int16_t dc[CHANNELS];
    float ratio;
    int i;
    int ok;

    /* one voice of constant level (as the ramp part), steady */
    scene_setup();
    W2(1, SPU2_R_VA(0) + SPU2_VA_SSA, 0x8000);
    W(1, SPU2_R_VP(0) + SPU2_VP_PITCH, 0x1000);
    W(1, SPU2_R_VP(0) + SPU2_VP_VOLL, 0x3FFF);
    W(1, SPU2_R_VP(0) + SPU2_VP_VOLR, 0x3FFF);
    W(1, SPU2_R_VMIXL, 0x0001);
    W(1, SPU2_R_VMIXR, 0x0001);
    W(0, SPU2_R_VMIXL, 0);
    W(0, SPU2_R_VMIXR, 0);
    W(1, SPU2_R_KON, 1);
    restart();
    for (i = 0; i < 20; i++) {
        pull(dev);
    }
    dc[0] = dev[2 * (PULL_FRAMES - 1)];
    dc[1] = dev[2 * (PULL_FRAMES - 1) + 1];
    CHECK(dc[0] > 20000 && dc[1] > 20000, "a steady level: %d %d", dc[0], dc[1]);

    /* the game frozen: 2 s of pulls */
    restart();
    ico_audio_sdl_summary_text(line, sizeof(line)); /* a new window */
    frames0 = P_LOAD(P.frames);
    silence0 = P_LOAD(P.silence);
    held0 = P_LOAD(P.held);
    for (i = 0; i < 200; i++) {
        pull_frozen(held_out + i * PULL_FRAMES * CHANNELS);
    }
    CHECK(P_LOAD(P.frames) - frames0 == LEAD_MAX_FRAMES, "rendered %llu frames, want %d",
          (unsigned long long)(P_LOAD(P.frames) - frames0), LEAD_MAX_FRAMES);
    CHECK(P_LOAD(P.held) - held0 == 200 * PULL_FRAMES - LEAD_MAX_FRAMES &&
              P_LOAD(P.silence) - silence0 == P_LOAD(P.held) - held0,
          "held back %llu frames, silence %llu, want %d of each",
          (unsigned long long)(P_LOAD(P.held) - held0),
          (unsigned long long)(P_LOAD(P.silence) - silence0), 200 * PULL_FRAMES - LEAD_MAX_FRAMES);
    CHECK(SDL_GetAudioStreamQueued(stream) == 0, "%d bytes left queued",
          SDL_GetAudioStreamQueued(stream));
    for (ok = 1, i = RAMP_FRAMES; i < LEAD_MAX_FRAMES && ok; i++) {
        ok = frame_is(held_out + i * CHANNELS, dc[0], dc[1]);
    }
    CHECK(ok, "the sound to the bound: frame %d is %d %d, want %d %d", i - 1, held_out[2 * (i - 1)],
          held_out[2 * (i - 1) + 1], dc[0], dc[1]);
    for (ok = 1, i = 0; i < RAMP_FRAMES && ok; i++) {
        ok = frame_is(held_out + (LEAD_MAX_FRAMES + i) * CHANNELS,
                      dc[0] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES,
                      dc[1] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES);
    }
    CHECK(ok, "the ramp at the bound: its frame %d is %d %d", i - 1,
          held_out[2 * (LEAD_MAX_FRAMES + i - 1)], held_out[2 * (LEAD_MAX_FRAMES + i - 1) + 1]);
    for (ok = 1, i = LEAD_MAX_FRAMES + RAMP_FRAMES; i < 200 * PULL_FRAMES && ok; i++) {
        ok = frame_is(held_out + i * CHANNELS, 0, 0);
    }
    CHECK(ok, "silence after the ramp: frame %d is %d %d", i - 1, held_out[2 * (i - 1)],
          held_out[2 * (i - 1) + 1]);
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1, "no 10 s line");
    printf("audio_pull_test: hold: %s\n", line);
    CHECK(strstr(line, "1880 ms of silence (1880 ms held back behind the game); ahead of the game "
                       "by 120 ms at most, slowed for 0.0 s") != NULL,
          "the line: %s", line);

    /* the game steps 1000 frames: 99 ms ahead, held until 80; the trim on */
    ico_audio_sdl_game_step(1000);
    ratio = SDL_GetAudioStreamFrequencyRatio(stream);
    CHECK(trimming && ratio > 0.99749f && ratio < 0.99751f, "trim %d, the stream at %f", trimming,
          (double)ratio);
    f0 = P_LOAD(P.frames);
    pull_frozen(dev);
    CHECK(P_LOAD(P.frames) == f0, "rendered %llu frames while held",
          (unsigned long long)(P_LOAD(P.frames) - f0));

    /* 1000 more: 78 ms ahead, the sound back, ramped in (resampled by the
       trim, so checked to a margin) */
    ico_audio_sdl_game_step(1000);
    held0 = P_LOAD(P.held);
    pull_frozen(dev);
    pull_frozen(dev + PULL_FRAMES * CHANNELS);
    CHECK(P_LOAD(P.frames) > f0 && P_LOAD(P.held) == held0,
          "%llu frames rendered, %llu held back after the game stepped",
          (unsigned long long)(P_LOAD(P.frames) - f0),
          (unsigned long long)(P_LOAD(P.held) - held0));
    CHECK(dev[0] < dc[0] / 4 && dev[1] < dc[1] / 4, "the sound does not ramp in: %d %d", dev[0],
          dev[1]);
    {
        const int16_t *f = dev + (2 * PULL_FRAMES - 1) * CHANNELS;

        CHECK(f[0] > dc[0] - dc[0] / 50 && f[0] < dc[0] + dc[0] / 50 && f[1] > dc[1] - dc[1] / 50 &&
                  f[1] < dc[1] + dc[1] / 50,
              "the level after the ramp in: %d %d, want about %d %d", f[0], f[1], dc[0], dc[1]);
    }

    /* the game a second on: level at the next callback, then the trim off */
    ico_audio_sdl_game_step(RATE);
    pull_frozen(dev);
    ico_audio_sdl_game_step(0);
    ratio = SDL_GetAudioStreamFrequencyRatio(stream);
    CHECK(!trimming && ratio == 1.0f, "trim %d, the stream at %f", trimming, (double)ratio);
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1, "no 10 s line");
    CHECK(strstr(line, "slowed for 1.0 s") != NULL, "the trim's time: %s", line);
}

int main(void)
{
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, CHANNELS, RATE};

    if (!SDL_Init(0)) {
        fprintf(stderr, "audio_pull_test: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    stream = SDL_CreateAudioStream(&spec, &spec);
    if (stream == NULL || lock_create() != 0) {
        fprintf(stderr, "audio_pull_test: SDL stream or mutex: %s\n", SDL_GetError());
        return 1;
    }
    pull_render = capture_render;
    SDL_SetAudioStreamGetCallback(stream, pull_cb, NULL);
    ico_audio_set_volume(1.0);

    test_bit_for_bit();
    test_free_running();
    test_ramp();
    test_summary_and_mode();
    test_hold();

    SDL_DestroyAudioStream(stream);
    stream = NULL;
    lock_destroy();
    pull_render = NULL;
    CHECK(!ico_audio_sdl_active(), "the output still active after its stream is gone");
    SDL_Quit();
    return ico_check_summary("audio_pull_test");
}
