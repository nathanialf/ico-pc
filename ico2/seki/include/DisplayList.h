/*
 * ico2/seki/include/DisplayList.h
 *
 * The declarations of what DisplayList.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef DISPLAYLIST_H
#define DISPLAYLIST_H

/* DisplayList.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void dl_Out(void);
void dl_SetDLPriority(int pri);
void dl_OpenDma(int id, const void *addr, int qwc);
int dl_GetPri(void);
void dl_CloseDma(void);
void dl_Init(void);

void dl_Clear(void);
void dl_Swap(void);

#endif /* DISPLAYLIST_H */
