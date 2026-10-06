/*
 * port/audio/test/libsd_irx_test.c
 *
 * The LIBSD.IRX reader (libsd_irx.c) on a synthetic module: an ELF with a
 * .data section at the disc module's address holding made-up tables (no
 * byte of the disc's module), parsed, installed and used by sceSdInit; the
 * structural checks that refuse a damaged table; and the fallback to the
 * built-in values when no disc is mounted.  sndn2_test checks the real
 * module when the disc image is there.
 */
#include <stdio.h>
#include <string.h>

#include "libsd_irx.h"
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

#define DATA_ADDR 0x4190u
#define DATA_SIZE 0x8E0u
#define DATA_OFF 0x100u
#define STR_OFF (DATA_OFF + DATA_SIZE)
#define SH_OFF 0xA00u
#define IMAGE (SH_OFF + 3u * 0x28u)

static uint8_t image[IMAGE];

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

static uint8_t *at(uint32_t addr)
{
    return image + DATA_OFF + (addr - DATA_ADDR);
}

/* Made-up values: mode m's size is (0x100 + 0x40 m) units of 8 bytes, its
   registers 0x1000 + 32 m + k, its addresses k + m, its input volumes
   0x8000; the idle block 0x12 0x07 0x34 ... */
static uint16_t reg_of(int m, int k)
{
    if (k < 10)
        return (uint16_t)(0x1000 + 32 * m + k);
    if (k < 30)
        return (uint16_t)(k + m);
    return 0x8000;
}

static void build(void)
{
    static const char names[] = "\0.data\0.shstrtab";
    uint8_t *sh;
    int m;
    int k;

    memset(image, 0, sizeof image);
    image[0] = 0x7F;
    memcpy(image + 1, "ELF", 3);
    image[4] = 1; /* 32-bit */
    image[5] = 1; /* little-endian */
    put32(image + 0x20, SH_OFF);
    put16(image + 0x2E, 0x28);
    put16(image + 0x30, 3);
    put16(image + 0x32, 2);
    memcpy(image + STR_OFF, names, sizeof names);
    sh = image + SH_OFF + 0x28; /* 1: .data */
    put32(sh, 1);
    put32(sh + 0x04, 1);
    put32(sh + 0x0C, DATA_ADDR);
    put32(sh + 0x10, DATA_OFF);
    put32(sh + 0x14, DATA_SIZE);
    sh += 0x28; /* 2: .shstrtab */
    put32(sh, 7);
    put32(sh + 0x04, 3);
    put32(sh + 0x10, STR_OFF);
    put32(sh + 0x14, sizeof names);
    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        uint8_t *p = at(ICO_LIBSD_PRESETS_ADDR + ICO_LIBSD_PRESET_BYTES * (uint32_t)m);

        put32(at(ICO_LIBSD_SIZES_ADDR + 4u * (uint32_t)m), 0x100u + 0x40u * (uint32_t)m);
        for (k = 0; k < 32; k++)
            put16(p + 4 + 2 * k, reg_of(m, k));
    }
    at(ICO_LIBSD_IDLE_ADDR)[0] = 0x12;
    at(ICO_LIBSD_IDLE_ADDR)[1] = 0x07;
    for (k = 2; k < 16; k++)
        at(ICO_LIBSD_IDLE_ADDR)[k] = (uint8_t)(0x30 + k);
}

