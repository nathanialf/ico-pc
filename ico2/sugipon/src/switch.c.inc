/* switch.c: the floor and wall levers and the switches; box.c includes this
   file. */

void switchOnSE(GObj *self)
{
    ExecuteSEPackage(self, 63);
}

void switchReleaseSE(GObj *self)
{
    ExecuteSEPackage(self, 64);
}

inline void SetSwitchTriggerFunc(GObj *lever, int (*func)(GObj *, int))
{
    ((LeverGeoWork *)GOBJ_SUB(lever)->work)->trigger = func;
}

inline int InitSwitchGeo(void)
{
    return 0;
}

inline void SwitchGeo(void) {}

inline void SwitchDL(void) {}

void GetFloorLeverGlobalHoldPoint(void *dst, GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;
    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    MatrixDrive_RotMatrixZ(p->angle);
    MatrixDrive_TransMatrix(0.0f, -50.0f, 0.0f);
    CopyVector(dst, MatrixDrive_GetMatrix()[3]);
}

inline void SetFloorLeverWithNodePoint(GObj *lev, GObj *actor, int focus)
{
    float buf[4];
    float pos[4];
    LeverGeoWork *q = GOBJ_SUB(lev)->work;
    int idx = GetSkeltonFocusNode(actor, focus);
    void *m;
    GetRootPosition(pos, lev);
    sceVu0SubVector(buf, (char *)GOBJ_SUB(actor)->nodeMtx + (idx << 6) + 0x30, pos);
    *(int *)&buf[3] = 0;
    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    m = MatrixDrive_GetMatrix();
    MatrixDrive_SetTransposeMatrix(m, MatrixDrive_GetMatrix());
    sceVu0ApplyMatrix(buf, MatrixDrive_GetMatrix(), buf);
    MatrixDrive_GetTurnYAngleXZ(&q->shake, &q->angle, buf[0], -buf[1], buf[2] * 0.0f);
}

inline int CanFloorLeverPull(GObj *lev)
{
    return ((LeverGeoWork *)GOBJ_SUB(lev)->work)->state == 0;
}

inline void initParentize(GObj *lev)
{
    ClipWork buf;
    GetRootPosition(buf.pt[0], lev);
    CopyVector(buf.pt[1], buf.pt[0]);
    buf.pt[0][1] -= 10.0f;
    buf.pt[1][1] += 200.0f;
    ClipFloor(&buf);
    if (buf.floor.elem != 0) {
        LinkParentOfDObj(lev, &buf.floor.o);
    }
}

/* the zeroed template both lever inits copy into a new geometry block */
static LeverGeoWork leverGeoInit = {0}; /* derived name */

inline LeverGeoWork *InitFloorLeverGeo(GObj *self, SObjSimpleSetting *lay)
{
    /* "src/switch.c" is this file's name in the game's source tree */
    LeverGeoWork *g = (LeverGeoWork *)iosMallocDebug(ios_partition_sugipon, sizeof(LeverGeoWork),
                                                     "src/switch.c", 141);
    *g = leverGeoInit;
    if (lay->obj != 0) {
        g->linked = 1;
    } else {
        g->linked = 0;
    }
    g->base = (ICO_WORD_PTR(struct Sub15C *))CSVSYSTEM_InitDObj(
        accessary[GOBJ_SUB(self)->accessary].model, lay);
    g->handle = CSVSYSTEM_InitDObj(accessary[GOBJ_SUB(self)->accessary].model2, lay);
    return g;
}

inline void SetSwitchState(GObj *lev, int state)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;
    int cur = p->state;
    unsigned char ne = cur != state;
    if (ne) {
        switchOnSE(lev);
        p->state = state;
    } else {
        p->state = state;
    }
}

void FloorLeverTriStateGeo(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;

    if (p->state != -1 && p->state != 1) {
        if (p->trigger != 0) {
            p->trigger(GOBJ_SUB(lev)->parent.obj, 0);
        }
    } else {
        if (p->trigger != 0) {
            p->trigger(GOBJ_SUB(lev)->parent.obj, p->state);
        }
    }
    UpdateRootMatrix(lev);
}

