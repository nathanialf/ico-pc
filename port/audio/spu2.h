/*
 * port/audio/spu2.h
 *
 * A software SPU2: the PS2's sound processor (two cores of 24 ADPCM voices
 * over 2 MB of sound RAM, a reverb unit per core, core 0 mixed into core 1)
 * rendered as 48 kHz interleaved stereo 16-bit.  Clean-room: written from
 * public hardware notes (psx-spx for the voice, envelope, interpolation and
 * reverb units, which the SPU2 cores share with the PS1 SPU; the ps2sdk
 * register layout and libsd encodings for the SPU2 register map).  No
 * emulator code.
 *
 * Units:
 *   - "SPU addresses" (SSA, LSAX, NAX, ESA, IRQA, TSA and the reverb
 *     offsets) count 16-bit halfwords, 20 bits wide (0..0xFFFFF), as the
 *     SPU2 registers do.  Byte address = SPU address * 2.
 *   - Time is the output sample counter at 48 kHz (spu2_time()), starting at
 *     0 on spu2_reset().
 *
 * Register access is by the SPU2 register offset within a core (the layout
 * of ps2sdk's spu2_mmio_hwport.h, core 0 numbering), so a voice register is
 * SPU2_R_VP(voice) + SPU2_VP_xxx.  The 0x760 block (master, reverb-depth,
 * input volumes and the reverb coefficients) is addressed with the core 0
 * offsets for both cores; spu2_write_reg's `core` selects the bank.
 *
 * Determinism: integer arithmetic only (right shifts of negative values are
 * arithmetic on every supported compiler); the same writes at the same
 * times render the same bits on every host.
 *
 * Threading: none.  The caller serialises every call; callbacks run inside
 * spu2_render() and may call spu2_write_reg() and the transfer functions
 * (their effects then start at the next sample).
 */
#ifndef ICO_PORT_AUDIO_SPU2_H
#define ICO_PORT_AUDIO_SPU2_H

#include <stdint.h>

#define SPU2_RAM_SIZE (2u * 1024u * 1024u)
#define SPU2_ADDR_MASK 0xFFFFFu /* halfword addresses */
#define SPU2_RATE 48000
#define SPU2_CORES 2
#define SPU2_VOICES 24

/* --- Register offsets (bytes, within a core) ------------------------------ */

/* Voice parameters: SPU2_R_VP(v) + SPU2_VP_*. */
#define SPU2_R_VP(v) ((unsigned)(v) * 0x10u)
#define SPU2_VP_VOLL 0x0
#define SPU2_VP_VOLR 0x2
#define SPU2_VP_PITCH 0x4
#define SPU2_VP_ADSR1 0x6
#define SPU2_VP_ADSR2 0x8
#define SPU2_VP_ENVX 0xA
#define SPU2_VP_VOLXL 0xC
#define SPU2_VP_VOLXR 0xE

/* Voice switches: two halfwords each, the first voices 0-15, the second
   voices 16-23. */
#define SPU2_R_PMON 0x180
#define SPU2_R_NON 0x184
#define SPU2_R_VMIXL 0x188
#define SPU2_R_VMIXEL 0x18C
#define SPU2_R_VMIXR 0x190
#define SPU2_R_VMIXER 0x194
#define SPU2_R_MMIX 0x198
#define SPU2_R_ATTR 0x19A
#define SPU2_R_IRQA 0x19C /* address pair: +0 bits 16-19, +2 bits 0-15 */
#define SPU2_R_KON 0x1A0
#define SPU2_R_KOFF 0x1A4
#define SPU2_R_TSA 0x1A8 /* address pair */
#define SPU2_R_DATA 0x1AC
#define SPU2_R_ADMAS 0x1B0

/* Voice addresses: SPU2_R_VA(v) + SPU2_VA_*, each an address pair. */
#define SPU2_R_VA(v) (0x1C0u + (unsigned)(v) * 0xCu)
#define SPU2_VA_SSA 0x0
#define SPU2_VA_LSAX 0x4
#define SPU2_VA_NAX 0x8

