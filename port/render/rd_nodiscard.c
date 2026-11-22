/* rd_nodiscard.c: the discard-free twins' pure part (rd_nodiscard.h): the
 * eligibility of a pass, the reasons' and modules' names, the software
 * rasteriser's test.  No renderer state: port/render/test/rd_nodiscard_test.c
 * calls it alone. */
#include "rd_nodiscard.h"

#include <string.h>

#include "rd_internal.h" /* RD_FS_VU, RD_FS_VU_TEXA */
#include "shader_consts.h"

int rd_nd_eligible(const RdNdPass *p)
{
    /* the twins exist for the *_nodual builds of vu_ps and vu_texa_ps (the
       planes' twins at a fractional scale and the destination reads have
       none) */
    if ((p->fs != RD_FS_VU && p->fs != RD_FS_VU_TEXA) || (p->nodual & RD_NODUAL_ON) == 0u ||
        p->dst != RHI_DST_READ_NONE) {
        return -1;
    }
    /* vu_pixel_tests' clip plane: DF_PLANE with a negative distance */
    if (p->plane || (p->flags & ICO_DF_PLANE) != 0u) {
        return RD_ND_PLANE;
    }
    /* vu_pixel_tests' DATE: the snapshot's test in the shader; the stencil
       bit's has none there, refused all the same (no proof wanted) */
    if ((p->flags & ICO_DF_DATE) != 0u || p->dateStencil) {
        return RD_ND_DATE;
    }
    /* vu_pixel_tests' side band: the flag is 1 only on a triangle the
       region rule draws beside the picture (VU_RULE_CLIP_BESIDE), which
       ICO_VU_NO_BESIDE turns off, clip mode none never runs and particles
       never take */
    const uint32_t clip = p->drawFlags & ICO_VU_CLIP_MASK;
    if (!p->particle && clip != ICO_VU_CLIP_NONE && (p->drawFlags & ICO_VU_NO_BESIDE) == 0u) {
        return RD_ND_BAND;
    }
    /* vu_pixel's and vu_pixel_texa's alpha test: gs_alpha_discard returns
       false whenever the ate bit (mode.z bit 8) is clear; the planner sets it
       only for a test that can fail (rd_pipeline.c, rd_alpha_plan.c) */
    if (!p->skipAlpha && (p->modeZ & 0x100u) != 0u) {
        return RD_ND_ALPHA;
    }
    /* Mesa's software clipper: a NaN band flag at the corners it adds
       behind the eye (rd_nodiscard.h); clip mode none adds none */
    if (p->softClip && (p->particle || clip != ICO_VU_CLIP_NONE)) {
        return RD_ND_CLIPPED;
    }
    return RD_ND_TWIN;
}

const char *rd_nd_reason_name(int why)
{
    static const char *const kNames[RD_ND_REASONS] = {
        "through them", "plane",   "DATE",         "band",
        "alpha",        "clipped", "not made yet", "timed without them"};
    return why >= 0 && why < RD_ND_REASONS ? kNames[why] : "?";
}

const char *rd_nd_module_name(uint8_t fs)
{
    return fs == RD_FS_VU ? "vu_ps_nodual_nd" : fs == RD_FS_VU_TEXA ? "vu_texa_ps_nodual_nd" : NULL;
}

bool rd_nd_soft_raster(uint32_t driverId, const char *adapter)
{
    return driverId == 13u || (adapter != NULL && strstr(adapter, "llvmpipe") != NULL);
}
