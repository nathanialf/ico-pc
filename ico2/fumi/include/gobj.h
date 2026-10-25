/*
 * ico2/fumi/include/gobj.h
 *
 * The declarations of what gobj.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GOBJ_H
#define GOBJ_H

#include "typedef.h"

struct GObj;

/* gobj.c's `inline` functions, in the order of their definitions' out-of-line
   copies at the end of the object (first-declaration order).  The functions that hand an
   object back return it as the untyped handle every caller takes. */
void isysGObjInit(int n);
void isysGObjRemoveAll(void);
void isysGObjMove(struct GObj *g, unsigned char kind, int prio);
void isysGObjMoveHead(struct GObj *g, unsigned char kind, int prio);
void *isysGObjAddAfterGObj(void (*fn)(struct GObj *), struct GObj *other);
void *isysGObjAddBeforeGObj(void (*fn)(struct GObj *), struct GObj *other);

int isysGetNbAllocedGObjs(void);

void isysGObjAlloc(int n);
void isysGObjRemove(struct GObj *g);
void isysGObjKindTableAdd(struct GObj *g, int kind);
void isysGObjKindTableRemove(struct GObj *g);
void isysGObjMoveAfterGObj(struct GObj *self, struct GObj *other);
void isysGObjMoveBeforeGObj(struct GObj *self, struct GObj *other);
void *isysGObjAdd(void (*fn)(struct GObj *), int a1, int a2);
void *isysGObjAddHead(void (*fn)(struct GObj *), int a1, int a2);
void *isysGObjSearchFromObjLayoutID(int layoutId);
void *isysGObjSearchFromObjKindID_begin(int kind);
void *isysGObjSearchFromObjKindID_next(struct GObj *g);
void *isysGObjSearchFromLabelTypeID(int labelType);
void *isysGObjGetExist_begin(void);
void *isysGObjGetExist_next(struct GObj *start);
void isysGObjActiveLink(int bit, int set);
void isysGObjActiveDlLink(int link, int on);
extern int debugKindOld;

#endif /* GOBJ_H */
