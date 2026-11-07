/*
 * port/platform/test/bones_ladder_test.c
 *
 * Issue 123's ladder across starts (bones_ladder.h):
 *   - the step for a run's evidence, over sequences of starts as the window
 *     applies them (window_bones.c): the fault seen -> step 1 written ->
 *     clean over 2,000 presents: healed; still differing -> step 2 written
 *     (or the ladder stops where step 2 cannot reach the driver) -> still
 *     differing: turnip_debug = off, then "both tried" and nothing more;
 *     step 2 under step 1 only after 8 twin events; step 2 healed with step
 *     1 in force: turnip_debug = retest, the re-test clean: removed, the
 *     re-test differing: nobin marked needed, then kept; a nobin without
 *     the mark's word (v0.10.5's) taken as a re-test;
 *     step 2 straight away when the ring read beside the picture did not
 *     clear it or the scene pass's execution differed; step 1 untested
 *     after 8 events with no evidence; a port race never acted on;
 *   - the mark written and read back, a hand-written value never taken for
 *     the game's;
 *   - the start and action lines word for word.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bones_ladder.h"

static int g_fail;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while (0)

/* the keys between starts, as config.toml holds them */
typedef struct Keys {
    int ring;   /* vu_bones = ring */
    int nobin;  /* turnip_debug = nobin (the word trial) */
    int needed; /* ... with the word needed */
    int retest; /* turnip_debug = retest (or nobin without a word) */
    int off;    /* turnip_debug = off */
} Keys;

/* one start: the keys in force, the device's reach, the run's evidence;
   the action, applied to the keys as window_bones.c writes them */
static BonesAction start(Keys *k, int avail, uint32_t replays, uint32_t twins, uint32_t differ,
                         uint32_t ringClear, uint32_t ringFail, uint32_t sceneOnly,
                         uint32_t probeWrong)
{
    BonesLadderIn in;
    BonesAction a;

    memset(&in, 0, sizeof(in));
    in.ring = k->ring;
    in.nobin = k->nobin;
    in.nobinNeeded = k->nobin && k->needed;
    in.retest = k->retest;
    in.nobinOff = k->off;
    in.nobinAvail = avail;
    in.ringUsable = 1; /* the plain set on Vulkan (testPlain: without) */
    in.replays = replays;
    in.twins = twins;
    in.differ = differ;
    in.ringClear = ringClear;
    in.ringFail = ringFail;
    in.sceneOnly = sceneOnly;
    in.probeWrong = probeWrong;
    a = bones_ladder_step(&in);
    if (a == BONES_WRITE_RING) {
        k->ring = 1;
    } else if (a == BONES_WRITE_NOBIN) {
        k->nobin = 1;
        k->needed = 0;
        k->retest = 0;
        k->off = 0;
    } else if (a == BONES_WRITE_OFF) {
        k->nobin = 0;
        k->needed = 0;
        k->off = 1;
    } else if (a == BONES_WRITE_RETEST) {
        k->nobin = 0;
        k->retest = 1;
    } else if (a == BONES_RETEST_CLEAR) {
        k->retest = 0;
    } else if (a == BONES_WRITE_NOBIN_NEEDED) {
        k->retest = 0;
        k->nobin = 1;
        k->needed = 1;
    }
    return a;
}

