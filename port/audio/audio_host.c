/*
 * port/audio/audio_host.c
 *
 * The per-vsync SPU2 render and its sinks (audio_host.h).
 */
#include "audio_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"
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
    spu2_stats st;

    spu2_get_stats(&st);
    if (st.chunked_frames + st.exact_frames > 0) {
        fprintf(stderr,
                "audio: SPU2 rendered %llu frames voice by voice in %llu chunks and %llu frame "
                "by frame (%llu hazards)\n",
                (unsigned long long)st.chunked_frames, (unsigned long long)st.chunks,
                (unsigned long long)st.exact_frames, (unsigned long long)st.hazards);
    }
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
    const char *vol = getenv("ICO_AUDIO_VOLUME");

    if (started) {
        return;
    }
    started = 1;
    if (vol != NULL && vol[0] != '\0') {
        ico_audio_set_volume(strtod(vol, NULL));
    }
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
    /* mirror mode (R7c): left and right swapped where the stereo output is
       produced, for the device and the dump alike; the SPU2 is untouched */
    ico_audio_pan_mirror(out, frames, ico_opt_mirror());
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

/* Mirror mode (Phase 6A, docs/port/OPTIONS.md): the option lives in
   port/game/options.c; ico_audio_host_vsync swaps the channels of each
   rendered block while it is on (renderer wave 7, R7c). */
void ico_audio_pan_mirror(int16_t *frames, int count, int mirror)
{
    int i;

    if (!mirror || frames == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        int16_t l = frames[2 * i];

        frames[2 * i] = frames[2 * i + 1];
        frames[2 * i + 1] = l;
    }
}

void ico_audio_set_mirror(int on)
{
    ico_opt_set_mirror(on);
}

int ico_audio_mirror(void)
{
    return ico_opt_mirror();
}
