/*
 * port/audio/out_sdl.c
 *
 * The SPU2's output on an SDL3 audio device (audio_host.h; window build
 * only).  One SDL_AudioStream at the SPU2's own format, 48 kHz stereo S16,
 * so SDL converts only if the device needs it.  The simulation thread
 * pushes one vsync of frames per vsync (SDL_PutAudioStreamData is
 * thread-safe; nothing else is shared with SDL's audio thread).
 *
 * Latency: the stream is kept at about two vsyncs (40 ms at 50 Hz) of
 * queued audio, plus the device buffer (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES
 * = 480, 10 ms).  The window paces the simulation to real time, so
 * production and consumption match on average; when the queue runs dry
 * (a stall) one vsync of silence is queued ahead of the block to rebuild
 * the cushion, and when more than four vsyncs are queued (the simulation ran
 * ahead) the block is dropped.  Both are counted and logged at the close.
 *
 * Volume: each block is scaled by audio_host.h's ico_audio_volume_q8()
 * (0..256 in 1/256 steps) just before it is queued, so a change from the
 * Settings menu is heard within two vsyncs.
 *
 * Device ([audio] device, docs/port/AUDIO.md "Output device"): the stream
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

static SDL_AudioStream *stream;
static int subsystem;            /* SDL_INIT_AUDIO is up */
static SDL_AudioDeviceID chosen; /* the named device the stream is on, 0: the default */
static int logged_unknown;
static unsigned long underruns;
static unsigned long drops;
static int16_t silence[1024 * CHANNELS];
static int16_t scaled[1024 * CHANNELS];

/* the playback device called `name`, 0 when none is */
static SDL_AudioDeviceID find_device(const char *name)
{
    SDL_AudioDeviceID found = 0;
    SDL_AudioDeviceID *ids;
    int n = 0;
    int i;

    ids = SDL_GetAudioPlaybackDevices(&n);
    for (i = 0; ids != NULL && i < n && found == 0; i++) {
        const char *s = SDL_GetAudioDeviceName(ids[i]);

        if (s != NULL && strcmp(s, name) == 0) {
            found = ids[i];
        }
    }
    SDL_free(ids);
    return found;
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
    if (open_stream(getenv("ICO_AUDIO_DEVICE")) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    subsystem = 1;
    fprintf(stderr, "audio: SDL driver %s, 48 kHz stereo, about two vsyncs buffered\n",
            SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "?");
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

void ico_audio_sdl_push(const int16_t *frames, int count)
{
    int queued;
    int vsync_bytes = count * BYTES_PER_FRAME;
    int q8 = ico_audio_volume_q8(); /* one read: the Settings menu may change it */

    if (stream == NULL || count <= 0) {
        return;
    }
    queued = SDL_GetAudioStreamQueued(stream);
    if (queued > 4 * vsync_bytes) {
        drops++;
        return;
    }
    if (queued < vsync_bytes / 4 && count <= 1024) {
        if (queued >= 0) {
            underruns++;
        }
        SDL_PutAudioStreamData(stream, silence, vsync_bytes);
    }
    if (count <= 1024 && q8 != 256) {
        /* the volume (audio_host.h): scaled into a copy, the caller's block
           stays unscaled for the WAV dump */
        ico_audio_scale(scaled, frames, count * CHANNELS, q8);
        frames = scaled;
    }
    SDL_PutAudioStreamData(stream, frames, vsync_bytes);
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
            "audio: SDL output closed (%lu refills after a dry queue, %lu blocks dropped)\n",
            underruns, drops);
}
