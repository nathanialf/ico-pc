#include "debug.h"
#include "sceneManager.h"
#include "memory.h"
#include "act.h"
#include "boyact.h"
#include "mail-add-data.h"
#include "Matrix.h"
#include "Primitive.h"
#include "frameDependSequence.h"
#include "matrixDrive.h"
#include "weapon.h"
#include <libvu0.h>

/* the colour the debug display draws a boundary sphere in */
static unsigned int acbSphereColor[4] = {0, 128, 255, 128}; /* derived name */

#include "attackCheckBoundary.h"
#include "ios.h"
#include "main.h"
#include "GifPacket.h"

static inline GObj *createAttackCheckBoundaryGObj(SObjSimpleSetting *lay) /* derived name */
{
    return CreateLayoutedGObj(63, 75, -1, 0, lay, -1, 7, 1);
}

/* The 12-byte work record this function allocates and the other members
   reach through the sub-object's 0x830: 0x4 is the hit flag AttackCheckBoundaryDL
   tests and the manager reads back and clears, 0x8 the attribute SetAttackCheckBoundaryAttribute stores, and 0x0
   the caller's int that CreateAttackCheckBoundary passed in the layout's
   handle, cleared again through it here. */
typedef struct { /* field names derived */
    int *handle; /* 0x0 */
    int hit;     /* 0x4 */
    int attr;    /* 0x8 */
} AcbWork;       /* derived name */

inline ICO_WORD InitAttackCheckBoundaryGeo(int unused, void *obj)
{
    AcbWork *w =
        (AcbWork *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(AcbWork, 12), __FILE__, 27);

    w->handle = (int *)((SObjSimpleSetting *)obj)->obj;
    w->hit = 0;
    *w->handle = 0;
    w->attr = 0;
    return (ICO_WORD)w;
}

inline void AttackCheckBoundaryGeo(GObj *self)
{
    GObj *owner = GOBJ_SUB(self)->parent.obj;
    if (owner == 0)
        return;
    if (owner->active == 0) {
        self->active = 0;
    }
}

inline void AttackCheckBoundaryDL(GObj *obj)
{
    AcbWork *m = GOBJ_SUB(obj)->work;
    float r;

    if (debug_skel_flag == 0) {
        return;
    }
    /* the boundary is drawn only while nothing has hit it */
    if (m->hit == 0) {
        gif_StartPacketPri(11);

        gif_SetZTest(1);
        gif_SetAlpha(1, 5, 128);
        _UnitMatrix(MatrixDrive_GetMatrix());
        CopyVector(MatrixDrive_GetMatrix()[3], (char *)GOBJ_SUB(obj)->nodeMtx + 0x30);
        r = GetAttackCheckBoundaryRadius(obj);
        prim_DispWireSphere(r, acbSphereColor, 4, 4);
        gif_EndPacket();
    }
}

inline void SetAttackCheckBoundaryAttribute(GObj *self, int attr)
{
    /* the work pointer, read through the int-typed sub-object handle */
    AcbWork *w = GOBJ_SUB(self)->work;
    w->attr = attr;
}

inline float GetAttackCheckBoundaryRadius(GObj *self)
{
    return GOBJ_SUB(self)->nodes->scale[0];
}

inline GObj *CreateAttackCheckBoundary(int *obj, float x, float y, float z, float r)
{
    SObjSimpleSetting lay = InitialSObjSimpleSetting;

    lay.pos[0] = x;
    lay.pos[1] = y;
    lay.pos[2] = z;
    lay.scale[0] = r;
    lay.obj = (ICO_WORD)obj;
    *obj = 0;
    return createAttackCheckBoundaryGObj(&lay);
}

inline void actAttackCheckBoundaryStart(GObj *self)
{
    Act *p = actInitialize(self);

    actInitialize_ext_charcter(self);
    _ACTWait(1);
    p->flags18.ll |= 1LL << 32;
}

