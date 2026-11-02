/*
 * port/platform/backup.c
 *
 * The player's data as one zip and back (backup.h), written and read with
 * miniz through FILE streams that host_fs.h opens, so a name outside the
 * ANSI code page works on Windows.
 */
#include "backup.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host_fs.h"
#include "miniz.h"

#define PATH_LEN 1200
#define MAX_ITEMS 16

/* out->why from fmt with up to two strings, cut to its size at the end */
static void say(IcoBackupReport *out, const char *fmt, const char *a, const char *b)
{
    char text[4096];
    if (out == NULL) {
        return;
    }
    snprintf(text, sizeof(text), fmt, a != NULL ? a : "", b != NULL ? b : "");
    const size_t n = strlen(text) < sizeof(out->why) ? strlen(text) : sizeof(out->why) - 1;
    memcpy(out->why, text, n);
    out->why[n] = '\0';
}

const char *ico_backup_result_text(IcoBackupResult r)
{
    switch (r) {
    case ICO_BACKUP_OK:
        return "ok";
    case ICO_BACKUP_NONE:
        return "no restore waits";
    case ICO_BACKUP_NOTHING:
        return "nothing to back up";
    case ICO_BACKUP_BUSY:
        return "the memory card, a save state or the disc is in use";
    case ICO_BACKUP_READ:
        return "a file could not be read";
    case ICO_BACKUP_WRITE:
        return "a file could not be written";
    case ICO_BACKUP_NOT_BACKUP:
        return "not a backup";
    case ICO_BACKUP_NEWER:
        return "made by a newer version";
    case ICO_BACKUP_DAMAGED:
        return "damaged";
    case ICO_BACKUP_UNSAFE:
        return "a file outside the backup's folders";
    case ICO_BACKUP_TOO_BIG:
        return "too big";
    }
    return "unknown";
}

/* the bounds (backup.h), lowered by a test */
static int s_maxFiles = ICO_BACKUP_MAX_FILES;
static unsigned long long s_maxFileBytes = ICO_BACKUP_MAX_FILE_BYTES;
static unsigned long long s_maxBytes = ICO_BACKUP_MAX_BYTES;

void ico_backup_test_bounds(int files, unsigned long long file_bytes, unsigned long long bytes)
{
    s_maxFiles = files > 0 ? files : ICO_BACKUP_MAX_FILES;
    s_maxFileBytes = file_bytes > 0 ? file_bytes : ICO_BACKUP_MAX_FILE_BYTES;
    s_maxBytes = bytes > 0 ? bytes : ICO_BACKUP_MAX_BYTES;
}

/* --- names ------------------------------------------------------------------ */

/* memcard, or memcard and letters, digits, '-' or '_' after a '-', '_',
   letter or digit (memcard-preview, memcard2) */
