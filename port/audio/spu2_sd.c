/*
 * port/audio/spu2_sd.c
 *
 * libsd's register-level behaviour on the software SPU2 (spu2_sd.h).
 * Register offsets and init values follow ps2sdk's clean-room libsd
 * (iop/sound/libsd/src/freesd.c, effect.c, voice.c; AFL-2.0, read for
 * behaviour, no code taken), checked against the disc's LIBSD.IRX:
 * sceSdInit's register writes are its code's, and the reverb presets and
 * the idle block are its data, read at start by libsd_irx.c.
 */
#include "spu2_sd.h"

#include <string.h>

#include "spu2.h"
#include "spu2_internal.h"

static uint64_t sd_time;
static spu2_sd_effect_attr effect_attr[2];
/* InitVoices' block (spu2_sd_set_idle_block): ps2sdk's VoiceDataInit, the
   same 16 bytes as the disc's LIBSD.IRX */
static uint8_t idle_block[SPU2_SD_IDLE_BLOCK_BYTES] = {
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
};

static uint64_t T(void)
{
    uint64_t now = spu2_time();

    return sd_time < now ? now : sd_time;
}

static void wr(int core, unsigned reg, uint16_t v)
{
    spu2_write_reg(core, reg, v, T());
}

static void wr_pair(int core, unsigned reg, uint32_t hw)
{
    wr(core, reg, (uint16_t)((hw >> 16) & 0xF));
    wr(core, reg + 2, (uint16_t)(hw & 0xFFFF));
}

/* The register a SetParam/GetParam entry names. */
static unsigned param_reg(uint16_t entry, int *core)
{
    unsigned idx = (entry >> 8) & 0xFF;
    unsigned voice = (entry >> 1) & 0x1F;

    *core = entry & 1;
    if (idx <= 7)
        return SPU2_R_VP(voice) + idx * 2;
    if (idx == 8)
        return SPU2_R_MMIX;
    if (idx <= 0x12)
        return SPU2_R_MVOLL + (idx - 9) * 2;
    return 0xFFFF;
}

static unsigned switch_reg(uint16_t entry)
{
    switch (entry & 0xFF00) {
    case SPU2_SD_SWITCH_PMON:
        return SPU2_R_PMON;
    case SPU2_SD_SWITCH_NON:
        return SPU2_R_NON;
    case SPU2_SD_SWITCH_KON:
        return SPU2_R_KON;
    case SPU2_SD_SWITCH_KOFF:
        return SPU2_R_KOFF;
    case SPU2_SD_SWITCH_ENDX:
        return SPU2_R_ENDX;
    case SPU2_SD_SWITCH_VMIXL:
        return SPU2_R_VMIXL;
    case SPU2_SD_SWITCH_VMIXEL:
        return SPU2_R_VMIXEL;
    case SPU2_SD_SWITCH_VMIXR:
        return SPU2_R_VMIXR;
    case SPU2_SD_SWITCH_VMIXER:
        return SPU2_R_VMIXER;
    default:
        return 0xFFFF;
    }
}

static unsigned addr_reg(uint16_t entry, int *core)
{
    unsigned voice = (entry >> 1) & 0x1F;

    *core = entry & 1;
    switch (entry & 0xFF00) {
    case SPU2_SD_ADDR_ESA:
        return SPU2_R_ESA;
    case SPU2_SD_ADDR_EEA:
        return SPU2_R_EEA;
    case SPU2_SD_ADDR_TSA:
        return SPU2_R_TSA;
    case SPU2_SD_ADDR_IRQA:
        return SPU2_R_IRQA;
    case SPU2_SD_VADDR_SSA:
        return SPU2_R_VA(voice) + SPU2_VA_SSA;
    case SPU2_SD_VADDR_LSAX:
        return SPU2_R_VA(voice) + SPU2_VA_LSAX;
    case SPU2_SD_VADDR_NAX:
        return SPU2_R_VA(voice) + SPU2_VA_NAX;
    default:
        return 0xFFFF;
    }
}

