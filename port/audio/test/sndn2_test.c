/*
 * port/audio/test/sndn2_test.c
 *
 * The SNDN2DRV host (sndn2_host.c, stream.c) and the Sg sequencer on the
 * host (sce/libsndn2/sound.c), checked against R1's reading of the IRX:
 *
 *   - transport: the init call (0x65) and tick calls (0x64) through the
 *     host SIF, the two alternating reply pages, the transfer counter at
 *     +0x1C0 written into both by the DMA-done callback;
 *   - voice, key, reverb, output and core packets as SPU2 register effects;
 *   - sample upload (0x20) and read-back (0x21) through IOP RAM;
 *   - the pitch computation, its clamp, the disc's table (when the image is
 *     given and present) and the formula fallback;
 *   - an ADPCM stream of a synthetic stereo .int ring through the half-fill
 *     scheme: one transfer per tick, de-interleaving, loop-flag patching,
 *     key-on on the third tick, read offsets lagging by a tick, refills as
 *     NAX crosses into each half, stop;
 *   - the PCM mixer: L/R sides, 16-bit wrapping adds, stop offset, the
 *     sample-step shift, read offsets in the reply;
 *   - a sound effect played through SgVabOpenFakeBody / SgSePlay /
 *     SgCalledTickProc on a synthetic .hd, so the sequencer's pointer seams
 *     (head context, header records, sequence bodies) run on this host.
 *
 * Usage: sndn2_test [disc image]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sifrpc.h>
#include <sound.h>

#include "adpcm.h"
#include "iop_ram.h"
#include "libsd_irx.h"
#include "sif_host.h"
#include "sndn2_host.h"
#include "spu2.h"
#include "spu2_sd.h"
#include "vfs.h"

static int failures;
static int checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* --- a packet page and the tick ------------------------------------------- */

static uint8_t page[256 * 16];
static int npk;

static void pk(uint32_t cmd, uint32_t id, uint32_t w2, uint32_t w3)
{
    put32(page + npk * 16, cmd);
    put32(page + npk * 16 + 4, id);
    put32(page + npk * 16 + 8, w2);
    put32(page + npk * 16 + 12, w3);
    npk++;
}

static sceSifRpcClientData client;
static uint8_t reply[ICO_SNDN2_REPLY_SIZE];

/* Send the packets queued with pk() as one tick; the reply lands in reply[]. */
static void tick(void)
{
    memset(reply, 0xAA, sizeof(reply));
    CHECK(sceSifCallRpc(&client, ICO_SNDN2_RPC_TICK, 1, page, npk * 16, reply, ICO_SNDN2_REPLY_SIZE,
                        NULL, NULL) == 0);
    npk = 0;
}

static int16_t scratch[2 * 4096];

static void render(int frames)
{
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;

        spu2_render(scratch, n);
        frames -= n;
    }
}

/* One vsync of the game: the audio, then the sound thread's tick. */
static void vsync(void)
{
    render(960);
    tick();
}

static void start(void)
{
    ico_sif_host_reset();
    ico_sndn2_host_reset();
    ico_sndn2_host_register();
    memset(&client, 0, sizeof(client));
    CHECK(sceSifBindRpc(&client, ICO_SNDN2_SERVER_ID, 0) == 0 && client.serve != 0);
    npk = 0;
}

static uint16_t vsel(int slot, uint16_t param)
{
    return (uint16_t)(param | SPU2_SD_VOICE(slot / 24, slot % 24));
}

/* A looping ADPCM sample of `blocks` blocks at IOP address `iop`: every
   sample the constant `v` (shift 0, filter 0, nibble v >> 12... as data). */
static void make_loop(uint8_t *dst, int blocks, uint8_t nibbles)
{
    int b;

    for (b = 0; b < blocks; b++) {
        uint8_t *blk = dst + b * 16;

        memset(blk, nibbles, 16);
        blk[0] = 0x00;
        blk[1] = (uint8_t)((b == 0 ? ADPCM_FLAG_LOOP_START : 0) |
                           (b == blocks - 1 ? ADPCM_FLAG_LOOP_END | ADPCM_FLAG_LOOP_REPEAT : 0));
    }
}

/* --- transport, init, voices ---------------------------------------------- */

