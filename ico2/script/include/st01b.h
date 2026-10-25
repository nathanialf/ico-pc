/*
 * ico2/script/include/st01b.h
 *
 * The declarations of what st01b.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST01B_H
#define ST01B_H

#include "typedef.h"

/* st01b.o's .sdata globals; st01b_floor is declared with its record in st01b.c */
extern unsigned int st01b_yure;
extern unsigned char st01b_yure_vol;
void actSt01bEneChk(GObj *volatile self);
void actSt01bFloorChk(GObj *volatile self);

void actSt01bWayOffChk(GObj *volatile self);
void actSt01bWayOnChk(GObj *volatile self);

#endif /* ST01B_H */
