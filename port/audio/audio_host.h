/*
 * port/audio/audio_host.h
 *
 * The audio half of the host loop, on one of two clocks (the mode below,
 * chosen once by ico_audio_host_init):
 *
 *   - the device's (the pull path, the window build's default): SDL's
 *     audio thread renders the SPU2 when the device asks for sound
 *     (out_sdl.c pull_cb), under the audio lock (audio_lock.h) that every
 *     entry point of the game's side takes; the driver's writes land at the
 *     next callback.  ico_audio_host_vsync renders nothing.
 *   - the game's (the sync path): once per simulated vsync
 *     ico_audio_host_vsync renders the vsync's 48 kHz frames (960 at 50 Hz,
 *     800.8 on average at 59.94 Hz) on the host context, in the same thread
 *     as the game's fibers, and hands them to the sinks: the SDL3 device
 *     (out_sdl.c's push), a WAV file (ini audio_dump=), or nothing
 *     (headless).
 *
 * The SNDN2DRV host keeps running in every mode, since the game reads its
 * envelopes and stream offsets, so a headless run is the same with or
 * without a sink.
 */
#ifndef ICO_PORT_AUDIO_AUDIO_HOST_H
#define ICO_PORT_AUDIO_AUDIO_HOST_H

#include <stddef.h>
#include <stdint.h>

/* Power the SPU2 and the driver on (ico_sndn2_host_register) and open the
   sinks the environment asks for:
     ICO_AUDIO_DUMP=PATH  write the mixed output to PATH as a WAV
                          (host_config.c sets it from the ini's audio_dump=)
     ICO_AUDIO=0          no audio device in the window build (ini audio=0)
     ICO_AUDIO_VOLUME, ICO_AUDIO_MUSIC, ICO_AUDIO_EFFECTS
                          [audio] volume, music, effects (0.0 .. 1.0;
                          the gains are mix_gain.h's)
     ICO_AUDIO_DEVICE     [audio] device, the SDL output's (out_sdl.c)
     ICO_AUDIO_SYNC=1     the game's clock ([dev] audio_sync, below)
   Idempotent. */
void ico_audio_host_init(void);

/* One vsync at `hz` (50 or 60).  On the sync path: render and push.  On
   the pull path: nothing rendered; the watchdog below runs.  Either way
   the voices-cut line (ico_audio_host_set_stage). */
void ico_audio_host_vsync(int hz);

/* --- The output's clock ------------------------------------------------------ */

/* ICO_AUDIO_MODE_PULL: the device's clock (out_sdl.c pull_cb renders).
   ICO_AUDIO_MODE_SYNC: the game's clock (ico_audio_host_vsync renders).
   SYNC in the headless build, with audio=0 (no device), with the WAV dump
   (the WAV is a function of the writes and their times only on the game's
   clock), in a run meant to repeat (the fixed clock, ICO_FIXED_CLOCK=1:
   a trace or headless=1; the driver's state the game reads follows the
   render's clock), with [dev] audio_sync (ICO_AUDIO_SYNC=1) and when no
   device opens; PULL otherwise.  On the pull path ico_audio_host_vsync falls back
   to SYNC for the rest of the run, logged, once the device has not asked
   for sound for 300 ms (2 s before its first request after an open or a
   resume) while the game stepped that long: the SPU2's time would stand
   still and the game's streams with it. */
enum { ICO_AUDIO_MODE_SYNC, ICO_AUDIO_MODE_PULL };

/* The choice (pure; volume.c): window_build: the SDL output is built;
   enable: ICO_AUDIO; dumping: a WAV dump is written; repeatable: the
   fixed clock is on (ICO_FIXED_CLOCK is "1"); sync: ICO_AUDIO_SYNC.
   *why (when not NULL): the reason for SYNC ("headless build", "audio=0",
   "audio_dump", "fixed clock", "[dev] audio_sync"), "" for PULL. */
int ico_audio_pick_mode(int window_build, const char *enable, int dumping, int repeatable,
                        const char *sync, const char **why);
/* The mode in use (SYNC before ico_audio_host_init). */
int ico_audio_mode(void);
/* For the start's summary line (window_host.c log_summary, which runs
   before ico_audio_host_init): "audio on the device's clock", "audio on
   the game's clock (audio_dump)", "audio off (audio=0)", ...: the mode in
   use, or before the init the one the environment asks for. */
const char *ico_audio_mode_text(void);

/* The pull path's render (audio_host.c: spu2_render and the mirror swap),
   run by the device's thread under the audio lock: `count` frames. */
typedef void (*IcoAudioRender)(int16_t *frames, int count);

/* Frames the vsync after `vsync_index` vsyncs at `hz` renders (pure). */
int ico_audio_host_frames(int hz, unsigned int vsync_index);

/* The stage running (the game's stage_no), named by the host loop before
   each ico_audio_host_vsync: the voices-cut line says it.  At most once a
   second ico_audio_host_vsync logs the voices the SPU2 cut short since the
   last look (spu2.h spu2_take_cuts; through the shared limiter):
     audio: 6 voices cut short at vsync 12345 (stage 3): a 2-sample release
     from level 7fff; loudest 0.18 of full scale (-15 dB)
   and the shutdown the run's count and loudest. */
void ico_audio_host_set_stage(int stage);

