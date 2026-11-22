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
 *              while a slot wait has blocked 100 ms or more (steady play's
 *              open frames without a long wait never copy), throttled to 4
 *              attempts in any 64 recycled frames (a long stalled stretch:
 *              never more in any window), paused for 600 after one too big,
 *              and its slot is an empty one, else the oldest frame
 *   history    the recycle run over a fake history of frames with the GPU
 *              a frame or two behind (no copy), then stuck at frame 129 with
 *              the wait stalled: the lost frame survives the ring moving
 *              on, at most four copies are ever held, and a GPU that
 *              finishes everything frees them all
 *   not sent   a frame no picture of which was handed to a replay
 *              (RdFrame.sent, Frame rate Original's gate leaving it out) is
 *              never needed, decided after the stall's test and before the
 *              throttle (no attempt counted); issue 179's v0.11.13 log: the
 *              8 copies its lines 782-1188 made (each frame newer than the
 *              newest submission) are the 8 "not sent" now, the same 8
 *              taken when sent
 *   unchanged  the loss report's choice: a copy of a frame never sent
 *              changes nothing the choice names when the lost frame and
 *              the one before are held (issue 179's run 3 with such a copy
 *              beside them); over a stall longer than the throttle's window
 *              the never-sent frames take no slot, so the lost frame is
 *              still held where v0.11.13's copies of them had pushed it
 *              out
 *   two in flight  the next picture submitted ahead (port/render
 *              rd_submit_thread.h "ahead": frame 299's work held, waited for
 *              on the waiter, frame 300 submitted behind it): a failed wait
 *              names the submission waited for, the oldest unfinished
 *              (299, the frame before 298), as the one wait at a time did;
 *              299 seen finished and the waiter's wait for 300 failing names
 *              300 and 299; no wait failed (a later submit did): the first
 *              open submission, 299.  What the pick cannot tell: a driver
 *              that answers the wait for 299 with the loss although 299 had
 *              finished and 300 faulted (Vulkan allows either answer after
 *              a loss) names 299, one picture early; 300 is then held but
 *              not written (pinned here, rd_loss_pick.c unchanged)
 *   head       the stall counted from the head's time (RhiGpuProgress
 *              headNs, rd_loss_keep_decide_head): issue 179's v0.11.14
 *              title and menus, two pictures in flight of about 55 ms GPU
 *              work each and slot waits of 100-110 ms (every one copied
 *              there), copy none (RD_LOSS_KEEP_NOT_HEAD, no attempt
 *              counted) where v0.11.14's rule took 4 in every 64; the head
 *              unknown decides as v0.11.14 did; a picture stuck on the GPU
 *              (nothing seen finished for 100 ms or more, a hang) still
 *              takes its copy, and over a hang after the title the lost
 *              frame is held; the frame that is the GPU's current one
 *              counts the whole wait, so a wait queued behind the picture
 *              before whose head then hangs (wait 130 ms, head 70 ms at
 *              that frame's one recycle) still copies it (review K1); a
 *              wait seen by several recycles counts once in the 10 s line
 *              (review K2) */
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

    CHECK(!rd_loss_keep_needed(129, true, 0));
    CHECK(!rd_loss_keep_needed(0, true, 129));
    CHECK(!rd_loss_keep_needed(128, true, 129));
    CHECK(rd_loss_keep_needed(129, true, 129) && rd_loss_keep_needed(130, true, 129));
    /* never handed to a replay: not on the GPU, whatever is open */
    CHECK(!rd_loss_keep_needed(129, false, 129) && !rd_loss_keep_needed(130, false, 129));
    CHECK(rd_loss_keep_release(10, 0));
    CHECK(rd_loss_keep_release(127, 129) && !rd_loss_keep_release(128, 129) &&
          !rd_loss_keep_release(129, 129));
    /* the stall: a wait in progress blocked 100 ms or more (the Mali-G52's
       steady slot waits, 35-55 ms and often 60-90 ms, stay under: a copy
       drains the threads and serialises the frame, rd_loss_pick.h) */
    CHECK(stall == 100000000ull);
    CHECK(!rd_loss_keep_stalled(0, stall * 5));
    CHECK(!rd_loss_keep_stalled(wait, stall - 1));
    CHECK(rd_loss_keep_stalled(wait, stall));
    CHECK(!rd_loss_keep_stalled(wait, 43200000ull));
    CHECK(!rd_loss_keep_stalled(wait, 90000000ull));
    CHECK(rd_loss_keep_stalled(wait, 150000000ull));
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

        CHECK(rd_loss_keep_decide(&t, 1, 128, true, 129, wait, stall) == RD_LOSS_KEEP_NOT_OPEN);
        CHECK(rd_loss_keep_decide(&t, 2, 129, true, 129, 0, 0) == RD_LOSS_KEEP_NO_STALL);
        CHECK(rd_loss_keep_decide(&t, 3, 129, true, 129, wait, stall / 2) == RD_LOSS_KEEP_NO_STALL);
        CHECK(t.attempts == 0);
        CHECK(rd_loss_keep_decide(&t, 4, 129, true, 129, wait, stall) == RD_LOSS_KEEP_TAKE);
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
            if (rd_loss_keep_decide(&t, seen, 200, true, 200, wait, stall) == RD_LOSS_KEEP_TAKE) {
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
            CHECK(rd_loss_keep_decide(&u, seen, 9, true, 9, wait, stall) == RD_LOSS_KEEP_TAKE);
        }
        CHECK(rd_loss_keep_decide(&u, 5, 9, true, 9, wait, stall) == RD_LOSS_KEEP_THROTTLED);
        CHECK(rd_loss_keep_decide(&u, RD_LOSS_KEEP_WINDOW, 9, true, 9, wait, stall) ==
              RD_LOSS_KEEP_THROTTLED);
        CHECK(rd_loss_keep_decide(&u, 1 + RD_LOSS_KEEP_WINDOW, 9, true, 9, wait, stall) ==
              RD_LOSS_KEEP_TAKE);
    }
    /* a frame too big (every attempt counted, a failed one too): no retry
       for RD_LOSS_KEEP_BIG_PAUSE recycled frames, a big frame not rebuilt
       at every recycle */
    {
        RdLossThrottle t = {0};

        CHECK(rd_loss_keep_decide(&t, 10, 50, true, 50, wait, stall) == RD_LOSS_KEEP_TAKE);
        rd_loss_keep_big(&t, 10);
        uint32_t tried = 0;

        for (uint64_t seen = 11; seen < 10 + RD_LOSS_KEEP_BIG_PAUSE; seen++) {
            const RdLossKeep k = rd_loss_keep_decide(&t, seen, 50, true, 50, wait, stall);

            tried += k == RD_LOSS_KEEP_TAKE;
            CHECK(k == RD_LOSS_KEEP_PAUSED);
        }
        CHECK(tried == 0 && t.attempts == 1);
        CHECK(rd_loss_keep_decide(&t, 10 + RD_LOSS_KEEP_BIG_PAUSE, 50, true, 50, wait, stall) ==
              RD_LOSS_KEEP_TAKE);
    }
}

