#include "typedef.h"
#include "debug.h"
#include "gamesys.h"
#include "memory.h"
#include "obj_manager.h"
#include "gobj.h"
#include "commonact.h"
#include "act-game.h"
#include "boyact.h"
#include "enemy_act.h"
#include "ebrain.h"
#include "geometryManager.h"
#include "multiBgaManager.h"
#include "debug_exception.h"
#include "gv.h"
#include <libvu0.h>
#include "camera-editor.h"
#include "ee_view.h"
#include <assert.h>

typedef struct GenBga { /* field names derived */
    BgaDisp *p;         /* the multi-BGA manager */
    char active;        /* set while the manager plays */
    char pad5[3];
} GenBga; /* derived name */

/* The generator's work record: whether the stage needs it hard, three state
   bytes, the enemy kind it calls, the direction it sends enemies off in, its
   four multi-BGA managers (each with its active byte), the main status, the
   current animation slot and the timers.  The direction is three floats: the byte at 0x2C follows it, and
   sceVu0ApplyMatrix writes a whole quadword there. */
typedef struct GenWork {        /* field names derived */
    int count;                  /* 0x00, frames since the generator started */
    int autoCallTimer;          /* 0x04, counts up to the automatic call */
    int callRequests;           /* 0x08, enemy calls waiting to be served */
    int hard;                   /* 0x0C */
    unsigned char masked;       /* 0x10, Generator_Mask */
    unsigned char active;       /* 0x11 */
    unsigned char resetRequest; /* 0x12, Generator_ResetCount */
    char pad13[1];
    int kind;              /* 0x14 */
    int pad18[2];          /* 0x18 */
    float dir[3];          /* 0x20 */
    unsigned char bgaDone; /* 0x2C, set when the BGA run ends */
    char pad2D[3];
    GenBga bga[4]; /* 0x30 */
    int status;    /* 0x50 */
    int callState; /* 0x54, 0 idle, 1 waiting out callDelay, 2 calling */
    int cur;       /* 0x58 */
    int callDelay; /* 0x5C */
    int bgaDelay;  /* 0x60, frames until the second BGA starts, -1 for none */
    int timer;     /* 0x64 */
} GenWork;         /* derived name */

/* the generator packet: 11277 bytes are read into it, which is what
   GetsizeGeneratorPacket returns, three under the buffer */
static int generatorPacket[2820]; /* derived name */

#include "generator.h"
#include <string.h>
#include "ios.h"
#include "main.h"

static GObj *IsNeedGeneratorHard(GObj *mother);

inline int SearchActiveGenerator(void)
{
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(33);
    while (g != 0) {
        GenWork *w = GOBJ_SUB(g)->work;

        if (g->active != 0) {
            if (w->status == 1) {
                return 1;
            }
        }
        g = isysGObjSearchFromObjKindID_next(g);
    }
    return 0;
}

int CheckGeneratorCollision(GObj *gobj, float *dir)
{
    float pos[4];
    float tmp[4];
    float p[4];
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(33);
    GetRootPosition(pos, gobj);
    sceVu0ScaleVector(tmp, dir, 100.0f);
    sceVu0AddVector(pos, pos, tmp);

    for (; g != 0; g = isysGObjSearchFromObjKindID_next(g)) {
        GenWork *w = GOBJ_SUB(g)->work;

        if (g->active == 0) {
            continue;
        }
        if (w->status != 1) {
            continue;
        }
        GetRootPosition(p, g);
        if (_DistxzSqGV(p, pos) < 22500.0f) {
            float d = p[1] - 50.0f - pos[1];

            if ((d < 0.0f ? -d : d) < 100.0f) {
                return 0;
            }
        }
    }
    return 1;
}

/* whether no live enemy stands close to pos; GetGeneratorSafePosition uses it
   twice and reads the result as a byte */
static inline unsigned char IsGeneratorSafePosition(float *pos) /* derived name */
{
    GObj *g;

    for (g = isysGObjSearchFromObjKindID_begin(17); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        float p[4];

        if (g->active == 0) {
            continue;
        }
        GetRootPosition(p, g);
        if (_DistxzSqGV(pos, p) < 22500.0f) {
            float d = pos[1] - 50.0f - p[1];

            if ((d < 0.0f ? -d : d) < 100.0f) {
                return 0;
            }
        }
    }
    return 1;
}

