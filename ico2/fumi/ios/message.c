#include "debug.h"
#include "memory.h"
#include <eekernel.h>
#include <eeregs.h>
#include "ios.h"
#include "thread.h"
#include "debug_exception.h"
#include "main.h"
#include "message.h"
#include <assert.h>

typedef struct IosMsg { /* field names derived */
    char pad0[68];
    struct IosMsg *next; /* 0x44 */
} IosMsg;                /* derived name */

/* the event thread iosMsgSetEvent spawns, one 16576-byte block: the
   IOSThread, its 16 KB stack and three trailing words of its own
   bookkeeping. */
typedef struct MsgEventThread { /* field names derived */
    IOSThread th;               /* 0x0000 */
    char stack[16384];          /* 0x0070, the thread's stack */
    char pad4070[32];
    IosMsgQueue *queue; /* 0x4090 */
    int val;            /* 0x4094 */
    int intc;           /* 0x4098 */
} MsgEventThread;       /* derived name */

/* the interrupt handler iosMsgSetEvent installs, defined at the end */
static int signal_handler(int cause);

/* the queue registered against each semaphore id */
static IosMsgQueue *msgQueueTable[256]; /* derived name */

static void deq_mes_th(IosMsgQueue *self)
{
    IosMsg *msg = self->head;

    if (msg != 0) {
        self->head = msg->next;
        msg->next = 0;
        SignalSema(self->sema);
    }
}

void iosMsgQueueCreate(IosMsgQueue *q, int *buf, int size)
{
    q->buf = buf;
    q->rd = 0;
    q->num = 0;

    q->head = 0;
    q->size = size;

    q->sem.initCount = 0;
    q->sem.maxCount = size;
    q->sem.attr = 1;
    q->sema = CreateSema(&q->sem);
    if (q->sema < 0) {
        debug_assert("ios/message.c", 120);
        __assert("ios/message.c", 120, "0");
    }
    msgQueueTable[q->sema] = q;
    debug_StdPrintfDummy("sema[%d] = %p\n", q->sema, q);
}

void iosMsgQueueDestroy(IosMsgQueue *q)
{
    debug_StdPrintfDummy("%p\n", q);
    if (q->sema < 0) {
        debug_assert("ios/message.c", 136);
        __assert("ios/message.c", 136, "0");
    }
    msgQueueTable[q->sema] = 0;
    DeleteSema(q->sema);
}

/* a message send, which iosMsgSend and send_signal_message inline */
static inline int msgSend(IosMsgQueue *q, int val, int mode) /* derived name */
{
    struct SemaParam st;

    if (q == 0) {
        debug_StdPrintfDummy("msg:null message queue\n");
        debug_assert("ios/message.c", 293);
        __assert("ios/message.c", 293, "0");
    }
    ReferSemaStatus(q->sema, &st);
    if (q->num == st.maxCount) {
        if (mode != 1) {
            debug_StdPrintfDummy("MSG NO SEND\n");
            return -1;
        }
        WaitSema(q->sema);
    }
    q->buf[(q->rd + q->num) % st.maxCount] = val;
    q->num += 1;
    if (st.numWaitThreads > 0) {
        SignalSema(q->sema);
    }
    return 0;
}

static void send_signal_message(void)
{
    MsgEventThread *self = (MsgEventThread *)iosGetIOSThreadFromId(GetThreadId());
    MsgEventThread *th = self->th.arg;

    th_sig = &self->th;
    debug_StdPrintfDummy("%d %d\n", self->th.id, th->val);

    for (;;) {
        iosThreadSleep();
        msgSend(th->queue, th->val, 0);
    }
}

void iosMsgSetEvent(int intc, IosMsgQueue *q, int val)
{
    MsgEventThread *th;
    int ret;

    if (q == 0) {
        debug_StdPrintfDummy("evt:null message queue\n");
    }
    th = iosMallocDebug(ios_partition_event, 16576, "ios/message.c", 453);
    iosThreadCreate(&th->th, 4, send_signal_message, th, th->stack, 16384, 11);
    th->queue = q;
    th->val = val;
    th->intc = intc;
    iosThreadStart(&th->th);
    debug_StdPrintfDummy("where is here\n");
    AddIntcHandler(intc, signal_handler, -1);
    ret = EnableIntc(intc);
    debug_StdPrintfDummy("evt:%d\n", ret);
    debug_StdPrintfDummy("evt:signal added\n");
}

/* .sdata, after the short strings above: the signal thread's record, which
   iosMsgInit's handler wakes */
IOSThread *th_sig = 0;

void iosMsgInit(void)
{
    IosMsgQueue **p = msgQueueTable;
    int i;
    p += 255;
    for (i = 255; i >= 0; i--) {
        *p = 0;
        p--;
    }
}

int iosMsgSend(IosMsgQueue *q, int val, int mode)
{
    return msgSend(q, val, mode);
}

int iosMsgRecv(IosMsgQueue *q, int *out, int mode)
{
    struct SemaParam st;
    if (q == 0) {
        debug_StdPrintfDummy("msg:null message queue\n");
        debug_assert("ios/message.c", 329);
        __assert("ios/message.c", 329, "0");
    }
    ReferSemaStatus(q->sema, &st);
    if (q->num == 0) {
        if (mode != 1)
            return -1;
        WaitSema(q->sema);
    }
    *out = q->buf[q->rd];
    q->rd = (q->rd + 1) % st.maxCount;
    q->num -= 1;
    if (q->num == st.maxCount) {
        if (st.numWaitThreads > 0) {
            SignalSema(q->sema);
        }
    }
    return 0;
}

void iosMsgQueueDestroyAll(void)
{
    IosMsgQueue *p;
    int i;
    IosMsgQueue **q = msgQueueTable;
    i = 255;
    do {
        p = *q++;
        if (p != 0) {
            iosMsgQueueDestroy(p);
        }
        i--;
    } while (i >= 0);
}

static int signal_handler(int cause)
{
    if (cause == 2) {
#ifdef ICO_HOST
        odd_even = 1; /* GS_CSR field bit read as 0: no GS on the host */
#else
        volatile unsigned long long *reg = (volatile unsigned long long *)GS_CSR;
        odd_even = (int)(((*reg >> 13) & 1) ^ 1);
#endif
        iWakeupThread(th_sig->id);
    }
    return 0;
}
