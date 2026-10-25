/*
 * ico2/script/include/st22a.h
 *
 * The declarations of what st22a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST22A_H
#define ST22A_H

#include "typedef.h"

struct SqEntry;

/* st22a.o's .sdata globals */
extern struct SqEntry *lightning;

void actSt22aIntroSub(GObj *volatile self);

#endif /* ST22A_H */
