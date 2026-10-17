/*
 * port/platform/pace_policy.h
 *
 * The window's "is a present slow" decision, free of SDL so a
 * unit test checks it. A present with vsync on blocks up to one display
 * refresh, so slow means a cost above max(refresh, period) + period / 2; with
 * an effects program loaded (ReShade, vkBasalt: rhi_injector_name() != NULL)
 * each present also runs the program's passes, so the threshold is
 * max(refresh, period) + refresh + period / 2.
 *
 * The cost is smoothed (the median of the last 8 presents, so one spike
 * changes nothing) and the state has hysteresis: it turns on after 3 presents
 * in a row above the threshold and off when the median falls below 80 % of it.
 */
#ifndef ICO_PLATFORM_PACE_POLICY_H
#define ICO_PLATFORM_PACE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PACE_HIST_LEN 8
#define PACE_ENTER_COUNT 3

/* Zero-initialise before the first call. */
typedef struct PaceHist {
    uint64_t cost[PACE_HIST_LEN];
    unsigned count; /* valid entries, up to PACE_HIST_LEN */
    unsigned next;  /* where the next cost goes */
    unsigned over;  /* consecutive presents with the median above the threshold */
    bool slow;      /* the state last returned */
} PaceHist;

/* The slow threshold in nanoseconds. */
uint64_t pace_slow_threshold(uint64_t refreshNs, uint64_t periodNs, bool injector);

/* Records one present's cost and returns whether presents are slow now. */
bool pace_slow_present(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                       bool injector);

/* The least time between two presents while the window presents between
 * game frames.  cap > 0: a frame rate limit, one present per 1/cap s.
 * uncappedVsync: frame rate "uncapped" with vsync on.  Then a sixteenth of
 * a refresh less than one display refresh (refreshNs), in mailbox mode and
 * under FIFO alike.  The gap counts from the start of the last present, and
 * late wake-ups and game steps only ever add to it, so the presents average
 * about one a refresh: under FIFO a present rarely finds the queue full and
 * waits, and in mailbox mode every refresh gets a new picture with few drawn
 * that the display never shows (a gap of exactly one refresh averaged a
 * little more than a refresh, and the display showed some pictures twice).
 * In mailbox mode never fewer than sixty a second whatever refresh the
 * display reports (a 4K screen reported 30 Hz while running at 60).
 * 0 (no gap: back to back) otherwise, i.e. "uncapped" without vsync. */
uint64_t pace_present_gap(int cap, bool uncappedVsync, bool mailbox, uint64_t refreshNs);

/* True when a numeric frame rate limit (cap > 0) equals the display's rate:
 * within 0.5 Hz (60 on a 59.94 or 60.00 Hz screen, 120 on 119.88 or 120.00),
 * and up to 1.0 Hz above a reported rate of 100 Hz or more that is a whole
 * number: Windows reports whole numbers cut down, so a 143.9 Hz screen reads
 * 143 and a 239.8 Hz one 239, and 144 or 240 equals them.  Below 100 Hz the
 * 0.5 Hz holds: SDL 3.4 turns Windows' 29, 59 and 119 into x / 1.001
 * (src/video/windows/SDL_windowsmodes.c WIN_GetRefreshRate), so a reported
 * whole 59 is a true 59.00 Hz mode, and a cap of 60 locked to it would run
 * the game 1.7 % slow.
 * With vsync on such a limit takes the vsync lock below when it is a whole
 * multiple of the simulation's rate (60 and 120 on 59.94: the grid, FIFO
 * unless the present-wait clock is on, then mailbox), else the FIFO present
 * mode and no timer gap (144 on 59.94: the blocking present holds each
 * present to a vblank, and pace_fifo_hold holds back one that would return
 * past the deadline).  A limit that does not equal the refresh prefers
 * mailbox and keeps its own gap.  docs/port/DISPLAY.md "Pacing". */
bool pace_cap_equals_refresh(int cap, uint64_t refreshNs);