static int card_name(const char *s, size_t n)
{
    if (n < 7 || strncmp(s, "memcard", 7) != 0) {
        return 0;
    }
    for (size_t i = 7; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (!(isalnum(c) || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int part_is(const char *s, size_t n, const char *word)
{
    return strlen(word) == n && strncmp(s, word, n) == 0;
}

int ico_backup_name_ok(const char *name, int folder)
{
    const size_t len = name != NULL ? strlen(name) : 0;
    const char *first = name;
    size_t firstLen = 0;
    int parts = 0;

    if (len == 0 || len > 240 || name[0] == '/') {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c == 0x7f || strchr("\\:<>\"|?*", (int)c) != NULL) {
            return 0;
        }
    }
    for (const char *p = name; *p != '\0';) {
        const char *end = strchr(p, '/');
        const size_t n = end != NULL ? (size_t)(end - p) : strlen(p);
        if (n == 0 || part_is(p, n, ".") || part_is(p, n, "..")) {
            return 0;
        }
        if (++parts == 1) {
            firstLen = n;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
        if (*p == '\0') {
            return 0; /* a trailing '/' */
        }
    }
    if (parts > 8) {
        return 0;
    }
    const int place = part_is(first, firstLen, "states") || card_name(first, firstLen);
    if (parts == 1) {
        if (folder) {
            return place;
        }
        return part_is(first, firstLen, ICO_BACKUP_MANIFEST) ||
               part_is(first, firstLen, "config.toml") ||
               part_is(first, firstLen, "achievements.toml");
    }
    return place;
}

static int ends_with(const char *s, const char *suffix)
{
    const size_t n = strlen(s), k = strlen(suffix);
    return n >= k && strcmp(s + n - k, suffix) == 0;
}

static int join(char *out, size_t size, const char *a, const char *b)
{
    const int n = snprintf(out, size, "%s/%s", a, b);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

/* --- the manifest ------------------------------------------------------------- */

static void copy_value(char *dst, size_t size, const char *v, size_t n)
{
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, v, n);
    dst[n] = '\0';
}

/* OK, NOT_BACKUP or NEWER */
static IcoBackupResult parse_manifest(const char *text, size_t len, IcoBackupManifest *m)
{
    const size_t magicLen = strlen(ICO_BACKUP_MAGIC);
    const char *p = text, *end = text + len;

    memset(m, 0, sizeof(*m));
    m->files = -1;
    if (len < magicLen + 2 || memcmp(text, ICO_BACKUP_MAGIC " ", magicLen + 1) != 0) {
        return ICO_BACKUP_NOT_BACKUP;
    }
    m->format = atoi(text + magicLen + 1);
    if (m->format < 1) {
        return ICO_BACKUP_NOT_BACKUP;
    }
    if (m->format > ICO_BACKUP_FORMAT) {
        return ICO_BACKUP_NEWER;
    }
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *lineEnd = nl != NULL ? nl : end;
        const char *sp = memchr(p, ' ', (size_t)(lineEnd - p));
        if (sp != NULL) {
            const size_t kn = (size_t)(sp - p), vn = (size_t)(lineEnd - sp - 1);
            const char *v = sp + 1;
            char num[32];
            copy_value(num, sizeof(num), v, vn);
            if (part_is(p, kn, "version")) {
                copy_value(m->version, sizeof(m->version), v, vn);
            } else if (part_is(p, kn, "commit")) {
                copy_value(m->commit, sizeof(m->commit), v, vn);
            } else if (part_is(p, kn, "platform")) {
                copy_value(m->platform, sizeof(m->platform), v, vn);
            } else if (part_is(p, kn, "disc")) {
                copy_value(m->disc, sizeof(m->disc), v, vn);
            } else if (part_is(p, kn, "discs")) {
                copy_value(m->discs, sizeof(m->discs), v, vn);
            } else if (part_is(p, kn, "from")) {
                copy_value(m->from, sizeof(m->from), v, vn);
            } else if (part_is(p, kn, "time")) {
                m->time = strtoll(num, NULL, 10);
            } else if (part_is(p, kn, "files")) {
                m->files = atoi(num);
            } else if (part_is(p, kn, "bytes")) {
                m->bytes = strtoull(num, NULL, 10);
            }
        }
        p = lineEnd + 1;
    }
    return m->files >= 0 ? ICO_BACKUP_OK : ICO_BACKUP_NOT_BACKUP;
}

/* a word appended to a space-separated list unless it is there already */
static void list_add(char *list, size_t size, const char *word, size_t n)
{
    const size_t have = strlen(list);
    for (const char *p = list; *p != '\0';) {
        const char *sp = strchr(p, ' ');
        const size_t k = sp != NULL ? (size_t)(sp - p) : strlen(p);
        if (k == n && strncmp(p, word, n) == 0) {
            return;
        }
        p = sp != NULL ? sp + 1 : p + k;
    }
    if (have + (have > 0) + n + 1 > size) {
        return;
    }
    if (have > 0) {
        list[have] = ' ';
    }
    memcpy(list + have + (have > 0), word, n);
    list[have + (have > 0) + n] = '\0';
}

/* --- writing ---------------------------------------------------------------- */

typedef struct Entry {
    char *rel;
    unsigned long long size;
    long long mtime;
} Entry;

typedef struct Collect {
    const char *root;
    size_t rootLen;
    Entry *e;
    int n, cap;
    int failed; /* no memory */
    unsigned long long bytes;
    int bigFile;
    char discs[96];
} Collect;

static int collect_add(Collect *c, const char *path)
{
    const char *rel = path + c->rootLen + 1;
    unsigned long long size = 0;
    long long mtime = 0;

    if (ends_with(rel, ".tmp") || ends_with(rel, ".part") || !ico_backup_name_ok(rel, 0) ||
        ico_path_kind(path, &size, &mtime) != 0) {
        return 0;
    }
    if (strncmp(rel, "states/", 7) == 0) {
        /* a slot's developer dumps (states/<slug>/dump-<tick>-<k>/) stay */
        if (strstr(rel, "/dump-") != NULL) {
            return 0;
        }
        const char *slug = rel + 7, *sl = strchr(slug, '/');
        if (sl != NULL) {
            list_add(c->discs, sizeof(c->discs), slug, (size_t)(sl - slug));
        }
    }
    if (c->n == c->cap) {
        const int ncap = c->cap != 0 ? c->cap * 2 : 64;
        Entry *g = realloc(c->e, (size_t)ncap * sizeof(*g));
        if (g == NULL) {
            c->failed = 1;
            return 1;
        }
        c->e = g;
        c->cap = ncap;
    }
    c->e[c->n].rel = malloc(strlen(rel) + 1);
    if (c->e[c->n].rel == NULL) {
        c->failed = 1;
        return 1;
    }
    strcpy(c->e[c->n].rel, rel);
    c->e[c->n].size = size;
    c->e[c->n].mtime = mtime;
    c->n++;
    c->bytes += size;
    c->bigFile |= size > s_maxFileBytes;
    /* stop early past the bounds */
    return c->n > s_maxFiles || c->bytes > s_maxBytes;
}

static int walk_visit(const char *path, const char *name, void *user)
{
    (void)name;
    return collect_add((Collect *)user, path);
}

static int top_visit(const char *name, int kind, void *user)
{
    Collect *c = (Collect *)user;
    char path[PATH_LEN];

    if (join(path, sizeof(path), c->root, name) != 0) {
        return 0;
    }
    if (kind == 0 && (strcmp(name, "config.toml") == 0 || strcmp(name, "achievements.toml") == 0)) {
        return collect_add(c, path);
    }
    if (kind != 0 && (strcmp(name, "states") == 0 || card_name(name, strlen(name)))) {
        return ico_dir_walk(path, 6, walk_visit, c) < 0
                   ? 0
                   : c->failed || c->n > s_maxFiles || c->bytes > s_maxBytes;
    }
    return 0;
}

static void collect_free(Collect *c)
{
    for (int i = 0; i < c->n; i++) {
        free(c->e[i].rel);
    }
    free(c->e);
    c->e = NULL;
    c->n = c->cap = 0;
}

/* an open file's size, -1 when it cannot be told; the file is at its start
   again */
static long long open_size(FILE *f)
{
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) {
        return -1;
    }
    const long long n = _ftelli64(f);
    return _fseeki64(f, 0, SEEK_SET) == 0 ? n : -1;
#else
    if (fseeko(f, 0, SEEK_END) != 0) {
        return -1;
    }
    const long long n = (long long)ftello(f);
    return fseeko(f, 0, SEEK_SET) == 0 ? n : -1;
#endif
}

/* packed already: stored as they are */
static mz_uint entry_level(const char *rel)
{
    return ends_with(rel, ".ics") || ends_with(rel, ".png") ? MZ_NO_COMPRESSION : MZ_BEST_SPEED;
}

IcoBackupResult ico_backup_write(const char *user_dir, const char *zip_path,
                                 const IcoBackupInfo *info, const IcoBackupGates *gates,
                                 IcoBackupProgressFn fn, void *user, IcoBackupReport *out)
{
    Collect c;
    char part[PATH_LEN], manifest[1024];
    IcoBackupResult r = ICO_BACKUP_OK;
    mz_zip_archive z;
    FILE *f = NULL;
    int zipOpen = 0;
    unsigned long long written = 0;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (gates != NULL && (gates->card_busy || gates->state_busy)) {
        say(out, "%s",
            gates->card_busy ? "the memory card is being written" : "a save state is being written",
            NULL);
        return ICO_BACKUP_BUSY;
    }
    memset(&c, 0, sizeof(c));
    c.root = user_dir;
    c.rootLen = user_dir != NULL ? strlen(user_dir) : 0;
    if (user_dir == NULL || zip_path == NULL || info == NULL ||
        snprintf(part, sizeof(part), "%s.part", zip_path) >= (int)sizeof(part)) {
        say(out, "%s", "no folder or file name, or a name too long", NULL);
        return ICO_BACKUP_WRITE;
    }
    if (ico_dir_entries(user_dir, 0, top_visit, &c) < 0) {
        say(out, "the folder %s cannot be read%s", user_dir, NULL);
        return ICO_BACKUP_READ;
    }
    if (c.failed) {
        say(out, "%s", "no memory for the list of files", NULL);
        collect_free(&c);
        return ICO_BACKUP_READ;
    }
    if (c.n > s_maxFiles || c.bytes > s_maxBytes || c.bigFile) {
        char what[96];
        snprintf(what, sizeof(what), "%d files, %llu MB", c.n, c.bytes >> 20);
        say(out, "the data is past the bounds a backup keeps (%s)%s", what, NULL);
        collect_free(&c);
        return ICO_BACKUP_TOO_BIG;
    }
    if (c.n == 0) {
        say(out, "%s", "the user folder holds no memory card, save state or settings", NULL);
        collect_free(&c);
        return ICO_BACKUP_NOTHING;
    }
    char discs[96] = "";
    const char *d = info->disc != NULL ? info->disc : "";
    /* the running disc first, then the save states' */
    list_add(discs, sizeof(discs), d, strlen(d));
    for (const char *p = c.discs; *p != '\0';) {
        const char *sp = strchr(p, ' ');
        const size_t k = sp != NULL ? (size_t)(sp - p) : strlen(p);
        list_add(discs, sizeof(discs), p, k);
        p = sp != NULL ? sp + 1 : p + k;
    }
    memset(&z, 0, sizeof(z));
    f = ico_fopen(part, "wb");
    if (f == NULL) {
        say(out, "cannot create %s: %s", part, strerror(errno));
        collect_free(&c);
        return ICO_BACKUP_WRITE;
    }
    if (!mz_zip_writer_init_cfile(&z, f, 0)) {
        say(out, "%s%s", "the zip could not be started", NULL);
        r = ICO_BACKUP_WRITE;
        goto done;
    }
    zipOpen = 1;
    for (int i = 0; i < c.n; i++) {
        char path[PATH_LEN];
        MZ_TIME_T t = (MZ_TIME_T)c.e[i].mtime;
        FILE *src;
        long long size;

        if (join(path, sizeof(path), user_dir, c.e[i].rel) != 0 ||
            (src = ico_fopen(path, "rb")) == NULL) {
            say(out, "cannot read %s: %s", c.e[i].rel, strerror(errno));
            r = ICO_BACKUP_READ;
            goto done;
        }
        /* the size of the file opened, not of the listing: a file replaced
           since (the achievements' writer renames a new one in) goes in
           whole as it is now */
        size = open_size(src);
        if (size < 0 || (unsigned long long)size > s_maxFileBytes ||
            written + (unsigned long long)size > s_maxBytes) {
            fclose(src);
            say(out, "%s %s", c.e[i].rel,
                size < 0 ? "could not be read" : "grew past the size a backup keeps");
            r = size < 0 ? ICO_BACKUP_READ : ICO_BACKUP_TOO_BIG;
            goto done;
        }
        const mz_bool ok = mz_zip_writer_add_cfile(&z, c.e[i].rel, src, (mz_uint64)size, &t, NULL,
                                                   0, entry_level(c.e[i].rel), NULL, 0, NULL, 0);
        const int readBad = ferror(src) != 0;
        fclose(src);
        if (!ok) {
            say(out, "%s could not be added: %s", c.e[i].rel,
                readBad ? "it could not be read"
                        : mz_zip_get_error_string(mz_zip_get_last_error(&z)));
            r = readBad ? ICO_BACKUP_READ : ICO_BACKUP_WRITE;
            goto done;
        }
        written += (unsigned long long)size;
        if (out != NULL) {
            out->files = i + 1;
            out->bytes = written;
        }
        if (fn != NULL) {
            fn(user, i + 1, c.n);
        }
    }
    /* the manifest last, with the figures of what went in */
    snprintf(manifest, sizeof(manifest),
             ICO_BACKUP_MAGIC " %d\nversion %s\ncommit %s\nplatform %s\ndisc %s\ndiscs "
                              "%s\ntime %lld\nfiles %d\nbytes %llu\n",
             ICO_BACKUP_FORMAT,
             info->version != NULL && info->version[0] ? info->version : "development",
             info->commit != NULL ? info->commit : "", info->platform != NULL ? info->platform : "",
             d, discs, info->time, c.n, written);
    if (!mz_zip_writer_add_mem(&z, ICO_BACKUP_MANIFEST, manifest, strlen(manifest),
                               MZ_BEST_SPEED)) {
        say(out, "the manifest could not be written: %s%s",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)), NULL);
        r = ICO_BACKUP_WRITE;
        goto done;
    }
    if (!mz_zip_writer_finalize_archive(&z)) {
        say(out, "the zip could not be finished: %s%s",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)), NULL);
        r = ICO_BACKUP_WRITE;
        goto done;
    }