static void testSequences(void)
{
    Keys k;

    /* the fault seen, the ring read beside the picture clears it: step 1;
       the next start clean over 2,000 presents: healed (said, kept) */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 40, 1600, 3, 2, 0, 0, 1) == BONES_WRITE_RING && k.ring && !k.nobin);
    CHECK(start(&k, 1, 1999, 80000, 0, 0, 0, 0, 0) == BONES_WAIT);
    CHECK(start(&k, 1, 2000, 80000, 0, 0, 0, 0, 0) == BONES_HEALED && k.ring && !k.nobin);

    /* step 1 does not clear it: step 2 for the next start, after 8 twin
       events (not 4); step 2 does not either: turnip_debug = off, both
       tried, the ladder stops; a later start with the twins differing says
       so and writes nothing */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 10, 400, 2, 1, 0, 0, 0) == BONES_WRITE_RING && k.ring);
    CHECK(start(&k, 1, 300, 12000, 4, 0, 0, 0, 0) == BONES_WAIT && k.ring && !k.nobin);
    CHECK(start(&k, 1, 300, 12000, 8, 0, 0, 0, 0) == BONES_WRITE_NOBIN && k.ring && k.nobin);
    CHECK(start(&k, 1, 500, 20000, 1, 0, 0, 0, 0) == BONES_WRITE_OFF && k.ring && !k.nobin &&
          k.off);
    CHECK(start(&k, 1, 500, 20000, 7, 0, 0, 0, 0) == BONES_WAIT && k.off);
    CHECK(start(&k, 1, 500, 20000, 8, 0, 0, 0, 0) == BONES_STOP && k.ring && !k.nobin && k.off);

    /* step 2's threshold under step 1: one stray event (v0.10.5 wrote
       nobin on it) and seven wait; the eighth writes it */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    CHECK(start(&k, 1, 100, 4000, 1, 0, 0, 0, 0) == BONES_WAIT && !k.nobin);
    CHECK(start(&k, 1, 100, 4000, 7, 0, 0, 0, 0) == BONES_WAIT && !k.nobin);
    CHECK(start(&k, 1, 100, 4000, BONES_NOBIN_EVENTS, 0, 0, 0, 0) == BONES_WRITE_NOBIN && k.nobin &&
          !k.needed);

    /* step 2 clears it with step 1 beside it: the re-test, written once
       2,000 presents are clean; the re-test clean over 2,000 presents: the
       key removed, step 1 alone from then on */
    CHECK(start(&k, 1, 1500, 60000, 0, 0, 0, 0, 0) == BONES_WAIT && k.nobin);
    CHECK(start(&k, 1, 2500, 90000, 0, 0, 0, 0, 0) == BONES_WRITE_RETEST && !k.nobin && k.retest);
    CHECK(start(&k, 1, 1999, 80000, 0, 0, 0, 0, 0) == BONES_WAIT && k.retest);
    CHECK(start(&k, 1, 2000, 80000, 0, 0, 0, 0, 0) == BONES_RETEST_CLEAR && !k.retest && !k.nobin &&
          k.ring);
    CHECK(start(&k, 1, 5000, 200000, 0, 0, 0, 0, 0) == BONES_HEALED && k.ring && !k.nobin);

    /* the re-test differing: a stray event waits, eight write nobin back
       marked needed; needed is kept healed (never re-tested again), and
       still goes off when the twins differ under it */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    k.retest = 1;
    CHECK(start(&k, 1, 300, 12000, 3, 0, 0, 0, 0) == BONES_WAIT && k.retest);
    CHECK(start(&k, 1, 300, 12000, 8, 0, 0, 0, 0) == BONES_WRITE_NOBIN_NEEDED && k.nobin &&
          k.needed && !k.retest);
    CHECK(start(&k, 1, 2500, 90000, 0, 0, 0, 0, 0) == BONES_HEALED && k.nobin && k.needed);
    CHECK(start(&k, 1, 9000, 400000, 0, 0, 0, 0, 0) == BONES_HEALED && k.nobin && k.needed);
    CHECK(start(&k, 1, 500, 20000, 1, 0, 0, 0, 0) == BONES_WRITE_OFF && !k.nobin && k.off);

    /* the re-test on a device nobin cannot reach (the phone's own driver
       loaded this start): not judged, the ladder's usual way */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    k.retest = 1;
    CHECK(start(&k, 0, 2500, 90000, 0, 0, 0, 0, 0) == BONES_HEALED && k.retest);

    /* step 2 healed without step 1 (it was skipped): kept, nothing else
       would cover the fault */
    memset(&k, 0, sizeof(k));
    k.nobin = 1;
    CHECK(start(&k, 1, 2500, 90000, 0, 0, 0, 0, 0) == BONES_HEALED && k.nobin);

    /* step 1 does not clear it on a device step 2 cannot reach (the
       phone's own driver, a desktop): the ladder stops, nothing written */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    CHECK(start(&k, 0, 100, 4000, 8, 0, 0, 0, 0) == BONES_STOP && k.ring && !k.nobin && !k.off);

    /* step 1 skipped: the ring read beside the picture differed too, or
       the twins differed twice while the probe drew right (the scene
       pass's execution); one wrong probe keeps step 1 the way */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 5, 200, 1, 0, 1, 0, 0) == BONES_WAIT && !k.nobin);
    CHECK(start(&k, 1, 5, 200, 2, 0, 2, 0, 0) == BONES_WRITE_NOBIN && !k.ring && k.nobin);
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 5, 200, 2, 0, 0, 2, 0) == BONES_WRITE_NOBIN && !k.ring && k.nobin);
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 5, 200, 1, 0, 0, 1, 0) == BONES_WAIT);
    CHECK(start(&k, 1, 5, 200, 2, 0, 0, 2, 1) == BONES_WAIT);
    /* evidence the ring read clears it wins over evidence it does not */
    CHECK(start(&k, 1, 5, 200, 2, 1, 1, 0, 0) == BONES_WRITE_RING && k.ring);
    /* step 2 straight away does not clear it: off; the next start's
       evidence then takes step 1 */
    memset(&k, 0, sizeof(k));
    k.nobin = 1;
    CHECK(start(&k, 1, 5, 200, 1, 0, 0, 0, 0) == BONES_WRITE_OFF && !k.ring && k.off);
    CHECK(start(&k, 1, 5, 200, 1, 1, 0, 0, 0) == BONES_WRITE_RING && k.ring && k.off);
    CHECK(start(&k, 1, 5, 200, 8, 0, 0, 0, 0) == BONES_STOP);

    /* no evidence either way (the ring read's pipelines not made yet):
       step 1 after 8 events */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 5, 200, 7, 0, 0, 0, 0) == BONES_WAIT && !k.ring);
    CHECK(start(&k, 1, 5, 200, 8, 0, 0, 0, 0) == BONES_WRITE_RING && k.ring);

    /* no twin differing and no step in force: nothing, however long */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 100000, 4000000, 0, 0, 0, 0, 0) == BONES_WAIT);

    /* a port race (H2: the ring's bytes changed under the GPU): never */
    {
        BonesLadderIn in;

        memset(&in, 0, sizeof(in));
        in.differ = 9;
        in.ringClear = 3;
        in.portRace = 1;
        in.nobinAvail = 1;
        CHECK(bones_ladder_step(&in) == BONES_PORT_RACE);
    }
}

