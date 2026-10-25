/*
 * ico2/script/include/op.h
 *
 * The declarations of what op.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef OP_H
#define OP_H

#include "typedef.h"

struct SqEntry;

/* op.o's .sdata globals */
extern struct SqEntry *op2;
extern struct SqEntry *adpcm_conte01_sea;

extern int opTitleLogoMode;
extern struct SqEntry *titleAdpcm;
/* op.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void actOpDemo03(GObj *volatile self);
inline void actSubMpegReturnPreload(GObj *volatile self);

inline void actSt26aConte01_1_newgame(GObj *volatile self);
inline void actOpDemo02Chk(GObj *volatile self);
inline void actSt24aConte01_2_Jimaku(GObj *volatile self);
void actOpDemo01_2Chk(GObj *volatile self);
void actOpDemo03Chk(GObj *volatile self);
void actSt13aConte01_3(GObj *volatile self);
void actSt24aConte01_2(GObj *volatile self);

#endif /* OP_H */