void GetGeneratorSafePosition(float *dst, GObj *gobj)
{
    float pos[4];
    int i;

    GetRootPosition(pos, gobj);
    if (IsGeneratorSafePosition(pos)) {
        dst[0] = pos[0];
        dst[1] = pos[1];
        dst[2] = pos[2];
        return;
    }

    {
        float probe[4];

        for (i = 0; i < 7; i++) {
            const SafePosOffset *e = &generatorSubPosition[i];

            if (e->kind != gobj->labelId) {
                continue;
            }
            probe[0] = -e->x;
            probe[1] = -e->y;
            probe[2] = -e->z;
            if (IsGeneratorSafePosition(probe)) {
                dst[0] = probe[0];
                dst[1] = probe[1];
                dst[2] = probe[2];
                return;
            }
        }
        GetRootPosition(probe, gobj);
        dst[0] = probe[0];
        dst[1] = probe[1];
        dst[2] = probe[2];
    }
}

static void switch_MainStatus(GObj *gobj, unsigned char st)
{
    float pos[4];
    GenWork *w = GOBJ_SUB(gobj)->work;

    if (st == w->status) {
        return;
    }

    switch (w->status) {
    case 0:
        if (!IsNeedGeneratorHard(gobj)) {
            w->status = 2;
            gamesysObjInfoUniqDataSet(gobj);
            break;
        }
        iosOmSendMail(gobj, 0, gobj);
        w->status = 1;
        w->timer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 4;
        GetGeneratorSafePosition(pos, gobj);
        SetRootPosition(gobj, pos);
        gamesysObjInfoUniqDataSet(gobj);
        break;

    case 1:
        iosOmSendMail(gobj, 2, gobj);
        w->status = 2;
        gamesysObjInfoUniqDataSet(gobj);
        break;

    case 2:
        break;
    }
}

/* stop the current animation slot's manager (ResetCurrentBga) and start the
   manager of a slot (EntryBga), for endfunc_BGA and GeneratorGeo */
static inline char *ResetCurrentBga(GObj *gobj) /* derived name */
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    if (w->cur != -1) {
        w->bga[w->cur].active = 0;
        w->bga[w->cur].p->frame = -1.0f;
    }
    return (char *)w;
}

static inline void EntryBga(GObj *gobj, GenWork *w, int slot) /* derived name */
{
    float mtx[4];

    memset(mtx, 0, 16);
    mtx[3] = 1.0f;
    EntryMultiBgaManager(w->bga[slot].p, 0, -1, (void *)test_CURRENTROOT(gobj), mtx);
}

static void endfunc_BGA(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    switch (w->cur) {
    case 0: {
        GenWork *cur;

        w->bga[1].active = 1;
        cur = (GenWork *)ResetCurrentBga(gobj);
        EntryBga(gobj, cur, 1);
        cur->cur = 1;
        break;
    }

    case 1:
        w->bga[1].p->frame = 0.0f;
        break;

    case 2: {
        GenWork *cur;

        w->bgaDone = 1;
        cur = (GenWork *)ResetCurrentBga(gobj);
        cur->cur = -1;
        break;
    }

    default:
        debug_assert(__FILE__, 504);
        __assert(__FILE__, 504, "0");
        break;
    }
}

