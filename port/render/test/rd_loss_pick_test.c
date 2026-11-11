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
 *   keep       a copy is needed when the first open frame is that frame
 *              or an older one, released two frames behind it, made only
 *              while a slot wait has blocked 50 ms or more (steady play's
 *              open frames without a long wait never copy), throttled to 4
 *              attempts in any 64 recycled frames (a long stalled stretch:
 *              never more in any window), paused for 600 after one too big,
 *              and its slot is an empty one, else the oldest frame
 *   history    the recycle run over a fake history of frames with the GPU
 *              a frame or two behind (no copy), then stuck at frame 129 with
 *              the wait stalled: the lost frame survives the ring moving
 *              on, at most four copies are ever held, and a GPU that
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
    const uint64_t stall = RD_LOSS_KEEP_STALL_NS, wait = 7;

    CHECK(!rd_loss_keep_needed(129, 0));
    CHECK(!rd_loss_keep_needed(0, 129));
    CHECK(!rd_loss_keep_needed(128, 129));
    CHECK(rd_loss_keep_needed(129, 129) && rd_loss_keep_needed(130, 129));
    CHECK(rd_loss_keep_release(10, 0));
    CHECK(rd_loss_keep_release(127, 129) && !rd_loss_keep_release(128, 129) &&
          !rd_loss_keep_release(129, 129));
    /* the stall: a wait in progress blocked 50 ms or more (issue 179's
       wait failed after about 0.1 s: a 100 ms gate could miss it; the
       Pixel's longest steady slot waits, 26.9 to 43.2 ms, stay under) */
    CHECK(stall == 50000000ull);
    CHECK(!rd_loss_keep_stalled(0, stall * 5));
    CHECK(!rd_loss_keep_stalled(wait, stall - 1));
    CHECK(rd_loss_keep_stalled(wait, stall));
    CHECK(!rd_loss_keep_stalled(wait, 43200000ull));
    CHECK(rd_loss_keep_stalled(wait, 90000000ull));
    /* the slot: an empty one, else the oldest frame */
    CHECK(rd_loss_keep_slot(slots, 4) == 0);
    slots[0] = 9;
    CHECK(rd_loss_keep_slot(slots, 4) == 1);
    slots[1] = 7;
    slots[2] = 8;
    slots[3] = 12;
    CHECK(rd_loss_keep_slot(slots, 4) == 1);

    /* the decision: not open, open without a stall (steady play on a
       GPU-bound phone: frames in flight, no long wait) never copies */
    {
        RdLossThrottle t = {0};

        CHECK(rd_loss_keep_decide(&t, 1, 128, 129, wait, stall) == RD_LOSS_KEEP_NOT_OPEN);
        CHECK(rd_loss_keep_decide(&t, 2, 129, 129, 0, 0) == RD_LOSS_KEEP_NO_STALL);
        CHECK(rd_loss_keep_decide(&t, 3, 129, 129, wait, stall / 2) == RD_LOSS_KEEP_NO_STALL);
        CHECK(t.attempts == 0);
        CHECK(rd_loss_keep_decide(&t, 4, 129, 129, wait, stall) == RD_LOSS_KEEP_TAKE);
        CHECK(t.attempts == 1);
    }
    /* the burst the cumulative throttle allowed (after the title and menus
       a quarter of every recycle so far was owed): a long stretch of
       stalled recycles gets at most 4 attempts in any 64 */
    {
        RdLossThrottle t = {0};
        uint32_t taken = 0, worst = 0;
        uint32_t inWindow[1024] = {0};

        for (uint64_t seen = 5000; seen < 5000 + 1024; seen++) {
            if (rd_loss_keep_decide(&t, seen, 200, 200, wait, stall) == RD_LOSS_KEEP_TAKE) {
                inWindow[seen - 5000] = 1;
                taken++;
            }
        }
        for (uint32_t i = 0; i + RD_LOSS_KEEP_WINDOW <= 1024; i++) {
            uint32_t n = 0;

            for (uint32_t k = 0; k < RD_LOSS_KEEP_WINDOW; k++) {
                n += inWindow[i + k];
            }
            worst = n > worst ? n : worst;
        }
        CHECK(worst <= RD_LOSS_KEEP_PER_WINDOW);
        CHECK(taken == 1024 / RD_LOSS_KEEP_WINDOW * RD_LOSS_KEEP_PER_WINDOW);
        /* the first four come at once, the fifth only a window after the first */
        RdLossThrottle u = {0};

        for (uint64_t seen = 1; seen <= 4; seen++) {
            CHECK(rd_loss_keep_decide(&u, seen, 9, 9, wait, stall) == RD_LOSS_KEEP_TAKE);
        }
        CHECK(rd_loss_keep_decide(&u, 5, 9, 9, wait, stall) == RD_LOSS_KEEP_THROTTLED);
        CHECK(rd_loss_keep_decide(&u, RD_LOSS_KEEP_WINDOW, 9, 9, wait, stall) ==
              RD_LOSS_KEEP_THROTTLED);
        CHECK(rd_loss_keep_decide(&u, 1 + RD_LOSS_KEEP_WINDOW, 9, 9, wait, stall) ==
              RD_LOSS_KEEP_TAKE);
    }
    /* a frame too big (every attempt counted, a failed one too): no retry
       for RD_LOSS_KEEP_BIG_PAUSE recycled frames, a big frame not rebuilt
       at every recycle */
    {
        RdLossThrottle t = {0};

        CHECK(rd_loss_keep_decide(&t, 10, 50, 50, wait, stall) == RD_LOSS_KEEP_TAKE);
        rd_loss_keep_big(&t, 10);
        uint32_t tried = 0;

        for (uint64_t seen = 11; seen < 10 + RD_LOSS_KEEP_BIG_PAUSE; seen++) {
            const RdLossKeep k = rd_loss_keep_decide(&t, seen, 50, 50, wait, stall);

            tried += k == RD_LOSS_KEEP_TAKE;
            CHECK(k == RD_LOSS_KEEP_PAUSED);
        }
        CHECK(tried == 0 && t.attempts == 1);
        CHECK(rd_loss_keep_decide(&t, 10 + RD_LOSS_KEEP_BIG_PAUSE, 50, 50, wait, stall) ==
              RD_LOSS_KEEP_TAKE);
    }
}