/* Reverb work area start and the reverb address registers (pairs). */
#define SPU2_R_ESA 0x2E0
#define SPU2_R_APF1_SIZE 0x2E4
#define SPU2_R_APF2_SIZE 0x2E8
#define SPU2_R_SAME_L_DST 0x2EC
#define SPU2_R_SAME_R_DST 0x2F0
#define SPU2_R_COMB1_L_SRC 0x2F4
#define SPU2_R_COMB1_R_SRC 0x2F8
#define SPU2_R_COMB2_L_SRC 0x2FC
#define SPU2_R_COMB2_R_SRC 0x300
#define SPU2_R_SAME_L_SRC 0x304
#define SPU2_R_SAME_R_SRC 0x308
#define SPU2_R_DIFF_L_DST 0x30C
#define SPU2_R_DIFF_R_DST 0x310
#define SPU2_R_COMB3_L_SRC 0x314
#define SPU2_R_COMB3_R_SRC 0x318
#define SPU2_R_COMB4_L_SRC 0x31C
#define SPU2_R_COMB4_R_SRC 0x320
#define SPU2_R_DIFF_L_SRC 0x324
#define SPU2_R_DIFF_R_SRC 0x328
#define SPU2_R_APF1_L_DST 0x32C
#define SPU2_R_APF1_R_DST 0x330
#define SPU2_R_APF2_L_DST 0x334
#define SPU2_R_APF2_R_DST 0x338
#define SPU2_R_EEA 0x33C  /* one halfword: bits 16-19 of the end address */
#define SPU2_R_ENDX 0x340 /* pair of voice masks */
#define SPU2_R_STATX 0x344

/* The 0x760 block (core 0 offsets; core 1's bank is selected by `core`). */
#define SPU2_R_MVOLL 0x760
#define SPU2_R_MVOLR 0x762
#define SPU2_R_EVOLL 0x764
#define SPU2_R_EVOLR 0x766
#define SPU2_R_AVOLL 0x768 /* external input (core 1: core 0's output) */
#define SPU2_R_AVOLR 0x76A
#define SPU2_R_BVOLL 0x76C /* sound data input (AutoDMA) */
#define SPU2_R_BVOLR 0x76E
#define SPU2_R_MVOLXL 0x770
#define SPU2_R_MVOLXR 0x772
#define SPU2_R_IIR_VOL 0x774
#define SPU2_R_COMB1_VOL 0x776
#define SPU2_R_COMB2_VOL 0x778
#define SPU2_R_COMB3_VOL 0x77A
#define SPU2_R_COMB4_VOL 0x77C
#define SPU2_R_WALL_VOL 0x77E
#define SPU2_R_APF1_VOL 0x780
#define SPU2_R_APF2_VOL 0x782
#define SPU2_R_IN_COEF_L 0x784
#define SPU2_R_IN_COEF_R 0x786

/* ATTR bits used. */
#define SPU2_ATTR_EFFECT 0x0080 /* reverb buffer writes enabled */
#define SPU2_ATTR_IRQ 0x0040    /* IRQ enable */
#define SPU2_ATTR_NOISE_SHIFT 8 /* bits 8-13: noise clock */

/* MMIX bits. */
#define SPU2_MMIX_SIN_WET_R 0x001
#define SPU2_MMIX_SIN_WET_L 0x002
#define SPU2_MMIX_SIN_DRY_R 0x004
#define SPU2_MMIX_SIN_DRY_L 0x008
#define SPU2_MMIX_MEMIN_WET_R 0x010
#define SPU2_MMIX_MEMIN_WET_L 0x020
#define SPU2_MMIX_MEMIN_DRY_R 0x040
#define SPU2_MMIX_MEMIN_DRY_L 0x080
#define SPU2_MMIX_VOICE_WET_R 0x100
#define SPU2_MMIX_VOICE_WET_L 0x200
#define SPU2_MMIX_VOICE_DRY_R 0x400
#define SPU2_MMIX_VOICE_DRY_L 0x800

/* --- Reverb presets -------------------------------------------------------- */

/* One libsd effect mode: its work area size in bytes and the 32 register
   values in the PS1 order psx-spx lists them (dAPF1 dAPF2 vIIR vCOMB1-4
   vWALL vAPF1 vAPF2 mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2 dLSAME
   dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 dLDIFF dRDIFF mLAPF1
   mRAPF1 mLAPF2 mRAPF2 vLIN vRIN; offsets in 8-byte units). */
typedef struct spu2_reverb_preset {
    uint32_t size;
    uint16_t regs[32];
} spu2_reverb_preset;

#define SPU2_REVERB_MODES 10 /* libsd SD_EFFECT_MODE_OFF .. _PIPE */

/* The preset of a libsd mode (0..9), or NULL.  The built-in values are the
   psx-spx ones; spu2_reverb_set_preset replaces one, e.g. with
   the table read from the user's LIBSD.IRX. */
const spu2_reverb_preset *spu2_reverb_get_preset(int mode);
void spu2_reverb_set_preset(int mode, const spu2_reverb_preset *p);

/* Write a preset into a core's reverb registers the way libsd's
   sceSdSetEffectAttr does: the work area is the last size/2 halfwords
   before the core's end address (EEA << 16 | 0xFFFF), so ESA = end -
   size/2 + 1; the address registers take the preset's 8-byte units as
   halfwords (x4); the coefficients go in as they are.  `time` as
   spu2_write_reg. */
