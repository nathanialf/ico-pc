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
 *     is left queued, no lock miss beyond the writer's own long holds;
 *   - the ramp: with the lock held by another thread, the first pull plays
 *     the lead and the second a 96-frame linear ramp from the last frame to
 *     0, then silence; two misses, the silence counted; once the lock is
 *     free the next sound ramps in over 96 frames; the lead in
 *     ico_audio_sdl_queued_ms and a cushion of 0;
 *   - the 10 s line's numbers (ico_audio_sdl_summary_text) with the
 *     driver's ticks and longest gap, and the choice of the mode
 *     (ico_audio_pick_mode).
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

static int pull(int16_t *out)
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
}

/* --- free-running ----------------------------------------------------------------- */

static int free_done;       /* the puller finished (atomic) */
static int free_backwards;  /* a stamp before the last one */
static int free_long_holds; /* writer holds of the lock over a millisecond */
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
            const Uint64 t0 = SDL_GetTicksNS();
            uint64_t t;

            ico_audio_lock();
            t = spu2_time(); /* the serve's stamp */
            spu2_write_reg(e->core, e->reg, e->value, t);
            ico_audio_unlock();
            if (SDL_GetTicksNS() - t0 > 1000000u) {
                free_long_holds++;
            }
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
    CHECK(P_LOAD(P.callbacks) - callbacks0 == 1000);
    /* what was put (rendered or silence) is what was taken plus the rest */
    CHECK(P_LOAD(P.frames) - frames0 + (P_LOAD(P.silence) - silence0) ==
              (uint64_t)1000 * PULL_FRAMES + (uint64_t)queued,
          "rendered %llu, silence %llu, taken %d, queued %d",
          (unsigned long long)(P_LOAD(P.frames) - frames0),
          (unsigned long long)(P_LOAD(P.silence) - silence0), 1000 * PULL_FRAMES, queued);
    /* the lead left queued, unless the last pull missed the lock */
    CHECK(queued == lead_frames || free_long_holds > 0, "the lead left queued: %d", queued);
    CHECK(time_locked() == P_LOAD(P.frames) - frames0, "the SPU2's time is the frames rendered");
    /* a miss needs the lock still held a quarter period on: only a long
       hold of the writer's (the system preempting it) can make one */
    CHECK(P_LOAD(P.misses) == misses0 || free_long_holds > 0, "%llu misses, %d long holds",
          (unsigned long long)(P_LOAD(P.misses) - misses0), free_long_holds);
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
    for (ok = 1, i = 0; i < PULL_FRAMES; i++) {
        ok &= dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "the level is constant");
    CHECK(ico_audio_sdl_queued_ms() == PULL_FRAMES * 1000 / RATE, "the lead queued: %d ms",
          ico_audio_sdl_queued_ms());
    CHECK(ico_audio_sdl_cushion_ms() == 0);

    /* the lock held by this thread while another pulls twice */
    misses0 = P_LOAD(P.misses);
    silence0 = P_LOAD(P.silence);
    memset(dev, 0x55, sizeof(dev));
    ico_audio_lock();
    p = SDL_CreateThread(puller_two, "puller", NULL);
    CHECK(p != NULL, "thread: %s", SDL_GetError());
    SDL_WaitThread(p, NULL);
    ico_audio_unlock();
    CHECK(P_LOAD(P.misses) - misses0 == 2);
    CHECK(P_LOAD(P.silence) - silence0 == PULL_FRAMES);
    for (ok = 1, i = 0; i < PULL_FRAMES; i++) {
        ok &= dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "the first pull plays the lead");
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
          "the second ramps from the last frame to 0");
    for (ok = 1, i = PULL_FRAMES + RAMP_FRAMES; i < 2 * PULL_FRAMES; i++) {
        ok &= dev[2 * i] == 0 && dev[2 * i + 1] == 0;
    }
    CHECK(ok, "then silence");

    /* the lock free again: the sound ramps in, the level where it was */
    pull(dev);
    for (ok = 1, i = 0; i < RAMP_FRAMES; i++) {
        const int raw0 = dc[0] < 0 ? -dc[0] : dc[0];

        ok &= (dev[2 * i] < 0 ? -dev[2 * i] : dev[2 * i]) <= raw0 * i / RAMP_FRAMES + 1;
        ok &= dev[2 * i] == dc[0] * i / RAMP_FRAMES && dev[2 * i + 1] == dc[1] * i / RAMP_FRAMES;
    }
    CHECK(ok, "the sound ramps in over %d frames", RAMP_FRAMES);
    for (ok = 1, i = RAMP_FRAMES; i < PULL_FRAMES; i++) {
        ok &= dev[2 * i] == dc[0] && dev[2 * i + 1] == dc[1];
    }
    CHECK(ok, "then the level it had");
}