static void test_transport_and_voices(void)
{
    uint8_t init[0x40];
    uint8_t ret[0x40];
    const uint8_t *p1;
    const uint8_t *p2;
    uint32_t iop;
    uint8_t *ram;
    int i;

    start();
    /* RPC 0x65: {0x1E, hot}; the reply is the return word and zeros */
    memset(init, 0, sizeof(init));
    put32(init, 0x1E);
    memset(ret, 0xAA, sizeof(ret));
    CHECK(sceSifCallRpc(&client, ICO_SNDN2_RPC_INIT, 0, init, 0x40, ret, 0x40, NULL, NULL) == 0);
    for (i = 0; i < 0x40 && ret[i] == 0; i++) {}
    CHECK(i == 0x40);
    spu2_apply_pending();
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXEL | 0) == 0);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXER | 1) == 0);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXL | 0) == 0xFFFFFF);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MMIX | 0) == 0xFF0);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MMIX | 1) == 0xFFC);

    /* upload a looping sample: IOP block -> SPU 0x10000 (bytes 0x800-0x3FFF
       are the SPU2's output write-back area), counter 7 */
    iop = ico_iop_heap_alloc(0x100);
    CHECK(iop != 0);
    make_loop(ico_iop_ptr(iop), 16, 0x77);
    pk(0x20, 7u << 8 | (iop >> 16 & 0xFF), iop << 16 | (0x10000 >> 8 & 0xFFFF), 0x100);
    tick();
    p1 = ico_sndn2_host_last_reply();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_XFER) == 0); /* not done before the render */
    render(2);
    ram = spu2_ram();
    CHECK(memcmp(ram + 0x10000, ico_iop_ptr(iop), 0x100) == 0);
    tick();
    p2 = ico_sndn2_host_last_reply();
    CHECK(p1 != p2); /* the two pages alternate */
    CHECK(rd32(reply + ICO_SNDN2_REPLY_XFER) == 7);
    CHECK(rd32(p1 + ICO_SNDN2_REPLY_XFER) == 7); /* written into both */
    tick();
    CHECK(ico_sndn2_host_last_reply() == p1);
    for (i = 0x1C4; i < 0x200 && reply[i] == 0; i++) {}
    CHECK(i == 0x200);

    /* read it back (0x21, counter 8) into another IOP block */
    {
        uint32_t back = ico_iop_heap_alloc(0x100);

        memset(ico_iop_ptr(back), 0, 0x100);
        pk(0x21, 8u << 8 | (back >> 16 & 0xFF), back << 16 | (0x10000 >> 8 & 0xFFFF), 0x100);
        tick();
        render(2);
        tick();
        CHECK(rd32(reply + ICO_SNDN2_REPLY_XFER) == 8);
        CHECK(memcmp(ico_iop_ptr(back), ico_iop_ptr(iop), 0x100) == 0);
    }

    /* voice 25 (core 1, voice 1): volume, ADSR, start address, pitch, key on */
    pk(0x01, 25, 0x1234, 0x2345);
    pk(0x02, 25, 0x00FF, 0x1FC0);
    pk(0x03, 25, 0x10000, 0);
    pk(0x04, 25, 60u << 24 | 60u << 16 | 0u << 8 | 64, 2u << 24 | 0x1000);
    pk(0x0A, 0, 0, 1u << 1);
    tick();
    spu2_apply_pending();
    CHECK(spu2_sd_get_param(vsel(25, SPU2_SD_VPARAM_VOLL)) == 0x1234);
    CHECK(spu2_sd_get_param(vsel(25, SPU2_SD_VPARAM_VOLR)) == 0x2345);
    CHECK(spu2_sd_get_param(vsel(25, SPU2_SD_VPARAM_ADSR1)) == 0x00FF);
    CHECK(spu2_sd_get_param(vsel(25, SPU2_SD_VPARAM_ADSR2)) == 0x1FC0);
    CHECK(spu2_sd_get_addr(vsel(25, SPU2_SD_VADDR_SSA)) == 0x10000);
    CHECK(spu2_sd_get_param(vsel(25, SPU2_SD_VPARAM_PITCH)) == 4096 * 441 / 480);
    render(200);
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX + 25 * 4) > 0x1000);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX + 24 * 4) == 0);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX + 25 * 4) <= 0x7FFF);
    /* key off (SgQuit's shape: id ignored, w2 core 0, w3 core 1) */
    pk(0x0B, 1, 0, 1u << 1);
    tick();
    render(48000);
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX + 25 * 4) < 2);
}

/* --- core, reverb, output -------------------------------------------------------- */