static GObj *IsNeedGeneratorHard(GObj *mother)
{
    GenWork *w = GOBJ_SUB(mother)->work;
    GObj *g;
    int count = 0;
    int isCalling = (w->status == 1);

    if (mother != 0 && mother->labelId == 3758) {
        int found = 0;

        g = isysGObjSearchFromObjLayoutID(3757);
        if (g != 0) {
            GenGeo *gv = &objLayout[g->labelId];

            if (gv->reviveCount == -1 || gv->reviveCount > 0) {
                found = 1;
            }
            if (isEnemyActive(g)) {
                found = 1;
            }
        }
        return found ? g : 0;
    }

    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Act *p = GOBJ_ACT(g);

        if ((unsigned int)(p->flags18.ll >> 34) & 1) {
            continue;
        }
        if (mother == 0 || (mother->labelId != GetMotherGeneratorLabelAskEnemy(g) &&
                            objLayout[g->labelId].parent != mother->labelId)) {
            continue;
        }
        if (objLayout[g->labelId].reviveCount == -1 || objLayout[g->labelId].reviveCount > 0) {
            return g;
        }
        if (isEnemyActive(g)) {
            return g;
        }
    }

    for (g = isysGObjSearchFromObjKindID_begin(33); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {}

    for (g = isysGObjSearchFromObjKindID_begin(62); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Act *p = GOBJ_ACT(g);

        count += (int)(p->flags18.ll >> 32) & 1;
    }

    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Act *p = GOBJ_ACT(g);
        GenGeo *gv = &objLayout[g->labelId];

        if (g->labelId == 3757) {
            continue;
        }
        if ((unsigned int)(p->flags18.ll >> 34) & 1) {
            if (count < 5) {
                continue;
            }
            return g;
        }
        if (mother != 0) {
            if (mother->labelId == GetMotherGeneratorLabelAskEnemy(g)) {
                continue;
            }
            if (objLayout[g->labelId].parent == mother->labelId) {
                continue;
            }
        }
        if (isEnemyActive(g)) {
            return g;
        }
        if (gv->reviveCount == -1 || gv->reviveCount > 0) {
            if (!isCalling) {
                return g;
            }
            {
                GObj *m = GetMotherGeneratorGObjAskEnemy(g);

                if (m != 0 && IsOpenGenerator(m)) {
                    return g;
                }
            }
        }
    }
    return 0;
}

inline int IsEnableCallEnemyByTargetGObj(GObj *gobj)
{
    GenGeo *g = &objLayout[gobj->labelId];
    Act *p = GOBJ_ACT(gobj);
    if (g->parent != 0) {
        return 0;
    }
    if ((unsigned int)(p->flags18.ll >> 34) & 1) {
        return 0;
    }
    if (((g->flags >> 21) & 1) == 0 && (g->reviveCount == -1 || g->reviveCount > 0)) {
        return 1;
    }
    return 0;
}

inline GObj *IsEnableCallEnemy(GObj *self)
{
    GObj *g;

    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Act *p = GOBJ_ACT(g);
        int no = g->labelId;
        GenGeo *gg = &objLayout[no];

        if (self != 0 && self->labelId == 3758 && no != 3757) {
            continue;
        }
        if (objLayout[no].parent != 0 && self != 0 && objLayout[no].parent != self->labelId) {
            continue;
        }
        if ((unsigned int)(p->flags18.ll >> 34) & 1) {
            continue;
        }
        if ((gg->flags >> 21) & 1) {
            continue;
        }
        if (gg->reviveCount == -1 || gg->reviveCount > 0) {
            return g;
        }
    }
    return 0;
}

inline GObj *DirectCallEnemy(GObj *gobj, GObj *mother, float *pos, float *dir, int kind)
{
    GenGeo *gg = &objLayout[gobj->labelId];

    gg->flags = (gg->flags | 0x200000) & 0xFFFBFFFF;

    debug_StdPrintfDummy("call enemy! = %d (%p : %d)\n", gobj->labelId, mother,
                         (mother != 0) ? mother->labelId : -1);
    debug_StdPrintfDummy("[%8s] %8f %8f %8f %8f\n", "revive", pos[0], pos[1], pos[2], pos[3]);
    if (mother != 0) {
        SetMotherGenerator(gobj->labelId, mother->labelId);
    }
    if (gg->reviveCount != -1) {
        gg->reviveCount--;
    }
    actEnemyRestart(gobj, pos, dir, kind, mother);
    ACTGame_SaveActorInformation(gobj);
    return gobj;
}

GObj *CallEnemy(GObj *mother, float *pos, float *dir, int kind)
{
    GObj *gobj = IsEnableCallEnemy(mother);

    if (gobj != 0) {
        return DirectCallEnemy(gobj, mother, pos, dir, kind);
    }
    return 0;
}