void spu2_sd_set_time(uint64_t time)
{
    sd_time = time;
}

void spu2_sd_set_param(uint16_t entry, uint16_t value)
{
    int core;
    unsigned reg = param_reg(entry, &core);

    if (reg != 0xFFFF)
        wr(core, reg, value);
}

uint16_t spu2_sd_get_param(uint16_t entry)
{
    int core;
    unsigned reg = param_reg(entry, &core);

    return reg != 0xFFFF ? spu2_read_reg(core, reg) : 0;
}

void spu2_sd_set_switch(uint16_t entry, uint32_t value)
{
    unsigned reg = switch_reg(entry);
    int core = entry & 1;

    if (reg == 0xFFFF)
        return;
    wr(core, reg, (uint16_t)(value & 0xFFFF));
    wr(core, reg + 2, (uint16_t)((value >> 16) & 0xFF));
}

uint32_t spu2_sd_get_switch(uint16_t entry)
{
    unsigned reg = switch_reg(entry);
    int core = entry & 1;

    if (reg == 0xFFFF)
        return 0;
    return spu2_read_reg(core, reg) | (uint32_t)(spu2_read_reg(core, reg + 2) & 0xFF) << 16;
}

void spu2_sd_set_addr(uint16_t entry, uint32_t value)
{
    int core;
    unsigned reg = addr_reg(entry, &core);

    if (reg == 0xFFFF)
        return;
    if (reg == SPU2_R_EEA)
        wr(core, reg, (uint16_t)((value >> 17) & 0xF));
    else
        wr_pair(core, reg, value >> 1);
}

uint32_t spu2_sd_get_addr(uint16_t entry)
{
    int core;
    unsigned reg = addr_reg(entry, &core);

    if (reg == 0xFFFF)
        return 0;
    if (reg == SPU2_R_EEA)
        return (uint32_t)(spu2_read_reg(core, reg) & 0xF) << 17 | 0x1FFFF;
    return ((uint32_t)(spu2_read_reg(core, reg) & 0xF) << 16 | spu2_read_reg(core, reg + 2)) << 1;
}

void spu2_sd_set_core_attr(uint16_t entry, uint16_t value)
{
    int core = entry & 1;
    uint16_t attr = spu2_shadow(core, SPU2_R_ATTR);
    int bit;

    switch (entry & ~1) {
    case SPU2_SD_CORE_NOISE_CLK:
        attr = (uint16_t)((attr & ~0x3F00) | (value & 0x3F) << 8);
        break;
    case SPU2_SD_CORE_SPDIF_MODE:
        return; /* digital output: nothing to do on the host */
    case SPU2_SD_CORE_EFFECT_ENABLE:
        bit = 7;
        attr = (uint16_t)((attr & ~(1 << bit)) | (value & 1) << bit);
        break;
    case SPU2_SD_CORE_IRQ_ENABLE:
        bit = 6;
        attr = (uint16_t)((attr & ~(1 << bit)) | (value & 1) << bit);
        break;
    case SPU2_SD_CORE_MUTE_ENABLE:
        bit = 14;
        attr = (uint16_t)((attr & ~(1 << bit)) | (value & 1) << bit);
        break;
    default:
        return;
    }
    wr(core, SPU2_R_ATTR, attr);
}

uint16_t spu2_sd_get_core_attr(uint16_t entry)
{
    uint16_t attr = spu2_shadow(entry & 1, SPU2_R_ATTR);

    switch (entry & ~1) {
    case SPU2_SD_CORE_NOISE_CLK:
        return (attr >> 8) & 0x3F;
    case SPU2_SD_CORE_EFFECT_ENABLE:
        return (attr >> 7) & 1;
    case SPU2_SD_CORE_IRQ_ENABLE:
        return (attr >> 6) & 1;
    case SPU2_SD_CORE_MUTE_ENABLE:
        return (attr >> 14) & 1;
    default:
        return 0;
    }
}

