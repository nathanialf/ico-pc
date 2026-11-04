/*
 * port/platform/test/pace_loop_test.c
 *
 * The vsync lock's loop (pace_policy.h pace_lock_vsync; docs/port/DISPLAY.md
 * "The vsync lock"): the
 * simulation on a grid of the wall clock, n presents a step, under a fake
 * clock and a fake swapchain.  Each simulated vsync the test calls the loop
 * as window_host.c's pace() does, then runs the game's step (its own cost);
 * a game frame closes every two steps.
 *
 * The fake's mailbox display: a present costs its CPU time and returns; its
 * picture is ready a GPU time later (never before the one before it); at
 * each vblank the display shows the newest picture presented that is ready
 * (one passed over is dropped; a vblank without a new one shows the last
 * again, a repeat).  Its display clock (present wait): a wait for a present
 * (the backend's thread waiting on each present) notes each present at
 * the vblank that showed it or a later one, plus a wake-up.
 *
 *   60.00 Hz, the grid on the reported refresh: every step starts on its
 *     due time and the due times are a period apart; one present a step;
 *     the alphas exactly 1/2, 1; no picture repeated or dropped in 60 s; the
 *     audio's ratio 60 / 59.94.  At 59.94 Hz the ratio is 1;
 *   NVIDIA's FIFO return pattern (RTX 3090): present returns 8.85, 31.71,
 *     9.43 ms apart, which a lock stepping on returns would follow.  Presents of 0.9, 1.2 and
 *     13 ms in turn (the same two quick returns and a slow one) leave every
 *     step on the grid; one of 25 ms ends past the next step's due time,
 *     which then starts late, and the grid does not move;
 *   120 Hz: two presents a step, alphas 1/4, 2/4, 3/4, 4/4;
 *   the phase: presents that finish near a vblank (GPU time jitter) repeat
 *     and drop pictures without the display clock; with it the grid moves
 *     away from the vblank and, from 16 starting phases and at two loads
 *     (4K's among them), none does over the last 30 s;
 *   a display 50 ppm faster than it reports: pictures repeated or dropped
 *     in 10 minutes without the clock, none after the first 10 s with it
 *     (the slot follows the display to within 3 ppm);
 *   without the clock (FIFO, as the window takes then): at 16 phases and
 *     two loads only the display's drift slips a picture, where mailbox
 *     repeats and drops them all the time at some phases;
 *   a display clock 2 % off the report, one that stops: left, the slot
 *     back on the report;
 *   off the screen for 10 s: no presents, the steps go on on the grid;
 *   a 30 ms step: that step's lag caught up, back on the grid; a 300 ms
 *     step: the lag dropped, the grid from then;
 *   the FIFO models of the old lock's tests (a present blocking to the
 *     vblank after it, a queue of 2 or 3 images, NVIDIA's fence waits and
 *     a menu closing) on a FIFO-only swapchain: the grid never moves and
 *     the steps start on it while the presents return before it;
 *   the first frame after a reset 1/2, 1; nothing presented: the grid;
 *   pace_fifo_hold (no lock at 144 Hz); the nominal ratio; the log's trace;
 *   the vblank clock (a note at each vblank of the fake's display, with
 *   the same wake-ups; FIFO): a FIFO queue against a display off its
 *   report by -1000 to +1000 ppm (it fills and skips or drains and
 *   repeats without the clock; nothing with it, the slot within 3 ppm),
 *   presents finishing near a vblank (the ratchet), D3D12's full queue
 *   at +163 ppm, the skip guard (no skip after a present that waited for
 *   the display, while a 30 ms step and a slow GPU still skip), and clocks
 *   not believed (twice the rate, one that stops, missed wake-ups);
 *   the late-present skip (testLateSkipLock): 39 ms presents of a 30
 *   frames a second game keep real time, never two frames in a row
 *   unpresented, no resync; 2 ms presents never skip;
 *   the present allowance (test120Budget): with the fence (a replay waits
 *   for the GPU's picture two before) and a GPU of 12 ms a picture at 120
 *   Hz, the second slot's present dropped before the steps run late, one
 *   present a step and the alphas on the grid, the probes failing back to
 *   1 with no step a period late; a GPU-bound phone (20 ms a picture at 60
 *   Hz) presents about its new frames only and keeps real time;
 *   the disc's excuse (testDiscWait126): every 150th step 100 ms long, the
 *   game thread waiting for a disc read the whole of it (issue 126), drops
 *   nothing and counts each in discExcused; the same steps without a disc
 *   wait count none;
 *   the render thread (port/platform/render_thread.h; the fake's async
 *   model: a present's own part, the interpolation and the record, on the
 *   game thread, its submit and present on the render thread's timeline,
 *   the picture handed over at the end of the game thread's part and ready
 *   the GPU's time after the render thread's part; the next present waits
 *   for the render thread's part before its own, as rd__wait_frame does):
 *   testAsyncLock, the lock at 60 Hz with a 5 ms step and a 12.5 ms present
 *   on the game thread falls behind and skips, the same present split 9 ms
 *   on the game thread and 3.5 ms on the render thread keeps every step on
 *   time, one present a step, the alphas on the grid; testRoomOffGame, the
 *   lock at 60 Hz with Direct3D 11's wait for room in the present queue on
 *   the render thread (a full FIFO queue: it ends at the next vblank):
 *   made at the end of the picture, after its present, it is about 14 ms a
 *   picture with no read wait in the step, the next present waiting about
 *   0.67 ms for it as the display's wait, the period kept (one present a
 *   step, no step past the next due time, nothing dropped, the alphas
 *   exact); made in the acquire, inside the reads, it puts about 4.2 ms of
 *   read waits in every step; testAsyncRates, the
 *   timer path paced by the GPU's time (vsync off or mailbox) with the
 *   presents handed over: pictures of 9, 12, 17, 20 and 25 ms give about a
 *   period over the picture time presents a step, never more than the GPU
 *   draws, no step a quarter of a period late; 30 ms pictures every new
 *   frame (30 a second); and a present whose CPU part is 9 ms (3
 *   interpolated, 2 recorded, 4 submitted and presented) with a 3 ms step:
 *   two presents a step with the render thread against one without;
 *   the render thread's own times (testRenderThreadPlan, the whole picture
 *   on the render thread, a step waiting for a picture's reads, the next
 *   present for the picture before): a further present planned by them
 *   (pace_rt_further_late) never makes a step wait, at 3, 9 and 22 ms
 *   pictures and a burst of 30 ms ones the presents a second are what the
 *   picture time allows and every frame is shown; without the plan the 22
 *   ms pictures and the burst make the steps wait; and the presents that
 *   wait for the GPU (testRenderThreadBehind, a present returning once the
 *   GPU finished the picture before it): with pace_rt_further_hold's holds
 *   a stage of 26 or 20 ms GPU pictures makes no step wait, where the plan
 *   alone leaves 40 and more slot waits.
 */
#include <stdio.h>
#include <string.h>
#include "pace_policy.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define SEC 1000000000ull
#define NTSC 16683333ull
#define R60 16666667ull /* what the window takes from a reported 60.00 Hz */
#define D60 (1e9 / 60.0)
#define ALPHA_LOG 64
#define PICS 64

enum { F_MAILBOX, F_FIFO, F_BLOCK, F_QUEUE, F_NV };

typedef struct Pic {
    uint64_t id, ready;
} Pic;

typedef struct Fake {
    int mode;
    uint64_t now;
    uint64_t phase; /* a vblank: vblank k at phase + k * display */
    double display; /* ns between vblanks */
    uint64_t cost;  /* a present's CPU time */
    uint32_t rng;
    /* present costs in turn (0 entries: cost) */
    const uint64_t *costs;
    unsigned nCosts, costAt;
    /* mailbox: a picture is ready gpu (+ up to gpuJitter) after its present
       returns; the display's state */
    uint64_t gpu, gpuJitter;
    unsigned spikeEvery; /* every spikeEvery-th picture takes spike more (0: none) */
    uint64_t spike;
    Pic pics[PICS];
    unsigned nPics;
    uint64_t nextId, shownId, vbDone, lastReady;
    uint64_t countFrom; /* repeats and drops counted from then */
    unsigned repeats, drops;
    /* the display clock: each present noted at the vblank that showed it
       or a later one (one passed over: when the next was shown), this long
       after it */
    uint64_t wake, wakeJitter;
    int clockFails;
#define NOTES 4096
    uint64_t noteId[NOTES], noteAt[NOTES];
    unsigned noteHead, noteCount;
    uint64_t noted; /* the last present noted */
    /* the window off the screen for vsyncs [hiddenFrom, hiddenTo) */
    unsigned vsync, hiddenFrom, hiddenTo;
    /* FIFO models (the old lock's tests) */
    uint64_t jitterMax; /* F_BLOCK, F_QUEUE: a return up to this after its vblank */
    unsigned depth, queued;
    uint64_t vbIdx;
    unsigned rebuildAt;
    uint64_t rebuildNs;
    /* F_NV: the acquire and the present return at once; the replay waits
       at its start for the GPU to finish the frame two before
       (RHI_FRAMES_IN_FLIGHT 2), and the GPU draws into an image only when
       the display let go of it (three images) */
    uint64_t pre, tail, gpuNv, tailJitter;
    unsigned rampAt, rampLen;
    int64_t rampTail, rampGpu;
#define NV_RING 8
    uint64_t gpuDone[NV_RING], showAt[NV_RING];
    unsigned nvCount;
    unsigned presents;
    float alpha[ALPHA_LOG];
    unsigned nAlpha;
    /* the vblank clock: a note at each vblank (vbDouble: and half a
       refresh before it, an output at twice the rate), wake (+ up to
       wakeJitter) after it; every vbDropEvery-th not noted (a wake-up
       missed) */
    int vbOn, vbDouble;
    unsigned vbDropEvery, vbCount;
#define VB_NOTES 1024
    uint64_t vbNote[VB_NOTES];
    unsigned vbHead, vbNotes;
    /* presents that took a quarter of the report or more while the lock
       was told they waited for nothing (Opt.noGuard: the rule before the
       skip guard) */
    uint64_t report;
    unsigned waitedSeen;
    /* the fence (mailbox and FIFO): a present waits for the picture two
       before it to be ready (RHI_FRAMES_IN_FLIGHT 2), and the GPU draws
       one picture at a time (ready = max(the last, the submit) + gpu);
       the last present's waits for the GPU and for the display (FIFO's
       acquire), the latter for the present allowance */
    int fence;
    uint64_t readyAt[4];
    uint64_t fenceWait, displayWait;
    /* the render thread (mailbox and FIFO): cost is the game thread's part
       of a present, rcost the render thread's (the submit and the
       present), which starts when the game thread's ends; the next present
       waits for it first (rd__wait_frame); rFree: when it is done */
    int async;
    uint64_t rcost, rFree;
    /* the render thread's display wait (testRoomOffGame: Direct3D 11 with
       the render thread submitting and presenting itself): a picture's job
       from the hand-over is its interpolation rInterp, the rest of its
       reads rRest, then its submit and present rPresent; its wait for room
       in the present queue ends at the next vblank (a full FIFO queue, a
       picture taken each refresh), made inside the reads after the
       interpolation (room 1: the acquire's) or after the present (room 2:
       at the end of the picture, outside the reads); the job's end is
       rFree.  readsEnd: when its reads end; the step's first renderer call
       waits for it (a read wait).  From countFrom: the waits for room, the
       read waits, the presents' waits for the render thread (the display's
       wait as the window counts it) and the steps that ended past the next
       step's due time */
    int room;
    uint64_t rInterp, rRest, rPresent, readsEnd;
    unsigned roomWaits, readWaits, presentWaits, periodMisses;
    uint64_t roomWaitNs, readWaitNs, presentWaitNs;
} Fake;

static uint64_t f_now(void *u)
{
    return ((Fake *)u)->now;
}

static uint32_t f_rand(Fake *f)
{
    f->rng = f->rng * 1664525u + 1013904223u;
    return f->rng >> 8;
}

static uint64_t f_vb(const Fake *f, uint64_t k)
{
    return f->phase + (uint64_t)((double)k * f->display);
}

/* the last vblank at or before t */
static uint64_t f_vb_index(const Fake *f, uint64_t t)
{
    uint64_t k = (uint64_t)((double)(t - f->phase) / f->display);

    while (f_vb(f, k + 1) <= t) {
        k++;
    }
    while (k > 0 && f_vb(f, k) > t) {
        k--;
    }
    return k;
}

static int f_hidden(const Fake *f)
{
    return f->vsync >= f->hiddenFrom && f->vsync < f->hiddenTo;
}

/* a vblank clock's note of the vblank at v */
static void f_vb_note(Fake *f, uint64_t v)
{
    f->vbCount++;
    if (f->vbDropEvery != 0 && f->vbCount % f->vbDropEvery == 0) {
        return; /* the wake-up missed */
    }
    if (f->vbNotes == VB_NOTES) {
        f->vbHead = (f->vbHead + 1) % VB_NOTES; /* the oldest dropped */
        f->vbNotes--;
    }
    f->vbNote[(f->vbHead + f->vbNotes++) % VB_NOTES] =
        v + f->wake + (f->wakeJitter ? f_rand(f) % f->wakeJitter : 0);
}

/* the mailbox display's vblanks up to t */
static void f_display_to(Fake *f, uint64_t t)
{
    for (;;) {
        const uint64_t v = f_vb(f, f->vbDone + 1);
        int best = -1;

        if (v > t) {
            break;
        }
        f->vbDone++;
        if (f->vbOn) {
            if (f->vbDouble) {
                f_vb_note(f, v - (uint64_t)(f->display / 2.0));
            }
            f_vb_note(f, v);
        }
        for (unsigned i = 0; i < f->nPics; i++) {
            if (f->pics[i].ready <= v) {
                best = (int)i;
            }
        }
        if (f->mode == F_FIFO) {
            /* in order: the next one when it is ready */
            best = f->nPics > 0 && f->pics[0].ready <= v ? 0 : -1;
        }
        if (best >= 0) {
            const uint64_t at = v + f->wake + (f->wakeJitter ? f_rand(f) % f->wakeJitter : 0);

            if (v >= f->countFrom) {
                f->drops += (unsigned)best;
            }
            f->shownId = f->pics[best].id;
            /* the clock's notes: this present and any passed over */
            while (f->noted < f->shownId && f->noteCount < NOTES) {
                const unsigned i = (f->noteHead + f->noteCount++) % NOTES;

                f->noteId[i] = ++f->noted;
                f->noteAt[i] = at;
            }
            memmove(f->pics, f->pics + best + 1, (f->nPics - (unsigned)best - 1) * sizeof(Pic));
            f->nPics -= (unsigned)best + 1;
        } else if (f->shownId != 0 && v >= f->countFrom && !f_hidden(f)) {
            f->repeats++;
        }
    }
}

static uint64_t f_cost(Fake *f)
{
    return f->nCosts ? f->costs[f->costAt++ % f->nCosts] : f->cost;
}

static uint64_t f_vblank_at(const Fake *f, uint64_t t);

static void f_present_mailbox(Fake *f)
{
    uint64_t handWait = 0;
    const bool counted = f->now >= f->countFrom;

    if (f->async && f->rFree > f->now) {
        /* the picture before's submit and present, on the render thread */
        handWait = f->rFree - f->now;
        f->now = f->rFree;
    }
    const uint64_t acquire = f->now;

    f_display_to(f, f->now);
    if (f->mode == F_FIFO) {
        /* three images: the acquire waits while one is on the screen and
           two wait for it */
        while (f->nPics >= 2) {
            f->now = f_vb(f, f->vbDone + 1);
            f_display_to(f, f->now);
        }
    }
    f->displayWait = f->now - acquire + handWait; /* as present_once counts it */
    if (f->room != 0 && counted && handWait != 0) {
        f->presentWaits++;
        f->presentWaitNs += handWait;
    }
    f->fenceWait = 0;
    if (f->fence && f->nextId >= 2) {
        /* the picture two before the one made now (ids from 1) */
        const uint64_t two = f->readyAt[(f->nextId - 1) % 4];

        f->fenceWait = two > f->now ? two - f->now : 0;
        f->now += f->fenceWait;
    }
    f->now += f_cost(f);
    f_display_to(f, f->now);
    uint64_t submitAt = f->now;

    if (f->room != 0) {
        /* the picture's whole job on the render thread, from the hand-over */
        uint64_t t = f->now + f->rInterp;

        if (f->room == 1) {
            const uint64_t v = f_vblank_at(f, t); /* the acquire: room at the vblank */

            if (counted) {
                f->roomWaits++;
                f->roomWaitNs += v - t;
            }
            t = v;
        }
        t += f->rRest;
        f->readsEnd = t;
        t += f->rPresent;
        submitAt = t;
        if (f->room == 2) {
            const uint64_t v = f_vblank_at(f, t); /* the next picture's room, made now */

            if (counted) {
                f->roomWaits++;
                f->roomWaitNs += v - t;
            }
            t = v;
        }
        f->rFree = t;
    } else if (f->async) {
        /* handed over: submitted and presented on the render thread */
        f->rFree = f->now + f->rcost;
        submitAt = f->rFree;
    }
    uint64_t ready = (f->fence && f->lastReady > submitAt ? f->lastReady : submitAt) + f->gpu +
                     (f->gpuJitter ? f_rand(f) % f->gpuJitter : 0);

    if (f->spikeEvery != 0 && (f->nextId + 1) % f->spikeEvery == 0) {
        ready += f->spike;
    }

    if (ready < f->lastReady) {
        ready = f->lastReady; /* the GPU's work in order */
    }
    f->lastReady = ready;
    if (f->nPics == PICS) {
        memmove(f->pics, f->pics + 1, (PICS - 1) * sizeof(Pic));
        f->nPics--;
    }
    f->pics[f->nPics++] = (Pic){++f->nextId, ready};
    f->readyAt[f->nextId % 4] = ready;
}

/* the queue after the vblanks up to now */
static void f_drain(Fake *f)
{
    const uint64_t cur = f_vb_index(f, f->now);
    const uint64_t pop = cur - f->vbIdx;

    f->queued = pop >= f->queued ? 0 : f->queued - (unsigned)pop;
    f->vbIdx = cur;
}

/* the ramp's share at this vsync (0 .. full) added to base */
static uint64_t f_ramp(const Fake *f, uint64_t base, int64_t full)
{
    int64_t add = 0;

    if (f->rampLen != 0 && f->vsync >= f->rampAt) {
        add = f->vsync >= f->rampAt + f->rampLen
                  ? full
                  : full * (int64_t)(f->vsync - f->rampAt) / (int64_t)f->rampLen;
    }
    return (uint64_t)((int64_t)base + add);
}

/* the first vblank at or after t */
static uint64_t f_vblank_at(const Fake *f, uint64_t t)
{
    const uint64_t k = f_vb_index(f, t);

    return f_vb(f, k) == t ? t : f_vb(f, k + 1);
}

static void f_present_nv(Fake *f)
{
    const unsigned i = f->nvCount;
    uint64_t start;

    f->now += f->pre;
    if (i >= 2 && f->now < f->gpuDone[(i - 2) % NV_RING]) {
        f->now = f->gpuDone[(i - 2) % NV_RING]; /* the fence wait */
    }
    f->now += f_ramp(f, f->tail, f->rampTail) + (f->tailJitter ? f_rand(f) % f->tailJitter : 0);
    start = f->now;
    if (i >= 1 && start < f->gpuDone[(i - 1) % NV_RING]) {
        start = f->gpuDone[(i - 1) % NV_RING];
    }
    if (i >= 3 && start < f->showAt[(i - 2) % NV_RING]) {
        start = f->showAt[(i - 2) % NV_RING]; /* the image is free */
    }
    f->gpuDone[i % NV_RING] = start + f_ramp(f, f->gpuNv, f->rampGpu);
    uint64_t show = f_vblank_at(f, f->gpuDone[i % NV_RING]);

    if (i >= 1 && show <= f->showAt[(i - 1) % NV_RING]) {
        show = f->showAt[(i - 1) % NV_RING] + (uint64_t)f->display;
    }
    f->showAt[i % NV_RING] = show;
    f->nvCount++;
}

static bool f_present(void *u, float a)
{
    Fake *f = u;

    f->presents++;
    if (f->nAlpha < ALPHA_LOG) {
        f->alpha[f->nAlpha++] = a;
    }
    switch (f->mode) {
    case F_MAILBOX:
    case F_FIFO:
        f_present_mailbox(f);
        break;
    case F_NV:
        f_present_nv(f);
        break;
    case F_QUEUE:
        f->now += f->cost;
        f_drain(f);
        if (f->queued >= f->depth) {
            /* full: wait for the vblank that takes one */
            f->now = f_vb(f, f->vbIdx + 1);
            f_drain(f);
            f->now += f->jitterMax ? f_rand(f) % f->jitterMax : 0;
        }
        f->queued++;
        break;
    default: /* F_BLOCK: to the first vblank after the replay */
        f->now += f->cost;
        f->now = f_vb(f, f_vb_index(f, f->now) + 1) + (f->jitterMax ? f_rand(f) % f->jitterMax : 0);
        break;
    }
    return true;
}

static bool f_visible(void *u)
{
    return !f_hidden(u);
}

static void f_sleep(void *u, uint64_t ns)
{
    Fake *f = u;

    if (ns > f->now) {
        f->now = ns;
    }
}

static uint64_t f_present_id(void *u)
{
    return ((Fake *)u)->nextId;
}

/* the display clock: the next note made by now */
static int f_shown(void *u, uint64_t *id, uint64_t *ns)
{
    Fake *f = u;

    if (f->clockFails) {
        return -1;
    }
    f_display_to(f, f->now);
    if (f->noteCount == 0 || f->noteAt[f->noteHead] > f->now) {
        return 0;
    }
    *id = f->noteId[f->noteHead];
    *ns = f->noteAt[f->noteHead];
    f->noteHead = (f->noteHead + 1) % NOTES;
    f->noteCount--;
    return 1;
}

/* the vblank clock: the next note made by now */
static int f_vblank(void *u, uint64_t *ns)
{
    Fake *f = u;

    if (f->clockFails) {
        return -1;
    }
    f_display_to(f, f->now);
    if (f->vbNotes == 0 || f->vbNote[f->vbHead] > f->now) {
        return 0;
    }
    *ns = f->vbNote[f->vbHead];
    f->vbHead = (f->vbHead + 1) % VB_NOTES;
    f->vbNotes--;
    return 1;
}

/* the present allowance's excuse: the last present's wait for the display
   (FIFO's acquire; 0 in mailbox) */
static uint64_t f_display_waited(void *u, uint64_t costNs)
{
    (void)costNs;
    return ((Fake *)u)->displayWait;
}

/* the rule before the skip guard: no present waited (the fake counts the
   ones that did) */
static uint64_t f_waited_none(void *u, uint64_t costNs)
{
    Fake *f = u;

    if (costNs * 4 >= f->report) {
        f->waitedSeen++;
    }
    return 0;
}

typedef struct Opt {
    unsigned n;
    uint64_t report; /* the refresh the display reports */
    uint64_t step, stepJitter;
    double seconds;
    unsigned bigAt; /* the step after vsync bigAt takes bigStep */
    uint64_t bigStep;
    bool fifo;         /* a FIFO-only swapchain */
    int clockKind;     /* the display clock offered (PACE_CLOCK_KIND_*) */
    bool noGuard;      /* the skip rule before the guard (f_waited_none) */
    double phaseFrom;  /* the step's offset after a vblank measured from (s; 0: not) */
    bool budget;       /* the present allowance judged (PaceLockIo.displayWaited) */
    uint64_t gpuNs;    /* the GPU time it is told (PaceLock.gpuNs; 0: not known) */
    unsigned bigEvery; /* also every bigEvery-th step takes bigStep (0: not) */
    bool bigDisc;      /* a bigStep is the disc's: PaceLock.discWaitNs bigStep after it */
} Opt;

typedef struct Run {
    unsigned steps, presents, skipped, unshown, resyncs;
    unsigned lateSteps;  /* steps that started 0.5 ms or more after their due time */
    uint64_t lateMax;    /* the latest step start against its due time */
    uint64_t gridDevMax; /* the most a due time was off a period (and the clock's nudge) after
                            the one before */
    int alphaBad;        /* an alpha not the exact fraction, or not increasing in a frame */
    unsigned alphaChecked;
    unsigned alphaOffGrid;            /* an alpha not k / (2n) for a whole k of 1 .. 2n, or not
                              increasing in a frame (with slots refused or skipped) */
    unsigned presentsFrom, stepsFrom; /* from the fake's countFrom */
    double ratio;                     /* pace_lock_rate_ratio over the last 10 s */
    unsigned repeats, drops;
    /* from the fake's countFrom: skips, presents that waited, the latest
       step, resyncs */
    unsigned skippedFrom, waitedFrom, resyncsFrom;
    uint64_t lateMaxFrom;
    /* from Opt.phaseFrom: the due times' offsets after the vblank before
       them (ns) */
    double phaseMin, phaseMax;
    PaceLock L;
} Run;

