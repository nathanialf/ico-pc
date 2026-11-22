/* rd_nodiscard.c: the discard-free twins' pure part (rd_nodiscard.h): the
 * eligibility of a pass, the reasons' and modules' names.  No renderer
 * state: port/render/test/rd_nodiscard_test.c calls it alone. */
#include "rd_nodiscard.h"

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
    /* clip mode none alone, on every device (rd_nodiscard.h, "Why clip mode
       none"): its corners are GS positions with w 1 and a depth in 0..1
       (vu_tri.hlsl vu_tri_verdict at 1 or 3, vu_common.hlsli
       vu_corner_position, vu_vtx_position, vu_gs_position; gs_math.hlsli
       gs_z_to_depth), so any clipper's new corner is a finite blend of
       corners whose side band flag is 0, and the *_nodual build's band
       test cannot fire; any other clip mode or particles may put a corner
       behind the eye, where a clipper may make the flag non-finite */
    /* ...unless the device's clipper check passed (rd_nodiscard.h, "The
       clipper check"): then the camera rule's passes too; particles never
       (the check draws none) */
    if (p->particle || (clip != ICO_VU_CLIP_NONE && !p->clipperClean)) {
        return RD_ND_CLIPPED;
    }
    /* ...and then only the forms it draws (the third review, R3-1) */
    if (clip != ICO_VU_CLIP_NONE && !rd_nd_checked_form(p)) {
        return RD_ND_FORM;
    }
    return RD_ND_TWIN;
}

bool rd_nd_checked_form(const RdNdPass *p)
{
    const uint32_t f = p->drawFlags;
    const uint32_t clip = f & ICO_VU_CLIP_MASK;
    const uint32_t off = ICO_VU_TWO_SIDED | ICO_VU_MIRROR | ICO_VU_BEHIND_EYE | ICO_VU_TICKS;
    return p->prelit && !p->particle &&
           (clip == ICO_VU_CLIP_REGION || clip == ICO_VU_CLIP_SCISSOR) &&
           (f & ICO_VU_CAMERA) != 0u && (f & ICO_VU_NO_BESIDE) != 0u && (f & off) == 0u;
}

const char *rd_nd_reason_name(int why)
{
    static const char *const kNames[RD_ND_REASONS] = {"through them",
                                                      "plane",
                                                      "DATE",
                                                      "band",
                                                      "alpha",
                                                      "clipped",
                                                      "form",
                                                      "not made yet",
                                                      "timed without them",
                                                      "entry not tested"};
    return why >= 0 && why < RD_ND_REASONS ? kNames[why] : "?";
}

const char *rd_nd_module_name(uint8_t fs)
{
    return fs == RD_FS_VU ? "vu_ps_nodual_nd" : fs == RD_FS_VU_TEXA ? "vu_texa_ps_nodual_nd" : NULL;
}

void rd_nd_use_note(RdNdUse *u, int verdict)
{
    if (verdict == RD_ND_TWIN || verdict == RD_ND_TRIAL) {
        u->twin = true; /* the twin exists: at the off arm too, the same replay's share */
    } else if (verdict == RD_ND_UNMADE) {
        u->unmade = true;
    }
}

bool rd_nd_use_counts(const RdNdUse *u)
{
    return u->twin && !u->unmade;
}
