/*
 * port/platform/test/dump_auto_test.c
 *
 * The automatic dumps' worker (dump_auto.h, issues 123 and 179), the zip
 * from memory and the bug report's bundle (dump_zip.h):
 *   mem_zip    ico_zip_mem_files: the entries and the log, read back; a
 *              missing log leaves the entries alone (1); nothing deleted;
 *   whole      a job is written whole by the worker: the zip holds the
 *              frame, the tick before and the log byte for byte, the
 *              sidecar, the result carries both costs, the newest slow zips
 *              are kept (ICO_SLOW_DUMP_KEEP);
 *   busy       a second job offered while one is written is dropped (-1)
 *              with its line, counted, not queued: the first finishes whole
 *              and the dropped one's blocks stay the caller's; after the
 *              result is polled a new job is taken;
 *   failure    a zip that cannot be written keeps the frame loose and
 *              reports -1 with the reason;
 *   shutdown   the worker joins with a job in flight (its zip is written),
 *              twice is safe, and a later job starts a new worker;
 *   bundle     ico_dump_auto_bundle: of five recorded zips the newest three
 *              go into the report zip, stored, with a note naming each; a
 *              size cap leaves one out and the note says so; none recorded
 *              adds nothing; the report zip's own entries are kept.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump_auto.h"
#include "dump_zip.h"
#include "host_fs.h"
#include "ico_thread.h"
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
    FILE *f = ico_fopen(path, "wb");
    const int ok = f != NULL && fwrite(data, 1, n, f) == n;

    if (f != NULL) {
        fclose(f);
    }
    return ok ? 0 : -1;
}

static int entry_is(mz_zip_archive *z, const char *name, const void *data, size_t n)
{
    mz_zip_archive_file_stat st;
    char *back = malloc(n + 1);
    const int i = mz_zip_reader_locate_file(z, name, NULL, 0);
    int ok = 0;

    if (back != NULL && i >= 0 && mz_zip_reader_file_stat(z, (mz_uint)i, &st) &&
        st.m_uncomp_size == n) {
        ok = mz_zip_reader_extract_to_mem(z, (mz_uint)i, back, n + 1, 0) &&
             memcmp(back, data, n) == 0;
    }
    free(back);
    return ok;
}

static void fill(IcoDumpJob *j, const char *dir, const char *stamp, const char *dump, size_t dlen)
{
    memset(j, 0, sizeof(*j));
    snprintf(j->tag, sizeof(j->tag), "slow-scene dump");
    snprintf(j->zip, sizeof(j->zip), "%s/slow-%s.zip", dir, stamp);
    snprintf(j->dumpName, sizeof(j->dumpName), "%s/slow-%s.rddump", dir, stamp);
    snprintf(j->text, sizeof(j->text), "text %s", stamp);
    j->dump = malloc(dlen);
    memcpy(j->dump, dump, dlen);
    j->dumpLen = dlen;
    j->pruneSlow = 1;
    j->gameMs = 2.5;
}

static void wait_done(IcoDumpDone *d)
{
    for (int i = 0; i < 5000 && !ico_dump_auto_poll(d); i++) {
        ico_thread_now_ms();
        for (volatile int k = 0; k < 200000; k++) {}
    }
}

static void test_mem_zip(const char *base)
{
    char dir[1200], zip[1300], log[1300], why[160];
    const char a[] = "frame bytes", b[] = "tick before", text[] = "log text\n";
    IcoZipMem e[2] = {{"f.rddump", a, sizeof(a)}, {"f-prev.rddump", b, sizeof(b)}};
    mz_zip_archive z;

    snprintf(dir, sizeof(dir), "%s/dump-auto-mem", base);
    ico_mkdir(dir);
    snprintf(zip, sizeof(zip), "%s/m.zip", dir);
    snprintf(log, sizeof(log), "%s/ico-pc.log", dir);
    CHECK(put(log, text, sizeof(text) - 1) == 0);
    CHECK(ico_zip_mem_files(zip, e, 2, log, 0, why, sizeof(why)) == 0);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, zip, 0));
    CHECK(mz_zip_reader_get_num_files(&z) == 3);
    CHECK(entry_is(&z, "f.rddump", a, sizeof(a)) && entry_is(&z, "f-prev.rddump", b, sizeof(b)));
    CHECK(entry_is(&z, "ico-pc.log", text, sizeof(text) - 1));
    mz_zip_reader_end(&z);
    CHECK(ico_path_kind(log, NULL, NULL) == 0); /* nothing deleted */
    CHECK(ico_zip_mem_files(zip, e, 1, "/nonexistent/ico-pc.log", 1, why, sizeof(why)) == 1);
    CHECK(ico_zip_mem_files(zip, e, 0, NULL, 0, why, sizeof(why)) == -1);
    CHECK(ico_zip_mem_files(zip, e, ICO_ZIP_MEM_MAX + 1, NULL, 0, why, sizeof(why)) == -1);
}

