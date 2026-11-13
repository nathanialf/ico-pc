/* rd_pace_screen.h: each picture made as fast as the GPU allows drawn for
 * the moment the screen will show it (rd_pace_screen.c, rd_pace_screen_live.c;
 * docs/port/DISPLAY.md "The alpha").
 *
 * With pictures as made (vsync off, uncapped, mailbox, the render thread on:
 * rd.h rd_present_as_made) a picture is shown at the first refresh whose
 * latch comes after its GPU work ends.  The GPU's ends are even (v0.11.6-rc4
 * on the owner's 120 Hz phone: the content against them 1.00, deviation
 * 0.04-0.07) but the latch rounds them to whole refreshes: at 70 pictures a
 * second about a third are shown for one refresh (8.35 ms) and the rest for
 * two, so an even content step is 1.7 times real speed on the one and 0.86
 * on the other (deviation 0.38-0.40 for the hand-over rule and the own
 * timer alike).  This rule draws each picture for the time it will be on
 * the glass:
 *
 *   E   its GPU end as predicted at its hand-over (rd_pace_gpu.h
 *       RdPaceGpuPlace.predEndMs: the queue model's);
 *   the display's grid, measured from the past presents' times the display
 *       gives back (VK_GOOGLE_display_timing's actualPresentTime, rd_glass.h):
 *       the period a least-squares fit over the last RD_PS_WIN shown
 *       pictures' times, the refresh the extension reports rounding them to
 *       whole refreshes; the phase the newest time;
 *   X   the latch's offset: a picture predicted to end at E is shown at the
 *       first refresh at or after E + X.  X is fitted, not assumed: each
 *       shown picture says E + X was within the refresh before its own, and
 *       X is the middle of the range that agrees with the most of the last
 *       RD_PS_WIN (it holds the compositor's latch lead, the prediction's
 *       mean error and any offset between rd's clock and the display's);
 *   w   the prediction's spread, from the fit's misses: with the phase of E
 *       spread evenly over a refresh a Gaussian error of deviation w misses
 *       the refresh in a share E|e| / P = w sqrt(2/pi) / P of pictures, so
 *       w = sqrt(pi/2) P (misses / pictures);
 *   g   the expected time on the glass: each refresh at its probability
 *       under that spread (the refresh itself where w is 0); mailbox's rule
 *       needs nothing more: a picture replaced by the next before the latch
 *       has the next one's refresh, the same g, so it takes no content time;
 *   L   the lag: the content drawn is g - L.  L starts where the moment
 *       drawn otherwise is (no jump when the rule takes over) and moves at
 *       most RD_PS_SLEW_MS a picture toward the middle of the range that
 *       keeps every one of the last RD_PS_LAG_WIN pictures inside its frame
 *       pair's tick (alpha 0 to 0.999: never before the frame's close, the
 *       rc3 failure, never past the newest frame the game has made); where
 *       no L keeps them all, toward the top of the range (no picture before
 *       its frame's close; a picture past the newest frame held there).
 *
 * v0.11.6-rc5's log (the owner's phone, the rule not drawn) refined it:
 *
 *   - a kept frame's present (a fade, a load, a menu over the picture before:
 *     rd_interp.c rd_present's keep path, no alpha drawn, its GPU work not in
 *     the queue model) is neither placed, sampled nor scored (RD_PS_KEPT):
 *     its predicted end is a scene picture's G late, and in the three blocks
 *     with most of them (op2, deja, the sacrifice scene) the latch's fit went
 *     to -21.6 and -32.3 ms with 37-41 of 64 agreeing and the hits fell to
 *     49-72 %;
 *   - hysteresis: the latch stays where it was unless another range agrees
 *     with RD_PS_HYST more of the window (two ranges of nearly equal
 *     agreement would otherwise swap the refresh a picture at a time);
 *   - the measured errors: where RD_PS_MEAS_MIN or more of the window's
 *     shown pictures have an exact GPU end, the expected time on the glass
 *     is the mean of the refreshes the picture would be shown at with each
 *     of their errors (its predicted end + the error + the exact end's
 *     latch), and the refresh predicted their most frequent: a skewed or a
 *     two-sided error (rc5's cage: one late against one early 1.3-2.0 to 1)
 *     is taken as measured rather than as a symmetric Gaussian.
 *
 * Each of the last two is a bit of RdPaceScreen.opts (all on after
 * rd_pace_screen_reset; the test turns each off to measure it).
 *
 * The moment never goes back (held at the one before) and never leaves the
 * frame pair's tick (held at its edge, counted).  Where the display gives no
 * times (desktop, no extension, OpenGL, Direct3D), the times are stale, off
 * one refresh grid or fit too few pictures, or the GPU's end is not
 * predicted, the moment is the one drawn otherwise (the hand-over rule, or
 * the own timer where that is on) and the reason is counted and logged.
 *
 * Nothing here moves, delays, drops or adds a picture: it is arithmetic on
 * the value handed over (rd_pace_rules.c from rd_thread.c presentRebase). */
