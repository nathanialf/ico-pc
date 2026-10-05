/*
 * port/platform/kernel_host.c
 *
 * The EE kernel calls port/compat/eekernel.h declares. Threads and
 * semaphores are the fiber scheduler's (sched.c); interrupts are a table
 * the host raises (ico_kernel_raise_intc: the vsync, host_loop.c); cache
 * and interrupt-disable calls do nothing on the host.
 *
 * ThreadParam and SemaParam hold only ints and pointers, so their layout
 * does not depend on the game TUs' -mno-ms-bitfields
 * (docs/research/compiler-semantics.md): this file, compiled with the
 * platform ABI, reads the same records the game writes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <eekernel.h>
#include <eeregs.h>
#include "diag_host.h"
#include "kernel_host.h"
#include "sched.h"

/* The last kernel call of the calling thread, for the diagnostics (its
   caller is the game's call site). */
#define NOTE(what, arg) ico_sched_note(what, (int)(arg), __builtin_return_address(0))

/* The link's small-data base (ee-gcc's $gp). thread.c stores its address
   in each ThreadParam; nothing on the host reads it. */
int _gp;

/* --- Interrupts ------------------------------------------------------------ */

#define INTC_CAUSES 15
#define DMAC_CHANNELS 16
#define HANDLERS_PER_CAUSE 8

typedef struct Handler {
    int id;
    int (*fn)(int);
} Handler;

typedef struct HandlerList {
    Handler h[HANDLERS_PER_CAUSE];
    int n;
} HandlerList;

static HandlerList intc_handlers[INTC_CAUSES];

static HandlerList dmac_handlers[DMAC_CHANNELS];

static unsigned int intc_mask;

static unsigned int dmac_mask;

static int next_handler_id = 1;

static volatile unsigned int *vsync_flag;

static volatile unsigned long long *vsync_csr;

#define GS_IMR_ALL_MASKED 0x7F00ull /* every GS interrupt masked */

static unsigned long long gs_imr = GS_IMR_ALL_MASKED;

static short crt_interlace;

static short crt_omode;

static short crt_ffmd;

static int tty_on = -1;

void ico_kernel_reset(void)
{
    int i;
    for (i = 0; i < INTC_CAUSES; i++) {
        intc_handlers[i].n = 0;
    }
    for (i = 0; i < DMAC_CHANNELS; i++) {
        dmac_handlers[i].n = 0;
    }
    intc_mask = 0;
    dmac_mask = 0;
    next_handler_id = 1;
    vsync_flag = 0;
    vsync_csr = 0;
    gs_imr = GS_IMR_ALL_MASKED;
    crt_interlace = 0;
    crt_omode = 0;
    crt_ffmd = 0;
}

/* next == 0 puts the handler first, anything else last (the EE's -1). */
static int add_handler(HandlerList *l, int (*fn)(int), int next)
{
    int i;
    if (fn == NULL || l->n == HANDLERS_PER_CAUSE) {
        return -1;
    }
    if (next == 0) {
        for (i = l->n; i > 0; i--) {
            l->h[i] = l->h[i - 1];
        }
        i = 0;
    } else {
        i = l->n;
    }
    l->h[i].id = next_handler_id++;
    l->h[i].fn = fn;
    l->n++;
    return l->h[i].id;
}

static int remove_handler(HandlerList *l, int id)
{
    int i;
    for (i = 0; i < l->n; i++) {
        if (l->h[i].id == id) {
            for (; i + 1 < l->n; i++) {
                l->h[i] = l->h[i + 1];
            }
            l->n--;
            return 0;
        }
    }
    return -1;
}

static int set_bit(unsigned int *mask, int bit, int limit, int on)
{
    unsigned int old;
    if (bit < 0 || bit >= limit) {
        return -1;
    }
    old = (*mask >> bit) & 1;
    if (on) {
        *mask |= 1u << bit;
    } else {
        *mask &= ~(1u << bit);
    }
    return old != (unsigned int)(on != 0); /* 1 when the state changed */
}

int ico_kernel_raise_intc(int cause)
{
    HandlerList *l;
    int i;
    int n = 0;
    if (cause < 0 || cause >= INTC_CAUSES) {
        return 0;
    }
    ico_sched_interrupt_begin();
    if (cause == ICO_INTC_VBLANK_S && vsync_flag != 0) {
        *vsync_csr = *GS_CSR;
        *vsync_flag = 1;
        vsync_flag = 0;
        vsync_csr = 0;
    }
    if ((intc_mask >> cause) & 1) {
        l = &intc_handlers[cause];
        for (i = 0; i < l->n; i++) {
            l->h[i].fn(cause);
            n++;
        }
    }
    ico_sched_interrupt_end();
    return n;
}

