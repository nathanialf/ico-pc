/* girl_brain_attract.c.inc: the girl brain's "attract" state, #included as
 * source into girl_act.c.  Its includer in the 2002 tree is
 * girl_brain_main.c.inc, which is not split out, so the include sits at the
 * point of girl_act.c that girl_brain_main.c.inc's text occupies. */

static int ATGoalTurnMail(void *actor, float *dir, int node);
/* void (void *, int, void *, void *) here, int (char *, int, void *, void *) in mail-add-data.h */
extern void ActSendMail_WithAdditionalData(void *gop, int msg, void *sender, void *data);

static void *attractMailData; /* derived name */

/* expanded twice in subGirlBrain_Attract: turn toward the target, and when
   already facing it send the attract mail with the target attached.  The
   result (whether a turn was started) is dropped by both callers. */
static inline int girlAttractTurnMailL(void *actor, void *obj, float *from,
                                       float *to) /* derived name */
{
    float dir[4];

    _OrientXZGV(dir, from, to);
    if (ATGoalTurnMail(actor, dir, 0x2C)) {
        return 1;
    }
    attractMailData = obj;
    ActSendMail_WithAdditionalData(actor, 0x156, actor, &attractMailData);
    return 0;
}

/* the same turn toward the boy, then the plain mail */
static inline int girlAttractTurnMailR(void *actor, float *from, float *to) /* derived name */
{
    float dir[4];

    _OrientXZGV(dir, from, to);
    if (!ATGoalTurnMail(actor, dir, 0x2C)) {
        ACTSendMailCorrect(actor, 0x158);
        return 0;
    }
    return 1;
}

static int ATGoalTurnMail(void *actor, float *dir, int node)
{
    float o1[4];
    float v[4];
    float o2[4];
    int r1;
    int m;
    int r2;
    int d;
    int ret;

    ret = 0;
    v[0] = dir[0];
    v[1] = dir[1];
    v[2] = dir[2];
    GetSkeltonOrient(o1, actor, 0x2C);
    r1 = _RotyGV(o1, v);
    m = (r1 < 0) ? -r1 : r1;
    if (m >= 0x15) {
        GetSkeltonOrient(o2, actor, node);
        r2 = _RotyGV(o2, v);
        if (m >= 0x5A) {
            goto use_r2;
        }
        if (r1 * r2 < 0) {
            goto set_r1;
        }
    use_r2:
        d = r2;
        goto have_d;
    set_r1:
        d = r1;
    have_d:
        if (d > 0) {
            ATGoalTurnSet(actor, 2, 2, v);
        } else {
            ATGoalTurnSet(actor, 2, 1, v);
        }
        ret = 1;
    }
    return ret;
}

static int checkWaterfallResult(float *out)
{
    union { /* field names derived */
        float f[4];
        int i[4];
    } t;

    float pa[4];
    float pb[4];
    int r1;
    int r0;
    int ret;

    r1 = -1;
    r0 = -1;
    ret = 0;
    GetRootProjectionPosOfGObj(pa, boyGObj);
    GetRootProjectionPosOfGObj(pb, girlGObj);
    if (1600.0f < pb[1] && pb[1] < 2300.0f) {
        if (pb[2] < -150.0f) {
            r1 = 0;
        } else if (850.0f < pb[2]) {
            r1 = 2;
        } else {
            r1 = 1;
        }
    }
    if (1600.0f < pa[1] && pa[1] < 2300.0f) {
        if (pa[2] < -150.0f) {
            r0 = 0;
        } else if (850.0f < pa[2]) {
            r0 = 2;
        } else {
            r0 = 1;
        }
    }
    if (r0 == -1) {
        return 0;
    }
    if (r1 == -1) {
        return 0;
    }
    if (r1 == 1) {
        return 1;
    }
    if (r0 == 1) {
        if (*(void **)((char *)GOBJ_SUB(boyGObj)) != 0 &&
            ICO_RAW(int, *(void **)((char *)GOBJ_SUB(boyGObj)), 0xC,
                    (*(GObj **)GOBJ_SUB(boyGObj))->kind) == 17) {
            t.f[0] = (test_CURRENTROOT(*(void **)((char *)GOBJ_SUB(boyGObj))))[0];
            t.f[1] = (test_CURRENTROOT(*(void **)((char *)GOBJ_SUB(boyGObj))))[1];
            t.f[2] = (test_CURRENTROOT(*(void **)((char *)GOBJ_SUB(boyGObj))))[2];
        } else {
            return 1;
        }
    }
    if (r1 == 0 && r0 != 0) {
        ret = 1;
        if (r0 == 1 && t.f[2] < -50.0f) {
            out[0] = pa[0];
            out[1] = 1800.0f;
            out[2] = -170.0f;
            ret = 2;
        }
    }
    if (r1 == 2 && r0 != r1) {
        ret = 1;
        if (r0 == 1 && 750.0f < t.f[2] && 650.0f < t.f[0] && t.f[0] < 950.0f) {
            out[0] = pa[0];
            out[1] = 1900.0f;
            out[2] = 870.0f;
            ret = 2;
        }
    }
    return ret;
}

