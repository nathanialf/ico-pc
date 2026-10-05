/*
 * port/audio/test/spu2_test.c
 *
 * The software SPU2 (spu2.h) and its libsd front end (spu2_sd.h):
 *
 *   - Gaussian interpolation table properties (psx-spx);
 *   - ADSR envelope lengths against the closed forms of psx-spx's envelope
 *     formula, and exponential decay against its geometric rate;
 *   - a sine encoded to ADPCM by a tiny test-only encoder, played at
 *     several pitches, frequency measured by zero crossings;
 *   - loop flags, ENDX, end+mute, key off, ENVX;
 *   - mixing, clamping, volume sweeps, core 0 -> core 1, AutoDMA input,
 *     noise, timed register writes, transfers and IRQ;
 *   - reverb impulse response (energy decays, stereo spread);
 *   - a determinism checksum of a fixed scene.
 *
 * Exit status 0 when every check passes.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../adpcm.h"
#include "../spu2.h"
#include "../spu2_internal.h"
#include "../spu2_sd.h"

/* The scene checksum (scene_checksum), first taken on linux-x64; every
   host must render the same bits. */
#define SPU2_SCENE_CHECKSUM 0x77DB2B76u

static int failures;
static int checks;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void check_eq(long long got, long long want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s: got %lld, want %lld\n", what, got, want);
    }
}

static void check_range(double got, double lo, double hi, const char *what)
{
    checks++;
    if (!(got >= lo && got <= hi)) {
        failures++;
        printf("FAIL: %s: got %g, want %g..%g\n", what, got, lo, hi);
    }
}

/* --- Helpers ----------------------------------------------------------------- */

/* Test-only encoder: for each block pick the (filter, shift) pair whose
   closed-loop decode is closest to the input, rounding each nibble. */
static void encode(const int16_t *pcm, int blocks, uint8_t *dst, const uint8_t *flags)
{
    adpcm_hist h;
    int b;

    memset(&h, 0, sizeof h);
    for (b = 0; b < blocks; b++) {
        const int16_t *x = pcm + b * 28;
        long long best_err = -1;
        uint8_t best[16];
        adpcm_hist best_h = h;
        int f;
        int sh;

        for (f = 0; f < 5; f++) {
            for (sh = 0; sh <= 12; sh++) {
                uint8_t blk[16];
                adpcm_hist t = h;
                int16_t dec[28];
                long long err = 0;
                int f0;
                int f1;
                int i;

                memset(blk, 0, sizeof blk);
                blk[0] = (uint8_t)(f << 4 | sh);
                adpcm_filter(f, &f0, &f1);
                for (i = 0; i < 28; i++) {
                    int32_t pred = (t.hist[0] * f0 + t.hist[1] * f1 + 32) >> 6;
                    double want = ((double)x[i] - pred) * (double)(1 << sh) / 4096.0;
                    int nib = (int)lrint(want);
                    int32_t s;

                    if (nib > 7)
                        nib = 7;
                    if (nib < -8)
                        nib = -8;
                    s = ((int32_t)(int16_t)(uint16_t)((nib & 0xF) << 12) >> sh) + pred;
                    if (s > 32767)
                        s = 32767;
                    if (s < -32768)
                        s = -32768;
                    t.hist[1] = t.hist[0];
                    t.hist[0] = s;
                    blk[2 + i / 2] |= (uint8_t)((nib & 0xF) << ((i & 1) * 4));
                    err += (long long)(s - x[i]) * (s - x[i]);
                }
                /* the real decoder must agree with the model */
                {
                    adpcm_hist chk = h;
                    adpcm_decode_block(blk, dec, &chk);
                    if (chk.hist[0] != t.hist[0])
                        err = -2;
                }
                if (err >= 0 && (best_err < 0 || err < best_err)) {
                    best_err = err;
                    memcpy(best, blk, 16);
                    best_h = t;
                }
            }
        }
        best[1] = flags ? flags[b] : 0;
        memcpy(dst + b * 16, best, 16);
        h = best_h;
    }
}

static void W(int core, unsigned reg, uint16_t v)
{
    spu2_write_reg(core, reg, v, spu2_time());
}

static void W2(int core, unsigned reg, uint32_t hw)
{
    W(core, reg, (uint16_t)(hw >> 16));
    W(core, reg + 2, (uint16_t)(hw & 0xFFFF));
}

/* A plain mixer setup: core 1 voices dry to the output, core 0 through
   core 1's external input, master volumes full, no reverb. */
