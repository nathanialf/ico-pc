/*
 * port/audio/audio_lock.h
 *
 * The one lock over the SPU2 and the sound driver's state.  With the
 * output on the device's clock (out_sdl.c, the pull path) SDL's audio
 * thread renders the SPU2 while the game's sound thread drives it through
 * the driver (sndn2_host.c ico_sndn2_host_serve), so every entry point the
 * game, the menus or the movie player reach the SPU2 or the driver's state
 * through takes this lock: the serve, the driver's reset and register, the
 * music and effects gains (mix_gain.c), the music gallery's reads
 * (port/ui/gallery_play.c), the movie's PCM writes into IOP RAM
 * (port/fmv/movie.c iop_write) and the host's once-a-second look at the
 * cut voices (audio_host.c).  Nothing under it switches fibers or calls
 * SDL's stream functions, so it is a leaf; the audio thread takes it inside
 * SDL's stream lock (out_sdl.c pull_cb), never the other way round.
 *
 * Installed by the SDL output (out_sdl.c ico_audio_sdl_open: SDL's mutex,
 * which is recursive, so a nested take is legal); until then, in the
 * headless build and in every unit test that installs none, both calls do
 * nothing, and everything runs on the one simulation thread as before.
 */
#ifndef ICO_PORT_AUDIO_AUDIO_LOCK_H
#define ICO_PORT_AUDIO_AUDIO_LOCK_H

typedef void (*IcoAudioLockFn)(void *user);

/* Take and release the lock; nothing when none is installed. */
void ico_audio_lock(void);
void ico_audio_unlock(void);
/* Install the lock's functions (both NULL: none).  On the main thread,
   with no audio thread running and the lock not held. */
void ico_audio_lock_install(IcoAudioLockFn lock, IcoAudioLockFn unlock, void *user);

#endif
