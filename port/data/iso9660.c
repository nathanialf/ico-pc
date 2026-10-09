/*
 * port/data/iso9660.c
 *
 * The VFS's ISO9660 backend: a minimal read-only reader over a disc image
 * on the host, enough for a PS2 DVD.  It reads the primary volume
 * descriptor and walks directories from the root record; it needs no path
 * table, no Joliet or Rock Ridge (the PAL disc has neither: its descriptor
 * set is one primary descriptor and the terminator) and no multi-extent
 * files (a multi-extent entry is refused).  References: ECMA-119 2nd edition
 * (1987), sections 8.4 (primary volume descriptor) and 9.1 (directory
 * record).
 *
 * Mastering tools for the PS2 record a directory's data length as the bytes
 * in use rather than a multiple of the sector size; the walk reads every
 * sector the length touches and stops at a zero length byte in each.
 *
 * The image is a plain .iso file, a raw CD image (.bin, or the .cue sheet
 * that names one) or a .chd (PCSX2 users keep their discs as MAME
 * "compressed hunks of data", read here with libchdr).  All give the same
 * logical bytes, the ISO's, through IcoDiscImage:
 *   - a DVD CHD (chdman createdvd): 2048-byte units, the ISO's bytes in
 *     order, `logicalbytes` long;
 *   - a CD CHD (chdman createcd, which some users run on a DVD image):
 *     2448-byte frames (2352 sector bytes plus 96 subcode bytes) described
 *     by the track metadata (CHT2 or CHTR); the first track must be a data
 *     track, and each frame's 2048 user bytes are taken where the track type
 *     puts them (libchdr's cdrom.h lists the types);
 *   - a raw CD image (.bin): 2352-byte sectors with no subcode, recognised
 *     by the 12-byte sync pattern at the start of the file (00, ten FF,
 *     00).  Byte 15 is the sector mode: in mode 1 the 2048 user bytes
 *     start at byte 16, in mode 2 form 1 at byte 24 after the subheader (a
 *     form 2 sector, submode bit 0x20 at byte 18, holds no ISO data and is
 *     refused).  The logical size is the whole frames times 2048.  A .cue
 *     sheet is read for its first FILE, first TRACK type and INDEX 01 (the
 *     frame where the track's sector 0 sits); the .bin it names is opened
 *     from beside the sheet, and when the sheet's type and the sector
 *     header both speak they must agree.  A sheet with several files or a
 *     first track that is not data is refused.
 * Each CHD read decodes whole hunks through a one-hunk cache.  A CHD that
 * needs a parent image (a "diff" CHD) is refused.
 */
#define _FILE_OFFSET_BITS 64

#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libchdr/chd.h>
#include "host_fs.h"
#include "../include/ico_endian.h"

#define PVD_FIRST_LSN 16
#define PVD_SCAN_MAX 64           /* descriptors examined before giving up */
#define DIR_MAX_BYTES (16u << 20) /* a sanity bound on one directory */
#define DIR_REC_MIN 34

#define CHD_MAGIC "MComprHD"
#define CD_FRAME_BYTES 2448u /* libchdr cdrom.h CD_FRAME_SIZE: 2352 + 96 subcode */
#define CD_RAW_BYTES 2352u
#define NO_HUNK 0xffffffffu
/* chd.h's CDROM_TRACK_METADATA2_FORMAT and CDROM_TRACK_METADATA_FORMAT with
   the string fields bounded to the buffers below */
#define CHT2_SCAN                                                                                  \
    "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d"
#define CHTR_SCAN "TRACK:%d TYPE:%31s SUBTYPE:%31s FRAMES:%d"

struct IcoDiscImage {
    FILE *fp;
    uint64_t bytes; /* the logical (ISO) size */
    /* a .chd only */
    chd_file *chd;
    uint32_t hunkbytes;
    /* a .chd or a raw .bin (0 unitbytes: a plain .iso) */
    uint64_t store_bytes; /* the container's own size: CHD hunk data in use, or the file */
    uint32_t unitbytes;   /* 2048 (DVD), or a CD frame (2448 CHD, 2352 or 2336 .bin) */
    uint32_t data_off;    /* CD: where a frame's 2048 user bytes start */
    uint32_t first_unit;  /* CD: the frame that holds the track's sector 0 */
    uint8_t *hunk;
    uint32_t hunk_num; /* the hunk in `hunk`, NO_HUNK when none */
};

