/*
 * port/audio/out_sdl.c
 *
 * The SPU2's output on an SDL3 audio device (audio_host.h; window build
 * only).  One SDL_AudioStream at the SPU2's own format, 48 kHz stereo S16,
 * so SDL converts only if the device needs it, with a device period of
 * 480 frames asked for (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, 10 ms; the
 * backends take it as a request).  It is fed one of two ways (audio_host.h,
 * the mode; audio_host.c ico_audio_host_init chooses):
 *
 * The pull path (the device's clock).  The stream has a get-callback,
 * pull_cb, which SDL's audio thread runs inside SDL's stream lock when the
 * device wants sound (on every backend a time-critical thread of SDL's, not
 * the backend's real-time callback).  It renders what SDL asks for plus a
 * lead of one device period (lead_frames, at least 480), so after each
 * take the lead stays queued, through the render audio_host.c hands to
 * ico_audio_sdl_open (spu2_render and the mirror swap), under the audio
 * lock (audio_lock.h, an SDL mutex installed here).  The lock is tried for
 * at most a quarter of the device period: when the game's side holds it
 * longer the callback renders nothing and the SPU2's time stands still
 * (nothing is lost); the queued lead plays, and when the lead does not
 * cover the request the shortfall (at least 96 frames) is silence ramped
 * down from the last sample over 96 frames (2 ms), after which the next
 * sound ramps in over 96 frames.  The open, a move to another device and a
 * resume also ramp in and prime nothing.  The SPU2 never runs more than
 * 120 ms (LEAD_MAX_FRAMES) past the game's own frames (the frames of the
 * vsyncs the game stepped, ico_audio_sdl_game_step): the callback counts
 * how far its render is ahead of them (never below 0: a game ahead of the
 * device is level), renders at most up to the bound, and covers the rest
 * with silence ramped like a miss; once held back (latched only by a
 * callback that rendered or had nothing left to render, never by one that
 * missed the lock) it renders nothing until the lead is back to the steady
 * lead it runs at, two device periods (pull_release), so a stall of the
 * game is one gap of sound, as on the game's clock, after which the sound
 * is as far ahead of the picture as before it, and a film's or a stream's
 * read position can lead the game by the bound at most.  While a film's
 * PCM channels are open (ico_audio_sdl_film, from stream.c st_pcm_init and
 * st_pcm_quit under the audio lock) the bound is 60 ms
 * (LEAD_FILM_FRAMES).  Either bound is at least the steady lead and 20 ms
 * (pull_bound), so a device with long periods is not held back at every
 * callback.  A device clock
 * faster than the system's would creep up to the bound; past 64 ms ahead
 * (TRIM_ON_FRAMES) the stream plays ICO_AUDIO_TRIM slower until it is under
 * 32 ms (TRIM_OFF_FRAMES), which also brings a film's sound back to its
 * picture after a stall.  Each block is scaled by the output volume.  The
 * callback counts (callbacks, frames rendered, the longest lock wait, lock
 * misses, late callbacks, silence, the silence of the bound and how far
 * ahead of the game it was) for the 10 s line (ico_audio_sdl_log_summary)
 * and the close; it never logs.  Latency:
 * a driver write lands at the next callback (at most a period), then plays
 * after the lead and the device's own buffer: about 25-30 ms at 10 ms
 * periods, constant.  It reports the lead as the queue and a cushion of 0
 * (ico_audio_sdl_queued_ms, ico_audio_sdl_cushion_ms).  A device that
 * stops asking leaves the SPU2's time standing; audio_host.c's watchdog
 * then falls back to the sync path (ico_audio_sdl_fall_back).
 *
 * The sync path (the game's clock: the WAV dump, [dev] audio_sync, or the
 * fall-back).  The simulation thread pushes one vsync of frames per vsync
 * (SDL_PutAudioStreamData is thread-safe; nothing else is shared with SDL's
 * audio thread).  Latency: the stream keeps a cushion of queued audio, plus
 * the device buffer.  The cushion
 * starts at two vsyncs (two of the pushed blocks: 33 ms at 59.94 Hz, 40 ms
 * at 50) and is what the open and every resume prime with silence.  The
 * window paces the simulation to real time, so production and consumption
 * match on average.  When the queue runs dry (a stall: the simulation fell
 * behind real time by more than the cushion) the cushion doubles, up to
 * 120 ms (CUSHION_MAX_FRAMES), and silence fills the queue to it before the
 * block, ramped down from the last pushed sample over 96 frames while
 * frames are still queued, plain once the queue ran empty; after 20 s
 * of pushed audio without a dry queue
 * (CUSHION_QUIET_FRAMES) it halves again, down to two vsyncs, and the trim
 * below plays the surplus out.  When more than the cushion plus four
 * vsyncs is queued (the simulation ran ahead; never more than 250 ms,
 * QUEUE_CEILING_FRAMES) the block is dropped.  Refills and drops are
 * counted and logged at the close with the cushion.
 *
 * Under the window's vsync lock the display is the simulation's clock, so
 * the simulation runs a little off 59.94 Hz (60.00 on a 60.00 Hz screen);
 * the window measures the rate every 10 s and the stream plays at that
 * ratio on both paths (ico_audio_set_rate_ratio,
 * SDL_SetAudioStreamFrequencyRatio).
 *
 * Volume: each block is scaled by audio_host.h's ico_audio_volume_q8()
 * (0..256 in 1/256 steps) just before it is queued, so a change from the
 * Settings menu is heard within the queued audio (the lead or the cushion,
 * and a vsync).
 *
 * Device ([audio] device): the stream
 * is opened on the playback device with that name, or on SDL's default
 * device (which follows the system's default) when the name is empty or no
 * device has it.  The Settings menu reopens it on another device; when the
 * named device goes away (SDL_EVENT_AUDIO_DEVICE_REMOVED) the stream moves
 * to the default device.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_host.h"
#include "audio_lock.h"

#define CHANNELS 2
#define BYTES_PER_FRAME (CHANNELS * 2)
#define RATE 48000
/* the ramp into and out of inserted silence: 2 ms */
#define RAMP_FRAMES 96
/* the most the pull path renders in one callback: 170 ms */
#define PULL_MAX_FRAMES 8192
/* the pull path's lead: one device period, at least the hint's 480
   frames */