static void test_core_packets(void)
{
    spu2_sd_effect_attr attr;

    start();
    pk(0x1E, 0, 0, 0);
    pk(0x14, 0, 0x1FFFFF, 0);
    pk(0x14, 1, 0x1DFFFF, 0);
    pk(0x15, 0, 4 | 0x100, 0); /* the clear bit is dropped */
    pk(0x16, 0, 0xCCC, 0xAAA);
    pk(0x15, 1, 4, 0);
    pk(0x28, 1, 0x3FFF, 0x2FFF);
    pk(0x0C, 0, 0x000003, 0x800000);
    pk(0x0D, 0, 0x000010, 0x000001);
    pk(0x32, 8, 30, 0x15);  /* noise clock: slot 30 is core 1 */
    pk(0x32, 10, 0x880, 0); /* S/PDIF: no effect */
    pk(0x4E, 8, 0, 0);
    tick();
    spu2_apply_pending();
    spu2_sd_get_effect_attr(0, &attr);
    CHECK(attr.mode == 4);
    CHECK(spu2_sd_get_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 0) == 1);
    CHECK(spu2_sd_get_core_attr(SPU2_SD_CORE_EFFECT_ENABLE | 1) == 1);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_EVOLL | 0) == 0xCCC);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_EVOLR | 0) == 0xAAA);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_EVOLL | 1) == 0); /* 0x15 sets depth 0 */
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MVOLL | 1) == 0x3FFF);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MVOLR | 1) == 0x2FFF);
    CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_EEA | 1) == 0x1DFFFF);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXEL | 0) == 3);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXER | 0) == 3);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_VMIXEL | 1) == 0x800000);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_NON | 0) == 0x10);
    CHECK(spu2_sd_get_switch(SPU2_SD_SWITCH_NON | 1) == 1);
    CHECK(spu2_sd_get_core_attr(SPU2_SD_CORE_NOISE_CLK | 1) == 0x15);
    CHECK(spu2_sd_get_core_attr(SPU2_SD_CORE_NOISE_CLK | 0) == 0);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MMIX | 0) == 0xFFC0);
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_MMIX | 1) == 0xFFCC);
    /* 0x1F drops the DMA callback: the counter stops updating */
    pk(0x1F, 0, 0, 0);
    pk(0x20, 3u << 8, 0x10000u << 16, 0x100);
    tick();
    render(4);
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_XFER) == 0);
}

/* --- pitch -------------------------------------------------------------------- */

static uint32_t pw2(int base, int note, int fine, int bend)
{
    return (uint32_t)base << 24 | (uint32_t)note << 16 | (uint32_t)(fine & 0xFF) << 8 |
           (uint32_t)bend;
}

