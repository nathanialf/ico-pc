/*
 * port/platform/test/cpu_keep_test.c
 *
 * The render, game and submit threads' cores (cpu_keep.h), every
 * expectation from the rule's words:
 *   the rule (cpu_keep_choose) on the owner's Pixel (0-1 2.2 GHz, 2-6 3.1
 *     GHz, 7 3.8 GHz): the ideal, core 7 refused, core 7 forbidden by the
 *     allowed cores, the pair, the start's rule, the game thread's core
 *     refused, only core 7 fast and allowed, every fast core refused; a
 *     Helio G85 (0-5 1.8 GHz, 6-7 2.0 GHz): two faster cores, the
 *     few-cores rule (render 7 alone, game 6 alone, the rest 0-5; 7
 *     refused, the game thread's 6 refused, both refused, the pair, the
 *     start's rule, a cpuset without core 7); the Pixel, 4 + 3 + 1, 4 + 4
 *     applying, 8 cores of one speed not; 1 + 7 (render alone, the game
 *     thread free, the rest 0-6, no pair);
 *     a 4 + 3 + 1 Mali phone (0-3 1.8, 4-6 2.6, 7 3.0); 4 bigs of one
 *     speed and 4 littles; a 4-core 1 + 2 + 1 device; a 4-core device of
 *     one speed and a one-core device (the rule does not apply);
 *   the keeper (cpu_keep_tick) against a fake kernel: every pin kept (the
 *     ideal at the start, a scene and the focus ask nothing more, the
 *     block's text); the Pixel's kernel widening core 7 alone to 2-7 (6
 *     alone instead, the 10 s retry, the ideal kept once the kernel lets
 *     it); the system changing the render thread's cores after the pin
 *     (counted once, tried again); a kernel widening every one-core pin
 *     (the pair, the game thread's cores refused one by one until it goes
 *     on the others' cores, then quiet); a cpuset without core 7 (the
 *     request never names it); the render thread's id not known yet (the
 *     request waits); the rule off;
 *   the few-cores rule against the fake kernel: the Helio G85 all kept,
 *     every one-core pin widened (the pair for both threads, the rest
 *     still 0-5), the game thread's core alone widened (both cores for it
 *     at the next tick), the held cores the sweep moves threads off (none
 *     under the start's rule or the rule for three or more), the start
 *     line word for word (2 + 6, 1 + 7; none for the Pixel);
 *   each thread's cores on five layouts read from a fake sysfs under the
 *     build folder (2 + 6, 1 + 7, 4 + 4, 1 + 3 + 4, 8 of one speed): the
 *     start's rule, the keeper's first tick, and the sweep's picks among
 *     the audio threads, the driver's thread, a worker on core 0, the UI
 *     thread and one not read (Linux);
 *   the words of the reasons;
 *   cpu_place.h's additions: the samples by core, a thread's cpuset and
 *     its cores from a fake /proc and /dev/cpuset under the build folder
 *     (Linux), and the registered free threads moved by cpu_place_refree
 *     (Linux, two or more cores allowed).
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* syscall */
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cpu_keep.h"
#include "cpu_place.h"

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            fprintf(stderr, "%s:%d: got\n  \"%s\"\nwant\n  \"%s\"\n", __FILE__, __LINE__, (got),   \
                    (want));                                                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void classes(CpuClasses *c, int n, const unsigned *mhz)
{
    memset(c, 0, sizeof(*c));
    c->count = n;
    for (int i = 0; i < n; i++) {
        c->khz[i] = mhz[i] * 1000u;
    }
}

static const unsigned PIXEL[8] = {2200, 2200, 3100, 3100, 3100, 3100, 3100, 3800};
static const unsigned HELIO[8] = {1800, 1800, 1800, 1800, 1800, 1800, 2000, 2000};
static const unsigned MALI[8] = {1800, 1800, 1800, 1800, 2600, 2600, 2600, 3000};
static const unsigned BIG4[8] = {1800, 1800, 1800, 1800, 2400, 2400, 2400, 2400};
static const unsigned SMALL4[4] = {1800, 2400, 2400, 3000};
static const unsigned SAME4[4] = {2000, 2000, 2000, 2000};
static const unsigned SAME8[8] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
static const unsigned ONE[1] = {2000};
static const unsigned ONE7[8] = {1800, 1800, 1800, 1800, 1800, 1800, 1800, 2400};

static int sets_are(const CpuKeepSets *s, CpuKeepLevel lv, uint64_t r, uint64_t g, uint64_t sub,
                    uint64_t o)
{
    if (s->level == lv && s->render == r && s->game == g && s->submit == sub && s->others == o) {
        return 1;
    }
    fprintf(stderr, "  sets: level %d render %#llx game %#llx submit %#llx others %#llx\n",
            (int)s->level, (unsigned long long)s->render, (unsigned long long)s->game,
            (unsigned long long)s->submit, (unsigned long long)s->others);
    return 0;
}

