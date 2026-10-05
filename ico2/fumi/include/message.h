/*
 * ico2/fumi/include/message.h
 *
 * The declarations of what message.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <eekernel.h>

/* One message: an int, or an address (thread.c, cdvd.c, mcard.c and
   StageManager.c send pointers), so pointer-wide on the host. A ring handed
   to iosMsgQueueCreate holds `size` of them, and iosMsgRecv stores one. */
#ifdef ICO_HOST
typedef __INTPTR_TYPE__ IosMsgWord; /* derived name */
#else
typedef int IosMsgWord; /* derived name */
#endif

/* a message queue: a ring of messages guarded by a kernel semaphore */
typedef struct IosMsgQueue { /* field names derived */
    IosMsgWord *buf;         /* 0x00, the ring */
    int rd;                  /* 0x04, the next slot iosMsgRecv reads */
    int num;                 /* 0x08, messages held */
    int size;                /* 0x0C, slots in the ring */
    struct IosMsg *head;     /* 0x10, the first waiting sender (message.c's IosMsg) */
    struct SemaParam sem;    /* 0x14 */
    int sema;                /* 0x2C, the semaphore id */
} IosMsgQueue;               /* derived name */

/* message.o's .sdata global: the signal thread's record */
extern struct IOSThread *th_sig;
void iosMsgInit(void);
void iosMsgQueueCreate(IosMsgQueue *q, IosMsgWord *buf, int size);
void iosMsgQueueDestroy(IosMsgQueue *q);
int iosMsgRecv(IosMsgQueue *q, IosMsgWord *out, int mode);
int iosMsgSend(IosMsgQueue *q, IosMsgWord val, int mode);
void iosMsgSetEvent(int intc, IosMsgQueue *q, IosMsgWord val);

#endif /* MESSAGE_H */