static Run run(Fake *f, const Opt *o)
{
    Run r;
    PaceLock *L = &r.L;
    const bool shown = o->clockKind == PACE_CLOCK_KIND_SHOWN;
    const PaceLockIo io = {f,
                           f_now,
                           f_present,
                           f_sleep,
                           f_visible,
                           shown ? f_present_id : NULL,
                           shown ? f_shown : NULL,
                           o->clockKind == PACE_CLOCK_KIND_VBLANK ? f_vblank : NULL,
                           o->noGuard ? f_waited_none : NULL,
                           o->budget ? f_display_waited : NULL};
    const uint64_t start = f->now, end = start + (uint64_t)(o->seconds * 1e9);
    const uint64_t ratioAt = start + (uint64_t)((o->seconds - 10.0) * 1e9);
    const uint64_t phaseAt = start + (uint64_t)(o->phaseFrom * 1e9);
    unsigned vsync = 0, gapsAtFrame = 0, skippedAt = 0, waitedAt = 0, resyncsAt = 0;
    unsigned presentsAt = 0, stepsAt = 0;
    bool newFrame = true, ratioStarted = false, counting = false, lastBig = false;

    memset(&r, 0, sizeof r);
    r.phaseMin = 1e18;
    r.phaseMax = -1e18;
    f->vbOn = o->clockKind == PACE_CLOCK_KIND_VBLANK;
    f->report = o->report;
    pace_lock_reset(L, o->n, o->report, NTSC, f->now, !o->fifo, o->clockKind);
    f->vbDone = f_vb_index(f, f->now);
    f->vbIdx = f->vbDone;
    if (f->countFrom == 0) {
        f->countFrom = start + SEC;
    }
    while (f->now < end) {
        const uint64_t before = L->due;
        const double periodBefore = L->slotNs * o->n;
        const unsigned resyncs = L->resyncs;

        f->vsync = vsync;
        if (f->rebuildAt != 0 && vsync == f->rebuildAt) {
            /* the swapchain made again: an empty queue */
            f->now += f->rebuildNs;
            f_drain(f);
            f->queued = 0;
            f->nvCount = 0;
        }
        if (newFrame) {
            /* the frame that just closed: its alphas were 1 / (2n) .. 1 */
            const unsigned gaps = L->skipped + L->unshown + L->skippedLate + L->budget.refused;

            for (unsigned i = 0; i < f->nAlpha && vsync > 4; i++) {
                /* on the grid whatever was left out: k / (2n) */
                const float x = f->alpha[i] * (float)(2 * o->n);

                if (x < 1.0f || x != (float)(unsigned)x ||
                    (i > 0 && !(f->alpha[i] > f->alpha[i - 1]))) {
                    r.alphaOffGrid++;
                }
            }
            for (unsigned i = 0; i < f->nAlpha && vsync > 4 && gaps == gapsAtFrame; i++) {
                r.alphaChecked++;
                if (f->alpha[i] != pace_lock_alpha(i + 1, o->n, 2) ||
                    (i > 0 && !(f->alpha[i] > f->alpha[i - 1]))) {
                    r.alphaBad++;
                }
            }
            f->nAlpha = 0;
            gapsAtFrame = gaps;
        }
        if (!ratioStarted && f->now >= ratioAt) {
            ratioStarted = true;
            (void)pace_lock_rate_ratio(L); /* the block starts */
        }
        if (!counting && f->now >= f->countFrom) {
            counting = true;
            skippedAt = L->skipped;
            waitedAt = L->waited + f->waitedSeen;
            resyncsAt = L->resyncs;
            presentsAt = f->presents;
            stepsAt = L->steps;
        }
        if (o->budget) {
            L->gpuNs = o->gpuNs;
            L->stepNs = o->step;
            /* the window's step profile: the step just run waited this long
               for the disc (window_disc.c window_disc_wait_ns) */
            L->discWaitNs = lastBig && o->bigDisc ? o->bigStep : 0;
        }
        const uint64_t due = pace_lock_vsync(L, &io, newFrame);
        const double dev = (double)due - (double)before - periodBefore;
        const uint64_t late = f->now - due;

        if (counting && late > r.lateMaxFrom) {
            r.lateMaxFrom = late;
        }
        if (o->phaseFrom > 0.0 && due >= phaseAt) {
            /* the due time's offset after the vblank before it */
            const double ph = (double)due - (double)f_vb(f, f_vb_index(f, due));

            r.phaseMin = ph < r.phaseMin ? ph : r.phaseMin;
            r.phaseMax = ph > r.phaseMax ? ph : r.phaseMax;
        }

        vsync++;
        if (L->resyncs == resyncs && vsync > 1) {
            const double a = dev < 0 ? -dev : dev;

            if ((uint64_t)a > r.gridDevMax) {
                r.gridDevMax = (uint64_t)a;
            }
        }
        if (late > r.lateMax) {
            r.lateMax = late;
        }
        if (late >= MS / 2) {
            r.lateSteps++;
        }
        lastBig = vsync == o->bigAt || (o->bigEvery != 0 && vsync % o->bigEvery == 0);
        if (f->readsEnd > f->now) {
            /* the step's first renderer call waits for the picture's reads
               (rd_thread.h rd__texlock_wait) */
            if (counting) {
                f->readWaits++;
                f->readWaitNs += f->readsEnd - f->now;
            }
            f->now = f->readsEnd;
        }
        f->now += lastBig ? o->bigStep : o->step + (o->stepJitter ? f_rand(f) % o->stepJitter : 0);
        if (f->room != 0 && counting && f->now > due + (uint64_t)(L->slotNs * (double)o->n)) {
            f->periodMisses++; /* past the next step's due time */
        }
        newFrame = vsync % 2 == 0; /* a frame every two steps */
    }
    f_display_to(f, f->now);
    r.steps = L->steps;
    r.presents = f->presents;
    r.skipped = L->skipped;
    r.unshown = L->unshown;
    r.resyncs = L->resyncs;
    r.repeats = f->repeats;
    r.drops = f->drops;
    r.ratio = pace_lock_rate_ratio(L);
    r.skippedFrom = L->skipped - skippedAt;
    r.waitedFrom = L->waited + f->waitedSeen - waitedAt;
    r.resyncsFrom = L->resyncs - resyncsAt;
    r.presentsFrom = f->presents - presentsAt;
    r.stepsFrom = L->steps - stepsAt;
    return r;
}

static Fake fake(double display)
{
    Fake f;

    memset(&f, 0, sizeof f);
    f.mode = F_MAILBOX;
    f.now = 5 * SEC;
    f.phase = 5 * SEC - 13 * MS; /* the display's own phase (before the start) */
    f.display = display;
    f.cost = 2 * MS;
    f.gpu = 5 * MS;
    f.wake = 30000ull;
    f.wakeJitter = 100000ull;
    f.rng = 12345;
    return f;
}

static Opt opt(unsigned n, uint64_t report, double seconds)
{
    Opt o;

    memset(&o, 0, sizeof o);
    o.n = n;
    o.report = report;
    o.step = 3 * MS;
    o.seconds = seconds;
    return o;
}

/* 60.00 Hz (16.6667 ms) under the 59.94 simulation: the grid on the
   report, mailbox, without and with the display clock */
static void test60(void)
{
    for (int clock = 0; clock < 2; clock++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 60.0);

        o.clockKind = clock ? PACE_CLOCK_KIND_SHOWN : PACE_CLOCK_KIND_NONE;
        o.budget = true; /* never late: the allowance never binds */
        const Run r = run(&f, &o);

        printf("60 Hz%s: %u presents, %u steps, late max %.3f ms, grid off by %.3f ms at most, "
               "%u repeated and %u dropped, ratio %.6f, %u refreshes timed\n",
               clock ? " (clock)" : "", r.presents, r.steps, (double)r.lateMax / 1e6,
               (double)r.gridDevMax / 1e6, r.repeats, r.drops, r.ratio, r.L.seen);
        CHECK(r.lateMax == 0);
        CHECK(r.gridDevMax <= (clock ? 20001u : 1u)); /* a period, and a nudge at most */
        CHECK(r.presents == r.steps);                 /* one present a step */
        CHECK(r.steps >= 3599 && r.steps <= 3601);
        CHECK(r.alphaBad == 0 && r.alphaChecked >= 3590);
        CHECK(r.repeats == 0 && r.drops == 0);
        CHECK(r.skipped == 0 && r.resyncs == 0);
        /* the audio plays 60 / 59.94 faster */
        CHECK(r.ratio > 1.000951 && r.ratio < 1.001051);
        CHECK(!clock || (r.L.clock == PACE_CLOCK_ON && r.L.seen > 3000));
        CHECK(r.L.budget.allow == 1 && r.L.budget.drops == 0 && r.L.budget.refused == 0);
    }
}

/* 59.94 Hz: the simulation at its own rate, ratio 1 */
static void test5994(void)
{
    Fake f = fake(1e9 * 1001.0 / 60000.0);
    Opt o = opt(1, 16683333ull, 30.0);
    const Run r = run(&f, &o);

    CHECK(r.lateMax == 0 && r.repeats == 0 && r.drops == 0);
    CHECK(r.ratio > 0.99995 && r.ratio < 1.00005);
}

/* NVIDIA's FIFO return pattern: two quick presents, then a slow one */
static void testRc3Pattern(void)
{
    /* the logged returns (ms since the one before): a lock stepping on
       each would put its steps as far apart */
    static const double trace[] = {8.85, 31.71, 9.43, 8.88, 31.92, 9.27, 8.85};
    double lo = 1e9, hi = 0;

    for (unsigned i = 0; i < sizeof trace / sizeof trace[0]; i++) {
        lo = trace[i] < lo ? trace[i] : lo;
        hi = trace[i] > hi ? trace[i] : hi;
    }
    printf("rc3 lock: steps %.2f to %.2f ms apart (the grid: 16.67)\n", lo, hi);
    CHECK(hi - lo > 20.0);

    static const uint64_t fits[] = {900000ull, 1200000ull, 13000000ull};
    static const uint64_t over[] = {900000ull, 1200000ull, 25000000ull};

    for (int clock = 0; clock < 2; clock++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 60.0);
        Run r;

        o.step = 2 * MS;
        o.clockKind = clock ? PACE_CLOCK_KIND_SHOWN : PACE_CLOCK_KIND_NONE;
        f.costs = fits;
        f.nCosts = 3;
        r = run(&f, &o);
        printf("presents of 0.9, 1.2, 13 ms%s: late max %.3f ms, grid off by %.3f ms, %u "
               "presents, %u steps\n",
               clock ? " (clock)" : "", (double)r.lateMax / 1e6, (double)r.gridDevMax / 1e6,
               r.presents, r.steps);
        CHECK(r.lateMax == 0);
        CHECK(r.gridDevMax <= (clock ? 20001u : 1u));
        CHECK(r.presents == r.steps);
        CHECK(r.steps >= 3599 && r.steps <= 3601);
        CHECK(r.alphaBad == 0 && r.alphaChecked >= 3590);

        /* a 25 ms present: the step after it starts late, the grid stays */
        f = fake(D60);
        f.costs = over;
        f.nCosts = 3;
        r = run(&f, &o);
        printf("presents of 0.9, 1.2, 25 ms%s: %u steps late (max %.3f ms), grid off by %.3f "
               "ms, %u steps, %u resyncs\n",
               clock ? " (clock)" : "", r.lateSteps, (double)r.lateMax / 1e6,
               (double)r.gridDevMax / 1e6, r.steps, r.resyncs);
        CHECK(r.lateSteps >= 1190 && r.lateSteps <= 1210); /* every third */
        CHECK(r.lateMax < 11 * MS);                        /* 2 + 25 - 16.67 */
        CHECK(r.gridDevMax <= (clock ? 20001u : 1u));
        CHECK(r.steps >= 3599 && r.steps <= 3601);
        CHECK(r.resyncs == 0);
        CHECK(r.presents == r.steps);
    }
}

/* 120 Hz: two presents a step, alphas 1/4, 2/4, 3/4, 4/4 */
static void test120(void)
{
    for (int clock = 0; clock < 2; clock++) {
        Fake f = fake(1e9 / 120.0);
        Opt o = opt(2, 8333333ull, 60.0);
        Run r;

        f.cost = 1 * MS;
        f.gpu = 3 * MS;
        o.clockKind = clock ? PACE_CLOCK_KIND_SHOWN : PACE_CLOCK_KIND_NONE;
        o.budget = true; /* never late: the allowance stays 2 */
        o.gpuNs = 3 * MS;
        r = run(&f, &o);
        printf("120 Hz%s: %u presents, %u steps, late max %.3f ms, %u repeated, %u dropped\n",
               clock ? " (clock)" : "", r.presents, r.steps, (double)r.lateMax / 1e6, r.repeats,
               r.drops);
        CHECK(r.lateMax == 0);
        CHECK(r.presents == 2 * r.steps);
        CHECK(r.steps >= 3599 && r.steps <= 3601);
        CHECK(r.alphaBad == 0 && r.alphaChecked >= 7180);
        CHECK(r.repeats == 0 && r.drops == 0);
        CHECK(r.L.budget.allow == 2 && r.L.budget.drops == 0 && r.L.budget.refused == 0);
    }
    CHECK(pace_lock_alpha(1, 2, 2) == 0.25f && pace_lock_alpha(4, 2, 2) == 1.0f);
}

/* The present allowance under the lock with the fence: a replay waits for
   the GPU's picture two before (RHI_FRAMES_IN_FLIGHT 2) and the GPU draws
   one picture at a time.  120 Hz, a 1 ms present, a 3 ms step and 12 ms of
   GPU a picture: two presents a step need 24 ms of GPU in 16.7.  The rule
   before: the second slot's present waits for the GPU, the steps end up to
   20 ms late and the skip guard drops a present now and then (1.4 a step).
   With the allowance: after the first second one present a step, no
   resync, the alphas on the grid, the allowance at 1.  The lock's climb is
   a probe (v0.9.6, issue 126): 2, 4, 8 then 16 s apart it tries two a step
   again, the GPU's backlog makes a step late within the probe's 60 steps
   and it drops back to 1, so a step is late by less than a period, a few
   times a minute, and the presents stay about one a step.  A GPU-bound
   phone (20 ms a picture at 60 Hz, n = 1): the rule before presents 50 a
   second with steps 32 ms late; the allowance about its new frames only,
   30 a second and under 33 with the probes' pictures, no step a period
   late.  The numbers printed are the trade. */
static void test120Budget(void)
{
    for (int on = 0; on < 2; on++) {
        Fake f = fake(1e9 / 120.0);
        Opt o = opt(2, 8333333ull, 60.0);
        Run r;

        f.fence = 1;
        f.cost = 1 * MS;
        f.gpu = 12 * MS;
        o.budget = on != 0;
        o.gpuNs = 12 * MS;
        r = run(&f, &o);
        printf("120 Hz, GPU 12 ms a picture%s: %.2f presents a step after 1 s, latest step %.3f "
               "ms, %u skipped, %u refused, allowance %u (%u drops, %u climbs: probes), %u "
               "resyncs\n",
               on ? " with the allowance" : "", (double)r.presentsFrom / (double)r.stepsFrom,
               (double)r.lateMaxFrom / 1e6, r.skippedFrom, r.L.budget.refused, r.L.budget.allow,
               r.L.budget.drops, r.L.budget.climbs, r.resyncs);
        CHECK(r.resyncsFrom == 0);
        CHECK(r.alphaOffGrid == 0);
        CHECK(r.L.skippedLate == 0); /* every new frame presented */
        if (on) {
            /* a failed probe's late step: under a period */
            CHECK(r.lateMaxFrom < NTSC);
            CHECK(r.presentsFrom * 10 >= r.stepsFrom * 9 &&
                  r.presentsFrom * 10 <= r.stepsFrom * 11);
            CHECK(r.L.budget.allow == 1 && r.L.budget.drops >= 2 && r.L.budget.refused > 0);
            CHECK(r.L.budget.climbs >= 1 && r.L.budget.climbRefusals == 0);
            CHECK(r.skippedFrom == 0);
        } else {
            CHECK(r.lateMaxFrom >= 8333333ull / 2);
            CHECK(r.skippedFrom > 0 && r.L.budget.drops == 0);
        }
    }
    for (int on = 0; on < 2; on++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        f.fence = 1;
        f.cost = 1 * MS;
        f.gpu = 20 * MS;
        o.budget = on != 0;
        o.gpuNs = 20 * MS;
        r = run(&f, &o);
        printf("a phone, GPU 20 ms a picture at 60 Hz%s: %.1f presents a second, latest step "
               "%.3f ms after 1 s, allowance %u, %u resyncs\n",
               on ? " with the allowance" : "", (double)r.presentsFrom / 29.0,
               (double)r.lateMaxFrom / 1e6, r.L.budget.allow, r.resyncs);
        CHECK(r.resyncs == 0);
        CHECK(r.alphaOffGrid == 0 && r.L.skippedLate == 0);
        if (on) {
            /* the new frames at least and under 33 a second with the probes */
            CHECK(r.lateMaxFrom < NTSC);
            CHECK(r.L.budget.allow == 0 && r.L.budget.climbs >= 1);
            CHECK(r.L.budget.climbRefusals == 0);
            CHECK(r.presentsFrom * 2 >= r.stepsFrom - 2 && r.presentsFrom * 29 <= r.stepsFrom * 16);
        } else {
            CHECK(r.lateMaxFrom >= NTSC / 2);
        }
    }
}

/* Presents that finish near a vblank: the GPU's time jitters by 3 ms.
   Without the clock the grid stays where it starts, and where that puts
   the presents' ends around a vblank pictures repeat and drop for as long
   as it runs; with it the grid moves away from the vblank.  16 starting
   phases, two loads: a 2 ms present and 8 ms of GPU; and 4K's (a logged
   RTX 3090 run: a 6 ms replay and 11 ms of GPU, a picture ready more than a refresh
   after its step). */
static void testPhase(void)
{
    unsigned without = 0, withLast = 0, phases = 0;

    for (unsigned p = 0; p < 32; p++) {
        for (int clock = 0; clock < 2; clock++) {
            Fake f = fake(D60);
            Opt o = opt(1, R60, 60.0);

            f.phase = 5 * SEC - (uint64_t)((double)(p % 16) * D60 / 16.0);
            f.cost = p < 16 ? 2 * MS : 6 * MS;
            f.gpu = p < 16 ? 8 * MS : 11 * MS;
            f.gpuJitter = 3 * MS;
            o.stepJitter = p < 16 ? 0 : 2 * MS;
            o.clockKind = clock ? PACE_CLOCK_KIND_SHOWN : PACE_CLOCK_KIND_NONE;
            if (clock) {
                f.countFrom = 5 * SEC + 30 * SEC; /* the last 30 s */
            }
            const Run r = run(&f, &o);

            if (clock) {
                withLast += r.repeats + r.drops;
                if (r.repeats + r.drops) {
                    printf("  load %u phase %u/16 with the clock: %u repeated, %u dropped in the "
                           "last 30 s (%u late, %u early)\n",
                           p / 16, p % 16, r.repeats, r.drops, r.L.late, r.L.early);
                }
                CHECK(r.gridDevMax <= 20001u);
                CHECK(r.lateMax == 0);
                CHECK(r.alphaBad == 0);
            } else {
                without += r.repeats + r.drops;
                phases += r.repeats + r.drops > 0;
            }
        }
    }
    printf("presents near a vblank, 2 loads x 16 phases x 60 s: %u repeated or dropped without "
           "the clock (%u runs), %u with it over the last 30 s\n",
           without, phases, withLast);
    CHECK(phases >= 6);
    CHECK(without > 2000);
    CHECK(withLast == 0);
}

/* Without the display clock the lock presents through FIFO: the grid's
   phase against the vblanks is whatever it starts at, and under mailbox a
   phase that puts the presents' ends around a vblank repeats and drops
   pictures for the whole session; FIFO shows them in order, a refresh
   later where one is late, so only the display's drift slips a picture
   (50 ppm: one every 5.5 minutes).  Two loads (a 3 +- 1 ms step, 2 ms
   presents, 6 +- 1 ms GPU; and 6 ms presents, 11 +- 1.5 ms GPU) at 16
   phases, 50 ppm both ways, 5 minutes each, counted after the first. */
static void testNoClockMode(void)
{
    static const uint64_t cost[] = {2 * MS, 6 * MS}, gpu[] = {6 * MS, 11 * MS},
                          gj[] = {2 * MS, 3 * MS};
    static const double ppm[] = {50.0, -50.0};
    unsigned mbRuns = 0, mbEvents = 0, fifoRuns = 0, fifoEvents = 0, fifoMax = 0;

    for (unsigned l = 0; l < 2; l++) {
        for (unsigned a = 0; a < 2; a++) {
            for (unsigned p = 0; p < 16; p++) {
                for (int fifo = 0; fifo < 2; fifo++) {
                    Fake f = fake(D60 * (1.0 - ppm[a] * 1e-6));
                    Opt o = opt(1, R60, 300.0);

                    f.mode = fifo ? F_FIFO : F_MAILBOX;
                    f.phase = 5 * SEC - (uint64_t)((double)p * D60 / 16.0);
                    f.cost = cost[l];
                    f.gpu = gpu[l];
                    f.gpuJitter = gj[l];
                    f.rng = 1000u + p * 7u + l;
                    f.countFrom = 5 * SEC + 60 * SEC;
                    o.stepJitter = 2 * MS;
                    o.fifo = fifo != 0;
                    const Run r = run(&f, &o);
                    const unsigned ev = r.repeats + r.drops;

                    CHECK(r.lateMax < MS / 2 || fifo);
                    CHECK(r.gridDevMax <= 1u);
                    if (fifo) {
                        fifoRuns += ev > 0;
                        fifoEvents += ev;
                        fifoMax = ev > fifoMax ? ev : fifoMax;
                    } else {
                        mbRuns += ev > 0;
                        mbEvents += ev;
                    }
                }
            }
        }
    }
    printf("no display clock, 64 runs of 5 min: mailbox %u runs with %u pictures repeated or "
           "dropped, FIFO %u runs with %u (at most %u in one)\n",
           mbRuns, mbEvents, fifoRuns, fifoEvents, fifoMax);
    CHECK(mbRuns >= 16 && mbEvents > 10000);
    CHECK(fifoMax <= 4);
}

/* A display 50 ppm faster than its report (a crystal off): without the
   clock the grid slides across the vblanks, with it the slot follows */
static void testDrift(void)
{
    const double d = D60 * (1.0 - 50e-6);
    Fake f = fake(d);
    Opt o = opt(1, R60, 600.0);
    Run r;

    f.gpu = 6 * MS;
    f.gpuJitter = 1 * MS;
    r = run(&f, &o);
    printf("a display 50 ppm fast, 10 min without the clock: %u repeated, %u dropped\n", r.repeats,
           r.drops);
    CHECK(r.repeats + r.drops > 0);
    f = fake(d);
    f.gpu = 6 * MS;
    f.gpuJitter = 1 * MS;
    f.countFrom = 5 * SEC + 10 * SEC;
    o.clockKind = PACE_CLOCK_KIND_SHOWN;
    r = run(&f, &o);
    printf("with the clock: %u repeated, %u dropped after 10 s, slot %.3f ns (display %.3f), "
           "phase moved %.3f ms, %u refreshes timed\n",
           r.repeats, r.drops, r.L.slotNs, d, r.L.nudgeSum / 1e6, r.L.seen);
    CHECK(r.repeats + r.drops == 0);
    CHECK(r.L.slotNs > d - 50.0 && r.L.slotNs < d + 50.0); /* 3 ppm: the wake-ups' noise */
    CHECK(r.L.clock == PACE_CLOCK_ON);
    CHECK(r.lateMax == 0);
}

static const char *rule_name(int kind)
{
    return kind == 0 ? "the rule before the guard" : kind == 1 ? "the guard" : "the vblank clock";
}

/* The FIFO queue against a display whose true refresh is not the report
   (Windows reports 59.951 Hz for a 59.940 Hz scan-out, +165 ppm
   against 16.6806 ms).  A grid shorter than the scan-out fills the queue:
   every present then waits for a vblank, the steps start later and later,
   and a present is skipped (a picture never made: a jump in motion); a
   longer one drains it: the display shows a picture twice.  No rule about
   skips can take a rate difference away (the guard alone only moves the
   skip a slot later); the vblank clock measures the refresh, the slot
   follows it and the steps sit a sixteenth of a refresh after a vblank:
   nothing fills or drains.  10 minutes each, counted from 60 s (with the
   clock from 10 s).  At +50 ppm the queue does not fill far enough to
   skip within 10 minutes; it waits. */
static void testFifoDrift(void)
{
    static const double disp[] = {1e9 / 59.94,        1e9 / 60.0,           16680600.0 * 1.001,
                                  16680600.0 * 0.999, 16680600.0 * 1.00005, 16680600.0 * 0.99995};

    for (unsigned i = 0; i < sizeof disp / sizeof disp[0]; i++) {
        const double ppm = pace_ppm(disp[i], 16680600.0);

        for (int kind = 0; kind < 3; kind++) {
            Fake f = fake(disp[i]);
            Opt o = opt(1, 16680600ull, 600.0);

            f.mode = F_FIFO;
            f.gpu = 6 * MS;
            f.gpuJitter = 1 * MS;
            f.countFrom = 5 * SEC + (kind == 2 ? 10 : 60) * SEC;
            o.fifo = true;
            o.noGuard = kind == 0;
            o.clockKind = kind == 2 ? PACE_CLOCK_KIND_VBLANK : PACE_CLOCK_KIND_NONE;
            const Run r = run(&f, &o);

            printf("fifo, display %.4f ms (%+.0f ppm against the report), %s: %u skipped, %u "
                   "repeated, %u dropped, %u waited, late max %.3f ms; the slot %+.2f ppm off "
                   "the display\n",
                   disp[i] / 1e6, ppm, rule_name(kind), r.skippedFrom, r.repeats, r.drops,
                   r.waitedFrom, (double)r.lateMaxFrom / 1e6, pace_ppm(r.L.slotNs, disp[i]));
            CHECK(r.resyncs == 0);
            if (kind == 2) {
                const double off = pace_ppm(r.L.slotNs, disp[i]);

                CHECK(r.L.clock == PACE_CLOCK_ON);
                CHECK(r.skippedFrom == 0 && r.repeats == 0 && r.drops == 0);
                CHECK(r.waitedFrom == 0);
                CHECK(off > -3.0 && off < 3.0);
                CHECK(r.lateMax == 0);
            } else if (ppm > 0.0) {
                /* fills: presents wait, and past 100 ppm the steps fall a
                   slot behind and skip */
                CHECK(r.waitedFrom > 0 && r.repeats == 0);
                CHECK(ppm < 100.0 || r.skippedFrom >= 1);
            } else {
                /* drains: pictures shown twice, nothing skipped */
                CHECK(r.repeats >= 1 && r.skippedFrom == 0 && r.waitedFrom == 0);
            }
        }
    }
}