static void test_choose(void)
{
    CpuClasses c;
    CpuKeepSets s;

    classes(&c, 8, PIXEL);
    CHECK(cpu_keep_applies(&c));
    /* the ideal: 7 alone, the next best (6: 3.1 GHz, the highest number)
       the game thread's, 2-5 the submit thread's, the rest 0-5 */
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0x3c, 0x3f));
    /* allowed 0 is every core with a speed: the same */
    CHECK(cpu_keep_choose(&c, 0, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0x3c, 0x3f));
    /* core 7 refused alone: 6 alone, 5 the game's, the submit thread on
       the rest of the faster cores 2-4 and 7 (a set of several, which the
       Pixel's kernel kept), others 0-4 and 7 */
    CHECK(cpu_keep_choose(&c, 0xff, 0x80, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x40, 0x20, 0x9c, 0x9f));
    /* core 7 forbidden (allowed 0-6): never asked; others 0-4 */
    CHECK(cpu_keep_choose(&c, 0x7f, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x40, 0x20, 0x1c, 0x1f));
    /* the pair (7 and 6 refused alone): 6-7, the game 5, submit 2-4 */
    CHECK(cpu_keep_choose(&c, 0xff, 0xc0, 0, CPU_KEEP_PAIR, &s));
    CHECK(sets_are(&s, CPU_KEEP_PAIR, 0xc0, 0x20, 0x1c, 0x1f));
    /* the start's rule: render on every faster core 2-7, submit on the
       next class 2-6, the game thread free, others everything */
    CHECK(cpu_keep_choose(&c, 0xff, 0xc0, 0, CPU_KEEP_FAST, &s));
    CHECK(sets_are(&s, CPU_KEEP_FAST, 0xfc, 0, 0x7c, 0xff));
    /* the game thread's 6 refused: 5 instead, submit 2-4 and 6 */
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0x40, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x20, 0x5c, 0x5f));
    /* only 7 fast and allowed (0-1, 7): the game and submit threads on
       the others' cores 0-1 */
    CHECK(cpu_keep_choose(&c, 0x83, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0, 0, 0x03));
    /* every fast core refused: nothing alone */
    CHECK(!cpu_keep_choose(&c, 0xff, 0xfc, 0, CPU_KEEP_ALONE, &s));

    /* two faster cores (2 + 6): not the rule for three or more but the
       few-cores rule (cpu_keep.h): the render thread on 7 alone, the game
       thread on 6 alone, the submit thread and every other thread on the
       slow cores 0-5 */
    classes(&c, 8, HELIO);
    CHECK(!cpu_keep_applies(&c) && cpu_keep_few(&c));
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0, 0x3f));
    /* core 7 refused alone: the render thread on 6 alone, the game thread
       on both (no other faster core of its own) */
    CHECK(cpu_keep_choose(&c, 0xff, 0x80, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x40, 0xc0, 0, 0x3f));
    /* the game thread's 6 refused: it shares both */
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0x40, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0xc0, 0, 0x3f));
    /* both refused alone: nothing alone; the pair: both threads on 6-7,
       the rest still on 0-5 */
    CHECK(!cpu_keep_choose(&c, 0xff, 0xc0, 0, CPU_KEEP_ALONE, &s));
    CHECK(cpu_keep_choose(&c, 0xff, 0xc0, 0, CPU_KEEP_PAIR, &s));
    CHECK(sets_are(&s, CPU_KEEP_PAIR, 0xc0, 0xc0, 0, 0x3f));
    /* the start's rule (FAST): both on 6-7, everything else on every core,
       as before the few-cores rule */
    CHECK(cpu_keep_choose(&c, 0xff, 0xc0, 0, CPU_KEEP_FAST, &s));
    CHECK(sets_are(&s, CPU_KEEP_FAST, 0xc0, 0xc0, 0, 0xff));
    /* core 7 not allowed (a cpuset of 0-6): one faster core allowed, the
       render thread on 6 alone, the game thread on every allowed core */
    CHECK(cpu_keep_choose(&c, 0x7f, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x40, 0x7f, 0, 0x3f));
    CHECK(!cpu_keep_choose(&c, 0x7f, 0, 0, CPU_KEEP_PAIR, &s));
    {
        CpuKeep k2;

        cpu_keep_init(&k2, &c);
        CHECK(k2.on && k2.few);
    }
    /* the Pixel (1 + 5 over 2): applies, its placement as measured good
       (render 7 alone, game 6 alone, submit 2-5) */
    classes(&c, 8, PIXEL);
    CHECK(cpu_keep_applies(&c));
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0x3c, 0x3f));
    /* 4 + 3 + 1 (MALI) and 4 + 4 (BIG4): four faster cores, applies */
    classes(&c, 8, MALI);
    CHECK(cpu_keep_applies(&c));
    classes(&c, 8, BIG4);
    CHECK(cpu_keep_applies(&c));
    CHECK(!cpu_keep_few(&c));
    /* 8 cores of one speed: none faster, off */
    classes(&c, 8, SAME8);
    CHECK(!cpu_keep_applies(&c) && !cpu_keep_few(&c));
    CHECK(!cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    /* 1 + 2 + 1 over 4 cores: three faster, applies */
    classes(&c, 4, SMALL4);
    CHECK(cpu_keep_applies(&c));

    classes(&c, 8, MALI);
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0x30, 0x3f));
    classes(&c, 8, BIG4);
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0x40, 0x30, 0x3f));
    classes(&c, 4, SMALL4);
    CHECK(cpu_keep_choose(&c, 0x0f, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x08, 0x04, 0x02, 0x03));

    classes(&c, 4, SAME4);
    CHECK(!cpu_keep_applies(&c) && !cpu_keep_few(&c));
    CHECK(!cpu_keep_choose(&c, 0x0f, 0, 0, CPU_KEEP_ALONE, &s));
    classes(&c, 1, ONE);
    CHECK(!cpu_keep_applies(&c) && !cpu_keep_few(&c));
    CHECK(!cpu_keep_choose(&c, 0x01, 0, 0, CPU_KEEP_FAST, &s));

    /* one faster core of eight (1 + 7): the render thread on it alone, the
       game thread on every core (as before), the rest on 0-6; no pair */
    classes(&c, 8, ONE7);
    CHECK(!cpu_keep_applies(&c) && cpu_keep_few(&c));
    CHECK(cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(sets_are(&s, CPU_KEEP_ALONE, 0x80, 0xff, 0, 0x7f));
    CHECK(!cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_PAIR, &s));
    CHECK(!cpu_keep_choose(&c, 0xff, 0x80, 0, CPU_KEEP_ALONE, &s));
    /* the start's rule: render on it, the game thread on the others' (all) */
    CHECK(cpu_keep_choose(&c, 0xff, 0x80, 0, CPU_KEEP_FAST, &s));
    CHECK(sets_are(&s, CPU_KEEP_FAST, 0x80, 0, 0, 0xff));
}