typedef struct {
    IcoDiscImage *img;
    uint32_t volume_sectors;
    uint32_t root_lsn;
    uint32_t root_size;
    uint8_t root_date[7];
} Iso;

static int seek64(FILE *fp, uint64_t off)
{
#ifdef _WIN32
    return _fseeki64(fp, (long long)off, SEEK_SET);
#else
    return fseeko(fp, (off_t)off, SEEK_SET);
#endif
}

static int64_t file_size(FILE *fp)
{
#ifdef _WIN32
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    return (int64_t)_ftelli64(fp);
#else
    if (fseeko(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    return (int64_t)ftello(fp);
#endif
}

/* --- the CHD -------------------------------------------------------------- */

/* Where a CD track type puts a sector's 2048 user bytes inside the stored
   frame, or -1 for a track that holds none (audio, mode 2 form 2).  The
   names are the TYPE values chdman writes in the track metadata (libchdr
   cdrom.h CD_TRACK_*), plus their cue-sheet spellings (MODE1/2048 and the
   like) in case another tool writes those: a cooked track stores its
   sectors' data from the frame's start, a raw one the whole 2352-byte
   sector (12 sync bytes and a 4-byte header, then the user data in mode 1;
   for mode 2 form 1 an 8-byte subheader follows the header). */
static int cd_data_offset(const char *type)
{
    static const struct {
        const char *name;
        int off;
    } types[] = {
        {"MODE1", 0},          {"MODE1/2048", 0}, {"MODE1_RAW", 16},  {"MODE1/2352", 16},
        {"MODE2_FORM1", 0},    {"MODE2/2048", 0}, {"MODE2", 8},       {"MODE2/2336", 8},
        {"MODE2_FORM_MIX", 8}, {"MODE2_RAW", 24}, {"MODE2/2352", 24},
    };

    size_t i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(type, types[i].name) == 0) {
            return types[i].off;
        }
    }
    return -1;
}

/* A CD CHD: read the first track's metadata and set where its sectors
   are.  0, or -1 with the reason logged. */
static int chd_cd_layout(IcoDiscImage *img, const char *path)
{
    char meta[256];
    char type[32], sub[32], pgtype[32], pgsub[32];
    uint32_t len = 0;
    int track = 0, frames = 0, pregap = 0, postgap = 0;
    int off;

    memset(meta, 0, sizeof(meta));
    pgtype[0] = '\0';
    if (chd_get_metadata(img->chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof(meta) - 1, &len, NULL,
                         NULL) == CHDERR_NONE) {
        if (sscanf(meta, CHT2_SCAN, &track, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) !=
            8) {
            track = 0;
        }
    } else if (chd_get_metadata(img->chd, CDROM_TRACK_METADATA_TAG, 0, meta, sizeof(meta) - 1, &len,
                                NULL, NULL) == CHDERR_NONE) {
        if (sscanf(meta, CHTR_SCAN, &track, type, sub, &frames) != 4) {
            track = 0;
        }
    } else {
        fprintf(stderr,
                "iso9660: %s: a CD-style CHD without readable track information is not "
                "supported\n",
                path);
        return -1;
    }
    if (track != 1 || frames <= 0) {
        fprintf(stderr, "iso9660: %s: the CHD's track information is malformed (\"%s\")\n", path,
                meta);
        return -1;
    }
    off = cd_data_offset(type);
    if (off < 0) {
        fprintf(stderr, "iso9660: %s: the CHD's first track is %s, not a data track\n", path, type);
        return -1;
    }
    /* a pregap the CHD stores ("V" types) comes before the track's sector
       0 and is counted in its frames */
    if (pregap > 0 && pgtype[0] == 'V') {
        if (pregap >= frames) {
            fprintf(stderr, "iso9660: %s: the CHD's track information is malformed (\"%s\")\n",
                    path, meta);
            return -1;
        }
        img->first_unit = (uint32_t)pregap;
        frames -= pregap;
    }
    img->data_off = (uint32_t)off;
    img->bytes = (uint64_t)frames * ICO_VFS_SECTOR;
    fprintf(stderr, "iso9660: %s: a CD-style CHD, track 1 %s, %d sectors\n", path, type, frames);
    return 0;
}

