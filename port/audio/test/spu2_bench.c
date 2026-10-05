/*
 * port/audio/test/spu2_bench.c
 *
 * The SPU2 render's cost and its bit-exactness (docs/port/AUDIO.md,
 * "Render cost"):
 *
 *   spu2_bench --check   renders five 10 s scenes and compares a CRC-32 of
 *                        everything observable (the output, the registers
 *                        the driver reads after every vsync, the callbacks
 *                        and their times, the final sound RAM) with golden
 *                        values taken from the frame-by-frame renderer
 *                        before S3's optimisation; then renders 24 random
 *                        2 s scenes both chunked and frame by frame
 *                        (spu2_set_exact) and compares those.  ctest
 *                        `spu2_render_crc`.
 *   spu2_bench --print   prints the CRCs of all of them (to compare with
 *                        another build of the SPU2).
 *   spu2_bench --trace-crc <scene | seed>
 *                        prints, after every vsync of one scene (a name, or
 *                        a random scene's seed), the harness PRNG state, the
 *                        running CRC and a CRC per component (samples,
 *                        registers, callbacks, RAM); diff two builds' output
 *                        to find the first vsync and component that differ.
 *   spu2_bench           times spu2_render per vsync (960 frames) for an
 *                        idle SPU2 and 24 and 48 voices with reverb on both
 *                        cores, and (when built with SPU2_PROFILE, the
 *                        spu2_prof target) prints the per-stage profile.
 *
 * The scenes are integer-only and seeded, so the CRCs hold on every host.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "../adpcm.h"
#include "../spu2.h"
#include "../spu2_internal.h"
#include "../spu2_sd.h"

/* --- CRC-32 (IEEE, reflected) and the PRNG ------------------------------------ */

static uint32_t crc_table[256];