void spu2_reverb_apply_preset(int core, const spu2_reverb_preset *p, uint64_t time);

/* --- Lifecycle, registers, rendering -------------------------------------- */

/* Power-on state: RAM and registers zero, every voice stopped, time 0. */
void spu2_reset(void);

/* The 2 MB sound RAM (little-endian halfwords). */
uint8_t *spu2_ram(void);

/* The current time: the number of frames rendered since spu2_reset(). */
uint64_t spu2_time(void);

/* Queue a register write to take effect before the frame rendered at
   `time` (a time already rendered means "before the next frame").  Writes
   with equal times apply in call order.  Unknown offsets are stored and
   otherwise ignored. */
void spu2_write_reg(int core, unsigned reg, uint16_t value, uint64_t time);

/* Apply every queued write now, whatever its time (block-accurate use). */
void spu2_apply_pending(void);

/* Read a register as of the last rendered frame (queued writes not yet
   applied are not visible).  ENVX, VOLX*, MVOLX*, NAX, LSAX, ENDX and STATX
   read live state. */
uint16_t spu2_read_reg(int core, unsigned reg);

/* Render `frames` stereo frames into out (L, R interleaved). */
void spu2_render(int16_t *out, int frames);

/* How spu2_render has rendered since start-up (not reset by spu2_reset):
   frames rendered voice by voice in chunks, frames rendered one at a time
   (around timed writes, transfers and callbacks, with an IRQ armed, or
   after a hazard), and hazards (a voice reading sound RAM that the chunk's
   frames write: a write-back or reverb work area).  The output is the same
   either way. */
typedef struct spu2_stats {
    uint64_t chunks;
    uint64_t chunked_frames;
    uint64_t exact_frames;
    uint64_t hazards;
} spu2_stats;

void spu2_get_stats(spu2_stats *st);

/* --- Sound RAM transfers ---------------------------------------------------- */

/* Copy into / out of sound RAM at once (byte address; wraps at 2 MB).  The
   copied range is checked against IRQA as a transfer would be. */
void spu2_dma_write(uint32_t addr, const void *src, uint32_t len);
void spu2_dma_read(uint32_t addr, void *dst, uint32_t len);

#define SPU2_TRANS_WRITE 0
#define SPU2_TRANS_READ 1

/* A timed voice transfer on DMA channel `chan` (0 or 1, libsd's channel =
   core): at `time` the data is copied (byte address `addr`), and the
   channel stays busy until the transfer's duration has elapsed (see
   spu2_set_dma_rate); then the channel's callback runs.  Returns -1 if the
   channel is busy at the call (libsd's busy check), else 0. */
int spu2_voice_trans(int chan, int dir, uint32_t addr, void *buf, uint32_t len, uint64_t time);

/* 1 while a transfer on `chan` is queued or in progress. */
int spu2_voice_trans_busy(int chan);

/* Finish the transfer on `chan` now: a queued one is copied at once, the
   channel goes idle and its callback runs (libsd's blocking status wait,
   which cannot advance render time). */
void spu2_voice_trans_wait(int chan);

typedef void (*spu2_trans_cb)(int chan, void *user);
void spu2_set_trans_callback(int chan, spu2_trans_cb cb, void *user);

/* Transfer speed in bytes per output frame; 0 (the default) completes a
   transfer one frame after it starts. */
void spu2_set_dma_rate(uint32_t bytes_per_frame);

/* --- AutoDMA sound data input (MEMIN) --------------------------------------- */

/* Start the AutoDMA input of `core` from a host ring of 0x800 bytes laid out
   as libsd's looping block transfer is: two halves of 0x400 bytes, each 256
   left samples then 256 right samples (signed 16-bit little-endian).  One
   stereo sample is consumed per output frame; each time the input finishes
   a half, cb(core, half_finished, user) runs (the half-done interrupt).
   The ring is read in place, so the callback refills the finished half. */
typedef void (*spu2_memin_cb)(int core, int half, void *user);
void spu2_memin_start(int core, const uint8_t *ring, spu2_memin_cb cb, void *user);
void spu2_memin_stop(int core);
/* The half (0 or 1) the input is reading now. */
int spu2_memin_half(int core);

/* --- Interrupts ------------------------------------------------------------- */

/* A core whose ATTR has SPU2_ATTR_IRQ set raises its IRQ when a voice reads
   the ADPCM block containing its IRQA, a transfer writes it, or an output
   write-back stores to it.  The flag stays set until cleared. */
int spu2_irq_pending(int core);
void spu2_irq_clear(int core);
typedef void (*spu2_irq_cb)(int core, void *user);
void spu2_set_irq_callback(spu2_irq_cb cb, void *user);

#endif