/* the fake kernel: a mask a thread (the log thread 0, the game 100, the
   render 101, the submit 102) */
typedef struct Fake {
    uint64_t mask[4];
    uint64_t widenCores; /* a one-core pin to one of these is widened to widenTo */
    bool widenSingles;   /* every one-core pin widened to widenTo */
    uint64_t widenTo;
    uint64_t forbid; /* a pin naming one of these refused (EINVAL) */
    uint64_t freeMask;
    unsigned frees;
} Fake;

static int slot(long tid)
{
    return tid == 0 ? 0 : tid == 100 ? 1 : tid == 101 ? 2 : tid == 102 ? 3 : -1;
}

static int pop(uint64_t m)
{
    int n = 0;

    for (; m; m &= m - 1) {
        n++;
    }
    return n;
}

static bool fake_pin(void *u, long tid, uint64_t mask, int *err)
{
    Fake *f = (Fake *)u;
    const int i = slot(tid);

    if (i < 0) {
        *err = ESRCH;
        return false;
    }
    if (mask & f->forbid) {
        *err = EINVAL;
        return false;
    }
    f->mask[i] = pop(mask) == 1 && (f->widenSingles || (mask & f->widenCores)) ? f->widenTo : mask;
    return true;
}

static uint64_t fake_read(void *u, long tid)
{
    const int i = slot(tid);

    return i < 0 ? 0 : ((Fake *)u)->mask[i];
}

static void fake_free(void *u, uint64_t mask)
{
    ((Fake *)u)->freeMask = mask;
    ((Fake *)u)->frees++;
}

static void fake_init(Fake *f, CpuKeepOps *ops)
{
    memset(f, 0, sizeof(*f));
    for (int i = 0; i < 4; i++) {
        f->mask[i] = 0xff;
    }
    ops->u = f;
    ops->pin = fake_pin;
    ops->read = fake_read;
    ops->keep_free = fake_free;
}

static void test_keep_all_kept(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;
    char t[1024];

    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    cpu_keep_init(&k, &c);
    CHECK(k.on);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(f.mask[2] == 0x80 && f.mask[1] == 0x40 && f.mask[3] == 0x3c && f.mask[0] == 0x3f);
    CHECK(f.freeMask == 0x3f && f.frees == 1);
    (void)cpu_keep_try_text(&k, "cpuset /top-app (cores 0-7)", t, sizeof(t));
    CHECK_STR(t, "asked 7, read back 7, cpuset /top-app (cores 0-7), the process may run on 0-7; "
                 "re-pin kept; the game thread on 6 alone (kept), the submit thread on 2-5 "
                 "(kept), every other thread on 0-5; why: the start");
    /* on the ideal, read back so: nothing more, a scene and the focus too */
    CHECK(!cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, 102, 100));
    CHECK(!cpu_keep_tick(&k, &ops, 2000.0, CPU_KEEP_WHY_SCENE | CPU_KEEP_WHY_FOCUS, 0xff, 101, 102,
                         100));
    CHECK(k.pending == 0 && k.tries == 1);
    (void)cpu_keep_block_text(&k, "cpuset /top-app (cores 0-7)", t, sizeof(t));
    CHECK_STR(t, "now on 7 alone (the ideal), the game thread on 6 alone, the submit thread on "
                 "2-5, every other thread on 0-5; read back as set at 2 of 2 reads; tries in the "
                 "block 1 (the ideal kept in 1), since the start 1 (kept in 1); changed by the "
                 "system 0 times in the block, 0 since the start; cpuset /top-app (cores 0-7)");
    cpu_keep_block_reset(&k);
    CHECK(k.blockTries == 0 && k.blockReads == 0 && k.tries == 1);

    /* the system moves the render thread to 2-7 after the pin: counted
       once, tried again at once */
    f.mask[2] = 0xfc;
    CHECK(cpu_keep_tick(&k, &ops, 3000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.changes == 1 && k.changeFrom == 0x80 && k.changeTo == 0xfc && !k.changeGame);
    CHECK(f.mask[2] == 0x80 && k.idealKept && k.tries == 2);
    (void)cpu_keep_block_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "now on 7 alone (the ideal), the game thread on 6 alone, the submit thread on "
                 "2-5, every other thread on 0-5; read back as set at 0 of 1 reads; tries in the "
                 "block 1 (the ideal kept in 1), since the start 2 (kept in 2); changed by the "
                 "system 1 times in the block, 1 since the start (the last: the render thread's "
                 "from 7 to 2-7)");
    (void)cpu_keep_try_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "asked 7, read back 7, the process may run on 0-7; re-pin kept; the game thread "
                 "on 6 alone (kept), the submit thread on 2-5 (kept), every other thread on 0-5; "
                 "why: the system changed the threads' cores");
}