static void crc_init(void)
{
    uint32_t i;
    int k;

    for (i = 0; i < 256; i++) {
        uint32_t c = i;

        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}

static uint32_t crc;

/* --trace-crc: a CRC per component (what crc_bytes is fed at the time) as
   well as the running one, printed after every vsync. */
enum { PART_SAMPLES, PART_REGS, PART_CALLBACKS, PART_RAM, PARTS };

static int trace;
static int crc_part;
static uint32_t part_crc[PARTS];

static uint32_t crc_step(uint32_t c, const uint8_t *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void crc_bytes(const void *p, size_t n)
{
    crc = crc_step(crc, (const uint8_t *)p, n);
    if (trace)
        part_crc[crc_part] = crc_step(part_crc[crc_part], (const uint8_t *)p, n);
}

static void crc_u32(uint32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    crc_bytes(b, 4);
}

static void crc_samples(const int16_t *s, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        uint8_t b[2];

        b[0] = (uint8_t)((uint16_t)s[i] & 0xFF);
        b[1] = (uint8_t)((uint16_t)s[i] >> 8);
        crc_bytes(b, 2);
    }
}

static uint32_t seed;

static uint32_t rnd(uint32_t n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

/* --- Sound data ------------------------------------------------------------------ */

/* Byte addresses of the sounds; ~ADPCM with every filter and shift. */
enum { SOUNDS = 8 };

static uint32_t snd_addr[SOUNDS];

/* sound k: blocks, loop start block (-1 none), end kind (0 loop repeat,
   1 end + mute, 2 no end flag) */
static const int snd_blocks[SOUNDS] = {40, 64, 12, 100, 7, 33, 200, 16};
static const int snd_loop[SOUNDS] = {0, 8, 0, 50, -1, 3, 100, -1};
static const int snd_end[SOUNDS] = {0, 0, 0, 0, 1, 0, 0, 1};

static void make_sound(uint8_t *dst, int blocks, int loop, int end, int quiet)
{
    int i;

    for (i = 0; i < blocks * 16; i++)
        dst[i] = (uint8_t)rnd(256);
    for (i = 0; i < blocks; i++) {
        int filter = (int)rnd(5);
        int shift = quiet ? 4 + (int)rnd(9) : (int)rnd(16);

        dst[i * 16] = (uint8_t)(filter << 4 | shift);
        dst[i * 16 + 1] = 0;
    }
    if (loop >= 0)
        dst[loop * 16 + 1] |= ADPCM_FLAG_LOOP_START;
    if (end == 0)
        dst[(blocks - 1) * 16 + 1] |= ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT;
    else if (end == 1)
        dst[(blocks - 1) * 16 + 1] |= ADPCM_FLAG_LOOP_END;
}

static void load_sounds(void)
{
    static uint8_t buf[256 * 16];
    uint32_t addr = 0x20000;
    int k;

    for (k = 0; k < SOUNDS; k++) {
        make_sound(buf, snd_blocks[k], snd_loop[k], snd_end[k], k & 1);
        spu2_dma_write(addr, buf, (uint32_t)snd_blocks[k] * 16u);
        snd_addr[k] = addr;
        addr += (uint32_t)snd_blocks[k] * 16u + 0x40;
    }
}

/* --- Scene helpers ---------------------------------------------------------------- */

#define VSEL(c, v) SPU2_SD_VOICE(c, v)

static void reverb_on(int core, int mode, uint32_t eea)
{
    spu2_sd_effect_attr a;

    spu2_sd_set_addr(SPU2_SD_ADDR_EEA | core, eea);
    memset(&a, 0, sizeof a);
    a.mode = mode;
    a.depth_L = 0x3000;
    a.depth_R = 0x2800;
    spu2_sd_set_effect_attr(core, &a);
    spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | core, 1);
}

/*
 * The scenes draw rnd() one call per full expression: C leaves the order of
 * function arguments and of the operands of | and + unspecified, and gcc and
 * clang differ (gcc evaluated `f(rnd(a), rnd(b))` right to left, clang left
 * to right), which gave the game and hazard scenes different CRCs under the
 * two compilers.  The draws below are in the order gcc used when the golden
 * CRCs were taken (operands left to right, arguments right to left).
 */
static void voice_start(int c, int v, int sound, int sustained)
{
    uint16_t sel = (uint16_t)VSEL(c, v);
    uint32_t attack_exp;
    uint32_t attack;
    uint32_t decay;
    uint32_t sustain;
    uint32_t sustain_mode;
    uint32_t sustain_rate;
    uint32_t release_exp;
    uint32_t release;

    spu2_sd_set_param(SPU2_SD_VPARAM_VOLL | sel, (uint16_t)(0x0800 + rnd(0x3000)));
    spu2_sd_set_param(SPU2_SD_VPARAM_VOLR | sel, (uint16_t)(0x0800 + rnd(0x3000)));
    spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | sel, (uint16_t)(0x0400 + rnd(0x3800)));
    /* attack linear/exp, decay, sustain level; sustain and release */
    attack_exp = rnd(2);
    attack = 0x10 + rnd(0x50);
    decay = rnd(16);
    sustain = sustained ? 0xC + rnd(4) : rnd(16);
    spu2_sd_set_param(SPU2_SD_VPARAM_ADSR1 | sel,
                      (uint16_t)(attack_exp << 15 | attack << 8 | decay << 4 | sustain));
    sustain_mode = rnd(4);
    sustain_rate = sustained ? 0x7F : rnd(0x80);
    release_exp = rnd(2);
    release = rnd(0x20);
    spu2_sd_set_param(
        SPU2_SD_VPARAM_ADSR2 | sel,
        (uint16_t)(sustain_mode << 14 | sustain_rate << 6 | release_exp << 5 | release));
    spu2_sd_set_addr(SPU2_SD_VADDR_SSA | sel, snd_addr[sound]);
}

/* voice_start with a random sound, sustained or not */
static void voice_start_random(int c, int v)
{
    int sustained = (int)rnd(2);
    int sound = (int)rnd(SOUNDS);

    voice_start(c, v, sound, sustained);
}

/* A random ADPCM header byte (filter, shift 4..12) */
static uint8_t random_header(void)
{
    uint32_t filter = rnd(5);
    uint32_t shift = 4 + rnd(9);

    return (uint8_t)(filter << 4 | shift);
}

/* A random address: a block in the first four of a random sound */
static uint32_t random_block_addr(void)
{
    uint32_t sound = rnd(SOUNDS);

    return snd_addr[sound] + 16 * rnd(4);
}

