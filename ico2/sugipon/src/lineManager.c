#include "lineManager.h"
#include "GsBase.h"
#include "GifPacket.h"
#include "typedef.h"
#include "Matrix.h"
#include "main.h"
#include "matrixDrive.h"

/* the GS PRIM values the plain line, the line strip and the gouraud line are
   drawn with */
static int linePrim = 0x142; /* derived name */

static int lineStripPrim = 0x189; /* derived name */

static int lineGouraudPrim = 0x18A; /* derived name */

void Draw2DLine(int *from, int *to, int *color, int z)
{
    gif_SetGsReg(0, linePrim);
    gif_SetGsReg(1, (long long)color[0] | ((long long)color[1] << 8) | ((long long)color[2] << 16) |
                        ((long long)color[3] << 24));
    if (z == 0) {
        gif_SetGsReg(5,
                     (long long)from[0] | ((long long)from[1] << 16) | ((long long)from[2] << 32));
        gif_SetGsReg(5, (long long)to[0] | ((long long)to[1] << 16) | ((long long)to[2] << 32));
    } else {
        gif_SetGsReg(5, (long long)from[0] | ((long long)from[1] << 16) | ((long long)z << 32));
        gif_SetGsReg(5, (long long)to[0] | ((long long)to[1] << 16) | ((long long)z << 32));
    }
}

void Draw2DLineSeg_Start(void)
{
    gif_SetGsReg(0, lineStripPrim);
}

void Draw2DLineSeg_Loop(int *from, int *to, int *color)
{
    gif_SetGsReg(1, (long long)color[0] | ((long long)color[1] << 8) | ((long long)color[2] << 16) |
                        ((long long)color[3] << 24));
    gif_SetGsReg(5, (long long)from[0] | ((long long)from[1] << 16) | ((long long)from[2] << 32));
    gif_SetGsReg(5, (long long)to[0] | ((long long)to[1] << 16) | ((long long)to[2] << 32));
}

void Draw2DLineG(int *from, int *fromColor, int *to, int *toColor, int z)
{
    gif_SetGsReg(0, lineGouraudPrim);
    if (z == 0) {
        gif_SetGsReg(1, (long long)fromColor[0] | ((long long)fromColor[1] << 8) |
                            ((long long)fromColor[2] << 16) | ((long long)fromColor[3] << 24));
        gif_SetGsReg(5,
                     (long long)from[0] | ((long long)from[1] << 16) | ((long long)from[2] << 32));
        gif_SetGsReg(1, (long long)toColor[0] | ((long long)toColor[1] << 8) |
                            ((long long)toColor[2] << 16) | ((long long)toColor[3] << 24));
        gif_SetGsReg(5, (long long)to[0] | ((long long)to[1] << 16) | ((long long)to[2] << 32));
    } else {
        gif_SetGsReg(1, (long long)fromColor[0] | ((long long)fromColor[1] << 8) |
                            ((long long)fromColor[2] << 16) | ((long long)fromColor[3] << 24));
        gif_SetGsReg(5, (long long)from[0] | ((long long)from[1] << 16) | ((long long)z << 32));
        gif_SetGsReg(1, (long long)toColor[0] | ((long long)toColor[1] << 8) |
                            ((long long)toColor[2] << 16) | ((long long)toColor[3] << 24));
        gif_SetGsReg(5, (long long)to[0] | ((long long)to[1] << 16) | ((long long)z << 32));
    }
}

#ifdef ICO_HOST

/* _getLine's helpers.  They were GNU nested inline functions inside it (as
 * in boy.c and staticBlur.c) that captured nothing; they are at file scope
 * so clang compiles them. */
static __inline__ void swapVector(float *a, float *b) /* derived name */
{
    float t[4];

    _CopyVector(t, a);
    _CopyVector(a, b);
    _CopyVector(b, t);
}