/* The record Act.enemy points at, as far as this file uses it: the sofa
   position the attract state hands over (copied from the actor's 0x5B0) and
   the facing _GetRootObjectOrient fills for it. */
typedef struct { /* field names derived */
    char pad0[560];
    sceVu0FVECTOR pos; /* 0x230 */
    sceVu0FVECTOR dir; /* 0x240 */
} GirlSofaWork;        /* derived name */

/* the three attract parameter sets */
static GirlAttractParam attractObjParam = {0, {0}, {0}, 0, 338, 100.0f, 100.0f}; /* derived name */

static GirlAttractParam attractSofaParam = {0, {0}, {0}, 68, 267, 50.0f, 100.0f,
                                            1, {0}, {0}, 0,  {0}, 200.0f}; /* derived name */

static GirlAttractParam attractBoyParam = {0, {0}, {0}, 0, 338, 100.0f, 70.0f}; /* derived name */

#ifdef ICO_HOST

/* former nested helpers of subGirlBrain_Attract; they read cnt, sub and cur
   through the static chain, which are now passed explicitly */
static __inline__ void setMode(int *cnt, Act *sub, int mode) /* derived name */
{
    *cnt = 0;
    sub->wayMode = mode;
}

static __inline__ int getKind(void *cur) /* derived name */
{
    if (cur == (void *)boyGObj && GOBJ_ACT(boyGObj)->actMode == 45) {
        return 0;
    } else if (ICO_RAW(int, cur, 0xC, ((GObj *)cur)->kind) == 16) {
        return 1;
    } else if (GOBJ_ACT(boyGObj)->actMode == 49 &&
#ifdef ICO_HOST
               /* the sub record's first word is parent.obj, 8 bytes here */
               (void *)GOBJ_SUB(girlGObj)->parent.obj == (void *)GOBJ_ACT(boyGObj)->box) {
#else
               *(int *)GOBJ_SUB(girlGObj) == (int)GOBJ_ACT(boyGObj)->box) {
#endif
        return 2;
    }
    return 3;
}

#endif

/* The actor entry parameter is volatile: the actor scheduler writes the
 * caller's home slot while this thread is parked in _ACTWait. */
