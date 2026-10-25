/*
 * ico2/script/include/st04r.h
 *
 * The declarations of what st04r.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST04R_H
#define ST04R_H

#include "typedef.h"

struct SqEntry;

/* st04r.o's .sdata globals */
extern struct SqEntry *solar4r;
extern struct SqEntry *ball1_4r;
extern struct SqEntry *ball2_4r;
extern struct SqEntry *ball3_4r;
extern struct SqEntry *crest1_4r;
extern struct SqEntry *crest2_4r;
extern struct SqEntry *crest3_4r;
extern struct SqEntry *stair5d;
extern struct SqEntry *sekizo5c;
extern unsigned char st05d_hasi;
extern int st04r_yure;
extern unsigned char st04r_yure_vol;
extern int sekizo_4r;


extern unsigned char sekizo_4r_vol;

void actSt04rBarricadeChk(GObj *volatile self);
void actSt04rBrg1Chk(GObj *volatile self);
void actSt04rBrg1WayChk(GObj *volatile self);
void actSt04rBrg2Chk(GObj *volatile self);
void actSt04rBrg2WayChk(GObj *volatile self);
void actSt04rBrgCommon(GObj *volatile self);
void actSt04rC1BallMain(GObj *volatile self);
void actSt04rC1BallSwitch(GObj *volatile self);
void actSt04rC1BallTurn(GObj *volatile self);
void actSt04rC2BallMain(GObj *volatile self);
void actSt04rC2BallSwitch(GObj *volatile self);
void actSt04rC2BallTurn(GObj *volatile self);
void actSt04rC3BallMain(GObj *volatile self);
void actSt04rC3BallSwitch(GObj *volatile self);
void actSt04rC3BallTurn(GObj *volatile self);
void actSt04rCrest2Main(GObj *volatile self);
void actSt04rCrestMain(GObj *volatile self);
void actSt04rGondolaCharaChk(GObj *volatile self);
void actSt04rSolarBeamChk(GObj *volatile self);
void actSt04rSolarStageChangeChk(GObj *volatile self);
void actSt04rTorch1_1Chk(GObj *volatile self);
void actSt04rTorch1_2Chk(GObj *volatile self);
void actSt04rTorch2_1Chk(GObj *volatile self);
void actSt04rTorch2_1XLChk(GObj *volatile self);
void actSt04rTorch2_2Chk(GObj *volatile self);
void actSt04rTorch2_2XLChk(GObj *volatile self);
void actSt04rTorch3_1Chk(GObj *volatile self);
void actSt04rTorch3_2Chk(GObj *volatile self);

#endif /* ST04R_H */
