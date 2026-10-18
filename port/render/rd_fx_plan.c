/* rd_fx_plan.c: the frame-size post sprites' plan (rd_replay.h RdFxDraw,
 * rd__fx_plan_draw; rd_internal.h rd__fx_enumerate; RdProfile.fxPlan,
 * fxDstLocal).
 *
 * A staticBlur sprite (rd_replay.c doBlurSpriteDraw) that reads its
 * destination takes a snapshot of its target, or swaps it, and draws in a
 * pass of its own through fx_sprite_ps.  Where the same bytes come from a
 * fixed-function draw inside the open pass (a constant fold, the
 * premultiplied add) or from the destination read by local read
 * (fx_local_ps), the plan draws it that way.
 *
 * Here every sprite takes doBlurSpriteDraw's path (rd__fx_plan_draw draws
 * nothing and returns false) and no key is added to the start-up set.  The
 * counters (rd.h RdPerfRecord.fxConstant, fxAdditive, fxLocal) count the
 * sprites it drew. */
#include "rd_replay.h"

bool rd__fx_plan_draw(Replay *r, const RdFxDraw *x)
{
    (void)r;
    (void)x;
    return false;
}

uint32_t rd__fx_enumerate(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    (void)out;
    (void)max;
    return n;
}
