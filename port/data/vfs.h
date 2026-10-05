/*
 * port/data/vfs.h
 *
 * The port's read-only view of the game disc.  The game addresses the disc
 * the way libcdvd does: it looks a file up by its ISO9660 path, gets back the
 * file's first logical sector number (LSN) and its byte size, and then reads
 * whole 2048-byte sectors by LSN.  It also does LSN arithmetic of its own:
 * DATA.DF's directory gives byte offsets inside DATA.DF, which
 * fumi/ios/cdvd.c turns into LSNs as DATA.DF's LSN + offset / 2048.  So
 * every backend serves one flat LSN space with the disc's own numbering.
 *
 * Backends:
 *   ico_vfs_iso9660   the user's ISO image read in place (dev mode,
 *                     port/data/iso9660.c)
 *   ico_vfs_archive   the archive the first-run extractor writes (ico.o2r,
 *                     port/data/archive.c); it keeps each file's disc LSN
 *                     and size (docs/port/DATA.md)
 *
 * Path spellings accepted everywhere a path is taken (case-insensitive):
 *   DFDATAS/DATA.DF        DFDATAS\DATA.DF;1      \DFDATAS\DATA.DF;1
 *   cdrom0:\SYSTEM.CNF;1   /SCES_507.60
 * ico_vfs_normalize turns any of these into "DFDATAS/DATA.DF" form.
 */
#ifndef ICO_PORT_VFS_H
#define ICO_PORT_VFS_H

#include <stddef.h>
#include <stdint.h>

#define ICO_VFS_SECTOR 2048u
#define ICO_VFS_PATH_MAX 256

/* A file the volume holds: its first sector, its byte size and its
   recording date as ISO9660 stores it (years since 1900, month, day, hour,
   minute, second, GMT offset in 15-minute units). */
typedef struct IcoVfsEntry {
    uint32_t lsn;
    uint32_t size;
    uint8_t date[7];
    uint8_t is_dir;
    char name[32]; /* the last path component as the volume spells it, ";1" included */
} IcoVfsEntry;

/* The backend table.  `state` is the backend's own per-volume record. */
typedef struct IcoVfsBackend {
    const char *name;
    /* open the volume at `location` (a file or directory path on the host);
       0 on success, a negative value on failure, with *state set */
    int (*mount)(void **state, const char *location);
    void (*unmount)(void *state);
    /* look up a normalized path ("DIR/FILE", no version); 0 if found */
    int (*lookup)(void *state, const char *path, IcoVfsEntry *out);
    /* read `count` sectors from `lsn`; 0 on success, -1 if any sector lies
       outside the volume or the host read fails */
    int (*read_sectors)(void *state, uint32_t lsn, uint32_t count, void *dst);
    /* the volume's size in sectors */
    uint32_t (*volume_sectors)(void *state);
} IcoVfsBackend;

typedef struct IcoVfs IcoVfs;

/* The ISO9660 backend (port/data/iso9660.c). */
extern const IcoVfsBackend ico_vfs_iso9660;

/* Mount a volume with a backend; NULL on failure (the reason goes to
   stderr). */
IcoVfs *ico_vfs_mount(const IcoVfsBackend *backend, const char *location);
void ico_vfs_unmount(IcoVfs *vfs);

/* The disc the libcdvd layer reads (port/data/cdvd_host.c).  Setting it
   does not take ownership; NULL detaches it ("no disc"). */
void ico_vfs_set_disc(IcoVfs *vfs);
IcoVfs *ico_vfs_disc(void);

/* Rewrite a game or host spelling of a disc path into "DIR/FILE" form:
   strips a device prefix ("cdrom0:", "cdrom:"), leading separators and a
   ";<version>" suffix, maps '\\' to '/', and upper-cases.  Returns 0, or -1
   if the result does not fit in `outsz`. */
int ico_vfs_normalize(const char *in, char *out, size_t outsz);

/* Look a path up in any of the accepted spellings; 0 if found. */
int ico_vfs_stat(IcoVfs *vfs, const char *path, IcoVfsEntry *out);

/* An open file: a looked-up entry plus the volume it is on. */
typedef struct IcoVfsFile {
    IcoVfs *vfs;
    IcoVfsEntry entry;
} IcoVfsFile;

int ico_vfs_open(IcoVfs *vfs, const char *path, IcoVfsFile *f);
uint32_t ico_vfs_size(const IcoVfsFile *f);

/* Read up to `len` bytes at byte `offset` in the file; returns the bytes
   read (short at the end of the file), or -1 on a read error. */
int64_t ico_vfs_read(const IcoVfsFile *f, uint64_t offset, void *dst, size_t len);

/* Read whole sectors by LSN anywhere on the volume. */
int ico_vfs_read_sectors(IcoVfs *vfs, uint32_t lsn, uint32_t count, void *dst);
uint32_t ico_vfs_volume_sectors(IcoVfs *vfs);

/* LSN <-> byte offset on the volume. */
static inline uint64_t ico_vfs_lsn_to_offset(uint32_t lsn)
{
    return (uint64_t)lsn * ICO_VFS_SECTOR;
}

static inline uint32_t ico_vfs_offset_to_lsn(uint64_t offset)
{
    return (uint32_t)(offset / ICO_VFS_SECTOR);
}

/* Sectors a file of `size` bytes spans. */
static inline uint32_t ico_vfs_size_to_sectors(uint32_t size)
{
    return (uint32_t)(((uint64_t)size + ICO_VFS_SECTOR - 1) / ICO_VFS_SECTOR);
}

#endif /* ICO_PORT_VFS_H */