/* The display's intervals: the times of the display clock's vblanks, or
 * of the lock's present returns, against a slot (the refresh the display
 * reports), for window_host.c's 10 s display line and for the vsync
 * lock's vblank clock (PaceLock.vb: its measured refresh).  An interval
 * under half a slot is dropped (a wake-up that was not a new refresh; last
 * stays); one under
 * 1.5 slots is one refresh; one of 1.5 slots or more is late, and when it
 * is about k slots (k 2..3: under 3.5) it counts as k refreshes in the
 * mean; a longer one (a pause, the window off the screen) is missed and
 * left out of the mean.  min and max are of the intervals not dropped. */
typedef struct PaceIntervals {
    uint64_t last;      /* the last time added (0: none) */
    double sum;         /* the counted intervals' total (ns) */
    uint64_t min, max;  /* (ns; min 0: none) */
    unsigned n;         /* refreshes the sum spans */
    unsigned intervals; /* intervals not dropped */
    unsigned late;      /* intervals of 1.5 slots or more */
    unsigned missed;    /* of those, longer than 3.5 slots (not in the mean) */
} PaceIntervals;

/* ------------------------------------------------------------------------
 * The vsync lock: the simulation on a grid of the wall clock
 * (docs/port/DISPLAY.md "The vsync lock").
 *
 * With vsync on and the cap equal to the display's rate, when the cap is a
 * whole multiple n of the simulation's rate (1 % at most off: 60 or 120 on
 * 59.94, 50 or 100 on 50, pace_lock_presents) and n display refreshes are
 * within 1 % of the simulation's period (pace_grid_period_ok), the
 * simulation steps on a grid whose period is n refreshes: 16.667 ms on a
 * 60.00 Hz display, where the game then runs 0.1 % fast and the audio
 * follows (pace_lock_rate_ratio).  Each step is followed by exactly n
 * presents, the first at once, the others a refresh apart (the grid's
 * slots); the k-th present since the last game frame closed shows the
 * exact alpha k / (n * vsyncs per frame) (pace_lock_alpha).
 *
 * The grid's period is the refresh the display reports.  The OS clock and
 * the display's differ by tens of ppm (a picture shown twice or never every
 * few minutes), and a report can be off by more (Windows' 59.951 Hz for a
 * 59.940 Hz scan-out: 163 ppm).  Two display clocks take that away
 * (PaceLock.clockKind); the clock never steps the simulation itself, and
 * the loop never waits for it.
 *
 * PACE_CLOCK_KIND_SHOWN, the present-wait clock (PaceLockIo.shown:
 * VK_KHR_present_wait, a thread of the backend's waiting on each present
 * and noting when it reached the screen; mailbox): a phase-locked loop on
 * the pictures shown.  At each step the loop takes the presents the
 * display showed since the last one: the refresh that showed one, against
 * the slot it was presented in (the time a present is due on the grid, n a
 * period) and the wanted offset, wrapped to half a refresh, is the phase
 * error e; the steps move by PACE_GRID_KP * e (at most
 * PACE_GRID_NUDGE_MAX_NS a step) and the slot by PACE_GRID_KI * e, never
 * past 1 % of the reported refresh (past it the clock is not believed: it
 * is left and the slot goes back to the report).
 *
 * The wanted offset is the first one seen, moved a 64th of a refresh each
 * time a picture is shown a refresh later or earlier than the run of them
 * (the median of the last PACE_GRID_RING): the presents finish around a
 * refresh, so the grid moves away from it, the way the first such picture
 * says (later: its present finished just after the refresh, the grid
 * moves earlier against the refreshes; earlier: later) until
 * PACE_GRID_CLEAN pictures in a row are shown with the run, and half a
 * refresh at most (past it pictures are late for something else, and the
 * grid stays).  A present that started half a slot late (after a slow
 * step) or took a whole slot (a replay past its slot) is not counted.
 *
 * PACE_CLOCK_KIND_VBLANK, the vblank clock (PaceLockIo.vblank: Windows, a
 * thread on the window's output's vblanks, display_clock.h; FIFO): every
 * vblank noted goes into PaceLock.vb (pace_intervals_add against the
 * report: an interval of about k refreshes counts k, a wake-up under half
 * a refresh after the last is dropped).  Once PACE_VB_MIN refreshes are
 * in, the measured refresh Tm (measuredNs) is the mean over all of them
 * (the first and last vblank's times over the refreshes between, less any
 * long gap), and the two laws run:
 *   period: each step slot += (Tm - slot) * PACE_VB_KP_PERIOD; the clock is
 *     left (the slot back to the report) when Tm is more than 1 % off the
 *     report at its first use (a wrong output) or the slot is later, so a
 *     wrong clock never runs the game more than 1 % off its speed;
 *   phase: the step is wanted a slot / PACE_VB_OFFSET_DIV after a vblank
 *     (the most time before the next one: a picture finished within
 *     15/16 of a refresh of its step is never a refresh late, so a full
 *     queue cannot ratchet).  For each vblank v, ph = (v - due) mod slot
 *     (due: the last step's due time) and e = wrap_half(ph + slot / 16)
 *     less the moves still owed; e > 0 when the step is earlier against
 *     the vblanks than wanted, and the steps move later by
 *     e * PACE_VB_KP_PHASE (nudgeLeft, at most PACE_GRID_NUDGE_MAX_NS a
 *     step).  The first time PACE_VB_MIN refreshes are in, and again after
 *     a resync or a pause (the grid moved), the grid is moved by e at once
 *     (phaseSet; half a slot at most), so nothing is pulled in slowly.
 * The noted times carry the thread's wake-up (tens of us), so the step
 * sits that much more after the true vblank.
 *
 * Without a clock (PACE_CLOCK_KIND_NONE: Linux, macOS, Android, a clock
 * that did not start) the grid stays on the report.  The presents go
 * through mailbox (PaceLock.mailbox: a present never waits for the
 * display, which shows the newest finished picture at each refresh) only
 * with the present-wait clock; otherwise FIFO.  Under mailbox without a
 * clock the grid's phase against the refreshes is left where it starts,
 * and where the presents' ends straddle a refresh pictures repeat and drop
 * for the whole session, while FIFO shows them in order whatever the phase
 * (pace_loop_test testNoClockMode: 61 of 64 runs against 30 with one slip
 * each).  The steps never wait on a present's return: a FIFO return is not
 * a vblank (on NVIDIA with three images two presents return about 9 ms
 * apart and the next one about 32 ms later).
 *
 * FIFO: on the display's rate a present returns at once while the queue
 * has room.  Nothing is held back (pace_fifo_hold is not used under the
 * lock).  A present that waited a quarter of a slot or more (PaceLockIo.
 * waited: the backend's own acquire, present and fence-wait time where it
 * gives one, else the whole present) waited for a vblank (the queue was
 * full) and is counted (waited, waitedMaxNs).  The late-present skip
 * (pace_late_skip, as on the timer path): a new frame not presented yet
 * while the next step is a whole period or more late is left unpresented
 * (skippedLate), never two frames in a row, the same frame again while it
 * holds, so a renderer slower than the frames (a GPU-bound phone) drops
 * pictures instead of the game falling behind until the lag is dropped.
 * The skip guard: a step
 * that ends past the next step's due time skips the rest of its presents
 * (a frame already shown), unless the present before waited for the
 * display and the step is less than a slot behind: then the lateness is
 * the display's own wait, the queue has pictures to show, and a skip is
 * what would make a picture shown twice.  A step a slot or more behind (a
 * slow GPU, a 30 ms step) still skips.
 *
 * Off the screen (PaceLockIo.visible false: minimised, hidden, occluded):
 * no presents, the grid goes on.  A step that ends past the next one's due
 * time presents nothing more that vsync but a new frame not shown yet;
 * more than PACE_LOCK_DEBT_VSYNCS periods behind, the lag is dropped
 * (counted) and the grid starts again from now.
 *
 * For the log: the first PACE_LOCK_TRACE_FIRST presents' intervals between
 * returns and, where the clock saw it, the time from the slot to the
 * refresh that showed it (pace_lock_trace_text); diagnostics only.
 */
