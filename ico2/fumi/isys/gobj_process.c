#include "ee_view.h"
#include "debug.h"
#include "memory.h"
#include "typedef.h"
#include "gobj_process.h"

/* the process pool and how many entries (sizeof(GProc), 0x94 bytes on the EE) it holds */
static char *procPool; /* derived name */

static int procMax; /* derived name */

#include "thread.h"
#include "ios.h"

void isysGObjProcessInit(unsigned int max)
{
    isysGObjProcessAlloc(max);
}

inline void isysGObjProcessAlloc(unsigned int max)
{
    void *ret = iosMallocDebug(ios_partition_isys, max * sizeof(GProc), "isys/gobj_process.c", 73);
    unsigned int i;
    procMax = max;
    procPool = (char *)ret;
    for (i = 0; i < max; i++) {
        ((GProc *)(procPool + i * sizeof(GProc)))->self = 0;
    }
}

static inline GProc *alloc_gobj_process(void) /* derived name */
{
    unsigned int i;
    unsigned int j;

    for (i = 0; i < procMax; i++) {
        if (((GProc *)(procPool + i * sizeof(GProc)))->self == 0) {
            break;
        }
    }
    if (i == procMax) {
        debug_StdPrintfDummy("isys:not enough memory for GObj\n");
        debug_StdPrintfDummy("isys:not enough memory for GObj\n");
        for (j = 0; j < procMax; j++) {
            debug_StdPrintfDummy("id %d %x %x \n", ((GProc *)(procPool + j * sizeof(GProc)))->self,
                                 ((GProc *)(procPool + j * sizeof(GProc)))->func,
                                 ICO_RAW(int, procPool + j * 0x94, 0x5C,
                                         ((GProc *)(procPool + j * sizeof(GProc)))->thread.func));
        }
        return 0;
    }
    return (GProc *)(procPool + i * sizeof(GProc));
}

static GProc *isysGObjProcAdd_(GObj *gobj, GObj *arg, void (*func)(), unsigned char noThread,
                               int pri, long long stackSize)
{
    GProc *p;
    GProc *h;
    GProc *t;

    if (func == 0) {
        return 0;
    }
    p = alloc_gobj_process();
    if (p == 0) {
        debug_StdPrintfDummy("isys:not enough memory for GObjProcess\n");
        return 0;
    }
    p->self = p;
    if (noThread == 0) {
        iosThreadCreateS(&p->thread, 1, func, arg ? (void *)arg : (void *)p, ios_partition_isys,
                         stackSize, pri);
        iosThreadStart(&p->thread);
        p->func = 0;
    } else {
        p->func = func;
    }
    p->noThread = noThread;
    p->owner = gobj;
    p->active = 1;
    p->priority = pri;
    h = gobj->procHead;
    if (h == 0) {
        p->next = 0;
        p->prev = 0;
        gobj->procHead = p;
        gobj->procTail = p;
    } else if ((unsigned int)pri < h->priority) {
        p->next = 0;
        p->prev = gobj->procHead;
        p->prev->next = p;
        gobj->procHead = p;
    } else {
        t = gobj->procTail;
        if (!((unsigned int)pri < t->priority)) {
            p->next = t;
            p->prev = 0;
            t->prev = p;
            gobj->procTail = p;
        } else {
            while (!((unsigned int)pri < h->prev->priority)) {
                h = h->prev;
            }
            p->next = h;
            p->prev = h->prev;
            h->prev = p;
            p->prev->next = p;
        }
    }
    return p;
}

inline GProc *isysGObjProcAddGOppArg(GObj *gobj, void (*func)(), int noThread, int pri)
{
    return isysGObjProcAdd_(gobj, 0, func, noThread & 0xFF, pri, 0x1800);
}

inline GProc *isysGObjProcAdd(GObj *gobj, void (*func)(), int noThread, int pri)
{
    return isysGObjProcAdd_(gobj, gobj, func, noThread & 0xFF, pri, 0x1800);
}

inline GProc *isysGObjProcAddS(GObj *gobj, void (*func)(), int noThread, int pri,
                               long long stackSize)
{
    return isysGObjProcAdd_(gobj, gobj, func, noThread & 0xFF, pri, stackSize);
}

inline GProc *isysGObjProcAddSGOppArg(GObj *a, void (*b)(), int c, int d, int e)
{
    return isysGObjProcAdd_(a, 0, b, c & 0xFF, d, e);
}

inline void isysGObjProcPause(char *self)
{
    ((GProc *)self)->active = 0;
}

inline void isysGObjProcPauseAll(GObj *p)
{
    GProc *cur = ((GObj *)p)->procHead;
    if (cur != 0) {
        do {
            cur->active = 0;
            cur = cur->prev;
        } while (cur != 0);
    }
}

inline void isysGObjProcPausePtr(void *gobj, void (*func)())
{
    GProc *p = ((GObj *)gobj)->procHead;
    while (p != 0) {
        if (p->func == func) {
            p->active = 0;
        }
        p = p->prev;
    }
}

inline void isysGObjProcActive(char *self)
{
    ((GProc *)self)->active = 1;
}

inline void isysGObjProcActiveAll(void *gobj)
{
    GProc *p = ((GObj *)gobj)->procHead;
    while (p != 0) {
        p->active = 1;
        p = p->prev;
    }
}

inline void isysGObjProcActivePtr(void *gobj, void (*func)())
{
    GProc *p = ((GObj *)gobj)->procHead;
    while (p != 0) {
        if (p->func == func) {
            p->active = 1;
        }
        p = p->prev;
    }
}

inline void free_gobj_process_resource(char *self)
{
    ((GProc *)self)->self = 0;
}

static void cut_gobj_process_link(GProc *p)
{
    if (p == 0) {
        debug_StdPrintfDummy("isys:null GObjProcess\n");
        return;
    }
    if (p->next == 0 && p->prev == 0) {
        /* not linked into a list */
    } else {
        if (p->next != 0) {
            p->next->prev = p->prev;
        }
        if (p->prev != 0) {
            p->prev->next = p->next;
        }
    }
    if (p == p->owner->procHead) {
        p->owner->procHead = p->prev;
    }
    if (p == p->owner->procTail) {
        p->owner->procTail = p->next;
    }
}

void isysGObjProcRemove(GProc *p)
{
    int v0;
    cut_gobj_process_link(p);
    v0 = p->noThread;
    p->self = 0;
    if (v0 != 0) {
        return;
    }
    return iosThreadDestroy(&p->thread);
}

inline void isysGObjProcRemoveAll(void *gobj)
{
    GProc *p = ((GObj *)gobj)->procHead;
    while (p != 0) {
        isysGObjProcRemove(p);
        p = p->prev;
    }
}

inline void isysGObjProcThreadSleep(int frames)
{
    while (frames != 0) {
        iosThreadStop(0);
        frames--;
    }
}