static void test_keep_widened(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;
    char t[1024];

    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    f.widenCores = 0x80; /* the Pixel's answer: core 7 alone becomes 2-7 */
    f.widenTo = 0xfc;
    cpu_keep_init(&k, &c);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(!k.idealKept && k.ideal.got == 0xfc && k.ideal.res == CPU_PIN_OTHER);
    CHECK(k.refused == 0x80 && k.sets.level == CPU_KEEP_ALONE);
    CHECK(f.mask[2] == 0x40 && f.mask[1] == 0x20 && f.mask[3] == 0x9c && f.freeMask == 0x9f);
    (void)cpu_keep_try_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "asked 7, read back 2-7, the process may run on 0-7; re-pin refused (the system "
                 "said yes and keeps it to cores 2-7); on 6 alone instead (kept); the game thread "
                 "on 5 alone (kept), the submit thread on 2-4, 7 (kept), every other thread on "
                 "0-4, 7; why: the start");
    /* not the ideal: nothing until the 10 s retry */
    CHECK(!cpu_keep_tick(&k, &ops, 5000.0, 0, 0xff, 101, 102, 100));
    CHECK(cpu_keep_tick(&k, &ops, 10000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.why == CPU_KEEP_WHY_RETRY && k.tries == 2 && k.kept == 0 && f.mask[2] == 0x40);
    /* a scene begun asks again before the next 10 s */
    CHECK(cpu_keep_tick(&k, &ops, 11000.0, CPU_KEEP_WHY_SCENE, 0xff, 101, 102, 100));
    CHECK(k.why == CPU_KEEP_WHY_SCENE && k.tries == 3 && f.mask[2] == 0x40);
    /* the kernel lets core 7 be: the ideal at the next retry */
    f.widenCores = 0;
    CHECK(!cpu_keep_tick(&k, &ops, 20000.0, 0, 0xff, 101, 102, 100));
    CHECK(cpu_keep_tick(&k, &ops, 21000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.idealKept && k.refused == 0 && k.kept == 1 && f.mask[2] == 0x80 && f.mask[1] == 0x40 &&
          f.mask[3] == 0x3c && f.freeMask == 0x3f);
}

static void test_keep_singles_widened(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;

    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    f.widenSingles = true;
    f.widenTo = 0xfc;
    cpu_keep_init(&k, &c);
    /* 7 widened, 6 widened: the pair 6-7 kept; the game thread's 5
       widened: refused for it, another at the next tick */
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(k.refused == 0xc0 && k.sets.level == CPU_KEEP_PAIR && f.mask[2] == 0xc0);
    CHECK(k.game.res == CPU_PIN_OTHER && k.gameRefused == 0x20 && f.mask[3] == 0x1c);
    /* the game thread's 4, 3 and 2 in turn, then the others' 0-5 */
    CHECK(cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.gameRefused == 0x30 && k.sets.level == CPU_KEEP_PAIR);
    CHECK(cpu_keep_tick(&k, &ops, 2000.0, 0, 0xff, 101, 102, 100));
    CHECK(cpu_keep_tick(&k, &ops, 3000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.gameRefused == 0x3c);
    CHECK(cpu_keep_tick(&k, &ops, 4000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.sets.game == 0 && f.mask[1] == 0x3f && k.game.res == CPU_PIN_KEPT);
    CHECK(f.mask[3] == 0x3c && f.mask[2] == 0xc0 && k.tries == 5);
    /* quiet until the 10 s retry */
    CHECK(!cpu_keep_tick(&k, &ops, 5000.0, 0, 0xff, 101, 102, 100));
}

static void test_keep_forbidden(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;
    char t[1024];

    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    f.forbid = 0x80; /* the cpuset has no core 7: a pin naming it is refused */
    cpu_keep_init(&k, &c);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0x7f, 101, 102, 100));
    CHECK(k.idealKept && f.mask[2] == 0x40 && f.mask[1] == 0x20 && f.mask[3] == 0x1c &&
          f.mask[0] == 0x1f);
    (void)cpu_keep_try_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "asked 6, read back 6, the process may run on 0-6; re-pin kept; the game thread "
                 "on 5 alone (kept), the submit thread on 2-4 (kept), every other thread on 0-4; "
                 "why: the start");
    /* the cores allowed grow to 0-7: tried again, the ideal now 7 */
    f.forbid = 0;
    CHECK(cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.why == CPU_KEEP_WHY_ALLOWED && f.mask[2] == 0x80 && f.mask[1] == 0x40);
}

