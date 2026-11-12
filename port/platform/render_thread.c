/*
 * port/platform/render_thread.c
 *
 * The window's render thread (render_thread.h): the submit and the present
 * of the pictures the game thread records, under frame_mailbox.h's rules.
 */
#include "render_thread.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "cpu_keep.h"
#include "cpu_place.h"
#include "diag_host.h"
#include "ico_thread.h" /* ico_thread_now_ms */
#include "pace_policy.h"
#include "rd.h"
#include "rd_submit_thread.h"
#include "rd_thread.h"
#include "rhi.h"
#include "sys_cell.h"
#include "thread_stat.h"
#include "window_lines.h" /* window_log_cost_mark */

#if defined(__linux__)
#include <unistd.h> /* getpid */
#endif

static struct {
    SDL_Thread *thread;
    SDL_Mutex *lock;
    SDL_Condition *cond; /* any change of the mailbox: both sides wait on it */
    SDL_ThreadID tid;    /* the render thread's */
    FrameMailbox mb;
    FmMode mode;
    /* a job handed over and not finished (the RHI guard's busy); written
       under the lock, read without it */
    int busy;
    /* the heartbeat without the lock (diag_host.c's watchdog thread): the
       picture in progress, since when (0: none), the last finished */
    unsigned long long beatSeq, beatDone;
    Uint64 beatSince;
    /* the game thread's waits for the render thread in the 10 s block */
    unsigned slotWaits;
    Uint64 slotWaitNs;
    /* the pictures handed over as the game thread plans further presents
       by them (ico_render_thread_plan): their reads (the hand-over) and
       their whole time, their typical times (pace_policy.h PaceRtTyp: a
       decaying mean and half its deviation); the render thread writes
       them, the game thread reads them, no lock */
    Uint64 readsEst, pictureEst;
    /* and the present call's wait of those pictures (its recent longest,
       pace_rt_estimate: PACE_RT_BEHIND's) */
    Uint64 presentEst;
    /* and their reads less the waits inside them (the fence, the submit
       thread, the acquire), their interpolation and the submit thread's
       job, typical times too: the game thread predicts a further picture's
       reads by them (pace_policy.h PaceRtPipe) */
    Uint64 cpuEst, interpEst, submitEst;
    /* the reads' mean and deviation, for the 10 s line */
    Uint64 readsMean, readsDev;
    /* the estimates' state, the render thread's alone: reads, whole job,
       own reads, interpolation, the submit thread's job */
    PaceRtTyp readsTyp, pictureTyp, cpuTyp, interpTyp, submitTyp;
    /* the further presents the game thread held back by them in the 10 s
       block, by why (pace_policy.h PaceRtHold; the game thread's) */
    unsigned held, heldMiss, heldOverrun, heldBehind;
    /* the step rule's asks in the block and their working (pace_policy.h
       PaceRtTraceSum; the game thread's) */
    PaceRtTraceSum asks;
    /* the game thread's steps in the block (ico_render_thread_note_step):
       those whose first renderer call waited for a picture's reads and the
       longest wait, those that missed their period and the latest, the
       steps planned at the whole period after a miss, the step's own time
       as last planned */
    unsigned lateStarts, misses, backoffSteps;
    unsigned pictureMisses; /* of the misses, the pictures' (the rest the game's own) */
    /* of the pictures': under the floor (nothing changed), the margin
       widened, the third counted in the window (the whole period) */
    unsigned smallMisses, widens, fallbacks;
    /* the misses judged in the block (pace_policy.h PaceStepEvent): counted
       toward the back-off, outliers (the longest outlier picture's GPU time
       and its typical); the back-offs ended by the pictures fitting, at
       the limit */
    unsigned counted, outliers, fitEnds, limitEnds;
    Uint64 outlierNs, outlierTypNs;
    Uint64 lateStartMaxNs, missMaxNs, stepPlanNs;
    /* the plan's parts as last noted: a step between frames, the margin's
       widening; the steps left out of the typical times in the block */
    Uint64 stepPlanBetweenNs, stepWidenNs;
    unsigned leftOut;
    /* the closing step's mean and deviation as last noted */
    Uint64 stepCloseMean, stepCloseDev;
} s_rt;

/* the cores (cpu_place.h): their speeds, read once at the first start, the
   slowest ones and the faster ones; whether the render and submit threads
   keep to the faster ones (Android, [dev] big_cores) and why not, and the
   cores each keeps to (cpu_place_split: the render thread alone on a
   single fastest core, the submit thread on the next ones; else both on
   the faster ones); the
   cores the game thread and the render thread ran on (one sample a step,
   one a picture: atomic bins, no lock), written before the threads start */
static struct {
    bool read;
    CpuClasses classes;
    uint64_t slowMask, fastMask, renderMask, submitMask;
    bool pin, split;
    CpuPlan plan;
    bool pinSubmit; /* the submit thread is kept too (not with two or fewer faster cores) */
    const char *noPin;
    CpuPlaceHist game, render;
    /* the threads' ids for their schedstat (thread_stat.h): the game
       thread's at place_init, the render and submit threads' at their
       start (atomic: the game thread reads them at its 10 s line); 0 not
       known */
    long gameTid, renderTid, submitTid;
} s_place;

/* the game thread's own pin when the rule keeps it (two faster cores: it
   and the render thread take them), the cores read back, one line; the
   start line says why a phone's threads are where they are */
static void place_game_pin(void)
{
    const CpuPlan *p = &s_place.plan;
    char fast[96], got[96], why[128], gotText[96];

    if (!s_place.pin || p->kind == CPU_PLAN_NONE) {
        return;
    }
    (void)cpu_place_mask_text(p->kind == CPU_PLAN_TWO_FAST ? p->game : p->render, fast,
                              sizeof(fast));
    if (p->kind == CPU_PLAN_MANY_FAST) {
        fprintf(stderr,
                "window: big cores: %d fast of %d: render and submit threads on them, "
                "the game thread free\n",
                p->fastCount, p->coreCount);
    } else if (p->kind == CPU_PLAN_ONE_FAST) {
        fprintf(stderr,
                "window: big cores: 1 fast of %d: the render thread alone on it (%s), "
                "the game and the submit thread free\n",
                p->coreCount, fast);
    } else {
        uint64_t read = 0;
        const CpuPinResult res = cpu_place_pin_checked(p->game, &read, why, sizeof(why));

        (void)cpu_place_mask_text(read, gotText, sizeof(gotText));
        snprintf(got, sizeof(got), "%s",
                 res == CPU_PIN_KEPT      ? "kept"
                 : res == CPU_PIN_UNKNOWN ? "kept (the cores could not be read back)"
                                          : "NOT kept");
        fprintf(stderr,
                "window: big cores: 2 fast of %d: game and render threads on them (%s), the "
                "submit thread free; the game thread %s%s%s\n",
                p->coreCount, fast, got,
                res == CPU_PIN_OTHER     ? ": the system keeps it to cores "
                : res == CPU_PIN_REFUSED ? ": "
                                         : "",
                res == CPU_PIN_OTHER ? gotText : (res == CPU_PIN_REFUSED ? why : ""));
    }
}

/* rhi_set_thread_begin's hook: a backend's thread (the pipeline workers,
   the present-wait thread) back on the process's cores */
static void place_rhi_thread_begin(void)
{
    (void)cpu_place_thread_free();
}

/* "window: threads: 31; kept to cores 6-7: ico_pc (the game thread),
   ico-render; on 0-7: ..." (cpu_place.h cpu_place_threads_format): once
   after the game thread's pin, then at a 10 s block when the text changed
   (a hash of it kept; the first 8 changes, then every 100th).  Linux and
   Android (nothing read elsewhere).  The game thread's */
/* The per-thread reads the 10 s block made on the game thread, moved to
 * the log thread's once-a-second tick (ico_render_thread_tick, Android;
 * window_stall.c asks_tick): the three threads' schedstat (one file each)
 * into cells every tick, and the thread list (opendir /proc/self/task and
 * every thread's comm and status: about 100 file reads, the owner's
 * Pixel's 2.5 ms "hold and game thread lines", rc10) every tenth tick into
 * a buffer.  The block reads the cells and the buffer; without the tick
 * (no log thread, or not Android) it reads the files itself, as before */
#define TICK_LIST_EVERY 10u
static int s_tickOn; /* __atomic: the tick has run: the block reads its cells */
static SysCell s_statCell[3];

static struct {
    SDL_SpinLock lock;
    uint32_t gen; /* under lock: a new list in text */
    char text[1024];
} s_list;

static long stat_tid(int i)
{
    return i == 0   ? s_place.gameTid
           : i == 1 ? __atomic_load_n(&s_place.renderTid, __ATOMIC_ACQUIRE)
                    : __atomic_load_n(&s_place.submitTid, __ATOMIC_ACQUIRE);
}

