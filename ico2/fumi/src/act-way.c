#include "main.h"
#include "camera-editor.h"
#include "motionManager2.h"
#include "act-game.h"
#include "act-way.h"
#include "act.h"
#include "gobj_process.h"
#include "way_sys.h"
#include "waySystemManager.h"
#include <libvu0.h>
#include "debug.h"
#include "girl_act.h"
#include "ee_view.h"
#include "box.h"
#include "commonact.h"
#include "gv.h"
#include "motionOrientManager.h"

/* the two-word playback-rate pair the wait counters are scaled by */

/* the three detour angles DetourCheck sweeps, in degrees, zero-terminated */
static int detourAngle[4] = {75, -75, 0, 0}; /* derived name */

static void DetourCheck(GObj *self, float *out)
{
    float orient[4];
    float cur[4];
    float o2[4];
    float tmp[4];
    float dir[4];
    Act *act = GOBJ_ACT(self);
    int i;
    int wait = 0;
    int ok = 0;

    if (debug_girl_detour_flag == 0) {
        return;
    }
    GetSkeltonOrient(orient, self, 0x2C);
    if (GOBJ_WORK(self)->wayHold != 0) {
        out[0] = GOBJ_WORK(self)->wayDirX;
        out[1] = GOBJ_WORK(self)->wayDirY;
        out[2] = GOBJ_WORK(self)->wayDirZ;
        GOBJ_WORK(self)->wayHold -= 1;
        return;
    }
    /* two identical case bodies */
    switch (act->actMode) {
    case 2:
        ok = 1;
        wait = (60 - systemStatus[0] * 10) / systemStatus[1] * 40 / 60;
        break;
    case 3:
        ok = 1;
        wait = (60 - systemStatus[0] * 10) / systemStatus[1] * 40 / 60;
        break;
    }
    /* the four tests are one statement */
    if (ok == 0 || act->stick.mag == 0.0f || (GOBJ_SUB(self)->ctrl.flags & 2) == 0 ||
        ((*(unsigned long long *)&GOBJ_WORK(self)->stopFrames >> 33) & 1) == 0) {
        return;
    }
    cur[0] = test_CURRENTROOT(self)[0];
    cur[1] = test_CURRENTROOT(self)[1];
    cur[2] = test_CURRENTROOT(self)[2];
    GetSkeltonOrient(o2, self, 0x2C);
    for (i = 0; detourAngle[i] != 0; i++) {
        dir[0] = o2[0];
        dir[1] = o2[1];
        dir[2] = o2[2];
        _ApplyRyGV(dir, (float)detourAngle[i] * 3.1415927f / 180.0f);
        sceVu0ScaleVector(tmp, dir, 100.0f);
        sceVu0AddVector(tmp, cur, tmp);
        if (ACTCheckCollis_WAY(10.0f, cur, tmp, 0, 0) == 0) {
            GOBJ_WORK(self)->wayDirX = dir[0];
            GOBJ_WORK(self)->wayDirY = dir[1];
            GOBJ_WORK(self)->wayDirZ = dir[2];
            GOBJ_WORK(self)->wayHold = wait;
            return;
        }
    }
}

/* motionOrientManager.h declares none of the motion tables */
extern MotionDef motionKind[];

static int checkPositionIllegal(GObj *self, float *pos)
{
    float v[4];
    float r[4];
    float dy;
    Act *act = GOBJ_ACT(self);

    if (act->actMode == 0x70) {
        return 1;
    }
    if (act->actMode == 0x26 ||
        (((GOBJ_SUB(self)->ctrl.motion + motionKind)->flags.word >> 12) & 1)) {
        return 1;
    }
    v[0] = pos[0];
    v[1] = pos[1];
    v[2] = pos[2];
    debug_NMarker(v, 0, 255, 0, 100.0f);
    r[0] = test_CURRENTROOT(self)[0];
    r[1] = test_CURRENTROOT(self)[1];
    r[2] = test_CURRENTROOT(self)[2];
    r[1] = r[1] - GetDifferenceFromLastField(self, 0x2C);
    debug_NMarker(r, 0, 0, 255, 100.0f);
    dy = v[1] - r[1];
    if (dy < 0.0f) {
        if (-dy > 40.0f) {
            return 1;
        }
        return 0;
    }
    return dy > 40.0f;
}

inline unsigned char WayMove_CheckCollis(float *p0, float *p1, void *actor, void *posout)
{
    float a[4];
    float b[4];

    a[0] = p0[0];
    a[1] = p0[1];
    a[2] = p0[2];
    b[0] = p1[0];
    b[1] = p1[1];
    b[2] = p1[2];
    a[1] -= 50.0f;
    b[1] -= 50.0f;
    return ACTCheckCollis_WAY(10.0f, a, b, actor, posout);
}

