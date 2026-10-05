#include "debug.h"
#include "memory.h"
#include "message.h"
#include <eekernel.h>
#include "debug_exception.h"
#include "ios.h"
#include "thread.h"
#include <assert.h>
#include <string.h>

/* The emission-order note: the `inline` functions of this TU have their
 * out-of-line copies at the end of the object, in first-declaration order
 * (thread.h's prototypes, then iosThreadDestroyMgr, declared below, and
 * iosThreadAllQuit, declared by its definition).  Only iosThreadCreate is
 * expanded into another function, iosThreadCreateS and iosThreadInit.  The
 * definitions here are in source order, which fixes the order of the TU's
 * strings in .rodata. */

/* the IOSThread each thread id maps to, the destroy manager's own message
   queue, the boot thread and its 8 KB stack */
static IOSThread *iosThreadTable[256]; /* derived name */

static IosMsgQueue iosThreadDestroyQueue; /* derived name */

static IOSThread iosBootThread; /* derived name */

/* a thread stack, 16-byte aligned as the kernel's CreateThread requires */
static char iosBootStack[8192] __attribute__((aligned(16))); /* derived name */

static void iosThreadMain(void *arg)
{
    int idx = GetThreadId();
    IOSThread *obj = iosThreadTable[idx];
    obj->func(arg);
    if (obj->sleeping == 0) {
        iosThreadSetPri(obj, 33);
    } else {
        iosThreadSetPri(obj, 34);
    }
}

/* 16-byte guard word stamped at both ends of a thread stack */
typedef struct { /* field names derived */
    char c[16];
} IosStackMark; /* derived name */

extern int _gp; /* linker-defined global pointer */

static int n_thread = 0; /* derived name: the number of live IOS threads */

static inline void
iosThreadDestroyMgr(void); /* deferred-tail member; see the emission-order note */

#ifdef ICO_HOST

/* port/platform/diag_host.h */
void ico_host_thread_func(int id, void *func, int priority);

/* iosThreadMessage's and iosThreadJoin's queue: the record and its 8-message
   ring in one allocation */
#define THREAD_JOIN_QUEUE_SIZE ((int)(sizeof(IosMsgQueue) + 8 * sizeof(IosMsgWord)))

_Static_assert(sizeof(void *) != 4 || THREAD_JOIN_QUEUE_SIZE == 80,
               "the EE's 80-byte join queue on 32-bit hosts");

#endif

/* iosThreadCreate, a public function that iosThreadCreateS and iosThreadInit
 * also expand: a plain `inline`, so its out-of-line copy goes to the end of
 * the object. */
inline void iosThreadCreate(IOSThread *th, int no, void (*func)(), void *arg, void *stack,
                            long long stackSize, int pri)
{
    th->param.entry = iosThreadMain;
    th->func = func;

    th->param.stack = stack;
    *(IosStackMark *)stack = *(const IosStackMark *)"<THREAD_SP>....";
    *(IosStackMark *)((char *)stack + stackSize - 16) = *(const IosStackMark *)"<THREAD_SP_END>";

    th->param.stackSize = stackSize - 16;
    th->param.gpReg = &_gp;
    th->param.initPriority = pri;
    th->param.currentPriority = pri;
    th->id = CreateThread(&th->param);
    th->sleeping = 0;

    th->arg = arg;

    if (th->id >= 256) {
        debug_StdPrintfDummy("thr:thread table over flow\n");
        debug_assert(__FILE__, 141);
        __assert(__FILE__, 141, "0");
    } else if (th->id <= 0) {
        debug_StdPrintfDummy("thr:can't create thread\n");
        debug_assert(__FILE__, 145);
        __assert(__FILE__, 145, "0");
    } else {
        iosThreadTable[th->id] = th;
    }
#ifdef ICO_HOST
    /* port/platform/diag_host.c: the thread lines name it by func */
    ico_host_thread_func(th->id, (void *)func, pri);
#endif

    n_thread++;
    debug_StdPrintfDummy("n_thread %d\n", n_thread);
    th->flags &= ~1;

    th->hasQueue = 0;
}