int spu2_sd_set_effect_attr(int core, const spu2_sd_effect_attr *attr)
{
    int mode = attr->mode & ~SPU2_SD_EFFECT_MODE_CLEAR;
    const spu2_reverb_preset *p;

    core &= 1;
    if (mode < 0 || mode > 9)
        return -1;
    p = spu2_reverb_get_preset(mode);
    effect_attr[core] = *attr;
    effect_attr[core].core = core;
    effect_attr[core].mode = mode;

    wr(core, SPU2_R_EVOLL, (uint16_t)attr->depth_L);
    wr(core, SPU2_R_EVOLR, (uint16_t)attr->depth_R);
    spu2_reverb_apply_preset(core, p, T());

    if (attr->mode & SPU2_SD_EFFECT_MODE_CLEAR) {
        /* zero the work area now (libsd: sceSdClearEffectWorkArea) */
        uint32_t end = ((uint32_t)(spu2_shadow(core, SPU2_R_EEA) & 0xF) << 17) | 0x1FFFFu;
        uint32_t start = end + 1u - p->size;
        static const uint8_t zero[1024];
        uint32_t done;

        for (done = 0; done < p->size; done += sizeof zero) {
            uint32_t n = p->size - done < sizeof zero ? p->size - done : (uint32_t)sizeof zero;

            spu2_dma_write(start + done, zero, n);
        }
    }
    return 0;
}

void spu2_sd_get_effect_attr(int core, spu2_sd_effect_attr *attr)
{
    *attr = effect_attr[core & 1];
}

int spu2_sd_voice_trans(int chan, uint16_t mode, void *buf, uint32_t spu, uint32_t size)
{
    int dir = (mode & 1) ? SPU2_TRANS_READ : SPU2_TRANS_WRITE;

    if (spu2_voice_trans(chan, dir, spu, buf, size, T()) < 0)
        return -1;
    return (int)size;
}

int spu2_sd_voice_trans_status(int chan, int flag)
{
    if (flag & 1) {
        spu2_voice_trans_wait(chan);
        return 1;
    }
    return spu2_voice_trans_busy(chan) ? 0 : 1;
}

void spu2_sd_set_idle_block(const uint8_t *block)
{
    if (block != NULL)
        memcpy(idle_block, block, sizeof idle_block);
    else
        memset(idle_block, 0x07, sizeof idle_block);
}

const uint8_t *spu2_sd_idle_block(void)
{
    return idle_block;
}

