/* rd_health_test.c: the picture-health guard's pure parts (rd_health.h,
 * CPU): the rule fed the patterns of the four logs it was checked against
 * (issue 179's two v0.11.6 Mali-G52 runs and its v0.11.5 one, issue 123's
 * Adreno 740 runs), an idle GPU's restarts, the window's threshold, the
 * steps' groups and parts, a recovery naming the culprit, no recovery
 * ending without a loop, the next start from each kind of mark, the mark's
 * text both ways and its start rule (kept, dropped after an update or in
 * another form), the parts' words. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_health.h"
#include "rd_perf_clock.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define P(x) (1u << (x))
#define G(x) (1u << (x))

enum { GA = 0, GB, GC, GD, GE };

/* every part on (a device where all of v0.11.6's are) */
#define ALL_PARTS ((1u << RD_HEALTH_PARTS) - 1u)
/* the Mali-G52 under v0.11.7: the compaction off by default */
#define MALI_PARTS (ALL_PARTS & ~P(RD_HEALTH_P_COMPACT))

/* one judged picture: the rule's verdict, a step taken where it triggers
   (the window's settle: replay + RD_HEALTH_SETTLE, as rd_health_live.c) */
static int feed(RdHealth *h, uint32_t replay, int sign, uint32_t applicable, RdHealthAct *a)
{
    const RdHealthPic p = {replay, true, sign};
    int k = rd_health_pic(h, &p, a);
    if (k == RD_HEALTH_TRIGGER_HIT) {
        rd_health_take(h, applicable, replay + RD_HEALTH_SETTLE, a);
        k = a->kind;
    }
    return k;
}

/* issue 179's v0.11.6 runs: every 3D picture from replay 149 on unfinished
   (its two logs: stall lines 1 to 32 at replays 149 to 180, then every
   10th to the end) */
static void test_mali(void)
{
    RdHealth h;
    RdHealthAct a;
    rd_health_reset(&h);
    uint32_t stepAt[8], exhaustedAt = 0;
    RdHealthAct steps[8];
    int n = 0;
    for (uint32_t r = 149; r < 2000; r++) {
        const int k = feed(&h, r, RD_RESET_HANG, MALI_PARTS, &a);
        if (k == RD_HEALTH_STEP && n < 8) {
            steps[n] = a;
            stepAt[n++] = r;
        } else if (k == RD_HEALTH_EXHAUSTED) {
            CHECK(exhaustedAt == 0); /* once */
            exhaustedAt = r;
            CHECK(a.pendingGroups == (G(GC) | G(GE)));
        } else {
            CHECK(k == RD_HEALTH_NONE);
        }
    }
    /* the 16th unfinished picture steps (replay 164, 0.6 s of 3D at 25
       fps); each later step 15 judged replays after the settle */
    CHECK(n == 4);
    CHECK(stepAt[0] == 164 && stepAt[1] == 182 && stepAt[2] == 200 && stepAt[3] == 218);
    CHECK(exhaustedAt == 236);
    CHECK(steps[0].fails == 16 && steps[0].judged == 16);
    /* (a) not on here: (b) first */
    CHECK(steps[0].groups == G(GB) && steps[0].live == P(RD_HEALTH_P_REC) && steps[0].next == 0);
    CHECK(steps[1].groups == G(GC) && steps[1].live == P(RD_HEALTH_P_DATE_ONE) &&
          steps[1].next == P(RD_HEALTH_P_FLAT0));
    CHECK(steps[2].groups == G(GD) &&
          steps[2].live ==
              (P(RD_HEALTH_P_VOLUME_FS) | P(RD_HEALTH_P_COMP_SPEC) | P(RD_HEALTH_P_PASTE_SPEC)));
    CHECK(steps[3].groups == G(GE) && steps[3].live == P(RD_HEALTH_P_EMPTY_SKIP) &&
          steps[3].next == P(RD_HEALTH_P_ROBUST));
    CHECK(h.phase == RD_HEALTH_DONE && h.off == MALI_PARTS);
    /* the mark it leaves: the restart parts pending */
    RdHealthMark m;
    rd_health_mark_of(&h, "0.11.7", "16 of 16", &m);
    CHECK(m.groups == (G(GB) | G(GC) | G(GD) | G(GE)) && m.state == RD_HEALTH_MARK_PENDING &&
          m.named == (G(GC) | G(GE)));

    /* the PowerVR class with the compaction on: (a) first */
    rd_health_reset(&h);
    int k = RD_HEALTH_NONE;
    uint32_t r = 1;
    for (; r < 100 && k == RD_HEALTH_NONE; r++) {
        k = feed(&h, r, RD_RESET_HANG, ALL_PARTS, &a);
    }
    CHECK(k == RD_HEALTH_STEP && r - 1 == 16 && a.groups == G(GA) &&
          a.live == P(RD_HEALTH_P_COMPACT));
    /* a group with no part on here is passed over */
    rd_health_reset(&h);
    for (k = RD_HEALTH_NONE, r = 1; r < 100 && k == RD_HEALTH_NONE; r++) {
        k = feed(&h, r, RD_RESET_RESTART, P(RD_HEALTH_P_PASTE_SPEC), &a); /* restarts count */
    }
    CHECK(k == RD_HEALTH_STEP && a.groups == G(GD) && a.live == P(RD_HEALTH_P_PASTE_SPEC));
}

