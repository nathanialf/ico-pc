/*
 * ico2/ito/include/stage_orient.h
 *
 * The declarations of what stage_orient.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef STAGE_ORIENT_H
#define STAGE_ORIENT_H

#include "typedef.h" /* VECTOR */

void StageOrientInit(void);
int StageOrientGet(VECTOR *ret, int stA, int stB);

int OtherStagePositionGet(float *ret, int stA, int stB, float *pos);

#endif /* STAGE_ORIENT_H */