/* three helpers inlined into ACTWayMove_BeginDetail and
   ACTWayMove_NextDetail */

/* the pull-up floor box's saved enable word and the box itself, held across
   the way search */
static int pullupBoxEnable; /* derived name */

static void *pullupBox; /* derived name */

static inline void SuspendGirlPullupFloorBox(void) /* derived name */
{
    pullupBox = FindGirlPullupFloorBoxGObj();
    pullupBoxEnable = pullupBox != 0 ? GOBJ_SUB(pullupBox)->disp : 0;
    if (pullupBoxEnable != 0) {
        GOBJ_SUB(pullupBox)->disp = 0;
    }
}

static inline void ResumeGirlPullupFloorBox(void) /* derived name */
{
    if (pullupBoxEnable != 0) {
        GOBJ_SUB(pullupBox)->disp = 1;
    }
}

static inline WayPoint *RequestWayBegin(char *self, float *goal, float *from, WVTObj *way,
                                        unsigned char sub) /* derived name */
{
    ActWork *req;
    WayRequest *ws;
    WayPoint *w;

    if (sub) {
        req = GOBJ_WORK(self);
        ws = &req->wayReq;
        if (ws->proc != 0) {
            isysGObjProcRemove(ws->proc);
            ws->proc = 0;
        }
        ws->from[0] = from[0];
        ws->from[1] = from[1];
        ws->from[2] = from[2];
        req->wayReq.way = *way;
        ws->goal[0] = goal[0];
        ws->goal[1] = goal[1];
        ws->goal[2] = goal[2];
        ws->proc = RequestGetWayBegin(ws);
        while (ws->done == 0) {
            _ACTWait(1);
        }
        w = ws->result;
        *way = ws->way;
        return w;
    }
    if (self == (char *)girlGObj) {
        SuspendGirlPullupFloorBox();
    }
    w = GetWay_begin(from, way, goal);
    if (self == (char *)girlGObj) {
        ResumeGirlPullupFloorBox();
    }
    return w;
}

int ACTWayMove_BeginDetail(GObj *self, float *goal, float *from, void *tgt, void *e,
                           unsigned char sub)
{
    WVTObj way;
    Act *act = GOBJ_ACT(self);
    WVTObj *home;
    WVTObj *wp;
    int ret = 0;
    WayPoint *w;

    /* disabled in retail: the way-begin-position (WBP) report */
    if (0) {
        debug_StdPrintfDummy("WBP <<begin>>\n");
    }
    home = &act->way;
    way = *home;
    wp = &way;
    act->wayFlags &= ~0x20000;
    act->wayFlags &= ~0x40000;
    act->wayState.flags = (act->wayState.flags & ~0x200000) |
                          ((long long)(checkPositionIllegal(self, goal) & 1) << 21);
    act->wayGoalY = goal[1];
    w = RequestWayBegin(self, goal, from, wp, sub);
    if (w != 0) {
        act->wayState.st[1] = 0;
        if (wp->flag3C == 0) {
            if (WayMove_CheckCollis(goal, from, tgt, e) == 0) {
                act->wayState.st[1] = 1;
                DeleteGuideWay(wp);
                ret = 3;
            } else {
                ret = 2;
            }
        } else {
            ret = 1;
        }
        *home = way;
        act->wayLast = 0;
        act->wayTarget = (void *)tgt;
        act->wayFromX = from[0];
        act->wayFromY = from[1];
        act->wayFromZ = from[2];
        act->wayState.st[0] = 1;
    } else {
        act->wayState.st[0] = 0;
    }
    act->wayState.flags &= ~0x10000;
    act->wayState.flags &= ~0x1E0000;
    act->wayDetailFlag = 0;
    GOBJ_WORK(self)->wayHold = 0;
    if (act->way.chk.start != 0 && act->way.chk.cross == 0) {
        if (_DistxzSqGV(act->way.chk.start->pos, goal) < 40000.0f) {
            act->wayFlags |= 0x40000;
        }
    }
    return ret;
}

/* the 0x20-byte way-step record the actor keeps at act + 0x3E0: the step
   direction, the 64-bit way state word and the two distances to the goal */
typedef struct {     /* field names derived */
    float dir[4];    /* 0x00 */
    long long state; /* 0x10 */
    float dist;      /* 0x18 */
    float dy;        /* 0x1C */
} WayStep;           /* derived name */