static void basic_mix(void)
{
    W(0, SPU2_R_MMIX, SPU2_MMIX_VOICE_DRY_L | SPU2_MMIX_VOICE_DRY_R);
    W(1, SPU2_R_MMIX,
      SPU2_MMIX_VOICE_DRY_L | SPU2_MMIX_VOICE_DRY_R | SPU2_MMIX_SIN_DRY_L | SPU2_MMIX_SIN_DRY_R);
    W(0, SPU2_R_MVOLL, 0x3FFF);
    W(0, SPU2_R_MVOLR, 0x3FFF);
    W(1, SPU2_R_MVOLL, 0x3FFF);
    W(1, SPU2_R_MVOLR, 0x3FFF);
    W(1, SPU2_R_AVOLL, 0x7FFF);
    W(1, SPU2_R_AVOLR, 0x7FFF);
}

/* Voice v of core c: sample at SPU address `ssa`, envelope that attacks at
   once and holds, both volumes `vol`, dry to both sides. */
static void voice_setup(int c, int v, uint32_t ssa, uint16_t pitch, uint16_t vol)
{
    W2(c, SPU2_R_VA(v) + SPU2_VA_SSA, ssa);
    W(c, SPU2_R_VP(v) + SPU2_VP_PITCH, pitch);
    W(c, SPU2_R_VP(v) + SPU2_VP_ADSR1, 0x000F); /* attack 0 linear, SL 15 */
    W(c, SPU2_R_VP(v) + SPU2_VP_ADSR2, 0x1FC0); /* sustain never steps */
    W(c, SPU2_R_VP(v) + SPU2_VP_VOLL, vol);
    W(c, SPU2_R_VP(v) + SPU2_VP_VOLR, vol);
    W(c, SPU2_R_VMIXL + (v >= 16 ? 2 : 0),
      (uint16_t)(spu2_shadow(c, SPU2_R_VMIXL + (v >= 16 ? 2 : 0)) | 1u << (v & 15)));
    W(c, SPU2_R_VMIXR + (v >= 16 ? 2 : 0),
      (uint16_t)(spu2_shadow(c, SPU2_R_VMIXR + (v >= 16 ? 2 : 0)) | 1u << (v & 15)));
}

static void key_on(int c, int v)
{
    W(c, SPU2_R_KON + (v >= 16 ? 2 : 0), (uint16_t)(1u << (v & 15)));
}

/* A block of constant value 28672 (nibble 7, shift 0, filter 0) that loops
   on itself. */
static void put_dc_block(uint32_t byte_addr)
{
    uint8_t b[16];

    memset(b, 0x77, sizeof b);
    b[0] = 0x00;
    b[1] = ADPCM_FLAG_LOOP_START | ADPCM_FLAG_LOOP_REPEAT | ADPCM_FLAG_LOOP_END;
    spu2_dma_write(byte_addr, b, 16);
}

static int16_t buf[2 * 96000];

/* --- Tests ------------------------------------------------------------------- */

static void test_gauss(void)
{
    int i;
    int mono = 1;

    for (i = 0; i < 16; i++)
        check_eq(spu2_gauss[i], -1, "gauss: first sixteen entries are -1");
    check_eq(spu2_gauss[0x1FF], 0x59B3, "gauss: peak entry");
    for (i = 0x10; i < 0x1FF; i++)
        if (spu2_gauss[i + 1] < spu2_gauss[i])
            mono = 0;
    check(mono, "gauss: non-decreasing from entry 0x10");
    for (i = 0; i < 256; i++) {
        int s =
            spu2_gauss[i] + spu2_gauss[0xFF - i] + spu2_gauss[0x100 + i] + spu2_gauss[0x1FF - i];
        if (s < 0x7F7F || s > 0x7F81) {
            check(0, "gauss: four taps sum to 0x7F7F..0x7F81");
            break;
        }
    }
    check(i == 256, "gauss: all 256 tap sets checked");
}

/* Ticks until an envelope reaches `target` (or 2M ticks). */
static long env_ticks(int32_t start, int32_t target, int rate, int exp, int dec)
{
    int32_t level = start;
    uint32_t counter = 0;
    long n = 0;

    while (n < 2000000 && (dec ? level > target : level < target)) {
        spu2_env_tick(&level, &counter, rate, exp, dec, 0, 0);
        n++;
    }
    return n;
}