static void test_pitch(const char *iso)
{
    unsigned c0;
    IcoVfs *disc;

    start();
    ico_vfs_set_disc(NULL);
    CHECK(ico_sndn2_pitch_load() == ICO_SNDN2_PITCH_FORMULA);
    CHECK(ico_sndn2_pitch_table()[208] == 0x1000);
    CHECK(ico_sndn2_pitch_table()[400] == 0x2000);
    CHECK(ico_sndn2_pitch_table()[16] == 0x800);
    /* unity, an octave up, an octave down (T[400] >> 2), a fifth (T[208 +
       7 * 16]), fine, the 441/480 step and the scale */
    CHECK(ico_sndn2_pitch_compute(pw2(60, 60, 0, 64), 0x1000) == 4096 * 441 / 480);
    CHECK(ico_sndn2_pitch_compute(pw2(60, 72, 0, 64), 0x1000) == 8192 * 441 / 480);
    CHECK(ico_sndn2_pitch_compute(pw2(60, 48, 0, 64), 0x1000) == (8192 >> 2) * 441 / 480);
    CHECK(ico_sndn2_pitch_compute(pw2(60, 67, 0, 64), 0x1000) ==
          ico_sndn2_pitch_table()[208 + 112] * 441u / 480u);
    CHECK(ico_sndn2_pitch_compute(pw2(60, 60, -3, 64), 0x1000) ==
          ico_sndn2_pitch_table()[205] * 441u / 480u);
    CHECK(ico_sndn2_pitch_compute(pw2(60, 60, 0, 64), 0x2000) == 2 * (4096 * 441 / 480));
    /* bend 127, range 2: (63 * 2) >> 2 = 31 sixteenths up */
    CHECK(ico_sndn2_pitch_compute(pw2(60, 60, 0, 127), 2u << 24 | 0x1000) ==
          ico_sndn2_pitch_table()[239] * 441u / 480u);
    /* bend 0, range 255: far below the table: clamped and counted */
    c0 = ico_sndn2_pitch_clamps();
    CHECK(ico_sndn2_pitch_compute(pw2(60, 60, 0, 0), 255u << 24 | 0x1000) ==
          ico_sndn2_pitch_table()[0] * 441u / 480u);
    CHECK(ico_sndn2_pitch_clamps() == c0 + 1);

    /* the disc's table: 32 entries differ from the formula, 30 by one and
       two (64, 172) by 75 and 256: hand-made data */
    disc = iso != NULL ? ico_vfs_mount(&ico_vfs_iso9660, iso) : NULL;
    if (disc == NULL) {
        printf("sndn2_test: no disc image; the disc's pitch table is not checked\n");
        return;
    }
    ico_vfs_set_disc(disc);
    CHECK(ico_sndn2_pitch_load() == ICO_SNDN2_PITCH_IRX);
    {
        const uint16_t *t = ico_sndn2_pitch_table();
        int i;
        int differ = 0;
        int worst = 0;

        CHECK(t[208] == 0x1000);
        for (i = 0; i < ICO_SNDN2_PITCH_ENTRIES; i++) {
            int d = (int)t[i] - (int)ico_sndn2_pitch_formula(i);

            if (d != 0) {
                differ++;
            }
            if (d * d > worst * worst) {
                worst = d;
            }
        }
        printf("sndn2_test: disc pitch table: %d of 608 entries differ from the formula, by at "
               "most %d\n",
               differ, worst);
        CHECK(differ == 32);
        CHECK(t[64] == 2360 && t[172] == 3340);
    }
    /* the disc's libsd values: the presets and
       sizes are ps2sdk's; against the built-in psx-spx table they differ in
       modes 0 (off), 1 (room), 7 (echo) and 8 (delay) only, and the idle
       block is ps2sdk's */
    CHECK(ico_libsd_load() == ICO_LIBSD_DISC);
    {
        const spu2_reverb_preset *p = spu2_reverb_get_preset(SPU2_SD_EFFECT_MODE_STUDIO_3);
        int m;

        CHECK(p->size == 0x6FE0 && p->regs[0] == 0x00E3 && p->regs[31] == 0x8000);
        CHECK(memcmp(spu2_sd_idle_block(), "\a\a\a\a\a\a\a\a\a\a\a\a\a\a\a\a", 16) == 0);
        for (m = 0; m < SPU2_REVERB_MODES; m++) {
            static const uint32_t sizes[SPU2_REVERB_MODES] = {
                0x10, 0x26C0, 0x1F40, 0x4840, 0x6FE0, 0xADE0, 0xF6C0, 0x18040, 0x18040, 0x3C00,
            };

            CHECK(spu2_reverb_get_preset(m)->size == sizes[m]);
        }
        /* room's comb 3/4 and diffusion addresses: ps2sdk's, not psx-spx's */
        CHECK(spu2_reverb_get_preset(1)->regs[18] == 0x0335);
    }
    ico_libsd_apply(NULL);
    ico_vfs_set_disc(NULL);
    ico_vfs_unmount(disc);
}

/* --- ADPCM stream ---------------------------------------------------------------- */

#define RING 0x5C000u
#define SPU_RING 0x4000u

/* The stereo .int ring: each 0x800-byte sector is 0x400 bytes of the left
   channel's blocks then 0x400 of the right's.  Every block carries its
   sector, channel and block-in-chunk in its data bytes. */
static void make_int(uint8_t *ring)
{
    uint32_t sec;
    int ch;
    int b;

    for (sec = 0; sec < RING / 0x800; sec++) {
        for (ch = 0; ch < 2; ch++) {
            for (b = 0; b < 0x400 / 16; b++) {
                uint8_t *blk = ring + sec * 0x800 + (uint32_t)ch * 0x400 + (uint32_t)b * 16;

                memset(blk, 0x11, 16);
                blk[0] = 0x00;
                blk[1] = 0x00;
                blk[2] = (uint8_t)sec;
                blk[3] = (uint8_t)ch;
                blk[4] = (uint8_t)b;
            }
        }
    }
}

/* SPU RAM at `spu` holds the channel's `len` bytes from IOP ring offset
   `off`, with the first and last block flags given. */