/* --- the 10 s line and the mode ---------------------------------------------------------- */

static void test_summary_and_mode(void)
{
    char line[512];
    unsigned long long callbacks, frames, gaveup, late, silence_ms, ticks, gap, over, bound;
    double waited = -1.0;
    const char *why = NULL;
    uint64_t base;
    int i;

    /* the window so far, discarded */
    pull(dev);
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1);
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
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1);
    printf("audio_pull_test: %s\n", line);
    CHECK(sscanf(line,
                 "audio: %llu callbacks, %llu frames; waited %lf ms at most for the driver (%llu "
                 "gave up), %llu callbacks late, %llu ms of silence; the driver ticked %llu "
                 "times, longest gap %llu ms (%llu over %llu ms)",
                 &callbacks, &frames, &waited, &gaveup, &late, &silence_ms, &ticks, &gap, &over,
                 &bound) == 10,
          "the line's shape: %s", line);
    CHECK(callbacks == 3 && frames == 3 * PULL_FRAMES, "%llu callbacks, %llu frames", callbacks,
          frames);
    CHECK(gaveup == 0 && silence_ms == 0 && waited < 1.0);
    CHECK(gap == 250 && over == 1 && bound == 200, "gap %llu, %llu over %llu", gap, over, bound);
    /* the ticks since the start: these five and none before */
    CHECK(ticks == 5, "%llu ticks", ticks);
    /* the next window's longest gap starts again */
    CHECK(ico_audio_sdl_summary_text(line, sizeof(line)) == 1);
    CHECK(strstr(line, "longest gap 0 ms (0 over 200 ms)") != NULL, "%s", line);

    /* the mode */
    CHECK(ico_audio_pick_mode(1, NULL, 0, NULL, &why) == ICO_AUDIO_MODE_PULL);
    CHECK_STR(why, "");
    CHECK(ico_audio_pick_mode(1, NULL, 0, "0", &why) == ICO_AUDIO_MODE_PULL);
    CHECK(ico_audio_pick_mode(1, NULL, 1, NULL, &why) == ICO_AUDIO_MODE_SYNC);
    CHECK_STR(why, "audio_dump");
    CHECK(ico_audio_pick_mode(1, NULL, 0, "1", &why) == ICO_AUDIO_MODE_SYNC);
    CHECK_STR(why, "[dev] audio_sync");
    CHECK(ico_audio_pick_mode(1, "0", 0, NULL, &why) == ICO_AUDIO_MODE_SYNC);
    CHECK_STR(why, "audio=0");
    CHECK(ico_audio_pick_mode(1, "1", 0, NULL, &why) == ICO_AUDIO_MODE_PULL);
    CHECK(ico_audio_pick_mode(0, NULL, 0, NULL, &why) == ICO_AUDIO_MODE_SYNC);
    CHECK_STR(why, "headless build");
    CHECK(ico_audio_pick_mode(1, NULL, 0, NULL, NULL) == ICO_AUDIO_MODE_PULL);
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

    SDL_DestroyAudioStream(stream);
    stream = NULL;
    lock_destroy();
    pull_render = NULL;
    CHECK(!ico_audio_sdl_active());
    SDL_Quit();
    return ico_check_summary("audio_pull_test");
}