static void test_parse(void)
{
    IcoLibsdValues v;
    int m;
    int k;
    int ok = 1;

    build();
    CHECK(ico_libsd_parse(image, sizeof image, &v) == NULL);
    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        ok &= v.presets[m].size == (0x100u + 0x40u * (uint32_t)m) * 8u;
        ok &= v.flags[m] == 0;
        for (k = 0; k < 32; k++)
            ok &= v.presets[m].regs[k] == reg_of(m, k);
    }
    CHECK(ok);
    CHECK(v.idle[0] == 0x12 && v.idle[1] == 0x07 && v.idle[15] == 0x3F);
    CHECK(ico_libsd_diff_builtin(&v) == 0x7FF);

    /* refusals */
    CHECK(ico_libsd_parse(image, 0x20, &v) != NULL);
    build();
    image[0] = 0;
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    memcpy(image + STR_OFF + 1, ".text", 5);
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    put32(image + SH_OFF + 0x28 + 0x14, 0x800); /* .data too short for the idle block */
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    put32(at(ICO_LIBSD_PRESETS_ADDR + ICO_LIBSD_PRESET_BYTES * 3), 1); /* flags word */
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    put16(at(ICO_LIBSD_PRESETS_ADDR) + 4 + 2 * 12, 0x100); /* mode 0 address = its size */
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    put16(at(ICO_LIBSD_PRESETS_ADDR + ICO_LIBSD_PRESET_BYTES * 4) + 4 + 2 * 30, 0x7FFF);
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    put32(at(ICO_LIBSD_SIZES_ADDR + 8), 0);
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
    build();
    at(ICO_LIBSD_IDLE_ADDR)[1] = 0x04; /* no loop end */
    CHECK(ico_libsd_parse(image, sizeof image, &v) != NULL);
}

/* The parsed values reach the SPU2 and sceSdInit. */
static void test_apply(void)
{
    IcoLibsdValues v;
    const uint8_t *ram;
    uint32_t size0;
    int i;

    build();
    CHECK(ico_libsd_parse(image, sizeof image, &v) == NULL);
    spu2_reset();
    ico_libsd_apply(&v);
    CHECK(spu2_reverb_get_preset(4)->size == v.presets[4].size);
    CHECK(memcmp(spu2_reverb_get_preset(9)->regs, v.presets[9].regs, 64) == 0);
    CHECK(memcmp(spu2_sd_idle_block(), v.idle, 16) == 0);

    spu2_sd_set_time(0);
    spu2_sd_init(0);
    spu2_apply_pending();
    ram = spu2_ram();
    CHECK(memcmp(ram + 0x5000, v.idle, 16) == 0);
    for (i = 16; i < 32; i++)
        CHECK(ram[0x5000 + i] == 0);
    /* cold: ESA is mode 0's area below the end address before EEA was set
       (0 after spu2_reset), then InitCoreVolume sets EEA to 0xE / 0xF */
    size0 = v.presets[0].size;
    CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 0) == ((0x1FFFFu - (size0 - 2u)) & ~1u));
    CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 1) == ((0x1FFFFu - (size0 - 2u)) & ~1u));
    CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_EEA | 0) == (0xEu << 17 | 0x1FFFF));
    /* the effect attr places the area from the installed size */
    {
        spu2_sd_effect_attr a;

        memset(&a, 0, sizeof a);
        a.mode = SPU2_SD_EFFECT_MODE_STUDIO_3;
        CHECK(spu2_sd_set_effect_attr(0, &a) == 0);
        spu2_apply_pending();
        CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 0) ==
              (((0xEu << 17 | 0x1FFFF) - (v.presets[4].size - 2u)) & ~1u));
    }
    /* hot: ESA untouched */
    spu2_sd_init(1);
    spu2_apply_pending();
    CHECK(spu2_sd_get_addr(SPU2_SD_ADDR_ESA | 0) ==
          (((0xEu << 17 | 0x1FFFF) - (v.presets[4].size - 2u)) & ~1u));
}

/* No disc: the built-in values, whatever was installed before. */
static void test_fallback(void)
{
    IcoLibsdValues v;
    IcoLibsdValues now;
    int m;

    build();
    CHECK(ico_libsd_parse(image, sizeof image, &v) == NULL);
    spu2_reset();
    ico_libsd_apply(&v);
    ico_vfs_set_disc(NULL);
    CHECK(ico_libsd_load() == ICO_LIBSD_BUILTIN);
    memset(&now, 0, sizeof now);
    for (m = 0; m < SPU2_REVERB_MODES; m++)
        now.presets[m] = *spu2_reverb_get_preset(m);
    memcpy(now.idle, spu2_sd_idle_block(), 16);
    CHECK(ico_libsd_diff_builtin(&now) == 0);
    CHECK(now.idle[0] == 0x07 && now.idle[1] == 0x07);
}

int main(void)
{
    test_parse();
    test_apply();
    test_fallback();
    printf("libsd_irx_test: %d checks, %s\n", checks, failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
