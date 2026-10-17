/*
 * port/platform/pace_policy.c
 *
 * The slow-present decision (pace_policy.h).
 */
#include "pace_policy.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

uint64_t pace_slow_threshold(uint64_t refreshNs, uint64_t periodNs, bool injector)
{
    if (injector) {
        /* one refresh more than the plain threshold */
        return (refreshNs > periodNs ? refreshNs : periodNs) + refreshNs + periodNs / 2;
    }
    return (refreshNs > periodNs ? refreshNs : periodNs) + periodNs / 2;
}

uint64_t pace_present_gap(int cap, bool uncappedVsync, bool mailbox, uint64_t refreshNs)
{
    if (uncappedVsync) {
        /* one present a refresh, but never fewer than sixty a second: a
           display can report a refresh it is not running at (a 4K screen
           running at 60 Hz can report 30.0 Hz, and one present per
           reported refresh would halve what the screen shows), and sixty
           a second on a true 30 Hz screen costs little. A sixteenth of a refresh early: the sleep's
           overshoot and a game step that a due present lands in only make
           a present later, and the gap counts from the last present's
           start, so a gap of exactly one refresh averaged more than one */
        /* Only in mailbox mode, where a present never blocks: under FIFO a
           present past the screen's own rate waits for the next refresh and
           would hold the game's step with it. */
        const uint64_t sixty = 16666667ull;
        const uint64_t r = mailbox && refreshNs > sixty ? sixty : refreshNs;
        return r - r / 16;
    }
    return cap > 0 ? 1000000000ull / (uint64_t)cap : 0;
}

bool pace_cap_equals_refresh(int cap, uint64_t refreshNs)
{
    if (cap <= 0 || refreshNs == 0) {
        return false;
    }
    /* the reported rate in mHz, rounded */
    const uint64_t mhz = (1000000000000ull + refreshNs / 2) / refreshNs;
    const uint64_t frac = mhz % 1000;
    const uint64_t c = (uint64_t)cap * 1000ull;
    /* a whole number (to 0.01 Hz) of 100 Hz or more may be a rate cut down:
       up to 1 Hz above it (below, SDL reports 59 as 59.94: a whole 59 is a
       true 59.00 Hz mode) */
    const uint64_t above = mhz >= 99990 && (frac <= 10 || frac >= 990) ? 1000 : 500;

    return c + 500 >= mhz && c <= mhz + above;
}

bool pace_fifo_hold(uint64_t nowNs, uint64_t lastReturnNs, uint64_t refreshNs, uint64_t deadlineNs,
                    bool frameShown, bool frameHeld)
{
    if (lastReturnNs == 0 || refreshNs == 0 || nowNs < lastReturnNs) {
        return false;
    }
    /* a new frame: shown once the deadline has passed, held at most once */
    if (!frameShown && (nowNs >= deadlineNs || frameHeld)) {
        return false;
    }
    /* the vblank a present made now returns on */
    const uint64_t k = (nowNs - lastReturnNs) / refreshNs + 1;

    return lastReturnNs + k * refreshNs > deadlineNs;
}

PaceSkip pace_late_skip(bool newFrame, bool skippedLast, bool paceSlow, uint64_t nowNs,
                        uint64_t deadlineNs, uint64_t periodNs)
{
    if (nowNs < deadlineNs || !newFrame || skippedLast) {
        return PACE_SKIP_NONE;
    }
    /* a whole step period or more behind the grid */
    if (periodNs > 0 && nowNs - deadlineNs >= periodNs) {
        return PACE_SKIP_LATE;
    }
    return paceSlow ? PACE_SKIP_SLOW : PACE_SKIP_NONE;
}

bool pace_skipped_last(bool skipOpen, uint32_t skippedFrame, uint32_t frame)
{
    return skipOpen && skippedFrame != frame;
}