/* iosThreadCreateS: iosThreadCreate over a malloc'd stack.  flags bit 0 marks
 * "this stack came from the heap"; iosThreadDestroyMgr reads it back and
 * frees the stack. */
void iosThreadCreateS(IOSThread *th, int no, void (*func)(), void *arg, void *heap,
                      long long stackSize, int pri)
{
    void *stack;

    stack = iosMallocDebug(heap, stackSize, __FILE__, 173);
    if (stack == 0) {
        debug_StdPrintfDummy("thr:can't create stack\n");
        return;
    }
    iosThreadCreate(th, no, func, arg, stack, stackSize, pri);
    th->flags |= 1;
}

void iosThreadStart(IOSThread *th)
{
    StartThread(th->id, th->arg);
}

void iosThreadStop(IOSThread *th)
{
    if (th == 0) {
        ExitThread();
    } else {
        TerminateThread(th->id);
    }
}

void iosThreadSleep(void)
{
    SleepThread();
}

inline int iosThreadWakeup(IOSThread *th)
{
    return WakeupThread(th->id);
}

/* The destroy-manager thread body.  iosThreadInit creates a thread running
 * this; iosThreadDestroy posts the dying IOSThread to its message queue and
 * this loop does the actual teardown.  Never returns. */
/* .sbss, owned by thread.o and reached only from this file: the manager
   queue's 2-slot message ring. */
static IosMsgWord iosThreadDestroyRing[2]; /* derived name */

static inline void iosThreadDestroyMgr(void)
{
    IOSThread *th;
    int id;

    debug_StdPrintfDummy("iosThreadDestroyMgr() in\n");

    iosMsgQueueCreate(&iosThreadDestroyQueue, iosThreadDestroyRing, 2);
    while (1) {
#ifdef ICO_HOST
        {
            IosMsgWord msg;
            iosMsgRecv(&iosThreadDestroyQueue, &msg, 1);
            th = (IOSThread *)msg;
        }
#else
        iosMsgRecv(&iosThreadDestroyQueue, (int *)&th, 1);
#endif

        id = th->id;
        n_thread--;
        debug_StdPrintfDummy("1:n_thread %d\n", n_thread);
        TerminateThread(id);
        DeleteThread(id);
        if ((th->flags & 1) == (unsigned)1)
            iosFree(iosThreadTable[id]->param.stack);

        if (th->hasQueue) {
            iosMsgQueueDestroy(th->queue);
            iosFree(th->queue);
        }
        iosThreadTable[id] = 0;
    }
}

void iosThreadDestroy(IOSThread *th)
{
    IOSThread *target = th;
    if (th == 0) {
        target = iosThreadTable[GetThreadId()];
    }
    iosMsgSend(&iosThreadDestroyQueue, (IosMsgWord)target, 0);
}

inline int iosThreadGetPri(IOSThread *th)
{
    IOSThread **base;
    if (th == 0) {
        int idx;
        base = iosThreadTable;
        idx = GetThreadId();
        th = base[idx];
    }
    return th->param.currentPriority;
}

void iosThreadSetPri(IOSThread *th, int pri)
{
    IOSThread *v;
    v = th;
    if (v == 0) {
        v = iosThreadTable[GetThreadId()];
    } else {
        v = th;
    }
    v->param.currentPriority = pri;
    ChangeThreadPriority(v->id, pri);
}

inline IOSThread *iosGetIOSThreadFromId(unsigned int id)
{
    IOSThread *ret;
    if (id < 257)
        goto valid;
    debug_StdPrintfDummy("thr:id out of range\n");
    ret = 0;
    goto out;
valid:
    ret = iosThreadTable[id];
out:
    return ret;
}

