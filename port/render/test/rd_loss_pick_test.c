/* rd_loss_pick_test.c: which frames the device-loss dump writes
 * (port/render/rd_loss_pick.c), pure, with a fake submission history.
 *
 *   issue179   v0.11.3's Mali report, run 3: replay 148 / frame 129 lost its
 *              wait, the game thread had recorded 130..132 meanwhile and
 *              the ring held 132 and 131 (130 in its third slot).  Without
 *              a copy the lost frame is not held and the nearest is written;
 *              with the copy the recycle made when frame 132 began (replay
 *              148 was open) the lost frame and the one before are written.
 *   issue211   an Adreno 710's vkQueueSubmit failed with every numbered
 *              submission finished and no replay recorded after the newest
 *              (249 / frame 150): a submit outside a picture, the newest
 *              picture (150) and the one before (149); with a replay 250 /
 *              frame 151 recorded and never numbered, that one and 150
 *   order      the wait that failed beats the first open submission, which
 *              beats the newest numbered; with nothing known the newest frame
 *              held; with nothing held no frame
 *   before     the frame replayed before the lost one (the pass ring's table),
 *              else the newest held below it
 *   keep       a copy is needed when the first open replay is that frame's
 *              or later, released two replays behind it, throttled after the
 *              first eight to a quarter of the recycles, and its slot is an
 *              empty one, else the oldest frame
 *   history    the recycle run over a fake history of frames and replays with
 *              the GPU stuck at replay 148: the lost frame survives the ring
 *              moving on, at most four copies are ever held, and a GPU that
 *              finishes everything frees them all */
#include "rd_loss_pick.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void issue179(void)
{
    static const uint32_t replayed[] = {124, 126, 128, 129, 130};
    static const uint32_t ringOnly[] = {130, 131, 132};
    static const uint32_t withCopies[] = {128, 129, 130, 131, 132};
    RdLossInput in = {0};
    RdLossChoice c;

    in.waitReplay = 148;
    in.waitFrame = 129;
    in.openReplay = 148;
    in.openFrame = 129;
    in.newestReplay = 149;
    in.newestFrame = 130;
    in.replayed = replayed;
    in.replayedCount = 5;
    in.held = ringOnly;
    in.heldCount = 3;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_WAIT && c.replay == 148 && c.frame == 129);
    CHECK(c.before == 128);
    /* the ring alone: neither is held, the nearest to the lost one is written
       (130) and nothing below it */
    CHECK(!c.frameHeld && !c.beforeHeld);
    CHECK(c.writeFrame == 130 && c.writeBefore == 0);

    in.held = withCopies;
    in.heldCount = 5;
    rd_loss_choose(&in, &c);
    CHECK(c.frameHeld && c.beforeHeld && c.writeFrame == 129 && c.writeBefore == 128);
}

static void issue211(void)
{
    static const uint32_t replayed[] = {141, 142, 144, 145, 147, 148, 149, 150};
    static const uint32_t held[] = {149, 150, 151};
    RdLossInput in = {0};
    RdLossChoice c;

    /* every numbered submission finished, the pass ring's newest replay is
     * the backend's newest: the submit that failed was none of a picture's */
    in.newestReplay = 249;
    in.newestFrame = 150;
    in.ringReplay = 249;
    in.ringFrame = 150;
    in.replayed = replayed;
    in.replayedCount = 8;
    in.held = held;
    in.heldCount = 3;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_SIDE && c.replay == 249 && c.frame == 150);
    CHECK(c.before == 149 && c.frameHeld && c.beforeHeld);
    CHECK(c.writeFrame == 150 && c.writeBefore == 149);
    /* a picture recorded after it (replay 250, frame 151) and never numbered:
     * its submit is the one that failed */
    in.ringReplay = 250;
    in.ringFrame = 151;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_NEXT && c.replay == 250 && c.frame == 151);
    CHECK(c.before == 150 && c.frameHeld && c.beforeHeld);
    CHECK(c.writeFrame == 151 && c.writeBefore == 150);
}

static void order(void)
{
    static const uint32_t held[] = {10, 11, 12};
    RdLossInput in = {0};
    RdLossChoice c;

    in.held = held;
    in.heldCount = 3;
    /* nothing known: the newest frame held */
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_NEWEST && c.frame == 12 && c.before == 11);
    /* the first open submission, the wait that failed beats it */
    in.openReplay = 7;
    in.openFrame = 10;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_OPEN && c.frame == 10 && c.replay == 7);
    CHECK(c.before == 0 && c.writeBefore == 0);
    in.waitReplay = 8;
    in.waitFrame = 11;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_WAIT && c.frame == 11 && c.before == 10 && c.writeBefore == 10);
    /* nothing held at all */
    memset(&in, 0, sizeof(in));
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_NONE && c.frame == 0 && c.writeFrame == 0);
    /* a lost frame older than everything held: the nearest, newer */
    in.held = held;
    in.heldCount = 3;
    in.waitReplay = 2;
    in.waitFrame = 3;
    rd_loss_choose(&in, &c);
    CHECK(!c.frameHeld && c.writeFrame == 10 && c.writeBefore == 0);
    /* a tie between an older and a newer frame takes the newer */
    in.waitFrame = 11;
    {
        static const uint32_t gap[] = {10, 12};

        in.held = gap;
        in.heldCount = 2;
        rd_loss_choose(&in, &c);
        CHECK(!c.frameHeld && c.writeFrame == 12 && c.writeBefore == 10);
    }
}

