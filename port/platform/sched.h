/*
 * port/platform/sched.h
 *
 * The EE kernel's thread and semaphore model on one host thread. Every PS2
 * thread the game creates is a fiber (fiber.h); this file decides which one
 * runs, with the EE kernel's rules:
 *
 *   - strict priority, 0 (highest) to 127; no time slice;
 *   - one ready queue per priority, FIFO; the running thread stays at the
 *     head of its queue, so a thread preempted by a higher-priority one
 *     resumes before its peers;
 *   - a thread that becomes ready (StartThread, WakeupThread, SignalSema,
 *     ResumeThread) goes to the tail of its priority's queue and preempts
 *     the caller at once when its priority is higher;
 *   - ChangeThreadPriority and RotateThreadReadyQueue move a ready thread to
 *     the tail of its (new) queue;
 *   - SleepThread/WakeupThread keep a wakeup count, so a wakeup that comes
 *     before the sleep is remembered;
 *   - semaphores keep their waiters in FIFO order.
 *
 * The EE kernel itself is not public. The rules follow Play!'s EE kernel
 * HLE (Source/ee/PS2OS.cpp, BSD-2-Clause, read for its semantics only) and
 * the game's own use; the cases where the EE's behaviour is not known
 * are noted where they apply.
 *
 * Contexts. Code runs either on a fiber (a game thread) or on the host
 * context (ico_sched_run's caller, the vsync and other "interrupt" code). A
 * call that would block is an error on the host context. A call that wakes
 * a thread from the host context never switches; the next ico_sched_run
 * picks the thread up, as the EE does on return from an interrupt.
 *
 * Busy waits. The EE's VSync() and the idle thread spin on the CPU: lower
 * priorities do not run, higher ones woken by an interrupt do. A fiber says
 * so with ico_sched_spin_vsync(): it stays ready at the head of its queue,
 * and ico_sched_run returns to the host when it is the highest ready
 * thread, until the host advances the vsync count.
 */
/* This file shadows the C library's <sched.h> for every source that has
   port/platform on its include path. A source that needs the library's
   (through <pthread.h>: diag_host.c) defines ICO_WANT_LIBC_SCHED around
   that include. */
#ifdef ICO_WANT_LIBC_SCHED
#include_next <sched.h>
#else
#ifndef ICO_PLATFORM_SCHED_H
#define ICO_PLATFORM_SCHED_H

#define ICO_SCHED_MAX_THREADS 256 /* ids 1..255 */
#define ICO_SCHED_MAX_SEMAS 256   /* ids 1..255 */
#define ICO_SCHED_PRIORITIES 128

/* Thread states, as ReferThreadStatus returns them (EE THS_*). */
#define ICO_THS_RUN 0x01
#define ICO_THS_READY 0x02
#define ICO_THS_WAIT 0x04
#define ICO_THS_SUSPEND 0x08
#define ICO_THS_WAITSUSPEND 0x0C
#define ICO_THS_DORMANT 0x10

/* What a waiting thread waits for (EE TSW_*). */
#define ICO_TSW_NONE 0
#define ICO_TSW_SLEEP 1
#define ICO_TSW_SEMA 2

/* ico_sched_run's result. */
#define ICO_SCHED_EMPTY 0    /* no thread is ready */
#define ICO_SCHED_SPINNING 1 /* the highest ready thread busy-waits for a vsync */

typedef struct IcoThreadInfo {
    int status;
    void (*entry)(void *);
    void *stack;
    int stack_size;
    void *gp;
    int init_priority;
    int current_priority;
    unsigned int attr;
    unsigned int option;
    int wait_type;
    int wait_id;
    int wakeup_count;
} IcoThreadInfo;

typedef struct IcoSemaInfo {
    int count;
    int max_count;
    int init_count;
    int num_wait;
    unsigned int attr;
    unsigned int option;
} IcoSemaInfo;

/* --- Host side ------------------------------------------------------------ */

/* Drops every thread and semaphore (destroying their fibers) and starts
   empty. The host context only. */
void ico_sched_reset(void);
/* A function every fiber runs before its thread's entry (the simulation's
   FP mode; fiber.h says why). NULL for none. */