static int spu_matches(uint32_t spu, uint32_t off, int ch, uint32_t len, int first, int last)
{
    const uint8_t *ram = spu2_ram();
    uint32_t i;

    for (i = 0; i < len; i += 16) {
        uint32_t sec = (off + (i / 0x400) * 0x800) / 0x800;
        const uint8_t *blk = ram + spu + i;
        int flags = i == 0 ? first : i == len - 16 ? last : 0;

        if (blk[1] != flags || blk[2] != (uint8_t)sec || blk[3] != ch ||
            blk[4] != (uint8_t)((i % 0x400) / 16)) {
            fprintf(stderr, "spu 0x%X + 0x%X: flags %d sec %d ch %d b %d\n", spu, i, blk[1], blk[2],
                    blk[3], blk[4]);
            return 0;
        }
    }
    return 1;
}

static uint32_t stream_off(int slot)
{
    return rd32(reply + ICO_SNDN2_REPLY_STREAM + 4 * slot);
}

static void test_adpcm_stream(void)
{
    uint32_t ring = ico_iop_heap_alloc(RING);
    uint32_t spu0 = 0x1E0000, spu1 = 0x1E4000;
    int slot;
    int t;
    int half2_seen = 0;

    start();
    pk(0x1E, 0, 0, 0);
    tick();
    CHECK(ring != 0);
    make_int(ico_iop_ptr(ring));
    /* adpcm_init.c's two requests, packed as SgStAdpcmOpen does:
       w1 = ch << 24 | attr & 0xFF0000 | spuSize & 0xFF00 | spuAddr >> 16 & 0xFF */
    pk(0x3C, 0, 0, 0);
    for (slot = 0; slot < 2; slot++) {
        uint32_t spu = slot == 0 ? spu0 : spu1;
        uint32_t iop = ring + 0x400u * (uint32_t)slot;

        pk(0x3E, (uint32_t)slot << 24 | 0x20000 | (SPU_RING & 0xFF00) | (spu >> 16 & 0xFF),
           spu << 16 | (RING >> 8 & 0xFFFF), RING << 24 | (iop & 0xFFFFFF));
    }
    pk(0x40, 1, 0, 0x3FFFu << 16 | 0);
    pk(0x40, 2, 0, 0u << 16 | 0x3FFF);
    pk(0x41, 3, 0, 48000);
    pk(0x42, 3, 0, 0);
    tick(); /* tick 1: FILL slot 0 */
    spu2_apply_pending();
    CHECK(spu2_sd_get_addr(vsel(0, SPU2_SD_VADDR_SSA)) == spu0);
    CHECK(spu2_sd_get_addr(vsel(1, SPU2_SD_VADDR_SSA)) == spu1);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_PITCH)) == 0x1000);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_VOLL)) == 0x3FFF);
    CHECK(spu2_sd_get_param(vsel(1, SPU2_SD_VPARAM_VOLR)) == 0x3FFF);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_ADSR1)) == 0x8080);
    CHECK(stream_off(0) == 0 && stream_off(1) == 0);
    vsync(); /* tick 2: slot 0's offset moves; FILL slot 1 */
    CHECK(spu_matches(spu0, 0, 0, SPU_RING / 2, 6, 2));
    CHECK(stream_off(0) == SPU_RING / 2 * 2 && stream_off(1) == 0);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX) == 0); /* not keyed on yet */
    vsync();                                        /* tick 3: slot 1's offset; KEYON */
    CHECK(spu_matches(spu1, 0, 1, SPU_RING / 2, 6, 2));
    CHECK(stream_off(1) == SPU_RING);
    vsync(); /* both voices play their first half; FILLs of the second halves */
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX) > 0);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX + 4) > 0);
    vsync();
    CHECK(spu_matches(spu0 + SPU_RING / 2, SPU_RING, 0, SPU_RING / 2, 2, 3));
    vsync();
    CHECK(spu_matches(spu1 + SPU_RING / 2, SPU_RING, 1, SPU_RING / 2, 2, 3));
    CHECK(stream_off(0) == 2 * SPU_RING && stream_off(1) == 2 * SPU_RING);
    /* the first half (14336 samples at 48 kHz, 15 vsyncs) ends: the voice
       enters the second half and the first is refilled from 0x8000 */
    for (t = 0; t < 40; t++) {
        vsync();
        if (!half2_seen && stream_off(0) == 3 * SPU_RING) {
            half2_seen = 1;
            CHECK(spu_matches(spu0, 2 * SPU_RING, 0, SPU_RING / 2, 6, 2));
        }
    }
    CHECK(half2_seen);
    /* the voices loop over the ring and keep sounding */
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX) > 0x1000);
    CHECK(stream_off(0) == stream_off(1));
    /* stop: offsets cleared, voices released */
    pk(0x43, 3, 0, 0);
    tick();
    CHECK(stream_off(0) == 0 && stream_off(1) == 0);
    render(48000);
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_ENVX) < 2);
    pk(0x3F, 0, 0, 0);
    pk(0x3F, 1, 0, 0);
    tick();
    ico_iop_heap_free(ring);
}

