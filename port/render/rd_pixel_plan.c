/* rd_pixel_plan.c: the pixel side at a fractional scale (rd_internal.h
 * rd__planes_dropped, rd__vu_pixel_fs, rd__vu_pixel_fs_enum; rd_replay.h
 * rd__fill_draw_cb_extra; RdProfile.vuPlanesFrac).
 *
 * vu_ps and vu_texa_ps compute a pixel's colour, STQ and plane distance
 * from the triangle's attribute planes at the pixel's offset from its
 * corner (vu_common.hlsli vu_pixel_attrs), and doScreen gives a screen-prim
 * triangle command its planes the same way (rd_screen_planes.c): the same
 * operations on the same values at every frame width, so the 4:3 columns
 * of a wide frame are the 4:3 frame's pixels.  That holds at a whole scale
 * only (vu_common.hlsli "the attribute planes": corners on the 1/16 grid,
 * pixel centres on k + 0.5).  On a target whose scale is not a whole
 * number on both axes (Enhanced at the window's own size: 2364x1080 is
 * 2.8125 x 2.41) the planes buy no exactness, and the GPU's screen-space
 * (noperspective) interpolation of the corners' values gives the same
 * values to a few ulp: a colour one level apart only within about 3e-5 of
 * a .5 boundary, an alpha-tested edge pixel within a level of AREF on the
 * other side (docs/port/DIVERGENCES.md).  rd__planes_dropped is that rule;
 * where it holds:
 *   - vu_ps becomes vu_np_ps (ICO_VU_NO_PLANES: locations 0 to 2 of
 *     VuVSOut read, the six flat float4 planes and their anchor not: 12
 *     input components of 40, and the planes' arithmetic gone);
 *   - a screen-prim triangle command keeps the plain sprite build
 *     (rd_screen_planes.c rd__screen_planes_want).
 * Kept with the planes: a draw with both filters nearest (fillDrawCB's
 * DF_NEAREST: the texel the GS takes at a texel boundary, gs_nearest_uv,
 * must not move with the interpolation's rounding), a draw of a texture
 * with GS levels (the level by Q, gs_lod_level, nearest between levels for
 * MMIN 2 and 4), a VU draw with a clip plane (DF_PLANE's discard at
 * distance 0), vu_texa_ps's draws (its twin vu_texa_np_ps is built, but
 * fillDrawCB's per-texel TEXA bits are vu_texa_ps's alone and the start-up
 * set would need a twin of every vu_texa_ps key), and every draw under an
 * exact blend (the twins have no destination-read builds: g_rd.dstMode).
 *
 * The start-up set: at a fractional scene scale (g_rd.sceneSx, sceneSy: the
 * scale the scene-class targets take, rd_core.c rd__target_scale_of) each
 * vu_ps key gets its vu_np_ps twin (rd__vu_pixel_fs_enum); at a whole scale
 * none.  A scale changed later (a resize, the fallback halving) makes the
 * twins it needs on first use.
 *
 * The log: the profile's start line ("VU planes whole scales only", or
 * "every scale" under [dev] vu_planes = false), one line a run the first
 * time a target drops them ("planes: ..."), and the count of VU passes
 * drawn through vu_np_ps (rd.h RdPerfRecord.vuNoPlanes: --stats, the 10 s
 * window line). */
#include <math.h>

#include "rd_replay.h"

static bool wholeScale(float s)
{
    return s == floorf(s);
}

bool rd__planes_dropped(const RdStateBlock *s, const RdTargetRec *tc)
{
    if (!g_rd.profile.vuPlanesFrac || g_rd.settings.preset != RD_PRESET_ENHANCED || !tc || !s ||
        (wholeScale(tc->sx) && wholeScale(tc->sy))) {
        return false;
    }
    const RdDrawState *d = &s->ds;
    if (d->texEnabled && d->magFilter == RD_FILTER_NEAREST && d->minFilter == RD_FILTER_NEAREST) {
        return false; /* fillDrawCB's DF_NEAREST (a pack replacement's too) */
    }
    const RdTexRec *t = d->texEnabled ? rd__tex_rec(s->tex) : NULL;
    if (t && t->kind == RD_TEXKIND_IMAGE && !t->replacement && t->gsLevels > 1) {
        return false; /* rd_replay.c drawGsLevels: sampled at the GS's level */
    }
    static bool logged; /* once a run */
    if (!logged) {
        logged = true;
        rd__log(
            "planes: a target of %ux%u texels at scale %gx%g, not a whole number: its 3D and "
            "screen-prim triangles take the GPU's interpolation without the attribute planes "
            "(vu_np_ps, the plain sprite build; nearest-filtered, GS-level and clip-plane draws "
            "keep them; [dev] vu_planes = false keeps them everywhere)",
            tc->tw, tc->th, (double)tc->sx, (double)tc->sy);
    }
    return true;
}

/* vu_np_ps is there: its optional module loaded (rd_internal.h
 * RD_FS_OPTIONAL_FIRST); without a device (the CPU tests' plans) assumed */
static bool twinLoaded(void)
{
    if (!g_rd.hasDevice || g_rd.fs[RD_FS_VU_NP].id) {
        return true;
    }
    static bool logged; /* once a run */
    if (!logged) {
        logged = true;
        rd__log("planes: vu_np_ps is not loaded: the VU draws keep their planes at every scale");
    }
    return false;
}

RdFsId rd__vu_pixel_fs(RdFsId today, const RdStateBlock *s, const RdTargetRec *tc,
                       const RdDrawPass *p)
{
    if (today != RD_FS_VU || g_rd.dstMode != RD_BLEND_EXACT_OFF ||
        (p && (p->flags & ICO_DF_PLANE)) || !rd__planes_dropped(s, tc) || !twinLoaded()) {
        return today;
    }
    return RD_FS_VU_NP;
}

uint32_t rd__vu_pixel_fs_enum(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k)
{
    if (k->fs != RD_FS_VU || !g_rd.profile.vuPlanesFrac ||
        g_rd.settings.preset != RD_PRESET_ENHANCED || g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        return n;
    }
    const float sx = g_rd.sceneSx > 0.0f ? g_rd.sceneSx : 1.0f;
    const float sy = g_rd.sceneSy > 0.0f ? g_rd.sceneSy : 1.0f;
    if ((wholeScale(sx) && wholeScale(sy)) || !twinLoaded()) {
        return n;
    }
    RdPipeKeyInt t = *k;
    t.fs = RD_FS_VU_NP;
    return rd__add_pipe_key(out, max, n, &t);
}

void rd__fill_draw_cb_extra(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                            IcoDrawCB *cb)
{
    (void)r;
    (void)ds;
    (void)cb;
    /* vu_np_ps reads the DrawCB vu_ps reads: nothing added; the pass counted */
    if (dp && dp->key.fs == RD_FS_VU_NP) {
        g_rdPerf.vuNoPlanes++;
    }
}
