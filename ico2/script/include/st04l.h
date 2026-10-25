/*
 * ico2/script/include/st04l.h
 *
 * The declarations of what st04l.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST04L_H
#define ST04L_H

#include "typedef.h"

struct SqEntry;

/* st04l.o's .sdata globals */
extern struct SqEntry *ball1_4l;
extern struct SqEntry *ball2_4l;
extern struct SqEntry *ball3_4l;
extern struct SqEntry *crest1;
extern struct SqEntry *crest2;
extern struct SqEntry *solar4l;
extern unsigned int oriup4c;
extern unsigned char oridown4c;
extern unsigned int st04l_yure;
extern unsigned char st04l_yure_vol;
extern unsigned int sekizo_4l;
extern unsigned char sekizo_4l_vol;
void actSt04eSolarBeamChk(GObj *volatile self);
void actSt04lBrg1Chk(GObj *volatile self);
void actSt04lBrg1WayChk(GObj *volatile self);
void actSt04lBrg2Chk(GObj *volatile self);
void actSt04lBrg2WayChk(GObj *volatile self);
void actSt04lC1BallMain(GObj *volatile self);
void actSt04lC1BallSwitch(GObj *volatile self);
void actSt04lC1BallTurn(GObj *volatile self);
void actSt04lC2BallMain(GObj *volatile self);
void actSt04lC2BallSwitch(GObj *volatile self);
void actSt04lC2BallTurn(GObj *volatile self);
void actSt04lC3BallMain(GObj *volatile self);
void actSt04lC3BallSwitch(GObj *volatile self);
void actSt04lC3BallTurn(GObj *volatile self);
void actSt04lCrest2Main(GObj *volatile self);
void actSt04lCrest3Main(GObj *volatile self);
void actSt04lCrestMain(GObj *volatile self);
void actSt04lGondolaCharaChk(GObj *volatile self);
void actSt04lGondolaChk(GObj *volatile self);
void actSt04lMonyou01Chk(GObj *volatile self);
void actSt04lMonyou02Chk(GObj *volatile self);
void actSt04lMonyou03Chk(GObj *volatile self);
void actSt04lMonyou04Chk(GObj *volatile self);
void actSt04lMonyou05Chk(GObj *volatile self);
void actSt04lMonyou06Chk(GObj *volatile self);
void actSt04lMonyou07Chk(GObj *volatile self);
void actSt04lOri2Chk(GObj *volatile self);
void actSt04lOriChk(GObj *volatile self);
void actSt04lOriRopeCutLChk(GObj *volatile self);
void actSt04lOriRopeCutRChk(GObj *volatile self);
void actSt04lRope1Chk(GObj *volatile self);
void actSt04lRope2Chk(GObj *volatile self);
void actSt04lRope3Chk(GObj *volatile self);
void actSt04lRope4Chk(GObj *volatile self);
void actSt04lSekizoChk(GObj *volatile self);
void actSt04lStairChk(GObj *volatile self);
void actSt04lSwordChk(GObj *volatile self);
void actSt04lTorch1_1Chk(GObj *volatile self);
void actSt04lTorch1_2Chk(GObj *volatile self);
void actSt04lTorch2_1Chk(GObj *volatile self);
void actSt04lTorch2_1XLChk(GObj *volatile self);
void actSt04lTorch2_2Chk(GObj *volatile self);
void actSt04lTorch2_2XLChk(GObj *volatile self);
void actSt04lTorch3_1Chk(GObj *volatile self);
void actSt04lTorch3_2Chk(GObj *volatile self);
void actSt04lTuriChk(GObj *volatile self);
void turnBall(GObj *self, int flag, int anim, int gobj1, int gobj2);

#endif /* ST04L_H */
