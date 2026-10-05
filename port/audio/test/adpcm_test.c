/*
 * port/audio/test/adpcm_test.c
 *
 * SPU ADPCM block decoding against vectors worked by hand from the
 * format's formula (adpcm.h):
 *
 *   s = clamp16((nibble << 12 >> shift) + ((old*f0 + older*f1 + 32) >> 6))
 *
 * Exit status 0 when every check passes.
 */
#include <stdio.h>
#include <string.h>

#include "../adpcm.h"

static int failures;
static int checks;

static void check_eq(long got, long want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s: got %ld, want %ld\n", what, got, want);
    }
}

int main(void)
{
    uint8_t b[16];
    int16_t out[28];
    adpcm_hist h;
    int flags;

    /* Block A: shift 0, filter 0, flags 0x03.  With no filter and no shift
       each sample is its nibble placed in bits 12-15:
         byte 2 = 0x21 -> sample 0 = 1 << 12 = 4096, sample 1 = 2 << 12 = 8192
         byte 3 = 0xF8 -> sample 2 = 0x8 = -8 -> -32768,
                          sample 3 = 0xF = -1 -> -4096
         bytes 4-15 = 0 -> zeros. */
    memset(b, 0, sizeof b);
    memset(&h, 0, sizeof h);
    b[0] = 0x00;
    b[1] = 0x03;
    b[2] = 0x21;
    b[3] = 0xF8;
    flags = adpcm_decode_block(b, out, &h);
    check_eq(flags, 3, "A flags");
    check_eq(out[0], 4096, "A s0");
    check_eq(out[1], 8192, "A s1");
    check_eq(out[2], -32768, "A s2");
    check_eq(out[3], -4096, "A s3");
    check_eq(out[4], 0, "A s4");
    check_eq(out[27], 0, "A s27");
    check_eq(h.hist[0], 0, "A hist0");

    /* Block B: shift 4, filter 1 (f0 = 60, f1 = 0), history zero.
         byte 2 = 0x77, bytes 3.. = 0.
         s0 = (7 << 12) >> 4 = 28672 / 16 = 1792; filter (0 + 32) >> 6 = 0
            -> 1792
         s1 = 1792 + ((1792*60 + 32) >> 6) = 1792 + (107552 >> 6)
            = 1792 + 1680 (107552 / 64 = 1680.5, floor) = 3472
         s2 = 0 + ((3472*60 + 32) >> 6) = 208352 >> 6 = 3255 (3255.5)
         s3 = 0 + ((3255*60 + 32) >> 6) = 195332 >> 6 = 3052 (3052.06) */
    memset(b, 0, sizeof b);
    memset(&h, 0, sizeof h);
    b[0] = 0x14;
    b[2] = 0x77;
    adpcm_decode_block(b, out, &h);
    check_eq(out[0], 1792, "B s0");
    check_eq(out[1], 3472, "B s1");
    check_eq(out[2], 3255, "B s2");
    check_eq(out[3], 3052, "B s3");

    /* Block C: shift 12, filter 2 (f0 = 115, f1 = -52), history old = -100,
       older = 50: the filter term of a negative sum rounds down.
         byte 2 = 0x10: s0 nibble 0, s1 nibble 1 -> (1 << 12) >> 12 = 1
         s0 = 0 + ((-100*115 + 50*-52 + 32) >> 6) = (-11500 - 2600 + 32) >> 6
            = -14068 >> 6 = -220 (-219.8, floor)
         s1 = 1 + ((-220*115 + -100*-52 + 32) >> 6) = 1 + (-20068 >> 6)
            = 1 - 314 (-313.6, floor) = -313 */
    memset(b, 0, sizeof b);
    h.hist[0] = -100;
    h.hist[1] = 50;
    b[0] = 0x2C;
    b[2] = 0x10;
    adpcm_decode_block(b, out, &h);
    check_eq(out[0], -220, "C s0");
    check_eq(out[1], -313, "C s1");

    /* Block D: shift 0, filter 4 (f0 = 122, f1 = -60), history old = 30000,
       older = 0: saturation.
         s0 = 28672 + ((30000*122 + 32) >> 6) = 28672 + 57188 = 85860
            -> 32767
         byte 2 = 0x87: s1 nibble 8 = -32768
         s1 = -32768 + ((32767*122 + 30000*-60 + 32) >> 6)
            = -32768 + (2197606 >> 6) = -32768 + 34337 = 1569 */
    memset(b, 0, sizeof b);
    h.hist[0] = 30000;
    h.hist[1] = 0;
    b[0] = 0x40;
    b[2] = 0x87;
    adpcm_decode_block(b, out, &h);
    check_eq(out[0], 32767, "D s0 clamps");
    check_eq(out[1], 1569, "D s1");

    /* Block E: the reserved shift 13 acts as 9 (psx-spx): nibble 1 ->
       4096 >> 9 = 8; and the undocumented filter 5 decodes as filter 0. */
    memset(b, 0, sizeof b);
    memset(&h, 0, sizeof h);
    h.hist[0] = 1000;
    b[0] = 0x5D;
    b[2] = 0x01;
    adpcm_decode_block(b, out, &h);
    check_eq(out[0], 8, "E shift 13 as 9, filter 5 as 0");

    /* adpcm_decode carries history across blocks: block B twice gives the
       second block's first sample 1792 + ((s27*60 + s26*0 + 32) >> 6). */
    {
        uint8_t two[32];
        int16_t o2[56];
        int32_t s27;

        memset(two, 0, sizeof two);
        two[0] = 0x14;
        two[2] = 0x77;
        memcpy(two + 16, two, 16);
        memset(&h, 0, sizeof h);
        check_eq(adpcm_decode(two, 2, o2, &h), 56, "decode count");
        s27 = o2[27];
        check_eq(o2[28], 1792 + ((s27 * 60 + 32) >> 6), "history carried");
    }

    printf("adpcm_test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