/* The keeper (cpu_keep.h): the render, game and submit threads' cores
 * after the start, on the log thread's tick (Android, the rule on, two or
 * more cores faster than the slowest).  The owner's Pixel answered yes to
 * the render thread on core 7 alone and read back 2-7 in 20 of 25 starts;
 * the keeper asks again when a 3D scene begins, on focus gained, back
 * from the background, when the system changes the threads' cores or the
 * cores it allows, and every 10 s while not on the ideal core.  Its lines
 * are made on the log thread (it must not write to the pipe it drains)
 * and written by the game thread: a try's line when its outcome changed
 * (ico_render_thread_note_game_core, a load and a compare a step), the
 * block's with the 10 s lines.  The requests from the other threads are
 * bits in s_keepWhy */
static CpuKeep s_keep; /* the log thread's */
static int s_keepOn;   /* __atomic: the keeper runs (set before the threads start) */
static unsigned s_keepWhy;
static uint32_t s_keepTaken; /* __atomic: the block lines the game thread took */

static struct {
    SDL_SpinLock lock;
    uint32_t tryGen; /* under lock (and read alone by the game thread's step) */
    char tryText[1024];
    char blockText[1024];
} s_keepOut;

static unsigned s_keepMoved, s_keepEnded; /* the log thread's: threads moved off the kept cores */

static void keep_request(unsigned why)
{
    if (__atomic_load_n(&s_keepOn, __ATOMIC_ACQUIRE)) {
        (void)__atomic_fetch_or(&s_keepWhy, why, __ATOMIC_RELAXED);
    }
}

static bool keep_pin(void *u, long tid, uint64_t mask, int *err)
{
    char why[128];
    const bool ok = cpu_place_pin_tid(tid, mask, why, sizeof(why));

    (void)u;
    *err = 0;
    if (!ok && sscanf(why, "errno %d", err) != 1) {
        *err = -1;
    }
    return ok;
}

static uint64_t keep_read(void *u, long tid)
{
    (void)u;
    return cpu_place_allowed_tid(tid);
}

/* the free mask for the threads made from now, and the registered ones
   moved to it (cpu_place_refree), the three picture threads left to the
   keeper */
static void keep_free(void *u, uint64_t mask)
{
    const long skip[3] = {s_place.gameTid, stat_tid(1), stat_tid(2)};
    unsigned ended = 0;

    (void)u;
    cpu_place_keep_free(mask);
    s_keepMoved += cpu_place_refree(mask, skip, 3, &ended);
    s_keepEnded += ended;
}

/* the log thread's part of the tick: the system's view of the render
   thread (the process's cores, its cpuset, its clamps), the keeper's tick,
   on a list tick the threads that inherited a kept core alone moved off
   it, the lines made */
static void keep_tick(const CpuThreads *list)
{
    static const CpuKeepOps ops = {NULL, keep_pin, keep_read, keep_free};
    static char s_lastPath[64];
    static uint32_t s_tryHash, s_taken;
    static bool s_said;
    const long rtid = stat_tid(1);
    char path[64], sys[256], text[1024], m[96];
    uint64_t proc = 0, cs = 0, allowed;
    unsigned why = __atomic_exchange_n(&s_keepWhy, 0u, __ATOMIC_RELAXED);
    unsigned umin = 0, umax = 0;
    int e = ENOENT;
    size_t n = 0;

#if defined(__linux__)
    proc = cpu_place_allowed_tid((long)getpid());
#endif
    path[0] = '\0';
    if (rtid > 0) {
        e = cpu_place_cpuset_read(NULL, NULL, rtid, path, sizeof(path), &cs);
    }
    allowed = proc != 0 && cs != 0 && (proc & cs) != 0 ? proc & cs : (proc != 0 ? proc : cs);
    if (e == 0 && s_lastPath[0] != '\0' && strcmp(path, s_lastPath) != 0) {
        why |= CPU_KEEP_WHY_ALLOWED; /* moved to another cpuset (foreground, top-app) */
    }
    if (e == 0) {
        snprintf(s_lastPath, sizeof(s_lastPath), "%s", path);
    }
    {
        const uint32_t taken = __atomic_load_n(&s_keepTaken, __ATOMIC_ACQUIRE);

        if (taken != s_taken) {
            s_taken = taken;
            cpu_keep_block_reset(&s_keep);
        }
    }
    const bool tried = cpu_keep_tick(&s_keep, &ops, ico_thread_now_ms(), why, allowed, rtid,
                                     stat_tid(2), s_place.gameTid);

    if (list != NULL && s_keep.have) {
        /* a thread the render or game thread made starts on its creator's
           cores (pthread_create(3)): a driver's thread made by the render
           thread kept to its core alone ("kept to core 7: ico render (2)",
           rc9, rc12) shares that core with it; moved to the others' */
        const CpuKeepSets *ks = &s_keep.sets;

        for (int i = 0; i < list->n; i++) {
            const CpuThread *t = &list->t[i];
            const bool alone =
                (ks->level == CPU_KEEP_ALONE && t->mask == ks->render) ||
                (ks->game != 0 && (ks->game & (ks->game - 1)) == 0 && t->mask == ks->game);
            char w[96];

            if (alone && t->tid != rtid && t->tid != stat_tid(2) && t->tid != s_place.gameTid &&
                cpu_place_pin_tid(t->tid, ks->others, w, sizeof(w))) {
                s_keepMoved++;
            }
        }
    }
    /* the system's view of the render thread */
    if (e == 0) {
        (void)cpu_place_mask_text(cs, m, sizeof(m));
        n = (size_t)snprintf(sys, sizeof(sys), "cpuset %s (cores %s)", path,
                             cs != 0 ? m : "not read");
    } else {
        n = (size_t)snprintf(sys, sizeof(sys), "cpuset not readable (errno %d)", e);
    }
    if (rtid > 0 && cpu_place_uclamp_read(rtid, &umin, &umax) && n < sizeof(sys)) {
        n += (size_t)snprintf(sys + n, sizeof(sys) - n, ", clamps %u-%u", umin, umax);
    }
    if (tried) {
        uint32_t h = 2166136261u; /* FNV-1a: the same outcome is not written again */

        (void)cpu_keep_try_text(&s_keep, sys, text, sizeof(text));
        for (const char *c = text; *c != '\0'; c++) {
            h = (h ^ (uint8_t)*c) * 16777619u;
        }
        if (!s_said || h != s_tryHash) {
            s_said = true;
            s_tryHash = h;
            SDL_LockSpinlock(&s_keepOut.lock);
            memcpy(s_keepOut.tryText, text, sizeof(text));
            __atomic_store_n(&s_keepOut.tryGen, s_keepOut.tryGen + 1u, __ATOMIC_RELEASE);
            SDL_UnlockSpinlock(&s_keepOut.lock);
        }
    }
    n = cpu_keep_block_text(&s_keep, sys, text, sizeof(text));
    if (n < sizeof(text)) {
        snprintf(text + n, sizeof(text) - n,
                 "; threads moved off the kept cores %u since the start (%u had ended)",
                 s_keepMoved, s_keepEnded);
    }
    SDL_LockSpinlock(&s_keepOut.lock);
    memcpy(s_keepOut.blockText, text, sizeof(text));
    SDL_UnlockSpinlock(&s_keepOut.lock);
}

void ico_render_thread_place_scene(bool play)
{
    static bool s_play;

    if (play && !s_play) {
        keep_request(CPU_KEEP_WHY_SCENE);
    }
    s_play = play;
}

void ico_render_thread_place_focus(void)
{
    keep_request(CPU_KEEP_WHY_FOCUS);
}

void ico_render_thread_tick(void)
{
    static uint32_t s_ticks;
    const CpuThreads *list = NULL;
    for (int i = 0; i < 3; i++) {
        ThreadStat now;
        const long tid = stat_tid(i);
        if (tid > 0 && thread_stat_read(NULL, tid, &now) == 0) {
            const double v[4] = {(double)now.runNs, (double)now.waitNs, (double)now.slices,
                                 (double)tid};
            sys_cell_put(&s_statCell[i], ico_thread_now_ms(), 0.0, v, 4);
        }
    }
    if (s_ticks++ % TICK_LIST_EVERY == 0u) {
        static char text[1024]; /* the log thread's alone */
        static CpuThreads s_threads;
        (void)cpu_place_threads_read(NULL, &s_threads);
        list = &s_threads;
        if (cpu_place_threads_format(&s_threads, s_place.gameTid, text, sizeof(text)) > 0) {
            SDL_LockSpinlock(&s_list.lock);
            memcpy(s_list.text, text, sizeof(text));
            s_list.gen++;
            SDL_UnlockSpinlock(&s_list.lock);
        }
    }
    if (__atomic_load_n(&s_keepOn, __ATOMIC_ACQUIRE)) {
        keep_tick(list);
    }
    __atomic_store_n(&s_tickOn, 1, __ATOMIC_RELEASE);
}

