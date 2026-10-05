/* motMan_rootUpdate.c.inc: the motion manager's root update, the wall, floor and water passes; motionManager.c includes this file. */
/* _wallHitReaction's helper */
static inline int getWallSlideDirection(void *dir, void *hit, void *wall) /* derived name */
{
    Vec16 t;

    if (_InnerProduct(hit, wall) < -0.99f) {
        return 0;
    }

    CopyVector(&t, hit);
    t.f[3] = 0.0f;
    GetProjectionOfPlane(dir, &t, wall);
    ((Vec16 *)dir)->f[3] = 0.0f;
    _NormalizeVector(dir, dir);

    return 1;
}

static void _wallHitReaction(ClipWork *w, void *pos, void *last, int noSlide)
{
    Vec16 mv;
    Vec16 nrm;
    Vec16 pen;
    Vec16 pl;
    Vec16 slide;
    float d;

    SubVectorXYZ(&mv, w->pt[1], w->pt[2]);
    SubVectorXYZ(pos, pos, &mv);
    GetOrientOfWall(&nrm, w->wall.elem, &w->wall.o);
    skelMotCtrl->flags = skelMotCtrl->flags | 2;

    CopyVector(&pl, &nrm);
    pl.f[3] = -sceVu0InnerProduct(pos, &nrm);
    d = GetDistanceFromPlane(&pl, last);
    sceVu0ScaleVector(&pen, &nrm, d);
    sceVu0SubVector(last, last, &pen);

    d = GetDistanceFromPlane(&w->normal, w->pt[1]);
    sceVu0ScaleVector(&pen, &nrm, d);
    sceVu0SubVector(&slide, &mv, &pen);

    if (noSlide == 0) {
        ClipWork cw = {{0}, {0}, w->radius};

        CopyVector(&cw, pos);
        _AddVectorXYZ(cw.pt[1], pos, &slide);

        cw.filter.o = w->wall.o;
        cw.filter.elem = w->wall.elem;
        if (skelRoot->fieldWall != 0) {
            ClipWallEField(&cw);
        } else {
            ClipWallE(&cw);
        }
        if (debug_wallhitcoldisp != 0) {
            /* an initialised record whose address is taken */
            WallCfg disp = {w->wall.o, w->wall.elem};

            DebugDisp1Collision(&disp);
        }
        if (cw.wall.elem != 0) {
            Vec16 prj;

            GetProjectionOfPlane(&prj, &cw.normal, cw.pt[1]);
            d = GetDistanceFromPlane(&w->normal, &prj);
            if (d < 0.0f) {
                Vec16 dir;

                if (getWallSlideDirection(&dir, &w->normal, &cw.normal) != 0) {
                    Vec16 step;

                    _SubVectorXYZ(&step, cw.pt[1], cw.pt[2]);
                    GetProjectionOfPlane(&step, &dir, &step);
                    _AddVectorXYZ(pos, cw.pt[2], &step);
                } else {
                    ClipWork cw2;
                    Vec16 push;

                    memset(&cw2, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
                    _NormalizeVector(&push, &slide);
                    _ScaleVectorXYZ(&push, &push, 100.0f);
                    CopyVector(&cw2, pos);
                    _AddVectorXYZ(cw2.pt[1], pos, &push);
                    ClipWall(&cw2);
                    _AddVectorXYZ(pos, pos, &slide);
                    if (debug_wallhitcoldisp != 0) {
                        DrawCollisionRay(&cw2);
                    }
                    if (cw2.wall.elem != 0) {
                        Vec16 hp;

                        cw2.normal.f[3] = cw2.normal.f[3] - (w->radius + 1.0f);
                        if (GetProjectionOfPlane(&hp, &cw2.normal, pos) < 0.0f) {
                            CopyVector(pos, &hp);
                            if (debug_wallhitcoldisp != 0) {
                                WallCfg disp = {cw2.wall.o, cw2.wall.elem};

                                DebugDisp1CollisionWithColor(&disp, wallHitColor);
                            }
                        }
                    }
                }
            } else {
                CopyVector(pos, &prj);
            }
            if (debug_wallhitcoldisp != 0) {
                WallCfg disp = {cw.wall.o, cw.wall.elem};

                DebugDisp1CollisionWithColor(&disp, wallHitColor2);
            }
        } else {
            _AddVectorXYZ(pos, pos, &slide);
        }
    } else {
        _AddVectorXYZ(pos, pos, &slide);
    }
}

/* rootUpdateStepSolution's three helpers; the caller passes the position,
 * the last position, the radius and the fuchi mode */
static inline void clipStepWallAndReact(struct MotRoot *cur, float *last, float rad,
                                        WallCfg *col) /* derived name */
{
    ClipWork w;

    if (debug_col_old_proc != 0) {
        CopyVector(&w, last);
    } else {
        CopyVector(&w, cur->clipFrom);
    }
    CopyVector(w.pt[1], cur->pos);
    w.radius = rad * skelScale;
    w.filter = *col;
    ClipWallE(&w);
    if (w.wall.elem != 0) {
        _wallHitReaction(&w, cur->pos, last, 1);
    }
}

static inline int clipFuchiWallAndReact(struct MotRoot *cur, float *last, int mode,
                                        float rad) /* derived name */
{
    ClipWork w;

    if (debug_col_old_proc != 0) {
        CopyVector(&w, last);
    } else {
        CopyVector(&w, cur->clipFrom);
    }
    CopyVector(w.pt[1], cur->pos);
    w.radius = rad * skelScale;
    ClipWallFuchiHangWalkStop(&w);
    if (w.wall.elem != 0) {
        int attr = GetWallAttribute(&w);

        switch (mode) {
        default:
            return 0;
        case 1:
            if ((attr & 0x20000000) != 0) {
                _wallHitReaction(&w, cur->pos, last, 0);
                return 1;
            }
            break;
        case 2:
            if ((attr & 0x10000000) != 0) {
                _wallHitReaction(&w, cur->pos, last, 0);
                return 1;
            }
            break;
        }
    }
    return 0;
}

static inline int clipFieldWallAndReset(struct MotRoot *cur, float *last) /* derived name */
{
    ClipWork w;

    if (debug_col_old_proc != 0) {
        CopyVector(&w, last);
    } else {
        CopyVector(&w, cur->clipFrom);
    }
    CopyVector(w.pt[1], cur->pos);
    w.filter = InitialColInfo;
    ClipWallField(&w);
    if (w.wall.elem != 0 && CompareAttribute(w.attr, 0x10000) != 0) {
        _wallHitReaction(&w, cur->pos, last, 0);
        return 1;
    }
    return 0;
}

static void _wallCollisionPreProcess(ClipWork *w, void *p, void *q, float dy)
{
    Vec16 save;
    float r;

    if (debug_col_old_proc != 0) {
        CopyVector(w, q);
    } else {
        CopyVector(w, skelRoot->clipFrom);
    }
    CopyVector(w->pt[1], p);
    w->filter = skelRoot->filter;
    w->pt[0][1] = w->pt[0][1] + dy;
    w->pt[1][1] = w->pt[1][1] + dy;
    if (skelRoot->fieldWall != 0) {
        r = skelRoot->radius;
        if (r < 20.0f) {
            w->radius = skelScale * 20.0f;
        } else {
            w->radius = r * skelScale;
        }
        ClipWallField(w);
    } else {
        w->radius = skelRoot->radius * skelScale;
        CopyVector(&save, w->pt[1]);
        if (w->filter.o.obj != 0) {
            ClipWallE(w);
        } else {
            ClipWall(w);
        }
        CopyVector(w->pt[1], w->pt[2]);
        ClipFloor(w);
        CopyVector(w->pt[1], &save);
    }
}

static inline int wallCollisionAndFloorFit(float *pos, float *last, float dy) /* derived name */
{
    ClipWork w;
    Vec16 t;

    _wallCollisionPreProcess(&w, pos, last, dy);
    if (w.wall.elem != 0) {
        _wallHitReaction(&w, pos, last, 0);
        return 1;
    } else if (w.floor.elem != 0) {
        CopyVector(&t, w.pt[2]);
        t.f[1] = t.f[1] - dy;
        CopyVector(pos, &t);
        CopyVector(last, &t);
        return 1;
    }
    return 0;
}

/* dy is the caller's constant */
static inline void wallCollisionAndReact(float dy) /* derived name */
{
    ClipWork w;
    struct MotRoot *g = skelRoot;
    float *last = g->last;

    _wallCollisionPreProcess(&w, g->pos, last, dy);
    if (w.wall.elem != 0) {
        _wallHitReaction(&w, g->pos, last, 0);
    }
}

/* The wave-force twin of _wallCollisionPreProcess: the same work-block head,
 * then ClipWallWaveForce and the horizontal push it reports.  rootUpdateSwim
 * uses it. */
static inline void waveForceReaction(struct MotRoot *cur, float *last, float dy) /* derived name */
{
    ClipWork w;
    float d;
    float k;

    if (debug_col_old_proc != 0) {
        CopyVector(&w, last);
    } else {
        CopyVector(&w, cur->clipFrom);
    }
    CopyVector(w.pt[1], cur->pos);
    w.filter = skelRoot->filter;
    w.pt[0][1] = w.pt[0][1] + dy;
    w.pt[1][1] = w.pt[1][1] + dy;
    w.radius = skelRoot->radius + 200.0f;
    CopyVector(w.pt[1], &w);
    ClipWallWaveForce(&w);
    if (w.wall.elem != 0) {
        d = GetDistanceFromPlane(&w.normal, &w);
        d = d + w.radius;
        if (d > 0.0f) {
            k = 1.0f / (d + 50.0f);
            /* the wall normal read through Vec16, as motionOrientManager.c
             * writes vector components */
            cur->pos[0] = cur->pos[0] + ((Vec16 *)&w.normal)->f[0] * 100.0f * k;
            cur->pos[2] = cur->pos[2] + ((Vec16 *)&w.normal)->f[2] * 100.0f * k;
            skelMotCtrl->flags |= 0x2000;
        }
    }
}

inline void *upperFieldCheck(float *pos, float dy)
{
    ClipWork buf;
    CopyVector(&buf, pos);
    CopyVector(buf.pt[1], pos);
    buf.pt[1][1] = buf.pt[1][1] + dy;
    ClipFloor(&buf);
    return buf.floor.elem;
}

/* getFieldCollision's two helpers */
static inline void setPoolHeightOfFloor(ClipWork *w) /* derived name */
{
    /* &w->floor is the floor's owning object: ico2/sugipon/src/box.c reads the
     * same word of the same work block as `char **`. */
    GObj *obj = w->floor.o.obj;

    skelMotCtrl->pool = obj;
    skelMotCtrl->waterY = GetPoolGlobalHeight(obj);
    skelMotCtrl->waterDepth = w->pt[2][1] - skelMotCtrl->waterY;
}

static inline void checkPoolFloor(ClipWork *w) /* derived name */
{
    int attr = GetFloorAttribute(w);

    if (CompareAttribute(attr, 0x50) != 0) {
        if (w->pt[2][1] - skelRoot->pos[1] < skelRoot->projHeight) {
            setPoolHeightOfFloor(w);
        }
    }
}

static int getFieldCollision(ClipWork *w)
{
    float d;

    CopyVector(w, skelRoot->pos);
    CopyVector(w->pt[1], w);
    w->pt[0][1] = w->pt[0][1] - 50.0f;
    w->pt[1][1] = w->pt[1][1] + 50000.0f;
    if (skelRoot->filter.o.obj != 0) {
        w->filter = skelRoot->filter;
        ClipFloorE(w);
    } else {
        ClipFloor(w);
    }
    if (w->floor.elem != 0) {
        checkPoolFloor(w);
        d = GetYDistanceFromPlane(skelRoot->plane.f, w->pt[2]);
        if (d <= 35.0f) {
            return 1;
        }
    }
    return 0;
}

/* setIKAndAdjustRootHeight's two helpers */

/* "PLANE<EUC-JP riMItto desu> %f <EUC-JP abunai node rakka sasemasu>\n":
 * the plane limit was exceeded, so the actor is dropped. */

/* Builds the plane the root height is measured against out of the floor plane
 * the motion block carries, and reports whether the floor was steep enough to
 * need one. */
static inline int makeRootFitPlane(float *pl, struct MotRoot *o) /* derived name */
{
    float *fl = o->plane.f;
    Vec16 pos;
    Vec16 n;
    int ret = 0;

    CopyVector(pl, fl);
    if (-0.3f < fl[1]) {
        CopyVector(&pos, o->pos);
        pos.f[1] = GetYProjectionOfPlane(fl, o->pos);

        pl[1] = 0.0f;
        _NormalizeVector(&n, pl);
        _ScaleVectorXYZ(pl, &n, _Sqrt(0.90999997f));
        pl[1] = -0.3f;

        pl[0] = 0.0f;
        pl[1] = -1.0f;
        pl[2] = 0.0f;
        pl[3] = -_InnerProduct(pl, &pos);
        ret = 1;
    }
    return ret;
}

/* Drops a probe from v onto the floor and returns how far the root has to move
 * to sit on it. */
static inline float getFloorDropAt(float *v, float *pl, float *o) /* derived name */
{
    ClipWork w;
    float d;

    CopyVector(&w, v);
    CopyVector(w.pt[1], v);
    w.pt[0][1] = w.pt[0][1] - 100.0f;
    w.pt[1][1] = w.pt[1][1] + 500.0f;
    ClipFloor(&w);
    if (w.floor.elem != 0) {
        d = -(w.pt[2][1] - v[1]);
    } else {
        d = 3.40282347e+38f;
    }

    if (w.floor.elem != 0) {
        float t = GetYProjectionOfPlane(w.normal.f, o) - GetYProjectionOfPlane(pl, o);

        if (45.0f < (t < 0.0f ? -t : t)) {
            d = GetYDistanceFromPlane(pl, v);
        }
    } else {
        d = GetYDistanceFromPlane(pl, v);
    }
    return d;
}

static void setIKAndAdjustRootHeight(char *mv)
{
    Vec16 p0;
    Vec16 p1;
    Vec16 p2;
    Vec16 pl;
    float d0;
    float d1;
    float dy;
    float h;
    float t;
    int steep;
    int node;

    if (skelRoot->stepNode < 0) {
        return;
    }
    steep = makeRootFitPlane(pl.f, skelRoot);
    h = GetYProjectionOfPlane(pl.f, skelRoot->pos) + *(float *)(mv + 4) - skelRoot->pos[1];
    /* the root height report, switched off */
    if (0) {
        debug_StdPrintfDummy("regular height: %f (%f)\n", fptodp(h), fptodp(skelRoot->pos[1]));
    }
    if (steep != 0) {
        if ((h < 0.0f ? -h : h) < 35.0f) {
            skelRoot->pos[1] = skelRoot->pos[1] + h;
            skelRoot->last[1] = skelRoot->last[1] + h;
            return;
        }
        skelMotCtrl->flags = skelMotCtrl->flags | 4;
        skelRoot->last[1] = skelRoot->pos[1];
        /* the release build's printf is debug_StdPrintfDummy */
        /* EUC-JP: "PLANE limit %f, it is steep so it is dropped" */
        debug_StdPrintfDummy("PLANEリミットです %f 急なので落下させます\n", fptodp(h));

        return;
    }
    if (skelMotCtrl->rootUpdateMode == 0x14) {
        skelRoot->pos[1] = skelRoot->pos[1] + h;
        skelRoot->last[1] = skelRoot->last[1] + h;
        return;
    }
    if (skelNode->pos[1] * skelScale < (h < 0.0f ? -h : h)) {
        skelRoot->pos[1] = skelRoot->pos[1] + h;
        skelRoot->last[1] = skelRoot->last[1] + h;
        return;
    }

    makeRootFitPlane(pl.f, skelRoot);

    AddVectorXYZ(&p0, nodePos2 + skelRoot->stepNode * 16, mv);

    AddVectorXYZ(&p1, nodePos2 + skelRoot->stepNode * 16, skelRoot->pos);

    p1.f[1] = p1.f[1] - p0.f[1];

    d0 = getFloorDropAt(p1.f, pl.f, skelRoot->pos);

    dy = d0;
    switch (skelNode[skelRoot->stepNode].kind) {
    case 0x30:
        node = GetSkeltonFocusNode((GObj *)skelGObj, 0x34);
        break;
    case 0x34:
        node = GetSkeltonFocusNode((GObj *)skelGObj, 0x30);
        break;
    default:
        node = -1;
        break;
    }
    if (node != -1) {
        Vec16 p3;

        AddVectorXYZ(&p2, nodePos2 + node * 16, skelRoot->pos);

        p2.f[1] = p1.f[1];
        CopyVector(&p3, &p2);
        p3.f[1] = p3.f[1] - d0;

        d1 = getFloorDropAt(p3.f, pl.f, skelRoot->pos);

        if (d1 < 0.0f) {
            dy = d0 + d1;
        }

        if (skelMotCtrl->slipOn == 0 || (skelMotCtrl->floorAttr & 0xF00000) == 0) {
            Vec16 pl2;

            pl2 = pl;
            pl2.f[3] = 0.0f;

            t = GetYDistanceFromPlane(pl2.f, skelRoot->move);

            if (!(5.0f < (t < 0.0f ? -t : t))) {
                if (debug_mot_slope_interp == 0) {
                    if (10.0f < (dy < 0.0f ? -dy : dy)) {
                        if (dy < 0.0f) {
                            dy = -10.0f;
                        } else {
                            dy = 10.0f;
                        }
                    }
                } else {
                    dy = dy * ((float)debug_mot_slope_interp * 0.1f);
                }
            }
        }
        if (skelRoot->liftOn != 0) {
            skelRoot->lifting = 1;
            if (skelNode[skelRoot->stepNode].kind == 0x30) {
                if (0.0f < d1) {
                    skelRoot->lift[0] = 0.0f;
                    skelRoot->lift[1] = -d1;
                } else {
                    skelRoot->lift[0] = d1;
                    skelRoot->lift[1] = 0.0f;
                }
            } else {
                if (0.0f < d1) {
                    skelRoot->lift[0] = -d1;
                    skelRoot->lift[1] = 0.0f;
                } else {
                    skelRoot->lift[0] = 0.0f;
                    skelRoot->lift[1] = d1;
                }
            }
            if (skelRoot->lift[0] < -(skelNode->pos[1] * skelScale * 0.5f)) {
                skelRoot->lift[0] = -(skelNode->pos[1] * skelScale * 0.5f);
            }
            if (skelRoot->lift[1] < -(skelNode->pos[1] * skelScale * 0.5f)) {
                skelRoot->lift[1] = -(skelNode->pos[1] * skelScale * 0.5f);
            }
        }
    }
    skelRoot->pos[1] = skelRoot->pos[1] - dy;
}

/* shared by rootUpdateFly and rootUpdateEnemyFly */
static inline int getFlyFieldCollision(ClipWork *w) /* derived name */
{
    float y;

    y = skelMotDef->landFrame;
    if (y < -0.5f) {
        return 0;
    }
    w->radius = 0.0f;
    CopyVector(w, skelRoot->pos);
    w->pt[0][1] = skelRoot->clipFrom[1];
    CopyVector(w->pt[1], skelRoot->pos);
    w->pt[1][1] = w->pt[1][1] + 10000.0f;
    ClipFloor(w);
    return CheckFieldContact(w, (GObj *)skelGObj, skelRoot->pos, skelRoot->projHeight);
}

/* landingFieldAction's helper */
static inline void adjustRootByLandingPlane(Vec16 *plane, float rate) /* derived name */
{
    Vec16 push;
    Vec16 half;
    Vec16 pl;
    float d;

    CopyVector(&pl, plane);
    pl.f[3] = 0.0f;
    d = plane_distance(skelRoot->move, &pl);
    if (d < 0.0f) {
        _ScaleVector(&push, &pl, d);
        push.f[3] = 0.0f;
        _SubVector(&half, skelRoot->move, &push);
        half.f[3] = 0.0f;
        half.f[1] = 0.0f;
        _ScaleVectorXYZ(&half, &half, rate);
        _SubVectorXYZ(skelRoot->last, skelRoot->pos, &half);
    }
}

void landingFieldAction(ClipWork *w)
{
    float h;

    skelRoot->pos[1] = w->pt[2][1] - skelRoot->projHeight;
    adjustRootByLandingPlane(&w->normal, 0.5f);
    h = skelRoot->footPos[1];
    CopyVector(&skelRoot->plane, &w->normal);
    skelMotCtrl->fallHeight = GetYProjectionOfPlane(w->normal.f, skelRoot->pos) - h;
    CopyVector(skelRoot->footPos, skelRoot->pos);
    skelRoot->footPos[1] = w->pt[2][1];
    skelMotCtrl->flags |= 8;
}

/* shared by rootUpdateFly and rootUpdateEnemyFly */
static inline void markPoolContact(ClipWork *w) /* derived name */
{
    skelMotCtrl->flags |= 0x400;
    setPoolHeightOfFloor(w);
}

static void floorFitting(int kind)
{
    Vec16 plane;
    Vec16 dir;
    float mtx[16];
    short ang;

    if (kind == 0x11 || kind == 0x14) {
        plane = skelRoot->plane;
        GetMatrixFromQuaternion(mtx, skelRoot->quat);
        _ApplyMatrix(&dir, mtx, ZUnitVector);
        plane.f[3] = 0.0f;
        floorSlope[1] = GetYDistanceFromPlane(plane.f, dir.f);
        floorSlope[2] = 1.0f;
        _NormalizeVector(floorSlope, floorSlope);
        ang = GetTableArcTan2(floorSlope[1], floorSlope[2]);
        RotQuaternionX(skelRoot->quat, ang);
        skelRoot->lifting = 0;
    }
}

/* rootUpdateXZ's helper */
static inline int getFloorAttributeUnderNode(void) /* derived name */
{
    ClipWork w;
    int node;

    memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    node = GetSkeltonFocusNode((GObj *)skelGObj, 51);
    _AddVectorXYZ(&w, skelRoot->pos, nodePos2 + node * 16);
    CopyVector(w.pt[1], &w);
    w.pt[0][1] = w.pt[0][1] - 50.0f;
    w.pt[1][1] = w.pt[1][1] + 50.0f;
    ClipFloor(&w);
    if (w.floor.elem != 0) {
        return GetFloorAttribute(&w);
    }
    return -1;
}

/* the wall pass after a horizontal move, used three times by rootUpdateXZ;
 * rootUpdateXZ_MotPos writes it out */
#define WALL_COLLISION_AND_WAVE_FORCE() /* derived name */                                         \
    if (wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, skelRoot->projHeight + -30.0f) ==  \
        0) {                                                                                       \
        if (skelMotCtrl->wallReact != 0) {                                                         \
            wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, 5.0f);                         \
        }                                                                                          \
    }                                                                                              \
    if (skelMotCtrl->wallReact != 0) {                                                             \
        waveForceReaction(skelRoot, skelRoot->last, 0.0f);                                         \
    }

