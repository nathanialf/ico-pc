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
 *   present a step and the alphas on the grid; a GPU-bound phone (20 ms a
 *   picture at 60 Hz) presents its new frames only and keeps real time.
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

static void f_present_mailbox(Fake *f)
{
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
    f->displayWait = f->now - acquire;
    f->fenceWait = 0;
    if (f->fence && f->nextId >= 2) {
        /* the picture two before the one made now (ids from 1) */
        const uint64_t two = f->readyAt[(f->nextId - 1) % 4];

        f->fenceWait = two > f->now ? two - f->now : 0;
        f->now += f->fenceWait;
    }
    f->now += f_cost(f);
    f_display_to(f, f->now);
    uint64_t ready = (f->fence && f->lastReady > f->now ? f->lastReady : f->now) + f->gpu +
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
    bool fifo;        /* a FIFO-only swapchain */
    int clockKind;    /* the display clock offered (PACE_CLOCK_KIND_*) */
    bool noGuard;     /* the skip rule before the guard (f_waited_none) */
    double phaseFrom; /* the step's offset after a vblank measured from (s; 0: not) */
    bool budget;      /* the present allowance judged (PaceLockIo.displayWaited) */
    uint64_t gpuNs;   /* the GPU time it is told (PaceLock.gpuNs; 0: not known) */
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
    bool newFrame = true, ratioStarted = false, counting = false;

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
        f->now += vsync == o->bigAt ? o->bigStep
                                    : o->step + (o->stepJitter ? f_rand(f) % o->stepJitter : 0);
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
   With the allowance: after the first second one present a step, no step
   late, no resync, the alphas on the grid, the allowance at 1 (the GPU gate:
   2 * 12 + 3 > 16.67 keeps it there).  A GPU-bound phone (20 ms a picture
   at 60 Hz, n = 1): the rule before presents 50 a second with steps 32 ms
   late; the allowance its new frames only, 30 a second, on time.  The
   numbers printed are the trade. */
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
               "ms, %u skipped, %u refused, allowance %u (%u drops, %u climbs), %u resyncs\n",
               on ? " with the allowance" : "", (double)r.presentsFrom / (double)r.stepsFrom,
               (double)r.lateMaxFrom / 1e6, r.skippedFrom, r.L.budget.refused, r.L.budget.allow,
               r.L.budget.drops, r.L.budget.climbs, r.resyncs);
        CHECK(r.resyncsFrom == 0);
        CHECK(r.alphaOffGrid == 0);
        CHECK(r.L.skippedLate == 0); /* every new frame presented */
        if (on) {
            CHECK(r.lateMaxFrom < 8333333ull);
            CHECK(r.presentsFrom * 10 >= r.stepsFrom * 9 &&
                  r.presentsFrom * 10 <= r.stepsFrom * 11);
            CHECK(r.L.budget.allow == 1 && r.L.budget.drops >= 1 && r.L.budget.refused > 0);
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
            CHECK(r.lateMaxFrom < NTSC / 2 + 1 * MS);
            CHECK(r.L.budget.allow == 0);
            CHECK(r.presentsFrom * 2 >= r.stepsFrom - 2 && r.presentsFrom * 2 <= r.stepsFrom + 2);
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
    testVblankClockLeft();
    if (fails) {
        printf("pace_loop_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_loop_test: ok\n");
    return 0;
}