static void place_threads_line(void)
{
    static uint32_t s_hash;
    static bool s_said;
    static uint32_t s_gen;
    char text[1024];
    uint32_t h = 2166136261u; /* FNV-1a */

    if (__atomic_load_n(&s_tickOn, __ATOMIC_ACQUIRE)) {
        /* the log thread's last list (up to TICK_LIST_EVERY s old) */
        SDL_LockSpinlock(&s_list.lock);
        const uint32_t gen = s_list.gen;
        if (gen != s_gen) {
            memcpy(text, s_list.text, sizeof(text));
        }
        SDL_UnlockSpinlock(&s_list.lock);
        if (gen == s_gen) {
            return; /* nothing new read */
        }
        s_gen = gen;
        text[sizeof(text) - 1] = '\0';
        if (text[0] == '\0' || strcmp(text, "threads: none read") == 0) {
            return;
        }
    } else if (cpu_place_threads_text(NULL, s_place.gameTid, text, sizeof(text)) == 0 ||
               strcmp(text, "threads: none read") == 0) {
        return;
    }
    for (const char *c = text; *c != '\0'; c++) {
        h = (h ^ (uint8_t)*c) * 16777619u;
    }
    if (s_said && h == s_hash) {
        return;
    }
    s_said = true;
    s_hash = h;
    ico_log_limited("window: threads", "window: %s", text);
}

/* The 10 s game-thread line's run-queue part (thread_stat.h): the game,
   render and submit threads' time on a core and waiting for one since the
   last block, "ran / waited for a core: game 3.9 s / 0.4 s, render 2.1 s /
   1.8 s, submit 0.0 s / 0.0 s"; a thread first read in this block (the
   render thread made again) is left out until the next.  Once, when no
   thread's could be read and the game thread's read failed: "ran / waited
   for a core: schedstat not readable (EACCES)".  "" off Linux (no thread
   ids).  The game thread's */
static ThreadStatTrack s_statTrack[3];

static void place_stat_prime(void)
{
    ThreadStat now, d;

    if (s_place.gameTid > 0 && thread_stat_read(NULL, s_place.gameTid, &now) == 0) {
        (void)thread_stat_track(&s_statTrack[0], s_place.gameTid, &now, &d);
    }
}

static size_t place_stat_part(char *out, size_t cap, bool submitOn)
{
    static const char *const who[3] = {"game", "render", "submit"};
    static bool s_failSaid;
    const long tids[3] = {s_place.gameTid, __atomic_load_n(&s_place.renderTid, __ATOMIC_ACQUIRE),
                          submitOn ? __atomic_load_n(&s_place.submitTid, __ATOMIC_ACQUIRE) : 0};
    ThreadStat d[3];
    bool have[3] = {false, false, false};
    int gameErr = 0;

    if (cap > 0) {
        out[0] = '\0';
    }
    const bool cells = __atomic_load_n(&s_tickOn, __ATOMIC_ACQUIRE);
    double oldest = -1.0;

    for (int i = 0; i < 3; i++) {
        ThreadStat now;
        int e;

        memset(&d[i], 0, sizeof(d[i]));
        if (tids[i] <= 0) {
            continue;
        }
        if (cells) {
            /* the log thread's last read of this thread (a cell of another
               thread id is an old one: not this thread's) */
            double v[4], at;
            e = sys_cell_get(&s_statCell[i], v, 4, &at, NULL) == 4 && (long)v[3] == tids[i]
                    ? 0
                    : ENOENT;
            if (e == 0) {
                now.runNs = (uint64_t)v[0];
                now.waitNs = (uint64_t)v[1];
                now.slices = (uint64_t)v[2];
                const double age = ico_thread_now_ms() - at;
                oldest = age > oldest ? age : oldest;
            } else {
                e = thread_stat_read(NULL, tids[i], &now); /* none read there: its own errno */
            }
        } else {
            e = thread_stat_read(NULL, tids[i], &now);
        }
        if (e != 0) {
            gameErr = i == 0 ? e : gameErr;
            s_statTrack[i].tid = 0; /* read again from nothing */
            continue;
        }
        have[i] = thread_stat_track(&s_statTrack[i], tids[i], &now, &d[i]);
    }
    if (have[0] || have[1] || have[2]) {
        size_t n = thread_stat_text(out, cap, who, d, have, 3);
        if (cells && oldest >= 0.0 && n < cap) {
            /* the deltas run between the log thread's reads, not the
               block's ends: up to a second more or less than the block */
            const int w = snprintf(out + n, cap - n,
                                   " (the threads' run times as the log thread read them up to "
                                   "%.0f ms before)",
                                   oldest);
            n += w > 0 ? (size_t)w : 0u;
            n = n < cap ? n : cap - 1;
        }
        return n;
    }
    if (gameErr != 0 && !s_failSaid) {
        s_failSaid = true;
        return thread_stat_fail_text(out, cap, gameErr);
    }
    return 0;
}

/* the cores' speeds and the placement rule, once (the game thread, before
   the render thread starts) */
static void place_init(void)
{
    char cls[384], allowed[96], pr[64], rm[96], sm[96], split[256];

    if (s_place.read) {
        return;
    }
    s_place.read = true;
    s_place.gameTid = thread_stat_tid();
    place_stat_prime();
    (void)cpu_place_classes_read(NULL, &s_place.classes);
    s_place.slowMask = cpu_place_slow_mask(&s_place.classes);
    s_place.fastMask = cpu_place_fast_mask(&s_place.classes);
    cpu_place_plan(&s_place.classes, &s_place.plan);
    s_place.split = s_place.plan.split;
    s_place.renderMask = s_place.plan.render;
    s_place.submitMask = s_place.plan.submit;
#if defined(__ANDROID__)
    if (!ico_config_get_bool("dev.big_cores", 1)) {
        s_place.noPin = "[dev] big_cores = false";
    } else if (s_place.fastMask == 0) {
        s_place.noPin = "the cores are of one speed, or their speeds are not known";
    } else {
        s_place.pin = true;
        s_place.pinSubmit = s_place.plan.submit != 0;
        if (cpu_keep_applies(&s_place.classes)) {
            /* the render, game and submit threads kept after the start
               (cpu_keep.h), from the log thread's first tick */
            cpu_keep_init(&s_keep, &s_place.classes);
            __atomic_store_n(&s_keepOn, 1, __ATOMIC_RELEASE);
        }
    }
#else
    s_place.noPin = "only on Android";
#endif
    (void)cpu_place_classes_text(&s_place.classes, cls, sizeof(cls));
    (void)cpu_place_mask_text(cpu_place_allowed(), allowed, sizeof(allowed));
    (void)cpu_place_priority_text(pr, sizeof(pr));
    (void)cpu_place_mask_text(s_place.renderMask, rm, sizeof(rm));
    (void)cpu_place_mask_text(s_place.submitMask, sm, sizeof(sm));
    split[0] = '\0';
    if (s_place.pin && s_place.plan.kind != CPU_PLAN_MANY_FAST) {
        snprintf(split, sizeof(split),
                 s_place.plan.kind == CPU_PLAN_TWO_FAST
                     ? "; the game and the render thread to the two faster cores %s, the submit "
                       "thread left to the system"
                     : "; the render thread to the one faster core %s alone, the game and the "
                       "submit thread left to the system",
                 rm);
    } else if (s_place.pin) {
        snprintf(split, sizeof(split),
                 s_place.split ? "; the render thread to the fastest core %s alone, the submit "
                                 "thread to the next faster cores %s, the game thread left to "
                                 "the system"
                               : "; the render and the submit thread to the faster cores %s%s, "
                                 "the game thread left to the system",
                 rm, s_place.split ? sm : "");
    }
    /* the game thread's own priority is not changed: said as it is */
    fprintf(stderr, "window: cores: %s; the game thread %s%s%s, may run on cores %s%s\n", cls,
            pr[0] ? "at " : "at its priority as made", pr, pr[0] ? " (not changed)" : "",
            cpu_place_allowed() != 0 ? allowed : "not known", split);
    if (s_place.pin && cpu_place_allowed() != 0) {
        /* the process's cores, kept before any pin: a thread made by a
           kept thread starts with its creator's cores (pthread_create(3)),
           so every thread the rule leaves free (the submit thread under
           the two-core rule, the texture decode pool, the pipeline
           workers, the pack loader, the save-state writers) puts itself
           back on these at its entry (cpu_place.h cpu_place_thread_free;
           ico_thread_create's threads in ico_thread.c, the RHI's through
           rhi_thread_begin) */
        cpu_place_keep_free(cpu_place_allowed());
        rhi_set_thread_begin(place_rhi_thread_begin);
        fprintf(stderr,
                "window: big cores: kept to the faster cores: %s; every other thread the "
                "game makes runs on every core it may (%s)\n",
                s_place.plan.kind == CPU_PLAN_TWO_FAST   ? "the game and the render thread"
                : s_place.plan.kind == CPU_PLAN_ONE_FAST ? "the render thread"
                : s_place.pinSubmit                      ? "the render and the submit thread"
                                                         : "the render thread",
                allowed);
    }
    place_game_pin();
    place_threads_line(); /* the threads so far and their cores, after the pin */
}