static ObjNode rootUpdateXZ(int kind, int no)
{
    Vec16 mv;
    Vec16 step;
    ObjNode ret = {0, -1};
    float mtx[16];

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&mv, mtx, rootMove);
    sceVu0ApplyMatrix(&step, mtx, skelRoot->stepMove);
    AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->trans);
    AddVectorXYZ(skelRoot->last, skelRoot->last, skelRoot->trans);

    if (no != -1) {
        Vec16 focus;

        skelRoot->stepNode = no;
        CopyVector(&focus, nodePos + no * 16);
        switch (skelNode[no].kind) {
        case 47:
            skelRoot->stepNode = GetSkeltonFocusNode((GObj *)skelGObj, 48);
            break;
        case 51:
            skelRoot->stepNode = GetSkeltonFocusNode((GObj *)skelGObj, 52);
            break;
        }
        if (skelRoot->standNode == no) {
            Vec16 dir;
            float keep = 1.0f;
            float give = 0.0f;

            /* the slope work is a block of its own, closing with the slope
             * test; dir sits one level out */
            {
                Vec16 nodePos, pl, pos, fall, n;
                float rate, d;

                MatrixDrive_PushMatrix();
                sceVu0UnitMatrix(MatrixDrive_GetMatrix());
                MultiMatrixByQuaternion(skelRoot->quat);
                sceVu0ApplyMatrix(&nodePos, MatrixDrive_GetMatrix(), skelRoot->focusLocal);
                SubVectorXYZ(&dir, &nodePos, &focus);
                MatrixDrive_PopMatrix();
                CopyVector(&pl, &skelRoot->plane);
                pl.f[3] = 0.0f;
                if (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0xF00000) != 0) {
                    memset(&fall, 0, 16);
                    fall.f[1] = 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) *
                                0.5f *
                                (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
                    pos = fall;
                    rate = 0.0f;
                    d = GetDistanceFromPlane(&pl, &pos);
                    sceVu0ScaleVector(&n, &pl, -d);
                    sceVu0SubVector(&fall, &n, &pos);
                    AddVectorXYZ(skelRoot->move, skelRoot->move, &fall);
                    if (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0x100000) != 0) {
                        rate = 0.5f;
                    }
                    if (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0x200000) != 0) {
                        rate = 0.7f;
                    }
                    if (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0x400000) != 0) {
                        rate = 0.8f;
                    }
                    if (skelMotCtrl->slipOn != 0 && (skelMotCtrl->floorAttr & 0x800000) != 0) {
                        rate = 0.95f;
                    }
                    keep = 1.0f - rate;
                    give = rate;
                    skelMotCtrl->slipFlags = skelMotCtrl->floorAttr;
                }
            }
            sceVu0ScaleVector(&dir, &dir, keep);
            sceVu0ScaleVector(skelRoot->move, skelRoot->move, give);
            AddVectorXYZ(skelRoot->move, skelRoot->move, &dir);
            {
                Vec16 t;

                CopyVector(&t, skelRoot->move);
                t.f[1] = 0.0f;
                AddVectorXYZ(skelRoot->pos, skelRoot->pos, &t);
            }
        } else {
            AddVectorXYZ(skelRoot->pos, skelRoot->pos, &step);
            CopyVector(skelRoot->move, &step);
        }
        WALL_COLLISION_AND_WAVE_FORCE();
        /* the work block closes after setIKAndAdjustRootHeight */
        {
            ClipWork w;
            int attr;

            if (getFieldCollision(&w) != 0) {
                CopyVector(&skelRoot->plane, &w.normal);
                skelMotCtrl->floorAttr = attr = GetFloorAttribute(&w);
                if (skelRoot->standNode == -1) {
                    skelMotCtrl->fallHeight = 0.0f;
                    adjustRootByLandingPlane(
                        &skelRoot->plane,
                        (skelMotCtrl->slipOn != 0 && (attr & 0xF00000) != 0) ? 0.5f : 1.0f);
                }
                ret = w.floor.o;
            } else {
                skelMotCtrl->flags |= 4;
            }
            setIKAndAdjustRootHeight((char *)&mv);
        }
        checkCliffAndWallStateOfLastPlane();
        MatrixDrive_PushMatrix();
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MultiMatrixByQuaternion(skelRoot->quat);
        CopyVector((void *)(MatrixDrive_GetMatrix()[3]), ZeroPoint);
        sceVu0TransposeMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix());
        sceVu0ApplyMatrix(skelRoot->focusLocal, MatrixDrive_GetMatrix(), &focus);
        MatrixDrive_PopMatrix();
        skelRoot->height = GetYDistanceFromPlane(skelRoot->plane.f, skelRoot->pos);
        {
            Vec16 sum;
            float proj, gap;

            AddVectorXYZ(&sum, nodePos2 + no * 16, skelRoot->pos);
            sceVu0CopyVector(skelRoot->footPos, &sum);
            skelRoot->footPos[3] = 1.0f;
            proj = GetYProjectionOfPlane(skelRoot->plane.f, sum.f);
            gap = proj - skelRoot->footPos[1];
            if ((gap < 0.0f ? -gap : gap) < skelNode->pos[1] * skelScale) {
                skelRoot->footPos[1] = proj;
            }
        }
        CopyVector(skelRoot->focusPos, &focus);
    } else {
        ClipWork w;

        if (skelRoot->gravity != 0) {
            skelRoot->move[1] += 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) *
                                 0.5f *
                                 (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1])) *
                                 (systemStatus[0] != 0 ? 0.7f : 1.0f);
            AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->move);
            WALL_COLLISION_AND_WAVE_FORCE();
        } else {
            if (skelRoot->standNode != no) {
                CopyVector(skelRoot->move, &step);
            }
            skelRoot->move[1] = 0.0f;
            AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->move);
            WALL_COLLISION_AND_WAVE_FORCE();
            if (skelRoot->slopeIK != 0) {
                if (getFieldCollision(&w) != 0) {
                    CopyVector(&skelRoot->plane, &w.normal);
                    ret = w.floor.o;
                    setIKAndAdjustRootHeight((char *)&mv);
                } else {
                    skelMotCtrl->flags |= 4;
                }
            } else {
                skelRoot->pos[1] = mv.f[1] + skelRoot->footPos[1];
            }
        }
        checkCliffAndWallStateAtJump();
        switch (getFlyFieldCollision(&w)) {
        case 1:
            landingFieldAction(&w);
            break;
        case 2:
            markPoolContact(&w);
            break;
        default:
            /* the unknown-reaction report ("whoever sees this message, report
               it to Sugiyama at once"), switched off */
            if (0) {
                debug_StdPrintfDummy(
                    "このメッセージを見た人は直ちにすぎやまに報告してくれー:NOWALLHITPROC\n");
            }
            break;
        }
    }
    if (skelMotCtrl->cliffBack != 0) {
        if (skelMotCtrl->wallDist < 20.0f && -10.0f < skelMotCtrl->wallFloorHeight) {
            skelMotCtrl->flags &= ~4;
        }
    }
    if (kind == 20) {
        int attr = getFloorAttributeUnderNode();

        if (attr != -1) {
            skelMotCtrl->floorAttr = attr;
        }
    }
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->last, skelRoot->pos);
    skelRoot->standNode = no;
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->delta, rootDelta);
    return ret;
}