static void key(int c, uint32_t mask, int on)
{
    spu2_sd_set_switch((on ? SPU2_SD_SWITCH_KON : SPU2_SD_SWITCH_KOFF) | c, mask);
}

/* The registers the driver and the sequencer read after a tick. */
static void crc_state(void)
{
    int c;
    int v;

    for (c = 0; c < 2; c++) {
        for (v = 0; v < 24; v++) {
            crc_u32(spu2_read_reg(c, SPU2_R_VP(v) + SPU2_VP_ENVX));
            crc_u32(spu2_read_reg(c, SPU2_R_VP(v) + SPU2_VP_VOLXL));
            crc_u32(spu2_read_reg(c, SPU2_R_VP(v) + SPU2_VP_VOLXR));
            crc_u32(spu2_read_reg(c, SPU2_R_VA(v) + SPU2_VA_NAX));
            crc_u32(spu2_read_reg(c, SPU2_R_VA(v) + SPU2_VA_NAX + 2));
            crc_u32(spu2_read_reg(c, SPU2_R_VA(v) + SPU2_VA_LSAX + 2));
        }
        crc_u32(spu2_read_reg(c, SPU2_R_ENDX));
        crc_u32(spu2_read_reg(c, SPU2_R_ENDX + 2));
        crc_u32(spu2_read_reg(c, SPU2_R_STATX));
        crc_u32(spu2_read_reg(c, SPU2_R_MVOLXL));
        crc_u32(spu2_read_reg(c, SPU2_R_MVOLXR));
        crc_u32((uint32_t)spu2_irq_pending(c));
    }
}

/* --- Callbacks (their times go into the CRC) ------------------------------------ */

static uint8_t memin_ring[0x800];
static uint8_t trans_buf[2][0x2000];
static int callbacks_write;

static void memin_cb(int core, int half, void *user)
{
    int i;

    (void)user;
    crc_u32(0x4D454D00u | (uint32_t)core << 4 | (uint32_t)half);
    crc_u32((uint32_t)spu2_time());
    for (i = 0; i < 0x400; i++)
        memin_ring[half * 0x400 + i] = (uint8_t)rnd(256);
    if (callbacks_write)
        spu2_write_reg(core, SPU2_R_BVOLL, (uint16_t)(0x2000 + rnd(0x4000)), spu2_time());
}

static void trans_cb(int chan, void *user)
{
    (void)user;
    crc_u32(0x54524E00u | (uint32_t)chan);
    crc_u32((uint32_t)spu2_time());
    crc_u32(spu2_read_reg(chan, SPU2_R_VA(2) + SPU2_VA_NAX + 2));
    if (callbacks_write) {
        uint64_t when = spu2_time() + rnd(40);
        uint16_t pitch = (uint16_t)(0x800 + rnd(0x2000));

        spu2_write_reg(chan, SPU2_R_VP(5) + SPU2_VP_PITCH, pitch, when);
    }
}

static void irq_cb(int core, void *user)
{
    (void)user;
    crc_u32(0x49525100u | (uint32_t)core);
    crc_u32((uint32_t)spu2_time());
    crc_u32(spu2_read_reg(core, SPU2_R_VA(0) + SPU2_VA_NAX + 2));
    crc_u32(spu2_read_reg(core, SPU2_R_VP(0) + SPU2_VP_ENVX));
}

/* --- Scenes ------------------------------------------------------------------------ */

enum { SCENE_IDLE, SCENE_GAME, SCENE_HAZARD, SCENE_FULL48, SCENE_FULL24, SCENES };

static const char *const scene_name[SCENES] = {"idle", "game", "hazard", "full48", "full24"};

/* Golden CRCs, taken from the frame-by-frame renderer before S3 (commit
   a0a98982's spu2.c and adpcm.c). */
static const uint32_t scene_golden[SCENES] = {0x56AEEA19u, 0xDFAE210Eu, 0x7D9023DEu, 0x5BC48C99u,
                                              0xE301EF03u};

/* The random scenes of the differential check (--check): a seed picks
   everything, the scene renders 2 s with the chunked renderer and again
   with the reference one (spu2_set_exact), and the CRCs must agree. */
#define SCENE_RANDOM SCENES
#define RANDOM_SCENES 24

static int random_irq;