static void test_adsr(void)
{
    /* Linear increase, psx-spx: step (7 - s) << max(0, 11 - shift), one
       step per 1 << max(0, shift - 11) ticks, to 0x7FFF. */
    static const int rates[] = {0x00, 0x13, 0x2C, 0x34, 0x47};
    int i;

    for (i = 0; i < (int)(sizeof rates / sizeof rates[0]); i++) {
        int r = rates[i];
        int sh = r >> 2;
        int st = 7 - (r & 3);
        long step = (long)st << (sh < 11 ? 11 - sh : 0);
        long every = 1L << (sh > 11 ? sh - 11 : 0);
        long want = ((0x7FFF + step - 1) / step) * every;
        char what[64];

        snprintf(what, sizeof what, "linear attack rate 0x%02X length", r);
        check_eq(env_ticks(0, 0x7FFF, r, 0, 0), want, what);
    }

    /* Linear release, shift 10 (rate 0x28): step -8 << 1 = -16 per tick,
       0x7FFF / 16 rounded up = 2048 ticks. */
    check_eq(env_ticks(0x7FFF, 0, 0x28, 0, 1), 2048, "linear release shift 10 length");

    /* Exponential attack, rate 0: +14336, +14336 (now above 0x6000, so the
       step is quartered), +3584, then clamp: 4 ticks. */
    check_eq(env_ticks(0, 0x7FFF, 0, 1, 0), 4, "exponential attack rate 0 length");

    /* Exponential decrease, shift 4: each tick multiplies the level by
       about (1 - k), k = 8 * 2^(11 - 4) / 32768 = 1/32, so from 0x7FFF to
       0x0800 (sustain level 0) takes about ln(2048/32767) / ln(1 - k) =
       87.3 ticks. */
    {
        double k = 8.0 * 128.0 / 32768.0;
        double want = log(2048.0 / 32767.0) / log(1.0 - k);

        check_range((double)env_ticks(0x7FFF, 0x0800, 4 << 2, 1, 1), want * 0.95, want * 1.02,
                    "exponential decay shift 4 length");
    }

    /* Exponential decrease, shift 11: the step is -8 * level / 32768,
       floored (an arithmetic shift), i.e. -ceil(level / 4096).  In the band
       (4096 (n - 1), 4096 n] the level falls n per tick, so each band takes
       its width / n ticks, the remainder carrying into the next band; from
       0x7FFF to 0x0800: 4095/8, 4096/7, /6, /5, /4, /3, /2 and 2048/1 =
       512 + 585 + 683 + 819 + 1024 + 1365 + 2048 + 2048 = 9084. */
    check_eq(env_ticks(0x7FFF, 0x0800, 11 << 2, 1, 1), 9084, "exponential decay shift 11 length");

    /* Exponential release reaches 0 (the floored step never rounds to 0):
       shift 15 (rate 0x3C), from 0x7FFF. */
    check(env_ticks(0x7FFF, 0, 0x3C, 1, 1) < 2000000, "exponential release reaches 0");

    /* Never-step rate: no movement. */
    {
        int32_t level = 1234;
        uint32_t counter = 0;
        int n;

        for (n = 0; n < 100000; n++)
            spu2_env_tick(&level, &counter, 0x7F, 0, 0, 0, 1);
        check_eq(level, 1234, "rate 0x7F never steps");
    }
}

/* Zero crossings (negative -> non-negative) of channel ch over n frames. */
static int crossings(const int16_t *b, int n, int ch)
{
    int i;
    int c = 0;

    for (i = 1; i < n; i++)
        if (b[2 * (i - 1) + ch] < 0 && b[2 * i + ch] >= 0)
            c++;
    return c;
}

static void test_pitch(void)
{
    /* 1000 Hz at 48 kHz = 48 samples per period; 12 blocks (336 samples)
       hold 7 periods, so the block loop is seamless. */
    enum { BLOCKS = 12 };

    int16_t pcm[BLOCKS * 28];
    uint8_t adp[BLOCKS * 16];
    uint8_t fl[BLOCKS];

    static const struct {
        uint16_t pitch;
        int hz;
    } cases[] = {{0x1000, 1000}, {0x0800, 500}, {0x2000, 2000}, {0x3000, 3000}, {0x0400, 250}};

    int i;

    for (i = 0; i < BLOCKS * 28; i++)
        pcm[i] = (int16_t)lrint(20000.0 * sin(2.0 * 3.14159265358979323846 * i / 48.0));
    memset(fl, 0, sizeof fl);
    fl[0] = ADPCM_FLAG_LOOP_START;
    fl[BLOCKS - 1] = ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT;
    encode(pcm, BLOCKS, adp, fl);

    /* the encoding is close to the sine */
    {
        int16_t dec[BLOCKS * 28];
        adpcm_hist h;
        double err = 0;

        memset(&h, 0, sizeof h);
        adpcm_decode(adp, BLOCKS, dec, &h);
        for (i = 0; i < BLOCKS * 28; i++)
            err += fabs((double)dec[i] - pcm[i]);
        check_range(err / (BLOCKS * 28), 0, 400, "test encoder: mean error");
    }

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        char what[64];
        int zc;

        spu2_reset();
        spu2_dma_write(0x10000, adp, sizeof adp);
        basic_mix();
        voice_setup(1, 5, 0x10000 / 2, cases[i].pitch, 0x3FFF);
        key_on(1, 5);
        spu2_render(buf, 1000); /* settle */
        spu2_render(buf, 48000);
        zc = crossings(buf, 48000, 0);
        snprintf(what, sizeof what, "pitch 0x%04X: %d Hz", cases[i].pitch, cases[i].hz);
        check_range(zc, cases[i].hz - 2, cases[i].hz + 2, what);
        check(spu2_read_reg(1, SPU2_R_ENDX) & (1u << 5), "looping voice sets ENDX");
    }
}