void iosThreadMessage(int msg)
{
    IOSThread *obj = iosThreadTable[GetThreadId()];
    int q;
    if (obj->hasQueue == 0) {
        void *r;
        obj->hasQueue = 1;
#ifdef ICO_HOST
        /* the queue record and its 8-message ring behind it: 48 + 8 * 4 = 80
           bytes on a 32-bit host, as on the EE */
        r = iosMallocDebug(ios_partition_root, THREAD_JOIN_QUEUE_SIZE, __FILE__, 478);
        obj->queue = r;
        iosMsgQueueCreate(r, (IosMsgWord *)((IosMsgQueue *)r + 1), 8);
#else
        r = iosMallocDebug(ios_partition_root, 80, __FILE__, 478);
        obj->queue = r;
        iosMsgQueueCreate(r, (int *)((char *)r + 48), 8);
#endif
    }
    q = iosMsgSend(obj->queue, msg, 0);
    debug_StdPrintfDummy("th:msg %d\n", q);
}

inline int iosThreadJoin(IOSThread *th)
{
    IosMsgWord buf[4];
    if (th->hasQueue == 0) {
        void *r;
        th->hasQueue = 1;
#ifdef ICO_HOST
        r = iosMallocDebug(ios_partition_root, THREAD_JOIN_QUEUE_SIZE, __FILE__, 506);
        th->queue = r;
        iosMsgQueueCreate(r, (IosMsgWord *)((IosMsgQueue *)r + 1), 8);
#else
        r = iosMallocDebug(ios_partition_root, 80, __FILE__, 506);
        th->queue = r;
        iosMsgQueueCreate(r, (int *)((char *)r + 48), 8);
#endif
    }
    iosMsgRecv(th->queue, buf, 1);
    debug_StdPrintfDummy("th:thread joined\n");
    return buf[0];
}

void iosThreadName(IOSThread *th, const char *name)
{
    strcpy(th->name, name);
}

void iosThreadSuspend(IOSThread *th)
{
    SuspendThread(th->id);
}

void iosThreadResume(IOSThread *th)
{
    ResumeThread(th->id);
}

inline int iosThreadCancelWakeup(IOSThread *th)
{
    int v;
    if (th == 0) {
        v = GetThreadId();
    } else {
        v = th->id;
    }
    return CancelWakeupThread(v);
}

inline int iosSemaCreate(IosSema *self, int initCount, int maxCount, int option)
{
    int rv;
    self->param.initCount = initCount;
    self->param.maxCount = maxCount;
    self->param.option = option;
    rv = CreateSema(&self->param);
    self->id = rv;
    if (rv < 0) {
        debug_StdPrintfDummy("sem: can't create %d\n", rv);
        debug_assert(__FILE__, 604);
        __assert(__FILE__, 604, "0");
        return self->id;
    }
    return 0;
}

inline int iosSemaDelete(IosSema *self)
{
    int rv = DeleteSema(self->id);
    if (rv < 0) {
        debug_StdPrintfDummy("sem: can't delete %d\n", self->id);
        debug_assert(__FILE__, 624);
        __assert(__FILE__, 624, "0");
        return rv;
    }
    return 0;
}

inline int iosSemaWait(IosSema *self)
{
    int rv = ReferSemaStatus(self->id, &self->param);
    if (rv < 0) {
        debug_StdPrintfDummy("sem: wait error? %d\n", self->id);
        return rv;
    }
    WaitSema(self->id);
    return 0;
}

inline int iosSemaSignal(IosSema *self)
{
    int v;
    int rv;
    v = SignalSema(self->id);
    rv = 0;
    if (v < 0) {
        debug_StdPrintfDummy("sem: signal error? %d\n", self->id);
        rv = v;
    }
    return rv;
}

inline int iosSemaReferStatus(IosSema *self)
{
    int rv = ReferSemaStatus(self->id, &self->status);
    if (rv < 0) {
        debug_StdPrintfDummy("sem: refer error? %d\n", self->id);
        debug_assert(__FILE__, 688);
        __assert(__FILE__, 688, "0");
        return rv;
    }
    return 0;
}

void iosThreadInit(void)
{
    iosThreadCreate(&iosBootThread, 0, iosThreadDestroyMgr, 0, iosBootStack, 8192, 13);
    iosThreadStart(&iosBootThread);
}

/* The last function of the TU, never called. */
inline void iosThreadAllQuit(int self)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (iosThreadTable[i] != 0 && i != self) {
            iosThreadDestroy(iosThreadTable[i]);
        }
    }
}