/* step 1's switch in the run as window_bones.c polls it each pump: asked
   once, at the pump after the write, never while a movie plays or in the
   run's first 30 s, never again in the run */
static void testRestart(void)
{
    int pending = 0, asked = 0, asks = 0;

    /* (the pump's movie flag, the run's seconds) */
    static const struct {
        int movie;
        double up;
    } pumps[] = {{0, 50.0}, {0, 50.1}, {1, 50.2}, {1, 51.0}, {0, 51.1}, {0, 51.2}, {0, 52.0}};

    CHECK(bones_restart_step(0, 0, 0, 100.0) == BONES_RESTART_NONE);
    CHECK(bones_restart_step(1, 0, 0, 29.9) == BONES_RESTART_WAIT);
    CHECK(bones_restart_step(1, 0, 0, 30.0) == BONES_RESTART_ASK);
    CHECK(bones_restart_step(1, 0, 1, 300.0) == BONES_RESTART_WAIT);
    CHECK(bones_restart_step(1, 1, 0, 300.0) == BONES_RESTART_NONE);
    for (size_t i = 0; i < sizeof(pumps) / sizeof(pumps[0]); i++) {
        const int step = bones_restart_step(pending, asked, pumps[i].movie, pumps[i].up);

        if (i == 0) {
            CHECK(step == BONES_RESTART_NONE); /* before the write */
        }
        if (i == 1) {
            pending = 1; /* the write at this pump, during a movie next */
        }
        if (step == BONES_RESTART_ASK) {
            CHECK(!pumps[i].movie);
            asks++;
            asked = 1;
            pending = 0;
        }
        /* the movie's pumps wait; the first pump after it asks */
        if (i == 2 || i == 3) {
            CHECK(step == BONES_RESTART_WAIT);
        }
        if (i == 4) {
            CHECK(step == BONES_RESTART_ASK);
        }
    }
    CHECK(asks == 1);
    /* written at pump 1 with no movie: asked at that pump */
    pending = 1;
    asked = 0;
    CHECK(bones_restart_step(pending, asked, pumps[1].movie, pumps[1].up) == BONES_RESTART_ASK);
}

