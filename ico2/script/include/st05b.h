/*
 * ico2/script/include/st05b.h
 *
 * The declarations of what st05b.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST05B_H
#define ST05B_H

#include "typedef.h"

struct SqEntry;

/* st05b.o's .sdata globals */
extern struct SqEntry *sekizo5b;

extern int sekizo_5b;
extern unsigned char sekizo_5b_vol;
void actSt05bGirlWay(GObj *volatile self);
void actSt05bSekizoChk(GObj *volatile self);

#endif /* ST05B_H */
