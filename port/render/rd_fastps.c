/* rd_fastps.c: the fast pixel build's pure part (rd_fastps.h): the
 * eligibility of a pass at its DrawCB, the reasons' names, the judge's use.
 * No renderer state: port/render/test/rd_fastps_test.c calls it alone. */
#include "rd_fastps.h"

#include "rd_internal.h"  /* RD_FS_VU */
#include "rd_nodiscard.h" /* RD_NODUAL_ON */
#include "shader_consts.h"

int rd_fps_eligible(const RdFpsPass *p)
{
    /* vu_ps's *_nodual build (vu_ps_nodual_fast) or its discard-free twin
       the twins' own rule gave the pass (RD_NODUAL_NODISCARD:
       vu_ps_nodual_fast_nd); not a fast key already, nor vu_texa_ps, the
       planes' twins, a destination read or a device with dual-source
       blending (nodual 0) */
    if (p->fs != RD_FS_VU ||
        (p->nodual != RD_NODUAL_ON && p->nodual != (RD_NODUAL_ON | RD_NODUAL_NODISCARD)) ||
        p->dst != RHI_DST_READ_NONE) {
        return -1;
    }
    const uint32_t x = p->mode[0];
    /* the texture function (vu_texture_function_f): textured, MODULATE, TCC */
    const uint32_t tex = (uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA |
                         (p->skipDecal ? 0u : (uint32_t)ICO_DF_DECAL);
    if ((x & tex) != ((uint32_t)ICO_DF_TEXTURED | (uint32_t)ICO_DF_TCC_RGBA)) {
        return RD_FPS_TEXTURE;
    }
    /* vu_texa_expand_f's TEXFMT: RGBA32 (0) returns the texel as it is */
    if ((p->mode[1] >> 8) != 0u) {
        return RD_FPS_TEXEL;
    }
    /* gs_sample's paths other than the sampler's own */
    if ((x & ((uint32_t)ICO_DF_GS_LOD | (uint32_t)ICO_DF_NEAREST |
              (uint32_t)ICO_DF_GS_BILINEAR_GRID)) != 0u) {
        return RD_FPS_LEVELS;
    }
    /* vu_pixel_tests' DATE (and its DATM, DATE_DST words) */
    if ((x & ((uint32_t)ICO_DF_DATE | (uint32_t)ICO_DF_DATM | (uint32_t)ICO_DF_DATE_DST)) != 0u) {
        return RD_FPS_DATE;
    }
    /* vu_pixel_tests' clip plane */
    if ((x & (uint32_t)ICO_DF_PLANE) != 0u) {
        return RD_FPS_PLANE;
    }
    /* gs_alpha_discard: false whenever the ate bit (mode.z bit 8) is clear */
    if ((p->mode[2] & 0x100u) != 0u) {
        return RD_FPS_ALPHA;
    }
    /* vu_uv: "g_scale.z != 0.0" its one read */
    if (p->scaleZ != 0.0f) {
        return RD_FPS_CROP;
    }
    /* every word the build holds as a constant is the DrawCB's own (with the
       sabotage the DECAL bit is not: the self-test must see the pictures
       differ) */
    return RD_FPS_FAST;
}

const char *rd_fps_reason_name(int why)
{
    static const char *const kNames[RD_FPS_REASONS] = {"through it",
                                                       "texture",
                                                       "texel",
                                                       "levels",
                                                       "DATE",
                                                       "plane",
                                                       "alpha",
                                                       "crop",
                                                       "not made yet",
                                                       "timed without it",
                                                       "module not tested"};
    return why >= 0 && why < RD_FPS_REASONS ? kNames[why] : "?";
}

void rd_fps_use_note(RdFpsUse *u, int verdict)
{
    if (verdict == RD_FPS_FAST || verdict == RD_FPS_TRIAL) {
        u->fast = true; /* its pipeline exists: at the off arm too, the same replay's share */
    } else if (verdict == RD_FPS_UNMADE) {
        u->unmade = true;
    }
}

bool rd_fps_use_counts(const RdFpsUse *u)
{
    return u->fast && !u->unmade;
}
