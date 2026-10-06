#include "typedef.h"
#include "tableSin.h"
#include "debug_exception.h"
#include <assert.h>

/* the scratch matrix _ScaleMatrixV fills in and multiplies through */
static float scaleWorkMatrix[4][4] = {{1.0f, 0.0f, 0.0f, 0.0f},
                                      {0.0f, 1.0f, 0.0f, 0.0f},
                                      {0.0f, 0.0f, 1.0f, 0.0f},
                                      {0.0f, 0.0f, 0.0f, 1.0f}}; /* derived name */

#include "Matrix.h"

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
    /* vf1-vf31 hold only the current matrix on the host (docs/port/MATH.md) */
    ico_vu0_registers_push();

    if (++vu0PushDepth >= 6) {
        debug_assert("src/Matrix.c", 1063);
        __assert("src/Matrix.c", 1063, "0");
    }
}

void _PopVu0Registers(void)
{
    ico_vu0_registers_pop();

    if (--vu0PushDepth < 0) {
        debug_assert("src/Matrix.c", 1119);
        __assert("src/Matrix.c", 1119, "0");
    }
}

inline void _ScaleMatrixV(void *dst, void *src, void *v)
{
    float *m = &scaleWorkMatrix[0][0];

    m[0] = ((float *)v)[0];
    m[5] = ((float *)v)[1];
    m[10] = ((float *)v)[2];
    _MulMatrix(dst, src, m);
}

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
