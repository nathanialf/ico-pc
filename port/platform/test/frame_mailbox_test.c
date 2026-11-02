/*
 * port/platform/test/frame_mailbox_test.c
 *
 * The game thread's and the render thread's hand-over (frame_mailbox.h),
 * both sides driven from one thread under a fake clock:
 *   the latest frame wins: three frames published before a take give the
 *     third, two counted as replaced; a take with nothing new is refused;
 *   slot pins: counted per slot, never below 0, a slot out of range ignored;
 *   the lock grid: none before the first set, the last one set after;
 *   jobs run in order with their sequence numbers; a full queue refuses
 *     the next (the caller runs it itself);
 *   the submit's handshake: replay 1's submit is job s; the next replay's
 *     wait (fm_done_up_to(s)) holds until the render thread finished s, not
 *     only started it, and fm_idle only then;
 *   park: refused posts once asked, the queued job runs first, then
 *     parked; unpark takes posts again;
 *   quit with a job queued: the job runs, then quit; a post after quit is
 *     refused;
 *   the heartbeat: the job in progress, its time, none after it finished;
 *     the last present noted for picture jobs only;
 *   the block: idle share, the longest wait for a job and the longest job
 *     over a fake 10 s, counts for pictures and replaced frames, a new block
 *     from the take;
 *   the mode: auto and on run the thread on Vulkan or D3D12, never on a
 *     backend that takes every call on one thread or without a window; off,
 *     sync; an unknown word is auto, said so.
 */
#include <stdio.h>
#include <string.h>
#include "frame_mailbox.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull

static int s_ran[16];
static int s_runs;

static void job(void *arg)
{
    if (s_runs < 16) {
        s_ran[s_runs] = (int)(size_t)arg;
    }
    s_runs++;
}

/* the render thread's side: the next step, and a job run at once from t to
   t + cost */
static FmNext step(FrameMailbox *m, uint64_t t, uint64_t cost)
{
    FmJob j;
    const FmNext n = fm_next(m, &j, t);

    if (n == FM_NEXT_JOB) {
        j.fn(j.arg);
        if (j.kind == FM_JOB_PICTURE) {
            const FmPresent p = {0, 0.0f, cost, 0, 0, t, t + cost, true};
            fm_present_note(m, &p);
        }
        fm_finish(m, &j, t + cost);
    }
    return n;
}

static void testLatest(void)
{
    FrameMailbox m;
    FmFrame f, out;

    fm_init(&m, 0);
    CHECK(!fm_take(&m, &out));
    memset(&f, 0, sizeof f);
    for (uint32_t i = 1; i <= 3; i++) {
        f.number = i;
        f.slot = i % FM_SLOTS;
        f.closeAt = i * 33 * MS;
        CHECK(fm_publish(&m, &f) == (i > 1));
    }
    CHECK(fm_take(&m, &out) && out.number == 3 && out.slot == 3 && out.closeAt == 99 * MS);
    CHECK(!fm_take(&m, &out));
    CHECK(m.replaced == 2);
    f.number = 4;
    CHECK(!fm_publish(&m, &f)); /* the last one was taken: nothing replaced */
    CHECK(fm_take(&m, &out) && out.number == 4);
}

static void testPins(void)
{
    FrameMailbox m;

    fm_init(&m, 0);
    CHECK(!fm_pinned(&m, 1));
    fm_pin(&m, 1);
    fm_pin(&m, 1);
    fm_pin(&m, 2);
    CHECK(fm_pinned(&m, 1) && fm_pinned(&m, 2) && !fm_pinned(&m, 0));
    fm_unpin(&m, 1);
    CHECK(fm_pinned(&m, 1));
    fm_unpin(&m, 1);
    fm_unpin(&m, 1); /* never below 0 */
    CHECK(!fm_pinned(&m, 1) && m.pins[1] == 0);
    fm_pin(&m, FM_SLOTS); /* out of range: ignored */
    CHECK(!fm_pinned(&m, FM_SLOTS));
}

static void testLock(void)
{
    FrameMailbox m;
    FmLock l = {100 * MS, 2, 1, 0.5}, out;

    fm_init(&m, 0);
    CHECK(!fm_lock_get(&m, &out));
    fm_lock_set(&m, &l);
    l.k = 2;
    fm_lock_set(&m, &l);
    CHECK(fm_lock_get(&m, &out) && out.n == 2 && out.k == 2 && out.dueNs == 100 * MS);
}

