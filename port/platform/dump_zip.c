/*
 * port/platform/dump_zip.c
 *
 * F12's zip (dump_zip.h), written with miniz at its default compression.
 */
#include "dump_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "miniz.h"

static void say(char *why, size_t why_size, const char *what, const char *detail)
{
    if (why != NULL && why_size > 0) {
        snprintf(why, why_size, "%s%s%s", what, detail != NULL ? ": " : "",
                 detail != NULL ? detail : "");
    }
}

/* the file's bytes in a malloc block (NULL when it cannot be read) */
static void *read_all(const char *path, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;

    if (f == NULL) {
        return NULL;
    }
    /* read until the end rather than trusting a size: the log is still
       growing and a character device or link has none */
    for (;;) {
        if (n == cap) {
            const size_t ncap = cap == 0 ? 65536 : cap * 2;
            unsigned char *nb = realloc(buf, ncap);

            if (nb == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
            cap = ncap;
        }
        const size_t got = fread(buf + n, 1, cap - n, f);

        n += got;
        if (got == 0) {
            break;
        }
    }
    const int bad = ferror(f);

    fclose(f);
    if (bad) {
        free(buf);
        return NULL;
    }
    *size = n;
    return buf;
}

static const char *base_name(const char *path)
{
    const char *b = path;

    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

/* the files paths[0..n) and, when it can be read, log_path, each under its
   own name, into zip_path; the loose files deleted after; as
   ico_frame_dump_zip's result (0, 1 or -1) */
static int files_zip(const char *zip_path, const char *const *paths, int n, const char *log_path,
                     char *why, size_t why_size)
{
    mz_zip_archive z;
    void *out = NULL;
    size_t out_size = 0, log_size = 0;
    void *data[ICO_LOSS_DUMP_FILES] = {NULL, NULL};
    size_t size[ICO_LOSS_DUMP_FILES] = {0, 0};
    void *log = log_path != NULL && log_path[0] != '\0' ? read_all(log_path, &log_size) : NULL;
    int r = -1, have = 1;

    if (n < 1 || n > ICO_LOSS_DUMP_FILES) {
        say(why, why_size, "no dump to add", NULL);
        free(log);
        return -1;
    }
    for (int i = 0; i < n && have; i++) {
        data[i] = read_all(paths[i], &size[i]);
        if (data[i] == NULL) {
            say(why, why_size, "cannot read the dump", paths[i]);
            have = 0;
        }
    }
    memset(&z, 0, sizeof(z));
    if (!have) {
        /* said above */
    } else if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        say(why, why_size, "cannot start the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
    } else {
        int ok = 1;

        for (int i = 0; i < n && ok; i++) {
            ok = mz_zip_writer_add_mem(&z, base_name(paths[i]), data[i], size[i],
                                       MZ_DEFAULT_LEVEL) != 0;
        }
        if (ok && log != NULL) {
            ok = mz_zip_writer_add_mem(&z, base_name(log_path), log, log_size, MZ_DEFAULT_LEVEL) !=
                 0;
        }
        if (!ok) {
            say(why, why_size, "cannot add to the archive",
                mz_zip_get_error_string(mz_zip_get_last_error(&z)));
            mz_zip_writer_end(&z);
        } else if (!mz_zip_writer_finalize_heap_archive(&z, &out, &out_size)) {
            say(why, why_size, "cannot finish the archive",
                mz_zip_get_error_string(mz_zip_get_last_error(&z)));
            mz_zip_writer_end(&z);
        } else {
            mz_zip_writer_end(&z);
            FILE *f = ico_fopen(zip_path, "wb");
            const int wrote = f != NULL && fwrite(out, 1, out_size, f) == out_size;

            if (f != NULL && fclose(f) != 0) {
                r = -1;
            } else if (wrote) {
                r = log != NULL ? 0 : 1;
            }
            if (r < 0) {
                say(why, why_size, "cannot write", zip_path);
                ico_remove(zip_path);
            } else {
                for (int i = 0; i < n; i++) {
                    ico_remove(paths[i]);
                }
            }
            mz_free(out);
        }
    }
    for (int i = 0; i < n; i++) {
        free(data[i]);
    }
    free(log);
    return r;
}

int ico_frame_dump_zip(const char *zip_path, const char *dump_path, const char *prev_path,
                       const char *log_path, char *why, size_t why_size)
{
    const char *paths[ICO_LOSS_DUMP_FILES] = {dump_path, prev_path};
    const int n = prev_path != NULL && prev_path[0] != '\0' ? 2 : 1;

    return files_zip(zip_path, paths, n, log_path, why, why_size);
}

int ico_loss_dump_zip(const char *zip_path, const char *lost_path, const char *before_path,
                      const char *log_path, char *why, size_t why_size)
{
    const char *paths[ICO_LOSS_DUMP_FILES] = {lost_path, before_path};
    const int n = before_path != NULL && before_path[0] != '\0' ? 2 : 1;

    return files_zip(zip_path, paths, n, log_path, why, why_size);
}

int ico_loss_dump_path(char *out, size_t size, const char *dir, const char *stamp)
{
    const size_t n = strlen(dir);
    const char *sep = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\') ? "" : "/";
    const int c = snprintf(out, size, "%s%sloss-%s.zip", dir, sep, stamp);

    return c < 0 || (size_t)c >= size ? -1 : 0;
}

/* ico_loss_dump_prune's walk: the loss zips' paths, in name order */
typedef struct PruneList {
    char (*paths)[1024];
    int count, cap;
} PruneList;

static int is_loss_zip(const char *name)
{
    const size_t n = strlen(name);

    return n > 9 && strncmp(name, "loss-", 5) == 0 && strcmp(name + n - 4, ".zip") == 0;
}

static int prune_visit(const char *path, const char *name, void *user)
{
    PruneList *l = (PruneList *)user;

    if (!is_loss_zip(name) || strlen(path) >= sizeof(l->paths[0])) {
        return 0;
    }
    if (l->count == l->cap) {
        const int ncap = l->cap ? l->cap * 2 : 16;
        char (*np)[1024] = realloc(l->paths, (size_t)ncap * sizeof(l->paths[0]));

        if (np == NULL) {
            return 1;
        }
        l->paths = np;
        l->cap = ncap;
    }
    memcpy(l->paths[l->count++], path, strlen(path) + 1);
    return 0;
}

int ico_loss_dump_prune(const char *dir, int keep)
{
    PruneList l = {NULL, 0, 0};
    int removed = 0;

    if (ico_dir_walk(dir, 0, prune_visit, &l) < 0) {
        free(l.paths);
        return -1;
    }
    /* the names hold the time (loss-YYYYMMDD-HHMMSS.zip): byte order is
       oldest first */
    for (int i = 0; i + (keep > 0 ? keep : 0) < l.count; i++) {
        removed += ico_remove(l.paths[i]) == 0;
    }
    free(l.paths);
    return removed;
}

void *ico_dump_zip_extract_nth(const char *zip_path, int nth, size_t *size, char *name,
                               size_t name_size)
{
    mz_zip_archive z;
    void *data = NULL, *zip = NULL;
    size_t zip_size = 0;

    zip = read_all(zip_path, &zip_size);
    if (zip == NULL) {
        return NULL;
    }
    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_mem(&z, zip, zip_size, 0)) {
        free(zip);
        return NULL;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&z);

    for (mz_uint i = 0; i < n && data == NULL; i++) {
        mz_zip_archive_file_stat st;

        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) {
            continue;
        }
        const size_t len = strlen(st.m_filename);

        if (len < 7 || strcmp(st.m_filename + len - 7, ".rddump") != 0 || nth-- > 0) {
            continue;
        }
        data = mz_zip_reader_extract_to_heap(&z, i, size, 0);
        if (data != NULL && name != NULL && name_size > 0) {
            snprintf(name, name_size, "%s", base_name(st.m_filename));
        }
    }
    mz_zip_reader_end(&z);
    free(zip);
    return data;
}

