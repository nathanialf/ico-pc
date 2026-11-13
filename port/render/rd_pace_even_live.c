/* rd_pace_even_live.c: the even cadence in the renderer (rd_pace_even.h).
 *
 * Threads: one, the poster's (window_present.c window_present_pace, which
 * runs rd_present_post's presentRebase too: rd_present_even_hold, rd_thread.c,
 * before a hand-over through rd_pace_even_hold_at, rd_pace_even_take_live inside it through rd_pace_rules_pick,
 * rd_pace_even_seen from the screen's model as it takes the shown pictures,
 * rd_pace_even_paced at every step the pictures are paced).  The state, the
 * aims kept for the score and the ledger's row are that thread's; the 10 s
 * sums are rd_pace_even.c's atomics the window's thread takes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_pace_even.h"
#include "rd_pace_gpu.h"
#include "rd_pace_screen.h"

bool rd_pace_even_on(void)
{
    static int s_on = -1;
    if (s_on < 0) {
        const char *e = getenv("ICO_RD_PACE_EVEN");
        s_on = e != NULL && e[0] != '\0' ? (e[0] != '0') : RD_PACE_EVEN_DEFAULT;
    }
    return s_on != 0;
}

/* the poster's */
static RdPaceEven s_e;
static RdPaceEvenIn s_in;  /* the last plan's inputs (the take's grid and X) */
static int s_lastHow = -1; /* the last plan's RD_PE_* (-1 none since the take) */
static int s_state = -1;   /* the row as last decided */
static uint32_t s_changes;
/* the aimed pictures not yet seen: their predicted end (the screen's
   sample carries the same value back) and their refresh */
#define AIMS 32u

static struct {
    double endMs, targetMs, refreshMs;
} s_aims[AIMS];

static uint32_t s_aimAt;

static void decide(bool asMade)
{
    const int clock = rd_pace_gpu_clock_state();
    const int times = rd_pace_screen_times_state();
    /* 0 paced, 1 measured, 2 no display times, 3 no GPU end; -1 not known */
    const int st = !asMade      ? 0
                   : clock == 0 ? 3
                   : times == 0 ? 2
                   : clock < 0  ? -1
                   : times == 1 ? 1
                                : -1;
    if (st < 0 || st == s_state) {
        return;
    }
    s_state = st;
    ico_attempt_decide_pace_even(asMade ? 1 : 0, clock, times, rd_pace_even_on() ? 1 : 0);
    const uint32_t k = s_changes++;
    if (k < 8u || k % 100u == 0u) {
        char line[1536];
        (void)rd_pace_even_start_text(st == 1, rd_pace_even_on(),
                                      ico_attempt_why_of(ICO_ATT_PACE_EVEN), line, sizeof line);
        fprintf(stderr, "%s\n", line);
    }
}

static void gather(double nowMs, double untilDeadlineMs, double tickMs, RdPaceEvenIn *in)
{
    memset(in, 0, sizeof(*in));
    in->nowMs = nowMs;
    const RdPaceScreenFit *f = rd_pace_screen_live_fit(nowMs, &in->fit, &in->xMs);
    in->anchorMs = f->anchorMs;
    in->periodMs = f->periodMs;
    (void)rd_pace_gpu_figures(&in->gMs, &in->dMs, &in->prevEndMs);
    in->stepEndMs = untilDeadlineMs > 0.0 ? nowMs + untilDeadlineMs : 0.0;
    in->tickMs = tickMs;
}

double rd_pace_even_hold_at(double nowMs, double untilDeadlineMs, double tickMs)
{
    decide(true); /* the row and its start line, on or off */
    if (!rd_pace_even_on()) {
        return 0.0;
    }
    const bool first = !s_e.planned;
    gather(nowMs, untilDeadlineMs, tickMs, &s_in);
    RdPaceEvenPlan p;
    rd_pace_even_plan(&s_e, &s_in, &p);
    s_lastHow = p.how;
    if (first) {
        /* once a picture: the pacer asks again after each wait */
        rd_pace_even_tally_plan(&p, s_in.periodMs);
    }
    return p.how == RD_PE_AIMED ? p.holdMs : 0.0;
}

double rd_pace_even_take_live(double endPredMs, bool kept)
{
    if (!rd_pace_even_on()) {
        return 0.0;
    }
    if (kept) {
        bool late;
        (void)rd_pace_even_take(&s_e, &s_in, 0.0, true, &late);
        rd_pace_even_tally_take(RD_PE_KEPT, false);
        s_lastHow = -1;
        return 0.0;
    }
    if (!s_e.planned) {
        /* no plan: the last plan's reason, or a present the pacer did not
           ask about */
        rd_pace_even_tally_take(s_lastHow > RD_PE_AIMED ? s_lastHow : RD_PE_NO_PLAN, false);
        s_lastHow = -1;
        return 0.0;
    }
    bool late = false;
    const double target = rd_pace_even_take(&s_e, &s_in, endPredMs, false, &late);
    rd_pace_even_tally_take(target > 0.0 ? RD_PE_AIMED : RD_PE_NO_PLAN, late);
    if (target > 0.0) {
        ico_attempt_fire(ICO_ATT_PACE_EVEN, 1u);
        if (late) {
            ico_attempt_fire2(ICO_ATT_PACE_EVEN, 1u);
        }
        if (endPredMs > 0.0) {
            s_aims[s_aimAt % AIMS].endMs = endPredMs;
            s_aims[s_aimAt % AIMS].targetMs = target;
            s_aims[s_aimAt % AIMS].refreshMs = s_in.periodMs;
            s_aimAt++;
        }
    }
    s_lastHow = -1;
    return target;
}

void rd_pace_even_seen(const RdPaceScreenSample *s)
{
    if (!rd_pace_even_on() || !(s->endPredMs > 0.0)) {
        return;
    }
    for (uint32_t i = 0; i < AIMS; i++) {
        if (s_aims[i].endMs == s->endPredMs) {
            const double ref = s->refreshMs > 0.0 ? s->refreshMs : s_aims[i].refreshMs;
            const int score = rd_pace_screen_score(s_aims[i].targetMs, s->actualMs, ref);
            rd_pace_even_shown(&s_e, score);
            rd_pace_even_tally_shown(score);
            s_aims[i].endMs = 0.0;
            return;
        }
    }
}

void rd_pace_even_paced(void)
{
    if (s_e.n != 0u || s_e.planned || s_e.lastTargetMs > 0.0) {
        rd_pace_even_reset(&s_e);
        memset(s_aims, 0, sizeof s_aims);
    }
    s_lastHow = -1;
    decide(false);
}