static void testJobs(void)
{
    FrameMailbox m;
    uint64_t seq[FM_JOBS + 1];

    fm_init(&m, 0);
    s_runs = 0;
    CHECK(fm_idle(&m));
    CHECK(fm_post(&m, NULL, NULL, FM_JOB_CALL) == 0);
    for (unsigned i = 0; i < FM_JOBS; i++) {
        seq[i] = fm_post(&m, job, (void *)(size_t)(i + 1), FM_JOB_CALL);
        CHECK(seq[i] == i + 1);
    }
    /* the queue full: the caller runs it */
    CHECK(fm_post(&m, job, (void *)(size_t)99, FM_JOB_CALL) == 0);
    CHECK(!fm_idle(&m));
    for (unsigned i = 0; i < FM_JOBS; i++) {
        CHECK(step(&m, 10 * MS * i, MS) == FM_NEXT_JOB);
    }
    CHECK(s_runs == (int)FM_JOBS);
    for (unsigned i = 0; i < FM_JOBS; i++) {
        CHECK(s_ran[i] == (int)i + 1); /* in order */
    }
    CHECK(fm_idle(&m) && fm_done_up_to(&m, seq[FM_JOBS - 1]));
    CHECK(step(&m, 100 * MS, 0) == FM_NEXT_WAIT);
}

/* The submit's handshake: replay r's list handed over as job s; replay r +
   1 waits for s to finish before it waits r's fence (rd__wait_frame) */
static void testHandshake(void)
{
    FrameMailbox m;
    FmJob j;

    fm_init(&m, 0);
    s_runs = 0;
    const uint64_t s = fm_post(&m, job, (void *)(size_t)1, FM_JOB_PICTURE);

    CHECK(s == 1 && !fm_done_up_to(&m, s) && !fm_idle(&m));
    /* started, not finished: the next replay still waits */
    CHECK(fm_next(&m, &j, 5 * MS) == FM_NEXT_JOB && j.seq == s);
    CHECK(!fm_done_up_to(&m, s) && !fm_idle(&m));
    {
        uint64_t seq, done;
        const uint64_t busy = fm_heartbeat(&m, 9 * MS, &seq, &done);

        CHECK(seq == s && done == 0 && busy == 4 * MS);
    }
    j.fn(j.arg);
    fm_finish(&m, &j, 12 * MS);
    CHECK(fm_done_up_to(&m, s) && fm_idle(&m));
    {
        uint64_t seq, done;

        CHECK(fm_heartbeat(&m, 20 * MS, &seq, &done) == 0 && seq == 0 && done == s);
    }
    /* a call job notes no present; a picture job does */
    FmPresent p;

    CHECK(!fm_last_present(&m, &p));
    CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) == 2);
    CHECK(step(&m, 20 * MS, 3 * MS) == FM_NEXT_JOB);
    CHECK(fm_last_present(&m, &p) && p.t0 == 20 * MS && p.t1 == 23 * MS && p.shown);
    CHECK(fm_post(&m, job, NULL, FM_JOB_CALL) == 3);
    CHECK(step(&m, 30 * MS, 1 * MS) == FM_NEXT_JOB);
    CHECK(fm_last_present(&m, &p) && p.t1 == 23 * MS && m.presents == 1);
}

static void testPark(void)
{
    FrameMailbox m;

    fm_init(&m, 0);
    s_runs = 0;
    CHECK(fm_post(&m, job, (void *)(size_t)7, FM_JOB_PICTURE) == 1);
    fm_park(&m);
    CHECK(!fm_is_parked(&m));
    CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) == 0); /* the game thread submits it itself */
    CHECK(step(&m, MS, MS) == FM_NEXT_JOB && s_runs == 1 && s_ran[0] == 7);
    CHECK(step(&m, 3 * MS, 0) == FM_NEXT_PARKED && fm_is_parked(&m) && fm_idle(&m));
    CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) == 0);
    CHECK(step(&m, 4 * MS, 0) == FM_NEXT_PARKED);
    fm_unpark(&m);
    CHECK(!fm_is_parked(&m));
    CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) == 2);
    CHECK(step(&m, 5 * MS, MS) == FM_NEXT_JOB && s_runs == 2);
    CHECK(step(&m, 7 * MS, 0) == FM_NEXT_WAIT);
}

static void testQuit(void)
{
    FrameMailbox m;

    fm_init(&m, 0);
    s_runs = 0;
    CHECK(fm_post(&m, job, (void *)(size_t)5, FM_JOB_PICTURE) == 1);
    fm_quit(&m);
    CHECK(fm_post(&m, job, NULL, FM_JOB_CALL) == 0);
    /* the queued job (a submitted list's present) runs before the end */
    CHECK(step(&m, MS, MS) == FM_NEXT_JOB && s_runs == 1 && s_ran[0] == 5);
    CHECK(step(&m, 2 * MS, 0) == FM_NEXT_QUIT);
    CHECK(fm_idle(&m) && fm_done_up_to(&m, 1));
    /* quit while parked ends too */
    fm_init(&m, 0);
    fm_park(&m);
    CHECK(step(&m, MS, 0) == FM_NEXT_PARKED);
    fm_quit(&m);
    CHECK(step(&m, 2 * MS, 0) == FM_NEXT_QUIT);
}

