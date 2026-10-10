#include "lineManager.h"
#include "GsBase.h"
#include "GifPacket.h"
#include "typedef.h"
#include "Matrix.h"
#include "main.h"
#include "matrixDrive.h"

#ifdef ICO_RD

#include "GifHost.h"

#endif

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
}

static __inline__ void clipAtX(float *d, float *a, float *b, float x) /* derived name */
{
    /* the point of a-b at x; d[3] is not written (the PS2 stored a stale
       register word there, which the callers do not use) */
    float q = ps2_div(x - a[0], b[0] - a[0]);
    float y = (b[1] - a[1]) * q + a[1];
    float z = (b[2] - a[2]) * q + a[2];

    d[0] = x;
    d[1] = y;
    d[2] = z;
}

static __inline__ void clipAtY(float *d, float *a, float *b, float y) /* derived name */
{
    /* the point of a-b at y; d[3] as in clipAtX */
    float q = ps2_div(y - a[1], b[1] - a[1]);
    float x = (b[0] - a[0]) * q + a[0];
    float z = (b[2] - a[2]) * q + a[2];

    d[0] = x;
    d[1] = y;
    d[2] = z;
}

/* Project a 3D segment to screen space and clip it to the screen, answering
 * -1 when it is off screen, else whether the end points were swapped.  One
 * of the helpers does both the near clip and the projection.  The four VU0 blocks carry
 * no memory clobber, like sugiCommon.h's distance_squared. */

int _getLine(float *o1, float *o2, float *from, float *to)
{
    float r0[4];
    float r1[4];
    int rev = 0;
    /* PC port: the x clip at the picture's sides, k times the 4:3
       half-width on a wide picture (GsBase.c; vsWidth * 0.5 at 4:3, the
       PS2's), so a line (Ico's light lines) is not cut at the 4:3 edge */
    float hw = gsb_HostLineHalfWidth();
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
    if (2048.0f + hw <= o1[0])
        return -1;
    if (o2[0] <= 2048.0f - hw)
        return -1;
    if (o1[0] < 2048.0f - hw)
        clipAtX(o1, o1, o2, 2048.0f - hw);
    if (2048.0f + hw < o2[0])
        clipAtX(o2, o1, o2, 2048.0f + hw);

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
#ifdef ICO_RD
    /* z 0 keeps the Z _getLine projected through the frame camera; any
       other z is a fixed depth that is no point's */
    gif_HostFrameProjected(z == 0);
#endif
    Draw2DLine(t2, t3, color, z);
#ifdef ICO_RD
    gif_HostFrameProjected(0);
#endif
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
#ifdef ICO_RD
    gif_HostFrameProjected(z == 0); /* as in DrawLine */
#endif
    if (r != 0) {
        Draw2DLineG(t3, fromColor, t2, toColor, z);
    } else {
        Draw2DLineG(t2, fromColor, t3, toColor, z);
    }
#ifdef ICO_RD
    gif_HostFrameProjected(0);
#endif
}