static void test_loop_end_mute(void)
{
    uint8_t b[32];
    int16_t out[2 * 200];

    /* Two blocks: the second ends with code 1 (end + mute). */
    spu2_reset();
    memset(b, 0x77, sizeof b);
    b[0] = 0;
    b[1] = ADPCM_FLAG_LOOP_START;
    b[16] = 0;
    b[17] = ADPCM_FLAG_LOOP_END;
    spu2_dma_write(0x10000, b, sizeof b);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    key_on(1, 0);
    spu2_render(out, 20);
    check(spu2_read_reg(1, SPU2_R_VP(0) + SPU2_VP_ENVX) > 0, "voice sounding before end");
    check(out[2 * 19] > 20000, "voice output before end");
    check_eq(spu2_read_reg(1, SPU2_R_ENDX) & 1, 0, "ENDX clear before end");
    spu2_render(out, 100);
    check_eq(spu2_read_reg(1, SPU2_R_VP(0) + SPU2_VP_ENVX), 0, "end+mute: ENVX 0");
    check_eq(spu2_read_reg(1, SPU2_R_ENDX) & 1, 1, "end+mute: ENDX set");
    check_eq(spu2_read_reg(1, SPU2_R_VA(0) + SPU2_VA_LSAX + 2), 0x8000, "LSAX latched");
    check_eq(out[2 * 99], 0, "end+mute: silent");

    /* key on again clears ENDX */
    key_on(1, 0);
    spu2_render(out, 1);
    check_eq(spu2_read_reg(1, SPU2_R_ENDX) & 1, 0, "KON clears ENDX");

    /* key off: release (linear, shift 10 -> 2048 frames from full) */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 2, 0x8000, 0x1000, 0x3FFF);
    W(1, SPU2_R_VP(2) + SPU2_VP_ADSR2, 0x1FC0 | 0x0A);
    key_on(1, 2);
    spu2_render(buf, 100);
    check_eq(spu2_read_reg(1, SPU2_R_VP(2) + SPU2_VP_ENVX), 0x7FFF, "sustained at full");
    W(1, SPU2_R_KOFF, 1u << 2);
    spu2_render(buf, 2047);
    check(spu2_read_reg(1, SPU2_R_VP(2) + SPU2_VP_ENVX) > 0, "release not done at 2047");
    spu2_render(buf, 1);
    check_eq(spu2_read_reg(1, SPU2_R_VP(2) + SPU2_VP_ENVX), 0, "release done at 2048");
}

static void test_mix_clamp(void)
{
    int16_t out[2 * 64];
    int32_t one;

    /* One DC voice at full volume: interpolated constant 28672 scaled by
       the table's 0x7F80/0x8000 gain (+-1), envelope 0x7FFF, volume
       0x7FFE, master 0x7FFE. */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    key_on(1, 0);
    spu2_render(out, 64);
    one = out[2 * 63];
    check_range(one, 28400, 28600, "one DC voice level");
    check_eq(out[2 * 63], out[2 * 63 + 1], "both sides equal");

    /* Two: the voice bus saturates at 32767, master 0x7FFE -> 32765. */
    voice_setup(1, 1, 0x8000, 0x1000, 0x3FFF);
    key_on(1, 1);
    spu2_render(out, 64);
    check_eq(out[2 * 63], 32765, "two DC voices clamp");

    /* Negative volume inverts. */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x7FFF & (uint16_t)(-0x2000));
    key_on(1, 0);
    spu2_render(out, 64);
    check(out[2 * 63] < -14000 && out[2 * 63] > -14400, "negative voice volume inverts");

    /* Core 0 reaches the output only through core 1's external input. */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(0, 0, 0x8000, 0x1000, 0x3FFF);
    key_on(0, 0);
    spu2_render(out, 64);
    check_range(out[2 * 63], 28300, 28600, "core 0 through core 1");
    W(1, SPU2_R_AVOLL, 0);
    spu2_render(out, 2);
    check_eq(out[2], 0, "AVOL 0 blocks core 0 (left)");
    check(out[3] > 28000, "right still on");
    W(1, SPU2_R_MMIX, SPU2_MMIX_VOICE_DRY_L | SPU2_MMIX_VOICE_DRY_R);
    spu2_render(out, 2);
    check_eq(out[3], 0, "MMIX without SIN blocks core 0");

    /* Volume sweep: linear increase from 0, rate 0x20 (shift 8, step 7 <<
       3 = 56 per frame): 0x7FFF / 56 rounded up = 586 frames. */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0);
    key_on(1, 0);
    spu2_render(out, 10);
    W(1, SPU2_R_VP(0) + SPU2_VP_VOLL, 0x8000 | 0x20);
    spu2_render(buf, 585);
    check(spu2_read_reg(1, SPU2_R_VP(0) + SPU2_VP_VOLXL) < 0x7FFF, "sweep not done at 585");
    spu2_render(buf, 1);
    check_eq(spu2_read_reg(1, SPU2_R_VP(0) + SPU2_VP_VOLXL), 0x7FFF, "sweep done at 586");
    check_eq(buf[1], 0, "right volume untouched");
}

