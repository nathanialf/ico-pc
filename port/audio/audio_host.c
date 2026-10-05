/*
 * port/audio/audio_host.c
 *
 * The per-vsync SPU2 render and its sinks (audio_host.h).
 */
#include "audio_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sndn2_host.h"
#include "spu2.h"

#define MAX_FRAMES_PER_VSYNC 1024 /* 960 at 50 Hz, 800 or 801 at 59.94 Hz */

static int started;
static IcoWav *wav;
#ifdef ICO_AUDIO_SDL
static int sdl_open;
#endif
static unsigned int vsync_index;
static int16_t out[MAX_FRAMES_PER_VSYNC * 2];

/* what the dump saw, for the summary line */
static int dump_peak;
static long long first_sound_vsync = -1;

int ico_audio_host_frames(int hz, unsigned int i)
{
    if (hz == 50) {
        return SPU2_RATE / 50;
    }
    /* 59.94 Hz (host_loop.c's NTSC step): 48000 * 1001 / 60000 = 800.8
       frames, spread exactly over every five vsyncs */
    return (int)((uint64_t)(i + 1) * 48000u * 1001u / 60000u -
                 (uint64_t)i * 48000u * 1001u / 60000u);
}

void ico_audio_host_shutdown(void)
{
    if (wav != NULL) {
        uint64_t frames = ico_wav_close(wav);

        wav = NULL;
        fprintf(stderr,
                "audio: dump closed: %llu frames (%.1f s), peak %d, first sound at vsync %lld\n",
                (unsigned long long)frames, (double)frames / SPU2_RATE, dump_peak,
                first_sound_vsync);
    }
#ifdef ICO_AUDIO_SDL
    if (sdl_open) {
        ico_audio_sdl_close();
        sdl_open = 0;
    }
#endif
}

void ico_audio_host_init(void)
{
    const char *dump = getenv("ICO_AUDIO_DUMP");
    const char *enable = getenv("ICO_AUDIO");

    if (started) {
        return;
    }
    started = 1;
    ico_sndn2_host_register();
    if (dump != NULL && dump[0] != '\0') {
        wav = ico_wav_open(dump, SPU2_RATE);
        if (wav != NULL) {
            fprintf(stderr, "audio: writing the mixed output to %s (48 kHz stereo)\n", dump);
        } else {
            fprintf(stderr, "audio: cannot create %s; no dump\n", dump);
        }
    }
#ifdef ICO_AUDIO_SDL
    if (enable != NULL && strcmp(enable, "0") == 0) {
        fprintf(stderr, "audio: audio=0: no audio device (the driver still runs)\n");
    } else {
        sdl_open = ico_audio_sdl_open() == 0;
    }
#else
    (void)enable;
    fprintf(stderr, "audio: headless build: the driver runs, the output is %s\n",
            wav != NULL ? "only dumped" : "discarded");
#endif
    atexit(ico_audio_host_shutdown);
}

void ico_audio_host_vsync(int hz)
{
    int frames = ico_audio_host_frames(hz, vsync_index);

    if (!started) {
        ico_audio_host_init();
    }
    spu2_render(out, frames);
    if (wav != NULL) {
        int i;

        for (i = 0; i < frames * 2; i++) {
            int a = out[i] < 0 ? -out[i] : out[i];

            if (a > dump_peak) {
                dump_peak = a;
            }
            if (a > 0 && first_sound_vsync < 0) {
                first_sound_vsync = (long long)vsync_index;
            }
        }
        ico_wav_write(wav, out, frames);
    }
#ifdef ICO_AUDIO_SDL
    if (sdl_open) {
        ico_audio_sdl_push(out, frames);
    }
#endif
    vsync_index++;
}