void ico_sched_set_fiber_start_hook(void (*hook)(void));
/* Runs fn(arg) on the host context's stack in the host FP mode
   (ico_fpenv_host_enter), then puts the simulation's FP mode back and
   returns. From a fiber the thread yields to ico_sched_run, which makes the
   call and resumes the same thread at once (no other thread runs in
   between); from the host context fn is called directly. For host work too
   deep for a fiber's stack (the GPU driver: replay, pipeline creation,
   present). fn must
   not make kernel calls. */
void ico_sched_call_on_host(void (*fn)(void *arg), void *arg);
/* Creates and starts the program's first thread (the EE's main thread, id
   1), which runs entry(arg) at the given priority. Returns its id. */
int ico_sched_boot(void (*entry)(void *), void *arg, int priority);
/* Runs threads until none is ready or the highest ready one busy-waits for
   the next vsync. Returns ICO_SCHED_EMPTY or ICO_SCHED_SPINNING. */
int ico_sched_run(void);
/* One vsync has passed: releases the busy waits that wait for it. */
void ico_sched_vsync_advance(void);
unsigned int ico_sched_vsync_count(void);
/* Bracket "interrupt" code run on the host context (for assertions). */
void ico_sched_interrupt_begin(void);
void ico_sched_interrupt_end(void);
int ico_sched_in_interrupt(void);
/* Context switches into fibers so far (statistics). */
unsigned long ico_sched_switch_count(void);

/* --- From a game thread ----------------------------------------------------- */

/* Busy-waits (holding the CPU against lower priorities) until the next
   vsync. */
void ico_sched_spin_vsync(void);
/* 1 on a fiber, 0 on the host context. */
int ico_sched_in_thread(void);

/* --- The EE kernel's calls (kernel_host.c maps the SDK names onto these) --- */

int ico_sched_create_thread(void (*entry)(void *), void *stack, int stack_size, void *gp,
                            int init_priority, unsigned int attr, unsigned int option);
int ico_sched_delete_thread(int id);
int ico_sched_start_thread(int id, void *arg);
void ico_sched_exit_thread(void);
void ico_sched_exit_delete_thread(void);
int ico_sched_terminate_thread(int id);
int ico_sched_change_priority(int id, int priority, int from_interrupt);
int ico_sched_rotate_ready_queue(int priority, int from_interrupt);
int ico_sched_get_thread_id(void);
int ico_sched_refer_thread(int id, IcoThreadInfo *info);
int ico_sched_sleep(void);
int ico_sched_wakeup(int id, int from_interrupt);
int ico_sched_cancel_wakeup(int id);
int ico_sched_suspend(int id, int from_interrupt);
int ico_sched_resume(int id, int from_interrupt);

int ico_sched_create_sema(int init_count, int max_count, unsigned int attr, unsigned int option);
int ico_sched_delete_sema(int id);
int ico_sched_signal_sema(int id, int from_interrupt);
int ico_sched_wait_sema(int id);
int ico_sched_poll_sema(int id);
int ico_sched_refer_sema(int id, IcoSemaInfo *info);

/* --- Diagnostics (diag_host.c) -------------------------------------------- */

/* What a thread was last seen doing: the kernel call, its argument and the
   caller's return address. */
typedef struct IcoSchedView {
    int status; /* ICO_THS_*; RUN for the current thread */
    int priority;
    int init_priority;
    int wait_type;
    int wait_id;
    int wakeup_count;
    int spinning;
    unsigned int spin_until;
    int parent; /* the creating thread's id, 0 for the host */
    void *entry;
    const char *note; /* NULL before the first call */
    int note_arg;
    void *note_caller;
    unsigned int note_vsync;
} IcoSchedView;

/* Records the current thread's kernel call (no-op on the host context). */
void ico_sched_note(const char *what, int arg, void *caller);
/* The thread on the CPU, 0 on the host context. */
int ico_sched_current(void);
/* 1 and *v when id is a thread, else 0. Reads only plain fields, so a
   watchdog thread may call it (the values may be a moment stale). */
int ico_sched_view(int id, IcoSchedView *v);
/* 1 with the count and the number of waiters when id is a semaphore. */
int ico_sched_sema_view(int id, int *count, int *num_wait);

#endif /* ICO_PLATFORM_SCHED_H */
#endif /* ICO_WANT_LIBC_SCHED */