static int chd_open_image(IcoDiscImage *img, const char *path)
{
    const chd_header *h;
    chd_error err;

    err = chd_open_file(img->fp, CHD_OPEN_READ, NULL, &img->chd);
    if (err == CHDERR_REQUIRES_PARENT) {
        fprintf(stderr,
                "iso9660: %s: a CHD that needs its parent image is not supported; make a "
                "standalone .chd or use the .iso\n",
                path);
        return -1;
    }
    if (err != CHDERR_NONE) {
        fprintf(stderr, "iso9660: %s: cannot open the CHD: %s\n", path, chd_error_string(err));
        img->chd = NULL;
        return -1;
    }
    h = chd_get_header(img->chd);
    img->hunkbytes = h->hunkbytes;
    img->store_bytes = (uint64_t)h->hunkbytes * h->totalhunks;
    img->unitbytes = h->unitbytes;
    if (h->hunkbytes == 0 || h->hunkbytes % h->unitbytes != 0) {
        fprintf(stderr, "iso9660: %s: the CHD's hunk size %u is not whole units of %u bytes\n",
                path, (unsigned)h->hunkbytes, (unsigned)h->unitbytes);
        return -1;
    }
    if (h->unitbytes == ICO_VFS_SECTOR) {
        /* a DVD CHD: the ISO's bytes in order */
        img->bytes = h->logicalbytes;
        fprintf(stderr, "iso9660: %s: a DVD CHD, %llu bytes\n", path,
                (unsigned long long)img->bytes);
    } else if (h->unitbytes == CD_FRAME_BYTES || h->unitbytes == CD_RAW_BYTES) {
        if (chd_cd_layout(img, path) != 0) {
            return -1;
        }
    } else {
        fprintf(stderr,
                "iso9660: %s: a CHD with %u-byte units is not a disc image this program can "
                "read (a DVD CHD has 2048, a CD CHD 2448)\n",
                path, (unsigned)h->unitbytes);
        return -1;
    }
    if (img->bytes == 0) {
        fprintf(stderr, "iso9660: %s: the CHD holds no data\n", path);
        return -1;
    }
    img->hunk = malloc(img->hunkbytes);
    if (img->hunk == NULL) {
        return -1;
    }
    img->hunk_num = NO_HUNK;
    return 0;
}

/* `n` bytes at byte `pos` of the CHD's own data (hunk after hunk). */
static int chd_read_bytes(IcoDiscImage *img, uint64_t pos, uint8_t *dst, size_t n)
{
    while (n > 0) {
        uint64_t hunk = pos / img->hunkbytes;
        uint32_t o = (uint32_t)(pos % img->hunkbytes);
        size_t k = img->hunkbytes - o;
        chd_error err;

        if (pos >= img->store_bytes || hunk >= NO_HUNK) {
            return -1;
        }
        if (k > n) {
            k = n;
        }
        if (img->hunk_num != (uint32_t)hunk) {
            img->hunk_num = NO_HUNK;
            err = chd_read(img->chd, (uint32_t)hunk, img->hunk);
            if (err != CHDERR_NONE) {
                fprintf(stderr, "iso9660: the CHD's block %llu cannot be read: %s\n",
                        (unsigned long long)hunk, chd_error_string(err));
                return -1;
            }
            img->hunk_num = (uint32_t)hunk;
        }
        memcpy(dst, img->hunk + o, k);
        dst += k;
        pos += k;
        n -= k;
    }
    return 0;
}

/* `n` bytes at byte `pos` of the container's own data: the CHD's hunks, or
   the .bin file.  The CD frame-to-sector mapping reads through this. */