#define PULL_MIN_FRAMES 480
/* how often the callback tries the audio lock while the game holds it */
#define LOCK_POLL_NS 50000u
/* the pull path's bound on the SPU2's lead over the game's frames: 120 ms,
   the pacer's 100 ms of lag before it drops time (window_host.c
   RESYNC_NS) and about a period and a lead */
#define LEAD_MAX_FRAMES (RATE * 120 / 1000)
/* the bound while a film's PCM channels are open (ico_audio_sdl_film) */
#define LEAD_FILM_FRAMES (RATE * 60 / 1000)
/* a bound's least room over the steady lead (two device periods) */
#define LEAD_SLACK_MIN_FRAMES (RATE * 20 / 1000)
/* the lead over the game above which the stream plays ICO_AUDIO_TRIM
   slower, and below which it stops */
#define TRIM_ON_FRAMES (RATE * 64 / 1000)
#define TRIM_OFF_FRAMES (RATE * 32 / 1000)
/* the cushion's largest size: the stalls of a slow renderer's cinematics */
#define CUSHION_MAX_FRAMES (RATE * 120 / 1000)
/* pushed audio without a dry queue before the cushion halves: 20 s */
#define CUSHION_QUIET_FRAMES (RATE * 20)
/* the queue's limit above which a block is dropped, at most */
#define QUEUE_CEILING_FRAMES (RATE * 250 / 1000)
/* the block size until the first push: a 59.94 Hz vsync */
#define BLOCK_DEFAULT_FRAMES 800

static SDL_AudioStream *stream;
static int subsystem;            /* SDL_INIT_AUDIO is up */
static SDL_AudioDeviceID chosen; /* the named device the stream is on, 0: the default */
static int logged_unknown;
static unsigned long underruns;
static unsigned long drops;
static float rate_ratio = 1.0f;    /* ico_audio_set_rate_ratio */
static float applied_ratio = 1.0f; /* the stream's (ico_audio_play_ratio of rate_ratio) */
static Uint64 last_push_ns;
static int block_frames = BLOCK_DEFAULT_FRAMES; /* the last pushed block's size */
static int cushion_level; /* the cushion is two blocks doubled this often (capped) */
static int cushion_peak;  /* the largest cushion since the open, frames */
static long quiet_frames; /* pushed since the last dry queue or halving */
static int16_t silence[1024 * CHANNELS];
static int16_t scaled[1024 * CHANNELS];
static int16_t push_last[CHANNELS]; /* the last pushed frame (the dry refill's ramp) */

/* --- the pull path's state --------------------------------------------------- */

static IcoAudioRender pull_render; /* non-NULL: the pull path (ico_audio_sdl_open) */
static const char *sync_why = "";  /* the sync path's reason, for the start line */
static SDL_Mutex *audio_mutex;     /* the audio lock (audio_lock.h) on the pull path */
static char stream_name[ICO_AUDIO_DEVICE_NAME_MAX]; /* the device the stream was asked for */
static int lead_frames = PULL_MIN_FRAMES;           /* left queued after each take: one period */
static Uint64 period_ns = 10000000u;                /* the device's period */
static int device_frames, device_rate; /* the device's period in its frames, its rate */

/* the callback's own (and the main thread's only while no callback can
   run: before the device starts, after the stream is destroyed, while it
   is paused) */
static int16_t pull_buf[PULL_MAX_FRAMES * CHANNELS];
static int16_t pull_last[CHANNELS]; /* the last frame put */
static Uint64 pull_prev_ns;         /* the last callback's start */
static uint64_t pull_game_seen;     /* game_frames at the last callback */
static int pull_ahead;              /* frames rendered past the game's (0..the bound) */
static int pull_holding;            /* held back at the bound (until pull_release) */
/* a film's PCM channels are open (ico_audio_sdl_film: the game's side,
   under the audio lock; the callback reads it before taking the lock) */
static int pull_film;

/* the game's frames: the frames of every vsync it stepped
   (ico_audio_sdl_game_step, main thread; the callback reads it) */
static uint64_t game_frames;
/* the trim (main thread): playing ICO_AUDIO_TRIM slower, and the game's
   frames stepped while it did, in all */
static int trimming;
static uint64_t trim_frames;

/* Between the callback and the main thread, through __atomic: totals the
   callback adds to, the longest lock waits it raises (since the last 10 s
   line, and over the run), the time of its last call, the lead it left
   queued, and the main thread's flags (ramp_in: the next sound ramps in;
   fresh: the next callback follows an open or resume, so its gap is not
   late; paused: the device is stopped on purpose). */
