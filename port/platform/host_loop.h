/*
 * port/platform/host_loop.h
 *
 * The host's drive of the simulation, in simulated time: boot the game, then
 * one call per vsync. No real-time pacing (the presentation layer adds it).
 *
 *   ico_host_init();            boot: the game's main() and boot() on the
 *                               boot fiber, until every thread waits
 *   for (;;) ico_host_step();   one vsync each: the vblank interrupt, the
 *                               on-vsync callbacks, then every thread that
 *                               became ready runs until all wait again
 *
 * Main ticks once every systemStatus[1] vsyncs, as on the PS2: the game's
 * scheduler thread (common/src/main.c) counts the vsyncs and wakes it.
 */
#ifndef ICO_PLATFORM_HOST_LOOP_H
#define ICO_PLATFORM_HOST_LOOP_H

/* Puts the FPU in simulation mode, allocates the EE RAM arena and runs the
   game's main() on the boot fiber until it and its threads wait. */
void ico_host_init(void);
/* One simulated vsync (field parity alternating from 0), then the threads
   to quiescence. */
void ico_host_step(void);
/* Vsyncs simulated so far. */
unsigned int ico_host_vsync_count(void);
/* The simulated vsync rate: 50 (PAL, systemStatus[0] != 0) or 60. */
int ico_host_vsync_hz(void);
/* Simulated time in microseconds (the sum of each vsync's period). */
unsigned long long ico_host_time_us(void);

/* Package Q1: where the last ico_host_step's real time went, for the window
   build's slow-step lines (window_host.c), in ms: the vsync callbacks (the
   disc's reads complete there), the audio block (the SPU2 mix and the SDL
   push), the game's threads, the achievements' poll (and its stats file
   write every 750 ticks while its stats change); with the fiber switches,
   the disc reads and sectors started in the step, whether a memory card
   command is pending (a save or load in progress), the stage and the
   game's data_loading flag. */
typedef struct IcoStepProfile {
    double totalMs, hooksMs, audioMs, threadsMs, achMs;
    unsigned long switches;
    unsigned int cdReads, cdSectors;
    int mcPending;
    int stage, loading;
} IcoStepProfile;

void ico_host_step_profile(IcoStepProfile *out);

/* Callbacks run at every simulated vsync, on the host context as interrupt
   code (wake threads with the i-calls: iWakeupThread, iSignalSema), after
   the vblank interrupt's handlers and before the threads run. Disc I/O
   (package 1C) completes from here, so a request issued during one tick
   completes at the next vsync. Returns 0, or -1 when the table is full. */
int ico_host_on_vsync_register(void (*fn)(void *user), void *user);
void ico_host_on_vsync_unregister(void (*fn)(void *user), void *user);
/* Runs the callbacks as interrupt code (ico_host_step does, after
   ico_vsync). The callback list lives in vsync_hooks.c, which needs no game
   symbols, so device code and its tests can link it alone. */
void ico_host_run_vsync_hooks(void);

/* The game's side (common/src/main.c): the vblank-start interrupt with the
   GS's current field (0 or 1, GS_CSR.FIELD). */
void ico_vsync(int field_parity);

#endif /* ICO_PLATFORM_HOST_LOOP_H */