done:
    if (zipOpen) {
        mz_zip_writer_end(&z);
    }
    if (r == ICO_BACKUP_OK && ico_fsync(f) != 0) {
        say(out, "%s could not be written to the disk: %s", part, strerror(errno));
        r = ICO_BACKUP_WRITE;
    }
    if (fclose(f) != 0 && r == ICO_BACKUP_OK) {
        say(out, "%s could not be closed: %s", part, strerror(errno));
        r = ICO_BACKUP_WRITE;
    }
    if (r == ICO_BACKUP_OK && ico_rename_replace(part, zip_path) != 0) {
        say(out, "%s could not be renamed: %s", part, strerror(errno));
        r = ICO_BACKUP_WRITE;
    }
    if (r != ICO_BACKUP_OK) {
        ico_remove(part);
        if (out != NULL) {
            out->files = 0;
            out->bytes = 0;
        }
    }
    collect_free(&c);
    return r;
}

/* --- reading ------------------------------------------------------------------ */

typedef struct Reader {
    mz_zip_archive z;
    FILE *f;
    int open;
} Reader;

static void reader_close(Reader *rd)
{
    if (rd->open) {
        mz_zip_reader_end(&rd->z);
    }
    if (rd->f != NULL) {
        fclose(rd->f);
    }
    memset(rd, 0, sizeof(*rd));
}