/* The wrap of the read offset in a 0x5C000 ring: a mono stream (one
   0x2000 refill per half) comes back to 0 after 46 refills. */
static void test_adpcm_wrap(void)
{
    uint32_t ring = ico_iop_heap_alloc(RING);
    int t;
    uint32_t prev = 0;
    int wrapped = 0;

    start();
    pk(0x1E, 0, 0, 0);
    pk(0x3C, 0, 0, 0);
    pk(0x3E, 2u << 24 | 0x10000 | (SPU_RING & 0xFF00) | 0x1E, 0x8000u << 16 | (RING >> 8),
       RING << 24 | (ring & 0xFFFFFF));
    pk(0x41, 4, 0, 192000); /* 4x: (192000 << 12) / 48000 = 0x4000 */
    pk(0x42, 4, 0, 0);
    tick();
    spu2_apply_pending();
    CHECK(spu2_sd_get_param(vsel(2, SPU2_SD_VPARAM_PITCH)) == 0x4000);
    for (t = 0; t < 400; t++) {
        vsync();
        if (stream_off(2) < prev) {
            wrapped = 1;
            CHECK(prev == RING - SPU_RING / 2);
        }
        CHECK(stream_off(2) % (SPU_RING / 2) == 0 && stream_off(2) < RING);
        prev = stream_off(2);
    }
    CHECK(wrapped);
    ico_iop_heap_free(ring);
}

/* 0x248C, the stream cancel:
   a stop in the tick of its play takes the voice's bit out of the queued
   KEYON (for a core 0 voice the same-numbered core 1 voice's bit too), and
   a stop forgets the pending fill, so the read offset stays at 0. */
static void open_mono(uint32_t ring, int slot, uint32_t spu)
{
    pk(0x3E, (uint32_t)slot << 24 | 0x10000 | (SPU_RING & 0xFF00) | (spu >> 16 & 0xFF),
       spu << 16 | (RING >> 8), RING << 24 | (ring & 0xFFFFFF));
    pk(0x40, slot < 24 ? 1u << slot : 0, slot < 24 ? 0 : 1u << (slot - 24), 0x3FFFu << 16 | 0x3FFF);
    pk(0x41, slot < 24 ? 1u << slot : 0, slot < 24 ? 0 : 1u << (slot - 24), 48000);
}

static uint32_t envx(int slot)
{
    return rd32(reply + ICO_SNDN2_REPLY_ENVX + 4 * slot);
}

static void test_adpcm_cancel(void)
{
    uint32_t ring = ico_iop_heap_alloc(RING);
    int t;

    CHECK(ring != 0);
    make_int(ico_iop_ptr(ring));

    /* core 1 stop: core 0's voice 0 still keys on */
    start();
    pk(0x1E, 0, 0, 0);
    pk(0x3C, 0, 0, 0);
    open_mono(ring, 0, 0x1E0000);
    open_mono(ring, 24, 0x1E4000);
    pk(0x42, 1, 1, 0);
    pk(0x43, 0, 1, 0);
    tick();
    for (t = 0; t < 4; t++) {
        vsync();
    }
    CHECK(envx(0) > 0);
    CHECK(envx(24) == 0);

    /* core 0 stop: voice 0 of core 1, not stopped, loses its key-on too */
    start();
    pk(0x1E, 0, 0, 0);
    pk(0x3C, 0, 0, 0);
    open_mono(ring, 0, 0x1E0000);
    open_mono(ring, 24, 0x1E4000);
    pk(0x42, 1, 1, 0);
    pk(0x43, 1, 0, 0);
    tick();
    for (t = 0; t < 4; t++) {
        vsync();
    }
    CHECK(envx(0) == 0);
    CHECK(envx(24) == 0);
    /* its FILL ran: the offset moved once, then nothing (not active) */
    CHECK(stream_off(24) == SPU_RING / 2);

    /* a stop while the voice's fill is in flight: no advance afterwards */
    start();
    pk(0x1E, 0, 0, 0);
    pk(0x3C, 0, 0, 0);
    open_mono(ring, 2, 0x1E8000);
    pk(0x42, 4, 0, 0);
    tick(); /* the FILL starts; pending */
    render(960);
    pk(0x43, 4, 0, 0);
    tick();
    CHECK(stream_off(2) == 0);
    vsync();
    CHECK(stream_off(2) == 0);
    ico_iop_heap_free(ring);
}

