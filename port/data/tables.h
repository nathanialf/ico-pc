/*
 * port/data/tables.h
 *
 * The runtime loader of the game's 73 data tables.  The host binary defines
 * the tables as empty arrays in one layout, PAL's (port/data/gen/
 * table_defs.c); before the game starts, the loader reads the boot ELF from
 * the user's disc and decodes every record field by field into that layout,
 * as the disc's descriptor set describes it (port/data/gen/<slug>/
 * table_desc.c, one set per row of config/regions.txt):
 *
 *   integers, floats, unions   copied (the EE is little-endian, as the host)
 *   bit-fields                 extracted from their unit, stored by setter
 *   function pointer words     the set's registry's host function for the
 *                              address
 *   object pointer words       the member's own string pool (staffroll_dat)
 *                              or the registry's host object
 *   bit words (another disc)   built bit by bit from the disc's bit words
 *                              (MotionDef's modeBits/flags/flags2 move)
 *   fields the disc lacks      their declared default (palSpeedRatio 1.0)
 *
 * Each row's bytes must match the CRC-32 the set's manifest
 * (config/tables_manifest[.<slug>].txt) records for it, checked for every
 * row before anything is written, so a wrong or modified ELF, or another
 * disc's, leaves the tables untouched.
 */
#ifndef ICO_PORT_DATA_TABLES_H
#define ICO_PORT_DATA_TABLES_H

#include <stddef.h>
#include <stdint.h>

#include "gen/table_desc.h" /* IcoTableSet, IcoTableId, ico_table_sets[] */
#include "vfs.h"

/* Fill the tables from an ELF image in memory with a disc's set (one of
   ico_table_sets[], in config/regions.txt's order).  0 on success; otherwise
   -1 and a one-line reason in err (the table, field and address at fault, and
   the disc the set reads). */
int ico_tables_load_elf(const IcoTableSet *set, const uint8_t *elf, size_t size, char *err,
                        size_t errsz);

/* Read the set's boot ELF from the volume and fill the tables. */
int ico_tables_load_vfs(IcoVfs *vfs, const IcoTableSet *set, char *err, size_t errsz);

/* Rows and records the last successful load filled (for the log). */
uint32_t ico_tables_loaded_rows(void);
uint32_t ico_tables_loaded_records(void);

/* The records the last successful load filled into a table (its count on
   that disc: texProperty 436 on the European disc, 412 on the Japanese); 0
   before a load. */
uint32_t ico_tables_count(IcoTableId id);

/* The set of the last successful load, or NULL. */
const IcoTableSet *ico_tables_loaded_set(void);

#endif /* ICO_PORT_DATA_TABLES_H */
