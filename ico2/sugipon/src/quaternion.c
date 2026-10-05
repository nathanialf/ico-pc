#include "typedef.h"
#include "debug.h"
#include "quaternion.h"
#include "matrixDrive.h"
#include "Matrix.h"

/* the current depth of the quaternion stack below */
static int quatStackIndex = -1; /* derived name */

/* The 64-deep quaternion stack; GetLastQuaternion reads the slot below the
   current one. */
static float quatStack[64][4]; /* derived name */

void MultiCurrentQuaternion(void *src)
{
    float *q = quatStack[quatStackIndex];
    MultiQuaternion(q, q, src);
}

void InvertCurrentQuaternion(void)
{
    float *p = quatStack[quatStackIndex];
    GetInverseQuaternion(p, p);
}

void SetCurrentQuaternion(float *q)
{
    CopyQuaternion(quatStack[quatStackIndex], q);
}

void RotCurrentQuaternionX(short ang)
{
    RotQuaternionX(quatStack[quatStackIndex], ang);
}

void RotCurrentQuaternionY(short ang)
{
    RotQuaternionY(quatStack[quatStackIndex], ang);
}

void RotCurrentQuaternionZ(short ang)
{
    RotQuaternionZ(quatStack[quatStackIndex], ang);
}

void PushQuaternion(void)
{
    int v = quatStackIndex;
    if (v < 0) {
        debug_StdPrintfDummy("Quaternion stack not initialized.\n");
        InitQuaternionDrive();
        v = quatStackIndex;
    }
    v++;
    quatStackIndex = v;
    if (v >= 0x40) {
        debug_StdPrintfDummy("Quaternion stack overflow!!\n");
        v = 0x3F;
        quatStackIndex = v;
    }
    {
        int idx = *(volatile int *)&quatStackIndex;
        CopyQuaternion(quatStack[idx], quatStack[idx - 1]);
    }
}

void InitQuaternionDrive(void)
{
    quatStackIndex = 0;
    SetIdentityQuaternion(quatStack);
}

float IdentityQuaternion[4] = {0.0f, 0.0f, 0.0f, 1.0f};

void SetIdentityQuaternion(void *q)
{
    CopyQuaternion(q, IdentityQuaternion);
}

/* the {1, 1, 1, sqrt(2)} multiplier GetMatrixFromQuaternion feeds $vf12 */
static float quatToMatrixScale[4] = {1.0f, 1.0f, 1.0f, 1.41421356f}; /* derived name */

void GetMatrixFromQuaternion(void *mtx, void *q)
{
#ifdef ICO_HOST
    float *m = mtx;

    ico_quaternion_rotation_rows((float (*)[4])mtx, (const float *)q);
    CopyVector(m + 12, ZeroPoint);
#else
    float *m = mtx;

    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0($5)\n"
                         "lqc2 $vf12, 0x0(%0)\n"
                         "vmr32.w $vf14, $vf0\n"
                         "vmr32.w $vf15, $vf0\n"
                         "vmr32.w $vf16, $vf0\n"
                         "vopmula.xyz ACC, $vf11, $vf12\n"
                         "vmadd.xyz $vf11, $vf0, $vf0\n"
                         "vmulw.xyzw $vf11, $vf11, $vf12w\n"
                         "vmul.xyz $vf13, $vf11, $vf11\n"
                         "vopmula.xyz ACC, $vf11, $vf11\n"
                         "vmaddw.xyz $vf15, $vf11, $vf11w\n"
                         "vmsubw.xyz $vf16, $vf11, $vf11w\n"
                         "vopmula.xyz ACC, $vf13, $vf12\n"
                         "vmadd.xyz $vf17, $vf13, $vf12\n"
                         "vopmula.xyz ACC, $vf15, $vf12\n"
                         "vmadd.xyz $vf15, $vf0, $vf0\n"
                         "vsub.xyz $vf14, $vf12, $vf17\n"
                         "vmove.y $vf17, $vf14\n"
                         "vmove.y $vf14, $vf16\n"
                         "vmove.y $vf16, $vf15\n"
                         "vmove.y $vf15, $vf17\n"
                         "vmove.z $vf17, $vf14\n"
                         "vmove.z $vf14, $vf15\n"
                         "vmove.z $vf15, $vf16\n"
                         "vmove.z $vf16, $vf17\n"
                         "sqc2 $vf14, 0x0($4)\n"
                         "sqc2 $vf15, 0x10($4)\n"
                         "sqc2 $vf16, 0x20($4)\n"
                         ".set reorder\n"
                         :
                         : "r"(quatToMatrixScale)
                         : "memory");
    CopyVector(m + 12, ZeroPoint);
