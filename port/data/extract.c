/*
 * port/data/extract.c
 *
 * The first-run extractor (extract.h): the user's disc image to its archive
 * (archive.h). It mounts the image with the ISO9660 backend, reads which disc
 * it is from SYSTEM.CNF's boot file (ico_discs), walks the root and DFDATAS
 * directories, hashes the whole image, then streams each chosen file into a
 * ZIP written by miniz with no compression, hashing the boot ELF and taking
 * the CRC-32 of DATA.DF's directory and members on the way, and writes
 * meta.json last, once the image has been accepted by that disc's rules.
 */
#define _FILE_OFFSET_BITS 64

#include "extract.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "archive.h"
#include "host_config.h"
#include "host_fs.h"
#include "miniz.h"
#include "../include/ico_endian.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define DF_PATH "DFDATAS/DATA.DF"
#define CNF_PATH "SYSTEM.CNF"
#define STORE_DIR "DFDATAS" /* stored whole, recursively */
#define ITEMS_MAX 1024
#define DEPTH_MAX 8
#define DIR_REC_MIN 34
#define DIR_BYTES_MAX (1u << 20)
#define HASH_CHUNK (1u << 20)
#define DF_MEMBERS_MAX 4096u /* a sanity bound; the PAL disc has 193, the Japanese 183 */

/* --- helpers ----------------------------------------------------------------- */

