/*
 * port/platform/test/dump_zip_test.c
 *
 * F12's zip (dump_zip.h): a fake .rddump and log written under the build
 * folder, zipped, and read back with miniz's reader: the entry names are
 * the files' own names, the sizes and bytes match, a missing log leaves the
 * dump alone in the zip, and a missing dump writes nothing.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <string.h>
#include "dump_zip.h"
#include "host_fs.h"
#include "miniz.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int put(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    const int ok = f != NULL && fwrite(data, 1, n, f) == n;

    if (f != NULL) {
        fclose(f);
    }
    return ok ? 0 : -1;
}

/* the entry called name in z: its index, or -1 */
static int find(mz_zip_archive *z, const char *name)
{
    return mz_zip_reader_locate_file(z, name, NULL, 0);
}

/* 1 when entry name holds exactly the n bytes of data */
static int entry_is(mz_zip_archive *z, const char *name, const void *data, size_t n)
{
    mz_zip_archive_file_stat st;
    char back[4096];
    const int i = find(z, name);

    if (i < 0 || !mz_zip_reader_file_stat(z, (mz_uint)i, &st) || st.m_uncomp_size != n ||
        n > sizeof(back)) {
        return 0;
    }
    return mz_zip_reader_extract_to_mem(z, (mz_uint)i, back, sizeof(back), 0) &&
           memcmp(back, data, n) == 0;
}

int main(int argc, char **argv)
{
    char dump[1200], log[1200], zip[1200], why[160];
    char rd[3000], text[] = "window: a log line\nwindow: another\n";
    mz_zip_archive z;

    if (argc < 2) {
        printf("dump_zip_test: needs a folder\n");
        return 2;
    }
    snprintf(dump, sizeof(dump), "%s/frame-x-v1.rddump", argv[1]);
    snprintf(log, sizeof(log), "%s/ico-pc.log", argv[1]);
    snprintf(zip, sizeof(zip), "%s/frame-x-v1.zip", argv[1]);
    for (size_t i = 0; i < sizeof(rd); i++) {
        rd[i] = (char)(i * 7 + (i >> 5));
    }
    CHECK(put(dump, rd, sizeof(rd)) == 0);
    CHECK(put(log, text, sizeof(text) - 1) == 0);

    /* both files, each under its own name, the dump's size and bytes kept */
    why[0] = '\0';
    CHECK(ico_frame_dump_zip(zip, dump, log, why, sizeof(why)) == 0);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 2);
    CHECK(entry_is(&z, "frame-x-v1.rddump", rd, sizeof(rd)));
    CHECK(entry_is(&z, "ico-pc.log", text, sizeof(text) - 1));
    mz_zip_reader_end(&z);

    /* an unreadable log: the dump alone, still a zip */
    snprintf(log, sizeof(log), "%s/no-such.log", argv[1]);
    CHECK(ico_frame_dump_zip(zip, dump, log, why, sizeof(why)) == 1);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 1);
    CHECK(entry_is(&z, "frame-x-v1.rddump", rd, sizeof(rd)));
    mz_zip_reader_end(&z);

    /* no log named (--console): the same */
    CHECK(ico_frame_dump_zip(zip, dump, NULL, why, sizeof(why)) == 1);

    /* no dump: refused with a reason, no new file */
    remove(zip);
    snprintf(dump, sizeof(dump), "%s/no-such.rddump", argv[1]);
    why[0] = '\0';
    CHECK(ico_frame_dump_zip(zip, dump, NULL, why, sizeof(why)) == -1);
    CHECK(why[0] != '\0');
    CHECK(ico_path_kind(zip, NULL, NULL) == -1);

    printf("dump_zip_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