#endif
}

/* the file's `nxt` permutation table */
static int nxt[3] = {1, 2, 0}; /* derived name */

#ifdef ICO_HOST

/* GetQuaternionFromMatrix's helper, a GNU nested function in the original
   (it captured nothing), at file scope so clang compiles it */
static void getQuaternionFromMatrix(float *q, float (*m)[4])
{
    float tr;
    float s;
    float t;
    int i;
    int j;
    int k;

    tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        s = _Sqrt(tr + 1.0f);
        q[3] = s * 0.5f;
        t = 0.5f / s;
        q[0] = (m[1][2] - m[2][1]) * t;
        q[1] = (m[2][0] - m[0][2]) * t;
        q[2] = (m[0][1] - m[1][0]) * t;
    } else {
        i = 0;
        if (m[1][1] > m[0][0]) {
            i = 1;
        }
        if (m[2][2] > m[i][i]) {
            i = 2;
        }
        j = nxt[i];
        k = nxt[j];
        s = _Sqrt(m[i][i] - (m[j][j] + m[k][k]) + 1.0f);
        q[i] = s * 0.5f;
        t = (s != 0.0f) ? 0.5f / s : 0.0f;
        q[3] = (m[j][k] - m[k][j]) * t;
        q[j] = (m[i][j] + m[j][i]) * t;
        q[k] = (m[i][k] + m[k][i]) * t;
    }
}

#endif

/* no caller; void as sugipon's output-parameter getters */
void GetQuaternionFromMatrix(void *q, void *mtx)
{
#ifdef ICO_HOST
    char local[64];
#else
    auto void getQuaternionFromMatrix(float *q, float (*m)[4]);
    char local[64];

    void getQuaternionFromMatrix(float *q, float (*m)[4])
    {
        float tr;
        float s;
        float t;
        int i;
        int j;
        int k;

        tr = m[0][0] + m[1][1] + m[2][2];
        if (tr > 0.0f) {
            s = _Sqrt(tr + 1.0f);
            q[3] = s * 0.5f;
            t = 0.5f / s;
            q[0] = (m[1][2] - m[2][1]) * t;
            q[1] = (m[2][0] - m[0][2]) * t;
            q[2] = (m[0][1] - m[1][0]) * t;
        } else {
            i = 0;
            if (m[1][1] > m[0][0]) {
                i = 1;
            }
            if (m[2][2] > m[i][i]) {
                i = 2;
            }
            j = nxt[i];
            k = nxt[j];
            s = _Sqrt(m[i][i] - (m[j][j] + m[k][k]) + 1.0f);
            q[i] = s * 0.5f;
            t = (s != 0.0f) ? 0.5f / s : 0.0f;
            q[3] = (m[j][k] - m[k][j]) * t;
            q[j] = (m[i][j] + m[j][i]) * t;
            q[k] = (m[i][k] + m[k][i]) * t;
        }
    }
#endif

    _TransposeMatrix(local, mtx);
    getQuaternionFromMatrix((float *)q, (float (*)[4])local);
}

void CopyQuaternion(void *dst, void *src)
{
    CopyVector(dst, src);
}

void GetInverseQuaternion(void *dst, void *src)
{
    CopyQuaternion(dst, src);
    _ScaleVectorXYZ(dst, src, -1.0f);
}

void RegularizeQuaternion(void *q)
{
#ifdef ICO_HOST
    _ScaleVector(q, q, 1.0f / _Sqrt(ico_quaternion_norm2((const float *)q)));
#else
    float d;
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf14, 0x0(%1)\n"
                         "lqc2 $vf15, 0x0(%1)\n"
                         "vmul.xyzw $vf15, $vf14, $vf15\n"
                         "vaddy.x $vf15, $vf15, $vf15y\n"
                         "vaddz.x $vf15, $vf15, $vf15z\n"
                         "vaddw.x $vf15, $vf15, $vf15w\n"
                         "qmfc2.ni $2, $vf15\n"
                         "mtc1 $2, %0\n"
                         ".set reorder\n"
                         : "=f"(d)
                         : "r"(q)
                         : "$2");
    _ScaleVector(q, q, 1.0f / _Sqrt(d));