#define PACE_LOCK_TRACE_FIRST 48
#define PACE_LOCK_DEBT_VSYNCS 6
/* a measured rate block for the audio needs this many steps */
#define PACE_LOCK_RATE_MIN_STEPS 100
/* the display clock's loop */
#define PACE_GRID_KP (1.0 / 64.0)
#define PACE_GRID_KI (1.0 / 8192.0)
#define PACE_GRID_NUDGE_MAX_NS 20000.0
#define PACE_GRID_PENDING 16 /* presents not yet seen on the screen, the oldest dropped */
#define PACE_GRID_RING 31    /* the run's latencies */
#define PACE_GRID_RING_MIN 15
#define PACE_GRID_CLEAN 300 /* pictures with the run that end a move away */
/* the vblank clock's laws */
#define PACE_VB_MIN 64                 /* refreshes measured before the laws run */
#define PACE_VB_KP_PERIOD (1.0 / 64.0) /* the slot's share of Tm - slot a step */
#define PACE_VB_KP_PHASE (1.0 / 16.0)  /* the steps' move: this of each vblank's error */
#define PACE_VB_OFFSET_DIV 16          /* the step wanted slot / 16 after a vblank */

/* the display clock offered (pace_lock_reset) */
enum {
    PACE_CLOCK_KIND_NONE = 0, /* none: the grid on the report */
    PACE_CLOCK_KIND_SHOWN,    /* present wait (PaceLockIo.shown, presentId) */
    PACE_CLOCK_KIND_VBLANK    /* the output's vblanks (PaceLockIo.vblank) */
};