static void test_keep_waits(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;
    char t[1024];

    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    cpu_keep_init(&k, &c);
    /* the render thread's id not known yet: the request waits */
    CHECK(!cpu_keep_tick(&k, &ops, 0.0, CPU_KEEP_WHY_FOCUS, 0xff, -1, 0, 100));
    CHECK(k.tries == 0 && k.pending == CPU_KEEP_WHY_FOCUS);
    /* the submit thread not started yet */
    CHECK(cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, -1, 100));
    (void)cpu_keep_try_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "asked 7, read back 7, the process may run on 0-7; re-pin kept; the game thread "
                 "on 6 alone (kept), the submit thread not started yet, every other thread on "
                 "0-5; why: the start, focus gained");
    /* it starts: tried again for it */
    CHECK(cpu_keep_tick(&k, &ops, 2000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.why == CPU_KEEP_WHY_THREAD && f.mask[3] == 0x3c);
    /* a new render thread (a backend switch): the start again */
    CHECK(cpu_keep_tick(&k, &ops, 3000.0, 0, 0xff, 101 + 0, 102, 100) == false);
    k.renderTid = 7; /* as if another thread had been kept */
    CHECK(cpu_keep_tick(&k, &ops, 4000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.why == CPU_KEEP_WHY_START);

    /* the rule off: nothing, and the block says so */
    classes(&c, 4, SAME4);
    cpu_keep_init(&k, &c);
    CHECK(!k.on && !cpu_keep_tick(&k, &ops, 0.0, CPU_KEEP_WHY_START, 0x0f, 101, 102, 100));
    (void)cpu_keep_block_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "off (no core faster than the slowest)");
}

/* the few-cores rule against the fake kernel (cpu_keep.h): the Helio G85
   (2 + 6) all kept, a kernel widening every one-core pin, one widening
   the game thread's alone; the held cores the sweep moves threads off;
   the start line word for word (2 + 6, 1 + 7, none for the Pixel) */
static void test_keep_few(void)
{
    CpuClasses c;
    CpuKeep k;
    CpuKeepOps ops;
    Fake f;
    char t[1024];

    classes(&c, 8, HELIO);
    fake_init(&f, &ops);
    cpu_keep_init(&k, &c);
    CHECK(k.on && k.few);
    CHECK(cpu_keep_held(&k) == 0); /* nothing kept yet */
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(f.mask[2] == 0x80 && f.mask[1] == 0x40 && f.mask[3] == 0x3f && f.mask[0] == 0x3f);
    CHECK(f.freeMask == 0x3f && f.frees == 1);
    CHECK(cpu_keep_held(&k) == 0xc0);
    (void)cpu_keep_try_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "asked 7, read back 7, the process may run on 0-7; re-pin kept; the game thread "
                 "on 6 alone (kept), the submit thread on 0-5 (kept), every other thread on 0-5; "
                 "why: the start");
    (void)cpu_keep_block_text(&k, "", t, sizeof(t));
    CHECK_STR(t, "now on 7 alone (the ideal), the game thread on 6 alone, the submit thread on "
                 "0-5, every other thread on 0-5; read back as set at 0 of 0 reads; tries in the "
                 "block 1 (the ideal kept in 1), since the start 1 (kept in 1); changed by the "
                 "system 0 times in the block, 0 since the start");
    CHECK(!cpu_keep_tick(&k, &ops, 1000.0, CPU_KEEP_WHY_SCENE, 0xff, 101, 102, 100));

    /* every one-core pin widened to 0-7: 7, then 6 refused alone, the
       pair 6-7 for both threads, the rest still on 0-5; quiet until the
       10 s retry */
    fake_init(&f, &ops);
    f.widenSingles = true;
    f.widenTo = 0xff;
    cpu_keep_init(&k, &c);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(k.sets.level == CPU_KEEP_PAIR && k.refused == 0xc0);
    CHECK(f.mask[2] == 0xc0 && f.mask[1] == 0xc0 && f.mask[3] == 0x3f && f.mask[0] == 0x3f);
    CHECK(cpu_keep_held(&k) == 0xc0);
    CHECK(!cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, 102, 100));

    /* the game thread's 6 alone widened (the render thread's 7 kept): 6-7
       for it from the next tick, then quiet */
    fake_init(&f, &ops);
    f.widenCores = 0x40;
    f.widenTo = 0xff;
    cpu_keep_init(&k, &c);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100));
    CHECK(k.gameRefused == 0x40 && f.mask[1] == 0xff && f.mask[2] == 0x80);
    CHECK(cpu_keep_tick(&k, &ops, 1000.0, 0, 0xff, 101, 102, 100));
    CHECK(k.sets.game == 0xc0 && f.mask[1] == 0xc0 && f.mask[2] == 0x80 && f.mask[3] == 0x3f);
    CHECK(!cpu_keep_tick(&k, &ops, 2000.0, 0, 0xff, 101, 102, 100));

    /* the start's rule reached (FAST): the others on every core, so no
       sweep */
    k.sets.level = CPU_KEEP_FAST;
    k.sets.others = 0xff;
    CHECK(cpu_keep_held(&k) == 0);

    /* the rule for three or more never sweeps */
    classes(&c, 8, PIXEL);
    fake_init(&f, &ops);
    cpu_keep_init(&k, &c);
    CHECK(cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100) && cpu_keep_held(&k) == 0);
    CHECK(cpu_keep_few_text(&c, 0xff, t, sizeof(t)) == 0 && t[0] == '\0');

    /* the start lines */
    classes(&c, 8, HELIO);
    (void)cpu_keep_few_text(&c, 0xff, t, sizeof(t));
    CHECK_STR(t, "kept (2 cores faster than the slowest): the render thread on 7 alone, the game "
                 "thread on 6 alone, the submit thread and every other thread of the process on "
                 "0-5 (the audio threads, the log thread, the workers, the driver's threads and "
                 "those made later: the log thread moves a thread found on 6-7 there once a "
                 "second; Android's UI thread is left to the system); where the system widens a "
                 "one-core pin, the render and the game thread share 6-7");
    classes(&c, 8, ONE7);
    (void)cpu_keep_few_text(&c, 0, t, sizeof(t));
    CHECK_STR(t, "kept (1 core faster than the slowest): the render thread on 7 alone, the game "
                 "thread on every core it may (0-7), the submit thread and every other thread of "
                 "the process on 0-6 (the audio threads, the log thread, the workers, the "
                 "driver's threads and those made later: the log thread moves a thread found on "
                 "7 there once a second; Android's UI thread is left to the system)");
}