void subGirlBrain_Attract(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
#ifdef ICO_HOST

    float mypos[4];

    float last[4];

    GirlAttractParam tgt;

    int cnt;

    void *cur = brain_val.lastTarget;

    int type = 0;

    int kind;

    GObj *param = 0;

    int dbg = 0; /* local debug switch, see the rate block below */

    int fr = (60 - systemStatus[0] * 10) / systemStatus[1];

    int fr3 = fr + (60 - systemStatus[0] * 10) / systemStatus[1] * 2;

    int turnCnt = 0;

    int hintCnt = 0;

#else
    float mypos[4];
    float last[4];
    GirlAttractParam tgt;
    int cnt;
    void *cur = brain_val.lastTarget;
    int type = 0;
    int kind;
    GObj *param = 0;
    int dbg = 0; /* local debug switch, see the rate block below */
    int fr = (60 - systemStatus[0] * 10) / systemStatus[1];
    int fr3 = fr + (60 - systemStatus[0] * 10) / systemStatus[1] * 2;
    int turnCnt = 0;
    int hintCnt = 0;

    /* two nested helpers, which read cnt, sub and cur through the static chain */
    __inline__ void setMode(int mode) /* derived name */
    {
        cnt = 0;
        sub->wayMode = mode;
    }
    __inline__ int getKind(void) /* derived name */
    {
        if (cur == (void *)boyGObj && GOBJ_ACT(boyGObj)->actMode == 45) {
            return 0;
        } else if (*(int *)((char *)cur + 0xC) == 16) {
            return 1;
        } else if (GOBJ_ACT(boyGObj)->actMode == 49 &&
                   *(int *)GOBJ_SUB(girlGObj) == (int)GOBJ_ACT(boyGObj)->box) {
            return 2;
        }
        return 3;
    }
#endif
    float root[4];

restart:
#ifdef ICO_HOST
    setMode(&cnt, sub, 0);

#else
    setMode(0);
#endif
    if (cur == 0) {
        debug_assert(__FILE__, 279);
        __assert(__FILE__, 279, "0");
    }
#ifdef ICO_HOST

    kind = getKind(cur);

#else
    kind = getKind();
#endif
    switch (kind) {
    case 0:
        type = 1;
        param = GOBJ_ACT(boyGObj)->env.sofaObj.obj;
        break;
    case 1:
        type = 1;
        param = cur;
        break;
    case 2:
        type = 2;
        param = 0;
        break;
    case 3:
        type = 0;
        param = 0;
        break;
    }

    switch (type) {
    case 0:
        tgt = attractObjParam;
        tgt.obj = cur;
        GetRootProjectionPosOfGObj(tgt.pos, cur);
        break;
    case 1:
        tgt = attractSofaParam;
        tgt.obj = cur;
        GetSofaPosition(girlGObj, param);
        tgt.pos[0] = sub->env.sofaPos[0];
        tgt.pos[1] = sub->env.sofaPos[1];
        tgt.pos[2] = sub->env.sofaPos[2];
#ifdef ICO_HOST
        /* GirlSofaWork's 0x230 and 0x240 are readyPos and readyDir, which
           actGirlReadyMove reads; the enemy work's pointers move them here */
        {
            float dir[4];

            GOBJ_ACT(self)->enemy->readyPosX = sub->env.sofaPos[0];
            GOBJ_ACT(self)->enemy->readyPosY = sub->env.sofaPos[1];
            GOBJ_ACT(self)->enemy->readyPosZ = sub->env.sofaPos[2];
            _GetRootObjectOrient(dir, param);
            GOBJ_ACT(self)->enemy->readyDirX = dir[0];
            GOBJ_ACT(self)->enemy->readyDirY = dir[1];
            GOBJ_ACT(self)->enemy->readyDirZ = dir[2];
        }
        tgt.dir[0] = GOBJ_ACT(self)->enemy->readyDirX;
        tgt.dir[1] = GOBJ_ACT(self)->enemy->readyDirY;
        tgt.dir[2] = GOBJ_ACT(self)->enemy->readyDirZ;
#else
        ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->pos[0] = sub->env.sofaPos[0];
        ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->pos[1] = sub->env.sofaPos[1];
        ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->pos[2] = sub->env.sofaPos[2];
        _GetRootObjectOrient(((GirlSofaWork *)GOBJ_ACT(self)->enemy)->dir, param);
        tgt.dir[0] = ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->dir[0];
        tgt.dir[1] = ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->dir[1];
        tgt.dir[2] = ((GirlSofaWork *)GOBJ_ACT(self)->enemy)->dir[2];
#endif
        tgt.goalTurn = 1;
        break;
    case 2:
        tgt = attractBoyParam;
        tgt.obj = boyGObj;
        GetRootProjectionPosOfGObj(tgt.pos, boyGObj);
        break;
    }

    for (;;) {
        int noTurn = 1;
        int waterHit = 0;
        int hint = 0;
        int escape = 0;
        int far = 0;
        int type2;
        float margin;
        float ratio;

        root[0] = test_CURRENTROOT((void *)self)[0];
        root[1] = test_CURRENTROOT((void *)self)[1];
        root[2] = test_CURRENTROOT((void *)self)[2];
        if (tgt.obj != 0 && ICO_RAW(int, tgt.obj, 0xC, ((GObj *)tgt.obj)->kind) == 32 &&
            !(_DistSqGV(root, test_CURRENTROOT(tgt.obj)) < 90000.0f)) {
            far = 1;
        }
        if (!ACTGame_FLAG_TETSUNAGI()) {
            sub->flags20.ll |= 0x2000000000ULL;
        }
        if (type == 0 && tgt.obj == (void *)boyGObj && GOBJ_WORK(self)->brainTimer != 0) {
            noTurn = 0;
        }
#ifdef ICO_HOST
        if (GOBJ_SUB(self)->parent.obj != 0 && GOBJ_SUB(self)->parent.obj->kind == 17 &&
#else
        if (*(int *)GOBJ_SUB(self) != 0 && *(int *)(*(int *)GOBJ_SUB(self) + 0xC) == 17 &&
#endif
            boyGObj != 0 && type == 0 && tgt.obj == (void *)boyGObj &&
            root[1] < test_CURRENTROOT(boyGObj)[1] &&
            _DistSqGV(test_CURRENTROOT((void *)self), test_CURRENTROOT(boyGObj)) <
                _ACTGame_GetParamF(30) * _ACTGame_GetParamF(30)) {
            hint = 1;
            escape = 1;
        }
        if ((int)(sub->flags20.ll >> 38) & 1) {
            hint = 1;
        }
        if (0 < brain_val.status31Timer) {
            _ACTCharStatus_Set((void *)self, 31, -1.0f, 0);
        }
#ifdef ICO_HOST
        if (kind != getKind(cur)) {
#else
        if (kind != getKind()) {
#endif
            goto restart;
        }
        type2 = girlBrainGetTargetType((void *)self);
        GetRootProjectionPosOfGObj(mypos, (void *)self);
        if (tgt.fixedPos == 0) {
            int onSofa = 0;
            float dir[4];
            float ofs[4];

            if (tgt.obj == (void *)boyGObj && GOBJ_ACT(tgt.obj)->actMode == 28) {
                onSofa = 1;
                dir[0] = test_CURRENTORIENT(boyGObj)[0];
                dir[1] = test_CURRENTORIENT(boyGObj)[1];
                dir[2] = test_CURRENTORIENT(boyGObj)[2];
            }
            if (onSofa) {
                sceVu0ScaleVector(ofs, dir, 50.0f);
                GetSkeltonPosition(tgt.pos, boyGObj, 22);
                sceVu0AddVector(tgt.pos, tgt.pos, ofs);
                tgt.pos[1] = tgt.pos[1] - 10.0f;
            } else {
                GetRootProjectionPosOfGObj(tgt.pos, tgt.obj);
            }
            if ((int)(GOBJ_ACT(boyGObj)->flags20.ll >> 43) & 1) {
                float wp[4];

                switch (checkWaterfallResult(wp)) {
                case 1:
                    tgt.pos[0] = mypos[0];
                    tgt.pos[1] = mypos[1];
                    tgt.pos[2] = mypos[2];
                    if (tgt.obj != 0) {
                        GirlBrainClearTarget((void *)self, &tgt);
                    }
                    break;
                case 2:
                    tgt.pos[0] = wp[0];
                    tgt.pos[1] = wp[1];
                    tgt.pos[2] = wp[2];
                    waterHit = 1;
                    break;
                }
            }
        }
        dispWayMarker(tgt.pos);
        if (tgt.obj != 0) {
            if (brainDecTargetTimer(tgt.obj)) {
                GirlBrainClearTarget((void *)self, &tgt);
            }
        }
        if (boyGObj != 0 && GOBJ_ACT(boyGObj)->actMode == 49) {
            if (tgt.obj == (void *)boyGObj || tgt.obj == GOBJ_ACT(boyGObj)->box) {
                GirlBrainClearTarget((void *)self, &tgt);
            }
        }
        if (sub->actMode == 81) {
            if (tgt.obj == (void *)boyGObj) {
                GirlBrainClearTarget((void *)self, &tgt);
            }
        }
        if (sub->wayMode == 3) {
            if (girlBrainGetTypeRatio(type2) == 0.0f) {
#ifdef ICO_HOST
                setMode(&cnt, sub, 0);
#else
                setMode(0);
#endif
            }
        }
        {
            float level = girlBrainGetTargetLevel();
            float speed = _ACTGame_GetParamF(29) + level * (1.0f - _ACTGame_GetParamF(29));
            float clamped;

            clamped = (speed < _ACTGame_GetParamF(29)) ? _ACTGame_GetParamF(29)
                                                       : ((1.0f < speed) ? 1.0f : speed);
            ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, clamped, 1);
        }
        {
            float rate = girlBrainGetTargetLevel() * 4.0f;
            float lo = _ACTGame_GetParamF(27);
            float hi = _ACTGame_GetParamF(28);
            float v = lo + rate * (hi - lo);

            v = (v < lo) ? lo : ((hi < v) ? hi : v);
            /* a second speed, computed and clamped like the one above and applied only
               with the debug switch set */
            if (dbg) {
                ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, v, 1);
            }
        }
        if (((int)(sub->flags18.ll >> 62) & 1) && type == 0 && tgt.obj == (void *)boyGObj) {
            if (sub->wayMode == 7) {
                if (!(_DistSqGV(test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj)) <
                      10000.0f)) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 1);
#else
                    setMode(1);
#endif
                }
            }
        }
        switch (sub->wayMode) {
        case 0:
            _ACTLookTarget_Set((void *)self, 0, 0, 6, 1);
            sub->stick.mag = 0.0f;
#ifdef ICO_HOST
            setMode(&cnt, sub, 6);
#else
            setMode(6);
#endif
            break;
        case 1: {
            int p = ACTWayMove_BeginDetail((void *)self, mypos, tgt.pos, tgt.obj, 0, 0);

            if (tgt.fixedPos != 0) {
                sub->wayState.bits.wayDetail = 1;
            }
            if (p != 0) {
#ifdef ICO_HOST
                setMode(&cnt, sub, 3);
#else
                setMode(3);
#endif
                last[0] = tgt.pos[0];
                last[1] = tgt.pos[1];
                last[2] = tgt.pos[2];
                turnCnt = 0;
                hintCnt = 0;
            } else {
#ifdef ICO_HOST
                setMode(&cnt, sub, 2);
#else
                setMode(2);
#endif
            }
            break;
        }
        case 3: {
            int p = ACTWayMove_NextDetail((void *)self, sub->dir, tgt.pos, 0,
                                          tgt.obj != 0 && IsTopHint(tgt.obj));

            if (GOBJ_ACT(self)->actMode == 45) {
                if (type == 1) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 4);
#else
                    setMode(4);
#endif
                    break;
                }
            } else if (type == 1) {
                if (tgt.obj != (void *)boyGObj && GOBJ_WORK(self)->sofaRestTimer != 0) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 8);
