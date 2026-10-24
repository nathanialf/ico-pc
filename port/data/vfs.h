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
 *   ico_vfs_iso9660   the user's disc image (.iso, .chd, or a raw .bin or
 *                     the .cue sheet that names one) read in place
 *                     (dev mode, the first-run extractor;
 *                     port/data/iso9660.c)
 *   ico_vfs_archive   the archive the first-run extractor writes (ico.o2r,
 *                     port/data/archive.c); it keeps each file's disc LSN
 *                     and size
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

/* The ISO9660 backend (port/data/iso9660.c).  Its location is a disc
   image: a plain .iso; a .chd (MAME's compressed hunks, what PCSX2 users
   keep) holding one, either a DVD CHD or a CD CHD whose first track is the
   data track; or a raw CD image, a .bin (found by the sync pattern at its
   start) or the .cue sheet that names one. */
extern const IcoVfsBackend ico_vfs_iso9660;

/* The Korean disc's ISO directory puts DFDATAS/DATA.DF 153,600 sectors
   before its archive header (archive.h IcoDiscInfo.df_lsn_shift).  The
   backend finds the disc by SYSTEM.CNF's BOOT2 file when it mounts and
   answers a lookup of DFDATAS/DATA.DF with the shifted entry: lsn + shift,
   size - shift * 2048 (a lookup fails if the directory's size is smaller).
   For the tests' synthetic images, which name no disc of the table: this
   makes every volume mounted afterwards use `sectors` instead of the
   table's value; a negative value goes back to the table. */
void ico_vfs_iso9660_force_df_shift(int64_t sectors);

/* A disc image's logical bytes, whatever the container: an .iso file as it
   is, or the ISO image a .chd or .bin holds (its SHA-1 and size are the
   ISO's).
   The first-run hash and the start-up check read through this. */
typedef struct IcoDiscImage IcoDiscImage;

/* The first track of a cue sheet, what the image needs from it. */
typedef struct {
    char file[512];      /* the FILE name, quotes removed */
    char type[32];       /* the TRACK type, e.g. MODE2/2352 */
    uint32_t unit_bytes; /* the number after the type's slash */
    uint32_t data_off;   /* where a frame's 2048 user bytes start */
    uint32_t first_unit; /* INDEX 01 in frames: where the track's sector 0 sits in the file */
} IcoCueTrack;

/* Parse cue sheet text (not NUL-terminated): the first `FILE "x" BINARY`
   (quoted or bare), the first `TRACK nn TYPE` and its `INDEX 01 mm:ss:ff`;
   keywords in any case, any line ending.  A second FILE, a first track that
   is audio, and a type outside the data types are refused.  0, or -1 with
   the reason in `err`. */
int ico_cue_parse(const char *text, size_t len, IcoCueTrack *out, char *err, size_t errsz);

/* NULL on failure, the reason in the log (stderr). */
IcoDiscImage *ico_disc_image_open(const char *path);
void ico_disc_image_close(IcoDiscImage *img);
/* the logical size in bytes */
uint64_t ico_disc_image_bytes(const IcoDiscImage *img);
/* read `len` bytes at `offset`; 0 on success, -1 if any byte lies past the
   end or the read fails */
int ico_disc_image_read(IcoDiscImage *img, uint64_t offset, void *dst, size_t len);

/* Mount a volume with a backend; NULL on failure (the reason goes to
   stderr). */
IcoVfs *ico_vfs_mount(const IcoVfsBackend *backend, const char *location);
void ico_vfs_unmount(IcoVfs *vfs);

/* A second, independent mount of vfs's volume: the same backend on the same
   location (ico_vfs_mount again, so the Korean DATA.DF shift is found
   again), with state of its own (the image backend its own file handle
   and, for a .chd, its own hunk buffers; the archive its own FILE*).  An
   IcoVfs is not thread-safe, but two mounts share nothing: one thread may
   read one while another reads the other (the texture export,
   port/render/texpack_export.c, reads its reopen of ico_vfs_disc()).  NULL
   when the volume cannot be mounted again (the reason on stderr); the new
   mount is unmounted on its own and may outlive vfs. */
IcoVfs *ico_vfs_reopen(const IcoVfs *vfs);
/* The location vfs was mounted from (NULL for NULL). */
const char *ico_vfs_location(const IcoVfs *vfs);

/* Where the game's files are on the volume, read once at mount from
   SYSTEM.CNF's BOOT2 file and the disc row that boots with it (archive.h
   IcoDiscInfo.game_dir): "" for a disc whose game is the root (every
   retail disc), "CUS97113" on the June 2001 magazine demo, whose BOOT2 is
   the magazine's menu; "" when SYSTEM.CNF names no row.  The game's own
   requests spell the folder out ("cdrom0:\\CUS97113\\LIBSD.IRX;1"), so
   they need none of this: it is for the port's own reads.
   ico_vfs_df_path: DATA.DF's path, the row's df_path ("DFDATAS/DATA.DF",
   "CUS97113/DFDATAS/DATA.DF"; "DFDATAS/DATA.DF" for no row).
   ico_vfs_game_path: `file` ("SNDN2DRV.IRX") under the game's folder into
   out; 0, or -1 when it does not fit.  NULL vfs: the root's answers. */
const char *ico_vfs_game_dir(const IcoVfs *vfs);
const char *ico_vfs_df_path(const IcoVfs *vfs);
int ico_vfs_game_path(const IcoVfs *vfs, const char *file, char *out, size_t outsz);

/* The disc the libcdvd layer reads (port/data/cdvd_host.c).  Setting it
   does not take ownership; NULL detaches it ("no disc"). */
void ico_vfs_set_disc(IcoVfs *vfs);
IcoVfs *ico_vfs_disc(void);

/* Rewrite a game or host spelling of a disc path into "DIR/FILE" form:
   strips a device prefix ("cdrom0:", "cdrom:"), leading separators and a
   ";<version>" suffix, maps '\\' to '/', and upper-cases.  Returns 0, or -1
   if the result does not fit in `outsz`. */
int ico_vfs_normalize(const char *in, char *out, size_t outsz);
/* SYSTEM.CNF's BOOT2 line as the boot file's normalized name ("SCPS_560.01"); 0, or -1
   (boot empty) when the line is absent or too long. */
int ico_disc_cnf_boot(const char *cnf, char *boot, size_t size);

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