int ico_kernel_intc_enabled(int cause)
{
    return cause >= 0 && cause < INTC_CAUSES && ((intc_mask >> cause) & 1);
}

int AddIntcHandler(int cause, int (*handler)(int cause), int next)
{
    if (cause < 0 || cause >= INTC_CAUSES) {
        return -1;
    }
    return add_handler(&intc_handlers[cause], handler, next);
}

int RemoveIntcHandler(int cause, int id)
{
    if (cause < 0 || cause >= INTC_CAUSES) {
        return -1;
    }
    return remove_handler(&intc_handlers[cause], id);
}

int AddDmacHandler(int channel, int (*handler)(int channel), int next)
{
    if (channel < 0 || channel >= DMAC_CHANNELS) {
        return -1;
    }
    return add_handler(&dmac_handlers[channel], handler, next);
}

int RemoveDmacHandler(int channel, int id)
{
    if (channel < 0 || channel >= DMAC_CHANNELS) {
        return -1;
    }
    return remove_handler(&dmac_handlers[channel], id);
}

int EnableIntc(int cause)
{
    return set_bit(&intc_mask, cause, INTC_CAUSES, 1);
}

int DisableIntc(int cause)
{
    return set_bit(&intc_mask, cause, INTC_CAUSES, 0);
}

int EnableDmac(int channel)
{
    return set_bit(&dmac_mask, channel, DMAC_CHANNELS, 1);
}

int DisableDmac(int channel)
{
    return set_bit(&dmac_mask, channel, DMAC_CHANNELS, 0);
}

int _EnableIntc(int cause)
{
    return EnableIntc(cause);
}

int _DisableIntc(int cause)
{
    return DisableIntc(cause);
}

int _EnableDmac(int channel)
{
    return EnableDmac(channel);
}

int _DisableDmac(int channel)
{
    return DisableDmac(channel);
}

int _iEnableIntc(int cause)
{
    return EnableIntc(cause);
}

int _iDisableIntc(int cause)
{
    return DisableIntc(cause);
}

int _iEnableDmac(int channel)
{
    return EnableDmac(channel);
}

int _iDisableDmac(int channel)
{
    return DisableDmac(channel);
}

int iEnableIntc(int cause)
{
    return EnableIntc(cause);
}

int iDisableIntc(int cause)
{
    return DisableIntc(cause);
}

int iEnableDmac(int channel)
{
    return EnableDmac(channel);
}

int iDisableDmac(int channel)
{
    return DisableDmac(channel);
}

/* No alarms on the host yet: only the movie player (ito/mpeg, not built
   headless) would use them. */
int SetAlarm(unsigned short time, void (*handler)(int id, unsigned short time, void *arg),
             void *arg)
{
    (void)time;
    (void)handler;
    (void)arg;
    return -1;
}

/* All fibers share one host thread and switch only at kernel calls, so
   there is nothing to mask. The EE's DIntr returns whether interrupts were
   enabled; they always are. */
int DIntr(void)
{
    return 1;
}

int EIntr(void)
{
    return 1;
}

/* --- Threads --------------------------------------------------------------- */

int CreateThread(struct ThreadParam *param)
{
    return ico_sched_create_thread(param->entry, param->stack, param->stackSize, param->gpReg,
                                   param->initPriority, param->attr, param->option);
}

int DeleteThread(int id)
{
    return ico_sched_delete_thread(id);
}

int StartThread(int id, void *arg)
{
    NOTE("StartThread", id);
    return ico_sched_start_thread(id, arg);
}

void ExitThread(void)
{
    NOTE("ExitThread", 0);
    ico_sched_exit_thread();
}

void ExitDeleteThread(void)
{
    NOTE("ExitDeleteThread", 0);
    ico_sched_exit_delete_thread();
}

int TerminateThread(int id)
{
    NOTE("TerminateThread", id);
    return ico_sched_terminate_thread(id);
}

int ChangeThreadPriority(int id, int priority)
{
    NOTE("ChangeThreadPriority", priority);
    return ico_sched_change_priority(id, priority, 0);
}

int RotateThreadReadyQueue(int priority)
{
    NOTE("RotateThreadReadyQueue", priority);
    return ico_sched_rotate_ready_queue(priority, 0);
}

int GetThreadId(void)
{
    return ico_sched_get_thread_id();
}

