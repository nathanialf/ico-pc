/*
 * ico2/script/include/st20a.h
 *
 * The declarations of what st20a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST20A_H
#define ST20A_H

#include "typedef.h"

struct SqEntry;

/* st20a.o's .sdata globals */
extern struct SqEntry *brg20a;

extern struct SqEntry *gondola_up;
extern struct SqEntry *gondola_down;
extern unsigned int st20a_yure;
extern unsigned char st20a_yure_vol;

void actSt20aBridgeDown(GObj *volatile self);
void actSt20aBridgeMain(GObj *volatile self);
void actSt20aBridgeSwitch(GObj *volatile self);
void actSt20aEneChk(GObj *volatile self);
void actSt20aExitChk(GObj *volatile self);
void actSt20aFenceDownChk(GObj *volatile self);
void actSt20aFenceDownChk2(GObj *volatile self);
void actSt20aFenceUpChk(GObj *volatile self);
void actSt20aFenceUpChk2(GObj *volatile self);
void actSt20aGondolaDown(GObj *volatile self);
void actSt20aGondolaMain(GObj *volatile self);
void actSt20aGondolaSwitch(GObj *volatile self);
void actSt20aGondolaUp(GObj *volatile self);

#endif /* ST20A_H */
