/*
 * ico2/fumi/include/gobj_process.h
 *
 * The declarations of what gobj_process.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GOBJ_PROCESS_H
#define GOBJ_PROCESS_H

#include "typedef.h"
#include "thread.h"

/* one process node of a game object (0x94 bytes): 0x4 owner GObj, 0x8 prev,
   0xC next; the owner keeps the list head at +0x2C and the tail at +0x30 */
typedef struct GProc {     /* field names derived */
    struct GProc *self;    /* 0x00, the node itself while the entry is in use, 0 when free */
    GObj *owner;           /* 0x04 */
    struct GProc *prev;    /* 0x08 */
    struct GProc *next;    /* 0x0C */
    int noThread;          /* 0x10, set when the process runs inline instead of on a thread */
    unsigned int priority; /* 0x14, the list is kept in ascending priority order */
    int active;            /* 0x18 */
    void (*func)();        /* 0x1C, the body of an inline process */
    ICO_WORD_PTR(void *) arg; /* 0x20, a sub thread's request record (RequestClipCollision, RequestGetWayBegin) */
    IOSThread thread; /* 0x24, the thread a threaded process runs on */
} GProc;              /* derived name */

/* gobj_process.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
inline void isysGObjProcessAlloc(unsigned int max);
GProc *isysGObjProcAdd(GObj *g, void (*fn)(), int noThread, int pri);
GProc *isysGObjProcAddS(GObj *g, void (*fn)(), int noThread, int pri, long long stack);
GProc *isysGObjProcAddGOppArg(GObj *g, void (*fn)(), int noThread, int pri);
void isysGObjProcPause(char *self);
void isysGObjProcPauseAll(struct GObj *p);
void isysGObjProcPausePtr(void *gobj, void (*func)());
void isysGObjProcActive(char *self);
void isysGObjProcActiveAll(void *gobj);
void isysGObjProcRemoveAll(void *gobj);
void isysGObjProcThreadSleep(int frames);
GProc *isysGObjProcAddSGOppArg(GObj *g, void (*fn)(), int noThread, int pri, int stack);
void isysGObjProcActivePtr(void *gobj, void (*func)());
void free_gobj_process_resource(char *self);
void isysGObjProcRemove(GProc *p);
void isysGObjProcessInit(unsigned int max);

#endif /* GOBJ_PROCESS_H */
