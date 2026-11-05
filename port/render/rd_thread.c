/* rd_thread.c: the renderer on the window's render thread (rd_thread.h). */
#include "rd_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ico_thread.h"
#include "rd_internal.h"
#include "rd_submit.h"
#include "rd_submit_thread.h"

static RdThreadHost s_host;
static bool s_on;

/* inside one of the render thread's jobs (a depth: a job's own calls run
 * where they are); the job's hand-over still open (rd__reads_done) */
static _Thread_local int t_onR;
static _Thread_local bool t_handed;
/* the outer job's start (rd__now_ms) and the reads of the last picture
 * handed over, from that start to rd__reads_done (rd_thread_take_reads_ns;
 * both the render thread's own) */
static _Thread_local double t_jobStartMs;
static _Thread_local uint64_t t_readsNs;
/* the longest present call of the job's replays (rd_thread_take_present_ns;
 * the render thread's own) */
static _Thread_local uint64_t t_presentNs;
/* the job's waits inside its reads (its replays' waits for the slot's fence
 * and the submit thread, and the acquire) and its interpolation
 * (rd_thread_take_reads_parts; the render thread's own) */
static _Thread_local uint64_t t_waitNs, t_interpNs;
/* a picture's job: the textures to retire still kept for its first
 * graphics call (rd__thread_retire_kept; the render thread's own) */
static _Thread_local bool t_retireKept;
/* the game thread's read waits in all, ns (rd_thread_read_wait_ns) */
static uint64_t s_readWaitTotalNs;
/* what the render thread does in the open hand-over (RdReadPhase: set at
 * the hand-over on the game thread, then by the render thread; read when a
 * game-thread call begins to wait) */
static int s_phase;
/* the slot ahead (rd__slot_ahead_take): the frame slot waited for at the
 * end of the picture before, -1 none (the render thread's inside its jobs,
 * the game thread's after a drain); ICO_RD_SLOT_AHEAD=0; the tests' hook */
static int s_aheadSlot = -1;
static bool s_aheadOff;
static void (*s_testHook)(int at);
/* the display's wait ahead (rd__room_ahead_take): the next picture's wait
 * for room in the present queue made at the end of the picture before,
 * after its present, outside the hand-over (rhi_wait_present_room).
 * s_roomAhead: made and not yet taken by an acquire (atomic: any thread's
 * acquire takes it); s_roomKnown: the backend has that wait (an answer
 * true since the host came; the render thread's); s_roomOff:
 * ICO_RD_ACQUIRE_AHEAD=0; s_roomT0/T1: the last one's span (rd__now_ms),
 * written in the job and read by the game thread after its drain; the
 * tests' seam in place of the backend's call */
static int s_roomAhead;
static bool s_roomKnown, s_roomOff;
static double s_roomT0, s_roomT1;
static bool (*s_roomHook)(void);

/* the hand-over's lock: the count of pictures whose reads have not ended,
 * the game thread's waits for them, the textures to retire, the block's
 * sums and the render thread's counts as last published */
static IcoMutex *s_lk;
static IcoCond *s_cv;
static int s_owned;
static RhiTexture *s_retire;
static uint32_t s_retireN, s_retireCap;
static RdThreadBlock s_block;
static RdStats s_rStats; /* the render thread's own counts (rd__stats_here) */
static RdStats s_rSnap;  /* as its last reads' end left them */
static RdStats s_view;   /* rd_get_stats's answer on the game thread */

/* the game thread's: the picture handed over (one at a time), its
 * result, rd_submit_hold, the waits for the picture before */
typedef struct Tramp {
    void (*fn)(void *arg);
    void *arg;
    bool handed;
    /* set to 0 at the job's very end (its slot ahead and display's wait
     * ahead included): rd__thread_picture_busy's flag; NULL none */
    int *busy;
} Tramp;

/* armSmooth: Motion blur: Smooth armed at one present a tick before the
 * replay, as rd_end_frame's replay inside the step arms it (rd_core.c) */
typedef struct ReplayArgs {
    const RdFrame *f;
    int keep;
    bool present, ok, armSmooth;
} ReplayArgs;

static ReplayArgs s_picReplay;

typedef struct PresentArgs {
    float alpha;
    double raw;
    bool ok;
} PresentArgs;

static PresentArgs s_picPresent;
static Tramp s_picTramp;
static bool s_async, s_asyncPresent, s_lastOk = true, s_hold;
static uint64_t s_waitedNs;
/* the frame of the last present rd_present_post made (0: none since the
 * host came): a present of another frame is a new frame's, for the 10 s
 * line's split of the waits (RdThreadBlock.freshWaits) */
static uint32_t s_lastPresentFrame;
static uint32_t s_failedPresents; /* rd_thread_failed_presents */
/* Frame rate Original's hand-over (rd__replay_frame_post): its picture's
 * job not yet ended (atomic; set by the game thread as it posts, cleared
 * by the render thread at the job's end); [dev] original_async = false
 * (ICO_RD_ORIGINAL_ASYNC=0, read with the host); the replay inside the
 * game's step running on this thread (rd__thread_in_step) */
static int s_picBusy;
static bool s_origOff;
static _Thread_local bool t_inStep;

static void lk(void)
{
    if (s_lk) {
        ico_mutex_lock(s_lk);
    }
}

static void ulk(void)
{
    if (s_lk) {
        ico_mutex_unlock(s_lk);
    }
}

/* the render thread's counts as the game thread will read them (its lock
 * held) */
static void publishLocked(void)
{
    s_rSnap = s_rStats;
    s_rSnap.pipelines = rd__pipeline_count();
}