static void test_timed_writes(void)
{
    int16_t out[2 * 100];
    int i;
    int first = -1;

    /* A key on stamped for frame 50 starts at frame 50, not before. */
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    spu2_write_reg(1, SPU2_R_KON, 1, 50);
    spu2_render(out, 100);
    for (i = 0; i < 100; i++)
        if (out[2 * i] != 0) {
            first = i;
            break;
        }
    check_range(first, 50, 53, "timed key on starts at its frame");
    check(spu2_read_reg(1, SPU2_R_VP(0) + SPU2_VP_ENVX) == 0x7FFF, "timed key on applied");
}

static int memin_calls[2];
static int memin_last_half = -1;

static void memin_cb(int core, int half, void *user)
{
    (void)user;
    memin_calls[core]++;
    memin_last_half = half;
}

static void test_memin(void)
{
    static uint8_t ring[0x800];
    int16_t out[2 * 600];
    int i;

    spu2_reset();
    basic_mix();
    W(1, SPU2_R_MMIX, SPU2_MMIX_MEMIN_DRY_L | SPU2_MMIX_MEMIN_DRY_R);
    W(1, SPU2_R_BVOLL, 0x7FFF);
    W(1, SPU2_R_BVOLR, 0x7FFF);
    /* half 0: left 1000, right -1000; half 1: left 2000, right -2000 */
    for (i = 0; i < 256; i++) {
        int16_t v[4] = {1000, -1000, 2000, -2000};
        int h;

        for (h = 0; h < 2; h++) {
            uint8_t *l = ring + h * 0x400 + i * 2;
            uint8_t *r = ring + h * 0x400 + 0x200 + i * 2;

            l[0] = (uint8_t)(v[2 * h] & 0xFF);
            l[1] = (uint8_t)((uint16_t)v[2 * h] >> 8);
            r[0] = (uint8_t)(v[2 * h + 1] & 0xFF);
            r[1] = (uint8_t)((uint16_t)v[2 * h + 1] >> 8);
        }
    }
    memin_calls[0] = memin_calls[1] = 0;
    spu2_render(out, 1); /* apply the register writes */
    spu2_memin_start(1, ring, memin_cb, NULL);
    spu2_render(out, 600);
    /* gain 0x7FFF * master 0x7FFE: 1000 -> 999 or 998 */
    check_range(out[0], 997, 1000, "MEMIN half 0 left");
    check_range(out[1], -1000, -997, "MEMIN half 0 right");
    check_range(out[2 * 300], 1995, 2000, "MEMIN half 1 left");
    check_eq(memin_calls[1], 2, "MEMIN half callbacks over 600 frames");
    check_eq(memin_last_half, 1, "MEMIN last finished half");
    check_eq(spu2_memin_half(1), 0, "MEMIN now in half 0");
}

static void test_noise(void)
{
    int16_t out[2 * 4800];
    int i;
    int changes = 0;

    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    W(1, SPU2_R_NON, 1);
    W(1, SPU2_R_ATTR, 0x3F << SPU2_ATTR_NOISE_SHIFT); /* fastest clock */
    key_on(1, 0);
    spu2_render(out, 4800);
    for (i = 1; i < 4800; i++)
        if (out[2 * i] != out[2 * i - 2])
            changes++;
    check(changes > 4000, "noise at the fastest clock changes most frames");
    spu2_reset();
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    W(1, SPU2_R_NON, 1);
    W(1, SPU2_R_ATTR, 0x20 << SPU2_ATTR_NOISE_SHIFT); /* shift 8, step 4: every 128 */
    key_on(1, 0);
    spu2_render(out, 4800);
    changes = 0;
    for (i = 1; i < 4800; i++)
        if (out[2 * i] != out[2 * i - 2])
            changes++;
    check(changes > 10 && changes <= 40, "slow noise clock changes every 128 frames");
}

static int trans_done;

static void trans_cb(int chan, void *user)
{
    (void)user;
    trans_done |= 1 << chan;
}

static int irq_seen;

static void irq_cb(int core, void *user)
{
    (void)user;
    irq_seen |= 1 << core;
}