#endif
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline float GetQuaternionCosRadian(void *qa, void *qb)
{
    float r;
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf14, 0x0(%1)\n"
                         "lqc2 $vf15, 0x0(%2)\n"
                         "vmul.xyzw $vf15, $vf14, $vf15\n"
                         "vaddy.x $vf15, $vf15, $vf15y\n"
                         "vaddz.x $vf15, $vf15, $vf15z\n"
                         "vaddw.x $vf15, $vf15, $vf15w\n"
                         "qmfc2.ni $2, $vf15\n"
                         "mtc1 $2, %0\n"
                         ".set reorder\n"
                         : "=f"(r)
                         : "r"(qa), "r"(qb)
                         : "$2");
    return r;
}

#endif /* ICO_HOST: port/math */

/* int (float) here, short (float) in tableSin.h */
extern int GetTableArcCos(float c);
/* float (int) here, float (short) in tableSin.h */
extern float GetTableSin(int x);

void GetSlerpQuaternionNoRegularize(void *out, void *qa, void *qb, float t)
{
    float tmp[4];
    float tq[4];
    float c;
    int ang;
    float s;
    float inv;
    float sa, sb;

    CopyQuaternion(tmp, qb);
    c = GetQuaternionCosRadian(qa, tmp);
    if (c < 0.0f) {
        _ScaleVector(tmp, tmp, -1.0f);
        c = GetQuaternionCosRadian(qa, tmp);
    }
    ang = GetTableArcCos(c);
    s = GetTableSin(ang);
    if (s < 0.05f) {
        _InterVector(out, qa, tmp, t);
        return;
    }
    inv = 1.0f / s;
    sa = GetTableSin((short)((float)ang * t)) * inv;
    sb = GetTableSin((short)((float)ang * (1.0f - t))) * inv;
    _ScaleVector(out, qa, sa);
    _ScaleVector(tq, tmp, sb);
    _AddVector(out, out, tq);
}

/* The slerp, then the result put back to unit length. */
void GetSlerpQuaternion(void *out, void *qa, void *qb, float t)
{
    GetSlerpQuaternionNoRegularize(out, qa, qb, t);
    RegularizeQuaternion(out);
}

inline float *GetCurrentQuaternion(void)
{
    return quatStack[quatStackIndex];
}

inline float *GetLastQuaternion(void)
{
    return quatStack[quatStackIndex - 1];
}

inline void PushQuaternionWithNoCopy(void)
{
    int v = quatStackIndex;
    if (v < 0) {
        debug_StdPrintfDummy("Quaternion stack not initialized.\n");
        InitQuaternionDrive();
        v = quatStackIndex;
    }
    v++;
    quatStackIndex = v;
    if (v >= 0x40) {
        debug_StdPrintfDummy("Quaternion stack overflow!!\n");
        v = 0x3F;
        quatStackIndex = v;
    }
}

inline void PopQuaternion(void)
{
    quatStackIndex -= 1;
    if (quatStackIndex < 0) {
        debug_StdPrintfDummy("Quaternion stack underflow!!\n");
        quatStackIndex = 0;
    }
}

/* float (int) here, float (short) in tableSin.h */
extern float GetTableCos(int x);

inline void SetQuaternionByAxisRotateVWithNoRegularize(float *self, short ang, float *src)
{
    int half = ang >> 1;
    float f;
    f = GetTableSin(half);
    _ScaleVector(self, src, f);
    self[3] = GetTableCos(half);
}

inline void SetQuaternionByAxisRotateV(float *self, short ang, float *src)
{
    float buf[4];
    _NormalizeVector(buf, src);
    SetQuaternionByAxisRotateVWithNoRegularize(self, ang, buf);
}

inline void SetQuaternionByAxisRotate(float *self, short ang, float x, float y, float z)
{
    float v[4] = {x, y, z, 0.0f};
    SetQuaternionByAxisRotateV(self, ang, v);
}

inline void SetQuaternionByAxisRotateWithNoRegularize(float *self, short ang, float x, float y,
                                                      float z)
{
    char buf[16];
    int half = (ang << 16) >> 17;
    float f;
    *(float *)(buf + 0) = x;
    *(float *)(buf + 4) = y;
    *(float *)(buf + 8) = z;
    *(int *)(buf + 0xC) = 0;
    f = GetTableSin(half);
    _ScaleVector(self, buf, f);
    self[3] = GetTableCos(half);
}

