/*
 * ico2/script/include/st08a.h
 *
 * The declarations of what st08a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST08A_H
#define ST08A_H

#include "typedef.h"

void actSt08aDoorMain(GObj *volatile self);
void actSt08aDoorSwitch(GObj *volatile self);
void actSt08aDoorUp(GObj *volatile self);
void actSt08aEne1Chk(GObj *volatile self);
void actSt08aEne2Chk(GObj *volatile self);

void actSt08aIntroChk(GObj *volatile self);

#endif /* ST08A_H */
