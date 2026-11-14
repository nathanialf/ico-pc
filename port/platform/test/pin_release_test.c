/*
 * port/platform/test/pin_release_test.c
 *
 * The pins let go at one picture a tick (pin_release.h), with the seconds
 * of the owner's v0.11.7-rc2 log (build-host/tmp/v0117/evidence/owner-rc2b,
 * the cage room): a stuck second 30 pictures for 30 game frames, the step
 * rule holding 35 of 46 asks, a picture's CPU 21.3 ms (:4233) against the
 * blocks' median 8.5 (the good blocks' picture CPU 8.06-9.00 ms, :4042-
 * :4148's hint lines); a good second 58 pictures for 30 frames, 18 of 86
 * held, 6.1 ms (the recovery, :4439-:4452):
 *   rule     stuck = one a tick with the CPU raised (1.25 x the median; any
 *            CPU known with fewer than two blocks); a one-a-tick second at
 *            9.0 ms against 8.5 is not stuck;
 *   states   pinned and stuck: let go (where allowed); let go: 10 judged
 *            seconds without one a tick take the pins again, a stuck one
 *            starts the count again, a second of fewer than 20 frames counts
 *            neither way; the rule no longer allowing it takes them again at
 *            the next second; not allowed: stuck seconds traced, nothing let
 *            go;
 *   trace    each stuck second, each second let go, five after the pins
 *            come back, then none; at most 300 lines a run, then the end
 *            said once;
 *   cores    the render and game threads' cores a second and a block, the
 *            core changes, those not known;
 *   lines    the let-go and taken-again lines, the trace with the ring's
 *            samples (each core the threads ran on) and without, the block
 *            clause with the cores' clocks and with the rule's words, every
 *            one word for word; a short buffer cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "pin_release.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the blocks' median and count the window passes (clock_trap_cpu_median) */
#define MED 8.5
#define BLOCKS 6u

static ClockTrapSecond sec(unsigned pictures, unsigned frames, unsigned asks, unsigned held,
                           double cpuMs)
{
    ClockTrapSecond s;

    memset(&s, 0, sizeof(s));
    s.t0Ms = 1000.0;
    s.pictures = pictures;
    s.frames = frames;
    s.asks = asks;
    s.held = held;
    s.asMade = true;
    s.cpuMs = cpuMs * pictures;
    s.cpuN = pictures;
    return s;
}

static ClockTrapSecond stuck(void)
{
    return sec(30, 30, 46, 35, 21.3);
}

static ClockTrapSecond good(void)
{
    return sec(58, 30, 86, 18, 6.1);
}

static void test_rule(void)
{
    ClockTrapSecond s = stuck();

    CHECK(pin_release_stuck(&s, MED, BLOCKS));
    /* one a tick, the CPU not raised: 9.0 < 1.25 x 8.5 = 10.625 */
    s = sec(30, 30, 46, 35, 9.0);
    CHECK(!pin_release_stuck(&s, MED, BLOCKS));
    /* the edge: 10.625 itself is raised */
    s = sec(30, 30, 46, 35, 10.625);
    CHECK(pin_release_stuck(&s, MED, BLOCKS));
    /* one block of level: any CPU known */
    s = sec(30, 30, 46, 35, 9.0);
    CHECK(pin_release_stuck(&s, MED, 1u));
    /* two a tick, the cage's good second */
    s = good();
    CHECK(!pin_release_stuck(&s, MED, BLOCKS));
    /* the CPU not known */
    s = stuck();
    s.cpuN = 0;
    CHECK(!pin_release_stuck(&s, 0.0, 0u));
}

static unsigned feed(PinRelease *p, ClockTrapSecond s, bool allowed)
{
    return pin_release_second(p, &s, MED, BLOCKS, allowed, NULL);
}