static struct {
    uint64_t callbacks, frames, silence, held, misses, late;
    uint64_t wait_max_ns, wait_run_ns;
    uint64_t last_ns, asked;
    int queued;
    int ahead, ahead_max; /* the lead over the game after a callback, its 10 s maximum */
    int ramp_in, fresh, paused;
} P;

/* the totals at the last 10 s line (main thread) */
static struct {
    uint64_t callbacks, frames, silence, held, misses, late, ticks, long_gaps, trim;
} P_at;

#define P_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define P_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#define P_ADD(x, v) __atomic_fetch_add(&(x), (v), __ATOMIC_RELAXED)

/* the current cushion, frames: two blocks doubled cushion_level times,
   at most CUSHION_MAX_FRAMES */
static int cushion_frames(void)
{
    const long f = (2L * block_frames) << cushion_level;

    return f > CUSHION_MAX_FRAMES ? CUSHION_MAX_FRAMES : (int)f;
}

/* the queue above which a block is dropped: the cushion plus four
   blocks, at most QUEUE_CEILING_FRAMES */
static int queue_limit_frames(void)
{
    const long f = cushion_frames() + 4L * block_frames;

    return f > QUEUE_CEILING_FRAMES ? QUEUE_CEILING_FRAMES : (int)f;
}

/* Queue silence: the cushion at the open and a resume, or the rest of it
   after a dry queue; in pieces of the silence buffer's size. */
static void buffer_silence(int frames)
{
    while (frames > 0) {
        const int n = frames > 1024 ? 1024 : frames;
        if (!SDL_PutAudioStreamData(stream, silence, n * BYTES_PER_FRAME)) {
            break;
        }
        frames -= n;
    }
}

/* Queue `frames` of silence on `s` that starts as a ramp from `last` (the
   frame queued before it) down to 0 over RAMP_FRAMES: frame i of the ramp
   is last * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES, so the 96th is 0. */
static void buffer_ramp_down(SDL_AudioStream *s, const int16_t last[CHANNELS], int frames)
{
    int16_t ramp[RAMP_FRAMES * CHANNELS];
    const int n = frames < RAMP_FRAMES ? frames : RAMP_FRAMES;
    int i;

    for (i = 0; i < n; i++) {
        ramp[2 * i] = (int16_t)(last[0] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES);
        ramp[2 * i + 1] = (int16_t)(last[1] * (RAMP_FRAMES - 1 - i) / RAMP_FRAMES);
    }
    if (n > 0 && !SDL_PutAudioStreamData(s, ramp, n * BYTES_PER_FRAME)) {
        return;
    }
    for (frames -= n; frames > 0;) {
        const int k = frames > 1024 ? 1024 : frames;

        if (!SDL_PutAudioStreamData(s, silence, k * BYTES_PER_FRAME)) {
            break;
        }
        frames -= k;
    }
}

/* The first RAMP_FRAMES of a block after silence ramped in: frame i times
   i / RAMP_FRAMES. */
static void ramp_up(int16_t *frames, int count)
{
    int i;

    for (i = 0; i < count && i < RAMP_FRAMES; i++) {
        frames[2 * i] = (int16_t)(frames[2 * i] * i / RAMP_FRAMES);
        frames[2 * i + 1] = (int16_t)(frames[2 * i + 1] * i / RAMP_FRAMES);
    }
}

/* --- the pull path ------------------------------------------------------------- */

static void lock_take(void *m)
{
    SDL_LockMutex((SDL_Mutex *)m);
}

static void lock_give(void *m)
{
    SDL_UnlockMutex((SDL_Mutex *)m);
}

/* The audio lock: an SDL mutex (recursive), installed for the game's side;
   0 on success. */
static int lock_create(void)
{
    audio_mutex = SDL_CreateMutex();
    if (audio_mutex == NULL) {
        return -1;
    }
    ico_audio_lock_install(lock_take, lock_give, audio_mutex);
    return 0;
}

/* Uninstalled and destroyed; only with no callback running (no stream). */
static void lock_destroy(void)
{
    if (audio_mutex != NULL) {
        ico_audio_lock_install(NULL, NULL, NULL);
        SDL_DestroyMutex(audio_mutex);
        audio_mutex = NULL;
    }
}

/* The SPU2 level with the game again (the open, a move, a resume): nothing
   rendered ahead of its frames, not held back.  Only while no callback
   runs. */
static void pull_level(void)
{
    pull_game_seen = P_LOAD(game_frames);
    pull_ahead = 0;
    pull_holding = 0;
    P_STORE(P.ahead, 0);
}

/* The lead and the lock's wait from a device period of `frames` (at 48
   kHz), and a fresh start: the next sound ramps in, the next callback's gap
   is not late, the watchdog's clock starts now.  Only while no callback
   runs. */
static void pull_setup(int frames)
{
    if (frames <= 0) {
        frames = PULL_MIN_FRAMES;
    }
    lead_frames = frames > PULL_MIN_FRAMES ? frames : PULL_MIN_FRAMES;
    period_ns = (Uint64)frames * 1000000000u / RATE;
    pull_prev_ns = 0;
    pull_last[0] = pull_last[1] = 0;
    pull_level();
    P_STORE(P.queued, 0);
    P_STORE(P.ramp_in, 1);
    P_STORE(P.fresh, 1);
    P_STORE(P.asked, 0);
    P_STORE(P.last_ns, SDL_GetTicksNS());
}

