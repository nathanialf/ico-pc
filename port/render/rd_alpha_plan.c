/* rd_alpha_plan.c: the alpha/blend planner (rd_internal.h RdAlphaRange,
 * rd__alpha_range_of_draw, rd__plan_screen_draw_alpha; RdProfile.alphaPlan).
 *
 * A draw's planned passes (rd_pipeline.c rd__plan_screen_draw_ex) assume
 * any source alpha.  Where the range of As a draw's pixels can take
 * decides the alpha test or the blend factor, the planner draws the passes
 * that range can need: the range gives every pixel the same outcome, so
 * the bytes are the same.
 *
 * The range here is [0, 255] for every draw and the plan is left as it is:
 * every draw is planned as rd__plan_screen_draw_ex plans it.  The counters
 * (rd.h RdPerfRecord.alphaOnePass, alphaOpaque) count the draws it
 * re-planned. */
#include "rd_internal.h"

void rd__alpha_range_of_draw(const RdStateBlock *s, const RdAlphaSrc *src, RdAlphaRange *out)
{
    (void)s;
    (void)src;
    out->lo = 0;
    out->hi = 255;
}

bool rd__plan_screen_draw_alpha(const RdStateBlock *s, RdAlphaRange as, RdDrawPass out[2], int *n)
{
    (void)s;
    (void)as;
    (void)out;
    (void)n;
    return false;
}