inline void SetQuaternionByAxisRotateVEAngle(void *out, float *cosAngle, void *axis)
{
    float buf[4];
    float first, second;
    first = _Sqrt((cosAngle[0] + 1.0f) * 0.5f);
    second = _Sqrt((1.0f - cosAngle[0]) * 0.5f);
    _NormalizeVector(buf, axis);
    *(float *)((char *)out + 0xC) = first;
    *(float *)((char *)out + 0x0) = buf[0] * second;
    *(float *)((char *)out + 0x4) = buf[1] * second;
    *(float *)((char *)out + 0x8) = buf[2] * second;
}

inline void SetQuaternionByAxisRotateEAngle(float *out, float *in, float x, float y, float z)
{
    float v[4] = {x, y, z, 0.0f};
    SetQuaternionByAxisRotateVEAngle(out, in, v);
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline void MultiQuaternion(void *out, void *qa, void *qb)
{
    VU0_LSV(lqc2, 11, 0x0, 5);
    VU0_LSV(lqc2, 12, 0x0, 6);
    VU0_V3OP(vmul.xyzw, 13, 11, 12);
    VU0_V3OP_BC(vaddy.x, 13, 13, 13, y);
    VU0_V3OP_BC(vaddz.x, 13, 13, 13, z);
    VU0_V3OP_BC(vsubx.w, 13, 13, 13, x);
    VU0_V3OP_BC(vmulw.xyz, 14, 12, 11, w);
    VU0_V3OP_BC(vmulw.xyz, 15, 11, 12, w);
    VU0_V3OP_ACC(vopmula.xyz, 12, 11);
    VU0_V3OP(vopmsub.xyz, 16, 11, 12);
    VU0_V3OP(vadd.xyz, 13, 14, 15);
    VU0_V3OP(vadd.xyz, 13, 13, 16);
    VU0_LSV(sqc2, 13, 0x0, 4);
}

#endif /* ICO_HOST: port/math */

inline void DivQuaternion(void *self, void *qa, void *qb)
{
    float buf[4];
    GetInverseQuaternion(buf, qb);
    /* the call goes through a cast of MultiQuaternion's declaration, whose
       int parameters would truncate 64-bit pointers on the host */
#ifdef ICO_HOST
    MultiQuaternion(self, buf, qa);
#else
    ((void (*)(int, int, int))MultiQuaternion)(self, buf, qa);
#endif
}

#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline void GetMatrixFromQuaternionRotElem(void *mtx, void *q)
{
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0($5)\n"
                         "lqc2 $vf12, 0x0(%0)\n"
                         "vmr32.w $vf14, $vf0\n"
                         "vmr32.w $vf15, $vf0\n"
                         "vmr32.w $vf16, $vf0\n"
                         "vopmula.xyz ACC, $vf11, $vf12\n"
                         "vmadd.xyz $vf11, $vf0, $vf0\n"
                         "vmulw.xyzw $vf11, $vf11, $vf12w\n"
                         "vmul.xyz $vf13, $vf11, $vf11\n"
                         "vopmula.xyz ACC, $vf11, $vf11\n"
                         "vmaddw.xyz $vf15, $vf11, $vf11w\n"
                         "vmsubw.xyz $vf16, $vf11, $vf11w\n"
                         "vopmula.xyz ACC, $vf13, $vf12\n"
                         "vmadd.xyz $vf17, $vf13, $vf12\n"
                         "vopmula.xyz ACC, $vf15, $vf12\n"
                         "vmadd.xyz $vf15, $vf0, $vf0\n"
                         "vsub.xyz $vf14, $vf12, $vf17\n"
                         "vmove.y $vf17, $vf14\n"
                         "vmove.y $vf14, $vf16\n"
                         "vmove.y $vf16, $vf15\n"
                         "vmove.y $vf15, $vf17\n"
                         "vmove.z $vf17, $vf14\n"
                         "vmove.z $vf14, $vf15\n"
                         "vmove.z $vf15, $vf16\n"
                         "vmove.z $vf16, $vf17\n"
                         "sqc2 $vf14, 0x0($4)\n"
                         "sqc2 $vf15, 0x10($4)\n"
                         "sqc2 $vf16, 0x20($4)\n"
                         ".set reorder\n"
                         :
                         : "r"(quatToMatrixScale)
                         : "memory");
}

