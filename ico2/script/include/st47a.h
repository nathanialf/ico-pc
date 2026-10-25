/*
 * ico2/script/include/st47a.h
 *
 * The declarations of what st47a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST47A_H
#define ST47A_H

#include "typedef.h"

struct SqEntry;

/* st47a.o's .sdata globals */
extern struct SqEntry *sekizo47a;
extern struct SqEntry *hane1up;
extern struct SqEntry *hane2up;
extern struct SqEntry *hane1down;
extern struct SqEntry *hane2down;
extern int sekizo_47a;
extern unsigned char sekizo_47a_vol;
/* The stage's object entry points; st47a.c defines them inline, and gcc
   outputs them at the end of the file in this order. */
void actSt47aSekizo1(GObj *volatile self);
void actSt47aSekizo2(GObj *volatile self);
void actSt47aSekizo3(GObj *volatile self);
void actSt47aSekizo4(GObj *volatile self);
void actSt47aHane1(GObj *volatile self);
void actSt47aHane2(GObj *volatile self);
void actSt47aRope(GObj *volatile self);
void actSt47aRopeSwing(GObj *volatile self); /* derived name */
void actSt47aBarricade(GObj *volatile self);
void actSt47aExit(GObj *volatile self);
void actSt47aExit2(GObj *volatile self);
void actSt47aEne(GObj *volatile self);
void actSt47aEnemy2(GObj *volatile self);
void actSt47aEnemy3(GObj *volatile self);
void actSt47aEnemy4(GObj *volatile self);
void actSt47aBarricadeChk(GObj *volatile self);
void actSt47aEneChk(GObj *volatile self);
void actSt47aExit2Chk(GObj *volatile self);
void actSt47aExitChk(GObj *volatile self);
void actSt47aGirlWay(GObj *volatile self);
void actSt47aHane1Down(GObj *volatile self);
void actSt47aHane1Main(GObj *volatile self);
void actSt47aHane1Switch(GObj *volatile self);
void actSt47aHane1Up(GObj *volatile self);
void actSt47aHane1_1Girl(GObj *volatile self);
void actSt47aHane1_2Girl(GObj *volatile self);
void actSt47aHane2Down(GObj *volatile self);
void actSt47aHane2Girl(GObj *volatile self);
void actSt47aHane2Main(GObj *volatile self);
void actSt47aHane2Switch(GObj *volatile self);
void actSt47aHane2Up(GObj *volatile self);
void actSt47aRopeChk(GObj *volatile self);
void actSt47aSekizo1Chk(GObj *volatile self);

#endif /* ST47A_H */
