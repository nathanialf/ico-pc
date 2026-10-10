/*
 * port/platform/host_loop.c
 *
 * The host loop in simulated time (host_loop.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include "../audio/audio_host.h"
#include "../data/cdvd_host.h"
#include "../game/achievements.h"
#include "../include/ico_credits.h"
#include "../save/mc_host.h"
#include "arena.h"
#include "diag_host.h"
#include "fpenv.h"
#include "host_loop.h"
#include "clock.h"
#include "sched.h"
#include "trace_host.h"

/* The game's main (common/src/main.c, renamed by CMakeLists.txt) and its
   video mode word: systemStatus[0] is 0 for NTSC (60 Hz), 1 for PAL. */
int ico_game_main(void);
extern int systemStatus[];
/* the step profile's context (common/src/main.c) */
extern int stage_no;
extern int data_loading;
/* port/ui/gallery_play.c: the music gallery's engine */
void gallery_engine_install(void);
/* port/game/credits_live.c: the Extras credits' engine */
void ico_credits_engine_install(void);

static unsigned int vsyncs;

static int field;

static IcoStepProfile profile;

/* The EE's main thread starts at priority 0 and libkernl's InitThread
   (sce/libkernl/thread.c, run by _InitSys before main) sets it to 1. */
#define BOOT_PRIORITY 1

static void boot_main(void *arg)
{
    (void)arg;
    ico_game_main();
    /* main returned: crt0 would call Exit */
    fprintf(stderr, "ico_pc: the game's main returned\n");
    exit(0);
}

void ico_host_init(void)
{
    ico_fpenv_sim_enter();
    if (ico_arena_init() != 0) {
        fprintf(stderr, "ico_pc: cannot allocate the EE RAM arena\n");
        exit(1);
    }
    /* The base turns the object pointers in the renderer's draw keys (a
       flapping draw's key shifted right by 16) back into EE addresses, and
       so into the heap partition and the allocation they came from. */
    fprintf(stderr, "ico_pc: EE RAM arena at %p\n", (void *)ico_arena_base());
    ico_diag_name_func((void *)boot_main, "boot (the game's main)");
    /* the SPU2 and the SNDN2DRV host, before the game binds to it */
    ico_audio_host_init();
    gallery_engine_install();
    ico_credits_engine_install();
    /* the EE timers follow the video mode (50 or 60 Hz vsyncs) */
    ico_clock_set_mode_word(systemStatus);
    ico_sched_reset();
    ico_sched_set_fiber_start_hook(ico_fpenv_sim_enter);
    ico_sched_boot(boot_main, NULL, BOOT_PRIORITY);
    ico_sched_run();
}

static double ms_since(unsigned long long *t)
{
    const unsigned long long now = ico_diag_now_ns();
    const double ms = (double)(now - *t) / 1e6;

    *t = now;
    return ms;
}

void ico_host_step(void)
{
    /* the step's phases in real time (ico_host_step_profile) */
    const unsigned long long start = ico_diag_now_ns();
    unsigned long long t = start;
    const unsigned long switches = ico_sched_switch_count();
    IcoCdvdStats cd0, cd1;

    ico_cdvd_host_stats(&cd0);
    /* the simulation's FP mode again: the host's work between steps (the
       window, SDL, the GPU driver) runs in the host mode and may leave
       anything in MXCSR or FPCR, and the fiber switch does not save it
       (fiber.h) */
    ico_fpenv_sim_enter();
    ico_sched_vsync_advance();
    vsyncs++;
    ico_vsync(field);
    field ^= 1;
    ico_host_run_vsync_hooks();
    profile.hooksMs = ms_since(&t);
    /* this vsync's audio from the SPU2 state the last sound tick left; the
       sound thread the vsync woke ticks in ico_sched_run below, and its
       writes take effect from the next block */
    ico_audio_host_vsync(ico_host_vsync_hz());
    profile.audioMs = ms_since(&t);
    /* the audio push is an SDL call */
    ico_fpenv_sim_enter();
    ico_sched_run();
    profile.threadsMs = ms_since(&t);
    /* the port's achievements: once per new Main tick, after the threads
       have run; reads game state, writes none */
    ico_ach_host_poll(ico_host_main_ticks());
    /* the Extras credits' playback: nothing unless one is running; then it
       watches the stage and puts the game flags back at the title */
    ico_credits_host_poll();
    profile.achMs = ms_since(&t);
    profile.totalMs = (double)(t - start) / 1e6;
    profile.switches = ico_sched_switch_count() - switches;
    ico_cdvd_host_stats(&cd1);
    profile.cdReads = cd1.reads - cd0.reads;
    profile.cdSectors = cd1.sectors - cd0.sectors;
    profile.mcPending = ico_mc_host_pending();
    profile.stage = stage_no;
    profile.loading = data_loading;
}

void ico_host_step_profile(IcoStepProfile *out)
{
    *out = profile;
}

unsigned int ico_host_vsync_count(void)
{
    return vsyncs;
}

int ico_host_vsync_hz(void)
{
    return systemStatus[0] != 0 ? 50 : 60;
}
