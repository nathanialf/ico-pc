/*
 * port/platform/test/cpu_place_test.c
 *
 * Which core a thread runs on (cpu_place.h):
 *   the histogram: notes counted by core, a core out of range and -1 as
 *     not known, the most-run core (the lowest on a tie), the take empties
 *     it, the share on a mask;
 *   the speeds (Linux and Android): a fake sysfs under the build folder,
 *     an 8-core phone (0-3 1.8 GHz, 4-6 2.6 GHz, 7 3.1 GHz) with core 5's
 *     file missing (offline) and a core past the last; the text, the
 *     slowest and the faster masks; one speed for all gives no masks;
 *   the split (cpu_place_split): rc9's phone (0-1 2.2 GHz, 2-6 3.1 GHz, 7
 *     3.8 GHz) gives the render thread core 7 alone and the submit thread
 *     2-6; an 8-core 4+3+1 phone 7 and 4-6; no split with two fastest
 *     cores, with the fastest the only one faster than the slowest, with
 *     one speed, or with none known;
 *   the masks' text: runs, single cores, none;
 *   the 10 s part word for word: three threads, a thread with no samples,
 *     one with no core known, the slowest-core shares, S left out;
 *   the calling thread: cpu_place_now inside the allowed cores, the
 *     priority text's shape, a pin to an empty mask refused, a pin to the
 *     cores already allowed taken (Linux).
 */
#include <stdio.h>
#include <string.h>
#include "cpu_place.h"

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

static void testHist(void)
{
    CpuPlaceHist h, t;
    uint32_t n = 99;

    memset(&h, 0, sizeof(h));
    CHECK(cpu_place_most(&h, &n) == -1 && n == 0);
    for (int i = 0; i < 6; i++) {
        cpu_place_note(&h, 7);
    }
    for (int i = 0; i < 3; i++) {
        cpu_place_note(&h, 2);
    }
    cpu_place_note(&h, -1);
    cpu_place_note(&h, CPU_PLACE_MAX); /* past the bins: not known */
    CHECK(cpu_place_most(&h, &n) == 7 && n == 6);
    CHECK(cpu_place_total(&h) == 11);
    CHECK(cpu_place_in(&h, 0x0f) == 3);
    CHECK(cpu_place_in(&h, 0xf0) == 6);
    cpu_place_note(&h, 2);
    cpu_place_note(&h, 2);
    cpu_place_note(&h, 2);
    CHECK(cpu_place_most(&h, &n) == 2 && n == 6); /* the tie: the lowest */
    cpu_place_take(&h, &t);
    CHECK(cpu_place_total(&h) == 0);
    CHECK(cpu_place_total(&t) == 14 && t.unknown == 2);
}