static ObjNode rootUpdateXZ_MotPos(int kind, int no)
{
    ObjNode ret = {0, -1};
    ClipWork w;
    Vec16 mv;
    Vec16 step;
    float mtx[16];
    float lim;
    int r;

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&mv, mtx, rootMove);
    sceVu0ApplyMatrix(&step, mtx, rootStep);
    AddVectorXYZ(skelRoot->pos, skelRoot->pos, &step);

    if (no != -1) {
        CopyVector(skelRoot->delta, rootDelta);
    } else {
        if (skelMotCtrl->justShifted != 0) {
            if (systemStatus[0] == 1) {
                AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->move);
            }
        }
        if (skelMotCtrl->motion == 21) {
            lim = -(60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 10.0f);
            if (skelRoot->delta[1] < lim) {
                skelRoot->delta[1] = lim;
                /* the release build's printf is debug_StdPrintfDummy */
                debug_StdPrintfDummy("inertia cancel.\n");
            }
        }
        AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->delta);
    }

    if (wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, skelRoot->projHeight + -30.0f) ==
        0) {
        if (skelMotCtrl->wallReact != 0) {
            wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, 5.0f);
        }
    }

    if (skelMotCtrl->floorFit != 0) {
        if (no != -1 || skelRoot->slopeIK != 0) {
            if (getFieldCollision(&w) != 0) {
                CopyVector(&skelRoot->plane, &w.normal);
                skelRoot->pos[1] = w.pt[2][1] + mv.f[1];
                if (no != -1 || skelMotCtrl->motion == 75 || skelMotCtrl->motion == 599 ||
                    skelMotCtrl->motion == 58) {
                    skelMotCtrl->floorAttr = GetFloorAttribute(&w);
                    ret = w.floor.o;
                } else {
                    skelMotCtrl->floorAttr = 0;
                }
            } else {
                skelMotCtrl->flags = skelMotCtrl->flags | 4;
            }
        } else {
            getFieldCollision(&w);
        }
        r = getFlyFieldCollision(&w);
        switch (r) {
        case 1:
            if (skelMotDef->landFrame > skelMotCtrl->animFrame) {
                skelRoot->pos[1] = w.pt[2][1] + mv.f[1];
            } else {
                landingFieldAction(&w);
            }
            break;
        case 2:
            markPoolContact(&w);
            break;
        }
    }

    checkCliffAndWallStateAtJump();
    floorFitting(kind);
    skelRoot->height = rootMove[1];
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->last, skelRoot->pos);
    skelRoot->standNode = no;
    return ret;
}