static int unit_read(IcoDiscImage *img, uint64_t pos, uint8_t *dst, size_t n)
{
    if (img->chd != NULL) {
        return chd_read_bytes(img, pos, dst, n);
    }
    if (pos > img->store_bytes || n > img->store_bytes - pos || seek64(img->fp, pos) != 0) {
        return -1;
    }
    return fread(dst, 1, n, img->fp) == n ? 0 : -1;
}

/* --- the raw CD image and its cue sheet ------------------------------------ */

#define CUE_MAX_BYTES (64u << 10) /* a cue sheet is a few lines; read no more */
#define CUE_LINE_MAX 1024

static int raw_sync(const uint8_t *h)
{
    int i;

    if (h[0] != 0x00 || h[11] != 0x00) {
        return 0;
    }
    for (i = 1; i < 11; i++) {
        if (h[i] != 0xff) {
            return 0;
        }
    }
    return 1;
}

static int ci_equal(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a, y = *b;

        if (x >= 'a' && x <= 'z') {
            x = (char)(x - 'a' + 'A');
        }
        if (y >= 'a' && y <= 'z') {
            y = (char)(y - 'a' + 'A');
        }
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* The next word of a line: bare, or quoted (the quotes dropped, spaces
   kept).  1 with the word in `out`, 0 when the line has no more, -1 when
   the word does not fit. */
static int cue_word(const char **pp, char *out, size_t outsz)
{
    const char *p = *pp;
    size_t n = 0;
    int quoted;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        *pp = p;
        return 0;
    }
    quoted = *p == '"';
    if (quoted) {
        p++;
    }
    while (*p != '\0' && (quoted ? *p != '"' : (*p != ' ' && *p != '\t'))) {
        if (n + 1 >= outsz) {
            return -1;
        }
        out[n++] = *p++;
    }
    if (quoted && *p == '"') {
        p++;
    }
    out[n] = '\0';
    *pp = p;
    return 1;
}

static void cue_fail(char *err, size_t errsz, const char *msg)
{
    snprintf(err, errsz, "%s", msg);
}

/* "mm:ss:ff" into frames (75 to the second); 0, or -1 if malformed. */
static int cue_msf(const char *w, uint32_t *frames)
{
    unsigned mm, ss, ff;
    char tail;

    if (sscanf(w, "%u:%u:%u%c", &mm, &ss, &ff, &tail) != 3 || ss >= 60 || ff >= 75 || mm > 9999) {
        return -1;
    }
    *frames = (mm * 60u + ss) * 75u + ff;
    return 0;
}

