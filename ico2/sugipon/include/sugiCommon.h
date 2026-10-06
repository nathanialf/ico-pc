/*
 * ico2/sugipon/include/sugiCommon.h, the `sugipon` programmer's shared
 * header: the random-number helpers, the VU0 plane and distance helpers, a
 * byte checksum and a two-view flag word.  Other programmers' files
 * include it as "../sugipon/include/sugiCommon.h".
 */
#ifndef SUGICOMMON_H
#define SUGICOMMON_H

#include <stdlib.h>
#include "typedef.h"
#include "Matrix.h"
#include "ee_view.h"

/* a display object's node-matrix and node-quaternion buffer words (Sub15C
   +0xC and +0x10 on the EE), as the buffer pointers the loaders store */
#define DOBJ_NODEMTX_PTR(d) ICO_RAW(void *, d, 0xC, *(void **)&(d)->nodeMtx)
#define DOBJ_NODEQUAT_PTR(d) ICO_RAW(void *, d, 0x10, *(void **)&(d)->nodeQuat)

/* the engine RNG, 0..1 */
static __inline__ float random_unit(void) /* derived name */
{
    return _GetRandom();
}

/* random_unit mapped to -1..+1 */
static __inline__ float random_signed(void) /* derived name */
{
    return random_unit() * 2.0f - 1.0f;
}

/* rand() scaled to 0..1 */
static __inline__ float crt_random_unit(void) /* derived name */
{
    return (float)((rand() >> 4) & 0xFFFF) * (1.0f / 65535.0f);
}

/* a second -1..+1 helper with the same body, used by EntryWaterDot,
   scpBornSpider, InitSpiderLayoutGeo, WeaponGeo and ExecWindManager */
static __inline__ float random_signed_b(void) /* derived name */
{
    return random_unit() * 2.0f - 1.0f;
}

/* Signed distance from a point to a plane: dot(plane.xyz, pos.xyz) + plane.w,
   evaluated on VU0 in macro mode.  vf1 <- pos (first argument), vf2 <- plane. */
static __inline__ float plane_distance(const void *pos, const void *plane) /* derived name */
{
    return ico_plane_distance(pos, plane);
}

/* squared distance between two points (xyz) */
static __inline__ float distance_squared(const void *a, const void *b) /* derived name */
{
    return ico_distance_squared(a, b);
}

/* a second squared-distance helper, written as separate VU0 macro
   statements.  GetBoxHoldPoint (listing rows 87-97) and subAP1BrainMain use
   it, though the listing cites only line 87 for subAP1BrainMain, the line
   torch.c's and spider.c's distance_squared rows cite. */
static __inline__ float distance_squared_b(const void *a, const void *b) /* derived name */
{
    return ico_distance_squared(a, b);
}

/* squared distance in the XZ plane, in distance_squared's form */
static __inline__ float distance_squared_xz(const void *a, const void *b) /* derived name */
{
    return ico_distance_squared_xz(a, b);
}

/* byte-sum checksum over a buffer, for ReadSkeltonFile and
   CSVSYSTEM_ReadCharFiles */
static __inline__ int byte_checksum(const unsigned char *p, int n) /* derived name */
{
    int sum;
    for (sum = 0; n > 0; n--) {
        sum += *p++;
    }
    return sum;
}

/* a flag word read as an int or as a doubleword */
typedef union DlFlag { /* field names derived */
    int i;
    long long ll;
} DlFlag; /* derived name */

#endif /* SUGICOMMON_H */