#if defined(__linux__)
static void write_speed(const char *root, int cpu, unsigned mhz)
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
        fprintf(f, "%u\n", mhz * 1000u);
        fclose(f);
    }
}

/* Each thread's cores on five layouts read from a fake sysfs under the
   build folder (cpu_place_classes_read, as cpu_place_test's): the start's
   rule (cpu_place_plan), then the keeper's first tick on a kernel that
   keeps every pin, then the few-cores rule's sweep over a table of the
   process's other threads as they were made (the audio threads and the
   driver's thread inheriting the game thread's start cores, a worker
   already on core 0, Android's UI thread, one whose cores could not be
   read).  The layouts with three or more faster cores keep the
   expectations above (test_choose, test_keep_all_kept) and sweep nothing */
typedef struct Layout {
    const char *name;
    int n;
    unsigned mhz[8];
    /* the start (cpu_place_plan) */
    CpuPlanKind kind;
    uint64_t startGame, startRender, startSubmit;
    /* after the keeper's first tick (0 masks: the keeper off) */
    bool on;
    uint64_t game, render, submit, log, held;
    /* the sweep's moves (thread ids, 0 ended) */
    long moved[4];
} Layout;

static void test_layouts(const char *dir)
{
    static const Layout L[5] = {
        {"2+6",
         8,
         {1800, 1800, 1800, 1800, 1800, 1800, 2000, 2000},
         CPU_PLAN_TWO_FAST,
         0xc0,
         0xc0,
         0,
         true,
         0x40,
         0x80,
         0x3f,
         0x3f,
         0xc0,
         {200, 201, 202, 0}},
        {"1+7",
         8,
         {1800, 1800, 1800, 1800, 1800, 1800, 1800, 2400},
         CPU_PLAN_ONE_FAST,
         0,
         0x80,
         0,
         true,
         0xff,
         0x80,
         0x7f,
         0x7f,
         0x80,
         {200, 201, 202, 0}},
        {"4+4",
         8,
         {1800, 1800, 1800, 1800, 2400, 2400, 2400, 2400},
         CPU_PLAN_MANY_FAST,
         0,
         0xf0,
         0xf0,
         true,
         0x40,
         0x80,
         0x30,
         0x3f,
         0,
         {0}},
        {"1+3+4",
         8,
         {1800, 1800, 1800, 1800, 2600, 2600, 2600, 3000},
         CPU_PLAN_MANY_FAST,
         0,
         0x80,
         0x70,
         true,
         0x40,
         0x80,
         0x30,
         0x3f,
         0,
         {0}},
        {"8",
         8,
         {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000},
         CPU_PLAN_NONE,
         0,
         0,
         0,
         false,
         0,
         0,
         0,
         0,
         0,
         {0}},
    };

    for (int li = 0; li < 5; li++) {
        const Layout *y = &L[li];
        char root[600];
        CpuClasses c;
        CpuPlan p;
        CpuKeep k;
        CpuKeepOps ops;
        Fake f;

        snprintf(root, sizeof(root), "%s/cpu_keep_layout_%d", dir, li);
        (void)mkdir(root, 0755);
        for (int i = 0; i < y->n; i++) {
            write_speed(root, i, y->mhz[i]);
        }
        CHECK(cpu_place_classes_read(root, &c) == y->n);
        cpu_place_plan(&c, &p);
        if (p.kind != y->kind || p.game != y->startGame || p.render != y->startRender ||
            p.submit != y->startSubmit) {
            fprintf(stderr, "  layout %s: start %d game %#llx render %#llx submit %#llx\n", y->name,
                    (int)p.kind, (unsigned long long)p.game, (unsigned long long)p.render,
                    (unsigned long long)p.submit);
            fails++;
        }
        fake_init(&f, &ops);
        /* the threads as the start leaves them: the game thread on its
           start cores (every core when free), the render and submit
           threads too */
        f.mask[1] = y->startGame != 0 ? y->startGame : 0xff;
        f.mask[2] = y->startRender != 0 ? y->startRender : 0xff;
        f.mask[3] = y->startSubmit != 0 ? y->startSubmit : 0xff;
        cpu_keep_init(&k, &c);
        CHECK(k.on == y->on);
        (void)cpu_keep_tick(&k, &ops, 0.0, 0, 0xff, 101, 102, 100);
        if (y->on && (f.mask[1] != y->game || f.mask[2] != y->render || f.mask[3] != y->submit ||
                      f.mask[0] != y->log || cpu_keep_held(&k) != y->held)) {
            fprintf(stderr,
                    "  layout %s: game %#llx render %#llx submit %#llx log %#llx held %#llx\n",
                    y->name, (unsigned long long)f.mask[1], (unsigned long long)f.mask[2],
                    (unsigned long long)f.mask[3], (unsigned long long)f.mask[0],
                    (unsigned long long)cpu_keep_held(&k));
            fails++;
        }
        {
            /* the process's other threads: Android's UI thread (50), the
               game, render and submit threads, SDL's audio thread (200)
               and AAudio's (201) made from the game thread at its start
               cores, the driver's (202) made before any pin, a worker
               already on core 0 (203), one not read (204) */
            const uint64_t g0 = y->startGame != 0 ? y->startGame : 0xff;
            const long ids[9] = {50, 100, 101, 102, 200, 201, 202, 203, 204};
            const uint64_t masks[9] = {0xff, f.mask[1], f.mask[2], f.mask[3], g0,
                                       g0,   0xff,      0x01,      0};
            const long skip[4] = {101, 102, 100, 50};
            long out[9];
            const int m = cpu_place_sweep_pick(ids, masks, 9, cpu_keep_held(&k), skip, 4, out, 9);
            int want = 0;

            while (want < 4 && y->moved[want] != 0) {
                want++;
            }
            CHECK(m == want);
            for (int i = 0; i < m && i < want; i++) {
                CHECK(out[i] == y->moved[i]);
            }
        }
    }
}
#endif