static int adjustSideWall(ClipWork *w, int nearTest, Vec16 *wallPlane)
{
    WallCfg cfg = {w->wall.o, w->wall.elem};
    Vec16 pl0;
    Vec16 pl1;
    float pts[16];
    Vec16 push;
    float d;
    float t;

    GetPureVerticalPlane(&pl0, &pl1, pts, &cfg, 0);
    d = (GetDistanceFromPlane(&pl0, w->pt[2]) < 0.0f ? -GetDistanceFromPlane(&pl0, w->pt[2])
                                                     : GetDistanceFromPlane(&pl0, w->pt[2])) -
        30.0f;
    if (nearTest != 0) {
        if ((d < 0.0f ? -d : d) < 30.0f) {
            return 1;
        }
    }
    t = sceVu0InnerProduct(wallPlane, &pl1);
    if ((t < 0.0f ? -t : t) < 0.8f) {
        CopyVector(&push, &pl1);
        push.f[3] = 0.0f;
        sceVu0ScaleVectorXYZ(&push, &push, GetDistanceFromPlane(&pl1, skelRoot->pos) - 30.0f);
        SubVectorXYZ(skelRoot->pos, skelRoot->pos, &push);
    }
    return 0;
}

static int checkSideWall(float *probe, float *mtx, ClipWork *p, ClipWork *base, Vec16 *wallPlane)
{
    char buf[16];
    int flag;
    int mag;
    int mask;

    p->radius = 0.0f;
    CopyVector(p, base);
    sceVu0ApplyMatrix(p->pt[1], mtx, probe);
    ClipWall(p);

    if (p->wall.elem != 0) {
        goto disp_one;
    }
    if (skelRoot->fuchiMode != 0) {
        ClipWallFuchiHangWalkStop(p);
        if (p->wall.elem != 0) {
            flag = 0;
            mag = GetWallAttribute(p);
            switch (skelRoot->fuchiMode) {
            default:
            case 1:
                mask = 0x20000000;
                break;
            case 2:
                mask = 0x10000000;
                break;
            }
            if (mag & mask) {
                flag = 1;
                if (mag & 0x0FFFFFFF) {
                    return -1;
                }
            }
            if (flag != 0) {
                return adjustSideWall(p, 0, wallPlane);
            }
            goto side_probe;
        }
    }
side_probe:
    _ApplyMatrix(buf, mtx, sideWallProbe);
    CopyVector(p, (void *)p->pt[1]);
    _AddVectorXYZ(p->pt[1], p, buf);
    ClipWall(p);
    if (p->wall.elem != 0) {
        goto disp_one;
    }
    CopyVector(p, (void *)p->pt[1]);
    CopyVector((void *)p->pt[1], base->pt[1]);
    ClipWall(p);
    if (p->wall.elem != 0) {
    disp_one:
        return adjustSideWall(p, 1, wallPlane);
    }
    return 0;
}

