/*
 * port/audio/libsd_irx.c
 *
 * The disc's libsd values (libsd_irx.h): LIBSD.IRX's reverb presets, their
 * work area sizes and sceSdInit's idle voice block, read at start from the
 * mounted disc like SNDN2DRV.IRX's pitch table (sndn2_host.c).  The table
 * addresses come from reading the module's code (docs/port/AUDIO.md,
 * "libsd values"); nothing of the module is committed.
 */
#include "libsd_irx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spu2_internal.h"
#include "vfs.h"
#include "../include/ico_endian.h"

/* The file offset of `len` bytes at module address `addr` in .data, or -1. */
static long data_off(uint32_t sh_addr, uint32_t sh_off, uint32_t sh_size, uint32_t addr,
                     uint32_t len)
{
    if (addr < sh_addr || addr - sh_addr > sh_size || sh_size - (addr - sh_addr) < len)
        return -1;
    return (long)(sh_off + (addr - sh_addr));
}

const char *ico_libsd_parse(const uint8_t *irx, size_t len, IcoLibsdValues *out)
{
    static const uint8_t elf_magic[4] = {0x7F, 'E', 'L', 'F'};
    uint32_t shoff, shentsize, shnum, shstrndx, str_off, str_size;
    uint32_t d_addr = 0, d_off = 0, d_size = 0;
    long o_sizes, o_presets, o_idle;
    uint32_t i;
    int found = 0;
    int m;

    memset(out, 0, sizeof *out);
    if (len < 0x34 || memcmp(irx, elf_magic, 4) != 0 || irx[4] != 1 || irx[5] != 1)
        return "not a 32-bit little-endian ELF";
    shoff = ico_le32(irx + 0x20);
    shentsize = ico_le16(irx + 0x2E);
    shnum = ico_le16(irx + 0x30);
    shstrndx = ico_le16(irx + 0x32);
    if (shentsize < 0x28 || shstrndx >= shnum || shoff > len ||
        (uint64_t)shnum * shentsize > len - shoff)
        return "bad section headers";
    str_off = ico_le32(irx + shoff + shstrndx * shentsize + 0x10);
    str_size = ico_le32(irx + shoff + shstrndx * shentsize + 0x14);
    if (str_off > len || str_size > len - str_off)
        return "bad section name table";
    for (i = 0; i < shnum; i++) {
        const uint8_t *sh = irx + shoff + i * shentsize;
        uint32_t name = ico_le32(sh);

        if (name < str_size && str_size - name >= 6 &&
            memcmp(irx + str_off + name, ".data", 6) == 0) {
            d_addr = ico_le32(sh + 0x0C);
            d_off = ico_le32(sh + 0x10);
            d_size = ico_le32(sh + 0x14);
            found = 1;
            break;
        }
    }
    if (!found)
        return "no .data section";
    if (d_off > len || d_size > len - d_off)
        return ".data runs past the file";
    o_sizes = data_off(d_addr, d_off, d_size, ICO_LIBSD_SIZES_ADDR, 4u * SPU2_REVERB_MODES);
    o_presets = data_off(d_addr, d_off, d_size, ICO_LIBSD_PRESETS_ADDR,
                         ICO_LIBSD_PRESET_BYTES * SPU2_REVERB_MODES);
    o_idle = data_off(d_addr, d_off, d_size, ICO_LIBSD_IDLE_ADDR, SPU2_SD_IDLE_BLOCK_BYTES);
    if (o_sizes < 0 || o_presets < 0 || o_idle < 0)
        return "the tables are not inside .data";

    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        const uint8_t *p = irx + o_presets + (long)ICO_LIBSD_PRESET_BYTES * m;
        spu2_reverb_preset *r = &out->presets[m];
        uint32_t units = ico_le32(irx + o_sizes + 4 * m);
        uint32_t top = 0;
        int k;

        if (units == 0 || units > (SPU2_RAM_SIZE >> 3))
            return "a work area size is out of range";
        r->size = units << 3;
        out->flags[m] = ico_le32(p);
        for (k = 0; k < 32; k++)
            r->regs[k] = (uint16_t)ico_le16(p + 4 + 2 * k);
        for (k = 10; k < 30; k++)
            top = r->regs[k] > top ? r->regs[k] : top;
        if (out->flags[m] != 0)
            return "a preset's leading word is not 0";
        if ((top << 3) >= r->size)
            return "a preset addresses past its work area";
        if (m > 0 && (r->regs[30] != 0x8000 || r->regs[31] != 0x8000))
            return "a preset's input volumes are not 0x8000";
    }
    memcpy(out->idle, irx + o_idle, SPU2_SD_IDLE_BLOCK_BYTES);
    if ((out->idle[1] & 3) != 3)
        return "the idle block does not loop on itself";
    return NULL;
}