int ico_cue_parse(const char *text, size_t len, IcoCueTrack *out, char *err, size_t errsz)
{
    char line[CUE_LINE_MAX], kw[32], a[512], b[512];
    size_t at = 0;
    int nfile = 0, ntrack = 0, have_index = 0;

    memset(out, 0, sizeof(*out));
    if (len >= 3 && memcmp(text, "\xef\xbb\xbf", 3) == 0) {
        at = 3; /* a byte-order mark */
    }
    while (at < len) {
        const char *p = line;
        size_t n = 0;

        /* one line: either kind of line end, an over-long line cut short */
        while (at < len && text[at] != '\n' && text[at] != '\r') {
            if (n + 1 < sizeof(line)) {
                line[n++] = text[at];
            }
            at++;
        }
        while (at < len && (text[at] == '\n' || text[at] == '\r')) {
            at++;
        }
        line[n] = '\0';
        if (cue_word(&p, kw, sizeof(kw)) != 1) {
            continue;
        }
        if (ci_equal(kw, "FILE")) {
            if (nfile > 0) {
                cue_fail(err, errsz,
                         "a cue sheet with several files is not supported; choose the .bin of "
                         "the data track");
                return -1;
            }
            nfile++;
            if (cue_word(&p, a, sizeof(a)) != 1 || a[0] == '\0' ||
                cue_word(&p, b, sizeof(b)) != 1 || !ci_equal(b, "BINARY")) {
                cue_fail(err, errsz, "the cue sheet's FILE line is not a name and BINARY");
                return -1;
            }
            memcpy(out->file, a, strlen(a) + 1);
        } else if (ci_equal(kw, "TRACK")) {
            ntrack++;
            if (ntrack == 1) {
                const char *slash;
                char *end;
                unsigned long unit;
                int off;

                if (nfile == 0 || cue_word(&p, a, sizeof(a)) != 1 ||
                    cue_word(&p, b, sizeof(b)) != 1) {
                    cue_fail(err, errsz, "the cue sheet's TRACK line has no file or type");
                    return -1;
                }
                if (ci_equal(b, "AUDIO")) {
                    cue_fail(err, errsz, "the cue sheet's first track is audio, not a data track");
                    return -1;
                }
                slash = strchr(b, '/');
                off = -1;
                if (slash != NULL) {
                    /* the table spells its names in capitals */
                    char up[32];
                    size_t i;

                    for (i = 0; b[i] != '\0' && i + 1 < sizeof(up); i++) {
                        up[i] = (b[i] >= 'a' && b[i] <= 'z') ? (char)(b[i] - 'a' + 'A') : b[i];
                    }
                    up[i] = '\0';
                    off = cd_data_offset(up);
                }
                unit = slash != NULL ? strtoul(slash + 1, &end, 10) : 0;
                if (off < 0 || slash == NULL || *end != '\0' || unit < (unsigned long)off + 2048 ||
                    unit > 4096) {
                    snprintf(err, errsz, "the cue sheet's first track type %s is not supported", b);
                    return -1;
                }
                snprintf(out->type, sizeof(out->type), "%s", b);
                out->unit_bytes = (uint32_t)unit;
                out->data_off = (uint32_t)off;
            }
        } else if (ci_equal(kw, "INDEX") && ntrack == 1) {
            if (cue_word(&p, a, sizeof(a)) == 1 && atoi(a) == 1 &&
                cue_word(&p, b, sizeof(b)) == 1) {
                if (cue_msf(b, &out->first_unit) != 0) {
                    cue_fail(err, errsz, "the cue sheet's INDEX 01 time is malformed");
                    return -1;
                }
                have_index = 1;
            }
        }
    }
    if (nfile == 0 || ntrack == 0 || !have_index) {
        cue_fail(err, errsz, "the cue sheet has no FILE, TRACK and INDEX 01 for its first track");
        return -1;
    }
    return 0;
}

/* Set the layout of a raw .bin file of `size` bytes.  `cue` is its sheet's
   first track, or NULL for a bare .bin, which must prove itself by its sync
   pattern.  0, or -1 with the reason logged. */
static int raw_layout(IcoDiscImage *img, const char *path, uint64_t size, const IcoCueTrack *cue)
{
    uint32_t unit = cue != NULL ? cue->unit_bytes : CD_RAW_BYTES;
    uint32_t first = cue != NULL ? cue->first_unit : 0;
    int off = cue != NULL ? (int)cue->data_off : -1;
    uint8_t h[24];
    uint64_t frames;

    /* the track's first sector header, when the sheet puts it in a raw frame */
    if (unit == CD_RAW_BYTES && size >= ((uint64_t)first + 1) * unit &&
        seek64(img->fp, (uint64_t)first * unit) == 0 &&
        fread(h, 1, sizeof(h), img->fp) == sizeof(h) && raw_sync(h)) {
        int mode = h[15];
        int sniffed = mode == 1 ? 16 : mode == 2 ? 24 : -1;

        if (sniffed < 0) {
            fprintf(stderr, "iso9660: %s: sector mode %d is not supported (mode 1 or 2 only)\n",
                    path, mode);
            return -1;
        }
        if (mode == 2 && (h[18] & 0x20) != 0) {
            fprintf(stderr,
                    "iso9660: %s: the first sector is mode 2 form 2, which holds no disc data\n",
                    path);
            return -1;
        }
        if (cue != NULL && off != sniffed) {
            fprintf(stderr, "iso9660: %s: the cue sheet says %s but the sectors are mode %d\n",
                    path, cue->type, mode);
            return -1;
        }
        off = sniffed;
    } else if (cue == NULL) {
        fprintf(stderr, "iso9660: %s holds no ISO9660 volume\n", path);
        return -1;
    }
    frames = size / unit;
    if (size % unit != 0) {
        fprintf(stderr,
                "iso9660: %s: the size is not a whole number of %u-byte frames; the last "
                "part is ignored\n",
                path, (unsigned)unit);
    }
    if (frames <= first) {
        fprintf(stderr, "iso9660: %s holds no data after the cue sheet's start\n", path);
        return -1;
    }
    img->unitbytes = unit;
    img->data_off = (uint32_t)off;
    img->first_unit = first;
    img->store_bytes = size;
    img->bytes = (frames - first) * ICO_VFS_SECTOR;
    fprintf(stderr, "iso9660: %s: a raw CD image, %u-byte frames, data at %d, %llu sectors\n", path,
            (unsigned)unit, off, (unsigned long long)(frames - first));
    return 0;
}