/* Presents that finish near a vblank on a display on its report: the
   GPU's time 6 +- 3 ms.  At some phases a picture finishes just after its
   vblank and is shown a refresh later (the display shows the one before
   twice), and so is every picture after it (the ratchet: the queue holds
   one more).  A GPU hitch (12 ms more every 450 pictures, a pan) ratchets
   again and the queue fills: the presents wait for the display.  With the
   vblank clock the steps sit a sixteenth of a refresh after a vblank
   (slot / 16 +- 150 us after 5 s: the clock's wake-up is 30 to 130 us), a
   picture finishes before the next one, and nothing repeats or waits
   (without hitches; a hitch still shows its picture late once, which no
   pacing hides).  16 phases, 60 s. */
static void testFifoRatchet(void)
{
    unsigned oldRepeatRuns = 0, oldWaitRuns = 0;

    for (int hitch = 0; hitch < 2; hitch++) {
        for (unsigned p = 0; p < 16; p++) {
            for (int kind = 0; kind < 3; kind += 2) {
                Fake f = fake(D60);
                Opt o = opt(1, R60, 60.0);

                f.mode = F_FIFO;
                f.phase = 5 * SEC - (uint64_t)((double)p * D60 / 16.0);
                f.gpu = 3 * MS;
                f.gpuJitter = 6 * MS;
                f.rng = 777u + p;
                f.spikeEvery = hitch ? 450 : 0;
                f.spike = 12 * MS;
                o.fifo = true;
                o.stepJitter = 1 * MS;
                o.noGuard = kind == 0;
                o.clockKind = kind == 2 ? PACE_CLOCK_KIND_VBLANK : PACE_CLOCK_KIND_NONE;
                o.phaseFrom = kind == 2 ? 5.0 : 0.0;
                const Run r = run(&f, &o);
                const unsigned waited = r.L.waited + f.waitedSeen;

                if (kind == 0) {
                    oldRepeatRuns += !hitch && r.repeats + r.drops > 0;
                    oldWaitRuns += hitch && waited > 0;
                    continue;
                }
                CHECK(r.skipped == 0 && waited == 0 && r.lateMax == 0);
                CHECK(hitch || (r.repeats == 0 && r.drops == 0));
                CHECK(r.phaseMin > D60 / 16.0 - 150000.0 && r.phaseMax < D60 / 16.0 + 150000.0);
                if (p == 0 && !hitch) {
                    printf("the ratchet with the vblank clock: the steps %.3f to %.3f ms after a "
                           "vblank (a sixteenth: %.3f)\n",
                           r.phaseMin / 1e6, r.phaseMax / 1e6, D60 / 16e6);
                }
            }
        }
    }
    printf("the ratchet without the clock, 16 phases x 60 s: %u with pictures shown late, %u "
           "with presents that waited for the display after GPU hitches\n",
           oldRepeatRuns, oldWaitRuns);
    CHECK(oldRepeatRuns >= 1);
    CHECK(oldWaitRuns >= 1);
}

/* D3D12's model: a queue the display drains one a vblank, full from the
   start (Present blocking 10-11 ms on average in Windows logs), the
   display 163 ppm slower than the report, 10 minutes.  Without the clock
   every present waits and the steps fall behind until one skips (both
   rules: the rate differs); with it the slot is the display's, the steps
   a sixteenth of a refresh after a vblank find the queue one picture
   short of full, and nothing waits, at a depth of 3 or 2. */
static void testQueueBlocking(void)
{
    for (unsigned depth = 2; depth <= 3; depth++) {
        for (int kind = 0; kind < 3; kind++) {
            Fake f = fake(16680600.0 * (1.0 + 163e-6));
            Opt o = opt(1, 16680600ull, 600.0);

            f.mode = F_QUEUE;
            f.depth = depth;
            f.queued = depth;
            f.cost = 2 * MS;
            f.jitterMax = 100000ull;
            f.countFrom = 5 * SEC + 10 * SEC;
            o.step = 2 * MS;
            o.stepJitter = 1 * MS;
            o.fifo = true;
            o.noGuard = kind == 0;
            o.clockKind = kind == 2 ? PACE_CLOCK_KIND_VBLANK : PACE_CLOCK_KIND_NONE;
            const Run r = run(&f, &o);
            const double off = pace_ppm(r.L.slotNs, f.display);

            printf("queue of %u, display +163 ppm, %s: %u skipped, %u of %u presents waited, "
                   "late max %.3f ms, the slot %+.2f ppm off the display\n",
                   depth, rule_name(kind), r.skippedFrom, r.waitedFrom, r.presents,
                   (double)r.lateMaxFrom / 1e6, off);
            CHECK(r.resyncs == 0);
            if (kind == 2) {
                CHECK(r.skippedFrom == 0 && r.waitedFrom == 0 && r.lateMaxFrom == 0);
                CHECK(off > -3.0 && off < 3.0);
            } else {
                CHECK(r.skippedFrom >= 1);
                CHECK(r.waitedFrom * 10 >= r.presents * 9); /* nearly every present */
            }
        }
    }
}

/* The skip guard.  A present that waited for the display (a FIFO
   present blocking to the vblank after it) while the step is less than a
   slot behind is not followed by a skip: with 8 ms steps at the phases
   where the vblank falls just before the present's end, the rule before
   the guard skipped every other present; the guard none.  A step or a
   GPU a slot or more behind still skips: a 30 ms step with the vblank
   clock (testLongSteps' numbers), and a GPU slower than the refresh (22
   ms a picture, NVIDIA's fence waits), where without the bound the game
   ran at the GPU's rate and dropped its lag every 6 periods.  The resync
   past PACE_LOCK_DEBT_VSYNCS is unchanged. */
static void testSkipGuard(void)
{
    unsigned oldHalf = 0;

    for (unsigned p = 0; p < 8; p++) {
        for (int kind = 0; kind < 2; kind++) {
            Fake f = fake(D60);
            Opt o = opt(1, R60, 30.0);

            f.mode = F_BLOCK;
            f.phase = 5 * SEC - (uint64_t)((double)p * D60 / 8.0);
            o.step = 8 * MS;
            o.stepJitter = 2 * MS;
            o.fifo = true;
            o.noGuard = kind == 0;
            const Run r = run(&f, &o);

            if (kind == 0) {
                oldHalf += r.skipped * 3 > r.steps; /* a third or more skipped */
            } else {
                CHECK(r.skipped == 0 && r.presents == r.steps && r.resyncs == 0);
                CHECK(r.lateMax < R60);
            }
        }
    }
    printf("8 ms steps, presents blocking to a vblank, 8 phases: %u where the rule before the "
           "guard skipped a third of the presents or more, none with the guard\n",
           oldHalf);
    CHECK(oldHalf >= 2);
    {
        /* a 30 ms step with the vblank clock */
        Fake f = fake(D60);
        Opt o = opt(1, R60, 20.0);
        Run r;

        f.mode = F_FIFO;
        o.fifo = true;
        o.clockKind = PACE_CLOCK_KIND_VBLANK;
        o.bigAt = 600;
        o.bigStep = 30 * MS;
        r = run(&f, &o);
        printf("a 30 ms step with the vblank clock: %u late (max %.3f ms), %u resyncs, %u "
               "skipped\n",
               r.lateSteps, (double)r.lateMax / 1e6, r.resyncs, r.skipped);
        CHECK(r.resyncs == 0 && r.lateSteps == 2 && r.lateMax < 16 * MS);
        CHECK(r.skipped >= 1);
        CHECK(r.steps >= 1199 && r.steps <= 1201);
        CHECK(r.L.clock == PACE_CLOCK_ON);
        /* a 300 ms step: the lag dropped, as without the clock */
        f = fake(D60);
        f.mode = F_FIFO;
        o.bigStep = 300 * MS;
        r = run(&f, &o);
        CHECK(r.resyncs == 1);
        CHECK(r.L.droppedNs > 280 * MS && r.L.droppedNs < 290 * MS);
        CHECK(r.lateSteps <= 1);
    }
    for (int kind = 0; kind < 2; kind++) {
        /* a GPU slower than the refresh: the game keeps its speed */
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        f.mode = F_NV;
        f.pre = 450000ull;
        f.tail = 3600000ull;
        f.gpuNv = 22 * MS;
        o.step = 2 * MS;
        o.fifo = true;
        o.noGuard = kind == 0;
        r = run(&f, &o);
        printf("a GPU of 22 ms a picture, %s: %u steps, %u presents, %u skipped, %u resyncs\n",
               rule_name(kind), r.steps, r.presents, r.skipped, r.resyncs);
        CHECK(r.steps >= 1799 && r.steps <= 1801);
        CHECK(r.resyncs == 0);
        CHECK(r.presents >= 1300 && r.presents <= 1400);
    }
}

/* the late-present skip under the lock (pace_late_skip, the timer path's
   rule): which frames were presented, the frame closing at each even
   vsync (testLateSkipLock) */
static unsigned s_trackFrame;
static unsigned char s_trackShown[2048];

static bool f_present_track(void *u, float a)
{
    s_trackShown[s_trackFrame] = 1;
    return f_present(u, a);
}

/* A 30 frames a second game whose presents take 39 ms (a GPU-bound phone
   at 2x on a 60 Hz panel, where Android's default framerate 60 takes the
   lock; each step 2.4 ms) keeps real time under the lock: new frames left
   unpresented while the next step is a whole period late (skippedLate),
   never two in a row, more than half the frames presented, no lag dropped
   (the rule before presented every new frame: 43.8 ms a frame against
   33.4, a resync every few frames).  Presents of 2 ms never skip. */
static void testLateSkipLock(void)
{
    for (int slow = 0; slow < 2; slow++) {
        Fake f = fake(D60);
        PaceLock L;
        const PaceLockIo io = {&f,   f_now, f_present_track, f_sleep, NULL, NULL, NULL, NULL,
                               NULL, NULL};
        unsigned frames = 0, run = 0, worst = 0, shown = 0;

        f.cost = slow ? 39 * MS : 2 * MS;
        f.gpu = 1 * MS;
        memset(s_trackShown, 0, sizeof s_trackShown);
        pace_lock_reset(&L, 1, R60, NTSC, f.now, true, PACE_CLOCK_KIND_NONE);
        for (unsigned v = 0; v < 1200; v++) {
            const bool close = v % 2 == 0;

            f.now += 2400000ull; /* the step */
            if (close) {
                s_trackFrame = ++frames;
            }
            (void)pace_lock_vsync(&L, &io, close);
        }
        /* the last frame may still be due: not counted */
        for (unsigned k = 1; k < frames; k++) {
            shown += s_trackShown[k];
            run = s_trackShown[k] ? 0 : run + 1;
            worst = run > worst ? run : worst;
        }
        printf("presents of %s: %u of %u frames presented, %u late skips, at most %u in a row "
               "unpresented, %u resyncs\n",
               slow ? "39 ms" : "2 ms", shown, frames - 1, L.skippedLate, worst, L.resyncs);
        if (slow) {
            CHECK(L.skippedLate > 0);
            CHECK(L.resyncs == 0);
            CHECK(worst == 1);
            CHECK(shown * 2 > frames - 1 && shown < frames - 1);
        } else {
            CHECK(L.skippedLate == 0 && L.resyncs == 0 && shown == frames - 1);
        }
    }
}

/* Vblank clocks not believed: an output at twice the rate (another
   display), one that stops; and one that misses every 7th wake-up (each
   such interval counts two refreshes): the slot on the display within 3
   ppm and the missed wake-ups counted */
static void testVblankClockLeft(void)
{
    Fake f = fake(D60);
    Opt o = opt(1, R60, 30.0);
    Run r;

    f.mode = F_FIFO;
    f.vbDouble = 1;
    o.fifo = true;
    o.clockKind = PACE_CLOCK_KIND_VBLANK;
    r = run(&f, &o);
    printf("a vblank clock at twice the rate: left %d, slot %.1f ns, measured %.1f ns\n", r.L.clock,
           r.L.slotNs, r.L.measuredNs);
    CHECK(r.L.clock == PACE_CLOCK_LEFT_PERIOD);
    CHECK(r.L.slotNs == (double)R60);
    CHECK(r.lateMax == 0 && r.skipped == 0);

    f = fake(D60);
    f.mode = F_FIFO;
    f.clockFails = 1;
    r = run(&f, &o);
    CHECK(r.L.clock == PACE_CLOCK_LEFT_ERROR);
    CHECK(r.L.slotNs == (double)R60);
    CHECK(r.lateMax == 0 && r.presents == r.steps);

    {
        const double d = D60 * (1.0 - 50e-6);

        f = fake(d);
        f.mode = F_FIFO;
        f.vbDropEvery = 7;
        f.countFrom = 5 * SEC + 10 * SEC;
        o.seconds = 120.0;
        r = run(&f, &o);
        const double off = pace_ppm(r.L.slotNs, d);
        const unsigned missed = r.L.vb.n - r.L.vb.intervals;

        printf("every 7th vblank not noted: the slot %+.2f ppm off the display, %u wake-ups "
               "missed of %u refreshes\n",
               off, missed, r.L.vb.n);
        CHECK(r.L.clock == PACE_CLOCK_ON);
        CHECK(off > -3.0 && off < 3.0);
        CHECK(missed * 7 >= r.L.vb.n - 7 && missed * 7 <= r.L.vb.n + 7);
        CHECK(r.repeats == 0 && r.drops == 0 && r.skippedFrom == 0);
    }
}

/* clocks not believed: 2 % off the report, one that stops */
static void testClockLeft(void)
{
    Fake f = fake(D60 * 1.02);
    Opt o = opt(1, R60, 30.0);
    Run r;

    o.clockKind = PACE_CLOCK_KIND_SHOWN;
    r = run(&f, &o);
    printf("a clock 2 %% off: left %d, slot %.1f ns\n", r.L.clock, r.L.slotNs);
    CHECK(r.L.clock == PACE_CLOCK_LEFT_PERIOD);
    CHECK(r.L.slotNs == (double)R60);
    CHECK(r.lateMax == 0);

    f = fake(D60);
    f.clockFails = 1;
    r = run(&f, &o);
    CHECK(r.L.clock == PACE_CLOCK_LEFT_ERROR);
    CHECK(r.lateMax == 0 && r.presents == r.steps);
}

/* off the screen for 10 s: no presents, the steps go on on the grid */
static void testHidden(void)
{
    Fake f = fake(D60);
    Opt o = opt(1, R60, 30.0);
    Run r;

    f.hiddenFrom = 300;
    f.hiddenTo = 900;
    o.clockKind = PACE_CLOCK_KIND_SHOWN;
    r = run(&f, &o);
    printf("hidden 10 s: %u presents, %u steps, %u not presented, late max %.3f ms\n", r.presents,
           r.steps, r.unshown, (double)r.lateMax / 1e6);
    CHECK(r.unshown == 600);
    CHECK(r.presents == r.steps - 600);
    CHECK(r.steps >= 1799 && r.steps <= 1801);
    CHECK(r.lateMax == 0);
    CHECK(r.gridDevMax <= 20001u);
    CHECK(r.L.clock == PACE_CLOCK_ON);
}

/* one 30 ms step: the two steps after it are late, the next ones on the
   grid; a 300 ms step (a stage load): the lag dropped, the grid from then */
static void testLongSteps(void)
{
    Fake f = fake(D60);
    Opt o = opt(1, R60, 20.0);
    Run r;

    o.bigAt = 600;
    o.bigStep = 30 * MS;
    r = run(&f, &o);
    printf("a 30 ms step: %u late (max %.3f ms), %u resyncs, %u presents skipped\n", r.lateSteps,
           (double)r.lateMax / 1e6, r.resyncs, r.skipped);
    /* the step after it (the new frame presented, 15.3 ms late) and the
       one after that (no present: 2 ms) */
    CHECK(r.resyncs == 0);
    CHECK(r.lateSteps == 2);
    CHECK(r.lateMax < 16 * MS);
    CHECK(r.gridDevMax <= 1u);
    CHECK(r.steps >= 1199 && r.steps <= 1201);
    f = fake(D60);
    o.bigStep = 300 * MS;
    r = run(&f, &o);
    printf("a 300 ms step: %u resyncs, %.0f ms dropped\n", r.resyncs, (double)r.L.droppedNs / 1e6);
    CHECK(r.resyncs == 1);
    CHECK(r.L.droppedNs > 280 * MS && r.L.droppedNs < 290 * MS); /* 300 less a period */
    CHECK(r.lateSteps <= 1);
}

/* The FIFO models of the old lock's tests on a FIFO-only swapchain (D3D12,
   a Vulkan surface without mailbox).  A queue of 2 or 3 images that the
   display drains one a vblank: a present returns at once while there is
   room, so on the display's rate the steps are on the grid. */
static void testFifoModels(void)
{
    static const struct {
        unsigned depth, n;
        double display;
        uint64_t cost;
    } q[] = {{2, 1, D60, 2 * MS}, {3, 1, D60, 2 * MS}, {3, 2, 1e9 / 120.0, 1 * MS}};

    for (unsigned i = 0; i < sizeof q / sizeof q[0]; i++) {
        Fake f = fake(q[i].display);
        Opt o = opt(q[i].n, q[i].n == 1 ? R60 : 8333333ull, 30.0);
        Run r;

        f.mode = F_QUEUE;
        f.depth = q[i].depth;
        f.cost = q[i].cost;
        o.step = 2 * MS;
        o.fifo = true;
        r = run(&f, &o);
        printf("fifo queue %u, %u a step: late max %.3f ms, %u waited, %u presents, %u steps\n",
               q[i].depth, q[i].n, (double)r.lateMax / 1e6, r.L.waited, r.presents, r.steps);
        CHECK(r.lateMax == 0);
        CHECK(r.gridDevMax <= 1u);
        CHECK(r.presents == q[i].n * r.steps);
        CHECK(r.steps >= 1799 && r.steps <= 1801);
        CHECK(r.alphaBad == 0);
        /* a swapchain rebuild (an empty queue) and a 300 ms step change
           nothing */
        f = fake(q[i].display);
        f.mode = F_QUEUE;
        f.depth = q[i].depth;
        f.cost = q[i].cost;
        f.rebuildAt = 900;
        f.rebuildNs = 2 * MS;
        o.bigAt = 1200;
        o.bigStep = 300 * MS;
        r = run(&f, &o);
        CHECK(r.resyncs == 1 && r.lateSteps <= 1 && r.gridDevMax <= 1u);
    }
    /* A present that blocks to the vblank after its replay (the old lock's
       model).  On the display's rate the vblanks keep their place against
       the grid: where one falls in a slot after the present's end the step
       is on time, where it falls before it the present returns past the
       next due time (nothing is held under the lock: the step is late, the
       present counted in waited) */
    {
        unsigned onTime = 0, waited = 0;

        for (unsigned p = 0; p < 8; p++) {
            Fake f = fake(D60);
            Opt o = opt(1, R60, 20.0);

            f.mode = F_BLOCK;
            f.phase = 5 * SEC - (uint64_t)((double)p * D60 / 8.0);
            o.step = 2 * MS;
            o.fifo = true;
            const Run r = run(&f, &o);

            CHECK(r.gridDevMax <= 1u && r.resyncs == 0);
            CHECK(r.steps >= 1199 && r.steps <= 1201);
            CHECK(r.lateMax <= 17 * MS);
            onTime += r.lateSteps == 0;
            waited += r.L.waited > 0;
        }
        printf("fifo blocking, 8 phases: %u with every step on time, %u with presents that "
               "waited\n",
               onTime, waited);
        CHECK(onTime >= 5);
    }
    /* NVIDIA's FIFO with the fence waits (F_NV) and the work rising
       by 5.8 ms over 12 vsyncs (a menu closing): on the grid */
    {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        f.mode = F_NV;
        f.pre = 450000ull;
        f.tail = 3600000ull;
        f.tailJitter = 1000000ull;
        f.gpuNv = 7900000ull;
        f.rampAt = 600;
        f.rampLen = 12;
        f.rampTail = 2700000;
        f.rampGpu = 3100000;
        o.step = 800000ull;
        o.fifo = true;
        r = run(&f, &o);
        printf("fifo nv, a menu closing: late max %.3f ms, %u waited, %u presents, %u steps\n",
               (double)r.lateMax / 1e6, r.L.waited, r.presents, r.steps);
        CHECK(r.lateMax == 0);
        CHECK(r.gridDevMax <= 1u);
        CHECK(r.presents == r.steps);
        CHECK(r.steps >= 1799 && r.steps <= 1801);
    }
}

/* A present that did nothing (a movie on the output): the grid */
static bool f_none(void *u, float a)
{
    (void)u;
    (void)a;
    return false;
}

static void testNothingPresented(void)
{
    Fake f = fake(D60);
    PaceLock L;
    const PaceLockIo io = {&f, f_now, f_none, f_sleep, NULL, NULL, NULL, NULL, NULL, NULL};
    uint64_t due;

    pace_lock_reset(&L, 1, R60, NTSC, f.now, true, PACE_CLOCK_KIND_NONE);
    due = pace_lock_vsync(&L, &io, true);
    CHECK(due == 5 * SEC + R60);
    CHECK(f.now == due);
    f.now += 3 * MS;
    due = pace_lock_vsync(&L, &io, false);
    CHECK(due == 5 * SEC + 2 * R60);
    CHECK(L.presents == 0);
}

/* The first frame after a reset (the lock starting, back from the
   background) is two vsyncs long like the rest: alphas 1/2, 1 */
static void testFirstFrame(void)
{
    Fake f = fake(D60);
    PaceLock L;
    const PaceLockIo io = {&f, f_now, f_present, f_sleep, NULL, NULL, NULL, NULL, NULL, NULL};

    pace_lock_reset(&L, 1, R60, NTSC, f.now, true, PACE_CLOCK_KIND_NONE);
    (void)pace_lock_vsync(&L, &io, true);
    f.now += 2 * MS;
    (void)pace_lock_vsync(&L, &io, false);
    f.now += 2 * MS;
    CHECK(f.nAlpha == 2 && f.alpha[0] == 0.5f && f.alpha[1] == 1.0f);
    /* the next close measures it: still two */
    (void)pace_lock_vsync(&L, &io, true);
    CHECK(L.perFrame == 2);
}

/* A host pause between vsyncs (a swapchain rebuilt at a frame rate change,
   ~140 ms on some drivers) is taken out of the grid with pace_lock_pause:
   the next step is on time, no lag is dropped (a resync) and no steps run
   back to back to catch up; without the pause the same stall is a resync
   that drops it */
static void testPause(void)
{
    for (int paused = 0; paused < 2; paused++) {
        Fake f = fake(D60);
        PaceLock L;
        const PaceLockIo io = {&f, f_now, f_present, f_sleep, NULL, NULL, NULL, NULL, NULL, NULL};
        uint64_t due;

        f.cost = 2 * MS;
        f.gpu = 6 * MS;
        pace_lock_reset(&L, 1, R60, NTSC, f.now, true, PACE_CLOCK_KIND_NONE);
        for (unsigned v = 0; v < 30; v++) {
            f.now += 1 * MS;
            (void)pace_lock_vsync(&L, &io, v % 2 == 0);
        }
        due = L.due;
        /* the stall */
        f.now += 140 * MS;
        if (paused) {
            pace_lock_pause(&L, 140 * MS);
            CHECK(L.due == due + 140 * MS);
        }
        const unsigned stepsBefore = L.steps, presentsBefore = L.presents;
        const uint64_t next = pace_lock_vsync(&L, &io, true);

        if (paused) {
            /* on the grid: one step and its present, a period on from the
               moved due time, nothing dropped */
            CHECK(L.resyncs == 0 && L.droppedNs == 0);
            CHECK(L.steps == stepsBefore + 1 && L.presents == presentsBefore + 1);
            CHECK(next == due + 140 * MS + R60);
            CHECK(L.skipped == 0);
        } else {
            /* the lag is dropped: a resync */
            CHECK(L.resyncs == 1 && L.droppedNs > 100 * MS);
        }
    }
}

/* Four frame rate changes within 10 s (a logged run, 240 -> original ->
   uncapped -> 60 -> 120 -> 60, would give 3 resyncs dropping 404 ms
   without the pauses, the screen black at those).  Each change, on the lock's side: the swapchain rebuilt
   between vsyncs (140 ms, the host's pause: pace_lock_pause), the lock
   started again from the moved due time (pace_locked resets it when the
   present mode flips), and the first present on the new chain taking 150
   ms, whose excess over a refresh the host pauses out after that vsync
   (present_once).  No lag is dropped and the step after each change starts
   on the grid; without the pauses every change is a resync. */