/* The recycle over a fake history: the game records frames 124..140 (a
 * ring of three) and the GPU is stuck at frame 129 (replay 148): the first
 * open frame is 129 from the time it is submitted, and the slot wait has
 * blocked past 100 ms from frame 131 on (before it the GPU is merely a
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
        const RdLossKeep k = rd_loss_keep_decide(&t, seen, old, true, open, stalled ? 9u : 0u,
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

/* the decision for a frame never sent */
static void notSent(void)
{
    const uint64_t stall = RD_LOSS_KEEP_STALL_NS, wait = 7;
    RdLossThrottle t = {0};

    CHECK(rd_loss_keep_decide(&t, 1, 128, false, 129, wait, stall) == RD_LOSS_KEEP_NOT_OPEN);
    CHECK(rd_loss_keep_decide(&t, 2, 129, false, 129, 0, 0) == RD_LOSS_KEEP_NO_STALL);
    CHECK(rd_loss_keep_decide(&t, 3, 129, false, 129, wait, stall) == RD_LOSS_KEEP_NOT_SENT);
    CHECK(rd_loss_keep_decide(&t, 4, 130, false, 129, wait, stall) == RD_LOSS_KEEP_NOT_SENT);
    CHECK(t.attempts == 0);
    CHECK(rd_loss_keep_decide(&t, 5, 129, true, 129, wait, stall) == RD_LOSS_KEEP_TAKE);
    CHECK(t.attempts == 1);
    /* paused after one too big: a frame never sent is still "not sent" */
    rd_loss_keep_big(&t, 5);
    CHECK(rd_loss_keep_decide(&t, 6, 130, false, 129, wait, stall) == RD_LOSS_KEEP_NOT_SENT);
    CHECK(rd_loss_keep_decide(&t, 7, 130, true, 129, wait, stall) == RD_LOSS_KEEP_PAUSED);

    /* issue 179's v0.11.13 log, its 8 "loss keep" lines (782, 913, 978,
       982, 987, 991, 1051, 1188): the frame copied, the backend's first
       open frame (= its newest tag's), the slot wait, the recycled frames
       so far */
    static const struct {
        uint32_t frame, open, waitMs;
        uint64_t seen;
    } log[8] = {
        {616, 613, 105, 619},    {1042, 1040, 119, 1045}, {1289, 1287, 134, 1292},
        {1297, 1295, 107, 1300}, {1301, 1300, 111, 1304}, {1305, 1304, 111, 1308},
        {1393, 1389, 120, 1396}, {1568, 1566, 112, 1571},
    };

    RdLossThrottle was = {0}, now = {0};
    uint32_t taken = 0, notTaken = 0;

    for (int i = 0; i < 8; i++) {
        const uint64_t ns = (uint64_t)log[i].waitMs * 1000000ull;

        CHECK(log[i].frame > log[i].open); /* newer than the newest submission */
        taken += rd_loss_keep_decide(&was, log[i].seen, log[i].frame, true, log[i].open, wait,
                                     ns) == RD_LOSS_KEEP_TAKE;
        notTaken += rd_loss_keep_decide(&now, log[i].seen, log[i].frame, false, log[i].open, wait,
                                        ns) == RD_LOSS_KEEP_NOT_SENT;
    }
    CHECK(taken == 8 && notTaken == 8);
    CHECK(was.attempts == 8 && now.attempts == 0);
}