static void test_transfers_irq(void)
{
    uint8_t src[256];
    uint8_t back[256];
    int16_t out[2 * 8];
    int i;

    spu2_reset();
    for (i = 0; i < 256; i++)
        src[i] = (uint8_t)i;
    spu2_set_trans_callback(1, trans_cb, NULL);
    spu2_set_dma_rate(64);
    trans_done = 0;
    check_eq(spu2_voice_trans(1, SPU2_TRANS_WRITE, 0x20000, src, 256, spu2_time()), 0,
             "transfer accepted");
    check(spu2_voice_trans_busy(1), "channel busy");
    check_eq(spu2_voice_trans(1, SPU2_TRANS_WRITE, 0x20000, src, 256, spu2_time()), -1,
             "second transfer refused while busy");
    spu2_render(out, 4); /* 256 / 64 = 4 frames */
    check(spu2_voice_trans_busy(1), "busy for the transfer's duration");
    spu2_render(out, 1);
    check(!spu2_voice_trans_busy(1), "idle after");
    check_eq(trans_done, 2, "callback on channel 1");
    spu2_dma_read(0x20000, back, 256);
    check(memcmp(src, back, 256) == 0, "data in sound RAM");

    /* libsd front end: blocking status finishes at once */
    spu2_set_dma_rate(0);
    trans_done = 0;
    check_eq(spu2_sd_voice_trans(0, SPU2_SD_TRANS_WRITE, src, 0x30000, 64), 64, "sd trans");
    check_eq(spu2_sd_voice_trans_status(0, 0), 0, "sd status busy");
    check_eq(spu2_sd_voice_trans_status(0, 1), 1, "sd status wait");
    check_eq(spu2_sd_voice_trans_status(0, 0), 1, "sd status idle");

    /* IRQ: a voice reading the block that holds IRQA */
    spu2_reset();
    irq_seen = 0;
    spu2_set_irq_callback(irq_cb, NULL);
    put_dc_block(0x10000);
    basic_mix();
    voice_setup(1, 0, 0x8000, 0x1000, 0x3FFF);
    W2(0, SPU2_R_IRQA, 0x8004);
    W(0, SPU2_R_ATTR, SPU2_ATTR_IRQ);
    key_on(1, 0);
    spu2_render(out, 2);
    check(spu2_irq_pending(0), "IRQ on voice read of IRQA's block");
    check_eq(irq_seen, 1, "IRQ callback core 0");
    spu2_irq_clear(0);
    check(!spu2_irq_pending(0), "IRQ cleared");
    /* a transfer over IRQA */
    W2(1, SPU2_R_IRQA, 0x18000);
    W(1, SPU2_R_ATTR, SPU2_ATTR_IRQ);
    spu2_render(out, 1);
    spu2_dma_write(0x30000 - 4, src, 16);
    check(spu2_irq_pending(1), "IRQ on transfer over IRQA");
    spu2_set_irq_callback(NULL, NULL);
}

static void test_sd(void)
{
    spu2_sd_effect_attr a;
    int16_t out[2 * 4];

    spu2_reset();
    spu2_sd_init(0);
    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | 0, 0x1FFFFF);
    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | 1, 0x1DFFFF);
    memset(&a, 0, sizeof a);
    a.mode = SPU2_SD_EFFECT_MODE_STUDIO_3;
    check_eq(spu2_sd_set_effect_attr(0, &a), 0, "effect attr mode 4");
    check_eq(spu2_sd_set_effect_attr(1, &a), 0, "effect attr mode 4 core 1");
    a.mode = 10;
    check_eq(spu2_sd_set_effect_attr(0, &a), -1, "effect mode 10 refused");
    spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 0, 1);
    spu2_sd_set_core_attr(SPU2_SD_CORE_NOISE_CLK | 1, 0x2A);
    spu2_sd_set_param(SPU2_SD_PARAM_EVOLL | 0, 0xCCC);
    spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | SPU2_SD_VOICE(1, 7), 0x1234);
    spu2_sd_set_addr(SPU2_SD_VADDR_SSA | 0x40 | SPU2_SD_VOICE(1, 7), 0x12340);
    spu2_render(out, 1);
    /* studio 3 (studio large): 0x6FE0 bytes below the end address */
    check_eq(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 0), 0x200000 - 0x6FE0, "ESA core 0");
    check_eq(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 1), 0x1E0000 - 0x6FE0, "ESA core 1");
    check_eq(spu2_sd_get_addr(SPU2_SD_ADDR_EEA | 1), 0x1DFFFF, "EEA core 1");
    check_eq(spu2_read_reg(0, SPU2_R_APF1_SIZE + 2), 0x00E3 * 4, "dAPF1 in halfwords");
    check_eq((int16_t)spu2_read_reg(0, SPU2_R_IIR_VOL), (int16_t)0x6F60, "vIIR");
    check_eq(spu2_sd_get_param(SPU2_SD_PARAM_EVOLL | 0), 0xCCC, "EVOL");
    check_eq(spu2_sd_get_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 0), 1, "effect enable");
    check_eq(spu2_sd_get_core_attr(SPU2_SD_CORE_NOISE_CLK | 1), 0x2A, "noise clock");
    check_eq(spu2_sd_get_param(SPU2_SD_VPARAM_PITCH | SPU2_SD_VOICE(1, 7)), 0x1234, "pitch");
    check_eq(spu2_read_reg(1, SPU2_R_VA(7) + SPU2_VA_SSA + 2), 0x12340 / 2 & 0xFFFF, "SSA");
    check_eq(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXL | 1), 0xFFFFFF, "VMIXL all on after init");
    check_eq(spu2_sd_get_param(SPU2_SD_PARAM_MMIX | 1), 0xFFC, "MMIX core 1 default");
}