static void testChanges(void)
{
    for (int paused = 0; paused < 2; paused++) {
        Fake f = fake(D60);
        PaceLock L;
        const PaceLockIo io = {&f, f_now, f_present, f_sleep, NULL, NULL, NULL, NULL, NULL, NULL};
        unsigned changes = 0, late = 0, resyncs = 0;
        uint64_t dropped = 0;

        f.mode = F_FIFO;
        f.cost = 2 * MS;
        f.gpu = 6 * MS;
        pace_lock_reset(&L, 1, R60, NTSC, f.now, false, PACE_CLOCK_KIND_NONE);
        for (unsigned v = 0; v < 600; v++) {
            const bool change = v > 0 && v % 120 == 0; /* at 2, 4, 6 and 8 s */

            if (change) {
                f.now += 140 * MS; /* the rebuild, in the pump */
                if (paused) {
                    pace_lock_pause(&L, 140 * MS);
                }
                /* the lock started again from the step's due time (its
                   counts start again too: summed here) */
                resyncs += L.resyncs;
                dropped += L.droppedNs;
                pace_lock_reset(&L, 1, R60, NTSC, L.due, false, PACE_CLOCK_KIND_NONE);
                f.cost = 150 * MS; /* the first present on the new chain */
                changes++;
            }
            f.now += 1 * MS; /* the step */
            const uint64_t next = pace_lock_vsync(&L, &io, v % 2 == 0);

            if (change) {
                f.cost = 2 * MS;
                if (paused) {
                    pace_lock_pause(&L, 150 * MS - R60); /* present_once's rule */
                }
            } else if (f.now > next + MS / 2) {
                late++;
            }
        }
        resyncs += L.resyncs;
        dropped += L.droppedNs;
        printf("four changes in 10 s%s: %u resyncs, %.0f ms dropped, %u steps late\n",
               paused ? " with the pauses" : "", resyncs, (double)dropped / 1e6, late);
        CHECK(changes == 4);
        if (paused) {
            CHECK(resyncs == 0 && dropped == 0);
            CHECK(late == 0);
        } else {
            CHECK(resyncs >= 4);
        }
    }
}

/* FIFO latest ready (VK_EXT_present_mode_fifo_latest_ready, opt-in):
   presents never wait, and at each vblank the display shows the newest
   picture that is ready, dropping older ones.  On the grid with one
   present a step: steps never late, one picture a refresh at a phase
   clear of the vblank.  A phase that puts the pictures' readiness around a
   vblank alternates repeats and drops, as mailbox does (nothing steers the
   phase without the display clock), and the display's drift sweeps the
   phase through that zone once per 5.5 minutes at 50 ppm, for ~20 s each
   time, where FIFO slips one picture once: so the mode stays opt-in; the
   numbers are measured here, not hidden. */
static void testLatestReady(void)
{
    unsigned badPhases = 0, worst = 0;

    for (unsigned p = 0; p < 16; p++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 60.0);

        f.mode = F_MAILBOX; /* presents return at once; the newest ready is shown */
        f.phase = 5 * SEC - (uint64_t)((double)p * D60 / 16.0);
        f.cost = 2 * MS;
        f.gpu = 7 * MS;
        f.gpuJitter = 2 * MS;
        o.fifo = true; /* the lock treats it as FIFO: the hold never fires on an at-once return */
        const Run r = run(&f, &o);

        CHECK(r.lateMax == 0);
        CHECK(r.presents == r.steps);
        CHECK(r.skipped == 0 && r.resyncs == 0);
        CHECK(r.alphaBad == 0);
        if (r.repeats + r.drops) {
            badPhases++;
            if (r.repeats + r.drops > worst) {
                worst = r.repeats + r.drops;
            }
        }
    }
    printf("fifo latest ready, 16 phases x 60 s: %u phases with repeats or drops, worst %u\n",
           badPhases, worst);
    CHECK(badPhases < 16); /* a phase clear of the vblank shows one picture a refresh */
    {
        /* 50 ppm of drift: a slip every few minutes, not every second */
        const double d = D60 * (1.0 - 50e-6);
        Fake f = fake(d);
        Opt o = opt(1, R60, 300.0);

        f.mode = F_MAILBOX;
        f.phase = 5 * SEC - (uint64_t)(D60 * 0.5);
        f.cost = 2 * MS;
        f.gpu = 6 * MS;
        f.gpuJitter = 1 * MS;
        o.fifo = true;
        const Run r = run(&f, &o);

        printf("fifo latest ready, a display 50 ppm fast, 5 min: %u repeated, %u dropped\n",
               r.repeats, r.drops);
        CHECK(r.lateMax == 0);
        /* measured, and why the mode stays opt-in: the phase sweeps through
           the vblank once per 5.5 minutes at 50 ppm and alternates repeats
           and drops for the ~20 s it takes the GPU's jitter zone to pass
           (a few hundred pictures), where FIFO slips one picture once */
        CHECK(r.repeats + r.drops > 0 && r.repeats + r.drops < 1000);
    }
}

/* Without the lock under FIFO at 144 Hz: the window's present loop for one
   simulated vsync, as window_host.c's pace() (the rule pace_fifo_hold; rc1
   makes the same call with frameShown always true, a rule that ignores
   whether the frame was shown).  Returns the new frames
   presented over `vsyncs`, the step stepNs long. */
static unsigned fifoNewFramesShown(uint64_t stepNs, unsigned vsyncs, int rc1)
{
    Fake f = fake(6944444.0);
    uint64_t deadline = f.now, lastReturn = 0;
    unsigned frame = 0, shownFrame = ~0u, heldFrame = ~0u, shownCount = 0;

    f.mode = F_BLOCK;
    for (unsigned v = 0; v < vsyncs; v++) {
        deadline += NTSC;
        if (v % 2 == 0) {
            frame++;
        }
        for (;;) {
            const bool shown = shownFrame == frame;

            if (pace_fifo_hold(f.now, lastReturn, 6944444ull, deadline, rc1 ? true : shown,
                               heldFrame == frame)) {
                if (!shown) {
                    heldFrame = frame;
                }
                f_sleep(&f, deadline);
                break;
            }
            if (shown && f.now >= deadline) {
                break;
            }
            f_present(&f, 0.5f);
            f.nAlpha = 0;
            lastReturn = f.now;
            if (!shown) {
                shownCount++;
            }
            shownFrame = frame;
        }
        f.now += stepNs;
    }
    return shownCount;
}

static void testFifoHold(void)
{
    const uint64_t r = 6944444ull, d = 100 * MS;

    /* a frame already shown: held when the next vblank is past the deadline */
    CHECK(pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, true, false));
    CHECK(!pace_fifo_hold(d - 10 * MS, d - 11 * MS, r, d, true, false));
    /* a new frame before the deadline: held once */
    CHECK(pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, false, false));
    CHECK(!pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, false, true));
    /* a new frame past the deadline (the game behind): never held */
    CHECK(!pace_fifo_hold(d + 1 * MS, d - 4 * MS, r, d, false, false));
    CHECK(!pace_fifo_hold(d + 1 * MS, 0, r, d, true, false));

    /* the game behind, a 20 ms step against the 16.68 ms period: every new
       frame shown; with frameShown always true (rc1) none of them */
    const unsigned now = fifoNewFramesShown(20 * MS, 600, 0);
    const unsigned rc1 = fifoNewFramesShown(20 * MS, 600, 1);

    printf("fifo at 144, the game behind: %u of 300 new frames shown (rc1 %u)\n", now, rc1);
    CHECK(now == 300);
    CHECK(rc1 < 10);
    /* a 3 ms step: all shown */
    CHECK(fifoNewFramesShown(3 * MS, 600, 0) == 300);
}

/* the audio's ratio from the lock's start: the nominal one */
static void testNominalRatio(void)
{
    PaceLock L;

    pace_lock_reset(&L, 1, 16666667ull, NTSC, 1, true, PACE_CLOCK_KIND_NONE);
    CHECK(pace_lock_nominal_ratio(&L) > 1.000999 && pace_lock_nominal_ratio(&L) < 1.001001);
    pace_lock_reset(&L, 2, 8333333ull, NTSC, 1, true, PACE_CLOCK_KIND_NONE);
    CHECK(pace_lock_nominal_ratio(&L) > 1.000999 && pace_lock_nominal_ratio(&L) < 1.001001);
    pace_lock_reset(&L, 1, 16683333ull, NTSC, 1, true, PACE_CLOCK_KIND_NONE);
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
    pace_lock_reset(&L, 1, 20000000ull, 20000000ull, 1, true, PACE_CLOCK_KIND_NONE); /* PAL on 50 */
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
    pace_lock_reset(&L, 1, 17500000ull, NTSC, 1, true,
                    PACE_CLOCK_KIND_NONE); /* 4.7 % off: not trusted */
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
}

/* The log's trace: the first presents, with the time to the refresh that
   showed them where the clock saw it */
static void testTrace(void)
{
    Fake f = fake(D60);
    PaceLock L;
    const PaceLockIo io = {&f,           f_now,   f_present, f_sleep, f_visible,
                           f_present_id, f_shown, NULL,      NULL,    NULL};
    char buf[1024];
    unsigned k;

    f.vbDone = f_vb_index(&f, f.now);
    pace_lock_reset(&L, 1, R60, NTSC, f.now, true, PACE_CLOCK_KIND_SHOWN);
    for (unsigned v = 0; v < 100; v++) {
        f.vsync = v;
        (void)pace_lock_vsync(&L, &io, v % 2 == 0);
        f.now += 2 * MS;
    }
    k = pace_lock_trace_text(&L, buf, sizeof buf);
    printf("trace: %s\n", buf);
    CHECK(k == PACE_LOCK_TRACE_FIRST);
    CHECK(strncmp(buf, "0.00", 4) == 0); /* the first: no interval */
    CHECK(strstr(buf, " 16.67 (") != NULL);
    /* a short buffer: whole entries only */
    k = pace_lock_trace_text(&L, buf, 20);
    CHECK(k >= 1 && strlen(buf) < 20 && buf[strlen(buf) - 1] != ' ');
}

/* Issue 126: every 150th step 100 ms long (a disc read the game thread
   waited for, 16 sectors on the reporter's z: drive took up to 123 ms) at
   60 Hz, one present a step, the allowance judged.  With the step's disc
   wait told to the lock (PaceLock.discWaitNs) nothing is dropped and each
   such step is counted as excused; told nothing, none is. */
static void testDiscWait126(void)
{
    for (int disc = 0; disc < 2; disc++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        o.budget = true;
        o.gpuNs = 5 * MS;
        o.bigEvery = 150;
        o.bigStep = 100 * MS;
        o.bigDisc = disc != 0;
        r = run(&f, &o);
        printf("every 150th step 100 ms%s: %u drops, %u arms, %u excused by the disc, allowance "
               "%u, %u resyncs\n",
               disc ? " waiting for the disc" : "", r.L.budget.drops, r.L.budget.arms,
               r.L.budget.discExcused, r.L.budget.allow, r.resyncs);
        if (disc) {
            CHECK(r.L.budget.drops == 0 && r.L.budget.allow == 1);
            CHECK(r.L.budget.discExcused >= 10);
        } else {
            CHECK(r.L.budget.discExcused == 0);
        }
    }
}

/* The lock at 60 Hz, a frame every two steps of 5 ms: a present of 12.5 ms
   all on the game thread (the interpolation and the record 9 ms, the
   submit and the present 3.5 ms) leaves the step past the period, so the
   steps fall behind and presents are skipped; with the submit and the
   present on the render thread the game thread's 9 ms and the step fit the
   period: every step on time, one present a step, the alphas exact */
static void testAsyncLock(void)
{
    for (int async = 0; async < 2; async++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        f.async = async;
        f.cost = async ? 9 * MS : 12500000ull;
        f.rcost = 3500000ull;
        f.gpu = 8 * MS;
        o.step = 5 * MS;
        r = run(&f, &o);
        printf("the render thread %s: a 5 ms step and a present of 9 + 3.5 ms at 60 Hz: %.2f "
               "presents a step after 1 s, latest step %.3f ms, %u skipped, %u late skips, %u "
               "resyncs\n",
               async ? "on" : "off", (double)r.presentsFrom / (double)r.stepsFrom,
               (double)r.lateMaxFrom / 1e6, r.skippedFrom, r.L.skippedLate, r.resyncs);
        CHECK(r.alphaOffGrid == 0);
        if (async) {
            CHECK(r.resyncs == 0 && r.skippedFrom == 0 && r.L.skippedLate == 0);
            CHECK(r.lateMaxFrom < MS);
            CHECK(r.presentsFrom * 100 >= r.stepsFrom * 99 && r.presentsFrom <= r.stepsFrom + 1);
            CHECK(r.alphaBad == 0);
        } else {
            CHECK(r.lateMaxFrom >= MS / 2 || r.skippedFrom > 0 || r.L.skippedLate > 0);
        }
    }
}

/* The lock at 60 Hz with a FIFO wait off the game thread (issue 126's
   Direct3D 11 path: the render thread submits and presents itself, the
   frame latency wait for room in the present queue ending at the next
   vblank, a full queue; the vblanks 3.67 ms after the due times, the
   fake's phase).  A present hands the picture over after 0.2 ms of the
   game thread's; the render thread's job is 1.5 ms of interpolation, 0.5
   ms more of reads, 0.3 ms of submit and present; the game's step 3 ms,
   its first renderer call waiting for the picture's reads.
   The wait at the end of the picture, after its present (room 2, rd_thread.c
   roomAhead): about 14 ms a picture on the render thread, none of it in
   the step (no read wait); the next present waits about 0.67 ms for the
   render thread, which the window counts as the display's wait; the step's
   period kept, one present a step, no step past the next one's due time,
   nothing dropped or skipped, the alphas exact.
   The wait in the acquire, inside the reads (room 1, as v0.10.0 ran): the
   steps carry about 4.2 ms of read waits each (the display's: the acquire
   ends at the vblank after the step's due time), 11.5 ms waits for room. */
static void testRoomOffGame(void)
{
    for (int room = 1; room <= 2; room++) {
        Fake f = fake(D60);
        Opt o = opt(1, R60, 30.0);
        Run r;

        f.async = 1;
        f.room = room;
        f.cost = 200000ull;
        f.rInterp = 1500000ull;
        f.rRest = 500000ull;
        f.rPresent = 300000ull;
        f.gpu = 8 * MS;
        o.step = 3 * MS;
        o.fifo = true;
        o.budget = true;
        o.gpuNs = 8 * MS;
        r = run(&f, &o);
        const double roomMs = f.roomWaits ? (double)f.roomWaitNs / 1e6 / (double)f.roomWaits : 0.0;
        const double readMs = f.readWaits ? (double)f.readWaitNs / 1e6 / (double)f.readWaits : 0.0;
        const double presMs =
            f.presentWaits ? (double)f.presentWaitNs / 1e6 / (double)f.presentWaits : 0.0;

        printf("the display's wait %s: %.2f presents a step after 1 s, %u waits for room of "
               "%.2f ms, %u read waits of %.2f ms in %u steps, %u presents that waited for the "
               "render thread %.2f ms (the display's wait), %u steps past the next due time, %u "
               "skipped, %u late skips, %u drops, %u resyncs\n",
               room == 2 ? "at the end of the picture (outside the reads)"
                         : "in the acquire (inside the reads)",
               (double)r.presentsFrom / (double)r.stepsFrom, f.roomWaits, roomMs, f.readWaits,
               readMs, r.stepsFrom, f.presentWaits, presMs, f.periodMisses, r.skippedFrom,
               r.L.skippedLate, r.L.budget.drops, r.resyncs);
        CHECK(r.alphaOffGrid == 0 && r.alphaBad == 0);
        CHECK(r.resyncs == 0 && r.skippedFrom == 0 && r.L.skippedLate == 0);
        CHECK(r.L.budget.drops == 0 && f.periodMisses == 0);
        CHECK(r.presentsFrom * 100 >= r.stepsFrom * 99 && r.presentsFrom <= r.stepsFrom + 1);
        CHECK(f.roomWaits * 100 >= r.stepsFrom * 99);
        if (room == 2) {
            /* off the game thread: 14.17 ms a picture, the step without it */
            CHECK(f.readWaits == 0);
            CHECK(roomMs >= 13.5 && roomMs <= 15.0);
            CHECK(f.presentWaits * 100 >= r.stepsFrom * 99 && presMs >= 0.3 && presMs <= 1.2);
        } else {
            /* in the step: 4.17 ms of each step the display's */
            CHECK(f.readWaits * 100 >= r.stepsFrom * 99 && readMs >= 3.5 && readMs <= 5.0);
            CHECK(roomMs >= 10.5 && roomMs <= 12.5);
        }
    }
}

typedef struct AsyncRun {
    unsigned frames, presents, shown, resyncs;
    uint64_t lateMax; /* the latest step after the first second */
} AsyncRun;

/* The timer path paced by the GPU's time (window_present.c pace, vsync off
   or mailbox; pace_policy_test's budgetLoop with the render thread): steps
   of stepNs, a frame every two; a present is the interpolation preNs, the
   wait for the render thread's part of the one before (async: rd__wait_frame
   at the replay's start), the fence (the picture two before), then the
   walk recNs; its submit and present subNs on the render thread (async) or
   on the game thread after the walk; the GPU draws gpuNs a picture, in
   order, from the submit */
static AsyncRun asyncTimerLoop(unsigned steps, uint64_t gpuNs, uint64_t preNs, uint64_t recNs,
                               uint64_t subNs, uint64_t stepNs, bool async)
{
    PaceHist h;
    PaceBudget b;
    AsyncRun r;
    uint64_t t = 0, deadline = 0, cost = 0, waitNs = 0, rFree = 0;
    uint64_t ready[4] = {0, 0, 0, 0};
    unsigned made = 0;
    uint32_t frame = 0, presented = 0, skipFrame = 0;
    bool skipOpen = false, slow = false;

    memset(&h, 0, sizeof h);
    memset(&r, 0, sizeof r);
    pace_budget_reset_paced(&b, PACE_ALLOW_MAX); /* as window_host.c window_timer_budget_reset */
    for (unsigned k = 0; k < steps; k++) {
        t += stepNs;
        if (k % 2 == 0) {
            frame++;
            r.frames++;
        }
        deadline += NTSC;
        waitNs = 0;
        for (;;) {
            const bool newFrame = presented != frame;

            if (pace_late_skip(newFrame, pace_skipped_last(skipOpen, skipFrame, frame), slow, t,
                               deadline, NTSC) != PACE_SKIP_NONE) {
                skipOpen = true;
                skipFrame = frame;
                break;
            }
            if (!newFrame) {
                const bool old = t >= deadline || slow || t + cost > deadline;

                if (old || !pace_budget_allows_paced(&b, false, gpuNs, NTSC)) {
                    t = t < deadline ? deadline : t;
                    break;
                }
            }
            const uint64_t start = t;

            t += preNs;
            const uint64_t hand = async && rFree > t ? rFree - t : 0;

            t += hand;
            const uint64_t two = made >= 2 ? ready[(made - 2) % 4] : 0;
            const uint64_t fence = two > t ? two - t : 0;

            t += fence + recNs;
            uint64_t submit;

            if (async) {
                rFree = t + subNs;
                submit = rFree;
            } else {
                t += subNs;
                submit = t;
            }
            const uint64_t prev = made >= 1 ? ready[(made - 1) % 4] : 0;

            ready[made % 4] = (prev > submit ? prev : submit) + gpuNs;
            made++;
            cost = t - start;
            waitNs = hand;
            slow = pace_slow_present(&h, cost, R60, NTSC, false);
            r.shown += newFrame ? 1u : 0u;
            presented = frame;
            skipOpen = false;
            r.presents++;
            pace_budget_present(&b, newFrame);
        }
        {
            const uint64_t late = t > deadline ? t - deadline : 0;

            if (k >= 60 && late > r.lateMax) {
                r.lateMax = late;
            }
            (void)pace_budget_step(&b, late, NTSC, R60, waitNs, gpuNs, stepNs, 0);
        }
        if (t > deadline && t - deadline > 100 * MS) {
            r.resyncs++;
            deadline = t;
        }
    }
    return r;
}

static void testAsyncRates(void)
{
    static const unsigned pictures[] = {9, 12, 17, 20, 25, 30};

    for (unsigned i = 0; i < sizeof(pictures) / sizeof(pictures[0]); i++) {
        const uint64_t gpu = pictures[i] * MS;
        /* the present's CPU 1 ms: half recorded, half submitted and
           presented on the render thread */
        const AsyncRun r = asyncTimerLoop(600, gpu, 0, MS / 2, MS / 2, 3 * MS, true);

        printf("the render thread on, paced, %u ms pictures: %u presents in 10 s (%.1f a "
               "second, %.2f a step), %u frames, latest step %.1f ms, %u resyncs\n",
               pictures[i], r.presents, (double)r.presents * 1e9 / (600.0 * (double)NTSC),
               (double)r.presents / 600.0, r.frames, (double)r.lateMax / 1e6, r.resyncs);
        CHECK(r.resyncs == 0 && r.lateMax < NTSC / 4 && r.shown == r.frames);
        CHECK((uint64_t)r.presents * gpu <= 601u * NTSC);
        if (pictures[i] < 30) {
            CHECK((uint64_t)r.presents + 5 >= 600u * NTSC / gpu);
        } else {
            CHECK(r.presents == r.frames); /* the new frames: 30 a second */
        }
    }
    {
        /* a CPU-bound present: 9 ms (3 interpolated, 2 recorded, 4
           submitted and presented), a 3 ms step, a 4 ms GPU: back to back
           a present takes max(3, 4) + 2 ms with the render thread */
        const AsyncRun off = asyncTimerLoop(600, 4 * MS, 3 * MS, 2 * MS, 4 * MS, 3 * MS, false);
        const AsyncRun on = asyncTimerLoop(600, 4 * MS, 3 * MS, 2 * MS, 4 * MS, 3 * MS, true);

        printf("a 9 ms present, a 3 ms step: %u presents with the render thread, %u without; "
               "latest step %.1f and %.1f ms\n",
               on.presents, off.presents, (double)on.lateMax / 1e6, (double)off.lateMax / 1e6);
        CHECK(on.resyncs == 0 && off.resyncs == 0);
        CHECK(on.shown == on.frames && off.shown == off.frames);
        CHECK(off.presents <= 605u && on.presents >= 1150u);
        CHECK(on.lateMax < NTSC / 4);
    }
}

/* The render thread as it is (S2 and S3, port/render/rd_thread.h): the
   whole picture on the render thread.  A present on the game thread waits
   for the picture before it to finish (the drain: the 10 s line's slot
   waits), then hands its picture over (postNs).  The picture holds the
   renderer's state for readsNs from its start (the fence wait for the
   picture two before comes first and is part of it), then takes restNs
   more (the submit and the present); the GPU draws gpuNs a picture from
   the end of the reads.  A step that starts while a picture's reads are
   open waits for them first (texLock: the read waits).  guard: the further
   presents planned by pace_rt_further_late with the estimates as
   render_thread.c keeps them (folded when a picture ends, the picture in
   progress from its start).  The present allowance is left out (its GPU
   gate is test120Budget's): only the plan and the window's own rules hold
   a present.  From step burstFrom for burstLen steps every
   picture holds the state burstReads and takes burstRest more (a stage
   change's textures uploaded) and the GPU draws it in burstGpu (0: gpuNs).
   depth (0: none) is the driver's back-pressure: a picture's present
   returns only once the GPU has finished the picture depth before it (the
   PowerVR phone's rc5 log: fence waits 0.0 ms, the present blocked up to
   34 ms behind 21-35 ms GPU pictures), its present call's time presentBase
   plus that wait.  mode: RT_NONE no plan, RT_PLAN the plan's
   pace_rt_further_late alone (pace_rt_further_hold unpaced), RT_HOLDS the
   paced holds too (PACE_RT_OVERRUN, PACE_RT_BEHIND by the present call's
   estimate, as render_thread.c keeps it).  interpNs (rtTimerLoopSplit; 0:
   none, the model above) is the submit thread (port/render/
   rd_submit_thread.h): a picture's job on the render thread ends with its
   reads, its submit and present (restNs, and the back-pressure's wait) run
   on the submit thread from there, and the render thread's next picture
   makes no graphics call for its first interpNs (the interpolation), then
   waits for the submit thread to be idle (subWaits) and for the fence of
   the picture two before; the present estimate folded at a job's end is
   the submit thread's last finished present (the picture before's) */
typedef struct RtLoad {
    uint64_t stepNs, postNs, readsNs, restNs, gpuNs;
    uint64_t burstReads, burstRest;
    unsigned burstFrom, burstLen;
    uint64_t burstGpu;
    unsigned depth;
    uint64_t presentBase;
} RtLoad;

/* rtTimerLoopRule's game (testBackoffCause sets it, then clears it): step
   slowAt takes slowNs more (0: none), the game's own long step (a load, a
   compile, a bug report's capture); the first picture posted in step
   spikeAt[i] reads spikeNs[i] longer (a surprise: a texture's upload, a
   pipeline's first use); rule: RT_RULE_NOW the window's (pace_policy.h
   PaceStepEst: the floor, the widened margin, the third miss's strict
   plan), RT_RULE_HANDOVER the one before FIX-BACKOFF (every miss of the
   pictures' planned the whole period, 120 steps doubled to 1920, halved by
   1920 clean ones; a hold of any size the pictures'), RT_RULE_ANYMISS that
   with every miss arming it, as before the rule asked whose miss it was */
enum { RT_RULE_NOW = 0, RT_RULE_HANDOVER, RT_RULE_ANYMISS };

