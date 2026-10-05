#include "typedef.h"
#include "sugiCommon.h"
#include "geometryManager.h"
#include "debug_exception.h"
#include "gobj.h"
#include "girl_act.h"
#include "motionManager2.h"
#include "motionManager.h"
#include <libvu0.h>
#include "Matrix.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include "fieldCollision.h"

static void getInitialInverseMatrix(char *mat, Sub15C *mdl, int no);
static void getInitialMatrix(Sub15C *mdl, int no);

#include <assert.h>

/* The display object's root block (Sub15C root, typedef.h) holds the root
   position, its quaternion and the root height; the object it hangs from is
   parent.obj and that object's node is parent.node. */
void GetRootQuaternionByDObj(void *q, Sub15C *dobj)
{
    GObj *parent;
    int idx;
    parent = dobj->parent.obj;
    if (parent == 0)
        goto null_path;
    idx = dobj->parent.node;
    MultiQuaternion(q, (float *)parent->dobj->nodeQuat + (idx << 2), dobj->root.quat);
    return;
null_path:
    CopyQuaternion(q, dobj->root.quat);
}

void UpdateRootMatrixByDObj(Sub15C *dobj)
{
    struct MotRoot *p = &dobj->root;
    float *fobj = (float *)dobj->nodeMtx;
    GetMatrixFromQuaternionPos(fobj, p->quat, p->pos);
    {
        GObj *q = dobj->parent.obj;
        if (q != 0) {
            sceVu0MulMatrix(fobj, (char *)q->dobj->nodeMtx + (dobj->parent.node << 6), fobj);
        }
    }
    fobj[13] += p->height;
    GetRootQuaternionByDObj((float *)dobj->nodeQuat, dobj);
}

void GetRootQuaternion(void *q, GObj *obj)
{
    GetRootQuaternionByDObj(q, obj->dobj);
}

void UpdateRootMatrix(GObj *obj)
{
    UpdateRootMatrixByDObj(obj->dobj);
}

void SetRootBaseQuaternion(GObj *obj, void *q)
{
    CopyQuaternion(obj->dobj->root.baseQuat, q);
}

void SetRootQuaternion(GObj *obj, void *quat)
{
    float *q = obj->dobj->root.quat;
    Sub15C *p;
    CopyQuaternion(q, quat);
    p = obj->dobj;
    if (p->parent.obj != 0) {
        Sub15C *m = p->parent.obj->dobj;
        DivQuaternion(q, quat, (float *)m->nodeQuat + (p->parent.node << 2));
    }
}