/* a thread the pictures wait on (the render thread, the submit thread), at
   its start: the high priority asked and what the system made of it, and
   with the rule on the faster cores; one line */
static void place_thread_begin(const char *who, bool render)
{
    const bool granted = SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    const uint64_t mask = render ? s_place.renderMask : s_place.submitMask;
    /* which cores (cpu_place_split): the fastest alone, the next ones, or
       the faster ones */
    const bool mine = render || s_place.pinSubmit;
    const char *what = s_place.plan.kind == CPU_PLAN_TWO_FAST   ? "the two faster cores"
                       : s_place.plan.kind == CPU_PLAN_ONE_FAST ? "the one faster core"
                       : !s_place.split                         ? "the faster cores"
                       : render                                 ? "the fastest core"
                                                                : "the next faster cores";
    char err[160], pr[64], fast[96], cores[160], allowed[96], why[128], place[768];

    __atomic_store_n(render ? &s_place.renderTid : &s_place.submitTid, thread_stat_tid(),
                     __ATOMIC_RELEASE);
    snprintf(err, sizeof(err), "%s", granted ? "" : SDL_GetError());
    (void)cpu_place_priority_text(pr, sizeof(pr));
    (void)cpu_place_mask_text(mask, fast, sizeof(fast));
    snprintf(cores, sizeof(cores), "%s %s%s", what, fast,
             (s_place.split || s_place.plan.kind == CPU_PLAN_ONE_FAST) && render ? " alone" : "");
    if (s_place.pin && mine) {
        /* the cores read back after the call, not the call's answer alone:
           a phone's kernel may say yes and keep the thread elsewhere
           (cpu_place.h CpuPinResult) */
        uint64_t got = 0;
        const CpuPinResult res = cpu_place_pin_checked(mask, &got, why, sizeof(why));
        char gotText[96], fb[384];

        (void)cpu_place_mask_text(got, gotText, sizeof(gotText));
        fb[0] = '\0';
        if (res != CPU_PIN_KEPT && res != CPU_PIN_UNKNOWN && s_place.split && render &&
            s_place.fastMask != mask) {
            /* the fastest core alone refused: the faster cores, as with
               no split */
            uint64_t got2 = 0;
            char why2[128], fast2[96], got2Text[96];
            const CpuPinResult res2 =
                cpu_place_pin_checked(s_place.fastMask, &got2, why2, sizeof(why2));

            (void)cpu_place_mask_text(s_place.fastMask, fast2, sizeof(fast2));
            (void)cpu_place_mask_text(got2, got2Text, sizeof(got2Text));
            snprintf(fb, sizeof(fb), "; kept to the faster cores %s instead%s%s%s", fast2,
                     res2 == CPU_PIN_KEPT    ? ""
                     : res2 == CPU_PIN_OTHER ? " (the system keeps it to cores "
                                             : " (",
                     res2 == CPU_PIN_KEPT    ? ""
                     : res2 == CPU_PIN_OTHER ? got2Text
                                             : (res2 == CPU_PIN_REFUSED ? why2 : "not read back"),
                     res2 == CPU_PIN_KEPT ? "" : ")");
        }
        if (res == CPU_PIN_KEPT || res == CPU_PIN_UNKNOWN) {
            snprintf(place, sizeof(place),
                     "kept to %s%s ([dev] big_cores = false leaves the placement to the system)",
                     cores, res == CPU_PIN_UNKNOWN ? " (the cores could not be read back)" : "");
        } else if (res == CPU_PIN_OTHER) {
            snprintf(place, sizeof(place),
                     "could not be kept to %s: the system said yes and keeps it to cores %s%s",
                     cores, gotText, fb);
        } else {
            snprintf(place, sizeof(place), "could not be kept to %s (%s)%s", cores, why, fb);
        }
    } else if (s_place.noPin == NULL && !mine) {
        /* left free: on the process's cores, not the game thread's two
           (ico_thread.c has already put it back at its entry; said here) */
        const CpuFreeResult fr = cpu_place_thread_free();
        char keep[96];

        (void)cpu_place_mask_text(cpu_place_free_mask(), keep, sizeof(keep));
        snprintf(place, sizeof(place), "free (%s): %s cores %s",
                 s_place.plan.kind == CPU_PLAN_TWO_FAST
                     ? "the game and render threads have the two faster cores"
                     : "the render thread has the one faster core",
                 fr == CPU_FREE_FAILED ? "could not be put back on the process's"
                                       : "on the process's",
                 keep);
    } else {
        snprintf(place, sizeof(place), "placed by the system (%s)",
                 s_place.noPin != NULL ? s_place.noPin : "no rule");
    }
    (void)cpu_place_mask_text(cpu_place_allowed(), allowed, sizeof(allowed));
    fprintf(stderr, "window: %s thread: the high priority %s%s%s%s%s; %s; may run on cores %s\n",
            who, granted ? "granted" : "refused (", err, granted ? "" : ")", pr[0] ? ", " : "", pr,
            place, cpu_place_allowed() != 0 ? allowed : "not known");
}

/* OpenGL's context is current on one thread at a time: the render thread's
   for its jobs; a game-thread RHI call takes it over (the call guard's
   wait, gl_take) and the next job takes it back.  owner: GL_NONE, GL_GAME
   (any thread but the render thread) or GL_RENDER */
enum { GL_NONE = 0, GL_GAME, GL_RENDER };

static struct {
    bool on;
    SDL_Window *win;
    SDL_GLContext ctx;
    int owner;
    unsigned takes, fails;
} s_gl;

static int gl_owner(void)
{
    return __atomic_load_n(&s_gl.owner, __ATOMIC_ACQUIRE);
}

static void gl_set_owner(int owner)
{
    __atomic_store_n(&s_gl.owner, owner, __ATOMIC_RELEASE);
}

/* on the thread that is to make the calls: the context current there */
static void gl_bind(int owner)
{
    if (!SDL_GL_MakeCurrent(s_gl.win, s_gl.ctx)) {
        if (s_gl.fails++ < 8) {
            fprintf(stderr,
                    "window: render thread: the OpenGL context could not be made current "
                    "on the %s thread (%s)\n",
                    owner == GL_RENDER ? "render" : "game", SDL_GetError());
        }
    }
    gl_set_owner(owner);
}

/* the thread that has it lets it go */
static void gl_unbind(void)
{
    (void)SDL_GL_MakeCurrent(s_gl.win, NULL);
    gl_set_owner(GL_NONE);
}

static void gl_release_job(void *u)
{
    (void)u;
    if (gl_owner() == GL_RENDER) {
        gl_unbind();
    }
}