/* the two frame-rate divisors, the pair box.c's own fall code reads */

/* The floor lever's per-frame function.  The frame divisor (60 -
   systemStatus[0] * 10) / systemStatus[1] is written twice, once for the
   limit test and once for the equality test; the parentize probe is the
   inlined initParentize above. */
void FloorLeverGeo(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;

    if (p->linked == 0) {
        if (p->linkWait++ >= 3) {
            initParentize(lev);
            p->linked = 1;
        }
    }
    if (p->state != 0) {
        p->timer = p->timer + 1;
        if (p->timer <= (60 - systemStatus[0] * 10) / systemStatus[1]) {
            p->shake = (short)(random_signed() * 128.0f);
            if (p->timer == (60 - systemStatus[0] * 10) / systemStatus[1]) {
                switchReleaseSE(lev);
            }
            if (p->trigger != 0) {
                p->trigger(GOBJ_SUB(lev)->parent.obj, p->state);
            }
        } else if (p->angle != 0) {
            p->shake = 0;
            p->angle = (short)((float)p->angle * 0.2f);
        } else {
            float q[4];

            SetIdentityQuaternion(q);
            RotQuaternionX(q, 16384);
            GetRootMatrix(MatrixDrive_GetMatrix(), lev);
            SetParticleEffect(0, MatrixDrive_GetMatrix()[3], q);
            p->state = 0;
            p->timer = 0;
            if (p->trigger != 0) {
                p->trigger(GOBJ_SUB(lev)->parent.obj, 0);
            }
        }
    } else {
        if (__builtin_abs(p->angle) < 1501) {
            if (__builtin_abs(p->shake) < 1501) {
                goto end;
            }
        }
        p->state = 1;
        p->timer = 0;
        switchOnSE(lev);
        ObjAction_MailCenter(lev, -1);
    }
end:
    UpdateRootMatrix(lev);
}

inline int GetFloorLeverAngle(GObj *lev)
{
    return ((LeverGeoWork *)GOBJ_SUB(lev)->work)->angle;
}

void FloorLeverDL(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;

    p2o_DispVU1(lev);
    MatrixDrive_PushMatrix();
    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    MatrixDrive_RotMatrixZ(p->angle);
    MatrixDrive_RotMatrixX(p->shake);
    CopyMatrix((void *)p->handle->nodeMtx, MatrixDrive_GetMatrix());
    p2o_DispVU1DObj(p->handle);
    MatrixDrive_PopMatrix();
}

void GetWallLeverGlobalHoldPoint(void *dst, GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;

    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    MatrixDrive_RotMatrixZ(p->angle);
    MatrixDrive_TransMatrix(-50.0f, 0.0f, 0.0f);
    CopyVector(dst, MatrixDrive_GetMatrix()[3]);
}

inline void SetWallLeverWithNodePoint(GObj *lev, GObj *actor, int focus)
{
    float buf[4];
    float pos[4];
    LeverGeoWork *q = GOBJ_SUB(lev)->work;
    int idx = GetSkeltonFocusNode(actor, focus);
    void *m;
    GetRootPosition(pos, lev);
    sceVu0SubVector(buf, (char *)GOBJ_SUB(actor)->nodeMtx + (idx << 6) + 0x30, pos);
    *(int *)&buf[3] = 0;
    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    m = MatrixDrive_GetMatrix();
    MatrixDrive_SetTransposeMatrix(m, MatrixDrive_GetMatrix());
    sceVu0ApplyMatrix(buf, MatrixDrive_GetMatrix(), buf);
    MatrixDrive_GetTurnXAngleYZ(&q->shake, &q->angle, buf[0], buf[1], buf[2] * 0.0f);
    q->timer = 0;
}

inline int CanWallLeverPull(GObj *lev)
{
    return ((LeverGeoWork *)GOBJ_SUB(lev)->work)->state == 0;
}