static int has_cue_ext(const char *path)
{
    size_t n = strlen(path);

    return n >= 4 && ci_equal(path + n - 4, ".cue");
}

/* Open the .bin a cue sheet names (beside the sheet) into `img`. */
static int cue_open_image(IcoDiscImage *img, const char *path)
{
    char text[CUE_MAX_BYTES], err[160], bin[2048];
    IcoCueTrack track;
    const char *base, *dir_end = path, *p;
    size_t n, dir;
    int64_t size;
    FILE *fp = ico_fopen(path, "rb");

    if (fp == NULL) {
        fprintf(stderr, "iso9660: cannot open %s\n", path);
        return -1;
    }
    n = fread(text, 1, sizeof(text), fp);
    fclose(fp);
    if (ico_cue_parse(text, n, &track, err, sizeof(err)) != 0) {
        fprintf(stderr, "iso9660: %s: %s\n", path, err);
        return -1;
    }
    for (p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            dir_end = p + 1;
        }
    }
    dir = (size_t)(dir_end - path);
    base = track.file;
    for (p = track.file; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (dir + strlen(base) + 1 > sizeof(bin) || *base == '\0') {
        fprintf(stderr, "iso9660: %s: the cue sheet's file name is unusable\n", path);
        return -1;
    }
    memcpy(bin, path, dir);
    memcpy(bin + dir, base, strlen(base) + 1);
    img->fp = ico_fopen(bin, "rb");
    if (img->fp == NULL) {
        fprintf(stderr, "iso9660: %s: cannot open %s, the file the cue sheet names\n", path, bin);
        return -1;
    }
    size = file_size(img->fp);
    if (size <= 0) {
        fprintf(stderr, "iso9660: %s is empty or cannot be read\n", bin);
        return -1;
    }
    return raw_layout(img, bin, (uint64_t)size, &track);
}

/* --- the image ------------------------------------------------------------ */

IcoDiscImage *ico_disc_image_open(const char *path)
{
    IcoDiscImage *img = calloc(1, sizeof(*img));
    uint8_t head[16];
    size_t got;
    int64_t size;

    if (img == NULL) {
        return NULL;
    }
    if (has_cue_ext(path)) {
        if (cue_open_image(img, path) != 0) {
            ico_disc_image_close(img);
            return NULL;
        }
        return img;
    }
    img->fp = ico_fopen(path, "rb"); /* UTF-8 path (host_fs.h) */
    if (img->fp == NULL) {
        fprintf(stderr, "iso9660: cannot open %s\n", path);
        free(img);
        return NULL;
    }
    got = fread(head, 1, sizeof(head), img->fp);
    if (got >= sizeof(CHD_MAGIC) - 1 && memcmp(head, CHD_MAGIC, sizeof(CHD_MAGIC) - 1) == 0) {
        if (chd_open_image(img, path) != 0) {
            ico_disc_image_close(img);
            return NULL;
        }
        return img;
    }
    size = file_size(img->fp);
    if (size <= 0) {
        fprintf(stderr, "iso9660: %s is empty or cannot be read\n", path);
        ico_disc_image_close(img);
        return NULL;
    }
    if (got == sizeof(head) && raw_sync(head)) {
        if (raw_layout(img, path, (uint64_t)size, NULL) != 0) {
            ico_disc_image_close(img);
            return NULL;
        }
        return img;
    }
    img->bytes = (uint64_t)size;
    return img;
}