static void forceFallDown(void)
{
    iosOmSendMail((GObj *)skelGObj, 0x18, (void *)skelGObj);
    CopyVector(skelRoot->last, skelRoot->pos);
}

static void forceReset(void)
{
    iosOmSendMail((GObj *)skelGObj, 0x103, (void *)skelGObj);
    CopyVector(skelRoot->last, (char *)skelRoot);
}

static void checkWallUpperWall(void)
{
    float (*v)[4];
    MatrixDrive_PushMatrix();
    v = MatrixDrive_GetMatrix();
    sceVu0UnitMatrix(v);
    {
        register float *p = skelRoot->pos;
        MatrixDrive_TransMatrix(p[0], p[1] - rootMove[1], p[2]);
    }
    MultiMatrixByQuaternion(skelRoot->quat);
    checkUpperWallState();
    MatrixDrive_PopMatrix();
}

/* rootUpdateHang's helper */
static inline void dropWhenFloorTooFar(void) /* derived name */
{
    ClipWork w;

    if (0.0f <= plane_distance(skelRoot->pos, &skelRoot->plane)) {
        return;
    }
    CopyVector(&w, skelRoot->pos);
    CopyVector(w.pt[1], &w);
    w.pt[1][1] = w.pt[1][1] + 50.0f;
    ClipFloor(&w);
    if (w.floor.elem != 0) {
        if ((GetFloorAttribute(&w) & 0x800000) == 0 && w.pt[2][1] - w.pt[0][1] < 50.0f) {
            forceFallDown();
        }
    }
}

static void calcWallFittingGeometry(void)
{
    Vec16 v;
    float mtx[16];
    Vec16 base;
    Vec16 rot;
    short ang;

    GetWallGlobalInfo(0, &v, skelRoot->wall.elem,
                      (char *)GOBJ_SUB(skelRoot->wall.o.obj)->nodeMtx + skelRoot->wall.o.node * 64);
    MatrixDrive_SetTransposeMatrix(mtx, MatrixDrive_GetMatrix());
    _ApplyMatrix(&v, mtx, &v);
    v.f[0] = 0.0f;
    _NormalizeVector(&v, &v);
    ang = (short)(atan2f(v.f[1], -v.f[2]) * 10430.378f);
    RotQuaternionX(skelRoot->quat, ang);
    MatrixDrive_PushMatrix();
    _ApplyMatrix(&base, MatrixDrive_GetMatrix(), rootMove);
    MatrixDrive_RotMatrixX(ang);
    _ApplyMatrix(&rot, MatrixDrive_GetMatrix(), rootMove);
    _SubVector(skelRoot->trans, &rot, &base);
    skelRoot->trans[3] = 0.0f;
    MatrixDrive_PopMatrix();
    skelRoot->height = 0.0f;
}

static ObjNode rootUpdateHang(int kind, int hand1Pt, int hand0Pt)
{
    Vec16 mv;
    Vec16 up;
    float mtx[16];
    Vec16 pl;
    Vec16 pl2;
    ClipWork w;
    float mtx2[16];
    Vec16 pos;
    int hit;

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&up, mtx, rootMove);
    sceVu0ApplyMatrix(&mv, mtx, rootStep);
    AddVectorXYZ(skelRoot->pos, skelRoot->pos, &mv);
    skelRoot->height = rootMove[1];
    hit = 0;
    CopyVector(&pos, skelRoot->pos);
    pos.f[1] = pos.f[1] - (rootMove[1] - 30.0f);
    MatrixDrive_PushMatrix();
    GetMatrixFromQuaternionPos((void *)MatrixDrive_GetMatrix(), skelRoot->quat, &pos);
    MatrixDrive_TransMatrix(0.0f, 0.0f, -rootMove[2] - 10.0f);
    CopyMatrix(mtx2, (void *)MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
    sceVu0ApplyMatrix(&w, mtx2, hangBack);
    sceVu0ApplyMatrix(w.pt[1], mtx2, hangFront);
    w.radius = 0.0f;

    /* the work records, in a block of their own */
    {
        int n;
        int attr;

        if (skelMotCtrl->updateModeChanged == 0) {
            ClipWall(&w);
            if (w.wall.elem != 0) {
                WallCfg rec = {w.wall.o, w.wall.elem};

                n = GetPureVerticalPlaneOfCurrentPosition(&skelRoot->plane, &pl, 0, &rec, 0,
                                                          skelRoot->pos);
                if (-0.25f < skelRoot->plane.f[1]) {
                    /* EUC-JP: "the wall angle is steep, so fall" */
                    debug_StdPrintfDummy("壁の角度が急なので落下\n");
                    forceFallDown();
                    goto out;
                }
                CopyVector(&pl2, &pl);
                if (skelRoot->wall.o.obj == rec.o.obj && skelRoot->wall.o.node == rec.o.node &&
                    skelRoot->wall.elem == rec.elem) {
                    hit = skelRoot->wallCount == n;
                }
                skelRoot->wall = rec;
                skelRoot->wallCount = n;
                skelMotCtrl->pureWallAttr = skelMotCtrl->wallAttr = attr = GetWallAttribute(&w);
                if (attr == 768) {
                    forceFallDown();
                    /* EUC-JP: "fall by the wall attribute" */
                    debug_StdPrintfDummy("壁属性により落下\n");
                    goto out;
                }
            } else {
                /* EUC-JP: "no wall, so fall" */
                debug_StdPrintfDummy("壁がなくて落下\n");
                forceFallDown();
                goto out;
            }
        } else {
            skelMotCtrl->updateModeChanged = 0;
            GetPureVerticalPlane(&skelRoot->plane, &pl, 0, &skelRoot->wall, 0);
            hit = 1;
            CopyVector(&pl2, &pl);
        }

        {
            Vec16 v;
            Vec16 sc;
            ClipWork sw1;
            ClipWork sw2;
            Vec16 dv;
            int r1;
            int r2;
            float d;
            float dd;

            pl2.f[3] = 0.0f;
            CopyVector(&v, skelRoot->pos);
            v.f[1] = v.f[1] - rootMove[1];
            d = GetDistanceFromPlane(&skelRoot->plane, &v);
            if (hit == 0) {
                if (d < 0.0f ? 30.0f < -d : 30.0f < d) {
                    /* EUC-JP: "no hold point on the wall, so fall (step: %f)" */
                    debug_StdPrintfDummy("壁のつかまり先がなくて落下 (段差:%f)\n", fptodp(d));
                    forceFallDown();
                    goto out;
                }
            }
            dd = -d;
            sceVu0ScaleVector(&sc, &skelRoot->plane, dd);
            AddVectorXYZ(&v, &v, &sc);
            d = GetDistanceFromPlane(&pl, &v);
            sceVu0ScaleVector(&sc, &pl, -d - rootMove[2]);
            AddVectorXYZ(&v, &v, &sc);
            CopyVector(skelRoot->pos, &v);
            CopyVector(skelRoot->footPos, skelRoot->pos);
            skelRoot->pos[1] = skelRoot->pos[1] + rootMove[1];

            r1 = checkSideWall(hangLeft, mtx2, &sw1, &w, &pl2);
            r2 = checkSideWall(hangRight, mtx2, &sw2, &w, &pl2);
            if (r1 == -1 || r2 == -1) {
                /* EUC-JP: "hit a forced-fall wall, so fall" */
                debug_StdPrintfDummy("強制落下の壁に当たったので落下\n");
                forceFallDown();
                goto out;
            }
            if (r1 != 0 && r2 != 0) {
                sceVu0SubVector(&dv, sw2.pt[2], sw1.pt[2]);
                sceVu0OuterProduct(skelMotCtrl->dir, &dv, YUnitVector);
                skelMotCtrl->dir[1] = 0.0f;
                sceVu0Normalize(skelMotCtrl->dir, skelMotCtrl->dir);
            } else {
                CopyVector(skelMotCtrl->dir, &pl2);
                skelMotCtrl->dir[0] = -skelMotCtrl->dir[0];
                skelMotCtrl->dir[2] = -skelMotCtrl->dir[2];
            }
        }
    }

    checkWallUpperWall();
    dropWhenFloorTooFar();
    wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, 0.0f);