inline void LockEnemyGenerate(GObj *gobj)
{
    Act *p;
    p = GOBJ_ACT(gobj);
    debug_StdPrintfDummy("lock! = %d\n", gobj->labelId);
    p->flags18.ll = p->flags18.ll | 0x400000000LL;
}

inline void UnlockEnemyGenerate(GObj *gobj)
{
    Act *p = GOBJ_ACT(gobj);
    GenGeo *g = &objLayout[gobj->labelId];
    debug_StdPrintfDummy("unlock! = %d\n", gobj->labelId);
    p->flags18.ll &= ~((unsigned long long)0x8000 << 19);
    g->flags = (g->flags | 0x200000) & 0xFFFBFFFF;
}

inline void RestoreReviveCount(GObj *gobj)
{
    GenGeo *g = &objLayout[gobj->labelId];
    if (g->reviveCount != -1) {
        int n = (short)(g->reviveCount + 1);
        int lim = ((g->flags >> 5) & 0x1F) + 1;
        n = (lim < n) ? lim : n;
        g->reviveCount = n;
        if (gobj->labelId == 3757) {
            g->reviveCount = (g->reviveCount < 0) ? 0 : ((g->reviveCount > 1) ? 1 : g->reviveCount);
        }
    }
}

void Generator_QuickCall(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    w->status = 1;
    gamesysObjInfoUniqDataSet(gobj);
    iosOmSendMail(gobj, 1, gobj);
}

inline void Generator_Call(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    w->callRequests += 1;
}

inline void Generator_ResetCount(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    w->resetRequest = 1;
}

inline void Generator_Mask(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    w->masked = 1;
}

inline void Generator_MaskOff(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    w->masked = 0;
}

void Generator_Delete(GObj *gobj)
{
    switch_MainStatus(gobj, 0);
}

int GetMotherGenerator(int label)
{
    int info[2];
    int x;
    int st;
    int i;
    int j;
    int best;
    int ret;

    if (label == 3757) {
        return 3758;
    }

    GetKidnapInfo(&info[0], &info[1]);
    if (info[0] != -1 && info[0] == label) {
        return info[1];
    }

    x = (int)((objLayout + label)->flags << 27) >> 27;
    if (x != -1) {
        st = GetStageFromLabel(label);
        for (i = stageData[st].labelTop; i < stageData[st].labelEnd; i++) {
            GenGeo *g = &objLayout[i];

            if (g->kind == 33) {
                if (((int)(g->flags << 27) >> 27) == x) {
                    return i;
                }
            }
        }
    }

    best = 0;
    ret = -1;
    st = GetStageFromLabel(label);
    for (j = stageData[st].labelTop; j < stageData[st].labelEnd; j++) {
        GenGeo *g = &objLayout[j];

        if (g->kind == 33) {
            int v = (g->flags >> 17) & 1;

            if (best < v) {
                best = v;
                ret = j;
            }
        }
    }
    return ret;
}

inline void SetMotherGenerator(int no, int label)
{
    int i;
    int cnt;

    if (no == 3757) {
        return;
    }
    cnt = 0;
    for (i = stageData[stage_no].labelTop; i < stageData[stage_no].labelEnd; i++) {
        GenGeo *g = &objLayout[i];
        if (g->kind == 33) {
            if (i == label) {
                GenGeo *m = &objLayout[no];
                m->flags = ((int)m->flags & ~0x3C00) | ((cnt & 0xF) << 10);
                return;
            }
            cnt++;
        }
    }
}

inline void Generator_Init(void)
{
    int i;

    for (i = 0; i < 3759; i++) {
        GenGeo *g = &objLayout[i];
        unsigned int x = g->flags & 0xFFDFFFFF;
        unsigned int y = x & 0xFFFBFFFF;

        y |= ((x >> 19) & 1) << 18;
        g->flags = y;
        g->reviveCount = (y >> 5) & 0x1F;
        if ((y >> 19) & 1) {
            unsigned int z = y | 0x40000;
            g->flags = z;
            if ((int)((z >> 5) & 0x1F) != -1) {
                g->reviveCount++;
            }
        }
    }
}

