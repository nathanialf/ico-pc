/*
 * port/audio/adpcm.c
 *
 * SPU ADPCM block decoding (adpcm.h).  Written from psx-spx's description
 * of the format; no decoder code was
 * consulted.
 */
#include "adpcm.h"

static const int8_t filter_pos[5] = {0, 60, 115, 98, 122};
static const int8_t filter_neg[5] = {0, 0, -52, -55, -60};

void adpcm_filter(int filter, int *f0, int *f1)
{
    if (filter < 0 || filter > 4)
        filter = 0;
    *f0 = filter_pos[filter];
    *f1 = filter_neg[filter];
}

int adpcm_decode_block(const uint8_t *block, int16_t *out, adpcm_hist *h)
{
    int shift = block[0] & 0x0F;
    int f0;
    int f1;
    int32_t old = h->hist[0];
    int32_t older = h->hist[1];
    int i;

    /* psx-spx (XA header bytes): ranges 13-15 are reserved and act as 9. */
    if (shift > 12)
        shift = 9;
    adpcm_filter((block[0] >> 4) & 0x07, &f0, &f1);

    /* No data and no prediction (filter 0, or a zero history): every
       sample is (0 >> shift) + (32 >> 6) = 0.  The idle block libsd's init
       loops every voice over is one. */
    if ((f0 == 0 && f1 == 0) || (old == 0 && older == 0)) {
        uint32_t any = 0;

        for (i = 2; i < ADPCM_BLOCK_BYTES; i++)
            any |= block[i];
        if (any == 0) {
            for (i = 0; i < ADPCM_BLOCK_SAMPLES; i++)
                out[i] = 0;
            h->hist[0] = 0;
            h->hist[1] = 0;
            return block[1];
        }
    }

    for (i = 0; i < ADPCM_BLOCK_SAMPLES; i++) {
        int nib = (block[2 + (i >> 1)] >> ((i & 1) * 4)) & 0x0F;
        /* sign-extend the nibble and place it in bits 12-15 */
        int32_t t = (int32_t)(int16_t)(uint16_t)(nib << 12);
        int32_t s = (t >> shift) + ((old * f0 + older * f1 + 32) >> 6);

        if (s > 32767)
            s = 32767;
        else if (s < -32768)
            s = -32768;
        out[i] = (int16_t)s;
        older = old;
        old = s;
    }
    h->hist[0] = old;
    h->hist[1] = older;
    return block[1];
}

int adpcm_decode(const uint8_t *src, int blocks, int16_t *out, adpcm_hist *h)
{
    int b;

    for (b = 0; b < blocks; b++)
        adpcm_decode_block(src + b * ADPCM_BLOCK_BYTES, out + b * ADPCM_BLOCK_SAMPLES, h);
    return blocks * ADPCM_BLOCK_SAMPLES;
}