/* --- PCM -------------------------------------------------------------------------- */

static int16_t s16_at(const uint8_t *p)
{
    return (int16_t)(p[0] | p[1] << 8);
}

static void test_pcm(void)
{
    uint32_t buf = ico_iop_heap_alloc(0x4000);
    uint8_t *b;
    const uint8_t *st;
    int i;

    start();
    pk(0x1E, 0, 0, 0);
    tick();
    CHECK(buf != 0);
    b = ico_iop_ptr(buf);
    /* the movie's layout: 0x200 bytes left, 0x200 right, repeated */
    for (i = 0; i < 0x4000 / 2; i++) {
        int right = (i / 0x100) & 1;
        int16_t v = right ? (int16_t)-1000 : (int16_t)0x7000;

        b[2 * i] = (uint8_t)v;
        b[2 * i + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    pk(0x28, 1, 0x3FFF, 0x3FFF);
    pk(0x46, 0, 0, 0);
    pk(0x48, 0u << 24 | 0x10400, buf, 0x4000);
    pk(0x48, 1u << 24 | 0x10400, buf + 0x200, 0x4000);
    pk(0x48, 2u << 24 | 0x10400, buf, 0x4000); /* a second left: the wrap */
    pk(0x48, 3u << 24 | 0x20400, buf, 0x4000); /* shift 2: each sample twice */
    pk(0x4A, 1, 0x7FFF, 0);
    pk(0x4A, 2, 0, 0x7FFF);
    pk(0x4A, 4, 0x7FFF, 0);
    pk(0x4A, 8, 0, 0);
    pk(0x4D, 1, 0x400, 0); /* ch 1 starts a frame later */
    pk(0x4B, 0xF, 0, 0);
    tick();
    spu2_apply_pending();
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_BVOLL | 1) == 0x7FFF);
    render(256); /* the first half-done: half 0 is refilled */
    st = ico_sndn2_staging();
    /* left: 2 x (0x7FFF * 0x7000 >> 15) = 2 x 0x6FFF, wrapped */
    CHECK(s16_at(st + 0) == (int16_t)(uint16_t)(2 * 0x6FFF));
    CHECK(s16_at(st + 0x1FE) == (int16_t)(uint16_t)(2 * 0x6FFF));
    /* right: ch 1 at offset 0x400 + 0x200: -1000 * 0x7FFF >> 15 */
    CHECK(s16_at(st + 0x200) == (int16_t)((-1000 * 0x7FFF) >> 15));
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_PCM + 0) == 0x400);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_PCM + 4) == 0x800);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_PCM + 12) == 0x400);
    /* stop ch 2 at its current offset (mode 1): its next pass is skipped */
    pk(0x4F, 4, 0x400, 1);
    pk(0x4C, 1, 0, 0); /* and stop ch 0 */
    tick();
    render(256); /* half 1 */
    CHECK(s16_at(st + 0x400) == 0);
    CHECK(s16_at(st + 0x600) == (int16_t)((-1000 * 0x7FFF) >> 15));
    tick();
    CHECK(rd32(reply + ICO_SNDN2_REPLY_PCM + 8) == 0x400);
    CHECK(rd32(reply + ICO_SNDN2_REPLY_PCM + 0) == 0x400);
    /* the input reaches the output through BVOL: audible on the right */
    render(512);
    {
        int any = 0;

        for (i = 0; i < 512; i++) {
            any |= scratch[2 * i + 1] != 0;
        }
        CHECK(any);
    }
    pk(0x47, 0, 0, 0);
    tick();
    spu2_apply_pending();
    CHECK(spu2_sd_get_param(SPU2_SD_PARAM_BVOLL | 1) == 0);
    ico_iop_heap_free(buf);
}

/* --- the sequencer, end to end ----------------------------------------------------- */

/* A minimal .hd with one SE (program 0, tone 0): a key-on event, a zero
   delta and the end of track (offsets: sound.c's SgSePlay and
   _SgTableEnvAdd / _SgSeMain). */