/* the logs where it must not step */
static void test_quiet(void)
{
    RdHealth h;
    RdHealthAct a;
    /* issue 179's v0.11.5 run: no stamp unwritten, no restart (its 110
       stall lines are waits of 100 ms or more: no sign) */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 8000; r++) {
        CHECK(feed(&h, r, RD_RESET_NONE, MALI_PARTS, &a) == RD_HEALTH_NONE);
    }
    /* issue 123's Adreno 740, v0.11.6: 13 hangs in about 325 s at its
       picture rate (DIAG3's table: replays 2074 ... 14734), the closest 3
       within 6 replays, a counted restart beside the 10910 hang, at most 4
       restarts in a 10 s block (here all 4 beside the closest hangs), and
       its 8 start-up restarts (replays 8 to 26) counted too */
    static const uint32_t hangs[] = {2074,  6160,  8148,  8237,  10905, 10910, 11089,
                                     11311, 11970, 14437, 14729, 14731, 14734};
    static const uint32_t restarts[] = {8,  11,    17,    19,    21,    24,   25,
                                        26, 10911, 14728, 14730, 14732, 14733};
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 16300; r++) {
        int sign = RD_RESET_NONE;
        for (size_t i = 0; i < sizeof hangs / sizeof hangs[0]; i++) {
            sign = hangs[i] == r ? RD_RESET_HANG : sign;
        }
        for (size_t i = 0; i < sizeof restarts / sizeof restarts[0]; i++) {
            sign = restarts[i] == r ? RD_RESET_RESTART : sign;
        }
        CHECK(feed(&h, r, sign, ALL_PARTS, &a) == RD_HEALTH_NONE);
    }
    CHECK(h.steps == 0 && h.off == 0);
    /* an idle GPU's restarts (DIAG3's idle sign: the app away, a pause, a
       capture) on every picture */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 500; r++) {
        CHECK(feed(&h, r, RD_RESET_IDLE, ALL_PARTS, &a) == RD_HEALTH_NONE);
    }
    /* pictures not judged (2D, not presented) never count */
    rd_health_reset(&h);
    for (uint32_t r = 1; r < 500; r++) {
        const RdHealthPic p = {r, false, RD_RESET_HANG};
        CHECK(rd_health_pic(&h, &p, &a) == RD_HEALTH_NONE);
    }
    /* the window: 15 of 32 never steps, the 16th of 32 does */
    rd_health_reset(&h);
    int k = RD_HEALTH_NONE;
    uint32_t r;
    for (r = 0; r < 31 && k == RD_HEALTH_NONE; r++) {
        k = feed(&h, r + 1, r % 2 == 0 ? RD_RESET_HANG : RD_RESET_NONE, ALL_PARTS, &a);
        if (r < 30) {
            CHECK(k == RD_HEALTH_NONE);
        }
    }
    CHECK(k == RD_HEALTH_STEP && r == 31 && a.fails == 16 && a.judged == 31);
}

