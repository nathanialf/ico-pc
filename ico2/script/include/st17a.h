/*
 * ico2/script/include/st17a.h
 *
 * The declarations of what st17a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST17A_H
#define ST17A_H

#include "typedef.h"

struct SqEntry;

/* st17a.o's .sdata globals */
extern struct SqEntry *cam;
void actLinkTestChk(GObj *volatile self);
void actSt17aDoorDownChk(GObj *volatile self);
void actSt17aDoorDownEffect(GObj *volatile self);
void actSt17aDoorUpChk(GObj *volatile self);
void actSt17aDoorUpEffect(GObj *volatile self);


void actSt17aGirlWay(GObj *volatile self);
void actSt17aHasiChk(GObj *volatile self);
void actSt17aHasiEffect(GObj *volatile self);

void actSt17aIntro(GObj *volatile self);
void actSt17aIntroChk(GObj *volatile self);

#endif /* ST17A_H */
