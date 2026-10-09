/*
 * port/platform/gpu_driver.c
 *
 * Graphics driver packages (gpu_driver.h).
 */
#include "gpu_driver.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "json.h"
#include "miniz.h"

#define MARKER_NAME "starting"

static void say(char *why, size_t n, const char *fmt, ...)
{
    if (why == NULL || n == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, n, fmt, ap);
    va_end(ap);
}

/* --- meta.json ------------------------------------------------------------ */

static int isNameChar(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

int ico_gpu_driver_library_name_ok(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    const size_t len = strlen(name);
    if (len < 4 || len >= sizeof(((IcoGpuDriverMeta *)0)->libraryName) ||
        strcmp(name + len - 3, ".so") != 0 || strstr(name, "..") != NULL) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isNameChar((unsigned char)name[i])) {
            return 0;
        }
    }
    return 1;
}

int ico_gpu_driver_entry_name_ok(const char *name)
{
    if (name == NULL || name[0] == '\0' || name[0] == '.' || strlen(name) > 127 ||
        strstr(name, "..") != NULL) {
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p < 0x20 || *p == 0x7f) {
            return 0;
        }
    }
    return 1;
}

/* copies a string member into dst; a longer one is cut at a UTF-8 character
   when cut is set, else rejected.  0 (absent is fine: dst stays empty), or
   -1 when the member is not a string or does not fit */
static int copyField(const IcoJsonNode *root, const char *key, char *dst, size_t n, int cut)
{
    const IcoJsonNode *v = ico_json_get(root, key);
    dst[0] = '\0';
    if (v == NULL) {
        return 0;
    }
    const char *s = ico_json_str(v);
    if (s == NULL) {
        return -1;
    }
    size_t len = strlen(s); /* stops at an embedded "\u0000" */
    if (len >= n) {
        if (!cut) {
            return -1;
        }
        len = n - 1;
        while (len > 0 && ((unsigned char)s[len] & 0xC0u) == 0x80u) {
            len--;
        }
    }
    memcpy(dst, s, len);
    dst[len] = '\0';
    return 0;
}

static int intField(const IcoJsonNode *root, const char *key, int *out)
{
    const IcoJsonNode *v = ico_json_get(root, key);
    int64_t i = 0;
    *out = 0;
    if (v == NULL) {
        return 0;
    }
    if (ico_json_i64(v, &i) != 0 || i < 0 || i > 100000) {
        return -1;
    }
    *out = (int)i;
    return 0;
}

int ico_gpu_driver_parse_meta(const char *text, size_t n, IcoGpuDriverMeta *out, char *why,
                              size_t whyN)
{
    IcoJson j;

    memset(out, 0, sizeof(*out));
    if (text == NULL || ico_json_parse(text, n, &j) != 0) {
        say(why, whyN, "meta.json is not JSON");
        ico_json_free(&j);
        return ICO_GPU_DRIVER_BAD;
    }
    int r = ICO_GPU_DRIVER_BAD;
    const IcoJsonNode *root = j.root;
    if (root == NULL || root->type != ICO_JSON_OBJ) {
        say(why, whyN, "meta.json is not an object");
    } else if (intField(root, "schemaVersion", &out->schemaVersion) != 0 ||
               intField(root, "minApi", &out->minApi) != 0) {
        say(why, whyN, "meta.json: schemaVersion or minApi is not a whole number");
    } else if (copyField(root, "name", out->name, sizeof(out->name), 1) != 0 ||
               copyField(root, "description", out->description, sizeof(out->description), 1) != 0 ||
               copyField(root, "author", out->author, sizeof(out->author), 1) != 0 ||
               copyField(root, "packageVersion", out->packageVersion, sizeof(out->packageVersion),
                         1) != 0 ||
               copyField(root, "vendor", out->vendor, sizeof(out->vendor), 1) != 0) {
        say(why, whyN, "meta.json: a text field is not a string");
    } else if (copyField(root, "driverVersion", out->driverVersion, sizeof(out->driverVersion),
                         0) != 0) {
        say(why, whyN, "meta.json: driverVersion is not a short string");
    } else if (out->name[0] == '\0') {
        say(why, whyN, "meta.json has no name");
    } else if (ico_json_get(root, "libraryName") == NULL) {
        say(why, whyN, "meta.json has no libraryName");
    } else if (copyField(root, "libraryName", out->libraryName, sizeof(out->libraryName), 0) != 0 ||
               !ico_gpu_driver_library_name_ok(out->libraryName)) {
        say(why, whyN, "meta.json: libraryName is not a plain .so file name");
    } else {
        r = ICO_GPU_DRIVER_OK;
    }
    ico_json_free(&j);
    if (r != ICO_GPU_DRIVER_OK) {
        memset(out, 0, sizeof(*out));
    }
    return r;
}