/* the two templates act-way.o keeps in .data: the cleared way-walker record
   (its waypoint id starts at -1) and the cleared way-step record (its two
   distances start at the largest float) */
static WVTObj wayWorkClear = {{0}, {0}, {0}, 0,   0,   0, 0,
                              0,   {0}, 0,   {0}, {0}, 0, -1}; /* derived name */

static WayStep wayStepClear = {
    {0.0f, 0.0f, 0.0f, 0.0f}, 0, 3.4028235e38f, 3.4028235e38f}; /* derived name */

#ifdef ICO_HOST

/* PC port: ACTWayMove_NextDetail clears the actor's way step by copying
   wayStepClear over Act from wayNodeX: dir over wayNodeX..pad3EC, state over
   wayFlags, dist over wayGoalDist, dy over wayGoalHeight; the host layouts
   must agree (tools/template_audit.py) */
ICO_LAYOUT_AT_FROM(WayStep, dir, Act, wayNodeX, wayNodeX);

ICO_LAYOUT_AT_FROM(WayStep, state, Act, wayNodeX, wayFlags);

ICO_LAYOUT_AT_FROM(WayStep, dist, Act, wayNodeX, wayGoalDist);

ICO_LAYOUT_AT_FROM(WayStep, dy, Act, wayNodeX, wayGoalHeight);

_Static_assert(sizeof(WayStep) ==
                   __builtin_offsetof(Act, wayLast) - __builtin_offsetof(Act, wayNodeX),
               "WayStep is not Act's wayNodeX to wayLast");

#endif

/* this TU's uses of the gv distance helpers do not fit the void returns gv.h
   carries, and its GetRootProjectionPosOfGObj / IsThisBoxTruck call forms do
   not fit motionManager2.h and box.h */

