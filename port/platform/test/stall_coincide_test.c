/*
 * port/platform/test/stall_coincide_test.c
 *
 * What a stall coincided with (stall_coincide.h): the block's medians (none
 * before five, the ring's last 64, a full table), the spike rule at its
 * bounds, the classes of a moment (issue 179's: memory under the floor;
 * a core wait; pressure rising; the GPU clock low; none; one piece grew and
 * the GPU slow as a whole from the pieces against their medians), the tally and
 * moment texts word for word, the pressure and status parsers, the
 * driver's thread names (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "stall_coincide.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void text_is(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n got  %s\n want %s\n", line, got, want);
        failures++;
    }
}

static void moment_179(StallMoment *m)
{
    memset(m, 0, sizeof(*m));
    m->gameCore = 6;
    m->gameMhz = 909;
    m->renderCore = 7;
    m->renderMhz = 1443;
    m->haveWaits = true;
    const double w[STALL_WAIT_THREADS] = {60, 40, 5, 3, -1};
    memcpy(m->waitMs, w, sizeof(w));
    m->drivers = 1;
    snprintf(m->driverName[0], sizeof m->driverName[0], "mali-cmar-backe");
    m->driverRunMs[0] = 120;
    m->driverWaitMs[0] = 30;
    m->availMb = 501;
    m->floorMb = 641;
    m->pressure = 1;
    m->some10 = 12.3;
    m->full10 = 4.1;
    m->some10Before = 12.3;
    m->headroom = 0.80;
    m->gpuMhz = m->gpuMaxMhz = -1;
}

/* one block of n stalls through the line rules: the tally's counts, the
   lines written returned */
static unsigned budget_block(StallLineBudget *b, StallTally *t, unsigned n)
{
    StallMoment m;
    memset(&m, 0, sizeof(m));
    m.gpuMhz = m.gpuMaxMhz = -1;
    m.availMb = m.floorMb = -1;
    m.pressure = -1;
    memset(t, 0, sizeof(*t));
    unsigned w = 0;
    for (unsigned i = 0; i < n; i++) {
        stall_tally_add(t, &m);
        w += stall_line_due(t, b) ? 1u : 0u;
    }
    CHECK(t->stalls == n && t->written + t->leftOut == n && t->written == w);
    return w;
}

/* The per-stall lines at the two rates of review 1 (stall_coincide.h):
   issue 123's tablet (its log of 2026-11-14: the block tallies 1, 1, 2, 1,
   4, 1, 1, 1, 3 stalls in 33 blocks of 10 s, its 13 present stalls among
   them) for 11 times its 325 s, every stall written; a stall every picture
   at 60 a second for an hour (360 blocks of 600): the rare rule's 500 lines,
   then v0.11.6's rule over the 215500 others (8 + 2155), every stall
   counted in its block's tally */
static void budget_cases(void)
{
    static const unsigned k_tablet[33] = {[3] = 1,  [6] = 1,  [9] = 2,  [12] = 1, [14] = 4,
                                          [17] = 1, [19] = 1, [26] = 1, [30] = 3};
    StallLineBudget b;
    StallTally t;
    unsigned long long stalls = 0, lines = 0;

    memset(&b, 0, sizeof(b));
    for (unsigned rep = 0; rep < 11u; rep++) {
        for (unsigned k = 0; k < 33u; k++) {
            lines += budget_block(&b, &t, k_tablet[k]);
            CHECK(t.leftOut == 0u);
            stalls += k_tablet[k];
        }
    }
    CHECK(stalls == 165u && lines == 165u && b.rare == 165u && b.excess == 0u);

    memset(&b, 0, sizeof(b));
    stalls = lines = 0;
    for (unsigned k = 0; k < 360u; k++) {
        lines += budget_block(&b, &t, 600u);
        stalls += 600u;
    }
    CHECK(stalls == 216000u && b.rare == STALL_RUN_LINES && b.excess == stalls - STALL_RUN_LINES);
    CHECK(lines == STALL_RUN_LINES + STALL_EXCESS_FIRST + b.excess / STALL_EXCESS_EVERY);
    CHECK(lines == 2663u);
    CHECK(t.written == 6u && t.leftOut == 594u); /* the last block: the 100ths alone */
}

