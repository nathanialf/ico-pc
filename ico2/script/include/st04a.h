/*
 * ico2/script/include/st04a.h
 *
 * The declarations of what st04a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST04A_H
#define ST04A_H

#include "typedef.h"

struct SqEntry;

/* st04a.o's .sdata globals */
extern struct SqEntry *gate1st;

extern struct SqEntry *gate1;

extern struct SqEntry *gate_ready_l;
extern struct SqEntry *gate_ready_r;

extern struct SqEntry *torch;
extern int gate_yure_low;
extern unsigned char gate_yure_low_vol;
extern int yure1;
extern unsigned char vol1;

extern int yure2;
extern unsigned char vol2;

void actConte09(GObj *volatile self);
void actConte09Jimaku(GObj *volatile self);
void actConte09_2(GObj *volatile self);
void actConte09_3(GObj *volatile self);
void actConte09_3Jimaku(GObj *volatile self);
void actSt04aConte06(GObj *volatile self);
void actSt04aConte06Jimaku(GObj *volatile self);

void actSt04aEnvSe(GObj *volatile self);
void actSt04aEnvSeWakare1(GObj *volatile self);
void actSt04aEnvSeWakare2(GObj *volatile self);

void actSt04aGateChk(GObj *volatile self);
void actSt04aGateLChk(GObj *volatile self);
void actSt04aGateOpen2Chk(GObj *volatile self);
void actSt04aGateOpen2ReadyChk(GObj *volatile self);
void actSt04aGateOpen3Chk(GObj *volatile self);
void actSt04aGateOpenChk(GObj *volatile self);
void actSt04aGateRChk(GObj *volatile self);
void actSt04aGirlSitChk(GObj *volatile self);
void actSt04aTorch1Chk(GObj *volatile self);
void actSt04aTorchAllFlagfChk(GObj *volatile self);

#endif /* ST04A_H */