static __inline__ int sortByX(float *a, float *b) /* derived name */
{
    if (a[0] > b[0]) {
        swapVector(a, b);
        return 1;
    }
    return 0;
}

static __inline__ int sortByY(float *a, float *b) /* derived name */
{
    if (a[1] > b[1]) {
        swapVector(a, b);
        return 1;
    }
    return 0;
}

static __inline__ int sortByZ(float *a, float *b) /* derived name */
{
    if (a[2] > b[2]) {
        swapVector(a, b);
        return 1;
    }
    return 0;
}

static __inline__ void perspLine(float *o1, float *o2, float *a, float *b) /* derived name */
{
#ifdef ICO_HOST
    /* a behind the near plane z = 1: slide it along the line to z = 1 */
    if (a[2] < 1.0f) {
        float q = ps2_div(1.0f - a[2], b[2] - a[2]);
        a[0] = a[0] + (b[0] - a[0]) * q;
        a[1] = a[1] + (b[1] - a[1]) * q;
        a[2] = 1.0f;
    }
    /* both ends through the current matrix, all four fields times 1/w */
    {
        float qa;
        float qb;
        int k;

        ico_apply_matrix(o1, (const float (*)[4])ico_current_matrix, a);
        ico_apply_matrix(o2, (const float (*)[4])ico_current_matrix, b);
        qa = ps2_div(1.0f, o1[3]);
        for (k = 0; k < 4; k++) {
            o1[k] = o1[k] * qa;
        }
        qb = ps2_div(1.0f, o2[3]);
        for (k = 0; k < 4; k++) {
            o2[k] = o2[k] * qb;
        }
    }
#else
    if (a[2] < 1.0f) {
        __asm__ __volatile__(".set noreorder\n\t"
                             "lqc2 $vf8, 0x0(%0)\n\t"
                             "lqc2 $vf9, 0x0(%1)\n\t"
                             "vsub.z $vf14, $vf9, $vf8\n\t"
                             "vsubz.w $vf15, $vf0, $vf8z\n\t"
                             "vdiv Q, $vf15w, $vf14z\n\t"
                             "vsub.xy $vf16, $vf9, $vf8\n\t"
                             "vwaitq\n\t"
                             "vmulq.xy $vf16, $vf16, Q\n\t"
                             "vaddw.z $vf8, $vf0, $vf0w\n\t"
                             "vadd.xy $vf8, $vf8, $vf16\n\t"
                             "sqc2 $vf8, 0x0(%0)"
                             "\n\t.set reorder"
                             :
                             : "r"(a), "r"(b));
    }
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf8, 0x0(%0)\n\t"
                         "lqc2 $vf9, 0x0(%1)\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf8x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf8y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf8z\n\t"
                         "vmaddw.xyzw $vf10, $vf7, $vf8w\n\t"
                         "vmulax.xyzw ACC, $vf4, $vf9x\n\t"
                         "vmadday.xyzw ACC, $vf5, $vf9y\n\t"
                         "vmaddaz.xyzw ACC, $vf6, $vf9z\n\t"
                         "vmaddw.xyzw $vf11, $vf7, $vf9w\n\t"
                         "vdiv Q, $vf0w, $vf10w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyzw $vf10, $vf10, Q\n\t"
                         "vdiv Q, $vf0w, $vf11w\n\t"
                         "vwaitq\n\t"
                         "vmulq.xyzw $vf11, $vf11, Q\n\t"
                         "sqc2 $vf10, 0x0(%2)\n\t"
                         "sqc2 $vf11, 0x0(%3)"
                         "\n\t.set reorder"
                         :
                         : "r"(a), "r"(b), "r"(o1), "r"(o2));
#endif
}

