/*
 * port/include/ico_shadow_clip.h
 *
 * The box the shadow volumes' edges are clipped to (ico2/seki/src/Shadow.c
 * clipVolumeEdge), as a function of the disc and the frame's size.
 *
 * Each disc's own code clips a projected volume edge to a box about the
 * screen centre, in GS pixels:
 *
 *   the European, Japanese, Korean and Asian discs
 *       +-ScreenWidth by +-ScreenHeight: +-512 by +-448 (+-512 in the tall
 *       mode), the picture and half of it again on every side
 *   the North American disc and the two 2001 previews
 *       +-(ScreenWidth * 3 / 2) by +-(ScreenHeight * 3 / 2): +-768 by +-672
 *       (+-768 in the tall mode), the picture and all of it again on every
 *       side
 *
 * An edge with both ends past one side of the box is marked away and the
 * prisms that use it are not drawn; an edge crossing the box has its ends
 * moved onto it, and the prism is drawn between the moved ends.  So a
 * smaller box than the disc's own draws less shadow inside the picture: a
 * prism dropped whole that the console draws, and the part of a prism past
 * the line joining two moved ends.  It never draws more.
 *
 * On the native frame (ico_frame.h: gsFrame.w GS pixels across, 512 at 4:3,
 * the PS2's height) the box keeps the disc's proportion to the frame across:
 * the frame's width times 1 or 3 / 2, so at 4:3 it is the disc's own box to
 * the pixel and at a wider frame it lies as far past the picture's side, in
 * picture widths, as on the console.  Across it is at most
 * ICO_SHADOW_CLIP_X_MAX: an end moved onto the box must pass
 * Shadow.c's range test (volumeVertsOutOfRange: GS x in 0x11 .. 0xFFEF
 * sixteenths about the centre 2048), or its prism is dropped whole; 2046
 * puts it on GS x 2 or 4094.  Down it is the disc's own at every width (the
 * frame's height is the PS2's): at most 768, GS y 1280 .. 2816.
 *
 * Never smaller than the disc's own box (the frame is at least 512 across)
 * and never smaller than the frame's width capped the same way, which was
 * the box on every disc before the discs' own proportions were followed.
 *
 * Plain C, header only, included by a relative path like ico_frame.h
 * (GsBase.h includes it; port/render/test/shadow_clip_test.c checks it).
 */
#ifndef ICO_SHADOW_CLIP_H
#define ICO_SHADOW_CLIP_H

/* ICO_REGION_<SLUG>: the rows of config/regions.txt */
#include "../../ico2/common/include/region.h"

/* the largest clip across: a clipped end at GS x 2 or 4094 */
#define ICO_SHADOW_CLIP_X_MAX 2046.0f

/* Whether the disc's own box is three halves of the frame (the North
   American disc and the two 2001 previews; every other row, and a number
   that is no row, the frame itself as the European disc has it). */
static inline int ico_shadow_clip_three_halves(int region)
{
    return region == ICO_REGION_NTSC || region == ICO_REGION_AUG6 || region == ICO_REGION_JUN26;
}

/* The clip across for a frame frameW GS pixels wide: +- this about the
   centre. */
static inline float ico_shadow_clip_x(int region, int frameW)
{
    const float x = (float)(ico_shadow_clip_three_halves(region) ? frameW * 3 / 2 : frameW);

    return x > ICO_SHADOW_CLIP_X_MAX ? ICO_SHADOW_CLIP_X_MAX : x;
}

/* The clip down for a frame screenH GS pixels high (ScreenHeight): +- this
   about the centre. */
static inline float ico_shadow_clip_y(int region, int screenH)
{
    return (float)(ico_shadow_clip_three_halves(region) ? screenH * 3 / 2 : screenH);
}

#endif /* ICO_SHADOW_CLIP_H */