void *ico_dump_zip_extract(const char *zip_path, size_t *size, char *name, size_t name_size)
{
    return ico_dump_zip_extract_nth(zip_path, 0, size, name, name_size);
}

static const char *const kSweepName[ICO_SWEEP_COUNT] = {"4x3", "16x9", "32x9", "48x9", "60x9"};

const char *ico_sweep_aspect_name(int i)
{
    return i >= 0 && i < ICO_SWEEP_COUNT ? kSweepName[i] : "";
}

float ico_sweep_aspect_value(int i)
{
    static const float v[ICO_SWEEP_COUNT] = {4.0f / 3.0f, 16.0f / 9.0f, 32.0f / 9.0f, 48.0f / 9.0f,
                                             60.0f / 9.0f};

    return i >= 0 && i < ICO_SWEEP_COUNT ? v[i] : 0.0f;
}

int ico_sweep_zip(const char *zip_path, const char *prefix, char *why, size_t why_size)
{
    mz_zip_archive z;
    void *out = NULL;
    size_t out_size = 0;
    char path[2][4096], entry[2][32];
    int have[ICO_SWEEP_COUNT] = {0}, added = 0, r = -1;
    static const char *const kExt[2] = {"rddump", "png"};

    memset(&z, 0, sizeof(z));
    if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        say(why, why_size, "cannot start the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        return -1;
    }
    for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
        void *data[2] = {NULL, NULL};
        size_t size[2] = {0, 0};
        int ok = 1;

        for (int k = 0; k < 2; k++) {
            snprintf(path[k], sizeof(path[k]), "%s%s.%s", prefix, kSweepName[i], kExt[k]);
            snprintf(entry[k], sizeof(entry[k]), "%s.%s", kSweepName[i], kExt[k]);
            data[k] = read_all(path[k], &size[k]);
            ok = ok && data[k] != NULL;
        }
        for (int k = 0; ok && k < 2; k++) {
            ok = mz_zip_writer_add_mem(&z, entry[k], data[k], size[k], MZ_DEFAULT_LEVEL) != 0;
        }
        free(data[0]);
        free(data[1]);
        if (ok) {
            have[i] = 1;
            added++;
        } else if (data[0] != NULL || data[1] != NULL) {
            /* one file of the pair is missing: that aspect is left out (a
               member half added cannot be taken back, so a failed add ends
               the archive) */
            if (mz_zip_get_last_error(&z) != MZ_ZIP_NO_ERROR) {
                say(why, why_size, "cannot add to the archive",
                    mz_zip_get_error_string(mz_zip_get_last_error(&z)));
                mz_zip_writer_end(&z);
                return -1;
            }
        }
    }
    if (added == 0) {
        say(why, why_size, "no aspect has both its dump and its picture", prefix);
        mz_zip_writer_end(&z);
        return -1;
    }
    if (!mz_zip_writer_finalize_heap_archive(&z, &out, &out_size)) {
        say(why, why_size, "cannot finish the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        mz_zip_writer_end(&z);
        return -1;
    }
    mz_zip_writer_end(&z);
    FILE *f = ico_fopen(zip_path, "wb");
    const int wrote = f != NULL && fwrite(out, 1, out_size, f) == out_size;

    if (f != NULL && fclose(f) != 0) {
        r = -1;
    } else if (wrote) {
        r = added;
    }
    if (r < 0) {
        say(why, why_size, "cannot write", zip_path);
        ico_remove(zip_path);
    } else {
        for (int i = 0; i < ICO_SWEEP_COUNT; i++) {
            for (int k = 0; have[i] && k < 2; k++) {
                snprintf(path[k], sizeof(path[k]), "%s%s.%s", prefix, kSweepName[i], kExt[k]);
                ico_remove(path[k]);
            }
        }
    }
    mz_free(out);
    return r;
}
