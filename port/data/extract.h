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
 * A disc whose game is in a folder (archive.h IcoDiscInfo.game_dir: the June
 * 2001 magazine demo, whose BOOT2 is the magazine's menu SCUS_971.58 and whose
 * game is CUS97113/): SYSTEM.CNF; the boot file listed in meta.json without
 * its bytes (looked up as on the disc, never read); in the folder, the
 * row's elf_path (CUS97113/SCPS_000.00), every *.IRX and DFDATAS/ whole,
 * under their disc paths ("CUS97113/DFDATAS/DATA.DF"), with the folder and
 * DFDATAS listed. Nothing else of the disc (the menu's other files, the
 * other games' folders, SCE_DD) is stored. The game asks for
 * "cdrom0:\\CUS97113\\..." and the archive answers through the same
 * look-up as for a root file.
 *
 * How it accepts the image, in order (the rule goes into meta.json and the
 * log):
 *   iso-sha1            the whole image's SHA-1 is the row's iso_sha1 (for
 *                       a .chd or .bin, the SHA-1 of the ISO it holds); a
 *                       row with no iso_sha1 (the magazine demo) skips the
 *                       image's hash, and meta.json's source.sha1 is then
 *                       the game ELF's
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
   "hash" (the image's SHA-1; none for a disc with no image SHA-1 on file,
   whose total is then the bytes stored alone), "extract", then "finish" (writing the archive's
   directory and moving it into place; done and total 0).  In "hash" and
   "extract" done and total count bytes over both phases (the image's size
   plus the bytes stored).  Return nonzero to cancel (ignored in "open" and
   "finish"; the next byte count asks again). */
typedef int (*IcoExtractProgressFn)(void *ctx, const char *phase, uint64_t done, uint64_t total);

typedef struct IcoExtractResult {
    char rule[32];     /* ICO_RULE_* (archive.h), or "" when refused */
    char iso_sha1[41]; /* the image's SHA-1; the game ELF's when iso_hashed is 0 */
    uint64_t iso_size;
    int iso_hashed;    /* 0: a disc with no image SHA-1 on file, the image was not read through */
    char elf_sha1[41]; /* the boot ELF as stored, "" if absent */
    int datadf_ok;     /* DATA.DF matched the manifest */
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

/* The check without the image's SHA-1, for a disc that has none on file
   (the magazine demo; main_host.c's start from the image): disc's
   elf_path hashes to its elf_sha1 (written to elf_hex, "" when unread)
   and its df_path matches its DATA.DF manifest (reads DATA.DF through).
   0, or -1 with the reason in why. */
int ico_extract_verify_files(const char *iso_path, const IcoDiscInfo *disc, char elf_hex[41],
                             char *why, size_t whysize);

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
