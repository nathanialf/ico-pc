#include "debug.h"
#include "tableSin.h"
#include <string.h>
#include <math.h>
#include "matrixDrive.h"
#include <libvu0.h>

void _InterGV(float *dst, float *a, float *b, float ta, float tb)
{
    if (dst == 0 || a == 0 || b == 0) {
        debug_StdPrintfDummy("error!");
    }
    sceVu0InterVector(dst, a, b, tb / (ta + tb));
}

void GetMatrixDirectionToZ(float *out, float *dir)
{
    float v[4];
    float w[4];
    float d;

    memset(v, 0, 16);
    memset(w, 0, 16);
    d = 3.1415927f;
    MatrixDrive_PushMatrix();
    v[1] = -atan2f(dir[0], dir[2]);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixY((short)(v[1] * 32768.0f / d));
    dir[3] = 0.0f;
    sceVu0ApplyMatrix(w, MatrixDrive_GetMatrix(), dir);
    v[0] = atan2f(w[1], w[2]);
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixX((short)(v[0] * 32768.0f / d));
    MatrixDrive_RotMatrixY((short)(v[1] * 32768.0f / d));
    CopyMatrix(out, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
}

/* defined below; gv.h is not included, because its declarations of the
   inline functions at the end of this file change the order the compiler
   emits them in */
int _RotyGV(float *dir, float *base);
void _ApplyRyGV(float *vec, float ang);

int _InterRotGV(float *dst, float *cur, float *tgt, int step)
{
    float buf[4];
    int hit = 0;
    int d = _RotyGV(tgt, cur);

    if (__builtin_abs(d) < step) {
        hit = 1;
        buf[0] = cur[0];
        buf[1] = cur[1];
        buf[2] = cur[2];
    } else if (d > 0) {
        buf[0] = tgt[0];
        buf[1] = tgt[1];
        buf[2] = tgt[2];
        _ApplyRyGV(buf, -step * 3.1415927f / 180.0f);
    } else {
        buf[0] = tgt[0];
        buf[1] = tgt[1];
        buf[2] = tgt[2];
        _ApplyRyGV(buf, step * 3.1415927f / 180.0f);
    }
    dst[0] = buf[0];
    dst[1] = buf[1];
    dst[2] = buf[2];
    return hit;
}

float _DistxzSqGV(void *a, void *b)
{
    char buf[16];
    sceVu0SubVector(buf, a, b);
    *(int *)(buf + 4) = 0;
    return sceVu0InnerProduct(buf, buf);
}

float _DistSqGV(void *a, void *b)
{
    char buf[16];
    sceVu0SubVector(buf, a, b);
    return sceVu0InnerProduct(buf, buf);
}

float _DistGV(void *a, void *b)
{
    char buf[16];
    sceVu0SubVector(buf, a, b);
    return FSqrt(sceVu0InnerProduct(buf, buf));
}

float _DistxzGV(void *a, void *b)
{
    char buf[16];
    sceVu0SubVector(buf, a, b);
    *(int *)(buf + 4) = 0;
    return FSqrt(sceVu0InnerProduct(buf, buf));
}

float _MoveGV(float *dst, float *from, float *to, float step)
{
    float buf[4];
    float ang;
    sceVu0SubVector(buf, to, from);
    ang = FSqrt(buf[0] * buf[0] + buf[1] * buf[1] + buf[2] * buf[2]);
    if (ang < step) {
        dst[0] = to[0];
        dst[1] = to[1];
        dst[2] = to[2];
    } else {
        _InterGV(dst, from, to, step, ang - step);
    }
    return ang;
}

int _RotyGV(float *dir, float *base)
{
    float a = atan2f(dir[0], dir[2]);
    float b = atan2f(base[0], base[2]);
    int d = (int)((a - b) * 180.0f / 3.1415927f);

    if (d > 180)
        d -= 360;
    if (d <= -180)
        d += 360;
    return d;
}

int _AbsRotyGV(void *dir, void *base)
{
    int d = _RotyGV(dir, base);

    return (d < 0) ? -d : d;
}

void _ApplyRyGV(float *vec, float ang)
{
    float m0[16];
    float m1[16];
    float v[4];
    sceVu0UnitMatrix(m0);
    sceVu0RotMatrixY(m1, m0, ang);
    /* PC port: callers such as chain.c's climb sway set only x, y and z, so
       w is a stale stack word; the host's sceVu0ApplyMatrix reads it as VU0
       does (port/math/matrix_stack.c ico_apply_matrix) */
    sceVu0ApplyMatrix(v, m1, vec);
    vec[0] = v[0];
    vec[1] = v[1];
    vec[2] = v[2];
}

float _GetDirection(float *dir)
{
    float buf[4];
    buf[1] = 0;
    buf[0] = dir[0];
    buf[2] = dir[2];
    sceVu0Normalize(buf, buf);
    return atan2f(buf[0], buf[2]);
}

inline int _RotGV(float *a, float *b)
{
    float buf[4];
    float buf2[4];
    buf[0] = a[0];
    buf[1] = a[1];
    buf[2] = a[2];
    buf2[0] = b[0];
    buf2[1] = b[1];
    buf2[2] = b[2];
    sceVu0Normalize(buf, buf);
    sceVu0Normalize(buf2, buf2);
    return GetTableArcCos(sceVu0InnerProduct(buf, buf2)) * 180 / 32768;
}

inline float _RotGVF(float *a, float *b)
{
    return _RotGV(a, b) * 3.1415927f / 180.0f;
}

inline void _OrientXZGV(float *dst, float *a, float *b)
{
    int buf[4];
    sceVu0SubVector(buf, a, b);
    buf[1] = 0;
    sceVu0Normalize(dst, buf);
}

inline void _OrientGV(float *dst, float *a, float *b)
{
    int buf[4];
    sceVu0SubVector(buf, a, b);
    sceVu0Normalize(dst, buf);
}

inline int _FrontGV(float *target, float *pos, float *dir, int deg)
{
    float *p;
    float buf[8];
    int r;
    p = &buf[4];
    sceVu0SubVector(p, target, pos);
    p = &buf[0];
    buf[5] = 0.0f;
    sceVu0Normalize(p, &buf[4]);
    r = _RotyGV(p, dir);
    return __builtin_abs(r) < deg;
}

inline void SwapGV(float *a, float *b)
{
    float tmp[3];
    tmp[0] = a[0];
    tmp[1] = a[1];
    tmp[2] = a[2];
    a[0] = b[0];
    a[1] = b[1];
    a[2] = b[2];
    b[0] = tmp[0];
    b[1] = tmp[1];
    b[2] = tmp[2];
}

inline float GetCorrectDistance(int deg, float dist)
{
    float r = GetTableCos((short)((deg << 15) / 180));
    if (r == 0.0f)
        return 3.40282347e+38f; /* FLT_MAX */
    return dist / r;
}

inline int RoundDegGV(int deg)
{
    if (deg > 0) {
        deg = deg % 360;
    } else {
        int a = deg < 0 ? -deg : deg;
        deg = (a / 360 + 1) * 360 + deg;
    }
    return (deg < 181) ? deg : deg - 360;
}

inline int AlignDegGV(int deg)
{
    if (deg < -135)
        deg = 180;
    else if (deg < -45)
        deg = -90;
    else if (deg < 45)
        deg = 0;
    else {
        int v = deg;
        deg = 180;
        if (v <= 134)
            deg = 90;
    }
    return deg;
}
