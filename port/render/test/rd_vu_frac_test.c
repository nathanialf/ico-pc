/* rd_vu_frac_test.c: the VU pixel side at a fractional scale
 * (port/render/rd_pixel_plan.c), CPU only.
 *
 *   identity  every VU pass keeps its fragment entry (vu_ps, vu_texa_ps),
 *             no twin key is added to the start-up set and the DrawCB is
 *             left as fillDrawCB made it
 *
 * Exits 77 after it: the rule's own cases (whole and fractional scales,
 * the nearest-filtered draws, the twins' pixels against the planes') are
 * not written yet. */
#include "rd_replay.h"
#include "../../test/ico_check.h"

int main(void)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    RdDrawPass p;
    memset(&p, 0, sizeof(p));
    CHECK(rd__vu_pixel_fs(RD_FS_VU, &s, NULL, &p) == RD_FS_VU);
    CHECK(rd__vu_pixel_fs(RD_FS_VU_TEXA, &s, NULL, &p) == RD_FS_VU_TEXA);
    RdPipeKeyInt keys[4];
    memset(keys, 0, sizeof(keys));
    CHECK(rd__vu_pixel_fs_enum(keys, 4, 1, &keys[0]) == 1);
    IcoDrawCB cb, before;
    memset(&cb, 0x5A, sizeof(cb));
    memcpy(&before, &cb, sizeof(cb));
    rd__fill_draw_cb_extra(NULL, &p, NULL, &cb);
    CHECK(memcmp(&cb, &before, sizeof(cb)) == 0, "the DrawCB changed");
    if (ico_check_failures) {
        return ico_check_summary("rd_vu_frac_test");
    }
    printf("rd_vu_frac_test: identity ok; the rule's cases: skipped (not written)\n");
    return 77;
}
