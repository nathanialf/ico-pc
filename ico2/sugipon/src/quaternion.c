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
    float *m = mtx;

    ico_quaternion_rotation_rows((float (*)[4])mtx, (const float *)q);
    CopyVector(m + 12, ZeroPoint);
}

/* the file's `nxt` permutation table */
static int nxt[3] = {1, 2, 0}; /* derived name */

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

/* no caller; void as sugipon's output-parameter getters */
void GetQuaternionFromMatrix(void *q, void *mtx)
{
    char local[64];

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
    _ScaleVector(q, q, 1.0f / _Sqrt(ico_quaternion_norm2((const float *)q)));
}

/* short (float), as tableSin.h and the definition: the int the decompiled
   declaration had read the whole return register, whose upper half the
   callee leaves unspecified (the x86-64 clang build returned 16383 as
   0x13FFF, which this slerp then multiplied; issue 19) */
extern short GetTableArcCos(float c);
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

inline void DivQuaternion(void *self, void *qa, void *qb)
{
    float buf[4];
    GetInverseQuaternion(buf, qb);
    /* the call goes through a cast of MultiQuaternion's declaration, whose
       int parameters would truncate 64-bit pointers on the host */
    MultiQuaternion(self, buf, qa);
}

inline void GetMatrixFromQuaternionPos(void *mtx, void *q, void *pos)
{
    float *m = mtx;

    ico_quaternion_rotation_rows((float (*)[4])mtx, (const float *)q);
    CopyVector(m + 12, pos);
    m[15] = 1.0f;
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
    MultiQuaternion(self, self, buf);
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
    MultiQuaternion(self, self, buf);
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
    MultiQuaternion(self, self, buf);
}

inline void RotQuaternionEAX(void *self, float *in)
{
    float q[4];
    SetQuaternionByAxisRotateEAngle(q, in, 1.0f, 0.0f, 0.0f);
    MultiQuaternion(self, self, q);
}

inline void RotQuaternionEAZ(void *self, float *in)
{
    float q[4];
    SetQuaternionByAxisRotateEAngle(q, in, 0.0f, 0.0f, 1.0f);
    MultiQuaternion(self, self, q);
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