/* the display clock (PaceLock.clock) */
enum {
    PACE_CLOCK_NONE = 0,   /* none offered */
    PACE_CLOCK_ON,         /* in use */
    PACE_CLOCK_LEFT_ERROR, /* it stopped (a wait failed) */
    PACE_CLOCK_LEFT_PERIOD /* it gave a refresh more than 1 % off the report */
};

/* Zero-initialise, then pace_lock_reset. */
typedef struct PaceLock {
    unsigned n;         /* presents a step (pace_lock_presents) */
    uint64_t refreshNs; /* the display's refresh as reported */
    uint64_t periodNs;  /* the simulation's own period (the audio's nominal rate) */
    bool mailbox;       /* presents never wait (else FIFO; no pace_fifo_hold under the lock) */
    /* the grid */
    uint64_t due;   /* when the last step was due */
    double dueFrac; /* and the fraction of a ns */
    double slotNs;  /* a refresh as the grid has it: a period is n of them */
    /* frames */
    unsigned sinceClose; /* slots since the last game frame closed */
    unsigned perFrame;   /* simulated vsyncs per game frame (the last two closes'; 1..4) */
    unsigned vsyncsSinceClose;
    bool closeSeen;      /* a frame closed since the reset (perFrame is measured from the next) */
    bool framePresented; /* the last frame that closed was presented */
    bool frameHeld;      /* cleared at each close; not read (no pace_fifo_hold under the lock) */
    bool lastBlocked;    /* the last present waited a quarter of a slot or more (.waited) */
    /* the late-present skip (pace_late_skip): the closes since the reset, a
       frame left unpresented and none presented since, its close's number */
    uint32_t frames;
    bool skipOpen;
    uint32_t skipFrame;
    /* the display clock */
    int clockKind; /* PACE_CLOCK_KIND_* offered */
    int clock;     /* PACE_CLOCK_*: its state */
    uint64_t pendId[PACE_GRID_PENDING];
    double pendSlot[PACE_GRID_PENDING];  /* the slot it was presented in */
    bool pendLate[PACE_GRID_PENDING];    /* started half a slot past it or took a slot: not
                                            counted for the phase */
    double pendNudge[PACE_GRID_PENDING]; /* nudgeSum when it was made */
    unsigned pendCount;
    uint64_t lastId; /* the last present id seen */
    double target;   /* the wanted offset of a refresh after a slot (0 .. slot) */
    bool targetSet;
    double nudgeLeft;           /* the move the steps still owe (ns, later when above 0) */
    double nudgeSum;            /* the moves made since the reset */
    unsigned seen;              /* presents timed on the screen */
    unsigned seenNoTime;        /* presents the clock saw on the screen at a time not known */
    double lat[PACE_GRID_RING]; /* the run: from a slot to the refresh that showed it */
    unsigned latCount, latNext;
    unsigned late, early; /* pictures a refresh later or earlier than the run */
    int away;             /* the way the wanted offset moves (+1 later, -1 earlier, 0 none) */
    double awayMoved;     /* how far it moved that way (half a refresh at most) */
    unsigned clean;       /* pictures with the run since the last that was not */
    /* the vblank clock: every vblank noted since the reset against the
       report (vb.n refreshes over vb.sum ns), the refresh measured from
       them (0: fewer than PACE_VB_MIN yet), the grid moved onto them once
       (phaseSet; again after a resync or a pause), the last vblank's
       offset before its step (ns, above 0: the step after it) */
    PaceIntervals vb;
    double measuredNs;
    bool phaseSet;
    unsigned phaseSnaps;
    double vbPhaseNs;
    /* counts since the reset; waited: presents that waited a quarter of
       a slot or more (PaceLockIo.waited; FIFO: the queue was full, they
       waited for a vblank), waitedMaxNs the longest wait */
    unsigned presents, steps, waited, unshown, skipped, resyncs;
    unsigned skippedLate; /* new frames left unpresented by the late-present skip */
    uint64_t waitedMaxNs;
    uint64_t droppedNs; /* lag dropped (more than PACE_LOCK_DEBT_VSYNCS periods) */
    /* the rate block (pace_lock_rate_ratio) */
    uint64_t rateFirst, rateLast;
    unsigned rateSteps;
    bool rateClean; /* no lag dropped since rateFirst */
    /* the log's trace: each present's interval from the return before (us,
       0: none), its id and the time from its slot to the refresh that
       showed it (us, 0: not seen) */
    uint64_t lastReturn;
    uint32_t traceUs[PACE_LOCK_TRACE_FIRST], traceShowUs[PACE_LOCK_TRACE_FIRST];
    uint64_t traceId[PACE_LOCK_TRACE_FIRST];
    unsigned traceCount;
    bool traceLogged; /* the caller logged them */
} PaceLock;

