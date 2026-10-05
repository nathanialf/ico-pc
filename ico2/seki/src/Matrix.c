#include "typedef.h"
#include "tableSin.h"
#include "debug_exception.h"
#include <assert.h>

/* Quadword copy of 64 bytes, parallel form: four lq into four distinct
   scratch GPRs, then four sq.  Only this file has it, so it is defined here
   rather than in ../common/include/typedef.h with the wrappers other trees
   share. */
#define QCOPY64_PARALLEL(s0, s1, s2, s3) /* derived name */                                        \
    __asm__ __volatile__("lq " s0 ", 0($5)" : : : "memory");                                       \
    __asm__ __volatile__("lq " s1 ", 0x10($5)" : : : "memory");                                    \
    __asm__ __volatile__("lq " s2 ", 0x20($5)" : : : "memory");                                    \
    __asm__ __volatile__("lq " s3 ", 0x30($5)" : : : "memory");                                    \
    __asm__ __volatile__("sq " s0 ", 0($4)" : : : "memory");                                       \
    __asm__ __volatile__("sq " s1 ", 0x10($4)" : : : "memory");                                    \
    __asm__ __volatile__("sq " s2 ", 0x20($4)" : : : "memory");                                    \
    __asm__ __volatile__("sq " s3 ", 0x30($4)" : : : "memory")

/* the scratch matrix _ScaleMatrixV fills in and multiplies through */
static float scaleWorkMatrix[4][4] = {{1.0f, 0.0f, 0.0f, 0.0f},
                                      {0.0f, 1.0f, 0.0f, 0.0f},
                                      {0.0f, 0.0f, 1.0f, 0.0f},
                                      {0.0f, 0.0f, 0.0f, 1.0f}}; /* derived name */

#include "Matrix.h"

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline void _InitCurrentMatrix(void)
{
    VU0_V2OP(vmove.xyzw, 7, 0);
    VU0_V2OP(vmr32.xyzw, 6, 7);
    VU0_V2OP(vmr32.xyzw, 5, 6);
    VU0_V2OP(vmr32.xyzw, 4, 5);
    VU0_REG("viaddi $vi15, $vi0, 0x0");
}

inline void _UnitCurrentMatrix(void)
{
    VU0_V2OP(vmove.xyzw, 7, 0);
    VU0_V2OP(vmr32.xyzw, 6, 7);
    VU0_V2OP(vmr32.xyzw, 5, 6);
    VU0_V2OP(vmr32.xyzw, 4, 5);
    VU0_REG("viaddi $vi15, $vi0, 0x0");
}

inline void _PushCurrentMatrix(void)
{
    VU0_REG("vsqi.xyzw $vf4, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf5, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf6, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf7, ($vi15++)");
}

inline void _PopCurrentMatrix(void)
{
    VU0_REG("vlqd.xyzw $vf7, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf6, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf5, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf4, (--$vi15)");
}

inline void _TransCurrentMatrix(void *v)
{
    VU0_LSV(lqc2, 8, 0x0, 4);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 7, 7, 8, w);
}

inline void _SetTransCurrentMatrix(void *v)
{
    VU0_LSV(lqc2, 8, 0x0, 4);
    VU0_V2OP(vmove.xyzw, 7, 8);
}

inline void _ClearTransCurrentMatrix(void)
{
    VU0_V2OP(vmove.xyzw, 7, 0);
}

inline void _RotCurrentMatrixX(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    __asm__ __volatile__("mfc1 $8, %0" : : "f"(c) : "$8");
    __asm__ __volatile__("mfc1 $9, %0" : : "f"(s) : "$9");
    VU0_QMTC2_NI(8, 1);
    VU0_QMTC2_NI(9, 2);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V2OP(vmr32.xyzw, 16, 17);
    VU0_V2OP(vmr32.xyzw, 15, 16);
    VU0_V2OP(vmr32.xyzw, 14, 15);
    VU0_V3OP_BC(vaddx.y, 15, 0, 1, x);
    VU0_V3OP_BC(vsubx.y, 16, 0, 2, x);
    VU0_V3OP_BC(vaddx.z, 15, 0, 2, x);
    VU0_V3OP_BC(vaddx.z, 16, 0, 1, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 14, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 15, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 16, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 17, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 14);
    VU0_V2OP(vmove.xyzw, 5, 15);
    VU0_V2OP(vmove.xyzw, 6, 16);
    VU0_V2OP(vmove.xyzw, 7, 17);
}

inline void _RotCurrentMatrixY(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    __asm__ __volatile__("mfc1 $8, %0" : : "f"(c) : "$8");
    __asm__ __volatile__("mfc1 $9, %0" : : "f"(s) : "$9");
    VU0_QMTC2_NI(8, 1);
    VU0_QMTC2_NI(9, 2);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V2OP(vmr32.xyzw, 16, 17);
    VU0_V2OP(vmr32.xyzw, 15, 16);
    VU0_V2OP(vmr32.xyzw, 14, 15);
    VU0_V3OP_BC(vaddx.x, 14, 0, 1, x);
    VU0_V3OP_BC(vaddx.x, 16, 0, 2, x);
    VU0_V3OP_BC(vsubx.z, 14, 0, 2, x);
    VU0_V3OP_BC(vaddx.z, 16, 0, 1, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 14, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 15, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 16, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 17, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 14);
    VU0_V2OP(vmove.xyzw, 5, 15);
    VU0_V2OP(vmove.xyzw, 6, 16);
    VU0_V2OP(vmove.xyzw, 7, 17);
}