void ico_disc_image_close(IcoDiscImage *img)
{
    if (img == NULL) {
        return;
    }
    if (img->chd != NULL) {
        chd_close(img->chd); /* it does not own the FILE (chd_open_file) */
    }
    if (img->fp != NULL) {
        fclose(img->fp);
    }
    free(img->hunk);
    free(img);
}

uint64_t ico_disc_image_bytes(const IcoDiscImage *img)
{
    return img->bytes;
}

int ico_disc_image_read(IcoDiscImage *img, uint64_t offset, void *dst, size_t len)
{
    uint8_t *d = dst;

    if (offset > img->bytes || len > img->bytes - offset) {
        return -1;
    }
    if (img->unitbytes == 0) {
        if (seek64(img->fp, offset) != 0) {
            return -1;
        }
        return fread(dst, 1, len, img->fp) == len ? 0 : -1;
    }
    if (img->unitbytes == ICO_VFS_SECTOR && img->data_off == 0 && img->first_unit == 0) {
        return unit_read(img, offset, d, len);
    }
    /* a CD image: sector by sector, each from its frame */
    while (len > 0) {
        uint64_t sector = offset / ICO_VFS_SECTOR;
        uint32_t in = (uint32_t)(offset % ICO_VFS_SECTOR);
        size_t k = ICO_VFS_SECTOR - in;
        uint64_t pos = (img->first_unit + sector) * img->unitbytes + img->data_off + in;

        if (k > len) {
            k = len;
        }
        if (unit_read(img, pos, d, k) != 0) {
            return -1;
        }
        d += k;
        offset += k;
        len -= k;
    }
    return 0;
}

/* --- the volume ----------------------------------------------------------- */

static int iso_read_raw(Iso *iso, uint32_t lsn, uint32_t count, void *dst)
{
    return ico_disc_image_read(iso->img, ico_vfs_lsn_to_offset(lsn), dst,
                               (size_t)count * ICO_VFS_SECTOR);
}

static int iso_mount(void **state, const char *location)
{
    unsigned char d[ICO_VFS_SECTOR];
    Iso *iso;
    int i;

    *state = NULL;
    iso = calloc(1, sizeof(*iso));
    if (iso == NULL) {
        return -1;
    }
    iso->img = ico_disc_image_open(location);
    if (iso->img == NULL) {
        free(iso);
        return -1;
    }
    for (i = 0; i < PVD_SCAN_MAX; i++) {
        if (iso_read_raw(iso, PVD_FIRST_LSN + (uint32_t)i, 1, d) != 0) {
            break;
        }
        if (memcmp(d + 1, "CD001", 5) != 0 || d[0] == 255) {
            break;
        }
        if (d[0] == 1 && d[6] == 1) {
            const unsigned char *root = d + 156;

            if (ico_le16(d + 128) != ICO_VFS_SECTOR || root[0] < DIR_REC_MIN) {
                break;
            }
            iso->volume_sectors = ico_le32(d + 80);
            iso->root_lsn = ico_le32(root + 2);
            iso->root_size = ico_le32(root + 10);
            memcpy(iso->root_date, root + 18, 7);
            *state = iso;
            return 0;
        }
    }
    fprintf(stderr, "iso9660: %s holds no ISO9660 volume\n", location);
    ico_disc_image_close(iso->img);
    free(iso);
    return -1;
}

static void iso_unmount(void *state)
{
    Iso *iso = state;

    if (iso != NULL) {
        ico_disc_image_close(iso->img);
        free(iso);
    }
}

/* Compare a directory record's identifier (with its ";version" and, for a
   name with no extension, the trailing '.') to one normalized component. */
