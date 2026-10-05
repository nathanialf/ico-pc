/*
 * ico2/sugipon/include/a_p_1.h
 *
 * The declarations of what a_p_1.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef A_P_1_H
#define A_P_1_H

#include "sceneManager.h"

struct GObj;

int AP1JumpReq(struct GObj *self, int mode, void *vel);
int AP1MotReq(struct GObj *self, int mode);
int AP1MotReqForce(struct GObj *self, int mode);
int AP1Turn(struct GObj *self, short angle);
ICO_WORD_PTR(char *) GetAP1Mode(struct GObj *self);
int GetAP1SpecType(struct GObj *self);
struct GObj *MakeAP1GObj(SObjSimpleSetting *setting);
void SetAP1VisualState(struct GObj *self, int visible);

#endif /* A_P_1_H */