inline void _RotCurrentMatrixZ(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    __asm__ __volatile__("mfc1 $8, %0" : : "f"(c) : "$8");
    __asm__ __volatile__("mfc1 $9, %0" : : "f"(s) : "$9");
    VU0_QMTC2_NI(8, 1);
    VU0_QMTC2_NI(9, 2);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V2OP(vmr32.xyzw, 16, 17);
    VU0_V2OP(vmr32.xyzw, 15, 16);
    VU0_V2OP(vmr32.xyzw, 14, 15);
    VU0_V3OP_BC(vaddx.x, 14, 0, 1, x);
    VU0_V3OP_BC(vsubx.x, 15, 0, 2, x);
    VU0_V3OP_BC(vaddx.y, 14, 0, 2, x);
    VU0_V3OP_BC(vaddx.y, 15, 0, 1, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 14, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 15, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 16, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 17, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 14);
    VU0_V2OP(vmove.xyzw, 5, 15);
    VU0_V2OP(vmove.xyzw, 6, 16);
    VU0_V2OP(vmove.xyzw, 7, 17);
}

inline void _ScaleCurrentMatrix(float sx, float sy, float sz)
{
    VU0_MFC1(6, 12);
    VU0_MFC1(7, 13);
    VU0_MFC1(8, 14);
    VU0_QMTC2_NI(6, 1);
    VU0_QMTC2_NI(7, 2);
    VU0_QMTC2_NI(8, 3);
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V2OP(vmr32.xyzw, 16, 17);
    VU0_V2OP(vmr32.xyzw, 15, 16);
    VU0_V2OP(vmr32.xyzw, 14, 15);
    VU0_V3OP_BC(vaddx.x, 14, 0, 1, x);
    VU0_V3OP_BC(vaddx.y, 15, 0, 2, x);
    VU0_V3OP_BC(vaddx.z, 16, 0, 3, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 14, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 15, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 16, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 17, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 14);
    VU0_V2OP(vmove.xyzw, 5, 15);
    VU0_V2OP(vmove.xyzw, 6, 16);
    VU0_V2OP(vmove.xyzw, 7, 17);
}

inline void _GetCurrentMatrix(void *dst)
{
    VU0_LSV(sqc2, 4, 0x0, 4);
    VU0_LSV(sqc2, 5, 0x10, 4);
    VU0_LSV(sqc2, 6, 0x20, 4);
    VU0_LSV(sqc2, 7, 0x30, 4);
}

inline void _GetCurrentMatrixTrans(void *dst)
{
    VU0_LSV(sqc2, 7, 0x0, 4);
}

inline void _SetCurrentMatrix(void *m)
{
    VU0_LSV(lqc2, 4, 0x0, 4);
    VU0_LSV(lqc2, 5, 0x10, 4);
    VU0_LSV(lqc2, 6, 0x20, 4);
    VU0_LSV(lqc2, 7, 0x30, 4);
}

inline void _MulCurrentMatrixR(void *m)
{
    VU0_LSV(lqc2, 14, 0x0, 4);
    VU0_LSV(lqc2, 15, 0x10, 4);
    VU0_LSV(lqc2, 16, 0x20, 4);
    VU0_LSV(lqc2, 17, 0x30, 4);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 14, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 14, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 14, z);
    VU0_V3OP_BC(vmaddw.xyzw, 14, 7, 14, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 15, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 15, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 15, z);
    VU0_V3OP_BC(vmaddw.xyzw, 15, 7, 15, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 16, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 16, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 16, z);
    VU0_V3OP_BC(vmaddw.xyzw, 16, 7, 16, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 17, 7, 17, w);
    VU0_V2OP(vmove.xyzw, 4, 14);
    VU0_V2OP(vmove.xyzw, 5, 15);
    VU0_V2OP(vmove.xyzw, 6, 16);
    VU0_V2OP(vmove.xyzw, 7, 17);
}

inline void _MulCurrentMatrixL(void *m)
{
    VU0_LSV(lqc2, 14, 0x0, 4);
    VU0_LSV(lqc2, 15, 0x10, 4);
    VU0_LSV(lqc2, 16, 0x20, 4);
    VU0_LSV(lqc2, 17, 0x30, 4);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 14, 4, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 15, 4, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 16, 4, z);
    VU0_V3OP_BC(vmaddw.xyzw, 4, 17, 4, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 14, 5, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 15, 5, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 16, 5, z);
    VU0_V3OP_BC(vmaddw.xyzw, 5, 17, 5, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 14, 6, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 15, 6, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 16, 6, z);
    VU0_V3OP_BC(vmaddw.xyzw, 6, 17, 6, w);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 14, 7, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 15, 7, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 16, 7, z);
    VU0_V3OP_BC(vmaddw.xyzw, 7, 17, 7, w);
}

inline void _ApplyCurrentMatrix(void *dst, void *src)
{
    VU0_LSV(lqc2, 8, 0x0, 5);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 7, 8, w);
    VU0_LSV(sqc2, 10, 0x0, 4);
}

inline void _RotTransPersCurrentMatrix(void *dst, void *src)
{
    VU0_LSV(lqc2, 8, 0x0, 5);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 7, 8, w);
    VU0_REG("vdiv Q, $vf0w, $vf10w");
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf10, $vf10, Q");
    VU0_LSV(sqc2, 10, 0x0, 4);
}

inline void _RotTransCurrentMatrix(void *dst, void *src)
{
    VU0_LSV(lqc2, 8, 0x0, 5);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 7, 8, w);
    VU0_REG("vdiv Q, $vf0w, $vf10w");
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf10, $vf10, Q");
    VU0_V2OP(vftoi4.xyz, 10, 10);
    VU0_V3OP(vsub.xy, 14, 10, 11);
    VU0_V3OP(vsub.xy, 15, 12, 11);
    VU0_V3OP(vsub.zw, 14, 14, 14);
    VU0_V3OP(vsub.zw, 15, 15, 15);
    VU0_V3OP_ACC(vopmula.xyz, 14, 15);
    VU0_V3OP(vopmsub.xyz, 16, 15, 14);
    VU0_V2OP(vmr32.y, 16, 16);
    VU0_V2OP(vmr32.x, 16, 16);
    VU0_LSV(sqc2, 10, 0x0, 4);
    VU0_QMFC2_NI(7, 16);
    VU0_MTC1(7, 0);
    VU0_V2OP(vmove.xy, 12, 11);
    VU0_V2OP(vmove.xy, 11, 10);
}

