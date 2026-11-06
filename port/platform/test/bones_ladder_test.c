/*
 * port/platform/test/bones_ladder_test.c
 *
 * Issue 123's ladder across starts (bones_ladder.h):
 *   - the step for a run's evidence, over sequences of starts as the window
 *     applies them (window_bones.c): the fault seen -> step 1 written ->
 *     clean over 2,000 presents: healed; still differing -> step 2 written
 *     (or the ladder stops where step 2 cannot reach the driver) -> still
 *     differing: turnip_debug = off, then "both tried" and nothing more;
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
    int ring;  /* vu_bones = ring */
    int nobin; /* turnip_debug = nobin */
    int off;   /* turnip_debug = off */
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
    in.nobinOff = k->off;
    in.nobinAvail = avail;
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
        k->off = 0;
    } else if (a == BONES_WRITE_OFF) {
        k->nobin = 0;
        k->off = 1;
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

    /* step 1 does not clear it: step 2 for the next start; step 2 does not
       either: turnip_debug = off, both tried, the ladder stops; a later
       start with the twins differing says so and writes nothing */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 10, 400, 2, 1, 0, 0, 0) == BONES_WRITE_RING && k.ring);
    CHECK(start(&k, 1, 300, 12000, 4, 0, 0, 0, 0) == BONES_WRITE_NOBIN && k.ring && k.nobin);
    CHECK(start(&k, 1, 500, 20000, 1, 0, 0, 0, 0) == BONES_WRITE_OFF && k.ring && !k.nobin &&
          k.off);
    CHECK(start(&k, 1, 500, 20000, 6, 0, 0, 0, 0) == BONES_STOP && k.ring && !k.nobin && k.off);

    /* step 2 clears it: healed under it, kept */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    CHECK(start(&k, 1, 100, 4000, 1, 0, 0, 0, 0) == BONES_WRITE_NOBIN && k.nobin);
    CHECK(start(&k, 1, 1500, 60000, 0, 0, 0, 0, 0) == BONES_WAIT && k.nobin);
    CHECK(start(&k, 1, 2500, 90000, 0, 0, 0, 0, 0) == BONES_HEALED && k.nobin);

    /* step 1 does not clear it on a device step 2 cannot reach (the
       phone's own driver, a desktop): the ladder stops, nothing written */
    memset(&k, 0, sizeof(k));
    k.ring = 1;
    CHECK(start(&k, 0, 100, 4000, 3, 0, 0, 0, 0) == BONES_STOP && k.ring && !k.nobin && !k.off);

    /* step 1 skipped: the ring read beside the picture differed too, or
       the twins differed twice while the probe drew right (the scene
       pass's execution); one wrong probe keeps step 1 the way */
    memset(&k, 0, sizeof(k));
    CHECK(start(&k, 1, 5, 200, 1, 0, 1, 0, 0) == BONES_WRITE_NOBIN && !k.ring && k.nobin);
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
    CHECK(start(&k, 1, 5, 200, 1, 0, 0, 0, 0) == BONES_STOP);

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
}

static void testLines(void)
{
    char line[640];
    BonesLadderIn in;
    const char *mark = "vu_skin_check 0.10.5 frame 812; 3 of 5200 twins differed";
    const char *mark2 = "vu_skin_check 0.10.5 frame 90; 4 of 12000 twins differed";

    CHECK(bones_ladder_start_line(line, sizeof(line), NULL, NULL, NULL, NULL, 0) == -1);
    CHECK(bones_ladder_start_line(line, sizeof(line), "cbuffer", NULL, NULL, NULL, 0) == -1);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, NULL, NULL, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 of 2 (the ring read) for this start "
                       "([dev] vu_bones = ring, written by the skin check at 0.10.5 frame 812; 3 "
                       "of 5200 twins differed)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", NULL, NULL, NULL, 0) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 1 of 2 (the ring read) for this start "
                       "([dev] vu_bones = ring, set without the skin check's mark)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", mark2, 1) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for this "
                       "start; step 1 (ring read) did not clear the twins (4 of 12000)") == 0);
    CHECK(bones_ladder_start_line(line, sizeof(line), "ring", mark, "nobin", mark2, 0) == 0);
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
                       "made yet; the next start reads them as before (c.toml)") == 0);
    in.ring = 1;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_WRITE_NOBIN, &in, 1, "config.toml") ==
          0);
    CHECK(strcmp(line, "window: VU skin check ladder: step 2 of 2 (TU_DEBUG=nobin) for the next "
                       "start: [dev] turnip_debug = nobin is written to config.toml; step 1 (ring "
                       "read) did not clear the twins (3 of 1600)") == 0);
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
    in.portRace = 2;
    CHECK(bones_ladder_action_line(line, sizeof(line), BONES_PORT_RACE, &in, 0, NULL) == 0);
    CHECK(strcmp(line, "window: VU skin check ladder: the wrong reads are the ring's bytes "
                       "changing under the GPU (H2, a port race: 2 checks), not the driver's "
                       "read: the ladder does not act") == 0);
}

int main(void)
{
    testSequences();
    testMark();
    testLines();
    printf("bones_ladder_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail;
}
