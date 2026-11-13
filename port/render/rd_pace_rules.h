/* rd_pace_rules.h: the three rules for the moment of a picture made as fast
 * as the GPU allows, computed side by side at every such picture
 * (rd_pace_rules.c; docs/port/DISPLAY.md "The alpha"):
 *
 *   RD_RULE_HAND   the hand-over less the drain's running mean (rd_rebase.h:
 *                  drawn by default);
 *   RD_RULE_GPU    the predicted GPU end less the mean latency
 *                  (rd_pace_gpu.h: drawn with ICO_RD_PACE_GPU=1);
 *   RD_RULE_TIMER  the own timer (rd_as_made_timer.h: drawn with
 *                  ICO_RD_AS_MADE_TIMER=1, over the GPU end);
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

enum { RD_RULE_HAND = 0, RD_RULE_GPU, RD_RULE_TIMER, RD_RULE_COUNT, RD_RULE_CLOCK = RD_RULE_COUNT };

/* a picture's rules, as handed over */
typedef struct RdRuleNote {
    float alpha[RD_RULE_COUNT]; /* -1: not computed */
    uint8_t rule;               /* the drawn one (RD_RULE_CLOCK: paced) */
    double tickMs;              /* the tick (0 not known) */
} RdRuleNote;

/* the poster's thread, a picture as made: raw the value presentRebase made
   (the hand-over rule's, plus applied / tick where the GPU end is drawn),
   appliedMs that shift, wouldMs the model's shift either way, closeAtMs the
   frame's close (alpha 0), tickMs, handMs the hand-over, frame the newer
   frame's number.  Returns the raw value to draw (raw itself unless the
   timer is drawn); *note the three rules' alphas and the drawn one */
double rd_pace_rules_pick(double raw, double appliedMs, double wouldMs, double closeAtMs,
                          double tickMs, double handMs, uint32_t frame, RdRuleNote *note);
/* the poster's thread, the pictures paced (rd_present_as_made(false)):
   the timer emptied, its ledger row not applicable */
void rd_pace_rules_paced(void);

/* the picture about to be made on this thread (rd_thread.c presentJob,
   NULL after it), taken by the record (rd__perf_begin; a note with rule
   RD_RULE_CLOCK and no alphas when none) */
void rd_pace_rules_note(const RdRuleNote *note);
void rd_pace_rules_take(RdRuleNote *out);

#endif