static void make_hd(uint8_t *hd)
{
    uint8_t *se = hd + 0x180;  /* the SE table (slot 0x3C) */
    uint8_t *ch = hd + 0x200;  /* the channel table (slot 0x40) */
    uint8_t *tb = hd + 0x300;  /* the program table (slot 0x44) */
    uint8_t *prog = tb + 0x10; /* program 0 */
    uint8_t *tone = prog + 8;
    static const uint8_t body[] = {0xA0, 60, 100, 0, 0x00, 0xFF, 0x2F, 0, 0};

    memset(hd, 0, 0x400);
    put32(hd + 0x0C, 0x64685353);
    put32(hd + 0x10, 0x100);
    put32(hd + 0x18, 0x140);
    put32(hd + 0x1C, 0x180);
    put32(hd + 0x20, 0x200);
    put32(hd + 0x24, 0x300);
    /* SE table: tbl[0] last program, tbl[1] its tone list (byte offset),
       tbl[2] last tone, tbl[3] tone 0's body (byte offset) */
    se[0] = 0;
    se[2] = 4;
    se[4] = 0;
    se[6] = 0x20;
    memcpy(se + 0x20, body, sizeof(body));
    ch[0] = 0x7F;    /* the slot's 0x1E level */
    ch[0x13] = 0x7F; /* channel volume */
    ch[0x1E] = 0x7F;
    tb[2] = 0x10; /* program 0's block at tb + 0x10 */
    prog[1] = 0x7F;
    prog[6] = 60;   /* first note */
    tone[1] = 0x40; /* priority */
    tone[2] = 60;   /* base note */
    tone[4] = 0x10; /* VAG offset (x 16 bytes) */
    tone[6] = 0xFF; /* ADSR1 0x80FF */
    tone[7] = 0x80;
    tone[8] = 0xC0; /* ADSR2 0x5FC0 */
    tone[9] = 0x5F;
    tone[0xB] = 0x7F;
    tone[0xC] = 0x40;
    tone[0xD] = 2;
}

static void test_sequencer(void)
{
    static uint8_t hd[0x400] __attribute__((aligned(16)));
    uint8_t before[0x400];
    int vab;
    int se;
    int ok = 1;
    int i;

    ico_sif_host_reset();
    ico_sndn2_host_reset();
    CHECK(SgSndn2RemoteInit() == 0);
    SgInit();
    make_hd(hd);
    memcpy(before, hd, sizeof(hd));
    vab = SgVabOpenFakeBody((int *)hd, 0x10000);
    CHECK(vab == 1);
    /* the host keeps the relocated slots to itself */
    CHECK(memcmp(before, hd, sizeof(hd)) == 0);
    se = SgSePlay(vab, 0, 0);
    CHECK(se == 0);
    SgCalledTickProc();
    spu2_apply_pending();
    CHECK(SgGetSlotStatus(0, 0) == 2);
    CHECK(spu2_sd_get_addr(vsel(0, SPU2_SD_VADDR_SSA)) == (0x10000 / 16 + 0x10) * 16);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_PITCH)) == 4096 * 441 / 480);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_ADSR1)) == 0x80FF);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_ADSR2)) == 0x5FC0);
    CHECK(spu2_sd_get_param(vsel(0, SPU2_SD_VPARAM_VOLL)) != 0);
    render(960);
    SgCalledTickProc();
    CHECK(SgGetSlotStatus(0, 0) == 2); /* sounding: ENVX keeps the slot */
    for (i = 0; i < 0x400; i++) {
        if (i >= 0x30 && i < 0x48) {
            continue;
        }
        ok &= hd[i] == before[i] || (i == 0x21A); /* SeMain writes the channel's 0x1A */
    }
    CHECK(ok);
    SgSeStop(se | 0x8000);
    SgCalledTickProc();
    render(48000);
    SgCalledTickProc();
    SgCalledTickProc();
    SgCalledTickProc();
    CHECK(SgGetSlotStatus(0, 0) == 0); /* released and freed */
    CHECK(SgVabClose(vab) == 0);
    /* the DMA status poll (mode 0) after an upload, without a fiber */
    SgDmaWrite(0x10000, 0x20000, 0x40);
    CHECK(SgGetDmaTransferStatus(0) == 0);
    SgCalledTickProc();
    render(2);
    SgCalledTickProc();
    CHECK(SgGetDmaTransferStatus(0) == 1);
}

int main(int argc, char **argv)
{
    const char *iso = argc > 1 ? argv[1] : NULL;
    FILE *f = iso != NULL ? fopen(iso, "rb") : NULL;

    if (f != NULL) {
        fclose(f);
    } else {
        iso = NULL;
    }
    test_transport_and_voices();
    test_core_packets();
    test_pitch(iso);
    test_adpcm_stream();
    test_adpcm_wrap();
    test_adpcm_cancel();
    test_pcm();
    test_sequencer();
    printf("sndn2_test: %d checks, %s\n", checks, failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