void ico_libsd_apply(const IcoLibsdValues *v)
{
    int m;

    for (m = 0; m < SPU2_REVERB_MODES; m++)
        spu2_reverb_set_preset(m, v != NULL ? &v->presets[m] : &spu2_reverb_presets[m]);
    spu2_sd_set_idle_block(v != NULL ? v->idle : NULL);
}

unsigned ico_libsd_diff_builtin(const IcoLibsdValues *v)
{
    static const uint8_t idle[SPU2_SD_IDLE_BLOCK_BYTES] = {
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    };
    unsigned diff = 0;
    int m;

    for (m = 0; m < SPU2_REVERB_MODES; m++) {
        const spu2_reverb_preset *b = &spu2_reverb_presets[m];

        if (b->size != v->presets[m].size ||
            memcmp(b->regs, v->presets[m].regs, sizeof b->regs) != 0)
            diff |= 1u << m;
    }
    if (memcmp(idle, v->idle, sizeof idle) != 0)
        diff |= 1u << SPU2_REVERB_MODES;
    return diff;
}

int ico_libsd_load(void)
{
    IcoVfs *disc = ico_vfs_disc();
    IcoVfsFile f;
    IcoLibsdValues v;
    uint8_t *buf = NULL;
    const char *why = NULL;
    unsigned diff;
    int m;

    if (disc == NULL) {
        why = "no disc is mounted";
    } else if (ico_vfs_open(disc, ICO_LIBSD_IRX_PATH, &f) != 0) {
        why = "the disc has no " ICO_LIBSD_IRX_PATH;
    } else if (ico_vfs_size(&f) != ICO_LIBSD_IRX_SIZE) {
        why = ICO_LIBSD_IRX_PATH " is not the PAL disc's (size)";
    } else if ((buf = malloc(ICO_LIBSD_IRX_SIZE)) == NULL) {
        why = "out of memory";
    } else if (ico_vfs_read(&f, 0, buf, ICO_LIBSD_IRX_SIZE) != (int64_t)ICO_LIBSD_IRX_SIZE) {
        why = ICO_LIBSD_IRX_PATH " cannot be read";
    } else {
        why = ico_libsd_parse(buf, ICO_LIBSD_IRX_SIZE, &v);
    }
    free(buf);
    if (why != NULL) {
        ico_libsd_apply(NULL);
        fprintf(stderr,
                "libsd: %s; using the built-in reverb presets (psx-spx) and idle block "
                "(ps2sdk) (DIVERGENCES.md A9, A14)\n",
                why);
        return ICO_LIBSD_BUILTIN;
    }
    ico_libsd_apply(&v);
    diff = ico_libsd_diff_builtin(&v);
    fprintf(
        stderr,
        "libsd: reverb presets, work area sizes and idle block from the disc's " ICO_LIBSD_IRX_PATH);
    if (diff != 0) {
        fputs("; differing from the built-in values in", stderr);
        for (m = 0; m < SPU2_REVERB_MODES; m++)
            if (diff & (1u << m))
                fprintf(stderr, " mode %d", m);
        if (diff & (1u << SPU2_REVERB_MODES))
            fputs(" the idle block", stderr);
    }
    fputc('\n', stderr);
    return ICO_LIBSD_DISC;
}