int ACTWayMove_NextDetail(GObj *self, float *node, float *goal, unsigned char d, unsigned char e)
{
    float pos[4];
    Act *act = GOBJ_ACT(self);
    WVTObj *way;
    int again = 0;
    int w;
    WayPoint *wp;
    int r;
    unsigned char ok;
    float dy;
    int n;

    *(WayStep *)&act->wayNodeX = wayStepClear;
    act->wayNodeX = node[0];
    act->wayNodeY = node[1];
    act->wayNodeZ = node[2];
    if (act->wayState.st[0] == 0) {
        /* EUC-JP, "there is no route" */
        debug_StdPrintfDummy("ルートがありません\n");
    }
    GetRootProjectionPosOfGObj(pos, self);
    dy = pos[1] - act->wayGoalY;
    if (dy < 0.0f ? -dy > 120.0f : dy > 120.0f) {
        /* disabled prints, here and at the two rechecks below */
        if (0) {
            debug_StdPrintfDummy("WBP set [height]\n");
        }
        ACTWay_SetBeginPositionIllegal(self);
    }
    act->wayGoalY = pos[1];
    if (((int)(act->wayState.flags >> 21) & 1) != 0) {
        if (checkPositionIllegal(self, pos) == 0) {
            act->wayState.flags &= ~0x200000;
            DeleteGuideWay(&act->way);
            act->way = wayWorkClear;
            if (0) {
                debug_StdPrintfDummy("WBP recheck first");
            }
            again = 1;
        }
    }
    if (((int)(act->wayState.flags >> 16) & 1) == 0) {
        if (_DistGV(goal, (char *)&act->wayFromX) > 300.0f) {
            if (((int)(act->wayState.flags >> 17) & 0xF) == 0) {
                if (act->actMode != 0x26) {
                    if (0) {
                        debug_StdPrintfDummy("WBP recheck second");
                    }
                    again = 1;
                }
            }
        }
    }
    if (again != 0) {
        w = ACTWayMove_BeginDetail(self, pos, goal, act->wayTarget, 0, d);
        act->wayFlags |= 0x10000;
        if (w == 0) {
            return 0;
        }
    }
    if (act->way.flag6C != 0) {
        if (act->way.chk.start != 0 && act->way.chk.cross == 0 &&
            _DistSqGV(act->way.chk.start->pos, pos) < 10000.0f) {
            float d = pos[1] - act->way.chk.start->pos[1];

            if (d < 0.0f ? -d < 150.0f : d < 150.0f) {
                act->wayFlags |= 0x20000;
            }
        }
    }
    if (act->way.chk.start != 0 && act->way.chk.cross == 0 &&
        _DistxzSqGV(act->way.chk.start->pos, pos) < 40000.0f) {
        act->wayFlags |= 0x40000;
    }
    if (act->actMode == 0x26) {
        float v0[4];
        float v1[4];

        {
            sceVu0CopyVector(v0, act->way.nrm);
            sceVu0CopyVector(v1, (float *)act->env.edgeOrient);
            v0[1] = v1[1] = 0.0f;
            if (sceVu0InnerProduct(v0, v1) > 0.0f) {
                act->stick.y = 255;
            } else {
                act->stick.y = 0;
            }
            act->stick.mag = 1.0f;
            act->wayState.st[1] = 0;
            if (self == girlGObj && goal[1] - act->wayFromY > 150.0f && pos[1] < goal[1]) {
                if (((int)(act->wayState.flags >> 17) & 0xF) == 0) {
                    act->wayDetailFlag = (60 - systemStatus[0] * 10) / systemStatus[1] * 90 / 60;
                }
                act->wayState.flags = (act->wayState.flags & ~0x1E0000) | 0x40000;
                act->wayDetailX = ((float *)act->env.edgeOrient)[0];
                act->wayDetailY = ((float *)act->env.edgeOrient)[1];
                act->wayDetailZ = ((float *)act->env.edgeOrient)[2];
            }
            if ((act->wayDetailFlag)-- > 0) {
                act->stick.y = 128;
            } else if (((int)(act->wayState.flags >> 17) & 0xF) != 0) {
                act->stick.y = 255;
            }
        }
    } else {
        n = (int)(act->wayState.flags >> 17) & 0xF;
        if (n > 0) {
            act->wayState.flags =
                (act->wayState.flags & ~0x1E0000) | ((long long)((n - 1) & 0xF) << 17);
            if (((int)(act->wayState.flags >> 17) & 0xF) == 0) {
                goto restart;
            }
            act->stick.mag = 1.0f;
            act->wayNodeX = act->wayDetailX;
            act->wayNodeY = act->wayDetailY;
            act->wayNodeZ = act->wayDetailZ;
            node[0] = act->wayNodeX;
            node[1] = act->wayNodeY;
            node[2] = act->wayNodeZ;
            return 1;
        }
    }
    switch (act->wayState.st[1]) {
    case 0:
        if (self == girlGObj) {
            SuspendGirlPullupFloorBox();
        }
        *(short *)&act->wayFlags = 1;
        way = &act->way;
        wp = GetWay_next(way, pos);
        if (wp != 0) {
            if (act->wayLast != wp) {
                if (act->wayLast != 0) {
                    if (act->way.guideFirst <= 0) {
                        *(short *)&act->wayFlags = 1;
                    }
                }
                act->wayLast = wp;
            }
        }
        if (self == girlGObj) {
            ResumeGirlPullupFloorBox();
        }
        act->wayNodeX = act->way.nrm[0];
        act->wayNodeY = act->way.nrm[1];
        act->wayNodeZ = act->way.nrm[2];
        if (self == boyGObj || self == girlGObj) {
            DetourCheck(self, (float *)&act->wayNodeX);
        }
        if (act->way.flag3C == 0 && WayMove_CheckCollis(pos, goal, act->wayTarget, 0) == 0 &&
            act->actMode != 0x26) {
            act->wayState.st[1] = 1;
            DeleteGuideWay(way);
            *(short *)&act->wayFlags = 2;
        }
        break;
    case 1: {
        int chk = 1;

        *(short *)&act->wayFlags = 2;
        ok = WayMove_CheckCollis(pos, goal, act->wayTarget, 0);
        if (stage_no != 22) {
            chk = 0;
        }
        if (stage_no == 8 || chk != 0) {
            if (self == girlGObj && wallGObj_ACTCheckCollis_WAY != 0 &&
#ifdef ICO_HOST
                ICO_RAW(int, wallGObj_ACTCheckCollis_WAY, 0xC,
                        ((GObj *)wallGObj_ACTCheckCollis_WAY)->kind) == 17 &&
#else
                *(int *)((char *)wallGObj_ACTCheckCollis_WAY + 0xC) == 17 &&
#endif
                IsThisBoxTruck(wallGObj_ACTCheckCollis_WAY) != 7 &&
                _DistSqGV(goal, test_CURRENTROOT(wallGObj_ACTCheckCollis_WAY)) < 40000.0f &&
                _DistxzSqGV(pos, test_CURRENTROOT(wallGObj_ACTCheckCollis_WAY)) < 40000.0f) {
                ok = 0;
            }
        }
        if (e != 0 && ok != 0) {
            float d = pos[1] - goal[1];

            if (d < 0.0f ? -d > 100.0f : d > 100.0f) {
                if (goal[1] < pos[1]) {
                    float p0[4];
                    float p1[4];
                    float dir[4];

                    p0[0] = pos[0];
                    p0[2] = pos[2];
                    p0[1] = pos[1] - 150.0f;
                    _OrientXZGV(dir, goal, pos);
                    sceVu0ScaleVector(dir, dir, 200.0f);
                    sceVu0AddVector(p1, p0, dir);
                    if (ACTCheckCollis_WAY(10.0f, p0, p1, 0, 0) == 0) {
                        ok = 0;
                    }
                }
            }
        }
        if (ok != 0) {
            goto restart;
        }
        _OrientXZGV((float *)&act->wayNodeX, goal, pos);
        if (self == boyGObj || self == girlGObj) {
            DetourCheck(self, (float *)&act->wayNodeX);
        }
        act->wayGoalDist = _DistxzGV(goal, pos);
        act->wayGoalHeight = goal[1] - pos[1];
        if (act->wayGoalDist < 200.0f) {
            act->wayFlags |= 0x40000;
        }
        break;
    }
    }
    if (act->way.reached == 0 ||
        (stage_no == 22 && self->kind == 4 && ((int)(act->wayFlags >> 17) & 1) != 0)) {
        goto done;
    }
restart:
    *(short *)&act->wayFlags = 3;
    r = ACTWayMove_BeginDetail(self, pos, goal, act->wayTarget, 0, d);
    if (r == 0) {
        return 0;
    }
done:
    return 1;
}