/* n for a cap equal to the refresh: round(cap * periodNs / 1e9) when the cap
 * is within 1 % of n simulated vsync rates, else 0 (144 or 75 on 59.94, 60
 * on 50: no whole multiple; the window steps on the simulation's grid). */
unsigned pace_lock_presents(int cap, uint64_t periodNs);

/* The period's sanity: n refreshes within 1 % of the simulation's period
 * (|n * refresh - period| < period / 100).  Else no lock (the game would
 * run more than 1 % off its speed). */
bool pace_grid_period_ok(unsigned n, uint64_t refreshNs, uint64_t periodNs);

/* The alpha of a present that lands k slots after the last frame closed:
 * k / (n * perFrame), 1 at most (0 when n or perFrame is 0). */
float pace_lock_alpha(unsigned k, unsigned n, unsigned perFrame);

/* A new lock (the first locked vsync, a new n, refresh or present mode, the
 * window back from the background): counts cleared, the grid from nowNs
 * (when the last step was due), its slot the reported refresh.  mailbox:
 * the swapchain presents in mailbox mode; clockKind: the display clock
 * offered (PACE_CLOCK_KIND_*: PaceLockIo.shown or .vblank). */
void pace_lock_reset(PaceLock *L, unsigned n, uint64_t refreshNs, uint64_t periodNs, uint64_t nowNs,
                     bool mailbox, int clockKind);