/* review M2: step 1 only where the ring builds can be put in force (the
   plain set on Vulkan): on the extended set the ladder waits, writes
   nothing, and step 2's evidence still acts; L3: step 2 in force only when
   the Turnip package loaded */
static void testPlain(void)
{
    BonesLadderIn in;
    char line[640];

    memset(&in, 0, sizeof(in));
    in.nobinAvail = 1;
    in.replays = 50;
    in.twins = 2000;
    in.differ = 3;
    in.ringClear = 2;
    CHECK(bones_ladder_step(&in) == BONES_WAIT_PLAIN);
    in.ringClear = 0;
    in.differ = 8;
    CHECK(bones_ladder_step(&in) == BONES_WAIT_PLAIN);
    in.ringUsable = 1;
    CHECK(bones_ladder_step(&in) == BONES_WRITE_RING);
    in.ringUsable = 0;
    in.ringFail = 2;
    CHECK(bones_ladder_step(&in) == BONES_WRITE_NOBIN);
    in.ringFail = 0;
    in.differ = 3;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WAIT_PLAIN, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: waits: the ring read needs the plain "
                       "shader set (this run's program entries are not it, or not on Vulkan): "
                       "step 1 not written (3 of 2000 twins differ)") == 0);

    {
        const char *trial = "vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; trial";
        const char *needed = "vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; needed";
        const char *old = "vu_skin_check 0.10.5 frame 90; 1 of 12000 twins differed";

        CHECK(bones_nobin_in_force("nobin", trial, "nobin", 1));
        CHECK(bones_nobin_in_force("nobin", needed, "startup,nobin", 1));
        CHECK(!bones_nobin_in_force("nobin", trial, "nobin", 0)); /* the open failed */
        CHECK(!bones_nobin_in_force("nobin", trial, NULL, 1));
        CHECK(!bones_nobin_in_force("nobin", trial, "gmem", 1));
        CHECK(!bones_nobin_in_force("off", trial, "nobin", 1));
        CHECK(!bones_nobin_in_force(NULL, trial, "nobin", 1));
        CHECK(!bones_nobin_in_force("", trial, "nobin", 1));
        /* a v0.10.5 nobin (no word) or one set by hand: a re-test, never in
           force */
        CHECK(!bones_nobin_in_force("nobin", old, "nobin", 1));
        CHECK(!bones_nobin_in_force("nobin", NULL, "nobin", 1));
        CHECK(!bones_nobin_in_force("retest", trial, "nobin", 1));

        CHECK(bones_turnip_state(NULL, NULL) == BONES_TU_NONE);
        CHECK(bones_turnip_state("", NULL) == BONES_TU_NONE);
        CHECK(bones_turnip_state("gmem", trial) == BONES_TU_NONE);
        CHECK(bones_turnip_state("off", old) == BONES_TU_OFF);
        CHECK(bones_turnip_state("retest", old) == BONES_TU_RETEST);
        CHECK(bones_turnip_state("retest", NULL) == BONES_TU_RETEST);
        CHECK(bones_turnip_state("nobin", trial) == BONES_TU_TRIAL);
        CHECK(bones_turnip_state("nobin", needed) == BONES_TU_NEEDED);
        CHECK(bones_turnip_state("nobin", old) == BONES_TU_UNMARKED);
        CHECK(bones_turnip_state("nobin", NULL) == BONES_TU_UNMARKED);
        CHECK(bones_turnip_state("nobin", "mine") == BONES_TU_UNMARKED);
        CHECK(bones_nobin_wanted("nobin", trial) && bones_nobin_wanted("nobin", needed));
        CHECK(!bones_nobin_wanted("nobin", old) && !bones_nobin_wanted("retest", needed));
    }
}