/* the frames held (copies and ring) as rd_dump.c's heldFrames lists them:
   ascending, without repeats */
static uint32_t heldList(const uint32_t *copies, const uint32_t *ring, uint32_t *out)
{
    uint32_t n = 0;

    for (uint32_t v = 1; v < 100000; v++) {
        bool in = false;

        for (int i = 0; i < 4; i++) {
            in = in || copies[i] == v;
        }
        for (int i = 0; i < 3; i++) {
            in = in || ring[i] == v;
        }
        if (in) {
            out[n++] = v;
        }
    }
    return n;
}

/* the recycles of frames 124..last, the GPU stuck at frame 129 from 130
   on with the wait stalled from 131; sentUpTo: the newest frame a picture
   was handed over for (the frames after it left out by the gate); `all`
   marks every frame sent (v0.11.13, which had no flag) */
static void stalled(uint32_t last, uint32_t sentUpTo, bool all, uint32_t *copies, uint32_t *ring)
{
    RdLossThrottle t = {0};
    uint64_t seen = 0;

    memset(copies, 0, 4 * sizeof(uint32_t));
    memset(ring, 0, 3 * sizeof(uint32_t));
    for (uint32_t n = 124; n <= last; n++) {
        const uint32_t old = ring[n % 3];
        const uint32_t open = n > 129 ? 129u : (n > 125 ? n - 2u : 0u);
        const bool st = n > 130;

        seen++;
        for (int i = 0; i < 4; i++) {
            if (copies[i] && rd_loss_keep_release(copies[i], open)) {
                copies[i] = 0;
            }
        }
        bool have = false;

        for (int i = 0; i < 4; i++) {
            have = have || (old && copies[i] == old);
        }
        if (old && !have &&
            rd_loss_keep_decide(&t, seen, old, all || old <= sentUpTo, open, st ? 9u : 0u,
                                st ? 150000000ull : 0u) == RD_LOSS_KEEP_TAKE) {
            copies[rd_loss_keep_slot(copies, 4)] = old;
        }
        ring[n % 3] = n;
    }
}

