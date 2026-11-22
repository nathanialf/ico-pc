/* rd_dump_reads_test.c: the game thread's dumps that wait for the reads
 * alone (rd_dump.c dumpFrame's reads-only mode, rd_thread.h
 * rd_thread_reads_wait; issue 179: a kept copy's drain was the late game
 * steps after a GPU stall, a slow-scene dump's 68.8 and 258.6 ms), on
 * frames recorded without a device and a host of the test's own whose
 * render thread is a thread the test starts for each picture handed over:
 *
 *   none open  with no picture handed over the slow-scene dump's three
 *              calls (rd_dump_retained_frame_bytes, _into, _mem_reads: the
 *              last measures the frame and allocates it once, its two
 *              passes each waiting for the reads alone)
 *              make no drain (the host's drain never called) and their
 *              bytes are the drained dump's (rd_dump_retained_frame_mem,
 *              which still drains: the host's drain called, the control);
 *   reads open a picture handed over (rd__replay_frame_async) whose job
 *              starts only after the call began: each of the three waits
 *              for its reads (the game thread's read waits grow, the job
 *              ran) and makes no drain; the bytes are the same;
 *   sent       rd__frame_sent: false for a frame recorded and never handed over,
 *              true once a picture of it is (rd_thread.c noteSent), false again
 *              when its slot is recycled (rd__frame_reset).
 * Each picture's job waits 150 ms after the call is asked for before it
 * starts: the order is what is checked, not a time (a job that starts
 * before the call waits, on a loaded runner, makes the call find no reads
 * open and the read-wait check fails loudly, never passes wrongly). */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ico_thread.h"
#include "rd.h"
#include "rd_dump_mem.h"
#include "rd_internal.h"
#include "rd_thread.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: " __VA_ARGS__);                                                          \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the host: a picture handed over (wait false) runs on a thread of its
 * own once s_go is set and 150 ms have passed; a post that waits runs
 * here; the drain joins the thread and is counted */
static struct {
    IcoThread *th;
    void (*fn)(void *arg);
    void *arg;
} s_job;

static atomic_int s_go, s_ran;
static unsigned s_drains;

#ifdef _WIN32
__declspec(dllimport) void __stdcall Sleep(unsigned long ms);
#endif

static void sleepMs(long ms)
{
#ifdef _WIN32
    Sleep((unsigned long)ms);
#else
    const struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static void jobMain(void *u)
{
    (void)u;
    while (!atomic_load(&s_go)) {
        sleepMs(1);
    }
    sleepMs(150);
    s_job.fn(s_job.arg);
    atomic_store(&s_ran, 1);
}

static bool hostPost(void *u, void (*fn)(void *arg), void *arg, bool picture, bool wait)
{
    (void)u;
    (void)picture;
    if (wait) {
        fn(arg);
        return true;
    }
    if (s_job.th != NULL) {
        return false; /* one at a time (the test's own) */
    }
    s_job.fn = fn;
    s_job.arg = arg;
    atomic_store(&s_go, 0);
    atomic_store(&s_ran, 0);
    s_job.th = ico_thread_create(jobMain, NULL);
    return s_job.th != NULL;
}

static uint64_t hostDrain(void *u)
{
    (void)u;
    s_drains++;
    if (s_job.th != NULL) {
        atomic_store(&s_go, 1);
        ico_thread_join(s_job.th);
        s_job.th = NULL;
    }
    return 0;
}

static void draw(uint32_t key)
{
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    v[1].x = v[1].y = 16;
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, RD_SPACE_UI, 1, key);
}

/* a frame with a texture, a temporary target and draws, closed */
static void recordFrame(RdTex a)
{
    rd_begin_frame();
    RdTarget tt = rd_temp_target(64, 32, 1, 0);
    rd_select_list(7);
    rd_set_target(tt, tt, 64, 32, 0);
    rd_texture(a, RD_TEXFN_DECAL, RD_TCC_RGB);
    draw(1);
    rd_select_list(11);
    rd_texture(rd_target_texture(tt, RD_VIEW_RGB24_TA0), RD_TEXFN_MODULATE, RD_TCC_RGBA);
    draw(2);
    rd_end_frame(0);
}

static bool sameBytes(const void *a, size_t na, const void *b, size_t nb)
{
    return a != NULL && b != NULL && na == nb && memcmp(a, b, na) == 0;
}

/* a picture of frame f handed over, its job not begun (the caller sets
   s_go when its call is asked for) */
static bool handOver(const RdFrame *f)
{
    return rd__replay_frame_async(f, 0, false) && s_job.th != NULL;
}