/* --- names ----------------------------------------------------------------- */

/* appends s to out (at *at) made safe, at most max bytes of it */
static void appendSafe(char *out, size_t n, size_t *at, const char *s, size_t max)
{
    size_t added = 0;
    int lastUnderscore = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && added < max; p++) {
        char c = isNameChar(*p) ? (char)*p : '_';
        if (c == '_' && lastUnderscore) {
            continue;
        }
        lastUnderscore = c == '_';
        if (*at + 1 >= n) {
            break;
        }
        out[(*at)++] = c;
        added++;
    }
    out[*at] = '\0';
}

/* the root's own files (the marker, the picker's copy) */
static int reservedName(const char *s)
{
    return strcmp(s, MARKER_NAME) == 0 || strcmp(s, "incoming.zip") == 0;
}

void ico_gpu_driver_folder_name(const IcoGpuDriverMeta *m, char *out, size_t n)
{
    size_t at = 0;
    if (n == 0) {
        return;
    }
    out[0] = '\0';
    appendSafe(out, n, &at, m->name, 63);
    if (m->driverVersion[0] != '\0') {
        appendSafe(out, n, &at, "-", 1);
        appendSafe(out, n, &at, m->driverVersion, 63);
    }
    if (out[0] == '\0' || strcmp(out, "_") == 0) {
        snprintf(out, n, "driver");
        at = strlen(out);
    }
    if (out[0] == '.') {
        out[0] = '_';
    }
    /* never ".." inside, never the staging suffix, never the root's files */
    for (char *d = strstr(out, ".."); d != NULL; d = strstr(out, "..")) {
        d[1] = '_';
    }
    if ((at >= 4 && strcmp(out + at - 4, ".tmp") == 0) || reservedName(out)) {
        if (at + 1 < n) {
            out[at++] = '_';
            out[at] = '\0';
        } else {
            snprintf(out, n, "driver");
        }
    }
}

int ico_gpu_driver_folder_ok(const char *folder)
{
    if (folder == NULL || folder[0] == '\0' || folder[0] == '.' || strlen(folder) > 140 ||
        strstr(folder, "..") != NULL || reservedName(folder)) {
        return 0;
    }
    const size_t len = strlen(folder);
    if (len >= 4 && strcmp(folder + len - 4, ".tmp") == 0) {
        return 0;
    }
    for (const unsigned char *p = (const unsigned char *)folder; *p; p++) {
        if (!isNameChar(*p)) {
            return 0;
        }
    }
    return 1;
}

void ico_gpu_driver_display_name(const IcoGpuDriverMeta *m, char *out, size_t n)
{
    if (m->driverVersion[0] != '\0' && strstr(m->name, m->driverVersion) == NULL) {
        snprintf(out, n, "%s %s", m->name, m->driverVersion);
    } else {
        snprintf(out, n, "%s", m->name);
    }
}

/* --- files ------------------------------------------------------------------ */

static int joinPath(char *out, size_t n, const char *a, const char *b)
{
    const size_t la = strlen(a);
    const int slash = la > 0 && (a[la - 1] == '/' || a[la - 1] == '\\');
    const int w = snprintf(out, n, "%s%s%s", a, slash ? "" : "/", b);
    return w < 0 || (size_t)w >= n ? -1 : 0;
}

/* reads a file of at most cap bytes, NUL-terminated (free it); NULL when it
   is missing, larger or unreadable */
