/* rd_aura_kernel.c: the aura's feedback read above 1x drawn with the GS's
 * own taps, as a measurement ([dev] aura_kernel = gs, ICO_RD_AURA_KERNEL=gs;
 * off by default; docs/port/WORK_BUFFERS.md, "The aura's feedback read").
 *
 * The aura's copyCurrentFBToFeedBackArea (ico2/sugipon/src/staticBlur.c,
 * auraInspireAfter mode 2, while the game is not paused) reads the whole of
 * SCENE into FEED128 (W / 4 x 128 GS texels) through the area box above 1x
 * (fx_sprite.hlsl fx_sprite_texel, common.hlsli GS_RS_BOX: 4 x 4 GS texels a
 * FEED128 texel, about 20 x 10 SCENE texels at the phone's 4.617 x 2.109,
 * 5.4 M taps in all).  On the PowerVR phone that pass is 4.9 ms (the rc3
 * passes CSV's median over 1086 replays) where the reduction's 1:1 read of
 * the same SCENE, 2.5 M texels of 4 taps, is 0.72 ms: the kernel, not the
 * 10 MB, looks like the cost.  The exact way to cheapen it (the box split
 * into a row pass and a column pass in integers, WORK_BUFFERS.md) is worth
 * building only if it is.  Under aura_kernel = gs that one sprite is drawn
 * with the GS's bilinear at its own sample point instead (the PS2's taps:
 * one GS texel's area of SCENE where the box weighs sixteen), so one session
 * on the device with [dev] perf_passes says how much of the pass the kernel
 * is.  The picture is not the default's (the aura's feedback samples less of
 * the frame and shimmers above 1x): a measurement, never a setting.
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

uint32_t rd__aura_kernel_pick(uint32_t steps, int feedFromScene, const char *env)
{
    if (!steps || !feedFromScene || !env || strcmp(env, "gs") != 0) {
        return steps;
    }
    return 0; /* no above-1x kernel: fx_sprite_ps's GS bilinear (or nearest) */
}

uint32_t rd__aura_kernel_steps(const RdTargetRec *tc, const RdTargetRec *src, uint32_t steps)
{
    static int logged;
    if (!steps || !tc || !src) {
        return steps;
    }
    const int feedFromScene = tc == rd__target_rec(rd_target(RD_TARGET_FEED128).id) &&
                              src == rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const uint32_t out = rd__aura_kernel_pick(steps, feedFromScene, getenv("ICO_RD_AURA_KERNEL"));
    if (out != steps && !logged) {
        logged = 1;
        rd__log("aura: the feedback read of SCENE into FEED128 drawn with the GS's own taps "
                "([dev] aura_kernel = gs, ICO_RD_AURA_KERNEL=gs: a measurement of the box's "
                "cost, the picture not the default's); the box would take %u x %u 32nds of a "
                "texel a FEED128 texel (reported once)",
                steps >> 12, steps & 4095u);
    }
    return out;
}