/* opens the zip, reads its manifest (raw text in *raw when not NULL, to
   free) and checks every entry's name and size against it and the bounds */
static IcoBackupResult reader_open(Reader *rd, const char *zip_path, const char *disc_now,
                                   IcoBackupManifest *m, char **raw, IcoBackupReport *out)
{
    unsigned long long size = 0;
    mz_zip_archive_file_stat st;
    unsigned long long bytes = 0;
    int files = 0;

    memset(rd, 0, sizeof(*rd));
    if (raw != NULL) {
        *raw = NULL;
    }
    if (zip_path == NULL || ico_path_kind(zip_path, &size, NULL) != 0 ||
        (rd->f = ico_fopen(zip_path, "rb")) == NULL) {
        say(out, "cannot open %s: %s", zip_path != NULL ? zip_path : "(none)", strerror(errno));
        return ICO_BACKUP_READ;
    }
    if (!mz_zip_reader_init_cfile(&rd->z, rd->f, size, 0)) {
        say(out, "%s is not a zip (%s)", zip_path,
            mz_zip_get_error_string(mz_zip_get_last_error(&rd->z)));
        reader_close(rd);
        return ICO_BACKUP_NOT_BACKUP;
    }
    rd->open = 1;
    const mz_uint n = mz_zip_reader_get_num_files(&rd->z);
    if (n > (mz_uint)s_maxFiles + 1u) {
        char num[32];
        snprintf(num, sizeof(num), "%u", (unsigned)n);
        say(out, "the zip holds %s entries%s", num, NULL);
        reader_close(rd);
        return ICO_BACKUP_TOO_BIG;
    }
    const int mi = mz_zip_reader_locate_file(&rd->z, ICO_BACKUP_MANIFEST, NULL, 0);
    if (mi < 0 || !mz_zip_reader_file_stat(&rd->z, (mz_uint)mi, &st) ||
        st.m_uncomp_size > ICO_BACKUP_MAX_MANIFEST || st.m_is_directory) {
        say(out, "the zip has no %s%s", ICO_BACKUP_MANIFEST, NULL);
        reader_close(rd);
        return ICO_BACKUP_NOT_BACKUP;
    }
    size_t textLen = 0;
    char *heap = mz_zip_reader_extract_to_heap(&rd->z, (mz_uint)mi, &textLen, 0);
    char *text = heap != NULL ? malloc(textLen + 1) : NULL;
    if (text == NULL) {
        say(out, "its %s cannot be read%s", ICO_BACKUP_MANIFEST, NULL);
        mz_free(heap);
        reader_close(rd);
        return ICO_BACKUP_DAMAGED;
    }
    /* the text with its end, for the copy a restore keeps */
    memcpy(text, heap, textLen);
    text[textLen] = '\0';
    mz_free(heap);
    const IcoBackupResult pr = parse_manifest(text, textLen, m);
    if (pr != ICO_BACKUP_OK) {
        say(out,
            pr == ICO_BACKUP_NEWER ? "the manifest is of a newer format%s%s"
                                   : "the manifest is not this program's%s%s",
            NULL, NULL);
        free(text);
        reader_close(rd);
        return pr;
    }
    for (mz_uint i = 0; i < n; i++) {
        if ((int)i == mi) {
            continue;
        }
        if (!mz_zip_reader_file_stat(&rd->z, i, &st)) {
            say(out, "an entry cannot be read%s%s", NULL, NULL);
            free(text);
            reader_close(rd);
            return ICO_BACKUP_DAMAGED;
        }
        if (st.m_is_directory) {
            char name[256];
            size_t k = strlen(st.m_filename);
            copy_value(name, sizeof(name), st.m_filename, k);
            while (k > 0 && name[k - 1] == '/') {
                name[--k] = '\0';
            }
            if (!ico_backup_name_ok(name, 1)) {
                say(out, "the entry \"%s\" is outside the backup's folders%s", st.m_filename, NULL);
                free(text);
                reader_close(rd);
                return ICO_BACKUP_UNSAFE;
            }
            continue;
        }
        if (!ico_backup_name_ok(st.m_filename, 0)) {
            say(out, "the entry \"%s\" is outside the backup's folders%s", st.m_filename, NULL);
            free(text);
            reader_close(rd);
            return ICO_BACKUP_UNSAFE;
        }
        if (st.m_is_encrypted || !st.m_is_supported) {
            say(out, "the entry \"%s\" is encrypted or packed in a way this program cannot read%s",
                st.m_filename, NULL);
            free(text);
            reader_close(rd);
            return ICO_BACKUP_DAMAGED;
        }
        if (st.m_uncomp_size > s_maxFileBytes || (bytes += st.m_uncomp_size) > s_maxBytes) {
            say(out, "the entry \"%s\" is past the size a backup keeps%s", st.m_filename, NULL);
            free(text);
            reader_close(rd);
            return ICO_BACKUP_TOO_BIG;
        }
        const char *sl = strchr(st.m_filename, '/');
        list_add(m->items, sizeof(m->items), st.m_filename,
                 sl != NULL ? (size_t)(sl - st.m_filename) : strlen(st.m_filename));
        files++;
    }
    if (files != m->files || bytes != m->bytes) {
        char what[96];
        snprintf(what, sizeof(what), "%d files of %llu bytes, the manifest says %d of %llu", files,
                 bytes, m->files, m->bytes);
        say(out, "the zip holds %s%s", what, NULL);
        free(text);
        reader_close(rd);
        return ICO_BACKUP_DAMAGED;
    }
    m->other_disc = disc_now != NULL && disc_now[0] != '\0' && m->disc[0] != '\0' &&
                    strcmp(disc_now, m->disc) != 0;
    if (raw != NULL) {
        *raw = text;
    } else {
        free(text);
    }
    return ICO_BACKUP_OK;
}

