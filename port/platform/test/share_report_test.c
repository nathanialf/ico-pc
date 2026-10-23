/*
 * port/platform/test/share_report_test.c
 *
 * The rc builds' share menu's files (share_report.h), over a fake logs and
 * dumps folder under the build folder: the log first, the run before's when
 * it is there, the newest zip of dumps (by modification time, a loss zip
 * included, an .rddump left out), the newest presented picture, the newest
 * report zip of logs (not another file that starts with report-); a tie in
 * time goes to the greater name; missing folders and files are skipped;
 * the names line for the log.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include "host_fs.h"
#include "share_report.h"

static int fails;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static char s_logs[1024], s_dumps[1024];

/* dir/name with a few bytes, its modification time t (Unix seconds) */
static void put(const char *dir, const char *name, long long t)
{
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = ico_fopen(path, "wb");
    if (f == NULL) {
        CHECK(0, "cannot write %s", path);
        return;
    }
    fputs(name, f);
    fclose(f);
    struct utimbuf u;
    u.actime = (time_t)t;
    u.modtime = (time_t)t;
    CHECK(utime(path, &u) == 0, "cannot set the time of %s", path);
}

static const char *base(const char *p)
{
    const char *s = strrchr(p, '/');
    return s != NULL ? s + 1 : p;
}

static void expect(const IcoShareReport *r, const char *const *names, int n, const char *what)
{
    CHECK(r->count == n, "%s: %d files, %d expected", what, r->count, n);
    for (int i = 0; i < n && i < r->count; i++) {
        CHECK(strcmp(base(r->path[i]), names[i]) == 0, "%s: file %d is %s, %s expected", what, i,
              r->path[i], names[i]);
    }
}

int main(int argc, char **argv)
{
    char root[1000]; /* 24 under s_logs/s_dumps: gcc's format-truncation check */
    snprintf(root, sizeof(root), "%s/share_report_test", argc > 1 ? argv[1] : ".");
    snprintf(s_logs, sizeof(s_logs), "%s/logs", root);
    snprintf(s_dumps, sizeof(s_dumps), "%s/dumps", root);
    ico_mkdir(root);
    ico_mkdir(s_logs);
    ico_mkdir(s_dumps);
    /* a run before this one's files would change the answers */
    static const char *const kOld[] = {"ico-pc.log", "ico-pc-previous.log",
                                       "report-20261020-101010.zip", "report-20261021-101010.zip",
                                       "report-notes.txt"};
    for (unsigned i = 0; i < sizeof(kOld) / sizeof(kOld[0]); i++) {
        char p[1200];
        snprintf(p, sizeof(p), "%s/%s", s_logs, kOld[i]);
        ico_remove(p);
    }
    static const char *const kOldDumps[] = {
        "frame-20261023-100000-v1.zip",      "frame-20261023-110000-v2.zip",
        "loss-20261023-103000.zip",          "frame-20261023-100000-present.png",
        "frame-20261023-110000-present.png", "rd-00001.rddump",
        "frame-20261023-120000-v3.zip",      "frame-20261023-115959-v3.zip"};
    for (unsigned i = 0; i < sizeof(kOldDumps) / sizeof(kOldDumps[0]); i++) {
        char p[1200];
        snprintf(p, sizeof(p), "%s/%s", s_dumps, kOldDumps[i]);
        ico_remove(p);
    }

    IcoShareReport r;
    char names[512];

    /* nothing there */
    CHECK(ico_share_report_collect(s_logs, s_dumps, &r) == 0, "empty folders: %d files", r.count);
    ico_share_report_names(&r, names, sizeof(names));
    CHECK(strcmp(names, "none") == 0, "empty folders: names \"%s\"", names);

    put(s_logs, "ico-pc.log", 5000);
    put(s_logs, "report-20261020-101010.zip", 100);
    put(s_logs, "report-20261021-101010.zip", 200);
    put(s_logs, "report-notes.txt", 9000);
    put(s_dumps, "frame-20261023-100000-v1.zip", 1000);
    put(s_dumps, "frame-20261023-110000-v2.zip", 2000);
    put(s_dumps, "loss-20261023-103000.zip", 1500);
    put(s_dumps, "frame-20261023-100000-present.png", 1001);
    put(s_dumps, "frame-20261023-110000-present.png", 2001);
    put(s_dumps, "rd-00001.rddump", 9000);
    {
        static const char *const want[] = {"ico-pc.log", "frame-20261023-110000-v2.zip",
                                           "frame-20261023-110000-present.png",
                                           "report-20261021-101010.zip"};
        ico_share_report_collect(s_logs, s_dumps, &r);
        expect(&r, want, 4, "no run before");
        ico_share_report_names(&r, names, sizeof(names));
        CHECK(strcmp(names, "ico-pc.log, frame-20261023-110000-v2.zip, "
                            "frame-20261023-110000-present.png, report-20261021-101010.zip") == 0,
              "names \"%s\"", names);
    }

    /* the run before's log second; a newer loss zip wins; a tie in time
       goes to the greater name */
    put(s_logs, "ico-pc-previous.log", 4000);
    put(s_dumps, "loss-20261023-103000.zip", 3000);
    {
        static const char *const want[] = {
            "ico-pc.log", "ico-pc-previous.log", "loss-20261023-103000.zip",
            "frame-20261023-110000-present.png", "report-20261021-101010.zip"};
        ico_share_report_collect(s_logs, s_dumps, &r);
        expect(&r, want, 5, "with the run before");
    }
    put(s_dumps, "frame-20261023-115959-v3.zip", 3000);
    put(s_dumps, "frame-20261023-120000-v3.zip", 3000);
    {
        static const char *const want[] = {
            "ico-pc.log", "ico-pc-previous.log", "loss-20261023-103000.zip",
            "frame-20261023-110000-present.png", "report-20261021-101010.zip"};
        ico_share_report_collect(s_logs, s_dumps, &r);
        expect(&r, want, 5, "a tie: the greater name (loss- after frame-)");
    }

    /* no logs folder: the dumps alone; no dumps folder: the logs alone */
    {
        static const char *const want[] = {"loss-20261023-103000.zip",
                                           "frame-20261023-110000-present.png"};
        ico_share_report_collect(NULL, s_dumps, &r);
        expect(&r, want, 2, "no logs folder");
    }
    {
        char missing[1100];
        snprintf(missing, sizeof(missing), "%s/no-such-folder", root);
        static const char *const want[] = {"ico-pc.log", "ico-pc-previous.log",
                                           "report-20261021-101010.zip"};
        ico_share_report_collect(s_logs, missing, &r);
        expect(&r, want, 3, "a missing dumps folder");
    }

    /* a short names buffer is cut, never overrun */
    ico_share_report_collect(s_logs, s_dumps, &r);
    char shortNames[12];
    memset(shortNames, 'x', sizeof(shortNames));
    ico_share_report_names(&r, shortNames, sizeof(shortNames));
    CHECK(strlen(shortNames) < sizeof(shortNames), "a short buffer holds a cut line");

    if (fails) {
        printf("share_report_test: %d failures\n", fails);
        return 1;
    }
    printf("share_report_test: ok\n");
    return 0;
}
