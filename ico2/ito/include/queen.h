/*
 * ico2/ito/include/queen.h
 *
 * The declarations of what queen.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef QUEEN_H
#define QUEEN_H

#include "typedef.h"

float GetQueenBallThickness(void);
int InqQueenBarrierExist(void);
float QueenBallRadius(struct GObj *gobj);
int QueenBarrierInqBreakable(void);
float QueenBarrierRadius(struct GObj *gobj);
int QueenInqDead(void);
void QueenStartAttack(void);
void gene_enemy(volatile ICO_WORD g);
void subQueenBrainMain(volatile ICO_WORD g);
void subQueenControl(volatile ICO_WORD g);

#endif /* QUEEN_H */