IcoBackupResult ico_backup_inspect(const char *zip_path, const char *disc_now, IcoBackupManifest *m,
                                   IcoBackupReport *out)
{
    Reader rd;
    IcoBackupManifest local;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    const IcoBackupResult r =
        reader_open(&rd, zip_path, disc_now, m != NULL ? m : &local, NULL, out);
    if (r == ICO_BACKUP_OK) {
        reader_close(&rd);
    }
    return r;
}

/* the folders of rel's parent under base, made as needed */
static int make_parents(const char *base, const char *rel)
{
    char path[PATH_LEN];
    const size_t baseLen = strlen(base);

    if (baseLen + 1 + strlen(rel) >= sizeof(path)) {
        return -1;
    }
    memcpy(path, base, baseLen);
    path[baseLen] = '/';
    strcpy(path + baseLen + 1, rel);
    for (char *p = path + baseLen + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (ico_mkdir(path) != 0 && (errno != EEXIST || ico_path_kind(path, NULL, NULL) != 1)) {
            return -1;
        }
        *p = '/';
    }
    return 0;
}

/* an entry's data into a file, its size held to the one its header gives */
typedef struct Sink {
    FILE *f;
    unsigned long long want, got;
    int writeFailed;
    int err;
} Sink;

static size_t sink_write(void *op, mz_uint64 ofs, const void *buf, size_t n)
{
    Sink *s = (Sink *)op;
    if (ofs != s->got || s->got + n > s->want) {
        return 0; /* more than the entry says: damaged */
    }
    if (fwrite(buf, 1, n, s->f) != n) {
        s->writeFailed = 1;
        s->err = errno;
        return 0;
    }
    s->got += n;
    return n;
}

