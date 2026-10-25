/*
 * ico2/script/include/st13a.h
 *
 * The declarations of what st13a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST13A_H
#define ST13A_H

#include "typedef.h"

struct SqEntry;

/* st13a.o's .sdata globals */
extern struct SqEntry *st13a_up;
extern struct SqEntry *st13a_down;

extern struct SqEntry *sekizo13a;

extern unsigned int st13a_yure;
extern unsigned char st13a_yure_vol;
extern int sekizo_13a;
extern unsigned char sekizo_13a_vol;

void actSt13aChainNG(GObj *volatile self);
void actSt13aChainOK(GObj *volatile self);

void actSt13aCheckChk(GObj *volatile self);
void actSt13aElevMain(GObj *volatile self);
void actSt13aElevSwitch(GObj *volatile self);

void actSt13aElevUp(GObj *volatile self);


void actSt13aSekizoChk(GObj *volatile self);

#endif /* ST13A_H */