#ifdef ICO_HOST

static unsigned char way_flag(int flags, int mask) /* derived name */
{
    if (flags & mask) {
        return 1;
    }
    return 0;
}

int ACTWayExec_Position(GObj *self, ICO_WORD tgt, float *dir, float speed, int flags)
{
#else
int ACTWayExec_Position(GObj *self, int tgt, float *dir, float speed, int flags)
{
    /* an inline function nested in the body: whether flags has the mask's bits */
    inline unsigned char way_flag(int mask) /* derived name */
    {
        if (flags & mask) {
            return 1;
        }
        return 0;
    }

#endif
    Act *w = GOBJ_ACT(self);
    char *node;
    float d2[4];
    float p2[4];
    float v[4];
    float pos[4];
    float f;

#ifdef ICO_HOST
    if (way_flag(flags, 1)) {
#else
    if (way_flag(1)) {
#endif
        sceVu0ScaleVector(v, dir, -1.0f);
    } else {
        v[0] = dir[0];
        v[1] = dir[1];
        v[2] = dir[2];
    }
#ifdef ICO_HOST
    if (way_flag(flags, 4)) {
        GetRootProjectionPosOfGObj(pos, (GObj *)tgt);
#else
    if (way_flag(4)) {
        GetRootProjectionPosOfGObj(pos, tgt);
#endif
        if (WayMove_CheckCollis(pos, v, 0, 0)) {
            v[0] = pos[0];
            v[1] = pos[1];
            v[2] = pos[2];
        }
    }
    d2[0] = v[0];
    d2[1] = v[1];
    d2[2] = v[2];
    GetRootProjectionPosOfGObj(p2, self);
    if (ACTWayMove_BeginDetail(self, p2, d2, (void *)tgt, 0, 0) == 0) {
        return 0;
    }
    node = (char *)w->dir;
    for (;;) {
        d2[0] = v[0];
        d2[1] = v[1];
        d2[2] = v[2];
        GetRootProjectionPosOfGObj(p2, self);
        if (ACTWayMove_NextDetail(self, (float *)node, d2, 0, 0) == 0) {
            return 0;
        }
        f = w->wayGoalDist;
        w->dir[0] = w->wayNodeX;
        w->dir[1] = w->wayNodeY;
        w->dir[2] = w->wayNodeZ;
        if (f < speed) {
            if (w->wayGoalHeight < 100.0f) {
                return 1;
            }
        }
#ifdef ICO_HOST
        if (f < 200.0f || way_flag(flags, 2)) {
#else
        if (f < 200.0f || way_flag(2)) {
#endif
            w->stick.mag = 0.5f;
        } else {
            w->stick.mag = 1.0f;
        }
        _ACTWait(1);
    }
}

int ACTWay_IsMustWalkFromWay(GObj *self)
{
    WayPoint *w = GOBJ_ACT(self)->way.chk.cur;
    float d;

    if (w == 0) {
        return 0;
    }
    d = w->float2C;
    if (d != 0.0f) {
        return _DistxzSqGV(w->pos, test_CURRENTROOT(self)) < d * d;
    }
    return 0;
}

void ACTWay_SetBeginPositionIllegal(GObj *self)
{
    Act *p = GOBJ_ACT(self);
    p->wayState.flags |= 0x200000;
}