IcoBackupResult ico_backup_restore(const char *zip_path, const char *from_name,
                                   const char *user_dir, const char *disc_now,
                                   const IcoBackupGates *gates, IcoBackupProgressFn fn, void *user,
                                   IcoBackupManifest *m, IcoBackupReport *out)
{
    Reader rd;
    IcoBackupManifest local;
    IcoBackupManifest *man = m != NULL ? m : &local;
    char part[PATH_LEN], staging[PATH_LEN];
    char *raw = NULL;
    IcoBackupResult r = ICO_BACKUP_OK;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (gates != NULL && (gates->card_busy || gates->state_busy || gates->disc_busy)) {
        say(out, "%s%s",
            gates->card_busy    ? "the memory card is being written"
            : gates->state_busy ? "a save state is being written"
                                : "the disc is being read",
            NULL);
        return ICO_BACKUP_BUSY;
    }
    if (user_dir == NULL || join(part, sizeof(part), user_dir, ICO_BACKUP_STAGING_PART) != 0 ||
        join(staging, sizeof(staging), user_dir, ICO_BACKUP_STAGING) != 0) {
        say(out, "%s%s", "no user folder, or its name is too long", NULL);
        return ICO_BACKUP_WRITE;
    }
    r = reader_open(&rd, zip_path, disc_now, man, &raw, out);
    if (r != ICO_BACKUP_OK) {
        return r;
    }
    if (ico_remove_tree(part) != 0 || ico_mkdir(part) != 0) {
        say(out, "cannot make %s: %s", part, strerror(errno));
        r = ICO_BACKUP_WRITE;
        goto done;
    }
    {
        const mz_uint n = mz_zip_reader_get_num_files(&rd.z);
        int count = 0;
        for (mz_uint i = 0; i < n && r == ICO_BACKUP_OK; i++) {
            mz_zip_archive_file_stat st;
            char path[PATH_LEN];
            Sink s;

            if (!mz_zip_reader_file_stat(&rd.z, i, &st) || st.m_is_directory ||
                strcmp(st.m_filename, ICO_BACKUP_MANIFEST) == 0) {
                continue;
            }
            if (make_parents(part, st.m_filename) != 0 ||
                join(path, sizeof(path), part, st.m_filename) != 0 ||
                (s.f = ico_fopen(path, "wb")) == NULL) {
                say(out, "cannot create %s: %s", st.m_filename, strerror(errno));
                r = ICO_BACKUP_WRITE;
                break;
            }
            s.want = st.m_uncomp_size;
            s.got = 0;
            s.writeFailed = 0;
            s.err = 0;
            const mz_bool ok = mz_zip_reader_extract_to_callback(&rd.z, i, sink_write, &s, 0);
            const int synced = ico_fsync(s.f) == 0;
            const int closed = fclose(s.f) == 0;
            if (s.writeFailed || ((!synced || !closed) && ok)) {
                say(out, "cannot write %s: %s", st.m_filename,
                    strerror(s.writeFailed ? s.err : errno));
                r = ICO_BACKUP_WRITE;
            } else if (!ok || s.got != s.want) {
                say(out, "the entry \"%s\" is damaged (%s)", st.m_filename,
                    mz_zip_get_error_string(mz_zip_get_last_error(&rd.z)));
                r = ICO_BACKUP_DAMAGED;
            } else {
                count++;
                if (out != NULL) {
                    out->files = count;
                    out->bytes += s.got;
                }
                if (fn != NULL) {
                    fn(user, count, man->files);
                }
            }
        }
    }
    if (r == ICO_BACKUP_OK) {
        /* the manifest beside the files, with the zip's name for the next
           start's line */
        char path[PATH_LEN];
        FILE *f =
            join(path, sizeof(path), part, ICO_BACKUP_MANIFEST) == 0 ? ico_fopen(path, "wb") : NULL;
        const size_t rawLen = strlen(raw);
        const int ok = f != NULL && fwrite(raw, 1, rawLen, f) == rawLen &&
                       (rawLen == 0 || raw[rawLen - 1] == '\n' || fputc('\n', f) != EOF) &&
                       fprintf(f, "from %s\n", from_name != NULL ? from_name : "") > 0 &&
                       ico_fsync(f) == 0;
        if (f != NULL && fclose(f) != 0) {
            r = ICO_BACKUP_WRITE;
        }
        if (!ok || r != ICO_BACKUP_OK) {
            say(out, "cannot write %s: %s", path, strerror(errno));
            r = ICO_BACKUP_WRITE;
        }
    }
    if (r == ICO_BACKUP_OK) {
        snprintf(man->from, sizeof(man->from), "%s", from_name != NULL ? from_name : "");
        if (ico_remove_tree(staging) != 0 || ico_rename_replace(part, staging) != 0) {
            say(out, "cannot rename %s: %s", part, strerror(errno));
            r = ICO_BACKUP_WRITE;
        }
    }
done:
    free(raw);
    reader_close(&rd);
    if (r != ICO_BACKUP_OK) {
        ico_remove_tree(part);
        if (out != NULL) {
            out->files = 0;
            out->bytes = 0;
        }
    }
    return r;
}

