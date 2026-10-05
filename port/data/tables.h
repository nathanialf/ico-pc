/*
 * port/data/tables.h
 *
 * The runtime loader of the game's 73 data tables (docs/port/DATA.md,
 * "The data tables").  The host binary defines the tables as empty arrays
 * (port/data/gen/table_defs.c); before the game starts, the loader reads the
 * boot ELF from the user's disc and decodes every record field by field into
 * the host layout, as port/data/gen/table_desc.c describes it:
 *
 *   integers, floats, unions   copied (the EE is little-endian, as the host)
 *   bit-fields                 extracted from their unit, stored by setter
 *   function pointer words     the registry's host function for the address
 *   object pointer words       the member's own string pool (staffroll_dat)
 *                              or the registry's host object
 *
 * Each row's bytes must match the CRC-32 config/tables_manifest.txt records
 * for it, checked for every row before anything is written, so a wrong or
 * modified ELF leaves the tables untouched.
 */
#ifndef ICO_PORT_DATA_TABLES_H
#define ICO_PORT_DATA_TABLES_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

/* The boot ELF's path on the PAL disc (SYSTEM.CNF's BOOT2). */
#define ICO_TABLES_BOOT_ELF "SCES_507.60"

/* Fill the tables from an ELF image in memory.  0 on success; otherwise -1
   and a one-line reason in err (the table, field and address at fault). */
int ico_tables_load_elf(const uint8_t *elf, size_t size, char *err, size_t errsz);

/* Read ICO_TABLES_BOOT_ELF from the volume and fill the tables. */
int ico_tables_load_vfs(IcoVfs *vfs, char *err, size_t errsz);

/* Rows and records the last successful load filled (for the log). */
uint32_t ico_tables_loaded_rows(void);
uint32_t ico_tables_loaded_records(void);

/* The CRC-32 (zlib's) of a byte range. */
uint32_t ico_tables_crc32(const uint8_t *p, size_t n);

#endif /* ICO_PORT_DATA_TABLES_H */
