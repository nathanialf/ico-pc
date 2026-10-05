/*
 * port/platform/host_loop.c
 *
 * The host loop in simulated time (host_loop.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include "../audio/audio_host.h"
#include "arena.h"
#include "diag_host.h"
#include "fpenv.h"
#include "host_loop.h"
#include "sched.h"

/* The game's main (common/src/main.c, renamed by CMakeLists.txt) and its
   video mode word: systemStatus[0] is 0 for NTSC (60 Hz), 1 for PAL. */
int ico_game_main(void);
extern int systemStatus[];

static unsigned int vsyncs;

static int field;

static unsigned long long time_us;

/* The EE's main thread starts at priority 0 and libkernl's InitThread
   (sce/libkernl/thread.c, run by _InitSys before main) sets it to 1. */
#define BOOT_PRIORITY 1

static void boot_main(void *arg)
{
    (void)arg;
    ico_game_main();
    /* main returned: crt0 would call Exit */
    fprintf(stderr, "host: the game's main returned\n");
    exit(0);
}

void ico_host_init(void)
{
    ico_fpenv_sim_enter();
    if (ico_arena_init() != 0) {
        fprintf(stderr, "host: cannot allocate the EE RAM arena\n");
        exit(1);
    }
    ico_diag_name_func((void *)boot_main, "boot (the game's main)");
    /* the SPU2 and the SNDN2DRV host, before the game binds to it */
    ico_audio_host_init();
    ico_sched_reset();
    ico_sched_set_fiber_start_hook(ico_fpenv_sim_enter);
    ico_sched_boot(boot_main, NULL, BOOT_PRIORITY);
    ico_sched_run();
}

void ico_host_step(void)
{
    ico_sched_vsync_advance();
    vsyncs++;
    time_us += ico_host_vsync_hz() == 50 ? 20000u : 16683u; /* NTSC: 59.94 Hz */
    ico_vsync(field);
    field ^= 1;
    ico_host_run_vsync_hooks();
    /* this vsync's audio from the SPU2 state the last sound tick left; the
       sound thread the vsync woke ticks in ico_sched_run below, and its
       writes take effect from the next block (docs/port/AUDIO.md) */
    ico_audio_host_vsync(ico_host_vsync_hz());
    ico_sched_run();
}

unsigned int ico_host_vsync_count(void)
{
    return vsyncs;
}

int ico_host_vsync_hz(void)
{
    return systemStatus[0] != 0 ? 50 : 60;
}

unsigned long long ico_host_time_us(void)
{
    return time_us;
}