static void random_setup(uint32_t rseed)
{
    int c;
    int v;

    seed = rseed * 2654435761u + 1u;
    callbacks_write = (int)rnd(2);
    memset(memin_ring, 0, sizeof memin_ring);
    memset(trans_buf, 0, sizeof trans_buf);
    spu2_sd_set_time(0);
    spu2_reset();
    spu2_sd_init(0);
    spu2_set_irq_callback(irq_cb, NULL);
    spu2_set_trans_callback(0, trans_cb, NULL);
    spu2_set_trans_callback(1, trans_cb, NULL);
    spu2_set_dma_rate(rnd(3) ? 0 : 16 + rnd(200));
    load_sounds();
    for (c = 0; c < 2; c++) {
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLL | c, (uint16_t)rnd(0x8000));
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLR | c,
                          (uint16_t)(rnd(4) ? rnd(0x8000) : 0x8000 | rnd(0x8000)));
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXL | c, rnd(0x1000000));
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXR | c, rnd(0x1000000));
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | c, rnd(0x1000000));
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | c, rnd(0x1000000));
        spu2_sd_set_switch(SPU2_SD_SWITCH_PMON | c, rnd(0x1000000) & rnd(0x1000000));
        spu2_sd_set_switch(SPU2_SD_SWITCH_NON | c,
                           rnd(0x1000000) & rnd(0x1000000) & rnd(0x1000000));
        spu2_sd_set_core_attr(SPU2_SD_CORE_NOISE_CLK | c, (uint16_t)rnd(0x40));
        spu2_sd_set_param(SPU2_SD_PARAM_MMIX | c, (uint16_t)rnd(0x1000));
        spu2_sd_set_param(SPU2_SD_PARAM_AVOLL | c, (uint16_t)rnd(0x10000));
        spu2_sd_set_param(SPU2_SD_PARAM_AVOLR | c, (uint16_t)rnd(0x10000));
        spu2_sd_set_param(SPU2_SD_PARAM_BVOLL | c, (uint16_t)rnd(0x10000));
        spu2_sd_set_param(SPU2_SD_PARAM_BVOLR | c, (uint16_t)rnd(0x10000));
        if (rnd(4))
            reverb_on(c, (int)rnd(10), c ? 0x1DFFFF : 0x1FFFFF);
        for (v = 0; v < 24; v++)
            if (rnd(3))
                voice_start_random(c, v);
        key(c, rnd(0x1000000), 1);
    }
    if (rnd(2))
        spu2_memin_start((int)rnd(2), memin_ring, memin_cb, NULL);
    random_irq = rnd(3) == 0;
    if (random_irq) {
        c = (int)rnd(2);
        spu2_sd_set_addr(SPU2_SD_ADDR_IRQA | c, random_block_addr());
        spu2_sd_set_core_attr(SPU2_SD_CORE_IRQ_ENABLE | c, 1);
    }
}