/* pictures complete again after a step: that group named */
static void test_recovered(void)
{
    RdHealth h;
    RdHealthAct a;
    rd_health_reset(&h);
    uint32_t recoveredAt = 0;
    for (uint32_t r = 149; r < 1000; r++) {
        /* unfinished until (c) is off (taken at replay 182, judged from 185) */
        const int sign = (h.off & P(RD_HEALTH_P_DATE_ONE)) ? RD_RESET_NONE : RD_RESET_HANG;
        const int k = feed(&h, r, sign, MALI_PARTS, &a);
        if (k == RD_HEALTH_RECOVERED) {
            CHECK(recoveredAt == 0);
            recoveredAt = r;
            CHECK(a.culprit == G(GC) && !a.fromStart);
        }
    }
    /* 64 in a row from replay 185 */
    CHECK(recoveredAt == 185 + RD_HEALTH_CLEAN - 1);
    CHECK(h.culprit == G(GC) && h.phase == RD_HEALTH_WATCH && h.steps == 2);
    RdHealthMark m;
    rd_health_mark_of(&h, "0.11.7", "", &m);
    /* the culprit confirmed in the run (its flat outputs, taken with it
       from the next start, go with its group's letter) */
    CHECK(m.groups == (G(GB) | G(GC)) && m.state == RD_HEALTH_MARK_CULPRIT && m.named == G(GC));
    /* a few isolated unfinished pictures after a step do not undo it, five
       in the window restart the count */
    rd_health_reset(&h);
    recoveredAt = 0;
    for (uint32_t r = 1; r < 400; r++) {
        int sign = (h.off & P(RD_HEALTH_P_REC)) ? RD_RESET_NONE : RD_RESET_HANG;
        if (h.off != 0 && (r == 30 || r == 31 || r == 40 || r == 41)) {
            sign = RD_RESET_HANG; /* 4 in the window: still clean */
        }
        if (feed(&h, r, sign, MALI_PARTS, &a) == RD_HEALTH_RECOVERED) {
            recoveredAt = r;
        }
    }
    /* stepped at 16 (b), judged from 19: 64 in a row end at 82 */
    CHECK(recoveredAt == 19 + RD_HEALTH_CLEAN - 1);
    rd_health_reset(&h);
    recoveredAt = 0;
    for (uint32_t r = 1; r < 400; r++) {
        int sign = (h.off & P(RD_HEALTH_P_REC)) ? RD_RESET_NONE : RD_RESET_HANG;
        if (h.off != 0 && r >= 30 && r < 35) {
            sign = RD_RESET_HANG; /* 5 in the window: the count starts again after them */
        }
        if (feed(&h, r, sign, MALI_PARTS, &a) == RD_HEALTH_RECOVERED) {
            recoveredAt = r;
        }
    }
    /* clean again once the window holds 4 or fewer: the 5 leave it 32
       judged pictures after the first of them (replay 62), then 64 */
    CHECK(recoveredAt == 62 + RD_HEALTH_CLEAN - 1);
}

