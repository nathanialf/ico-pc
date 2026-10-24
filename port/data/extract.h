/*
 * port/data/extract.h
 *
 * The first-run extractor:
 * reads the user's disc image once and writes its archive (archive.h:
 * `ico.o2r` for the European disc, `ico-jpn.o2r` for the Japanese one).
 * Which disc it is comes from SYSTEM.CNF's BOOT2 file, matched against
 * ico_discs (archive.h) by name; that row's values verify it.
 *
 * What it stores: SYSTEM.CNF, the boot file its BOOT2 line names (the
 * row's boot ELF, SCES_507.60 or SCPS_110.03), DUMMY.TXT when the disc has
 * one, every *.IRX module in the root (the port reads SNDN2DRV.IRX's
 * pitch table and LIBSD.IRX's reverb presets and idle voice block; the rest
 * are small and kept for later checks), and every file
 * under DFDATAS/ (DATA.DF whole). meta.json also lists the root and DFDATAS
 * directories, so look-ups of them answer as on the disc.
 *
 * How it accepts the image, in order (the rule goes into meta.json and the
 * log):
 *   iso-sha1            the whole image's SHA-1 is the row's iso_sha1 (for
 *                       a .chd or .bin, the SHA-1 of the ISO it holds)
 *   elf-sha1+datadf-crc the boot ELF hashes to the row's elf_sha1 and
 *                       DATA.DF has that disc's size, directory and
 *                       members: the CRC-32 of its directory and of each of
 *                       its members (193 on the European disc, 183 on the
 *                       Japanese) match the manifest compiled into
 *                       discs.c (a re-dump with different padding or a
 *                       different volume size still passes)
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
#include "archive.h"
#include "vfs.h"

#define ICO_EXTRACT_NO_VERIFY 1u

/* The boxes a player sees for an image that is not one of the discs, and
   for one that cannot be read (first %s: the image's path, second %s:
   ico_disc_list_text(), the discs of the regions table). One text each,
   used by the extractor's refusal, main_host.c and the Android import. */
#define ICO_WRONG_DISC_TEXT                                                                        \
    "%s is not a disc image this port plays.\n"                                                    \
    "\n"                                                                                           \
    "Use a complete, unmodified image of\n"                                                        \
    "%s,\n"                                                                                        \
    "as a .iso, .chd, .bin or .cue file. Other editions do not work."
#define ICO_UNREADABLE_DISC_TEXT                                                                   \
    "Cannot read the disc image %s.\n"                                                             \
    "\n"                                                                                           \
    "Check that the file is complete and is a .iso, .chd, .bin or .cue copy of\n"                  \
    "%s.\n"                                                                                        \
    "The log says why."

/* Progress: phase "open" (reading the image's directory; done and total 0),
   "hash" (the image's SHA-1), "extract", then "finish" (writing the archive's
   directory and moving it into place; done and total 0).  In "hash" and
   "extract" done and total count bytes over both phases (the image's size
   plus the bytes stored).  Return nonzero to cancel (ignored in "open" and
   "finish"; the next byte count asks again). */
typedef int (*IcoExtractProgressFn)(void *ctx, const char *phase, uint64_t done, uint64_t total);

typedef struct IcoExtractResult {
    char rule[32]; /* ICO_RULE_* (archive.h), or "" when refused */
    char iso_sha1[41];
    uint64_t iso_size;
    char elf_sha1[41];    /* the boot ELF as stored, "" if absent */
    int datadf_ok;        /* DATA.DF matched the manifest */
    char datadf_why[256]; /* why not */
    char disc_id[16];     /* from SYSTEM.CNF's BOOT2 ("SCES-50760", "SCPS-11003") */
    uint32_t files;       /* files stored */
    uint64_t bytes;       /* their bytes */
    uint64_t archive_bytes;
    double hash_seconds;
    double extract_seconds;
    int cancelled;
    /* why a refusal happened, for the message a player sees: unreadable,
       the file is not a disc image this program reads (not an .iso, a
       .bin or .cue, or a .chd made from one; a .chd that needs its parent,
       a damaged file); wrong_disc, it reads but is neither disc of
       ico_discs (another region or edition, or a modified image).  Both 0 for the other failures
       (no space, a write error, a cancel). */
    int unreadable;
    int wrong_disc;
} IcoExtractResult;

/* Extracts iso_path into out_path. 0, or -1 with the reason in why. */
int ico_extract_archive(const char *iso_path, const char *out_path, unsigned flags,
                        IcoExtractProgressFn progress, void *ctx, IcoExtractResult *res, char *why,
                        size_t whysize);

/* The SHA-1 (lower-case hex) and size of a disc image's logical bytes: an
   .iso file as it is, the ISO a .chd or .bin holds, so both give the ISO's values
   (vfs.h, IcoDiscImage). 0, or -1 with the reason in why. */
int ico_extract_image_sha1(const char *iso_path, char hex[41], uint64_t *bytes, char *why,
                           size_t whysize);

/* Which disc a mounted volume is: the ico_discs row that boots with `boot`
   (SYSTEM.CNF's BOOT2 file); when two rows share the name (SCUS_971.13: the
   North American disc and the August 2001 preview) the row whose elf_sha1
   the boot ELF has, hashed from the volume (elf_sha1 NULL) or given (40
   lower-case hex digits). NULL for none, or when the ELF cannot be read.
   Logs the hash once when it had to decide. */
const IcoDiscInfo *ico_disc_identify(IcoVfs *vfs, const char *boot, const char *elf_sha1);

/* Which disc the image is, by SYSTEM.CNF's BOOT2 file (ico_disc_identify):
   its ico_discs row, or NULL (not readable, or another disc). Copies the
   boot file's name ("" when none) into boot when given. Reads the directory,
   and the boot ELF (4 MB) when two discs share the name. */
const IcoDiscInfo *ico_extract_identify(const char *iso_path, char *boot, size_t size);

/* Prints the DATA.DF manifest of the disc mounted as `iso` in the form
   discs.c compiles for the region `slug` ("pal", "jpn": the block's
   BEGIN/END lines and names; sizes, offsets and CRC-32s only, no names of
   members, no data). 0, or -1. Used by archive_test's `manifest` mode to
   regenerate it. */
int ico_extract_print_datadf_manifest(IcoVfs *iso, const char *slug, FILE *out);

#endif /* ICO_PORT_EXTRACT_H */