static int name_matches(const unsigned char *id, unsigned len, const char *comp, size_t clen)
{
    unsigned i;

    for (i = 0; i < len; i++) {
        if (id[i] == ';') {
            len = i;
            break;
        }
    }
    if (len > 0 && id[len - 1] == '.') {
        len--;
    }
    if (len != clen) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = id[i];

        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        if (c != (unsigned char)comp[i]) {
            return 0;
        }
    }
    return 1;
}

static void fill_entry(IcoVfsEntry *out, const unsigned char *rec)
{
    unsigned nlen = rec[32];

    memset(out, 0, sizeof(*out));
    out->lsn = ico_le32(rec + 2);
    out->size = ico_le32(rec + 10);
    memcpy(out->date, rec + 18, 7);
    out->is_dir = (rec[25] & 2) != 0;
    if (nlen >= sizeof(out->name)) {
        nlen = sizeof(out->name) - 1;
    }
    memcpy(out->name, rec + 33, nlen);
    out->name[nlen] = '\0';
}

/* Find one component in the directory at (lsn, size). */
static int find_in_dir(Iso *iso, uint32_t lsn, uint32_t size, const char *comp, size_t clen,
                       IcoVfsEntry *out)
{
    unsigned char sec[ICO_VFS_SECTOR];
    uint32_t nsec;
    uint32_t s;

    if (size > DIR_MAX_BYTES) {
        return -1;
    }
    nsec = ico_vfs_size_to_sectors(size);
    for (s = 0; s < nsec; s++) {
        unsigned o = 0;

        if (iso_read_raw(iso, lsn + s, 1, sec) != 0) {
            return -1;
        }
        while (o + DIR_REC_MIN <= ICO_VFS_SECTOR) {
            const unsigned char *rec = sec + o;
            unsigned rlen = rec[0];
            unsigned nlen;

            if (rlen == 0) {
                break; /* the rest of this sector is padding */
            }
            nlen = rec[32];
            if (rlen < DIR_REC_MIN || o + rlen > ICO_VFS_SECTOR || 33u + nlen > rlen) {
                break; /* malformed: give up on this sector */
            }
            /* skip "." and ".." (identifiers 0x00 and 0x01) */
            if (!(nlen == 1 && rec[33] <= 1) && name_matches(rec + 33, nlen, comp, clen)) {
                if (rec[25] & 0x80) {
                    fprintf(stderr, "iso9660: %.*s is a multi-extent file (unsupported)\n",
                            (int)clen, comp);
                    return -1;
                }
                fill_entry(out, rec);
                return 0;
            }
            o += rlen;
        }
    }
    return -1;
}

static int iso_lookup(void *state, const char *path, IcoVfsEntry *out)
{
    Iso *iso = state;
    IcoVfsEntry cur;
    const char *p = path;

    memset(&cur, 0, sizeof(cur));
    cur.lsn = iso->root_lsn;
    cur.size = iso->root_size;
    cur.is_dir = 1;
    memcpy(cur.date, iso->root_date, 7);
    while (*p != '\0') {
        const char *slash = strchr(p, '/');
        size_t clen = slash != NULL ? (size_t)(slash - p) : strlen(p);

        if (!cur.is_dir || find_in_dir(iso, cur.lsn, cur.size, p, clen, &cur) != 0) {
            return -1;
        }
        p += clen;
        if (*p == '/') {
            p++;
        }
    }
    *out = cur;
    return 0;
}

static int iso_read_sectors(void *state, uint32_t lsn, uint32_t count, void *dst)
{
    Iso *iso = state;

    if (lsn >= iso->volume_sectors || count > iso->volume_sectors - lsn) {
        return -1;
    }
    return iso_read_raw(iso, lsn, count, dst);
}

static uint32_t iso_volume_sectors(void *state)
{
    return ((Iso *)state)->volume_sectors;
}

const IcoVfsBackend ico_vfs_iso9660 = {
    "iso9660", iso_mount, iso_unmount, iso_lookup, iso_read_sectors, iso_volume_sectors,
};
