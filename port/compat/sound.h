/*
 * port/compat/sound.h
 *
 * The host build's sound.h: the Sg sequencer's own header,
 * sce/libsndn2/sound.h (the decomp's, MIT), in its host view (pointer-sized
 * and 64-bit types host-correct, the SgSetSePitchDirect prototype the EE
 * header leaves out).  The sequencer itself, sce/libsndn2/sound.c, is in
 * the host build since Phase 4B (docs/port/AUDIO.md).
 */
#ifndef ICO_COMPAT_SOUND_H
#define ICO_COMPAT_SOUND_H

#ifndef ICO_SG_HOST_HEADER
#define ICO_SG_HOST_HEADER 1
#endif
#include "../../sce/libsndn2/sound.h"

#endif /* ICO_COMPAT_SOUND_H */
