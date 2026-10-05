/*
 * port/audio/spu2_internal.h
 *
 * Tables shared between spu2.c and spu2_tables.c (and the tests).
 */
#ifndef ICO_PORT_AUDIO_SPU2_INTERNAL_H
#define ICO_PORT_AUDIO_SPU2_INTERNAL_H

#include "spu2.h"

extern const int16_t spu2_gauss[512];
extern const int16_t spu2_reverb_fir[39];
extern const spu2_reverb_preset spu2_reverb_presets[SPU2_REVERB_MODES];

/* One step of the SPU envelope generator (ADSR phases and volume sweeps),
   psx-spx "Envelope Operation depending on Shift/Step/Mode/Direction".
   rate = shift << 2 | step (7 bits); never_step is the "all rate bits set"
   case, which neither steps nor saturates.  Exposed for the tests. */
void spu2_env_tick(int32_t *level, uint32_t *counter, int rate, int exponential, int decrease,
                   int negative, int never_step);

/* The most recent value queued for a register below 0x400 (0 before any
   write), whether or not it has been applied yet. */
uint16_t spu2_shadow(int core, unsigned reg);
/* 1: render every frame with the reference frame-by-frame renderer, never
   the chunked one (spu2.c, "Chunked rendering"); for the tests that
   compare the two. */
void spu2_set_exact(int on);

/* Stage profile of spu2_render (test/spu2_bench.c's spu2_prof target, built
   with SPU2_PROFILE; compiled out otherwise).  SPU2_LAP(stage) charges the
   time since the previous lap to `stage`; spu2_bench subtracts the cost of
   a lap (one clock read) per lap. */
enum {
    SPU2_PROF_EVENTS,   /* queued writes, transfers, callbacks */
    SPU2_PROF_DECODE,   /* ADPCM block decode (and the IRQ check) */
    SPU2_PROF_INTERP,   /* Gaussian interpolation or noise */
    SPU2_PROF_ENVELOPE, /* ADSR and volume sweeps */
    SPU2_PROF_MIX,      /* envelope x volume into the buses */
    SPU2_PROF_PITCH,    /* pitch counter, sample feed */
    SPU2_PROF_VOICE,    /* the chunked voice loop: interp to pitch in one */
    SPU2_PROF_BUS,      /* bus clamp, MMIX, external and AutoDMA inputs */
    SPU2_PROF_REVERB,   /* resampling filters and the reverb unit */
    SPU2_PROF_OUTPUT,   /* EVOL, master volume, write-backs, output */
    SPU2_PROF_STAGES
};

#ifdef SPU2_PROFILE
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct spu2_prof_t {
    uint64_t ns[SPU2_PROF_STAGES];
    uint64_t laps[SPU2_PROF_STAGES];
    uint64_t last;
    uint64_t vsyncs;
} spu2_prof_t;

extern spu2_prof_t spu2_prof;

/* nanoseconds: clock_gettime(CLOCK_MONOTONIC), QueryPerformanceCounter on
   Windows */
static inline uint64_t spu2_prof_clock(void)
{
#ifdef _WIN32
    LARGE_INTEGER c;
    LARGE_INTEGER f;

    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

#define SPU2_LAP(stage)                                                                            \
    do {                                                                                           \
        uint64_t spu2_t_ = spu2_prof_clock();                                                      \
        spu2_prof.ns[stage] += spu2_t_ - spu2_prof.last;                                           \
        spu2_prof.laps[stage]++;                                                                   \
        spu2_prof.last = spu2_t_;                                                                  \
    } while (0)

void spu2_prof_reset(void);
double spu2_prof_lap_cost(void);
const char *spu2_prof_stage_name(int stage);

#else
#define SPU2_LAP(stage) ((void)0)
#endif
#endif
