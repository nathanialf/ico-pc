/* rd_upscale_hook.c: the temporal upscaler's dispatch at the upscale point
 * (rd_upscale.h rd__upscale_dispatch; docs/port/RENDER.md "The upscale
 * point").  This file holds the test's stand-in: SCENE upscaled into
 * UPSCALED by a plain bilinear pixel shader (the presenter's blit,
 * rd__present_blit, LINEAR, the whole of each), so the whole path (the
 * marker, the scale split, the edge guard, the inputs, UPSCALED, the
 * reduction's read of it) runs with no upscaler built in.  It takes
 * neither the depth, the motion vectors nor the jitter: the picture is
 * SCENE's, softened by the bilinear step, and moves by the jitter where one
 * is set.  The temporal upscaler replaces this file's body. */
#include "rd_upscale.h"

const char *rd__upscale_hook_name(void)
{
    return "the test's bilinear upscale";
}

bool rd__upscale_dispatch(RhiCommandList cl, const RdUpscaleIn *in)
{
    if (!in->color.id || !in->out.id || in->outW == 0u || in->outH == 0u) {
        return false;
    }
    rd__transition(cl, in->color, in->colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, in->out, in->outState, RHI_STATE_RENDER_TARGET);
    const RhiRect box = {0, 0, in->outW, in->outH};
    rd__present_blit(cl, in->color, in->colorW, in->colorH, in->out, RHI_FMT_RGBA8_UNORM, in->outW,
                     in->outH, RHI_LOAD_DONT_CARE, &box, RD_FILTER_LINEAR, 0, 0.0f);
    return true;
}