#endif /* ICO_HOST: port/math */

inline void GetMatrixFromQuaternionPos(void *mtx, void *q, void *pos)
{
#ifdef ICO_HOST
    float *m = mtx;

    ico_quaternion_rotation_rows((float (*)[4])mtx, (const float *)q);
    CopyVector(m + 12, pos);
    m[15] = 1.0f;
#else
    float *m = mtx;

    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%2)\n"
                         "lqc2 $vf12, 0x0(%0)\n"
                         "vmr32.w $vf14, $vf0\n"
                         "vmr32.w $vf15, $vf0\n"
                         "vmr32.w $vf16, $vf0\n"
                         "vopmula.xyz ACC, $vf11, $vf12\n"
                         "vmadd.xyz $vf11, $vf0, $vf0\n"
                         "vmulw.xyzw $vf11, $vf11, $vf12w\n"
                         "vmul.xyz $vf13, $vf11, $vf11\n"
                         "vopmula.xyz ACC, $vf11, $vf11\n"
                         "vmaddw.xyz $vf15, $vf11, $vf11w\n"
                         "vmsubw.xyz $vf16, $vf11, $vf11w\n"
                         "vopmula.xyz ACC, $vf13, $vf12\n"
                         "vmadd.xyz $vf17, $vf13, $vf12\n"
                         "vopmula.xyz ACC, $vf15, $vf12\n"
                         "vmadd.xyz $vf15, $vf0, $vf0\n"
                         "vsub.xyz $vf14, $vf12, $vf17\n"
                         "vmove.y $vf17, $vf14\n"
                         "vmove.y $vf14, $vf16\n"
                         "vmove.y $vf16, $vf15\n"
                         "vmove.y $vf15, $vf17\n"
                         "vmove.z $vf17, $vf14\n"
                         "vmove.z $vf14, $vf15\n"
                         "vmove.z $vf15, $vf16\n"
                         "vmove.z $vf16, $vf17\n"
                         "sqc2 $vf14, 0x0(%1)\n"
                         "sqc2 $vf15, 0x10(%1)\n"
                         "sqc2 $vf16, 0x20(%1)\n"
                         ".set reorder\n"
                         :
                         : "r"(quatToMatrixScale), "r"(m), "r"(q)
                         : "memory");
    CopyVector(m + 12, pos);
    m[15] = 1.0f;
#endif
}

inline void MultiMatrixByQuaternion(void *src)
{
    float local[16];
    void *r1, *r2;
    GetMatrixFromQuaternion(local, src);
    r1 = MatrixDrive_GetMatrix();
    r2 = MatrixDrive_GetMatrix();
    _MulMatrix(r1, r2, local);
}

inline void GetMirrorQuaternion(float *dst, float *src, int mode)
{
    CopyQuaternion(dst, src);
    switch (mode) {
    case 0:
        dst[0] = -dst[0];
        break;
    case 1:
        dst[1] = -dst[1];
        break;
    case 2:
        dst[2] = -dst[2];
        break;
    case 4:
        dst[0] = -dst[0];
        dst[1] = -dst[1];
        break;
    case 3:
        dst[0] = -dst[0];
        dst[2] = -dst[2];
        break;
    case 5:
        dst[1] = -dst[1];
        dst[2] = -dst[2];
        break;
    case 6:
    default:
        dst[0] = -dst[0];
        dst[1] = -dst[1];
        dst[2] = -dst[2];
        break;
    }
}

inline void RotQuaternionX(void *self, short ang)
{
    char buf[16];
    int half = (-(ang << 16)) >> 17;
    float *axis = XUnitVector;
    float f;
    f = GetTableSin(half);
    _ScaleVector((int *)buf, axis, f);
    *(float *)(buf + 0xC) = GetTableCos(half);
#ifdef ICO_HOST
    MultiQuaternion(self, self, buf);
#else
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%0)\n"
                         "lqc2 $vf12, %1\n"
                         "vmul.xyzw $vf13, $vf11, $vf12\n"
                         "vaddy.x $vf13, $vf13, $vf13y\n"
                         "vaddz.x $vf13, $vf13, $vf13z\n"
                         "vsubx.w $vf13, $vf13, $vf13x\n"
                         "vmulw.xyz $vf14, $vf12, $vf11w\n"
                         "vmulw.xyz $vf15, $vf11, $vf12w\n"
                         "vopmula.xyz ACC, $vf12, $vf11\n"
                         "vopmsub.xyz $vf16, $vf11, $vf12\n"
                         "vadd.xyz $vf13, $vf14, $vf15\n"
                         "vadd.xyz $vf13, $vf13, $vf16\n"
                         "sqc2 $vf13, 0x0(%0)\n"
                         ".set reorder\n"
                         :
                         : "r"(self), "m"(buf[0])
                         : "memory");