static int SDLCALL rt_main(void *u)
{
    (void)u;
    /* a phone's scheduler may still move it to a little core: the 10 s
       line's cores say where it ran */
    place_thread_begin("render", true);
    SDL_LockMutex(s_rt.lock);
    for (;;) {
        FmJob job;
        const FmNext next = fm_next(&s_rt.mb, &job, SDL_GetTicksNS());

        if (next == FM_NEXT_QUIT) {
            break;
        }
        if (next != FM_NEXT_JOB) {
            if (next == FM_NEXT_PARKED && s_gl.on && gl_owner() == GL_RENDER) {
                /* the surface is made again on the game thread meanwhile */
                gl_unbind();
            }
            /* idle or parked: said to a game thread waiting for either */
            SDL_BroadcastCondition(s_rt.cond);
            SDL_WaitCondition(s_rt.cond, s_rt.lock);
            continue;
        }
        const Uint64 t0 = SDL_GetTicksNS();

        /* the watchdog's heartbeat counts pictures only: a call between
           pictures (the targets made again, a movie's picture) runs on this
           thread as it ran on the game thread, under the watchdog's own
           limit */
        if (job.kind == FM_JOB_PICTURE) {
            __atomic_store_n(&s_rt.beatSeq, (unsigned long long)job.seq, __ATOMIC_RELAXED);
            __atomic_store_n(&s_rt.beatSince, t0 != 0 ? t0 : 1, __ATOMIC_RELEASE);
        }
        SDL_UnlockMutex(s_rt.lock);
        if (s_gl.on && gl_owner() != GL_RENDER) {
            gl_bind(GL_RENDER); /* the game thread let it go when it posted */
        }
        job.fn(job.arg);
        const Uint64 t1 = SDL_GetTicksNS();
        FmPresent p;

        if (job.kind == FM_JOB_PICTURE) {
            cpu_place_note(&s_place.render, cpu_place_now()); /* the 10 s line's cores */
        }

        if (job.kind == FM_JOB_PICTURE) {
            /* a picture handed over (rd_thread.h): how long it held the
               renderer's state and how long it took, for the game thread's
               next further presents */
            const uint64_t reads = rd_thread_take_reads_ns();
            const uint64_t pres = rd_thread_take_present_ns();
            const uint64_t sub = rd_thread_submit_take_job_ns();
            uint64_t waits = 0, interp = 0;

            rd_thread_take_reads_parts(&waits, &interp);
            if (sub != 0) {
                /* the submit thread's job finished since (the picture
                   before's, one picture late) */
                pace_rt_typ_note(&s_rt.submitTyp, sub);
                __atomic_store_n(&s_rt.submitEst, pace_rt_typ_ns(&s_rt.submitTyp),
                                 __ATOMIC_RELAXED);
            }

            if (pres != 0) {
                /* how long its present waited: the display or the GPU
                   behind (pace_rt_further_hold) */
                const Uint64 e = __atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED);

                __atomic_store_n(&s_rt.presentEst, pace_rt_estimate(e, pres), __ATOMIC_RELAXED);
            }
            if (reads != 0) {
                /* the typical times, not the longest (pace_policy.h
                   PaceRtTyp): the reads of its own less the waits (at
                   least 1 ns, 0 is "not known") */
                const uint64_t own = reads > waits ? reads - waits : 1u;

                pace_rt_typ_note(&s_rt.readsTyp, reads);
                pace_rt_typ_note(&s_rt.pictureTyp, t1 - t0);
                pace_rt_typ_note(&s_rt.cpuTyp, own);
                pace_rt_typ_note(&s_rt.interpTyp, interp < own ? interp : own);
                __atomic_store_n(&s_rt.readsEst, pace_rt_typ_ns(&s_rt.readsTyp), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.pictureEst, pace_rt_typ_ns(&s_rt.pictureTyp),
                                 __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.cpuEst, pace_rt_typ_ns(&s_rt.cpuTyp), __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.interpEst, pace_rt_typ_ns(&s_rt.interpTyp),
                                 __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.readsMean, s_rt.readsTyp.meanNs, __ATOMIC_RELAXED);
                __atomic_store_n(&s_rt.readsDev, pace_rt_typ_dev_ns(&s_rt.readsTyp),
                                 __ATOMIC_RELAXED);
            }
        }

        memset(&p, 0, sizeof(p));
        if (job.kind == FM_JOB_PICTURE) {
            p.t0 = t0;
            p.t1 = t1;
            p.costNs = t1 - t0;
            p.shown = rhi_present_shown();
        }
        SDL_LockMutex(s_rt.lock);
        if (job.kind == FM_JOB_PICTURE) {
            fm_present_note(&s_rt.mb, &p);
        }
        fm_finish(&s_rt.mb, &job, t1);
        __atomic_store_n(&s_rt.beatSince, (Uint64)0, __ATOMIC_RELAXED);
        __atomic_store_n(&s_rt.beatDone, (unsigned long long)job.seq, __ATOMIC_RELEASE);
        if (fm_idle(&s_rt.mb)) {
            /* what the job wrote is the game thread's once it reads this */
            __atomic_store_n(&s_rt.busy, 0, __ATOMIC_RELEASE);
        }
        SDL_BroadcastCondition(s_rt.cond);
    }
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    return 0;
}

/* the render thread's part of the guard: a job runs, or with OpenGL the
   context is not the game thread's */
static bool rt_render_busy(void)
{
    return __atomic_load_n(&s_rt.busy, __ATOMIC_ACQUIRE) != 0 || (s_gl.on && gl_owner() != GL_GAME);
}

/* the RHI guard (rhi.h rhi_set_call_guard): busy on any thread but this one
   while a job runs, and with OpenGL while the context is not that thread's;
   the submit thread's (rd_submit_thread.h) calls never wait, this thread's
   wait for the submit thread's work, every other thread's for both */
static bool rt_busy(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        return rd_thread_submit_busy();
    }
    return rt_render_busy() || rd_thread_submit_busy();
}

/* rhi_get_stats's question: the counters written by another thread now */
static bool rt_stale(void *u)
{
    (void)u;
    if (rd_thread_on_submit()) {
        return false;
    }
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        return rd_thread_submit_stale();
    }
    return rt_busy(u);
}

/* the render thread's jobs finished (the submit thread's may go on) */
static uint64_t rt_drain_render(void)
{
    if (s_rt.thread == NULL || !rt_render_busy()) {
        return 0;
    }
    const Uint64 t0 = SDL_GetTicksNS();

    SDL_LockMutex(s_rt.lock);
    while (!fm_idle(&s_rt.mb)) {
        SDL_WaitCondition(s_rt.cond, s_rt.lock);
    }
    SDL_UnlockMutex(s_rt.lock);
    const Uint64 waited = SDL_GetTicksNS() - t0;

    __atomic_fetch_add(&s_rt.slotWaits, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_rt.slotWaitNs, waited, __ATOMIC_RELAXED);
    return waited;
}

uint64_t ico_render_thread_drain(void)
{
    if (s_rt.thread == NULL) {
        return 0;
    }
    /* the render thread idle first: only it posts to the submit thread */
    const uint64_t waited = rt_drain_render();

    return waited + rd_thread_submit_drain();
}

/* OpenGL: the context to the calling thread (the render thread idle) */
static void gl_take(void)
{
    if (gl_owner() == GL_RENDER) {
        (void)ico_render_thread_post(gl_release_job, NULL, FM_JOB_CALL, true);
    }
    if (gl_owner() == GL_RENDER) {
        return; /* not taken (parked meanwhile: it let the context go) */
    }
    gl_bind(GL_GAME);
    s_gl.takes++;
    if (s_gl.takes <= 8 || s_gl.takes % 100 == 0) {
        fprintf(stderr,
                "window: render thread: the game thread took the OpenGL context for its own "
                "calls (%u times; the render thread's next picture takes it back)\n",
                s_gl.takes);
    }
}

static void rt_wait(void *u)
{
    (void)u;
    if (SDL_GetCurrentThreadID() == s_rt.tid) {
        (void)rd_thread_submit_drain(); /* its list kept for the submit thread first */
        return;
    }
    (void)ico_render_thread_drain();
    if (s_gl.on && gl_owner() != GL_GAME) {
        gl_take();
    }
}

bool ico_render_thread_post(void (*fn)(void *arg), void *arg, FmJobKind kind, bool wait)
{
    if (s_rt.thread == NULL || fn == NULL || SDL_GetCurrentThreadID() == s_rt.tid) {
        return false;
    }
    if (s_gl.on && gl_owner() == GL_GAME) {
        gl_unbind(); /* the render thread makes it current for the job */
    }
    SDL_LockMutex(s_rt.lock);
    if (fm_idle(&s_rt.mb) && !rd_thread_submit_stale()) {
        /* the counters as the hand-over leaves them (rhi_get_stats while
           the job runs); this thread alone is in the RHI now (with the
           submit thread at work, the last ones kept) */
        rhi_call_guard_snapshot();
    }
    const uint64_t seq = fm_post(&s_rt.mb, fn, arg, kind);

    if (seq == 0) {
        SDL_UnlockMutex(s_rt.lock);
        return false;
    }
    __atomic_store_n(&s_rt.busy, 1, __ATOMIC_RELEASE);
    SDL_BroadcastCondition(s_rt.cond);
    if (wait) {
        while (!fm_done_up_to(&s_rt.mb, seq)) {
            SDL_WaitCondition(s_rt.cond, s_rt.lock);
        }
    }
    SDL_UnlockMutex(s_rt.lock);
    return true;
}

/* rd_thread.h RdThreadHost: the replays, the presents and the renderer's
   calls */
static bool rt_host_post(void *u, void (*fn)(void *arg), void *arg, bool picture, bool wait)
{
    (void)u;
    return ico_render_thread_post(fn, arg, picture ? FM_JOB_PICTURE : FM_JOB_CALL,
                                  wait || s_rt.mode == FM_MODE_SYNC);
}

static uint64_t rt_host_drain(void *u)
{
    (void)u;
    return rt_drain_render(); /* rd_thread_drain waits for the submit thread after it */
}

/* the submit thread's first call (rd_submit_thread.h): this thread's priority */
static void rt_submit_begin(void *u)
{
    (void)u;
    place_thread_begin("submit", false); /* the render thread's next picture waits on it */
}

/* diag_host.h ico_diag_set_render_heartbeat: no lock (the watchdog's
   thread); with no picture in progress here, the submit thread's job still
   to finish is the last picture's (its submit and present) */