inline void ReturnEnemyToGenerator(int label)
{
    GenGeo *g = &objLayout[label];
    unsigned int x = g->flags & 0xFFDFFFFF;
    unsigned int y = x & 0xFFFBFFFF;
    y |= ((x >> 19) & 1) << 18;
    g->flags = y;
    if ((y >> 19) & 1) {
        unsigned int z = y | 0x40000;
        g->flags = z;
        if ((int)((z >> 5) & 0x1F) != -1) {
            g->reviveCount++;
        }
    }
}

inline int *GetbufpGeneratorPacket(void)
{
    return generatorPacket;
}

inline int GetsizeGeneratorPacket(void)
{
    return 11277;
}

void ReadGeneratorPacket(void)
{
    unsigned char *p = (unsigned char *)GetbufpGeneratorPacket();
    int i;

    for (i = 0; i < 3759; i++) {
        GenGeo *g = &objLayout[i];
        unsigned int b = *p++;
        unsigned int x = (b >> 4) << 10;

        g->flags = ((int)g->flags & ~0x3C00) | x;
        g->flags = (g->flags & ~0x200000) | ((b & 1) << 21);
    }

    for (i = 0; i < 3759; i++) {
        GenGeo *g = &objLayout[i];
        char c = *(char *)p++;

        g->flags = (g->flags & ~0x40000) | ((c & 1) << 18);
    }

    for (i = 0; i < 3759; i++) {
        objLayout[i].reviveCount = (char)*p;
        p++;
    }
}

void MakeGeneratorPacket(void)
{
    char *p = (char *)GetbufpGeneratorPacket();
    int i;

    for (i = 0; i < 3759; i++) {
        *p++ = (((int)(objLayout[i].flags << 18) >> 28) << 4) | ((objLayout[i].flags >> 21) & 1);
    }

    for (i = 0; i < 3759; i++) {
        *p++ = (objLayout[i].flags >> 18) & 1;
    }

    for (i = 0; i < 3759; i++) {
        *p++ = objLayout[i].reviveCount;
    }
}

inline void ResetReviveCountEnemy(GObj *gobj)
{
    objLayout[gobj->labelId].reviveCount = 0;
}

inline void SetInfoSpKidnapEnemy(short *work)
{
    GenGeo *info = &objLayout[3757]; /* the kidnap enemy's layout record */
    info->flags |= 0x200000;
    info->flags &= ~0x40000;
    info->reviveCount = 0;
}

inline void SetInfoSpKidnapGenerator(short *info)
{
    info[0] = 1;
    info[1] = 1;
}

inline int RestoreGeneratorGeo(float *dst, float *src)
{
    dst[0] = src[4];
    dst[1] = src[5];
    dst[2] = src[6];
    return 1;
}

inline int RestoreGeneratorExtGeo(GObj *gobj, short *info)
{
    GenWork *p = GOBJ_SUB(gobj)->work;
    p->status = info[24];
    p->callRequests = info[25];
    if (info[24] == 1) {
        p->callState = 2;
        iosOmSendMail(gobj, 1, gobj);
    }
    return 1;
}

inline int MemoryGenerator(short *info, GObj *gobj)
{
    GenWork *p = GOBJ_SUB(gobj)->work;
    info[0] = p->status;
    info[1] = p->callRequests;
    return 1;
}

/* The generator's view of its object's mail box (GObj 0x54, typedef.h's
   IosMailBox): the count, and the queued mails with an unsigned kind. */
typedef struct GenReqEntry { /* field names derived */
    unsigned int kind;
    int arg;
} GenReqEntry; /* derived name */

typedef struct GenReq { /* field names derived */
    int queue;
    int count;
} GenReq; /* derived name */

