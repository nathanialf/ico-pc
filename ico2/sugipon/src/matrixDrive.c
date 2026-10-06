#include "typedef.h"
#include "quaternion.h"
#include "tableSin.h"
#include <libvu0.h>
#include "matrixDrive.h"

/* a matrix as UnitRotation keeps it: its translation row moved as one
   128-bit quadword around the unit fill */
typedef ICO_QW Qw128; /* derived name */

typedef struct { /* field names derived */
    char pad[48];
    Qw128 q; /* 0x30, the translation row */
} MatDrive;  /* derived name */

/* the current depth of the matrix stack below */
static int matrixStackIndex = 0; /* derived name */

/* the 64-deep matrix stack; MatrixDrive_GetLastMatrix reads one slot below
   the current one */
static float matrixStack[64][4][4]; /* derived name */

void InitMatrixDrive(void)
{
    matrixStackIndex = 0;
    sceVu0UnitMatrix(matrixStack);
    InitTableSin();
    InitQuaternionDrive();
}

void MatrixDrive_PushMatrix(void)
{
    matrixStackIndex += 1;
    CopyMatrix(matrixStack[matrixStackIndex], matrixStack[matrixStackIndex - 1]);
}

/* The six leading objects are the engine-wide constant vectors and the
   identity matrix, which other files use by name.  The four matrices that
   follow are this file's own scratch templates: each rotate/scale entry point
   writes its varying terms into one of them and multiplies it through. */
float ZeroVector[4] = {0.0f, 0.0f, 0.0f, 0.0f};

float ZeroPoint[4] = {0.0f, 0.0f, 0.0f, 1.0f};

float XUnitVector[4] = {1.0f, 0.0f, 0.0f, 0.0f};

float YUnitVector[4] = {0.0f, 1.0f, 0.0f, 0.0f};

float ZUnitVector[4] = {0.0f, 0.0f, 1.0f, 0.0f};

float InitialMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

/* the scratch matrix MatrixDrive_RotMatrixX fills in and multiplies through */
static float rotXWorkMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

void MatrixDrive_RotMatrixX(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    rotXWorkMatrix[10] = c;
    rotXWorkMatrix[9] = -s;
    rotXWorkMatrix[6] = s;
    rotXWorkMatrix[5] = c;
    sceVu0MulMatrix(matrixStack[matrixStackIndex], matrixStack[matrixStackIndex], rotXWorkMatrix);
}