/* What the loop needs from the window (a fake one in pace_loop_test). */
typedef struct PaceLockIo {
    void *user;
    uint64_t (*now)(void *user);
    /* replays and presents at alpha; false: nothing presented (a movie on
       the output, nothing closed yet) */
    bool (*present)(void *user, float alpha);
    void (*sleepUntil)(void *user, uint64_t ns);
    /* false: the window is off the screen (minimised, hidden, occluded):
       no present.  NULL: always on it */
    bool (*visible)(void *user);
    /* the display clock (NULL: none): the id of the last present (0:
       none), and the next present the display showed, oldest first, with
       when it did (*ns 0: at a time not known): 1 one taken, 0 none yet, -1
       the clock stopped */
    uint64_t (*presentId)(void *user);
    int (*shown)(void *user, uint64_t *id, uint64_t *ns);
    /* the vblank clock (NULL: none): the next vblank noted, oldest first,
       *ns when (the now clock): 1 one taken, 0 none yet, -1 the clock
       stopped */
    int (*vblank)(void *user, uint64_t *ns);
    /* how long the last present waited, given its whole time costNs: the
       backend's own blocked time (the acquire, the present, fence waits)
       where it knows it.  NULL: costNs */
    uint64_t (*waited)(void *user, uint64_t costNs);
} PaceLockIo;

/* One simulated vsync under the lock (L->n > 0), called right after its
 * step: the display clock's news taken, n presents in the step's slots,
 * then a sleep to the next step's due time on the grid, which it returns
 * (the caller steps at once). */
uint64_t pace_lock_vsync(PaceLock *L, const PaceLockIo *io, bool newFrame);

/* The host paused for ns (a swapchain rebuilt at a frame rate change: the
 * device waited idle, the new chain made, its first picture drawn): the
 * grid and everything timed on it move by ns, so the pause is neither lag
 * to drop (a jump in game time) nor lag to catch up (a burst of steps).
 * Called between vsyncs, never inside pace_lock_vsync. */
void pace_lock_pause(PaceLock *L, uint64_t ns);

/* The first present on a swapchain made since the last one (swapchains:
 * the backend's count, RhiStats.swapchains, against *seen, which it
 * updates): what it cost over a refresh is the rebuild's (the first
 * acquire, the compositor taking the new chain: ~140 ms on NVIDIA's
 * Windows driver), a pause of the host for the caller to take out of the
 * pacing before it judges the lag.  Returns the pause: 0 for any other
 * present, or one within a refresh. */
uint64_t pace_first_present_pause(uint64_t swapchains, uint64_t *seen, uint64_t costNs,
                                  uint64_t refreshNs);

/* The trace as text: "16.67 (12.40) 16.66 (12.38) 1.20 ..." (ms since the
 * return before, and in brackets ms from the present's slot to the refresh
 * that showed it where the clock saw it).  Returns the presents written. */
unsigned pace_lock_trace_text(const PaceLock *L, char *buf, unsigned len);

/* The simulated vsync rate under the lock as the display's nominal
 * refresh gives it, over the simulation's: periodNs / (n * refreshNs), 1.001
 * for one present a step on a 60.00 Hz screen under 59.94 (1.0 when more
 * than 2 % from 1, or n or the refresh is 0).  The audio's ratio from the
 * lock's start, until the first measured block (pace_lock_rate_ratio), so
 * its queue does not grow by 0.1 % over the first 10 s. */
double pace_lock_nominal_ratio(const PaceLock *L);

/* The simulated vsync rate measured over the steps since the last call,
 * over the nominal (1e9 / periodNs): 1.001 on a 60.00 Hz display under a
 * 59.94 Hz simulation.  1.0 when lag was dropped in the block or the ratio
 * is more than 2 % from 1; either way a new block starts.  0 while a steady
 * block has fewer than PACE_LOCK_RATE_MIN_STEPS steps: it goes on (the
 * caller keeps its ratio). */
double pace_lock_rate_ratio(PaceLock *L);