#else
                    setMode(8);
#endif
                    sub->stick.mag = 0.0f;
                    break;
                }
                if (type == 1 && ((int)(sub->flags20.ll >> 12) & 1) &&
                    _DistSqGV(test_CURRENTROOT((void *)self), tgt.pos) < 40000.0f) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 2);
#else
                    setMode(2);
#endif
                    break;
                }
            }
            if (p == 0) {
#ifdef ICO_HOST
                setMode(&cnt, sub, 2);
            } else if ((int)((unsigned long long)sub->wayFlags >> 17) & 1) {
                setMode(&cnt, sub, 2);
            } else if (hint) {
                setMode(&cnt, sub, 2);
#else
                setMode(2);
            } else if ((int)((unsigned long long)sub->wayFlags >> 17) & 1) {
                setMode(2);
            } else if (hint) {
                setMode(2);
#endif
            } else {
                if (!noTurn) {
                    margin = 100.0f;
                } else if (sub->actMode == 3 && (double)girlBrainGetTypeRatio(type2) > 0.8) {
                    margin = 100.0f;
                } else {
                    margin = 0.0f;
                }
                sub->dir[0] = sub->wayNodeX;
                sub->dir[1] = sub->wayNodeY;
                sub->dir[2] = sub->wayNodeZ;
                if (waterHit) {
                    if (sub->wayGoalDist < 50.0f && sub->wayGoalHeight < tgt.goalHeight) {
#ifdef ICO_HOST
                        setMode(&cnt, sub, 4);
#else
                        setMode(4);
#endif
                        sub->stick.mag = 0.0f;
                    } else if (sub->wayGoalDist < 300.0f) {
                        girlBrainSetWalkRatio((void *)self, 0.5f);
                    } else {
                        ratio = far ? 1.0f : girlBrainGetTypeRatio(type2);
                        girlBrainSetWalkRatio((void *)self, ratio);
                    }
                } else {
                    if (sub->wayGoalDist < tgt.goalDist + margin &&
                        ((sub->wayGoalHeight < 0.0f) ? -sub->wayGoalHeight : sub->wayGoalHeight) <
                            tgt.goalHeight) {
                        if (tgt.goalTurn) {
                            if (ATGoalTurnMail((void *)self, tgt.dir, 0x23)) {
                                noTurn = 0;
                            }
                        }
                        if (noTurn) {
#ifdef ICO_HOST
                            setMode(&cnt, sub, 4);
#else
                            setMode(4);
#endif
                        } else {
                            sub->stick.mag = 0.0f;
                        }
                    } else {
                        ratio = far ? 1.0f : girlBrainGetTypeRatio(type2);
                        girlBrainSetWalkRatio((void *)self, ratio);
                        if (sub->wayGoalDist < tgt.slowDist) {
                            sub->stick.mag = (0.5f < sub->stick.mag) ? 0.5f : sub->stick.mag;
                        }
                    }
                }
            }
            if (!(_DistSqGV(last, tgt.pos) < 90000.0f)) {
                if (_DistSqGV(mypos, tgt.pos) > _DistSqGV(mypos, last)) {
                    brainSubLevelGop(tgt.obj, 0.5f);
                }
                last[0] = tgt.pos[0];
                last[1] = tgt.pos[1];
                last[2] = tgt.pos[2];
            }
            if (tgt.obj != (void *)boyGObj && !(tgt.obj != 0 && IsTopHint(tgt.obj)) &&
                !ACTGameViewSimple_Check(self, tgt.obj)) {
                switch (sub->actMode) {
                case 2:
                    turnCnt = turnCnt + 1;
                    break;
                case 3:
                    turnCnt = turnCnt + 2;
                    break;
                }
                if ((60 - systemStatus[0] * 10) / systemStatus[1] * 15 < turnCnt) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 2);
