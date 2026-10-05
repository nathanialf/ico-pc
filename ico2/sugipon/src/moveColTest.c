#include "debug.h"
#include "memory.h"
#include "script.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "moveColTest.h"
#include <stdlib.h>
#include "ios.h"
#include "main.h"
#include "DisplayP2O.h"

inline short *InitMoveColTestGeo(int gobj, int *layout)
{
    short *r = iosMallocDebug(ios_partition_sugipon, 12, "src/moveColTest.c", 28);
    *(int *)r = layout[12];
    r[2] = (short)rand();
    r[3] = (short)rand();
    r[4] = (short)rand();
    r[5] = 0;
    return r;
}

/* the 12-byte work block InitMoveColTestGeo allocates, hung at sub+0x830 */
typedef struct MctWork { /* field names derived */
    int obj;             /* 0x00 */
    short r1;            /* 0x04 */
    short r2;            /* 0x06 */
    short r3;            /* 0x08 */
    short angle;         /* 0x0A */
} MctWork;               /* derived name */

/* the blink counter and the test offset */
static unsigned char blinkCount = 0; /* derived name */

static int testOffset = 0; /* derived name */

void MoveColTestGeo(GObj *self)
{
    float pos[4];
    MctWork *w = GOBJ_SUB(self)->work;
    int c;

    CopyMatrix(MatrixDrive_GetMatrix(),
               ICO_RAWP(char *, GOBJ_SUB(self), 0x20, (char *)GOBJ_SUB(self)->matrix));
    MatrixDrive_RotMatrixZ(w->angle);
    CopyQuaternion(GOBJ_SUB(self)->root.quat,
                   ICO_RAWP(char *, GOBJ_SUB(self), 0x60, (char *)GOBJ_SUB(self)->quat));
    RotQuaternionZ(GOBJ_SUB(self)->root.quat, w->angle);
    CopyMatrix((void *)GOBJ_SUB(self)->nodeMtx, MatrixDrive_GetMatrix());
    UpdateRootMatrix(self);

    if (pad[1].now & 4) {
        if (pad[1].ana[1] >= 0x81) {
            c = pad[1].ana[1];
            if (c - 0x80 >= 0x15) {
                w->angle += (c - 0x94) * 3;
            }
        } else {
            c = pad[1].ana[1];
            if (c - 0x80 < -0x14) {
                w->angle += (c - 0x6C) * 3;
            }
        }
    }
    if ((blinkCount++ >> 5) & 1) {
        debug_PrintfDummy(10, 60, 0x4080FF00, "PUSH R3 TO BORN SPIDER.");
    }

    GetRootPosition(pos, boyGObj);
    pos[1] += -500.0f;
    if (pad[0].flags & 0x400) {
        scpBornSpider(0xA, pos[0], pos[1], pos[2], 300.0f);
        testOffset += 0xA;
        debug_StdPrintfDummy("%d\n", testOffset);
    }
    if (pad[0].flags & 0x200) {
        scpBornSpider(1, pos[0], pos[1], pos[2], 300.0f);
        testOffset += 1;
        debug_StdPrintfDummy("%d\n", testOffset);
    }
}

void MoveColTestDL(GObj *self)
{
    p2o_DispVU1(self);
}
