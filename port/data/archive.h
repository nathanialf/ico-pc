/*
 * port/data/archive.h
 *
 * The extracted game data, one archive per disc (ico_discs' archive_name:
 * `ico.o2r` for the European disc, `ico-jpn.o2r` for the Japanese one,
 * `ico-kor.o2r`, `ico-ntsc.o2r`, ... for the others): a ZIP whose entries are stored, not compressed,
 * written once by the first-run extractor (extract.h) from the user's disc
 * image.
 *
 *   disc/<PATH>   the disc file PATH ("DFDATAS/DATA.DF", "SCES_507.60";
 *                 on a disc whose game is in a folder, IcoDiscInfo.game_dir,
 *                 under it: "CUS97113/DFDATAS/DATA.DF"), byte for byte
 *   tail/<PATH>   the bytes after PATH's end up to its last sector's end,
 *                 only when they are not all zero (the PAL disc has none)
 *   meta.json     the source image (SHA-1, size, the rule that accepted it;
 *                 for a disc with no image SHA-1 on file the image is not
 *                 hashed and "sha1" holds the game ELF's), the extractor,
 *                 the disc id, the volume's sector count and, for every disc
 *                 file and directory the archive answers for, its path, ISO
 *                 name, first LSN, size and date (an entry with no "data"
 *                 is looked up as on the disc but not stored: the boot file
 *                 of a disc whose BOOT2 names a menu)
 *
 * The backend (`ico_vfs_archive`) answers look-ups from meta.json and reads
 * sectors by LSN: each stored file covers the LSNs it had on the disc, so
 * DATA.DF keeps one contiguous range and `DATA.DF's LSN + offset / 2048`
 * (fumi/ios/cdvd.c) addresses the same bytes as on the disc. A sector no
 * stored file covers reads as an error (-1). Reads go straight to the stored
 * bytes with the C library; miniz only parses the ZIP directory at mount.
 */
#ifndef ICO_PORT_ARCHIVE_H
#define ICO_PORT_ARCHIVE_H

#include <stdint.h>
#include <stdio.h>
#include "vfs.h"
/* ICO_REGION_COUNT (the game's region header, by path: port/core has a
   region.h too) */
#include "../../ico2/common/include/region.h"

#define ICO_ARCHIVE_FORMAT "ico.o2r"
#define ICO_ARCHIVE_VERSION 1
/* the extractor that wrote the archive; a different major value is refused
   at mount and the archive is written again */
#define ICO_ARCHIVE_EXTRACTOR "ico-pc extract 1"
#define ICO_ARCHIVE_META "meta.json"

/* One entry of a disc's DATA.DF manifest: a member's offset, size and
   CRC-32 (zlib), in directory order (discs.c). */
typedef struct IcoDfManifestEntry {
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
} IcoDfManifestEntry;

/* The discs this program plays, one per row of config/regions.txt in its
   order (port/core/region.h's rows), and their verification values
   (config/sha1sums.<slug>.txt for the boot ELFs). Each disc's data goes to
   its own archive, so all can be extracted side by side.

   df_lsn_shift: the Korean disc's ISO directory lists \DFDATAS\DATA.DF at
   LSN 3157 with a size of 866,859,008, but the archive header lies 153,600
   sectors (300 MB of filler) later; the original game adds that to the LSN
   in its file search and keeps the size.  The port applies the shift once,
   where the volume answers a lookup of DFDATAS/DATA.DF (iso9660.c, once
   SYSTEM.CNF names this row): the entry's LSN grows by df_lsn_shift and its
   size shrinks by df_lsn_shift * 2048, the span that holds exactly the
   archive.  Everything above the VFS (the extractor, the manifest, the
   .o2r's meta.json lsn/size, sceCdSearchFile, df_pack, the movie reader)
   sees the shifted entry; df_size and the manifest are of that span. */
typedef struct IcoDiscInfo {
    const char *id;        /* "SCES-50760" (meta.json's disc_id) */
    const char *slug;      /* "pal": the region's row (config/regions.txt) */
    const char *boot_elf;  /* SYSTEM.CNF's BOOT2 file, "SCES_507.60" */
    const char *game_dir;  /* "" (the game is the disc's root), or the folder that holds it
                              when BOOT2 names a menu ("CUS97113" on the magazine demo) */
    const char *elf_path;  /* the file elf_sha1 is of, by disc path ("SCES_507.60"; the
                              game's ELF in game_dir when there is one, not the menu) */
    const char *df_path;   /* DATA.DF's disc path ("DFDATAS/DATA.DF", game_dir's prefixed) */
    const char *iso_sha1;  /* the whole image's SHA-1; NULL for a disc identified by its
                              elf_sha1 and DATA.DF manifest alone (the magazine demo) */
    const char *elf_sha1;  /* elf_path's SHA-1 */
    uint64_t iso_bytes;    /* the image's size */
    uint32_t df_size;      /* DATA.DF's size (as the volume reports it, see df_lsn_shift) */
    uint32_t df_count;     /* its directory's member count */
    uint32_t df_dir_crc;   /* the CRC-32 of its directory */
    uint32_t df_lsn_shift; /* 0; 153600 on the Korean and Asian discs (see above) */
    const IcoDfManifestEntry *members; /* df_count entries */
    uint32_t member_count;             /* entries in members (df_count) */
    const char *archive_name;          /* "ico.o2r", "ico-jpn.o2r" */
    const char *image_base;            /* "Ico_PAL": Ico_PAL.iso, .chd, .bin, .cue */
    int cd_type;                       /* sceCdGetDiskType's value for it (cdvd_host.h) */
    const char *product_name;          /* "the European disc": regions.txt's last column */
} IcoDiscInfo;