#if defined(__linux__)
static void writeSpeed(const char *root, int cpu, const char *text)
{
    char p[1024];
    FILE *f;

    snprintf(p, sizeof(p), "%s/cpu%d", root, cpu);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/cpu%d/cpufreq", root, cpu);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/cpu%d/cpufreq/cpuinfo_max_freq", root, cpu);
    f = fopen(p, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void testClasses(const char *dir)
{
    char root[768], s[256];
    CpuClasses c;
    static const char *const speeds[8] = {"1800000\n", "1800000\n", "1800000\n", "1800000\n",
                                          "2600000\n", NULL,        "2600000\n", "3100000\n"};

    snprintf(root, sizeof(root), "%s/cpu_place_sys", dir);
    (void)mkdir(root, 0755);
    for (int i = 0; i < 8; i++) {
        if (speeds[i] != NULL) {
            writeSpeed(root, i, speeds[i]);
        } else {
            char p[1024];

            snprintf(p, sizeof(p), "%s/cpu%d/cpufreq/cpuinfo_max_freq", root, i);
            (void)remove(p); /* an offline core: no file */
        }
    }
    writeSpeed(root, 9, "garbage\n"); /* unreadable: not counted */
    CHECK(cpu_place_classes_read(root, &c) == 8);
    CHECK(c.khz[0] == 1800000u && c.khz[5] == 0u && c.khz[7] == 3100000u);
    (void)cpu_place_classes_text(&c, s, sizeof(s));
    CHECK_STR(s, "0-3 1.8 GHz, 4 2.6 GHz, 5 speed not known, 6 2.6 GHz, 7 3.1 GHz");
    CHECK(cpu_place_slow_mask(&c) == 0x0fu);
    CHECK(cpu_place_fast_mask(&c) == 0xd0u);
    /* core 5 online again */
    writeSpeed(root, 5, "2600000\n");
    CHECK(cpu_place_classes_read(root, &c) == 8);
    (void)cpu_place_classes_text(&c, s, sizeof(s));
    CHECK_STR(s, "0-3 1.8 GHz, 4-6 2.6 GHz, 7 3.1 GHz");
    CHECK(cpu_place_fast_mask(&c) == 0xf0u);
    /* one speed for all: nothing slow, nothing faster */
    for (int i = 0; i < 8; i++) {
        writeSpeed(root, i, "2000000\n");
    }
    CHECK(cpu_place_classes_read(root, &c) == 8);
    CHECK(cpu_place_slow_mask(&c) == 0 && cpu_place_fast_mask(&c) == 0);
    (void)cpu_place_classes_text(&c, s, sizeof(s));
    CHECK_STR(s, "0-7 2.0 GHz");
    /* nothing there */
    snprintf(root, sizeof(root), "%s/cpu_place_none", dir);
    CHECK(cpu_place_classes_read(root, &c) == 0);
    (void)cpu_place_classes_text(&c, s, sizeof(s));
    CHECK_STR(s, "not known");
}
#endif

/* the classes from speeds in MHz (0: not known) */
static void classesOf(CpuClasses *c, const unsigned *mhz, int n)
{
    memset(c, 0, sizeof(*c));
    c->count = n;
    for (int i = 0; i < n; i++) {
        c->khz[i] = mhz[i] * 1000u;
    }
}

static void testSplit(void)
{
    static const unsigned rc9[] = {2200, 2200, 3100, 3100, 3100, 3100, 3100, 3800};
    static const unsigned phone[] = {1800, 1800, 1800, 1800, 2600, 2600, 2600, 3100};
    static const unsigned twoTop[] = {1800, 1800, 1800, 1800, 2600, 2600, 3100, 3100};
    static const unsigned oneFast[] = {1800, 1800, 1800, 1800, 1800, 1800, 1800, 3100};
    static const unsigned flat[] = {2000, 2000, 2000, 2000};
    static const unsigned unknownTop[] = {1800, 1800, 2600, 2600, 0, 3100, 2600, 2600};
    CpuClasses c;
    uint64_t r, s;
    char t[64];

    classesOf(&c, rc9, 8);
    CHECK(cpu_place_split(&c, &r, &s) && r == 0x80u && s == 0x7cu);
    (void)cpu_place_mask_text(s, t, sizeof(t));
    CHECK_STR(t, "2-6");
    classesOf(&c, phone, 8);
    CHECK(cpu_place_split(&c, &r, &s) && r == 0x80u && s == 0x70u);
    /* a core whose speed is not known is in neither */
    classesOf(&c, unknownTop, 8);
    CHECK(cpu_place_split(&c, &r, &s) && r == 0x20u && s == 0xccu);
    /* two fastest cores: both threads on the faster cores, as before */
    classesOf(&c, twoTop, 8);
    CHECK(!cpu_place_split(&c, &r, &s) && r == 0xf0u && s == 0xf0u);
    /* the fastest the only one faster than the slowest: not the submit
       thread on the slowest, not both on one core: as before */
    classesOf(&c, oneFast, 8);
    CHECK(!cpu_place_split(&c, &r, &s) && r == 0x80u && s == 0x80u);
    classesOf(&c, flat, 4);
    CHECK(!cpu_place_split(&c, &r, &s) && r == 0 && s == 0);
    memset(&c, 0, sizeof(c));
    CHECK(!cpu_place_split(&c, &r, &s) && r == 0 && s == 0);
}

static void testText(void)
{
    char s[256];
    CpuPlaceHist g, r, st, none, unk;

    (void)cpu_place_mask_text(0x0f, s, sizeof(s));
    CHECK_STR(s, "0-3");
    (void)cpu_place_mask_text(0x8d, s, sizeof(s));
    CHECK_STR(s, "0, 2-3, 7");
    (void)cpu_place_mask_text(0, s, sizeof(s));
    CHECK_STR(s, "none");

    memset(&g, 0, sizeof(g));
    memset(&r, 0, sizeof(r));
    memset(&st, 0, sizeof(st));
    memset(&none, 0, sizeof(none));
    memset(&unk, 0, sizeof(unk));
    for (int i = 0; i < 78; i++) {
        cpu_place_note(&g, 4);
    }
    for (int i = 0; i < 22; i++) {
        cpu_place_note(&g, 6);
    }
    for (int i = 0; i < 64; i++) {
        cpu_place_note(&r, 7);
    }
    for (int i = 0; i < 12; i++) {
        cpu_place_note(&r, 1);
    }
    for (int i = 0; i < 24; i++) {
        cpu_place_note(&r, 5);
    }
    for (int i = 0; i < 9; i++) {
        cpu_place_note(&st, 5);
    }
    cpu_place_note(&st, 3);
    cpu_place_note(&unk, -1);
    (void)cpu_place_part(s, sizeof(s), &g, &r, &st, 0);
    CHECK_STR(s, "on cores G:4 (78 %) R:7 (64 %) S:5 (90 %)");
    (void)cpu_place_part(s, sizeof(s), &g, &r, &st, 0x0f);
    CHECK_STR(s, "on cores G:4 (78 %) R:7 (64 %) S:5 (90 %), on the slowest cores (0-3) G 0 % R "
                 "12 % S 10 %");
    (void)cpu_place_part(s, sizeof(s), &none, &unk, NULL, 0x0f);
    CHECK_STR(s, "on cores G:- R:not known, on the slowest cores (0-3) G - R 0 %");
    /* cut at the buffer: terminated, the length what was kept */
    {
        char small[12];
        const size_t n = cpu_place_part(small, sizeof(small), &g, &r, &st, 0);

        CHECK(n == sizeof(small) - 1 && strlen(small) == n);
    }
}

static void testThread(void)
{
    char s[64], why[128];
    const int cpu = cpu_place_now();
    const uint64_t allowed = cpu_place_allowed();

#if defined(__linux__) || defined(_WIN32)
    CHECK(cpu >= 0);
#else
    CHECK(cpu == -1);
#endif
    if (allowed != 0 && cpu >= 0 && cpu < CPU_PLACE_MAX) {
        CHECK((allowed >> cpu) & 1u);
    }
    (void)cpu_place_priority_text(s, sizeof(s));
#if defined(__linux__)
    CHECK(strncmp(s, "nice ", 5) == 0);
#else
    CHECK(s[0] == '\0');
#endif
    CHECK(!cpu_place_pin(0, why, sizeof(why)));
    CHECK_STR(why, "no cores to keep it to");
#if defined(__linux__)
    if (allowed != 0) {
        CHECK(cpu_place_pin(allowed, why, sizeof(why))); /* no change */
        CHECK(cpu_place_allowed() == allowed);
    }
#else
    CHECK(!cpu_place_pin(1, why, sizeof(why)));
    CHECK_STR(why, "not on this system");
#endif
}

int main(int argc, char **argv)
{
    testHist();
#if defined(__linux__)
    testClasses(argc > 1 ? argv[1] : ".");
#else
    (void)argc;
    (void)argv;
#endif
    testSplit();
    testText();
    testThread();
    if (fails) {
        printf("cpu_place: %d failed\n", fails);
        return 1;
    }
    printf("cpu_place: ok\n");
    return 0;
}