inline void _TransposeCurrentMatrix(void)
{
    VU0_V3OP(vsub.xyzw, 1, 0, 0);
    VU0_V3OP_BC(vaddx.y, 14, 1, 5, x);
    VU0_V3OP_BC(vaddx.z, 14, 1, 6, x);
    VU0_V3OP_BC(vaddx.w, 14, 1, 7, x);
    VU0_V3OP_BC(vaddy.x, 15, 1, 4, y);
    VU0_V3OP_BC(vaddy.z, 15, 1, 6, y);
    VU0_V3OP_BC(vaddy.w, 15, 1, 7, y);
    VU0_V3OP_BC(vaddz.x, 16, 1, 4, z);
    VU0_V3OP_BC(vaddz.y, 16, 1, 5, z);
    VU0_V3OP_BC(vaddz.w, 16, 1, 7, z);
    VU0_V3OP_BC(vaddw.x, 17, 1, 4, w);
    VU0_V3OP_BC(vaddw.y, 17, 1, 5, w);
    VU0_V3OP_BC(vaddw.z, 17, 1, 6, w);
    VU0_V2OP(vmove.yzw, 4, 14);
    VU0_V2OP(vmove.xzw, 5, 15);
    VU0_V2OP(vmove.xyw, 6, 16);
    VU0_V2OP(vmove.xyz, 7, 17);
}

inline void _TransposeRotationCurrentMatrix(void)
{
    VU0_V3OP(vsub.xyzw, 1, 0, 0);
    VU0_V3OP_BC(vaddx.y, 14, 1, 5, x);
    VU0_V3OP_BC(vaddx.z, 14, 1, 6, x);
    VU0_V3OP_BC(vaddy.x, 15, 1, 4, y);
    VU0_V3OP_BC(vaddy.z, 15, 1, 6, y);
    VU0_V3OP_BC(vaddz.x, 16, 1, 4, z);
    VU0_V3OP_BC(vaddz.y, 16, 1, 5, z);
    VU0_V2OP(vmove.yz, 4, 14);
    VU0_V2OP(vmove.xz, 5, 15);
    VU0_V2OP(vmove.xy, 6, 16);
}

inline void _InverseCurrentMatrix(void)
{
    VU0_V3OP(vsub.xyzw, 1, 0, 0);
    VU0_V3OP_BC(vsubw.x, 2, 0, 0, w);
    VU0_V3OP_BC(vaddx.y, 14, 1, 5, x);
    VU0_V3OP_BC(vaddx.z, 14, 1, 6, x);
    VU0_V3OP_BC(vaddy.x, 15, 1, 4, y);
    VU0_V3OP_BC(vaddy.z, 15, 1, 6, y);
    VU0_V3OP_BC(vaddz.x, 16, 1, 4, z);
    VU0_V3OP_BC(vaddz.y, 16, 1, 5, z);
    VU0_V3OP_BC(vmulx.xyz, 17, 7, 2, x);
    VU0_V2OP(vmove.yz, 4, 14);
    VU0_V2OP(vmove.xz, 5, 15);
    VU0_V2OP(vmove.xy, 6, 16);
    VU0_V2OP(vmove.w, 17, 0);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 4, 17, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 5, 17, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 6, 17, z);
    VU0_V3OP_BC(vmaddw.xyzw, 7, 0, 17, w);
}

#endif /* ICO_HOST: port/math */

/* 32 quadwords, the size of the VU0 register file the push and pop save;
   nothing addresses it (the saves go to VU0 memory through vi15). */
static float vu0RegisterSave[32][4] = {{0.0f}}; /* derived name */

/* a second save area of the same 32 quadwords, uninitialised; nothing
   addresses it */
static float vu0RegisterSaveWork[32][4]; /* derived name */

/* the VU0 register save depth the push and pop check */
static int vu0PushDepth = 0; /* derived name */

void _PushVu0Registers(void)
{
#ifdef ICO_HOST /* vf1-vf31 hold only the current matrix on the host (docs/port/MATH.md) */
    ico_vu0_registers_push();
#else
    DI();
    VU0_REG("vsqi.xyzw $vf1, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf2, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf3, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf4, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf5, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf6, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf7, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf8, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf9, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf10, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf11, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf12, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf13, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf14, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf15, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf16, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf17, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf18, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf19, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf20, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf21, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf22, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf23, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf24, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf25, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf26, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf27, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf28, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf29, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf30, ($vi15++)");
    VU0_REG("vsqi.xyzw $vf31, ($vi15++)");
    EI();
#endif

    if (++vu0PushDepth >= 6) {
        debug_assert("src/Matrix.c", 1063);
        __assert("src/Matrix.c", 1063, "0");
    }
}

void _PopVu0Registers(void)
{
#ifdef ICO_HOST
    ico_vu0_registers_pop();
#else
    DI();
    VU0_REG("vlqd.xyzw $vf31, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf30, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf29, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf28, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf27, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf26, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf25, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf24, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf23, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf22, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf21, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf20, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf19, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf18, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf17, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf16, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf15, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf14, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf13, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf12, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf11, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf10, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf9, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf8, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf7, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf6, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf5, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf4, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf3, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf2, (--$vi15)");
    VU0_REG("vlqd.xyzw $vf1, (--$vi15)");
    EI();
#endif

    if (--vu0PushDepth < 0) {
        debug_assert("src/Matrix.c", 1119);
        __assert("src/Matrix.c", 1119, "0");
    }
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline void _NormalizeVector(void *dst, void *src)
{
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf1, 0x0(%1)\n\t"
                         "vmul.xyz $vf3, $vf1, $vf1\n\t"
                         "vmulax.w ACC, $vf0, $vf3x\n\t"
                         "vmadday.w ACC, $vf0, $vf3y\n\t"
                         "vmaddz.w $vf3, $vf0, $vf3z\n\t"
                         "vrsqrt Q, $vf0w, $vf3w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyz $vf1, $vf1, Q\n\t"
                         "sqc2 $vf1, 0x0(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(dst), "r"(src)
                         : "memory");
}

