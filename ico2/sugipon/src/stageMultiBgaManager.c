#include "StageAnimation.h"
#include "multiBgaManager.h"
#include "quaternion.h"
#include "Matrix.h"
#include "main.h"

/* the thirty multi-BGA slots and the animation each one is playing */
static BgaDisp stageBga[30]; /* derived name */

static BgaPlayNode *stageBgaAnim[30]; /* derived name */

/* the number of stage animations entered */
static int stageBgaCount = 0; /* derived name */

#include "stageMultiBgaManager.h"
#include <stdio.h>

/* PC port: a stage animation the stage has not loaded makes no play node
   (stage_MakePlayBgAnimation returns 0) and the PS2 code below writes the
   effect's position into it.  The PS2 never gets there: the motions and
   objects that enter a stage animation run only in the stages that load it.
   The model viewer (port/game/model_viewer.c) and an unforeseen path can, so
   the effect is skipped, leaving the slots as they were
   (docs/port/DIVERGENCES.md GL-10). */
static void stageBgaNoNode(int kind)
{
    static int reported;

    if (!reported) {
        reported = 1;
        fprintf(stderr,
                "stage: no animation node for effect kind %d (stage %d): the effect is skipped\n",
                kind, stage_no);
    }
}

inline void InitStageMultiBgaManager(void)
{
    int i;

    for (i = 0; i < 30; i++) {
        stageBga[i] = *(BgaDisp *)&InitialBgaMultiAnimeState;
        stageBgaAnim[i] = 0;
    }
    stageBgaCount = 0;
}

inline void EntryStageMultiBgaManagerWithStay(int kind, void *pos, void *rot, int stay)
{
    BgaPlayNode *node = stage_MakePlayBgAnimation(kind);

    if (node == 0) { /* PC port: stageBgaNoNode */
        stageBgaNoNode(kind);
        return;
    }
    stageBgaAnim[stageBgaCount] = node;
    _CopyVector(stageBgaAnim[stageBgaCount]->pos, pos);
    CopyQuaternion(stageBgaAnim[stageBgaCount]->rot, rot);
    EntryMultiBgaManager(stageBga, stageBgaCount++, kind, pos, rot);
    stageBga[stageBgaCount - 1].stay = stay;
    if (stageBgaCount >= 30) {
        stageBgaCount = 0;
    }
}

static void EntryStageMultiBgaManager_ntscaug6(int kind, void *pos, void *rot)
{
    stageBgaAnim[stageBgaCount] = stage_MakePlayBgAnimation(kind);
    _CopyVector(stageBgaAnim[stageBgaCount]->pos, pos);
    CopyQuaternion(stageBgaAnim[stageBgaCount]->rot, rot);
    EntryMultiBgaManager(stageBga, stageBgaCount++, kind, pos, rot);
    if (stageBgaCount >= 30) {
        stageBgaCount = 0;
    }
}

inline void EntryStageMultiBgaManager(int kind, void *pos, void *rot)
{
    if (ICO_NTSCAUG6) {
        EntryStageMultiBgaManager_ntscaug6(kind, pos, rot);
        return;
    }
    EntryStageMultiBgaManagerWithStay(kind, pos, rot, 0);
}

inline void EntryStageMultiBgaManagerSensitiveWithStay(int kind, void *pos, void *rot, void *vel,
                                                       int stay)
{
    BgaPlayNode *node = stage_MakePlayBgAnimation(kind);

    if (node == 0) { /* PC port: stageBgaNoNode */
        stageBgaNoNode(kind);
        return;
    }
    stageBgaAnim[stageBgaCount] = node;
    _CopyVector(stageBgaAnim[stageBgaCount]->pos, pos);
    CopyQuaternion(stageBgaAnim[stageBgaCount]->rot, rot);
    EntryMultiBgaManagerSensitive(stageBga, stageBgaCount++, kind, pos, rot, vel);
    stageBga[stageBgaCount - 1].stay = stay;
    if (stageBgaCount >= 30) {
        stageBgaCount = 0;
    }
}

static void EntryStageMultiBgaManagerSensitive_ntscaug6(int kind, void *pos, void *rot, void *vel)
{
    stageBgaAnim[stageBgaCount] = stage_MakePlayBgAnimation(kind);
    _CopyVector(stageBgaAnim[stageBgaCount]->pos, pos);
    CopyQuaternion(stageBgaAnim[stageBgaCount]->rot, rot);
    EntryMultiBgaManagerSensitive(stageBga, stageBgaCount++, kind, pos, rot, vel);
    if (stageBgaCount >= 30) {
        stageBgaCount = 0;
    }
}

inline void EntryStageMultiBgaManagerSensitive(int kind, void *pos, void *rot, void *vel)
{
    if (ICO_NTSCAUG6) {
        EntryStageMultiBgaManagerSensitive_ntscaug6(kind, pos, rot, vel);
        return;
    }
    EntryStageMultiBgaManagerSensitiveWithStay(kind, pos, rot, vel, 0);
}

static void DispStageMultiBgaManager_ntsc(void)
{
    int i;

    for (i = 0; i < 30; i++) {
        if (stageBgaAnim[i] == 0) {
            continue;
        }
        if (stage_DispBgAnimation(&stageBgaAnim[i])) {
            stageBgaAnim[i] = 0;
            continue;
        }
        if (systemStatus[5] == 0) {
            _AddVector(stageBgaAnim[i]->pos, stageBgaAnim[i]->pos, stageBga[i].vel);
        }
    }
}

static void DispStageMultiBgaManager_aug6(void)
{
    int i;

    for (i = 0; i < 30; i++) {
        if (stageBgaAnim[i] == 0) {
            continue;
        }
        if (stage_DispBgAnimation(&stageBgaAnim[i])) {
            stageBgaAnim[i] = 0;
            continue;
        }
        _AddVector(stageBgaAnim[i]->pos, stageBgaAnim[i]->pos, stageBga[i].vel);
    }
}

void DispStageMultiBgaManager(void)
{
    if (ICO_NTSC) {
        DispStageMultiBgaManager_ntsc();
        return;
    }
    if (ICO_AUG6) {
        DispStageMultiBgaManager_aug6();
        return;
    }
    int i;

    for (i = 0; i < 30; i++) {
        if (stageBgaAnim[i] == 0) {
            continue;
        }
        if (stageBga[i].stay) {
            stage_DispBgAnimationNoFinish(&stageBgaAnim[i]);
        } else {
            if (stage_DispBgAnimation(&stageBgaAnim[i])) {
                stageBgaAnim[i] = 0;
                continue;
            }
        }
        if (systemStatus[5] == 0) {
            _AddVector(stageBgaAnim[i]->pos, stageBgaAnim[i]->pos, stageBga[i].vel);
        }
    }
}