#endif
}

inline void RotQuaternionY(void *self, short ang)
{
    char buf[16];
    int half = (-(ang << 16)) >> 17;
    float *axis = YUnitVector;
    float f;
    f = GetTableSin(half);
    _ScaleVector((int *)buf, axis, f);
    *(float *)(buf + 0xC) = GetTableCos(half);
#ifdef ICO_HOST
    MultiQuaternion(self, self, buf);
#else
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%0)\n"
                         "lqc2 $vf12, %1\n"
                         "vmul.xyzw $vf13, $vf11, $vf12\n"
                         "vaddy.x $vf13, $vf13, $vf13y\n"
                         "vaddz.x $vf13, $vf13, $vf13z\n"
                         "vsubx.w $vf13, $vf13, $vf13x\n"
                         "vmulw.xyz $vf14, $vf12, $vf11w\n"
                         "vmulw.xyz $vf15, $vf11, $vf12w\n"
                         "vopmula.xyz ACC, $vf12, $vf11\n"
                         "vopmsub.xyz $vf16, $vf11, $vf12\n"
                         "vadd.xyz $vf13, $vf14, $vf15\n"
                         "vadd.xyz $vf13, $vf13, $vf16\n"
                         "sqc2 $vf13, 0x0(%0)\n"
                         ".set reorder\n"
                         :
                         : "r"(self), "m"(buf[0])
                         : "memory");
#endif
}

inline void RotQuaternionZ(void *self, short ang)
{
    char buf[16];
    int half = (-(ang << 16)) >> 17;
    float *axis = ZUnitVector;
    float f;
    f = GetTableSin(half);
    _ScaleVector((int *)buf, axis, f);
    *(float *)(buf + 0xC) = GetTableCos(half);
#ifdef ICO_HOST
    MultiQuaternion(self, self, buf);
#else
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%0)\n"
                         "lqc2 $vf12, %1\n"
                         "vmul.xyzw $vf13, $vf11, $vf12\n"
                         "vaddy.x $vf13, $vf13, $vf13y\n"
                         "vaddz.x $vf13, $vf13, $vf13z\n"
                         "vsubx.w $vf13, $vf13, $vf13x\n"
                         "vmulw.xyz $vf14, $vf12, $vf11w\n"
                         "vmulw.xyz $vf15, $vf11, $vf12w\n"
                         "vopmula.xyz ACC, $vf12, $vf11\n"
                         "vopmsub.xyz $vf16, $vf11, $vf12\n"
                         "vadd.xyz $vf13, $vf14, $vf15\n"
                         "vadd.xyz $vf13, $vf13, $vf16\n"
                         "sqc2 $vf13, 0x0(%0)\n"
                         ".set reorder\n"
                         :
                         : "r"(self), "m"(buf[0])
                         : "memory");
#endif
}

inline void RotQuaternionEAX(void *self, float *in)
{
    float q[4];
    SetQuaternionByAxisRotateEAngle(q, in, 1.0f, 0.0f, 0.0f);
#ifdef ICO_HOST
    MultiQuaternion(self, self, q);
#else
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%0)\n"
                         "lqc2 $vf12, %1\n"
                         "vmul.xyzw $vf13, $vf11, $vf12\n"
                         "vaddy.x $vf13, $vf13, $vf13y\n"
                         "vaddz.x $vf13, $vf13, $vf13z\n"
                         "vsubx.w $vf13, $vf13, $vf13x\n"
                         "vmulw.xyz $vf14, $vf12, $vf11w\n"
                         "vmulw.xyz $vf15, $vf11, $vf12w\n"
                         "vopmula.xyz ACC, $vf12, $vf11\n"
                         "vopmsub.xyz $vf16, $vf11, $vf12\n"
                         "vadd.xyz $vf13, $vf14, $vf15\n"
                         "vadd.xyz $vf13, $vf13, $vf16\n"
                         "sqc2 $vf13, 0x0(%0)\n"
                         ".set reorder\n"
                         :
                         : "r"(self), "m"(q[0])
                         : "memory");