/* Under FIFO with a refresh shorter than the simulation's period and no
 * lock (a cap of 144 on 59.94, 60 on PAL's 50; never under the lock):
 * whether the window holds this present back and sleeps to the deadline
 * because it would return on a vblank past it (a FIFO present returns on
 * the first vblank after it is made, on the last return's grid of
 * refreshes; one started just before the deadline would run the step a
 * refresh late).  Never a frame that is not shown yet once the
 * deadline has passed (the game behind: one present per new frame), and a
 * new frame is held back at most once (frameHeld: this frame was held
 * before), so a game whose step is about a period long still shows its
 * frames.  lastReturn 0: no present yet, nothing held. */
bool pace_fifo_hold(uint64_t nowNs, uint64_t lastReturnNs, uint64_t refreshNs, uint64_t deadlineNs,
                    bool frameShown, bool frameHeld);

/* Why a new frame was left unpresented (pace_late_skip). */
typedef enum PaceSkip {
    PACE_SKIP_NONE = 0,
    PACE_SKIP_LATE, /* the simulation a whole step period or more behind its grid */
    PACE_SKIP_SLOW  /* presents slow (pace_slow_present) */
} PaceSkip;

/* The late-present skip, on the timer path and under the vsync lock
 * (pace_lock_vsync, deadlineNs the next step's due time, the slow arm
 * not used): whether the window leaves a new frame
 * unpresented and runs the next step at once (the next present shows the
 * newer frame), so the simulation catches up with real time instead of
 * falling behind until the pacer drops the lag (a resync).  A present that
 * holds the next step back keeps the game below real time; on a renderer
 * that cannot present every frame in time (a GPU-bound phone at 2x) that is
 * the game in slow motion with a jump at every resync.  Not PACE_SKIP_NONE
 * only when all hold: the next step's due time has passed (nowNs >=
 * deadlineNs), the frame is not presented yet (newFrame), the frame before
 * it was presented (skippedLast false, pace_skipped_last: never two frames
 * in a row, so the picture keeps at least every other frame), and either
 * the simulation is a whole step period or more
 * behind (nowNs - deadlineNs >= periodNs: PACE_SKIP_LATE, the debt the
 * window's "step late" figure measures at the step's start) or presents are
 * slow (paceSlow, pace_slow_present: PACE_SKIP_SLOW).  The late arm comes
 * from the loop's own lateness, not the audio queue (on the device's clock
 * the audio keeps no queue that a late step drains), so it holds with or
 * without a sound device: a game behind real time is slow with or without
 * sound.  The same frame may be
 * left unpresented again at the next steps while the condition holds (a
 * game at 30 frames a second closes a frame every second step; skipping it
 * at one step and presenting it at the next saves nothing), until a newer
 * frame closes, which is then presented.  A single late present on a
 * renderer that keeps up (a FIFO return just past the deadline, less than a
 * period behind, presents not slow) never skips.  No step or audio is
 * dropped, only presents. */
PaceSkip pace_late_skip(bool newFrame, bool skippedLast, bool paceSlow, uint64_t nowNs,
                        uint64_t deadlineNs, uint64_t periodNs);

/* pace_late_skip's skippedLast: a frame was left unpresented and none
 * presented since (skipOpen; a present closes it), and it was an older frame
 * than the one now newest (skippedFrame != frame).  The same frame skipped
 * again is not "two in a row". */
bool pace_skipped_last(bool skipOpen, uint32_t skippedFrame, uint32_t frame);

/* The limiter's grid: when the present after one that started at startNs is
 * due.  nextPresent is when that one was due (0: none yet).  The result is
 * nextPresent + gap, so a late wake-up or a present held behind a game step
 * does not push every later present back; when startNs is more than a gap
 * past nextPresent (a stall) the grid restarts at startNs + gap. */
uint64_t pace_next_present(uint64_t nextPresent, uint64_t startNs, uint64_t gap);

/* The counts cleared; last kept, so the interval across two blocks counts
 * in the second (zero the struct to forget it too). */
void pace_intervals_reset(PaceIntervals *s);