inline float _InnerProduct(void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 4);
    VU0_LSV(lqc2, 2, 0x0, 5);
    VU0_V3OP_BC(vaddw.x, 3, 0, 0, w);
    VU0_V3OP(vmul.xyz, 2, 1, 2);
    VU0_V3OP_ACC_BC(vaddax.x, 0, 2, x);
    VU0_V3OP_ACC_BC(vmadday.x, 3, 2, y);
    VU0_V3OP_BC(vmaddz.x, 2, 3, 2, z);
    VU0_QMFC2_NI(2, 2);
    VU0_MTC1(2, 0);
}

inline void _OuterProduct(void *dst, void *a, void *b)
{
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf1, 0x0(%1)\n\t"
                         "lqc2 $vf2, 0x0(%2)\n\t"
                         "vopmula.xyz ACC, $vf1, $vf2\n\t"
                         "vopmsub.xyz $vf3, $vf2, $vf1\n\t"
                         "vsub.w $vf3, $vf3, $vf3\n\t"
                         "sqc2 $vf3, 0x0(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(dst), "r"(a), "r"(b)
                         : "memory");
}

inline void _AddVector(void *dst, void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP(vadd.xyzw, 3, 1, 2);
    VU0_LSV(sqc2, 3, 0x0, 4);
}

inline void _AddVectorXYZ(void *dst, void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP(vadd.xyz, 1, 1, 2);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline void _SubVector(void *dst, void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP(vsub.xyzw, 3, 1, 2);
    VU0_LSV(sqc2, 3, 0x0, 4);
}

inline void _SubVectorXYZ(void *dst, void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP(vsub.xyz, 1, 1, 2);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline void _ScaleVector(void *dst, void *src, float s)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 2);
    VU0_NOREORDER_END();
    VU0_V3OP_BC(vmulx.xyzw, 3, 1, 2, x);
    VU0_LSV(sqc2, 3, 0x0, 4);
}

inline void _ScaleVectorXYZ(void *dst, void *src, float s)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 2);
    VU0_NOREORDER_END();
    VU0_V3OP_BC(vmulx.xyz, 1, 1, 2, x);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline void _ScaleVector2XYZ(void *dst, void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP_BC(vmulx.x, 1, 1, 2, x);
    VU0_V3OP_BC(vmuly.y, 1, 1, 2, y);
    VU0_V3OP_BC(vmulz.z, 1, 1, 2, z);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline void _FTOI4Vector(void *dst, void *src)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_V2OP(vftoi4.xyzw, 2, 1);
    VU0_LSV(sqc2, 2, 0x0, 4);
}

inline void _FTOI0Vector(void *dst, void *src)
{
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_V2OP(vftoi0.xyzw, 2, 1);
    VU0_LSV(sqc2, 2, 0x0, 4);
}

inline void _CopyVector(void *dst, void *src)
{
    __asm__ __volatile__("lq $8, 0x0(%1)\n\t"
                         "sq $8, 0x0(%0)"
                         :
                         : "r"(dst), "r"(src)
                         : "$8", "memory");
}

inline void _CopyIVector(void *dst, void *src)
{
    QCOPY16("$8");
}

inline void _UnitVector(void *dst)
{
    VU0_LSV(sqc2, 0, 0x0, 4);
}

inline void _InterVector(void *dst, void *a, void *b, float t)
{
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 3);
    VU0_NOREORDER_END();
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP_BC(vsubx.w, 8, 0, 3, x);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 1, 3, x);
    VU0_V3OP_BC(vmaddw.xyzw, 9, 2, 8, w);
    VU0_LSV(sqc2, 9, 0x0, 4);
}