inline LeverGeoWork *InitWallLeverGeo(GObj *self, SObjSimpleSetting *lay)
{
    LeverGeoWork *g = (LeverGeoWork *)iosMallocDebug(ios_partition_sugipon, sizeof(LeverGeoWork),
                                                     "src/switch.c", 300);
    *g = leverGeoInit;
    if (lay->obj != 0) {
        g->linked = 1;
    } else {
        g->linked = 0;
    }
    g->base = (ICO_WORD_PTR(struct Sub15C *))CSVSYSTEM_InitDObj(
        accessary[GOBJ_SUB(self)->accessary].model, lay);
    g->handle = CSVSYSTEM_InitDObj(accessary[GOBJ_SUB(self)->accessary].model2, lay);
    return g;
}

inline int GetWallLeverAngle(GObj *lev)
{
    return ((LeverGeoWork *)GOBJ_SUB(lev)->work)->angle;
}

inline int IsWallLeverStatus(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;
    int ret = 0;
    if (__builtin_abs(p->angle) < 3001) {
        if (__builtin_abs(p->shake) < 3001) {
            goto end;
        }
    }
    ret = 1;
end:
    return ret;
}

/* the wall lever's counterpart of FloorLeverGeo: the same parentize probe
   and the same frame divisor written twice, no trigger callback, and a mail
   sent every frame that reports the lever's rest state (the inlined
   IsWallLeverStatus above) */
void WallLeverGeo(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;

    if (p->linked == 0) {
        if (p->linkWait++ >= 3) {
            initParentize(lev);
            p->linked = 1;
        }
    }
    if (p->state != 0) {
        p->timer = p->timer + 1;
        if (p->timer <= (60 - systemStatus[0] * 10) / systemStatus[1]) {
            p->shake = (short)(random_signed() * 128.0f);
            if (p->timer == (60 - systemStatus[0] * 10) / systemStatus[1]) {
                switchReleaseSE(lev);
            }
        } else if (p->angle != 0) {
            p->shake = 0;
            p->angle = (short)((float)p->angle * 0.2f);
        } else {
            float q[4];

            SetIdentityQuaternion(q);
            RotQuaternionX(q, 16384);
            GetRootMatrix(MatrixDrive_GetMatrix(), lev);
            SetParticleEffect(0, MatrixDrive_GetMatrix()[3], q);
            p->state = 0;

            p->timer = 0;
        }
    } else {
        if (__builtin_abs(p->angle) < 1001) {
            if (__builtin_abs(p->shake) < 1001) {
                goto end;
            }
        }
        p->state = 1;
        p->timer = 0;
        switchOnSE(lev);
        ObjAction_MailCenter(lev, -1);
    }
end:
    if (IsWallLeverStatus(lev)) {
        ObjAction_MailCenter(lev, 0);
    } else {
        ObjAction_MailCenter(lev, 1);
    }
    UpdateRootMatrix(lev);
}

void WallLeverDL(GObj *lev)
{
    LeverGeoWork *p = GOBJ_SUB(lev)->work;
    Sub15C *d;
    p2o_DispVU1(lev);
    MatrixDrive_PushMatrix();
    GetRootMatrix(MatrixDrive_GetMatrix(), lev);
    MatrixDrive_RotMatrixZ(p->angle);
    MatrixDrive_RotMatrixX(p->shake);
    d = p->handle;
    CopyMatrix((void *)d->nodeMtx, MatrixDrive_GetMatrix());
    p2o_DispVU1DObj(p->handle);
    MatrixDrive_PopMatrix();
}

/* the poll loop the four functions below share */
static inline int checkReadySwitchKind(int kind) /* derived name */
{
    GObj *o;

    for (o = isysGObjSearchFromObjKindID_begin(kind); o != 0;
         o = isysGObjSearchFromObjKindID_next(o)) {
        LeverGeoWork *p = GOBJ_SUB(o)->work;

        if (p->linked == 0) {
            return 0;
        }
    }
    return 1;
}

int CheckReadyAllSwitches(void)
{
    if (checkReadySwitchKind(22) == 0) {
        return 0;
    }
    if (checkReadySwitchKind(23) == 0) {
        return 0;
    }
    if (checkReadySwitchKind(24) == 0) {
        return 0;
    }
    return checkReadySwitchKind(25);
}
