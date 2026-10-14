/*
 * port/audio/out_sdl.c
 *
 * The SPU2's output on an SDL3 audio device (audio_host.h; window build
 * only).  One SDL_AudioStream at the SPU2's own format, 48 kHz stereo S16,
 * so SDL converts only if the device needs it.  The simulation thread
 * pushes one vsync of frames per vsync (SDL_PutAudioStreamData is
 * thread-safe; nothing else is shared with SDL's audio thread).
 *
 * Latency: the stream keeps a cushion of queued audio, plus the device
 * buffer (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES = 480, 10 ms).  The cushion
 * starts at two vsyncs (two of the pushed blocks: 33 ms at 59.94 Hz, 40 ms
 * at 50) and is what the open and every resume prime with silence.  The
 * window paces the simulation to real time, so production and consumption
 * match on average.  When the queue runs dry (a stall: the simulation fell
 * behind real time by more than the cushion) the cushion doubles, up to
 * 120 ms (CUSHION_MAX_FRAMES), and silence fills the queue to it before the
 * block; after 20 s of pushed audio without a dry queue
 * (CUSHION_QUIET_FRAMES) it halves again, down to two vsyncs, and the trim
 * below plays the surplus out.  When more than the cushion plus four
 * vsyncs is queued (the simulation ran ahead; never more than 250 ms,
 * QUEUE_CEILING_FRAMES) the block is dropped.  Refills and drops are
 * counted and logged at the close with the cushion.
 * Under the window's vsync lock the display is the simulation's clock, so
 * the simulation runs a little off 59.94 Hz (60.00 on a 60.00 Hz screen);
 * the window measures the rate every 10 s and the stream plays at that
 * ratio (ico_audio_set_rate_ratio, SDL_SetAudioStreamFrequencyRatio).
 *
 * Volume: each block is scaled by audio_host.h's ico_audio_volume_q8()
 * (0..256 in 1/256 steps) just before it is queued, so a change from the
 * Settings menu is heard within the queued audio (the cushion and a
 * vsync).
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

#define CHANNELS 2
#define BYTES_PER_FRAME (CHANNELS * 2)
#define RATE 48000
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

/* the stream on `name`'s device, or the default; playing */
static int open_stream(const char *name)
{
    SDL_AudioSpec spec;
    SDL_AudioDeviceID id = 0;

    memset(&spec, 0, sizeof(spec));
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = CHANNELS;
    spec.freq = 48000;
    if (name != NULL && name[0] != '\0') {
        id = find_device(name);
        if (id == 0 && !logged_unknown) {
            logged_unknown = 1;
            fprintf(stderr, "audio: no output device called \"%s\"; using the default\n", name);
        }
    }
    chosen = 0;
    if (id != 0) {
        stream = SDL_OpenAudioDeviceStream(id, &spec, NULL, NULL);
        if (stream != NULL) {
            chosen = id;
        } else {
            fprintf(stderr, "audio: cannot open \"%s\" (%s); using the default\n", name,
                    SDL_GetError());
        }
    }
    if (stream == NULL) {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (stream == NULL) {
        fprintf(stderr, "audio: no audio device (%s); no sound\n", SDL_GetError());
        return -1;
    }
    applied_ratio = rate_ratio;
    if (rate_ratio != 1.0f) {
        SDL_SetAudioStreamFrequencyRatio(stream, rate_ratio);
    }
    buffer_silence(cushion_frames());
    SDL_ResumeAudioStreamDevice(stream);
    if (chosen != 0) {
        fprintf(stderr, "audio: output on \"%s\"\n", name);
    }
    return 0;
}

int ico_audio_sdl_open(void)
{
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio: SDL audio unavailable (%s); no sound\n", SDL_GetError());
        return -1;
    }
    cushion_level = 0; /* before the open primes it */
    if (open_stream(getenv("ICO_AUDIO_DEVICE")) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    subsystem = 1;
    underruns = drops = 0;
    last_push_ns = 0;
    quiet_frames = 0;
    cushion_peak = cushion_frames();
    fprintf(stderr,
            "audio: SDL driver %s, 48 kHz stereo, %d ms buffered (up to %d ms after a dry "
            "queue)\n",
            SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "?",
            cushion_frames() * 1000 / RATE, CUSHION_MAX_FRAMES * 1000 / RATE);
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
        if (!SDL_PauseAudioStreamDevice(stream)) {
            fprintf(stderr, "audio: pause: %s\n", SDL_GetError());
        }
        return;
    }
    /* what was queued before the pause would play late: from silence */
    SDL_ClearAudioStream(stream);
    buffer_silence(cushion_frames());
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
    applied_ratio = r; /* the trim, if any, again at the next push */
    if (stream != NULL && !SDL_SetAudioStreamFrequencyRatio(stream, r)) {
        fprintf(stderr, "audio: rate ratio: %s\n", SDL_GetError());
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
        buffer_silence(cushion_frames() - queued / BYTES_PER_FRAME);
    }
    if (count <= 1024 && q8 != 256) {
        /* the volume (audio_host.h): scaled into a copy, the caller's block
           stays unscaled for the WAV dump */
        ico_audio_scale(scaled, frames, count * CHANNELS, q8);
        frames = scaled;
    }
    SDL_PutAudioStreamData(stream, frames, vsync_bytes);
}

int ico_audio_sdl_active(void)
{
    return stream != NULL;
}

int ico_audio_sdl_queued_ms(void)
{
    const int queued = stream != NULL ? SDL_GetAudioStreamQueued(stream) : -1;

    return queued < 0 ? -1 : (int)((long long)queued / BYTES_PER_FRAME * 1000 / RATE);
}

int ico_audio_sdl_cushion_ms(void)
{
    return stream != NULL ? cushion_frames() * 1000 / RATE : 0;
}

void ico_audio_sdl_close(void)
{
    if (stream == NULL && !subsystem) {
        return;
    }
    if (stream != NULL) {
        SDL_DestroyAudioStream(stream);
    }
    stream = NULL;
    chosen = 0;
    subsystem = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    fprintf(stderr,
            "audio: SDL output closed (cushion %d ms, largest %d ms; %lu refills after a dry "
            "queue, %lu blocks dropped)\n",
            cushion_frames() * 1000 / RATE, cushion_peak * 1000 / RATE, underruns, drops);
}