#ifndef ICO_RD_PACE_SCREEN_H
#define ICO_RD_PACE_SCREEN_H

/* 1: drawn by default on Android only (rd_pace_screen_decide; v0.11.6: the
   release candidates on the owner's phone were steadiest with it, the own
   timer as its fall-back; it was measured on Android phones only).  Where
   drawn it can place only where the display gives its times back; every
   picture it does not place is the moment drawn otherwise to the bit with
   the reason counted.  Off a non-Android build (a desktop, a Mac or an
   iPhone, even where the driver offers VK_GOOGLE_display_timing) it is
   computed and reported on the on-glass line as a what-if, never drawn, so
   the drawn moment is v0.11.5's there.  ICO_RD_PACE_SCREEN=0/1 (the tests'
   switch) over it on every platform */
#ifndef RD_PACE_SCREEN_DEFAULT
#define RD_PACE_SCREEN_DEFAULT 1
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the shown pictures kept for the grid and the latch's fit (about a second
   at 64 pictures a second) and the number before the rule draws */
#define RD_PS_WIN 64
#define RD_PS_MIN 32
/* the newest shown picture's predicted end this far behind the hand-over:
   the display's times are stale (a paused read-back, a lost swapchain) */
#define RD_PS_STALE_MS 250.0
/* a shown time further than this share of a refresh off the fitted grid is
   off it; more than one in RD_PS_OFF_GRID_DIV off it, or a fitted period
   more than RD_PS_PERIOD_TOL from the reported refresh: not one grid (a
   changing refresh rate) */
#define RD_PS_GRID_TOL 0.125
#define RD_PS_OFF_GRID_DIV 8u
#define RD_PS_PERIOD_TOL 0.02
/* the latch's fit agreeing with fewer than this share: not predictable */
#define RD_PS_FIT_MIN 0.5
/* the spread at most half a refresh (beyond it the expected time is the
   even prediction's) */
#define RD_PS_WIDTH_MAX 0.5
/* the lag: its window, the pictures before it moves, its most a picture */
#define RD_PS_LAG_WIN 64
#define RD_PS_LAG_MIN 16
#define RD_PS_SLEW_MS 0.25
/* the top of the frame pair's tick (rd_rebase.h rd_rebase_alpha's clamp) */
#define RD_PS_TOP 0.999
/* the latch kept where it was unless another range agrees with this many
   more of the window; the exact ends the measured errors need */
#define RD_PS_HYST 4u
#define RD_PS_MEAS_MIN 16u
#define RD_PS_OPT_HYST 1u
#define RD_PS_OPT_MEASURED 2u
#define RD_PS_OPT_ALL 3u
/* the chain's restarts, as the evenness line's (rd_even.h) */
#define RD_PS_FRAME_JUMP 8
#define RD_PS_GAP_RESET_MS 1000.0

/* one shown picture (the collecting thread, rd_glass.c, to the poster's) */
typedef struct RdPaceScreenSample {
    double endPredMs; /* its GPU end as predicted at its hand-over (rd's clock) */
    double actualMs;  /* the display's time it was shown (the display's clock) */
    double refreshMs; /* the refresh the display reports (0 not known) */
    double gpuEndMs;  /* its GPU end where exact (0 none: the lead's diagnostic fit) */
} RdPaceScreenSample;

/* the display's grid and the latch, fitted from the samples */
typedef struct RdPaceScreenFit {
    bool grid;         /* the samples on one grid */
    double anchorMs;   /* a refresh of it: the newest shown time snapped (display clock) */
    double periodMs;   /* the fitted period */
    double reportedMs; /* the refresh the display reported */
    uint32_t offGrid;  /* samples further than RD_PS_GRID_TOL off it */
    bool latch;        /* X fitted (at least RD_PS_FIT_MIN agree) */
    double xMs;        /* X: shown at the first refresh at or after E + X */
    uint32_t hits, n;  /* the fit's agreeing samples of n */
    double widthMs;    /* w */
    bool lead;         /* the same fit over exact GPU ends */
    double leadMs;     /* shown at the first refresh at or after the GPU's end + this */
    uint32_t leadHits, leadN;
    /* the exact ends' errors (gpuEnd - endPred), RD_PS_OPT_MEASURED */
    double err[RD_PS_WIN];
    uint32_t errN;
    bool measured; /* used: the lead fitted and errN >= RD_PS_MEAS_MIN */
    bool held;     /* the latch kept by RD_PS_HYST */
} RdPaceScreenFit;

