/*
 * port/data/archive.c
 *
 * The VFS backend over `ico.o2r` (archive.h). At mount, miniz reads the ZIP's central directory over this
 * file's own read callback, meta.json is parsed, and every stored entry's
 * data offset is found from its local header; from then on sector reads are
 * plain fseek/fread on the archive file.
 */
#define _FILE_OFFSET_BITS 64

#include "archive.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"

#include "host_fs.h"

/* --- host files ---------------------------------------------------------- */

/* UTF-8 paths through port/platform/host_fs.h's wide helpers on Windows */
FILE *ico_archive_fopen(const char *path, const char *mode)
{
    return ico_fopen(path, mode);
}

int ico_archive_seek(FILE *fp, uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(fp, (long long)offset, SEEK_SET);
#else
    return fseeko(fp, (off_t)offset, SEEK_SET);
#endif
}

int64_t ico_archive_tell(FILE *fp)
{
#ifdef _WIN32
    return (int64_t)_ftelli64(fp);
#else
    return (int64_t)ftello(fp);
#endif
}

int ico_archive_remove(const char *path)
{
    return ico_remove(path);
}

int ico_archive_replace(const char *from, const char *to)
{
    return ico_rename_replace(from, to);
}

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

/* --- a small JSON reader (RFC 8259), enough for meta.json ------------------ */

enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ };

typedef struct JNode {
    int type;
    const char *key; /* inside an object */
    const char *str; /* J_STR */
    uint64_t u;      /* J_NUM: a non-negative integer */
    int is_int;      /* J_NUM: u holds the value exactly */
    int child;       /* J_ARR, J_OBJ: first member, -1 if none */
    int next;        /* next member of the parent, -1 */
} JNode;

typedef struct JParse {
    char *p;
    JNode *n;
    int count;
    int cap;
    int bad;
} JParse;

#define J_DEPTH_MAX 16

static int jnew(JParse *j, int type)
{
    if (j->count == j->cap) {
        int cap = j->cap ? j->cap * 2 : 64;
        JNode *n = realloc(j->n, (size_t)cap * sizeof(*n));

        if (n == NULL) {
            j->bad = 1;
            return -1;
        }
        j->n = n;
        j->cap = cap;
    }
    memset(&j->n[j->count], 0, sizeof(JNode));
    j->n[j->count].type = type;
    j->n[j->count].child = -1;
    j->n[j->count].next = -1;
    return j->count++;
}