void generatorBeforeFunc(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    GenReq *q = (GenReq *)&gobj->mailBox;
    int i;

    for (i = 0; i < q->count; i++) {
        switch (((GenReqEntry *)gobj->mailBox.mail)[i].kind) {
        case 0: {
            GenWork *cur;

            w->bga[0].active = 1;
            cur = (GenWork *)ResetCurrentBga(gobj);
            EntryBga(gobj, cur, 0);
            cur->cur = 0;
            break;
        }

        case 1: {
            GenWork *cur;

            w->bga[1].active = 1;
            cur = (GenWork *)ResetCurrentBga(gobj);
            EntryBga(gobj, cur, 1);
            cur->cur = 1;
            break;
        }

        case 2:
            w->bgaDelay = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 4;
            break;
        }
    }
    q->count = 0;
}

inline GenWork *InitGeneratorGeo(GObj *gobj, GenGeo *src)
{
    GenWork *p = iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(GenWork, 112), __FILE__, 1230);
    int i;

    p->count = 0;
    p->autoCallTimer = 0;
    p->callRequests = 0;
    p->masked = 0;
    p->resetRequest = 0;
    p->kind = src->accessary;
    p->hard = 0;

    p->status = 0;
    p->callState = 0;
    p->cur = -1;
    p->callDelay = -1;
    p->bgaDelay = -1;
    p->timer = 0;

    p->dir[0] = 0.0f;
    p->dir[1] = 0.0f;
    p->dir[2] = 1.0f;
    /* the direction is a quadword whose fourth word holds the bgaDone byte */
    *(float *)&p->bgaDone = 0.0f;

    _ApplyRyGV(p->dir, src->rot[2]);

    for (i = 0; i < 4; i++) {
        p->bga[i].p = InitMultiBgaManager(1);
        p->bga[i].active = 0;
    }

    return p;
}

/* for GeneratorGeo: the generator's forward axis, the direction it sends
   enemies off in */
static inline void SetGeneratorAimVector(GObj *gobj) /* derived name */
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    float m[16];
    float v[4];

    UpdateRootMatrix(gobj);

    memset(v, 0, 16);
    v[2] = 1.0f;
    GetRootMatrix(m, gobj);
    v[3] = 0.0f;
    sceVu0ApplyMatrix(w->dir, m, v);
}

/* for GeneratorGeo: reset the current slot and start slot 2 */
static inline void EntryGeneratorBga2(GObj *gobj) /* derived name */
{
    GenWork *cur = (GenWork *)ResetCurrentBga(gobj);

    EntryBga(gobj, cur, 2);
    cur->cur = 2;
}

/* whether any live hard-stage generator is calling, for GeneratorGeo, which
   reads the result as a byte */
static inline unsigned char IsGeneratorCalling(void) /* derived name */
{
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(33);
    while (g != 0) {
        GenWork *w = GOBJ_SUB(g)->work;

        if (g->active != 0) {
            if (w->hard != 0) {
                if (w->status == 1) {
                    return 1;
                }
            }
        }
        g = isysGObjSearchFromObjKindID_next(g);
    }
    return 0;
}

/* call an enemy out of the generator and start its call animation, for
   GeneratorGeo */
static inline void CallEnemyFromGenerator(GObj *gobj, float *pos, float *dir) /* derived name */
{
    GenWork *w = GOBJ_SUB(gobj)->work;

    if (CallEnemy(gobj, pos, dir, w->kind) != 0) {
        float mtx[4];

        memset(mtx, 0, 16);
        mtx[3] = 1.0f;
        EntryMultiBgaManager(w->bga[3].p, 0, -1, (void *)test_CURRENTROOT(gobj), mtx);
    }
}