/* Mirror mode: the option is port/game/options.c's (ico_opt_mirror).  While
   it is on left and right are swapped in each rendered block (by
   ico_audio_host_vsync on the sync path, by the pull path's render):
   the device and the WAV dump hear the mirrored pan; the SPU2 and the
   driver are untouched. */
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
/* The ratio the sync path's push plays at (volume.c, pure; the pull path
   plays at the rate ratio alone): the window's rate ratio
   (ico_audio_set_rate_ratio), and ICO_AUDIO_TRIM faster while more than
   targetBytes of audio wait before a vsync's block is queued. The rate
   ratio is measured over 10 s, so what the simulation produced ahead of it
   (a 60.00 Hz screen: 0.1 % for the first 10 s, a vsync's worth over a
   long session of measuring errors) would stay queued as latency until
   the queue limit cut a block; the trim plays it out (a quarter of a
   percent: a vsync in about 7 s, a pitch change no one hears). */
#define ICO_AUDIO_TRIM 1.0025
double ico_audio_play_ratio(double rateRatio, int queuedBytes, int targetBytes);

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

/* 0 when a 48 kHz stereo S16 device stream is open and playing, on the
   device ICO_AUDIO_DEVICE names ([audio] device), or the default one when
   that is empty or names no device (logged once).  pull: the pull path's
   render (the stream gets the get-callback, an SDL mutex is installed as
   the audio lock, nothing is primed), or NULL for the sync path (the push
   below); sync_why: the sync path's reason, for the start line. */
int ico_audio_sdl_open(IcoAudioRender pull, const char *sync_why);
/* The sync path: queue one vsync of frames, keeping a cushion queued: two
   vsyncs, doubled after each dry queue up to 120 ms, halved again after
   20 s without one (out_sdl.c). */
void ico_audio_sdl_push(const int16_t *frames, int count);
/* Whether an output stream is open (0 without the SDL output). */
int ico_audio_sdl_active(void);
/* The audio queued on the stream in milliseconds, -1 when no stream is
   open; and the cushion the queue is kept at, 0 when none is.  The window
   (pace) reads them: below half the cushion the audio is about to run
   dry.  The pull path reports the lead its last callback left queued and a
   cushion of 0, which is never low (pace_policy.h pace_audio_skip). */
int ico_audio_sdl_queued_ms(void);
int ico_audio_sdl_cushion_ms(void);
void ico_audio_sdl_close(void);
/* The pull path's watchdog input: milliseconds since the device last asked
   for sound, or since the stream opened, moved or resumed; -1 on the sync
   path, while paused or without a stream.  *asked: whether it has asked
   since that open or resume. */
int ico_audio_sdl_pull_idle_ms(int *asked);
/* The pull path's end: the stream opened again on the same device without
   the callback, the audio lock removed; the sync path from here (the push
   and its cushion).  0 on success, -1 when no device opens. */
int ico_audio_sdl_fall_back(void);
/* The pull path's 10 s line (window_host.c pace_log calls the log one):
     audio: 1000 callbacks, 480000 frames; waited 0.21 ms at most for the
     driver (0 gave up), 0 callbacks late, 0 ms of silence; the driver ticked
     599 times, longest gap 17 ms (0 over 200 ms)
   the counts since the last line, the longest waits since then (zeroed);
   1 when written into out, 0 (nothing) on the sync path. */
int ico_audio_sdl_summary_text(char *out, size_t size);
void ico_audio_sdl_log_summary(void);

/* The output device ([audio] device).
   Without the SDL output (headless) these are stubs: no devices, reopen
   fails, removal does nothing. */
#define ICO_AUDIO_DEVICE_NAME_MAX 128
/* The playback devices' names (cut to fit), at most `max`; their count.
   0 when the output is not open. */
int ico_audio_sdl_devices(char names[][ICO_AUDIO_DEVICE_NAME_MAX], int max);
/* Close the stream and open it again on the device called `name` ("" or
   NULL: the system's default; a name no device has: the default, logged
   once), playing.  0 on success; -1 when the output is not open (audio=0,
   no SDL audio) or no device opens. */
int ico_audio_sdl_reopen(const char *name);
/* SDL_EVENT_AUDIO_DEVICE_REMOVED (window_host.c): when `which` is the
   device the stream plays on, the stream moves to the default device (the
   setting stays, for the next start).  The default device needs nothing:
   SDL follows the system's default itself. */
void ico_audio_sdl_device_removed(uint32_t which);
/* The Android lifecycle: 1 stops the output device (the app goes to the
   background), 0 starts it again with the stream's queue
   emptied, so no stale audio plays on return (the sync path primes its
   cushion; the pull path primes nothing and ramps in). Nothing without an
   open stream; a stub without the SDL output. */
void ico_audio_sdl_pause(int paused);
/* The rate the output plays the samples at, over 48 kHz: the window's
   vsync lock runs the simulation at the display's rate (0.1 % fast on a
   60.00 Hz screen under the 59.94 Hz game), so the output plays that much
   faster: on the sync path its queue neither grows (dropped blocks) nor
   runs dry; on the pull path the SPU2's time advances with the game's.
   1.0 is normal; kept across a device reopen.  On every SDL output (the
   desktop and Android share out_sdl.c); nothing without it. */
void ico_audio_set_rate_ratio(double ratio);

#endif
