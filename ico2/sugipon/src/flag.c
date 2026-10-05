#include "flag.h"
#include "ios.h"
#include "attackCheckBoundary.h"
#include "memory.h"
#include "DisplayP2O.h"
#include "Light.h"
#include "Matrix.h"
#include "Primitive.h"
#include "clothAnimation.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "quaternion.h"
#include <string.h>

/* the corners (x, y) of each half of a fixed four-point flag, one row
   per id */
static float flag4PointFix[2][4][4] = {
    {{0.0f, 0.0f, 0.0f, 0.0f},
     {0.48f, 0.02f, 0.0f, 0.0f},
     {0.0f, 0.1f, 0.0f, 0.0f},
     {0.5f, 0.2f, 0.0f, 0.0f}},
    {{0.48f, 0.02f, 0.0f, 0.0f},
     {1.0f, 0.0f, 0.0f, 0.0f},
     {0.5f, 0.2f, 0.0f, 0.0f},
     {1.0f, 0.25f, 0.0f, 0.0f}},
}; /* derived name */

/* Set a four-point flag's mesh: each vertex is interpolated between the
   corners of the id's row of flag4PointFix, its height raised by k.
   SetFlag4PointFixID is the one caller, passing turns * 0.25f as k.
 */
static inline void setFlag4PointMesh(Mesh3D *mesh, ClothCfg *cl, float k, int id) /* derived name */
{
    float *tbl = flag4PointFix[id][0];
    Prim3DVec *v = mesh->st;
    int n = cl->num;
    int m = cl->div;
    int i;
    int j;
    float a;
    float b;
    float c;
    float d;

    for (i = 0; i < n; i++) {
        a = tbl[0] + (tbl[4] - tbl[0]) * i / (n - 1);
        b = tbl[8] + (tbl[12] - tbl[8]) * i / (n - 1);
        for (j = 0; j < m; j++) {
            c = tbl[1] + (tbl[9] - tbl[1]) * j / (m - 1) + k;
            d = tbl[5] + (tbl[13] - tbl[5]) * j / (m - 1) + k;
            v[i * m + j].y = c + (d - c) * i / (n - 1);
            v[i * m + j].x = a + (b - a) * j / (m - 1);
        }
    }
}

void SetFlag4PointFixID(GObj *self, int turns, int id)
{
    FlagWork *w;
    short ang;

    w = GOBJ_SUB(self)->work;
    w->turns = turns;
    _UnitMatrix(MatrixDrive_GetMatrix());
    ang = -turns * 0x4000;
    MatrixDrive_RotMatrixZ(ang);
    /* the root position, Sub15C + 0xA0 (flag.h) */
    _ApplyMatrix(FLAG_ROOT_POS(self), MatrixDrive_GetMatrix(), FLAG_ROOT_POS(self));
    RotQuaternionZ(GOBJ_SUB(self)->root.quat, ang);
    /* the clothes' one cloth: its mesh, and the config's rows and columns */
    setFlag4PointMesh(w->clothes->rec->mesh, w->cfg, turns * 0.25f, id);
    prim_UpdateMesh3D(w->clothes->rec->mesh, 8, 0);
    prim_UpdateMesh3D(w->clothes->rec->mesh, 8, 1);
}

/* InitFlagGeo builds a cloth from the laid-out object's row of layoutClothDef
   (attackCheckBoundary.h): its corners, the cloth type in the low four bits,
   the rows and columns, the length shared out over the columns and the fall. */

/* the flag's cloth template, copied into every new config; InitFlagGeo
   passes __LINE__, so its lines below are kept out of clang-format */
static ClothCfg flagCfg = {8, 50.0f, 10, 0, 0, 0, 5.0f}; /* derived name */