out:
    CopyVector(skelRoot->trans, ZeroPoint);
    if (kind == 15) {
        calcWallFittingGeometry();
    }
    SubVectorXYZ(skelRoot->up, skelRoot->pos, &up);
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->delta, rootDelta);
    skelRoot->standNode = -1;
    return skelRoot->wall.o;
}

static ObjNode rootUpdateSwim(void)
{
    ClipWork w;
    Vec16 v;
    ObjNode shift = {0, -1};
    float mtx[16];
    int node;

    node = GetSkeltonFocusNode((GObj *)skelGObj, 34);
    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&v, mtx, rootStep);
    v.f[0] = v.f[0] * 0.2f;
    v.f[1] = v.f[1] * 0.2f;
    v.f[2] = v.f[2] * 0.2f;
    sceVu0AddVector(skelRoot->move, skelRoot->move, &v);
    GetLowerPlaneCollision(&w, skelRoot->pos);
    if (GetWaterReaction(
            0, 0, &w, skelRoot->pos, skelRoot->move, skelRoot->pos[1] - 80.0f,
            skelRoot->pos[1] + *(float *)(nodePos + node * 16 + 4), skelRoot->pos[1] + 80.0f, 0.9f,
            60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]) * -0.1f *
                (60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]))) != 0) {
        float depth = 10.0f;

        if (w.pt[2][1] - depth < skelRoot->pos[1]) {
            skelRoot->pos[1] = w.pt[2][1] - depth;
            skelRoot->last[1] = w.pt[2][1] - depth;
        }
        setPoolHeightOfFloor(&w);
    } else {
        skelMotCtrl->flags |= 0x800;
        /* the release build's printf is debug_StdPrintfDummy */
        /* EUC-JP: "there is no DWATER water bottom below.  If this message keeps coming, it has not left the swim state" */
        debug_StdPrintfDummy(
            "下にDWATERの水底がないです。このメッセージが連続で\n出てるなら、泳ぎから遷移していない状態です\n\n");
    }
    if (w.floor.elem != 0) {
        skelMotCtrl->floorAttr = GetFloorAttribute(&w);
        CopyVector(&skelRoot->plane, &w.normal);
    }
    wallCollisionAndFloorFit(skelRoot->pos, skelRoot->last, 0.0f);
    if (skelMotCtrl->wallReact != 0) {
        waveForceReaction(skelRoot, skelRoot->last, 0.0f);
    }
    checkCliffAndWallStateAtJump();
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->footPos, skelRoot->pos);
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->delta, rootDelta);
    skelRoot->height = 0.0f;
    skelRoot->standNode = -1;
    return shift;
}

/* shared by rootUpdateStepSolution and rootUpdateY */
static inline void clipWallAhead(ClipWork *w) /* derived name */
{
    float mtx[16];

    GetMatrixFromQuaternionPos(mtx, skelRoot->quat, skelRoot->pos);
    sceVu0ApplyMatrix(w, mtx, clipWallBack);
    sceVu0ApplyMatrix(w->pt[1], mtx, clipWallFront);
    w->radius = 0.0f;
    ClipWall(w);
}

/* avoidSideAdjustWall's two helpers */
static inline int clipSideWallAndHit(ClipWork *w) /* derived name */
{
    int hit = 1;

    ClipWallAdjustPos(w);

    return w->wall.elem != 0 ? hit : 0;
}

static inline void pushRootAlongSideWall(ClipWork *w) /* derived name */
{
    Vec16 t;

    _ScaleVectorXYZ(&t, &w->normal, 40.0f);
    _AddVectorXYZ(&t, (w->pt[2]), &t);
    _SubVectorXYZ(&t, &t, skelRoot->pos);
    _ScaleVectorXYZ(&t, &t, 0.07f);
    _AddVectorXYZ(skelRoot->pos, skelRoot->pos, &t);
}

static int avoidSideAdjustWall(void)
{
    float mtx[16];
    /* a 0xD0-byte block, cleared to the clip request's 0xC0 */
#ifdef ICO_HOST
    ClipWork wk;
    char *w = (char *)&wk;

    memset(w, 0, sizeof(ClipWork));
#else
    char w[208];

    memset(w, 0, 0xC0);
#endif
    GetMatrixFromQuaternionPos(mtx, skelRoot->quat, skelRoot->pos);
    _ApplyMatrix(w, mtx, avoidLeft);
    _ApplyMatrix(w + 0x10, mtx, avoidRight);
    if (clipSideWallAndHit((ClipWork *)w) == 0) {
        _ApplyMatrix(w, mtx, avoidRight);
        _ApplyMatrix(w + 0x10, mtx, avoidLeft);
        if (clipSideWallAndHit((ClipWork *)w) == 0) {
            return 0;
        }
    }
    pushRootAlongSideWall((ClipWork *)w);
    return 1;
}