/* issue 179's classes (the v0.11.7 log's stalls all "none of these"): the
   pieces against their medians, the two classes at their edges, the line's
   clauses word for word, the median peeked without adding */
static void pieces_cases(void)
{
    StallMoment m;
    char t[1024];
    /* list 0 at 6x its median, the others near theirs: one piece grew */
    {
        const double ms[4] = {120.0, 9.0, 2.5, 2.0}, med[4] = {20.0, 8.0, 2.0, 2.0};
        memset(&m, 0, sizeof m);
        m.gameCore = m.renderCore = -1;
        m.gpuMhz = m.gpuMaxMhz = -1;
        m.availMb = m.floorMb = -1;
        m.pressure = -1;
        m.headroom = -1.0;
        CHECK(stall_pieces_judge(&m, ms, med, 4) == 0);
        CHECK(m.pieceN == 4u && m.grown == 1u && m.pieceFactor == 6.0);
        CHECK(m.othersFactor == 1.125); /* 1.0, 1.125, 1.25: the middle */
        CHECK(stall_classes(&m) == 1u << STALL_CLASS_ONE_PIECE);
        snprintf(m.pieceName, sizeof m.pieceName, "list 0");
        m.idleKnown = true;
        m.idleBeforeMs = 0.2;
        m.trisKnown = true;
        m.tris = 13000.0;
        m.trisMedian = 12000.0;
        (void)stall_moment_text(&m, t, sizeof t);
        text_is(t,
                "the game thread's core not known; the core waits not sampled; device memory not "
                "known; its 4 timed pieces against their block medians: list 0 x6.0, the others "
                "x1.1 (their median; 1 of 4 at x2 or more); the GPU idle 0.2 ms before its "
                "work; 13000 VU triangles against the block's median 12000 (x1.08); coincided "
                "with: one piece grew",
                __LINE__);
        /* the others' median at the bound: not one piece */
        const double ms2[3] = {120.0, 16.0, 1.0}, med2[3] = {20.0, 8.0, 1.0};
        (void)stall_pieces_judge(&m, ms2, med2, 3);
        CHECK(m.pieceN == 3u && m.grown == 2u && m.othersFactor == 1.5);
        CHECK(stall_classes(&m) == 1u << STALL_CLASS_ONE_PIECE);
        const double ms3[3] = {120.0, 16.0, 2.0}, med3[3] = {20.0, 8.0, 1.0};
        (void)stall_pieces_judge(&m, ms3, med3, 3);
        CHECK(m.othersFactor == 2.0 && m.grown == 3u);
        CHECK(stall_classes(&m) == 1u << STALL_CLASS_GPU_WHOLE);
    }
    /* every piece grew: the GPU slow as a whole, the range of factors; a
       piece with no median left out; begun before the one before ended */
    {
        const double ms[4] = {60.0, 9.0, 5.0, 7.0}, med[4] = {20.0, 3.0, -1.0, 2.0};
        memset(&m, 0, sizeof m);
        m.gameCore = m.renderCore = -1;
        m.gpuMhz = m.gpuMaxMhz = -1;
        m.availMb = m.floorMb = -1;
        m.pressure = -1;
        m.headroom = -1.0;
        CHECK(stall_pieces_judge(&m, ms, med, 4) == 3); /* 3.5x */
        CHECK(m.pieceN == 3u && m.grown == 3u && m.factorMin == 3.0 && m.factorMax == 3.5);
        CHECK(stall_classes(&m) == 1u << STALL_CLASS_GPU_WHOLE);
        m.idleKnown = true;
        m.idleBeforeMs = -1.5;
        (void)stall_moment_text(&m, t, sizeof t);
        text_is(t,
                "the game thread's core not known; the core waits not sampled; device memory not "
                "known; its 3 timed pieces against their block medians: every piece grew, x3.0 "
                "to x3.5; its work begun 1.5 ms before the picture before ended; coincided with: "
                "the GPU slow as a whole",
                __LINE__);
    }
    /* one piece alone: no class of the two; none with a median: nothing */
    {
        const double ms[1] = {90.0}, med[1] = {10.0};
        memset(&m, 0, sizeof m);
        m.availMb = m.floorMb = -1;
        m.gpuMhz = m.gpuMaxMhz = -1;
        CHECK(stall_pieces_judge(&m, ms, med, 1) == 0 && m.pieceN == 1u);
        CHECK(stall_classes(&m) == 1u << STALL_CLASS_NONE);
        const double none[1] = {-1.0};
        CHECK(stall_pieces_judge(&m, ms, none, 1) == -1 && m.pieceN == 0u);
    }
    /* the median peeked: as add's, nothing added */
    {
        StallMedians md;
        stall_medians_reset(&md);
        CHECK(stall_medians_peek(&md, 3) < 0.0);
        for (int i = 0; i < 5; i++) {
            (void)stall_medians_add(&md, 3, 10.0 + i);
        }
        CHECK(stall_medians_peek(&md, 3) == 12.0 && md.k[0].n == 5u);
        CHECK(stall_medians_add(&md, 3, 1.0) == 12.0);
        CHECK(stall_medians_peek(&md, 4) < 0.0);
    }
}