static void random_tick(unsigned vs, int frames)
{
    uint64_t now = spu2_time();
    int events = (int)rnd(8);
    int e;

    for (e = 0; e < events; e++) {
        int c = (int)rnd(2);
        int v = (int)rnd(24);
        uint16_t sel = (uint16_t)VSEL(c, v);

        spu2_sd_set_time(rnd(2) ? now : now + rnd((uint32_t)frames + 50));
        switch (rnd(14)) {
        case 0:
        case 1:
            voice_start_random(c, v);
            key(c, 1u << v, 1);
            break;
        case 2:
            key(c, rnd(0x1000000), 0);
            break;
        case 3:
            spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | sel, (uint16_t)rnd(0x10000));
            break;
        case 4: {
            uint16_t vol = (uint16_t)rnd(0x10000);

            spu2_sd_set_param((rnd(2) ? SPU2_SD_VPARAM_VOLL : SPU2_SD_VPARAM_VOLR) | sel, vol);
            break;
        }
        case 5:
            spu2_sd_set_param(SPU2_SD_VPARAM_ENVX | sel, (uint16_t)rnd(0x10000));
            break;
        case 6:
            spu2_sd_set_switch(SPU2_SD_SWITCH_PMON | c, rnd(0x1000000));
            break;
        case 7:
            spu2_sd_set_switch(SPU2_SD_SWITCH_NON | c, rnd(0x1000000) & rnd(0x1000000));
            break;
        case 8:
            spu2_sd_set_addr(SPU2_SD_VADDR_NAX | sel, random_block_addr());
            break;
        case 9:
            spu2_sd_set_addr(SPU2_SD_VADDR_LSAX | sel, snd_addr[rnd(SOUNDS)]);
            break;
        case 10:
            spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | c, (uint16_t)rnd(2));
            break;
        case 11:
            spu2_sd_set_param(SPU2_SD_VPARAM_ADSR1 | sel, (uint16_t)rnd(0x10000));
            spu2_sd_set_param(SPU2_SD_VPARAM_ADSR2 | sel, (uint16_t)rnd(0x10000));
            break;
        case 12:
            spu2_sd_set_param(SPU2_SD_PARAM_MVOLL | c, (uint16_t)rnd(0x10000));
            break;
        default:
            if (!spu2_voice_trans_busy(c)) {
                int i;

                for (i = 0; i < 12 * 16; i++)
                    trans_buf[c][i] = (uint8_t)rnd(256);
                for (i = 0; i < 12; i++) {
                    trans_buf[c][i * 16] = random_header();
                    trans_buf[c][i * 16 + 1] = (uint8_t)(rnd(4) ? 0 : rnd(8));
                }
                spu2_sd_voice_trans(c, SPU2_SD_TRANS_WRITE, trans_buf[c], snd_addr[rnd(SOUNDS)],
                                    12 * 16);
            }
            break;
        }
    }
    if (random_irq && (vs % 4) == 0)
        spu2_irq_clear((int)rnd(2));
    spu2_sd_set_time(0);
}

static void scene_setup(int scene)
{
    int c;
    int v;

    seed = 0xC0FFEEu + (uint32_t)scene * 7919u;
    callbacks_write = 0;
    memset(memin_ring, 0, sizeof memin_ring);
    memset(trans_buf, 0, sizeof trans_buf);
    spu2_sd_set_time(0);
    spu2_reset();
    spu2_sd_init(0);
    spu2_set_irq_callback(irq_cb, NULL);
    spu2_set_trans_callback(0, trans_cb, NULL);
    spu2_set_trans_callback(1, trans_cb, NULL);
    load_sounds();
    for (c = 0; c < 2; c++) {
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLL | c, 0x3FFF);
        spu2_sd_set_param(SPU2_SD_PARAM_MVOLR | c, 0x3FFF);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXL | c, 0xFFFFFF);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXR | c, 0xFFFFFF);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | c, 0xFFFFFF);
        spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXER | c, 0xFFFFFF);
    }
    reverb_on(0, 4, 0x1FFFFF); /* studio large */
    reverb_on(1, 5, 0x1DFFFF); /* hall */
    if (scene == SCENE_IDLE)
        return;

    if (scene == SCENE_FULL48 || scene == SCENE_FULL24) {
        for (c = 0; c < 2; c++) {
            uint32_t mask = scene == SCENE_FULL48 || c == 0 ? 0xFFFFFF : 0;

            for (v = 0; v < 24; v++)
                if (mask & 1u << v)
                    voice_start(c, v, (int)rnd(SOUNDS) & ~4, 1); /* looping sounds */
            key(c, mask, 1);
        }
        return;
    }

    /* game / hazard: a mix of everything the SPU2 does */
    spu2_sd_set_core_attr(SPU2_SD_CORE_NOISE_CLK | 0, 0x2C);
    spu2_sd_set_core_attr(SPU2_SD_CORE_NOISE_CLK | 1, 0x13);
    spu2_sd_set_switch(SPU2_SD_SWITCH_NON | 0, 1u << 7);
    spu2_sd_set_switch(SPU2_SD_SWITCH_PMON | 0, (1u << 9) | (1u << 10));
    spu2_sd_set_switch(SPU2_SD_SWITCH_PMON | 1, 1u << 4);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXEL | 1, 0x0F0F0F);
    spu2_sd_set_switch(SPU2_SD_SWITCH_VMIXR | 1, 0xFFF0FF);
    spu2_sd_set_param(SPU2_SD_PARAM_AVOLL | 1, 0x6000);
    spu2_sd_set_param(SPU2_SD_PARAM_AVOLR | 1, 0x5000);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLL | 1, 0x4000);
    spu2_sd_set_param(SPU2_SD_PARAM_BVOLR | 1, 0x3000);
    spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 1, 0xFF3);
    spu2_sd_set_param(SPU2_SD_PARAM_MMIX | 0, 0xF0C);
    spu2_memin_start(1, memin_ring, memin_cb, NULL);
    for (c = 0; c < 2; c++) {
        for (v = 0; v < 24; v += 2)
            voice_start(c, v, (int)rnd(SOUNDS), 0);
        key(c, 0x555555, 1);
    }
    if (scene == SCENE_HAZARD) {
        static uint8_t blk[16 * 64];

        /* IRQ on core 0 at a block a looping voice reads, with the
           callback reading live state; a voice whose sound sits in the
           write-back area; one in core 1's reverb work area */
        spu2_sd_set_addr(SPU2_SD_ADDR_IRQA | 0, snd_addr[0] + 16 * 20);
        spu2_sd_set_core_attr(SPU2_SD_CORE_IRQ_ENABLE | 0, 1);
        make_sound(blk, 64, 0, 0, 0);
        spu2_dma_write(0x1000, blk, sizeof blk); /* halfword 0x800: core 0's output */
        voice_start(0, 1, 0, 1);
        spu2_sd_set_addr(SPU2_SD_VADDR_SSA | VSEL(0, 1), 0x1000);
        spu2_dma_write(0x1D8000, blk, sizeof blk); /* inside core 1's hall area */
        voice_start(1, 3, 0, 1);
        spu2_sd_set_addr(SPU2_SD_VADDR_SSA | VSEL(1, 3), 0x1D8000);
        key(0, 1u << 1, 1);
        key(1, 1u << 3, 1);
        spu2_set_dma_rate(48);
        callbacks_write = 1;
    }
}