static void test_states(void)
{
    PinRelease p;
    ClockTrapSecond s;
    unsigned ev;
    char b[1024];

    pin_release_reset(&p);
    CHECK(p.lastRender == -1 && p.lastGame == -1);
    /* good seconds: nothing, no trace */
    CHECK(feed(&p, good(), true) == 0u);
    CHECK(feed(&p, good(), true) == 0u);
    /* stuck: let go, traced as pinned */
    s = stuck();
    ev = pin_release_second(&p, &s, MED, BLOCKS, true, NULL);
    CHECK(ev == (PIN_RELEASE_EV_STUCK | PIN_RELEASE_EV_RELEASE | PIN_RELEASE_EV_TRACE));
    CHECK(p.released && p.runReleases == 1u && p.label == 0 && p.labelN == 0u);
    (void)pin_release_release_text(&p, &s, MED, b, sizeof b);
    CHECK_STR(b, "pins let go: 30 pictures for 30 game frames in a second (one a tick) with a "
                 "picture's CPU 21.3 ms against 8.5 (the blocks' median): the render and game "
                 "threads may run on any faster core until 10 seconds pass without one picture a "
                 "tick; 1 this run");
    (void)pin_release_release_text(&p, &s, 0.0, b, sizeof b);
    CHECK(strstr(b, "21.3 ms (no blocks' level yet): ") != NULL);
    /* three more stuck while let go: traced, the count at 0 */
    for (unsigned k = 1; k <= 3u; k++) {
        CHECK(feed(&p, stuck(), true) == (PIN_RELEASE_EV_STUCK | PIN_RELEASE_EV_TRACE));
        CHECK(p.labelN == k && p.good == 0u);
    }
    CHECK(p.heldS == 3u && p.heldStuck == 3u);
    /* nine good: counted, traced */
    for (unsigned k = 0; k < 9u; k++) {
        CHECK(feed(&p, good(), true) == PIN_RELEASE_EV_TRACE);
    }
    CHECK(p.good == 9u && p.released);
    /* a second of fewer than 20 frames (a pause): neither way, traced */
    CHECK(feed(&p, sec(5, 5, 0, 0, 6.0), true) == PIN_RELEASE_EV_TRACE);
    CHECK(p.good == 9u && p.heldS == 13u);
    /* the tenth good: taken again */
    CHECK(feed(&p, good(), true) == (PIN_RELEASE_EV_RESTORE | PIN_RELEASE_EV_TRACE));
    CHECK(!p.released && p.runRestores == 1u && p.heldS == 14u && p.labelN == 14u);
    (void)pin_release_restore_text(&p, b, sizeof b);
    CHECK_STR(b, "pins taken again: 10 seconds without one picture a tick; let go for 14 s, 3 of "
                 "them at one a tick; 1 this run");
    /* five traced after, then none */
    for (unsigned k = 1; k <= 5u; k++) {
        CHECK(feed(&p, good(), true) == PIN_RELEASE_EV_TRACE);
        CHECK(p.label == 2 && p.labelN == k);
    }
    CHECK(feed(&p, good(), true) == 0u);
    CHECK(p.traceLines == 1u + 3u + 9u + 1u + 1u + 5u);
    /* the block's counts, taken */
    (void)pin_release_block_text(&p, NULL, 0, NULL, b, sizeof b);
    CHECK_STR(b, "; the render thread's cores not sampled; the game thread's cores not sampled; "
                 "pins let go 1, taken again 1, let go for 14 s of the block (at one a tick 1 s "
                 "pinned, 3 s let go)");
    (void)pin_release_block_text(&p, NULL, 0, NULL, b, sizeof b);
    CHECK(strstr(b, "pins let go 0, taken again 0, let go for 0 s of the block (at one a tick 0 s "
                    "pinned, 0 s let go)") != NULL);

    /* a stuck second inside the count starts it again */
    pin_release_reset(&p);
    CHECK(feed(&p, stuck(), true) & PIN_RELEASE_EV_RELEASE);
    for (unsigned k = 0; k < 9u; k++) {
        (void)feed(&p, good(), true);
    }
    CHECK(p.good == 9u);
    (void)feed(&p, stuck(), true);
    CHECK(p.good == 0u && p.released);
    for (unsigned k = 0; k < 9u; k++) {
        CHECK(!(feed(&p, good(), true) & PIN_RELEASE_EV_RESTORE));
    }
    CHECK(feed(&p, good(), true) & PIN_RELEASE_EV_RESTORE);
    CHECK(p.heldS == 20u && p.heldStuck == 1u);
    /* stuck again after: let go again */
    CHECK(feed(&p, stuck(), true) & PIN_RELEASE_EV_RELEASE);
    CHECK(p.runReleases == 2u && p.heldS == 0u);
    /* the rule no longer allows it: taken again at the next second */
    CHECK(feed(&p, stuck(), false) & PIN_RELEASE_EV_RESTORE);
    CHECK(!p.released && p.runRestores == 2u);

    /* not allowed (the few-cores rule, nothing pinned): stuck seconds
       traced, never let go */
    pin_release_reset(&p);
    CHECK(feed(&p, stuck(), false) == (PIN_RELEASE_EV_STUCK | PIN_RELEASE_EV_TRACE));
    CHECK(feed(&p, stuck(), false) == (PIN_RELEASE_EV_STUCK | PIN_RELEASE_EV_TRACE));
    CHECK(!p.released && p.runReleases == 0u);
    (void)pin_release_block_text(&p, NULL, 0, "the few-cores rule keeps its pins", b, sizeof b);
    CHECK_STR(b, "; the render thread's cores not sampled; the game thread's cores not sampled; "
                 "pins not let go here (the few-cores rule keeps its pins), at one a tick 2 s");
    /* the CPU not raised: not stuck, nothing */
    CHECK(feed(&p, sec(30, 30, 46, 35, 9.0), true) == 0u);

    /* the trace's lines spent: the end once, then nothing */
    pin_release_reset(&p);
    p.traceLines = PIN_RELEASE_TRACE_MAX - 1u;
    CHECK(feed(&p, stuck(), false) & PIN_RELEASE_EV_TRACE);
    ev = feed(&p, stuck(), false);
    CHECK((ev & PIN_RELEASE_EV_TRACE_END) && !(ev & PIN_RELEASE_EV_TRACE));
    ev = feed(&p, stuck(), false);
    CHECK(ev == PIN_RELEASE_EV_STUCK);
}