/* The disc's LIBSD.IRX sceSdInit (0x1420), read off its code; ps2sdk's freesd.c does the same. */
void spu2_sd_init(int hot)
{
    uint8_t block[2 * SPU2_SD_IDLE_BLOCK_BYTES];
    int core;
    int v;

    hot &= 1;
    memset(effect_attr, 0, sizeof effect_attr);

    if (!hot) {
        /* ResetAll (0x1A00): every voice keyed off; PMON and NON cleared
           on core 1 only (the loop over the cores never steps the
           register base) */
        for (core = 0; core < 2; core++) {
            wr(core, SPU2_R_KOFF, 0xFFFF);
            wr(core, SPU2_R_KOFF + 2, 0xFFFF);
        }
        wr(1, SPU2_R_PMON, 0);
        wr(1, SPU2_R_PMON + 2, 0);
        wr(1, SPU2_R_NON, 0);
        wr(1, SPU2_R_NON + 2, 0);
        /* the effect area of mode 0 below the end address the core has
           before InitCoreVolume sets EEA (0x1210: ESA = end - (size - 2)) */
        for (core = 0; core < 2; core++) {
            uint32_t end = (uint32_t)(spu2_shadow(core, SPU2_R_EEA) & 0xF) << 17 | 0x1FFFFu;
            uint32_t size = spu2_reverb_get_preset(SPU2_SD_EFFECT_MODE_OFF)->size;

            wr_pair(core, SPU2_R_ESA, (end - (size - 2u)) >> 1);
        }
    }

    /* InitVoices (0x3BA0): the idle block goes to byte 0x5000 through the
       data port, 16 halfwords: the module's 16 bytes and the 16 that follow
       them in memory, the start of .bss, zero at the first sceSdInit */
    memcpy(block, idle_block, SPU2_SD_IDLE_BLOCK_BYTES);
    memset(block + SPU2_SD_IDLE_BLOCK_BYTES, 0, SPU2_SD_IDLE_BLOCK_BYTES);
    spu2_dma_write(0x5000, block, sizeof block);

    for (core = 0; core < 2; core++) {
        for (v = 0; v < 24; v++) {
            wr(core, SPU2_R_VP(v) + SPU2_VP_VOLL, 0);
            wr(core, SPU2_R_VP(v) + SPU2_VP_VOLR, 0);
            wr(core, SPU2_R_VP(v) + SPU2_VP_PITCH, 0x3FFF);
            wr(core, SPU2_R_VP(v) + SPU2_VP_ADSR1, 0);
            wr(core, SPU2_R_VP(v) + SPU2_VP_ADSR2, 0);
            wr_pair(core, SPU2_R_VA(v) + SPU2_VA_SSA, 0x5000 >> 1);
        }
    }
    for (core = 0; core < 2; core++) {
        wr(core, SPU2_R_KON, 0xFFFF);
        wr(core, SPU2_R_KON + 2, 0xFF);
    }
    for (core = 0; core < 2; core++) {
        wr(core, SPU2_R_KOFF, 0xFFFF);
        wr(core, SPU2_R_KOFF + 2, 0xFF);
    }
    /* ENDX of core 0 only */
    wr(0, SPU2_R_ENDX + 2, 0);
    wr(0, SPU2_R_ENDX, 0);

    /* InitCoreVolume (0xE8C) */
    wr(0, SPU2_R_ATTR, (uint16_t)(0xC000 | (hot ? 0x80 : 0)));
    wr(1, SPU2_R_ATTR, (uint16_t)(0xC001 | (hot ? 0x80 : 0)));
    for (core = 0; core < 2; core++) {
        wr(core, SPU2_R_VMIXL, 0xFFFF);
        wr(core, SPU2_R_VMIXL + 2, 0xFF);
        wr(core, SPU2_R_VMIXR, 0xFFFF);
        wr(core, SPU2_R_VMIXR + 2, 0xFF);
        wr(core, SPU2_R_VMIXEL, 0xFFFF);
        wr(core, SPU2_R_VMIXEL + 2, 0xFF);
        wr(core, SPU2_R_VMIXER, 0xFFFF);
        wr(core, SPU2_R_VMIXER + 2, 0xFF);
    }
    wr(0, SPU2_R_MMIX, 0xFF0);
    wr(1, SPU2_R_MMIX, 0xFFC);
    if (!hot) {
        for (core = 0; core < 2; core++) {
            wr(core, SPU2_R_MVOLL, 0);
            wr(core, SPU2_R_MVOLR, 0);
            wr(core, SPU2_R_EVOLL, 0);
            wr(core, SPU2_R_EVOLR, 0);
        }
        wr(0, SPU2_R_EEA, 0xE);
        wr(1, SPU2_R_EEA, 0xF);
    }
    wr(0, SPU2_R_AVOLL, 0);
    wr(0, SPU2_R_AVOLR, 0);
    wr(1, SPU2_R_AVOLL, 0x7FFF);
    wr(1, SPU2_R_AVOLR, 0x7FFF);
    for (core = 0; core < 2; core++) {
        wr(core, SPU2_R_BVOLL, 0);
        wr(core, SPU2_R_BVOLR, 0);
    }
}