/* clang-format off */
char *InitFlagGeo(char *self, char *arg)
{
    char *p = iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(FlagWork, 20), __FILE__, __LINE__);
    Vec4Flag v[4];
    char *mesh;
    Vec4Flag *q;
    const LayoutClothDef *ent = &layoutClothDef[*(int *)(arg + 0x30)];
    float k = ent->length / (float)ent->count;
    float d;
    int type, i, j;
    ClothCfg *cl = iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(ClothCfg, 56), __FILE__, __LINE__);
    *cl = flagCfg;
    cl->weight = ent->weight;
    cl[1].num = -1;
    cl->num = ent->rows;
    cl->div = ent->count;

    mesh = iosMallocDebug(ios_partition_sugipon, cl->num * 48, __FILE__, __LINE__);
    FLAG_SET_ANCHORS(cl, mesh);
    /* the four corners of the layout entry, with y negated and w set
       to 1 */
    v[0].m[0] = ent->pt[0][0];
    v[0].m[1] = -ent->pt[0][1];
    v[0].m[2] = ent->pt[0][2];
    v[0].m[3] = 1.0f;

    v[1].m[0] = ent->pt[1][0];
    v[1].m[1] = -ent->pt[1][1];
    v[1].m[2] = ent->pt[1][2];
    v[1].m[3] = 1.0f;

    v[2].m[0] = ent->pt[2][0];
    v[2].m[1] = -ent->pt[2][1];
    v[2].m[2] = ent->pt[2][2];
    v[2].m[3] = 1.0f;

    v[3].m[0] = ent->pt[3][0];
    v[3].m[1] = -ent->pt[3][1];
    v[3].m[2] = ent->pt[3][2];
    v[3].m[3] = 1.0f;

    ICO_RAW(int, p, 0x10, ((FlagWork *)p)->turns) = 0;
    type = ent->kind & 0xF;
    ICO_RAW(int, p, 0x0, ((FlagWork *)p)->type) = type;
    switch (type) {
    case 1:
    case 2:
        FLAG_ALLOC_NODES(SUBHANDLE_OF(self)->sub, cl->div - 1);
        CopyVector(ICO_RAWP(char *, SUBHANDLE_OF(self)->p, 0xA0, (char *)SUBHANDLE_OF(self)->sub->root.pos), ZeroPoint);
        CopyVector(mesh + 0x10, &v[0]);
        CopyVector(mesh + 0x20, &v[1]);
        *(int *)mesh = -1;
        *(float *)(mesh + 4) = k;
        ICO_RAW(ClothSet *, p, 4, ((FlagWork *)p)->clothes) = InitClothes(cl);
        break;
        /* types 1 and 2 above reach the 0x15C slot through SubHandle, as
           cage.c and girlForceField.c do; the other slot reads here are
           GOBJ_SUB's.  Type 0 hangs the cloth along the line between the
           first two corners. */
    case 0:
        cl->tex = iosMallocDebug(ios_partition_sugipon, strlen(ent->name) + 1, __FILE__, __LINE__);
        strcpy(cl->tex, ent->name);


        _InterVectorXYZ(GOBJ_SUB(self)->root.pos, &v[0], &v[1], 0.5f);
        _SubVectorXYZ(&v[0], &v[0], GOBJ_SUB(self)->root.pos);
        _SubVectorXYZ(&v[1], &v[1], GOBJ_SUB(self)->root.pos);

        d = GetPointDistance(&v[0], &v[1]);
        cl->segLength = d / (float)cl->num;



        for (i = 0; i < cl->num; i++) {
            _InterVector(mesh + 0x10 + i * 48, &v[0], &v[1],
                         (float)i / (float)(cl->num - 1));
            *(int *)(mesh + i * 48) = -1;
            *(float *)(mesh + i * 48 + 4) = k;
        }

        ICO_RAW(ClothSet *, p, 4, ((FlagWork *)p)->clothes) = InitClothes(cl);
        break;




    case 4:
        cl->tex = iosMallocDebug(ios_partition_sugipon, strlen(ent->name) + 1, __FILE__, __LINE__);
        strcpy(cl->tex, ent->name);
        /* type 4: the object's root is the mean of the four corners, the
           corners are made relative to it, and each column's length is
           shared out between the lengths of the two sides
         */
        CopyVector(GOBJ_SUB(self)->root.pos, ZeroPoint);
        for (i = 0; i < 4; i++)
            _AddVectorXYZ(GOBJ_SUB(self)->root.pos, GOBJ_SUB(self)->root.pos, &v[i]);
        _ScaleVectorXYZ(GOBJ_SUB(self)->root.pos, GOBJ_SUB(self)->root.pos, 0.25f);
        FLAG_SET_POSW(self);
        for (i = 0, q = v; i < 4; i++, q++)
            _SubVectorXYZ(q, q, GOBJ_SUB(self)->root.pos);

        d = GetPointDistance(&v[0], &v[1]);
        cl->segLength = d / (float)(cl->num - 1);



        for (j = 0; j < 4; j++)
            CopyVector(mesh + 0x10 + j * 48, &v[j]);

        k = GetPointDistance(&v[0], &v[2]);
        d = GetPointDistance(&v[1], &v[3]);
        for (j = 0; j < cl->num; j++) {
            *(int *)(mesh + j * 48) = -1;
            *(float *)(mesh + j * 48 + 4) = (k + (d - k) * j / (float)(cl->num - 1)) / (float)(cl->div - 1);
        }


        ICO_RAW(ClothSet *, p, 4, ((FlagWork *)p)->clothes) = InitClothesNoShade(cl);
        break;
    }



    ICO_RAW(int, p, 8, ((FlagWork *)p)->cfg) = (ICO_WORD_PTR(ClothCfg *))cl;




    if (GOBJ_SUB(self)->colData != 0) {

        ICO_RAW(int, p, 0xC, ((FlagWork *)p)->collide) = 1;
    } else { ICO_RAW(int, p, 0xC, ((FlagWork *)p)->collide) = 0; }

    GOBJ_SUB(self)->disp = 0;

    GOBJ_SUB(self)->nodes->rot[0] = GOBJ_SUB(self)->nodes->rot[1] = GOBJ_SUB(self)->nodes->rot[2] = 0;
    ((FlagNodeWord *)&GOBJ_SUB(self)->nodes->scale[0])->f = ((FlagNodeWord *)&GOBJ_SUB(self)->nodes->scale[1])->f = ((FlagNodeWord *)&GOBJ_SUB(self)->nodes->scale[2])->f = 1.0f;

    SetIdentityQuaternion(GOBJ_SUB(self)->root.quat);

    return p;
}

