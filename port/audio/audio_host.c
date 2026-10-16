/*
 * port/audio/audio_host.c
 *
 * The per-vsync SPU2 render and its sinks (audio_host.h).
 */
#include "audio_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_lock.h"
#include "diag_host.h"
#include "mix_gain.h"
#include "options.h"
#include "sndn2_host.h"
#include "spu2.h"
#include "stream_watch.h"
#if defined(__ANDROID__) || defined(ICO_IOS)
#include "host_loop.h"
#endif

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

/* the voices cut short (spu2.h spu2_take_cuts): the stage the host loop
   last named (ico_audio_host_set_stage) and the vsync of the last look */
static int stage_now;
static unsigned int cuts_seen_at;

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

void ico_audio_host_set_stage(int stage)
{
    stage_now = stage;
}

/* The samples a release at rate 0 takes from `level` to 0 (spu2.c
   env_tick): a linear one steps -0x4000 a frame, an exponential one
   (ADSR2 bit 5) halves the level, rounded down, each frame. */
static int cut_samples(uint32_t level, uint16_t adsr2)
{
    int n = 0;

    if ((adsr2 & 0x20u) == 0) {
        return (int)((level + 0x3FFFu) / 0x4000u);
    }
    while (level > 0) {
        level >>= 1;
        n++;
    }
    return n;
}

/* A peak in 0..32768 as a fraction of full scale and in dB, for the cut
   lines. */
static void cut_peak_text(char *out, size_t size, uint32_t peak)
{
    if (peak == 0) {
        snprintf(out, size, "silent at the cut");
        return;
    }
    snprintf(out, size, "loudest %.2f of full scale (%.0f dB)", peak / 32768.0,
             20.0 * log10(peak / 32768.0));
}

/* At most once a second: the voices the SPU2 cut short since the last
   look (a key-off at release rate 0, the game's stop of its sounds at a
   stage change), one line through the shared limiter with how many, the
   loudest one's release and its level of full scale: what the click a
   player hears there measures. */
static void cut_line(int hz)
{
    spu2_stats w;
    char peak[64];

    if (vsync_index - cuts_seen_at < (unsigned int)hz) {
        return;
    }
    cuts_seen_at = vsync_index;
    ico_audio_lock(); /* the render writes them (audio_lock.h) */
    spu2_take_cuts(&w);
    ico_audio_unlock();
    if (w.cuts == 0) {
        return;
    }
    cut_peak_text(peak, sizeof(peak), w.cut_peak);
    ico_log_limited("audio: voices cut short",
                    "audio: %llu voice%s cut short at vsync %u (stage %d): a %d-sample release "
                    "from level %04x; %s",
                    (unsigned long long)w.cuts, w.cuts == 1 ? "" : "s", vsync_index, stage_now,
                    cut_samples(w.cut_level, w.cut_adsr2), (unsigned)w.cut_level, peak);
}

void ico_audio_host_shutdown(void)
{
    spu2_stats st;
    IcoAudioStreamStats ss;
    static int streams_said; /* the shutdown can run twice (atexit) */

#ifdef ICO_AUDIO_SDL
    /* the device first: after it no thread renders, so the SPU2's
       statistics below are read alone */
    if (sdl_open) {
        ico_audio_sdl_close();
        sdl_open = 0;
    }
#endif
    ico_audio_stream_stats(&ss);
    if (!streams_said && ss.opened + ss.keyoffs + ss.pages_full > 0) {
        streams_said = 1;
        fprintf(stderr,
                "audio: streams: %u opened, %u of them on a full packet page; %u key-offs from "
                "the sequencer on stream voices; %u packet pages full (%u packets carried, %u of "
                "them a stream's, %u dropped)\n",
                ss.opened, ss.opens_on_full, ss.keyoffs, ss.pages_full, ss.carried,
                ss.stream_carried, ss.dropped);
    }
    spu2_get_stats(&st);
    if (st.chunked_frames + st.exact_frames > 0) {
        fprintf(stderr,
                "audio: SPU2 rendered %llu frames voice by voice in %llu chunks and %llu frame "
                "by frame (%llu hazards)\n",
                (unsigned long long)st.chunked_frames, (unsigned long long)st.chunks,
                (unsigned long long)st.exact_frames, (unsigned long long)st.hazards);
    }
    if (st.cuts > 0) {
        char peak[64];

        cut_peak_text(peak, sizeof(peak), st.cut_peak);
        fprintf(stderr, "audio: %llu voices cut short over the run (a release at rate 0); %s\n",
                (unsigned long long)st.cuts, peak);
    }
    if (wav != NULL) {
        uint64_t frames = ico_wav_close(wav);

        wav = NULL;
        fprintf(stderr,
                "audio: dump closed: %llu frames (%.1f s), peak %d, first sound at vsync %lld\n",
                (unsigned long long)frames, (double)frames / SPU2_RATE, dump_peak,
                first_sound_vsync);
    }
}

void ico_audio_host_init(void)
{
    const char *dump = getenv("ICO_AUDIO_DUMP");
    const char *enable = getenv("ICO_AUDIO");
    const char *vol = getenv("ICO_AUDIO_VOLUME");
    const char *music = getenv("ICO_AUDIO_MUSIC");
    const char *effects = getenv("ICO_AUDIO_EFFECTS");

    if (started) {
        return;
    }
    started = 1;
    if (vol != NULL && vol[0] != '\0') {
        ico_audio_set_volume(strtod(vol, NULL));
    }
    /* [audio] music and effects (mix_gain.h), handed over by host_config.c */
    if (music != NULL && music[0] != '\0') {
        ico_audio_set_gain(ICO_AUDIO_CAT_MUSIC, strtod(music, NULL));
    }
    if (effects != NULL && effects[0] != '\0') {
        ico_audio_set_gain(ICO_AUDIO_CAT_EFFECTS, strtod(effects, NULL));
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
#if defined(__ANDROID__) || defined(ICO_IOS)
    /* Mobile: Quit returns from ico_host_main, which runs this */
    ico_host_at_shutdown(ico_audio_host_shutdown);
#else
    atexit(ico_audio_host_shutdown);
#endif
}

void ico_audio_host_vsync(int hz)
{
    int frames = ico_audio_host_frames(hz, vsync_index);

    if (!started) {
        ico_audio_host_init();
    }
    spu2_render(out, frames);
    /* mirror mode: left and right swapped where the stereo output is
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
    cut_line(hz);
    vsync_index++;
}

#ifndef ICO_AUDIO_SDL
int ico_audio_sdl_active(void)
{
    return 0;
}

int ico_audio_sdl_queued_ms(void)
{
    return -1;
}

int ico_audio_sdl_cushion_ms(void)
{
    return 0;
}

/* No SDL output (the headless build): no devices to choose from
   (audio_host.h). */
int ico_audio_sdl_devices(char names[][ICO_AUDIO_DEVICE_NAME_MAX], int max)
{
    (void)names;
    (void)max;
    return 0;
}

int ico_audio_sdl_reopen(const char *name)
{
    (void)name;
    return -1;
}

void ico_audio_sdl_device_removed(uint32_t which)
{
    (void)which;
}

void ico_audio_sdl_pause(int paused)
{
    (void)paused;
}

void ico_audio_set_rate_ratio(double ratio)
{
    (void)ratio;
}
#endif

/* Left and right swapped in place (mirror mode, audio_host.h). */
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
