/*
 * port/data/extract.h
 *
 * The first-run extractor:
 * reads the user's disc image once and writes `ico.o2r` (archive.h).
 *
 * What it stores: SYSTEM.CNF, the boot file its BOOT2 line names,
 * SCES_507.60, every *.IRX module in the root (the port reads SNDN2DRV.IRX's
 * pitch table and LIBSD.IRX's reverb presets and idle voice block; the rest
 * are small and kept for later checks), and every file
 * under DFDATAS/ (DATA.DF whole). meta.json also lists the root and DFDATAS
 * directories, so look-ups of them answer as on the disc.
 *
 * How it accepts the image, in order (the rule goes into meta.json and the
 * log):
 *   iso-sha1            the whole image's SHA-1 is ICO_DISC_ISO_SHA1
 *   elf-sha1+datadf-crc SCES_507.60 hashes to ICO_DISC_ELF_SHA1 and DATA.DF
 *                       has the PAL disc's size, directory and members: the
 *                       CRC-32 of its directory and of each of its 193
 *                       members match the manifest compiled into extract.c
 *                       (a re-dump with different padding or a different
 *                       volume size still passes)
 * Anything else is refused and nothing is left behind, unless the caller
 * asks for ICO_EXTRACT_NO_VERIFY (tests on a synthetic image), which writes
 * the archive with the rule "unverified"; ico_archive_info_acceptable
 * refuses such an archive.
 *
 * The archive is written to `<out>.tmp` and moved over `out` atomically once
 * it is complete and reads back (MoveFileEx on Windows); a failure (disk
 * full, a read error, a refused image, a cancel) removes the .tmp.
 */
#ifndef ICO_PORT_EXTRACT_H
#define ICO_PORT_EXTRACT_H

#include <stdint.h>
#include <stdio.h>
#include "vfs.h"

#define ICO_EXTRACT_NO_VERIFY 1u

/* Progress: phase "hash" (the image's SHA-1) then "extract"; done and
   total count bytes over both phases (the image's size plus the bytes
   stored). Return nonzero to cancel. */
typedef int (*IcoExtractProgressFn)(void *ctx, const char *phase, uint64_t done, uint64_t total);

typedef struct IcoExtractResult {
    char rule[32]; /* ICO_RULE_* (archive.h), or "" when refused */
    char iso_sha1[41];
    uint64_t iso_size;
    char elf_sha1[41];    /* SCES_507.60 as stored, "" if absent */
    int datadf_ok;        /* DATA.DF matched the manifest */
    char datadf_why[256]; /* why not */
    char disc_id[16];     /* from SYSTEM.CNF's BOOT2 ("SCES-50760") */
    uint32_t files;       /* files stored */
    uint64_t bytes;       /* their bytes */
    uint64_t archive_bytes;
    double hash_seconds;
    double extract_seconds;
    int cancelled;
} IcoExtractResult;

/* Extracts iso_path into out_path. 0, or -1 with the reason in why. */
int ico_extract_archive(const char *iso_path, const char *out_path, unsigned flags,
                        IcoExtractProgressFn progress, void *ctx, IcoExtractResult *res, char *why,
                        size_t whysize);

/* Prints the DATA.DF manifest of the disc mounted as `iso` in the form
   extract.c compiles (sizes, offsets and CRC-32s only; no names, no data).
   0, or -1. Used by archive_test's `manifest` mode to regenerate it. */
int ico_extract_print_datadf_manifest(IcoVfs *iso, FILE *out);

/* The number of members the compiled manifest describes (0: none). */
uint32_t ico_extract_datadf_manifest_count(void);

#endif /* ICO_PORT_EXTRACT_H */