static struct {
    unsigned slowAt;
    uint64_t slowNs;
    int rule;
    const unsigned *spikeAt;
    const uint64_t *spikeNs;
    unsigned spikes;
    /* the game's frame (fieldNs 0: every step L->stepNs): the step that
       closes a frame (the even ones) takes frameSeq[i % frameSeqN] for the
       i-th frame, the step between fieldNs; fieldQuiet: the step between
       makes no renderer call (no read wait; a phone's in play) */
    const uint64_t *frameSeq;
    unsigned frameSeqN;
    uint64_t fieldNs;
    bool fieldQuiet;
    /* the rule blind to the frame's shape (before pace_policy.h
       PaceStepTyp: every step planned by the one recent maximum, the step
       after the next always behind a new frame's picture) */
    bool blind;
    /* the pictures' own loads, cycled by the picture's number (0: the
       load's): the reads (readsSeq), the interpolation (interpSeq: the
       rule then plans by its estimate, else by the interpNs given), the
       submit thread's job (restSeq); the step between frames' times cycled
       by the frame (fieldSeq, 0: fieldNs) */
    const uint64_t *readsSeq, *interpSeq, *restSeq, *fieldSeq;
    unsigned readsSeqN, interpSeqN, restSeqN, fieldSeqN;
    /* the estimates PACE3 planned by (pace_rt_estimate, the recent maximum,
       and each step kind's mean and one deviation) in the window's rule
       too: the comparison's "before" (testRc11) */
    bool maxEst;
    /* the present call's wait of each picture (cycled by the picture's
       number; 0 entries: the load's presentBase), its time inside the
       submit thread's job; and the pipe's fence as before PACE5
       (oldBehind: the picture before's whenever the presents' wait is half
       a refresh or more, the submit thread on or off; else
       pace_rt_pipe_behind) */
    const uint64_t *presentSeq;
    unsigned presentSeqN;
    bool oldBehind;
    /* VKSLOTS' world (slots: the frame slots apart): the render thread's
       next picture waits for the submit thread only for the picture two
       before's job, the submit thread runs the jobs one after another, the
       GPU starts a picture once the submit thread has run subPartNs of its
       job, and the pipe reads the submit thread's job 0 with submitOn
       (render_thread.c ico_render_thread_plan), not with it when asRun
       (rc13's build: the rule took the job's 0 for the thread off).  The
       GPU's time per picture gpuSeq (cycled by the picture's number; 0
       entries: the load's); gpuRecords: the pipe's GPU time a picture the
       median of the last 8 records, each two pictures late (window_host.c
       perf_drain), else the load's */
    bool slots, asRun, gpuRecords;
    uint64_t subPartNs;
    const uint64_t *gpuSeq;
    unsigned gpuSeqN;
    /* the present gap (0: the mode's, the 120 Hz mailbox's one a refresh
       or back to back; else the cap's, pace_present_gap: 1e9 / 60 for
       Frame rate 60 on a 120 Hz mailbox), and the paced path's "presents
       behind" hold as before the second review (oldHold: PACE_RT_BEHIND
       whenever the presents wait half a refresh or more, the submit thread
       on or off; else pace_rt_further_trace's, with it off only) */
    uint64_t gapNs;
    bool oldHold;
} s_rtGame;

/* The render thread's estimates as render_thread.c keeps them, folded when
   a picture's job ends: in the window's rule (typ) the typical times
   (pace_policy.h PaceRtTyp) of the reads, the whole job, the reads less
   their waits, the interpolation and the submit thread's job, the present
   call's wait by its recent maximum; in the rules before (RT_PLAN,
   RT_HOLDS) all by the recent maximum they ran with (pace_rt_estimate) */
typedef struct RtEst {
    bool typ;
    uint64_t reads, whole, present, cpu, sub, interp;
    PaceRtTyp tReads, tWhole, tCpu, tSub, tInterp;
} RtEst;

static void rtFold(RtEst *e, uint64_t reads, uint64_t whole, uint64_t present, uint64_t cpu,
                   uint64_t sub, uint64_t interp)
{
    const uint64_t in = interp < cpu ? interp : cpu;

    e->present = pace_rt_estimate(e->present, present);
    if (e->typ) {
        pace_rt_typ_note(&e->tReads, reads);
        e->reads = pace_rt_typ_ns(&e->tReads);
        pace_rt_typ_note(&e->tWhole, whole);
        e->whole = pace_rt_typ_ns(&e->tWhole);
        pace_rt_typ_note(&e->tCpu, cpu);
        e->cpu = pace_rt_typ_ns(&e->tCpu);
        pace_rt_typ_note(&e->tInterp, in);
        e->interp = pace_rt_typ_ns(&e->tInterp);
        if (sub != 0) {
            pace_rt_typ_note(&e->tSub, sub);
            e->sub = pace_rt_typ_ns(&e->tSub);
        }
    } else {
        e->reads = pace_rt_estimate(e->reads, reads);
        e->whole = pace_rt_estimate(e->whole, whole);
        e->cpu = pace_rt_estimate(e->cpu, cpu);
        e->interp = pace_rt_estimate(e->interp, in);
        e->sub = sub != 0 ? pace_rt_estimate(e->sub, sub) : e->sub;
    }
}

/* PACE3's step plan (s_rtGame.maxEst): the kind's mean and one deviation */
static uint64_t rtPlanMax(const PaceStepEst *e, bool closes, uint64_t periodNs)
{
    const PaceStepTyp *t = &e->typ[closes ? 1 : 0];

    if (t->n == 0 || e->backoff != 0) {
        return pace_step_est_ns(e, periodNs);
    }
    return t->meanNs + pace_step_typ_dev_ns(t) + e->widenNs;
}

/* RT_RULE_HANDOVER's back-off (FIX-HANDOVER's pace_step_est_note) */
typedef struct RtOldBackoff {
    unsigned backoff, len, clean, backoffs;
} RtOldBackoff;

static void rtOldBackoffNote(RtOldBackoff *o, bool missed, bool byPictures)
{
    if (missed && byPictures) {
        o->backoffs++;
        o->len = o->len == 0 ? 120u : o->len >= 960u ? 1920u : 2u * o->len;
        o->backoff = o->len;
        o->clean = 0;
    } else if (o->backoff != 0) {
        o->backoff--;
    } else if (++o->clean >= 1920u && o->len != 0) {
        o->len = o->len > 120u ? o->len / 2u : 0u;
        o->clean = 0;
    }
}

/* The pipe's GPU time a picture for the picture made next (s_rtGame.
   gpuRecords): the median of the last 8 records of the pictures up to two
   before it (window_host.c perf_drain: a record comes two pictures late;
   0 before the first) */
static uint64_t rtGpuIn(PaceHist *h, const uint64_t *rec, unsigned *added, uint64_t *median,
                        unsigned made)
{
    while (*added + 2u <= made) {
        *median = pace_hist_add(h, rec[*added % 4u]);
        (*added)++;
    }
    return *median;
}

/* RT_STEP (rtTimerLoopRule): the window's rule (pace_policy.h
   pace_rt_further_ask: the paced holds, then the step-deadline rule by the
   step's estimate and the pipeline's prediction, PaceStepEst's back-off);
   RT_ASMADE: the same with the pictures as made (vsync off, uncapped: no
   PACE_RT_BEHIND) */
enum { RT_NONE = 0, RT_PLAN, RT_HOLDS, RT_STEP, RT_ASMADE };

typedef struct RtRun {
    unsigned frames, presents, shown, resyncs, held;
    unsigned readWaits, slotWaits; /* after the first second, as lateMax */
    uint64_t readWaitNs, slotWaitNs;
    uint64_t lateMax; /* a step's start past its due time, after the first second */
    unsigned overrun, behind, blocked; /* holds by why; presents that waited for the GPU */
    unsigned subWaits; /* the render thread's waits for the submit thread, after the first second */
    uint64_t subWaitNs;
    /* RT_STEP / RT_ASMADE: holds by the step rule; after the first second,
       the steps that missed their period (ended past their deadline) and
       the latest (a step started late by a picture is a read wait); the
       back-offs begun in all (PaceStepEst) */
    unsigned miss, misses, backoffs;
    uint64_t missMax;
    unsigned pictureMisses, gameMisses; /* the misses after the first second, by whose */
    /* of the pictures' misses after the first second, those over the floor
       (PACE_STEP_MISS_FLOOR_NS); the steps planned at the whole period
       (from the start), the most in a 10 s block (600 steps), and the
       fewest presents in a block; the margin's widenings in all */
    unsigned overFloor, strict, strictBlockMax, blockPresentsMin, widens;
    /* the asks that reached the step rule (pace_policy.h PaceRtTrace), the
       holds by its test, the asks whose pipe waited for the picture
       before's fence */
    unsigned asks, byNext, byAfter, fenceBefore;
} RtRun;

/* rtTimerLoopSplit with RT_STEP / RT_ASMADE (the window's rule: the step's
   own time estimated and its misses backed off, the pipeline predicted as
   window_present.c does, from the render thread's estimates folded as
   render_thread.c keeps them: the reads less their waits, the
   interpolation, the submit thread's job; the GPU's time the load's) and
   immediate (vsync off with an immediate swapchain: no gap, presents back
   to back; else the 120 Hz mailbox's one a refresh) */
static RtRun rtTimerLoopRule(unsigned steps, const RtLoad *L, int mode, uint64_t interpNs,
                             bool immediate)
{
    PaceHist h;
    RtRun r;
    const uint64_t refresh = 8333333ull; /* a 120 Hz mailbox: one present a refresh */
    const uint64_t gap = s_rtGame.gapNs != 0 ? s_rtGame.gapNs
                         : immediate         ? 0
                                             : pace_present_gap(0, true, true, refresh);
    uint64_t t = 0, deadline = 0, cost = 0, nextPresent = 0;
    uint64_t rStart = 0, rReadsEnd = 0, rFree = 0;
    uint64_t rPresent = 0;
    uint64_t sFree = 0, sPresent = 0; /* the submit thread's (interpNs != 0) */
    uint64_t rCpu = 0, rSub = 0, sSub = 0, rInterp = 0;
    RtEst E;
    uint64_t ready[4] = {0, 0, 0, 0};
    bool folded = true;
    unsigned made = 0;
    uint32_t frame = 0, presented = 0, skipFrame = 0;
    bool skipOpen = false, slow = false;
    PaceStepEst se;
    RtOldBackoff oldBo;
    PaceRtPipe pipe;
    uint64_t prevEnd = 0; /* the step before's end (pace_step_start_late) */
    unsigned blockPresents = 0, blockStrict = 0, nextSpike = 0;
    /* s_rtGame.slots: each picture's submit job's end; gpuRecords: the GPU
       times by picture (a ring: the records two pictures late) into the
       median */
    uint64_t subEnd[4] = {0, 0, 0, 0}, gpuRec[4] = {0, 0, 0, 0}, gpuMedian = 0;
    unsigned gpuAdded = 0;
    PaceHist gpuHist;

    memset(&h, 0, sizeof h);
    memset(&r, 0, sizeof r);
    memset(&se, 0, sizeof se);
    memset(&oldBo, 0, sizeof oldBo);
    memset(&pipe, 0, sizeof pipe);
    memset(&E, 0, sizeof E);
    memset(&gpuHist, 0, sizeof gpuHist);
    E.typ = mode >= RT_STEP && !s_rtGame.maxEst;
    r.blockPresentsMin = ~0u;
    for (unsigned k = 0; k < steps; k++) {
        const bool burst = k >= L->burstFrom && k < L->burstFrom + L->burstLen;
        const uint64_t readsNs = burst ? L->burstReads : L->readsNs;
        const uint64_t restNs = burst ? L->burstRest : L->restNs;
        const uint64_t gpuNs = burst && L->burstGpu ? L->burstGpu : L->gpuNs;
        const uint64_t due = deadline;
        /* the presents' part of a late start, before the read wait */
        const uint64_t presentsLate = pace_step_start_late(t, due, prevEnd);
        const bool closes = k % 2 == 0; /* this step closes a frame */
        const uint64_t base = s_rtGame.fieldNs == 0 ? L->stepNs
                              : closes ? s_rtGame.frameSeq[(k / 2) % s_rtGame.frameSeqN]
                              : s_rtGame.fieldSeqN != 0
                                  ? s_rtGame.fieldSeq[(k / 2) % s_rtGame.fieldSeqN]
                                  : s_rtGame.fieldNs;
        const bool touches = closes || s_rtGame.fieldNs == 0 || !s_rtGame.fieldQuiet;
        const uint64_t own =
            base + (s_rtGame.slowNs != 0 && k == s_rtGame.slowAt ? s_rtGame.slowNs : 0);
        uint64_t waited = 0;
        /* the first picture posted in this step reads spike longer */
        uint64_t spike = 0;

        if (nextSpike < s_rtGame.spikes && s_rtGame.spikeAt[nextSpike] == k) {
            spike = s_rtGame.spikeNs[nextSpike++];
        }
        if (rReadsEnd > t && touches) {
            /* the step's first call that changes the renderer's state */
            if (k >= 60) {
                r.readWaits++;
                r.readWaitNs += rReadsEnd - t;
            }
            waited = rReadsEnd - t;
            t = rReadsEnd;
        }
        if (k >= 60 && t - due > r.lateMax) {
            r.lateMax = t - due;
        }
        t += own;
        prevEnd = t;
        if (k % 2 == 0) {
            frame++;
            r.frames++;
        }
        deadline += NTSC;
        {
            /* window_present.c step_note: the step's own time, a miss */
            const bool missed = t > deadline;
            const uint64_t late = missed ? t - deadline : 0;
            const uint64_t held = presentsLate + waited;
            const bool byPictures = s_rtGame.rule == RT_RULE_ANYMISS ? missed
                                    : s_rtGame.rule == RT_RULE_HANDOVER
                                        ? missed && held != 0 && late <= held
                                        : pace_step_by_pictures(missed, late, presentsLate, waited);

            if (missed && k >= 60) {
                r.misses++;
                r.missMax = late > r.missMax ? late : r.missMax;
                r.pictureMisses += byPictures ? 1u : 0u;
                r.gameMisses += byPictures ? 0u : 1u;
                r.overFloor += byPictures && late >= PACE_STEP_MISS_FLOOR_NS ? 1u : 0u;
            }
            if (!s_rtGame.blind) {
                /* window_present.c step_note: the step's kind's typical time */
                pace_step_typ_note(&se.typ[closes ? 1 : 0], own, NTSC, false);
            }
            if (s_rtGame.rule == RT_RULE_NOW) {
                pace_step_est_note(&se, own, missed, late, byPictures);
            } else {
                /* the estimate alone, the back-off the old one's */
                pace_step_est_note(&se, own, missed, late, false);
                rtOldBackoffNote(&oldBo, missed, byPictures);
            }
            const bool strict = s_rtGame.rule == RT_RULE_NOW ? se.backoff != 0 : oldBo.backoff != 0;

            r.strict += strict ? 1u : 0u;
            blockStrict += strict ? 1u : 0u;
        }
        for (;;) {
            const bool newFrame = presented != frame;

            if (!folded && rFree <= t) {
                /* render_thread.c after the picture's job */
                rtFold(&E, rReadsEnd - rStart, rFree - rStart, rPresent, rCpu, rSub, rInterp);
                folded = true;
            }
            if (pace_late_skip(newFrame, pace_skipped_last(skipOpen, skipFrame, frame), slow, t,
                               deadline, NTSC) != PACE_SKIP_NONE) {
                skipOpen = true;
                skipFrame = frame;
                break;
            }
            if (!newFrame) {
                const bool old = t >= deadline || slow || t + cost > deadline;
                PaceRtHold why = PACE_RT_GO;

                if (!old && mode >= RT_STEP && E.whole != 0) {
                    /* window_present.c rt_ask */
                    PaceRtAsk q;

                    memset(&q, 0, sizeof q);
                    q.nowNs = t;
                    q.dueNs = deadline;
                    q.periodNs = NTSC;
                    if (s_rtGame.rule != RT_RULE_NOW && oldBo.backoff != 0) {
                        q.stepNs = se.estNs > NTSC ? se.estNs : NTSC;
                        q.afterNs = s_rtGame.blind ? 0 : q.stepNs;
                    } else if (s_rtGame.blind) {
                        q.stepNs = pace_step_est_ns(&se, NTSC);
                    } else if (s_rtGame.maxEst) {
                        q.stepNs = rtPlanMax(&se, (k + 1u) % 2u == 0, NTSC);
                        q.afterNs = rtPlanMax(&se, (k + 2u) % 2u == 0, NTSC);
                    } else {
                        /* each step by its own kind (window_present.c rt_ask) */
                        q.stepNs = pace_step_plan_ns(&se, (k + 1u) % 2u == 0, NTSC);
                        q.afterNs = pace_step_plan_ns(&se, (k + 2u) % 2u == 0, NTSC);
                    }
                    q.nextBetween = !s_rtGame.blind && (k + 1u) % 2u != 0;
                    q.sinceNs = rFree > t ? rStart : 0;
                    q.readsNs = E.reads;
                    q.pictureNs = E.whole;
                    q.presentNs = E.present;
                    q.refreshNs = refresh;
                    q.cpuNs = E.cpu;
                    q.interpNs = s_rtGame.interpSeqN != 0 ? E.interp : interpNs;
                    q.submitNs = s_rtGame.slots ? 0 : E.sub;
                    q.submitOn = s_rtGame.slots && !s_rtGame.asRun;
                    q.gpuNs = s_rtGame.gpuRecords
                                  ? rtGpuIn(&gpuHist, gpuRec, &gpuAdded, &gpuMedian, made)
                                  : gpuNs;
                    q.paced = true;
                    q.asMade = mode == RT_ASMADE;
                    {
                        PaceRtTrace tr;

                        why = pace_rt_further_trace(&q, &pipe, &tr);
                        if (s_rtGame.oldHold && !q.asMade && why != PACE_RT_OVERRUN &&
                            pace_rt_behind(q.presentNs, q.refreshNs)) {
                            /* the hold as before: the submit thread not read */
                            why = PACE_RT_BEHIND;
                        }
                        if (s_rtGame.oldBehind && why != PACE_RT_OVERRUN && why != PACE_RT_BEHIND) {
                            /* the pipe as before PACE5 */
                            why = pace_rt_step_trace(
                                      &q, &pipe, pace_rt_busy_left(q.nowNs, q.sinceNs, q.pictureNs),
                                      pace_rt_behind(q.presentNs, q.refreshNs), &tr)
                                      ? PACE_RT_MISS
                                      : PACE_RT_GO;
                        }
                        if (why == PACE_RT_GO || why == PACE_RT_MISS) {
                            r.asks++;
                            r.byNext += tr.test == PACE_RT_TEST_NEXT ? 1u : 0u;
                            r.byAfter += tr.test == PACE_RT_TEST_AFTER ? 1u : 0u;
                            r.fenceBefore += tr.fenceBefore ? 1u : 0u;
                        }
                    }
                } else if (!old && mode != RT_NONE && E.whole != 0) {
                    why = pace_rt_further_hold(t, deadline, L->stepNs, rFree > t ? rStart : 0,
                                               E.reads, E.whole, E.present, refresh,
                                               mode == RT_HOLDS);
                }
                const bool late = why != PACE_RT_GO;

                r.held += late ? 1u : 0u;
                r.overrun += why == PACE_RT_OVERRUN ? 1u : 0u;
                r.behind += why == PACE_RT_BEHIND ? 1u : 0u;
                r.miss += why == PACE_RT_MISS ? 1u : 0u;
                if (old || late) {
                    t = t < deadline ? deadline : t;
                    break;
                }
            }
            if (nextPresent && t < nextPresent) {
                if (nextPresent >= deadline) {
                    t = t < deadline ? deadline : t;
                    break;
                }
                t = nextPresent;
                continue;
            }
            const uint64_t start = t;

            if (rFree > t) {
                /* rd__thread_picture_begin's drain */
                if (k >= 60) {
                    r.slotWaits++;
                    r.slotWaitNs += rFree - t;
                }
                t = rFree;
                if (!folded) {
                    rtFold(&E, rReadsEnd - rStart, rFree - rStart, rPresent, rCpu, rSub, rInterp);
                    folded = true;
                }
            }
            t += L->postNs;
            rStart = t;
            {
                const uint64_t two = made >= 2 ? ready[(made - 2) % 4] : 0;
                const uint64_t fence = two > rStart ? two - rStart : 0;
                const uint64_t prev = made >= 1 ? ready[(made - 1) % 4] : 0;
                /* the present returns once the GPU finished the picture
                   depth before (the driver's back-pressure) */
                const uint64_t back =
                    L->depth != 0 && made >= L->depth ? ready[(made - L->depth) % 4] : 0;
                /* its reads, the step's surprise on the first of the step;
                   the picture's own loads (s_rtGame's sequences) */
                const uint64_t pReads = s_rtGame.readsSeqN != 0
                                            ? s_rtGame.readsSeq[made % s_rtGame.readsSeqN]
                                            : readsNs;
                const uint64_t pRest =
                    s_rtGame.restSeqN != 0 ? s_rtGame.restSeq[made % s_rtGame.restSeqN] : restNs;
                const uint64_t pInterp = s_rtGame.interpSeqN != 0
                                             ? s_rtGame.interpSeq[made % s_rtGame.interpSeqN]
                                             : interpNs;
                const uint64_t reads = pReads + spike;
                uint64_t block;
                uint64_t sBegin = 0; /* when the submit thread begins its job */

                rInterp = pInterp;

                spike = 0;

                if (interpNs == 0) {
                    rReadsEnd = rStart + fence + reads;
                    const uint64_t end = rReadsEnd + pRest;

                    block = back > end ? back - end : 0;
                    rFree = end + block;
                    rPresent = L->presentBase + block;
                } else {
                    /* the interpolation, then the first graphics call:
                       the submit thread idle, the fence */
                    uint64_t at = rStart + pInterp;
                    /* the submit thread's job the picture waits for: the
                       one before's, or with the frame slots apart the one
                       two before's (its own slot's) */
                    const uint64_t subWait = !s_rtGame.slots ? sFree
                                             : made >= 2     ? subEnd[(made - 2) % 4]
                                                             : 0;

                    if (subWait > at) {
                        if (k >= 60) {
                            r.subWaits++;
                            r.subWaitNs += subWait - at;
                        }
                        at = subWait;
                    }
                    at = two > at ? two : at;
                    rReadsEnd = at + (reads > pInterp ? reads - pInterp : 0);
                    rFree = rReadsEnd; /* the job ends with the walk */
                    sBegin = s_rtGame.slots && sFree > rReadsEnd ? sFree : rReadsEnd;
                    const uint64_t end = sBegin + pRest;

                    block = back > end ? back - end : 0;
                    sFree = end + block;
                    subEnd[made % 4] = sFree;
                    rPresent = sPresent; /* the one finished before this job ends */
                    sPresent = (s_rtGame.presentSeqN != 0
                                    ? s_rtGame.presentSeq[made % s_rtGame.presentSeqN]
                                    : L->presentBase) +
                               block;
                    rSub = sSub; /* and its job */
                    sSub = pRest + block;
                }
                /* its reads less their waits (rd_thread_take_reads_parts) */
                rCpu = reads;
                if (mode >= RT_STEP) {
                    /* window_present.c rt_pipe_posted */
                    PaceRtAsk q;

                    memset(&q, 0, sizeof q);
                    q.readsNs = E.reads;
                    q.cpuNs = E.cpu;
                    q.interpNs = s_rtGame.interpSeqN != 0 ? E.interp : interpNs;
                    q.gpuNs = s_rtGame.gpuRecords
                                  ? rtGpuIn(&gpuHist, gpuRec, &gpuAdded, &gpuMedian, made)
                                  : gpuNs;
                    q.submitNs = s_rtGame.slots ? 0 : E.sub;
                    q.submitOn = s_rtGame.slots && !s_rtGame.asRun;
                    q.presentNs = E.present;
                    q.refreshNs = refresh;
                    pace_rt_pipe_post(&pipe,
                                      pace_rt_pipe_reads_end(
                                          &pipe, rStart, &q,
                                          s_rtGame.oldBehind ? pace_rt_behind(E.present, refresh)
                                                             : pace_rt_pipe_behind(&q)),
                                      &q);
                }
                if (block > 0 && k >= 60) {
                    r.blocked++;
                }
                {
                    /* the GPU's time for this picture, from its submit
                       with the frame slots apart */
                    const uint64_t gi =
                        s_rtGame.gpuSeqN != 0 ? s_rtGame.gpuSeq[made % s_rtGame.gpuSeqN] : gpuNs;
                    const uint64_t gs =
                        s_rtGame.slots && interpNs != 0 ? sBegin + s_rtGame.subPartNs : rReadsEnd;

                    ready[made % 4] = (prev > gs ? prev : gs) + gi;
                    gpuRec[made % 4] = gi;
                }
            }
            folded = false;
            made++;
            cost = t - start;
            slow = pace_slow_present(&h, cost, refresh, NTSC, false);
            r.shown += newFrame ? 1u : 0u;
            presented = frame;
            skipOpen = false;
            r.presents++;
            blockPresents++;
            nextPresent = gap ? pace_next_present(nextPresent, start, gap) : 0;
        }
        if ((k + 1u) % 600u == 0) {
            /* a 10 s block's end */
            r.strictBlockMax = blockStrict > r.strictBlockMax ? blockStrict : r.strictBlockMax;
            r.blockPresentsMin =
                blockPresents < r.blockPresentsMin ? blockPresents : r.blockPresentsMin;
            blockPresents = blockStrict = 0;
        }
        if (t > deadline && t - deadline > 100 * MS) {
            r.resyncs++;
            deadline = t;
        }
    }
    r.backoffs = s_rtGame.rule == RT_RULE_NOW ? se.backoffs : oldBo.backoffs;
    r.widens = se.widens;
    return r;
}

static RtRun rtTimerLoopSplit(unsigned steps, const RtLoad *L, int mode, uint64_t interpNs)
{
    return rtTimerLoopRule(steps, L, mode, interpNs, false);
}

static RtRun rtTimerLoop(unsigned steps, const RtLoad *L, int mode)
{
    return rtTimerLoopSplit(steps, L, mode, 0);
}

static void rtPrint(const char *what, const RtRun *r)
{
    printf("%s: %u presents in 10 s for %u frames (%u shown), %u held back (%u past the "
           "estimate, %u behind); read waits %u (%.1f ms), slot waits %u (%.1f ms); latest step "
           "%.1f ms, %u resyncs; %u presents waited for the GPU; %u waits for the submit "
           "thread (%.1f ms)\n",
           what, r->presents, r->frames, r->shown, r->held, r->overrun, r->behind, r->readWaits,
           (double)r->readWaitNs / 1e6, r->slotWaits, (double)r->slotWaitNs / 1e6,
           (double)r->lateMax / 1e6, r->resyncs, r->blocked, r->subWaits,
           (double)r->subWaitNs / 1e6);
}

