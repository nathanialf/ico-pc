/*
 * port/platform/fiber.h
 *
 * The context-switch layer under the EE thread scheduler (sched.c): a thin
 * wrapper over minicoro (port/third_party/minicoro, Unlicense or MIT-0).
 *
 * A fiber is an asymmetric coroutine: ico_fiber_resume() runs it on its own
 * stack until it calls ico_fiber_yield(), which returns to the resumer. The
 * scheduler is the only resumer, so every switch between two game threads
 * goes fiber -> scheduler -> fiber.
 *
 * Backends (minicoro's own selection, minicoro.h "MCO_USE_*"): assembly
 * switch on x86-64 Linux and Windows and arm64. Stacks are
 * ICO_FIBER_STACK_SIZE bytes. On the assembly backend the stack has a
 * no-access guard page directly below it, so an overflow faults instead of
 * corrupting the fiber's control block.
 *
 * Neither backend is relied on for the floating-point control state: the
 * assembly switch (x86-64 Windows included: minicoro.h picks MCO_USE_ASM for
 * GCC and MSVC there) does not save MXCSR or the x87 control word, and
 * minicoro's Windows fiber path (other Windows targets) creates fibers with
 * FIBER_FLAG_FLOAT_SWITCH (minicoro.h, _mco_create_context), which gives
 * each fiber an FP state of its own. The scheduler therefore runs a start
 * hook (the simulation's FP mode, fpenv.c) at the top of every fiber; see
 * docs/port/PLATFORM.md.
 */
#ifndef ICO_PLATFORM_FIBER_H
#define ICO_PLATFORM_FIBER_H

#include <stddef.h>

#define ICO_FIBER_STACK_SIZE (256 * 1024)

typedef struct IcoFiber IcoFiber;

/* Creates a suspended fiber that will run fn(arg); NULL on failure. A fiber
   whose fn returns is finished and can only be destroyed. */
IcoFiber *ico_fiber_create(void (*fn)(void *arg), void *arg, size_t stack_size);
/* Runs the fiber until it yields or finishes. Returns 0, or -1 if the fiber
   cannot be resumed (finished, running, or its stack overflowed). */
int ico_fiber_resume(IcoFiber *f);
/* From inside a fiber: back to the ico_fiber_resume() that ran it. */
void ico_fiber_yield(void);
/* Destroys a suspended or finished fiber and frees its stack. */
void ico_fiber_destroy(IcoFiber *f);
/* 1 once the fiber's function has returned. */
int ico_fiber_finished(IcoFiber *f);
/* The running fiber, or NULL on the host (scheduler) context. */
IcoFiber *ico_fiber_current(void);
/* The backend's name: "asm", "ucontext" or "windows-fibers". */
const char *ico_fiber_backend(void);
/* 1 when stacks have a guard page of ours below them. */
int ico_fiber_has_guard_page(void);
/* The running fiber's stack bounds on this OS thread; -1 on the host
   context or when the backend does not expose them (Windows fibers). */
int ico_fiber_current_stack(void **lo, void **hi);

#endif /* ICO_PLATFORM_FIBER_H */
