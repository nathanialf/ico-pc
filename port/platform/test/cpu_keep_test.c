/*
 * port/platform/test/cpu_keep_test.c
 *
 * The render, game and submit threads' cores (cpu_keep.h), every
 * expectation from the rule's words:
 *   the rule (cpu_keep_choose) on the owner's Pixel (0-1 2.2 GHz, 2-6 3.1
 *     GHz, 7 3.8 GHz): the ideal, core 7 refused, core 7 forbidden by the
 *     allowed cores, the pair, the start's rule, the game thread's core
 *     refused, only core 7 fast and allowed, every fast core refused; a
 *     Helio G85 (0-5 1.8 GHz, 6-7 2.0 GHz): two faster cores, the keeper
 *     off (it needs three); the Pixel, 4 + 3 + 1, 4 + 4 applying, 8 cores
 *     of one speed not;
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

    /* two faster cores (2 + 6): the keeper does not apply (the render and
       game threads alone would leave the submit thread and every worker on
       the slow cores); the start's rule stands, nothing chosen at any
       level, and the keeper is off */
    classes(&c, 8, HELIO);
    CHECK(!cpu_keep_applies(&c));
    CHECK(!cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_ALONE, &s));
    CHECK(!cpu_keep_choose(&c, 0xff, 0, 0, CPU_KEEP_FAST, &s));
    {
        CpuKeep k2;

        cpu_keep_init(&k2, &c);
        CHECK(!k2.on);
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
    /* 8 cores of one speed: none faster, off */
    classes(&c, 8, SAME8);
    CHECK(!cpu_keep_applies(&c));
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
    CHECK(!cpu_keep_applies(&c));
    CHECK(!cpu_keep_choose(&c, 0x0f, 0, 0, CPU_KEEP_ALONE, &s));
    classes(&c, 1, ONE);
    CHECK(!cpu_keep_applies(&c));
    CHECK(!cpu_keep_choose(&c, 0x01, 0, 0, CPU_KEEP_FAST, &s));
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
    CHECK_STR(t, "off (fewer than three cores faster than the slowest)");
}

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

int main(int argc, char **argv)
{
    test_choose();
    test_keep_all_kept();
    test_keep_widened();
    test_keep_singles_widened();
    test_keep_forbidden();
    test_keep_waits();
    test_words();
#if defined(__linux__)
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