void GeneratorGeo(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    int hard = 0;
    const StgPre *sd = &stageData[stage_no];
    int noBoy = sd->flag1 && girlGObj == 0;

#ifdef ICO_HOST
    w->hard = IsNeedGeneratorHard(gobj) != 0; /* only ever tested against 0 */
#else
    w->hard = (int)IsNeedGeneratorHard(gobj);
#endif
    if (w->hard != 0) {
        if (w->timer < 30) {
            w->timer = 30;
        }
    }

    if (w->count >= 12) {
        if (w->resetRequest != 0) {
            w->callRequests = 0;
            w->resetRequest = 0;
        }

        if (isysGObjSearchFromObjKindID_begin(47) != 0) {
            w->active = 1;
        } else {
            w->active = 0;
        }

        SetGeneratorAimVector(gobj);

        if (noBoy) {
            w->callRequests = 0;
        }

        if (w->callRequests == 0) {
            if (w->masked == 0) {
                if (!noBoy) {
                    w->autoCallTimer = w->autoCallTimer + 1;
                    if (w->autoCallTimer >= 11) {
                        Generator_Call(gobj);
                        w->autoCallTimer = 0;
                    }
                }
            }
        }

        switch (w->callState) {
        case 0:
            if (w->callRequests != 0) {
                w->callState = 1;
                w->callDelay = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 2;
                switch_MainStatus(gobj, 1);
            }
            break;

        case 1:
            w->callDelay = w->callDelay - 1;
            if (w->callDelay < 0) {
                w->callState = 2;
            }
            break;

        case 2:
            hard = 1;
            break;

        default:
            debug_assert(__FILE__, 1371);
            __assert(__FILE__, 1371, "0");
            break;
        }

        if (w->active != 0) {
            hard = 1;
        }

        {
            float pos[4];
            int i;

            if (hard) {
                if (((60 - systemStatus[0] * 10) / systemStatus[1]) / 2 < w->count) {
                    if (w->callRequests != 0) {
                        GetRootPosition(pos, gobj);
                        for (i = 0; i < w->callRequests; i++) {
                            CallEnemyFromGenerator(gobj, pos, w->dir);
                        }
                        w->callRequests = 0;
                    }
                }
            }

            if (!IsGeneratorCalling()) {
                if (w->timer == 0) {
                    switch_MainStatus(gobj, 0);
                }
            }

            if (w->bgaDelay != -1) {
                if (w->bgaDelay == 0) {
                    w->bga[2].active = 1;
                    EntryGeneratorBga2(gobj);
                    w->bgaDelay = -1;
                } else {
                    w->bgaDelay = w->bgaDelay - 1;
                }
            }

            debug_Arrow(100.0f, (void *)test_CURRENTROOT(gobj), w->dir, 0xFF, 0, 0xFF);
        }
    }

    if (w->timer != 0) {
        w->timer = w->timer - 1;
    }
    w->count = w->count + 1;
}

/* for GeneratorDL: put the animation managers at the generator's root */
static inline void SetGeneratorBgaRootPosition(GObj *gobj, GenBga *tbl) /* derived name */
{
    float pos[3];
    int i;

    pos[0] = test_CURRENTROOT(gobj)[0];
    pos[1] = test_CURRENTROOT(gobj)[1];
    pos[2] = test_CURRENTROOT(gobj)[2];

    for (i = 0; i < 4; i++) {
        BgaDisp *p = tbl[i].p;

        p->pos[0] = pos[0];
        p->pos[1] = pos[1];
        p->pos[2] = pos[2];
    }
}

void GeneratorDL(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    GenBga *tbl = w->bga;
    int idx;

    SetGeneratorBgaRootPosition(gobj, tbl);

    if (w->active) {
        DispMultiBgaManagerWithKind(508, w->bga[1].p, 1);
    } else {
        DispMultiBgaManagerWithKind(507, w->bga[0].p, 1);
        DispMultiBgaManagerWithKind(508, w->bga[1].p, 1);
        DispMultiBgaManagerWithKind(509, w->bga[2].p, 1);
        DispMultiBgaManagerWithKind(506, w->bga[3].p, 1);
    }

    idx = w->cur;
    if (idx != -1) {
        GenBga *e = &tbl[idx];

        if (*(float *)e->p < 0.0f) {
            endfunc_BGA(gobj);
        }
    }
}

inline int GeneratorWorkEnd(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    return w->callRequests == 0;
}

inline int IsOpenGenerator(GObj *gobj)
{
    GenWork *w = GOBJ_SUB(gobj)->work;
    int ret = 0;
    if (w->status == 1) {
        GenGeo *g = &objLayout[gobj->labelId];
        ret = (((int)(g->flags << 27) >> 27) == -2) ? 0 : w->status;
    }
    return ret;
}
