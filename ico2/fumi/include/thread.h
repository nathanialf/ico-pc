/*
 * ico2/fumi/include/thread.h
 *
 * The declarations of what thread.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef THREAD_H
#define THREAD_H

#include <eekernel.h>

/* --- ios thread object (SCE ee_thread_t at offset 0 + ICO bookkeeping) --- */
typedef struct IOSThread {     /* field names derived */
    struct ThreadParam param;  /* 0x00 the kernel's thread record */
    int id;                    /* 0x30 kernel thread id                */
    void *arg;                 /* 0x34 argument handed to func         */
    void (*func)();            /* 0x38 body run by iosThreadMain       */
    int flags;                 /* 0x3C, bit 0: the stack was allocated (iosThreadCreateS) */
    int sleeping;              /* 0x40 read by iosThreadMain           */
    char pad44[4];
    int hasQueue;              /* 0x48 */
    struct IosMsgQueue *queue; /* 0x4C, the join queue iosThreadMessage creates */
    char name[16];             /* 0x50 */
    char pad60[16];            /* 0x60: the record is 0x70 bytes, the gap
                            between the boot thread and its stack in
                            .bss */
} IOSThread;                   /* derived name */

/* --- ios semaphore object: the parameter block CreateSema is handed (and
   iosSemaWait refers the status into), the status iosSemaReferStatus last
   read, and the kernel's semaphore id --- */
typedef struct IosSema {     /* field names derived */
    struct SemaParam param;  /* 0x00 */
    struct SemaParam status; /* 0x18 */
    int id;                  /* 0x30 */
} IosSema;                   /* derived name */

/* thread.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void iosThreadCreate(IOSThread *th, int no, void (*func)(), void *arg, void *stack, long long stackSize,
                     int pri);

int iosThreadGetPri(IOSThread *th);
IOSThread *iosGetIOSThreadFromId(unsigned int id);
int iosThreadWakeup(IOSThread *th);
int iosThreadJoin(IOSThread *th);
int iosThreadCancelWakeup(IOSThread *th);
int iosSemaCreate(IosSema *self, int initCount, int maxCount, int option);
int iosSemaDelete(IosSema *self);
int iosSemaWait(IosSema *self);
int iosSemaSignal(IosSema *self);
int iosSemaReferStatus(IosSema *self);

/* The entry points thread.c compiles in place. */
void iosThreadCreateS(IOSThread *th, int no, void (*func)(), void *arg, void *heap, long long stackSize,
                      int pri);

void iosThreadDestroy(IOSThread *th);
void iosThreadInit(void);
void iosThreadSetPri(IOSThread *th, int pri);
/* omori/src/camera-editor.c alone passes an argument, through its own
   one-argument declaration; ios/thread.c defines it (void). */
void iosThreadSleep(void);
void iosThreadStart(IOSThread *th);
void iosThreadStop(IOSThread *th);
void iosThreadMessage(int msg);

#endif /* THREAD_H */
