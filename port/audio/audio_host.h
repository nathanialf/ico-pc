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

/* Mirror mode: the option is port/game/options.c's (ico_opt_mirror).  While
   it is on ico_audio_host_vsync swaps left and right in each rendered block
   (renderer wave 7, R7c): the device and the WAV dump hear the mirrored
   pan; the SPU2 and the driver are untouched. */
void ico_audio_set_mirror(int on);
int ico_audio_mirror(void);
/* frames: `count` interleaved stereo S16 frames, swapped in place when
   mirror is non-zero (pure; the unit test's entry) */
void ico_audio_pan_mirror(int16_t *frames, int count, int mirror);

/* Output volume (volume.c; the SDL device only, a WAV dump stays unscaled).
   `volume` is 0.0 .. 1.0 (the ini's [audio] volume, exported as
   ICO_AUDIO_VOLUME and read at init); stored as 0..256 in 1/256 steps,
   clamped, and live: the next pushed block uses it. */
void ico_audio_set_volume(double volume);
int ico_audio_volume_q8(void);
/* dst[i] = clamp16(src[i] * q8 / 256), `count` samples (dst may equal src);
   integer math, q8 clamped to 0..256, 256 is a copy. */
void ico_audio_scale(int16_t *dst, const int16_t *src, int count, int q8);

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