static int rt_heartbeat(unsigned long long *seq, unsigned long long *done)
{
    const Uint64 since = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);

    *seq = since != 0 ? __atomic_load_n(&s_rt.beatSeq, __ATOMIC_RELAXED) : 0;
    *done = __atomic_load_n(&s_rt.beatDone, __ATOMIC_ACQUIRE);
    if (since == 0 && *done != 0 && rd_thread_submit_unfinished()) {
        *seq = *done;
        *done = *seq - 1;
    }
    return 1;
}

FmMode ico_render_thread_start(void)
{
    static const RhiCallGuard guard = {NULL, rt_busy, rt_wait, rt_stale};
    RdThreadHost host = {NULL, rt_host_post, rt_host_drain, false, true, rt_submit_begin};
    const RhiLimits *l = rhi_limits();
    const char *why = "";
    FmMode mode;

    if (s_rt.thread != NULL) {
        return s_rt.mode;
    }
    place_init(); /* before either thread starts: they read the rule */
    mode = fm_mode_choose(ico_config_get_string("dev.render_thread", "auto"),
                          l == NULL || l->singleThreadCommands, true, &why);
    if (mode != FM_MODE_OFF) {
        s_rt.lock = SDL_CreateMutex();
        s_rt.cond = SDL_CreateCondition();
        fm_init(&s_rt.mb, SDL_GetTicksNS());
        s_rt.busy = 0;
        s_rt.beatSeq = s_rt.beatDone = 0;
        s_rt.beatSince = 0;
        s_rt.slotWaits = 0;
        s_rt.slotWaitNs = 0;
        s_rt.readsEst = s_rt.pictureEst = s_rt.presentEst = 0;
        s_rt.cpuEst = s_rt.interpEst = s_rt.submitEst = 0;
        s_rt.readsMean = s_rt.readsDev = 0;
        memset(&s_rt.readsTyp, 0, sizeof(s_rt.readsTyp));
        memset(&s_rt.pictureTyp, 0, sizeof(s_rt.pictureTyp));
        memset(&s_rt.cpuTyp, 0, sizeof(s_rt.cpuTyp));
        memset(&s_rt.interpTyp, 0, sizeof(s_rt.interpTyp));
        memset(&s_rt.submitTyp, 0, sizeof(s_rt.submitTyp));
        s_rt.stepCloseMean = s_rt.stepCloseDev = 0;
        s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
        s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = s_rt.pictureMisses = 0;
        s_rt.smallMisses = s_rt.widens = s_rt.fallbacks = 0;
        s_rt.lateStartMaxNs = s_rt.missMaxNs = s_rt.stepPlanNs = 0;
        s_rt.stepPlanBetweenNs = s_rt.stepWidenNs = 0;
        s_rt.leftOut = 0;
        memset(&s_gl, 0, sizeof(s_gl));
        if (rhi_backend() == RHI_BACKEND_OPENGL) {
            /* made current here at rhi_init: the render thread's from now */
            s_gl.win = SDL_GL_GetCurrentWindow();
            s_gl.ctx = SDL_GL_GetCurrentContext();
            s_gl.on = s_gl.win != NULL && s_gl.ctx != NULL;
            s_gl.owner = GL_GAME;
        }
        if (s_rt.lock != NULL && s_rt.cond != NULL) {
            /* the walk's own stack: the main thread's size on every platform */
            const SDL_PropertiesID props = SDL_CreateProperties();

            SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                                   (void *)rt_main);
            SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "ico render");
            SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 8 * 1024 * 1024);
            s_rt.thread = props != 0 ? SDL_CreateThreadWithProperties(props) : NULL;
            if (props != 0) {
                SDL_DestroyProperties(props);
            }
        } else {
            s_rt.thread = NULL;
        }
        if (s_rt.thread == NULL) {
            fprintf(stderr, "window: render thread: off (the thread could not be made: %s)\n",
                    SDL_GetError());
            if (s_rt.cond != NULL) {
                SDL_DestroyCondition(s_rt.cond);
            }
            if (s_rt.lock != NULL) {
                SDL_DestroyMutex(s_rt.lock);
            }
            s_rt.cond = NULL;
            s_rt.lock = NULL;
            s_rt.mode = FM_MODE_OFF;
            return FM_MODE_OFF;
        }
        s_rt.tid = SDL_GetThreadID(s_rt.thread);
        s_rt.mode = mode;
        rhi_set_call_guard(&guard);
        host.sync = mode == FM_MODE_SYNC; /* every picture waited: none counted as handed over */
        rd_set_thread_host(&host);
        ico_diag_set_render_heartbeat(rt_heartbeat);
    }
    s_rt.mode = mode;
    if (mode == FM_MODE_ON) {
        fprintf(stderr,
                "window: render thread: on (interpolation, walk, submit and present off the game "
                "thread; [dev] render_thread = off keeps them on it; %s, %s)\n",
                rhi_backend_kind_name(rhi_backend()), why);
    } else if (mode == FM_MODE_SYNC) {
        fprintf(stderr,
                "window: render thread: sync (interpolation, walk, submit and present on the "
                "render thread, %s)\n",
                why);
    } else {
        fprintf(stderr, "window: render thread: off (%s)\n", why);
    }
    return mode;
}

void ico_render_thread_stop(void)
{
    if (s_rt.thread == NULL) {
        return;
    }
    (void)ico_render_thread_drain();
    if (s_gl.on && gl_owner() != GL_GAME) {
        gl_take(); /* the context back on this thread for good */
    }
    /* nothing more handed over: the renderer and the RHI back on this
       thread alone (the textures still to retire destroyed here) */
    ico_diag_set_render_heartbeat(NULL);
    rd_set_thread_host(NULL); /* the submit thread ended too */
    rhi_set_call_guard(NULL);
    SDL_LockMutex(s_rt.lock);
    fm_quit(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    SDL_WaitThread(s_rt.thread, NULL);
    s_rt.thread = NULL;
    /* both threads ended: their ids are not known until they start again
       (ico_render_thread_tids: a session must not be given an ended
       thread's id) */
    __atomic_store_n(&s_place.renderTid, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s_place.submitTid, 0, __ATOMIC_RELEASE);
    SDL_DestroyCondition(s_rt.cond);
    SDL_DestroyMutex(s_rt.lock);
    s_rt.cond = NULL;
    s_rt.lock = NULL;
    s_rt.mode = FM_MODE_OFF;
    s_gl.on = false;
}

FmMode ico_render_thread_mode(void)
{
    return s_rt.thread != NULL ? s_rt.mode : FM_MODE_OFF;
}

void ico_render_thread_park(void)
{
    static unsigned s_lines;

    if (s_rt.thread == NULL) {
        return;
    }
    SDL_LockMutex(s_rt.lock);
    fm_park(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    while (!fm_is_parked(&s_rt.mb)) {
        SDL_WaitCondition(s_rt.cond, s_rt.lock);
    }
    SDL_UnlockMutex(s_rt.lock);
    /* and the submit thread's last present, before the surface goes */
    (void)rd_thread_submit_drain();
    if (s_lines++ < 16) {
        fprintf(stderr, "window: render thread: parked (the pictures are submitted and presented "
                        "on the game thread until it goes on)\n");
    }
}

void ico_render_thread_unpark(void)
{
    static unsigned s_lines;

    if (s_rt.thread == NULL) {
        return;
    }
    keep_request(CPU_KEEP_WHY_RESUME); /* back from the background: the cores asked again */
    SDL_LockMutex(s_rt.lock);
    fm_unpark(&s_rt.mb);
    SDL_BroadcastCondition(s_rt.cond);
    SDL_UnlockMutex(s_rt.lock);
    if (s_lines++ < 16) {
        fprintf(stderr, "window: render thread: goes on\n");
    }
}

bool ico_render_thread_last_present(FmPresent *out)
{
    bool have;

    if (s_rt.thread == NULL) {
        return false;
    }
    SDL_LockMutex(s_rt.lock);
    have = fm_last_present(&s_rt.mb, out);
    SDL_UnlockMutex(s_rt.lock);
    return have;
}

bool ico_render_thread_plan(IcoRtPlan *out)
{
    if (s_rt.thread == NULL || s_rt.mode != FM_MODE_ON) {
        return false;
    }
    const Uint64 whole = __atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED);

    if (whole == 0) {
        return false; /* no picture handed over yet */
    }
    /* the picture in progress, if any (the heartbeat's start: pictures only) */
    out->sinceNs = __atomic_load_n(&s_rt.beatSince, __ATOMIC_ACQUIRE);
    out->readsNs = __atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED);
    out->pictureNs = whole;
    out->presentNs = __atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED);
    out->cpuNs = __atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED);
    out->interpNs = __atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED);
    /* the submit thread's job is before the next picture only while the
       frame slots are not apart (VKSLOTS: with them apart the next picture
       records while the one before is ended and submitted) */
    out->submitOn = rd_thread_submit_on();
    out->submitNs = rd_thread_submit_on() && !rd_thread_submit_slots_apart()
                        ? __atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED)
                        : 0;
    return true;
}

