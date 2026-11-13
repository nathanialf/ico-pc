/* rd_pace_even.h: the even cadence for pictures as made, an rc variant off by
 * default (rd_pace_even.c, rd_pace_even_live.c; docs/port/DISPLAY.md "The
 * alpha").
 *
 * With pictures as made (vsync off, uncapped, mailbox, the render thread on)
 * and a GPU that cannot make one a refresh, the pictures reach the screen a
 * mix of one, two and three refreshes apart (v0.11.6-rc5 on the owner's
 * 120 Hz phone in play: 190-239 one refresh apart and 459-501 two in 10 s),
 * and no moment drawn for the content takes the mix away: the screen's own
 * rule (rd_pace_screen.h) reached a deviation of the content's speed on the
 * glass of .27-.35 there.  The even cadence shows every picture for the same
 * whole number of refreshes N instead:
 *
 *   T   the picture time: the larger of the GPU model's median span G and its
 *       median hand-over to first submit D (rd_pace_gpu.h: the GPU and the
 *       render thread, the two frames in flight's two stages);
 *   N   the fewest refreshes with T + RD_PE_UP_MS <= N P (P the display's
 *       measured refresh, rd_pace_screen.h's grid), at most the game's tick
 *       in refreshes (4 for 30 frames a second on 120 Hz; a picture time
 *       over it: as made, RD_PE_SLOW): up at once when T grows
 *       past it, or when RD_PE_MISS_UP of the last RD_PE_MISS_WIN aims were
 *       late; down, to the fewest that T + RD_PE_DOWN_MS fits, only after
 *       that has held for RD_PE_DOWN_HOLD_MS (so a picture time on a
 *       boundary does not flap);
 *   R   each picture's refresh: the one before's + N P;
 *   the hand-over: held by the window's pacer (window_present.c) until the
 *       picture's predicted GPU end E (the queue model's, from the moment it
 *       would be handed over) sits RD_PE_AIM of a refresh before R's latch
 *       (E + X = R - RD_PE_AIM P, X the screen's fitted latch): a picture is
 *       shown at R when E + X is in (R - P, R], so the aim leaves
 *       RD_PE_AIM P of lateness (the game step on the same thread, the
 *       prediction's error) and (1 - RD_PE_AIM) P of earliness before it
 *       would be shown a refresh early;
 *   late: where the picture cannot be handed over in time for R (its
 *       earliest end + X past R - RD_PE_LATE_MS), R moves on a refresh and
 *       the aim counts as late (the cadence broken once);
 *   the content: drawn for R (the screen's rule placed with R as its
 *       refresh: rd_pace_screen.h RdPaceScreenIn.targetMs).
 *
 * It decides when a picture is handed over, so it lowers the pictures a
 * second where the GPU could make more than one every N refreshes (on a
 * 120 Hz screen: 60 a second when a picture takes about 8.4-16.6 ms, 40 at
 * 16.7-25, 120 when it fits one refresh); the game's own frames and steps
 * are not touched.  Applies only to pictures as made on a display that gives
 * its times back (VK_GOOGLE_display_timing) with the screen's grid and latch
 * fitted; otherwise the pictures go as made and the reason is counted. */
#ifndef ICO_RD_PACE_EVEN_H
#define ICO_RD_PACE_EVEN_H

/* Off by default (an rc variant flips it); ICO_RD_PACE_EVEN=1 (the tests'
   switch) turns it on */
#ifndef RD_PACE_EVEN_DEFAULT
#define RD_PACE_EVEN_DEFAULT 0
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the picture time's margins for N up and down, the time N - 1 must fit
   before N goes down, the most refreshes a picture */