/* 10 s of 60 pictures a second of 4 ms each: 76 % idle, the longest wait
   for a frame 12.7 ms, the longest job 4 ms; one 30 ms job: the heartbeat's
   max; a block taken in a job counts it so far */
static void testBlock(void)
{
    FrameMailbox m;
    FmBlock b;
    FmFrame f;
    uint64_t t = 0;

    fm_init(&m, 0);
    memset(&f, 0, sizeof f);
    for (unsigned i = 0; i < 600; i++) {
        t = (uint64_t)i * 16666667ull;
        f.number = i;
        (void)fm_publish(&m, &f);
        (void)fm_publish(&m, &f); /* each one replaced once */
        CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) != 0);
        CHECK(step(&m, t, i == 300 ? 30 * MS : 4 * MS) == FM_NEXT_JOB);
        (void)fm_take(&m, &f);
    }
    fm_block_take(&m, 10000 * MS, &b);
    printf("frame_mailbox_test: block: idle %.1f %%, waited for a frame %.1f ms, heartbeat max "
           "%.1f ms, %u pictures, %u replaced\n",
           b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures, b.replaced);
    CHECK(b.pictures == 600 && b.jobs == 600 && b.replaced == 600);
    CHECK(b.idlePercent > 75.0 && b.idlePercent < 77.0);
    CHECK(b.waitMaxMs > 12.6 && b.waitMaxMs < 12.7);
    CHECK(b.busyMaxMs > 29.9 && b.busyMaxMs < 30.1);
    /* a new block from the take; a job in progress counted so far */
    CHECK(fm_post(&m, job, NULL, FM_JOB_PICTURE) != 0);
    {
        FmJob j;

        CHECK(fm_next(&m, &j, 10002 * MS) == FM_NEXT_JOB);
        fm_block_take(&m, 10052 * MS, &b);
        CHECK(b.busyMaxMs > 49.9 && b.busyMaxMs < 50.1 && b.pictures == 0);
        CHECK(b.idlePercent > 3.8 && b.idlePercent < 3.9); /* 2 ms of the 52 */
        fm_finish(&m, &j, 10060 * MS);
    }
}

static void testMode(void)
{
    const char *why = NULL;

    CHECK(fm_mode_choose("auto", false, true, &why) == FM_MODE_ON && why != NULL);
    CHECK(fm_mode_choose(NULL, false, true, &why) == FM_MODE_ON);
    CHECK(fm_mode_choose("", false, true, &why) == FM_MODE_ON);
    CHECK(fm_mode_choose("on", false, true, &why) == FM_MODE_ON);
    CHECK(fm_mode_choose("true", false, true, &why) == FM_MODE_ON);
    CHECK(fm_mode_choose("sync", false, true, &why) == FM_MODE_SYNC);
    CHECK(fm_mode_choose("off", false, true, &why) == FM_MODE_OFF &&
          strcmp(why, "[dev] render_thread = off") == 0);
    CHECK(fm_mode_choose("false", false, true, &why) == FM_MODE_OFF);
    /* OpenGL and Direct3D 11: as the setting says, every call on the
       render thread (the context its own) */
    CHECK(fm_mode_choose("auto", true, true, &why) == FM_MODE_ON &&
          strstr(why, "every graphics call on the render thread") != NULL);
    CHECK(fm_mode_choose("on", true, true, &why) == FM_MODE_ON);
    CHECK(fm_mode_choose("sync", true, true, &why) == FM_MODE_SYNC);
    CHECK(fm_mode_choose("off", true, true, &why) == FM_MODE_OFF);
    /* no window: off */
    CHECK(fm_mode_choose("on", false, false, &why) == FM_MODE_OFF && strcmp(why, "no window") == 0);
    /* a word it does not know: auto, said so */
    CHECK(fm_mode_choose("fast", false, true, &why) == FM_MODE_ON && strstr(why, "taken as auto"));
    CHECK(strcmp(fm_mode_name(FM_MODE_ON), "on") == 0 &&
          strcmp(fm_mode_name(FM_MODE_SYNC), "sync") == 0 &&
          strcmp(fm_mode_name(FM_MODE_OFF), "off") == 0);
}

int main(void)
{
    testLatest();
    testPins();
    testLock();
    testJobs();
    testHandshake();
    testPark();
    testQuit();
    testBlock();
    testMode();
    if (fails) {
        printf("frame_mailbox_test: %d failures\n", fails);
        return 1;
    }
    printf("frame_mailbox_test: ok\n");
    return 0;
}
