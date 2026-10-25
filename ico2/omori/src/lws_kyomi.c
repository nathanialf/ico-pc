#include "sceneManager.h"
#include "memory.h"
#include "StageAnimation.h"
#include "debug.h"
#include <string.h>
#include "typedef.h"
#include "ios.h"
#include "lws_kyomi.h"
#include "main.h"
#include "geometryManager.h"
#include "brain.h"

struct HintInfo { /* field names derived */
    int anim;     /* the stage animation the hint plays */
    int no;
    int time;
    int flags;
};

/* The two 4-byte hint flag sets the save block carries, then one timer per
   hint; Hint_Init clears the whole record. */
static struct {      /* field names derived */
    char save[8];    /* 0x00 */
    float timer[28]; /* 0x08 */
} hintWork;          /* derived name */

/* the record a new hint GObj starts from: no stage, no hint, no time, no
   flags */
static struct HintInfo hintDefault = {-1, -1, -1, 0}; /* derived name */

static GObj *CreateKyomiGObj_ntsc(int no)
{
    (void)no; /* PC port: the European parameter, unused here */
    float lay[16];
    GObj *gobj;

    memset(lay, 0, 64);
    lay[8] = 1.0f;
    lay[9] = 1.0f;
    lay[10] = 1.0f;
    gobj = CreateLayoutedGObj(60, 69, -1, 0, lay, 1, 7, 0);
    brainStatusDefaultSet(&brainGirl, gobj, 1);
    return gobj;
}

static GObj *CreateKyomiGObj_aug6(int no)
{
    (void)no; /* PC port: the European parameter, unused here */
    float lay[16];
    GObj *gobj;

    memset(lay, 0, 64);
    lay[8] = 1.0f;
    lay[9] = 1.0f;
    lay[10] = 1.0f;
    gobj = CreateLayoutedGObj(59, 68, -1, 0, lay, 1, 7, 0);
    brainStatusDefaultSet(&brainGirl, gobj, 1);
    return gobj;
}

