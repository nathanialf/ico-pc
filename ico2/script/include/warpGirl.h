/*
 * ico2/script/include/warpGirl.h
 *
 * The declarations of what warpGirl.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef WARPGIRL_H
#define WARPGIRL_H

/* warpGirl.o's globals */
extern int warpGirlInStageSet;

extern int warpGirlId;
/* warpGirl.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void warpGirlInit(void);
void warpGirlInStage(int stageNo);
void warpGirlOutStage(int stage, int noSet);

#endif /* WARPGIRL_H */
