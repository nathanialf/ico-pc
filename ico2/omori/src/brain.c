#include "debug.h"
#include "act-game.h"
#include "commonact.h"
#include "ebrain.h"
#include "typedef.h"
#include "brain.h"
#include "main.h"
#include "gv.h"
#include "gamesys.h"

static inline void brainSetTargetTimer(BrainTarget *t) /* derived name */
{
    int n;

    if (t->gobj != 0) {
        n = (int)objKindData[t->gobj->kind].targetTime;
        if (n != -1) {
            n = n * ((60 - systemStatus[0] * 10) / systemStatus[1]);
        }
    } else {
        n = -1;
    }
    t->timer = n;
}

Brain brainGirl = {0};

static void brainAddLevel(BrainTarget *t, float lv);
static void brainSetLevel(Brain *b, BrainTarget *t, float lv);

void brainAddLevelGirl(float lv)
{
    if (brainGirl.cur != 0) {
        brainAddLevel(brainGirl.cur, lv);
    }
}

void brainInit(void)
{
    Brain *b = &brainGirl;
    int i;

    b->girl = 0;
    b->cur = 0;
    b->spMode = 0;
    b->spCount = 0;
    for (i = 0; i < 40; i++) {
        b->tgt[i].gobj = 0;
    }
    b->threshold = 0.0f;
    b->idx = -1;
    b->targetType = 0;
    b->lock = 0;
    eBrainInit();
}

void OverrideBrainStatusByGObj(Brain *b, GObj *gobj, float levelCap, float rate, float capStep)
{
    BrainTarget *t;
    int i;

    for (i = 0; i < 40; i++) {
        if (b->tgt[i].gobj == gobj) {
            t = &b->tgt[i];
            t->levelCap = levelCap;
            t->capStep = capStep;
            t->rate = rate;
            t->level = 0.0f;
            return;
        }
    }
    /* "failed to override the brain level" */
    debug_StdPrintfDummy("ブレインレベルのオーバーライドに失敗しました\n");
}

static inline void brainSetTargetSub(Brain *b, GObj *gobj, float lvl, int k) /* derived name */
{
    float capStep = objKindData[k].brainCapStep;
    float rate = objKindData[k].brainRate;
    BrainTarget *t;
    int i;

    for (i = 0; i < 40; i++) {
        if (b->tgt[i].gobj == 0) {
            break;
        }
    }
    if (i == 40) {
        return;
    }
    t = &b->tgt[i];
    t->gobj = gobj;
    t->level = 0.0f;
    t->levelCap = lvl;
    t->capStep = capStep;
    t->rate = rate;

    t->lookOnly = 0;
    t->alwaysSeen = 0;
    *(int *)&t->lookOnly &= ~0x10000;
    brainSetTargetTimer(t);
}

void brainStatusDefaultSet(Brain *b, GObj *gobj, int idx)
{
    GenGeo *d = objLayout + idx;
    int k = d->kind;

    /* bit 20 of the layout row's flags puts the object in the girl's brain */
    if ((d->flags >> 20) & 1) {
        if (objKindData[k].brainLevel != 0) {
            brainSetTargetSub(b, gobj, (float)objKindData[k].brainLevel, k);
        }
    }
}

static inline void brainLevelUp(BrainTarget *t) /* derived name */
{
    float d = t->rate;

    if (t->level <= t->levelCap) {
        float r;
        t->level = t->level + d;
        if (t->level < 0.0f) {
            r = 0.0f;
        } else if (t->level > t->levelCap) {
            r = t->levelCap;
        } else {
            r = t->level;
        }
        t->level = r;
    }
}

/* whether the girl sees the target, brainCheckView's test inlined into
   brainLevelProcess */
static inline int brainTargetInView(Brain *b, BrainTarget *t) /* derived name */
{
    if (t->alwaysSeen != 0) {
        return 1;
    }
    return ACTGameView_Check(b->girl, t->gobj) != 0;
}

void brainLevelProcess(Brain *b)
{
    int i;

    b->threshold = b->threshold - _ACTGame_GetParamF(24) * 0.1f;
    if (b->threshold < b->minThreshold) {
        b->threshold = b->minThreshold;
    }
    for (i = 0; i < 40; i++) {
        BrainTarget *t = &b->tgt[i];

        if (t->gobj == 0) {
            continue;
        }
        if (t->gobj->active == 0) {
            t->level = 0.0f;
            continue;
        }
        if (b->lock != 0) {
            t->level = 0.0f;
            continue;
        }
        if (t != b->cur && t->level > 1.9 && girlGObj != 0 &&
            ((int)(GOBJ_ACT(girlGObj)->flags20.ll >> 27) & 1)) {
            float r;
            /* 3.40282347e+38f is FLT_MAX */
            if (ACTGameViewSimple_Check(b->girl, t->gobj) != 0) {
                t->level = t->level - 0.002;
            } else {
                t->level = t->level - 0.005;
            }
            if (t->level < 1.9) {
                r = 1.9f;
            } else if (t->level > 3.40282347e+38f) {
                r = 3.40282347e+38f;
            } else {
                r = t->level;
            }
            t->level = r;
            continue;
        }
        if (brainTargetInView(b, t) == 0) {
            continue;
        }
        brainLevelUp(t);
        if (t->levelCap < t->level) {
            float r;
            t->level = t->level - t->rate / 10.0f;
            if (t->level < t->levelCap) {
                r = t->levelCap;
            } else if (t->level > 3.40282347e+38f) {
                r = 3.40282347e+38f;
            } else {
                r = t->level;
            }
            t->level = r;
        }
    }
}