static void unchanged(void)
{
    static const uint32_t replayed[] = {124, 126, 128, 129, 130};
    uint32_t copies[4], ring[3], held[16];
    RdLossInput in = {0};
    RdLossChoice a, b;

    /* issue 179's run 3 (issue179 above): the lost frame and the one before
       held, with and without a copy of 133, a frame never sent */
    {
        static const uint32_t sentOnly[] = {128, 129, 130, 131, 132};
        static const uint32_t withUnsent[] = {128, 129, 130, 131, 132, 133};

        in.waitReplay = in.openReplay = 148;
        in.waitFrame = in.openFrame = 129;
        in.newestReplay = 149;
        in.newestFrame = 130;
        in.replayed = replayed;
        in.replayedCount = 5;
        in.held = sentOnly;
        in.heldCount = 5;
        rd_loss_choose(&in, &a);
        in.held = withUnsent;
        in.heldCount = 6;
        rd_loss_choose(&in, &b);
        CHECK(a.how == b.how && a.replay == b.replay && a.frame == b.frame &&
              a.before == b.before && a.frameHeld == b.frameHeld && a.beforeHeld == b.beforeHeld &&
              a.writeFrame == b.writeFrame && a.writeBefore == b.writeBefore);
        CHECK(a.writeFrame == 129 && a.writeBefore == 128);
    }
    /* the history's report moment (frame 132 begun): every frame sent up to
       130 (the picture handed over beside the stalled one), the same copies
       and the same choice as v0.11.13's */
    {
        uint32_t c0[4], r0[3];

        stalled(132, 130, true, c0, r0);
        stalled(132, 130, false, copies, ring);
        CHECK(memcmp(c0, copies, sizeof(c0)) == 0 && memcmp(r0, ring, sizeof(r0)) == 0);
        in.held = held;
        in.heldCount = heldList(copies, ring, held);
        rd_loss_choose(&in, &a);
        CHECK(a.writeFrame == 129 && a.frameHeld);
    }
    /* a stall past the throttle's window (frames to 400): v0.11.13's copies
       of the frames never sent took the four slots and pushed 129 out (the
       nearest held, 130 or newer, written in its place); now they take
       none and 129 is still written */
    {
        stalled(400, 130, true, copies, ring);
        in.held = held;
        in.heldCount = heldList(copies, ring, held);
        rd_loss_choose(&in, &a);
        CHECK(!a.frameHeld && a.writeFrame != 129);
        stalled(400, 130, false, copies, ring);
        in.heldCount = heldList(copies, ring, held);
        rd_loss_choose(&in, &b);
        CHECK(b.frameHeld && b.writeFrame == 129 &&
              b.writeBefore == 0); /* 128 recycled before the stall */
    }
}