/* the next start from each mark */
static void test_next_start(void)
{
    RdHealth h;
    RdHealthAct a;
    RdHealthMark m;
    /* pending (the Mali run's mark): the restart parts confirmed by this
       start's pictures */
    memset(&m, 0, sizeof m);
    m.groups = G(GB) | G(GC) | G(GD) | G(GE);
    m.state = RD_HEALTH_MARK_PENDING;
    m.named = G(GC) | G(GE);
    rd_health_begin(&h, &m);
    CHECK(h.phase == RD_HEALTH_AFTER && h.fromStart && h.lastGroups == (G(GC) | G(GE)));
    CHECK(h.off == rd_health_parts_of(m.groups) && h.nextGroup == RD_HEALTH_GROUPS);
    uint32_t at = 0;
    for (uint32_t r = 1; r < 200; r++) {
        if (feed(&h, r, RD_RESET_NONE, MALI_PARTS, &a) == RD_HEALTH_RECOVERED) {
            CHECK(at == 0 && a.fromStart && a.culprit == (G(GC) | G(GE)));
            at = r;
        }
    }
    CHECK(at == RD_HEALTH_CLEAN);
    rd_health_mark_of(&h, "0.11.7", "", &m);
    CHECK(m.state == RD_HEALTH_MARK_CULPRIT && m.named == (G(GC) | G(GE)));
    /* ... and still unfinished: nothing left, said once, no loop */
    m.state = RD_HEALTH_MARK_PENDING;
    rd_health_begin(&h, &m);
    int exhausted = 0;
    for (uint32_t r = 1; r < 500; r++) {
        const int k = feed(&h, r, RD_RESET_HANG, MALI_PARTS, &a);
        exhausted += k == RD_HEALTH_EXHAUSTED;
        CHECK(k == RD_HEALTH_NONE || k == RD_HEALTH_EXHAUSTED);
    }
    CHECK(exhausted == 1 && h.phase == RD_HEALTH_DONE && h.pendingGroups == 0);
    rd_health_mark_of(&h, "0.11.7", "", &m);
    CHECK(m.state == RD_HEALTH_MARK_EXHAUSTED);
    /* exhausted: no step at all */
    rd_health_begin(&h, &m);
    CHECK(h.phase == RD_HEALTH_DONE);
    for (uint32_t r = 1; r < 200; r++) {
        CHECK(feed(&h, r, RD_RESET_HANG, MALI_PARTS, &a) == RD_HEALTH_NONE);
    }
    /* a culprit: watching, the next trigger goes on after it */
    memset(&m, 0, sizeof m);
    m.groups = G(GB);
    m.state = RD_HEALTH_MARK_CULPRIT;
    m.named = G(GB);
    rd_health_begin(&h, &m);
    CHECK(h.phase == RD_HEALTH_WATCH && h.culprit == G(GB) && h.nextGroup == GC);
    int k = RD_HEALTH_NONE;
    for (uint32_t r = 1; r < 100 && k == RD_HEALTH_NONE; r++) {
        k = feed(&h, r, RD_RESET_HANG, MALI_PARTS, &a);
    }
    CHECK(k == RD_HEALTH_STEP && a.groups == G(GC));
    /* stepping (a run that ended before a verdict): its last group confirmed */
    m.state = RD_HEALTH_MARK_STEPPING;
    m.named = 0;
    m.groups = G(GB) | G(GD);
    rd_health_begin(&h, &m);
    CHECK(h.phase == RD_HEALTH_AFTER && h.lastGroups == G(GD) && h.nextGroup == GE);
}

