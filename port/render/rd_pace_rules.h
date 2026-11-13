/* rd_pace_rules.h: the three rules for the moment of a picture made as fast
 * as the GPU allows, computed side by side at every such picture
 * (rd_pace_rules.c; docs/port/DISPLAY.md "The alpha"):
 *
 *   RD_RULE_HAND   the hand-over less the drain's running mean (rd_rebase.h:
 *                  drawn where the others are not: a desktop's default);
 *   RD_RULE_GPU    the predicted GPU end less the mean latency
 *                  (rd_pace_gpu.h: drawn with ICO_RD_PACE_GPU=1);
 *   RD_RULE_TIMER  the own timer (rd_as_made_timer.h: drawn on Android by
 *                  default, ICO_RD_AS_MADE_TIMER=0/1 over it; over the GPU
 *                  end);
 *   RD_RULE_SCREEN the time the screen will show the picture (rd_pace_screen.h:
 *                  drawn by default on Android only, over the others; where
 *                  it cannot predict, the value of the rule drawn
 *                  otherwise);
 *   RD_RULE_CLOCK  the present clock or the lock's grid: paced modes (the
 *                  others not computed).
 *
 * Each picture's alpha by every rule computed goes with it to its record
 * (rd__perf_begin: RdPerfRecord.ruleAlpha), so the on-glass line (rd_glass.h)
 * can put each rule's content against the display's own times in one run. */
#ifndef ICO_RD_PACE_RULES_H
#define ICO_RD_PACE_RULES_H

#include <stdbool.h>
#include <stdint.h>

enum {
    RD_RULE_HAND = 0,
    RD_RULE_GPU,
    RD_RULE_TIMER,
    RD_RULE_SCREEN,
    RD_RULE_COUNT,
    RD_RULE_CLOCK = RD_RULE_COUNT
};

/* a picture's rules, as handed over */
typedef struct RdRuleNote {
    float alpha[RD_RULE_COUNT]; /* -1: not computed */
    uint8_t rule;               /* the drawn one (RD_RULE_CLOCK: paced) */
    double tickMs;              /* the tick (0 not known) */
    /* the screen's prediction for the on-glass line's score and the
       model's samples (rd_pace_screen.h): the GPU end it was predicted
       from (rd's clock) and the refresh predicted (the display's clock);
       0 none */
    double screenEndMs, screenAtMs;
    /* the even cadence's desired present time (rd_pace_even.h; the
       display's clock, ms; 0 none) */
    double desiredMs;
} RdRuleNote;

/* a note with nothing computed (a present not made as fast as the GPU
   allows, or no note) */
#define RD_RULE_NOTE_NONE {{-1.0f, -1.0f, -1.0f, -1.0f}, RD_RULE_CLOCK, 0.0, 0.0, 0.0, 0.0}

/* the poster's thread, a picture as made: raw the value presentRebase made
   (the hand-over rule's, plus applied / tick where the GPU end is drawn),
   appliedMs that shift, wouldMs the model's shift either way, closeAtMs the
   frame's close (alpha 0), tickMs, handMs the hand-over, frame the newer
   frame's number, kept a kept frame's present (a fade, a load: its alpha
   not drawn, rd_interp.c rd_present's keep path; no rule's alpha noted,
   the screen's model and the even cadence leave it out).  Returns the raw
   value to draw (raw itself unless the timer or the screen's rule is
   drawn: the screen's where it is on and placed the picture, or where the
   even cadence aimed the picture, rd_pace_even.h); *note the four rules'
   alphas, the drawn one and the screen's prediction */
double rd_pace_rules_pick(double raw, double appliedMs, double wouldMs, double closeAtMs,
                          double tickMs, double handMs, uint32_t frame, bool kept,
                          RdRuleNote *note);
/* the poster's thread, the pictures paced (rd_present_as_made(false)):
   the timer and the screen's model emptied, their ledger rows not
   applicable */
void rd_pace_rules_paced(void);

/* the picture about to be made on this thread (rd_thread.c presentJob,
   NULL after it), taken by the record (rd__perf_begin; a note with rule
   RD_RULE_CLOCK and no alphas when none) */
void rd_pace_rules_note(const RdRuleNote *note);
void rd_pace_rules_take(RdRuleNote *out);

#endif