int ReferThreadStatus(int id, struct ThreadParam *info)
{
    IcoThreadInfo ti;
    int r = ico_sched_refer_thread(id, &ti);
    if (r > 0 && info != NULL) {
        info->status = ti.status;
        info->entry = ti.entry;
        info->stack = ti.stack;
        info->stackSize = ti.stack_size;
        info->gpReg = ti.gp;
        info->initPriority = ti.init_priority;
        info->currentPriority = ti.current_priority;
        info->attr = ti.attr;
        info->option = ti.option;
        info->waitType = ti.wait_type;
        info->waitId = ti.wait_id;
        info->wakeupCount = ti.wakeup_count;
    }
    return r;
}

int SleepThread(void)
{
    NOTE("SleepThread", 0);
    return ico_sched_sleep();
}

int WakeupThread(int id)
{
    NOTE("WakeupThread", id);
    return ico_sched_wakeup(id, 0);
}

int iWakeupThread(int id)
{
    return ico_sched_wakeup(id, 1);
}

int CancelWakeupThread(int id)
{
    return ico_sched_cancel_wakeup(id);
}

int SuspendThread(int id)
{
    NOTE("SuspendThread", id);
    return ico_sched_suspend(id, 0);
}

int ResumeThread(int id)
{
    NOTE("ResumeThread", id);
    return ico_sched_resume(id, 0);
}

/* --- Semaphores ------------------------------------------------------------ */

int CreateSema(struct SemaParam *param)
{
    return ico_sched_create_sema(param->initCount, param->maxCount, param->attr, param->option);
}

int DeleteSema(int sema)
{
    NOTE("DeleteSema", sema);
    return ico_sched_delete_sema(sema);
}

int SignalSema(int sema)
{
    NOTE("SignalSema", sema);
    return ico_sched_signal_sema(sema, 0);
}

int iSignalSema(int sema)
{
    return ico_sched_signal_sema(sema, 1);
}

int WaitSema(int sema)
{
    NOTE("WaitSema", sema);
    return ico_sched_wait_sema(sema);
}

int PollSema(int sema)
{
    NOTE("PollSema", sema);
    return ico_sched_poll_sema(sema);
}

int ReferSemaStatus(int sema, struct SemaParam *info)
{
    IcoSemaInfo si;
    int r = ico_sched_refer_sema(sema, &si);
    if (r >= 0 && info != NULL) {
        info->currentCount = si.count;
        info->maxCount = si.max_count;
        info->initCount = si.init_count;
        info->numWaitThreads = si.num_wait;
        info->attr = si.attr;
        info->option = si.option;
    }
    return r;
}

/* --- Miscellany ------------------------------------------------------------ */

void Exit(int status)
{
    ico_diag_log("ico_pc: the game called Exit(%d)", status);
    fflush(stdout);
    fflush(stderr);
    exit(status);
}

/* The host's caches are coherent and nothing does DMA from game memory. */
void FlushCache(int operation)
{
    (void)operation;
}

void SyncDCache(void *start, void *end)
{
    (void)start;
    (void)end;
}

unsigned long long GsGetIMR(void)
{
    return gs_imr;
}

unsigned long long GsPutIMR(unsigned long long imr)
{
    unsigned long long old = gs_imr;
    gs_imr = imr;
    return old;
}

void SetGsCrt(short interlace, short omode, short ffmd)
{
    crt_interlace = interlace;
    crt_omode = omode;
    crt_ffmd = ffmd;
}

void ico_kernel_gs_crt(short *interlace, short *omode, short *ffmd)
{
    *interlace = crt_interlace;
    *omode = crt_omode;
    *ffmd = crt_ffmd;
}

/* The kernel writes 1 to *flag and GS_CSR to *csr at the next vblank start
   (ico_kernel_raise_intc). */
void SetVSyncFlag(unsigned int *flag, unsigned long long *csr)
{
    vsync_flag = flag;
    vsync_csr = csr;
}

void SetVTLBRefillHandler(int cause, void *handler)
{
    (void)cause;
    (void)handler;
}

void SetVCommonHandler(int cause, void *handler)
{
    (void)cause;
    (void)handler;
}

/* VSync and VSync2 poll INTC_STAT on the EE (sce/libkernl/glue.c): a busy
   wait, during which interrupts and the threads they wake still run but
   lower priorities do not. */
void VSync(void)
{
    NOTE("VSync", 0);
    ico_sched_spin_vsync();
}

long long VSync2(void)
{
    NOTE("VSync2", 0);
    ico_sched_spin_vsync();
    return (long long)*GS_CSR;
}

void ico_kernel_set_tty(int on)
{
    tty_on = on;
}

/* The retail game's scePrintf output went to the development kit's TTY. */
void scePrintf(char *fmt, ...)
{
    va_list ap;
    if (tty_on < 0) {
        tty_on = getenv("ICO_TTY") != NULL;
    }
    if (!tty_on) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}