static ObjNode rootUpdateStepSolution(int kind)
{
    Vec16 mv;
    Vec16 up;
    /* every arm of the switch below reassigns it */
    WallCfg cfg = {{0, -1}, 0};
    float mtx[16];
    Vec16 d1;
    Vec16 d2;
    float pl[4];
    float pl2[4];
    Vec16 corner[4];
    Vec16 avg;
    Vec16 pos;
    Vec16 adj;
    ClipWork w;
    float dir;
    float d;
    int node;
    int i;

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&mv, mtx, rootStep);
    sceVu0ApplyMatrix(&up, mtx, rootMove);
    if (stepFocusNode != -1) {
        node = GetSkeltonFocusNode((GObj *)skelGObj, stepFocusNode);
        _SubVectorXYZ(&d2, (char *)GOBJ_SUB(skelGObj)->nodeMtx + node * 64 + 48,
                      (char *)GOBJ_SUB(skelGObj)->nodeMtx + 48);
        _SubVectorXYZ(&d1, nodePos2 + node * 16, &d2);
        _SubVectorXYZ(skelRoot->pos, skelRoot->pos, &d1);
    } else {
        AddVectorXYZ(skelRoot->pos, skelRoot->pos, &mv);
    }
    skelRoot->height = rootMove[1];

    switch (kind) {
    case 9:
        cfg = skelRoot->cliffWall;
        dir = 1.0f;
        GetPureVerticalPlane(&skelRoot->plane, pl, (float *)corner, &cfg, 0);
        break;
    case 13:
        cfg = skelRoot->cliffWall;
        dir = 1.0f;
        GetPureVerticalPlane(&skelRoot->plane, pl, (float *)corner, &cfg, 1);
        break;
    case 7:
    case 16:
    default:
        cfg = skelRoot->wall;
        dir = -1.0f;
        GetPureVerticalPlane(&skelRoot->plane, pl, (float *)corner, &cfg, 0);
        break;
    case 8:
        cfg = skelRoot->wall;
        dir = -1.0f;
        GetPureVerticalPlane(&skelRoot->plane, pl, (float *)corner, &cfg, 1);
        break;
    }

    CopyVector(pl2, pl);
    pl2[3] = 0.0f;
    if (skelRoot->avgWallPlane != 0) {
        memset(&avg, 0, 16);
        avg.f[3] = 1.0f;
        for (i = 0; i < 4; i++) {
            AddVectorXYZ(&avg, &avg, &corner[i]);
        }
        sceVu0ScaleVector(&avg, &avg, 0.25f);
        skelRoot->pos[0] = skelRoot->pos[0] + (avg.f[0] - skelRoot->pos[0]) * 0.2f;
        skelRoot->pos[2] = skelRoot->pos[2] + (avg.f[2] - skelRoot->pos[2]) * 0.2f;
    }

    CopyVector(&pos, skelRoot->pos);
    pos.f[1] = pos.f[1] - rootMove[1];
    d = GetDistanceFromPlane(&skelRoot->plane, &pos);
    sceVu0ScaleVector(&adj, &skelRoot->plane, -d);
    AddVectorXYZ(&pos, &pos, &adj);
    d = GetDistanceFromPlane(pl, &pos);
    sceVu0ScaleVector(&adj, pl, -d + rootMove[2] * dir);
    AddVectorXYZ(&pos, &pos, &adj);
    CopyVector(skelRoot->pos, &pos);
    skelRoot->pos[1] = skelRoot->pos[1] + rootMove[1];
    sceVu0ScaleVectorXYZ(skelMotCtrl->dir, pl2, dir);

    CopyVector(&w, skelRoot->pos);
    CopyVector(w.pt[1], &w);
    w.pt[1][1] = w.pt[1][1] + (skelRoot->projHeight + 10.0f);
    ClipFloor(&w);
    if (w.floor.elem != 0) {
        skelMotCtrl->floorAttr = GetFloorAttribute(&w) & 0xF0;
    }

    if (skelRoot->fuchiMode != 0) {
        avoidSideAdjustWall();
    }

    if (kind >= 7 && (kind < 9 || kind == 16)) {
        if (skelMotCtrl->wallReact != 0) {
            if (skelMotDef->wallFeedback == 0) {
                clipStepWallAndReact(skelRoot, skelRoot->last, skelRoot->radius, &cfg);
            }
            if (upperFieldCheck(skelRoot->pos, -50.0f) != 0) {
                forceFallDown();
            }
        }
        checkWallUpperWall();
        if (skelMotCtrl->motion == 85) {
            MatrixDrive_PushMatrix();
            _UnitMatrix(MatrixDrive_GetMatrix());
            MatrixDrive_TransMatrix(skelRoot->pos[0],
                                    GetYProjectionOfPlane(skelRoot->plane.f, skelRoot->pos),
                                    skelRoot->pos[2]);
            MultiMatrixByQuaternion(skelRoot->quat);
            checkCliffState(1);
            MatrixDrive_PopMatrix();
        }
    } else {
        clipStepWallAndReact(skelRoot, skelRoot->last, skelRoot->radius, &cfg);
    }

    if (clipFuchiWallAndReact(skelRoot, skelRoot->last, skelRoot->fuchiMode, skelRoot->radius) !=
        0) {
        /* EUC-JP: "hit an extension wall" */
        debug_StdPrintfDummy("拡張壁に当たった\n");
    }
    if (skelRoot->fieldWall != 0) {
        if (clipFieldWallAndReset(skelRoot, skelRoot->last) != 0) {
            forceReset();
        }
    }

    CopyVector(skelRoot->trans, ZeroPoint);
    switch (kind) {
    case 16:
        calcWallFittingGeometry();
        /* falls through to the kind-7/8 wall probe */
    case 7:
    case 8: {
        ClipWork cw;

        clipWallAhead(&cw);
        {
            WallCfg rec = {cw.wall.o, cw.wall.elem};
            Vec16 wp;

            if (cw.wall.elem != 0) {
                skelRoot->aheadWall = rec;
                GetPureVerticalPlane(&wp, 0, 0, &rec, 0);
                skelMotCtrl->cliffDepth = GetDistanceFromPlane(&wp, skelRoot->pos) - rootMove[1];
            }
        }
    } break;
    case 9:
        dropWhenFloorTooFar();
        break;
    }

    SubVectorXYZ(skelRoot->up, skelRoot->pos, &up);
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->move, ZeroVector);
    CopyVector(skelRoot->footPos, skelRoot->pos);
    skelRoot->footPos[1] = skelRoot->footPos[1] - rootMove[1];
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->delta, rootDelta);
    skelRoot->standNode = -1;
    skelRoot->wall = cfg;
    return cfg.o;
}

static ObjNode rootUpdateTrueMotion(int kind)
{
    float mtx[16];
    Vec16 v;
    int noMove = kind;

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    if (noMove == 0) {
        sceVu0ApplyMatrix(&v, mtx, rootMove);
        v.f[1] = v.f[1] + skelNode->pos[1];
        v.f[0] = v.f[2] = 0.0f;
        CopyVector(skelRoot->trans, &v);
    } else {
        CopyVector(skelRoot->trans, ZeroPoint);
    }
    skelRoot->standNode = -1;
    skelRoot->height = 0.0f;
    CopyVector(skelRoot->delta, rootDelta);
    return InitialObjPointer;
}

static ObjNode rootUpdateDirectPlayForStream(void)
{
    float *p;

    SetIdentityQuaternion(skelRoot->quat);
    RotQuaternionY(skelRoot->quat, -32768);
    CopyVector(skelRoot->pos, rootMove);
    p = skelRoot->pos;
    p[0] = -p[0];
    p[2] = -p[2];
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->move, ZeroVector);
    CopyVector(skelRoot->trans, ZeroVector);
    skelRoot->standNode = -1;
    skelRoot->height = 0.0f;
    AdjustMotionHeightToNearestField((GObj *)skelGObj);
    CopyVector(skelRoot->delta, rootDelta);
    return InitialObjPointer;
}

static ObjNode rootUpdateDirectPlay(int mode)
{
    int kind = mode;
    float *p;
    float s;

    SetIdentityQuaternion(skelRoot->quat);
    RotQuaternionY(skelRoot->quat, -32768);
    CopyVector(skelRoot->pos, rootMove);
    p = skelRoot->pos;
    p[0] = p[0] - skelMotDef->rootOffset[0];
    p[1] = p[1] - skelMotDef->rootOffset[1];
    p[2] = p[2] + skelMotDef->rootOffset[2];
    if (kind == 14) {
        s = 1.0f / skelScale;
        p[0] = p[0] * -s;
        p[1] = p[1] * s;
        p[2] = p[2] * -s;
    } else {
        p[0] = -p[0];
        p[2] = -p[2];
    }
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->move, ZeroVector);
    CopyVector(skelRoot->trans, ZeroVector);
    skelRoot->standNode = -1;
    skelRoot->height = 0.0f;
    AdjustMotionHeightToNearestField((GObj *)skelGObj);
    CopyVector(skelRoot->delta, rootDelta);
    return InitialObjPointer;
}

/* a nested function in rootUpdateNodeFix, called once */
static void keepVerticalZWithQ(void *q)
{
    float mtx[16];
    Vec16 v;

    GetMatrixFromQuaternion(mtx, q);
    sceVu0ApplyMatrix(&v, mtx, ZUnitVector);
    v.f[1] = 0.0f;
    sceVu0Normalize(&v, &v);
    SetIdentityQuaternion(skelRoot->quat);
    RotQuaternionY(skelRoot->quat, -GetTableArcTan2(v.f[0], -v.f[2]));
    MultiQuaternion(skelRoot->quat, skelRoot->quat, skelRoot->fixQuat);
    CopyMatrix((void *)MatrixDrive_GetMatrix(),
               (char *)GOBJ_SUB(skelRoot->fixObj)->nodeMtx + skelRoot->fixNode * 64);
    MatrixDrive_TransMatrixV(skelRoot->fixPos);
    UnitRotation(MatrixDrive_GetMatrix());
    MultiMatrixByQuaternion(skelRoot->quat);
    MatrixDrive_TransMatrix(rootMove[0], rootMove[1], rootMove[2]);
    CopyVector(skelRoot->pos, MatrixDrive_GetMatrix()[3]);
}