static void testMark(void)
{
    BonesMark m, n;
    char text[128];

    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.5");
    m.frame = 812;
    m.differ = 3;
    m.twins = 5200;
    CHECK(bones_mark_format(text, sizeof(text), &m) == 0);
    CHECK(strcmp(text, "vu_skin_check 0.10.5 frame 812; 3 of 5200 twins differed") == 0);
    CHECK(bones_mark_parse(text, &n) == 0 && strcmp(n.version, "0.10.5") == 0 && n.frame == 812 &&
          n.differ == 3 && n.twins == 5200);
    CHECK(bones_mark_format(text, 16, &m) == -1);
    CHECK(bones_mark_parse(NULL, &n) == -1);
    CHECK(bones_mark_parse("", &n) == -1);
    CHECK(bones_mark_parse("mine", &n) == -1);
    CHECK(bones_mark_parse("vu_skin_check 0.10.5 frame 812", &n) == -1);
    CHECK(bones_mark_parse("vu_skin_check 0.10.5 frame 812; 3 of 5200 twins differed!", &n) == -1);
    memset(&m, 0, sizeof(m));
    CHECK(bones_mark_format(text, sizeof(text), &m) == 0);
    CHECK(strcmp(text, "vu_skin_check dev frame 0; 0 of 0 twins differed") == 0);
    /* turnip_debug = nobin's words */
    snprintf(m.version, sizeof(m.version), "0.11.0");
    m.frame = 90;
    m.differ = 9;
    m.twins = 12000;
    snprintf(m.word, sizeof(m.word), "trial");
    CHECK(bones_mark_format(text, sizeof(text), &m) == 0);
    CHECK(strcmp(text, "vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; trial") == 0);
    CHECK(bones_mark_parse(text, &n) == 0 && strcmp(n.word, "trial") == 0 && n.frame == 90);
    CHECK(bones_mark_parse("vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; needed",
                           &n) == 0 &&
          strcmp(n.word, "needed") == 0 && n.differ == 9);
    CHECK(bones_mark_parse("vu_skin_check 0.10.5 frame 812; 3 of 5200 twins differed", &n) == 0 &&
          n.word[0] == '\0');
    CHECK(bones_mark_parse("vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; mine", &n) ==
              -1 &&
          n.word[0] == '\0' && n.frame == 0);
    CHECK(bones_mark_parse("vu_skin_check 0.11.0 frame 90; 9 of 12000 twins differed; trial!",
                           &n) == -1);
}