/* --- putting a restore in place ----------------------------------------------- */

static unsigned s_failMoves; /* bit n - 1: the nth move fails */
static int s_moves;

void ico_backup_test_fail_move(int nth)
{
    s_failMoves = nth > 0 && nth <= 32 ? 1u << (nth - 1) : 0u;
    s_moves = 0;
}

void ico_backup_test_fail_also(int nth)
{
    if (nth > 0 && nth <= 32) {
        s_failMoves |= 1u << (nth - 1);
    }
}

static int move_path(const char *from, const char *to)
{
    if (s_failMoves != 0 && ++s_moves <= 32 && (s_failMoves >> (s_moves - 1) & 1u) != 0) {
        errno = EBUSY;
        return -1;
    }
    return ico_rename_replace(from, to);
}

/* a thing not as it was after the report's left list: "<name> (yours is
   in <old copy's name>)", or with no old copy (old NULL: there was none)
   "<name> (the backup's; you had none)" */
static void left_add(IcoBackupReport *out, const char *name, const char *old)
{
    if (out == NULL) {
        return;
    }
    const char *base = old != NULL ? strrchr(old, '/') : NULL;
    const size_t have = strlen(out->left);
    if (old != NULL) {
        snprintf(out->left + have, sizeof(out->left) - have, "%s%s (yours is in %s)",
                 have > 0 ? ", " : "", name, base != NULL ? base + 1 : old);
    } else {
        snprintf(out->left + have, sizeof(out->left) - have, "%s%s (the backup's; you had none)",
                 have > 0 ? ", " : "", name);
    }
}

/* the old copies (<place>.old-<digits>...) beside the user data, named in
   out->stale */
static int stale_visit(const char *name, int kind, void *user)
{
    IcoBackupReport *out = (IcoBackupReport *)user;
    const char *o = strstr(name, ".old-");
    char base[64];
    (void)kind;
    if (o == NULL || o[5] < '0' || o[5] > '9' || (size_t)(o - name) >= sizeof(base)) {
        return 0;
    }
    memcpy(base, name, (size_t)(o - name));
    base[o - name] = '\0';
    if (!ico_backup_name_ok(base, 0) && !ico_backup_name_ok(base, 1)) {
        return 0;
    }
    const size_t have = strlen(out->stale);
    if (have + 2 + strlen(name) < sizeof(out->stale)) {
        snprintf(out->stale + have, sizeof(out->stale) - have, "%s%s", have > 0 ? ", " : "", name);
    }
    return 0;
}

typedef struct Items {
    char name[MAX_ITEMS][64];
    int n;
} Items;

static int item_visit(const char *name, int kind, void *user)
{
    Items *it = (Items *)user;
    if (strcmp(name, ICO_BACKUP_MANIFEST) == 0 || strlen(name) >= sizeof(it->name[0]) ||
        it->n >= MAX_ITEMS || !ico_backup_name_ok(name, kind != 0)) {
        return 0;
    }
    snprintf(it->name[it->n++], sizeof(it->name[0]), "%s", name);
    return 0;
}

static int exists(const char *path)
{
    return ico_path_kind(path, NULL, NULL) >= 0;
}