void ico_render_thread_note_held(int why)
{
    s_rt.held++;
    if (why == PACE_RT_OVERRUN) {
        s_rt.heldOverrun++;
    } else if (why == PACE_RT_BEHIND) {
        s_rt.heldBehind++;
    } else {
        s_rt.heldMiss++; /* PACE_RT_MISS */
    }
}

/* the step rule's held asks of the run: the trace line's limit, the first 8
   of the run then every 100th (it was the block's count, so every block
   with holds wrote 8 lines of about 900 bytes: 11 KB of a 37 KB block on
   the owner's Pixel); the block's summary line is unchanged */
static unsigned s_heldLines;

void ico_render_thread_note_ask(const struct PaceRtAsk *q, const struct PaceRtTrace *t, int why)
{
    pace_rt_trace_sum_add(&s_rt.asks, q, t, (PaceRtHold)why);
    const unsigned run = why == PACE_RT_MISS ? ++s_heldLines : 0u;
    if (why == PACE_RT_MISS && (run <= 8u || run % 100u == 0)) {
        char line[1536];

        (void)pace_rt_trace_text(q, t, s_rt.asks.held, line, sizeof(line));
        fprintf(stderr, "window: step rule: %s\n", line);
    }
}

void ico_render_thread_note_step(uint64_t readWaitNs, bool missed, uint64_t missNs, bool backedOff,
                                 uint64_t planCloseNs, uint64_t planBetweenNs, uint64_t widenNs,
                                 uint64_t closeMeanNs, uint64_t closeDevNs, bool leftOut, int act,
                                 const struct PaceStepEvent *ev)
{
    const bool byPictures = act != PACE_STEP_NONE;

    if (ev != NULL) {
        s_rt.counted += ev->judged == PACE_STEP_COUNTED ? 1u : 0u;
        s_rt.fallbacks += ev->began ? 1u : 0u;
        s_rt.fitEnds += ev->ended == PACE_STEP_END_FIT ? 1u : 0u;
        s_rt.limitEnds += ev->ended == PACE_STEP_END_LIMIT ? 1u : 0u;
        if (ev->judged == PACE_STEP_OUTLIER) {
            s_rt.outliers++;
            if (ev->miss.picNs > s_rt.outlierNs) {
                s_rt.outlierNs = ev->miss.picNs;
                s_rt.outlierTypNs = ev->miss.typNs;
            }
        }
    }

    if (readWaitNs != 0) {
        s_rt.lateStarts++;
        s_rt.lateStartMaxNs = readWaitNs > s_rt.lateStartMaxNs ? readWaitNs : s_rt.lateStartMaxNs;
    }
    if (missed) {
        s_rt.misses++;
        s_rt.pictureMisses += byPictures ? 1u : 0u;
        s_rt.smallMisses += act == PACE_STEP_SMALL ? 1u : 0u;
        s_rt.widens += act == PACE_STEP_WIDEN ? 1u : 0u;
        s_rt.missMaxNs = missNs > s_rt.missMaxNs ? missNs : s_rt.missMaxNs;
    }
    s_rt.backoffSteps += backedOff ? 1u : 0u;
    s_rt.stepPlanNs = planCloseNs;
    s_rt.stepPlanBetweenNs = planBetweenNs;
    s_rt.stepWidenNs = widenNs;
    s_rt.stepCloseMean = closeMeanNs;
    s_rt.stepCloseDev = closeDevNs;
    s_rt.leftOut += leftOut ? 1u : 0u;
}

void ico_render_thread_tids(long *render, long *submit)
{
    const bool on = s_rt.thread != NULL;
    const long r = __atomic_load_n(&s_place.renderTid, __ATOMIC_ACQUIRE);
    const long sub = __atomic_load_n(&s_place.submitTid, __ATOMIC_ACQUIRE);

    *render = !on ? 0 : (r > 0 ? r : -1);
    *submit = !on || !rd_thread_submit_on() ? 0 : (sub > 0 ? sub : -1);
}

void ico_render_thread_note_game_core(void)
{
    static uint32_t s_tryPrinted;

    cpu_place_note(&s_place.game, cpu_place_now());
    if (__atomic_load_n(&s_keepOut.tryGen, __ATOMIC_ACQUIRE) != s_tryPrinted) {
        /* the keeper's try with a new outcome (the log thread's text) */
        char text[1024];

        SDL_LockSpinlock(&s_keepOut.lock);
        s_tryPrinted = s_keepOut.tryGen;
        memcpy(text, s_keepOut.tryText, sizeof(text));
        SDL_UnlockSpinlock(&s_keepOut.lock);
        text[sizeof(text) - 1] = '\0';
        ico_log_limited_n("window: render thread cores", 32u, 100u,
                          "window: render thread cores: %s", text);
    }
}