void SetRootMatrixWithTransOffsetByDObj(Sub15C *dobj, float x, float y, float z)
{
    MatrixDrive_PushMatrix();
    CopyMatrix(MatrixDrive_GetMatrix(), &dobj->matrix);
    MatrixDrive_TransMatrix(x, y, z);
    CopyMatrix((void *)dobj->nodeMtx, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
}

void SetRootMatrixWithTransOffset(GObj *obj, float x, float y, float z)
{
    SetRootMatrixWithTransOffsetByDObj(obj->dobj, x, y, z);
}

void GetRootMatrixRotOffsetByDObj(void *q, Sub15C *dobj)
{
    GetInverseQuaternion(q, dobj->quat);
    MultiQuaternion(q, q, (void *)dobj->nodeQuat);
}

void GetRootMatrixRotOffset(void *q, GObj *obj)
{
    GetRootMatrixRotOffsetByDObj(q, obj->dobj);
}

void SetRootMatrixRotOffsetByDObj(Sub15C *dobj, void *q)
{
    MatrixDrive_PushMatrix();
    CopyMatrix(MatrixDrive_GetMatrix(), &dobj->matrix);
    MultiMatrixByQuaternion(q);
    CopyMatrix((void *)dobj->nodeMtx, MatrixDrive_GetMatrix());
    MatrixDrive_PopMatrix();
    MultiQuaternion((void *)dobj->nodeQuat, dobj->quat, q);
}

void SetRootMatrixRotOffset(GObj *obj, void *q)
{
    SetRootMatrixRotOffsetByDObj(obj->dobj, q);
}

/* the bodies of GetRootPositionByDObj, GetRootPosition, SetRootPosition and
 * SetDirectRootPositionNoFitting, which the functions below inline and the
 * exported functions further down call */
static __inline__ void GetRootPositionByDObj_i(float *pos, Sub15C *src) /* derived name */
{
    struct MotRoot *root = &src->root;
    float f0;
    GObj *g = src->parent.obj;
    if (g) {
        sceVu0ApplyMatrix(pos, (char *)g->dobj->nodeMtx + (src->parent.node << 6), root->pos);
    } else {
        CopyVector(pos, root->pos);
    }
    f0 = root->height;
    pos[1] += f0;
    pos[3] = 1.0f;
}

static __inline__ void GetRootPosition_i(float *pos, GObj *obj) /* derived name */
{
    GetRootPositionByDObj_i(pos, obj->dobj);
}

static __inline__ void SetRootPosition_i(GObj *obj, void *pos) /* derived name */
{
    float buf[16];
    Vec4 *p = (Vec4 *)obj->dobj->root.pos;
    CopyVector(p, pos);
    p->f[1] = p->f[1] - ICO_RAW(float, p, 0xC0, obj->dobj->root.height);
    p->f[3] = 1.0f;
    {
        Sub15C *sub = obj->dobj;
        GObj *q = sub->parent.obj;
        if (q != 0) {
            MatrixDrive_SetTransposeMatrix(buf,
                                           (float *)(q->dobj->nodeMtx + (sub->parent.node << 6)));
            sceVu0ApplyMatrix(p, buf, p);
        }
    }
}

static __inline__ void SetDirectRootPositionNoFitting_i(GObj *self, void *v) /* derived name */
{
    Sub15C *sub = self->dobj;
    float *p = sub->root.pos;
    float pos[4];
    float tmp[4];

    CopyVector(pos, v);
    CopyVector(tmp, p);
    SetRootPosition_i(self, pos);
    CopyVector(sub->root.last, p);
    CopyVector(sub->root.savePos, p);
    self->dobj->ctrl.posReserve = 0;
    CopyVector(sub->root.clipFrom, v);
    CopyVector(sub->root.move, ZeroVector);
    CopyVector(sub->root.delta, ZeroVector);
}

void SetDirectRootPositionNoFittingWithNodePoint(GObj *gobj, int node, float *pos, float t)
{
    float v[4];
    float w[4];
    int idx;

    idx = GetSkeltonFocusNode(gobj, node);
    sceVu0SubVector(v, pos, (float *)(gobj->dobj->nodeMtx + (idx << 6)) + 12);
    sceVu0ScaleVector(v, v, t);
    GetRootPosition_i(w, gobj);
    sceVu0AddVector(w, w, v);
    SetDirectRootPositionNoFitting_i(gobj, w);
}

void SetDirectRootPositionNoFittingWithNodePointXZ(GObj *gobj, int node, float *pos, float t)
{
    float v[4];
    float w[4];
    int idx;

    idx = GetSkeltonFocusNode(gobj, node);
    sceVu0SubVector(v, pos, (float *)(gobj->dobj->nodeMtx + (idx << 6)) + 12);
    sceVu0ScaleVector(v, v, t);
    GetRootPosition_i(w, gobj);
    v[1] = 0.0f;
    sceVu0AddVector(w, w, v);
    SetDirectRootPositionNoFitting_i(gobj, w);
}

void SetDirectRootPositionWithNodePoint(GObj *gobj, int node, float *pos, float t)
{
    SetDirectRootPositionNoFittingWithNodePoint(gobj, node, pos, t);
    AdjustMotionHeightToNearestField(gobj);
}

/* The 0x15C slot is the engine's sub-object handle: the code stores an int and
 * reads it back as a pointer; the functions below read it through this union. */

#define SUBOF(o) (((SubHandle *)&((GObj *)(o))->dobj)->sub) /* derived name */

/* the body of LocalizeDirectionOrient, which LocalizeGeometry inlines and
 * LocalizeDirectionOrient calls */
static __inline__ void LocalizeDirectionOrient_i(GObj *self, ObjNode *link) /* derived name */
{
    float buf[16];
    GObj *obj = link->obj;
    Sub15C *ctx = obj->dobj;
    CopyMatrix(buf, (void *)(ctx->nodeMtx + (link->node << 6)));
    MatrixDrive_SetTransposeMatrix(buf, buf);
    sceVu0ApplyMatrix(self->dobj->ctrl.dir, buf, self->dobj->ctrl.dir);
    sceVu0Normalize(self->dobj->ctrl.dir, self->dobj->ctrl.dir);
    self->dobj->ctrl.dir[3] = 0;
}

void LocalizeGeometry(GObj *gobj, ObjNode *link)
{
    float mtx[16];
    Sub15C *sub;
    struct MotRoot *m;

    sub = SUBOF(gobj);

    m = &sub->root;
    if (sub->parent.obj != 0) {
        debug_assertMessage(
            "src/geometryManager.c", 371,
            "Fatal error! Geometry localize function called with GObj\n    that already have parent.\nExit...\n");
        __assert("src/geometryManager.c", 371, "e");
        debug_assert("src/geometryManager.c", 372);
        __assert("src/geometryManager.c", 372, "0");
    }

    m->pos[3] = 1.0f;
    m->pos[1] -= SUBOF(gobj)->root.height;
    m->last[1] -= SUBOF(gobj)->root.height;
    MatrixDrive_SetTransposeMatrix(mtx, (float *)(SUBOF(link->obj)->nodeMtx + (link->node << 6)));
    sceVu0ApplyMatrix(m->pos, mtx, m->pos);
    sceVu0ApplyMatrix(sub->root.last, mtx, sub->root.last);
    DivQuaternion(sub->root.quat, sub->root.quat,
                  (char *)SUBOF(link->obj)->nodeQuat + (link->node << 4));
    LocalizeDirectionOrient_i(gobj, link);
}

void GetGlobalDirectionOrient(float *dir, GObj *obj, void *src)
{
    CopyVector(dir, src);
    {
        Sub15C *sub = obj->dobj;
        GObj *a = sub->parent.obj;
        if (a != 0) {
            Sub15C *psub = a->dobj;
            int idx = sub->parent.node;
            sceVu0ApplyMatrix(dir, (char *)psub->nodeMtx + (idx << 6), src);
        }
    }
    dir[1] = 0.0f;
    sceVu0Normalize(dir, dir);
}

void GlobalizeGeometry(GObj *gobj)
{
    Sub15C *sub = SUBOF(gobj);
    float *m = sub->root.pos;
    struct MotCtrl *w = &sub->ctrl;

    sub->root.pos[3] = 1.0f;
    if (SUBOF(gobj)->parent.obj != 0) {
        sceVu0ApplyMatrix(
            m, (char *)SUBOF(SUBOF(gobj)->parent.obj)->nodeMtx + (SUBOF(gobj)->parent.node << 6),
            m);
        sceVu0ApplyMatrix(SUBOF(gobj)->root.last,
                          (char *)SUBOF(SUBOF(gobj)->parent.obj)->nodeMtx +
                              (SUBOF(gobj)->parent.node << 6),
                          SUBOF(gobj)->root.last);
    }
    SUBOF(gobj)->root.pos[1] = SUBOF(gobj)->root.pos[1] + SUBOF(gobj)->root.height;
    SUBOF(gobj)->root.last[1] = SUBOF(gobj)->root.last[1] + SUBOF(gobj)->root.height;
    GetRootQuaternion(sub->root.quat, gobj);
    GetGlobalDirectionOrient(sub->ctrl.dir, gobj, sub->ctrl.dir);
    w->dir[1] = 0;
    sceVu0Normalize(SUBOF(gobj)->ctrl.dir, SUBOF(gobj)->ctrl.dir);
    w->dir[3] = 0;
}

void GetRootVelocity(float *vel, GObj *obj)
{
    CopyVector(vel, obj->dobj->root.move);
}

void GetInitialInverseMatrixByDObj(char *mat, Sub15C *mdl)
{
    int no;
    SkelNode *nd;

    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    no = 0;
loop:
    {
        nd = &mdl->skel[no];
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrixV(nd->pos);
        MultiMatrixByQuaternion(nd->quat);
        MatrixDrive_SetTransposeMatrix(mat + (no << 6), MatrixDrive_GetMatrix());
        if (nd->child != -1) {
            getInitialInverseMatrix(mat, mdl, nd->child);
        }
        MatrixDrive_PopMatrix();
    }
    if (nd->sibling != -1) {
        no = nd->sibling;
        goto loop;
    }
    MatrixDrive_PopMatrix();
}

void GetInitialInverseMatrix(char *mat, GObj *gobj)
{
    Sub15C *mdl;
    int no;
    SkelNode *nd;

    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    mdl = GOBJ_SUB(gobj);
    no = 0;
loop:
    {
        nd = &mdl->skel[no];
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrixV(nd->pos);
        MultiMatrixByQuaternion(nd->quat);
        MatrixDrive_SetTransposeMatrix(mat + (no << 6), MatrixDrive_GetMatrix());
        if (nd->child != -1) {
            getInitialInverseMatrix(mat, mdl, nd->child);
        }
        MatrixDrive_PopMatrix();
    }
    if (nd->sibling != -1) {
        no = nd->sibling;
        goto loop;
    }
    MatrixDrive_PopMatrix();
}

void GetInitialSkeltonMatrixByDObj(Sub15C *mdl)
{
    int no;
    SkelNode *nd;
    char *dst;

    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_RotMatrixX(-0x8000);
    no = 0;
loop:
    {
        nd = &mdl->skel[no];
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrixV(nd->pos);
        MultiMatrixByQuaternion(nd->quat);
        dst = (char *)mdl->nodeMtx + (no << 6);
        CopyMatrix(dst, MatrixDrive_GetMatrix());
        if (nd->child != -1) {
            getInitialMatrix(mdl, nd->child);
        }
        MatrixDrive_PopMatrix();
    }
    if (nd->sibling != -1) {
        no = nd->sibling;
        goto loop;
    }
    MatrixDrive_PopMatrix();
}

/* the object kinds the character list builder accepts, -1 terminated */
static int charGObjKinds[5] = {1, 2, 4, 47, -1}; /* derived name */

/* the live character objects the cylinder check walks */
static GObj *charGObjList[64]; /* derived name */

/* the number of entries in charGObjList */
static int charGObjCount = 0; /* derived name */

/* the kind test the list builder runs on every live object */
static inline int isCharGObj(GObj *o) /* derived name */
{
    int i;

    if (o->labelType == 1 && o->active != 0) {
        for (i = 0; charGObjKinds[i] != -1; i++) {
            if (o->kind == charGObjKinds[i]) {
                return 1;
            }
        }
    }
    return 0;
}

void MakeCharGObjList(void)
{
    GObj *o;

    o = isysGObjGetExist_begin();
    charGObjCount = 0;
    while (o != 0) {
        if (isCharGObj(o) != 0) {
            charGObjList[charGObjCount++] = o;
            if (charGObjCount > 64) {
                debug_assertMessage("src/geometryManager.c", 558,
                                    "TOO MANY CHARACTERS EXIST ON THIS STAGE(>64)\n");
                __assert("src/geometryManager.c", 558, "e");
            }
        }
        o = isysGObjGetExist_next(o);
    }
    charGObjList[charGObjCount] = 0;
}

int cylinderCollisionCheck(GObj *self, float *ppos, GObj *target, float r, float rr, float h,
                           float s, float t, int ctrl, int exceptOwn)
{
    float pos[4];
    float v[4];
    float d1[4];
    float d2[4];
    float d3[4];
    ClipWork w;
    float dy;
    float len;
    float over;
    float a;
    float b;
    float c;

    GetRootPosition_i(pos, target);
    dy = pos[1] - ppos[1];
    if (!((dy < 0.0f ? -dy : dy) < h)) {
        goto fail;
    }
    sceVu0SubVector(v, pos, ppos);
    v[1] = 0.0f;
    len = VectorLengthSquare(v);
    if (!(len < rr)) {
        goto fail;
    }
    over = r - _Sqrt(len);
    sceVu0Normalize(v, v);
    sceVu0ScaleVector(d1, v, over * s);
    sceVu0SubVector(d2, ppos, d1);
    sceVu0ScaleVector(d1, v, over * t);
    sceVu0AddVector(d3, pos, d1);

    if (self != 0) {
        if (exceptOwn != 0) {
            w.filter.o.obj = self;
            w.filter.o.node = -1;
            w.filter.elem = 0;
            w.radius = SUBOF(self)->root.radius;
            CopyVector(w.pt[0], ppos);
            CopyVector(w.pt[1], d2);
            ClipWallE(&w);
            if (w.wall.elem != 0) {
                CopyVector(d2, w.pt[2]);
            }
            w.radius = SUBOF(target)->root.radius;
            CopyVector(w.pt[0], pos);
            CopyVector(w.pt[1], d3);
            ClipWallE(&w);
            if (w.wall.elem != 0) {
                CopyVector(d3, w.pt[2]);
            }
            goto moved;
        }
        w.radius = SUBOF(self)->root.radius;
        CopyVector(w.pt[0], ppos);
        CopyVector(w.pt[1], d2);
        ClipWall(&w);
        if (w.wall.elem != 0) {
            CopyVector(d2, w.pt[2]);
        }
    }
    w.radius = SUBOF(target)->root.radius;
    CopyVector(w.pt[0], pos);
    CopyVector(w.pt[1], d3);
    ClipWall(&w);
    if (w.wall.elem != 0) {
        CopyVector(d3, w.pt[2]);
    }
moved:
    if (ctrl != 0) {
        if (self != 0) {
            b = SUBOF(self)->root.last[1];
            c = SUBOF(self)->root.clipFrom[1];
            a = SUBOF(self)->root.move[1];
            SetDirectRootPositionNoFitting_i(self, d2);
            SUBOF(self)->root.move[1] = a;
            SUBOF(self)->root.last[1] = b;
            SUBOF(self)->root.clipFrom[1] = c;
        }
        b = SUBOF(target)->root.last[1];
        c = SUBOF(target)->root.clipFrom[1];
        a = SUBOF(target)->root.move[1];
        SetDirectRootPositionNoFitting_i(target, d3);
        SUBOF(target)->root.move[1] = a;
        SUBOF(target)->root.last[1] = b;
        SUBOF(target)->root.clipFrom[1] = c;
    } else {
        if (self != 0) {
            SetRootPosition_i(self, d2);
        }
        SetRootPosition_i(target, d3);
    }
    return 1;
fail:
    return 0;
}

void LocalizeDirectionOrient(GObj *self, ObjNode *link)
{
    LocalizeDirectionOrient_i(self, link);
}

/* defined in girl_act.c; girl_act.h does not declare it */
extern int isMustCheckCylinder(void *a, void *b);

/* no caller; int, as sugipon's other scalar getters */
int GetCylinderCollision(GObj *self, GObj *target, float r, float h, float s, int ctrl)
{
    float pos[4];

    GetRootPosition_i(pos, self);
    return cylinderCollisionCheck(self, pos, target, r, r * r, h, s, 1.0f - s, ctrl, 0);
}

int GetCylinderCollisionWithExceptOwnCollision(GObj *self, GObj *target, float r, float h, float s,
                                               float t, int ctrl)
{
    float pos[4];

    GetRootPosition_i(pos, self);
    return cylinderCollisionCheck(self, pos, target, r, r * r, h, s, t, ctrl, 1);
}

/* the body of CylinderCollisionWithControlDynamics, which CylinderCollision
 * inlines and CylinderCollisionWithControlDynamics calls */
static __inline__ int CylinderCollisionWithControlDynamics_i(GObj *self, int group, int ctrl,
                                                             float r, float h,
                                                             float s) /* derived name */
{
    float pos[4];
    int hit = 0;
    int i;
    GObj *o;
    Sub15C *sub;
    float rr;

    sub = GOBJ_SUB(self);
    if (sub->cylinderOn == 0 || sub->root.cylinder == 0) {
        return 0;
    }
    GetRootPosition_i(pos, self);
    rr = r * r;
    for (i = 0, o = charGObjList[0]; i < charGObjCount; i++, o = charGObjList[i]) {
        if (o->kind != group)
            continue;
        if (o == self)
            continue;
        {
            Sub15C *osub = GOBJ_SUB(o);
            if (osub->cylinderOn == 0 || osub->root.cylinder == 0) {
                if (isMustCheckCylinder(self, o) == 0)
                    continue;
            }
        }
        hit = cylinderCollisionCheck(self, pos, o, r, rr, h, s, 1.0f - s, ctrl, 0);
    }
    return hit;
}

int CylinderCollision(GObj *self, int group, float r, float h, float s)
{
    return CylinderCollisionWithControlDynamics_i(self, group, 1, r, h, s);
}

int CylinderCollisionWithControlDynamics(GObj *self, int group, int ctrl, float r, float h, float s)
{
    return CylinderCollisionWithControlDynamics_i(self, group, ctrl, r, h, s);
}

void GetRootMatrixByDObj(float *m, Sub15C *src)
{
    struct MotRoot *root = &src->root;
    GetMatrixFromQuaternionPos(m, root->quat, root->pos);
    {
        GObj *g = src->parent.obj;
        if (g) {
            sceVu0MulMatrix(m, (char *)g->dobj->nodeMtx + (src->parent.node << 6), m);
        }
    }
    m[13] += root->height;
}

void GetRootMatrix(void *mtx, GObj *obj)
{
    float *m = mtx;
    Sub15C *src = obj->dobj;
    struct MotRoot *root = &src->root;
    GetMatrixFromQuaternionPos(m, root->quat, root->pos);
    {
        GObj *g = src->parent.obj;
        if (g) {
            sceVu0MulMatrix(m, (char *)g->dobj->nodeMtx + (src->parent.node << 6), m);
        }
    }
    m[13] += root->height;
}

void GetRootPositionByDObj(void *dst, Sub15C *src)
{
    GetRootPositionByDObj_i(dst, src);
}

void SetDirectRootPosition(GObj *self, void *v)
{
    Sub15C *sub = self->dobj;
    float *p = sub->root.pos;
    float pos[4];
    float tmp[4];

    CopyVector(pos, v);
    CopyVector(tmp, p);
    SetRootPosition_i(self, pos);
    CopyVector(sub->root.last, p);
    CopyVector(sub->root.savePos, p);
    self->dobj->ctrl.posReserve = 0;
    CopyVector(sub->root.clipFrom, v);
    CopyVector(sub->root.move, ZeroVector);
    CopyVector(sub->root.delta, ZeroVector);
    AdjustMotionHeightToNearestField(self);
}

void SetDirectRootPositionNoFitting(GObj *self, void *v)
{
    SetDirectRootPositionNoFitting_i(self, v);
}

void SetRootPosition(GObj *obj, void *pos)
{
    SetRootPosition_i(obj, pos);
}

void GetRootPosition(void *dst, GObj *obj)
{
    GetRootPosition_i(dst, obj);
}

void GetRootOrient(float *dir, GObj *obj)
{
    float buf[4][4];
    Sub15C *sub = GOBJ_SUB(obj);
    struct MotRoot *root = &sub->root;
    GetMatrixFromQuaternionPos(buf, root->quat, root->pos);
    {
        GObj *q = sub->parent.obj;
        if (q != 0) {
            sceVu0MulMatrix(buf, (char *)(GOBJ_SUB(q)->nodeMtx + (sub->parent.node << 6)), buf);
        }
    }
    buf[3][1] = buf[3][1] + root->height;
    sceVu0ApplyMatrix(dir, buf, ZUnitVector);
    dir[1] = 0.0f;
    sceVu0Normalize(dir, dir);
}

int LimitExistGeometry(float *pos, float *move)
{
    int ret = 0;
    int i;

    for (i = 2; i >= 0; move++, pos++, i--) {
        if (*pos < -100000.0f) {
            *pos = -100000.0f;
            *move = 0.0f;
            ret = 1;
        } else if (*pos > 100000.0f) {
            *pos = 100000.0f;
            *move = 0.0f;
            ret = 1;
        }
    }
    return ret;
}

void GetRootMatrixTransOffsetByDObj(float *dst, Sub15C *src)
{
    float tmp[4][4];
    MatrixDrive_SetTransposeMatrix(tmp, &src->matrix);
    sceVu0MulMatrix(tmp, tmp, (void *)src->nodeMtx);
    CopyVector(dst, tmp[3]);
}

void GetRootMatrixTransOffset(float *dst, GObj *src)
{
    float tmp[4][4];
    Sub15C *p = GOBJ_SUB(src);
    MatrixDrive_SetTransposeMatrix(tmp, &p->matrix);
    sceVu0MulMatrix(tmp, tmp, (void *)p->nodeMtx);
    CopyVector(dst, tmp[3]);
}

void GetRootMotionOrient(float *dir, GObj *obj)
{
    char m[64];
    float buf[4][4];
    float (*b)[4] = buf;
    Sub15C *sub = GOBJ_SUB(obj);
    struct MotRoot *root = &sub->root;
    GetMatrixFromQuaternionPos(b, root->quat, root->pos);
    {
        GObj *q = sub->parent.obj;
        if (q != 0) {
            sceVu0MulMatrix(b, (char *)(GOBJ_SUB(q)->nodeMtx + (sub->parent.node << 6)), b);
        }
    }
    b[3][1] = b[3][1] + root->height;
    GetMatrixFromQuaternion(m, GOBJ_SUB(obj)->root.motionQuat);
    sceVu0MulMatrix(m, b, m);
    sceVu0ApplyMatrix(dir, m, ZUnitVector);
}

void GetRootMotionMatrix(float (*mtx)[4], GObj *obj)
{
    float buf[4][4];
    Sub15C *sub = GOBJ_SUB(obj);
    struct MotRoot *root = &sub->root;
    GetMatrixFromQuaternionPos(buf, root->quat, root->pos);
    {
        GObj *q = sub->parent.obj;
        if (q != 0) {
            sceVu0MulMatrix(buf, (char *)(GOBJ_SUB(q)->nodeMtx + (sub->parent.node << 6)), buf);
        }
    }
    buf[3][1] = buf[3][1] + root->height;
    GetMatrixFromQuaternion(mtx, GOBJ_SUB(obj)->root.motionQuat);
    sceVu0MulMatrix(mtx, buf, mtx);
}

void GetProjectionPosOfPlane(void *out, void *plane, void *pos)
{
    float buf[4];
    float dot;
    dot = plane_distance(pos, plane);
    _ScaleVectorXYZ(buf, plane, -dot);
    AddVectorXYZ(out, pos, buf);
    *(float *)((char *)out + 0xC) = 1.0f;
}

float GetProjectionOfPlane(void *out, void *plane, void *pos)
{
    float buf[4];
    float f = 0.0f;
    float dot;
    dot = plane_distance(pos, plane);
    _ScaleVectorXYZ(buf, plane, -dot + f);
    _AddVectorXYZ(out, pos, buf);
    return dot;
}

float GetProjectionOfPlaneWithKeepAway(void *out, void *plane, void *pos, float keepAway)
{
    float buf[4];
    float dot;
    dot = plane_distance(pos, plane);
    _ScaleVectorXYZ(buf, plane, -dot + keepAway);
    _AddVectorXYZ(out, pos, buf);
    return dot;
}

GObj **GetCharGObjList(void)
{
    return charGObjList;
}

static void getInitialInverseMatrix(char *mat, Sub15C *mdl, int no)
{
    SkelNode *nd = &mdl->skel[no];
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(nd->pos);
    MultiMatrixByQuaternion(nd->quat);
    MatrixDrive_SetTransposeMatrix(mat + (no << 6), MatrixDrive_GetMatrix());
    if (nd->child != -1) {
        getInitialInverseMatrix(mat, mdl, nd->child);
    }
    MatrixDrive_PopMatrix();
    if (nd->sibling != -1) {
        getInitialInverseMatrix(mat, mdl, nd->sibling);
    }
}

static void getInitialMatrix(Sub15C *mdl, int no)
{
    SkelNode *nd = &mdl->skel[no];
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrixV(nd->pos);
    MultiMatrixByQuaternion(nd->quat);
    CopyMatrix((char *)mdl->nodeMtx + (no << 6), MatrixDrive_GetMatrix());
    if (nd->child != -1) {
        getInitialMatrix(mdl, nd->child);
    }
    MatrixDrive_PopMatrix();
    if (nd->sibling != -1) {
        getInitialMatrix(mdl, nd->sibling);
    }
}