/* the ring: four samples in the second from 1000 ms (the rc2b trace :4233's
   clocks), the Pixel's speeds */
static void ring_of(GpuSysfsRing *r)
{
    static const uint16_t lo[4] = {729, 729, 729, 729};
    static const uint16_t mid[4] = {898, 608, 760, 1209};
    static const uint16_t top[4] = {1305, 1305, 883, 1305};

    memset(r, 0, sizeof(*r));
    r->cpuCount = 8;
    for (int i = 0; i < 8; i++) {
        r->cpuKhz[i] = i < 2 ? 2246000u : (i < 7 ? 3052000u : 3800000u);
    }
    for (int k = 0; k < 4; k++) {
        r->s[k].tMs = 1100.0 + 250.0 * k;
        for (int i = 0; i < 8; i++) {
            r->s[k].cpuMhz[i] = i < 2 ? lo[k] : (i < 7 ? mid[k] : top[k]);
        }
    }
    r->count = 4;
    r->next = 4;
}

static void test_cores_and_trace(void)
{
    PinRelease p;
    PinCores c;
    GpuSysfsRing ring;
    ClockTrapSecond s = stuck();
    char b[1536];
    double mhz[8];

    pin_release_reset(&p);
    /* 29 pictures with the render thread on 7, one on 6; the game thread
       on 6 throughout; one picture whose cores were not known */
    for (int k = 0; k < 29; k++) {
        pin_release_note(&p, 7, 6);
    }
    pin_release_note(&p, 6, 6);
    pin_release_note(&p, -1, 99);
    CHECK(p.sec.render[7] == 29u && p.sec.render[6] == 1u && p.sec.renderMoves == 1u);
    CHECK(p.sec.renderUnknown == 1u && p.sec.gameUnknown == 1u && p.sec.game[6] == 30u);
    CHECK(p.lastRender == 6 && p.lastGame == 6);
    CHECK(pin_release_second(&p, &s, MED, BLOCKS, true, &c) & PIN_RELEASE_EV_RELEASE);
    CHECK(c.render[7] == 29u && p.sec.render[7] == 0u && p.block.render[7] == 29u);
    (void)pin_release_trace_text(&p, &s, &c, 0x80u, 0x40u, NULL, b, sizeof b);
    CHECK_STR(b, "pin trace (pinned, at one a tick): 30 pictures for 30 game frames (1.00 a "
                 "tick), a picture's CPU 21.3 ms; the step rule held 35 of 46 further pictures "
                 "asked; the CPU clocks not sampled here; the render thread on core 7 29, core 6 "
                 "1, not known 1 (core changes 1), may run on 7; the game thread on core 6 30, "
                 "not known 1 (core changes 0), may run on 6; their cores' clocks not sampled "
                 "here");
    /* the next second let go, with the ring: each core run on, the render
       thread's first, in core order */
    ring_of(&ring);
    for (int k = 0; k < 20; k++) {
        pin_release_note(&p, k < 10 ? 6 : 7, 5);
    }
    s = stuck();
    (void)pin_release_second(&p, &s, MED, BLOCKS, true, &c);
    CHECK(c.renderMoves == 1u); /* 6 (the last before) to 6, then 6 to 7 */
    (void)pin_release_trace_text(&p, &s, &c, 0xfcu, 0xfcu, &ring, b, sizeof b);
    CHECK_STR(b, "pin trace (let go, second 1): 30 pictures for 30 game frames (1.00 a tick), a "
                 "picture's CPU 21.3 ms; the step rule held 35 of 46 further pictures asked; the "
                 "CPU clocks in that second (4 samples, MHz): cores 0-1 729 729 729 729, cores "
                 "2-6 898 608 760 1209, core 7 1305 1305 883 1305; the render thread on core 6 "
                 "10, core 7 10 (core changes 1), may run on 2-7; the game thread on core 5 20 "
                 "(core changes 1), may run on 2-7; their cores' clocks in that second (MHz): "
                 "core 6 898 608 760 1209, core 7 1305 1305 883 1305, core 5 898 608 760 1209");
    /* nothing sampled for the threads, the allowed cores not known */
    memset(&c, 0, sizeof(c));
    (void)pin_release_trace_text(&p, &s, &c, 0, 0, &ring, b, sizeof b);
    CHECK(strstr(b, "; the render thread's core not sampled; the game thread's core not sampled; "
                    "their cores' clocks in that second (MHz): none run on") != NULL);
    /* the block: 29 + 10 on 7, 1 + 10 on 6 (50 samples; the unknown one
       left out of the shares); the game thread 30 on 6, 20 on 5 */
    memset(mhz, 0, sizeof(mhz));
    mhz[7] = 1180.0;
    mhz[5] = 950.0;
    (void)pin_release_block_text(&p, mhz, 8, NULL, b, sizeof b);
    CHECK_STR(b, "; the render thread's cores: 7 78 % (1180 MHz), 6 22 % (clock not sampled), "
                 "core changes 2; the game thread's: 6 60 % (clock not sampled), 5 40 % (950 "
                 "MHz), core changes 1; pins let go 1, taken again 0, let go for 1 s of the block "
                 "(at one a tick 1 s pinned, 1 s let go)");
    /* a short buffer: cut and terminated, the whole length returned */
    {
        char t[16];
        const int n = pin_release_trace_text(&p, &s, &c, 0, 0, NULL, t, sizeof t);

        CHECK(n > 100 && strlen(t) == 15u);
    }
}

int main(void)
{
    test_rule();
    test_states();
    test_cores_and_trace();
    if (failures) {
        printf("pin_release_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("pin_release_test: ok\n");
    return 0;
}