static void keep(void)
{
    uint32_t slots[4] = {0, 0, 0, 0};

    CHECK(!rd_loss_keep_needed(148, 0));
    CHECK(!rd_loss_keep_needed(0, 148));
    CHECK(!rd_loss_keep_needed(147, 148));
    CHECK(rd_loss_keep_needed(148, 148) && rd_loss_keep_needed(149, 148));
    CHECK(rd_loss_keep_release(10, 0));
    CHECK(rd_loss_keep_release(146, 148) && !rd_loss_keep_release(147, 148) &&
          !rd_loss_keep_release(148, 148));
    /* the throttle: free for the first eight, then a quarter of the recycles */
    CHECK(rd_loss_keep_allowed(0, 0) && rd_loss_keep_allowed(7, 7));
    CHECK(!rd_loss_keep_allowed(8, 8) && !rd_loss_keep_allowed(100, 399));
    CHECK(rd_loss_keep_allowed(100, 400) && rd_loss_keep_allowed(9, 40));
    /* the slot: an empty one, else the oldest frame */
    CHECK(rd_loss_keep_slot(slots, 4) == 0);
    slots[0] = 9;
    CHECK(rd_loss_keep_slot(slots, 4) == 1);
    slots[1] = 7;
    slots[2] = 8;
    slots[3] = 12;
    CHECK(rd_loss_keep_slot(slots, 4) == 1);
}

/* The recycle over a fake history: the game records frames 124..140 (a
 * ring of three), the replays 145.. draw only some of them, and the GPU
 * is stuck at replay 148 (frame 129): open = 148 from the time replay
 * 148 is submitted.  Each begin recycles the oldest of the ring. */
static void history(void)
{
    static const struct {
        uint32_t frame, replay;
    } drawn[] = {{124, 145}, {126, 146}, {128, 147}, {129, 148}, {130, 149}};

    uint32_t copies[4] = {0}, copyReplay[4] = {0};
    uint32_t ring[3] = {0, 0, 0};
    uint64_t taken = 0, seen = 0;
    uint32_t maxHeld = 0;
    uint32_t lastFrame = 140;

    for (uint32_t n = 124; n <= lastFrame; n++) {
        /* the frame the slot held (n - 3), recycled as n begins */
        const uint32_t old = ring[n % 3];
        uint32_t replay = 0, open = 0;

        for (unsigned i = 0; i < sizeof(drawn) / sizeof(drawn[0]); i++) {
            replay = drawn[i].frame == old ? drawn[i].replay : replay;
            /* replay 148 is submitted when frame 129 has been drawn: from
               then on the first open submission is 148 */
            open = n > 129 ? 148u : 0u;
        }
        seen++;
        for (int i = 0; i < 4; i++) {
            if (copies[i] && rd_loss_keep_release(copyReplay[i], open)) {
                copies[i] = copyReplay[i] = 0;
            }
        }
        if (old && rd_loss_keep_needed(replay, open) && rd_loss_keep_allowed(taken, seen)) {
            const uint32_t s = rd_loss_keep_slot(copies, 4);

            copies[s] = old;
            copyReplay[s] = replay;
            taken++;
        }
        ring[n % 3] = n;
        {
            uint32_t held = 0;

            for (int i = 0; i < 4; i++) {
                held += copies[i] != 0;
            }
            maxHeld = held > maxHeld ? held : maxHeld;
        }
        if (n == 132) {
            /* the moment of the report: the ring holds 130..132, and the
               lost frame 129 and the one before it are copies */
            int have129 = 0, have128 = 0;

            for (int i = 0; i < 4; i++) {
                have129 |= copies[i] == 129;
                have128 |= copies[i] == 128;
            }
            CHECK(have129);
            /* 128 was recycled before 148 was open: it finished */
            CHECK(!have128);
        }
    }
    CHECK(maxHeld <= 4);
    CHECK(taken >= 1);
    /* the GPU finished everything: every copy goes at the next recycle */
    for (int i = 0; i < 4; i++) {
        if (copies[i] && rd_loss_keep_release(copyReplay[i], 0)) {
            copies[i] = 0;
        }
    }
    CHECK(copies[0] == 0 && copies[1] == 0 && copies[2] == 0 && copies[3] == 0);
}

int main(void)
{
    issue179();
    issue211();
    order();
    keep();
    history();
    if (failures == 0) {
        printf("rd_loss_pick_test: ok\n");
    }
    return failures != 0;
}
