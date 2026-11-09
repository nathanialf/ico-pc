/*
 * port/platform/test/report_test.c
 *
 * The bug report's zip (report.h ico_report_write): a stored loss dump
 * (the loss path writes its zip stored, dump_zip.c) is deflated when the
 * report takes it, so a report of issue 123's 28 MB stored loss zip fits
 * GitHub's 25 MB attachment limit; the slow-scene zip (already deflated)
 * is stored as it is; both read back byte for byte with miniz's reader;
 * ico_report_last_bytes is the zip's size (the log line names it).
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "miniz.h"
#include "report.h"

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

/* the entry called name: its method and sizes, 1 when its bytes are data */
static int entry(mz_zip_archive *z, const char *name, const unsigned char *data, size_t n,
                 mz_zip_archive_file_stat *st)
{
    const int i = mz_zip_reader_locate_file(z, name, NULL, 0);
    size_t got = 0;
    void *p;
    int same;

    if (i < 0 || !mz_zip_reader_file_stat(z, (mz_uint)i, st)) {
        return 0;
    }
    p = mz_zip_reader_extract_to_heap(z, (mz_uint)i, &got, 0);
    same = p != NULL && got == n && memcmp(p, data, n) == 0;
    mz_free(p);
    return same;
}

int main(int argc, char **argv)
{
    /* 256 KB of a stored dump's kind of bytes: repeated records */
    enum { LOSS_BYTES = 256 * 1024, SLOW_BYTES = 4096 };

    static unsigned char loss[LOSS_BYTES], slow[SLOW_BYTES];
    char dir[512], lossPath[600], slowPath[600], zipPath[700], name[160];
    mz_zip_archive z;
    mz_zip_archive_file_stat st;
    IcoReportInputs in;
    size_t i;

    if (argc < 2) {
        printf("usage: report_test <folder>\n");
        return 2;
    }
    snprintf(dir, sizeof dir, "%s/report_test_logs", argv[1]);
    (void)ico_remove_tree(dir);
    CHECK(ico_mkdir(dir) == 0);
    for (i = 0; i < LOSS_BYTES; i++) {
        loss[i] = (unsigned char)((i % 64) < 48 ? 0 : (i / 64) * 7);
    }
    for (i = 0; i < SLOW_BYTES; i++) {
        slow[i] = (unsigned char)(i * 13);
    }
    snprintf(lossPath, sizeof lossPath, "%s/loss-20261109-000000.zip", dir);
    snprintf(slowPath, sizeof slowPath, "%s/slow-20261109-000000.zip", dir);
    CHECK(put(lossPath, loss, sizeof loss) == 0);
    CHECK(put(slowPath, slow, sizeof slow) == 0);

    ico_report_set_paths(dir, dir, dir, NULL);
    memset(&in, 0, sizeof in);
    in.what = "the test";
    in.summary = "";
    in.disc = "";
    in.loss_zip = lossPath;
    in.slow_zip = slowPath;
    name[0] = '\0';
    CHECK(ico_report_write(&in, name, sizeof name) == 0);
    CHECK(strncmp(name, "report-", 7) == 0);
    snprintf(zipPath, sizeof zipPath, "%s/%s", dir, name);
    {
        /* the size the log line names is the file's */
        unsigned long long size = 0;

        CHECK(ico_path_kind(zipPath, &size, NULL) >= 0);
        CHECK(size != 0 && ico_report_last_bytes() == size);
    }

    memset(&z, 0, sizeof z);
    CHECK(mz_zip_reader_init_file(&z, zipPath, 0));
    /* the loss zip: deflated, well under its stored size */
    memset(&st, 0, sizeof st);
    CHECK(entry(&z, "loss-20261109-000000.zip", loss, sizeof loss, &st));
    CHECK(st.m_method == MZ_DEFLATED);
    CHECK(st.m_uncomp_size == LOSS_BYTES);
    CHECK(st.m_comp_size < LOSS_BYTES / 4);
    /* the slow-scene zip: stored as it is (its writer deflated it) */
    memset(&st, 0, sizeof st);
    CHECK(entry(&z, "slow-20261109-000000.zip", slow, sizeof slow, &st));
    CHECK(st.m_method == 0);
    CHECK(st.m_comp_size == SLOW_BYTES);
    mz_zip_reader_end(&z);

    (void)ico_remove_tree(dir);
    if (fails != 0) {
        printf("report_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("report_test: ok\n");
    return 0;
}