/* The device's period, read back from SDL (the hint is a request), as
   frames at 48 kHz; the start line prints the device's own figures. */
static void pull_start(void)
{
    SDL_AudioSpec dev;
    int frames = 0;

    memset(&dev, 0, sizeof(dev));
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &dev, &frames) || frames <= 0 ||
        dev.freq <= 0) {
        frames = PULL_MIN_FRAMES;
        dev.freq = RATE;
    }
    device_frames = frames;
    device_rate = dev.freq;
    pull_setup((int)((long long)frames * RATE / dev.freq));
}

/* The longest lock waits, raised (the main thread zeroes the 10 s one). */
static void note_wait(Uint64 ns)
{
    uint64_t cur = P_LOAD(P.wait_max_ns);

    while (ns > cur && !__atomic_compare_exchange_n(&P.wait_max_ns, &cur, (uint64_t)ns, 1,
                                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    cur = P_LOAD(P.wait_run_ns);
    while (ns > cur && !__atomic_compare_exchange_n(&P.wait_run_ns, &cur, (uint64_t)ns, 1,
                                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* The lead over the game after a callback, and its maximum since the last
   10 s line raised. */
static void note_ahead(int ahead)
{
    int cur = P_LOAD(P.ahead_max);

    P_STORE(P.ahead, ahead);
    while (ahead > cur && !__atomic_compare_exchange_n(&P.ahead_max, &cur, ahead, 1,
                                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* The bound on the lead over the game: LEAD_MAX_FRAMES, LEAD_FILM_FRAMES
   while a film's PCM channels are open, and at least the steady lead (two
   device periods) and LEAD_SLACK_MIN_FRAMES */
static int pull_bound(void)
{
    const int base = P_LOAD(pull_film) ? LEAD_FILM_FRAMES : LEAD_MAX_FRAMES;
    const int least = 2 * lead_frames + LEAD_SLACK_MIN_FRAMES;

    return base > least ? base : least;
}

/* The lead a hold releases at: the steady lead the callbacks run at, one
   period queued after a take and the period's render ahead of the game's
   frames (two device periods), at most a period under the bound */
static int pull_release(int bound)
{
    const int steady = 2 * lead_frames;

    return steady < bound - lead_frames ? steady : bound - lead_frames;
}

/* How many of `want` frames the bound lets the callback render: the game's
   frames since the last callback taken off the lead (not below 0), then up
   to the bound (pull_bound) ahead, nothing while held back (released once
   the lead is back to pull_release).  *cut: the bound cut the request; the
   caller latches the hold once it rendered (or had nothing to render),
   never on a lock miss. */
static int pull_allow(int want, int *cut)
{
    const uint64_t g = P_LOAD(game_frames);
    const uint64_t moved = g - pull_game_seen;
    const int bound = pull_bound();
    int room;

    pull_game_seen = g;
    pull_ahead = moved >= (uint64_t)pull_ahead ? 0 : pull_ahead - (int)moved;
    if (pull_holding && pull_ahead <= pull_release(bound)) {
        pull_holding = 0;
    }
    room = pull_holding || pull_ahead >= bound ? 0 : bound - pull_ahead;
    *cut = want > room;
    return *cut ? room : want;
}

/* `frames` of silence ramped down from the last frame put, after a miss or
   at the bound: the next sound ramps in. */
static void pull_silence(SDL_AudioStream *s, int frames)
{
    buffer_ramp_down(s, pull_last, frames);
    pull_last[0] = pull_last[1] = 0;
    P_ADD(P.silence, (uint64_t)frames);
    P_STORE(P.ramp_in, 1);
}

/* The audio lock, tried every LOCK_POLL_NS for at most a quarter of the
   device period (and once more after a pause that overran it, so only a
   lock still held then is a miss); 1 when held.  *waited: the time it took
   or gave up after. */
static int pull_lock(Uint64 *waited)
{
    const Uint64 limit = period_ns / 4;
    Uint64 start = 0;
    int tried = 0;

    *waited = 0;
    for (;;) {
        Uint64 now;

        if (SDL_TryLockMutex(audio_mutex)) {
            if (tried) {
                *waited = SDL_GetTicksNS() - start;
            }
            return 1;
        }
        now = SDL_GetTicksNS();
        if (!tried) {
            tried = 1;
            start = now;
        } else if (now - start >= limit) {
            *waited = now - start;
            return 0;
        }
        SDL_DelayPrecise(LOCK_POLL_NS);
    }
}

/* SDL's audio thread, inside the stream's lock, when the device wants
   sound: `total` bytes (input format, the frequency ratio counted) are
   about to be taken, `additional` of them not queued yet.  Renders enough
   that the lead stays queued after the take; see the file comment. */
static void SDLCALL pull_cb(void *user, SDL_AudioStream *s, int additional, int total)
{
    const Uint64 t0 = SDL_GetTicksNS();
    const int queued = SDL_GetAudioStreamQueued(s) / BYTES_PER_FRAME;
    const int need = additional / BYTES_PER_FRAME;
    const int take = total / BYTES_PER_FRAME;
    int want = take + lead_frames - queued;
    int n, cut = 0;
    Uint64 waited = 0;

    (void)user;
    P_ADD(P.callbacks, 1);
    P_ADD(P.asked, 1);
    if (__atomic_exchange_n(&P.fresh, 0, __ATOMIC_ACQ_REL) == 0 && pull_prev_ns != 0 &&
        t0 - pull_prev_ns > 2 * period_ns) {
        P_ADD(P.late, 1); /* SDL's thread itself ran late: the device may have run dry */
    }
    pull_prev_ns = t0;
    P_STORE(P.last_ns, t0);
    if (want < need) {
        want = need;
    }
    if (want > PULL_MAX_FRAMES) {
        want = PULL_MAX_FRAMES;
    }
    if (want <= 0 || pull_render == NULL || audio_mutex == NULL) {
        P_STORE(P.queued, queued > take ? queued - take : 0);
        return;
    }
    /* the bound: at most pull_bound past the game's frames (the callback's
       own counts: before the lock, which a held-back callback does not
       take) */
    n = pull_allow(want, &cut);
    if (n > 0 && !pull_lock(&waited)) {
        /* the game's side holds the SPU2: its time stands still; the lead
           plays, and silence ramped down from the last frame covers what
           it does not (`need`, SDL's count past what is queued; at least
           the ramp) */
        int put = 0;

        note_wait(waited);
        P_ADD(P.misses, 1);
        if (need > 0) {
            put = need > RAMP_FRAMES ? need : RAMP_FRAMES;
            pull_silence(s, put);
        }
        P_STORE(P.queued, queued + put > take ? queued + put - take : 0);
        return;
    }
    if (n > 0) {
        int q8;

        note_wait(waited);
        pull_render(pull_buf, n);
        SDL_UnlockMutex(audio_mutex);
        q8 = ico_audio_volume_q8(); /* one read: Settings may change it */
        if (q8 != 256) {
            ico_audio_scale(pull_buf, pull_buf, n * CHANNELS, q8);
        }
        if (__atomic_exchange_n(&P.ramp_in, 0, __ATOMIC_ACQ_REL) != 0) {
            ramp_up(pull_buf, n);
        }
        pull_last[0] = pull_buf[2 * (n - 1)];
        pull_last[1] = pull_buf[2 * (n - 1) + 1];
        SDL_PutAudioStreamData(s, pull_buf, n * BYTES_PER_FRAME);
        P_ADD(P.frames, (uint64_t)n);
        pull_ahead += n;
    }
    if (cut) {
        pull_holding = 1; /* after a render (or none left to make): not on a lock miss */
    }
    note_ahead(pull_ahead);
    if (n < want && need > n) {
        /* held back: SDL's shortfall is silence */
        const int put = need - n > RAMP_FRAMES ? need - n : RAMP_FRAMES;

        pull_silence(s, put);
        P_ADD(P.held, (uint64_t)put);
        n += put;
    }
    P_STORE(P.queued, queued + n > take ? queued + n - take : 0);
}

/* the playback device called `name`, 0 when none is.  A name as long as
   the list's buffers hold (ico_audio_sdl_devices cuts at
   ICO_AUDIO_DEVICE_NAME_MAX - 1 bytes, and Settings saves what it listed)
   matches a device whose name starts with it, when no name is equal. */
static SDL_AudioDeviceID find_device(const char *name)
{
    SDL_AudioDeviceID found = 0, prefixed = 0;
    SDL_AudioDeviceID *ids;
    const size_t len = strlen(name);
    int n = 0;
    int i;

    ids = SDL_GetAudioPlaybackDevices(&n);
    for (i = 0; ids != NULL && i < n && found == 0; i++) {
        const char *s = SDL_GetAudioDeviceName(ids[i]);

        if (s == NULL) {
            continue;
        }
        if (strcmp(s, name) == 0) {
            found = ids[i];
        } else if (prefixed == 0 && len >= ICO_AUDIO_DEVICE_NAME_MAX - 1 &&
                   strncmp(s, name, len) == 0) {
            prefixed = ids[i];
        }
    }
    SDL_free(ids);
    return found != 0 ? found : prefixed;
}

/* the stream on `name`'s device, or the default; playing.  The pull path
   (pull_render set) opens it with the callback and primes nothing; the
   sync path primes the cushion with silence. */
static int open_stream(const char *name)
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID id = 0;
    SDL_AudioStreamCallback cb = pull_render != NULL ? pull_cb : NULL;

    memset(&spec, 0, sizeof(spec));
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = CHANNELS;
    spec.freq = 48000;
    if (name != stream_name) {
        snprintf(stream_name, sizeof(stream_name), "%s", name != NULL ? name : "");
    }
    if (name != NULL && name[0] != '\0') {
        id = find_device(name);
        if (id == 0 && !logged_unknown) {
            logged_unknown = 1;
            fprintf(stderr, "audio: no output device called \"%s\"; using the default\n", name);
        }
    }
    chosen = 0;
    if (id != 0) {
        stream = SDL_OpenAudioDeviceStream(id, &spec, cb, NULL);
        if (stream != NULL) {
            chosen = id;
        } else {
            fprintf(stderr, "audio: cannot open \"%s\" (%s); using the default\n", name,
                    SDL_GetError());
        }
    }
    if (stream == NULL) {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, cb, NULL);
    }
    if (stream == NULL) {
        fprintf(stderr, "audio: no audio device (%s); no sound\n", SDL_GetError());
        return -1;
    }
    applied_ratio = rate_ratio;
    if (rate_ratio != 1.0f) {
        SDL_SetAudioStreamFrequencyRatio(stream, rate_ratio);
    }
    if (pull_render != NULL) {
        pull_start(); /* before the device starts: no callback yet */
    } else {
        buffer_silence(cushion_frames());
        push_last[0] = push_last[1] = 0;
    }
    SDL_ResumeAudioStreamDevice(stream);
    if (chosen != 0) {
        fprintf(stderr, "audio: output on \"%s\"\n", name);
    }
    return 0;
}

int ico_audio_sdl_open(IcoAudioRender pull, const char *why)
{
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio: SDL audio unavailable (%s); no sound\n", SDL_GetError());
        return -1;
    }
    cushion_level = 0; /* before the open primes it */
    pull_render = pull;
    sync_why = why != NULL ? why : "";
    if (pull_render != NULL && lock_create() != 0) {
        fprintf(stderr, "audio: no lock for the device's thread (%s); on the game's clock\n",
                SDL_GetError());
        pull_render = NULL;
        sync_why = "no lock";
    }
    if (open_stream(getenv("ICO_AUDIO_DEVICE")) != 0) {
        lock_destroy();
        pull_render = NULL;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    subsystem = 1;
    underruns = drops = 0;
    last_push_ns = 0;
    quiet_frames = 0;
    cushion_peak = cushion_frames();
    if (pull_render != NULL) {
        fprintf(stderr,
                "audio: SDL driver %s, 48 kHz stereo, played on the device's clock: %d frames a "
                "callback (%.1f ms at the device's %d Hz), %.1f ms kept ahead\n",
                SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "?",
                device_frames, device_frames * 1000.0 / device_rate, device_rate,
                lead_frames * 1000.0 / RATE);
    } else {
        fprintf(stderr,
                "audio: SDL driver %s, 48 kHz stereo, %d ms buffered (up to %d ms after a dry "
                "queue); on the game's clock (%s)\n",
                SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "?",
                cushion_frames() * 1000 / RATE, CUSHION_MAX_FRAMES * 1000 / RATE, sync_why);
    }
    return 0;
}

int ico_audio_sdl_devices(char names[][ICO_AUDIO_DEVICE_NAME_MAX], int max)
{
    SDL_AudioDeviceID *ids;
    int n = 0;
    int k = 0;
    int i;

    if (!subsystem || names == NULL || max <= 0) {
        return 0;
    }
    ids = SDL_GetAudioPlaybackDevices(&n);
    for (i = 0; ids != NULL && i < n && k < max; i++) {
        const char *s = SDL_GetAudioDeviceName(ids[i]);

        if (s != NULL && s[0] != '\0') {
            snprintf(names[k++], ICO_AUDIO_DEVICE_NAME_MAX, "%s", s);
        }
    }
    SDL_free(ids);
    return k;
}

int ico_audio_sdl_reopen(const char *name)
{
    if (!subsystem) {
        return -1;
    }
    if (stream != NULL) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
    logged_unknown = 0; /* a new name: its own line */
    return open_stream(name);
}

void ico_audio_sdl_device_removed(uint32_t which)
{
    if (stream != NULL && chosen != 0 && which == chosen) {
        fprintf(stderr, "audio: the output device was removed; using the default\n");
        ico_audio_sdl_reopen("");
    }
}

void ico_audio_sdl_pause(int paused)
{
    if (stream == NULL) {
        return;
    }
    if (paused) {
        P_STORE(P.paused, 1); /* the watchdog waits */
        if (!SDL_PauseAudioStreamDevice(stream)) {
            fprintf(stderr, "audio: pause: %s\n", SDL_GetError());
        }
        return;
    }
    /* what was queued before the pause would play late: from silence (the
       pull path's lead, at most a period, dropped; the next sound ramps
       in) */
    SDL_ClearAudioStream(stream);
    if (pull_render != NULL) {
        /* the device is paused: no callback runs; a miss right after the
           resume ramps down from silence, not from before the pause */
        pull_level();
        pull_last[0] = pull_last[1] = 0;
        P_STORE(P.ramp_in, 1);
        P_STORE(P.fresh, 1);
        P_STORE(P.asked, 0);
        P_STORE(P.last_ns, SDL_GetTicksNS());
        P_STORE(P.paused, 0);
    } else {
        buffer_silence(cushion_frames());
        push_last[0] = push_last[1] = 0;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        fprintf(stderr, "audio: resume: %s\n", SDL_GetError());
    }
}

void ico_audio_set_rate_ratio(double ratio)
{
    /* SDL takes 0.01 .. 100; the window's ratios are within 2 % of 1 */
    const float r = ratio > 0.5 && ratio < 2.0 ? (float)ratio : 1.0f;

    if (r == rate_ratio) {
        return;
    }
    rate_ratio = r;
    applied_ratio = r; /* the trim, if any, again at the next ico_audio_sdl_game_step */
    if (stream != NULL && !SDL_SetAudioStreamFrequencyRatio(stream, r)) {
        fprintf(stderr, "audio: rate ratio: %s\n", SDL_GetError());
    }
}

void ico_audio_sdl_film(int on)
{
    P_STORE(pull_film, on != 0);
}

void ico_audio_sdl_game_step(int frames)
{
    int ahead;
    float want;

    if (frames > 0) {
        __atomic_fetch_add(&game_frames, (uint64_t)frames, __ATOMIC_RELEASE);
    }
    if (stream == NULL || pull_render == NULL) {
        trimming = 0;
        return;
    }
    /* the trim: past TRIM_ON_FRAMES ahead of the game the stream plays
       ICO_AUDIO_TRIM slower (the SPU2 renders that much less a second)
       until under TRIM_OFF_FRAMES */
    ahead = P_LOAD(P.ahead);
    if (!trimming && ahead > TRIM_ON_FRAMES) {
        trimming = 1;
    } else if (trimming && ahead < TRIM_OFF_FRAMES) {
        trimming = 0;
    }
    if (trimming && frames > 0) {
        trim_frames += (uint64_t)frames;
    }
    want = trimming ? (float)(rate_ratio / ICO_AUDIO_TRIM) : rate_ratio;
    if (want != applied_ratio && SDL_SetAudioStreamFrequencyRatio(stream, want)) {
        applied_ratio = want;
    }
}

void ico_audio_sdl_push(const int16_t *frames, int count)
{
    int queued;
    int vsync_bytes = count * BYTES_PER_FRAME;
    int q8 = ico_audio_volume_q8(); /* one read: the Settings menu may change it */

    if (stream == NULL || count <= 0) {
        return;
    }
    const Uint64 now = SDL_GetTicksNS();
    const double gap_ms = last_push_ns ? (double)(now - last_push_ns) / 1e6 : 0.0;
    last_push_ns = now;
    block_frames = count;
    /* a quiet stretch: the cushion one step back toward two vsyncs */
    quiet_frames += count;
    if (quiet_frames >= CUSHION_QUIET_FRAMES) {
        quiet_frames = 0;
        if (cushion_level > 0) {
            cushion_level--;
        }
    }
    queued = SDL_GetAudioStreamQueued(stream);
    {
        /* more than the cushion waiting: played out a little faster,
           never into the cushion itself */
        const float want =
            (float)ico_audio_play_ratio(rate_ratio, queued, cushion_frames() * BYTES_PER_FRAME);

        if (want != applied_ratio && SDL_SetAudioStreamFrequencyRatio(stream, want)) {
            applied_ratio = want;
        }
    }
    if (queued > queue_limit_frames() * BYTES_PER_FRAME) {
        drops++;
        return;
    }
    if (queued >= 0 && queued < vsync_bytes / 4) {
        /* dry: the cushion was too short for this stall; doubled (up to
           CUSHION_MAX_FRAMES) and filled with silence before the block */
        underruns++;
        quiet_frames = 0;
        if (cushion_frames() < CUSHION_MAX_FRAMES) {
            cushion_level++;
        }
        if (cushion_frames() > cushion_peak) {
            cushion_peak = cushion_frames();
        }
        if (underruns <= 8) {
            fprintf(stderr,
                    "audio: low queue %d frames, %.1f ms between pushes; refilled to a %d ms "
                    "cushion\n",
                    queued / BYTES_PER_FRAME, gap_ms, cushion_frames() * 1000 / RATE);
        }
        if (queued / BYTES_PER_FRAME > 0) {
            /* ramped down from what is still queued (2 ms) */
            buffer_ramp_down(stream, push_last, cushion_frames() - queued / BYTES_PER_FRAME);
        } else {
            /* nothing left: the device already played its own silence, so
               plain silence follows it */
            buffer_silence(cushion_frames());
        }
    }
    if (count <= 1024 && q8 != 256) {
        /* the volume (audio_host.h): scaled into a copy, the caller's block
           stays unscaled for the WAV dump */
        ico_audio_scale(scaled, frames, count * CHANNELS, q8);
        frames = scaled;
    }
    push_last[0] = frames[2 * (count - 1)];
    push_last[1] = frames[2 * (count - 1) + 1];
    SDL_PutAudioStreamData(stream, frames, vsync_bytes);
}

int ico_audio_sdl_active(void)
{
    return stream != NULL;
}

int ico_audio_sdl_queued_ms(void)
{
    int queued;

    if (stream != NULL && pull_render != NULL) {
        /* the lead the last callback left (no call into SDL's stream lock,
           which a callback holds while it renders) */
        return (int)((long long)P_LOAD(P.queued) * 1000 / RATE);
    }
    queued = stream != NULL ? SDL_GetAudioStreamQueued(stream) : -1;
    return queued < 0 ? -1 : (int)((long long)queued / BYTES_PER_FRAME * 1000 / RATE);
}

int ico_audio_sdl_cushion_ms(void)
{
    /* the pull path keeps no cushion: 0 */
    return stream != NULL && pull_render == NULL ? cushion_frames() * 1000 / RATE : 0;
}

int ico_audio_sdl_pull_idle_ms(int *asked)
{
    const Uint64 last = P_LOAD(P.last_ns);
    const Uint64 now = SDL_GetTicksNS();

    if (asked != NULL) {
        *asked = P_LOAD(P.asked) != 0;
    }
    if (stream == NULL || pull_render == NULL || P_LOAD(P.paused) != 0) {
        return -1;
    }
    return now > last ? (int)((now - last) / 1000000u) : 0;
}

/* What SDL says at the pull path's fall-back (main thread, once a run): the
   last error SDL set on this thread (the device's own thread keeps its
   own) and whether the stream's device is still among the playback
   devices. */
static void fall_back_reason(void)
{
    const char *err = SDL_GetError();
    const SDL_AudioDeviceID dev = stream != NULL ? SDL_GetAudioStreamDevice(stream) : 0;
    const char *name = dev != 0 ? SDL_GetAudioDeviceName(dev) : NULL;
    char held[ICO_AUDIO_DEVICE_NAME_MAX];
    SDL_AudioDeviceID *ids;
    int n = 0;
    int listed = 0;
    int i;

    snprintf(held, sizeof(held), "%s", name != NULL ? name : "");
    ids = SDL_GetAudioPlaybackDevices(&n);
    for (i = 0; ids != NULL && i < n && held[0] != '\0'; i++) {
        const char *s = SDL_GetAudioDeviceName(ids[i]);

        listed |= s != NULL && strcmp(s, held) == 0;
    }
    SDL_free(ids);
    fprintf(stderr,
            "audio: at the fall-back SDL's last error on this thread is \"%s\"; the stream's "
            "device \"%s\" is %s (%d playback devices listed)\n",
            err != NULL && err[0] != '\0' ? err : "(none)", held[0] != '\0' ? held : "(unnamed)",
            held[0] == '\0' ? "unknown" : (listed ? "still listed" : "not listed"), n);
}

int ico_audio_sdl_fall_back(void)
{
    if (!subsystem || pull_render == NULL) {
        return stream != NULL ? 0 : -1;
    }
    fall_back_reason();
    if (stream != NULL) {
        SDL_DestroyAudioStream(stream); /* waits for a callback in progress */
        stream = NULL;
    }
    lock_destroy();
    pull_render = NULL;
    sync_why = "the device stopped asking";
    cushion_level = 0;
    block_frames = BLOCK_DEFAULT_FRAMES;
    quiet_frames = 0;
    last_push_ns = 0;
    return open_stream(stream_name);
}

int ico_audio_sdl_summary_text(char *out, size_t size)
{
    IcoAudioTicks t;
    uint64_t callbacks, frames, silence, held, misses, late, wait;
    int ahead;

    if (stream == NULL || pull_render == NULL || out == NULL || size == 0) {
        return 0;
    }
    callbacks = P_LOAD(P.callbacks);
    frames = P_LOAD(P.frames);
    silence = P_LOAD(P.silence);
    held = P_LOAD(P.held);
    misses = P_LOAD(P.misses);
    late = P_LOAD(P.late);
    wait = __atomic_exchange_n(&P.wait_max_ns, 0, __ATOMIC_ACQ_REL);
    ahead = __atomic_exchange_n(&P.ahead_max, 0, __ATOMIC_ACQ_REL);
    ico_audio_tick_take(&t);
    snprintf(out, size,
             "audio: %llu callbacks, %llu frames; waited %.2f ms at most for the driver (%llu "
             "gave up), %llu callbacks late, %llu ms of silence (%llu ms held back behind the "
             "game); ahead of the game by %d ms at most, slowed for %.1f s; the driver ticked "
             "%llu times, longest gap %llu ms (%llu over %llu ms)",
             (unsigned long long)(callbacks - P_at.callbacks),
             (unsigned long long)(frames - P_at.frames), wait / 1e6,
             (unsigned long long)(misses - P_at.misses), (unsigned long long)(late - P_at.late),
             (unsigned long long)((silence - P_at.silence) * 1000u / RATE),
             (unsigned long long)((held - P_at.held) * 1000u / RATE), ahead * 1000 / RATE,
             (double)(trim_frames - P_at.trim) / RATE, (unsigned long long)(t.ticks - P_at.ticks),
             (unsigned long long)(t.gap_max_ns / 1000000u),
             (unsigned long long)(t.long_gaps - P_at.long_gaps),
             (unsigned long long)(ICO_AUDIO_TICK_LONG_NS / 1000000u));
    P_at.callbacks = callbacks;
    P_at.frames = frames;
    P_at.silence = silence;
    P_at.held = held;
    P_at.trim = trim_frames;
    P_at.misses = misses;
    P_at.late = late;
    P_at.ticks = t.ticks;
    P_at.long_gaps = t.long_gaps;
    return 1;
}

void ico_audio_sdl_log_summary(void)
{
    char line[640];

    if (ico_audio_sdl_summary_text(line, sizeof(line))) {
        fprintf(stderr, "%s\n", line);
    }
}

void ico_audio_sdl_close(void)
{
    if (stream == NULL && !subsystem) {
        return;
    }
    if (stream != NULL) {
        SDL_DestroyAudioStream(stream); /* waits for a callback in progress */
    }
    stream = NULL;
    lock_destroy();
    chosen = 0;
    subsystem = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (pull_render != NULL) {
        fprintf(stderr,
                "audio: SDL output closed (on the device's clock: %llu callbacks, %llu frames, "
                "%llu callbacks late; the lock waited %.2f ms at most and gave up %llu times, "
                "%llu ms of silence, %llu ms of it held back behind the game; slowed for %.1f "
                "s)\n",
                (unsigned long long)P_LOAD(P.callbacks), (unsigned long long)P_LOAD(P.frames),
                (unsigned long long)P_LOAD(P.late), P_LOAD(P.wait_run_ns) / 1e6,
                (unsigned long long)P_LOAD(P.misses),
                (unsigned long long)(P_LOAD(P.silence) * 1000u / RATE),
                (unsigned long long)(P_LOAD(P.held) * 1000u / RATE), (double)trim_frames / RATE);
        pull_render = NULL;
        return;
    }
    fprintf(stderr,
            "audio: SDL output closed (cushion %d ms, largest %d ms; %lu refills after a dry "
            "queue, %lu blocks dropped)\n",
            cushion_frames() * 1000 / RATE, cushion_peak * 1000 / RATE, underruns, drops);
}