/* clang-format on */

void FlagGeo(GObj *self)
{
    FlagWork *gd;
    ClothSet *o;

    gd = GOBJ_SUB(self)->work;
    o = gd->clothes;
    if (*(char **)((char *)GOBJ_SUB(self)) != 0 &&
        ICO_RAW(int, *(char **)((char *)GOBJ_SUB(self)), 0x16C,
                GOBJ_SUB(self)->parent.obj->active) == 0) {
        return;
    }
    GetRootMatrix(MatrixDrive_GetMatrix(), self);
    /* types 0 and 1 hang the cloth from its anchors, type 2 pins the far end
       of each row as well (GetClothAnimation's last argument), type 4 holds
       it by its four corners; a flag that collides is its own wall owner, and
       the collision flag goes in the wall-count slot the callee recomputes */
    switch (gd->type) {
    case 0:
    case 1:
        GetClothAnimation(o->rec->pos, o->rec->vel, 0, MatrixDrive_GetMatrix(), gd->cfg,
                          gd->collide, gd->collide != 0 ? self : 0, 0);
        break;
    case 2:
        GetClothAnimation(o->rec->pos, o->rec->vel, 0, MatrixDrive_GetMatrix(), gd->cfg,
                          gd->collide, gd->collide != 0 ? self : 0, 1);
        break;
    case 4:
        GetClothAnimationFix4Points(o->rec->pos, o->rec->vel, gd->cfg, MatrixDrive_GetMatrix());
        break;
    }
}

void FlagDL(GObj *self)
{
    Vec4Flag l0;
    Vec4Flag l10;
    Vec4Flag l20;
    Vec4Flag l30;
    Vec4Flag l40;
    FlagWork *gd;
    ClothSet *o;
    char *base;
    char *m;
    int i;
    int n;

    gd = GOBJ_SUB(self)->work;
    o = gd->clothes;
    if (*(char **)((char *)GOBJ_SUB(self)) != 0 &&
        ICO_RAW(int, *(char **)((char *)GOBJ_SUB(self)), 0x16C,
                GOBJ_SUB(self)->parent.obj->active) == 0) {
        return;
    }
    /* types 1 and 2 turn the cloth's first row into the object's node
       matrices and draw the nodes; types 0 and 4 draw the cloth mesh */
    switch (gd->type) {
    case 1:
    case 2:
        n = gd->cfg->div;
        base = (char *)o->rec->pos[0];
        memset(&l0, 0, 0x10);
        l0.m[3] = 1.0f;
        memset(&l10, 0, 0x10);
        l10.m[1] = 1.0f;
        for (i = 1; i < n; i++) {
            _SubVector(&l20, base + i * 0x10, base + (i * 0x10 - 0x10));
            _NormalizeVector(&l30, &l20);
            GetDifferencialQuaternionWithNoRegularize(&l40, &l30, &l10);
            MultiQuaternion(&l0, &l40, &l0);
            CopyVector(&l10, &l30);
            GetMatrixFromQuaternionPos((char *)GOBJ_SUB(self)->nodeMtx + (i * 0x40 - 0x40), &l0,
                                       base + (i * 0x10 - 0x10));
        }
        p2o_DispVU1Multi(self);
        break;
    case 0:
        light_MakeLightMatrix(GOBJ_SUB(self), 0);
        m = (char *)GOBJ_SUB(self)->lightMtx;
        DispClothMesh(o->rec, m + 0x40, m);
        break;
    case 4:
        light_MakeLightMatrix(GOBJ_SUB(self), 0);
        m = (char *)GOBJ_SUB(self)->lightMtx;
        DispClothMesh(o->rec, m + 0x40, m);
        break;
    }
}
