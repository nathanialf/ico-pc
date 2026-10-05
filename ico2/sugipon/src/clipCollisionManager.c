#include "ee_view.h"
#include "gobj_process.h"
#include "clipCollisionManager.h"
#include "GobjProc.h"
#include "debug.h"
#include "matrixDrive.h"
#include "Matrix.h"
#include "main.h"
#include "act.h"

/* the manager object CreateClipCollisionManagerGObj made */
static GObj *clipCollisionManagerGObj = 0; /* derived name */

/* `self` is volatile: the thread yields in _ACTWait below, and the entry
   argument is read back from its stack home after each resume. */
static void actClipCollisionCore(volatile unsigned ICO_WORD self)
{
    ClipColReq *w = ICO_RAW(ClipColReq *, self, 0x20, (ClipColReq *)((GProc *)self)->arg);
    float a[4];
    float b[4];
    float step;
    int n;
    int i;
    int clung;

    n = (int)(GetPointDistance(w->clip.pt[0], w->clip.pt[1]) * 0.01f) + 1;
    step = 1.0f / (float)n;
    i = 0;
    w->clip.floor.elem = 0;
    w->clip.wall.elem = 0;
    if ((60 - systemStatus[0] * 10) / systemStatus[1] * 5 < n) {
        /* an enemy in flight cast a RAY that costs too many frames, so it was
           dropped */
        debug_StdPrintfDummy(
            "飛び中の敵があまりにフレーム数のかかるRAYを飛ばしたので無効にしました.\n");
        w->done = 1;
        return;
    }
    while (1) {
        clung = 0;
        if (w->obj != 0) {
            clung = GOBJ_SUB(w->obj)->disp;
        }
        CopyVector(a, w->clip.pt[0]);
        CopyVector(b, w->clip.pt[1]);
        _InterVectorXYZ(w->clip.pt[0], b, a, (float)i * step);
        _InterVectorXYZ(w->clip.pt[1], b, a, (float)(i + 1) * step);
        if (clung != 0) {
            GOBJ_SUB(w->obj)->disp = 0;
        }
        w->func(&w->clip);
        if (clung != 0) {
            GOBJ_SUB(w->obj)->disp = 1;
        }
        CopyVector(w->clip.pt[0], a);
        CopyVector(w->clip.pt[1], b);
        if (w->clip.floor.elem != 0 || w->clip.wall.elem != 0) {
            break;
        }
        i++;
        if (i >= n) {
            break;
        }
        _ACTWait(1);
    }
    w->done = 1;
}

inline void *RequestClipCollision(ClipColReq *req)
{
    void *t = actCreateSubThreadGOppArg(actClipCollisionCore, 21);
    ICO_RAW(ClipColReq *, t, 0x20, ((GProc *)t)->arg) = req;
    req->done = 0;
    return t;
}

/* the manager thread's idle body, handed to CreateGObjByFuncSet;
   waySystemManager.c has its own thStart */
static inline void thStart(void)
{
    for (;;) {
        _ACTWait(1);
    }
}

GObj *CreateClipCollisionManagerGObj(void)
{
    GObj *v = CreateGObjByFuncSet(0, 0, 0, 0, thStart, 0, 0);
    clipCollisionManagerGObj = v;
    return v;
}