static __inline__ void clipAtX(float *d, float *a, float *b, float x) /* derived name */
{
#ifdef ICO_HOST
    /* the point of a-b at x; d[3] is not written (the PS2 stored a stale
       register word there, which the callers do not use) */
    float q = ps2_div(x - a[0], b[0] - a[0]);
    float y = (b[1] - a[1]) * q + a[1];
    float z = (b[2] - a[2]) * q + a[2];

    d[0] = x;
    d[1] = y;
    d[2] = z;
#else
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf8, 0x0(%1)\n\t"
                         "lqc2 $vf9, 0x0(%2)\n\t"
                         "mfc1 $8, %3\n\t"
                         "qmtc2.ni $8, $vf2\n\t"
                         "vsub.x $vf15, $vf2, $vf8\n\t"
                         "vsub.x $vf14, $vf9, $vf8\n\t"
                         "vdiv Q, $vf15x, $vf14x\n\t"
                         "vmove.x $vf16, $vf2\n\t"
                         "vsub.yz $vf16, $vf9, $vf8\n\t"
                         "vwaitq\n\t"
                         "vmulq.yz $vf16, $vf16, Q\n\t"
                         "vadd.yz $vf16, $vf16, $vf8\n\t"
                         "sqc2 $vf16, 0x0(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(d), "r"(a), "r"(b), "f"(x));
#endif
}

static __inline__ void clipAtY(float *d, float *a, float *b, float y) /* derived name */
{
#ifdef ICO_HOST
    /* the point of a-b at y; d[3] as in clipAtX */
    float q = ps2_div(y - a[1], b[1] - a[1]);
    float x = (b[0] - a[0]) * q + a[0];
    float z = (b[2] - a[2]) * q + a[2];

    d[0] = x;
    d[1] = y;
    d[2] = z;
#else
    __asm__ __volatile__(".set noreorder\n\t"
                         "lqc2 $vf8, 0x0(%1)\n\t"
                         "lqc2 $vf9, 0x0(%2)\n\t"
                         "mfc1 $8, %3\n\t"
                         "qmtc2.ni $8, $vf2\n\t"
                         "vsuby.x $vf15, $vf2, $vf8y\n\t"
                         "vsub.y $vf14, $vf9, $vf8\n\t"
                         "vdiv Q, $vf15x, $vf14y\n\t"
                         "vaddx.y $vf16, $vf0, $vf2x\n\t"
                         "vsub.xz $vf16, $vf9, $vf8\n\t"
                         "vwaitq\n\t"
                         "vmulq.xz $vf16, $vf16, Q\n\t"
                         "vadd.xz $vf16, $vf16, $vf8"
                         "\n\tsqc2 $vf16, 0x0(%0)"
                         "\n\t.set reorder"
                         :
                         : "r"(d), "r"(a), "r"(b), "f"(y));
#endif
}

/* Project a 3D segment to screen space and clip it to the screen, answering
 * -1 when it is off screen, else whether the end points were swapped.  One
 * of the helpers does both the near clip and the projection.  The four VU0 blocks carry
 * no memory clobber, like sugiCommon.h's distance_squared. */
#else
/* Project a 3D segment to screen space and clip it to the screen, answering
 * -1 when it is off screen, else whether the end points were swapped.  The
 * helpers are nested inline functions, as in boy.c and staticBlur.c; one of
 * them does both the near clip and the projection.  The four VU0 blocks carry
 * no memory clobber, like sugiCommon.h's distance_squared. */
#endif

