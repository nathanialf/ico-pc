/*
 * ico2/seki/include/Basic.h
 *
 * The declarations of what Basic.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef BASIC_H
#define BASIC_H

/* Basic.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void malloc_SetPartition(int val);
int malloc_GetPartition(void);
void *mallocseki(int size);
int freeseki(void *ptr);
void resetmallocseki(void);
void *mallocsekistage(int size);
#ifdef ICO_HOST
void *reallocseki(void *ptr, int size);
#else
int reallocseki(int size, int align);
#endif

void dma_init(void);
void matrix_init(void);
void malloc_MemCpy(void *dst, void *src, int size);

/* ABSF and SIGNF: a float's absolute value and its sign (-1, 0 or 1).
 * Texture.c's tex_scrollClut takes the sign of an int through them and
 * BgAnimation.c's _RotTransCurrentMatrixYXZ derives each sine as
 * SIGNF(angle) * sqrt(1 - cos^2).  Light.c's LIGHT_ABS is the same text as
 * ABSF. */
#define ABSF(x) ((x) < 0.0f ? -(x) : (x)) /* derived name */
#define SIGNF(x) ((x) < 0.0f ? -1.0f : ((x) > 0.0f ? 1.0f : 0.0f)) /* derived name */

/* Basic.c's globals */
extern struct DmaChan *dmaVif;
extern struct DmaChan *dmaGif;
extern struct DmaChan *dmaFSp;
extern int fadeStatus;
extern float fadeSpeed;
extern int fadeContinue;
extern unsigned char fadeColor[4];

#endif /* BASIC_H */
