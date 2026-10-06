#include "debug.h"
#include "gobj.h"
#include "act-game.h"
#include "boyact.h"
#include "generator.h"
#include "geometryManager.h"
#include "ebrain.h"
#include <libvu0.h>
#include <string.h>
#include "typedef.h"
#include "gamesys.h"
#include "debug_exception.h"
#include "main.h"
#include <assert.h>

#ifdef ICO_HOST

/* port/game/options.c: [gameplay] yorda_safe, docs/port/OPTIONS.md */
extern int ico_opt_yorda_safe(void);

#endif

int eBrainBoyChaseCount;

int eBrainGirlChaseCount;

/* How many enemies are registered against the boy and against the girl, the
   enemy holding the girl, and whether the boy's state makes every enemy
   wait; then the slot pool and the two registration lists. */
static int boyTargetNum; /* derived name */

static int girlTargetNum; /* derived name */

static GObj *girlHolder; /* derived name */

static int enemiesWait; /* derived name */

static EBSlot ebrainSlots[32]; /* derived name */

static inline void eBrainSetStatus(EBSlot *p, int newst) /* derived name */
{
    int st = p->status;

    switch (st) {
    case 1:
        eBrainBoyChaseCount--;
        break;
    case 2:
        eBrainGirlChaseCount--;
        break;
    }
    switch (newst) {
    case 1:
        eBrainBoyChaseCount++;
        if (st != 1)
            p->chaseFrames = 0;
        break;
    case 2:
        eBrainGirlChaseCount++;
        break;
    }
    p->status = newst;
}

static inline EBSlot *eBrainGetPacket(GObj *gop) /* derived name */
{
    int i;

    for (i = 0; i < 32; i++) {
        if (ebrainSlots[i].owner == gop)
            break;
    }
    if (i == 32)
        return 0;
    else
        return &ebrainSlots[i];
}

inline void eBrainInit(void)
{
    int i;
    eBrainGirlChaseCount = 0;
    eBrainBoyChaseCount = 0;
    girlHolder = 0;
    enemiesWait = 0;
    for (i = 0; i < 32; i++) {
        ebrainSlots[i].owner = 0;
    }
}

inline ICO_WORD eBrainStatusSet(GObj *gop, int status)
{
    EBSlot *slot;
    int i;
    if (status != 4)
        return 0;
    for (i = 0; i < 32; i++) {
        if (ebrainSlots[i].owner == 0)
            break;
    }
    if (i < 32)
        slot = &ebrainSlots[i];
    else
        slot = 0;
    if (slot == 0) {
        debug_StdPrintfDummy("eBrainStatusSet: ebrain area over\n");
        return 0;
    }
    slot->owner = gop;
    slot->status = 0;
    slot->message = 0;
    return (ICO_WORD)slot;
}

static EBSlot *boyTargets[32]; /* derived name */

static EBSlot *girlTargets[32]; /* derived name */

static inline void eBrainRegistTarget(EBSlot **list, int n, EBSlot *e, int w) /* derived name */
{
    int j;
    EBSlot *cur = e;
    EBSlot *t;
    float key = cur->dist[w];
    float tk;

    for (j = 0; j < n; j++) {
        t = list[j];
        tk = t->dist[w];

        if (key < tk) {
            list[j] = cur;
            cur = t;
            key = tk;
        }
    }
    list[j] = cur;
}

void eBrainProcess(void)
{
    float bpos[4];
    float gpos[4];
    float epos[4];
    float d[4];
    EBSlot *s;
    int i;

    girlTargetNum = 0;
    boyTargetNum = 0;
#ifdef ICO_HOST
    /* With the boy or the girl not in the stage (a stage entered without
       her), the loop below still subtracts bpos or gpos, which the EE takes
       as its stack left them: a word that is a number there, a NaN or Inf
       bit pattern here (a float trap in the fptrap build, a run-to-run
       difference otherwise).  The host starts both at 0 (DIVERGENCES.md
       D15). */
    memset(bpos, 0, sizeof(bpos));
    memset(gpos, 0, sizeof(gpos));
#endif

    if (boyGObj == 0) {
        if (girlGObj == 0)
            return;
    } else {
        enemiesWait = IsBoyStatus_EnemyMustWait();
        GetRootPosition(bpos, boyGObj);
    }

    if (girlGObj != 0) {
        GetRootPosition(gpos, girlGObj);
    }

    for (i = 0; i < 32; i++) {
        s = &ebrainSlots[i];
        if (s->owner == 0)
            continue;

        GetRootPosition(epos, s->owner);
        sceVu0SubVector(d, epos, bpos);
        s->dist[0] = sceVu0InnerProduct(d, d);
        sceVu0SubVector(d, epos, gpos);
        s->dist[1] = sceVu0InnerProduct(d, d);

        if (s->status == 0) {
            if (boyGObj != 0) {
                eBrainRegistTarget(boyTargets, boyTargetNum, s, 0);
                boyTargetNum = boyTargetNum + 1;
            }
            if (girlGObj != 0) {
                eBrainRegistTarget(girlTargets, girlTargetNum, s, 1);
                girlTargetNum = girlTargetNum + 1;
            }
        } else if (s->status == 1) {
            s->chaseFrames++;
        }
    }
}