int _getLine(float *o1, float *o2, float *from, float *to)
{
#ifndef ICO_HOST
    inline void swapVector(float *a, float *b) /* derived name */
    {
        float t[4];

        _CopyVector(t, a);
        _CopyVector(a, b);
        _CopyVector(b, t);
    }

    inline int sortByX(float *a, float *b) /* derived name */
    {
        if (a[0] > b[0]) {
            swapVector(a, b);
            return 1;
        }
        return 0;
    }

    inline int sortByY(float *a, float *b) /* derived name */
    {
        if (a[1] > b[1]) {
            swapVector(a, b);
            return 1;
        }
        return 0;
    }

    inline int sortByZ(float *a, float *b) /* derived name */
    {
        if (a[2] > b[2]) {
            swapVector(a, b);
            return 1;
        }
        return 0;
    }

    inline void perspLine(float *o1, float *o2, float *a, float *b) /* derived name */
    {
        if (a[2] < 1.0f) {
            __asm__ __volatile__(".set noreorder\n\t"
                                 "lqc2 $vf8, 0x0(%0)\n\t"
                                 "lqc2 $vf9, 0x0(%1)\n\t"
                                 "vsub.z $vf14, $vf9, $vf8\n\t"
                                 "vsubz.w $vf15, $vf0, $vf8z\n\t"
                                 "vdiv Q, $vf15w, $vf14z\n\t"
                                 "vsub.xy $vf16, $vf9, $vf8\n\t"
                                 "vwaitq\n\t"
                                 "vmulq.xy $vf16, $vf16, Q\n\t"
                                 "vaddw.z $vf8, $vf0, $vf0w\n\t"
                                 "vadd.xy $vf8, $vf8, $vf16\n\t"
                                 "sqc2 $vf8, 0x0(%0)"
                                 "\n\t.set reorder"
                                 :
                                 : "r"(a), "r"(b));
        }
        __asm__ __volatile__(".set noreorder\n\t"
                             "lqc2 $vf8, 0x0(%0)\n\t"
                             "lqc2 $vf9, 0x0(%1)\n\t"
                             "vmulax.xyzw ACC, $vf4, $vf8x\n\t"
                             "vmadday.xyzw ACC, $vf5, $vf8y\n\t"
                             "vmaddaz.xyzw ACC, $vf6, $vf8z\n\t"
                             "vmaddw.xyzw $vf10, $vf7, $vf8w\n\t"
                             "vmulax.xyzw ACC, $vf4, $vf9x\n\t"
                             "vmadday.xyzw ACC, $vf5, $vf9y\n\t"
                             "vmaddaz.xyzw ACC, $vf6, $vf9z\n\t"
                             "vmaddw.xyzw $vf11, $vf7, $vf9w\n\t"
                             "vdiv Q, $vf0w, $vf10w\n\t"
                             "vwaitq\n\t"
                             "vmulq.xyzw $vf10, $vf10, Q\n\t"
                             "vdiv Q, $vf0w, $vf11w\n\t"
                             "vwaitq\n\t"
                             "vmulq.xyzw $vf11, $vf11, Q\n\t"
                             "sqc2 $vf10, 0x0(%2)\n\t"
                             "sqc2 $vf11, 0x0(%3)"
                             "\n\t.set reorder"
                             :
                             : "r"(a), "r"(b), "r"(o1), "r"(o2));
    }

    inline void clipAtX(float *d, float *a, float *b, float x) /* derived name */
    {
        __asm__ __volatile__(".set noreorder\n\t"
                             "lqc2 $vf8, 0x0(%1)\n\t"
                             "lqc2 $vf9, 0x0(%2)\n\t"
                             "mfc1 $8, %3\n\t"
                             "qmtc2.ni $8, $vf2\n\t"
                             "vsub.x $vf15, $vf2, $vf8\n\t"
                             "vsub.x $vf14, $vf9, $vf8\n\t"
                             "vdiv Q, $vf15x, $vf14x\n\t"
                             "vmove.x $vf16, $vf2\n\t"
                             "vsub.yz $vf16, $vf9, $vf8\n\t"
                             "vwaitq\n\t"
                             "vmulq.yz $vf16, $vf16, Q\n\t"
                             "vadd.yz $vf16, $vf16, $vf8\n\t"
                             "sqc2 $vf16, 0x0(%0)"
                             "\n\t.set reorder"
                             :
                             : "r"(d), "r"(a), "r"(b), "f"(x));
    }

    inline void clipAtY(float *d, float *a, float *b, float y) /* derived name */
    {
        __asm__ __volatile__(".set noreorder\n\t"
                             "lqc2 $vf8, 0x0(%1)\n\t"
                             "lqc2 $vf9, 0x0(%2)\n\t"
                             "mfc1 $8, %3\n\t"
                             "qmtc2.ni $8, $vf2\n\t"
                             "vsuby.x $vf15, $vf2, $vf8y\n\t"
                             "vsub.y $vf14, $vf9, $vf8\n\t"
                             "vdiv Q, $vf15x, $vf14y\n\t"
                             "vaddx.y $vf16, $vf0, $vf2x\n\t"
                             "vsub.xz $vf16, $vf9, $vf8\n\t"
                             "vwaitq\n\t"
                             "vmulq.xz $vf16, $vf16, Q\n\t"
                             "vadd.xz $vf16, $vf16, $vf8"
                             "\n\tsqc2 $vf16, 0x0(%0)"
                             "\n\t.set reorder"
                             :
                             : "r"(d), "r"(a), "r"(b), "f"(y));
    }

#endif
    float r0[4];
    float r1[4];
    int rev = 0;
    VECTOR w0 = {from[0], from[1], from[2], 1.0f};
    VECTOR w1 = {to[0], to[1], to[2], 1.0f};

    _SetCurrentMatrix(MatrixDrive_GetMatrix());
    _MulCurrentMatrixL(matrixptr + 0x80);
    _ApplyCurrentMatrix(r0, &w0);
    _ApplyCurrentMatrix(r1, &w1);

    if (sortByZ(r0, r1))
        rev = !rev;
    if (r1[2] < 1.0f)
        return -1;
    _SetCurrentMatrix(matrixptr + 0xC0);
    perspLine(o1, o2, r0, r1);

    if (sortByX(o1, o2))
        rev = !rev;
    if (2048.0f + vsWidth * 0.5f <= o1[0])
        return -1;
    if (o2[0] <= 2048.0f - vsWidth * 0.5f)
        return -1;
    if (o1[0] < 2048.0f - vsWidth * 0.5f)
        clipAtX(o1, o1, o2, 2048.0f - vsWidth * 0.5f);
    if (2048.0f + vsWidth * 0.5f < o2[0])
        clipAtX(o2, o1, o2, 2048.0f + vsWidth * 0.5f);

    if (sortByY(o1, o2))
        rev = !rev;
    if (2048.0f + vsHeight * 0.5f <= o1[1])
        return -1;
    if (o2[1] <= 2048.0f - vsHeight * 0.5f)
        return -1;
    if (o1[1] < 2048.0f - vsHeight * 0.5f)
        clipAtY(o1, o1, o2, 2048.0f - vsHeight * 0.5f);
    if (2048.0f + vsHeight * 0.5f < o2[1])
        clipAtY(o2, o1, o2, 2048.0f + vsHeight * 0.5f);
    return rev;
}

void DrawLine(void *from, void *to, void *color, int z)
{
    float t0[4];
    float t1[4];
    int t2[4];
    int t3[4];
    if (_getLine(t0, t1, from, to) < 0)
        return;
    _FTOI4Vector(t2, t0);
    _FTOI4Vector(t3, t1);
    Draw2DLine(t2, t3, color, z);
}

void DrawLineG(void *from, void *fromColor, void *to, void *toColor, int z)
{
    float t0[4];
    float t1[4];
    int t2[4];
    int t3[4];
    int r;

    _InitCurrentMatrix();
    r = _getLine(t0, t1, from, to);
    if (r == -1) {
        return;
    }
    _FTOI4Vector(t2, t0);
    _FTOI4Vector(t3, t1);
    if (r != 0) {
        Draw2DLineG(t3, fromColor, t2, toColor, z);
    } else {
        Draw2DLineG(t2, fromColor, t3, toColor, z);
    }
}