void ico_render_thread_block_line(double stepMs, double stepCpuMs, double pictureCpuMs,
                                  double pumpMs)
{
    const unsigned waits = __atomic_exchange_n(&s_rt.slotWaits, 0u, __ATOMIC_RELAXED);
    const Uint64 waitNs = __atomic_exchange_n(&s_rt.slotWaitNs, (Uint64)0, __ATOMIC_RELAXED);
    const unsigned held = s_rt.held, heldMiss = s_rt.heldMiss;
    const unsigned heldOverrun = s_rt.heldOverrun, heldBehind = s_rt.heldBehind;
    const unsigned lateStarts = s_rt.lateStarts, misses = s_rt.misses;
    const unsigned pictureMisses = s_rt.pictureMisses;
    const unsigned smallMisses = s_rt.smallMisses, widens = s_rt.widens;
    const unsigned fallbacks = s_rt.fallbacks;
    const unsigned counted = s_rt.counted, outliers = s_rt.outliers;
    const unsigned fitEnds = s_rt.fitEnds, limitEnds = s_rt.limitEnds;
    const double outlierMs = (double)s_rt.outlierNs / 1e6;
    const double outlierTypMs = (double)s_rt.outlierTypNs / 1e6;
    const unsigned backoffSteps = s_rt.backoffSteps;
    const unsigned leftOut = s_rt.leftOut;
    const double lateStartMaxMs = (double)s_rt.lateStartMaxNs / 1e6;
    const double missMaxMs = (double)s_rt.missMaxNs / 1e6;
    RdThreadBlock tb;
    RdSubmitBlock sb;
    unsigned closes;
    double closeMs, closeMaxMs;
    char rt[1536], st[384], cores[1024];
    CpuPlaceHist gc, rc, sc;

    s_rt.held = s_rt.heldMiss = s_rt.heldOverrun = s_rt.heldBehind = 0;
    s_rt.lateStarts = s_rt.misses = s_rt.backoffSteps = s_rt.pictureMisses = 0;
    s_rt.smallMisses = s_rt.widens = s_rt.fallbacks = 0;
    s_rt.counted = s_rt.outliers = s_rt.fitEnds = s_rt.limitEnds = 0;
    s_rt.outlierNs = s_rt.outlierTypNs = 0;
    s_rt.lateStartMaxNs = s_rt.missMaxNs = 0;
    s_rt.leftOut = 0;

    /* the cores each thread ran on most in the block (cpu_place.h), and
       their share on the slowest cores where the cores differ */
    cpu_place_take(&s_place.game, &gc);
    cpu_place_take(&s_place.render, &rc);
    rd_thread_submit_take_cores(&sc);
    (void)cpu_place_part(cores, sizeof(cores), &gc, &rc, rd_thread_submit_on() ? &sc : NULL,
                         s_place.slowMask);
#if defined(__ANDROID__)
    {
        /* each thread's samples by core (one a step, a picture, a submit
           job: the thread's own getcpu, no file read), where the most-run
           core alone does not say how a thread spread */
        char sh[400];
        const size_t at = strlen(cores);

        (void)cpu_place_shares_text(sh, sizeof(sh), &gc, &rc, rd_thread_submit_on() ? &sc : NULL);
        snprintf(cores + at, sizeof(cores) - at, "; %s", sh);
    }
#endif
    if (cpu_place_free_mask() != 0) {
        /* the threads made by a kept thread and put back on every core at
           their entry since the start (cpu_place_thread_free) */
        unsigned resets, failures;
        const size_t at = strlen(cores);

        cpu_place_free_counts(&resets, &failures);
        snprintf(cores + at, sizeof(cores) - at,
                 ", threads put back on every core %u (%u could not be)", resets, failures);
    }
    {
        /* the threads' time on a core and waiting for one in the block
           (thread_stat.h), after the cores */
        char ran[200];
        const size_t at = strlen(cores);

        if (place_stat_part(ran, sizeof(ran), rd_thread_submit_on()) > 0) {
            snprintf(cores + at, sizeof(cores) - at, "; %s", ran);
        }
    }
    window_log_cost_mark("the game thread line's cores and run times");
    rd_thread_block_take(&tb);
    rd_thread_submit_block_take(&sb);
    rd_frame_close_take(&closes, &closeMs, &closeMaxMs);
    /* Frame rate Original's pictures: handed over, waited, skipped while
       the render thread was busy, replayed inside the step (rd.h
       rd_frame_post_take; empty at the other frame rates) */
    RdFramePost fp;
    char post[288];

    rd_frame_post_take(&fp);
    rd_frame_post_text(&fp, post, sizeof(post));
    /* the submit thread (rd_submit_thread.h): per picture its submit and
       present, the most pictures queued and running at once, the render
       thread's waits for it (its next picture's first graphics call) and
       the others' (the game thread's drains), the lists the render thread
       ended and submitted itself (a call of its own needed the backend
       first) */
    if (rd_thread_submit_on()) {
        snprintf(st, sizeof(st),
                 "; submit thread: %.1f ms a picture (submit %.1f, present %.1f; %u pictures, "
                 "longest %.1f ms), queue depth max %u, the render thread waited for it %u times "
                 "(%.1f ms), the others %u times (%.1f ms, longest %.1f ms), %u submitted on the "
                 "render thread",
                 sb.submitMs + sb.presentMs, sb.submitMs, sb.presentMs, sb.jobs, sb.longestMs,
                 sb.depthMax, sb.renderWaits, sb.renderWaitMs, sb.waits - sb.renderWaits,
                 sb.waitMs - sb.renderWaitMs, sb.waitMaxMs, sb.onRender);
    } else {
        snprintf(st, sizeof(st), "; submit thread: off");
    }
    if (s_rt.thread != NULL) {
        FmBlock b;

        SDL_LockMutex(s_rt.lock);
        fm_block_take(&s_rt.mb, SDL_GetTicksNS(), &b);
        SDL_UnlockMutex(s_rt.lock);
        /* per picture of its own: its CPU time (the window's replays line),
           the interpolation, the uploads and the walk, the submit and the
           present, and the GPU's and the display's waits inside them */
        snprintf(rt, sizeof(rt),
                 "render thread: CPU %.1f ms a picture, idle %.0f %%, waited for a frame %.1f ms, "
                 "heartbeat max %.1f ms (%u pictures%s); interp %.1f ms, walk %.1f ms, submit "
                 "%.1f ms, GPU and display waits %.1f ms a picture; %u textures destroyed for the "
                 "game thread; %u further presents held back (%u would have made the step miss "
                 "its period, %u while a picture ran past its estimate, %u while the presents "
                 "waited for the display or the GPU; a picture holds the renderer for %.1f ms "
                 "typically (its mean %.1f ms and half its deviation of %.1f ms; %.1f ms of its "
                 "own, %.1f ms of it the interpolation; the rest its waits for the GPU and the "
                 "submit thread), takes %.1f ms, "
                 "its present waits %.1f ms at the longest lately, the submit thread's part %.1f "
                 "ms; the steps planned at %.1f ms closing a frame (its mean %.1f ms, its "
                 "deviation %.1f ms) and %.1f ms between frames (their typical time, %.1f "
                 "ms of it the margin widened after a miss; %u long steps of the game's own left "
                 "out)); steps started late by the pictures: %u (longest %.1f ms); steps "
                 "that missed their period: %u (%u the pictures': %u under %.0f ms, %u widened "
                 "the margin (%u counted toward the back-off, %u outliers at %.1f ms against a "
                 "typical %.1f ms), %u the third in %u steps; %u the game's own; latest %.1f ms "
                 "past it; %u steps planned at the whole period after the pictures' (back-offs "
                 "ended: %u by the pictures between frames fitting, %u at the limit of %u "
                 "steps))",
                 pictureCpuMs, b.idlePercent, b.waitMaxMs, b.busyMaxMs, b.pictures,
                 s_rt.mode == FM_MODE_SYNC ? ", sync" : "", tb.interpMs, tb.walkMs, tb.submitMs,
                 tb.waitMs, tb.retired, held, heldMiss, heldOverrun, heldBehind,
                 (double)__atomic_load_n(&s_rt.readsEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.readsMean, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.readsDev, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.cpuEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.interpEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.pictureEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.presentEst, __ATOMIC_RELAXED) / 1e6,
                 (double)__atomic_load_n(&s_rt.submitEst, __ATOMIC_RELAXED) / 1e6,
                 (double)s_rt.stepPlanNs / 1e6, (double)s_rt.stepCloseMean / 1e6,
                 (double)s_rt.stepCloseDev / 1e6, (double)s_rt.stepPlanBetweenNs / 1e6,
                 (double)s_rt.stepWidenNs / 1e6, leftOut, lateStarts, lateStartMaxMs, misses,
                 pictureMisses, smallMisses, (double)PACE_STEP_MISS_FLOOR_NS / 1e6, widens, counted,
                 outliers, outlierMs, outlierTypMs, fallbacks, PACE_STEP_MISS_WINDOW,
                 misses - pictureMisses, missMaxMs, backoffSteps, fitEnds, limitEnds,
                 PACE_STEP_BACKOFF_STEPS);
    } else {
        snprintf(rt, sizeof(rt), "render thread: off (CPU %.1f ms a picture on the game thread)",
                 pictureCpuMs);
    }
    /* slot waits: the game thread's waits for everything posted to end
       (ico_render_thread_drain), by whose: a new frame's present (its
       picture before still in its submit and present: the frame's close
       already waited for that picture's reads), a further present's, the
       rest (a graphics call's, a settings change's, a load's); read waits:
       the game thread's calls that changed the renderer's state and waited
       for the render thread's reads of a picture (its interpolation and
       walk; rd_thread.h) */
    const unsigned byPresents = tb.freshWaits + tb.furtherWaits;
    /* the read waits by what the render thread was doing as each began,
       the frame slots it waited for outside the hand-over and the
       display's waits it made there (rd_thread.h) */
    char rw[1024], cpu[48];

    rd_thread_read_waits_text(&tb, rw, sizeof(rw));
    /* the step's CPU time against its wall time (cpu_place.h
       cpu_place_thread_cpu_ns; left out when not known) */
    if (cpu_place_cpu_text(stepCpuMs, cpu, sizeof(cpu)) > 0) {
        const size_t at = strlen(cpu);

        snprintf(cpu + at, sizeof(cpu) - at, "; ");
    }
    fprintf(stderr,
            "window: game thread: step %.1f ms (%sclosing a frame %.1f ms of it, longest %.1f ms, "
            "%u frames%s), pump %.1f ms, slot waits %u (%.1f ms; %u (%.1f ms) by a new frame's "
            "present, %u (%.1f ms) by a further present's, %u other), read waits %u (%.1f ms, "
            "longest %.1f ms%s); %s%s; %s\n",
            stepMs, cpu, closeMs, closeMaxMs, closes, post, pumpMs, waits, (double)waitNs / 1e6,
            tb.freshWaits, tb.freshWaitMs, tb.furtherWaits, tb.furtherWaitMs,
            waits > byPresents ? waits - byPresents : 0u, tb.readWaits, tb.readWaitMs,
            tb.readWaitMaxMs, rw, rt, st, cores);
    if (s_rt.asks.asks != 0) {
        /* the step rule's asks in the block: which test held them, by how
           much, what set the reads' end, the inputs' means */
        char ask[1536];

        (void)pace_rt_trace_sum_text(&s_rt.asks, 10.0, ask, sizeof(ask));
        fprintf(stderr, "window: step rule: %s\n", ask);
    }
    memset(&s_rt.asks, 0, sizeof(s_rt.asks));
    if (__atomic_load_n(&s_keepOn, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&s_tickOn, __ATOMIC_ACQUIRE)) {
        /* the keeper's block (the log thread's text, up to a second old),
           with the game thread's own step: the cores it is kept to beside
           the time it took (rc12's cage: 9-14 ms steps on cores 2-6) */
        char kt[1024], kc[48];

        (void)cpu_place_cpu_text(stepCpuMs, kc, sizeof(kc));
        SDL_LockSpinlock(&s_keepOut.lock);
        memcpy(kt, s_keepOut.blockText, sizeof(kt));
        SDL_UnlockSpinlock(&s_keepOut.lock);
        kt[sizeof(kt) - 1] = '\0';
        if (kt[0] != '\0') {
            fprintf(
                stderr,
                "window: render thread cores: in 10 s: %s; the game thread's step %.1f ms%s%s%s "
                "(the log thread's, up to 1 s old)\n",
                kt, stepMs, kc[0] != '\0' ? " (" : "", kc, kc[0] != '\0' ? ")" : "");
        }
        (void)__atomic_fetch_add(&s_keepTaken, 1u, __ATOMIC_RELEASE);
    }
    window_log_cost_mark("the game thread line");
    place_threads_line(); /* again only when the threads or their cores changed */
    window_log_cost_mark("the thread list");
}
