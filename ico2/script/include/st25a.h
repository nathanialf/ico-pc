/*
 * ico2/script/include/st25a.h
 *
 * The declarations of what st25a.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ST25A_H
#define ST25A_H

#include "typedef.h"

struct SqEntry;

void actConte12(GObj *volatile self);
void actConte12Jimaku(GObj *volatile self);
void actConte13Jimaku(GObj *volatile self);
void actSt25aQueenAppearChk(GObj *volatile self);
void actSt25aQueenDeadChk(GObj *volatile self);
void actSt25aQueenTalkChk(GObj *volatile self);
extern const char faceShadowTex[];
extern const char faceShadowTex00[];
extern const char wall01Tex[];
/* st25a.o's .sdata globals: ADPCM request slots */
extern struct SqEntry *conte12;
extern struct SqEntry *sd2;
extern struct SqEntry *dead;
void BoySekikaTexScroll(void);
extern ActMail queen_before_mes[]; /* st25a.o .data: the queen-before actor mail list */
/* st25a.o .data: the queen-appear actor mail list, per disc */
extern ActMail queen_appear_mes_pal[];
extern ActMail queen_appear_mes_ntsc[];
extern ActMail queen_appear_mes_aug6jun26[];

#define queen_appear_mes                                                                           \
    PAL_NTSC_AUG6JUN26(queen_appear_mes_pal, queen_appear_mes_ntsc, queen_appear_mes_aug6jun26)
#endif /* ST25A_H */