static void test_job(const char *base)
{
    char dir[1200], log[1300], side[1300], why[160];
    const char dump[] = "the slow frame", prev[] = "the tick before", text[] = "the log\n";
    IcoDumpJob j;
    IcoDumpDone d;
    mz_zip_archive z;

    snprintf(dir, sizeof(dir), "%s/dump-auto-job", base);
    ico_mkdir(dir);
    snprintf(log, sizeof(log), "%s/ico-pc.log", dir);
    snprintf(side, sizeof(side), "%s/skin-sidecar.txt", dir);
    CHECK(put(log, text, sizeof(text) - 1) == 0);
    CHECK(put(side, "sidecar", 7) == 0);
    /* four older slow zips: the prune keeps ICO_SLOW_DUMP_KEEP in all */
    for (int i = 1; i <= 4; i++) {
        char old[1300];

        snprintf(old, sizeof(old), "%s/slow-2026110%d-000000.zip", dir, i);
        CHECK(put(old, "x", 1) == 0);
    }
    CHECK(!ico_dump_auto_busy());
    fill(&j, dir, "20261110-100000", dump, sizeof(dump));
    j.prev = malloc(sizeof(prev));
    memcpy(j.prev, prev, sizeof(prev));
    j.prevLen = sizeof(prev);
    snprintf(j.prevName, sizeof(j.prevName), "%s/slow-20261110-100000-prev.rddump", dir);
    snprintf(j.logPath, sizeof(j.logPath), "%s", log);
    snprintf(j.sidecar, sizeof(j.sidecar), "%s", side);
    CHECK(ico_dump_auto_submit(&j) == 0);
    memset(&d, 0, sizeof(d));
    wait_done(&d);
    CHECK(d.result == 0 && d.workerMs >= 0.0 && d.gameMs == 2.5 && d.bytes == sizeof(dump));
    CHECK(strcmp(d.text, "text 20261110-100000") == 0 && strcmp(d.tag, "slow-scene dump") == 0);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, d.zip, 0));
    CHECK(entry_is(&z, "slow-20261110-100000.rddump", dump, sizeof(dump)));
    CHECK(entry_is(&z, "slow-20261110-100000-prev.rddump", prev, sizeof(prev)));
    CHECK(entry_is(&z, "ico-pc.log", text, sizeof(text) - 1));
    CHECK(entry_is(&z, "skin-sidecar.txt", "sidecar", 7));
    CHECK(mz_zip_reader_locate_file(&z, "slow-20261110-100000.rddump", NULL, 0) == 0);
    mz_zip_reader_end(&z);
    /* the prune: 5 slow zips written or older, 3 kept */
    {
        int left = 0;

        for (int i = 1; i <= 4; i++) {
            char old[1300];

            snprintf(old, sizeof(old), "%s/slow-2026110%d-000000.zip", dir, i);
            left += ico_path_kind(old, NULL, NULL) == 0;
        }
        CHECK(left == 2);
        CHECK(ico_path_kind(d.zip, NULL, NULL) == 0);
    }
    CHECK(!ico_dump_auto_busy()); /* the result was polled */
    CHECK(!ico_dump_auto_poll(&d));

    /* a zip that cannot be written: the frame is kept loose */
    fill(&j, dir, "20261110-100100", dump, sizeof(dump));
    snprintf(j.zip, sizeof(j.zip), "%s/no-such-folder/x.zip", dir);
    CHECK(ico_dump_auto_submit(&j) == 0);
    wait_done(&d);
    CHECK(d.result == -1 && d.why[0] != '\0');
    CHECK(ico_path_kind(j.dumpName, NULL, NULL) == 0);
    (void)why;
}

static void test_busy(const char *base)
{
    char dir[1200];
    IcoDumpJob a, b;
    IcoDumpDone d;
    const unsigned drops = ico_dump_auto_dropped();

    snprintf(dir, sizeof(dir), "%s/dump-auto-busy", base);
    ico_mkdir(dir);
    ico_dump_auto_test_gate(1); /* the worker holds the job it takes */
    fill(&a, dir, "20261110-110000", "first", 6);
    fill(&b, dir, "20261110-110001", "second", 7);
    CHECK(ico_dump_auto_submit(&a) == 0);
    CHECK(ico_dump_auto_busy());
    CHECK(ico_dump_auto_submit(&b) == -1); /* dropped, not queued */
    CHECK(ico_dump_auto_dropped() == drops + 1);
    free(b.dump); /* the dropped job's block is the caller's */
    ico_dump_auto_test_gate(0);
    wait_done(&d);
    CHECK(d.result >= 0 && strstr(d.zip, "20261110-110000") != NULL);
    CHECK(ico_path_kind(b.zip, NULL, NULL) == -1); /* the second was never written */
    /* after the result is polled a new job is taken */
    fill(&b, dir, "20261110-110002", "third", 6);
    CHECK(ico_dump_auto_submit(&b) == 0);
    wait_done(&d);
    CHECK(d.result >= 0 && strstr(d.zip, "20261110-110002") != NULL);
}