IcoBackupResult ico_backup_apply_pending(const char *user_dir, long long now, IcoBackupManifest *m,
                                         IcoBackupReport *out)
{
    IcoBackupManifest local;
    IcoBackupManifest *man = m != NULL ? m : &local;
    char part[PATH_LEN], staging[PATH_LEN], path[PATH_LEN];
    Items it;
    char target[MAX_ITEMS][PATH_LEN], old[MAX_ITEMS][PATH_LEN], src[MAX_ITEMS][PATH_LEN];
    int had[MAX_ITEMS], moved = 0;
    IcoBackupResult r = ICO_BACKUP_OK;

    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    memset(man, 0, sizeof(*man));
    if (user_dir == NULL || join(part, sizeof(part), user_dir, ICO_BACKUP_STAGING_PART) != 0 ||
        join(staging, sizeof(staging), user_dir, ICO_BACKUP_STAGING) != 0) {
        return ICO_BACKUP_NONE;
    }
    if (out != NULL) {
        ico_dir_entries(user_dir, 1, stale_visit, out);
    }
    if (exists(part)) {
        ico_remove_tree(part);
        if (out != NULL) {
            out->leftover = 1;
        }
    }
    if (ico_path_kind(staging, NULL, NULL) != 1) {
        return ICO_BACKUP_NONE;
    }
    {
        char text[ICO_BACKUP_MAX_MANIFEST + 256];
        FILE *f = join(path, sizeof(path), staging, ICO_BACKUP_MANIFEST) == 0
                      ? ico_fopen(path, "rb")
                      : NULL;
        const size_t n = f != NULL ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if (f != NULL) {
            fclose(f);
        }
        text[n] = '\0';
        if (n == 0 || parse_manifest(text, n, man) != ICO_BACKUP_OK) {
            say(out, "the waiting restore's %s cannot be read; it was deleted%s",
                ICO_BACKUP_MANIFEST, NULL);
            ico_remove_tree(staging);
            return ICO_BACKUP_DAMAGED;
        }
    }
    memset(&it, 0, sizeof(it));
    ico_dir_entries(staging, 1, item_visit, &it);
    for (int k = 0; k < it.n; k++) {
        if (join(target[k], sizeof(target[k]), user_dir, it.name[k]) != 0 ||
            join(src[k], sizeof(src[k]), staging, it.name[k]) != 0 ||
            snprintf(old[k], sizeof(old[k]), "%s.old-%lld", target[k], now) >=
                (int)sizeof(old[k])) {
            say(out, "the path of %s is too long%s", it.name[k], NULL);
            r = ICO_BACKUP_WRITE;
            break;
        }
        /* a name already taken (two restores in one second) gets a number */
        for (int u = 2; exists(old[k]) && u < 100; u++) {
            snprintf(old[k], sizeof(old[k]), "%s.old-%lld-%d", target[k], now, u);
        }
        had[k] = exists(target[k]);
        if (had[k] && move_path(target[k], old[k]) != 0) {
            say(out, "%s could not be moved aside: %s", it.name[k], strerror(errno));
            r = ICO_BACKUP_WRITE;
            break;
        }
        if (move_path(src[k], target[k]) != 0) {
            say(out, "the backup's %s could not be moved in: %s", it.name[k], strerror(errno));
            if (had[k] && move_path(old[k], target[k]) != 0) {
                left_add(out, it.name[k], old[k]); /* moved aside, not back */
            }
            r = ICO_BACKUP_WRITE;
            break;
        }
        moved = k + 1;
    }
    if (r != ICO_BACKUP_OK) {
        /* every earlier move undone, newest first; a thing whose move back
           fails keeps its old copy, named for the log and the box (a file
           moved back over the backup's own is as it was even when the
           backup's could not go back to the staging folder first) */
        for (int k = moved - 1; k >= 0; k--) {
            const int out_ok = move_path(target[k], src[k]) == 0;
            if (had[k] ? move_path(old[k], target[k]) != 0 : !out_ok) {
                left_add(out, it.name[k], had[k] ? old[k] : NULL);
            }
        }
        ico_remove_tree(staging);
        return r;
    }
    for (int k = 0; k < it.n; k++) {
        list_add(man->items, sizeof(man->items), it.name[k], strlen(it.name[k]));
        if (had[k]) {
            ico_remove_tree(old[k]);
        }
    }
    ico_remove_tree(staging);
    if (out != NULL) {
        out->files = man->files;
        out->bytes = man->bytes;
    }
    return ICO_BACKUP_OK;
}

void ico_backup_default_name(char *out, size_t size, long long t)
{
    const time_t tt = (time_t)t;
    struct tm lt;
    char day[16] = "";

#ifdef _WIN32
    if (localtime_s(&lt, &tt) == 0) {
#else
    if (localtime_r(&tt, &lt) != NULL) {
#endif
        strftime(day, sizeof(day), "%Y-%m-%d", &lt);
    }
    snprintf(out, size, "ico-pc-backup-%s.zip", day[0] != '\0' ? day : "data");
}