uint64_t pace_next_present(uint64_t nextPresent, uint64_t startNs, uint64_t gap)
{
    const uint64_t n = nextPresent + gap;

    if (nextPresent == 0 || startNs > n) {
        return startNs + gap;
    }
    return n;
}

/* the median of n costs (n <= PACE_AUTO_SAMPLES; 0 with none) */
static uint64_t median_of(const uint64_t *cost, unsigned n)
{
    uint64_t v[PACE_AUTO_SAMPLES];
    unsigned i, j;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        v[i] = cost[i];
    }
    for (i = 1; i < n; i++) {
        const uint64_t x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    /* an even count: the mean of the middle two */
    return n % 2 ? v[n / 2] : v[n / 2 - 1] / 2 + v[n / 2] / 2;
}

static uint64_t median(const PaceHist *h)
{
    return median_of(h->cost, h->count);
}

bool pace_slow_present(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                       bool injector)
{
    const uint64_t thr = pace_slow_threshold(refreshNs, periodNs, injector);
    uint64_t med;

    h->cost[h->next] = costNs;
    h->next = (h->next + 1) % PACE_HIST_LEN;
    if (h->count < PACE_HIST_LEN) {
        h->count++;
    }
    med = median(h);
    if (h->slow) {
        if (med < thr / 10 * 8) {
            h->slow = false;
            h->over = 0;
        }
    } else if (med > thr) {
        if (++h->over >= PACE_ENTER_COUNT) {
            h->slow = true;
        }
    } else {
        h->over = 0;
    }
    return h->slow;
}

/* resolution "auto": the cost samples */
void pace_samples_reset(PaceSamples *s, uint64_t nowNs)
{
    s->count = s->next = 0;
    s->firstNs = s->lastNs = nowNs;
}

void pace_samples_add(PaceSamples *s, uint64_t nowNs, uint64_t costNs)
{
    if (s->firstNs == 0) {
        s->firstNs = nowNs;
    }
    s->lastNs = nowNs;
    s->cost[s->next] = costNs;
    s->next = (s->next + 1) % PACE_AUTO_SAMPLES;
    if (s->count < PACE_AUTO_SAMPLES) {
        s->count++;
    }
}

uint64_t pace_samples_median(const PaceSamples *s)
{
    return median_of(s->cost, s->count);
}

int pace_auto_resolution_step(const PaceSamples *s, int currentScale, float windowScale)
{
    if (s->count < PACE_AUTO_MIN_SAMPLES || s->lastNs - s->firstNs < PACE_AUTO_WINDOW_NS ||
        s->budgetNs == 0) {
        return currentScale;
    }
    if (s->lastStepNs != 0 && s->lastNs - s->lastStepNs < PACE_AUTO_WINDOW_NS) {
        return currentScale;
    }
    /* over 70 % of the budget */
    if (pace_samples_median(s) * 10 <= s->budgetNs * 7) {
        return currentScale;
    }
    if (currentScale <= 0) {
        /* the window's size: the largest of 3x, 2x, 1x below it (a window
           at 1x or less has nothing below it: the scene is never smaller
           than the game's own) */
        for (int n = 3; n >= 1; n--) {
            if ((float)n < windowScale - 0.05f) {
                return n;
            }
        }
        return currentScale;
    }
    if (currentScale > 3) {
        return 3;
    }
    return currentScale > 1 ? currentScale - 1 : 1;
}

/* ------------------------------------------------------------ the vsync lock */

unsigned pace_lock_presents(int cap, uint64_t periodNs)
{
    if (cap <= 0 || periodNs == 0) {
        return 0;
    }
    /* cap / simulated rate = cap * period / 1e9 */
    const uint64_t x = (uint64_t)cap * periodNs;
    const uint64_t n = (x + 500000000ull) / 1000000000ull;

    if (n == 0) {
        return 0;
    }
    /* within 1 % of n rates: |x - n e9| <= x / 100 */
    const uint64_t d = x > n * 1000000000ull ? x - n * 1000000000ull : n * 1000000000ull - x;
    return d * 100 <= x ? (unsigned)n : 0;
}

