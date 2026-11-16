/*
 * port/platform/test/report_test.c
 *
 * The bug report's zip (report.h ico_report_write): a stored loss dump
 * (the loss path writes its zip stored, dump_zip.c) is deflated when the
 * report takes it, so a report of issue 123's 28 MB stored loss zip fits
 * GitHub's 25 MB attachment limit; the slow-scene zip (already deflated)
 * is stored as it is; both read back byte for byte with miniz's reader;
 * ico_report_last_bytes is the zip's size (the log line names it); the
 * logs folder's last-run.txt and last-run-previous.txt (last_run.h, issue
 * 215) go in byte for byte, and a report without them has neither entry;
 * so do exit-info.txt and the newest exit trace (exit_info.h, issue 215),
 * with the verdict line in report.txt's head.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_fs.h"
#include "miniz.h"
#include "report.h"

static int fails;

/* exit_info.h's files and verdict, as exit_info_begin leaves them */
static const char kExitInfo[] = "ico-pc exit info: read at the start of 2026-11-16 20:56:01 UTC\n"
                                "the previous run ended: it was killed (signal 9, SIGKILL) ...\n";
static const char kTrace[] = "\x08\x01\x12\x04tomb"; /* a tombstone's protobuf bytes */
static const char kVerdict[] = "the previous run ended: it was killed (signal 9, SIGKILL) (...)";

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
    {
        /* last-run.txt and its previous run's copy, beside the log */
        static const char kRun[] = "ico-pc last run: this run's slots\n";
        static const char kPrev[] = "ico-pc last run: the run before's slots\n";
        char p[600];
        snprintf(p, sizeof p, "%s/last-run.txt", dir);
        CHECK(put(p, kRun, sizeof kRun - 1) == 0);
        snprintf(p, sizeof p, "%s/last-run-previous.txt", dir);
        CHECK(put(p, kPrev, sizeof kPrev - 1) == 0);
        /* how the run before ended: exit-info.txt and the newest trace */
        snprintf(p, sizeof p, "%s/exit-info.txt", dir);
        CHECK(put(p, kExitInfo, sizeof kExitInfo - 1) == 0);
        snprintf(p, sizeof p, "%s/exit-trace-1794862215000.pb", dir);
        CHECK(put(p, kTrace, sizeof kTrace - 1) == 0);
    }
    ico_report_set_exit_info(kVerdict, "exit-trace-1794862215000.pb");

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
    /* the last-run files, byte for byte */
    {
        static const char kRun[] = "ico-pc last run: this run's slots\n";
        static const char kPrev[] = "ico-pc last run: the run before's slots\n";
        CHECK(entry(&z, "last-run.txt", (const unsigned char *)kRun, sizeof kRun - 1, &st));
        CHECK(entry(&z, "last-run-previous.txt", (const unsigned char *)kPrev, sizeof kPrev - 1,
                    &st));
        CHECK(entry(&z, "exit-info.txt", (const unsigned char *)kExitInfo, sizeof kExitInfo - 1,
                    &st));
        CHECK(entry(&z, "exit-trace-1794862215000.pb", (const unsigned char *)kTrace,
                    sizeof kTrace - 1, &st));
    }
    {
        /* the verdict in report.txt's head, after the memory line */
        size_t got = 0;
        char *txt = mz_zip_reader_extract_file_to_heap(&z, "report.txt", &got, 0);
        CHECK(txt != NULL);
        if (txt != NULL) {
            char *t = malloc(got + 1);
            if (t != NULL) {
                memcpy(t, txt, got);
                t[got] = '\0';
                const char *mem = strstr(t, "\nmemory: ");
                const char *ver = strstr(t, "\nthe previous run ended: it was killed (signal 9");
                CHECK(mem != NULL && ver != NULL && mem < ver);
                free(t);
            }
            mz_free(txt);
        }
    }
    mz_zip_reader_end(&z);
    {
        /* the next report has neither once the files are gone */
        char p[600];
        snprintf(p, sizeof p, "%s/last-run.txt", dir);
        (void)ico_remove(p);
        snprintf(p, sizeof p, "%s/last-run-previous.txt", dir);
        (void)ico_remove(p);
        snprintf(p, sizeof p, "%s/exit-info.txt", dir);
        (void)ico_remove(p);
        /* the trace stays on disc: the report takes it only when named */
    }
    ico_report_set_exit_info("", "");

    /* the log's key lines: the picture-health report's line first (issue
       179: a reporter's zip says at its top that the picture came back
       unfinished), the summary after it */
    {
        static const char kLog[] =
            "ico_pc: summary: version test; Android arm64\n"
            "rhi_vk: device table: Adreno (TM) 740 (device 5143:43050a01; driver ID 8; Vulkan "
            "1.3.128; Android): 2 of the table's 27 rows: tile-vendors, android\n"
            "rhi_vk: device table: android (Vulkan on Android): a tile-based GPU\n"
            "rd: picture health: 3D pictures are coming back unfinished from the GPU: 16 of the "
            "last 16 (replay 164, frame 139); the picture is probably black\n"
            "window: something else\n";
        char logPath[600];
        snprintf(logPath, sizeof logPath, "%s/ico-pc.log", dir);
        CHECK(put(logPath, kLog, sizeof kLog - 1) == 0);
        (void)ico_remove(zipPath); /* the first report: a name in the same second */
        in.log_path = logPath;
        in.loss_zip = NULL;
        in.slow_zip = NULL;
        name[0] = '\0';
        CHECK(ico_report_write(&in, name, sizeof name) == 0);
        snprintf(zipPath, sizeof zipPath, "%s/%s", dir, name);
        memset(&z, 0, sizeof z);
        CHECK(mz_zip_reader_init_file(&z, zipPath, 0));
        CHECK(mz_zip_reader_locate_file(&z, "last-run.txt", NULL, 0) < 0);
        CHECK(mz_zip_reader_locate_file(&z, "last-run-previous.txt", NULL, 0) < 0);
        CHECK(mz_zip_reader_locate_file(&z, "exit-info.txt", NULL, 0) < 0);
        CHECK(mz_zip_reader_locate_file(&z, "exit-trace-1794862215000.pb", NULL, 0) < 0);
        size_t got = 0;
        char *txt = mz_zip_reader_extract_file_to_heap(&z, "report.txt", &got, 0);
        CHECK(txt != NULL);
        if (txt != NULL) {
            char *t = malloc(got + 1);
            if (t != NULL) {
                memcpy(t, txt, got);
                t[got] = '\0';
                const char *keys = strstr(t, "the log's key lines (the latest of each):");
                const char *mine = keys ? strstr(keys, "rd: picture health: 3D pictures are "
                                                       "coming back unfinished")
                                        : NULL;
                const char *sum = keys ? strstr(keys, "ico_pc: summary: version test") : NULL;
                CHECK(keys != NULL && mine != NULL && sum != NULL && mine < sum);
                /* the device table's header (the rows' ids, which the summary
                   counts alone) is a key line; a row's line is not */
                const char *table =
                    keys ? strstr(keys, "rhi_vk: device table: Adreno (TM) 740 (") : NULL;
                CHECK(table != NULL && sum != NULL && table > sum);
                CHECK(keys != NULL && strstr(keys, "device table: android (") == NULL);
                free(t);
            }
            mz_free(txt);
        }
        mz_zip_reader_end(&z);
    }

    (void)ico_remove_tree(dir);
    if (fails != 0) {
        printf("report_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("report_test: ok\n");
    return 0;
}
