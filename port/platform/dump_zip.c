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

int ico_frame_dump_zip(const char *zip_path, const char *dump_path, const char *log_path, char *why,
                       size_t why_size)
{
    mz_zip_archive z;
    void *out = NULL;
    size_t out_size = 0, dump_size = 0, log_size = 0;
    void *dump = read_all(dump_path, &dump_size);
    void *log = log_path != NULL && log_path[0] != '\0' ? read_all(log_path, &log_size) : NULL;
    int r = -1;

    memset(&z, 0, sizeof(z));
    if (dump == NULL) {
        say(why, why_size, "cannot read the dump", dump_path);
    } else if (!mz_zip_writer_init_heap(&z, 0, 0)) {
        say(why, why_size, "cannot start the archive",
            mz_zip_get_error_string(mz_zip_get_last_error(&z)));
    } else {
        int ok =
            mz_zip_writer_add_mem(&z, base_name(dump_path), dump, dump_size, MZ_DEFAULT_LEVEL) != 0;

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
            }
            mz_free(out);
        }
    }
    free(dump);
    free(log);
    return r;
}
