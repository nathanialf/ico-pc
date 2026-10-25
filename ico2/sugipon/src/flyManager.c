#include "fieldCollision.h"
#include "matrixDrive.h"
#include "main.h"

/* the flying object the manager tracks */
static GObj *flyGObj = 0; /* derived name */

#include "flyManager.h"
#include "debug.h"
#include <string.h>

static inline int getFloorLimitValue(int attr) /* derived name */
{
    int type = attr & 0xF;

    if (type == 1) {
        return 0;
    }
    if (!ICO_NTSCAUG6JUN26) {
        if (type == 15 && (stage_no == 19 || stage_no == 28)) {
            type = 20;
        }
    }
    return type * 100;
}

static inline void *clipFlyFloor(ClipWork *work, void *pos) /* derived name */
{
    CopyVector(work->pt[0], pos);
    CopyVector(work->pt[1], pos);
    work->pt[1][1] += 100000.0f;
    ClipFloorByGObj(work, flyGObj);
    return work->floor.elem;
}

inline int InitFlyInfo(GObj *self)
{
    Sub15C *d = self->dobj;
    flyGObj = self;
    d->disp = 0;
    return 0;
}

void DispFlyInfo(void)
{
    int test = debug_fly_limit_test;
    GObj *g = flyGObj;
    if (test == 0) {
        return;
    }
    if (g == 0) {
        return;
    }
    DrawGObjFloorCollision(g, 0);
}

inline void InitFlyManager(void)
{
    flyGObj = 0;
}

inline int GetFlyLimitClearance(void *pos)
{
    ClipWork work;

    if (flyGObj != 0) {
        memset(&work, 0, sizeof(work));
        if (clipFlyFloor(&work, pos) != 0) {
            return -getFloorLimitValue(work.attr);
        }
    }
    return -10000;
}

inline int GetFlyLimitHeight(FlyLimitInfo *info, void *pos)
{
    ClipWork work;

    if (flyGObj != 0) {
        memset(&work, 0, sizeof(work));
        if (clipFlyFloor(&work, pos) != 0) {
            info->floorY = work.pt[2][1];
            info->limitOfs = -getFloorLimitValue(work.attr);
            info->limitY = info->floorY + info->limitOfs;
            info->flags = 0;
            return 1;
        }
    }
    return 0;
}