/* One vsync of driver activity (the sound tick lands at the block start;
   some writes are stamped later in the block, as a finer host would). */
static void scene_tick(int scene, unsigned vs, int frames)
{
    uint64_t now = spu2_time();
    int c;
    int v;

    if (scene == SCENE_IDLE)
        return;
    if (scene == SCENE_RANDOM) {
        random_tick(vs, frames);
        return;
    }
    c = (int)rnd(2);
    v = (int)rnd(24);
    spu2_sd_set_time(now);
    if (scene == SCENE_FULL48 || scene == SCENE_FULL24) {
        /* light churn: one pitch bend, a retrigger every 8 vsyncs */
        if (scene == SCENE_FULL24)
            c = 0;
        spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | VSEL(c, v), (uint16_t)(0x0400 + rnd(0x3800)));
        if ((vs & 7) == 0) {
            voice_start(c, v, (int)rnd(SOUNDS) & ~4, 1);
            key(c, 1u << v, 1);
        }
        spu2_sd_set_time(0);
        return;
    }

    switch (rnd(6)) {
    case 0:
    case 1:
        voice_start_random(c, v);
        key(c, 1u << v, 1);
        break;
    case 2:
        key(c, 1u << v, 0);
        break;
    case 3:
        spu2_sd_set_param(SPU2_SD_VPARAM_PITCH | VSEL(c, v), (uint16_t)rnd(0x5000));
        break;
    case 4:
        /* sweeps: voice volume, master */
        spu2_sd_set_param(SPU2_SD_VPARAM_VOLL | VSEL(c, v), (uint16_t)(0x8000 | rnd(0x8000)));
        if (rnd(4) == 0)
            spu2_sd_set_param(SPU2_SD_PARAM_MVOLR | c, (uint16_t)(0x8000 | rnd(0x8000)));
        break;
    default:
        /* ENVX on a voice that may have ended (it sounds again) */
        spu2_sd_set_param(SPU2_SD_VPARAM_ENVX | VSEL(c, v), (uint16_t)rnd(0x8000));
        break;
    }
    if (rnd(3) == 0) {
        int on;

        /* a write inside the block */
        spu2_sd_set_time(now + rnd((uint32_t)frames));
        on = (int)rnd(2);
        key(c ^ 1, 1u << rnd(24), on);
    }
    if ((vs % 25) == 3) {
        /* a sample upload over sound 2 (voices may be playing it) */
        int ch = (int)(vs / 25) & 1;
        int i;

        for (i = 0; i < 12 * 16; i++)
            trans_buf[ch][i] = (uint8_t)rnd(256);
        for (i = 0; i < 12; i++) {
            trans_buf[ch][i * 16] = random_header();
            trans_buf[ch][i * 16 + 1] = 0;
        }
        trans_buf[ch][11 * 16 + 1] = ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT;
        spu2_sd_set_time(now + rnd(100));
        spu2_sd_voice_trans(ch, SPU2_SD_TRANS_WRITE, trans_buf[ch], snd_addr[2], 12 * 16);
    }
    if ((vs % 50) == 7)
        spu2_sd_set_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 1, (uint16_t)((vs / 50) & 1));
    if (scene == SCENE_HAZARD && (vs % 3) == 0)
        spu2_irq_clear(0);
    spu2_sd_set_time(0);
}

