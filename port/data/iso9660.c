/*
 * port/data/iso9660.c
 *
 * The VFS's ISO9660 backend: a minimal read-only reader over an image file
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
 */
#define _FILE_OFFSET_BITS 64

#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PVD_FIRST_LSN 16
#define PVD_SCAN_MAX 64           /* descriptors examined before giving up */
#define DIR_MAX_BYTES (16u << 20) /* a sanity bound on one directory */
#define DIR_REC_MIN 34

typedef struct {
    FILE *fp;
    uint32_t volume_sectors;
    uint32_t root_lsn;
    uint32_t root_size;
    uint8_t root_date[7];
} Iso;

static uint16_t le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int seek64(FILE *fp, uint64_t off)
{
#ifdef _WIN32
    return _fseeki64(fp, (long long)off, SEEK_SET);
#else
    return fseeko(fp, (off_t)off, SEEK_SET);
#endif
}

static int iso_read_raw(Iso *iso, uint32_t lsn, uint32_t count, void *dst)
{
    size_t n = (size_t)count * ICO_VFS_SECTOR;

    if (seek64(iso->fp, ico_vfs_lsn_to_offset(lsn)) != 0) {
        return -1;
    }
    return fread(dst, 1, n, iso->fp) == n ? 0 : -1;
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
    iso->fp = fopen(location, "rb");
    if (iso->fp == NULL) {
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

            if (le16(d + 128) != ICO_VFS_SECTOR || root[0] < DIR_REC_MIN) {
                break;
            }
            iso->volume_sectors = le32(d + 80);
            iso->root_lsn = le32(root + 2);
            iso->root_size = le32(root + 10);
            memcpy(iso->root_date, root + 18, 7);
            *state = iso;
            return 0;
        }
    }
    fclose(iso->fp);
    free(iso);
    return -1;
}

static void iso_unmount(void *state)
{
    Iso *iso = state;

    if (iso != NULL) {
        fclose(iso->fp);
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
    out->lsn = le32(rec + 2);
    out->size = le32(rec + 10);
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