static void test_words(void)
{
    char t[512];

    (void)cpu_keep_why_text(0, t, sizeof(t));
    CHECK_STR(t, "none");
    (void)cpu_keep_why_text(0xffu, t, sizeof(t));
    CHECK_STR(t, "the start, a 3D scene began, focus gained, back from the background, the system "
                 "changed the threads' cores, the cores the process may run on changed, the 10 s "
                 "retry, the submit thread started");
    {
        CpuPlaceHist g, r, s;

        memset(&g, 0, sizeof(g));
        memset(&r, 0, sizeof(r));
        memset(&s, 0, sizeof(s));
        g.n[2] = 1;
        g.n[3] = 2;
        g.unknown = 1;
        r.n[7] = 10;
        s.n[2] = 3;
        s.n[3] = 3;
        (void)cpu_place_shares_text(t, sizeof(t), &g, &r, &s);
        CHECK_STR(t, "the samples by core: G 2:25 3:50 ?:25 %, R 7:100 %, S 2:50 3:50 %");
        memset(&g, 0, sizeof(g));
        (void)cpu_place_shares_text(t, sizeof(t), &g, &r, NULL);
        CHECK_STR(t, "the samples by core: G -, R 7:100 %");
    }
}

#if defined(__linux__)
static void put_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_cpuset(const char *dir)
{
    char root[400], p[800], cs[420], path[64];
    uint64_t cpus = 1;

    snprintf(root, sizeof(root), "%s/cpu_keep_fake", dir);
    (void)mkdir(root, 0755);
    snprintf(p, sizeof(p), "%s/task", root);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/task/123", root);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/task/124", root);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/task/125", root);
    (void)mkdir(p, 0755);
    snprintf(cs, sizeof(cs), "%s/cpuset", root);
    (void)mkdir(cs, 0755);
    snprintf(p, sizeof(p), "%s/top-app", cs);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/fg", cs);
    (void)mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/task/123/cpuset", root);
    put_file(p, "/top-app\n");
    snprintf(p, sizeof(p), "%s/top-app/cpus", cs);
    put_file(p, "0-7\n");
    snprintf(p, sizeof(p), "%s/task/124/cpuset", root);
    put_file(p, "/\n");
    snprintf(p, sizeof(p), "%s/cpus", cs);
    put_file(p, "0-3\n");
    snprintf(p, sizeof(p), "%s/task/125/cpuset", root);
    put_file(p, "/fg\n");
    snprintf(p, sizeof(p), "%s/fg/effective_cpus", cs);
    put_file(p, "0-6\n");
    snprintf(p, sizeof(p), "%s/task", root);
    CHECK(cpu_place_cpuset_read(p, cs, 123, path, sizeof(path), &cpus) == 0);
    CHECK_STR(path, "/top-app");
    CHECK(cpus == 0xff);
    CHECK(cpu_place_cpuset_read(p, cs, 124, path, sizeof(path), &cpus) == 0);
    CHECK_STR(path, "/");
    CHECK(cpus == 0x0f);
    CHECK(cpu_place_cpuset_read(p, cs, 125, path, sizeof(path), &cpus) == 0);
    CHECK_STR(path, "/fg");
    CHECK(cpus == 0x7f);
    CHECK(cpu_place_cpuset_read(p, cs, 999, path, sizeof(path), &cpus) == ENOENT);
    CHECK(cpus == 0 && path[0] == '\0');
}

