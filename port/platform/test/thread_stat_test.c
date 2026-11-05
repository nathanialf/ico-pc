/*
 * port/platform/test/thread_stat_test.c
 *
 * A thread's time on a core and waiting for one (thread_stat.h, issue
 * 179):
 *   parse   the kernel's "run wait slices" text, blanks, a buffer without
 *           a NUL read to its length only, two numbers or none refused;
 *   read    a fake /proc/self/task under the build folder (Linux): a
 *           thread's file read, a missing thread ENOENT, a malformed file
 *           EINVAL, tid 0 ENOSYS; the calling thread's own file under the
 *           real /proc read when there is one;
 *   track   the first read and a thread made again give no difference,
 *           the next read of one thread its difference, a count that went
 *           back 0;
 *   texts   the 10 s part word for word (three threads, one left out,
 *           none), the failure part, a cut buffer.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "thread_stat.h"

#if defined(__linux__)
#include <sys/stat.h>
#endif

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (got), (want));    \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void testParse(void)
{
    ThreadStat s = {7, 7, 7};
    static const char noNul[3] = {'1', ' ', '2'};

    CHECK(thread_stat_parse("3900000000 400000000 1234\n", 26, &s));
    CHECK(s.runNs == 3900000000ull && s.waitNs == 400000000ull && s.slices == 1234u);
    CHECK(thread_stat_parse("  1\t2 3", 7, &s) && s.runNs == 1 && s.waitNs == 2 && s.slices == 3);
    s.runNs = 99;
    CHECK(!thread_stat_parse("1 2\n", 4, &s) && s.runNs == 99); /* two numbers: kept as it was */
    CHECK(!thread_stat_parse("", 0, &s));
    CHECK(!thread_stat_parse("x 1 2", 5, &s));
    CHECK(!thread_stat_parse(noNul, sizeof(noNul), &s)); /* read to its length: two numbers */
}

#if defined(__linux__)
static void writeFile(const char *root, long tid, const char *text)
{
    char p[1024];
    FILE *f;

    snprintf(p, sizeof(p), "%s/%ld", root, tid);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/%ld/schedstat", root, tid);
    f = fopen(p, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void testRead(const char *dir)
{
    char root[768];
    ThreadStat s;

    snprintf(root, sizeof(root), "%s/thread_stat_task", dir);
    (void)mkdir(root, 0755);
    writeFile(root, 4242, "2100000000 1800000000 977\n");
    writeFile(root, 4243, "garbage\n");
    CHECK(thread_stat_read(root, 4242, &s) == 0);
    CHECK(s.runNs == 2100000000ull && s.waitNs == 1800000000ull && s.slices == 977u);
    CHECK(thread_stat_read(root, 4243, &s) == EINVAL);
    CHECK(thread_stat_read(root, 4244, &s) == ENOENT);
    CHECK(thread_stat_read(root, 0, &s) == ENOSYS);
    CHECK(thread_stat_tid() > 0);
    {
        /* the real file of this thread, where the kernel has it */
        const int e = thread_stat_read(NULL, thread_stat_tid(), &s);

        CHECK(e == 0 || e == ENOENT || e == EACCES);
    }
}
#endif

static void testTrack(void)
{
    ThreadStatTrack t;
    ThreadStat now = {1000, 200, 5}, d;

    memset(&t, 0, sizeof(t));
    CHECK(!thread_stat_track(&t, 77, &now, &d)); /* the first read */
    CHECK(d.runNs == 0 && d.waitNs == 0);
    now.runNs = 4000;
    now.waitNs = 700;
    now.slices = 9;
    CHECK(thread_stat_track(&t, 77, &now, &d));
    CHECK(d.runNs == 3000 && d.waitNs == 500 && d.slices == 4);
    now.runNs = 10;
    CHECK(!thread_stat_track(&t, 78, &now, &d)); /* made again: another id */
    now.runNs = 5;                               /* went back */
    CHECK(thread_stat_track(&t, 78, &now, &d) && d.runNs == 0 && d.waitNs == 0);
}

static void testText(void)
{
    static const char *const who[3] = {"game", "render", "submit"};
    const ThreadStat d[3] = {
        {3900000000ull, 400000000ull, 0}, {2100000000ull, 1800000000ull, 0}, {0, 0, 0}};
    bool have[3] = {true, true, true};
    char b[256];

    (void)thread_stat_text(b, sizeof(b), who, d, have, 3);
    CHECK_STR(b, "ran / waited for a core: game 3.9 s / 0.4 s, render 2.1 s / 1.8 s, submit 0.0 s "
                 "/ 0.0 s");
    have[2] = false;
    (void)thread_stat_text(b, sizeof(b), who, d, have, 3);
    CHECK_STR(b, "ran / waited for a core: game 3.9 s / 0.4 s, render 2.1 s / 1.8 s");
    have[0] = false;
    (void)thread_stat_text(b, sizeof(b), who, d, have, 3);
    CHECK_STR(b, "ran / waited for a core: render 2.1 s / 1.8 s");
    have[1] = false;
    CHECK(thread_stat_text(b, sizeof(b), who, d, have, 3) == 0 && b[0] == '\0');
    (void)thread_stat_fail_text(b, sizeof(b), EACCES);
    CHECK_STR(b, "ran / waited for a core: schedstat not readable (EACCES)");
    (void)thread_stat_fail_text(b, sizeof(b), 1234);
    CHECK_STR(b, "ran / waited for a core: schedstat not readable (errno 1234)");
    {
        char t[10];
        const size_t n = thread_stat_fail_text(t, sizeof(t), EACCES);

        CHECK(n == strlen(t) && n < sizeof(t));
        have[0] = true;
        CHECK(thread_stat_text(t, sizeof(t), who, d, have, 3) == strlen(t));
    }
}

int main(int argc, char **argv)
{
    testParse();
#if defined(__linux__)
    testRead(argc > 1 ? argv[1] : ".");
#else
    (void)argc;
    (void)argv;
#endif
    testTrack();
    testText();
    if (fails) {
        printf("thread_stat: %d failed\n", fails);
        return 1;
    }
    printf("thread_stat: ok\n");
    return 0;
}