static void twoInFlight(void)
{
    static const uint32_t replayed[] = {296, 297, 298, 299, 300};
    static const uint32_t held[] = {298, 299, 300, 301, 302};
    RdLossInput in = {0};
    RdLossChoice c;

    in.replayed = replayed;
    in.replayedCount = 5;
    in.held = held;
    in.heldCount = 5;
    in.newestReplay = 500;
    in.newestFrame = 300;
    in.ringReplay = 502;
    in.ringFrame = 302;
    /* the waiter's wait for 299 (replay 499) failed, 300 submitted behind it */
    in.waitReplay = 499;
    in.waitFrame = 299;
    in.openReplay = 499;
    in.openFrame = 299;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_WAIT && c.replay == 499 && c.frame == 299 && c.before == 298);
    CHECK(c.writeFrame == 299 && c.writeBefore == 298);
    /* 299 seen finished (the waiter's wait for it returned), the wait for
       300 failed */
    in.waitReplay = 500;
    in.waitFrame = 300;
    in.openReplay = 500;
    in.openFrame = 300;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_WAIT && c.frame == 300 && c.before == 299);
    CHECK(c.writeFrame == 300 && c.writeBefore == 299);
    /* no wait failed (a later submit found the loss): the first submission
       not seen finished, the older of the two */
    in.waitReplay = 0;
    in.waitFrame = 0;
    in.openReplay = 499;
    in.openFrame = 299;
    rd_loss_choose(&in, &c);
    CHECK(c.how == RD_LOSS_HOW_OPEN && c.frame == 299 && c.before == 298);
    CHECK(c.writeFrame == 299 && c.writeBefore == 298);
    /* the limit: the wait for 299 answered with the loss although 300
       faulted: 299 named, 300 held and not written */
    in.waitReplay = 499;
    in.waitFrame = 299;
    rd_loss_choose(&in, &c);
    CHECK(c.frame == 299 && c.writeFrame == 299 && c.writeBefore == 298 && c.writeFrame != 300 &&
          c.writeBefore != 300);
}