inline int GetStageFromLabel(int label)
{
    int UseStageNo = -1;
    int i;

    for (i = 0; i < 106; i++) {
        if (label >= stageData[i].labelTop && label < stageData[i].labelEnd) {
            UseStageNo = i;
            break;
        }
    }
    if (!(UseStageNo > 0)) {
        debug_assert("src/ebrain.c", 472);
        __assert("src/ebrain.c", 472, "UseStageNo>0");
    }
    return UseStageNo;
}

inline int eBrainGetTargetGeneratorFromLabelStage(int label, int stage)
{
    int pri = -1;
    int GeneratorLabel = -1;
    int i;
    int ret;
    int st;
    int f;

    ret = GetMotherGenerator(label);
    if (ret != -1)
        return ret;

    st = stage;
    for (i = stageData[st].labelTop; i < stageData[st].labelEnd; i++) {
        GenGeo *g = &objLayout[i];
        if (g->kind == 33) {
            f = g->flags >> 17;
            f &= 1;
            if (pri < f) {
                pri = f;
                GeneratorLabel = i;
            }
        }
    }
    if (!(GeneratorLabel > 0)) {
        debug_assert("src/ebrain.c", 506);
        __assert("src/ebrain.c", 506, "GeneratorLabel>0");
    }
    return GeneratorLabel;
}

int eBrainGetTargetGeneratorFromLabel(int label)
{
    int GeneratorLabel = -1;
    int pri = -1;
    int UseStageNo;
    int i;
    int ret;
    int st;
    int f;

    ret = GetMotherGenerator(label);
    if (ret != -1)
        return ret;

    UseStageNo = -1;
    for (i = 0; i < 106; i++) {
        if (label >= stageData[i].labelTop && label < stageData[i].labelEnd) {
            UseStageNo = i;
            break;
        }
    }
    if (!(UseStageNo > 0)) {
        debug_assert("src/ebrain.c", 472);
        __assert("src/ebrain.c", 472, "UseStageNo>0");
    }

    st = UseStageNo;
    for (i = stageData[st].labelTop; i < stageData[st].labelEnd; i++) {
        GenGeo *g = &objLayout[i];
        if (g->kind == 33) {
            f = g->flags >> 17;
            f &= 1;
            if (pri < f) {
                pri = f;
                GeneratorLabel = i;
            }
        }
    }
    if (!(GeneratorLabel > 0)) {
        debug_assert("src/ebrain.c", 542);
        __assert("src/ebrain.c", 542, "GeneratorLabel>0");
    }
    return GeneratorLabel;
}

static inline int eBrainCanSeeTarget(GObj *gop, GObj *target) /* derived name */
{
    float mypos[4];
    float tpos[4];

    if (target == 0)
        return 0;
    GetRootPosition(mypos, gop);
    GetRootPosition(tpos, target);
    return ACTCheckViewCl(gop, target, tpos, 180, 100.0f);
}