static void test_mark(void)
{
    RdHealthMark m, back;
    char value[16], by[288];
    memset(&m, 0, sizeof m);
    snprintf(m.version, sizeof m.version, "0.11.7");
    m.groups = G(GB) | G(GC);
    m.state = RD_HEALTH_MARK_CULPRIT;
    m.named = G(GC);
    snprintf(m.evidence, sizeof m.evidence, "complete again from replay 248, frame 223");
    CHECK(rd_health_mark_format(value, sizeof value, by, sizeof by, &m) == 0);
    CHECK(strcmp(value, "bc") == 0);
    CHECK(strcmp(by, "picture health 0.11.7; culprit c; complete again from replay 248, frame "
                     "223") == 0);
    CHECK(rd_health_mark_parse(value, by, &back) == 0);
    CHECK(strcmp(back.version, "0.11.7") == 0 && back.groups == m.groups && back.state == m.state &&
          back.named == m.named && strcmp(back.evidence, m.evidence) == 0);
    /* each state */
    m.state = RD_HEALTH_MARK_PENDING;
    m.named = G(GC) | G(GE);
    m.groups = G(GB) | G(GC) | G(GD) | G(GE);
    m.evidence[0] = '\0';
    CHECK(rd_health_mark_format(value, sizeof value, by, sizeof by, &m) == 0);
    CHECK(strcmp(value, "bcde") == 0 && strcmp(by, "picture health 0.11.7; pending ce") == 0);
    CHECK(rd_health_mark_parse(value, by, &back) == 0 && back.state == RD_HEALTH_MARK_PENDING &&
          back.named == (G(GC) | G(GE)) && back.evidence[0] == '\0');
    m.state = RD_HEALTH_MARK_EXHAUSTED;
    CHECK(rd_health_mark_format(value, sizeof value, by, sizeof by, &m) == 0);
    CHECK(strcmp(by, "picture health 0.11.7; exhausted") == 0);
    CHECK(rd_health_mark_parse(value, by, &back) == 0 && back.state == RD_HEALTH_MARK_EXHAUSTED);
    m.state = RD_HEALTH_MARK_STEPPING;
    snprintf(m.evidence, sizeof m.evidence, "16 of 16");
    CHECK(rd_health_mark_format(value, sizeof value, by, sizeof by, &m) == 0);
    CHECK(strcmp(by, "picture health 0.11.7; stepping; 16 of 16") == 0);
    CHECK(rd_health_mark_format(value, sizeof value, by, 10, &m) == -1);
    /* the start rule: this version's kept, another's and a foreign form
       dropped, absent nothing */
    CHECK(rd_health_mark_start("bc", "picture health 0.11.7; culprit c", "0.11.7", &back) ==
          RD_HEALTH_START_KEEP);
    CHECK(back.groups == (G(GB) | G(GC)) && back.named == G(GC));
    CHECK(rd_health_mark_start("bc", "picture health 0.11.7; culprit c", "0.11.8", &back) ==
          RD_HEALTH_START_DROP_VERSION);
    CHECK(rd_health_mark_start("", "", "0.11.7", &back) == RD_HEALTH_START_NONE);
    CHECK(rd_health_mark_start(NULL, NULL, "0.11.7", &back) == RD_HEALTH_START_NONE);
    CHECK(rd_health_mark_start("bz", "picture health 0.11.7; stepping", "0.11.7", &back) ==
          RD_HEALTH_START_DROP_FORM);
    CHECK(rd_health_mark_start("bc", "", "0.11.7", &back) == RD_HEALTH_START_DROP_FORM);
    CHECK(rd_health_mark_start("", "picture health 0.11.7; stepping", "0.11.7", &back) ==
          RD_HEALTH_START_DROP_FORM);
    CHECK(rd_health_mark_start("bc", "set by hand", "0.11.7", &back) == RD_HEALTH_START_DROP_FORM);
    CHECK(rd_health_mark_start("bc", "picture health 0.11.7; culprit d", "0.11.7", &back) ==
          RD_HEALTH_START_DROP_FORM);
    CHECK(rd_health_mark_start("bc", "picture health 0.11.7; sleeping", "0.11.7", &back) ==
          RD_HEALTH_START_DROP_FORM);
}

static void test_words(void)
{
    char t[512];
    CHECK(rd_health_group_letter(GA) == 'a' && rd_health_group_letter(GE) == 'e' &&
          rd_health_group_letter(5) == '?');
    CHECK(rd_health_groups_of(P(RD_HEALTH_P_ROBUST) | P(RD_HEALTH_P_REC)) == (G(GB) | G(GE)));
    CHECK(rd_health_live_parts() == (ALL_PARTS & ~P(RD_HEALTH_P_FLAT0) & ~P(RD_HEALTH_P_ROBUST)));
    rd_health_parts_text(t, sizeof t, rd_health_parts_of(G(GC) | G(GE)));
    CHECK(strcmp(t, "(c) one DATE set-up, (c) flat outputs at the provoking corner, (e) empty "
                    "composites skipped and (e) robust access off") == 0);
    rd_health_parts_text(t, sizeof t, P(RD_HEALTH_P_REC));
    CHECK(strcmp(t, "(b) the VU record sized from need") == 0);
    rd_health_parts_text(t, sizeof t, 0);
    CHECK(t[0] == '\0');
    rd_health_letters(t, sizeof t, G(GA) | G(GD));
    CHECK(strcmp(t, "ad") == 0);
}

int main(void)
{
    test_mali();
    test_quiet();
    test_recovered();
    test_next_start();
    test_mark();
    test_words();
    if (failures) {
        printf("rd_health_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_health_test: ok\n");
    return 0;
}
