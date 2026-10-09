/*
 * port/data/archive.h
 *
 * The extracted game data, `ico.o2r`: a ZIP whose entries are stored, not
 * compressed, written once by the first-run extractor (extract.h) from the
 * user's disc image.
 *
 *   disc/<PATH>   the disc file PATH ("DFDATAS/DATA.DF", "SCES_507.60"),
 *                 byte for byte
 *   tail/<PATH>   the bytes after PATH's end up to its last sector's end,
 *                 only when they are not all zero (the PAL disc has none)
 *   meta.json     the source image (SHA-1, size, the rule that accepted it),
 *                 the extractor, the disc id, the volume's sector count and,
 *                 for every disc file and directory the archive answers for,
 *                 its path, ISO name, first LSN, size and date
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

#define ICO_ARCHIVE_NAME "ico.o2r"
#define ICO_ARCHIVE_FORMAT "ico.o2r"
#define ICO_ARCHIVE_VERSION 1
/* the extractor that wrote the archive; a different major value is refused
   at mount and the archive is written again */
#define ICO_ARCHIVE_EXTRACTOR "ico-pc extract 1"
#define ICO_ARCHIVE_META "meta.json"

/* The disc this build plays, and its verification values (config/sha1sums.txt). */
#define ICO_DISC_ID "SCES-50760"
#define ICO_DISC_ISO_SHA1 "1017b53f6e80f41f823369b0be1d8c69f7e16dc6"
#define ICO_DISC_ELF_SHA1 "da3644c54c26fe760f3b6a591a5fc2eab396ed2b"

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

/* Whether the archive was written from this build's disc by an accepted
   rule: disc id ICO_DISC_ID, the boot ELF's SHA-1 ICO_DISC_ELF_SHA1, the
   rule iso-sha1 or elf-sha1+datadf-crc, this format version and extractor.
   1, or 0 with the reason in why. */
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