/* The further presents planned by the render thread's times
   (pace_rt_further_late; the phone's stage changes, a 10 s block of its log with
   102 slot waits for 1193.8 ms and 63 read waits for 562.3 ms): with it no
   further present makes a step wait, every frame is shown, and the
   presents a second are the rate the render thread's picture time allows:
   two a step and more for 3 ms pictures, one a step for 9 ms ones, the new
   frames for 22 ms ones; without it the 9 and 22 ms pictures and a burst
   of slow ones (a stage's textures uploaded) make the steps wait.  What
   the plan leaves: a new frame's present is never held, so one the gap
   puts just before the next step (3 ms pictures: a few, under 0.1 ms
   each) or one whose picture holds the state past it (the burst: one a
   frame) still makes that step wait */
static void testRenderThreadPlan(void)
{
    /* the estimates: a higher sample at once, a lower one an eighth of the
       way a picture; the picture in progress */
    CHECK(pace_rt_estimate(0, 9 * MS) == 9 * MS);
    CHECK(pace_rt_estimate(9 * MS, 34 * MS) == 34 * MS);
    CHECK(pace_rt_estimate(34 * MS, 10 * MS) == 31 * MS);
    CHECK(pace_rt_busy_left(5 * MS, 0, 9 * MS) == 0);
    CHECK(pace_rt_busy_left(5 * MS, 2 * MS, 9 * MS) == 6 * MS);
    CHECK(pace_rt_busy_left(20 * MS, 2 * MS, 9 * MS) == 0);
    /* posted at 10 with 2 left: reads 4 end at 16 (due 16.68), the picture
       9 at 21 (the step's end 22.68): taken; reads 5 end past the due time,
       a picture 11 past the step's end: held */
    CHECK(!pace_rt_further_late(10 * MS, NTSC, 6 * MS, 2 * MS, 4 * MS, 9 * MS));
    CHECK(pace_rt_further_late(10 * MS, NTSC, 6 * MS, 2 * MS, 5 * MS, 9 * MS));
    CHECK(pace_rt_further_late(10 * MS, NTSC, 6 * MS, 2 * MS, 4 * MS, 11 * MS));
    {
        /* light pictures: two presents a step either way, nothing waits */
        const RtLoad L = {3 * MS, MS / 10, 3 * MS / 2, 3 * MS / 2, 3 * MS, 0, 0, 0, 0, 0, 0, 0};
        const RtRun on = rtTimerLoop(600, &L, RT_PLAN);
        const RtRun off = rtTimerLoop(600, &L, RT_NONE);

        rtPrint("render thread, 3 ms pictures, planned", &on);
        rtPrint("render thread, 3 ms pictures, not planned", &off);
        CHECK(on.readWaitNs < MS / 2 && on.resyncs == 0 && on.shown == on.frames);
        CHECK(off.readWaitNs > 10 * on.readWaitNs);
        CHECK(on.presents >= 1190u && on.presents + 5 >= off.presents);
    }
    {
        /* 9 ms pictures (the phone's mean: 4 ms reads, 5 ms submit and
           present), a 6 ms step: one present a step, 60 a second */
        const RtLoad L = {6 * MS, MS / 10, 4 * MS, 5 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, 0};
        const RtRun on = rtTimerLoop(600, &L, RT_PLAN);
        const RtRun off = rtTimerLoop(600, &L, RT_NONE);

        rtPrint("render thread, 9 ms pictures, planned", &on);
        rtPrint("render thread, 9 ms pictures, not planned", &off);
        CHECK(on.readWaits == 0 && on.resyncs == 0 && on.shown == on.frames);
        CHECK(on.presents >= 595u && on.lateMax < MS);
        CHECK(off.readWaits > 100u && off.lateMax > MS);
    }
    {
        /* 22 ms pictures (8 ms reads): more than a step; the new frames
           are shown, the steps on time; without the plan the further
           presents run into the steps */
        const RtLoad L = {6 * MS, MS / 10, 8 * MS, 14 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, 0};
        const RtRun on = rtTimerLoop(600, &L, RT_PLAN);
        const RtRun off = rtTimerLoop(600, &L, RT_NONE);

        rtPrint("render thread, 22 ms pictures, planned", &on);
        rtPrint("render thread, 22 ms pictures, not planned", &off);
        CHECK(on.readWaits == 0 && on.resyncs == 0 && on.shown == on.frames);
        CHECK(on.lateMax < MS && on.presents >= on.frames);
        CHECK((uint64_t)on.presents * 22u * MS <= 601u * NTSC);
        CHECK(off.readWaits > 50u && off.lateMax > on.lateMax);
    }
    {
        /* the phone's stage change: 9 ms pictures, then for 2 s (120
           steps) 30 ms ones holding the state 14 ms (the stage's textures
           uploaded), then 9 ms again: planned, the new frames' own
           pictures still run into the step after them (one a frame), the
           further presents no more; the waits a sixth of the unplanned */
        const RtLoad L = {6 * MS,  MS / 10, 4 * MS, 5 * MS, 8 * MS, 14 * MS,
                          16 * MS, 240,     120,    0,      0,      0};
        const RtRun on = rtTimerLoop(600, &L, RT_PLAN);
        const RtRun off = rtTimerLoop(600, &L, RT_NONE);

        rtPrint("render thread, a burst of 30 ms pictures, planned", &on);
        rtPrint("render thread, a burst of 30 ms pictures, not planned", &off);
        CHECK(on.resyncs == 0 && on.shown == on.frames);
        CHECK(on.readWaits <= L.burstLen / 2u + 4u && off.readWaits > 4u * on.readWaits);
        CHECK(on.readWaitNs * 4 < off.readWaitNs && on.slotWaitNs < off.slotWaitNs);
        CHECK(on.presents >= on.frames);
    }
}

/* The presents that wait for the GPU (pace_rt_further_hold's paced holds;
   the phone's rc5 log at a stage change: 88 slot waits for 1003.2 ms and 51
   read waits for 533.3 ms in 10 s, presents blocked up to 34.4 ms behind
   21-35 ms GPU pictures while 411 further presents were already held by
   the plan).  The model: a 9 ms picture on the render thread (4 ms reads,
   5 ms submit and present), a 5 ms step, light 3 ms GPU pictures, then for
   4 s (steps 120-359) the new stage's 26 or 20 ms ones, each present
   returning only once the GPU finished the picture before it.  The plan
   alone posts further presents whose present then waits for the GPU (its
   estimate of the picture is folded only once a slow one ended), and the
   next new frame's present waits for it; with the holds a further present
   waits for a picture in progress past its estimate and for the presents'
   wait to fall: no step waits, every frame is shown, and the in-betweens
   of the light seconds stay.  With nothing waiting for the GPU (TEXLOCK's
   loads, a light stage with the back-pressure) the holds change nothing */
static void testRenderThreadBehind(void)
{
    const uint64_t refresh = 8333333ull; /* 120 Hz */

    /* the holds' own cases: a picture in progress past its estimate */
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, MS, MS, 3 * MS, 0, refresh, true) ==
          PACE_RT_OVERRUN);
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, MS, MS, 3 * MS, 0, refresh, false) ==
          PACE_RT_GO);
    CHECK(pace_rt_further_hold(3 * MS, NTSC, 6 * MS, MS, MS, 3 * MS, 0, refresh, true) ==
          PACE_RT_GO);
    /* the presents' wait: half a refresh or more */
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, 0, MS, 3 * MS, refresh / 2, refresh, true) ==
          PACE_RT_GO);
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, 0, MS, 3 * MS, refresh / 2 + 1, refresh,
                               true) == PACE_RT_BEHIND);
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, 0, MS, 3 * MS, 34 * MS, refresh, false) ==
          PACE_RT_GO);
    CHECK(pace_rt_further_hold(5 * MS, NTSC, 6 * MS, 0, MS, 3 * MS, 34 * MS, 0, true) ==
          PACE_RT_GO);
    /* the plan's rule after the holds, paced or not */
    CHECK(pace_rt_further_hold(10 * MS, NTSC, 6 * MS, 0, 7 * MS, 9 * MS, 0, refresh, true) ==
          PACE_RT_LATE);
    CHECK(pace_rt_further_hold(10 * MS, NTSC, 6 * MS, 0, 7 * MS, 9 * MS, 0, refresh, false) ==
          PACE_RT_LATE);
    {
        /* TEXLOCK's loads, no back-pressure: the holds take nothing more */
        static const RtLoad loads[] = {
            {3 * MS, MS / 10, 3 * MS / 2, 3 * MS / 2, 3 * MS, 0, 0, 0, 0, 0, 0, MS / 2},
            {6 * MS, MS / 10, 4 * MS, 5 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, MS / 2},
            {6 * MS, MS / 10, 8 * MS, 14 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, MS / 2},
        };

        for (unsigned i = 0; i < sizeof(loads) / sizeof(loads[0]); i++) {
            const RtRun plan = rtTimerLoop(600, &loads[i], RT_PLAN);
            const RtRun holds = rtTimerLoop(600, &loads[i], RT_HOLDS);

            CHECK(holds.presents == plan.presents && holds.shown == plan.shown);
            CHECK(holds.readWaits == plan.readWaits && holds.slotWaits == plan.slotWaits);
            CHECK(holds.overrun == 0 && holds.behind == 0);
        }
    }
    {
        /* a light stage with the back-pressure: no present waits, nothing held more */
        const RtLoad L = {5 * MS, MS / 10, 4 * MS, 5 * MS, 3 * MS, 0, 0, 0, 0, 0, 1, MS / 2};
        const RtRun plan = rtTimerLoop(600, &L, RT_PLAN);
        const RtRun holds = rtTimerLoop(600, &L, RT_HOLDS);

        rtPrint("render thread, light pictures, the present waiting for the GPU, holds", &holds);
        CHECK(holds.presents == plan.presents && holds.blocked == 0);
        CHECK(holds.slotWaits == 0 && holds.readWaits == 0 && holds.shown == holds.frames);
    }
    {
        static const uint64_t gpu[] = {26 * MS, 20 * MS};

        for (unsigned i = 0; i < sizeof(gpu) / sizeof(gpu[0]); i++) {
            const RtLoad L = {5 * MS, MS / 10, 4 * MS, 5 * MS, 3 * MS, 4 * MS,
                              5 * MS, 120,     240,    gpu[i], 1,      MS / 2};
            const RtRun none = rtTimerLoop(600, &L, RT_NONE);
            const RtRun plan = rtTimerLoop(600, &L, RT_PLAN);
            const RtRun holds = rtTimerLoop(600, &L, RT_HOLDS);
            char what[96];

            snprintf(what, sizeof what, "render thread, a stage of %u ms GPU pictures, no plan",
                     (unsigned)(gpu[i] / MS));
            rtPrint(what, &none);
            snprintf(what, sizeof what, "render thread, a stage of %u ms GPU pictures, planned",
                     (unsigned)(gpu[i] / MS));
            rtPrint(what, &plan);
            snprintf(what, sizeof what, "render thread, a stage of %u ms GPU pictures, holds",
                     (unsigned)(gpu[i] / MS));
            rtPrint(what, &holds);
            /* the plan alone: the presents wait for the GPU, the steps for them */
            CHECK(plan.blocked > 0 && plan.slotWaits >= 20u);
            CHECK(none.slotWaits > plan.slotWaits);
            /* the holds: nothing waits, every frame shown, the light
               seconds' in-betweens kept */
            CHECK(holds.slotWaits == 0 && holds.readWaits == 0 && holds.lateMax < MS);
            CHECK(holds.resyncs == 0 && holds.shown == holds.frames);
            CHECK(holds.behind > 0 && holds.blocked < plan.blocked);
            CHECK(holds.presents >= holds.frames + 150u);
        }
    }
}

/* The submit thread (port/render/rd_submit_thread.h; the phone's rc7 log:
   a 4.5 ms step, the render thread's pictures holding its state 10.3-13.9
   ms and taking 13.4-19.3 ms with the driver's submit and the present,
   374-399 further presents a block held back as running into the next
   step, 30 presented fps).  The model: a 4.5 ms step, 10 ms reads (3 ms of
   them the interpolation, which makes no graphics call), 8 ms of submit
   and present, 8 ms GPU pictures (under a refresh: the GPU's side is
   another rule's).  With the submit and present on the render thread the
   picture takes 18 ms, so a further present posted after the second step
   of a frame would run past the step after it: only the new frames are
   shown (one present a frame).  With them on the submit thread the
   render thread is free when its walk ends, the in-between picture fits:
   two presents a frame, every step on time, nothing waits.  With a lighter
   submit (4 ms) both give two a frame; at a stage whose GPU pictures take
   26 ms with the driver's back-pressure the holds keep every wait away
   with the submit thread too */
static void testSubmitThread(void)
{
    {
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 8 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, MS / 2};
        const RtRun on = rtTimerLoopSplit(600, &L, RT_HOLDS, 3 * MS);
        const RtRun off = rtTimerLoop(600, &L, RT_HOLDS);

        rtPrint("render thread, 10 ms reads and 8 ms submit and present on it", &off);
        rtPrint("render thread, 10 ms reads, 8 ms submit and present on the submit thread", &on);
        CHECK(off.shown == off.frames && off.presents <= off.frames + 5u);
        CHECK(on.shown == on.frames && on.presents + 10u >= 2u * on.frames);
        CHECK(on.readWaits == 0 && on.slotWaits == 0 && on.subWaits == 0);
        CHECK(on.resyncs == 0 && on.lateMax < MS);
        CHECK(off.readWaits == 0 && off.slotWaits == 0 && off.lateMax < MS);
    }
    {
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 4 * MS, 8 * MS, 0, 0, 0, 0, 0, 0, MS / 2};
        const RtRun on = rtTimerLoopSplit(600, &L, RT_HOLDS, 3 * MS);
        const RtRun off = rtTimerLoop(600, &L, RT_HOLDS);

        rtPrint("render thread, 10 ms reads, 4 ms submit and present on the submit thread", &on);
        CHECK(off.presents + 10u >= 2u * off.frames && on.presents + 10u >= 2u * on.frames);
        CHECK(on.readWaits == 0 && on.slotWaits == 0 && on.lateMax < MS);
    }
    {
        const RtLoad L = {5 * MS, MS / 10, 4 * MS, 5 * MS,  3 * MS, 4 * MS,
                          5 * MS, 120,     240,    26 * MS, 1,      MS / 2};
        const RtRun on = rtTimerLoopSplit(600, &L, RT_HOLDS, 2 * MS);

        rtPrint("render thread, a stage of 26 ms GPU pictures, holds, the submit thread", &on);
        CHECK(on.slotWaits == 0 && on.readWaits == 0 && on.lateMax < MS);
        CHECK(on.resyncs == 0 && on.shown == on.frames && on.behind > 0);
        CHECK(on.presents >= on.frames + 150u);
    }
}

/* The step-deadline rule (pace_policy.h pace_rt_further_ask; the user's
   "just give pictures when they can"; a PowerVR phone's log: reads 10-14
   ms, a 4.5 ms step, 30 presented fps while the rule before, the reads by
   the next step's due time, refused the second picture a game frame by
   about 1 ms).  A further present is
   held only when it would make a step miss its period; the numbers are
   build-host/tmp/v098/evidence/fixbackoff/step_rule_cells.out (pr_sim.py,
   this model line for line).  The rate requirement, presents in 10 s for
   300 game frames:
     1. the phone (4.5 ms step, 3 ms interpolation, 3.2 ms submit and
        present on the submit thread, 8 ms GPU, a 120 Hz mailbox): two a
        game frame while the reads fit the period less the step (12.18 ms),
        past it as many as keep every step inside its period; reads 10 /
        12 / 13 / 15 / 17 ms give 700 / 600 / 550 / 447 / 301 (the rule
        before: 600 / 600 / 301 / 301 / 301), no step missing its period
        after the first second (13 and 15 ms: 501 and 421 while a miss in
        the first second planned the whole period for 120 steps; now it
        widens the margin);
     2. reads that jump from 10 to 16 ms for 2 s: the first picture's
        surprise costs one step its period, the margin widens by it (no
        fall-back to the whole period) and no step misses again (618
        presents: the typical reads learn the jump over pictures, the
        recent maximum at once: 621);
     3. vsync off, uncapped, an immediate swapchain (6 ms reads, 2 ms
        interpolation, 2 ms submit): the pictures as the GPU makes them,
        about 10 s / G (capped by two a step): 9 / 12 / 13 / 15 / 17 / 25
        ms give 1080 / 834 / 771 / 668 / 590 / 400 (the GPU 1112 / 834 /
        770 / 667 / 589 / 400), no BEHIND hold, no step missing its period
        (no step starting later than its slack and the margin: planned by
        the typical times, PACE4);
     4. vsync off on the PowerVR phone (a mailbox whose present returns
        once the GPU finished the picture before): the present's block on
        the submit thread, the render thread's wait for it inside the next
        picture's reads; 26 ms GPU pictures give 376 (the GPU's 385 less
        the pictures whose wait would make a step miss; PACE5, build-host/
        tmp/v098/evidence/pace5/c_cells.out: the submit thread on, the
        present's wait is its job's and the fences stay at two in flight,
        pace_policy.h pace_rt_pipe_behind; with the picture before's fence
        whenever the present waited half a refresh, 320, as the BEHIND hold
        gave); every frame shown, no step missing its period.
   Frame rate Original makes no further present (window_present.c returns
   before the loop; pace_policy_test testOriginalLoop) */
static void testStepRule(void)
{
    static const struct {
        unsigned readsMs, rule, before;
    } phone[] = {{10, 700, 600}, {12, 600, 600}, {13, 550, 301}, {15, 447, 301}, {17, 301, 301}};

    static const struct {
        unsigned gpuMs, asMade, before;
    } made[] = {{9, 1080, 684}, {12, 834, 617}, {13, 771, 615},
                {15, 668, 589}, {17, 590, 561}, {25, 400, 375}};

    const uint64_t slack = NTSC - 9 * MS / 2 - PACE_RT_STEP_MARGIN_NS;

    for (unsigned i = 0; i < sizeof(phone) / sizeof(phone[0]); i++) {
        const RtLoad L = {
            9 * MS / 2, MS / 10, phone[i].readsMs * MS, 16 * MS / 5, 8 * MS, 0, 0, 0, 0, 0,
            0,          MS / 2};
        const RtRun rule = rtTimerLoopRule(600, &L, RT_STEP, 3 * MS, false);
        const RtRun before = rtTimerLoopRule(600, &L, RT_HOLDS, 3 * MS, false);
        char what[96];

        snprintf(what, sizeof what, "the step rule, the phone, %u ms reads", phone[i].readsMs);
        rtPrint(what, &rule);
        printf("  %u steps missed their period (latest %.1f ms), %u held by the rule\n",
               rule.misses, (double)rule.missMax / 1e6, rule.miss);
        CHECK(rule.presents + 3u >= phone[i].rule && rule.presents <= phone[i].rule + 3u);
        CHECK(before.presents + 3u >= phone[i].before && before.presents <= phone[i].before + 3u);
        CHECK(rule.presents >= before.presents);
        CHECK(rule.misses == 0 && rule.resyncs == 0 && rule.shown == rule.frames);
        CHECK(rule.lateMax <= slack + MS / 10);
        CHECK(rule.overrun == 0 && rule.behind == 0 && rule.miss == rule.held);
    }
    {
        const RtLoad L = {9 * MS / 2,  MS / 10, 10 * MS, 16 * MS / 5, 8 * MS, 16 * MS,
                          16 * MS / 5, 240,     120,     0,           0,      MS / 2};
        const RtRun rule = rtTimerLoopRule(600, &L, RT_STEP, 3 * MS, false);
        const RtRun before = rtTimerLoopRule(600, &L, RT_HOLDS, 3 * MS, false);

        rtPrint("the step rule, reads jumping from 10 to 16 ms for 2 s", &rule);
        printf("  %u steps missed their period (latest %.1f ms), %u back-offs, %u widenings\n",
               rule.misses, (double)rule.missMax / 1e6, rule.backoffs, rule.widens);
        CHECK(rule.misses == 1 && rule.backoffs == 0 && rule.widens >= 1);
        CHECK(rule.missMax < 10 * MS && rule.overFloor == 1);
        CHECK(rule.resyncs == 0 && rule.shown == rule.frames);
        CHECK(rule.presents + 3u >= 618u && rule.presents <= 621u && before.presents < 540u);
    }
    for (unsigned i = 0; i < sizeof(made) / sizeof(made[0]); i++) {
        const uint64_t g = made[i].gpuMs * MS;
        const RtLoad L = {9 * MS / 2, MS / 10, 6 * MS, 2 * MS, g, 0, 0, 0, 0, 0, 0, MS / 2};
        const RtRun asMade = rtTimerLoopRule(600, &L, RT_ASMADE, 2 * MS, true);
        const RtRun before = rtTimerLoopRule(600, &L, RT_HOLDS, 2 * MS, true);
        const uint64_t gpuMakes = 600u * NTSC / g;
        char what[96];

        snprintf(what, sizeof what, "vsync off, uncapped: pictures as made, %u ms GPU pictures",
                 made[i].gpuMs);
        rtPrint(what, &asMade);
        CHECK(asMade.presents + 3u >= made[i].asMade && asMade.presents <= made[i].asMade + 3u);
        CHECK(before.presents + 3u >= made[i].before && before.presents <= made[i].before + 3u);
        /* what the GPU makes (two a step at most), never more */
        CHECK((uint64_t)asMade.presents * 100u >= 97u * (gpuMakes < 1200u ? gpuMakes : 1200u));
        CHECK((uint64_t)asMade.presents <= gpuMakes + 3u);
        CHECK(asMade.misses == 0 && asMade.resyncs == 0 && asMade.shown == asMade.frames);
        /* planned by the typical times (the reads with their waits for the
           GPU vary), a step may start past its slack by a surprise, inside
           the margin: 10.55 ms at 15 ms GPU pictures, the slack 10.18 */
        CHECK(asMade.behind == 0 && asMade.overrun == 0 &&
              asMade.lateMax <= slack + PACE_RT_STEP_MARGIN_NS);
    }
    {
        const RtLoad L = {9 * MS / 2, MS / 10, 6 * MS, 2 * MS, 26 * MS, 0, 0, 0, 0, 0, 1, MS / 2};
        const RtRun asMade = rtTimerLoopRule(600, &L, RT_ASMADE, 2 * MS, false);
        const RtRun before = rtTimerLoopRule(600, &L, RT_HOLDS, 2 * MS, false);

        rtPrint("vsync off, uncapped: pictures as made, the PowerVR mailbox, 26 ms GPU", &asMade);
        CHECK(asMade.presents + 3u >= 376u && asMade.presents <= 379u);
        CHECK(asMade.presents + 3u >= before.presents && before.behind > 0);
        CHECK(asMade.fenceBefore == 0 && asMade.lateMax <= NTSC - 9 * MS / 2);
        CHECK(asMade.behind == 0 && asMade.misses == 0 && asMade.shown == asMade.frames);
        CHECK(asMade.blocked > 0 && asMade.resyncs == 0);
    }
}

/* The back-off's cause and size (pace_policy.h pace_step_by_pictures,
   PaceStepEst).  Numbers from build-host/tmp/v098/evidence/fixbackoff/
   (pr_sim.py with the rule now and the rules before; backoff_cells.out,
   rc9_test_cells.out), re-run with each step planned by its kind's typical
   time (build-host/tmp/v098/evidence/pace3/rebase_cells.out): the game's
   long step is left out of the typical time where the recent maximum took
   it in, so cell 1 gives 700 / 698 / 701 presents where it gave 696 / 696
   / 693; every other count is the same.  Re-run with the render thread's
   typical times (PACE4, build-host/tmp/v098/evidence/pace4/c_cells.out):
   cell 1 698 / 698 / 701; in rc9's cells a surprise lands on a plan with
   less headroom, so more of the misses are over the floor (S 13, M 16
   where they were 2 and 13; the latest 19.7 ms where it was 14.7), each
   widening the margin, still none planning the whole period in S and M,
   and 397-400 presents a block (the GPU's 40) where they were 389-393.
   1. The phone's load at 10 ms reads (testStepRule's first cell: 700
      presents, no miss) with one long step of the game's own at step 300:
      no back-off and no fall-back, where the rule before every miss's
      cause (every miss) planned the next 120 steps at the whole period;
      the reads' jump's miss (the pictures') widens the margin.
   2. rc9 (build-host/tmp/v098/evidence/rc9/ico-pc.log: a 4.5 ms step, 13
      ms reads, 6 ms submit and present on the submit thread, its present
      3 ms, 25 ms GPU pictures, the GPU's 40 a second): one picture's reads
      26 ms longer at step 300 (a 14.7 ms miss), then one picture's reads
      longer every 250 steps, over 100 s (ten 10 s blocks):
        S: 12 / 12.5 ms longer, misses of 0.7 / 1.2 ms (under the floor);
        M: 12, 15, 18, 12.5, 14, 13 ms longer, misses of 0.7-8.6 ms;
        B: the 14.7 ms one, then three over the floor within 300 steps.
      The rule before (FIX-HANDOVER) planned the whole period 5819 of the
      6000 steps in S and M (600 in every block after the first: 30 a
      second, rc9's), the rule now none (397-400 presents a block: the
      GPU's 40); in B one fall-back of 120 steps (FIX-HANDOVER: 2722).
      Every miss is one surprise's: one a spike, and the first spike's
      two (the step it hits and, two steps on, the new frame's own
      picture behind it); none of the rule's own. */