static void test_refree(void)
{
    const uint64_t a = cpu_place_allowed();
    const long self = syscall(SYS_gettid);
    uint64_t one = 0;
    unsigned ended = 9;
    char why[96];

    if (a == 0 || (a & (a - 1)) == 0) {
        printf("cpu_keep_test: refree skipped (one core allowed)\n");
        return;
    }
    for (int i = 0; i < CPU_PLACE_MAX && one == 0; i++) {
        one = (a >> i) & 1u ? (uint64_t)1 << i : 0;
    }
    /* registered by its own free (a mask kept), then kept to one core */
    cpu_place_keep_free(a);
    CHECK(cpu_place_thread_free() == CPU_FREE_ALREADY);
    CHECK(cpu_place_pin(one, why, sizeof(why)) && cpu_place_allowed() == one);
    /* skipped by its id: left alone */
    CHECK(cpu_place_refree(a, &self, 1, &ended) == 0 && ended == 0);
    CHECK(cpu_place_allowed() == one);
    /* moved back */
    CHECK(cpu_place_refree(a, NULL, 0, &ended) == 1 && ended == 0);
    CHECK(cpu_place_allowed() == a);
    /* already there: not touched */
    CHECK(cpu_place_refree(a, NULL, 0, NULL) == 0);
    cpu_place_keep_free(0);
}
#endif

/* The pipeline set made while the few-cores rule is in force
   (cpu_keep_set_cores): on the 2 + 6 and 1 + 7 layouts a worker starting
   while the set runs keeps every core (the process's, or every core with a
   speed when not known), once the set is done the rule's own (0); under the
   rule for three or more (the Pixel) and with the keeper off, the rule's
   own throughout.  The sweep leaves the set's workers (their ids in the
   skip list while it runs) on every core and moves one after.  The line
   word for word in its three modes */
static void test_pipe_set(void)
{
    CpuClasses c;
    CpuKeep k;
    char t[640];

    classes(&c, 8, HELIO);
    cpu_keep_init(&k, &c);
    CHECK(k.on && k.few);
    CHECK(cpu_keep_set_cores(&k, true, 0xff) == 0xff);
    CHECK(cpu_keep_set_cores(&k, true, 0) == 0xff); /* every core with a speed */
    CHECK(cpu_keep_set_cores(&k, true, 0x3f) == 0x3f);
    CHECK(cpu_keep_set_cores(&k, false, 0xff) == 0);
    classes(&c, 8, ONE7);
    cpu_keep_init(&k, &c);
    CHECK(k.few && cpu_keep_set_cores(&k, true, 0xff) == 0xff);
    classes(&c, 8, PIXEL);
    cpu_keep_init(&k, &c);
    CHECK(k.on && !k.few && cpu_keep_set_cores(&k, true, 0xff) == 0);
    classes(&c, 8, SAME8);
    cpu_keep_init(&k, &c);
    CHECK(!k.on && cpu_keep_set_cores(&k, true, 0xff) == 0);

    {
        /* the 2 + 6 sweep: a worker (300) on every core is skipped while
           the set runs, moved once it is done; the audio thread (200) on
           the held cores moved either way */
        const long ids[3] = {300, 200, 101};
        const uint64_t masks[3] = {0xff, 0xc0, 0x80};
        const long during[5] = {101, 102, 100, 50, 300};
        long out[3];
        int m = cpu_place_sweep_pick(ids, masks, 3, 0xc0, during, 5, out, 3);
        CHECK(m == 1 && out[0] == 200);
        m = cpu_place_sweep_pick(ids, masks, 3, 0xc0, during, 4, out, 3);
        CHECK(m == 2 && out[0] == 300 && out[1] == 200);
    }

    (void)cpu_keep_set_text(t, sizeof(t), 1, 7, 0xff, 0x3f, 12.34, 61.7);
    CHECK_STR(t, "made in 12.3 s; its 7 workers on every core 0-7 while it ran (the few-cores "
                 "rule's exception: the set is made before the game and render threads have "
                 "steady work); the process's CPU time 61.7 s over it (5.0 cores busy on "
                 "average); pipeline workers made from here (lazy compiles, the background "
                 "finisher) run on 0-5");
    (void)cpu_keep_set_text(t, sizeof(t), 0, 0, 0, 0, 51.2, -1.0);
    CHECK_STR(t, "made in 51.2 s with no cores' rule in force (nothing pinned: its workers ran "
                 "on any core the system gave them)");
    (void)cpu_keep_set_text(t, sizeof(t), 2, 4, 0x3f, 0x3f, 2.0, 3.0);
    CHECK_STR(t, "made in 2.0 s; its 4 workers on the free cores 0-5 (the cores' rule in "
                 "force); the process's CPU time 3.0 s over it (1.5 cores busy on average); "
                 "pipeline workers made from here (lazy compiles, the background finisher) "
                 "run on 0-5");
    {
        char s[20];
        const size_t n = cpu_keep_set_text(s, sizeof(s), 1, 7, 0xff, 0x3f, 1.0, 1.0);
        CHECK(n == strlen(s) && n < sizeof(s));
    }
}

int main(int argc, char **argv)
{
    test_pipe_set();
    test_choose();
    test_keep_all_kept();
    test_keep_widened();
    test_keep_singles_widened();
    test_keep_forbidden();
    test_keep_waits();
    test_keep_few();
    test_words();
#if defined(__linux__)
    test_layouts(argc > 1 ? argv[1] : ".");
    test_cpuset(argc > 1 ? argv[1] : ".");
    test_refree();
#else
    (void)argc;
    (void)argv;
#endif
    if (fails != 0) {
        fprintf(stderr, "cpu_keep_test: %d failed\n", fails);
        return 1;
    }
    printf("cpu_keep_test: ok\n");
    return 0;
}