static ObjNode rootUpdateNodeFix(void)
{
    Vec16 rq;
    Vec16 va;
    Vec16 vb;

    CopyQuaternion(&rq, (char *)GOBJ_SUB(skelRoot->fixObj)->nodeQuat + skelRoot->fixNode * 16);
    switch (skelRoot->fixMode) {
    default:
    case 0:
        MultiQuaternion(skelRoot->quat, &rq, skelRoot->fixQuat);
        CopyMatrix((void *)MatrixDrive_GetMatrix(),
                   (char *)GOBJ_SUB(skelRoot->fixObj)->nodeMtx + skelRoot->fixNode * 64);
        MultiMatrixByQuaternion(skelRoot->fixQuat);
        MatrixDrive_TransMatrixV(skelRoot->fixPos);
        MatrixDrive_TransMatrix(rootMove[0], rootMove[1], rootMove[2]);
        CopyVector(skelRoot->pos, MatrixDrive_GetMatrix()[3]);
        break;
    case 1:
        break;
    case 3:
        GetRootQuaternion(&rq, (GObj *)skelRoot->fixObj);
        MultiQuaternion(&rq, &rq, GOBJ_SUB(skelRoot->fixObj)->root.motionQuat);
        /* fall through */
    case 2:
        keepVerticalZWithQ(&rq);
        break;
    }
    _ScaleVector(&va, skelRoot->last, 1.0f - skelRoot->fixWeight);
    _ScaleVector(&vb, skelRoot->pos, skelRoot->fixWeight);
    _AddVector(skelRoot->pos, &va, &vb);
    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->footPos, skelRoot->pos);
    CopyVector(skelRoot->move, ZeroVector);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->delta, rootDelta);
    skelRoot->standNode = -1;
    skelRoot->height = 0.0f;
    skelMotCtrl->floorAttr = 0;
    return InitialObjPointer;
}

static ObjNode rootUpdateY(void)
{
    Vec16 mv;
    Vec16 mv2;
    float mtx[16];
    float plane[4];
    float vp[4];
    Vec16 corner[4];
    Vec16 center;
    Vec16 pos;
    Vec16 push;
    ClipWork w;
    ClipWork cw;
    float d;
    int i;

    GetMatrixFromQuaternion(mtx, skelRoot->quat);
    sceVu0ApplyMatrix(&mv, mtx, rootMove);
    sceVu0ApplyMatrix(&mv2, mtx, rootStep);

    CopyVector(skelRoot->move, &mv2);
    sceVu0AddVector(skelRoot->pos, skelRoot->pos, skelRoot->move);
    CopyVector(skelRoot->footPos, skelRoot->pos);

    GetPureVerticalPlane(0, vp, (float *)corner, &skelRoot->wall, 0);
    CopyVector(plane, vp);
    plane[3] = 0.0f;

    memset(&center, 0, 16);
    center.f[3] = 1.0f;
    for (i = 0; i < 4; i++) {
        AddVectorXYZ(&center, &center, &corner[i]);
    }
    sceVu0ScaleVector(&center, &center, 0.25f);
    skelRoot->pos[0] = center.f[0];
    skelRoot->pos[2] = center.f[2];
    skelRoot->pos[0] = skelRoot->pos[0] + mv.f[0];
    skelRoot->pos[2] = skelRoot->pos[2] + mv.f[2];

    CopyVector(&pos, skelRoot->pos);
    pos.f[1] = pos.f[1] - rootMove[1];
    d = GetDistanceFromPlane(vp, &pos);
    sceVu0ScaleVector(&push, vp, -d - rootMove[2]);
    AddVectorXYZ(&pos, &pos, &push);
    pos.f[3] = 1.0f;
    CopyVector(skelRoot->pos, &pos);
    skelRoot->pos[1] = skelRoot->pos[1] + rootMove[1];
    sceVu0ScaleVectorXYZ(skelMotCtrl->dir, plane, -1.0f);

    if (skelRoot->stairStep != 0) {
        float t = skelRoot->pos[1] - mv.f[1];
        float e;

        t = t - (float)((int)(t / 50.0f) * 50);
        if (t < 0.0f) {
            t = t + 50.0f;
        }
        if (t > 25.0f) {
            e = 50.0f - t;
            skelRoot->pos[1] = skelRoot->pos[1] + e;
        } else {
            skelRoot->pos[1] = skelRoot->pos[1] - t;
        }
    }

    getFieldCollision(&w);
    CopyVector(&skelRoot->plane, &w.normal);
    clipWallAhead(&cw);
    if (cw.wall.elem != 0) {
        WallCfg rec = {cw.wall.o, cw.wall.elem};

        skelRoot->aheadWall = rec;
    }

    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrix(skelRoot->pos[0], skelRoot->pos[1] + 100.0f, skelRoot->pos[2]);
    MultiMatrixByQuaternion(skelRoot->quat);
    checkWallState(2);
    MatrixDrive_PopMatrix();

    CopyVector(skelRoot->last, skelRoot->pos);
    CopyVector(skelRoot->delta, rootDelta);
    CopyVector(skelRoot->trans, ZeroVector);
    skelRoot->standNode = -1;
    return skelRoot->wall.o;
}

/* the parameter is unused */
static ObjNode rootUpdateY_Rope(int actPt)
{
    float mtx[16];
    Vec16 dir;
    Vec16 pos;
    Vec16 v;
    Vec16 cur;
    float save;
    int mode;

    CopyVector(skelRoot->footPos, skelRoot->pos);
    mode = skelRoot->ropeState;
    if (mode >= 2) {
        GetMatrixFromQuaternion(mtx, skelRoot->quat);
        sceVu0ApplyMatrix(&dir, mtx, rootMove);
        sceVu0AddVector(&pos, skelRoot->holdPoint, &dir);
        skelRoot->pos[0] = pos.f[0];
        skelRoot->pos[2] = pos.f[2];
    } else if (mode != 0) {
        int node = GetSkeltonFocusNode((GObj *)skelGObj, 22);

        v.f[0] = skelRoot->pos[0];
        v.f[1] = skelRoot->pos[1];
        v.f[2] = skelRoot->pos[2];
        _SubVector(skelRoot->pos, skelRoot->holdPoint, nodePos2 + node * 16);
        if (skelRoot->ropeState < 0) {
            _InterGV(skelRoot->pos, v.f, skelRoot->pos, ropeInterRate, 1.0f);
        }
    }
    save = skelRoot->radius;
    CopyVector(&cur, skelRoot->pos);
    CopyVector(&v, skelRoot->last);
    skelRoot->radius = 60.0f;
    wallCollisionAndFloorFit(cur.f, v.f, 5.0f);
    skelRoot->radius = save;
    skelMotCtrl->fallHeight = 0.0f;
    skelRoot->height = 0.0f;
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->footPos, skelRoot->pos);
    CopyVector(skelRoot->last, skelRoot->pos);
    skelRoot->standNode = -1;
    return InitialObjPointer;
}

static ObjNode rootUpdateFly(void)
{
    ObjNode shift = {0, -1};
    ClipWork w;
    int contact;
    float h;
    float d;

    skelMotCtrl->floorAttr = 0;
    contact = getFlyFieldCollision(&w);
    switch (contact) {
    case 1:
        skelMotCtrl->flags |= 8;
        h = skelRoot->footPos[1];
        CopyVector(&skelRoot->plane, &w.normal);
        d = GetYProjectionOfPlane(w.normal.f, skelRoot->pos);
        d = d - h;
        skelMotCtrl->fallHeight = d;
        break;
    case 2:
        markPoolContact(&w);
        break;
    }
    wallCollisionAndReact(skelNode->pos[1]);
    skelRoot->height = 0.0f;
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->move, ZeroVector);
    CopyVector(skelRoot->last, skelRoot->pos);
    skelRoot->standNode = -1;
    return shift;
}

static ObjNode rootUpdateEnemyFly(void)
{
    ObjNode shift = {0, -1};
    ClipWork w;
    int contact;

    AddVectorXYZ(skelRoot->pos, skelRoot->pos, skelRoot->move);
    if (GOBJ_SUB(skelGObj)->ctrl.noFieldClip == 0) {
        wallCollisionAndReact(0.0f);
        contact = getFlyFieldCollision(&w);
        switch (contact) {
        case 1:
            CopyVector(skelRoot->footPos, skelRoot->pos);
            skelMotCtrl->fallHeight = 0.0f;
            CopyVector(&skelRoot->plane, &w.normal);
            skelMotCtrl->flags |= 8;
            break;
        case 2:
            markPoolContact(&w);
            break;
        }
    }
    skelRoot->height = 0.0f;
    SubVectorXYZ(skelRoot->move, skelRoot->pos, skelRoot->last);
    CopyVector(skelRoot->trans, ZeroPoint);
    CopyVector(skelRoot->last, skelRoot->pos);
    skelRoot->standNode = -1;
    return shift;
}