/* the scratch matrix MatrixDrive_RotMatrixY fills in and multiplies through */
static float rotYWorkMatrix[16] = {1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                                   0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

void MatrixDrive_RotMatrixY(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    rotYWorkMatrix[10] = c;
    rotYWorkMatrix[8] = s;
    rotYWorkMatrix[2] = -s;
    rotYWorkMatrix[0] = c;
    sceVu0MulMatrix(matrixStack[matrixStackIndex], matrixStack[matrixStackIndex], rotYWorkMatrix);
}

/* the scratch matrix MatrixDrive_RotMatrixZ fills in and multiplies through */
static float rotZWorkMatrix[16] = {1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

void MatrixDrive_RotMatrixZ(short angle)
{
    float c = GetTableCos(angle);
    float s = GetTableSin(angle);
    rotZWorkMatrix[5] = c;
    rotZWorkMatrix[4] = -s;
    rotZWorkMatrix[1] = s;
    rotZWorkMatrix[0] = c;
    sceVu0MulMatrix(matrixStack[matrixStackIndex], matrixStack[matrixStackIndex], rotZWorkMatrix);
}

/* the scratch matrix MatrixDrive_ScaleMatrix fills in and multiplies through */
static float scaleWorkMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

void MatrixDrive_ScaleMatrix(float x, float y, float z)
{
    scaleWorkMatrix[0] = x;
    scaleWorkMatrix[5] = y;
    scaleWorkMatrix[10] = z;
    sceVu0MulMatrix(matrixStack[matrixStackIndex], matrixStack[matrixStackIndex], scaleWorkMatrix);
}

void MatrixDrive_TurnViewMatrix(float x, float y, float z)
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {x, 0.0f, z, 1.0f};

    sceVu0Normalize(v0, v0);
    sceVu0Normalize(v1, v1);
    {
        float c = v1[2];
        float s = v1[0];
        float m[4][4] = {{c, 0.0f, -s, 0.0f},
                         {0.0f, 1.0f, 0.0f, 0.0f},
                         {s, 0.0f, c, 0.0f},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
        sceVu0MulMatrix(matrixStack[matrixStackIndex], m, matrixStack[matrixStackIndex]);
    }
    {
        float len = FSqrt(v0[0] * v0[0] + v0[2] * v0[2]);
        float t = v0[1];
        float m[4][4] = {{1.0f, 0.0f, 0.0f, 0.0f},
                         {0.0f, len, t, 0.0f},
                         {0.0f, -t, len, 0.0f},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
        sceVu0MulMatrix(matrixStack[matrixStackIndex], m, matrixStack[matrixStackIndex]);
    }
}

void MatrixDrive_PushMatrixWithNoCopy(void)
{
    matrixStackIndex += 1;
}

void MatrixDrive_PopMatrix(void)
{
    matrixStackIndex -= 1;
}

float (*MatrixDrive_GetMatrix(void))[4]
{
    return matrixStack[matrixStackIndex];
}

float (*MatrixDrive_GetLastMatrix(void))[4]
{
    return matrixStack[matrixStackIndex - 1];
}

void MatrixDrive_TransMatrixV(void *v)
{
    float buf[4];
    sceVu0ApplyMatrix(buf, matrixStack[matrixStackIndex], v);
    buf[3] = 1.0f;
    CopyVector(matrixStack[matrixStackIndex][3], buf);
}

void MatrixDrive_TransMatrix(float x, float y, float z)
{
    float v[4];
    float buf[4];
    float *m = buf;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 1.0f;
    sceVu0ApplyMatrix((int *)m, matrixStack[matrixStackIndex], v);
    m[3] = 1.0f;
    CopyVector(matrixStack[matrixStackIndex][3], m);
}

/* the body of MatrixDrive_GetTurnZAngleYX, which the next function inlines and
   MatrixDrive_GetTurnZAngleYX calls */
static inline void GetTurnZAngleYX_i(short *ay, short *ax, float x, float y,
                                     float z) /* derived name */
{
    float v0[4] = {x, y, -z, 1.0f};
    float v1[4] = {x, 0.0f, -z, 1.0f};
    float len;
    float p;
    float q;
    float yy;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(x * x + z * z)) {
        sceVu0Normalize(v1, v1);
        p = v1[2];
        q = v1[0];
        *ay = GetTableArcTan2(-q, -p);
    }
    len = FSqrt(v0[0] * v0[0] + v0[2] * v0[2]);
    yy = v0[1];
    *ax = -GetTableArcTan2(yy, len);
}

void MatrixDrive_TurnObjectMatrix(float x, float y, float z)
{
    short ay;
    short ax;

    GetTurnZAngleYX_i(&ay, &ax, x, y, z);
    MatrixDrive_RotMatrixY(ay);
    MatrixDrive_RotMatrixX(ax);
}

/* the body of MatrixDrive_GetTurnXAngleZY, which the next function inlines and
   MatrixDrive_GetTurnXAngleZY calls */
static inline void GetTurnXAngleZY_i(short *az, short *ay, float x, float y,
                                     float z) /* derived name */
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {x, y, 0.0f, 1.0f};
    float len;
    float zz;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(x * x + y * y)) {
        sceVu0Normalize(v1, v1);
        *az = GetTableArcTan2(v1[1], v1[0]);
    }
    len = FSqrt(v0[0] * v0[0] + v0[1] * v0[1]);
    zz = v0[2];
    *ay = -GetTableArcTan2(zz, len);
}

void MatrixDrive_TurnXObjectMatrixZY(float x, float y, float z)
{
    short az;
    short ay;

    GetTurnXAngleZY_i(&az, &ay, x, y, z);
    MatrixDrive_RotMatrixZ(az);
    MatrixDrive_RotMatrixY(ay);
}

/* the body of MatrixDrive_GetTurnXAngleYZ, which the next function inlines and
   MatrixDrive_GetTurnXAngleYZ calls */
static inline void GetTurnXAngleYZ_i(short *ay, short *az, float x, float y,
                                     float z) /* derived name */
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {x, 0.0f, z, 1.0f};
    float len;
    float t;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(x * x + z * z)) {
        sceVu0Normalize(v1, v1);
        *ay = -GetTableArcTan2(v1[2], v1[0]);
    }
    len = FSqrt(v0[0] * v0[0] + v0[2] * v0[2]);
    t = v0[1];
    *az = GetTableArcTan2(t, len);
}

void MatrixDrive_TurnXObjectMatrixYZ(float x, float y, float z)
{
    short ay;
    short az;

    GetTurnXAngleYZ_i(&ay, &az, x, y, z);
    MatrixDrive_RotMatrixY(ay);
    MatrixDrive_RotMatrixZ(az);
}

/* the body of MatrixDrive_GetTurnYAngleXZ, which the next function inlines and
   MatrixDrive_GetTurnYAngleXZ calls */