static double win_energy(const int16_t *b, int from, int n, int ch)
{
    double e = 0;
    int i;

    for (i = from; i < from + n; i++)
        e += (double)b[2 * i + ch] * b[2 * i + ch];
    return e;
}

static void test_reverb(void)
{
    static int16_t rv[2 * 96000];
    uint8_t blk[32];
    spu2_sd_effect_attr a;
    double e[20];
    double peak = 0;
    int peak_i = 0;
    int i;
    int differ = 0;

    /* An impulse: one block with a single large sample, then a silent
       self-loop. */
    spu2_reset();
    spu2_sd_init(0);
    memset(blk, 0, sizeof blk);
    blk[0] = 0x00;
    blk[2] = 0x07; /* sample 0 = 28672 */
    blk[16] = 0x00;
    blk[17] = ADPCM_FLAG_LOOP_START | ADPCM_FLAG_LOOP_REPEAT | ADPCM_FLAG_LOOP_END;
    spu2_dma_write(0x10000, blk, sizeof blk);
    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | 1, 0x1FFFFF);
    memset(&a, 0, sizeof a);
    a.mode = SPU2_SD_EFFECT_MODE_STUDIO_3;
    a.depth_L = 0x7FFF;
    a.depth_R = 0x7FFF;
    spu2_sd_set_effect_attr(1, &a);
    spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 1, 1);
    spu2_sd_set_param(SPU2_SD_PARAM_MVOLL | 1, 0x3FFF);
    spu2_sd_set_param(SPU2_SD_PARAM_MVOLR | 1, 0x3FFF);
    /* wet only: voices to the reverb, nothing dry */
    spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 1, SPU2_MMIX_VOICE_WET_L | SPU2_MMIX_VOICE_WET_R);
    spu2_sd_set_param(SPU2_SD_VPARAM_VOLL | SPU2_SD_VOICE(1, 0), 0x3FFF);
    spu2_sd_set_param(SPU2_SD_VPARAM_VOLR | SPU2_SD_VOICE(1, 0), 0x3FFF);
    spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | SPU2_SD_VOICE(1, 0), 0x1000);
    spu2_sd_set_param(SPU2_SD_VPARAM_ADSR1 | SPU2_SD_VOICE(1, 0), 0x000F);
    spu2_sd_set_param(SPU2_SD_VPARAM_ADSR2 | SPU2_SD_VOICE(1, 0), 0x1FC0);
    spu2_sd_set_addr(SPU2_SD_VADDR_SSA | SPU2_SD_VOICE(1, 0), 0x10000);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 1, 1);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 1, 1);
    spu2_render(rv, 1);
    /* the attack ramps over three frames, so the impulse is the
       interpolated block start: still a short burst */
    spu2_sd_set_switch(SPU2_SD_SWITCH_KON | 1, 1);
    spu2_render(rv, 96000);

    for (i = 0; i < 20; i++) {
        e[i] = win_energy(rv, i * 4800, 4800, 0) + win_energy(rv, i * 4800, 4800, 1);
        if (e[i] > peak) {
            peak = e[i];
            peak_i = i;
        }
    }
    check(peak > 0, "reverb produces output");
    check(peak_i <= 2, "reverb energy peaks early");
    check(e[19] < peak * 0.05, "reverb energy decays (last 0.1 s under 5% of peak)");
    check(e[10] < e[peak_i], "reverb energy lower at 1 s");
    for (i = 0; i < 96000; i++)
        if (rv[2 * i] != rv[2 * i + 1])
            differ++;
    check(differ > 1000, "reverb stereo spread (L and R differ)");
    {
        double el = win_energy(rv, 0, 48000, 0);
        double er = win_energy(rv, 0, 48000, 1);

        check(el > 0 && er > 0, "reverb on both sides");
    }

    /* EVOL 0 silences it; dry path unaffected */
    spu2_sd_set_param(SPU2_SD_PARAM_EVOLL | 1, 0);
    spu2_sd_set_param(SPU2_SD_PARAM_EVOLR | 1, 0);
    spu2_render(rv, 100);
    check(win_energy(rv, 1, 99, 0) == 0, "EVOL 0 silences reverb");
}