bool pace_grid_period_ok(unsigned n, uint64_t refreshNs, uint64_t periodNs)
{
    if (n == 0 || refreshNs == 0 || periodNs == 0) {
        return false;
    }
    const uint64_t p = (uint64_t)n * refreshNs;
    const uint64_t d = p > periodNs ? p - periodNs : periodNs - p;

    return d * 100 < periodNs;
}

float pace_lock_alpha(unsigned k, unsigned n, unsigned perFrame)
{
    if (n == 0 || perFrame == 0) {
        return 0.0f;
    }
    const unsigned d = n * perFrame;

    return k >= d ? 1.0f : (float)k / (float)d;
}

void pace_lock_reset(PaceLock *L, unsigned n, uint64_t refreshNs, uint64_t periodNs, uint64_t nowNs,
                     bool mailbox, int clockKind)
{
    *L = (PaceLock){0};
    L->n = n;
    L->refreshNs = refreshNs;
    L->periodNs = periodNs;
    L->mailbox = mailbox;
    L->clockKind = clockKind;
    L->clock = clockKind != PACE_CLOCK_KIND_NONE ? PACE_CLOCK_ON : PACE_CLOCK_NONE;
    L->due = nowNs;
    L->slotNs = (double)refreshNs;
    L->perFrame = 2; /* a game frame every two vsyncs until two closes are seen */
    L->rateClean = true;
}

/* fmod without libm: the remainder of x / s with the sign of x */
static double rem_of(double x, double s)
{
    return x - s * (double)(int64_t)(x / s);
}

/* e wrapped to (-s/2, s/2] */
static double wrap_half(double e, double s)
{
    e = rem_of(e, s);
    if (e > s / 2) {
        e -= s;
    } else if (e <= -s / 2) {
        e += s;
    }
    return e;
}

static void pend_pop(PaceLock *L)
{
    for (unsigned i = 1; i < L->pendCount; i++) {
        L->pendId[i - 1] = L->pendId[i];
        L->pendSlot[i - 1] = L->pendSlot[i];
        L->pendLate[i - 1] = L->pendLate[i];
        L->pendNudge[i - 1] = L->pendNudge[i];
    }
    if (L->pendCount > 0) {
        L->pendCount--;
    }
}

static void pend_push(PaceLock *L, uint64_t id, double slot, bool late)
{
    if (L->pendCount == PACE_GRID_PENDING) {
        pend_pop(L); /* the oldest: never seen, not waited for again */
    }
    L->pendId[L->pendCount] = id;
    L->pendSlot[L->pendCount] = slot;
    L->pendLate[L->pendCount] = late;
    L->pendNudge[L->pendCount] = L->nudgeSum;
    L->pendCount++;
}

/* the display clock is left: the slot back to the report */
static void clock_leave(PaceLock *L, int why)
{
    L->clock = why;
    L->pendCount = 0;
    L->nudgeLeft = 0.0;
    L->slotNs = (double)L->refreshNs;
}

