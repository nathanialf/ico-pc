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

#endif