/* the target's level as the chooser weighs it, the current target holding
   the threshold on top: brainGetLevel's body inlined into brainGetTarget */
static inline float brainTargetLevel(Brain *b, BrainTarget *t) /* derived name */
{
    if (b->cur == t) {
        return t->level + b->threshold;
    }
    return t->level;
}

void brainGetTarget(Brain *b)
{
    BrainTarget *best = 0;
    BrainTarget *t;
    float r;
    /* the winner index */
    volatile int idx;
    int n;
    int lv;
    int i;

    for (i = 0; i < 40; i++) {
        if (b->tgt[i].gobj == 0) {
            continue;
        }
        t = &b->tgt[i];
        if (best == 0 || brainTargetLevel(b, t) > brainTargetLevel(b, best)) {
            best = t;
            idx = i;
        } else if (best != 0 && brainTargetLevel(b, t) == brainTargetLevel(b, best)) {
            if (girlGObj != 0) {
                if (_DistSqGV(test_CURRENTROOT(t->gobj), test_CURRENTROOT(girlGObj)) <
                    _DistSqGV(test_CURRENTROOT(best->gobj), test_CURRENTROOT(girlGObj))) {
                    best = t;
                    idx = i;
                }
            }
        }
    }

    if (b->spMode != 0) {
        b->spMode = 0;
        b->spCount = b->spCount + 1;
    } else {
        b->spCount = b->spCount - 1;
    }
    if (b->spCount >= 0) {
        n = b->spCount > (60 - systemStatus[0] * 10) / systemStatus[1] / 3 +
                             (60 - systemStatus[0] * 10) / systemStatus[1] / 12
                ? (60 - systemStatus[0] * 10) / systemStatus[1] / 3 +
                      (60 - systemStatus[0] * 10) / systemStatus[1] / 12
                : b->spCount;
    } else {
        n = 0;
    }
    b->spCount = n;

    if ((60 - systemStatus[0] * 10) / systemStatus[1] / 3 < b->spCount) {
        best = b->cur;
        for (i = 0; i < 40; i++) {
            t = &b->tgt[i];
            if (t == best) {
                idx = i;
                break;
            }
        }
    }

    b->idx = -1;
    if (best != 0) {
        b->idx = idx;
        lv = (int)(brainTargetLevel(b, best) - b->threshold);

        b->targetLevel = (float)lv / 10.0f;
        if (b->targetLevel < 0.0f) {
            r = 0.0f;
        } else if (b->targetLevel > 1.0f) {
            r = 1.0f;
        } else {
            r = b->targetLevel;
        }
        b->targetLevel = r;
        if (lv > 0) {
            if (best->lookOnly != 0 || lv < 2) {
                b->targetType = 1;
            } else if (lv < 4) {
                b->targetType = 2;
            } else {
                b->targetType = 3;
            }
        } else {
            b->targetType = 0;
        }
    }
}

void brainStatusDel(char *self)
{
    *(int *)(self + 0x0) = 0;
}

float brainGetLevel(Brain *b, BrainTarget *t)
{
    if (b->cur == t) {
        return t->level + b->threshold;
    }
    return t->level;
}

void brainClsTargetLevel(Brain *b)
{
    BrainTarget *t;

    if (b->idx == -1) {
        return;
    }
    t = &b->tgt[b->idx];
    t->level = 0.0f;
    b->targetType = 0;
    t->levelCap = t->levelCap - t->capStep;
    if (t->levelCap < t->level) {
        t->levelCap = t->level;
    }
    *(int *)&t->lookOnly &= ~0x10000;
    brainSetTargetTimer(t);
}

void brainInitGirlSet(GObj *girl, GObj *cur)
{
    Brain *b = &brainGirl;
    BrainTarget *t = b->tgt;
    b->girl = girl;
    if (t->gobj == 0) {
        return;
    }
    do {
        if (t->gobj == cur) {
            b->cur = t;
        }
        ACTGameView_Add(girl, t->gobj);
        t++;
    } while (t->gobj != 0);
}

void brainAddLevelGirlDetail(int flag, float lv)
{
    Brain *b = &brainGirl;

    if (b->cur != 0) {
        brainAddLevel(b->cur, lv);
        if (flag != 0) {
            *(int *)&b->cur->lookOnly |= 0x10000;
        }
    }
}