static void testLines(void)
{
    char line[640];
    BonesLadderIn in;
    const char *mark = "vu_skin_check 0.10.5 frame 812; 3 of 5200 twins differed";
    const char *mark2 = "vu_skin_check 0.10.5 frame 90; 4 of 12000 twins differed";
    const char *trial = "vu_skin_check 0.11.0 frame 90; 4 of 12000 twins differed; trial";
    const char *needed = "vu_skin_check 0.11.0 frame 90; 4 of 12000 twins differed; needed";

    CHECK(bones_ladder_start_line(line, sizeof(line), NULL, NULL, NULL, NULL, 0) == -1);
    CHECK(bones_ladder_start_line(line, sizeof(line), "cbuffer", NULL, NULL, NULL, 0) == -1);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, NULL, NULL, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 of 2 (the ring read) for this start "
                       "([dev] vu_bones = ring, written by the skin check at 0.10.5 frame 812; 3 "
                       "of 5200 twins differed)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", NULL, NULL, NULL, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 of 2 (the ring read) for this start "
                       "([dev] vu_bones = ring, set without the skin check's mark)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", trial, 1) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for this "
                       "start; step 1 (ring read) did not clear the twins (4 of 12000)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", needed, 1) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for this "
                       "start, needed (the re-test without it saw the twins differ): kept, not "
                       "re-tested; step 1 (ring read) did not clear the twins (4 of 12000)") == 0);
    /* v0.10.5's nobin (no word): the re-test at this start, said so */
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", mark2, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: the re-test of step 2 at this start: "
                       "TU_DEBUG=nobin not set ([dev] turnip_debug = nobin without the "
                       "word trial or needed: taken as retest, written by the skin check at 0.10.5 "
                       "frame 90; 4 of 12000 twins differed); step 1 (ring read) in force; 2000 "
                       "presents with no twin differing remove the key, 8 twin events write nobin "
                       "back as needed") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), NULL, NULL, "retest", mark2, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: the re-test of step 2 at this start: "
                       "TU_DEBUG=nobin not set ([dev] turnip_debug = retest, written by the skin "
                       "check at 0.10.5 frame 90; 4 of 12000 twins differed); 2000 presents with "
                       "no twin differing remove the key, 8 twin events write nobin back as "
                       "needed") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", trial, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: [dev] turnip_debug = nobin, but TU_DEBUG "
                       "was not set at this start (only before a Turnip driver package loads on "
                       "Android): step 2 is not in force") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "off", mark2, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: both tried, neither cleared the twins ([dev] "
                       "turnip_debug = off, written by the skin check at 0.10.5 frame 90; 4 of "
                       "12000 twins differed); the ladder stops; step 1 (ring read) stays in "
                       "force") == 0);

    memset(&in, 0, sizeof(in));
    in.differ = 3;
    in.twins = 1600;
    in.addr = 200;
    in.replays = 40;
    in.ringClear = 2;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WAIT, &in, 0, NULL) == -1);
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_RING, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line, "window: VU bones: [dev] vu_bones = ring is written (the skin check saw 3 "
                       "twin events at bone address 200 over 40 presents); the ring read beside "
                       "the picture cleared it 2 times; the next start reads the skinned draws' "
                       "bones through the ring (config.toml)") == 0);
    in.addr = 0;
    in.ringClear = 0;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_RING, &in, 0, "c.toml") == 0);
    CHECK(strcmp(line, "window: VU bones: [dev] vu_bones = ring could not be written (the skin "
                       "check saw 3 twin events at a bone address the probe did not name over 40 "
                       "presents); the ring read untested beside the picture: its pipelines not "
                       "made yet; the bones are read as before (c.toml)") == 0);
    /* step 1 in the run: the renderer made again in place now, or the
       switch waiting */
    in.addr = 200;
    in.ringClear = 2;
    CHECK(bones_ring_line(line, sizeof(line), &in, 1, "config.toml", BONES_RING_NOW, NULL) == 0);
    CHECK(strcmp(line, "window: VU bones: [dev] vu_bones = ring is written (the skin check saw 3 "
                       "twin events at bone address 200 over 40 presents); the ring read beside "
                       "the picture cleared it 2 times; in force from the next picture "
                       "(config.toml)") == 0);
    CHECK(bones_ring_line(line, sizeof(line), &in, 1, "config.toml", BONES_RING_WAITS,
                          "the movie's end") == 0);
    CHECK(strcmp(line, "window: VU bones: [dev] vu_bones = ring is written (the skin check saw 3 "
                       "twin events at bone address 200 over 40 presents); the ring read beside "
                       "the picture cleared it 2 times; the switch to the ring read waits for the "
                       "movie's end (else the next start reads it) (config.toml)") == 0);
    CHECK(bones_restart_line(line, sizeof(line)) == 0);
    CHECK(strcmp(line, "window: VU bones: switching to the ring read now (the renderer made again "
                       "in place, as Options > Graphics > Apply does); [dev] vu_bones = ring "
                       "written for every later start") == 0);
    CHECK(bones_restart_line(line, 16) == -1);
    in.addr = 0;
    in.ringClear = 0;
    in.ring = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_NOBIN, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for the next "
                       "start (the driver library is loaded once a process): [dev] turnip_debug = "
                       "nobin is written to config.toml; step 1 (ring read) did not clear the "
                       "twins (3 of 1600)") == 0);
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_OFF, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line, "window: VU skin check ladder: both tried, neither cleared the twins (3 of "
                       "1600 under TU_DEBUG=nobin); [dev] turnip_debug = off is written to "
                       "config.toml and the ladder stops") == 0);
    in.nobinOff = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_STOP, &in, 0, NULL) == 0);
    CHECK(strcmp(line,
                 "window: VU skin check ladder: both tried, neither cleared the twins (3 of "
                 "1600 under the ring read; [dev] turnip_debug = off): the ladder stops") == 0);
    in.nobinOff = 0;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_STOP, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 (ring read) did not clear the twins "
                       "(3 of 1600); step 2 (TU_DEBUG=nobin) needs a Turnip driver package on "
                       "Android, not this device's driver: the ladder stops") == 0);
    in.differ = 0;
    in.replays = 2000;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_HEALED, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 (ring read) healed the twins: 0 of "
                       "1600 differ over 2000 presents") == 0);
    in.nobin = 1;
    in.ring = 0;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_HEALED, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 (TU_DEBUG=nobin) healed the twins: 0 "
                       "of 1600 differ over 2000 presents; [dev] turnip_debug = nobin is kept "
                       "(step 1 not in force: nothing else would cover the fault)") == 0);
    in.nobinNeeded = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_HEALED, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 (TU_DEBUG=nobin) healed the twins: 0 "
                       "of 1600 differ over 2000 presents; [dev] turnip_debug = nobin is kept "
                       "(needed: the re-test without it saw the twins differ)") == 0);
    in.nobinNeeded = 0;
    in.ring = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_RETEST, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line,
                 "window: VU skin check ladder: step 2 (TU_DEBUG=nobin) healed the twins "
                 "with step 1 (ring read) beside it: 0 of 1600 differ over 2000 presents; "
                 "[dev] turnip_debug = retest is written to config.toml: the next start runs "
                 "without TU_DEBUG=nobin, to see whether step 1 alone keeps them equal") == 0);
    in.nobin = 0;
    in.retest = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_RETEST_CLEAR, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line, "window: VU skin check ladder: the re-test without TU_DEBUG=nobin kept the "
                       "twins equal: 0 of 1600 differ over 2000 presents; [dev] turnip_debug is "
                       "removed from config.toml: step 2 not needed; step 1 (ring read) stays in "
                       "force") == 0);
    in.differ = 8;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_NOBIN_NEEDED, &in, 1,
                                   "config.toml") == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: the re-test without TU_DEBUG=nobin: the "
                       "twins differ again (8 of 1600); [dev] turnip_debug = nobin is written to "
                       "config.toml, marked needed: step 2 for every later start, not re-tested "
                       "again") == 0);
    in.retest = 0;
    in.differ = 0;
    in.portRace = 2;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_PORT_RACE, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: the wrong reads are the ring's bytes "
                       "changing under the GPU (H2, a port race: 2 checks), not the driver's "
                       "read: the ladder does not act") == 0);
}

int main(void)
{
    testSequences();
    testRestart();
    testPlain();
    testMark();
    testLines();
    printf("bones_ladder_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail;
}
