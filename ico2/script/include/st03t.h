/*
 * ico2/script/include/st03t.h
 *
 * The declarations of what st03t.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST03T_H
#define ST03T_H

#include "typedef.h"

void actSt03tEneChk(GObj *volatile self);
void actSt03tGirlCamEndChk(GObj *volatile self);
void actSt03tGirlCamStartChk(GObj *volatile self);

void actSt03tSwitchLChk(GObj *volatile self);
void actSt03tSwitchLUpChk(GObj *volatile self);
void actSt03tSwitchRChk(GObj *volatile self);
void actSt03tSwitchRUpChk(GObj *volatile self);

void actSt03tWayOffChk(GObj *volatile self);
void actSt03tWayOnChk(GObj *volatile self);

#endif /* ST03T_H */
