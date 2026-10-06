/*
 * port/audio/adpcm.h
 *
 * The SPU ("VAG") ADPCM block decoder, shared by the software SPU2
 * (spu2.c) and the stream code of Phase 4B (.int files).
 *
 * A block is 16 bytes and decodes to 28 signed 16-bit samples:
 *
 *   byte 0     bits 0-3 shift (range), bits 4-6 filter
 *   byte 1     flags: bit 0 loop end, bit 1 loop repeat, bit 2 loop start
 *   bytes 2-15 28 four-bit samples, low nibble first
 *
 * sample = clamp16((nibble << 12 >> shift) + ((old*f0 + older*f1 + 32) >> 6))
 * with (f0, f1) one of the five SPU filter pairs (0,0) (60,0) (115,-52)
 * (98,-55) (122,-60).  Source: psx-spx, "SPU ADPCM Samples" and "CDROM
 * XA-ADPCM decode_28_nibbles".  Integer math
 * only: every host decodes the same bits.
 */
#ifndef ICO_PORT_AUDIO_ADPCM_H
#define ICO_PORT_AUDIO_ADPCM_H

#include <stdint.h>

#define ADPCM_BLOCK_BYTES 16
#define ADPCM_BLOCK_SAMPLES 28

/* Flag bits, byte 1 of a block. */
#define ADPCM_FLAG_LOOP_END 0x01
#define ADPCM_FLAG_LOOP_REPEAT 0x02
#define ADPCM_FLAG_LOOP_START 0x04

/* The decoder's history: the last two output samples (hist[0] the most
   recent).  Zero it before the first block of a sound. */
typedef struct adpcm_hist {
    int32_t hist[2];
} adpcm_hist;

/* The filter pair a header selects.  Filters 5-7 are not documented; they
   decode as filter 0 here. */
void adpcm_filter(int filter, int *f0, int *f1);

/* Decode one block into out[28], updating h.  Returns the block's flag
   byte (byte 1). */
int adpcm_decode_block(const uint8_t *block, int16_t *out, adpcm_hist *h);

/* Decode `blocks` consecutive blocks into out (28 samples each), carrying
   the history across blocks; the flags are ignored.  Returns the number of
   samples written. */
int adpcm_decode(const uint8_t *src, int blocks, int16_t *out, adpcm_hist *h);

#endif