int main(void)
{
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    uint8_t px[8 * 8 * 4];
    for (int i = 0; i < (int)sizeof(px); i++) {
        px[i] = (uint8_t)(i * 13 + 5);
    }
    RdTex a = rd_create_texture(8, 8, px, RD_TEXA_7F_81_AEM, "a");
    recordFrame(a);
    const RdFrame *f = rd__last_frame();
    const uint32_t number = f->number;
    CHECK(f->closed && !rd__frame_sent(f),
          "sent: a frame recorded and never handed over is not sent");

    /* the drained dump with no host: the bytes every call below must make */
    void *ref = NULL;
    size_t refLen = 0;
    CHECK(rd_dump_retained_frame_mem(number, &ref, &refLen, NULL) && refLen > 0,
          "the drained dump made");

    static const RdThreadHost host = {NULL, hostPost, hostDrain, false, false, NULL};
    rd_set_thread_host(&host);
    CHECK(rd_thread_host_on(), "the test's host is on");

    /* none open: no drain, the same bytes */
    {
        const unsigned d0 = s_drains;
        size_t want = 0, got = 0;
        void *mem = NULL;
        size_t memLen = 0;

        CHECK(rd_dump_retained_frame_bytes(number, &want) && want == refLen,
              "none open: measured %zu, the drained dump %zu", want, refLen);
        uint8_t *blk = want ? malloc(want) : NULL;
        CHECK(blk &&
                  rd_dump_retained_frame_into(number, blk, want, &got, NULL) == RD_DUMP_INTO_OK &&
                  sameBytes(blk, got, ref, refLen),
              "none open: the copy into a block is the drained dump");
        CHECK(rd_dump_retained_frame_mem_reads(number, &mem, &memLen, NULL) &&
                  sameBytes(mem, memLen, ref, refLen),
              "none open: the copy measured and allocated here is the drained dump");
        CHECK(s_drains == d0, "none open: no drain (%u)", s_drains - d0);
        free(blk);
        free(mem);
        /* the control: the drained call drains (the counter sees drains) */
        CHECK(rd_dump_retained_frame_mem(number, &mem, &memLen, NULL) &&
                  sameBytes(mem, memLen, ref, refLen) && s_drains > d0,
              "control: the drained dump drains (%u)", s_drains - d0);
        free(mem);
    }

    /* reads open: each call waits for the picture's reads, no drain */
    for (int call = 0; call < 3; call++) {
        static const char *const names[3] = {"the measure", "the copy into a block",
                                             "the copy allocated here"};
        if (!handOver(f)) {
            CHECK(0, "reads open: a picture handed over (%s)", names[call]);
            break;
        }
        CHECK(rd__frame_sent(f), "sent: the frame handed over is sent (%s)", names[call]);
        const unsigned d0 = s_drains;
        const uint64_t w0 = rd_thread_read_wait_ns();
        size_t want = 0, got = 0;
        void *mem = NULL;
        uint8_t *blk = malloc(refLen);
        bool ok = false;

        atomic_store(&s_go, 1);
        switch (call) {
        case 0:
            ok = rd_dump_retained_frame_bytes(number, &want) && want == refLen;
            break;
        case 1:
            ok = blk &&
                 rd_dump_retained_frame_into(number, blk, refLen, &got, NULL) == RD_DUMP_INTO_OK &&
                 sameBytes(blk, got, ref, refLen);
            break;
        default:
            ok = rd_dump_retained_frame_mem_reads(number, &mem, &got, NULL) &&
                 sameBytes(mem, got, ref, refLen);
            break;
        }
        const uint64_t w1 = rd_thread_read_wait_ns();
        const unsigned d1 = s_drains;
        CHECK(ok, "reads open: %s made the drained dump's bytes", names[call]);
        CHECK(w1 > w0, "reads open: %s waited for the picture's reads", names[call]);
        CHECK(d1 == d0, "reads open: %s made no drain (%u)", names[call], d1 - d0);
        free(blk);
        free(mem);
        (void)rd_thread_drain(); /* the job's end, the thread joined */
        CHECK(atomic_load(&s_ran) == 1, "reads open: the picture's job ran (%s)", names[call]);
    }

    rd_set_thread_host(NULL);
    /* the slot recycled: sent again 0 (the frames after it are not sent) */
    recordFrame(a);
    recordFrame(a);
    recordFrame(a);
    for (int i = 0; i < RD_FRAME_RING; i++) {
        CHECK(g_rd.frames[i].number != number, "the ring moved past frame %u", number);
        CHECK(!rd__frame_sent(&g_rd.frames[i]), "sent: slot %d recycled, not sent", i);
    }
    free(ref);
    rd_destroy_texture(a);
    rd_shutdown();
    if (failures) {
        printf("rd_dump_reads_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_dump_reads_test: ok\n");
    return 0;
}