EBSlot *eBrainGetTarget(GObj *gop)
{
    EBSlot *p;
    int changed;

    p = eBrainGetPacket(gop);
    if (p == 0)
        return 0;

    switch (p->message) {
    case 1:
    case 5:
        eBrainSetStatus(p, 1);
        break;
    case 2:
#ifdef ICO_HOST
        /* yorda_safe: no enemy takes the girl as its target */
        if (ico_opt_yorda_safe()) {
            eBrainSetStatus(p, 1);
            break;
        }
#endif
        eBrainSetStatus(p, 2);
        break;
    case 3:
        eBrainSetStatus(p, 5);
        break;
    case 4:
        eBrainSetStatus(p, 0);
        break;
    case 6:
#ifdef ICO_HOST
        if (ico_opt_yorda_safe()) {
            break;
        }
#endif
        if (girlGObj != 0) {
            eBrainSetStatus(p, 3);
        }
        break;
    case 7:
        eBrainSetStatus(p, 4);
        break;
    }
    p->message = 0;
    if (enemiesWait != 0 && p->status == 1) {
        eBrainSetStatus(p, 8);
    }

    do {
        changed = 0;
        switch (p->status) {
        case 0: {
            int n;
            int cnt;
            int found;
            int boyIdx;
            int girlIdx;

            found = 0;
            cnt = 0;
            for (n = 0; n < boyTargetNum; n++) {
                EBSlot *e = boyTargets[n];
                if (p == e) {
                    found = 1;
                    break;
                }
                if (e != 0)
                    cnt++;
            }
            boyIdx = -1;
            if (found && cnt + eBrainBoyChaseCount <= 31)
                boyIdx = n;
            found = 0;
            cnt = 0;
            for (n = 0; n < girlTargetNum; n++) {
                EBSlot *e = girlTargets[n];
                if (p == e) {
                    found = 1;
                    break;
                }
                if (e != 0)
                    cnt++;
            }
            girlIdx = -1;
            if (found && cnt + eBrainGirlChaseCount <= 31)
                girlIdx = n;

            {
                int order[3];
                int i;

                memset(order, 0, sizeof(order));
                i = 0;
                if (boyIdx >= 0) {
                    if (girlIdx >= 0) {
                        if (boyTargets[boyIdx]->dist[0] < girlTargets[girlIdx]->dist[1]) {
                            order[0] = 1;
                            order[1] = 2;
                        } else {
                            order[0] = 2;
                            order[1] = 1;
                        }
                    } else {
                        order[0] = 1;
                    }
                } else if (girlIdx >= 0) {
                    order[0] = 2;
                }
                for (; order[i] != 0; i++) {
                    if (order[i] == 1) {
                        if (eBrainCanSeeTarget(gop, boyGObj)) {
                            eBrainSetStatus(p, 1);
                            break;
                        }
                    } else {
#ifdef ICO_HOST
                        if (ico_opt_yorda_safe()) {
                            continue;
                        }
#endif
                        if (eBrainCanSeeTarget(gop, girlGObj)) {
                            eBrainSetStatus(p, 2);
                            break;
                        }
                    }
                }
            }
            if (p->status != 0) {
#ifdef ICO_HOST
                /* PC port: one of the two indices is -1 here (the enemy chases
                   the one it found). The EE stores to the word before the
                   array (ebrain.o 0x9fc and 0xa08, .bss girlTargets at 0x400
                   and boyTargets at 0x380 after ebrainSlots[32]):
                   girlTargets[-1] is boyTargets[31] and boyTargets[-1] is
                   ebrainSlots[31].owner. The host's statics are not laid out
                   so; it clears those two (DIVERGENCES.md D13) */
                if (girlIdx >= 0) {
                    girlTargets[girlIdx] = 0;
                } else {
                    boyTargets[31] = 0;
                }
                if (boyIdx >= 0) {
                    boyTargets[boyIdx] = 0;
                } else {
                    ebrainSlots[31].owner = 0;
                }
#else
                boyTargets[boyIdx] = girlTargets[girlIdx] = 0;
#endif
                changed = 1;
            }
            break;
        }
        case 1:
            p->target = boyGObj;
            if (p->chaseFrames >= 181) {
#ifdef ICO_HOST
                if (ico_opt_yorda_safe()) {
                    break;
                }
#endif
                if (p->dist[1] < p->dist[0] + 250000.0f) {
                    if (eBrainCanSeeTarget(gop, girlGObj)) {
                        eBrainSetStatus(p, 2);
                        changed = 1;
                    }
                }
            }
            break;
        case 2:
            p->target = girlGObj;
            break;
        case 5:
            p->target = girlGObj;
            if (p->dist[0] < 250000.0f) {
                if (eBrainCanSeeTarget(gop, boyGObj)) {
                    eBrainSetStatus(p, 1);
                    changed = 1;
                }
            }
            if (girlHolder == 0) {
                eBrainSetStatus(p, 1);
                changed = 1;
            }
            break;
        case 4:
            p->target = isysGObjSearchFromObjLayoutID(
                eBrainGetTargetGeneratorFromLabel(((GObj *)gop)->labelId));
            break;
        case 6:
            p->target = 0;
            break;
        case 3:
            if (eBrainCanSeeTarget(gop, girlGObj)) {
                eBrainSetStatus(p, 2);
                changed = 1;
            }
            break;
        case 8:
            p->target = boyGObj;
            if (enemiesWait == 0) {
                eBrainSetStatus(p, 0);
            }
            break;
        }
    } while (changed);

    if (girlHolder != 0 && girlHolder != gop && p->status == 2) {
        eBrainSetStatus(p, 5);
    }
    return p;
}

inline void eBrainSendMes(GObj *gop, int mes)
{
    EBSlot *p = eBrainGetPacket(gop);

    p->message = mes;
    switch (mes) {
    case 9:
        girlHolder = gop;
        break;
    case 10:
        if (girlHolder == gop)
            girlHolder = 0;
        eBrainSetStatus(p, 7);
        break;
    case 4:
    case 5:
        if (girlHolder == gop)
            girlHolder = 0;
        break;
    }
}
