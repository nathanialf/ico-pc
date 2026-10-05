#include "sugiCommon.h"
#include "memory.h"
#include "fieldCollision.h"
#include "lineManager.h"
#include "stormTest.h"
#include <string.h>
#include <libvu0.h>
#include "typedef.h"
#include "ios.h"
#include "main.h"
#include "matrixDrive.h"
#include "GifPacket.h"
#include "windField.h"

/* the clip plane normal StormTestDL transforms into view space */
static float stormClipPlane[4] = {0.0f, 0.0f, 1.0f, 0.0f}; /* derived name */

typedef struct StormPackage { /* field names derived */
    /* 0x00 */ int mode;
    /* 0x04 */ int num;
    /* 0x08 */ float (*pos)[4];
    /* 0x0C */ float (*vel)[4];
    /* 0x10 */ int (*disp)[4];
    /* 0x14 */ float *rate;
    char pad18[4];
} StormPackage; /* derived name */

static __inline__ void StormStoreI4(void *dst, void *src) /* derived name */
{
#ifdef ICO_HOST
    _FTOI4Vector(dst, src);
#else
    VU0_LSV_R(lqc2, 4, 0x0, src);
    VU0_V2OP(vftoi4.xyzw, 5, 4);
    VU0_LSV_R(sqc2, 5, 0x0, dst);
#endif
}

static __inline__ void StormPerspective(void *dst, void *src) /* derived name */
{
    float t[4];

    sceVu0ApplyMatrix(t, matrixptr + 0xC0, src);
    sceVu0ScaleVector(t, t, 1.0f / t[3]);
    StormStoreI4(dst, t);
}

static __inline__ float StormScreen(void *dst, void *src) /* derived name */
{
    float t[4];
    float w;

    sceVu0ApplyMatrix(t, matrixptr + 0x100, src);
    w = 1.0f / t[3];
    sceVu0ScaleVector(t, t, w);
    StormStoreI4(dst, t);
    return w;
}

static __inline__ void StormProject(void *dst, void *src) /* derived name */
{
    float t[4];

    sceVu0ApplyMatrix(t, matrixptr + 0x80, src);
    StormPerspective(dst, t);
}

StormPackage *InitStormPackage(int mode, int num, int flag)
{
    StormPackage *pkg;
    float t[4];
    int i;

    pkg = (StormPackage *)iosMallocDebug(ios_partition_sugipon, sizeof(StormPackage),
                                         "src/stormTest.c", 67);
    pkg->mode = mode;
    pkg->num = num;
    pkg->pos = (float (*)[4])iosMallocDebug(ios_partition_sugipon, num * 16, "src/stormTest.c", 71);
    pkg->vel = (float (*)[4])iosMallocDebug(ios_partition_sugipon, num * 16, "src/stormTest.c", 72);
    pkg->disp = (int (*)[4])iosMallocDebug(ios_partition_sugipon, num * 16, "src/stormTest.c", 73);
    pkg->rate = (float *)iosMallocDebug(ios_partition_sugipon, num * 4, "src/stormTest.c", 74);

    for (i = 0; i < num; i++) {
        CopyVector(pkg->vel[i], ZeroVector);
        pkg->pos[i][0] = (_GetRandom() * 2.0f - 1.0f) * 1000.0f;
        pkg->pos[i][1] = -(_GetRandom() * 400.0f);
        pkg->pos[i][2] = (_GetRandom() * 2.0f - 1.0f) * 1000.0f;
        pkg->pos[i][3] = 1.0f;
        pkg->rate[i] = 0.18181819f;
        sceVu0ApplyMatrix(t, matrixptr + 0x80, pkg->pos[i]);
        StormPerspective(pkg->disp[i], t);
    }

    if (flag) {
        for (i = 0; i < num; i++) {
            pkg->rate[i] = 0.1f / (_GetRandom() * 0.9f + 0.1f);
        }
    } else {
        for (i = 0; i < num; i++) {
            pkg->rate[i] = 0.1f / (_GetRandom() * 0.5f + 0.5f);
        }
    }
    return pkg;
}

void ClipStormByVolume(StormPackage *pkg)
{
    int i;
    int clipped;

    for (i = 0; i < pkg->num; i++) {
        float *p = pkg->pos[i];

        clipped = 0;
        if (0.0f < p[1]) {
            p[1] -= 400.0f;
            clipped = 1;
        }
        if (p[1] < -400.0f) {
            p[1] += 400.0f;
            clipped = 1;
        }
        if (p[0] > 1500.0f) {
            p[0] -= 2000.0f;
            clipped = 1;
        }
        if (p[0] < -500.0f) {
            p[0] += 2000.0f;
            clipped = 1;
        }
        if (1000.0f < p[2]) {
            p[2] -= 2000.0f;
            clipped = 1;
        }
        if (p[2] < -1000.0f) {
            p[2] += 2000.0f;
            clipped = 1;
        }
        if (clipped) {
            StormProject(pkg->disp[i], p);
        }
    }
}