/* the model (one thread: the poster's) */
typedef struct RdPaceScreen {
    RdPaceScreenSample s[RD_PS_WIN];
    uint32_t sN, sAt;
    bool dirty;
    RdPaceScreenFit fit;
    uint32_t opts;      /* RD_PS_OPT_* (RD_PS_OPT_ALL after a reset) */
    double newestEndMs; /* the newest sample's predicted end (staleness) */
    /* the lag's chain */
    bool have;
    uint32_t frame;
    double hand, content, lag, lastHardMs;
    double y[RD_PS_LAG_WIN]; /* g - the frame's close */
    uint32_t yN, yAt;
} RdPaceScreen;

/* how a picture's moment was decided */
enum {
    RD_PS_PLACED = 0, /* by the screen */
    RD_PS_NO_TIMES,   /* no shown picture's time back yet */
    RD_PS_NO_END,     /* its GPU end not predicted (the queue model not ready) */
    RD_PS_FEW,        /* fewer than RD_PS_MIN shown pictures since the start */
    RD_PS_STALE,      /* the newest shown picture over RD_PS_STALE_MS behind */
    RD_PS_OFF_GRID,   /* the times not on one refresh grid (or no refresh reported) */
    RD_PS_NO_FIT,     /* the latch's fit agrees with too few */
    RD_PS_KEPT,       /* a kept frame (a fade, a load): not modelled */
    RD_PS_HOW_COUNT
};

typedef struct RdPaceScreenIn {
    double handMs;    /* the hand-over */
    uint32_t frame;   /* the newer frame's number */
    double closeAtMs; /* the frame's close (alpha 0) */
    double tickMs;
    double endPredMs; /* E (0 not predicted) */
    double rawOther;  /* the value drawn otherwise, in ticks from the close */
    bool kept;        /* a kept frame's present (RD_PS_KEPT) */
    /* the refresh the even cadence aims this picture at (rd_pace_even.h;
       the display's clock; 0 none): its expected time on the glass and the
       refresh predicted, where it is placed */
    double targetMs;
} RdPaceScreenIn;

typedef struct RdPaceScreenPlace {
    double raw;          /* the value to draw by this rule (rawOther unless placed) */
    int how;             /* RD_PS_* */
    double hardMs;       /* the refresh predicted (display clock; 0 not placed) */
    double glassMs;      /* g */
    double lagMs;        /* L after this picture */
    bool restarted;      /* the chain restarted here */
    bool heldLo, heldHi; /* held at the frame's close / at the newest frame */
    bool heldBack;       /* held at the moment before */
    bool replaced;       /* predicted on the same refresh as the picture before */
    bool measured;       /* by the measured errors (RD_PS_OPT_MEASURED) */
} RdPaceScreenPlace;

void rd_pace_screen_reset(RdPaceScreen *m);
/* a shown picture into the window */
void rd_pace_screen_add(RdPaceScreen *m, const RdPaceScreenSample *s);
/* the fit over the window (rd_pace_screen_place makes it when the window
   changed) */
void rd_pace_screen_fit(RdPaceScreen *m);
/* the picture handed over: its value by this rule */
void rd_pace_screen_place(RdPaceScreen *m, const RdPaceScreenIn *in, RdPaceScreenPlace *out);
/* pure parts for the tests: the expected time on the glass of a picture
   whose end + X is t on a grid anchored at a with period p and spread w
   (*hard the refresh itself) */
double rd_pace_screen_expect(double t, double a, double p, double w, double *hard);
/* the latch's fit: the middle of the range of x that the most samples
   agree with (sample i: x in (d[i] - p, d[i]]), prefer the range nearest
   prev (NAN none); *hits how many agree */
double rd_pace_screen_latch(const double *d, uint32_t n, double p, double prev, uint32_t *hits);
/* the latch the even cadence aims by (rd_pace_even.h), at nowMs: false
   where the fit cannot be used (fewer than RD_PS_MIN shown pictures, not one
   grid, the newest over RD_PS_STALE_MS behind, neither latch fitted); *xMs
   the exact GPU end's latch plus the median of the exact ends' measured
   errors where those are fitted (RdPaceScreenFit.measured: the GPU end and
   the compositor's lead are each measured, so this does not need the
   pictures' phases to spread over a refresh), else the predicted end's
   latch X.  The fit made first where the window changed */