#else
                    setMode(2);
#endif
                }
            }
            if (tgt.obj != 0 && IsTopHint(tgt.obj) &&
                ((int)((unsigned long long)sub->wayFlags >> 18) & 1)) {
                hintCnt = hintCnt + actModeTbl[GOBJ_ACT(self)->actMode].hintCount;
                if ((60 - systemStatus[0] * 10) / systemStatus[1] * 4 < hintCnt) {
                    sub->stick.mag = 0.0f;
                }
                if ((60 - systemStatus[0] * 10) / systemStatus[1] * 5 < hintCnt) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 2);
#else
                    setMode(2);
#endif
                }
            }
            break;
        }
        case 6:
            if (fr < cnt || brain_val.lookHold != 0) {
                if (girlBrainGetTypeRatio(type2) != 0.0f) {
#ifdef ICO_HOST
                    setMode(&cnt, sub, 1);
#else
                    setMode(1);
#endif
                }
            }
            /* fallthrough */
        case 2:
        case 7:
            if (cnt++ < fr3 + ((tgt.obj != 0 && IsTopHint(tgt.obj))
                                   ? (60 - systemStatus[0] * 10) / systemStatus[1] * 2
                                   : 0)) {
                sub->stick.mag = 0;
                _ACTLookTarget_Set((void *)self, tgt.obj, 0, 2, 1);
                if (tgt.obj != 0 && IsTopHint(tgt.obj) && sub->wayMode == 2 &&
                    girlControlMode == 0) {
                    girlAttractTurnMailL((void *)self, tgt.obj, tgt.pos, mypos);
                    if (actModeTbl[GOBJ_ACT(self)->actMode].attractWait) {
                        cnt = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
                    }
                }
                if (tgt.obj == (void *)boyGObj && sub->wayMode == 2 && escape == 0 &&
                    GOBJ_WORK(self)->turnMailWait == 0 && girlControlMode == 0 &&
                    brain_val.targetFlag != 0) {
                    girlAttractTurnMailR((void *)self, tgt.pos, mypos);
                    if (sub->actMode == 77) {
                        cnt = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
                    }
                }
            } else {
                _ACTLookTarget_Set((void *)self, 0, 0, 6, 1);
#ifdef ICO_HOST
                setMode(&cnt, sub, 8);
#else
                setMode(8);
#endif
            }
            if (((int)(sub->flags18.ll >> 63) & 1) || (sub->flags20.i[0] & 1)) {
                sub->flags20.ll |= 2;
            }
            break;
        case 4:
            if (tgt.kind == 0 && (tgt.obj == 0 || !IsTopHint(tgt.obj))) {
#ifdef ICO_HOST
                setMode(&cnt, sub, 7);
            } else {
                setMode(&cnt, sub, 5);
#else
                setMode(7);
            } else {
                setMode(5);
#endif
            }
            break;
        case 5:
            if (cnt++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 200 / 60 +
                            ((tgt.obj != 0 && IsTopHint(tgt.obj))
                                 ? (60 - systemStatus[0] * 10) / systemStatus[1] * 2
                                 : 0)) {
                sub->stick.mag = 0.0f;
                if (tgt.obj != 0 && IsTopHint(tgt.obj) && girlControlMode == 0) {
                    girlAttractTurnMailL((void *)self, tgt.obj, tgt.pos, mypos);
                    if (actModeTbl[GOBJ_ACT(self)->actMode].attractWait) {
                        cnt = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
                    }
                } else {
                    sub->orientMot = tgt.kind;
                    ACTSendMailCorrect((void *)self, tgt.mail);
                }
            } else {
#ifdef ICO_HOST
                setMode(&cnt, sub, 8);
#else
                setMode(8);
#endif
            }
            break;
        case 8:
            cnt = cnt + 1;
            if (cnt == 2) {
                GirlBrainClearTarget((void *)self, &tgt);
            }
            if ((60 - systemStatus[0] * 10) / systemStatus[1] < cnt) {
#ifdef ICO_HOST
                setMode(&cnt, sub, 0);
#else
                setMode(0);
#endif
            }
            break;
        }
        _ACTWait(1);
    }
}