/* [ICO_REGION_PAL] the European release SCES-50760 (a DVD),
   [ICO_REGION_JPN] the Japanese release SCPS-11003 (a CD),
   [ICO_REGION_KOR] the Korean release SCPS-56001 (a DVD),
   [ICO_REGION_NTSC] the North American release SCUS-97113 (a CD),
   [ICO_REGION_AUG6] the August 2001 preview PROTO-20010806 (a CD; boots
   SCUS_971.13 like the North American disc, told apart by the boot ELF's
   SHA-1), [ICO_REGION_JUN26] the June 2001 magazine demo SCUS-97158 (a
   DVD; its game is in the CUS97113 folder) and [ICO_REGION_CN] the Asian
   release SCPS-55001 (a DVD, its DATA.DF shifted as the Korean disc's);
   defined in discs.c with the manifests */
extern const IcoDiscInfo ico_discs[ICO_REGION_COUNT];

/* The row whose id ("SCPS-11003"), boot file ("SCPS_110.03", as
   SYSTEM.CNF names it after normalizing) or slug ("jpn") is given; NULL
   for none. A boot file two rows share (SCUS_971.13: ntsc and aug6) gives
   the first of them: use it only for what the rows share (df_lsn_shift and
   cd_type, which the tests hold equal) and name the disc with
   ico_disc_identify (extract.h) or ico_disc_by_boot_elf. */
const IcoDiscInfo *ico_disc_by_id(const char *id);
const IcoDiscInfo *ico_disc_by_boot(const char *boot_elf);
/* How many rows boot with that file (1, or 2 for SCUS_971.13; 0 for none). */
int ico_disc_boot_rows(const char *boot_elf);
/* The row that boots with boot_elf and whose elf_sha1 is elf_sha1 (40
   lower-case hex digits); NULL for none. */
const IcoDiscInfo *ico_disc_by_boot_elf(const char *boot_elf, const char *elf_sha1);
const IcoDiscInfo *ico_disc_by_slug(const char *slug);
/* For the tests' synthetic images, as ico_vfs_iso9660_force_df_shift is:
   one more row the look-ups above answer after the table's (a disc whose
   game is in a folder, game_dir, before the table has one); NULL removes
   it. The wording below stays the table's. */
void ico_disc_set_test_row(const IcoDiscInfo *row);

/* Wording built from the rows, so a new row needs no new text.  Each
   returns a static buffer (filled on first use).
   ico_disc_list_text: "the European disc (SCES-50760), the Japanese disc
   (SCPS-11003) or the Korean disc (SCPS-56001)";
   ico_disc_image_names_text(".iso"): "Ico_PAL.iso, Ico_JPN.iso or
   Ico_KOR.iso" (ext "" for the bare names);
   ico_disc_boots_text: "SCES_507.60, SCPS_110.03 or SCPS_560.01";
   ico_disc_archive_names_text: "ico.o2r, ico-jpn.o2r or ico-kor.o2r". */
const char *ico_disc_list_text(void);
const char *ico_disc_image_names_text(const char *ext);
const char *ico_disc_boots_text(void);
const char *ico_disc_archive_names_text(void);

/* The rules the extractor accepts an image by (extract.h). */
#define ICO_RULE_ISO_SHA1 "iso-sha1"
#define ICO_RULE_ELF_DATADF "elf-sha1+datadf-crc"
#define ICO_RULE_UNVERIFIED "unverified"

/* meta.json's header, as ico_archive_read_info returns it. */
typedef struct IcoArchiveInfo {
    char format[16];
    int version;
    char extractor[64];
    char disc_id[16];
    char iso_sha1[41];
    uint64_t iso_size;
    char accepted_by[32];
    char elf_sha1[41];
    uint32_t volume_sectors;
    uint32_t entries;    /* files and directories meta.json lists */
    uint32_t stored;     /* of which files with stored bytes */
    uint64_t file_bytes; /* the archive file's size */
} IcoArchiveInfo;

/* The archive backend: `location` is the archive file's path. */
extern const IcoVfsBackend ico_vfs_archive;

/* ico_vfs_mount(&ico_vfs_archive, path). */
IcoVfs *ico_vfs_mount_archive(const char *path);

/* Opens the archive, checks its structure (every entry meta.json names is
   stored, uncompressed, inside the file, with the size meta.json gives) and
   fills *out. 0, or -1 with the reason in why. */
int ico_archive_read_info(const char *path, IcoArchiveInfo *out, char *why, size_t whysize);

/* Whether the archive was written from one of this build's discs by an
   accepted rule: a disc id of ico_discs, that row's boot ELF SHA-1 (and,
   by iso-sha1, its image SHA-1), the rule iso-sha1 or elf-sha1+datadf-crc,
   this format version and extractor. 1, or 0 with the reason in why. */
int ico_archive_info_acceptable(const IcoArchiveInfo *info, char *why, size_t whysize);

/* Host file helpers shared with the extractor: fopen, remove and the atomic
   replace of `to` by `from` forward to port/platform/host_fs.h (ico_fopen,
   ico_remove, ico_rename_replace: UTF-8 paths on Windows); seek and tell
   are 64-bit, which host_fs.h does not offer. */
FILE *ico_archive_fopen(const char *path, const char *mode);
int ico_archive_seek(FILE *fp, uint64_t offset);
int64_t ico_archive_tell(FILE *fp);
int ico_archive_remove(const char *path);
int ico_archive_replace(const char *from, const char *to);

#endif /* ICO_PORT_ARCHIVE_H */