static inline void GetTurnYAngleXZ_i(short *ax, short *az, float x, float y,
                                     float z) /* derived name */
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {0.0f, y, z, 1.0f};
    float len;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(y * y + z * z)) {
        sceVu0Normalize(v1, v1);
        *ax = -GetTableArcTan2(v1[2], v1[1]);
    }
    len = FSqrt(v0[1] * v0[1] + v0[2] * v0[2]);
    *az = GetTableArcTan2(v0[0], len);
}

void MatrixDrive_TurnYObjectMatrixXZ(float x, float y, float z)
{
    short ax;
    short az;

    GetTurnYAngleXZ_i(&ax, &az, x, y, z);
    MatrixDrive_RotMatrixX(ax);
    MatrixDrive_RotMatrixZ(az);
}

/* the body of MatrixDrive_GetTurnZAngleXY, which the next function inlines and
   MatrixDrive_GetTurnZAngleXY calls */
static inline void GetTurnZAngleXY_i(short *ax, short *ay, float x, float y,
                                     float z) /* derived name */
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {0.0f, y, z, 1.0f};
    float len;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(y * y + z * z)) {
        sceVu0Normalize(v1, v1);
        *ax = -GetTableArcTan2(v1[1], v1[2]);
    }
    len = FSqrt(v0[1] * v0[1] + v0[2] * v0[2]);
    *ay = GetTableArcTan2(v0[0], len);
}

void MatrixDrive_TurnZObjectMatrixXY(float x, float y, float z)
{
    short ax;
    short ay;

    GetTurnZAngleXY_i(&ax, &ay, x, y, z);
    MatrixDrive_RotMatrixX(ax);
    MatrixDrive_RotMatrixY(ay);
}

void MatrixDrive_GetTurnXAngleZY(short *az, short *ay, float x, float y, float z)
{
    GetTurnXAngleZY_i(az, ay, x, y, z);
}

void MatrixDrive_GetTurnXAngleYZ(short *ay, short *az, float x, float y, float z)
{
    GetTurnXAngleYZ_i(ay, az, x, y, z);
}

void MatrixDrive_GetTurnYAngleXZ(short *ax, short *az, float x, float y, float z)
{
    GetTurnYAngleXZ_i(ax, az, x, y, z);
}

/* no caller; void, as sugipon's other output-parameter getters */
void MatrixDrive_GetTurnYEAngleXZ(float *ex, float *ez, float x, float y, float z)
{
    float v0[4];
    float v1[4];
    float t;

    v0[0] = x;
    v0[1] = y;
    v0[2] = z;
    v0[3] = 1.0f;
    v1[0] = 0.0f;
    v1[1] = y;
    v1[2] = z;
    v1[3] = 1.0f;
    sceVu0Normalize(v0, v0);
    t = FSqrt(y * y + z * z);
    if (0.01f < t) {
        sceVu0Normalize(v1, v1);
        ex[0] = v1[1];
        ex[1] = v1[2];
    } else {
        ex[0] = 1.0f;
        ex[1] = 0.0f;
    }
    ez[0] = FSqrt(v0[1] * v0[1] + v0[2] * v0[2]);
    ez[1] = v0[0];
}

void MatrixDrive_GetTurnZAngleXY(short *ax, short *ay, float x, float y, float z)
{
    GetTurnZAngleXY_i(ax, ay, x, y, z);
}

void MatrixDrive_GetTurnZAngleYX(short *ay, short *ax, float x, float y, float z)
{
    GetTurnZAngleYX_i(ay, ax, x, y, z);
}

void MatrixDrive_GetTurnMinusZAngleXY(short *ax, short *ay, float x, float y, float z)
{
    float v0[4] = {x, y, z, 1.0f};
    float v1[4] = {0.0f, y, z, 1.0f};
    float len;
    float p;
    float q;

    sceVu0Normalize(v0, v0);
    if (0.01f < FSqrt(y * y + z * z)) {
        sceVu0Normalize(v1, v1);
        p = v1[2];
        q = v1[1];
        *ax = GetTableArcTan2(-q, -p);
    }
    len = FSqrt(v0[1] * v0[1] + v0[2] * v0[2]);
    *ay = GetTableArcTan2(v0[0], len);
}

typedef struct { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} __attribute__((aligned(16))) MdVec; /* derived name */

void MatrixDrive_SetTransposeMatrix(void *dstMtx, void *srcMtx)
{
    float *dst = dstMtx;
    float *src = srcMtx;
    MdVec v = {-src[12], -src[13], -src[14], 0.0f};

    sceVu0TransposeMatrix(dst, src);
    dst[3] = dst[7] = dst[11] = 0.0f;
    sceVu0ApplyMatrix(&dst[12], dst, &v);
    dst[15] = 1.0f;
}