static int scene_frames(int scene, unsigned vs)
{
    if (scene == SCENE_GAME || scene == SCENE_HAZARD || scene == SCENE_RANDOM) {
        /* 59.94 Hz: 800 or 801 (audio_host.c) */
        return (int)((uint64_t)(vs + 1) * 48000u * 1001u / 60000u -
                     (uint64_t)vs * 48000u * 1001u / 60000u);
    }
    return 960;
}

static int16_t out[1024 * 2];

static double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER c;
    LARGE_INTEGER f;

    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart * 1e3 / (double)f.QuadPart;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
#endif
}

static double vsync_ms[2048];

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return x < y ? -1 : x > y;
}

/* Render 10 s of a scene (a random scene: 2 s of seed `rseed`); returns
   the CRC, and the median, mean and worst vsync. */
static uint32_t scene_run(int scene, uint32_t rseed, double *mean_ms, double *max_ms,
                          double *median_ms)
{
    unsigned vs = 0;
    uint64_t total = 0;
    uint64_t length = scene == SCENE_RANDOM ? 2u * SPU2_RATE : 10u * SPU2_RATE;
    double sum = 0;
    double worst = 0;

    crc = 0xFFFFFFFFu;
    memset(part_crc, 0xFF, sizeof part_crc);
    if (scene == SCENE_RANDOM)
        random_setup(rseed);
    else
        scene_setup(scene);
    while (total < length) {
        int frames = scene_frames(scene, vs);
        double t0;
        double dt;

        scene_tick(scene, vs, frames);
        crc_part = PART_CALLBACKS;
        t0 = now_ms();
        spu2_render(out, frames);
        dt = now_ms() - t0;
        crc_part = PART_SAMPLES;
        sum += dt;
        if (vs < 2048)
            vsync_ms[vs] = dt;
        if (dt > worst)
            worst = dt;
        crc_samples(out, frames * 2);
        crc_part = PART_REGS;
        crc_state();
        crc_part = PART_SAMPLES;
        if (trace) {
            part_crc[PART_RAM] = crc_step(0xFFFFFFFFu, spu2_ram(), SPU2_RAM_SIZE);
            printf("vs %4u frames %3d seed %08X crc %08X samples %08X regs %08X callbacks "
                   "%08X ram %08X\n",
                   vs, frames, (unsigned)seed, (unsigned)crc, (unsigned)part_crc[PART_SAMPLES],
                   (unsigned)part_crc[PART_REGS], (unsigned)part_crc[PART_CALLBACKS],
                   (unsigned)part_crc[PART_RAM]);
        }
        total += (uint64_t)frames;
        vs++;
    }
    crc_part = PART_RAM;
    crc_bytes(spu2_ram(), SPU2_RAM_SIZE);
    crc_part = PART_SAMPLES;
    if (mean_ms)
        *mean_ms = sum / vs;
    if (max_ms)
        *max_ms = worst;
    if (median_ms) {
        unsigned m = vs < 2048 ? vs : 2048;

        qsort(vsync_ms, m, sizeof vsync_ms[0], cmp_double);
        *median_ms = vsync_ms[m / 2];
    }
    return crc ^ 0xFFFFFFFFu;
}

#ifdef SPU2_PROFILE