/* The recycle over a fake history: the game records frames 124..140 (a
 * ring of three) and the GPU is stuck at frame 129 (replay 148): the first
 * open frame is 129 from the time it is submitted, and the slot wait has
 * blocked past 50 ms from frame 131 on (before it the GPU is merely a
 * frame or two behind: open, no stall, no copy).  Each begin recycles the
 * oldest of the ring. */
static void history(void)
{
    uint32_t copies[4] = {0};
    uint32_t ring[3] = {0, 0, 0};
    uint64_t taken = 0, seen = 0, steadyAsked = 0;
    uint32_t maxHeld = 0;
    uint32_t lastFrame = 140;
    RdLossThrottle t = {0};

    for (uint32_t n = 124; n <= lastFrame; n++) {
        /* the frame the slot held (n - 3), recycled as n begins */
        const uint32_t old = ring[n % 3];
        /* steady play before the stall: the frame before the newest still
           open (two in flight), never a long wait */
        const uint32_t open = n > 129 ? 129u : (n > 125 ? n - 2u : 0u);
        const bool stalled = n > 130;

        seen++;
        for (int i = 0; i < 4; i++) {
            if (copies[i] && rd_loss_keep_release(copies[i], open)) {
                copies[i] = 0;
            }
        }
        const RdLossKeep k = rd_loss_keep_decide(&t, seen, old, open, stalled ? 9u : 0u,
                                                 stalled ? 150000000ull : 0u);

        steadyAsked += !stalled && k == RD_LOSS_KEEP_TAKE;
        if (old && k == RD_LOSS_KEEP_TAKE) {
            copies[rd_loss_keep_slot(copies, 4)] = old;
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
               lost frame 129 (recycled as 132 began, the wait stalled) is a
               copy; 128 was recycled before the stall: not copied */
            int have129 = 0, have128 = 0;

            for (int i = 0; i < 4; i++) {
                have129 |= copies[i] == 129;
                have128 |= copies[i] == 128;
            }
            CHECK(have129);
            CHECK(!have128);
        }
    }
    CHECK(steadyAsked == 0);
    CHECK(maxHeld <= 4);
    CHECK(taken >= 1 && taken <= RD_LOSS_KEEP_PER_WINDOW);
    /* the GPU finished everything: every copy goes at the next recycle */
    for (int i = 0; i < 4; i++) {
        if (copies[i] && rd_loss_keep_release(copies[i], 0)) {
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