#endif
}

inline void RotQuaternionEAZ(void *self, float *in)
{
    float q[4];
    SetQuaternionByAxisRotateEAngle(q, in, 0.0f, 0.0f, 1.0f);
#ifdef ICO_HOST
    MultiQuaternion(self, self, q);
#else
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf11, 0x0(%0)\n"
                         "lqc2 $vf12, %1\n"
                         "vmul.xyzw $vf13, $vf11, $vf12\n"
                         "vaddy.x $vf13, $vf13, $vf13y\n"
                         "vaddz.x $vf13, $vf13, $vf13z\n"
                         "vsubx.w $vf13, $vf13, $vf13x\n"
                         "vmulw.xyz $vf14, $vf12, $vf11w\n"
                         "vmulw.xyz $vf15, $vf11, $vf12w\n"
                         "vopmula.xyz ACC, $vf12, $vf11\n"
                         "vopmsub.xyz $vf16, $vf11, $vf12\n"
                         "vadd.xyz $vf13, $vf14, $vf15\n"
                         "vadd.xyz $vf13, $vf13, $vf16\n"
                         "sqc2 $vf13, 0x0(%0)\n"
                         ".set reorder\n"
                         :
                         : "r"(self), "m"(q[0])
                         : "memory");
#endif
}

inline void GetXUnitVectorOfQuaternion(float *out, float *q)
{
    float w = q[3];
    float x = q[0];
    float y = q[1];
    float z = q[2];
    float v[4] = {-(y * y + z * z), x * y - w * z, x * z + w * y, 0.0f};
    _ScaleVectorXYZ(out, v, 2.0f);
    out[0] = out[0] + 1.0f;
}

inline void GetYUnitVectorOfQuaternion(float *out, float *q)
{
    float w = q[3];
    float x = q[0];
    float y = q[1];
    float z = q[2];
    float v[4] = {x * y + w * z, -(x * x + z * z), y * z - w * x, 0.0f};
    _ScaleVectorXYZ(out, v, 2.0f);
    out[1] = out[1] + 1.0f;
}

inline void GetZUnitVectorOfQuaternion(float *out, float *q)
{
    float w = q[3];
    float x = q[0];
    float y = q[1];
    float z = q[2];
    float v[4] = {x * z - w * y, y * z + w * x, -(x * x + y * y), 0.0f};
    _ScaleVectorXYZ(out, v, 2.0f);
    out[2] = out[2] + 1.0f;
}

inline void SetQuaternionByCosineAxisRotateVWithNoRegularize(void *out, void *axis, float angle)
{
    float first, second;
    first = _Sqrt((angle + 1.0f) * 0.5f);
    second = _Sqrt((1.0f - angle) * 0.5f);
    _ScaleVector(out, axis, second);
    *(float *)((char *)out + 0xC) = first;
}

inline void SetQuaternionByCosineAxisRotateV(void *out, void *axis, float angle)
{
    float buf[4];
    _NormalizeVector(buf, axis);
    SetQuaternionByCosineAxisRotateVWithNoRegularize(out, buf, angle);
}

inline void GetDifferencialQuaternionWithNoRegularize(void *out, void *a, void *b)
{
    float v[4];
    float c;
    _OuterProduct(v, a, b);
    c = _InnerProduct(a, b);
    SetQuaternionByCosineAxisRotateV(out, v, c);
}

/* no caller; float as sugipon's scalar getters */
#ifndef ICO_HOST /* the host build has these in port/math (docs/port/MATH.md) */

inline float GetQuaternionMagnitude(void *q)
{
    float r;
    __asm__ __volatile__(".set noreorder\n"
                         "lqc2 $vf14, 0x0(%1)\n"
                         "lqc2 $vf15, 0x0(%1)\n"
                         "vmul.xyzw $vf15, $vf14, $vf15\n"
                         "vaddy.x $vf15, $vf15, $vf15y\n"
                         "vaddz.x $vf15, $vf15, $vf15z\n"
                         "vaddw.x $vf15, $vf15, $vf15w\n"
                         "qmfc2.ni %0, $vf15\n"
                         ".set reorder\n"
                         : "=r"(r)
                         : "r"(q));
    return _Sqrt(r);
}

#endif /* ICO_HOST: port/math */