static void profile_print(void)
{
    double ns[SPU2_PROF_STAGES];
    double total = 0;
    double per_lap = spu2_prof_lap_cost();
    int i;

    for (i = 0; i < SPU2_PROF_STAGES; i++) {
        ns[i] = (double)spu2_prof.ns[i] - per_lap * (double)spu2_prof.laps[i];
        if (ns[i] < 0)
            ns[i] = 0;
        total += ns[i];
    }
    printf("    stage profile (ms per vsync, lap overhead %.1f ns subtracted):\n", per_lap);
    for (i = 0; i < SPU2_PROF_STAGES; i++)
        printf("      %-10s %7.3f\n", spu2_prof_stage_name(i),
               ns[i] / 1e6 / (double)spu2_prof.vsyncs);
    printf("      %-10s %7.3f\n", "sum", total / 1e6 / (double)spu2_prof.vsyncs);
}

#endif

int main(int argc, char **argv)
{
    int check = argc > 1 && strcmp(argv[1], "--check") == 0;
    int print = argc > 1 && strcmp(argv[1], "--print") == 0;
    int failures = 0;
    int s;

    crc_init();
    if (argc > 2 && strcmp(argv[1], "--trace-crc") == 0) {
        /* --trace-crc <scene name | random seed>: the CRCs after every vsync
           (diff two builds' output to find the first divergence) */
        int scene = SCENE_RANDOM;
        uint32_t rseed = 0;

        for (s = 0; s < SCENES; s++)
            if (strcmp(argv[2], scene_name[s]) == 0)
                scene = s;
        if (scene == SCENE_RANDOM)
            rseed = (uint32_t)strtoul(argv[2], NULL, 0);
        trace = 1;
        printf("%s crc 0x%08X\n", argv[2], (unsigned)scene_run(scene, rseed, NULL, NULL, NULL));
        return 0;
    }
    if (check || print) {
        for (s = 0; s < SCENES; s++) {
            uint32_t got = scene_run(s, 0, NULL, NULL, NULL);

            printf("%-8s crc 0x%08X", scene_name[s], (unsigned)got);
            if (check && got != scene_golden[s]) {
                printf("  FAIL: want 0x%08X", (unsigned)scene_golden[s]);
                failures++;
            }
            printf("\n");
        }
        if (print) {
            uint32_t r;

            for (r = 1; r <= RANDOM_SCENES; r++)
                printf("random %-2u crc 0x%08X\n", (unsigned)r,
                       (unsigned)scene_run(SCENE_RANDOM, r, NULL, NULL, NULL));
        }
        if (check) {
            spu2_stats st;
            uint32_t r;

            for (r = 1; r <= RANDOM_SCENES; r++) {
                uint32_t fast = scene_run(SCENE_RANDOM, r, NULL, NULL, NULL);
                uint32_t ref;

                spu2_set_exact(1);
                ref = scene_run(SCENE_RANDOM, r, NULL, NULL, NULL);
                spu2_set_exact(0);
                if (fast != ref) {
                    printf("random scene %u: chunked crc 0x%08X, frame by frame 0x%08X  FAIL\n",
                           (unsigned)r, (unsigned)fast, (unsigned)ref);
                    failures++;
                }
            }
            spu2_get_stats(&st);
            printf("%d random scenes, chunked and frame by frame compared\n", RANDOM_SCENES);
            printf("frames voice by voice %llu in %llu chunks, frame by frame %llu, hazards "
                   "%llu\n",
                   (unsigned long long)st.chunked_frames, (unsigned long long)st.chunks,
                   (unsigned long long)st.exact_frames, (unsigned long long)st.hazards);
            printf("spu2_bench: %d checks, %d failures\n", SCENES + RANDOM_SCENES, failures);
        }
        return failures != 0;
    }

    for (s = 0; s < SCENES; s++) {
        double mean;
        double worst;
        double median;
        uint32_t got;

#ifdef SPU2_PROFILE
        spu2_prof_reset();
#endif
        got = scene_run(s, 0, &mean, &worst, &median);
        printf("%-8s %6.3f ms per vsync median, %6.3f mean, %6.3f worst; crc 0x%08X\n",
               scene_name[s], median, mean, worst, (unsigned)got);
#ifdef SPU2_PROFILE
        profile_print();
#endif
    }
    return 0;
}
