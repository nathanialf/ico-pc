/*
 * port/audio/spu2_sd.h
 *
 * A libsd-shaped front end on the software SPU2: the calls SNDN2DRV.IRX
 * makes (docs/research/sndn2drv.md, "Imports" and recommendation 2), with
 * libsd's argument encodings, turned into SPU2 register writes.  Phase 4B's
 * sndn2 host drives the SPU2 through these.
 *
 * The encodings (SD_VPARAM_*, SD_PARAM_*, SD_SWITCH_*, SD_ADDR_*,
 * SD_CORE_*, the effect modes) are libsd's public ABI as ps2sdk's
 * common/include/libsd-common.h spells it; the values are restated here
 * so the port does not depend on the ps2sdk headers.  Behaviour follows
 * R1 where it read the IRX and ps2sdk's clean-room libsd (freesd.c,
 * effect.c, voice.c) elsewhere; docs/port/AUDIO.md lists which is which
 * and what is still to check against the disc's LIBSD.IRX.
 *
 * Every write is stamped with the time set by spu2_sd_set_time() (an SPU2
 * frame number, spu2_time() by default when it is behind), so a host that
 * runs one IOP tick per block of frames gets block-accurate timing and one
 * that stamps finer gets sample-accurate timing.
 */
#ifndef ICO_PORT_AUDIO_SPU2_SD_H
#define ICO_PORT_AUDIO_SPU2_SD_H

#include <stdint.h>

/* Voice selector: core | voice << 1 (libsd SD_VOICE). */
#define SPU2_SD_VOICE(core, v) ((core) | ((v) << 1))

#define SPU2_SD_VPARAM_VOLL (0x00 << 8)
#define SPU2_SD_VPARAM_VOLR (0x01 << 8)
#define SPU2_SD_VPARAM_PITCH (0x02 << 8)
#define SPU2_SD_VPARAM_ADSR1 (0x03 << 8)
#define SPU2_SD_VPARAM_ADSR2 (0x04 << 8)
#define SPU2_SD_VPARAM_ENVX (0x05 << 8)
#define SPU2_SD_VPARAM_VOLXL (0x06 << 8)
#define SPU2_SD_VPARAM_VOLXR (0x07 << 8)
#define SPU2_SD_PARAM_MMIX (0x08 << 8)
#define SPU2_SD_PARAM_MVOLL ((0x09 << 8) | 0x80)
#define SPU2_SD_PARAM_MVOLR ((0x0A << 8) | 0x80)
#define SPU2_SD_PARAM_EVOLL ((0x0B << 8) | 0x80)
#define SPU2_SD_PARAM_EVOLR ((0x0C << 8) | 0x80)
#define SPU2_SD_PARAM_AVOLL ((0x0D << 8) | 0x80)
#define SPU2_SD_PARAM_AVOLR ((0x0E << 8) | 0x80)
#define SPU2_SD_PARAM_BVOLL ((0x0F << 8) | 0x80)
#define SPU2_SD_PARAM_BVOLR ((0x10 << 8) | 0x80)
#define SPU2_SD_PARAM_MVOLXL ((0x11 << 8) | 0x80)
#define SPU2_SD_PARAM_MVOLXR ((0x12 << 8) | 0x80)

#define SPU2_SD_SWITCH_PMON (0x13 << 8)
#define SPU2_SD_SWITCH_NON (0x14 << 8)
#define SPU2_SD_SWITCH_KON (0x15 << 8)
#define SPU2_SD_SWITCH_KOFF (0x16 << 8)
#define SPU2_SD_SWITCH_ENDX (0x17 << 8)
#define SPU2_SD_SWITCH_VMIXL (0x18 << 8)
#define SPU2_SD_SWITCH_VMIXEL (0x19 << 8)
#define SPU2_SD_SWITCH_VMIXR (0x1A << 8)
#define SPU2_SD_SWITCH_VMIXER (0x1B << 8)

/* Addresses are byte addresses.  Sony's IRX sets 0x40 on the voice
   address selectors (R1, command 0x03); it is ignored. */
#define SPU2_SD_ADDR_ESA (0x1C << 8)
#define SPU2_SD_ADDR_EEA (0x1D << 8)
#define SPU2_SD_ADDR_TSA (0x1E << 8)
#define SPU2_SD_ADDR_IRQA (0x1F << 8)
#define SPU2_SD_VADDR_SSA (0x20 << 8)
#define SPU2_SD_VADDR_LSAX (0x21 << 8)
#define SPU2_SD_VADDR_NAX (0x22 << 8)

#define SPU2_SD_CORE_EFFECT_ENABLE 0x2
#define SPU2_SD_CORE_IRQ_ENABLE 0x4
#define SPU2_SD_CORE_MUTE_ENABLE 0x6
#define SPU2_SD_CORE_NOISE_CLK 0x8
#define SPU2_SD_CORE_SPDIF_MODE 0xA

#define SPU2_SD_TRANS_WRITE 0
#define SPU2_SD_TRANS_READ 1

#define SPU2_SD_EFFECT_MODE_OFF 0
#define SPU2_SD_EFFECT_MODE_STUDIO_3 4
#define SPU2_SD_EFFECT_MODE_PIPE 9
#define SPU2_SD_EFFECT_MODE_CLEAR 0x100

/* libsd's sceSdEffectAttr. */
typedef struct spu2_sd_effect_attr {
    int core;
    int mode;
    short depth_L;
    short depth_R;
    int delay;
    int feedback;
} spu2_sd_effect_attr;

/* The time stamp for the calls that follow (never behind spu2_time()). */
void spu2_sd_set_time(uint64_t time);

/* sceSdInit: hot = 0 full initialisation, 1 keeps the master/effect
   volumes and the effect end addresses. */
void spu2_sd_init(int hot);

void spu2_sd_set_param(uint16_t entry, uint16_t value);
uint16_t spu2_sd_get_param(uint16_t entry);
void spu2_sd_set_switch(uint16_t entry, uint32_t value);
uint32_t spu2_sd_get_switch(uint16_t entry);
void spu2_sd_set_addr(uint16_t entry, uint32_t value);
uint32_t spu2_sd_get_addr(uint16_t entry);
void spu2_sd_set_core_attr(uint16_t entry, uint16_t value);
uint16_t spu2_sd_get_core_attr(uint16_t entry);

/* sceSdSetEffectAttr: loads the mode's preset (spu2_reverb_get_preset),
   sets EVOL to the depths and ESA from the core's EEA.  Returns -1 for a
   mode above 9.  delay and feedback are ignored (DIVERGENCES.md A9). */
int spu2_sd_set_effect_attr(int core, const spu2_sd_effect_attr *attr);
void spu2_sd_get_effect_attr(int core, spu2_sd_effect_attr *attr);

/* sceSdVoiceTrans on channel `chan`: mode bit 0 = read; `buf` host memory
   (the IOP buffer), `spu` the byte address.  Returns `size`, or -1 while
   the channel is busy. */
int spu2_sd_voice_trans(int chan, uint16_t mode, void *buf, uint32_t spu, uint32_t size);

/* sceSdVoiceTransStatus: flag 0 polls (1 idle, 0 busy); flag 1 waits,
   which here finishes the transfer at once (spu2_voice_trans_wait). */
int spu2_sd_voice_trans_status(int chan, int flag);

#endif