GObj *CreateKyomiGObj(int no)
{
    if (ICO_NTSC) {
        return CreateKyomiGObj_ntsc(no);
    }
    if (ICO_AUG6) {
        return CreateKyomiGObj_aug6(no);
    }
    float lay[16];
    GObj *gobj;
    struct HintInfo *hint;
    int i;

    memset(lay, 0, 64);
    lay[8] = 1.0f;
    lay[9] = 1.0f;
    lay[10] = 1.0f;
    gobj = CreateLayoutedGObj(61, 75, -1, 0, lay, 1, 7, 0);
    hint = (struct HintInfo *)iosMallocDebug(ios_partition_sugipon, 16, "src/lws_kyomi.c", 101);
    GOBJ_SUB(gobj)->work = hint;
    *hint = hintDefault;
    hint->anim = no;
    for (i = 0; i < 28; i++) {
        if (hintTable[i].stage == stage_no && hintTable[i].no == no) {
            hint->no = i;
            hint->time = (int)(hintTable[i].time * 60.0f * 60.0f *
                               (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f);
        }
    }
    brainStatusDefaultSet(&brainGirl, gobj, 1);
    return gobj;
}

/* the hint timers, a window onto the per-hint elapsed-time array */
static float *hintTimers; /* derived name */

static void LwsKyomiGeo_ntscaug6(GObj *gobj)
{
    brainSubLevelGop(gobj, 0.1f);
}

void LwsKyomiGeo(GObj *gobj)
{
    if (ICO_NTSCAUG6) {
        LwsKyomiGeo_ntscaug6(gobj);
        return;
    }
    struct HintInfo *hint;
    int i;
    unsigned char fin;

    hint = GOBJ_SUB(gobj)->work;
    hint->flags &= ~1;
    for (i = 0; i < 28; i++) {
        if (hintTable[i].stage == stage_no && (hintTable[i].flags & 1) == 0) {
            if ((((unsigned int)hintTable[i].flags >> 1) & 1) == 0 && i == hint->no) {
                hint->flags |= 1;
            }
            break;
        }
    }
    if (hint->no != -1) {
        fin = stage_CheckAnimationFinish(hint->anim);
        if (hint->flags & 1) {
            hintTimers[hint->no] += 1.0f;
            if ((float)hint->time < hintTimers[hint->no]) {
                if (fin != 0) {
                    stage_SetAnimation(hint->anim, 1, 0);
                }
            }
        } else {
            if (fin == 0) {
                stage_SetAnimation(hint->anim, -1, -2);
            }
        }
    }
    brainSubLevelGop(gobj, 0.1f);
}

/* Clear one 4-byte half of the save block, then pack one flag bit of every
 * hint into it.  `buf` is the half being packed and `off` its byte offset
 * inside the block. */
#define MAKE_HINT_SAVE_BITS(buf, off, bit) /* derived name */                                      \
    for (i = 0; i < 4; i++) {                                                                      \
        hintWork.save[(off) + i] = 0;                                                              \
    }                                                                                              \
    for (i = 0; i < 28; i++) {                                                                     \
        if (((unsigned int)hintTable[i].flags >> (bit)) & 1) {                                     \
            int m = 1 << (i % 8);                                                                  \
            (buf)[i / 8] |= m;                                                                     \
        }                                                                                          \
    }

void MakeHintSaveInfo(void)
{
    int i;

    MAKE_HINT_SAVE_BITS(hintWork.save, 0, 0);
    MAKE_HINT_SAVE_BITS(hintWork.save + 4, 4, 1);
}

/* the inverse of MAKE_HINT_SAVE_BITS, unpacking one 4-byte half back into the
 * flag bit */
#define READ_HINT_SAVE_BITS(buf, bit) /* derived name */                                           \
    end = 0;                                                                                       \
    for (k = 0; k < 28; k++) {                                                                     \
        (hintTable + k)->flags &= ~(1 << (bit));                                                   \
    }                                                                                              \
    n = 0;                                                                                         \
    for (k = 0; k < 4; k++) {                                                                      \
        for (j = 0; j < 8; j++) {                                                                  \
            if ((((unsigned char *)(buf))[k] >> j) & 1) {                                          \
                (hintTable + n)->flags |= 1 << (bit);                                              \
            }                                                                                      \
            n++;                                                                                   \
            if (n < 28) {                                                                          \
                continue;                                                                          \
            }                                                                                      \
            end = 1;                                                                               \
            break;                                                                                 \
        }                                                                                          \
        if (end) {                                                                                 \
            break;                                                                                 \
        }                                                                                          \
    }

void ReadHintSaveInfo(void)
{
    int j;
    int k;
    int n;
    int end;

    READ_HINT_SAVE_BITS(hintWork.save, 0);
    READ_HINT_SAVE_BITS(hintWork.save + 4, 1);
}

void SetParamKyomiGObj(GObj *gobj, float *root, float *param)
{
    float pos[4];
    float lv;
    int on1;
    int on2;

    on1 = 1;
    lv = param[0];
    if (param[1] < 0.5f) {
        on1 = 0;
    }
    on2 = 1;
    if (param[2] < 0.5f) {
        on2 = 0;
    }
    if (debug_lwskyomi_lookonly != 0) {
        if (on2 != 0) {
            on1 = 1;
        }
    }
    SetDirectRootPosition(gobj, root);
    UpdateRootMatrix(gobj);
    GetRootPosition(pos, gobj);
    brainSetLevelGop(gobj, lv, on1, on2);
}

void FinishHint(int no)
{
    (hintTable + no)->flags |= 1;
}

void SleepHint(int no)
{
    (hintTable + no)->flags |= 2;
}

void WakeupHint(int no)
{
    (hintTable + no)->flags &= ~2;
}

int IsTopHint(GObj *gobj)
{
    if (gobj->kind == 61) {
        struct HintInfo *hint = GOBJ_SUB(gobj)->work;

        if (hint->flags & 1) {
            return 1;
        }
    }
    return 0;
}

void DebugHintStart(GObj *gobj)
{
    struct HintInfo *hint;

    hint = GOBJ_SUB(gobj)->work;
    hintTimers[hint->no] = hint->time;
}

int GetSizeHintSaveInfo(void)
{
    return sizeof(hintWork);
}

char *GetBuffHintSaveInfo(void)
{
    return hintWork.save;
}

void Hint_Init(void)
{
    struct HintDef *p;
    int i;

    hintTimers = hintWork.timer;
    memset(&hintWork, 0, sizeof(hintWork));
    p = hintTable;
    for (i = 0; i < 28; i++) {
        p->flags &= ~2;
        p->flags &= ~1;
        p++;
    }
}
