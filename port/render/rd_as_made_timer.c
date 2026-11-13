/* rd_as_made_timer.c: the own timer for pictures as made (rd_as_made_timer.h).
 * Pure but for the switch's one getenv (rd_as_made_timer_on, the poster's
 * thread only). */
#include "rd_as_made_timer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void rd_as_made_timer_reset(RdAsMadeTimer *t)
{
    memset(t, 0, sizeof(*t));
}

static void note(RdAsMadeTimer *t, double hand)
{
    t->hist[t->histAt] = hand;
    t->histAt = (t->histAt + 1u) % RD_AMT_HIST;
    if (t->histN < RD_AMT_HIST) {
        t->histN++;
    }
}

/* the mean interval of the hand-overs within the window before hand */
static double interval(const RdAsMadeTimer *t, double hand, double tickMs)
{
    double oldest = hand;
    uint32_t j = 0;
    for (uint32_t i = 0; i < t->histN; i++) {
        const double h = t->hist[(t->histAt + RD_AMT_HIST - 1u - i) % RD_AMT_HIST];
        if (hand - h > RD_AMT_WINDOW_TICKS * tickMs) {
            break;
        }
        oldest = h;
        j = i + 1u;
    }
    return j != 0u ? (hand - oldest) / (double)j : 0.0;
}

void rd_as_made_timer_step(RdAsMadeTimer *t, double targetMs, double handMs, uint32_t frame,
                           double tickAtMs, double tickMs, RdAsMadeTimerOut *out)
{
    memset(out, 0, sizeof(*out));
    if (t->have) {
        const int64_t df = (int64_t)frame - (int64_t)t->frame;
        const double gap = handMs - t->hand;
        if (df < 0 || df > RD_AMT_FRAME_JUMP || !(gap > 0.0) || gap > RD_AMT_GAP_RESET_MS ||
            !(tickMs > 0.0)) {
            rd_as_made_timer_reset(t);
        }
    }
    const double iv = t->have ? interval(t, handMs, tickMs) : 0.0;
    const bool fresh = !t->have || frame != t->frame;
    targetMs += t->lead; /* the lead its fresh pictures need (below) */
    double c;
    if (!t->have || !(iv > 0.0)) {
        c = targetMs;
        out->reset = true;
    } else {
        const double pred = t->c + iv;
        double e = targetMs - pred;
        uint32_t k = 0;
        if (e > 0.5 * iv) {
            /* a slot passed without a picture (a held one): whole slots */
            k = (uint32_t)floor(e / iv + 0.5);
            e -= (double)k * iv;
        }
        c = pred + (double)k * iv + RD_AMT_GAIN * e;
        if (fabs(targetMs - c) > 0.5 * tickMs) {
            /* a stall or a resync: on the hand-over rule's moment */
            c = targetMs;
            out->reset = true;
        }
        if (c < t->c + RD_AMT_MIN_STEP * iv) {
            c = t->c + RD_AMT_MIN_STEP * iv; /* never back, never the same moment */
        }
        out->slots = k;
    }
    if (fresh && t->have) {
        /* a tick's first picture: how far below the close the timer would
           be without its lead; the lead the largest of the last 16 (0 when
           they all had room) */
        t->need[t->needAt] = tickAtMs - (c - t->lead);
        t->needAt = (t->needAt + 1u) % RD_AMT_NEED;
        if (t->needN < RD_AMT_NEED) {
            t->needN++;
        }
        double l = 0.0;
        for (uint32_t i = 0; i < t->needN; i++) {
            l = t->need[i] > l ? t->need[i] : l;
        }
        t->lead = l;
    }
    /* inside the frame pair's tick: a moment before the close would clamp
       the alpha at 0 (the picture before's pair's end), one past the newest
       frame at 0.999; the timer itself is held there instead, so the next
       step goes on from the moment drawn */
    if (c < tickAtMs) {
        c = tickAtMs;
        out->window = true;
    } else if (c > tickAtMs + 0.999 * tickMs) {
        c = tickAtMs + 0.999 * tickMs;
        out->window = true;
    }
    note(t, handMs);
    t->have = true;
    t->frame = frame;
    t->hand = handMs;
    t->c = c;
    out->momentMs = c;
    out->intervalMs = iv;
    out->errorMs = targetMs - t->lead - c;
    out->leadMs = t->lead;
}

bool rd_as_made_timer_decide(int env, bool defaultOn, bool android, const char **why)
{
    if (env == 0) {
        *why = "ICO_RD_AS_MADE_TIMER=0: the tests' switch";
        return false;
    }
    if (env == 1) {
        *why = "ICO_RD_AS_MADE_TIMER=1: the tests' switch";
        return true;
    }
    if (!defaultOn) {
        *why = "RD_AS_MADE_TIMER_DEFAULT 0: computed and reported";
        return false;
    }
    if (!android) {
        /* the timer was measured on the owner's phone only; a desktop as
           made (vsync off, Uncapped) keeps the v0.11.5 rule to the bit */
        *why = "not Android: the hand-over rule kept, the timer measured on a phone only; "
               "computed and reported";
        return false;
    }
    *why = "Android: each picture's moment from its own timer";
    return true;
}

static int s_on = -1;
static const char *s_why = "";

static void read_switch(void)
{
    if (s_on >= 0) {
        return;
    }
    const char *e = getenv("ICO_RD_AS_MADE_TIMER");
    const int env = e != NULL && e[0] != '\0' ? (e[0] != '0' ? 1 : 0) : -1;
#ifdef __ANDROID__
    const bool android = true;
#else
    const bool android = false;
#endif
    s_on = rd_as_made_timer_decide(env, RD_AS_MADE_TIMER_DEFAULT != 0, android, &s_why) ? 1 : 0;
}

bool rd_as_made_timer_on(void)
{
    read_switch();
    return s_on != 0;
}

const char *rd_as_made_timer_why(void)
{
    read_switch();
    return s_why;
}