/* One time ns (any monotonic clock, above 0) against slotNs. */
void pace_intervals_add(PaceIntervals *s, uint64_t ns, uint64_t slotNs);

/* The mean interval a refresh (ns; 0 with none). */
double pace_intervals_mean(const PaceIntervals *s);

/* A measured refresh against the reported one in ppm: positive when the
 * measured refresh is longer (a grid on the report runs faster than the
 * display); 0 when either is 0. */
double pace_ppm(double measuredNs, double reportedNs);

/* The pictures the display showed, from a swapchain's own counts (DXGI's
 * frame statistics: PresentCount and PresentRefreshCount of the last
 * picture shown): over two samples, shown = the presents shown between,
 * refreshes = the refreshes between, and when more refreshes passed than
 * pictures were shown the difference is pictures shown again (repeats).
 * A count that went back (a new swapchain) or a gap of more than
 * PACE_SHOWN_GAP refreshes (a pause) starts again from that sample
 * (resets). */
#define PACE_SHOWN_GAP 240u

/* Zero-initialise. */
typedef struct PaceShown {
    uint64_t lastPresent, lastRefresh;
    bool have; /* lastPresent and lastRefresh hold a sample */
    unsigned shown, refreshes, repeats, resets;
} PaceShown;

void pace_shown_add(PaceShown *s, uint64_t presentCount, uint64_t presentRefresh);

/* Resolution "auto" (video_options.h ICO_RES_AUTO).  The window
 * feeds each present's cost (the GPU time of a replay when the backend has
 * timestamps, else its CPU time without the acquire and the present, which
 * wait for the display) into a PaceSamples; every PACE_AUTO_WINDOW_NS it asks
 * pace_auto_resolution_step for the scene scale and starts a new window.
 *
 * The rule: when the median cost over the window (at least
 * PACE_AUTO_WINDOW_NS of samples, at least PACE_AUTO_MIN_SAMPLES of them)
 * is above 70 % of the frame budget (1/60 s, or the frame rate cap's
 * period), the scale steps down once: the window's own size (scale 0) goes
 * to the largest of 3x, 2x, 1x below the window's scale, Nx to (N-1)x and
 * anything above 3x straight to 3x;
 * never below 1x, never back up, and at most once per PACE_AUTO_WINDOW_NS
 * (lastStepNs). */
#define PACE_AUTO_SAMPLES 256
#define PACE_AUTO_MIN_SAMPLES 8
#define PACE_AUTO_WINDOW_NS 2000000000ull

/* Zero-initialise (or pace_samples_reset) before the first sample. */
typedef struct PaceSamples {
    uint64_t cost[PACE_AUTO_SAMPLES]; /* a ring: the newest PACE_AUTO_SAMPLES */
    unsigned count;                   /* valid entries, up to PACE_AUTO_SAMPLES */
    unsigned next;                    /* where the next cost goes */
    uint64_t firstNs;                 /* the window's start (0: its first sample's time) */
    uint64_t lastNs;                  /* the newest sample's time */
    uint64_t budgetNs;                /* the frame budget the costs are held against */
    uint64_t lastStepNs;              /* when the scale last stepped (0: never) */
} PaceSamples;

/* A new window starting at nowNs: the samples dropped; budgetNs and
   lastStepNs kept. */
void pace_samples_reset(PaceSamples *s, uint64_t nowNs);

/* One present's cost at time nowNs (any monotonic clock in ns, above 0). */
void pace_samples_add(PaceSamples *s, uint64_t nowNs, uint64_t costNs);

/* The median of the samples (0 with none). */
uint64_t pace_samples_median(const PaceSamples *s);

/* The scene scale resolution "auto" should have: currentScale (0 = the
 * window's size, else N) or the next step down.  windowScale is the
 * window's size against the game's 1x (the presentation box's height over
 * 448), to know which step is below the window's own. */
int pace_auto_resolution_step(const PaceSamples *s, int currentScale, float windowScale);

#endif /* ICO_PLATFORM_PACE_POLICY_H */
