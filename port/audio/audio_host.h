/*
 * port/audio/audio_host.h
 *
 * The audio half of the host loop (docs/port/AUDIO.md, "Output"): once per
 * simulated vsync the SPU2 renders the vsync's 48 kHz frames (960 at 50 Hz,
 * 800.8 on average at 59.94 Hz) and hands them to the sinks: the SDL3
 * device (window build, out_sdl.c), a WAV file (ini audio_dump=), or
 * nothing (headless).  The SNDN2DRV host keeps running in every mode, since
 * the game reads its envelopes and stream offsets, so a headless run is the
 * same with or without a sink.
 *
 * Called from port/platform/host_loop.c on the host context, in the same
 * thread as the game's fibers: render and the driver's register writes
 * never overlap.
 */
#ifndef ICO_PORT_AUDIO_AUDIO_HOST_H
#define ICO_PORT_AUDIO_AUDIO_HOST_H

#include <stdint.h>

/* Power the SPU2 and the driver on (ico_sndn2_host_register) and open the
   sinks the environment asks for:
     ICO_AUDIO_DUMP=PATH  write the mixed output to PATH as a WAV
                          (host_config.c sets it from the ini's audio_dump=)
     ICO_AUDIO=0          no audio device in the window build (ini audio=0)
   Idempotent. */
void ico_audio_host_init(void);

/* One vsync at `hz` (50 or 60): render and push. */
void ico_audio_host_vsync(int hz);

/* Frames the vsync after `vsync_index` vsyncs at `hz` renders (pure). */
int ico_audio_host_frames(int hz, unsigned int vsync_index);

/* Close the WAV and the device (also an atexit handler). */
void ico_audio_host_shutdown(void);

/* --- WAV writer (wav.c) --------------------------------------------------- */

typedef struct IcoWav IcoWav;

/* 16-bit stereo at `rate`; NULL if the file cannot be created. */
IcoWav *ico_wav_open(const char *path, int rate);
void ico_wav_write(IcoWav *w, const int16_t *frames, int count);
/* Patch the sizes and close; returns the frames written. */
uint64_t ico_wav_close(IcoWav *w);

/* --- SDL3 output (out_sdl.c, window build) --------------------------------- */

/* 0 when a 48 kHz stereo S16 device stream is open and playing. */
int ico_audio_sdl_open(void);
/* Queue one vsync of frames, keeping about two vsyncs buffered. */
void ico_audio_sdl_push(const int16_t *frames, int count);
void ico_audio_sdl_close(void);

#endif