int main(void)
{
    StallMedians md;
    stall_medians_reset(&md);
    /* none before five of the key */
    for (int i = 0; i < 5; i++) {
        CHECK(stall_medians_add(&md, 7, 10.0 + i) < 0.0);
    }
    CHECK(stall_medians_add(&md, 7, 100.0) == 12.0); /* 10..14 */
    CHECK(stall_medians_add(&md, 7, 0.0) == 12.5);   /* 10..14, 100: even count */
    CHECK(stall_medians_add(&md, 8, 1.0) < 0.0);     /* another key */
    /* the ring keeps the last 64 */
    for (int i = 0; i < 200; i++) {
        (void)stall_medians_add(&md, 9, i < 136 ? 1000.0 : 2.0);
    }
    CHECK(stall_medians_add(&md, 9, 2.0) == 2.0);
    /* a full table drops a new key */
    for (uint32_t k = 100; md.used < STALL_KEYS; k++) {
        (void)stall_medians_add(&md, k, 1.0);
    }
    CHECK(stall_medians_add(&md, 99999, 1.0) < 0.0);

    /* the rule: over 8x and over 40 ms */
    CHECK(stall_pass_spike(283.3, 48.0) == false); /* 5.9x */
    CHECK(stall_pass_spike(400.0, 48.0) == true);
    CHECK(stall_pass_spike(40.0, 4.0) == false); /* not over 40 ms */
    CHECK(stall_pass_spike(40.1, 4.0) == true);
    CHECK(stall_pass_spike(500.0, -1.0) == false); /* no median */

    StallMoment m;
    moment_179(&m);
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_MEMORY);
    char t[1024];
    (void)stall_moment_text(&m, t, sizeof t);
    text_is(t,
            "the game thread on core 6 (909 MHz), the render thread on core 7 (1443 MHz); in the "
            "last second the core waits: game 60 ms, render 40 ms, submit 5 ms, SDL audio 3 ms; "
            "the driver's threads: mali-cmar-backe ran 120 ms, waited 30 ms; memory 501 MB "
            "available against the floor 641 MB (under it); pressure some 12.3 %, full 4.1 % "
            "(avg10; some 12.3 % a second before: not rising); thermal headroom 0.80; coincided "
            "with: memory under the floor",
            __LINE__);

    StallTally ty;
    memset(&ty, 0, sizeof ty);
    (void)stall_tally_text(&ty, t, sizeof t);
    text_is(t, "stalls by what coincided: 0 in the block", __LINE__);
    stall_tally_add(&ty, &m);
    m.waitMs[1] = 150.0;  /* the render thread waited */
    m.some10Before = 3.2; /* rising */
    CHECK(stall_classes(&m) ==
          ((1u << STALL_CLASS_WAIT) | (1u << STALL_CLASS_MEMORY) | (1u << STALL_CLASS_PRESSURE)));
    stall_tally_add(&ty, &m);
    moment_179(&m);
    m.availMb = 900; /* above the floor */
    m.some10Before = -1.0;
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_NONE);
    stall_tally_add(&ty, &m);
    (void)stall_tally_text(&ty, t, sizeof t);
    text_is(t,
            "stalls by what coincided: 3 in the block, 0 of them hangs (the GPU's own sign of a "
            "reset with a stamp unwritten) and 3 other; all 3 by class: a core wait over 100 ms "
            "1, memory under the floor 2, memory pressure rising 1, the GPU clock low 0 (not "
            "readable), the GPU's own sign of a reset 0, one piece grew 0, the GPU slow as a "
            "whole 0, none of these 1",
            __LINE__);
    /* the GPU clock where readable */
    m.gpuMhz = 300;
    m.gpuMaxMhz = 950;
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_GPU);
    m.pressure = 0;
    m.pressureErr = 13;
    m.haveWaits = false;
    m.gameCore = -1;
    m.renderCore = -1;
    m.drivers = 0;
    m.headroom = -1.0;
    m.availMb = -1;
    (void)stall_moment_text(&m, t, sizeof t);
    text_is(t,
            "the game thread's core not known; the core waits not sampled; device memory not "
            "known; memory pressure not readable (errno 13); the GPU clock 300 MHz of 950; "
            "coincided with: the GPU clock low",
            __LINE__);
    {
        char s[16];
        const size_t n = stall_moment_text(&m, s, sizeof s);
        CHECK(n == strlen(s) && n == sizeof s - 1u);
    }

    /* issue 123's Adreno 740 (v0.11.5 log, replay 689: 19 of 24 stamps
       unwritten after its work was signalled finished, the present of 690
       blocked 402 ms): KGSL not readable, memory under Qualcomm's floor all
       run, so the reset's own class is what tells these stalls apart */
    memset(&m, 0, sizeof m);
    m.gameCore = 6;
    m.gameMhz = 1920;
    m.renderCore = -1;
    m.availMb = 1716;
    m.floorMb = 1947;
    m.pressure = -1;
    m.some10Before = -1.0;
    m.headroom = 0.65;
    m.gpuMhz = m.gpuMaxMhz = -1;
    m.hangReplay = 689;
    m.stampsMissing = 19;
    CHECK(stall_classes(&m) == ((1u << STALL_CLASS_MEMORY) | (1u << STALL_CLASS_RESET)));
    (void)stall_moment_text(&m, t, sizeof t);
    text_is(t,
            "the game thread on core 6 (1920 MHz); the core waits not sampled; memory 1716 MB "
            "available against the floor 1947 MB (under it); thermal headroom 0.65; the GPU's own "
            "sign of a reset at replay 689: 19 stamps came back unwritten after its work was "
            "signalled finished (a hang); coincided with: memory under the floor, the GPU's own "
            "sign of a reset",
            __LINE__);
    /* v0.11.6's shape: the hung replay's stamps and the next one's clock */
    snprintf(m.firstMissing, sizeof m.firstMissing, "list 0's end");
    m.resetReplay = 690;
    m.clockBack = true;
    (void)stall_moment_text(&m, t, sizeof t);
    CHECK(strstr(t, "; the GPU's own sign of a reset at replay 689: 19 stamps came back unwritten "
                    "after its work was signalled finished (the first: list 0's end), and at "
                    "replay 690: the GPU's clock restarted (a hang); coincided with: ") != NULL);
    /* a sign without a hang (issue 123's 197 ms present at :2842: replay
       10911's restart after 10910's hang, two replays back) */
    m.hangReplay = 0;
    m.stampsMissing = 0;
    m.resetReplay = 10911;
    m.ranTwice = true;
    CHECK(stall_classes(&m) == ((1u << STALL_CLASS_MEMORY) | (1u << STALL_CLASS_RESET)));
    (void)stall_moment_text(&m, t, sizeof t);
    CHECK(strstr(t,
                 "; the GPU's own sign of a reset at replay 10911: its first list ran twice, the "
                 "GPU's clock restarted (not a hang: no stamp unwritten in the stalled replay "
                 "or the two before it); coincided with: ") != NULL);
    /* a restart after a span with no GPU work: said, not a sign (issue
       123's capture at :2736) */
    m.resetReplay = 0;
    m.ranTwice = m.clockBack = false;
    m.idleReplay = 10621;
    m.idleMs = 1150.5;
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_MEMORY);
    (void)stall_moment_text(&m, t, sizeof t);
    CHECK(strstr(t, "; the GPU's clock restarted at replay 10621 after 1150.5 ms with no GPU work: "
                    "not a sign of a reset; coincided with: memory under the floor") != NULL);
    m.idleMs = -1.0;
    (void)stall_moment_text(&m, t, sizeof t);
    CHECK(strstr(t,
                 "; the GPU's clock restarted at replay 10621 after a span the game was not "
                 "presenting (a pause, a capture, the app away): not a sign of a reset;") != NULL);
    m.idleReplay = 0;

    /* the hangs apart in the tally, and the block's cap: issue 123's block
       at :2900 (three hangs, the 197 ms present after them) */
    {
        StallTally h;
        memset(&h, 0, sizeof h);
        StallMoment hm;
        memset(&hm, 0, sizeof hm);
        hm.gpuMhz = hm.gpuMaxMhz = -1;
        hm.availMb = hm.floorMb = -1;
        hm.pressure = -1;
        hm.hangReplay = 10905;
        stall_tally_add(&h, &hm);
        stall_tally_add(&h, &hm);
        stall_tally_add(&h, &hm);
        hm.hangReplay = 0;
        hm.resetReplay = 10911;
        stall_tally_add(&h, &hm);
        CHECK(h.stalls == 4 && h.hangs == 3 && h.cls[STALL_CLASS_RESET] == 4);
        (void)stall_tally_text(&h, t, sizeof t);
        text_is(t,
                "stalls by what coincided: 4 in the block, 3 of them hangs (the GPU's own sign of "
                "a reset with a stamp unwritten) and 1 other; all 4 by class: a core wait over 100 "
                "ms 0, memory under the floor 0, memory pressure rising 0, the GPU clock low 0 "
                "(not readable), the GPU's own sign of a reset 4, one piece grew 0, the GPU slow "
                "as a whole 0, none of these 0",
                __LINE__);
        /* a block of 352 stalls on a fresh run: its first 16 by the rare
           rule, then v0.11.6's rule over the 336 others (the first 8, the
           100th, 200th and 300th): 27 written, 325 left out */
        StallLineBudget b;
        memset(&b, 0, sizeof(b));
        unsigned due = 0;
        for (unsigned i = 0; i < 352u; i++) {
            due += stall_line_due(&h, &b) ? 1u : 0u;
        }
        CHECK(due == 27u && h.written == 27u && h.leftOut == 325u);
        CHECK(b.rare == 16u && b.excess == 336u);
        (void)stall_tally_text(&h, t, sizeof t);
        CHECK(strstr(t, "none of these 0; 325 stall lines left out (27 written: a block's first "
                        "16 while the run has written fewer than 500, then the first 8 and every "
                        "100th of the others)") != NULL);
    }

    /* the submissions around a stall: issue 123's first (replay 2074's
       scene list held list 0's end, the first unwritten; the present of
       2075 blocked 365 ms from 0 ms), the times as the records would carry
       them */
    {
        StallMoment sm;
        memset(&sm, 0, sizeof sm);
        sm.gameCore = sm.renderCore = -1;
        sm.gpuMhz = sm.gpuMaxMhz = -1;
        sm.availMb = sm.floorMb = -1;
        sm.pressure = -1;
        sm.headroom = -1.0;
        sm.subs = 4;
        const StallSub s0 = {2074, 1, 2, 1, 0, 0, 1, 1, 0, 0, 12, 23, 25, 20, -21.5, 3, 365.4, 0};
        const StallSub s1 = {2074, 2, 2, 1, 1, 0, 0, 1, 1, -1, -1, 0, 5, 0, -21.1, 1, 365.4, 0};
        const StallSub s2 = {2075, 1, 2, 1, 0, 0, 0, 0, 0, -1, -1, 0, 0, 0, -1.2, 0, 0.0, 0};
        const StallSub s3 = {2075, 2, 2, 1, 1, 1, 0, 0, 0, -1, -1, 0, 0, 0, -0.4, 2, 2.0, 0};
        sm.sub[0] = s0;
        sm.sub[1] = s1;
        sm.sub[2] = s2;
        sm.sub[3] = s3;
        (void)stall_moment_text(&sm, t, sizeof t);
        text_is(t,
                "the game thread's core not known; the core waits not sampled; device memory not "
                "known; the submissions around it (from the stall's start): replay 2074's 1 of 2 "
                "(lists 0-12, 23 pieces; 25 stamps, 20 unwritten; it holds the first unwritten "
                "stamp) submitted at -21.5 ms, signalled at or before +365.4 ms (its replay's "
                "last's; not seen apart); replay 2074's 2 of 2 (the present's list, its replay's "
                "present waits on it; 5 stamps, all written) submitted at -21.1 ms, signalled at "
                "+365.4 ms (the slot's wait's end); replay 2075's 1 of 2 (1 list) submitted at "
                "-1.2 ms, its signal not known; replay 2075's 2 of 2 (1 list, the stalled present "
                "waits on it) submitted at -0.4 ms, signalled at or before +2.0 ms (finished when "
                "the slot's wait began); in flight at the stall's start: 1 seen, 3 not known; "
                "coincided with: none of these",
                __LINE__);
        /* under the pause menu (rd_hold_replay_kind): the scene drawn in full
           by the hung replay, its next one held */
        sm.sub[0].scene = 2;
        sm.sub[2].scene = 1;
        (void)stall_moment_text(&sm, t, sizeof t);
        CHECK(strstr(t, "replay 2074's 1 of 2 (lists 0-12, menu open, scene replayed, 23 pieces; "
                        "25 stamps, 20 unwritten;") != NULL);
        CHECK(strstr(t, "replay 2075's 1 of 2 (1 list, menu open, scene held) submitted at "
                        "-1.2 ms") != NULL);
    }

    /* the parsers */
    double some = -1, full = -1;
    const char *pr = "some avg10=12.34 avg60=1.00 avg300=0.50 total=123\n"
                     "full avg10=4.10 avg60=0.20 avg300=0.10 total=45\n";
    CHECK(stall_pressure_parse(pr, strlen(pr), &some, &full) && some == 12.34 && full == 4.10);
    CHECK(!stall_pressure_parse("some avg10=1.0\n", 15, &some, &full));
    long a = 0, f = 0, s = 0;
    const char *st = "Name:\tico-main\nVmRSS:\t  839000 kB\nRssAnon:\t  412000 kB\n"
                     "RssFile:\t  420000 kB\nRssShmem:\t    7000 kB\n";
    CHECK(stall_status_parse(st, strlen(st), &a, &f, &s) && a == 412000 && f == 420000 &&
          s == 7000);
    CHECK(!stall_status_parse("VmRSS: 1 kB\n", 12, &a, &f, &s));
    /* VmSwap: the pages moved out to swap (zram), past RssShmem */
    {
        const char *sw = "VmRSS:\t  839000 kB\nRssAnon:\t  412000 kB\nVmPTE:\t 1200 kB\n"
                         "VmSwap:\t  301234 kB\nHugetlbPages:\t 0 kB\n";
        long w = -1;
        CHECK(stall_status_swap(sw, strlen(sw), &w) && w == 301234);
        CHECK(!stall_status_swap(st, strlen(st), &w));
    }

    budget_cases();
    pieces_cases();

    /* the driver's thread names */
    CHECK(stall_driver_name("mali-cmar-backe"));
    CHECK(stall_driver_name("MALI-utility-wo"));
    CHECK(stall_driver_name("kgsl_worker_thr"));
    CHECK(stall_driver_name("QGLThread"));
    CHECK(!stall_driver_name("ico-gpu-sysfs"));
    CHECK(!stall_driver_name("SDLAudioP15"));
    CHECK(!stall_driver_name("ico render"));

    if (failures) {
        printf("stall_coincide_test: %d failures\n", failures);
        return 1;
    }
    printf("stall_coincide_test: ok\n");
    return 0;
}
