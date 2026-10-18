/* rd_pixel_plan.c: the VU pixel side at a fractional scale (rd_internal.h
 * rd__vu_pixel_fs, rd__vu_pixel_fs_enum; rd_replay.h rd__fill_draw_cb_extra;
 * RdProfile.vuPlanesFrac).
 *
 * vu_ps and vu_texa_ps compute a pixel's colour, STQ and plane from the
 * triangle's planes at the pixel's GS offset, exact at a whole scale.  On a
 * target at a fractional scale the planes buy no exactness, and the twins
 * without them (vu_np_ps, vu_texa_np_ps: ICO_VU_NO_PLANES) read the GPU's
 * interpolation instead.
 *
 * Here every VU draw keeps its entry (rd__vu_pixel_fs returns today), the
 * start-up set gets no twin key and the DrawCB nothing more.  The counter
 * (rd.h RdPerfRecord.vuNoPlanes) counts the draw passes through a twin. */
#include "rd_replay.h"

RdFsId rd__vu_pixel_fs(RdFsId today, const RdStateBlock *s, const RdTargetRec *tc,
                       const RdDrawPass *p)
{
    (void)s;
    (void)tc;
    (void)p;
    return today;
}

uint32_t rd__vu_pixel_fs_enum(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k)
{
    (void)out;
    (void)max;
    (void)k;
    return n;
}

void rd__fill_draw_cb_extra(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                            IcoDrawCB *cb)
{
    (void)r;
    (void)dp;
    (void)ds;
    (void)cb;
}