static double median_lat(const PaceLock *L)
{
    double v[PACE_GRID_RING];
    unsigned i, j;
    const unsigned n = L->latCount;

    for (i = 0; i < n; i++) {
        v[i] = L->lat[i];
    }
    for (i = 1; i < n; i++) {
        const double x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    return n ? v[n / 2] : 0.0;
}

/* the display clock saw the oldest pending present on the screen at the
   refresh at t */
static void clock_seen(PaceLock *L, uint64_t t)
{
    const double s = L->slotNs;
    /* the refresh's offset after the slot the present was made in (the
       grid then: the moves made since are in nudgeSum's difference) */
    double ph = rem_of((double)t - L->pendSlot[0] - (L->nudgeSum - L->pendNudge[0]), s);

    if (ph < 0.0) {
        ph += s;
    }
    L->seen++;
    if (!L->targetSet) {
        /* the phase found, once the moves still owed are made */
        L->target = rem_of(ph - L->nudgeLeft, s);
        if (L->target < 0.0) {
            L->target += s;
        }
        L->targetSet = true;
    }
    /* the error left once the moves the steps still owe are made */
    const double e = wrap_half(ph - L->nudgeLeft - L->target, s);

    L->nudgeLeft += PACE_GRID_KP * e;
    L->slotNs += PACE_GRID_KI * e;
    const double away = L->slotNs - (double)L->refreshNs;

    if ((away < 0.0 ? -away : away) * 100.0 > (double)L->refreshNs) {
        clock_leave(L, PACE_CLOCK_LEFT_PERIOD);
        return;
    }
    const double lat = (double)t - L->pendSlot[0];

    for (unsigned i = 0; i < L->traceCount; i++) {
        if (L->traceId[i] == L->pendId[0] && lat > 0.0) {
            L->traceShowUs[i] = lat / 1000.0 > 4e9 ? 0xffffffffu : (uint32_t)(lat / 1000.0);
        }
    }
    if (L->pendLate[0]) {
        return; /* after a slow step: late for that, not for the phase */
    }
    /* a picture a refresh later or earlier than the run: the presents
       finish near a refresh; the wanted offset moves away from it, the
       way the first such picture says (later: the grid earlier) until
       PACE_GRID_CLEAN pictures in a row are shown with the run.  With the
       ends straddling a refresh the run's median takes either side, and a
       way chosen each time went back and forth */
    if (L->latCount >= PACE_GRID_RING_MIN) {
        const double med = median_lat(L);
        const int off = lat > med + s / 2 ? 1 : (lat < med - s / 2 ? -1 : 0);

        if (off != 0) {
            if (off > 0) {
                L->late++;
            } else {
                L->early++;
            }
            if (L->away == 0) {
                L->away = off;
                L->awayMoved = 0.0;
            }
            L->clean = 0;
            /* half a refresh at most: past it the pictures are late for
               something else than the phase (a slow GPU), and the grid
               stays */
            if (L->awayMoved < s / 2) {
                L->awayMoved += s / 64.0;
                L->target = rem_of(L->target + (double)L->away * s / 64.0, s);
                if (L->target < 0.0) {
                    L->target += s;
                }
            }
        } else if (L->away != 0 && ++L->clean >= PACE_GRID_CLEAN) {
            L->away = 0;
        }
    }
    L->lat[L->latNext] = lat;
    L->latNext = (L->latNext + 1) % PACE_GRID_RING;
    if (L->latCount < PACE_GRID_RING) {
        L->latCount++;
    }
}

/* the display clock's news: each present it saw on the screen since the
   last call, oldest first */
static void clock_take(PaceLock *L, const PaceLockIo *io)
{
    uint64_t id, t;
    int r;

    while (L->clock == PACE_CLOCK_ON && (r = io->shown(io->user, &id, &t)) != 0) {
        if (r < 0) {
            clock_leave(L, PACE_CLOCK_LEFT_ERROR);
            break;
        }
        /* presents before it that it never named: not timed */
        while (L->pendCount > 0 && L->pendId[0] < id) {
            pend_pop(L);
        }
        if (L->pendCount == 0 || L->pendId[0] != id) {
            continue; /* not one of the lock's (made before its reset) */
        }
        if (t != 0) {
            clock_seen(L, t);
        } else {
            L->seenNoTime++;
        }
        if (L->clock == PACE_CLOCK_ON) {
            pend_pop(L);
        }
    }
}

/* the grid moved by e ns (the vblank clock's one move onto the vblanks):
   the last step's due time, and the rate block's start with it, so the
   move is no rate */
static void due_move(PaceLock *L, double e)
{
    const double at = L->dueFrac + e;
    const double whole = at >= 0.0 ? (double)(uint64_t)at : -(double)(uint64_t)(1.0 - at);

    L->due = (uint64_t)((int64_t)L->due + (int64_t)whole);
    L->dueFrac = at - whole;
    if (L->rateFirst != 0) {
        L->rateFirst = (uint64_t)((int64_t)L->rateFirst + (int64_t)whole);
    }
}

/* the vblank clock's news: each vblank noted since the last call, oldest
   first (PaceLock.vb), then the period law once for the step */
static void vblank_take(PaceLock *L, const PaceLockIo *io)
{
    const double rep = (double)L->refreshNs;
    uint64_t v;
    int r;

    while (L->clock == PACE_CLOCK_ON && (r = io->vblank(io->user, &v)) != 0) {
        if (r < 0) {
            clock_leave(L, PACE_CLOCK_LEFT_ERROR);
            return;
        }
        pace_intervals_add(&L->vb, v, L->refreshNs);
        if (L->vb.n < PACE_VB_MIN) {
            continue;
        }
        if (L->measuredNs == 0.0) {
            /* the first use: an interval more than 1 % off the report is
               another output's (or no vblank at all) */
            const double tm = pace_intervals_mean(&L->vb);
            const double off = tm - rep;

            if ((off < 0.0 ? -off : off) * 100.0 > rep) {
                L->measuredNs = tm;
                clock_leave(L, PACE_CLOCK_LEFT_PERIOD);
                return;
            }
        }
        L->measuredNs = pace_intervals_mean(&L->vb);
        /* the phase: the vblank's offset after the last step against the
           wanted one (the step slot / 16 after a vblank); e > 0: the step
           is to move later */
        const double sl = L->slotNs;
        double ph = rem_of((double)v - ((double)L->due + L->dueFrac), sl);

        if (ph < 0.0) {
            ph += sl;
        }
        L->vbPhaseNs = wrap_half(-ph, sl);
        if (!L->phaseSet) {
            /* once: the grid onto the vblanks at once */
            due_move(L, wrap_half(ph + sl / PACE_VB_OFFSET_DIV, sl));
            L->nudgeLeft = 0.0;
            L->phaseSet = true;
            L->phaseSnaps++;
            continue;
        }
        L->nudgeLeft +=
            wrap_half(ph + sl / PACE_VB_OFFSET_DIV - L->nudgeLeft, sl) * PACE_VB_KP_PHASE;
    }
    if (L->clock != PACE_CLOCK_ON || L->measuredNs == 0.0) {
        return;
    }
    /* the period: the slot toward the measured refresh, never past 1 % of
       the report */
    L->slotNs += (L->measuredNs - L->slotNs) * PACE_VB_KP_PERIOD;
    const double away = L->slotNs - rep;

    if ((away < 0.0 ? -away : away) * 100.0 > rep) {
        clock_leave(L, PACE_CLOCK_LEFT_PERIOD);
    }
}

/* a present's interval from the one before, for the log */
static void lock_trace(PaceLock *L, uint64_t returnNs, uint64_t id)
{
    if (L->traceCount >= PACE_LOCK_TRACE_FIRST) {
        return;
    }
    const uint64_t us =
        L->lastReturn != 0 && returnNs > L->lastReturn ? (returnNs - L->lastReturn) / 1000u : 0;

    L->traceUs[L->traceCount] = us > 0xffffffffu ? 0xffffffffu : (uint32_t)us;
    L->traceShowUs[L->traceCount] = 0;
    L->traceId[L->traceCount] = id;
    L->traceCount++;
}

static int ms_text(char *buf, unsigned len, const char *fmt, uint32_t us)
{
    const unsigned frac = (us % 1000u + 5u) / 10u;

    return snprintf(buf, len, fmt, us / 1000u, frac > 99u ? 99u : frac);
}

unsigned pace_lock_trace_text(const PaceLock *L, char *buf, unsigned len)
{
    unsigned at = 0, i;

    if (len == 0) {
        return 0;
    }
    buf[0] = '\0';
    for (i = 0; i < L->traceCount; i++) {
        char one[48];
        int w = ms_text(one, sizeof one, i ? " %u.%02u" : "%u.%02u", L->traceUs[i]);

        if (L->traceShowUs[i] != 0 && w > 0) {
            w += ms_text(one + w, sizeof one - (unsigned)w, " (%u.%02u)", L->traceShowUs[i]);
        }
        if (w < 0 || (unsigned)w >= len - at) {
            break;
        }
        memcpy(buf + at, one, (size_t)w + 1u);
        at += (unsigned)w;
    }
    return i;
}

/* a step at dueNs: counted, and the rate block's */
static void lock_step(PaceLock *L, uint64_t dueNs)
{
    L->steps++;
    if (L->rateSteps == 0 && L->rateFirst == 0) {
        L->rateFirst = dueNs;
    } else {
        L->rateSteps++;
    }
    L->rateLast = dueNs;
}

uint64_t pace_lock_vsync(PaceLock *L, const PaceLockIo *io, bool newFrame)
{
    uint64_t now = io->now(io->user);
    const double period = L->slotNs * (double)L->n;

    if (L->clock == PACE_CLOCK_ON && L->clockKind == PACE_CLOCK_KIND_SHOWN && io->shown != NULL) {
        clock_take(L, io);
    } else if (L->clock == PACE_CLOCK_ON && L->clockKind == PACE_CLOCK_KIND_VBLANK &&
               io->vblank != NULL) {
        vblank_take(L, io);
    }
    L->vsyncsSinceClose++;
    if (newFrame) {
        const unsigned v = L->vsyncsSinceClose;

        L->frames++;
        /* the first close since the reset ends no whole frame: perFrame
           stays (two) until the next */
        if (L->closeSeen) {
            L->perFrame = v < 1 ? 1 : (v > 4 ? 4 : v);
        }
        L->closeSeen = true;
        L->vsyncsSinceClose = 0;
        L->sinceClose = 0;
        L->framePresented = false;
        L->frameHeld = false;
    }
    /* the next step: a period on, moved by what the clock asks (a little a
       step) */
    double nudge = L->nudgeLeft;

    if (nudge > PACE_GRID_NUDGE_MAX_NS) {
        nudge = PACE_GRID_NUDGE_MAX_NS;
    } else if (nudge < -PACE_GRID_NUDGE_MAX_NS) {
        nudge = -PACE_GRID_NUDGE_MAX_NS;
    }
    L->nudgeLeft -= nudge;
    L->nudgeSum += nudge;
    const double off = L->dueFrac + period + nudge; /* above 0: a period is ms, a nudge us */
    uint64_t next = L->due + (uint64_t)off;
    double nextFrac = off - (double)(uint64_t)off;

    if (now > next && (double)(now - next) > PACE_LOCK_DEBT_VSYNCS * period) {
        /* far behind (a stage load): the lag is dropped and the grid starts
           again from now; the presents in flight were on the old one */
        L->resyncs++;
        L->droppedNs += now - next;
        L->rateClean = false;
        L->due = now - (uint64_t)period;
        L->dueFrac = 0.0;
        next = now;
        nextFrac = 0.0;
        L->pendCount = 0;
        L->latCount = L->latNext = 0;
        L->phaseSet = false; /* the vblank clock moves the new grid onto them again */
    }
    for (unsigned j = 0; j < L->n; j++) {
        /* the slot: the step's due time for the first present, a refresh
           apart for the others */
        const double so = L->dueFrac + (double)j * L->slotNs;
        const uint64_t slot = L->due + (uint64_t)so;
        const float a = pace_lock_alpha(L->sinceClose + 1, L->n, L->perFrame);

        L->sinceClose++;
        if (j > 0 && io->now(io->user) < slot) {
            io->sleepUntil(io->user, slot);
        }
        now = io->now(io->user);
        if (io->visible != NULL && !io->visible(io->user)) {
            /* off the screen: no present, nothing to wait for */
            L->unshown++;
            L->pendCount = 0;
            continue;
        }
        /* the late-present skip (pace_late_skip, the timer path's rule): a
           frame not presented yet while the next step is already a whole
           period late is left unpresented, never two frames in a row
           (pace_skipped_last on the closes' count), the same frame again
           while it holds; the steps catch up instead of falling behind
           until the lag is dropped */
        if (pace_late_skip(!L->framePresented,
                           pace_skipped_last(L->skipOpen, L->skipFrame, L->frames), false, now,
                           next, (uint64_t)period) != PACE_SKIP_NONE) {
            L->skipOpen = true;
            L->skipFrame = L->frames;
            L->skippedLate++;
            continue;
        }
        if (now >= next && L->framePresented &&
            (!L->lastBlocked || (double)(now - next) >= L->slotNs)) {
            /* behind: no picture again until the step.  Not after a
               present that waited for the display while less than a slot
               behind (the skip guard): the step is late by the display's
               own wait, the queue has pictures to show, and the skip
               would be the picture shown twice */
            L->skipped++;
            continue;
        }
        /* FIFO: no hold (pace_fifo_hold) under the lock. The hold judges
           the next vblank from the last present's return, and on NVIDIA's
           Vulkan a return is not a vblank (and present_once's time holds
           the replay's fence waits): it would hold back presents at cap
           60, each a repeated picture. A present that returns past the
           next step makes that step late by less than a slot, and its
           pictures keep their exact grid alphas, so nothing is seen. */
        if (!io->present(io->user, a)) {
            continue; /* nothing presented */
        }
        const uint64_t ret = io->now(io->user);
        const uint64_t id = L->clock == PACE_CLOCK_ON && L->clockKind == PACE_CLOCK_KIND_SHOWN &&
                                    io->presentId != NULL
                                ? io->presentId(io->user)
                                : 0;
        /* the time it waited (the backend's own where it knows it) */
        const uint64_t waitNs = io->waited != NULL ? io->waited(io->user, ret - now) : ret - now;

        L->presents++;
        L->framePresented = true;
        L->skipOpen = false;
        lock_trace(L, ret, id > L->lastId ? id : 0);
        L->lastReturn = ret;
        L->lastBlocked = (double)waitNs * 4.0 >= L->slotNs;
        if (L->lastBlocked) {
            /* FIFO's queue was full: counted (the log), and the skip guard */
            L->waited++;
            if (waitNs > L->waitedMaxNs) {
                L->waitedMaxNs = waitNs;
            }
        }
        if (id > L->lastId) {
            /* not counted for the phase: a present that started half a
               slot late (a slow step) or took a whole slot (a replay past
               its slot; at 4K one of 9 to 15 ms at 60 Hz is the normal
               run): its picture is late for that */
            L->lastId = id;
            pend_push(L, id, (double)L->due + so,
                      (double)(now > slot ? now - slot : 0) * 2.0 >= L->slotNs ||
                          (double)(ret - now) >= L->slotNs);
        }
    }
    io->sleepUntil(io->user, next);
    L->due = next;
    L->dueFrac = nextFrac;
    lock_step(L, next);
    return next;
}

void pace_lock_pause(PaceLock *L, uint64_t ns)
{
    L->due += ns;
    if (L->lastReturn != 0) {
        L->lastReturn += ns;
    }
    if (L->rateFirst != 0) {
        L->rateFirst += ns;
    }
    if (L->rateLast != 0) {
        L->rateLast += ns;
    }
    for (unsigned i = 0; i < L->pendCount; i++) {
        L->pendSlot[i] += (double)ns;
    }
    L->phaseSet = false; /* the vblank clock moves the grid onto them again */
}

uint64_t pace_first_present_pause(uint64_t swapchains, uint64_t *seen, uint64_t costNs,
                                  uint64_t refreshNs)
{
    if (swapchains == *seen) {
        return 0;
    }
    *seen = swapchains;
    return costNs > refreshNs ? costNs - refreshNs : 0;
}

double pace_lock_nominal_ratio(const PaceLock *L)
{
    if (L->n == 0 || L->refreshNs == 0 || L->periodNs == 0) {
        return 1.0;
    }
    const double r = (double)L->periodNs / ((double)L->n * (double)L->refreshNs);

    return r > 1.02 || r < 0.98 ? 1.0 : r;
}

double pace_lock_rate_ratio(PaceLock *L)
{
    double ratio = 1.0;

    if (L->rateClean && L->rateSteps < PACE_LOCK_RATE_MIN_STEPS) {
        return 0.0; /* too short yet: the block goes on */
    }
    if (L->rateClean && L->rateLast > L->rateFirst && L->periodNs != 0) {
        ratio = (double)L->rateSteps * (double)L->periodNs / (double)(L->rateLast - L->rateFirst);
        if (ratio > 1.02 || ratio < 0.98) {
            ratio = 1.0;
        }
    }
    /* a new block from the last step */
    L->rateFirst = L->rateLast;
    L->rateSteps = 0;
    L->rateClean = true;
    return ratio;
}

void pace_intervals_reset(PaceIntervals *s)
{
    const uint64_t last = s->last;

    *s = (PaceIntervals){0};
    s->last = last;
}

void pace_intervals_add(PaceIntervals *s, uint64_t ns, uint64_t slotNs)
{
    if (ns == 0 || slotNs == 0) {
        return;
    }
    if (s->last == 0 || ns <= s->last) {
        s->last = ns;
        return;
    }
    const uint64_t d = ns - s->last;

    if (d * 2 < slotNs) {
        return; /* not a new refresh: last stays */
    }
    s->last = ns;
    s->intervals++;
    if (s->min == 0 || d < s->min) {
        s->min = d;
    }
    if (d > s->max) {
        s->max = d;
    }
    if (d * 2 < slotNs * 3) {
        s->n++;
        s->sum += (double)d;
        return;
    }
    s->late++;
    if (d * 2 < slotNs * 7) {
        /* about 2 or 3 slots: that many refreshes */
        s->n += d * 2 < slotNs * 5 ? 2u : 3u;
        s->sum += (double)d;
    } else {
        s->missed++;
    }
}

double pace_intervals_mean(const PaceIntervals *s)
{
    return s->n ? s->sum / (double)s->n : 0.0;
}

double pace_ppm(double measuredNs, double reportedNs)
{
    if (measuredNs <= 0.0 || reportedNs <= 0.0) {
        return 0.0;
    }
    return (measuredNs / reportedNs - 1.0) * 1e6;
}

void pace_shown_add(PaceShown *s, uint64_t presentCount, uint64_t presentRefresh)
{
    if (s->have && presentCount == s->lastPresent && presentRefresh == s->lastRefresh) {
        return; /* no picture shown since */
    }
    if (s->have && presentCount > s->lastPresent && presentRefresh >= s->lastRefresh &&
        presentRefresh - s->lastRefresh <= PACE_SHOWN_GAP) {
        const uint64_t dp = presentCount - s->lastPresent;
        const uint64_t dr = presentRefresh - s->lastRefresh;

        s->shown += (unsigned)dp;
        s->refreshes += (unsigned)dr;
        if (dr > dp) {
            s->repeats += (unsigned)(dr - dp);
        }
    } else if (s->have) {
        s->resets++; /* went back, a gap, or refreshes with no picture */
    }
    s->lastPresent = presentCount;
    s->lastRefresh = presentRefresh;
    s->have = true;
}