/* FNV-1a over the rendered bytes of a fixed scene: voices on pseudo-random
   ADPCM (every filter and shift, a looping tail), three pitches, a noise
   voice, a sweep, a timed key off, core 0 through core 1, reverb on both
   cores.  Integer-only set-up, so the golden value holds on every host. */
static uint32_t scene_checksum(void)
{
    enum { BLOCKS = 64 };

    uint8_t adp[BLOCKS * 16];
    spu2_sd_effect_attr a;
    uint32_t h = 2166136261u;
    uint32_t seed = 12345;
    int i;
    int k;

    for (i = 0; i < BLOCKS * 16; i++) {
        seed = seed * 1103515245u + 12345u;
        adp[i] = (uint8_t)(seed >> 16);
    }
    for (i = 0; i < BLOCKS; i++) {
        /* filter 0..4, shift 2..14 (13 and 14 reserved) */
        adp[i * 16] = (uint8_t)((i % 5) << 4 | (2 + i % 13));
        adp[i * 16 + 1] = 0;
    }
    adp[16 * 8 + 1] = ADPCM_FLAG_LOOP_START;
    adp[16 * (BLOCKS - 1) + 1] = ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT;

    spu2_reset();
    spu2_sd_init(0);
    spu2_dma_write(0x10000, adp, sizeof adp);
    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | 0, 0x1FFFFF);
    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | 1, 0x1DFFFF);
    memset(&a, 0, sizeof a);
    a.mode = SPU2_SD_EFFECT_MODE_STUDIO_3;
    a.depth_L = 0x2000;
    a.depth_R = 0x2000;
    spu2_sd_set_effect_attr(0, &a);
    spu2_sd_set_effect_attr(1, &a);
    spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 0, 1);
    spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 1, 1);
    spu2_sd_set_core_attr(SPU2_SD_CORE_NOISE_CLK | 0, 0x30);
    for (k = 0; k < 2; k++) {
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLL | k, 0x3FFF);
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLR | k, 0x3FFF);
    }
    for (i = 0; i < 4; i++) {
        int c = i & 1;
        int v = i * 3;

        spu2_sd_set_param(SPU2_SD_VPARAM_VOLL | SPU2_SD_VOICE(c, v),
                          (uint16_t)(0x1000 + i * 0x300));
        spu2_sd_set_param(SPU2_SD_VPARAM_VOLR | SPU2_SD_VOICE(c, v),
                          (uint16_t)(0x2000 - i * 0x300));
        spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | SPU2_SD_VOICE(c, v),
                          (uint16_t)(0x0C00 + i * 0x377));
        spu2_sd_set_param(SPU2_SD_VPARAM_ADSR1 | SPU2_SD_VOICE(c, v), 0x0A8A);
        spu2_sd_set_param(SPU2_SD_VPARAM_ADSR2 | SPU2_SD_VOICE(c, v), 0x5FCC);
        spu2_sd_set_addr(SPU2_SD_VADDR_SSA | SPU2_SD_VOICE(c, v), 0x10000);
    }
    spu2_sd_set_switch(SPU2_SD_SWITCH_NON | 0, 1u << 6);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 0, 0x41);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | 1, 0x08);
    spu2_sd_set_switch(SPU2_SD_SWITCH_KON | 0, (1u << 0) | (1u << 6));
    spu2_sd_set_switch(SPU2_SD_SWITCH_KON | 1, (1u << 3) | (1u << 9));
    for (k = 0; k < 10; k++) {
        if (k == 5) {
            spu2_sd_set_time(spu2_time() + 123);
            spu2_sd_set_switch(SPU2_SD_SWITCH_KOFF | 0, 1u << 0);
            spu2_sd_set_param(SPU2_SD_VPARAM_VOLL | SPU2_SD_VOICE(1, 3), 0xC000 | 0x30);
        }
        spu2_render(buf, 4800);
        for (i = 0; i < 2 * 4800; i++) {
            uint16_t s = (uint16_t)buf[i];

            h = (h ^ (s & 0xFF)) * 16777619u;
            h = (h ^ (s >> 8)) * 16777619u;
        }
    }
    spu2_sd_set_time(0);
    return h;
}

int main(int argc, char **argv)
{
    uint32_t sum;

    test_gauss();
    test_adsr();
    test_pitch();
    test_loop_end_mute();
    test_mix_clamp();
    test_timed_writes();
    test_memin();
    test_noise();
    test_transfers_irq();
    test_sd();
    test_reverb();

    sum = scene_checksum();
    check_eq(scene_checksum(), sum, "scene renders the same twice");
    printf("scene checksum 0x%08X\n", (unsigned)sum);
    if (argc < 2 || strcmp(argv[1], "--no-golden") != 0)
        check_eq(sum, SPU2_SCENE_CHECKSUM, "scene checksum matches the golden value");

    printf("spu2_test: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