#define RD_PE_UP_MS 0.25
#define RD_PE_DOWN_MS 1.0
#define RD_PE_DOWN_HOLD_MS 1000.0
#define RD_PE_N_MAX 8u
/* late aims over the last RD_PE_MISS_WIN that take N up; N then not below
   the raised value while the picture time is within RD_PE_FLOOR_T_MS of the
   time at the raise (what the GPU model cannot see, the game step's and the
   step rule's holds, is not tried again until the picture time itself
   changed: with three refreshes a picture on 120 Hz the hand-overs go round
   the game's tick of four and one in four falls in the step rule's hold
   before the tick's close) */
#define RD_PE_MISS_WIN 32u
#define RD_PE_MISS_UP 8u
#define RD_PE_FLOOR_T_MS 1.0
/* the aim: the predicted end + X this share of a refresh before R; late
   when the earliest end + X is past R less this */
#define RD_PE_AIM 0.5
#define RD_PE_LATE_MS 1.0
/* the aim moved this much earlier at each picture shown a refresh late,
   later at each one shown early, at most half a refresh either way: with
   every picture aimed at one phase of its refresh the shown times no longer
   tell where in the refresh the latch is (each one only says it made it),
   so the misses themselves move the aim (a compositor whose latch moved) */
#define RD_PE_BIAS_STEP_MS 0.5
/* and back toward 0 by this at each picture shown on its refresh; the bias
   and the late aims forgotten when the picture time moves this far from
   where they were taken (a new scene: the GPU model's prediction is off
   while its medians catch up, and its misses say nothing of the latch) */
#define RD_PE_BIAS_DECAY_MS 0.1
#define RD_PE_REGIME_MS 2.0
/* a hand-over due inside the game's next step (the pacer's deadline: the
   step runs on its thread) is made before the step instead when that is at
   most the aim's early side less RD_PE_LATE_MS early ((1 - RD_PE_AIM) P -
   RD_PE_LATE_MS: 3.17 ms at 120 Hz) */

/* why a picture was not aimed */
enum {
    RD_PE_AIMED = 0,
    RD_PE_NO_FIT,  /* the screen's grid or latch not fitted (or stale, or too few) */
    RD_PE_NO_GPU,  /* the GPU model's span or lead not known yet */
    RD_PE_KEPT,    /* a kept frame's present (a fade, a load) */
    RD_PE_NO_PLAN, /* handed over without a plan (a present the pacer did not hold) */
    RD_PE_SLOW,    /* a picture takes longer than a game tick: as made */
    RD_PE_HOW_COUNT
};

/* the poster's thread's state */
typedef struct RdPaceEven {
    uint32_t n;          /* refreshes a picture (0: not started) */
    double lastTargetMs; /* the last picture's refresh (the display's clock; 0 none) */
    double downSinceMs;  /* rd's clock: N - 1 has fitted since (0 not) */
    uint32_t missBits;   /* the last RD_PE_MISS_WIN aims: 1 late */
    uint32_t floorN;     /* N not below this ... */
    double floorTMs;     /* ... while the picture time is within RD_PE_FLOOR_T_MS of this */
    double biasMs;       /* the aim this much earlier (RD_PE_BIAS_STEP_MS) */
    double tRefMs;       /* the picture time the bias and the late aims were taken at */
    double periodMs;     /* P at the last plan (the bias's bound) */
    /* the plan for the picture about to be handed over */
    bool planned;
    double planTargetMs;
} RdPaceEven;

/* what the plan reads (the poster's thread, rd_pace_even_live.c) */
typedef struct RdPaceEvenIn {
    double nowMs;     /* rd's clock */
    bool fit;         /* the screen's grid and latch usable */
    double anchorMs;  /* a refresh of the grid (the display's clock) */
    double periodMs;  /* P, measured */
    double xMs;       /* X: shown at the first refresh at or after E + X */
    double gMs, dMs;  /* the GPU model's median span and lead (0 not known) */
    double prevEndMs; /* the GPU model's last predicted end (rd's clock; 0 none) */
    double stepEndMs; /* the pacer's deadline: the game steps from it (0 not known) */
    double tickMs;    /* the game's frame (N at most its refreshes, rounded; 0 not known) */
} RdPaceEvenIn;