static void say(char *why, size_t n, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

static void say(char *why, size_t n, const char *fmt, ...)
{
    va_list ap;

    if (why == NULL || n == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(why, n, fmt, ap);
    va_end(ap);
}

static double now_s(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec ts;

    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

/* Flushes fp, syncs it to the disk and closes it, always: 0, or the errno
   of the first step that failed. The archive is on the disk before it is
   moved over the old one, so a power cut cannot leave a complete-looking
   archive whose data never reached it. */
static int close_synced(FILE *fp)
{
    int err = 0;

    if (ico_fsync(fp) != 0) {
        err = errno ? errno : EIO;
    }
    if (fclose(fp) != 0 && err == 0) {
        err = errno ? errno : EIO;
    }
    return err;
}

static void sha1_hex(IcoSha1 *s, char hex[41])
{
    unsigned char d[20];
    int i;

    ico_sha1_final(s, d);
    for (i = 0; i < 20; i++) {
        snprintf(hex + 2 * i, 3, "%02x", d[i]);
    }
}

/* A growing text buffer for meta.json. */
typedef struct Buf {
    char *p;
    size_t n;
    size_t cap;
    int bad;
} Buf;

static void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int need;

    if (b->bad) {
        return;
    }
    va_start(ap, fmt);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        b->bad = 1;
        return;
    }
    if (b->n + (size_t)need + 1 > b->cap) {
        size_t cap = (b->cap ? b->cap * 2 : 4096) + (size_t)need;
        char *p = realloc(b->p, cap);

        if (p == NULL) {
            b->bad = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(b->p + b->n, (size_t)need + 1, fmt, ap);
    va_end(ap);
    b->n += (size_t)need;
}

/* a JSON string; disc names are ASCII, anything else is escaped */
static void buf_jstr(Buf *b, const char *s)
{
    buf_printf(b, "\"");
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\') {
            buf_printf(b, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7F) {
            buf_printf(b, "\\u%04x", c);
        } else {
            buf_printf(b, "%c", c);
        }
    }
    buf_printf(b, "\"");
}

/* --- DATA.DF's directory and member CRCs ---------------------------------------- */

typedef struct DfState {
    int valid;     /* the directory parsed */
    uint32_t size; /* DATA.DF's size */
    uint32_t count;
    uint32_t dir_len; /* 4 + 40 * count */
    uint32_t dir_crc;
    IcoDfManifestEntry *m; /* offset and size from the image's directory, crc running */
    uint64_t next;         /* the next byte expected (the CRCs need order) */
    int out_of_order;
} DfState;

static int df_begin(DfState *df, IcoVfs *iso, char *why, size_t whysize)
{
    IcoVfsFile f;
    unsigned char hdr[4];
    unsigned char *dir;
    uint32_t i;

    memset(df, 0, sizeof(*df));
    if (ico_vfs_open(iso, DF_PATH, &f) != 0) {
        say(why, whysize, "the image has no " DF_PATH);
        return -1;
    }
    df->size = f.entry.size;
    if (ico_vfs_read(&f, 0, hdr, 4) != 4) {
        say(why, whysize, DF_PATH ": cannot read its directory");
        return -1;
    }
    df->count = ico_le32(hdr);
    if (df->count == 0 || df->count > DF_MEMBERS_MAX || 4u + 40u * (uint64_t)df->count > df->size) {
        say(why, whysize, DF_PATH ": not a member directory (count %u)", (unsigned)df->count);
        return -1;
    }
    df->dir_len = 4u + 40u * df->count;
    dir = malloc(df->dir_len);
    df->m = calloc(df->count, sizeof(*df->m));
    if (dir == NULL || df->m == NULL || ico_vfs_read(&f, 0, dir, df->dir_len) != df->dir_len) {
        free(dir);
        say(why, whysize, DF_PATH ": cannot read its directory");
        return -1;
    }
    for (i = 0; i < df->count; i++) {
        df->m[i].offset = ico_le32(dir + 4 + 40 * i + 32);
        df->m[i].size = ico_le32(dir + 4 + 40 * i + 36);
        if ((uint64_t)df->m[i].offset + df->m[i].size > df->size) {
            free(dir);
            say(why, whysize, DF_PATH ": member %u lies outside the file", (unsigned)i);
            return -1;
        }
        df->m[i].crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    }
    free(dir);
    df->dir_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, NULL, 0);
    df->valid = 1;
    return 0;
}

static void df_update(DfState *df, uint64_t o, const unsigned char *p, size_t n)
{
    uint64_t end = o + n;
    uint32_t i;

    if (!df->valid) {
        return;
    }
    if (o != df->next) {
        df->out_of_order = 1;
        return;
    }
    df->next = end;
    if (o < df->dir_len) {
        uint64_t e = end < df->dir_len ? end : df->dir_len;

        df->dir_crc = (uint32_t)mz_crc32(df->dir_crc, p, (size_t)(e - o));
    }
    for (i = 0; i < df->count; i++) {
        uint64_t a = df->m[i].offset;
        uint64_t b = a + df->m[i].size;

        if (a < o) {
            a = o;
        }
        if (b > end) {
            b = end;
        }
        if (a < b) {
            df->m[i].crc = (uint32_t)mz_crc32(df->m[i].crc, p + (a - o), (size_t)(b - a));
        }
    }
}

/* Whether the image's DATA.DF is the disc's manifest's. */
static int df_matches(const DfState *df, const IcoDiscInfo *disc, char *why, size_t whysize)
{
    uint32_t i;

    if (!df->valid) {
        say(why, whysize, "DATA.DF has no member directory");
        return 0;
    }
    if (df->out_of_order || df->next != df->size) {
        say(why, whysize, "DATA.DF was not read through in order");
        return 0;
    }
    if (disc == NULL || disc->df_count == 0) {
        say(why, whysize, "the disc is not one this build has a DATA.DF manifest for");
        return 0;
    }
    if (df->size != disc->df_size) {
        say(why, whysize, "DATA.DF is %u bytes, the %s disc's is %u", (unsigned)df->size, disc->id,
            (unsigned)disc->df_size);
        return 0;
    }
    if (df->count != disc->df_count) {
        say(why, whysize, "DATA.DF has %u members, the %s disc's %u", (unsigned)df->count, disc->id,
            (unsigned)disc->df_count);
        return 0;
    }
    if (df->dir_crc != disc->df_dir_crc) {
        say(why, whysize, "DATA.DF's directory CRC-32 %08x, expected %08x", (unsigned)df->dir_crc,
            (unsigned)disc->df_dir_crc);
        return 0;
    }
    for (i = 0; i < df->count; i++) {
        const IcoDfManifestEntry *w = &disc->members[i];

        if (df->m[i].offset != w->offset || df->m[i].size != w->size || df->m[i].crc != w->crc) {
            say(why, whysize,
                "DATA.DF member %u: offset %u size %u CRC-32 %08x, expected %u %u %08x",
                (unsigned)i, (unsigned)df->m[i].offset, (unsigned)df->m[i].size,
                (unsigned)df->m[i].crc, (unsigned)w->offset, (unsigned)w->size, (unsigned)w->crc);
            return 0;
        }
    }
    return 1;
}

int ico_extract_print_datadf_manifest(IcoVfs *iso, const char *slug, FILE *out)
{
    char up[16], camel[16];
    size_t k;
    static unsigned char chunk[HASH_CHUNK];
    IcoVfsFile f;
    DfState df;
    char why[256];
    uint64_t o;
    uint32_t i;

    for (k = 0; slug[k] != '\0' && k + 1 < sizeof(up); k++) {
        up[k] = (slug[k] >= 'a' && slug[k] <= 'z') ? (char)(slug[k] - 'a' + 'A') : slug[k];
        camel[k] = k == 0 ? up[k] : slug[k];
    }
    up[k] = camel[k] = '\0';
    if (df_begin(&df, iso, why, sizeof(why)) != 0 || ico_vfs_open(iso, DF_PATH, &f) != 0) {
        fprintf(stderr, "manifest: %s\n", why);
        free(df.m);
        return -1;
    }
    for (o = 0; o < df.size;) {
        int64_t n = ico_vfs_read(&f, o, chunk, sizeof(chunk));

        if (n <= 0) {
            free(df.m);
            return -1;
        }
        df_update(&df, o, chunk, (size_t)n);
        o += (uint64_t)n;
    }
    fprintf(out,
            "/* BEGIN DATA.DF MANIFEST %s */\n"
            "#define DF_%s_SIZE %uu\n#define DF_%s_COUNT %uu\n#define DF_%s_DIR_CRC 0x%08xu\n"
            "static const IcoDfManifestEntry dfMembers%s[] = {\n",
            slug, up, (unsigned)df.size, up, (unsigned)df.count, up, (unsigned)df.dir_crc, camel);
    for (i = 0; i < df.count; i++) {
        fprintf(out, "    {%uu, %uu, 0x%08xu},\n", (unsigned)df.m[i].offset, (unsigned)df.m[i].size,
                (unsigned)df.m[i].crc);
    }
    fprintf(out, "};\n/* END DATA.DF MANIFEST %s */\n", slug);
    free(df.m);
    return 0;
}

/* --- the walk ----------------------------------------------------------------- */

typedef struct Item {
    char path[ICO_VFS_PATH_MAX];
    IcoVfsEntry e;
    int store;
} Item;

typedef struct Ex {
    IcoVfs *iso;
    Item *items;
    int n;
    char boot[64];           /* BOOT2's file, normalized */
    const IcoDiscInfo *disc; /* the row of that boot file, or NULL */
    char elf_path[64];       /* the boot ELF hashed: the row's, else BOOT2's file */

    IcoExtractProgressFn progress;
    void *ctx;
    uint64_t done;
    uint64_t total;
    int cancelled;

    FILE *out;
    uint64_t out_pos;
    int write_errno;

    /* the file being streamed */
    IcoVfsFile f;
    int is_elf;
    int is_df;
    uint64_t next;
    int read_err;
    IcoSha1 elf;
    int elf_seen;
    int elf_in_order;
    DfState df;
} Ex;

static int add_item(Ex *x, const char *path, int store, char *why, size_t whysize)
{
    Item *it;

    if (x->n >= ITEMS_MAX) {
        say(why, whysize, "more than %d files to extract", ITEMS_MAX);
        return -1;
    }
    it = &x->items[x->n];
    memset(it, 0, sizeof(*it));
    snprintf(it->path, sizeof(it->path), "%s", path);
    if (ico_vfs_stat(x->iso, path, &it->e) != 0) {
        say(why, whysize, "cannot look up %s on the image", path[0] ? path : "the root");
        return -1;
    }
    it->store = store && !it->e.is_dir;
    x->n++;
    return 0;
}

static int root_file_wanted(const Ex *x, const char *comp)
{
    size_t n = strlen(comp);

    /* DUMMY.TXT is the file after DATA.DF on the PAL disc (LSN 443201, where
       DATA.DF's last sector ends): kept so a read that runs over DATA.DF's
       end finds what the disc had there (the Japanese disc has none: its
       DATA.DF is the last file) */
    return strcmp(comp, CNF_PATH) == 0 ||
           (x->elf_path[0] != '\0' && strcmp(comp, x->elf_path) == 0) ||
           strcmp(comp, "DUMMY.TXT") == 0 || (x->boot[0] != '\0' && strcmp(comp, x->boot) == 0) ||
           (n > 4 && strcmp(comp + n - 4, ".IRX") == 0);
}

static int walk(Ex *x, const char *dir, const IcoVfsEntry *d, int store_all, int depth, char *why,
                size_t whysize)
{
    unsigned char *buf;
    uint32_t nsec, s;
    int r = 0;

    if (depth > DEPTH_MAX || d->size > DIR_BYTES_MAX) {
        say(why, whysize, "the directory %s is too deep or too large", dir[0] ? dir : "/");
        return -1;
    }
    nsec = ico_vfs_size_to_sectors(d->size);
    buf = malloc((size_t)nsec * ICO_VFS_SECTOR);
    if (buf == NULL || ico_vfs_read_sectors(x->iso, d->lsn, nsec, buf) != 0) {
        free(buf);
        say(why, whysize, "cannot read the directory %s", dir[0] ? dir : "/");
        return -1;
    }
    for (s = 0; s < nsec && r == 0; s++) {
        unsigned o = 0;
        const unsigned char *sec = buf + (size_t)s * ICO_VFS_SECTOR;

        while (o + DIR_REC_MIN <= ICO_VFS_SECTOR && r == 0) {
            const unsigned char *rec = sec + o;
            unsigned rlen = rec[0];
            unsigned nlen;
            char comp[40];
            char path[ICO_VFS_PATH_MAX];
            unsigned i, k = 0;
            int is_dir;

            if (rlen == 0) {
                break;
            }
            nlen = rec[32];
            if (rlen < DIR_REC_MIN || o + rlen > ICO_VFS_SECTOR || 33u + nlen > rlen) {
                break;
            }
            o += rlen;
            if (nlen == 1 && rec[33] <= 1) {
                continue; /* "." and ".." */
            }
            for (i = 0; i < nlen && k + 1 < sizeof(comp) && rec[33 + i] != ';'; i++) {
                char c = (char)rec[33 + i];

                comp[k++] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            }
            if (k > 0 && comp[k - 1] == '.') {
                k--;
            }
            comp[k] = '\0';
            if (k == 0) {
                continue;
            }
            snprintf(path, sizeof(path), "%s%s%s", dir, dir[0] ? "/" : "", comp);
            is_dir = (rec[25] & 2) != 0;
            if (is_dir && (store_all || (depth == 0 && strcmp(comp, STORE_DIR) == 0))) {
                IcoVfsEntry sub;

                r = add_item(x, path, 0, why, whysize);
                if (r == 0) {
                    sub = x->items[x->n - 1].e;
                    r = walk(x, path, &sub, 1, depth + 1, why, whysize);
                }
            } else if (!is_dir && (store_all || (depth == 0 && root_file_wanted(x, comp)))) {
                r = add_item(x, path, 1, why, whysize);
            }
        }
    }
    free(buf);
    return r;
}

/* SYSTEM.CNF's BOOT2 file, normalized ("SCES_507.60"); "" when absent. */
static void read_boot_name(IcoVfs *iso, char *boot, size_t size)
{
    char cnf[1024];
    IcoVfsFile f;
    int64_t n;

    boot[0] = '\0';
    if (ico_vfs_open(iso, CNF_PATH, &f) != 0) {
        return;
    }
    n = ico_vfs_read(&f, 0, cnf, sizeof(cnf) - 1);
    if (n <= 0) {
        return;
    }
    cnf[n] = '\0';
    (void)ico_disc_cnf_boot(cnf, boot, size);
}

const IcoDiscInfo *ico_extract_identify(const char *iso_path, char *boot, size_t size)
{
    IcoVfs *iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    char name[64];

    if (boot != NULL && size > 0) {
        boot[0] = '\0';
    }
    if (iso == NULL) {
        return NULL;
    }
    read_boot_name(iso, name, sizeof(name));
    ico_vfs_unmount(iso);
    if (boot != NULL && size > 0) {
        snprintf(boot, size, "%s", name);
    }
    return ico_disc_by_boot(name);
}

/* "SCES_507.60" -> "SCES-50760" */
static void disc_id_of(const char *boot, char *out, size_t size)
{
    size_t k = 0;

    for (; *boot != '\0' && k + 1 < size; boot++) {
        if (*boot == '_') {
            out[k++] = '-';
        } else if (*boot != '.') {
            out[k++] = *boot;
        }
    }
    out[k] = '\0';
}

/* --- the ZIP ------------------------------------------------------------------ */

static int tick(Ex *x, const char *phase, uint64_t n)
{
    x->done += n;
    if (x->progress != NULL && x->progress(x->ctx, phase, x->done, x->total) != 0) {
        x->cancelled = 1;
    }
    return x->cancelled;
}

/* A step with no byte count (done and total 0). A stop asked in the reply
   is kept for the next tick, which then ends the extraction: a reply may
   report a key press only once (Android's Back). After "finish" nothing
   reads it, so the archive is completed. */
static void note_phase(Ex *x, const char *phase)
{
    if (x->progress != NULL && x->progress(x->ctx, phase, 0, 0) != 0) {
        x->cancelled = 1;
    }
}

static size_t zip_write(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    Ex *x = opaque;
    size_t w;

    if (ofs != x->out_pos) {
        if (ico_archive_seek(x->out, ofs) != 0) {
            x->write_errno = errno ? errno : EIO;
            return 0;
        }
        x->out_pos = ofs;
    }
    w = fwrite(buf, 1, n, x->out);
    x->out_pos += w;
    if (w != n) {
        x->write_errno = errno ? errno : ENOSPC;
    }
    return w;
}

static size_t zip_read_iso(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
    Ex *x = opaque;
    int64_t got;

    if (x->cancelled) {
        return 0;
    }
    /* miniz asks for a full buffer each time and takes a short read as the
       end of the file and 0 as nothing more: store_item checks afterwards
       that the whole file went in */
    got = ico_vfs_read(&x->f, ofs, buf, n);
    if (got < 0) {
        x->read_err = 1;
        return 0;
    }
    if (got == 0) {
        return 0;
    }
    if (x->is_elf) {
        if (ofs != x->next) {
            x->elf_in_order = 0;
        }
        ico_sha1_update(&x->elf, buf, (size_t)got);
    }
    if (x->is_df) {
        df_update(&x->df, ofs, buf, (size_t)got);
    }
    x->next = ofs + (uint64_t)got;
    if (tick(x, "extract", (uint64_t)got)) {
        return 0;
    }
    return (size_t)got;
}

static int store_item(Ex *x, mz_zip_archive *zip, const Item *it, int *tail_out, char *why,
                      size_t whysize)
{
    char name[ICO_VFS_PATH_MAX + 8];
    unsigned char sec[ICO_VFS_SECTOR];
    uint32_t used = it->e.size % ICO_VFS_SECTOR;
    uint32_t i;

    *tail_out = 0;
    memset(&x->f, 0, sizeof(x->f));
    x->f.vfs = x->iso;
    x->f.entry = it->e;
    x->is_elf = x->elf_path[0] != '\0' && strcmp(it->path, x->elf_path) == 0;
    x->is_df = strcmp(it->path, DF_PATH) == 0;
    x->next = 0;
    x->read_err = 0;
    if (x->is_elf) {
        ico_sha1_init(&x->elf);
        x->elf_seen = 1;
        x->elf_in_order = 1;
    }
    snprintf(name, sizeof(name), "disc/%s", it->path);
    if (!mz_zip_writer_add_read_buf_callback(zip, name, zip_read_iso, x, it->e.size, NULL, NULL, 0,
                                             MZ_NO_COMPRESSION, NULL, 0, NULL, 0) ||
        x->cancelled || x->read_err || x->next != it->e.size) {
        if (x->cancelled) {
            say(why, whysize, "cancelled");
        } else if (x->read_err || x->next != it->e.size) {
            say(why, whysize, "cannot read %s from the image", it->path);
        } else if (x->write_errno != 0) {
            say(why, whysize, "cannot write the archive: %s", strerror(x->write_errno));
        } else {
            say(why, whysize, "cannot add %s to the archive: %s", it->path,
                mz_zip_get_error_string(mz_zip_get_last_error(zip)));
        }
        return -1;
    }
    if (used == 0) {
        return 0;
    }
    /* the bytes after the file's end in its last sector: kept when any is
       nonzero, so sector reads return what the disc held */
    if (ico_vfs_read_sectors(x->iso, it->e.lsn + it->e.size / ICO_VFS_SECTOR, 1, sec) != 0) {
        say(why, whysize, "cannot read the last sector of %s", it->path);
        return -1;
    }
    for (i = used; i < ICO_VFS_SECTOR && sec[i] == 0; i++) {}
    if (i == ICO_VFS_SECTOR) {
        return 0;
    }
    snprintf(name, sizeof(name), "tail/%s", it->path);
    if (!mz_zip_writer_add_mem_ex(zip, name, sec + used, ICO_VFS_SECTOR - used, NULL, 0,
                                  MZ_NO_COMPRESSION, 0, 0)) {
        say(why, whysize, "cannot add the tail of %s: %s", it->path,
            x->write_errno ? strerror(x->write_errno)
                           : mz_zip_get_error_string(mz_zip_get_last_error(zip)));
        return -1;
    }
    *tail_out = 1;
    return 0;
}

static void write_meta(Buf *b, const Ex *x, const IcoExtractResult *res, const int *tails)
{
    int i, k;

    buf_printf(b, "{\n  \"format\": ");
    buf_jstr(b, ICO_ARCHIVE_FORMAT);
    buf_printf(b, ",\n  \"version\": %d,\n  \"extractor\": ", ICO_ARCHIVE_VERSION);
    buf_jstr(b, ICO_ARCHIVE_EXTRACTOR);
    buf_printf(b, ",\n  \"disc_id\": ");
    buf_jstr(b, res->disc_id);
    buf_printf(b, ",\n  \"source\": {\n    \"sha1\": ");
    buf_jstr(b, res->iso_sha1);
    buf_printf(b,
               ",\n    \"size\": %llu,\n    \"accepted_by\": ", (unsigned long long)res->iso_size);
    buf_jstr(b, res->rule);
    buf_printf(b, ",\n    \"elf_sha1\": ");
    buf_jstr(b, res->elf_sha1);
    buf_printf(b, ",\n    \"datadf_manifest\": %s\n  },\n", res->datadf_ok ? "true" : "false");
    buf_printf(b, "  \"volume_sectors\": %u,\n  \"entries\": [\n",
               (unsigned)ico_vfs_volume_sectors(x->iso));
    for (i = 0; i < x->n; i++) {
        const Item *it = &x->items[i];

        buf_printf(b, "    {\"path\": ");
        buf_jstr(b, it->path);
        buf_printf(b, ", \"name\": ");
        buf_jstr(b, it->e.name);
        buf_printf(b, ", \"lsn\": %u, \"size\": %u", (unsigned)it->e.lsn, (unsigned)it->e.size);
        if (it->e.is_dir) {
            buf_printf(b, ", \"dir\": true");
        }
        buf_printf(b, ", \"date\": [");
        for (k = 0; k < 7; k++) {
            buf_printf(b, "%s%u", k ? ", " : "", (unsigned)it->e.date[k]);
        }
        buf_printf(b, "]");
        if (it->store) {
            buf_printf(b, ", \"data\": \"disc/%s\"", it->path);
            if (tails[i]) {
                buf_printf(b, ", \"tail\": \"tail/%s\"", it->path);
            }
        }
        buf_printf(b, "}%s\n", i + 1 < x->n ? "," : "");
    }
    buf_printf(b, "  ]\n}\n");
}

/* The image's SHA-1 over its logical bytes (an .iso as it is, the ISO a
   .chd holds), with progress when `x` is given. */
static int hash_logical(Ex *x, const char *iso_path, IcoSha1 *s, char *why, size_t whysize)
{
    IcoDiscImage *img = ico_disc_image_open(iso_path);
    unsigned char *buf;
    uint64_t at = 0, total;
    int err = 0;

    if (img == NULL) {
        say(why, whysize, "cannot read the disc image %s", iso_path);
        return -1;
    }
    buf = malloc(HASH_CHUNK);
    if (buf == NULL) {
        ico_disc_image_close(img);
        say(why, whysize, "out of memory");
        return -1;
    }
    total = ico_disc_image_bytes(img);
    ico_sha1_init(s);
    while (at < total) {
        size_t got = total - at < HASH_CHUNK ? (size_t)(total - at) : HASH_CHUNK;

        if (ico_disc_image_read(img, at, buf, got) != 0) {
            err = 1;
            break;
        }
        ico_sha1_update(s, buf, got);
        at += got;
        if (x != NULL && tick(x, "hash", got)) {
            break;
        }
    }
    ico_disc_image_close(img);
    free(buf);
    if (x != NULL && x->cancelled) {
        say(why, whysize, "cancelled");
        return -1;
    }
    if (err) {
        say(why, whysize, "a read error in %s", iso_path);
        return -1;
    }
    return 0;
}

static int hash_image(Ex *x, const char *iso_path, IcoExtractResult *res, char *why, size_t whysize)
{
    IcoSha1 s;

    if (hash_logical(x, iso_path, &s, why, whysize) != 0) {
        return -1;
    }
    res->iso_size = s.length;
    sha1_hex(&s, res->iso_sha1);
    return 0;
}

int ico_extract_image_sha1(const char *iso_path, char hex[41], uint64_t *bytes, char *why,
                           size_t whysize)
{
    IcoSha1 s;

    if (hash_logical(NULL, iso_path, &s, why, whysize) != 0) {
        return -1;
    }
    *bytes = s.length;
    sha1_hex(&s, hex);
    return 0;
}

int ico_extract_archive(const char *iso_path, const char *out_path, unsigned flags,
                        IcoExtractProgressFn progress, void *ctx, IcoExtractResult *res, char *why,
                        size_t whysize)
{
    char tmp[ICO_PATH_MAX + 8];
    mz_zip_archive zip;
    int zip_open = 0;
    IcoVfsEntry root;
    IcoArchiveInfo info;
    Ex x;
    Buf meta;
    int *tails = NULL;
    int iso_ok, elf_ok;
    int i, ok = 0;
    double t0, t1;
    char dfwhy[256] = "";

    memset(res, 0, sizeof(*res));
    memset(&x, 0, sizeof(x));
    memset(&meta, 0, sizeof(meta));
    memset(&zip, 0, sizeof(zip));
    /* a cut-off name would not end in ".tmp": the failure path below
       removes `tmp`, which could then be another file */
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", out_path) >= (int)sizeof(tmp)) {
        say(why, whysize, "the archive path is too long: %s", out_path);
        return -1;
    }
    x.progress = progress;
    x.ctx = ctx;

    /* opening the image, reading its directory and the first megabyte of the
       hash take a while before any byte count exists: say so, with no size */
    note_phase(&x, "open");
    x.iso = ico_vfs_mount(&ico_vfs_iso9660, iso_path);
    if (x.iso == NULL) {
        res->unreadable = 1;
        say(why, whysize, ICO_UNREADABLE_DISC_TEXT, iso_path, ico_disc_list_text());
        return -1;
    }
    x.items = calloc(ITEMS_MAX, sizeof(*x.items));
    tails = calloc(ITEMS_MAX, sizeof(*tails));
    if (x.items == NULL || tails == NULL) {
        say(why, whysize, "out of memory");
        goto done;
    }
    read_boot_name(x.iso, x.boot, sizeof(x.boot));
    disc_id_of(x.boot, res->disc_id, sizeof(res->disc_id));
    /* the row by the boot file first; its rules below */
    x.disc = ico_disc_by_boot(x.boot);
    snprintf(x.elf_path, sizeof(x.elf_path), "%s", x.disc != NULL ? x.disc->boot_elf : x.boot);
    if (add_item(&x, "", 0, why, whysize) != 0) {
        goto done;
    }
    root = x.items[0].e;
    if (walk(&x, "", &root, 0, 0, why, whysize) != 0) {
        goto done;
    }
    for (i = 0; i < x.n; i++) {
        if (x.items[i].store) {
            res->files++;
            res->bytes += x.items[i].e.size;
        }
    }
    if (res->files == 0) {
        res->wrong_disc = 1; /* a disc, but not this game's */
        say(why, whysize, "%s holds none of the game's files", iso_path);
        goto done;
    }
    if (df_begin(&x.df, x.iso, dfwhy, sizeof(dfwhy)) != 0) {
        x.df.valid = 0;
    }
    {
        /* the bytes the hash reads: the ISO's, whatever the container */
        IcoDiscImage *img = ico_disc_image_open(iso_path);

        x.total = (img != NULL ? ico_disc_image_bytes(img) : 0) + res->bytes;
        ico_disc_image_close(img);
    }

    /* 1. the image's SHA-1 */
    t0 = now_s();
    if (hash_image(&x, iso_path, res, why, whysize) != 0) {
        res->cancelled = x.cancelled;
        res->unreadable = !x.cancelled; /* cut short or damaged */
        goto done;
    }
    t1 = now_s();
    res->hash_seconds = t1 - t0;
    iso_ok = x.disc != NULL && strcmp(res->iso_sha1, x.disc->iso_sha1) == 0;

    /* 2. the files */
    x.out = ico_archive_fopen(tmp, "wb");
    if (x.out == NULL) {
        say(why, whysize, "cannot create %s: %s", tmp, strerror(errno));
        goto done;
    }
    zip.m_pWrite = zip_write;
    zip.m_pIO_opaque = &x;
    if (!mz_zip_writer_init_v2(&zip, 0, 0)) {
        say(why, whysize, "cannot start the archive: %s",
            mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        goto done;
    }
    zip_open = 1;
    for (i = 0; i < x.n; i++) {
        if (x.items[i].store && store_item(&x, &zip, &x.items[i], &tails[i], why, whysize) != 0) {
            res->cancelled = x.cancelled;
            goto done;
        }
    }
    res->extract_seconds = now_s() - t1;

    /* 3. the rule */
    if (x.elf_seen && x.elf_in_order) {
        sha1_hex(&x.elf, res->elf_sha1);
    }
    elf_ok = x.disc != NULL && strcmp(res->elf_sha1, x.disc->elf_sha1) == 0;
    res->datadf_ok = df_matches(&x.df, x.disc, dfwhy[0] ? NULL : dfwhy, sizeof(dfwhy));
    snprintf(res->datadf_why, sizeof(res->datadf_why), "%s", res->datadf_ok ? "" : dfwhy);
    if (iso_ok) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_ISO_SHA1);
    } else if (elf_ok && res->datadf_ok) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_ELF_DATADF);
    } else if (flags & ICO_EXTRACT_NO_VERIFY) {
        snprintf(res->rule, sizeof(res->rule), "%s", ICO_RULE_UNVERIFIED);
    } else {
        res->wrong_disc = 1;
        /* the box's text, then what the log needs to tell the cases apart */
        if (x.disc == NULL) {
            say(why, whysize,
                ICO_WRONG_DISC_TEXT
                "\n"
                "Its boot file is %s, which is none of the supported discs' (%s); "
                "image SHA-1 %s.",
                iso_path, ico_disc_list_text(), x.boot[0] ? x.boot : "(absent)",
                ico_disc_boots_text(), res->iso_sha1);
        } else {
            say(why, whysize,
                ICO_WRONG_DISC_TEXT "\n"
                                    "Its boot file %s is %s's, but the image is not an "
                                    "unmodified copy of it.\n"
                                    "Image SHA-1 %s (expected %s);\n"
                                    "%s SHA-1 %s (expected %s);\n"
                                    "DATA.DF: %s.",
                iso_path, ico_disc_list_text(), x.disc->boot_elf, x.disc->id, res->iso_sha1,
                x.disc->iso_sha1, x.disc->boot_elf, res->elf_sha1[0] ? res->elf_sha1 : "(absent)",
                x.disc->elf_sha1, res->datadf_ok ? "matches" : res->datadf_why);
        }
        goto done;
    }

    /* 4. meta.json, the directory, and the move into place (a few seconds
       of writing to storage with no byte count) */
    note_phase(&x, "finish");
    write_meta(&meta, &x, res, tails);
    if (meta.bad) {
        say(why, whysize, "out of memory");
        goto done;
    }
    if (!mz_zip_writer_add_mem_ex(&zip, ICO_ARCHIVE_META, meta.p, meta.n, NULL, 0,
                                  MZ_NO_COMPRESSION, 0, 0) ||
        !mz_zip_writer_finalize_archive(&zip)) {
        say(why, whysize, "cannot finish the archive: %s",
            x.write_errno ? strerror(x.write_errno)
                          : mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        goto done;
    }
    mz_zip_writer_end(&zip);
    zip_open = 0;
    {
        int err = close_synced(x.out);

        x.out = NULL;
        if (err != 0) {
            say(why, whysize, "cannot write %s: %s", tmp, strerror(err));
            goto done;
        }
    }
    if (ico_archive_read_info(tmp, &info, why, whysize) != 0) {
        goto done;
    }
    res->archive_bytes = info.file_bytes;
    if (ico_archive_replace(tmp, out_path) != 0) {
        say(why, whysize, "cannot move %s to %s: %s", tmp, out_path, strerror(errno));
        goto done;
    }
    ok = 1;

done:
    if (zip_open) {
        mz_zip_writer_end(&zip);
    }
    if (x.out != NULL) {
        fclose(x.out);
    }
    if (!ok) {
        ico_archive_remove(tmp);
    }
    ico_vfs_unmount(x.iso);
    free(x.items);
    free(x.df.m);
    free(tails);
    free(meta.p);
    return ok ? 0 : -1;
}