static void testBackoffCause(void)
{
    static const struct {
        unsigned slowMs, presents, misses, pictures, before, beforeBackoffs;
    } slow[] = {{20, 698, 2, 1, 660, 2}, {40, 698, 4, 0, 647, 4}, {700, 701, 1, 0, 679, 1}};

    for (unsigned i = 0; i < sizeof(slow) / sizeof(slow[0]); i++) {
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 16 * MS / 5, 8 * MS, 0,
                          0,          0,       0,       0,           0,      MS / 2};
        RtRun now, before;
        char what[96];

        s_rtGame.slowAt = 300;
        s_rtGame.slowNs = slow[i].slowMs * MS;
        s_rtGame.rule = RT_RULE_NOW;
        now = rtTimerLoopRule(600, &L, RT_STEP, 3 * MS, false);
        s_rtGame.rule = RT_RULE_ANYMISS;
        before = rtTimerLoopRule(600, &L, RT_STEP, 3 * MS, false);
        memset(&s_rtGame, 0, sizeof s_rtGame);
        snprintf(what, sizeof what, "the back-off's cause: one game step %u ms longer",
                 slow[i].slowMs);
        rtPrint(what, &now);
        printf("  %u misses (%u the pictures', %u the game's), %u back-offs, %u widenings; every "
               "miss arming the rule before: %u presents, %u back-offs\n",
               now.misses, now.pictureMisses, now.gameMisses, now.backoffs, now.widens,
               before.presents, before.backoffs);
        CHECK(now.misses == slow[i].misses && now.pictureMisses == slow[i].pictures);
        CHECK(now.gameMisses == slow[i].misses - slow[i].pictures);
        /* the game's own misses change nothing; a picture's over the floor
           widens the margin, never the whole period */
        CHECK(now.backoffs == 0 && now.strict == 0 && now.overFloor == slow[i].pictures);
        CHECK(now.presents + 3u >= slow[i].presents && now.presents <= slow[i].presents + 3u);
        CHECK(before.presents + 3u >= slow[i].before && before.presents <= slow[i].before + 3u);
        CHECK(before.backoffs == slow[i].beforeBackoffs && now.presents >= before.presents);
        CHECK(now.shown == now.frames);
    }
    {
        /* the reads' jump (testStepRule): the miss is the pictures' */
        const RtLoad L = {9 * MS / 2,  MS / 10, 10 * MS, 16 * MS / 5, 8 * MS, 16 * MS,
                          16 * MS / 5, 240,     120,     0,           0,      MS / 2};
        const RtRun r = rtTimerLoopRule(600, &L, RT_STEP, 3 * MS, false);

        CHECK(r.misses == 1 && r.pictureMisses == 1 && r.gameMisses == 0 && r.backoffs == 0);
        CHECK(r.overFloor == 1 && r.strict == 0);
    }
    {
        /* rc9 (2. above) */
        static const struct {
            const char *what;
            unsigned misses, pictures, overFloor;
            unsigned presents, strict, strictBlockMax, blockMin, backoffs;
            unsigned beforePresents, beforeStrict;
        } cells[] = {
            {"S: then 12 / 12.5 ms longer", 25, 24, 13, 3994, 0, 0, 397, 0, 3031, 5819},
            {"M: then 12-18 ms longer", 25, 24, 16, 3982, 0, 0, 397, 0, 3031, 5819},
            {"B: then three over the floor within 300 steps", 6, 5, 5, 3975, 120, 120, 380, 1, 3548,
             2722},
        };

        const RtLoad L = {9 * MS / 2, MS / 10, 13 * MS, 6 * MS, 25 * MS, 0, 0, 0, 0, 0, 0, 3 * MS};
        unsigned at[3][24];
        uint64_t ns[3][24];
        unsigned n[3] = {0, 0, 0};

        for (unsigned c = 0; c < 2; c++) {
            static const uint64_t small[] = {12 * MS, 25 * MS / 2};
            static const uint64_t mixed[] = {12 * MS,     15 * MS, 18 * MS,
                                             25 * MS / 2, 14 * MS, 13 * MS};
            const uint64_t *x = c == 0 ? small : mixed;
            const unsigned xn = c == 0 ? 2u : 6u;

            at[c][n[c]] = 300;
            ns[c][n[c]++] = 26 * MS;
            for (unsigned k = 550, j = 0; k < 6000; k += 250, j++) {
                at[c][n[c]] = k;
                ns[c][n[c]++] = x[j % xn];
            }
        }
        {
            static const unsigned bAt[] = {300, 1000, 1100, 1200};
            static const uint64_t bNs[] = {26 * MS, 15 * MS, 16 * MS, 17 * MS};

            for (unsigned j = 0; j < 4; j++) {
                at[2][n[2]] = bAt[j];
                ns[2][n[2]++] = bNs[j];
            }
        }
        for (unsigned c = 0; c < 3; c++) {
            RtRun now, before;
            char what[128];

            s_rtGame.spikeAt = at[c];
            s_rtGame.spikeNs = ns[c];
            s_rtGame.spikes = n[c];
            s_rtGame.rule = RT_RULE_NOW;
            now = rtTimerLoopRule(6000, &L, RT_STEP, 3 * MS, false);
            s_rtGame.rule = RT_RULE_HANDOVER;
            before = rtTimerLoopRule(6000, &L, RT_STEP, 3 * MS, false);
            memset(&s_rtGame, 0, sizeof s_rtGame);
            snprintf(what, sizeof what, "rc9, 100 s, one picture's reads 26 ms longer, %s",
                     cells[c].what);
            rtPrint(what, &now);
            printf("  %u misses (%u the pictures', %u over the floor), %u steps at the whole "
                   "period (at most %u in a block), %u back-offs, %u widenings, the fewest "
                   "presents in a block %u; the rule before: %u presents, %u steps at the whole "
                   "period\n",
                   now.misses, now.pictureMisses, now.overFloor, now.strict, now.strictBlockMax,
                   now.backoffs, now.widens, now.blockPresentsMin, before.presents, before.strict);
            CHECK(now.misses == cells[c].misses && now.pictureMisses == cells[c].pictures);
            CHECK(now.overFloor == cells[c].overFloor && now.backoffs == cells[c].backoffs);
            CHECK(now.strict == cells[c].strict && now.strictBlockMax == cells[c].strictBlockMax);
            /* under 50 steps a block at the whole period on average */
            CHECK(now.strict < 50u * 10u);
            CHECK(now.presents + 3u >= cells[c].presents && now.presents <= cells[c].presents + 3u);
            CHECK(now.blockPresentsMin + 3u >= cells[c].blockMin);
            CHECK(before.presents + 3u >= cells[c].beforePresents &&
                  before.presents <= cells[c].beforePresents + 3u);
            CHECK(before.strict + 3u >= cells[c].beforeStrict &&
                  before.strict <= cells[c].beforeStrict + 3u);
            CHECK(now.shown == now.frames && now.resyncs == 0);
            /* every block near the GPU's 40 a second (the rule before: 30) */
            CHECK(now.blockPresentsMin >= 380u);
        }
    }
}

/* The rule by the game's frame (pace_policy.h PaceStepTyp,
   pace_step_plan_ns; pace_rt_step_miss's afterNs and nextBetween).  The
   game's frame is two steps: the one that closes it carries the game's
   work, the one between frames under 1 ms and no renderer call in play (a
   PowerVR phone's log, build-host/tmp/v098/evidence/rc10/ico-pc.log: step
   4.4-5.1 ms on average, every slow step line, 8-10.8 ms, at the vsync that
   closes a frame, 4-17 read waits a block although the frame's own
   picture's reads ran past the step between nearly every frame).  The rule
   blind to that (s_rtGame.blind: one recent maximum, 8.4-8.8 ms, for every
   step, and the step after the next always behind a new frame's picture)
   held the in-between picture whenever the frame's picture would start the
   step between a few ms late.  Numbers from build-host/tmp/v098/evidence/
   pace3/rc10_cells.py (.out), the C line for line, re-run with the render
   thread's typical times (PACE4: build-host/tmp/v098/evidence/pace4/
   c_cells.py, .out; the reads measured with their waits for the GPU and
   the submit thread vary from picture to picture, so their typical time
   is under their recent longest even at a steady load).  rc10's load: reads 12
   ms (4.2 ms interpolation), the submit thread's job 7 ms, its present 3
   ms, vsync off uncapped on a 120 Hz mailbox, the closing step 8-10 ms in
   turn, the step between 0.5 ms.
   1. By the GPU's time: 21.7 / 23 / 25 / 26 ms give 451 / 433 / 399 / 385
      presents in 10 s (the blind rule 390 / 389 / 376 / 374; the GPU
      makes 461 / 435 / 400 / 385); with the driver's back-pressure at 25
      ms 349 (337; PACE5's pipe, pace5/c_cells.out: the submit thread's
      job carries the present's wait, the fences stay at two in flight;
      318 and 319 with the picture before's fence).
   2. The CPU side alone (8 ms GPU pictures), the closing step 8.5 / 9.5 ms
      (an average step of 4.5 / 5 ms), reads 8 / 10 / 10.5 / 12 ms: 602 /
      600 / 600 / 500 and 601 / 600 / 597 / 450 (blind: 653 / 600 / 600 /
      391 and 601 / 600 / 552 / 301).  A frame's two pictures chain the
      closing step, the frame's picture's reads, its submit and the further
      picture's reads past its interpolation (9.5 + 0.1 + 12 + 7 + 7.8 =
      36.4 ms against the frame's 33.4 at reads 12): the lateness left
      carries into the next frame until a further picture is held.
   Every cell: no step misses its period, every frame shown, no resync,
   and no step starts later than the closing step's slack (period less its
   time and the margin) and 0.1 ms.
   3. The step between calling the renderer too (the rule's assumption
      when it cannot know): reads 10 / 12 ms give 600 / 487 (blind 600 /
      428), no miss, no step later than the step between's own slack. */
static void testFrameShape(void)
{
    static const uint64_t w[8] = {8 * MS,      9 * MS,      17 * MS / 2, 10 * MS,
                                  15 * MS / 2, 19 * MS / 2, 17 * MS / 2, 9 * MS};

    static const struct {
        uint64_t gpuNs;
        unsigned depth, shape, blind;
    } gpu[] = {{21700000u, 0, 451, 390},
               {23 * MS, 0, 433, 389},
               {25 * MS, 0, 399, 376},
               {26 * MS, 0, 385, 374},
               {25 * MS, 1, 349, 337}};

    static const struct {
        uint64_t closeNs, readsNs;
        unsigned shape, blind;
    } cpu[] = {{17 * MS / 2, 8 * MS, 602, 653},      {17 * MS / 2, 10 * MS, 600, 600},
               {17 * MS / 2, 21 * MS / 2, 600, 600}, {17 * MS / 2, 12 * MS, 500, 391},
               {19 * MS / 2, 8 * MS, 601, 601},      {19 * MS / 2, 10 * MS, 600, 600},
               {19 * MS / 2, 21 * MS / 2, 597, 552}, {19 * MS / 2, 12 * MS, 450, 301}};

    static const struct {
        uint64_t readsNs;
        unsigned shape, blind;
    } loud[] = {{10 * MS, 600, 600}, {12 * MS, 487, 428}};

    for (unsigned c = 0; c < 3; c++) {
        const unsigned n = c == 0   ? (unsigned)(sizeof(gpu) / sizeof(gpu[0]))
                           : c == 1 ? (unsigned)(sizeof(cpu) / sizeof(cpu[0]))
                                    : (unsigned)(sizeof(loud) / sizeof(loud[0]));

        for (unsigned i = 0; i < n; i++) {
            const uint64_t reads = c == 0 ? 12 * MS : c == 1 ? cpu[i].readsNs : loud[i].readsNs;
            const uint64_t g = c == 0 ? gpu[i].gpuNs : 8 * MS;
            const unsigned depth = c == 0 ? gpu[i].depth : 0;
            const unsigned want = c == 0 ? gpu[i].shape : c == 1 ? cpu[i].shape : loud[i].shape;
            const unsigned wantBlind = c == 0   ? gpu[i].blind
                                       : c == 1 ? cpu[i].blind
                                                : loud[i].blind;
            const uint64_t one = c == 1 ? cpu[i].closeNs : 0;
            const RtLoad L = {9 * MS / 2, MS / 10, reads, 7 * MS, g, 0, 0, 0, 0, 0, depth, 3 * MS};
            RtRun shape, blind;
            char what[160];

            s_rtGame.frameSeq = c == 1 ? &one : w;
            s_rtGame.frameSeqN = c == 1 ? 1u : 8u;
            s_rtGame.fieldNs = MS / 2;
            s_rtGame.fieldQuiet = c != 2;
            shape = rtTimerLoopRule(600, &L, RT_ASMADE, 21 * MS / 5, false);
            s_rtGame.blind = true;
            blind = rtTimerLoopRule(600, &L, RT_ASMADE, 21 * MS / 5, false);
            memset(&s_rtGame, 0, sizeof s_rtGame);
            snprintf(what, sizeof what,
                     "the frame's shape: reads %.1f ms, GPU %.1f ms, depth %u, closing step %s",
                     (double)reads / 1e6, (double)g / 1e6, depth,
                     c == 1 ? (one == 17 * MS / 2 ? "8.5 ms" : "9.5 ms") : "8-10 ms");
            rtPrint(what, &shape);
            printf("  %u presents blind to the shape; %u steps missed their period, latest start "
                   "%.1f ms\n",
                   blind.presents, shape.misses, (double)shape.lateMax / 1e6);
            CHECK(shape.presents + 3u >= want && shape.presents <= want + 3u);
            CHECK(blind.presents + 3u >= wantBlind && blind.presents <= wantBlind + 3u);
            CHECK(shape.misses == 0 && shape.resyncs == 0 && shape.shown == shape.frames);
            CHECK(shape.behind == 0);
            {
                /* the lag: under the closing step's slack (its plan 9.2 ms
                   here, the bound by 9.6), the step between's when it calls
                   the renderer too */
                const uint64_t closeMax = c == 1 ? one : 9600000u;
                const uint64_t bound = c == 2 ? NTSC - MS / 2 - PACE_RT_STEP_MARGIN_NS
                                              : NTSC - closeMax - PACE_RT_STEP_MARGIN_NS;

                CHECK(shape.lateMax <= bound + MS / 10);
            }
            if (c != 0 || gpu[i].depth == 0) {
                /* or two a frame: the blind rule plans the 8.5 ms closing
                   step at the recent maximum of both kinds (7.5 ms) and
                   gives some frames a third picture, 4.8 ms late */
                CHECK(shape.presents + 3u >= blind.presents || shape.presents >= 2u * shape.frames);
            }
        }
    }
}

/* rc11's load tables (tenths of a ms; build-host/tmp/v098/evidence/pace4/
   rc11_load.py): each picture's reads, 100 + 25 Z, and its submit, 80 + 15
   Z', Z a pseudo-random deviate of 1; testRc11's, and testRc12's scaled */
static const unsigned short s_rc11ReadsT[61] = {
    75,  107, 96,  56,  105, 121, 141, 80,  113, 113, 156, 124, 73, 109, 91,  128,
    147, 80,  69,  91,  120, 72,  85,  109, 65,  93,  88,  66,  99, 79,  140, 125,
    118, 87,  111, 58,  103, 119, 96,  159, 73,  108, 99,  91,  69, 150, 124, 94,
    77,  106, 87,  136, 123, 64,  101, 83,  107, 66,  81,  93,  98};
static const unsigned short s_rc11RestT[61] = {
    86, 59,  76,  73, 60, 79, 68,  104, 95, 91, 72, 87, 55,  82, 91, 78, 115, 64, 85, 80, 74,
    61, 110, 95,  76, 66, 83, 72,  101, 94, 59, 81, 70, 84,  59, 69, 76, 79,  65, 84, 77, 54,
    83, 92,  105, 68, 88, 88, 113, 94,  64, 86, 74, 97, 108, 68, 62, 75, 92,  63, 71};

/* The render thread's typical times (pace_policy.h PaceRtTyp: a decaying
   mean and half the deviation) against their recent maxima at rc11's load
   (build-host/tmp/v098/evidence/rc11/ico-pc.log, a PowerVR phone, stage 6:
   the clause planned by reads of 10.2-17.7 ms, 5.0-8.7 ms of them the
   interpolation, the closing step at 9.9-12.0 ms, where the same blocks'
   means were about 10, 3.3-4.0 and 9; 529-589 further presents held a
   block, 30-32 presented fps at GPU spans of 24-26 ms).  The load
   (build-host/tmp/v098/evidence/pace4/rc11_load.py, .out): each picture's
   reads from a table of 61 (a mean of 10 ms, a deviation of 2.5 ms), its
   interpolation 3.6 ms and 0.6 of the reads' deviation, the submit
   thread's job 8 ms (a deviation of 1.5 ms), the closing step 8.5, 9.3,
   7.8, 9.8, 8.4, 9.3, 7.5, 9.8, 8.0, 8.3, 9.3, 14.4, 8.4, 8.2, 7.6, 9.4 ms
   in turn (9.0 on average), the step between 0.6, 1.4, 0.8, 1.2 ms with no
   renderer call, the present 3 ms, vsync off uncapped on a 120 Hz mailbox.
   Numbers from pace4/c_cells.py (.out; rc11_cells.out the same runs with
   the estimates' averages), the C line for line.  Over 100 s:
     GPU 24 / 25 / 26 ms: 4098 / 3947 / 3814 presents (41.0 / 39.5 / 38.1
     a second) where the recent maxima give 3482 / 3435 / 3429 (34.8 /
     34.4 / 34.3; the GPU makes 41.7 / 40 / 38.5);
     the steps that missed their period, all the pictures': 14 / 22 / 2
     (7 / 8 / 0 over the 2 ms floor, each widening the margin; the latest
     2.8 / 3.5 / 1.3 ms past it), one fall-back of 120 steps at 24 and 25
     ms, in the first 10 s while the means learn the load; the recent
     maxima none;
     the lag (a step's start past its due time) at most 8.3 / 7.0 / 6.7 ms
     (the maxima 4.2 / 6.5 / 3.5): the sound, which the game feeds up to
     120 ms ahead (docs/port/AUDIO.md: about 78-100 ms late before a hold),
     is never held back by it.
   In 10 s at 25 ms, the driver's back-pressure (depth 1): 343 (322; PACE5's
   pipe, pace5/c_cells.out, where the picture before's fence gave 320 /
   317); the step between calling the renderer too: 397 (341), no miss. */
static void testRc11(void)
{
    static const unsigned short closeT[16] = {85, 93, 78, 98,  84, 93, 75, 98,
                                              80, 83, 93, 144, 84, 82, 76, 94};
    static const unsigned short fieldT[4] = {6, 14, 8, 12};

    static const struct {
        uint64_t gpuNs;
        unsigned presents, misses, overFloor, backoffs, strict, before;
        uint64_t missMax, lateMax, beforeLate;
    } cells[] = {{24 * MS, 4098, 14, 7, 1, 120, 3482, 2790001u, 8333334u, 4153334u},
                 {25 * MS, 3947, 22, 8, 1, 120, 3435, 3456669u, 7033334u, 6533334u},
                 {26 * MS, 3814, 2, 0, 0, 0, 3429, 1270003u, 6693334u, 3493334u}};

    static uint64_t reads[61], interp[61], rest[61], close[16], field[4];

    for (unsigned i = 0; i < 61; i++) {
        reads[i] = (uint64_t)s_rc11ReadsT[i] * MS / 10u;
        interp[i] =
            (uint64_t)((int64_t)(36 * MS / 10) + ((int64_t)reads[i] - (int64_t)(10 * MS)) * 6 / 10);
        rest[i] = (uint64_t)s_rc11RestT[i] * MS / 10u;
    }
    for (unsigned i = 0; i < 16; i++) {
        close[i] = (uint64_t)closeT[i] * MS / 10u;
    }
    for (unsigned i = 0; i < 4; i++) {
        field[i] = (uint64_t)fieldT[i] * MS / 10u;
    }
    for (unsigned c = 0; c < 5; c++) {
        const uint64_t g = c < 3 ? cells[c].gpuNs : 25 * MS;
        const unsigned depth = c == 3 ? 1u : 0u;
        const unsigned steps = c < 3 ? 6000u : 600u;
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 8 * MS, g, 0, 0, 0, 0, 0, depth, 3 * MS};
        RtRun now, before;
        char what[160];

        for (unsigned pass = 0; pass < 2; pass++) {
            memset(&s_rtGame, 0, sizeof s_rtGame);
            s_rtGame.frameSeq = close;
            s_rtGame.frameSeqN = 16;
            s_rtGame.fieldNs = MS;
            s_rtGame.fieldSeq = field;
            s_rtGame.fieldSeqN = 4;
            s_rtGame.fieldQuiet = c != 4;
            s_rtGame.readsSeq = reads;
            s_rtGame.readsSeqN = 61;
            s_rtGame.interpSeq = interp;
            s_rtGame.interpSeqN = 61;
            s_rtGame.restSeq = rest;
            s_rtGame.restSeqN = 61;
            s_rtGame.maxEst = pass == 1;
            if (pass == 0) {
                now = rtTimerLoopRule(steps, &L, RT_ASMADE, 36 * MS / 10, false);
            } else {
                before = rtTimerLoopRule(steps, &L, RT_ASMADE, 36 * MS / 10, false);
            }
        }
        memset(&s_rtGame, 0, sizeof s_rtGame);
        snprintf(what, sizeof what, "rc11's load, the typical times: GPU %.0f ms%s, %u s",
                 (double)g / 1e6,
                 c == 3   ? ", depth 1"
                 : c == 4 ? ", the step between calling the renderer"
                          : "",
                 steps / 60u);
        rtPrint(what, &now);
        printf("  %u misses (%u the pictures', %u over the floor; latest %.1f ms), %u back-offs, "
               "%u steps at the whole period, latest start %.1f ms; the recent maxima: %u "
               "presents, %u misses, latest start %.1f ms\n",
               now.misses, now.pictureMisses, now.overFloor, (double)now.missMax / 1e6,
               now.backoffs, now.strict, (double)now.lateMax / 1e6, before.presents, before.misses,
               (double)before.lateMax / 1e6);
        CHECK(now.shown == now.frames && now.resyncs == 0 && now.behind == 0);
        CHECK(before.shown == before.frames && before.resyncs == 0);
        if (c < 3) {
            CHECK(now.presents + 3u >= cells[c].presents && now.presents <= cells[c].presents + 3u);
            CHECK(before.presents + 3u >= cells[c].before &&
                  before.presents <= cells[c].before + 3u);
            /* 3.5 a second more at least */
            CHECK(now.presents >= before.presents + 350u);
            CHECK(now.misses == cells[c].misses && now.pictureMisses == now.misses);
            CHECK(now.overFloor == cells[c].overFloor && now.backoffs == cells[c].backoffs);
            CHECK(now.strict == cells[c].strict && now.strictBlockMax == cells[c].strict);
            CHECK(now.missMax == cells[c].missMax && now.lateMax == cells[c].lateMax);
            CHECK(before.misses == 0 && before.lateMax == cells[c].beforeLate);
            /* a few misses a 10 s block at most, a few ms each, and the
               game's clock never more than 10 ms behind (the sound's lead
               up to 120 ms) */
            CHECK(now.misses <= 30u && now.missMax < 4 * MS && now.lateMax < 10 * MS);
            /* the fewest presents in a block: the first, while the means
               learn */
            CHECK(now.blockPresentsMin + 3u >= (c == 0 ? 392u : c == 1 ? 378u : 381u));
        } else if (c == 3) {
            CHECK(now.presents + 3u >= 343u && now.presents <= 346u && now.misses == 0);
            CHECK(before.presents + 3u >= 322u && before.presents <= 325u);
        } else {
            CHECK(now.presents + 3u >= 397u && now.presents <= 400u && now.misses == 0);
            CHECK(before.presents + 3u >= 341u && before.presents <= 344u);
            CHECK(now.lateMax < 13 * MS);
        }
    }
}

/* rc12's three blocks' closing steps and steps between (tenths of a ms;
   pace5/rc12_load.py frame): testRc12's, and testRc13's native cells */
static const unsigned short s_rc12CloseT[3][16] = {
    {60, 128, 77, 66, 107, 87, 124, 106, 85, 104, 71, 84, 82, 81, 72, 86},
    {47, 126, 66, 54, 102, 79, 122, 101, 77, 99, 60, 75, 73, 72, 60, 77},
    {16, 127, 43, 26, 93, 61, 121, 91, 57, 88, 34, 55, 52, 51, 35, 58}};
static const unsigned short s_rc12FieldT[3][4] = {{6, 14, 8, 12}, {3, 7, 4, 6}, {2, 3, 2, 4}};

/* rc12 (build-host/tmp/v098/evidence/rc12/ico-pc.log, a phone, stage 6,
   vsync off uncapped on a 120 Hz mailbox): three 10 s blocks, the rule
   holding 495-584 further presents each, at 44.6 / 32.6 / 31.0 presented
   fps where the GPU's spans (22.4 / 21.7 / 29.5 ms) make 44.6 / 46.1 /
   33.9.  The block of shorter reads and a shorter closing step (:654:
   reads 9.1 ms typically against 13.4, the closing step 9.2 against 9.9)
   held more.  The discrepancy (build-host/tmp/v098/evidence/pace5/
   rc12_cells.py, .out, rc12_partly.out, c_cells.py, .out; the C line for
   line): the present call's wait the rule reads is its recent longest
   (5.2 / 7.6 / 4.5 ms at the blocks' ends), and at half the 120 Hz
   refresh or more the pipe planned the fence of the picture before, one
   picture in flight: every further picture then waits for the frame's own
   picture's whole GPU time (21.7 ms and more) and is held, 30 fps.  With
   the submit thread on, that wait is the submit thread's (its job, 7.7
   ms, which the next picture's first graphics call waits for already),
   and the GPU keeps two pictures in flight (block :601 made 44.6 fps at a
   22.4 ms span, the GPU's own rate, with presents of 3.9 ms on average).
   The loads (pace5/rc12_load.py): per picture its own reads, its
   interpolation (the load's mean and half the reads' deviation) and the
   submit thread's job, rc11's tables of 61 scaled (in integers), the
   closing step from 16 and the step between from 4 (tenths of a ms
   below), means chosen so the typical times the rule plans by are the
   clause's (A: own reads 12.4 ms, interpolation 5.4, submit 7.7, closing
   9.9, between 1.2; B: 8.7, 3.3, 7.6, 9.3, 0.6; C: 10.0, 3.1, 8.2, 8.0,
   0.3).  In 10 s:
     the pipe as before (the picture before's fence whenever the present
     waited half a refresh): presents waiting 1 ms 410 / 462 / 340, 5 ms
     302 / 302 / 301: the phone's 44.6 is A's presents under half a
     refresh (the model 3.6 below it), its 32.6 and 31.0 B and C over it
     most of the time (rc12_partly.out: B 34.9 at 77 % of the asks, C
     30.5 at 50 %);
     the pipe now: 410 / 462 / 340 whatever the present waited, B's and
     C's the GPU's rates with no miss; A's load is the tight one (reads of
     11 ms with a deviation of 2.8): 2 misses (1 over the floor) and one
     fall-back of 120 steps in the first 10 s, with the present's wait
     under half a refresh as much as over it; over 100 s 4274 / 4601 /
     3392, the blocks after the first 428 / 459 / 339 at least, no start
     later than 8.4 ms. */