static void retireDrain(void)
{
    lk();
    RhiTexture *list = s_retire;
    const uint32_t n = s_retireN;
    s_retire = NULL;
    s_retireN = s_retireCap = 0;
    ulk();
    for (uint32_t i = 0; i < n; i++) {
        rhi_destroy_texture(list[i]);
    }
    free(list);
    if (n) {
        lk();
        s_block.retired += n;
        ulk();
    }
}

/* The ownership is handed over here, on the game thread, as it posts a
 * picture (rd_present_post, rd__replay_frame_async), with the render
 * thread drained first: the only thread whose calls wait for the reads is
 * this one, so a call that waited for the picture before has run before
 * this picture's reads can begin (no second picture's reads behind a
 * waiting call) */
static void handOver(void)
{
    lk();
    __atomic_store_n(&s_phase, RD_READ_HANDOVER, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    ulk();
}

static void giveBack(void)
{
    lk();
    __atomic_sub_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    if (s_cv) {
        ico_cond_broadcast(s_cv);
    }
    ulk();
}

/* The slot ahead (rd_thread.h rd__slot_ahead_take): at the end of a
 * picture's job handed over, its reads ended and its list posted to the
 * submit thread, the next picture's frame slot waited for here, outside
 * the hand-over: the game thread's calls go on meanwhile (they change the
 * textures, meshes and frames; this reads none of them: the submit
 * thread's jobs of that slot, the slot's fence and its recycling, the bind
 * groups' epoch); a game-thread graphics call waits for this job through
 * the call guard as for any job */
static void slotAhead(void)
{
    if (s_aheadOff || s_aheadSlot >= 0 || !g_rd.hasDevice || rhi_device_lost()) {
        return;
    }
    const bool apart = rd_thread_submit_apart();
    if (rd_thread_submit_on() && !apart) {
        return; /* one at a time: the wait would be the submit thread's whole job on this picture */
    }
    const double t0 = rd__now_ms();
    if (s_testHook) {
        s_testHook(RD_READ_PHASES);
    }
    if (apart) {
        /* as rd__submit_drain: the jobs of the slot rhi_wait_frame recycles */
        (void)rd_thread_submit_wait_slot((rhi_frame_slot() + 1u) % RHI_FRAMES_IN_FLIGHT);
    }
    rhi_wait_frame();
    rd__bind_epoch_next(); /* as rd__wait_frame's: the slot's bind groups anew */
    s_aheadSlot = (int)rhi_frame_slot();
    const double ms = rd__now_ms() - t0;
    lk();
    s_block.aheads++;
    s_block.aheadMs += ms;
    s_block.aheadMaxMs = ms > s_block.aheadMaxMs ? ms : s_block.aheadMaxMs;
    ulk();
}

/* The display's wait ahead (rd_thread.h rd__room_ahead_take): at the end
 * of a picture's job handed over that presented, its reads ended, its
 * submit and present done (no submit thread: rd__submit_frame made both
 * in this job) and the next picture's slot waited for (slotAhead), the
 * wait for room in the present queue the next acquire would make, made
 * here: Direct3D 11's frame latency wait.  Outside the hand-over: the game
 * thread's calls go on meanwhile (this reads nothing of theirs; the
 * backend's swap chain is this job's, a game-thread graphics call waits
 * for the job through the call guard as for any job).  The next post's
 * drain waits for what is left of it: the present's own wait, counted as
 * the display's (rd_submit_waited_ns, the window's present_once).  Not with
 * the submit thread on (the present is that thread's, after this job),
 * not for a picture that presented nothing, not under
 * ICO_RD_ACQUIRE_AHEAD=0. */
static void roomAhead(void)
{
    if (s_roomOff || t_presentNs == 0 || !g_rd.hasDevice || rhi_device_lost() ||
        rd_thread_submit_on()) {
        return;
    }
    const double t0 = rd__now_ms();
    const bool made = s_roomHook != NULL ? s_roomHook() : rhi_wait_present_room();
    const double t1 = rd__now_ms();
    if (!made) {
        return; /* no such wait (Vulkan, OpenGL), or none to make now */
    }
    if (!s_roomKnown) {
        /* the first on this host: the backend has the wait (an event, the
           first 8 then every 100th) */
        static unsigned s_lines;
        s_roomKnown = true;
        if (++s_lines <= 8 || s_lines % 100 == 0) {
            rd__log("render thread: the display's wait for room in the present queue made at the "
                    "end of a picture, after its present (%.1f ms): the next picture's acquire "
                    "does not wait inside its reads",
                    t1 - t0);
        }
    }
    s_roomT0 = t0;
    s_roomT1 = t1;
    __atomic_store_n(&s_roomAhead, 1, __ATOMIC_RELEASE);
    const double ms = t1 - t0;
    lk();
    s_block.roomAheads++;
    s_block.roomAheadMs += ms;
    s_block.roomAheadMaxMs = ms > s_block.roomAheadMaxMs ? ms : s_block.roomAheadMaxMs;
    ulk();
}

void rd__room_ahead_take(void)
{
    const int had = __atomic_exchange_n(&s_roomAhead, 0, __ATOMIC_ACQ_REL);
    if (!had && s_on && t_onR != 0 && t_handed && s_roomKnown && !s_roomOff) {
        lk();
        s_block.roomInside++; /* the wait in this picture's reads, as before */
        ulk();
    }
}

void rd__thread_room_hook(bool (*fn)(void))
{
    s_roomHook = fn;
}

/* a drain of the game thread's present (d0 its start, ns its wait): the
 * part of it that waited for the display's wait ahead of the picture
 * before (the span the job recorded; one that ended before d0 was an
 * earlier job's) */
static void noteRoomDrain(double d0, uint64_t ns)
{
    if (ns == 0 || !(s_roomT1 > d0)) {
        return;
    }
    const double d1 = d0 + (double)ns / 1e6;
    const double a = s_roomT0 > d0 ? s_roomT0 : d0;
    const double b = s_roomT1 < d1 ? s_roomT1 : d1;
    if (b > a) {
        lk();
        s_block.roomDrains++;
        s_block.roomDrainMs += b - a;
        ulk();
    }
}

bool rd__slot_ahead_take(void)
{
    if (s_on && t_onR == 0) {
        (void)rd_thread_drain(); /* the render thread's slot ahead ended: its record read below */
    }
    const int slot = s_aheadSlot;
    s_aheadSlot = -1;
    if (slot >= 0 && (uint32_t)slot == rhi_frame_slot()) {
        return true;
    }
    if (s_on && t_onR != 0 && t_handed) {
        lk();
        s_block.fenceInside++; /* the wait in this picture's reads, as before */
        ulk();
    }
    return false;
}

void rd__thread_test_hook(void (*fn)(int at))
{
    s_testHook = fn;
}

void rd__thread_phase(RdReadPhase phase)
{
    if (t_onR != 0 && t_handed) {
        __atomic_store_n(&s_phase, (int)phase, __ATOMIC_RELAXED);
        if (s_testHook) {
            s_testHook((int)phase);
        }
    }
}

/* a job on the render thread: its reads handed over or not, the textures
 * the game thread destroyed since the last job destroyed first */
static void runTramp(Tramp *t, bool picture)
{
    const bool outer = t_onR == 0;
    const bool handed = t_handed;
    t_onR++;
    t_handed = t->handed;
    rd__thread_phase(RD_READ_INTERP); /* begun here: up to its fence wait */
    if (outer) {
        t_jobStartMs = rd__now_ms();
        t_presentNs = 0; /* this job's presents only */
        t_waitNs = t_interpNs = 0;
        if (picture) {
            /* a picture's interpolation makes no graphics call: the
             * textures are destroyed at its first one, after the submit
             * thread's wait there (rd__thread_retire_kept), so the
             * interpolation runs while the submit thread works and not
             * behind it (a destroy's call guard waits for that thread) */
            t_retireKept = true;
            s_rStats.draws = 0; /* the picture's draws, as rd_begin_frame's count was */
        } else {
            retireDrain();
        }
    }
    t->fn(t->arg);
    rd__reads_done();
    if (outer && t_retireKept) {
        /* a picture that made no graphics call (nothing to blend) */
        t_retireKept = false;
        retireDrain();
    }
    if (outer) {
        /* the list kept for the submit thread (rd_submit_thread.h): its
         * end, submit and present there, this thread free for the next */
        rd__submit_post_pending();
        rd__overlay_hand_over_drop(); /* a picture whose present drew nothing */
        lk();
        publishLocked();
        if (picture) {
            s_block.pictures++;
        }
        ulk();
        if (picture && t->handed) {
            slotAhead(); /* the next picture's slot, the hand-over ended */
            roomAhead(); /* and its room in the present queue */
        }
        if (t->busy != NULL) {
            /* the last read of t: the game thread may post the next
               picture into it from here */
            __atomic_store_n(t->busy, 0, __ATOMIC_RELEASE);
        }
    }
    t_handed = handed;
    t_onR--;
}

static void pictureTramp(void *arg)
{
    runTramp(arg, true);
}

static void callTramp(void *arg)
{
    runTramp(arg, false);
}

void rd_set_thread_host(const RdThreadHost *host)
{
    if (s_on) {
        (void)rd_thread_drain();
        rd__submit_thread_stop(); /* everything posted to it finished first */
        /* the render thread's counts back into g_rd.stats: the counters
         * the window takes deltas of keep going */
        g_rd.stats.draws += s_rStats.draws;
        g_rd.stats.pipelineCreates += s_rStats.pipelineCreates;
        g_rd.stats.textureUploads += s_rStats.textureUploads;
        g_rd.stats.tempTargets += s_rStats.tempTargets;
        g_rd.stats.tempReused += s_rStats.tempReused;
        memset(&s_rStats, 0, sizeof(s_rStats));
    }
    s_on = false;
    s_host = (RdThreadHost){0};
    s_aheadSlot = -1; /* the next replay waits for its own slot */
    __atomic_store_n(&s_roomAhead, 0, __ATOMIC_RELAXED);
    s_roomKnown = false;
    s_roomT0 = s_roomT1 = 0.0;
    retireDrain(); /* here: nothing is posted any more */
    if (host != NULL && host->post != NULL && host->drain != NULL) {
        if (!s_lk) {
            s_lk = ico_mutex_create();
            s_cv = ico_cond_create();
        }
        if (!s_lk || !s_cv) {
            rd__log("render thread: no lock could be made: the replays stay on this thread");
            return;
        }
        rd__perf_lock_init();
        s_host = *host;
        memset(&s_rStats, 0, sizeof(s_rStats));
        lk();
        publishLocked();
        memset(&s_block, 0, sizeof(s_block));
        ulk();
        s_owned = 0;
        {
            /* the slot ahead's switch, its line on each start (the first
               8, then every 100th) */
            static unsigned s_aheadLines;
            const char *e = getenv("ICO_RD_SLOT_AHEAD");
            s_aheadOff = e != NULL && strcmp(e, "0") == 0;
            if (++s_aheadLines <= 8 || s_aheadLines % 100 == 0) {
                rd__log("render thread: %s",
                        s_aheadOff ? "the next picture waits for its frame slot inside its reads "
                                     "(ICO_RD_SLOT_AHEAD=0: the game thread's calls wait through "
                                     "the GPU's fence)"
                                   : "the next picture's frame slot is waited for at the end of "
                                     "the picture before, outside the hand-over (the game "
                                     "thread's calls that change the renderer's state no longer "
                                     "wait through the GPU's fence in a picture's reads; with "
                                     "the submit thread and the frame slots one at a time, inside "
                                     "as before; ICO_RD_SLOT_AHEAD=0 keeps it inside)");
            }
        }
        {
            /* the display's wait ahead's switch ([dev] acquire_ahead), its
               line on each start (the first 8, then every 100th) */
            static unsigned s_roomLines;
            const char *e = getenv("ICO_RD_ACQUIRE_AHEAD");
            s_roomOff = e != NULL && strcmp(e, "0") == 0;
            if (++s_roomLines <= 8 || s_roomLines % 100 == 0) {
                rd__log("render thread: %s",
                        s_roomOff ? "the next picture waits for room in the present queue inside "
                                    "its reads (ICO_RD_ACQUIRE_AHEAD=0, [dev] acquire_ahead = "
                                    "false: the game thread's calls wait through the display)"
                                  : "the next picture's wait for room in the present queue "
                                    "(Direct3D 11's frame latency wait) is made at the end of "
                                    "the picture before, after its present, outside the "
                                    "hand-over (the game thread's calls that change the "
                                    "renderer's state no longer wait through the display; the "
                                    "present after waits for what is left, as the display's "
                                    "wait; with the submit thread on, or where the acquire "
                                    "makes the wait itself, nothing changes; "
                                    "ICO_RD_ACQUIRE_AHEAD=0 keeps it inside)");
            }
        }
        s_async = false;
        s_lastOk = true;
        s_lastPresentFrame = 0;
        __atomic_store_n(&s_picBusy, 0, __ATOMIC_RELAXED);
        s_on = true;
        if (host->submit && !host->sync) {
            /* the pictures' submit and present on a thread of their own */
            (void)rd__submit_thread_start(host->submitBegin, host->user);
        }
        {
            /* Frame rate Original's hand-over (rd__replay_frame_post), its
               escape ([dev] original_async = false), its line on each start
               (the first 8, then every 100th) */
            static unsigned s_origLines;
            const char *e = getenv("ICO_RD_ORIGINAL_ASYNC");
            s_origOff = e != NULL && strcmp(e, "0") == 0;
            if (++s_origLines <= 8 || s_origLines % 100 == 0) {
                if (s_origOff || host->sync) {
                    rd__log("render thread: with Frame rate Original each game frame's picture is "
                            "replayed and presented inside the game step, the game thread "
                            "waiting (%s)",
                            host->sync ? "[dev] render_thread = sync"
                                       : "[dev] original_async = false");
                } else {
                    rd__log("render thread: with Frame rate Original each game frame's picture "
                            "is handed over as the other frame rates' are: its submit and "
                            "present run on the %s; the game step waits for the picture "
                            "before's reads only, and skips a picture when the render thread is "
                            "still busy, but a kept frame (a fade, a load) still waits for the "
                            "picture before, its present included (counted in the 10 s line's "
                            "\"waited for the picture before\") ([dev] original_async = false "
                            "replays it inside the step as before)",
                            rd_thread_submit_on() ? "submit thread" : "render thread");
                }
            }
        }
    }
}

bool rd_thread_host_on(void)
{
    return s_on;
}

bool rd_thread_on_render(void)
{
    return t_onR != 0;
}

/* the render thread's jobs finished (the submit thread's may still run:
 * a present's wait for the picture before, rd_present_post); 0 and
 * nothing done on the render thread */
static uint64_t renderDrain(void)
{
    if (!s_on || t_onR) {
        return 0;
    }
    const uint64_t ns = s_host.drain(s_host.user);
    if (s_async) {
        s_lastOk = s_asyncPresent ? s_picPresent.ok : s_picReplay.ok;
        if (s_asyncPresent && !s_lastOk) {
            s_failedPresents++;
        }
        s_async = false;
        s_asyncPresent = false;
    }
    return ns;
}

uint64_t rd_thread_drain(void)
{
    if (!s_on) {
        return 0;
    }
    /* the render thread's jobs, then the submit thread's (on the render
     * thread: the submit thread's alone, its list kept included) */
    const uint64_t ns = renderDrain();
    return ns + rd_thread_submit_drain();
}

bool rd__thread_handed(void)
{
    return t_onR != 0 && t_handed;
}

bool rd_thread_last_ok(void)
{
    return s_lastOk;
}

uint32_t rd_thread_failed_presents(void)
{
    return s_failedPresents;
}

void rd_submit_hold(bool wait)
{
    s_hold = wait;
}

bool rd_submit_async(void)
{
    return s_async;
}

uint64_t rd_submit_waited_ns(void)
{
    return s_waitedNs;
}

uint64_t rd_thread_take_reads_ns(void)
{
    const uint64_t ns = t_readsNs;
    t_readsNs = 0;
    return ns;
}

void rd_thread_take_reads_parts(uint64_t *waitNs, uint64_t *interpNs)
{
    *waitNs = t_waitNs;
    *interpNs = t_interpNs;
    t_waitNs = t_interpNs = 0;
}

uint64_t rd_thread_read_wait_ns(void)
{
    return __atomic_load_n(&s_readWaitTotalNs, __ATOMIC_RELAXED);
}

uint64_t rd_thread_take_present_ns(void)
{
    uint64_t ns = t_presentNs;
    t_presentNs = 0;
    /* and the submit thread's presents finished since (rd_submit_thread.h) */
    const uint64_t sub = rd_thread_submit_take_present_ns();
    return sub > ns ? sub : ns;
}

void rd__thread_note_present(double ms)
{
    if (!s_on || !t_onR) {
        return;
    }
    const uint64_t ns = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u; /* 0 is "none" */
    t_presentNs = ns > t_presentNs ? ns : t_presentNs;
}

void rd_thread_block_take(RdThreadBlock *out)
{
    lk();
    *out = s_block;
    memset(&s_block, 0, sizeof(s_block));
    ulk();
    if (out->pictures) {
        out->interpMs /= out->pictures;
        out->walkMs /= out->pictures;
        out->submitMs /= out->pictures;
        out->waitMs /= out->pictures;
    }
}

void rd_thread_read_waits_text(const RdThreadBlock *b, char *out, size_t cap)
{
    static const char *const names[RD_READ_PHASES] = {
        "the hand-over (the picture not yet begun there)",
        "the interpolation",
        "its frame slot's fence",
        "the acquire",
        "the ring and the uploads",
        "the walk",
    };
    size_t n = 0;
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    if (b->readWaits != 0) {
        n += (size_t)snprintf(out + n, cap - n, "; the render thread as each began:");
        for (int i = 0; i < RD_READ_PHASES && n < cap; i++) {
            n += (size_t)snprintf(out + n, cap - n, "%s %s %u (%.1f ms)", i ? "," : "", names[i],
                                  b->readWaitBy[i], b->readWaitByMs[i]);
        }
    }
    if (n < cap) {
        n += (size_t)snprintf(out + n, cap - n,
                              "; the next picture's frame slot waited for at the end of the one "
                              "before %u times (%.1f ms in all, longest %.1f ms), %u pictures "
                              "waited for their slot inside their reads",
                              b->aheads, b->aheadMs, b->aheadMaxMs, b->fenceInside);
    }
    if (n < cap && (b->roomAheads != 0 || b->roomInside != 0 || b->roomDrains != 0)) {
        /* the display's share: where its wait was made (left out where the
           backend has none to make ahead) */
        (void)snprintf(out + n, cap - n,
                       "; the display's wait for room in the present queue made at the end of "
                       "the picture before %u times (%.1f ms in all, longest %.1f ms; the "
                       "presents waited for it %u times, %.1f ms, as the display's wait), %u "
                       "pictures waited for it inside their reads",
                       b->roomAheads, b->roomAheadMs, b->roomAheadMaxMs, b->roomDrains,
                       b->roomDrainMs, b->roomInside);
    }
}

/* ------------------------------------------------------------ texLock */

void rd__texlock_wait(void)
{
    if (!s_on || t_onR || __atomic_load_n(&s_owned, __ATOMIC_ACQUIRE) == 0) {
        return;
    }
    const double t0 = rd__now_ms();
    /* what the render thread was doing as this wait began (RdReadPhase) */
    int phase = __atomic_load_n(&s_phase, __ATOMIC_RELAXED);
    if (phase < 0 || phase >= RD_READ_PHASES) {
        phase = RD_READ_HANDOVER;
    }
    lk();
    while (__atomic_load_n(&s_owned, __ATOMIC_ACQUIRE) != 0) {
        ico_cond_wait(s_cv, s_lk);
    }
    const double ms = rd__now_ms() - t0;
    __atomic_fetch_add(&s_readWaitTotalNs, ms > 0.0 ? (uint64_t)(ms * 1e6) : 0u, __ATOMIC_RELAXED);
    s_block.readWaits++;
    s_block.readWaitMs += ms;
    s_block.readWaitMaxMs = ms > s_block.readWaitMaxMs ? ms : s_block.readWaitMaxMs;
    s_block.readWaitBy[phase]++;
    s_block.readWaitByMs[phase] += ms;
    ulk();
}

void rd__thread_retire_kept(void)
{
    if (t_retireKept && t_onR != 0) {
        t_retireKept = false;
        retireDrain();
    }
}

void rd__reads_done(void)
{
    if (!t_handed) {
        return;
    }
    t_handed = false;
    {
        const double ms = rd__now_ms() - t_jobStartMs;
        t_readsNs = ms > 0.0 ? (uint64_t)(ms * 1e6) : 1u;
    }
    lk();
    publishLocked();
    __atomic_sub_fetch(&s_owned, 1, __ATOMIC_RELEASE);
    ico_cond_broadcast(s_cv);
    ulk();
}

void rd__retire_texture(RhiTexture t)
{
    if (!t.id) {
        return;
    }
    if (!s_on || t_onR) {
        rhi_destroy_texture(t);
        return;
    }
    bool kept = false;
    lk();
    if (s_retireN == s_retireCap) {
        const uint32_t cap = s_retireCap ? s_retireCap * 2u : 64u;
        RhiTexture *n = realloc(s_retire, (size_t)cap * sizeof(*n));
        if (n) {
            s_retire = n;
            s_retireCap = cap;
        }
    }
    if (s_retireN < s_retireCap) {
        s_retire[s_retireN++] = t;
        kept = true;
    }
    ulk();
    if (!kept) {
        rhi_destroy_texture(t); /* no memory for the list: the call guard waits for the thread */
    }
}

bool rd__render_call(void (*fn)(void *arg), void *arg)
{
    if (!s_on) {
        return false;
    }
    if (t_onR) {
        fn(arg);
        return true;
    }
    Tramp t = {fn, arg, false, NULL};
    return s_host.post(s_host.user, callTramp, &t, false, true);
}

/* ---------------------------------------------------------- the replays */

static void replayJob(void *arg)
{
    ReplayArgs *r = arg;
    if (r->armSmooth) {
        rd__blur_smooth_arm(1, 1); /* Motion blur: Smooth at one present a tick */
    }
    r->ok = rd__replay_frame_here(r->f, r->keep, r->present);
}

bool rd__replay_frame(const RdFrame *f, int keep, bool present)
{
    if (!s_on || t_onR) {
        return rd__replay_frame_here(f, keep, present);
    }
    ReplayArgs r = {f, keep, present, false, false};
    Tramp t = {replayJob, &r, false, NULL};
    if (present) {
        (void)rd_thread_drain();
        rd__overlay_hand_over(); /* the UI's layers on this thread (rd_present.c) */
    }
    if (s_host.post(s_host.user, pictureTramp, &t, present, true)) {
        return r.ok;
    }
    retireDrain();
    r.ok = rd__replay_frame_here(f, keep, present);
    rd__overlay_hand_over_drop();
    return r.ok;
}

/* a picture waits for its end when its result is read right after it, and
 * every picture under a host whose posts all wait (sync) */
static bool pictureWaits(void)
{
    return s_hold || s_host.sync || rd__capture_armed() || rd__blended_dump_armed();
}

/* the picture before finished: its result, the wait counted (the
 * window's display wait, present_once) */
void rd__thread_picture_begin(void)
{
    /* the render thread's picture before: its submit and present may go on
     * on the submit thread (rd_submit_thread.h) */
    const double d0 = rd__now_ms();
    const uint64_t ns = renderDrain();
    s_waitedNs += ns;
    noteRoomDrain(d0, ns);
}

bool rd__replay_frame_async(const RdFrame *f, int keep, bool present)
{
    if (!s_on || t_onR) {
        return rd__replay_frame_here(f, keep, present);
    }
    rd__thread_picture_begin();
    const bool wait = pictureWaits();
    if (present) {
        rd__overlay_hand_over(); /* the UI's layers on this thread (rd_present.c) */
    }
    s_picReplay = (ReplayArgs){f, keep, present, false, false};
    s_picTramp = (Tramp){replayJob, &s_picReplay, !wait, NULL};
    if (!wait) {
        handOver();
    }
    if (s_host.post(s_host.user, pictureTramp, &s_picTramp, present, wait)) {
        s_async = !wait;
        s_asyncPresent = false;
        if (wait) {
            s_lastOk = s_picReplay.ok;
            return s_picReplay.ok;
        }
        return true;
    }
    if (!wait) {
        giveBack();
    }
    retireDrain();
    s_lastOk = rd__replay_frame_here(f, keep, present);
    rd__overlay_hand_over_drop(); /* nothing posted holds it */
    return s_lastOk;
}

/* ------------------------------------------ Frame rate Original's post */

bool rd__replay_frame_post_ok(void)
{
    return s_on && t_onR == 0 && !s_origOff && !pictureWaits();
}

/* The rule's input (rd.h RdReplayGate busy): the render thread has not
 * ended the job of the picture before (its walk, or its acquire waiting
 * behind a present, its slot ahead waiting for the GPU), or the submit
 * thread has not ended that picture's submit and present (the swapchain's
 * lock is held through the driver's present call, vk_swapchain.c, and the
 * next acquire takes it: a picture posted now would wait for that present
 * inside its reads, and the game step's next renderer call with it) */
bool rd__thread_picture_busy(void)
{
    if (!rd__replay_frame_post_ok()) {
        return false;
    }
    return __atomic_load_n(&s_picBusy, __ATOMIC_ACQUIRE) != 0 || rd_thread_submit_unfinished();
}

bool rd__thread_picture_busy_render(void)
{
    return __atomic_load_n(&s_picBusy, __ATOMIC_ACQUIRE) != 0;
}

bool rd__replay_frame_post(const RdFrame *f, int keep, double *waitMs)
{
    *waitMs = 0.0;
    if (!rd__replay_frame_post_ok()) {
        return false;
    }
    /* the picture before ended, its present too: a no-op unless the gate
       presented this frame while busy (a keep frame), or there is none */
    const double d0 = rd__now_ms();
    const uint64_t ns = renderDrain();
    noteRoomDrain(d0, ns);
    if (ns != 0) {
        lk();
        s_block.freshWaits++; /* the 10 s line's slot waits by a new frame's present */
        s_block.freshWaitMs += (double)ns / 1e6;
        ulk();
    }
    (void)rd_thread_submit_drain();
    *waitMs = rd__now_ms() - d0;
    /* rd_last_present_info names this frame only now: the picture before's
       job may still present inside the wait above, and its screenshot
       check (rd_present.c shotTake) would read this newer frame and drop a
       shot armed for it as later than armed */
    rd__note_frame_present(f);
    /* the UI's layers on this thread, before the hand-over (rd_present.c
       s_pre), as rd__replay_frame_async */
    rd__overlay_hand_over();
    s_picReplay = (ReplayArgs){f, keep, true, false, true};
    s_picTramp = (Tramp){replayJob, &s_picReplay, true, &s_picBusy};
    __atomic_store_n(&s_picBusy, 1, __ATOMIC_RELEASE);
    handOver();
    if (s_host.post(s_host.user, pictureTramp, &s_picTramp, true, false)) {
        s_async = true;
        s_asyncPresent = false;
        return true;
    }
    /* refused (parked, the queue full): nothing posted holds the state;
       the caller replays it inside the step on the host's stack */
    __atomic_store_n(&s_picBusy, 0, __ATOMIC_RELEASE);
    giveBack();
    rd__overlay_hand_over_drop();
    return false;
}

void rd__thread_in_step(bool on)
{
    t_inStep = on;
}

bool rd__thread_in_step_now(void)
{
    return t_inStep;
}

/* ------------------------------------------------------- the presents */

/* the present clock's value the poster took, for rd_present on the
 * render thread (rd__thread_clock_raw) */
static _Thread_local bool t_rawSet;
static _Thread_local double t_raw;

static void presentJob(void *arg)
{
    PresentArgs *p = arg;
    const bool set = t_rawSet;
    const double raw = t_raw;
    t_rawSet = true;
    t_raw = p->raw;
    p->ok = rd_present(p->alpha);
    t_rawSet = set;
    t_raw = raw;
}

double rd__thread_clock_raw(void)
{
    if (t_rawSet) {
        const double r = t_raw;
        t_rawSet = false; /* taken: a second present in the job finds none */
        return r;
    }
    return rd__present_clock_raw_take();
}

/* rd_present_post's wait for the picture before: counted as
 * rd__thread_picture_begin does, and by whose it is, a new frame's present
 * (fresh) or a further one's */
static void presentDrain(bool fresh)
{
    const double d0 = rd__now_ms();
    const uint64_t ns = renderDrain(); /* the submit thread's job may go on */
    s_waitedNs += ns;
    noteRoomDrain(d0, ns);
    if (ns != 0) {
        lk();
        if (fresh) {
            s_block.freshWaits++;
            s_block.freshWaitMs += (double)ns / 1e6;
        } else {
            s_block.furtherWaits++;
            s_block.furtherWaitMs += (double)ns / 1e6;
        }
        ulk();
    }
}

bool rd_present_post(float alpha)
{
    if (!s_on || t_onR) {
        return rd_present(alpha);
    }
    {
        /* the frames are the game thread's: no wait to read which */
        const RdFrame *last = rd__last_frame();
        const uint32_t number = last != NULL ? last->number : 0u;
        const bool fresh = number != s_lastPresentFrame;
        s_lastPresentFrame = number;
        presentDrain(fresh);
    }
    const RdFrame *cur = rd__last_frame();
    if (!rd_interpolation_active() || !g_rd.hasDevice || g_rd.videoShown || cur == NULL ||
        !cur->closed) {
        /* nothing to hand over: its answer here, the render thread idle */
        return rd_present(alpha);
    }
    const bool wait = pictureWaits();
    /* the UI's layers on this thread, before the hand-over: their
     * callbacks share port/ui's font state with the step's own text
     * (rd_present.c s_pre) */
    rd__overlay_hand_over();
    s_picPresent = (PresentArgs){alpha, rd__present_clock_raw_take(), false};
    s_picTramp = (Tramp){presentJob, &s_picPresent, !wait, NULL};
    if (!wait) {
        handOver();
    }
    if (s_host.post(s_host.user, pictureTramp, &s_picTramp, true, wait)) {
        s_async = !wait;
        s_asyncPresent = !wait;
        if (wait) {
            s_lastOk = s_picPresent.ok;
            return s_picPresent.ok;
        }
        return true;
    }
    if (!wait) {
        giveBack();
    }
    retireDrain();
    presentJob(&s_picPresent);
    if (!s_async) {
        rd__overlay_hand_over_drop(); /* no picture handed over holds it */
    }
    s_lastOk = s_picPresent.ok;
    return s_lastOk;
}

/* --------------------------------------------------------------- stats */

RdStats *rd__stats_here(void)
{
    return s_on && t_onR ? &s_rStats : &g_rd.stats;
}

const RdStats *rd__thread_stats(void)
{
    lk();
    s_view = g_rd.stats;
    s_view.draws += s_rSnap.draws;
    s_view.pipelineCreates += s_rSnap.pipelineCreates;
    s_view.textureUploads += s_rSnap.textureUploads;
    s_view.tempTargets += s_rSnap.tempTargets;
    s_view.tempReused += s_rSnap.tempReused;
    s_view.pipelines = s_rSnap.pipelines;
    ulk();
    return &s_view;
}

void rd__thread_note_replay(double walkMs, double submitMs, double waitMs)
{
    if (!s_on || !t_onR) {
        return;
    }
    t_waitNs += waitMs > 0.0 ? (uint64_t)(waitMs * 1e6) : 0u;
    lk();
    s_block.walkMs += walkMs;
    s_block.submitMs += submitMs;
    s_block.waitMs += waitMs;
    ulk();
}

void rd__thread_note_submit(double ms)
{
    if (!s_on) {
        return;
    }
    lk();
    s_block.submitMs += ms;
    ulk();
}

void rd__thread_note_interp(double ms)
{
    if (!s_on || !t_onR) {
        return;
    }
    t_interpNs += ms > 0.0 ? (uint64_t)(ms * 1e6) : 0u;
    lk();
    s_block.interpMs += ms;
    ulk();
}

/* ------------------------------------------- the renderer's own thread */

#define LOC_JOBS 8u

static struct {
    IcoThread *th;
    IcoMutex *m;
    IcoCond *c;
    Tramp *jobs[LOC_JOBS];
    void (*fn[LOC_JOBS])(void *arg);
    uint32_t head, count;
    uint64_t posted, done;
    bool quit;
    int busy;
    RdThreadMode mode;
} s_loc;

static void locMain(void *u)
{
    (void)u;
    ico_mutex_lock(s_loc.m);
    for (;;) {
        while (s_loc.count == 0 && !s_loc.quit) {
            ico_cond_wait(s_loc.c, s_loc.m);
        }
        if (s_loc.count == 0) {
            break;
        }
        void (*fn)(void *arg) = s_loc.fn[s_loc.head];
        void *arg = s_loc.jobs[s_loc.head];
        s_loc.head = (s_loc.head + 1u) % LOC_JOBS;
        s_loc.count--;
        ico_mutex_unlock(s_loc.m);
        fn(arg);
        ico_mutex_lock(s_loc.m);
        s_loc.done++;
        if (s_loc.count == 0) {
            __atomic_store_n(&s_loc.busy, 0, __ATOMIC_RELEASE);
        }
        ico_cond_broadcast(s_loc.c);
    }
    ico_mutex_unlock(s_loc.m);
}

static bool locPost(void *u, void (*fn)(void *arg), void *arg, bool picture, bool wait)
{
    (void)u;
    (void)picture;
    ico_mutex_lock(s_loc.m);
    if (s_loc.count == LOC_JOBS || s_loc.quit) {
        ico_mutex_unlock(s_loc.m);
        return false;
    }
    const uint32_t at = (s_loc.head + s_loc.count) % LOC_JOBS;
    s_loc.fn[at] = fn;
    s_loc.jobs[at] = arg;
    s_loc.count++;
    const uint64_t seq = ++s_loc.posted;
    __atomic_store_n(&s_loc.busy, 1, __ATOMIC_RELEASE);
    ico_cond_broadcast(s_loc.c);
    if (wait || s_loc.mode == RD_THREAD_SYNC) {
        while (s_loc.done < seq) {
            ico_cond_wait(s_loc.c, s_loc.m);
        }
    }
    ico_mutex_unlock(s_loc.m);
    return true;
}

static uint64_t locDrain(void *u)
{
    (void)u;
    const double t0 = ico_thread_now_ms();
    ico_mutex_lock(s_loc.m);
    while (s_loc.done < s_loc.posted) {
        ico_cond_wait(s_loc.c, s_loc.m);
    }
    ico_mutex_unlock(s_loc.m);
    return (uint64_t)((ico_thread_now_ms() - t0) * 1e6);
}

/* the RHI call guard: a call from any other thread waits for the jobs;
 * the submit thread's calls never wait, the render thread's wait for the
 * submit thread (rd_submit_thread.h) */
static bool locBusy(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (t_onR != 0) {
        return rd_thread_submit_busy();
    }
    return __atomic_load_n(&s_loc.busy, __ATOMIC_ACQUIRE) != 0 || rd_thread_submit_busy();
}

static bool locStale(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (t_onR != 0) {
        return rd_thread_submit_stale();
    }
    return locBusy(u);
}

static void locWait(void *u)
{
    if (t_onR == 0) {
        (void)locDrain(u);
    }
    (void)rd_thread_submit_drain();
}

bool rd_thread_mode_parse(const char *s, RdThreadMode *out)
{
    if (s == NULL) {
        return false;
    }
    if (strcmp(s, "off") == 0) {
        *out = RD_THREAD_OFF;
    } else if (strcmp(s, "sync") == 0) {
        *out = RD_THREAD_SYNC;
    } else if (strcmp(s, "on") == 0) {
        *out = RD_THREAD_ON;
    } else {
        return false;
    }
    return true;
}

bool rd_thread_local_start(RdThreadMode mode)
{
    static const RhiCallGuard guard = {NULL, locBusy, locWait, locStale};
    const RdThreadHost host = {NULL, locPost, locDrain, mode == RD_THREAD_SYNC, true, NULL};
    if (mode == RD_THREAD_OFF || s_loc.th != NULL) {
        return false;
    }
    if (g_rd.hasDevice && rhi_backend() == RHI_BACKEND_OPENGL) {
        rd__log("render thread: not with OpenGL here (its context is the calling thread's)");
        return false;
    }
    memset(&s_loc, 0, sizeof(s_loc));
    s_loc.mode = mode;
    s_loc.m = ico_mutex_create();
    s_loc.c = ico_cond_create();
    s_loc.th = s_loc.m && s_loc.c ? ico_thread_create(locMain, NULL) : NULL;
    if (s_loc.th == NULL) {
        if (s_loc.c) {
            ico_cond_destroy(s_loc.c);
        }
        if (s_loc.m) {
            ico_mutex_destroy(s_loc.m);
        }
        memset(&s_loc, 0, sizeof(s_loc));
        rd__log("render thread: the thread could not be made");
        return false;
    }
    rhi_set_call_guard(&guard);
    rd_set_thread_host(&host);
    rd__log("render thread: %s (the replays on a thread of their own%s)",
            mode == RD_THREAD_SYNC ? "sync" : "on",
            mode == RD_THREAD_SYNC ? ", each waited for" : "");
    return true;
}

void rd_thread_local_stop(void)
{
    if (s_loc.th == NULL) {
        return;
    }
    rd_set_thread_host(NULL); /* drains first */
    rhi_set_call_guard(NULL);
    ico_mutex_lock(s_loc.m);
    s_loc.quit = true;
    ico_cond_broadcast(s_loc.c);
    ico_mutex_unlock(s_loc.m);
    ico_thread_join(s_loc.th);
    ico_cond_destroy(s_loc.c);
    ico_mutex_destroy(s_loc.m);
    memset(&s_loc, 0, sizeof(s_loc));
}

static RdThreadMode s_want;

void rd_thread_local_want(RdThreadMode mode)
{
    s_want = mode;
}

void rd__thread_local_resume(void)
{
    if (s_want != RD_THREAD_OFF && !g_rd.windowed && g_rd.hasDevice && s_loc.th == NULL) {
        (void)rd_thread_local_start(s_want);
    }
}

void rd__thread_local_pause(void)
{
    rd_thread_local_stop();
}
