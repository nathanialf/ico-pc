/* rd_alpha_plan_test.c: the alpha/blend planner (port/render/rd_alpha_plan.c),
 * CPU only.
 *
 *   identity  the range of a draw is [0, 255] and the plan of a split
 *             alpha-tested draw stays as rd__plan_screen_draw made it
 *
 * Exits 77 after it: the planner's own cases (the truth table over every
 * ATST x AREF x range, the blend identities, a constant-alpha mesh in one
 * pass) are not written yet. */
#include "rd_internal.h"
#include "../../test/ico_check.h"

int main(void)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_AT_GT64_FBONLY);
    RdDrawPass dp[2], before[2];
    int n = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, RHI_FMT_RGBA8_UNORM,
                                 RHI_FMT_D32F_S8, dp);
    memcpy(before, dp, sizeof(dp));
    const int n0 = n;
    RdAlphaRange as = {7, 9};
    rd__alpha_range_of_draw(&s, &(RdAlphaSrc){.kind = RD_ALPHA_SRC_SCREEN}, &as);
    CHECK(as.lo == 0 && as.hi == 255, "range %u..%u", as.lo, as.hi);
    CHECK(!rd__plan_screen_draw_alpha(&s, as, dp, &n));
    CHECK(n == n0 && memcmp(dp, before, sizeof(dp)) == 0, "the plan changed");
    if (ico_check_failures) {
        return ico_check_summary("rd_alpha_plan_test");
    }
    printf("rd_alpha_plan_test: identity ok; the planner's cases: skipped (not written)\n");
    return 77;
}