bool rd_pace_screen_even_latch(RdPaceScreen *m, double nowMs, double *xMs);
/* how many of d agree with x (d[i] - p < x <= d[i]) */
uint32_t rd_pace_screen_cover(const double *d, uint32_t n, double p, double x);
/* the expected time on the glass from the measured errors: the mean of the
   refreshes of t + err[i] (t the predicted end + the exact end's latch) on
   the grid (a, p); *hard their most frequent (ties: the earliest) */
double rd_pace_screen_expect_measured(double t, double a, double p, const double *err, uint32_t n,
                                      double *hard);

/* --------------------------------------------- the collecting thread's */

/* a shown picture with its prediction (rd_glass.c at the join, where its
   score, the refresh predicted against the one shown, goes into the
   on-glass line's sums, rd_glass.h RdGlassWindow): the sample over a
   single-producer ring to the poster's thread (dropped, and counted, when
   the poster has not taken the 128 before it) */
void rd_pace_screen_seen(const RdPaceScreenSample *s);
/* the poster's thread: the samples taken into m; the count taken */
uint32_t rd_pace_screen_drain(RdPaceScreen *m);
/* the ring emptied (paced) */
void rd_pace_screen_drop(void);

/* the score of one shown picture: the refresh shown less the refresh
   predicted, in refreshes (0 a hit, 1 one late, -1 one early) */
int rd_pace_screen_score(double predictedMs, double actualMs, double refreshMs);

/* ------------------------------------------- the 10 s sums and texts */

typedef struct RdPaceScreenWindow {
    uint64_t how[RD_PS_HOW_COUNT];
    uint64_t restarts, heldLo, heldHi, heldBack, replaced, dropped, measured;
    /* the last fit (the poster's, at its last placement) */
    uint32_t grid, latch, lead;
    uint32_t periodNs, reportedNs, hits, n, leadHits, leadN, offGrid;
    int32_t xUs, widthUs, leadUs, lagUs;
} RdPaceScreenWindow;

/* into the sums (the poster's thread) and taken by the window's */
void rd_pace_screen_tally(const RdPaceScreenPlace *p, const RdPaceScreenFit *f);
void rd_pace_screen_tally_drop(void);
void rd_pace_screen_take(RdPaceScreenWindow *w);
/* "; the screen's predicted refresh: ..." appended at out (the on-glass
   line's clause after the rules); false and "" when no picture was placed
   or fell back.  scored/hit/late/early/further: the line's score sums */
bool rd_pace_screen_format(const RdPaceScreenWindow *w, uint64_t scored, uint64_t hit,
                           uint64_t late, uint64_t early, uint64_t further, char *out, size_t size);
/* a reason's words */
const char *rd_pace_screen_how_text(int how);
/* the start line ("rd: in-between pictures for the screen's refresh: ...") */
size_t rd_pace_screen_start_text(bool measured, bool drawn, const char *why, char *out,
                                 size_t size);

/* ----------------------------------------------- the renderer's side */

/* the decision, pure: env the tests' switch (ICO_RD_PACE_SCREEN: -1 unset,
   0, 1), defaultOn RD_PACE_SCREEN_DEFAULT, android the build is Android's.
   True where the rule is drawn; *why (never NULL) its words for the start
   line and the attempt ledger's row */
bool rd_pace_screen_decide(int env, bool defaultOn, bool android, const char **why);
/* rd_pace_screen_decide with ICO_RD_PACE_SCREEN, the define and the
   platform (read once, the poster's thread); rd_pace_screen_why its words */
const char *rd_pace_screen_why(void);
/* RD_PACE_SCREEN_DEFAULT on Android, ICO_RD_PACE_SCREEN=0/1 over it */
bool rd_pace_screen_on(void);
/* the poster's thread, a picture as made (rd_pace_rules.c): the samples
   taken, the ledger's row decided, the picture placed and tallied */
void rd_pace_screen_step(const RdPaceScreenIn *in, RdPaceScreenPlace *out);
/* the poster's thread, the pictures paced: the model emptied, the ring
   dropped, the row not applicable */
void rd_pace_screen_paced(void);
/* the poster's thread: the shown pictures taken (each new one to the even
   cadence, rd_pace_even_seen), the fit made where they changed; *fit
   whether the grid and a latch can be used at nowMs and *xMs the latch to
   aim by (rd_pace_screen_even_latch); the fit itself */
const RdPaceScreenFit *rd_pace_screen_live_fit(double nowMs, bool *fit, double *xMs);
/* 1 a shown picture's time came back this run, 0 none after 600 pictures
   as made, -1 not known yet */
int rd_pace_screen_times_state(void);

#endif