typedef struct RdPaceEvenPlan {
    int how;         /* RD_PE_* */
    double holdMs;   /* wait this long before the hand-over (0: now) */
    double targetMs; /* R (the display's clock; 0 not aimed) */
    double endMs;    /* the end aimed at (rd's clock) */
    uint32_t n;      /* N in force */
    bool changed;    /* N changed at this plan */
    bool late;       /* R moved on: the picture cannot make the one N refreshes on */
    bool early;      /* handed over before the game's step rather than after it */
} RdPaceEvenPlan;

void rd_pace_even_reset(RdPaceEven *e);
/* pure: the fewest refreshes of p a picture of t ms fits with margin m */
uint32_t rd_pace_even_n_for(double t, double p, double m);
/* the plan for the next hand-over (idempotent until rd_pace_even_take: the
   pacer asks again after it waited) */
void rd_pace_even_plan(RdPaceEven *e, const RdPaceEvenIn *in, RdPaceEvenPlan *out);
/* the picture handed over at handMs with its predicted end endMs: its
   refresh (0 not aimed), the plan consumed; *late it missed the plan's
   refresh at the hand-over (aimed at a later one) */
double rd_pace_even_take(RdPaceEven *e, const RdPaceEvenIn *in, double endMs, bool kept,
                         bool *late);
/* a shown picture's score against its aim (rd_pace_screen_score: 0 on it):
   the late window and the aim's bias */
void rd_pace_even_shown(RdPaceEven *e, int score);

/* ------------------------------------------- the 10 s sums and texts */

typedef struct RdPaceEvenWindow {
    uint64_t how[RD_PE_HOW_COUNT];
    uint64_t changes, lateAims, held, heldUs, early, shownOff, shownLate, shownEarly, shown;
    uint32_t n, periodNs; /* the last */
} RdPaceEvenWindow;

/* the poster's thread adds, the window's thread takes */
void rd_pace_even_tally_plan(const RdPaceEvenPlan *p, double periodMs);
void rd_pace_even_tally_take(int how, bool late);
void rd_pace_even_tally_shown(int score);
void rd_pace_even_take_window(RdPaceEvenWindow *w);
/* "; the even cadence: ..." appended at out (the on-glass line's clause);
   false and "" when nothing was planned or taken */
bool rd_pace_even_format(const RdPaceEvenWindow *w, char *out, size_t size);
const char *rd_pace_even_how_text(int how);
/* the start line ("rd: an even number of refreshes a picture: ...") */
size_t rd_pace_even_start_text(bool measured, bool on, const char *why, char *out, size_t size);

/* ----------------------------------------------- the renderer's side */

/* RD_PACE_EVEN_DEFAULT, ICO_RD_PACE_EVEN=0/1 over it (read once) */
bool rd_pace_even_on(void);
/* the poster's thread (rd_pace_rules.c, a picture as made handed over with
   its predicted GPU end; kept a kept frame's present): the refresh it is
   aimed at (the display's clock; 0 not aimed: off, no plan, kept), the plan
   taken and tallied */
double rd_pace_even_take_live(double endPredMs, bool kept);
/* the poster's thread: a shown picture the screen's model took (its score
   against the refresh it was aimed at, where it was) */
struct RdPaceScreenSample;
void rd_pace_even_seen(const struct RdPaceScreenSample *s);
/* the poster's thread, the pictures paced: the state emptied, the row not
   applicable */
void rd_pace_even_paced(void);
/* the poster's thread at nowMs (rd's clock; rd.h rd_present_even_hold
   from the window's pacer): the hold before the next hand-over (ms, 0
   none), the plan made and its first ask tallied */
double rd_pace_even_hold_at(double nowMs, double untilDeadlineMs, double tickMs);

#endif
