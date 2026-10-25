/*
 * ico2/omori/include/brain.h
 *
 * The declarations of what brain.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef BRAIN_H
#define BRAIN_H

#include "typedef.h"

/* the girl's brain record */
extern Brain brainGirl;
void OverrideBrainStatusByGObj(Brain *b, GObj *gobj, float levelCap, float rate, float capStep);
void brainAddLevelGirl(float lv);
void brainAddLevelGirlDetail(int flag, float lv);

void brainAddLevelGop(GObj *gobj, float lv);
int brainCheckView(Brain *b, BrainTarget *t);
void brainClsTargetLevel(Brain *b);
float brainGetLevel(Brain *b, BrainTarget *t);
void brainGetTarget(Brain *b);
int brainDecTargetTimer(GObj *gobj);
void brainInit(void);
void brainInitGirlSet(GObj *girl, GObj *cur);
void brainLevelProcess(Brain *b);
void brainLockGirl(void);
void brainSetLevelGop(GObj *gobj, float lv, int lookOnly, int alwaysSeen);
void brainSetSpMode(void);
void brainStatusDefaultSet(Brain *b, GObj *gobj, int idx);
BrainTarget *brainStatusSet(Brain *b, GObj *gobj, float levelCap, float capStep, float rate);
void brainSubLevelGop(GObj *gobj, float lv);
void brainUnlockGirl(void);

#endif /* BRAIN_H */