static void head(void)
{
    const uint64_t ms = 1000000ull;
    const uint64_t wait = 7;

    /* the time counted: the wait's, at most the head's when known */
    CHECK(rd_loss_keep_counted_ns(105 * ms, false, 0) == 105 * ms);
    CHECK(rd_loss_keep_counted_ns(105 * ms, false, 50 * ms) == 105 * ms);
    CHECK(rd_loss_keep_counted_ns(105 * ms, true, 50 * ms) == 50 * ms);
    CHECK(rd_loss_keep_counted_ns(105 * ms, true, 300 * ms) == 105 * ms);
    CHECK(rd_loss_keep_counted_ns(105 * ms, true, 0) == 0);
    /* the same 100 ms: no new limit */
    CHECK(rd_loss_keep_stalled_head(wait, 105 * ms, false, 0));
    CHECK(!rd_loss_keep_stalled_head(wait, 105 * ms, true, 50 * ms));
    CHECK(!rd_loss_keep_stalled_head(wait, 105 * ms, true, RD_LOSS_KEEP_STALL_NS - 1));
    CHECK(rd_loss_keep_stalled_head(wait, 105 * ms, true, RD_LOSS_KEEP_STALL_NS));
    CHECK(!rd_loss_keep_stalled_head(0, 500 * ms, true, 500 * ms));
    {
        RdLossThrottle t = {0};

        /* queued behind the picture before (frame 130 behind the first
           open one, 129): not copied, no attempt */
        CHECK(rd_loss_keep_decide_head(&t, 1, 130, true, 129, wait, 105 * ms, true, 50 * ms) ==
              RD_LOSS_KEEP_NOT_HEAD);
        CHECK(t.attempts == 0);
        /* the frame that is itself the GPU's current picture (130 the first
           open one) counts the whole wait, the head's time ignored: it may
           be the hung one (review K1) */
        RdLossThrottle k1 = {0};
        CHECK(rd_loss_keep_decide_head(&k1, 1, 130, true, 130, wait, 105 * ms, true, 50 * ms) ==
              RD_LOSS_KEEP_TAKE);
        /* a wait under 100 ms is no stall by either rule */
        CHECK(rd_loss_keep_decide_head(&t, 2, 129, true, 129, wait, 90 * ms, true, 90 * ms) ==
              RD_LOSS_KEEP_NO_STALL);
        /* the head unknown: v0.11.14's rule, unchanged */
        CHECK(rd_loss_keep_decide_head(&t, 3, 129, true, 129, wait, 105 * ms, false, 0) ==
              RD_LOSS_KEEP_TAKE);
        CHECK(rd_loss_keep_decide(&t, 4, 129, true, 129, wait, 105 * ms) == RD_LOSS_KEEP_TAKE);
        /* a stuck head (nothing seen finished for 150 ms, the wait as long):
           copied */
        CHECK(rd_loss_keep_decide_head(&t, 5, 129, true, 129, wait, 150 * ms, true, 150 * ms) ==
              RD_LOSS_KEEP_TAKE);
        /* the head stuck since before the wait began: the wait's 105 ms count */
        CHECK(rd_loss_keep_decide_head(&t, 6, 129, true, 129, wait, 105 * ms, true, 400 * ms) ==
              RD_LOSS_KEEP_TAKE);
        CHECK(t.attempts == 4);
        /* the not-sent and open rules come as before */
        RdLossThrottle u = {0};

        CHECK(rd_loss_keep_decide_head(&u, 1, 128, true, 129, wait, 150 * ms, true, 150 * ms) ==
              RD_LOSS_KEEP_NOT_OPEN);
        CHECK(rd_loss_keep_decide_head(&u, 2, 129, false, 129, wait, 150 * ms, true, 150 * ms) ==
              RD_LOSS_KEEP_NOT_SENT);
        CHECK(rd_loss_keep_decide_head(&u, 3, 130, false, 129, wait, 150 * ms, true, 50 * ms) ==
              RD_LOSS_KEEP_NOT_HEAD);
        CHECK(rd_loss_keep_decide_head(&u, 4, 129, false, 129, wait, 150 * ms, true, 50 * ms) ==
              RD_LOSS_KEEP_NOT_SENT);
        CHECK(u.attempts == 0);
    }
    /* the title and menus (issue 179's v0.11.14 log, lines 555-765): two
       pictures in flight, each about 55 ms on the GPU, so a slot wait for
       the newer blocks 105-110 ms of which its own picture has run at most
       55 ms; the frame recycled is behind the first open one (the log's 8
       copies: frame 156 with 155 open, 318 with 316, ...).  v0.11.14's rule
       took 4 copies in every 64 recycles there; the head's takes none.  Then the GPU hangs on frame 300: nothing is seen
       finished, the head's time grows with the wait, the copies come and
       the hung frame is held */
    {
        RdLossThrottle oldT = {0}, headT = {0};
        uint32_t oldTaken = 0, headTaken = 0, notHead = 0;
        uint32_t copies[4] = {0};
        bool hungHeld = false;

        for (uint64_t seen = 1; seen <= 256; seen++) {
            const uint32_t frame = 100u + (uint32_t)seen;
            const uint64_t waitNs = (105 + seen % 6) * ms;
            const uint64_t headNs = (40 + seen % 16) * ms;

            oldTaken += rd_loss_keep_decide(&oldT, seen, frame, true, frame - 1u, wait, waitNs) ==
                        RD_LOSS_KEEP_TAKE;
            const RdLossKeep k = rd_loss_keep_decide_head(&headT, seen, frame, true, frame - 1u,
                                                          wait, waitNs, true, headNs);

            headTaken += k == RD_LOSS_KEEP_TAKE;
            notHead += k == RD_LOSS_KEEP_NOT_HEAD;
        }
        CHECK(oldTaken == 256 / RD_LOSS_KEEP_WINDOW * RD_LOSS_KEEP_PER_WINDOW);
        CHECK(headTaken == 0 && notHead == 256 && headT.attempts == 0);
        /* the hang: frame 300 the first open one from recycle 300 on; its
           wait and the head's time both grow by 40 ms a recycle, and the
           recycle reuses the slot of the frame three before (the ring's
           three), so frame 300's comes at recycle 303, 160 ms in */
        for (uint64_t seen = 300; seen <= 320; seen++) {
            const uint32_t frame = (uint32_t)seen - 3u;
            const uint64_t stuck = (seen - 299) * 40 * ms;

            for (int i = 0; i < 4; i++) {
                if (copies[i] && rd_loss_keep_release(copies[i], 300)) {
                    copies[i] = 0;
                }
            }
            if (rd_loss_keep_decide_head(&headT, seen, frame, true, 300, wait, stuck, true,
                                         stuck) == RD_LOSS_KEEP_TAKE) {
                copies[rd_loss_keep_slot(copies, 4)] = frame;
                headTaken++;
            }
        }
        for (int i = 0; i < 4; i++) {
            hungHeld = hungHeld || copies[i] == 300;
        }
        CHECK(headTaken > 0 && hungHeld);
    }
    /* review K1, the brief's case: a wait queued behind the picture before,
       then the head itself hangs.  The title stream as above (frame n
       recycled behind the open n - 1); then frame 499 finishes and 500
       hangs at once.  The slot wait began while 499 ran, so at frame 500's
       one recycle (it is the first open frame now) the wait is 60 ms past
       the head's time: 130 ms of wait, 70 ms since 499 was seen finished.
       The head's time alone would leave 500 uncopied; the whole wait
       counts for the GPU's current frame, so it is copied and held for the
       loss report.  The recycles after it (frames behind the hung 500, the
       head's time now over 100 ms) copy too, as before */
    {
        RdLossThrottle t = {0};
        uint32_t copies[4] = {0}, taken = 0;
        bool hungHeld = false;

        for (uint32_t n = 401; n < 500; n++) {
            taken += rd_loss_keep_decide_head(&t, n, n, true, n - 1u, wait, 107 * ms, true,
                                              50 * ms) == RD_LOSS_KEEP_TAKE;
        }
        CHECK(taken == 0);
        CHECK(
            !rd_loss_keep_stalled_head(wait, 130 * ms, true, 70 * ms)); /* the head's time alone */
        if (rd_loss_keep_decide_head(&t, 500, 500, true, 500, wait, 130 * ms, true, 70 * ms) ==
            RD_LOSS_KEEP_TAKE) {
            copies[rd_loss_keep_slot(copies, 4)] = 500;
        }
        for (uint32_t n = 501; n <= 503; n++) {
            const uint64_t h = (70 + (n - 500) * 40) * ms;

            if (rd_loss_keep_decide_head(&t, n, n, true, 500, wait, 60 * ms + h, true, h) ==
                RD_LOSS_KEEP_TAKE) {
                copies[rd_loss_keep_slot(copies, 4)] = n;
            }
        }
        for (int i = 0; i < 4; i++) {
            hungHeld = hungHeld || copies[i] == 500;
        }
        CHECK(hungHeld && copies[1] == 501);
    }
    /* review K2: a wait seen by several recycles counts once (the 10 s
       line's "slot waits ... not counted as stalled") */
    {
        uint64_t last = 0;
        unsigned n = 0;
        static const uint64_t seenWaits[] = {41, 41, 41, 42, 42, 0, 43, 41};

        for (unsigned i = 0; i < sizeof(seenWaits) / sizeof(seenWaits[0]); i++) {
            n += rd_loss_keep_wait_new(&last, seenWaits[i]);
        }
        CHECK(n == 4); /* 41, 42, 43 and 41 again after 43; 0 never */
    }
}

int main(void)
{
    issue179();
    issue211();
    order();
    keep();
    history();
    notSent();
    unchanged();
    twoInFlight();
    head();
    if (failures == 0) {
        printf("rd_loss_pick_test: ok\n");
    }
    return failures != 0;
}