static void jws(JParse *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n') {
        j->p++;
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* At '"': unescapes the string in place and returns it, NUL-terminated. */
static const char *jstring(JParse *j)
{
    char *r = j->p + 1;
    char *w = r;
    const char *s = r;

    for (;;) {
        unsigned char c = (unsigned char)*r;

        if (c == '\0' || c < 0x20) {
            j->bad = 1;
            return NULL;
        }
        if (c == '"') {
            break;
        }
        if (c != '\\') {
            *w++ = *r++;
            continue;
        }
        r++;
        switch (*r) {
        case '"':
        case '\\':
        case '/':
            *w++ = *r++;
            break;
        case 'b':
            *w++ = '\b';
            r++;
            break;
        case 'f':
            *w++ = '\f';
            r++;
            break;
        case 'n':
            *w++ = '\n';
            r++;
            break;
        case 'r':
            *w++ = '\r';
            r++;
            break;
        case 't':
            *w++ = '\t';
            r++;
            break;
        case 'u': {
            unsigned v = 0;
            int i;

            for (i = 1; i <= 4; i++) {
                int h = hexval(r[i]);

                if (h < 0) {
                    j->bad = 1;
                    return NULL;
                }
                v = v << 4 | (unsigned)h;
            }
            r += 5;
            /* UTF-8; a surrogate (outside the BMP) becomes '?' */
            if (v < 0x80) {
                *w++ = (char)v;
            } else if (v < 0x800) {
                *w++ = (char)(0xC0 | v >> 6);
                *w++ = (char)(0x80 | (v & 0x3F));
            } else if (v >= 0xD800 && v < 0xE000) {
                *w++ = '?';
            } else {
                *w++ = (char)(0xE0 | v >> 12);
                *w++ = (char)(0x80 | (v >> 6 & 0x3F));
                *w++ = (char)(0x80 | (v & 0x3F));
            }
            break;
        }
        default:
            j->bad = 1;
            return NULL;
        }
    }
    j->p = r + 1;
    *w = '\0';
    return s;
}

static int jvalue(JParse *j, int depth)
{
    int idx;

    jws(j);
    if (depth > J_DEPTH_MAX) {
        j->bad = 1;
        return -1;
    }
    switch (*j->p) {
    case '{':
    case '[': {
        int obj = *j->p == '{';
        char close = obj ? '}' : ']';
        int last = -1;

        idx = jnew(j, obj ? J_OBJ : J_ARR);
        if (idx < 0) {
            return -1;
        }
        j->p++;
        jws(j);
        if (*j->p == close) {
            j->p++;
            return idx;
        }
        for (;;) {
            const char *key = NULL;
            int v;

            jws(j);
            if (obj) {
                if (*j->p != '"' || (key = jstring(j)) == NULL) {
                    j->bad = 1;
                    return -1;
                }
                jws(j);
                if (*j->p != ':') {
                    j->bad = 1;
                    return -1;
                }
                j->p++;
            }
            v = jvalue(j, depth + 1);
            if (v < 0) {
                return -1;
            }
            j->n[v].key = key;
            if (last < 0) {
                j->n[idx].child = v;
            } else {
                j->n[last].next = v;
            }
            last = v;
            jws(j);
            if (*j->p == ',') {
                j->p++;
                continue;
            }
            if (*j->p == close) {
                j->p++;
                return idx;
            }
            j->bad = 1;
            return -1;
        }
    }
    case '"': {
        const char *s = jstring(j);

        if (s == NULL) {
            return -1;
        }
        idx = jnew(j, J_STR);
        if (idx >= 0) {
            j->n[idx].str = s;
        }
        return idx;
    }
    case 't':
        if (strncmp(j->p, "true", 4) == 0) {
            j->p += 4;
            return jnew(j, J_TRUE);
        }
        break;
    case 'f':
        if (strncmp(j->p, "false", 5) == 0) {
            j->p += 5;
            return jnew(j, J_FALSE);
        }
        break;
    case 'n':
        if (strncmp(j->p, "null", 4) == 0) {
            j->p += 4;
            return jnew(j, J_NULL);
        }
        break;
    default: {
        char *start = j->p;
        uint64_t u = 0;
        int digits = 0;
        int exact = 1;

        if (*j->p == '-') {
            exact = 0;
            j->p++;
        }
        while (*j->p >= '0' && *j->p <= '9') {
            unsigned d = (unsigned)(*j->p - '0');

            if (u > (UINT64_MAX - d) / 10) {
                exact = 0;
            }
            u = u * 10 + d;
            digits++;
            j->p++;
        }
        if (*j->p == '.' || *j->p == 'e' || *j->p == 'E') {
            char *end;

            exact = 0;
            (void)strtod(start, &end);
            j->p = end;
        }
        if (digits == 0) {
            break;
        }
        idx = jnew(j, J_NUM);
        if (idx >= 0) {
            j->n[idx].u = u;
            j->n[idx].is_int = exact;
        }
        return idx;
    }
    }
    j->bad = 1;
    return -1;
}

static int jget(const JParse *j, int obj, const char *key)
{
    int c;

    if (obj < 0 || j->n[obj].type != J_OBJ) {
        return -1;
    }
    for (c = j->n[obj].child; c >= 0; c = j->n[c].next) {
        if (strcmp(j->n[c].key, key) == 0) {
            return c;
        }
    }
    return -1;
}

static const char *jget_str(const JParse *j, int obj, const char *key)
{
    int v = jget(j, obj, key);

    return v >= 0 && j->n[v].type == J_STR ? j->n[v].str : NULL;
}

static int jget_u64(const JParse *j, int obj, const char *key, uint64_t *out)
{
    int v = jget(j, obj, key);

    if (v < 0 || j->n[v].type != J_NUM || !j->n[v].is_int) {
        return -1;
    }
    *out = j->n[v].u;
    return 0;
}

static int jget_bool(const JParse *j, int obj, const char *key)
{
    int v = jget(j, obj, key);

    return v >= 0 && j->n[v].type == J_TRUE;
}

/* --- the archive ----------------------------------------------------------- */

typedef struct AEntry {
    char path[ICO_VFS_PATH_MAX]; /* "DIR/FILE", upper case; "" is the root */
    IcoVfsEntry e;
    int stored;        /* the file's bytes are in the archive */
    uint64_t data_ofs; /* where they start in the archive file */
    uint64_t tail_ofs; /* a tail/ entry's bytes, when tail_len > 0 */
    uint32_t tail_len;
    uint32_t nsec; /* sectors the file spans on the disc */
} AEntry;

typedef struct Arch {
    FILE *fp;
    AEntry *ent;
    uint32_t count;
    IcoArchiveInfo info;
} Arch;

static size_t zip_read(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
    FILE *fp = opaque;

    if (ico_archive_seek(fp, ofs) != 0) {
        return 0;
    }
    return fread(buf, 1, n, fp);
}

static void copy_str(char *out, size_t size, const char *s)
{
    snprintf(out, size, "%s", s != NULL ? s : "");
}

static int is_hex40(const char *s)
{
    int i;

    for (i = 0; i < 40; i++) {
        if (hexval(s[i]) < 0) {
            return 0;
        }
    }
    return s[40] == '\0';
}

/* The offset of an entry's data: its local header, then name and extra. */
static int data_offset(Arch *a, const mz_zip_archive_file_stat *st, uint64_t *out)
{
    unsigned char h[30];

    if (ico_archive_seek(a->fp, st->m_local_header_ofs) != 0 || fread(h, 1, 30, a->fp) != 30) {
        return -1;
    }
    if (h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4) {
        return -1;
    }
    *out = st->m_local_header_ofs + 30u + (uint64_t)(h[26] | h[27] << 8) +
           (uint64_t)(h[28] | h[29] << 8);
    return 0;
}

/* A stored, uncompressed entry of exactly `size` bytes inside the file. */
static int stored_entry(Arch *a, mz_zip_archive *zip, const char *name, uint64_t size,
                        uint64_t *ofs, char *why, size_t whysize)
{
    mz_zip_archive_file_stat st;
    int idx = mz_zip_reader_locate_file(zip, name, NULL, 0);

    if (idx < 0 || !mz_zip_reader_file_stat(zip, (mz_uint)idx, &st)) {
        say(why, whysize, "the archive has no entry %s", name);
        return -1;
    }
    if (st.m_method != 0 || st.m_is_encrypted || st.m_comp_size != st.m_uncomp_size) {
        say(why, whysize, "%s is compressed or encrypted (method %u)", name, (unsigned)st.m_method);
        return -1;
    }
    if (st.m_uncomp_size != size) {
        say(why, whysize, "%s holds %llu bytes, meta.json says %llu", name,
            (unsigned long long)st.m_uncomp_size, (unsigned long long)size);
        return -1;
    }
    if (data_offset(a, &st, ofs) != 0 || *ofs + size > a->info.file_bytes) {
        say(why, whysize, "%s: bad local header or data past the end of the archive", name);
        return -1;
    }
    return 0;
}

static void arch_free(Arch *a)
{
    if (a != NULL) {
        if (a->fp != NULL) {
            fclose(a->fp);
        }
        free(a->ent);
        free(a);
    }
}

static int parse_entry(Arch *a, mz_zip_archive *zip, const JParse *j, int node, AEntry *ae,
                       char *why, size_t whysize)
{
    const char *path = jget_str(j, node, "path");
    const char *name = jget_str(j, node, "name");
    const char *data = jget_str(j, node, "data");
    const char *tail = jget_str(j, node, "tail");
    int date = jget(j, node, "date");
    uint64_t lsn, size;
    int i;

    memset(ae, 0, sizeof(*ae));
    if (path == NULL || name == NULL || jget_u64(j, node, "lsn", &lsn) != 0 ||
        jget_u64(j, node, "size", &size) != 0 || lsn > UINT32_MAX || size > UINT32_MAX ||
        ico_vfs_normalize(path, ae->path, sizeof(ae->path)) != 0) {
        say(why, whysize, "meta.json: a malformed entry (%s)", path ? path : "no path");
        return -1;
    }
    ae->e.lsn = (uint32_t)lsn;
    ae->e.size = (uint32_t)size;
    ae->e.is_dir = (uint8_t)jget_bool(j, node, "dir");
    copy_str(ae->e.name, sizeof(ae->e.name), name);
    if (date >= 0 && j->n[date].type == J_ARR) {
        int c = j->n[date].child;

        for (i = 0; i < 7 && c >= 0; i++, c = j->n[c].next) {
            ae->e.date[i] = (uint8_t)j->n[c].u;
        }
    }
    ae->nsec = ico_vfs_size_to_sectors(ae->e.size);
    if (ae->e.is_dir || data == NULL) {
        return 0;
    }
    if ((uint64_t)ae->e.lsn + ae->nsec > a->info.volume_sectors) {
        say(why, whysize, "meta.json: %s lies past the end of the volume", path);
        return -1;
    }
    if (stored_entry(a, zip, data, size, &ae->data_ofs, why, whysize) != 0) {
        return -1;
    }
    if (tail != NULL) {
        ae->tail_len = (uint32_t)((uint64_t)ae->nsec * ICO_VFS_SECTOR - size);
        if (ae->tail_len == 0 ||
            stored_entry(a, zip, tail, ae->tail_len, &ae->tail_ofs, why, whysize) != 0) {
            if (ae->tail_len == 0) {
                say(why, whysize, "meta.json: %s has a tail but ends on a sector", path);
            }
            return -1;
        }
    }
    ae->stored = 1;
    a->info.stored++;
    return 0;
}

static int arch_open(const char *path, Arch **out, char *why, size_t whysize)
{
    mz_zip_archive zip;
    JParse j;
    Arch *a;
    char *text = NULL;
    size_t textlen = 0;
    int idx, root, src, ents, c;
    int64_t end;
    uint64_t v;
    int ok = 0;

    *out = NULL;
    memset(&j, 0, sizeof(j));
    a = calloc(1, sizeof(*a));
    if (a == NULL) {
        say(why, whysize, "out of memory");
        return -1;
    }
    a->fp = ico_archive_fopen(path, "rb");
    if (a->fp == NULL) {
        say(why, whysize, "cannot open %s: %s", path, strerror(errno));
        free(a);
        return -1;
    }
    if (fseek(a->fp, 0, SEEK_END) != 0 || (end = ico_archive_tell(a->fp)) < 0) {
        say(why, whysize, "cannot size %s", path);
        arch_free(a);
        return -1;
    }
    a->info.file_bytes = (uint64_t)end;

    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = zip_read;
    zip.m_pIO_opaque = a->fp;
    if (!mz_zip_reader_init(&zip, a->info.file_bytes, 0)) {
        say(why, whysize, "%s is not a ZIP archive (%s)", path,
            mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        arch_free(a);
        return -1;
    }
    idx = mz_zip_reader_locate_file(&zip, ICO_ARCHIVE_META, NULL, 0);
    if (idx >= 0) {
        text = mz_zip_reader_extract_to_heap(&zip, (mz_uint)idx, &textlen, 0);
    }
    if (text == NULL) {
        say(why, whysize, "%s has no readable " ICO_ARCHIVE_META, path);
        goto done;
    }
    /* NUL-terminate the copy the parser works in */
    {
        char *t = realloc(text, textlen + 1);

        if (t == NULL) {
            say(why, whysize, "out of memory");
            goto done;
        }
        text = t;
        text[textlen] = '\0';
    }
    j.p = text;
    root = jvalue(&j, 0);
    jws(&j);
    if (root < 0 || j.bad || *j.p != '\0' || j.n[root].type != J_OBJ) {
        say(why, whysize, ICO_ARCHIVE_META " is not valid JSON");
        goto done;
    }
    copy_str(a->info.format, sizeof(a->info.format), jget_str(&j, root, "format"));
    if (jget_u64(&j, root, "version", &v) == 0 && v <= INT_MAX) {
        a->info.version = (int)v;
    }
    copy_str(a->info.extractor, sizeof(a->info.extractor), jget_str(&j, root, "extractor"));
    copy_str(a->info.disc_id, sizeof(a->info.disc_id), jget_str(&j, root, "disc_id"));
    if (strcmp(a->info.format, ICO_ARCHIVE_FORMAT) != 0) {
        say(why, whysize, ICO_ARCHIVE_META ": format '%s', not " ICO_ARCHIVE_FORMAT,
            a->info.format);
        goto done;
    }
    src = jget(&j, root, "source");
    copy_str(a->info.iso_sha1, sizeof(a->info.iso_sha1), jget_str(&j, src, "sha1"));
    copy_str(a->info.accepted_by, sizeof(a->info.accepted_by), jget_str(&j, src, "accepted_by"));
    copy_str(a->info.elf_sha1, sizeof(a->info.elf_sha1), jget_str(&j, src, "elf_sha1"));
    if (jget_u64(&j, src, "size", &v) == 0) {
        a->info.iso_size = v;
    }
    if (jget_u64(&j, root, "volume_sectors", &v) != 0 || v == 0 || v > UINT32_MAX) {
        say(why, whysize, ICO_ARCHIVE_META ": no volume_sectors");
        goto done;
    }
    a->info.volume_sectors = (uint32_t)v;
    ents = jget(&j, root, "entries");
    if (ents < 0 || j.n[ents].type != J_ARR) {
        say(why, whysize, ICO_ARCHIVE_META ": no entries");
        goto done;
    }
    for (c = j.n[ents].child; c >= 0; c = j.n[c].next) {
        a->count++;
    }
    a->ent = calloc(a->count ? a->count : 1, sizeof(*a->ent));
    if (a->ent == NULL) {
        say(why, whysize, "out of memory");
        goto done;
    }
    a->info.entries = a->count;
    idx = 0;
    for (c = j.n[ents].child; c >= 0; c = j.n[c].next, idx++) {
        if (parse_entry(a, &zip, &j, c, &a->ent[idx], why, whysize) != 0) {
            goto done;
        }
    }
    ok = 1;

done:
    mz_zip_reader_end(&zip);
    free(j.n);
    if (text != NULL) {
        mz_free(text);
    }
    if (!ok) {
        arch_free(a);
        return -1;
    }
    *out = a;
    return 0;
}

int ico_archive_read_info(const char *path, IcoArchiveInfo *out, char *why, size_t whysize)
{
    Arch *a;

    memset(out, 0, sizeof(*out));
    if (arch_open(path, &a, why, whysize) != 0) {
        return -1;
    }
    *out = a->info;
    arch_free(a);
    return 0;
}

int ico_archive_info_acceptable(const IcoArchiveInfo *info, char *why, size_t whysize)
{
    if (info->version != ICO_ARCHIVE_VERSION) {
        say(why, whysize, "format version %d, this build reads %d", info->version,
            ICO_ARCHIVE_VERSION);
        return 0;
    }
    if (strcmp(info->extractor, ICO_ARCHIVE_EXTRACTOR) != 0) {
        say(why, whysize, "written by '%s', this build is '%s'", info->extractor,
            ICO_ARCHIVE_EXTRACTOR);
        return 0;
    }
    if (strcmp(info->disc_id, ICO_DISC_ID) != 0) {
        say(why, whysize, "disc id '%s', this build plays " ICO_DISC_ID, info->disc_id);
        return 0;
    }
    if (strcmp(info->accepted_by, ICO_RULE_ISO_SHA1) != 0 &&
        strcmp(info->accepted_by, ICO_RULE_ELF_DATADF) != 0) {
        say(why, whysize, "the source image was not verified (rule '%s')", info->accepted_by);
        return 0;
    }
    if (strcmp(info->accepted_by, ICO_RULE_ISO_SHA1) == 0 &&
        (!is_hex40(info->iso_sha1) || strcmp(info->iso_sha1, ICO_DISC_ISO_SHA1) != 0)) {
        say(why, whysize, "source image SHA-1 %s, expected " ICO_DISC_ISO_SHA1, info->iso_sha1);
        return 0;
    }
    if (strcmp(info->elf_sha1, ICO_DISC_ELF_SHA1) != 0) {
        say(why, whysize, "boot ELF SHA-1 %s, expected " ICO_DISC_ELF_SHA1, info->elf_sha1);
        return 0;
    }
    return 1;
}

/* --- the backend ------------------------------------------------------------ */

static int ar_mount(void **state, const char *location)
{
    char why[512];
    Arch *a;

    *state = NULL;
    if (arch_open(location, &a, why, sizeof(why)) != 0) {
        fprintf(stderr, "archive: %s\n", why);
        return -1;
    }
    *state = a;
    return 0;
}

static void ar_unmount(void *state)
{
    arch_free(state);
}

static int ar_lookup(void *state, const char *path, IcoVfsEntry *out)
{
    Arch *a = state;
    uint32_t i;

    for (i = 0; i < a->count; i++) {
        if (strcmp(a->ent[i].path, path) == 0) {
            *out = a->ent[i].e;
            return 0;
        }
    }
    return -1;
}

static int read_at(FILE *fp, uint64_t ofs, void *dst, size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (ico_archive_seek(fp, ofs) != 0) {
        return -1;
    }
    return fread(dst, 1, n, fp) == n ? 0 : -1;
}

static int ar_read_sectors(void *state, uint32_t lsn, uint32_t count, void *dst)
{
    Arch *a = state;
    unsigned char *d = dst;

    if (lsn >= a->info.volume_sectors || count > a->info.volume_sectors - lsn) {
        return -1;
    }
    while (count > 0) {
        const AEntry *f = NULL;
        uint64_t off, want, data;
        uint32_t n, i;

        for (i = 0; i < a->count; i++) {
            const AEntry *e = &a->ent[i];

            if (e->stored && lsn >= e->e.lsn && lsn - e->e.lsn < e->nsec) {
                f = e;
                break;
            }
        }
        if (f == NULL) {
            return -1; /* no stored file covers this sector */
        }
        n = f->e.lsn + f->nsec - lsn;
        if (n > count) {
            n = count;
        }
        off = (uint64_t)(lsn - f->e.lsn) * ICO_VFS_SECTOR;
        want = (uint64_t)n * ICO_VFS_SECTOR;
        data = off < f->e.size ? f->e.size - off : 0;
        if (data > want) {
            data = want;
        }
        if (read_at(a->fp, f->data_ofs + off, d, (size_t)data) != 0) {
            return -1;
        }
        if (data < want) {
            /* the last sector's bytes past the file's end */
            uint64_t t = off + data - f->e.size;

            if (f->tail_len > 0) {
                if (read_at(a->fp, f->tail_ofs + t, d + data, (size_t)(want - data)) != 0) {
                    return -1;
                }
            } else {
                memset(d + data, 0, (size_t)(want - data));
            }
        }
        d += want;
        lsn += n;
        count -= n;
    }
    return 0;
}

static uint32_t ar_volume_sectors(void *state)
{
    return ((Arch *)state)->info.volume_sectors;
}

const IcoVfsBackend ico_vfs_archive = {
    "archive", ar_mount, ar_unmount, ar_lookup, ar_read_sectors, ar_volume_sectors,
};

IcoVfs *ico_vfs_mount_archive(const char *path)
{
    return ico_vfs_mount(&ico_vfs_archive, path);
}