void AttackCheckBoundaryBeforeFunc(GObj *self)
{
    IosMailBox *box = &self->mailBox;
    IosMail *e = box->mail;
    int i;

    for (i = 0; i < box->num; i++, e++) {
        if (e->type == 13) {
            if (e->arg == boyGObj) {
                AcbWork *b = GOBJ_SUB(self)->work;
                void *g = GetBoyWeaponGObj();

                if (g != 0) {
                    int k = CheckWeaponKind(g);

                    if (k == 4 || k == 5 || k == 6 || k == 9 || k == 8) {
                        if (*b->handle < 2) {
                            GOBJ_SUB(g)->ctrl.wallAttr = b->attr;
                            ExecuteSEPackage(g, 74);
                            b->hit = 2;
                            *b->handle = 2;
                            /* " - cut by the sword" */
                            debug_StdPrintfDummy(" - 剣で切られた\n");
                        }
                        goto done;
                    }
                }
                if (*b->handle <= 0) {
                    ActSendMail_WithAdditionalData(boyGObj, 209, self, &b->attr);
                    b->hit = 1;
                    *b->handle = 1;
                    /* " - cannot cut" */
                    debug_StdPrintfDummy(" - きれない\n");
                }
            }
        done:
            {
                Act *q = GOBJ_ACT(self);

                q->attacker = 0;
                q->hit = 0;
            }
        }
    }
    box->num = 0;
}

/* the manager's 8-byte roster entries and its work block at sub+0x830 */
typedef struct AcbEntry {     /* field names derived */
    ICO_WORD_PTR(GObj *) obj; /* 0x00, the boundary object, kept as a word */
    int hit;                  /* 0x04 */
} AcbEntry;                   /* derived name */

typedef struct AcbMgr { /* field names derived */
    int count;          /* 0x00 */
    int cur;            /* 0x04 */
    int prev;           /* 0x08 */
    AcbEntry *list;     /* 0x0C */
} AcbMgr;               /* derived name */

/* the blank roster entry each slot starts from */
static AcbEntry acbBlankEntry = {0, 0}; /* derived name */

AcbMgr *InitAttackCheckBoundaryManagerGeo(GObj *self, SObjSimpleSetting *lay)
{
    float v0[4];
    float v1[4];
    float v2[4];
    float p[4];
    AcbMgr *mgr;
    const LayoutClothDef *rec;
    float len;
    int i;
    GObj *g;

    rec = &layoutClothDef[lay->obj];
    mgr = (AcbMgr *)iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(AcbMgr, 16), __FILE__, 180);
    mgr->prev = mgr->cur;
    mgr->cur = 0;
    mgr->count = rec->count;
    mgr->list = (AcbEntry *)iosMallocDebug(ios_partition_sugipon,
                                           mgr->count * ICO_MAX_SIZE(AcbEntry, 8), __FILE__, 189);
    v0[0] = rec->pt[0][0];
    v0[1] = -rec->pt[0][1];
    v0[2] = rec->pt[0][2];
    v0[3] = 1.0f;
    v1[0] = rec->pt[1][0];
    v1[1] = -rec->pt[1][1];
    v1[2] = rec->pt[1][2];
    v1[3] = 1.0f;
    _SubVector(v2, v1, v0);
    _ScaleVector(v2, v2, 0.5f / (float)mgr->count);
    _AddVectorXYZ(v0, v0, v2);
    _SubVectorXYZ(v1, v1, v2);
    len = VectorLength(v2);
    for (i = 0; i < mgr->count; i++) {
        _InterVector(p, v1, v0, (float)i / (float)(mgr->count - 1));
        mgr->list[i] = acbBlankEntry;
        g = CreateAttackCheckBoundary(&mgr->cur, p[0], p[1], p[2], len);
        mgr->list[i].obj = (ICO_WORD_PTR(GObj *))g;
        SetAttackCheckBoundaryAttribute(g, rec->attr);
        /* the boundary hangs from the manager: AttackCheckBoundaryGeo
           deactivates it with its owner */
        GOBJ_SUB(g)->parent.obj = self;
    }
    return mgr;
}

void AttackCheckBoundaryManagerGeo(GObj *self)
{
    AcbMgr *m = GOBJ_SUB(self)->work;
    int i;

    for (i = 0; i < m->count; i++) {
        GObj *e = (GObj *)m->list[i].obj;

        /* whether the member's work says it has been hit */
        m->list[i].hit = ((AcbWork *)GOBJ_SUB(e)->work)->hit;
        ((AcbWork *)GOBJ_SUB(e)->work)->hit = 0;
        e->active = 1;
    }
    m->prev = m->cur;
    m->cur = 0;
}

void AttackCheckBoundaryManagerDL(void) {}

inline int GetAttackCheckBoundaryManagerStatus(GObj *self)
{
    AcbMgr *m = GOBJ_SUB(self)->work;

    return m->prev;
}