inline void _InterVectorXYZ(void *dst, void *a, void *b, float t)
{
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(8, 12);
    VU0_QMTC2_NI(8, 3);
    VU0_NOREORDER_END();
    VU0_LSV(lqc2, 1, 0x0, 5);
    VU0_LSV(lqc2, 2, 0x0, 6);
    VU0_V3OP_BC(vsubx.w, 8, 0, 3, x);
    VU0_V3OP_ACC_BC(vmulax.xyz, 1, 3, x);
    VU0_V3OP_BC(vmaddw.xyz, 1, 2, 8, w);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline float _GetNorm(void *v)
{
    VU0_LSV(lqc2, 3, 0x0, 4);
    VU0_V3OP(vmul.xyz, 3, 3, 3);
    VU0_V3OP_ACC_BC(vmulax.w, 0, 3, x);
    VU0_V3OP_ACC_BC(vmadday.w, 0, 3, y);
    VU0_V3OP_BC(vmaddz.w, 3, 0, 3, z);
    VU0_WORD(0x4B8303BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

inline float _GetLength(void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 4);
    VU0_LSV(lqc2, 2, 0x0, 5);
    VU0_V3OP(vsub.xyzw, 3, 1, 2);
    VU0_V3OP(vmul.xyz, 3, 3, 3);
    VU0_V3OP_ACC_BC(vmulax.w, 0, 3, x);
    VU0_V3OP_ACC_BC(vmadday.w, 0, 3, y);
    VU0_V3OP_BC(vmaddz.w, 3, 0, 3, z);
    VU0_WORD(0x4B8303BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

inline float _GetLengthXY(void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 4);
    VU0_LSV(lqc2, 2, 0x0, 5);
    VU0_V3OP(vsub.xyzw, 3, 1, 2);
    VU0_V3OP(vmul.xy, 3, 3, 3);
    VU0_V3OP_BC(vaddy.x, 3, 3, 3, y);
    VU0_WORD(0x4A0303BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

inline float _GetLengthXZ(void *a, void *b)
{
    VU0_LSV(lqc2, 1, 0x0, 4);
    VU0_LSV(lqc2, 2, 0x0, 5);
    VU0_V3OP(vsub.xyzw, 3, 1, 2);
    VU0_V3OP(vmul.xz, 3, 3, 3);
    VU0_V3OP_BC(vaddz.x, 3, 3, 3, z);
    VU0_WORD(0x4A0303BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(2, 22);
    VU0_MTC1(2, 0);
    VU0_NOREORDER_END();
}

inline void _CopyMatrix(void *dst, const void *src)
{
    QCOPY64_PARALLEL("$6", "$7", "$8", "$9");
}

inline void _MulMatrix(void *dst, void *a, void *b)
{
    __asm__ __volatile__("lqc2 $vf14, 0x0(%1)\n\t"
                         "lqc2 $vf15, 0x10(%1)\n\t"
                         "lqc2 $vf16, 0x20(%1)\n\t"
                         "lqc2 $vf17, 0x30(%1)\n\t"
                         "lqc2 $vf24, 0x0(%2)\n\t"
                         "lqc2 $vf25, 0x10(%2)\n\t"
                         "lqc2 $vf26, 0x20(%2)\n\t"
                         "lqc2 $vf27, 0x30(%2)\n\t"
                         "vmulax.xyzw ACC, $vf14, $vf24x\n\t"
                         "vmadday.xyzw ACC, $vf15, $vf24y\n\t"
                         "vmaddaz.xyzw ACC, $vf16, $vf24z\n\t"
                         "vmaddw.xyzw $vf24, $vf17, $vf24w\n\t"
                         "vmulax.xyzw ACC, $vf14, $vf25x\n\t"
                         "vmadday.xyzw ACC, $vf15, $vf25y\n\t"
                         "vmaddaz.xyzw ACC, $vf16, $vf25z\n\t"
                         "vmaddw.xyzw $vf25, $vf17, $vf25w\n\t"
                         "vmulax.xyzw ACC, $vf14, $vf26x\n\t"
                         "vmadday.xyzw ACC, $vf15, $vf26y\n\t"
                         "vmaddaz.xyzw ACC, $vf16, $vf26z\n\t"
                         "vmaddw.xyzw $vf26, $vf17, $vf26w\n\t"
                         "vmulax.xyzw ACC, $vf14, $vf27x\n\t"
                         "vmadday.xyzw ACC, $vf15, $vf27y\n\t"
                         "vmaddaz.xyzw ACC, $vf16, $vf27z\n\t"
                         "vmaddw.xyzw $vf27, $vf17, $vf27w\n\t"
                         "sqc2 $vf24, 0x0(%0)\n\t"
                         "sqc2 $vf25, 0x10(%0)\n\t"
                         "sqc2 $vf26, 0x20(%0)\n\t"
                         "sqc2 $vf27, 0x30(%0)\n\t"
                         "nop"
                         :
                         : "r"(dst), "r"(a), "r"(b)
                         : "memory");
}

inline void _ApplyMatrix(void *dst, void *m, void *v)
{
    VU0_LSV(lqc2, 8, 0x0, 6);
    VU0_LSV(lqc2, 14, 0x0, 5);
    VU0_LSV(lqc2, 15, 0x10, 5);
    VU0_LSV(lqc2, 16, 0x20, 5);
    VU0_LSV(lqc2, 17, 0x30, 5);
    VU0_V3OP_ACC_BC(vmulax.xyzw, 14, 8, x);
    VU0_V3OP_ACC_BC(vmadday.xyzw, 15, 8, y);
    VU0_V3OP_ACC_BC(vmaddaz.xyzw, 16, 8, z);
    VU0_V3OP_BC(vmaddw.xyzw, 10, 17, 8, w);
    VU0_LSV(sqc2, 10, 0x0, 4);
}

inline void _UnitMatrix(void *dst)
{
    __asm__ __volatile__(".set noreorder\n\t"
                         "vmove.xyzw $vf17, $vf0\n\t"
                         "vmr32.xyzw $vf16, $vf17\n\t"
                         "vmr32.xyzw $vf15, $vf16\n\t"
                         "vmr32.xyzw $vf14, $vf15\n\t"
                         "sqc2 $vf14, 0x0(%0)\n\t"
                         "sqc2 $vf15, 0x10(%0)\n\t"
                         "sqc2 $vf16, 0x20(%0)\n\t"
                         "sqc2 $vf17, 0x30(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(dst)
                         : "memory");
}

inline void _UnitRotation(void *dst)
{
    VU0_V2OP(vmove.xyzw, 17, 0);
    VU0_V2OP(vmr32.xyzw, 16, 17);
    VU0_V2OP(vmr32.xyzw, 15, 16);
    VU0_V2OP(vmr32.xyzw, 14, 15);
    VU0_LSV(sqc2, 14, 0x0, 4);
    VU0_LSV(sqc2, 15, 0x10, 4);
    VU0_LSV(sqc2, 16, 0x20, 4);
}

#endif /* ICO_HOST: port/math */

inline void _ScaleMatrixV(void *dst, void *src, void *v)
{
    float *m = &scaleWorkMatrix[0][0];

    m[0] = ((float *)v)[0];
    m[5] = ((float *)v)[1];
    m[10] = ((float *)v)[2];
    _MulMatrix(dst, src, m);
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline void _TransposeMatrix(void *dst, void *src)
{
    __asm__ __volatile__("lq $8, 0x0($5)" : : : "memory");
    __asm__ __volatile__("lq $9, 0x10($5)" : : : "memory");
    __asm__ __volatile__("lq $10, 0x20($5)" : : : "memory");
    __asm__ __volatile__("lq $11, 0x30($5)" : : : "memory");
    __asm__ __volatile__("pextlw $12, $9, $8");
    __asm__ __volatile__("pextuw $13, $9, $8");
    __asm__ __volatile__("pextlw $14, $11, $10");
    __asm__ __volatile__("pextuw $15, $11, $10");
    __asm__ __volatile__("pcpyld $8, $14, $12");
    __asm__ __volatile__("pcpyud $9, $12, $14");
    __asm__ __volatile__("pcpyld $10, $15, $13");
    __asm__ __volatile__("pcpyud $11, $13, $15");
    __asm__ __volatile__("sq $8, 0x0($4)" : : : "memory");
    __asm__ __volatile__("sq $9, 0x10($4)" : : : "memory");
    __asm__ __volatile__("sq $10, 0x20($4)" : : : "memory");
    __asm__ __volatile__("sq $11, 0x30($4)" : : : "memory");
}

inline void _InversMatrix(void *dst, void *src)
{
    __asm__ __volatile__(".set noreorder\n\t"
                         "lq $8, 0x0(%1)\n\t"
                         "lq $9, 0x10(%1)\n\t"
                         "lq $10, 0x20(%1)\n\t"
                         "lqc2 $vf4, 0x30(%1)\n\t"
                         "vmove.xyzw $vf5, $vf4\n\t"
                         "vsub.xyz $vf4, $vf4, $vf4\n\t"
                         "vmove.xyzw $vf9, $vf4\n\t"
                         "qmfc2.ni $11, $vf4\n\t"
                         "pextlw $12, $9, $8\n\t"
                         "pextuw $13, $9, $8\n\t"
                         "pextlw $14, $11, $10\n\t"
                         "pextuw $15, $11, $10\n\t"
                         "pcpyld $8, $14, $12\n\t"
                         "pcpyud $9, $12, $14\n\t"
                         "pcpyld $10, $15, $13\n\t"
                         "qmtc2.ni $8, $vf6\n\t"
                         "qmtc2.ni $9, $vf7\n\t"
                         "qmtc2.ni $10, $vf8\n\t"
                         "vmulax.xyz ACC, $vf6, $vf5x\n\t"
                         "vmadday.xyz ACC, $vf7, $vf5y\n\t"
                         "vmaddz.xyz $vf4, $vf8, $vf5z\n\t"
                         "vsub.xyz $vf4, $vf9, $vf4\n\t"
                         "sq $8, 0x0(%0)\n\t"
                         "sq $9, 0x10(%0)\n\t"
                         "sq $10, 0x20(%0)\n\t"
                         "sqc2 $vf4, 0x30(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(dst), "r"(src)
                         : "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "memory");
}

#endif /* ICO_HOST: port/math */

inline void _SetCameraMatrix(void *dst, void *pos, void *dir, void *up)
{
    float m[4][4];
    float t[4];

    _UnitMatrix(m);
    _OuterProduct(t, up, dir);
    _NormalizeVector(m[0], t);
    _NormalizeVector(m[2], dir);
    _OuterProduct(m[1], m[2], m[0]);
    _CopyVector(m[3], pos);
    _InversMatrix(dst, m);
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline float _Sqrt(float x)
{
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(6, 12);
    VU0_QMTC2_NI(6, 1);
    VU0_NOREORDER_END();
    VU0_WORD(0x4A0103BD);
    VU0_WAIT();
    VU0_NOREORDER_BEGIN();
    VU0_CFC2_NI(7, 22);
    VU0_MTC1(7, 0);
    VU0_NOREORDER_END();
}

inline void _MakeNormalLightMatrix(void *dst, void *s0, void *s1, void *s2)
{
    __asm__ __volatile__("vsubw.x $vf1, $vf0, $vf0w\n\t"
                         "lqc2 $vf24, 0(%1)\n\t"
                         "lqc2 $vf25, 0(%2)\n\t"
                         "lqc2 $vf26, 0(%3)\n\t"
                         "vmulx.xyz $vf24, $vf24, $vf1x\n\t"
                         "vmulx.xyz $vf25, $vf25, $vf1x\n\t"
                         "vmulx.xyz $vf26, $vf26, $vf1x\n\t"
                         "vmul.xyz $vf14, $vf24, $vf24\n\t"
                         "vaddy.x $vf14, $vf14, $vf14y\n\t"
                         "vaddz.x $vf14, $vf14, $vf14z\n\t"
                         "vrsqrt Q, $vf0w, $vf14x\n\t"
                         "vmul.xyz $vf15, $vf25, $vf25\n\t"
                         "vaddy.x $vf15, $vf15, $vf15y\n\t"
                         "vaddz.x $vf15, $vf15, $vf15z\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyz $vf24, $vf24, Q\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vrsqrt Q, $vf0w, $vf15x\n\t"
                         "vmul.xyz $vf16, $vf26, $vf26\n\t"
                         "vaddy.x $vf16, $vf16, $vf16y\n\t"
                         "vaddz.x $vf16, $vf16, $vf16z\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyz $vf25, $vf25, Q\n\t"
                         "vnop\n\t"
                         "vnop\n\t"
                         "vrsqrt Q, $vf0w, $vf16x\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyz $vf26, $vf26, Q\n\t"
                         "vmove.xyzw $vf27, $vf0\n\t"
                         "vmove.xyzw $vf14, $vf24\n\t"
                         "vmove.xyzw $vf15, $vf25\n\t"
                         "vmove.xyzw $vf16, $vf26\n\t"
                         "vmove.xyzw $vf17, $vf27\n\t"
                         "vsub.xyzw $vf1, $vf0, $vf0\n\t"
                         "vaddx.y $vf14, $vf1, $vf25x\n\t"
                         "vaddx.z $vf14, $vf1, $vf26x\n\t"
                         "vaddx.w $vf14, $vf1, $vf27x\n\t"
                         "vaddy.x $vf15, $vf1, $vf24y\n\t"
                         "vaddy.z $vf15, $vf1, $vf26y\n\t"
                         "vaddy.w $vf15, $vf1, $vf27y\n\t"
                         "vaddz.x $vf16, $vf1, $vf24z\n\t"
                         "vaddz.y $vf16, $vf1, $vf25z\n\t"
                         "vaddz.w $vf16, $vf1, $vf27z\n\t"
                         "vaddw.x $vf17, $vf1, $vf24w\n\t"
                         "vaddw.y $vf17, $vf1, $vf25w\n\t"
                         "vaddw.z $vf17, $vf1, $vf26w\n\t"
                         "sqc2 $vf14, 0x0(%0)\n\t"
                         "sqc2 $vf15, 0x10(%0)\n\t"
                         "sqc2 $vf16, 0x20(%0)\n\t"
                         "sqc2 $vf17, 0x30(%0)\n\t"
                         "nop"
                         :
                         : "r"(dst), "r"(s0), "r"(s1), "r"(s2)
                         : "$6", "$7", "$8", "$9", "memory");
}

inline void _MakeLightColorMatrix(void *dst, void *s0, void *s1, void *s2, void *s3)
{
    __asm__ __volatile__("lq $6, 0(%1)\n\t"
                         "lq $7, 0(%2)\n\t"
                         "lq $8, 0(%3)\n\t"
                         "lq $9, 0(%4)\n\t"
                         "sq $6, 0(%0)\n\t"
                         "sq $7, 0x10(%0)\n\t"
                         "sq $8, 0x20(%0)\n\t"
                         "sq $9, 0x30(%0)\n\t"
                         "nop"
                         :
                         : "r"(dst), "r"(s0), "r"(s1), "r"(s2), "r"(s3)
                         : "$6", "$7", "$8", "$9", "memory");
}

inline void _InitRandom(float seed)
{
    VU0_NOREORDER_BEGIN();
    VU0_MFC1(6, 12);
    VU0_QMTC2_NI(6, 1);
    VU0_NOREORDER_END();
    VU0_V3OP_BC(vaddw.x, 2, 1, 0, w);
    VU0_V3OP(vadd.x, 1, 1, 1);
    VU0_REG("vrinit R, $vf2x");
    VU0_REG("vrxor R, $vf1x");
}

inline float _GetRandom(void)
{
    float ret;
    __asm__ __volatile__(".set noreorder\n"
                         "vrnext.x $vf1, R\n"
                         "vsubw.x $vf1, $vf1, $vf0w\n"
                         "qmfc2.ni $7, $vf1\n"
                         "mtc1 $7, %0\n"
                         ".set reorder\n"
                         : "=f"(ret)::"$7");
    return ret;
}

inline void _GetRandomVector(void *dst)
{
    VU0_REG("vrnext.x $vf1, R");
    VU0_REG("vrnext.y $vf1, R");
    VU0_REG("vrnext.z $vf1, R");
    VU0_V3OP_BC(vsubw.xyz, 1, 1, 0, w);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

inline void _GetRandomVector0(void *dst)
{
    VU0_REG("vrnext.xyz $vf1, R");
    VU0_V3OP_BC(vsubw.xyz, 1, 1, 0, w);
    VU0_LSV(sqc2, 1, 0x0, 4);
}

static inline void _MakeNormal3(void *dst, void *verts, int i0, int i1, int i2) /* derived name */
{
    char *v0 = (char *)((i0 << 4) + (int)verts);
    char *v1 = (char *)((i1 << 4) + (int)verts);
    char *v2 = (char *)((i2 << 4) + (int)verts);
    VU0_LSV_R(lqc2, 10, 0x0, v0);
    VU0_LSV_R(lqc2, 11, 0x0, v1);
    VU0_LSV_R(lqc2, 12, 0x0, v2);
    VU0_V3OP(vsub.w, 26, 0, 0);
    VU0_V3OP(vsub.xyz, 11, 11, 10);
    VU0_V3OP(vsub.xyz, 12, 12, 10);
    VU0_V3OP(vmul.xyz, 16, 11, 11);
    VU0_V3OP_BC(vaddz.x, 16, 16, 16, z);
    VU0_V3OP_BC(vaddy.x, 16, 16, 16, y);
    VU0_REG("vrsqrt Q, $vf0w, $vf16x");
    VU0_V3OP(vmul.xyz, 17, 12, 12);
    VU0_V3OP_BC(vaddz.x, 17, 17, 17, z);
    VU0_V3OP_BC(vaddy.x, 17, 17, 17, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf21, $vf11, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf17x");
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf22, $vf12, Q");
    VU0_V3OP_ACC(vopmula.xyz, 21, 22);
    VU0_V3OP(vopmsub.xyz, 26, 22, 21);
    VU0_LSV_R(sqc2, 26, 0x0, dst);
}

static inline void _MakeNormal4(void *dst, void *verts, int i0, int i1, int i2,
                                int i3) /* derived name */
{
    float k;
    char *v0 = (char *)((i0 << 4) + (int)verts);
    char *v1 = (char *)((i1 << 4) + (int)verts);
    char *v2 = (char *)((i2 << 4) + (int)verts);
    char *v3 = (char *)((i3 << 4) + (int)verts);
    k = 0.5f;
    VU0_LSV_R(lqc2, 10, 0x0, v0);
    VU0_LSV_R(lqc2, 11, 0x0, v1);
    VU0_LSV_R(lqc2, 12, 0x0, v2);
    VU0_LSV_R(lqc2, 13, 0x0, v3);
    VU0_V3OP(vsub.xyz, 11, 11, 10);
    VU0_V3OP(vsub.xyz, 12, 12, 10);
    VU0_V3OP(vsub.xyz, 13, 13, 10);
    VU0_V3OP(vmul.xyz, 16, 11, 11);
    VU0_V3OP_BC(vaddz.x, 16, 16, 16, z);
    VU0_V3OP_BC(vaddy.x, 16, 16, 16, y);
    VU0_REG("vrsqrt Q, $vf0w, $vf16x");
    VU0_V3OP(vmul.xyz, 17, 12, 12);
    VU0_V3OP_BC(vaddz.x, 17, 17, 17, z);
    VU0_V3OP_BC(vaddy.x, 17, 17, 17, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf21, $vf11, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf17x");
    VU0_V3OP(vmul.xyz, 18, 13, 13);
    VU0_V3OP_BC(vaddz.x, 18, 18, 18, z);
    VU0_V3OP_BC(vaddy.x, 18, 18, 18, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf22, $vf12, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf18x");
    VU0_NOREORDER_BEGIN();
    __asm__ __volatile__("mfc1 $8, $f3" : : "f"(k) : "$8");
    VU0_QMTC2_NI(8, 2);
    VU0_NOREORDER_END();
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf23, $vf18, Q");
    VU0_V3OP_ACC(vopmula.xyz, 21, 22);
    VU0_V3OP(vopmsub.xyz, 26, 22, 21);
    VU0_V3OP_ACC(vopmula.xyz, 22, 23);
    VU0_V3OP(vopmsub.xyz, 27, 23, 22);
    VU0_V3OP(vsub.w, 30, 0, 0);
    VU0_V3OP(vadd.xyz, 30, 0, 26);
    VU0_V3OP(vadd.xyz, 30, 30, 27);
    VU0_V3OP_BC(vmulx.xyz, 30, 30, 2, x);
    VU0_LSV_R(sqc2, 30, 0x0, dst);
}

static inline void _MakeNormal5(void *dst, void *verts, int i0, int i1, int i2, int i3,
                                int i4) /* derived name */
{
    float k;
    char *v0 = (char *)((i0 << 4) + (int)verts);
    char *v1 = (char *)((i1 << 4) + (int)verts);
    char *v2 = (char *)((i2 << 4) + (int)verts);
    char *v3 = (char *)((i3 << 4) + (int)verts);
    char *v4 = (char *)((i4 << 4) + (int)verts);
    k = 0.25f;
    VU0_LSV_R(lqc2, 10, 0x0, v0);
    VU0_LSV_R(lqc2, 11, 0x0, v1);
    VU0_LSV_R(lqc2, 12, 0x0, v2);
    VU0_LSV_R(lqc2, 13, 0x0, v3);
    VU0_LSV_R(lqc2, 14, 0x0, v4);
    VU0_V3OP(vsub.xyz, 11, 11, 10);
    VU0_V3OP(vsub.xyz, 12, 12, 10);
    VU0_V3OP(vsub.xyz, 13, 13, 10);
    VU0_V3OP(vsub.xyz, 14, 14, 10);
    VU0_V3OP(vmul.xyz, 16, 11, 11);
    VU0_V3OP_BC(vaddz.x, 16, 16, 16, z);
    VU0_V3OP_BC(vaddy.x, 16, 16, 16, y);
    VU0_REG("vrsqrt Q, $vf0w, $vf16x");
    VU0_V3OP(vmul.xyz, 17, 12, 12);
    VU0_V3OP_BC(vaddz.x, 17, 17, 17, z);
    VU0_V3OP_BC(vaddy.x, 17, 17, 17, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf21, $vf11, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf17x");
    VU0_V3OP(vmul.xyz, 18, 13, 13);
    VU0_V3OP_BC(vaddz.x, 18, 18, 18, z);
    VU0_V3OP_BC(vaddy.x, 18, 18, 18, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf22, $vf12, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf18x");
    VU0_V3OP(vmul.xyz, 19, 14, 14);
    VU0_V3OP_BC(vaddz.x, 19, 19, 19, z);
    VU0_V3OP_BC(vaddy.x, 19, 19, 19, y);
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf23, $vf18, Q");
    VU0_REG("vnop");
    VU0_REG("vnop");
    VU0_REG("vrsqrt Q, $vf0w, $vf19x");
    VU0_WAIT();
    VU0_REG("vmulq.xyz $vf24, $vf19, Q");
    VU0_V3OP_ACC(vopmula.xyz, 21, 22);
    VU0_V3OP(vopmsub.xyz, 26, 22, 21);
    VU0_V3OP_ACC(vopmula.xyz, 22, 23);
    VU0_V3OP(vopmsub.xyz, 27, 23, 22);
    VU0_V3OP_ACC(vopmula.xyz, 23, 24);
    VU0_V3OP(vopmsub.xyz, 28, 24, 23);
    VU0_V3OP_ACC(vopmula.xyz, 24, 21);
    VU0_V3OP(vopmsub.xyz, 29, 21, 24);
    VU0_V3OP(vsub.w, 30, 0, 0);
    VU0_V3OP(vadd.xyz, 30, 0, 26);
    __asm__ __volatile__("mfc1 $8, $f4" : : "f"(k) : "$8");
    VU0_V3OP(vadd.xyz, 30, 30, 27);
    VU0_QMTC2_NI(8, 2);
    VU0_V3OP(vadd.xyz, 30, 30, 28);
    VU0_V3OP(vadd.xyz, 30, 30, 29);
    VU0_V3OP_BC(vmulx.xyz, 30, 30, 2, x);
    VU0_LSV_R(sqc2, 30, 0x0, dst);
}

void _RemakeNormal(void *dst, void *verts, int *idx)
{
    int i;

    for (i = 0; idx[i] != -1; i++) {}

    i--;

    switch (i) {
    case 2:
        _MakeNormal3(dst, verts, idx[0], idx[1], idx[2]);
        break;
    case 3:
        _MakeNormal4(dst, verts, idx[0], idx[1], idx[2], idx[3]);
        break;
    case 4:
        _MakeNormal5(dst, verts, idx[0], idx[1], idx[2], idx[3], idx[4]);
        break;
    }
}

#endif /* ICO_HOST: port/math */