static void testRc12(void)
{
    const unsigned short (*closeT)[16] = s_rc12CloseT;
    const unsigned short (*fieldT)[4] = s_rc12FieldT;

    static const struct {
        const char *what;
        uint64_t gpuNs, interpNs; /* the GPU's span, the interpolation's mean */
        /* the reads' and the submit's mean and deviation (tenths of a ms) */
        int readsMean, readsDev, restMean, restDev;
        double phone;            /* the phone's presented fps */
        unsigned near;           /* the run the phone's regime is (0: presents of 1 ms, 1: 5 ms) */
        double nearFps;          /* how near */
        unsigned low, high, now; /* presents before (1 ms, 5 ms presents), now (either) */
        unsigned misses, overFloor, strict, long100, misses100, backoffs100, blockMin;
    } cells[3] = {{"A (:601)", 22400000u, 4800000u, 110, 28, 70,  15,   44.6, 0, 4.0,
                   410,        302,       410,      2,   1,  120, 4274, 39,   1, 410},
                  {"B (:654)", 21700000u, 2800000u, 76, 22, 69, 15,   32.6, 1, 2.5,
                   462,        302,       462,      0,  0,  0,  4601, 6,    0, 459},
                  {"C (:772)", 29500000u, 2500000u, 88, 25, 73, 18,   31.0, 1, 1.0,
                   340,        301,       340,      0,  0,  0,  3392, 0,    0, 339}};

    static uint64_t reads[61], interp[61], rest[61], close[16], field[4], presLow[1], presHigh[1];

    presLow[0] = MS;
    presHigh[0] = 5 * MS;
    for (unsigned c = 0; c < 3; c++) {
        uint64_t sum = 0;

        for (unsigned i = 0; i < 61; i++) {
            /* rc12_load.py seq: rc11's tables scaled, the division
               truncated toward zero */
            const int r =
                cells[c].readsMean + ((int)s_rc11ReadsT[i] - 100) * cells[c].readsDev / 25;
            const int b = cells[c].restMean + ((int)s_rc11RestT[i] - 80) * cells[c].restDev / 15;

            reads[i] = (uint64_t)(r > 1 ? r : 1) * MS / 10u;
            rest[i] = (uint64_t)(b > 1 ? b : 1) * MS / 10u;
            sum += reads[i];
        }
        for (unsigned i = 0; i < 61; i++) {
            /* rc12_load.py lin: the mean and half the reads' deviation */
            const int64_t v =
                (int64_t)cells[c].interpNs + ((int64_t)reads[i] - (int64_t)(sum / 61u)) / 2;

            interp[i] = v > (int64_t)(MS / 10) ? (uint64_t)v : MS / 10;
        }
        for (unsigned i = 0; i < 16; i++) {
            close[i] = (uint64_t)closeT[c][i] * MS / 10u;
        }
        for (unsigned i = 0; i < 4; i++) {
            field[i] = (uint64_t)fieldT[c][i] * MS / 10u;
        }
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 7 * MS, cells[c].gpuNs, 0, 0, 0,
                          0,          0,       0,       0};
        RtRun run[5];

        /* 0, 1: before, presents of 1 / 5 ms; 2, 3: now; 4: now over 100 s */
        for (unsigned k = 0; k < 5; k++) {
            memset(&s_rtGame, 0, sizeof s_rtGame);
            s_rtGame.frameSeq = close;
            s_rtGame.frameSeqN = 16;
            s_rtGame.fieldNs = MS;
            s_rtGame.fieldSeq = field;
            s_rtGame.fieldSeqN = 4;
            s_rtGame.fieldQuiet = true;
            s_rtGame.readsSeq = reads;
            s_rtGame.readsSeqN = 61;
            s_rtGame.interpSeq = interp;
            s_rtGame.interpSeqN = 61;
            s_rtGame.restSeq = rest;
            s_rtGame.restSeqN = 61;
            s_rtGame.presentSeq = k == 0 || k == 2 ? presLow : presHigh;
            s_rtGame.presentSeqN = 1;
            s_rtGame.oldBehind = k < 2;
            run[k] = rtTimerLoopRule(k == 4 ? 6000u : 600u, &L, RT_ASMADE, 3 * MS, false);
        }
        memset(&s_rtGame, 0, sizeof s_rtGame);
        {
            char what[160];

            snprintf(what, sizeof what, "rc12 block %s, GPU %.1f ms, presents waiting 5 ms",
                     cells[c].what, (double)cells[c].gpuNs / 1e6);
            rtPrint(what, &run[3]);
        }
        printf("  the phone %.1f fps; before %u / %u presents (presents of 1 / 5 ms; %u / %u asks "
               "with the picture before's fence, %u / %u held by the next step), now %u / %u (%u / "
               "%u), over 100 s %u, %u misses\n",
               cells[c].phone, run[0].presents, run[1].presents, run[0].fenceBefore,
               run[1].fenceBefore, run[0].byNext, run[1].byNext, run[2].presents, run[3].presents,
               run[2].fenceBefore, run[3].fenceBefore, run[4].presents, run[4].misses);
        for (unsigned k = 0; k < 5; k++) {
            CHECK(run[k].shown == run[k].frames && run[k].resyncs == 0 && run[k].behind == 0);
        }
        /* before: the phone's regime, every ask behind at 5 ms presents
           held by the next step's start: 30 fps */
        CHECK(run[0].presents + 3u >= cells[c].low && run[0].presents <= cells[c].low + 3u);
        CHECK(run[1].presents + 3u >= cells[c].high && run[1].presents <= cells[c].high + 3u);
        CHECK(run[0].fenceBefore == 0 && run[1].fenceBefore + 5u >= run[1].asks);
        CHECK(run[1].byNext + run[1].byAfter + 40u >= run[1].held && run[1].misses == 0);
        /* the phone's fps near the run of its regime (A's presents under
           half a refresh, B's and C's over it most of the time) */
        {
            const double fps = run[cells[c].near].presents / 10.0;

            CHECK(cells[c].phone >= fps - cells[c].nearFps &&
                  cells[c].phone <= fps + cells[c].nearFps);
        }
        /* now: the present's wait changes nothing, the GPU's rate */
        for (unsigned k = 2; k < 4; k++) {
            CHECK(run[k].presents + 3u >= cells[c].now && run[k].presents <= cells[c].now + 3u);
            CHECK(run[k].fenceBefore == 0 && run[k].misses == cells[c].misses);
            CHECK(run[k].overFloor == cells[c].overFloor && run[k].strict == cells[c].strict);
            CHECK((uint64_t)run[k].presents * cells[c].gpuNs <= 601u * NTSC + 3u * cells[c].gpuNs);
        }
        CHECK(run[2].presents == run[3].presents && run[2].lateMax == run[3].lateMax);
        CHECK(run[4].presents + 3u >= cells[c].long100 && run[4].presents <= cells[c].long100 + 3u);
        CHECK(run[4].misses == cells[c].misses100 && run[4].strict == cells[c].strict);
        CHECK(run[4].backoffs == cells[c].backoffs100 && run[4].strictBlockMax == cells[c].strict);
        CHECK(run[4].blockPresentsMin + 3u >= cells[c].blockMin && run[4].lateMax < 10 * MS);
        CHECK(run[4].missMax < 4 * MS);
    }
}

/* PACE6: rc13's blocks (build-host/tmp/v098/evidence/rc13/ico-pc.log, the
   PowerVR phone, stage 6, vsync off uncapped on a 120 Hz mailbox) in
   VKSLOTS' world (s_rtGame.slots: the frame slots apart, the pipe's GPU
   time the median of the records).  O (:572-:579) the Original preset,
   512x448: reads of 8.7 ms on average (9.1 of its own typical, 2.2 the
   interpolation), the submit thread's job 7.2 ms (4.6 before the GPU can
   start), the closing step 3.4 ms, the GPU 14.8 ms a picture (the block's
   mean of 14.76; the window line's 'GPU span 1.9 ms' is the last 8's
   median at the block's end, the loading pictures after it), 45.6
   presented fps on the phone; E (:977-:981) the Enhanced preset at the
   window's 1x (848x448), 45.5; X (:909-:913) the Enhanced preset at 2x
   (1696x896) for part of its block, 34.3; and rc12's native blocks
   (2364x1080, testRc12's loads) in the same world.  "As run" is rc13's
   build: the rule read the submit thread's job of 0 (the slots apart) as
   the thread off and planned the picture before's fence with the presents
   waiting 5 ms (half the refresh or more): 46.0 / 45.1 / 30.2 presented
   fps, the phone's 45.6 / 45.5 (X's block changed resolution inside it);
   now (render_thread.h IcoRtPlan.submitOn, handed on by window_present.c
   rt_ask) 67.1 / 65.3 / 54.5 and 43.1 / 45.8 / 33.9 for rc12's, about the
   GPU's own rate (67.6 / 68.5 / 56.2, 44.6 / 46.1 / 33.9): the Original
   preset past the display's 60.  With the GPU's time alternating 21.0 /
   8.6 ms by the picture (the slow replay lines :768-:779: 21.2, 8.2, 21.1,
   8.1 on consecutive replays, the same mean) 63.3.  Numbers from
   build-host/tmp/v098/evidence/pace6/c_cells_rc13.py (.out), the loop's
   port */
static void testRc13(void)
{
    /* O, E, X (rc13_load.py frame); rc12's from s_rc12CloseT */
    static const unsigned short closeT[3][16] = {
        {20, 52, 28, 23, 42, 33, 50, 41, 32, 41, 25, 31, 30, 30, 25, 32},
        {19, 119, 43, 28, 88, 59, 113, 86, 56, 84, 36, 54, 51, 50, 36, 57},
        {69, 125, 83, 74, 108, 92, 122, 107, 90, 106, 79, 89, 87, 87, 79, 91}};
    static const unsigned short fieldT[3][4] = {{5, 8, 6, 10}, {3, 5, 4, 4}, {3, 5, 4, 4}};

    static const struct {
        const char *what;
        uint64_t gpuNs, interpNs, subPartNs;        /* the GPU's time, the interpolation's mean */
        int readsMean, readsDev, restMean, restDev; /* tenths of a ms */
        double phone;                               /* the phone's presented fps (0: none) */
        double near;                                /* how near the run as run */
        unsigned asRun, now, misses, overFloor, strict, backoffs;
    } cells[6] = {{"O (rc13 :572, the Original preset, 512x448)", 14800000u, 2200000u, 4600000u, 78,
                   26, 72, 15, 45.6, 0.5, 460, 671, 0, 0, 0, 0},
                  {"E (rc13 :977, Enhanced, 848x448)", 14600000u, 2500000u, 4300000u, 76, 24, 69,
                   15, 45.5, 0.5, 451, 653, 6, 3, 120, 1},
                  {"X (rc13 :909, Enhanced, 1696x896)", 17800000u, 2900000u, 4500000u, 79, 25, 69,
                   15, 34.3, 4.5, 302, 545, 3, 1, 0, 0},
                  {"rc12 A (:601, 2364x1080)", 22400000u, 4800000u, 4600000u, 110, 28, 70, 15, 0.0,
                   0.0, 302, 431, 2, 0, 0, 0},
                  {"rc12 B (:654, 2364x1080)", 21700000u, 2800000u, 4600000u, 76, 22, 69, 15, 0.0,
                   0.0, 302, 458, 0, 0, 0, 0},
                  {"rc12 C (:772, 2364x1080)", 29500000u, 2500000u, 4600000u, 88, 25, 73, 18, 0.0,
                   0.0, 301, 339, 2, 0, 120, 1}};

    static uint64_t reads[61], interp[61], rest[61], close[16], field[4], pres[1], alt[2];

    pres[0] = 5 * MS;
    alt[0] = 21 * MS;
    alt[1] = 86 * MS / 10;
    for (unsigned c = 0; c < 6; c++) {
        uint64_t sum = 0;

        for (unsigned i = 0; i < 61; i++) {
            /* rc12_load.py seq */
            const int r =
                cells[c].readsMean + ((int)s_rc11ReadsT[i] - 100) * cells[c].readsDev / 25;
            const int b = cells[c].restMean + ((int)s_rc11RestT[i] - 80) * cells[c].restDev / 15;

            reads[i] = (uint64_t)(r > 1 ? r : 1) * MS / 10u;
            rest[i] = (uint64_t)(b > 1 ? b : 1) * MS / 10u;
            sum += reads[i];
        }
        for (unsigned i = 0; i < 61; i++) {
            /* rc12_load.py lin */
            const int64_t v =
                (int64_t)cells[c].interpNs + ((int64_t)reads[i] - (int64_t)(sum / 61u)) / 2;

            interp[i] = v > (int64_t)(MS / 10) ? (uint64_t)v : MS / 10;
        }
        for (unsigned i = 0; i < 16; i++) {
            close[i] = (uint64_t)(c < 3 ? closeT[c][i] : s_rc12CloseT[c - 3][i]) * MS / 10u;
        }
        for (unsigned i = 0; i < 4; i++) {
            field[i] = (uint64_t)(c < 3 ? fieldT[c][i] : s_rc12FieldT[c - 3][i]) * MS / 10u;
        }
        const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 7 * MS, cells[c].gpuNs, 0, 0, 0,
                          0,          0,       0,       0};
        RtRun run[5];
        const unsigned runs = c == 0 ? 5u : 2u;

        /* 0: as run, 1: now; O also 2, 3: the GPU's time alternating as
           run and now, 4: now over 100 s */
        for (unsigned k = 0; k < runs; k++) {
            memset(&s_rtGame, 0, sizeof s_rtGame);
            s_rtGame.frameSeq = close;
            s_rtGame.frameSeqN = 16;
            s_rtGame.fieldNs = MS;
            s_rtGame.fieldSeq = field;
            s_rtGame.fieldSeqN = 4;
            s_rtGame.fieldQuiet = true;
            s_rtGame.readsSeq = reads;
            s_rtGame.readsSeqN = 61;
            s_rtGame.interpSeq = interp;
            s_rtGame.interpSeqN = 61;
            s_rtGame.restSeq = rest;
            s_rtGame.restSeqN = 61;
            s_rtGame.presentSeq = pres;
            s_rtGame.presentSeqN = 1;
            s_rtGame.slots = true;
            s_rtGame.asRun = k == 0 || k == 2;
            s_rtGame.gpuRecords = true;
            s_rtGame.subPartNs = cells[c].subPartNs;
            s_rtGame.gpuSeq = k == 2 || k == 3 ? alt : NULL;
            s_rtGame.gpuSeqN = k == 2 || k == 3 ? 2u : 0u;
            run[k] = rtTimerLoopRule(k == 4 ? 6000u : 600u, &L, RT_ASMADE, 3 * MS, false);
        }
        memset(&s_rtGame, 0, sizeof s_rtGame);
        {
            char what[160];

            snprintf(what, sizeof what, "rc13 %s, GPU %.1f ms, slots apart, now", cells[c].what,
                     (double)cells[c].gpuNs / 1e6);
            rtPrint(what, &run[1]);
        }
        printf("  as run %u presents (%u of %u asks with the picture before's fence), now %u (%u; "
               "%u misses, %u over the floor, %u back-offs)%s\n",
               run[0].presents, run[0].fenceBefore, run[0].asks, run[1].presents,
               run[1].fenceBefore, run[1].misses, run[1].overFloor, run[1].backoffs,
               c == 0 ? "; the GPU alternating:" : "");
        if (c == 0) {
            printf("  as run %u, now %u (%u misses, %u over the floor); now over 100 s %u, the "
                   "fewest in a block %u, %u misses\n",
                   run[2].presents, run[3].presents, run[3].misses, run[3].overFloor,
                   run[4].presents, run[4].blockPresentsMin, run[4].misses);
        }
        for (unsigned k = 0; k < runs; k++) {
            CHECK(run[k].shown == run[k].frames && run[k].resyncs == 0 && run[k].behind == 0);
        }
        /* as run: nearly every ask planned the picture before's fence, the
           phone's fps */
        for (unsigned k = 0; k < runs; k += 2) {
            if (k == 4) {
                break;
            }
            CHECK(run[k].presents + 3u >= cells[c].asRun && run[k].presents <= cells[c].asRun + 3u);
            CHECK(run[k].fenceBefore + 5u >= run[k].asks && run[k].misses == 0);
        }
        if (cells[c].phone != 0.0) {
            const double fps = run[0].presents / 10.0;

            CHECK(cells[c].phone >= fps - cells[c].near && cells[c].phone <= fps + cells[c].near);
        }
        /* now: two in flight, the GPU's rate */
        CHECK(run[1].presents + 3u >= cells[c].now && run[1].presents <= cells[c].now + 3u);
        CHECK(run[1].fenceBefore == 0 && run[1].misses == cells[c].misses);
        CHECK(run[1].overFloor == cells[c].overFloor && run[1].strict == cells[c].strict);
        CHECK(run[1].backoffs == cells[c].backoffs);
        CHECK((uint64_t)run[1].presents * cells[c].gpuNs <= 601u * NTSC + 3u * cells[c].gpuNs);
        CHECK(run[1].presents >= run[0].presents + 35u);
        if (c == 0) {
            /* the Original preset past the display's 60 a second */
            CHECK(run[1].presents >= 600u && run[3].presents >= 600u);
            CHECK(run[3].presents + 3u >= 633u && run[3].presents <= 636u);
            CHECK(run[3].fenceBefore == 0 && run[3].misses == 6 && run[3].overFloor == 2);
            CHECK(run[3].strict == 0);
            CHECK(run[4].presents + 3u >= 6725u && run[4].presents <= 6728u);
            CHECK(run[4].misses == 0 && run[4].strict == 0 && run[4].blockPresentsMin + 3u >= 671u);
        }
    }
}

/* rc14's native regime (build-host/tmp/v098/evidence/rc14/ico-pc.log
   :639-:644: stage 6, the Enhanced preset at the window's 2364x1080, the
   PowerVR DXT-48 at 120 Hz, the frame slots apart, the submit thread on,
   60.7 presented fps as made) under Android's default settings, Vertical
   sync on and Frame rate 60 (port/game/video_options.c): 60 is not the
   120 Hz refresh, so no vsync lock, a mailbox present (window_host.c), the
   paced path not as made, the cap's gap of 1e9 / 60.  The present call's
   wait 2.5 ms with one of 14 ms every 13 pictures (the phone's mean 5.6
   ms, its longest 31.8): its recent longest at or over half the 4.17 ms
   refresh in about 9 asks of 13 (the phone's 703 of 1027).  The loads:
   reads mean 8.8 dev 2.8, the interpolation 1.4, the submit thread's job
   8.3 (submit 2.7 + present 5.6), the GPU 13.0 a replay, the game's step
   5.7 (closing 10.8, between 0.4).  As before the second review the paced
   path held every further present while the presents waited half a
   refresh (the submit thread's wait read as the render thread's): 30 a
   second for 30 game frames; now that hold reads the submit thread as the
   pipe's fence does (pace_rt_pipe_behind) and the step rule decides: 53
   a second, under the cap's 60 by the steps the rule keeps whole.  The
   numbers are the Python reference's
   (build-host/tmp/v098/evidence/fixreview2/dflt_cells.py, .out). */
static void testDefaults(void)
{
    static const unsigned short closeT[16] = {57,  177, 86, 68,  140, 105, 170, 138,
                                              102, 135, 77, 100, 96,  95,  78,  103};
    static uint64_t reads[61], interp[61], rest[61], close[16], field[4], pres[13];
    uint64_t sum = 0;

    for (unsigned i = 0; i < 61; i++) {
        const int r = 88 + ((int)s_rc11ReadsT[i] - 100) * 28 / 25;
        const int b = 83 + ((int)s_rc11RestT[i] - 80) * 15 / 15;

        reads[i] = (uint64_t)(r > 1 ? r : 1) * MS / 10u;
        rest[i] = (uint64_t)(b > 1 ? b : 1) * MS / 10u;
        sum += reads[i];
    }
    for (unsigned i = 0; i < 61; i++) {
        const int64_t v = (int64_t)(14 * MS / 10) + ((int64_t)reads[i] - (int64_t)(sum / 61u)) / 2;

        interp[i] = v > (int64_t)(MS / 10) ? (uint64_t)v : MS / 10;
    }
    for (unsigned i = 0; i < 16; i++) {
        close[i] = (uint64_t)closeT[i] * MS / 10u;
    }
    field[0] = 3 * MS / 10;
    field[1] = 5 * MS / 10;
    field[2] = field[3] = 4 * MS / 10;
    pres[0] = 14 * MS;
    for (unsigned i = 1; i < 13; i++) {
        pres[i] = 25 * MS / 10;
    }
    const RtLoad L = {9 * MS / 2, MS / 10, 10 * MS, 7 * MS, 13 * MS, 0, 0, 0, 0, 0, 0, 0};
    RtRun run[4];

    /* 0: as rc14 ran (vsync off, uncapped: as made); 1: the defaults with
       the hold as before; 2: the defaults now; 3: now over 100 s */
    for (unsigned k = 0; k < 4; k++) {
        memset(&s_rtGame, 0, sizeof s_rtGame);
        s_rtGame.frameSeq = close;
        s_rtGame.frameSeqN = 16;
        s_rtGame.fieldNs = MS;
        s_rtGame.fieldSeq = field;
        s_rtGame.fieldSeqN = 4;
        s_rtGame.fieldQuiet = true;
        s_rtGame.readsSeq = reads;
        s_rtGame.readsSeqN = 61;
        s_rtGame.interpSeq = interp;
        s_rtGame.interpSeqN = 61;
        s_rtGame.restSeq = rest;
        s_rtGame.restSeqN = 61;
        s_rtGame.presentSeq = pres;
        s_rtGame.presentSeqN = 13;
        s_rtGame.slots = true;
        s_rtGame.gpuRecords = true;
        s_rtGame.subPartNs = 46 * MS / 10;
        s_rtGame.gapNs = k == 0 ? 0 : pace_present_gap(60, false, true, 8333333ull);
        s_rtGame.oldHold = k == 1;
        run[k] =
            rtTimerLoopRule(k == 3 ? 6000u : 600u, &L, k == 0 ? RT_ASMADE : RT_STEP, 3 * MS, false);
    }
    memset(&s_rtGame, 0, sizeof s_rtGame);
    rtPrint("rc14 native, the defaults (vsync on, 60 on a 120 Hz mailbox), now", &run[2]);
    printf("  as rc14 ran (as made) %u presents; the defaults before %u (%u held as behind), now "
           "%u (%u misses, %u back-offs); now over 100 s %u, the fewest in a block %u\n",
           run[0].presents, run[1].presents, run[1].behind, run[2].presents, run[2].misses,
           run[2].backoffs, run[3].presents, run[3].blockPresentsMin);
    CHECK(pace_present_gap(60, false, true, 8333333ull) == 16666666ull);
    for (unsigned k = 0; k < 4; k++) {
        CHECK(run[k].shown == run[k].frames && run[k].resyncs == 0 && run[k].fenceBefore == 0);
    }
    /* as rc14 ran: near the phone's 60.7 */
    CHECK(run[0].presents + 3u >= 568u && run[0].presents <= 571u && run[0].behind == 0);
    /* the hold as before: every further present held behind, 30 a second */
    CHECK(run[1].presents + 3u >= 301u && run[1].presents <= 304u && run[1].behind >= 500u);
    /* now: none held behind, the step rule's rate under the cap */
    CHECK(run[2].presents + 3u >= 533u && run[2].presents <= 536u && run[2].behind == 0);
    CHECK(run[2].presents >= run[1].presents + 200u && run[2].presents <= 601u);
    CHECK(run[2].overFloor == 0 && run[2].backoffs == 0 && run[2].strict == 0);
    CHECK(run[3].presents + 10u >= 5436u && run[3].presents <= 5446u && run[3].behind == 0);
    CHECK(run[3].blockPresentsMin >= 530u);
}

int main(void)
{
    test60();
    test5994();
    testRc3Pattern();
    test120();
    testPhase();
    testDrift();
    testNoClockMode();
    testClockLeft();
    testHidden();
    testLongSteps();
    testFifoModels();
    testNothingPresented();
    testFirstFrame();
    testFifoHold();
    testNominalRatio();
    testTrace();
    testPause();
    testChanges();
    testLatestReady();
    testFifoDrift();
    testFifoRatchet();
    testQueueBlocking();
    testSkipGuard();
    testLateSkipLock();
    test120Budget();
    testDiscWait126();
    testVblankClockLeft();
    testAsyncLock();
    testRoomOffGame();
    testAsyncRates();
    testRenderThreadPlan();
    testRenderThreadBehind();
    testSubmitThread();
    testStepRule();
    testBackoffCause();
    testFrameShape();
    testRc11();
    testRc12();
    testRc13();
    testDefaults();
    if (fails) {
        printf("pace_loop_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_loop_test: ok\n");
    return 0;
}
