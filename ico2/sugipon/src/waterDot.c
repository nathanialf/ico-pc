#include "typedef.h"
#include "sugiCommon.h"
#include "memory.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "waterDot.h"
#include "main.h"
#include "GifPacket.h"
#include "Matrix.h"
#include "ios.h"
#include "windField.h"

/* the registered emitters; InitializeWaterDot clears all five */
static WaterDotWork *waterDots[5]; /* derived name */

/* how many water dots are registered */
static int waterDotCount = 0; /* derived name */

inline void InitializeWaterDot(void)
{
    int i;

    waterDotCount = 0;
    for (i = 4; i >= 0; i--) {
        waterDots[i] = 0;
    }
}

/* the three templates AllocWaterDot copies into a new work block */
static WaterDotWork initWaterDotWork = {0, 0, 0, 0, 0, 0, 0}; /* derived name */

static WaterDot initWaterDot = {
    0, 0, 128, 1.0f, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}; /* derived name */

static WaterDot initWaterDot2 = {
    0, 0, 0, 0.0f, {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}; /* derived name */

WaterDotWork *AllocWaterDot(GObj *gobj, int num, int num2)
{
    WaterDotWork *w;
    int i;

    w = iosMallocDebug(ios_partition_sugipon, sizeof(WaterDotWork), "src/waterDot.c", 29);
    *w = initWaterDotWork;

    w->num = num;
    w->dot = iosMallocDebug(ios_partition_sugipon, num * sizeof(WaterDot), "src/waterDot.c", 33);
    for (i = 0; i < num; i++)
        w->dot[i] = initWaterDot;

    w->num2 = num2;
    w->dot2 = iosMallocDebug(ios_partition_sugipon, num2 * sizeof(WaterDot), "src/waterDot.c", 38);
    for (i = 0; i < num2; i++)
        w->dot2[i] = initWaterDot2;

    w->gobj = gobj;

    waterDots[waterDotCount] = w;
    waterDotCount++;

    return w;
}

void setWaterDot(WaterDot *dot, VECTOR *pos, VECTOR *vel)
{
    int n;

    dot->used = 1;
    dot->frame = 0;
    n = (int)(crt_random_unit() * 64.0f + 32.0f);
    dot->life = n;
    dot->scale = ico_d2f(ico_dsub(ICO_D(1.0), ico_dmul(ico_i2d(256 - n), ICO_D(0.00078125))));
    CopyVector(&dot->pos, pos);
    CopyVector(&dot->vel, vel);
}

inline void EntryWaterDot(WaterDotWork *w, void *pos, void *vel, float range)
{
    VECTOR v = {random_signed_b() * range, random_signed_b() * range, random_signed_b() * range,
                1.0f};

    _AddVectorXYZ(&v, &v, pos);
    setWaterDot(&w->dot[w->cur], &v, vel);
    if (++w->cur == w->num)
        w->cur = 0;
}

/* age one dot and report whether it has expired */
static inline int stepWaterDot(WaterDot *p) /* derived name */
{
    if (p->frame++ < 30) {
        /* the gravity step, the frame-rate quotient written out twice as
           clothAnimation.c writes the same term */
        p->vel.y += 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f *
                    (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        _ScaleVectorXYZ(&p->vel, &p->vel, p->scale);
        _AddVectorXYZ(&p->pos, &p->pos, &p->vel);

        return 0;
    }
    return 1;
}

void ExecWaterDot(WaterDotWork *w)
{
    VECTOR wind;
    VECTOR pos;
    WaterDot *p;
    WaterDot *q;
    int i;

    GetRootPosition(&pos, w->gobj);
    _ScaleVectorXYZ(&wind, GetWindVector(0, &pos.x), 0.2f);

    p = w->dot;
    for (i = 0; i < w->num; i++) {
        if (p->used != 0) {
            if (stepWaterDot(p) != 0) {
                p->used = 0;
            }
        }
        p++;
    }

    q = w->dot2;
    for (i = 0; i < w->num2; i++) {
        if (q->used != 0) {
            q->used = 0;
        }
        q++;
    }
}

/* the PRIM register value the splash packet draws with */
static int waterDotPrim = 0x1C0; /* derived name */

/* project one dot into GS fixed-point screen space */
static inline void getWaterDotScreenPos(int *out, VECTOR *pos) /* derived name */
{
    VECTOR v;
    float q;

    _ApplyMatrix(&v, matrixptr + 0x100, pos);
    q = v.w;
    _ScaleVector(&v, &v, 1.0f / q);
    _FTOI4Vector(out, &v);
}

void DispWaterDot(WaterDotWork *w)
{
    int ip[4];
    WaterDot *p;
    int i;

    gif_StartPacketPri(11);

    p = w->dot;
    gif_SetGsReg(0, waterDotPrim);
    gif_SetZTest(1);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 5, 128);

    for (i = 0; i < w->num; i++, p++) {
        if (p->used != 0) {
            getWaterDotScreenPos(ip, &p->pos);

            if (ip[0] >= 26368 && ip[0] <= 39168) {
                if (ip[1] >= 29568 && ip[1] <= 35968) {
                    gif_SetGsReg(1, 0x80LL | (0x80LL << 8) | (0x80LL << 16) |
                                        ((long long)p->life << 24) | (0x3F800000LL << 32));
                    gif_SetGsReg(5, (long long)ip[0] | ((long long)ip[1] << 16) |
                                        ((long long)ip[2] << 32));
                }
            }
        }
    }

    gif_EndPacket();

    for (i = 0; i < w->num2; i++) {}
}
