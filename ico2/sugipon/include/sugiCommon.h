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
#ifdef ICO_HOST
    return ico_plane_distance(pos, plane);
#else
    float d;
    /* one asm block: the result goes from vf3 through $v0 to the float register */
    __asm__ __volatile__("lqc2 $vf1, 0x0(%1)\n\t"
                         "lqc2 $vf2, 0x0(%2)\n\t"
                         "vmul.xyz $vf3, $vf1, $vf2\n\t"
                         "vaddy.x $vf3, $vf3, $vf3y\n\t"
                         "vaddz.x $vf3, $vf3, $vf3z\n\t"
                         "vaddw.x $vf3, $vf3, $vf2w\n\t"
                         "qmfc2.ni $2, $vf3\n\t"
                         "mtc1 $2, %0"
                         : "=f"(d)
                         : "r"(pos), "r"(plane)
                         : "$2");
    return d;
#endif
}

/* squared distance between two points (xyz) */
static __inline__ float distance_squared(const void *a, const void *b) /* derived name */
{
#ifdef ICO_HOST
    return ico_distance_squared(a, b);
#else
    float d;
    /* one asm block in plane_distance's style, with no memory clobber */
    __asm__ __volatile__("lqc2 $vf1, 0x0(%1)\n\t"
                         "lqc2 $vf2, 0x0(%2)\n\t"
                         "vsub.wxyz $vf3, $vf1, $vf2\n\t"
                         "vmul.xyz $vf3, $vf3, $vf3\n\t"
                         "vaddy.x $vf3, $vf3, $vf3y\n\t"
                         "vaddz.x $vf3, $vf3, $vf3z\n\t"
                         "qmfc2.ni $2, $vf3\n\t"
                         "mtc1 $2, %0"
                         : "=f"(d)
                         : "r"(a), "r"(b)
                         : "$2");
    return d;
#endif
}

/* a second squared-distance helper, written as separate VU0 macro
   statements.  GetBoxHoldPoint (listing rows 87-97) and subAP1BrainMain use
   it, though the listing cites only line 87 for subAP1BrainMain, the line
   torch.c's and spider.c's distance_squared rows cite. */
static __inline__ float distance_squared_b(const void *a, const void *b) /* derived name */
{
#ifdef ICO_HOST
    return ico_distance_squared(a, b);
#else
    float d;
    int t;
    VU0_LSV_R(lqc2, 1, 0x0, a);
    VU0_LSV_R(lqc2, 2, 0x0, b);
    VU0_V3OP(vsub.wxyz, 3, 1, 2);
    VU0_V3OP(vmul.xyz, 3, 3, 3);
    VU0_V3OP_BC(vaddy.x, 3, 3, 3, y);
    VU0_V3OP_BC(vaddz.x, 3, 3, 3, z);
    __asm__ __volatile__("qmfc2.ni %0, $vf3" : "=r"(t));
    __asm__ __volatile__("mtc1 %1, %0" : "=f"(d) : "r"(t));
    return d;
#endif
}

/* squared distance in the XZ plane, in distance_squared's form */
static __inline__ float distance_squared_xz(const void *a, const void *b) /* derived name */
{
#ifdef ICO_HOST
    return ico_distance_squared_xz(a, b);
#else
    float d;
    __asm__ __volatile__("lqc2 $vf1, 0x0(%1)\n\t"
                         "lqc2 $vf2, 0x0(%2)\n\t"
                         "vsub.wxyz $vf3, $vf1, $vf2\n\t"
                         "vmul.xz $vf3, $vf3, $vf3\n\t"
                         "vaddz.x $vf3, $vf3, $vf3z\n\t"
                         "qmfc2.ni $2, $vf3\n\t"
                         "mtc1 $2, %0"
                         : "=f"(d)
                         : "r"(a), "r"(b)
                         : "$2");
    return d;
#endif
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