static char *readSmall(const char *path, size_t cap, size_t *len)
{
    unsigned long long size = 0;
    if (ico_path_kind(path, &size, NULL) != 0 || size > cap) {
        return NULL;
    }
    FILE *f = ico_fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    size_t got = buf != NULL ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (buf == NULL || got != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    if (len != NULL) {
        *len = got;
    }
    return buf;
}

static int removeFileCb(const char *path, const char *name, void *user)
{
    (void)name;
    int *failed = user;
    if (ico_remove(path) != 0) {
        (*failed)++;
    }
    return 0;
}

/* deletes the files of dir (a flat folder) and dir; 0 or -1 */
static int removeFlatDir(const char *dir)
{
    if (ico_path_kind(dir, NULL, NULL) != 1) {
        return ico_path_kind(dir, NULL, NULL) < 0 ? 0 : -1;
    }
    /* a pass that removed everything is followed by an empty walk, which
       ends the loop; a failure ends it at once; further passes only matter
       if files appear meanwhile */
    for (int pass = 0; pass < 4; pass++) {
        int failed = 0;
        const int n = ico_dir_walk(dir, 0, removeFileCb, &failed);
        if (n <= 0 || failed) {
            break;
        }
    }
    /* hidden files (a leading '.') are not walked; a package has none, but
       a folder left by hand may: rmdir then fails, which is reported */
    return ico_rmdir(dir) == 0 ? 0 : -1;
}

/* --- install ---------------------------------------------------------------- */

typedef struct Sink {
    FILE *f;
    uint64_t written, max;
    int err; /* errno of a failed write; -1 over the limit */
} Sink;

static size_t sinkWrite(void *opaque, mz_uint64 ofs, const void *buf, size_t n)
{
    Sink *s = opaque;
    if (ofs != s->written || s->written + n > s->max) {
        s->err = -1;
        return 0;
    }
    if (fwrite(buf, 1, n, s->f) != n) {
        s->err = errno != 0 ? errno : EIO;
        return 0;
    }
    s->written += n;
    return n;
}

int ico_gpu_driver_install_zip(const char *root, const char *zipPath, int apiLevel, char *folder,
                               size_t folderN, char *why, size_t whyN)
{
    return ico_gpu_driver_install_zip_max(root, zipPath, apiLevel, ICO_GPU_DRIVER_ENTRY_MAX, folder,
                                          folderN, why, whyN);
}

int ico_gpu_driver_install_zip_max(const char *root, const char *zipPath, int apiLevel,
                                   uint64_t entryMax, char *folder, size_t folderN, char *why,
                                   size_t whyN)
{
    mz_zip_archive zip;
    IcoGpuDriverMeta meta;
    char name[256], dirName[160], dir[1024], stage[1024], path[1200];
    int r = ICO_GPU_DRIVER_BAD;
    FILE *zf = NULL;
    void *metaText = NULL;
    int staged = 0;
    int metaIdx;
    char stageName[176];
    mz_uint count;

    if (folder != NULL && folderN > 0) {
        folder[0] = '\0';
    }
    say(why, whyN, "");
    memset(&zip, 0, sizeof(zip));
    zf = ico_fopen(zipPath, "rb");
    if (zf == NULL || !mz_zip_reader_init_cfile(&zip, zf, 0, 0)) {
        say(why, whyN, "not a zip file");
        if (zf != NULL) {
            fclose(zf);
        }
        return ICO_GPU_DRIVER_BAD;
    }
    count = mz_zip_reader_get_num_files(&zip);
    if (count == 0 || count > ICO_GPU_DRIVER_FILES_MAX) {
        say(why, whyN, "the zip holds %u files (1 to %d)", (unsigned)count,
            ICO_GPU_DRIVER_FILES_MAX);
        goto done;
    }
    /* every file checked before anything is written */
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        const mz_uint need = mz_zip_reader_get_filename(&zip, i, NULL, 0);
        if (need == 0 || need > sizeof(name) ||
            !mz_zip_reader_get_filename(&zip, i, name, sizeof(name)) ||
            !mz_zip_reader_file_stat(&zip, i, &st)) {
            say(why, whyN, "a file name in the zip cannot be read");
            goto done;
        }
        if (st.m_is_directory || !ico_gpu_driver_entry_name_ok(name)) {
            say(why, whyN, "the zip holds \"%s\", not a plain file name", name);
            goto done;
        }
        if (st.m_is_encrypted || !st.m_is_supported) {
            say(why, whyN, "\"%s\" in the zip is encrypted or packed in an unknown way", name);
            goto done;
        }
        if (st.m_uncomp_size > entryMax) {
            say(why, whyN, "\"%s\" in the zip is larger than %llu bytes", name,
                (unsigned long long)entryMax);
            goto done;
        }
        if (mz_zip_reader_locate_file(&zip, name, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE) != (int)i) {
            say(why, whyN, "the zip holds \"%s\" twice", name);
            goto done;
        }
    }
    metaIdx = mz_zip_reader_locate_file(&zip, "meta.json", NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (metaIdx < 0) {
        say(why, whyN, "the zip has no meta.json");
        goto done;
    }
    {
        mz_zip_archive_file_stat st;
        size_t len = 0;
        if (!mz_zip_reader_file_stat(&zip, (mz_uint)metaIdx, &st) ||
            st.m_uncomp_size > ICO_GPU_DRIVER_META_MAX) {
            say(why, whyN, "meta.json is too large");
            goto done;
        }
        metaText = mz_zip_reader_extract_to_heap(&zip, (mz_uint)metaIdx, &len, 0);
        if (metaText == NULL) {
            say(why, whyN, "meta.json cannot be unpacked");
            goto done;
        }
        if (ico_gpu_driver_parse_meta(metaText, len, &meta, why, whyN) != ICO_GPU_DRIVER_OK) {
            goto done;
        }
    }
    if (mz_zip_reader_locate_file(&zip, meta.libraryName, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE) < 0) {
        say(why, whyN, "the zip does not hold %s, the library meta.json names", meta.libraryName);
        goto done;
    }
    if (meta.minApi > apiLevel) {
        say(why, whyN, "the driver needs Android API level %d; this phone has %d", meta.minApi,
            apiLevel);
        goto done;
    }
    ico_gpu_driver_folder_name(&meta, dirName, sizeof(dirName));
    snprintf(stageName, sizeof(stageName), "%s.tmp", dirName);
    if (joinPath(dir, sizeof(dir), root, dirName) != 0 ||
        joinPath(stage, sizeof(stage), root, stageName) != 0) {
        say(why, whyN, "the driver folder's path is too long");
        r = ICO_GPU_DRIVER_IO;
        goto done;
    }
    if (ico_mkdir(root) != 0 && errno != EEXIST) {
        say(why, whyN, "cannot make the folder %s", root);
        r = ICO_GPU_DRIVER_IO;
        goto done;
    }
    removeFlatDir(stage); /* an install that stopped half way */
    if (ico_mkdir(stage) != 0) {
        say(why, whyN, "cannot make the folder %s", stage);
        r = ICO_GPU_DRIVER_IO;
        goto done;
    }
    staged = 1;
    for (mz_uint i = 0; i < count; i++) {
        /* the check loop above read every name, so this read succeeds */
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        if (joinPath(path, sizeof(path), stage, name) != 0) {
            say(why, whyN, "a path in the driver folder is too long");
            r = ICO_GPU_DRIVER_IO;
            goto done;
        }
        Sink s = {ico_fopen(path, "wb"), 0, entryMax, 0};
        if (s.f == NULL) {
            const int e = errno;
            say(why, whyN, "cannot write %s", path);
            r = e == ENOSPC ? ICO_GPU_DRIVER_NOSPACE : ICO_GPU_DRIVER_IO;
            goto done;
        }
        errno = 0;
        const int ok = mz_zip_reader_extract_to_callback(&zip, i, sinkWrite, &s, 0);
        int closeErr = 0;
        if (ok && s.err == 0 && ico_fsync(s.f) != 0) {
            closeErr = errno != 0 ? errno : EIO;
        }
        if (fclose(s.f) != 0 && closeErr == 0 && ok && s.err == 0) {
            closeErr = errno != 0 ? errno : EIO;
        }
        if (s.err == -1) {
            say(why, whyN, "\"%s\" in the zip is larger than it says, or than %llu bytes", name,
                (unsigned long long)entryMax);
            goto done; /* BAD */
        }
        if (s.err != 0 || closeErr != 0) {
            const int e = s.err != 0 ? s.err : closeErr;
            say(why, whyN, "cannot write %s: %s", path, strerror(e));
            r = e == ENOSPC ? ICO_GPU_DRIVER_NOSPACE : ICO_GPU_DRIVER_IO;
            goto done;
        }
        if (!ok) {
            say(why, whyN, "\"%s\" in the zip is damaged", name);
            goto done; /* BAD */
        }
    }
    if (removeFlatDir(dir) != 0) {
        say(why, whyN, "cannot replace the driver installed in %s", dir);
        r = ICO_GPU_DRIVER_IO;
        goto done;
    }
    if (ico_rename_replace(stage, dir) != 0) {
        say(why, whyN, "cannot move %s to %s", stage, dir);
        r = ICO_GPU_DRIVER_IO;
        goto done;
    }
    staged = 0;
    if (folder != NULL && folderN > 0) {
        snprintf(folder, folderN, "%s", dirName);
    }
    r = ICO_GPU_DRIVER_OK;
done:
    if (staged) {
        removeFlatDir(stage);
    }
    if (metaText != NULL) {
        mz_free(metaText);
    }
    mz_zip_reader_end(&zip);
    fclose(zf);
    return r;
}

/* --- listing ---------------------------------------------------------------- */

typedef struct ListCtx {
    const char *root;
    size_t rootLen;
    IcoGpuDriver *out;
    int max, n;
} ListCtx;

static int listCb(const char *path, const char *name, void *user)
{
    ListCtx *c = user;
    char folder[160], lib[1200];
    size_t len = 0;

    if (strcmp(name, "meta.json") != 0 || strncmp(path, c->root, c->rootLen) != 0) {
        return 0;
    }
    const char *rel = path + c->rootLen;
    while (*rel == '/') {
        rel++;
    }
    const char *slash = strchr(rel, '/');
    if (slash == NULL || (size_t)(slash - rel) >= sizeof(folder) || strcmp(slash + 1, name) != 0) {
        return 0; /* a meta.json in the root itself */
    }
    memcpy(folder, rel, (size_t)(slash - rel));
    folder[slash - rel] = '\0';
    if (!ico_gpu_driver_folder_ok(folder) || c->n >= c->max) {
        return 0;
    }
    char *text = readSmall(path, ICO_GPU_DRIVER_META_MAX, &len);
    IcoGpuDriver d;
    memset(&d, 0, sizeof(d));
    const int ok = text != NULL && ico_gpu_driver_parse_meta(text, len, &d.meta, NULL, 0) == 0;
    free(text);
    if (!ok) {
        return 0;
    }
    snprintf(lib, sizeof(lib), "%.*s%s", (int)(slash - path + 1), path, d.meta.libraryName);
    if (ico_path_kind(lib, NULL, NULL) != 0) {
        return 0;
    }
    snprintf(d.folder, sizeof(d.folder), "%s", folder);
    c->out[c->n++] = d;
    return 0;
}

static int byFolder(const void *a, const void *b)
{
    return strcmp(((const IcoGpuDriver *)a)->folder, ((const IcoGpuDriver *)b)->folder);
}

int ico_gpu_driver_list(const char *root, IcoGpuDriver *out, int max)
{
    ListCtx c = {root, strlen(root), out, max, 0};
    while (c.rootLen > 1 && (root[c.rootLen - 1] == '/' || root[c.rootLen - 1] == '\\')) {
        c.rootLen--;
    }
    if (max <= 0 || ico_path_kind(root, NULL, NULL) != 1) {
        return 0;
    }
    ico_dir_walk(root, 1, listCb, &c);
    qsort(out, (size_t)c.n, sizeof(*out), byFolder);
    return c.n;
}

int ico_gpu_driver_remove(const char *root, const char *folder)
{
    char dir[1024];
    if (!ico_gpu_driver_folder_ok(folder) || joinPath(dir, sizeof(dir), root, folder) != 0 ||
        ico_path_kind(dir, NULL, NULL) != 1) {
        return -1;
    }
    return removeFlatDir(dir);
}

/* --- the trial marker -------------------------------------------------------- */

int ico_gpu_driver_trial_begin(const char *root, const char *folder)
{
    char path[1024], tmp[1040];
    if (!ico_gpu_driver_folder_ok(folder) || joinPath(path, sizeof(path), root, MARKER_NAME) != 0) {
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = ico_fopen(tmp, "wb");
    if (f == NULL) {
        return -1;
    }
    const int ok = fputs(folder, f) >= 0 && ico_fsync(f) == 0;
    if (fclose(f) != 0 || !ok || ico_rename_replace(tmp, path) != 0) {
        ico_remove(tmp);
        return -1;
    }
    return 0;
}

void ico_gpu_driver_trial_ok(const char *root)
{
    char path[1024];
    if (joinPath(path, sizeof(path), root, MARKER_NAME) == 0) {
        ico_remove(path);
    }
}

int ico_gpu_driver_trial_marker(const char *root, char *out, size_t n)
{
    char path[1024];
    size_t len = 0;
    if (n > 0) {
        out[0] = '\0';
    }
    if (joinPath(path, sizeof(path), root, MARKER_NAME) != 0) {
        return 0;
    }
    char *text = readSmall(path, 4096, &len);
    if (text == NULL) {
        return 0;
    }
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ')) {
        text[--len] = '\0';
    }
    if (n > 0) {
        snprintf(out, n, "%s", text);
    }
    free(text);
    return 1;
}

int ico_gpu_driver_trial_crashed(const char *root, const char *folder)
{
    char marked[256];
    return folder != NULL && folder[0] != '\0' &&
           ico_gpu_driver_trial_marker(root, marked, sizeof(marked)) && strcmp(marked, folder) == 0;
}