void brainAddLevelGop(GObj *gobj, float lv)
{
    int i;

    for (i = 0; i < 40; i++) {
        if (brainGirl.tgt[i].gobj == gobj) {
            brainAddLevel(&brainGirl.tgt[i], lv);
        }
    }
}

/* the target list's address: Brain + 0x28 on the EE, the field on the host */
#ifdef ICO_HOST
#define BRAIN_TGT_ADDR(b) ((b)->tgt)
#else
#define BRAIN_TGT_ADDR(b) ((b) + 0x28)
#endif

/* brainSubLevelGop, brainSetLevelGop and brainDecTargetTimer walk the
   targets from an int address; brainGirl.tgt[i] or a BrainTarget pointer
   moves .text in each */
void brainSubLevelGop(GObj *gobj, float lv)
{
    ICO_WORD_PTR(Brain *) brain = (ICO_WORD_PTR(Brain *)) & brainGirl;
    ICO_WORD_PTR(BrainTarget *) tgt = BRAIN_TGT_ADDR(brain);
    int i;

    for (i = 0; i < 40; i++) {
        if (((BrainTarget *)tgt)[i].gobj == gobj) {
            float r;
            ((BrainTarget *)tgt)[i].level = ((BrainTarget *)tgt)[i].level - lv;
            /* 3.40282347e+38f is FLT_MAX */
            if (((BrainTarget *)tgt)[i].level < 0.0f) {
                r = 0.0f;
            } else if (((BrainTarget *)tgt)[i].level > 3.40282347e+38f) {
                r = 3.40282347e+38f;
            } else {
                r = ((BrainTarget *)tgt)[i].level;
            }
            ((BrainTarget *)tgt)[i].level = r;
        }
    }
}

void brainSetLevelGop(GObj *gobj, float lv, int lookOnly, int alwaysSeen)
{
    ICO_WORD_PTR(Brain *) brain = (ICO_WORD_PTR(Brain *)) & brainGirl;
    ICO_WORD_PTR(BrainTarget *) tgt = BRAIN_TGT_ADDR(brain);
    int i;

    for (i = 0; i < 40; i++) {
        if (((BrainTarget *)tgt)[i].gobj == gobj) {
            ((BrainTarget *)tgt)[i].lookOnly = lookOnly;
            ((BrainTarget *)tgt)[i].alwaysSeen = alwaysSeen;
            brainSetLevel((Brain *)brain, &((BrainTarget *)tgt)[i], lv);
        }
    }
}

static inline int brainDecTimer(BrainTarget *e) /* derived name */
{
    int t;

    if (e == 0) {
        return 0;
    }
    if (e->timer == -1) {
        return 0;
    }
    e->timer--;
    if (e->timer >= 0) {
        t = e->timer > 0xFFFFFFF ? 0xFFFFFFF : e->timer;
    } else {
        t = 0;
    }
    e->timer = t;
    return e->timer == 0;
}

int brainDecTargetTimer(GObj *gobj)
{
    ICO_WORD_PTR(Brain *) brain = (ICO_WORD_PTR(Brain *)) & brainGirl;
    ICO_WORD_PTR(BrainTarget *) tgt = BRAIN_TGT_ADDR(brain);
    BrainTarget *e;
    int i;

    for (i = 0; i < 40; i++) {
        if (((BrainTarget *)tgt)[i].gobj == gobj) {
            e = &((BrainTarget *)tgt)[i];
            goto found;
        }
    }
    e = 0;
found:
    return brainDecTimer(e);
}

void brainSetSpMode(void)
{
    brainGirl.spMode = 1;
}

void brainLockGirl(void)
{
    brainGirl.lock = 1;
}

void brainUnlockGirl(void)
{
    brainGirl.lock = 0;
}

static void brainAddLevel(BrainTarget *t, float lv)
{
    float r;

    t->level = t->level + t->rate * lv;
    if (t->level < 0.0f) {
        r = 0.0f;
    } else if (t->level > 10.0f) {
        r = 10.0f;
    } else {
        r = t->level;
    }
    t->level = r;
}

static void brainSetLevel(Brain *b, BrainTarget *t, float lv)
{
    int cond;
    if (t->alwaysSeen != 0) {
        cond = 1;
    } else {
        cond = ACTGameView_Check(b->girl, t->gobj) != 0;
    }
    if (cond) {
        float r;
        t->level = lv;
        if (t->level < 0.0f) {
            r = 0.0f;
        } else if (t->level > 20.0f) {
            r = 20.0f;
        } else {
            r = t->level;
        }
        t->level = r;
    }
}

int brainCheckView(Brain *b, BrainTarget *t)
{
    if (t->alwaysSeen != 0) {
        return 1;
    }
    return ACTGameView_Check(b->girl, t->gobj) != 0;
}
