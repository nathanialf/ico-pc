/*
 * ico2/script/include/st05e.h
 *
 * The declarations of what st05e.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST05E_H
#define ST05E_H

#include "typedef.h"

struct SqEntry;

/* st05e.o's .sdata globals */
extern struct SqEntry *solar;
void actSt05eSolarChk(GObj *volatile self);

void actSt05eWaterFlagOn(GObj *volatile self);

void actSt05eWaterMain(GObj *volatile self);
void actSt05eWaterStop(GObj *volatile self);
void actSt05eWaterSwitch(GObj *volatile self);

#endif /* ST05E_H */
