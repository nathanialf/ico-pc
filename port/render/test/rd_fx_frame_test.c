/* rd_fx_frame_test.c: the frame-size post sprites' plan
 * (port/render/rd_fx_plan.c), CPU only.
 *
 *   identity  no key is added to the start-up set and no sprite is drawn
 *             by the plan (rd__fx_plan_draw false)
 *
 * Exits 77 after it: the plan's own cases (the constant fold, the
 * premultiplied add, the local read against fx_sprite_ps's bytes) are not
 * written yet. */
#include "rd_replay.h"
#include "../../test/ico_check.h"

int main(void)
{
    RdPipeKeyInt keys[4];
    CHECK(rd__fx_enumerate(keys, 4, 3) == 3);
    Replay r;
    memset(&r, 0, sizeof(r));
    RdFxDraw x;
    memset(&x, 0, sizeof(x));
    CHECK(!rd__fx_plan_draw(&r, &x));
    if (ico_check_failures) {
        return ico_check_summary("rd_fx_frame_test");
    }
    printf("rd_fx_frame_test: identity ok; the plan's cases: skipped (not written)\n");
    return 77;
}
