/*
 * port/audio/libsd_irx.h
 *
 * The libsd values the software SPU2 takes from the user's disc: the ten
 * reverb presets with their work area sizes, and the idle voice block
 * sceSdInit writes, read out of LIBSD.IRX's .data (docs/port/AUDIO.md,
 * "libsd values").  No byte of the module is in the repository; without
 * the file the built-in values stay (spu2_tables.c, spu2_sd.c).
 */
#ifndef ICO_PORT_AUDIO_LIBSD_IRX_H
#define ICO_PORT_AUDIO_LIBSD_IRX_H

#include <stddef.h>
#include <stdint.h>

#include "spu2.h"
#include "spu2_sd.h"

/* LIBSD.IRX on the PAL disc: its size, and the module addresses (in .data)
   of the three tables, found through the code that reads them:
   sceSdSetEffectAttr (0x900) loads sizes[mode] from 0x41C8 and copies the
   0x44-byte preset at 0x41F0 + mode * 0x44; InitVoices (0x3BA0) writes 16
   halfwords from 0x4A60 to the data port. */
#define ICO_LIBSD_IRX_PATH "LIBSD.IRX"
#define ICO_LIBSD_IRX_SIZE 26285u
#define ICO_LIBSD_SIZES_ADDR 0x41C8u   /* 10 x u32, work area size in 8-byte units */
#define ICO_LIBSD_PRESETS_ADDR 0x41F0u /* 10 x {u32 flags; u16 regs[32]} */
#define ICO_LIBSD_PRESET_BYTES 0x44u
#define ICO_LIBSD_IDLE_ADDR 0x4A60u /* 16 bytes */

typedef struct IcoLibsdValues {
    spu2_reverb_preset presets[SPU2_REVERB_MODES];
    uint32_t flags[SPU2_REVERB_MODES]; /* each preset's leading word (0 on the disc) */
    uint8_t idle[SPU2_SD_IDLE_BLOCK_BYTES];
} IcoLibsdValues;

/* Parse a whole LIBSD.IRX image (an ELF the IOP loads, sections linked at
   0): the .data section by name, then the tables at the addresses above,
   with structural checks (every preset's flags word 0 and its address
   registers inside its work area, the input volumes of modes 1-9 0x8000,
   the idle block's loop flags).  Returns NULL on success or why not. */
const char *ico_libsd_parse(const uint8_t *irx, size_t len, IcoLibsdValues *out);

enum { ICO_LIBSD_BUILTIN = 0, ICO_LIBSD_DISC = 1 };

/* Read LIBSD.IRX from the mounted disc (ico_vfs_disc) and install its
   values (spu2_reverb_set_preset for the ten modes, spu2_sd_set_idle_block);
   on any failure keep the built-in values and log one line.  Call after
   spu2_reset, which restores the built-in presets.  Returns the source. */
int ico_libsd_load(void);

/* Install parsed values, or the built-in ones for NULL. */
void ico_libsd_apply(const IcoLibsdValues *v);

/* Whether the installed values match the built-in ones, per mode
   (bit i set: mode i differs) and for the idle block (bit 10). */
unsigned ico_libsd_diff_builtin(const IcoLibsdValues *v);

#endif
