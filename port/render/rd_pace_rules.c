/* rd_pace_rules.c: the three rules side by side (rd_pace_rules.h). */
#include "rd_pace_rules.h"

#include <stdio.h>

#include "attempt_ledger.h"
#include "rd_as_made_timer.h"
#include "rd_pace_even.h"
#include "rd_pace_gpu.h"
#include "rd_pace_screen.h"
#include "rd_rebase.h"

/* the poster's */
static RdAsMadeTimer s_timer;
static int s_state = -1; /* the timer's row as last decided: 0 paced, 1 as made */
static uint32_t s_changes;

static void decide(bool asMade)
{
    const int st = asMade ? 1 : 0;
    if (st == s_state) {
        return;
    }
    s_state = st;
    ico_attempt_decide_as_made_timer(st, rd_as_made_timer_on() ? 1 : 0);
    const uint32_t k = s_changes++;
    if (asMade && (k < 8u || k % 100u == 0u)) {
        fprintf(stderr,
                "rd: in-between pictures on their own timer: %s (%s); each picture's moment the "
                "one before plus the mean hand-over interval over the last %.0f ticks, pulled "
                "%.2f of the way to the hand-over rule's moment, a held picture's slot taken "
                "whole, a lead kept for a tick's first picture after the close, held inside the "
                "frame's tick; picture by picture the three rules go to the on-glass line\n",
                rd_as_made_timer_on() ? "on" : "off by default",
                ico_attempt_why_of(ICO_ATT_AS_MADE_TIMER), RD_AMT_WINDOW_TICKS, RD_AMT_GAIN);
    }
}

double rd_pace_rules_pick(double raw, double appliedMs, double wouldMs, double closeAtMs,
                          double tickMs, double handMs, uint32_t frame, bool kept, RdRuleNote *note)
{
    decide(true);
    const double rawHand = appliedMs != 0.0 ? raw - appliedMs / tickMs : raw;
    const double rawGpu = rawHand + wouldMs / tickMs;
    RdAsMadeTimerOut to;
    rd_as_made_timer_step(&s_timer, closeAtMs + rawHand * tickMs, handMs, frame, closeAtMs, tickMs,
                          &to);
    const double rawTimer = (to.momentMs - closeAtMs) / tickMs;
    ico_attempt_fire(ICO_ATT_AS_MADE_TIMER, 1u);
    if (to.window) {
        ico_attempt_fire2(ICO_ATT_AS_MADE_TIMER, 1u);
    }
    note->alpha[RD_RULE_HAND] = rd_rebase_alpha(rawHand);
    note->alpha[RD_RULE_GPU] = rd_rebase_alpha(rawGpu);
    note->alpha[RD_RULE_TIMER] = rd_rebase_alpha(rawTimer);
    note->tickMs = tickMs;
    /* the value drawn otherwise: the timer's where it is on, else the one
       presentRebase made, untouched */
    const bool timer = rd_as_made_timer_on();
    const double rawOther = timer ? rawTimer : raw;
    const uint8_t ruleOther =
        timer ? RD_RULE_TIMER : (rd_pace_gpu_on() ? RD_RULE_GPU : RD_RULE_HAND);
    /* the screen's rule over it (rd_pace_screen.h): computed at every
       picture as made, drawn where it is on and could predict, or where the
       even cadence aimed the picture at a refresh (rd_pace_even.h); a kept
       frame's present (its GPU work not in the queue model, rd_thread.c
       presentRebase) neither placed, sampled nor scored */
    const double endPred = kept ? 0.0 : rd_pace_gpu_pred_end();
    const double target = rd_pace_even_take_live(endPred, kept);
    const RdPaceScreenIn in = {handMs, frame, closeAtMs, tickMs, endPred, rawOther, kept, target};
    RdPaceScreenPlace sp;
    rd_pace_screen_step(&in, &sp);
    note->alpha[RD_RULE_SCREEN] = rd_rebase_alpha(sp.raw);
    note->screenEndMs = in.endPredMs > 0.0 ? in.endPredMs : 0.0; /* the model's samples */
    note->screenAtMs = sp.how == RD_PS_PLACED ? sp.hardMs : 0.0;
    if (kept) {
        /* no motion drawn: no rule's content on the on-glass line */
        for (int i = 0; i < RD_RULE_COUNT; i++) {
            note->alpha[i] = -1.0f;
        }
    }
    if ((rd_pace_screen_on() || target > 0.0) && sp.how == RD_PS_PLACED) {
        note->rule = RD_RULE_SCREEN;
        return sp.raw;
    }
    note->rule = ruleOther;
    return rawOther;
}

void rd_pace_rules_paced(void)
{
    if (s_timer.have) {
        rd_as_made_timer_reset(&s_timer);
    }
    decide(false);
    rd_pace_screen_paced();
    rd_pace_even_paced();
}

static _Thread_local RdRuleNote t_note;
static _Thread_local bool t_have;

void rd_pace_rules_note(const RdRuleNote *note)
{
    t_have = note != NULL;
    if (note != NULL) {
        t_note = *note;
    }
}

void rd_pace_rules_take(RdRuleNote *out)
{
    if (t_have) {
        *out = t_note;
    } else {
        *out = (RdRuleNote)RD_RULE_NOTE_NONE;
    }
    t_have = false;
}