static void test_shutdown(const char *base)
{
    char dir[1200];
    IcoDumpJob a;
    IcoDumpDone d;

    snprintf(dir, sizeof(dir), "%s/dump-auto-stop", base);
    ico_mkdir(dir);
    ico_dump_auto_test_gate(1);
    fill(&a, dir, "20261110-120000", "in flight", 10);
    CHECK(ico_dump_auto_submit(&a) == 0);
    ico_dump_auto_shutdown(); /* opens the gate, joins after the job */
    CHECK(ico_path_kind(a.zip, NULL, NULL) == 0);
    CHECK(ico_dump_auto_poll(&d) == 1 && d.result >= 0); /* the last result */
    ico_dump_auto_shutdown();                            /* twice is safe */
    fill(&a, dir, "20261110-120001", "again", 6);
    CHECK(ico_dump_auto_submit(&a) == 0); /* a new worker */
    wait_done(&d);
    CHECK(d.result >= 0);
    ico_dump_auto_shutdown();
}

static void test_bundle(const char *base)
{
    char dir[1200], rep[1300], line[400], p[6][1300];
    mz_zip_archive z;
    const char keep[] = "the report's own entry";
    IcoZipMem e = {"report.txt", keep, sizeof(keep)};
    char why[160];
    char big[3000];

    snprintf(dir, sizeof(dir), "%s/dump-auto-bundle", base);
    ico_mkdir(dir);
    snprintf(rep, sizeof(rep), "%s/report.zip", dir);
    CHECK(ico_zip_mem_files(rep, &e, 1, NULL, 0, why, sizeof(why)) == 1);
    ico_dump_auto_forget();
    CHECK(ico_dump_auto_bundle(rep, line, sizeof(line)) == 0 && line[0] != '\0');
    memset(big, 'z', sizeof(big));
    for (int i = 0; i < 5; i++) {
        snprintf(p[i], sizeof(p[i]), "%s/slow-2026111%d-000000.zip", dir, i);
        CHECK(put(p[i], big, 100 + (size_t)i) == 0);
        ico_dump_auto_record(p[i]);
    }
    snprintf(p[5], sizeof(p[5]), "%s/slow-gone.zip", dir);
    ico_dump_auto_record(p[5]); /* recorded, no longer on disk */
    CHECK(ico_dump_auto_bundle(rep, line, sizeof(line)) == ICO_BUNDLE_MAX_FILES);
    printf("bundle: %s\n", line);
    memset(&z, 0, sizeof(z));
    CHECK(mz_zip_reader_init_file(&z, rep, 0));
    CHECK(entry_is(&z, "report.txt", keep, sizeof(keep)));
    CHECK(mz_zip_reader_locate_file(&z, "slow-20261114-000000.zip", NULL, 0) >= 0);
    CHECK(mz_zip_reader_locate_file(&z, "slow-20261113-000000.zip", NULL, 0) >= 0);
    CHECK(mz_zip_reader_locate_file(&z, "slow-20261112-000000.zip", NULL, 0) >= 0);
    CHECK(mz_zip_reader_locate_file(&z, "slow-20261111-000000.zip", NULL, 0) < 0);
    {
        const int i = mz_zip_reader_locate_file(&z, "slow-20261114-000000.zip", NULL, 0);
        mz_zip_archive_file_stat st;

        CHECK(i >= 0 && mz_zip_reader_file_stat(&z, (mz_uint)i, &st) && st.m_method == 0);
        CHECK(i >= 0 && st.m_uncomp_size == 104);
    }
    {
        const int i = mz_zip_reader_locate_file(&z, "automatic-captures.txt", NULL, 0);
        char note[2048] = "";

        CHECK(i >= 0 && mz_zip_reader_extract_to_mem(&z, (mz_uint)i, note, sizeof(note) - 1, 0));
        CHECK(strstr(note, "slow-20261114-000000.zip: 104 bytes, included") != NULL);
        CHECK(
            strstr(note, "slow-20261111-000000.zip: 101 bytes, left out: only the newest three") !=
            NULL);
        CHECK(strstr(note, "slow-20261110-000000.zip: 100 bytes, left out") != NULL);
        CHECK(strstr(note, "slow-gone") == NULL); /* not on disk: not listed */
    }
    mz_zip_reader_end(&z);
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : ".";

    test_mem_zip(base);
    test_job(base);
    test_busy(base);
    test_shutdown(base);
    test_bundle(base);
    printf("dump_auto_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