void ClipStormByCamera(StormPackage *pkg)
{
    float cam[4];
    float d[4];
    int i;
    int j;
    int clipped;

    MatrixDrive_SetTransposeMatrix(MatrixDrive_GetMatrix(), matrixptr + 0x80);
    MatrixDrive_TransMatrix(0.0f, 0.0f, 500.0f);
    CopyVector(cam, MatrixDrive_GetMatrix()[3]);
    for (i = 0; i < pkg->num; i++) {
        float *p = pkg->pos[i];

        clipped = 0;
        sceVu0SubVector(d, p, cam);
        for (j = 0; j < 3; j++) {
            if (500.0f < d[j]) {
                float t = d[j] - 500.0f;
                clipped = 1;
                p[j] = (cam[j] - 500.0f) + t;
            } else if (d[j] < -500.0f) {
                p[j] = (cam[j] + 500.0f) + (d[j] + 500.0f);
                clipped = 1;
            }
        }
        if (clipped) {
            StormProject(pkg->disp[i], p);
        }
    }
}

void UpdateStormPackage(StormPackage *pkg)
{
    Vec4 v;
    Vec4 t;
    Vec4 w;
    int i;

    t.f[0] = _GetRandom() * 2.0f - 1.0f;
    t.f[1] = _GetRandom() * 2.0f - 1.0f;
    t.f[2] = _GetRandom() * 2.0f - 1.0f;
    t.f[3] = 0.0f;
    v = t;

    for (i = 0; i < pkg->num; i++) {
        float rate = pkg->rate[i];
        float *pos = pkg->pos[i];
        float *vel = pkg->vel[i];
        void *wind;

        v.f[i & 3] = _GetRandom() * 2.0f - 1.0f;
        wind = GetWindVector(0, pos);
        sceVu0ScaleVectorXYZ(&w, &v, rate);
        sceVu0ScaleVectorXYZ(&t, wind, 0.1f);
        AddVectorXYZ(vel, vel, &t);
        AddVectorXYZ(vel, vel, &w);
        sceVu0ScaleVectorXYZ(vel, vel, 1.0f - rate * 0.1f);
        AddVectorXYZ(pos, pos, vel);
    }

    switch (pkg->mode) {
    case 0:
    default:
        ClipStormByVolume(pkg);
        break;
    case 1:
        ClipStormByCamera(pkg);
        break;
    }
}

void DispStormPackage(StormPackage *pkg, void *color)
{
    int sv[4];
    int iv[4];
    float col[4] = {0.0f, 0.0f, 0.0f, 128.0f};
    float plane[4];
    int i;

    MatrixDrive_SetTransposeMatrix(MatrixDrive_GetMatrix(), matrixptr + 0x80);
    sceVu0ApplyMatrix(plane, MatrixDrive_GetMatrix(), stormClipPlane);
    plane[3] = -sceVu0InnerProduct(plane, MatrixDrive_GetMatrix()[3]);
    gif_StartPacketPri(11);
    gif_SetZTest(1);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 5, 128);
    Draw2DLineSeg_Start();
    for (i = 0; i < pkg->num; i++) {
        int *d = pkg->disp[i];

        if (GetDistanceFromPlane(plane, pkg->pos[i]) < 0.0f) {
            d[2] = -1;
        } else {
            float w = StormScreen(sv, pkg->pos[i]);

            if (d[2] >= 0 && (sv[0] >= 26368 && sv[0] <= 39168) &&
                (sv[1] >= 29568 && sv[1] <= 35968) && (d[0] >= 26368 && d[0] <= 39168) &&
                (d[1] >= 29568 && d[1] <= 35968)) {
                float dx;
                float dy;

                d[0] += 32;
                dx = (float)(sv[0] - d[0]);
                dy = (float)(sv[1] - d[1]);
                sceVu0ScaleVectorXYZ(col, color,
                                     80.0f / (FSqrt(dx * dx + dy * dy) + 30.0f) * w * 300.0f);
                sceVu0ClampVector(col, col, 0.0f, 255.0f);
                sceVu0FTOI0Vector(iv, col);
                Draw2DLineSeg_Loop(sv, d, iv);
            }
            CopyIVector(d, sv);
        }
    }
    gif_SetZWrite(1);
    gif_EndPacket();
}

inline StormTestWork *InitStormTestGeo(GObj *self, SObjSimpleSetting *lay)
{
    StormTestWork *obj =
        iosMallocDebug(ios_partition_sugipon, sizeof(StormTestWork), "src/stormTest.c", 283);
    int v = lay->obj;
    int flag = 1;
    obj->num = v;
    if (!(0.0f < lay->pos[0]))
        flag = 0;
    obj->pkg = InitStormPackage(1, v, flag);
    obj->color[0] = lay->scale[0];
    obj->color[1] = lay->scale[1];
    obj->color[2] = lay->scale[2];
    obj->color[3] = 128.0f;
    return obj;
}

void StormTestGeo(GObj *self)
{
    StormTestWork *p = GOBJ_SUB(self)->work;
    UpdateStormPackage(p->pkg);
}

void StormTestDL(GObj *self)
{
    StormTestWork *p = GOBJ_SUB(self)->work;
    DispStormPackage(p->pkg, p->color);
}
