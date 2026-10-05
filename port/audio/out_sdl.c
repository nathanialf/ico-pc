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
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "audio_host.h"

#define CHANNELS 2
#define BYTES_PER_FRAME (CHANNELS * 2)

static SDL_AudioStream *stream;
static unsigned long underruns;
static unsigned long drops;
static int16_t silence[1024 * CHANNELS];

int ico_audio_sdl_open(void)
{
    SDL_AudioSpec spec;

    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio: SDL audio unavailable (%s); no sound\n", SDL_GetError());
        return -1;
    }
    memset(&spec, 0, sizeof(spec));
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = CHANNELS;
    spec.freq = 48000;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (stream == NULL) {
        fprintf(stderr, "audio: no audio device (%s); no sound\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    SDL_ResumeAudioStreamDevice(stream);
    fprintf(stderr, "audio: SDL driver %s, 48 kHz stereo, about two vsyncs buffered\n",
            SDL_GetCurrentAudioDriver() != NULL ? SDL_GetCurrentAudioDriver() : "?");
    return 0;
}

void ico_audio_sdl_push(const int16_t *frames, int count)
{
    int queued;
    int vsync_bytes = count * BYTES_PER_FRAME;

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
    SDL_PutAudioStreamData(stream, frames, vsync_bytes);
}

void ico_audio_sdl_close(void)
{
    if (stream == NULL) {
        return;
    }
    SDL_DestroyAudioStream(stream);
    stream = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    fprintf(stderr,
            "audio: SDL output closed (%lu refills after a dry queue, %lu blocks dropped)\n",
            underruns, drops);
}
