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
 * ICO_FIBER_STACK_SIZE bytes. The stack has a no-access guard page directly
 * below it, so an overflow faults instead of corrupting the fiber's control
 * block.
 *
 * The pool. Every fiber lives in one of ICO_FIBER_POOL_SLOTS slots of one
 * address space reservation, pinned at ICO_FIBER_POOL_BASE (anywhere, with
 * one log line, when that address is taken). A slot is, from its start:
 * a header page (the IcoFiber record, minicoro's mco_coro and its switch
 * context, laid out as minicoro's _mco_create_context expects), the guard
 * page, the stack and minicoro's 16 spare bytes, rounded up to 64 KB (the
 * Windows reservation granularity). A slot is committed when a fiber is
 * created in it and decommitted when the fiber is destroyed, so a long
 * session keeps only the live fibers' pages. The scheduler puts thread id
 * N in slot N (sched.c), which is what lets a save state put a thread's
 * stack back at the same offsets (docs/port/PLATFORM.md, "Fibers and the
 * scheduler").
 *
 * Neither backend is relied on for the floating-point control state: the
 * assembly switch (x86-64 Windows included: minicoro.h picks MCO_USE_ASM for
 * GCC and MSVC there) does not save MXCSR or the x87 control word, and
 * minicoro's Windows fiber path (other Windows targets) creates fibers with
 * FIBER_FLAG_FLOAT_SWITCH (minicoro.h, _mco_create_context), which gives
 * each fiber an FP state of its own. The scheduler therefore runs a start
 * hook (the simulation's FP mode, fpenv.c) at the top of every fiber.
 */
#ifndef ICO_PLATFORM_FIBER_H
#define ICO_PLATFORM_FIBER_H

#include <stddef.h>
#include <stdint.h>

#include "state_reloc.h"

#define ICO_FIBER_STACK_SIZE (256 * 1024)
/* sched.h's ICO_SCHED_MAX_THREADS (sched.c checks they agree) */
#define ICO_FIBER_POOL_SLOTS 256
/* The arena's pinned base (arena.h ICO_ARENA_PIN_BASE) + 256 MB */
#define ICO_FIBER_POOL_BASE 0x3C10000000ull

typedef struct IcoFiber IcoFiber;

/* Creates a suspended fiber that will run fn(arg) in the highest free slot;
   NULL on failure. stack_size is at most ICO_FIBER_STACK_SIZE (0 or less
   gets the pool's stack, which is always ICO_FIBER_STACK_SIZE). A fiber
   whose fn returns is finished and can only be destroyed. */
IcoFiber *ico_fiber_create(void (*fn)(void *arg), void *arg, size_t stack_size);
/* The same in slot `slot` (0..ICO_FIBER_POOL_SLOTS-1); NULL when the slot
   is in use or out of range, or the memory is not there. */
IcoFiber *ico_fiber_create_in_slot(int slot, void (*fn)(void *arg), void *arg);
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

/* --- The pool ---------------------------------------------------------------- */

/* Reserves the pool at `base` exactly (NULL: anywhere) and gives the old
   reservation back, the new one taken first so the two never share an
   address. -1 while any slot is in use or when the address is taken. The
   first fiber reserves the pool at ICO_FIBER_POOL_BASE by itself; this is
   for the tests (a second pool elsewhere). */
int ico_fiber_pool_init_at(void *base_or_NULL);
/* The pool's first byte (reserving it at the pinned base on the first
   call); NULL when no memory could be reserved. */
void *ico_fiber_pool_base(void);
/* One slot's bytes (a multiple of 64 KB); the pool is
   ICO_FIBER_POOL_SLOTS of them. */
size_t ico_fiber_pool_slot_bytes(void);
/* 1 when the pool sits at ICO_FIBER_POOL_BASE. */
int ico_fiber_pool_pinned(void);
/* The fiber in a slot, NULL when the slot is free. */
IcoFiber *ico_fiber_in_slot(int slot);
/* The slot a fiber lives in. */
int ico_fiber_slot(const IcoFiber *f);

/* --- Save states --------------------------------------------------------------
 * A suspended fiber is its slot's header page (the control block) and its
 * stack from the deepest word it ever held (a slot's stack starts zeroed;
 * the dead frames under the stack pointer are kept, since the game's code
 * reads them when it reads a local it never set), and at least from the
 * saved stack pointer minus 128 bytes (the System V red zone), to the
 * stack's top. Restored into the zeroed slot, so the stack is the saved one
 * byte for byte, into the same slot
 * of this process's pool, at the same offsets, with the addresses in it
 * moved: the saved return address (rip; lr and x30 on arm64) by the
 * MODULE delta, the stack pointer (rsp; sp) and the Windows TEB stack words
 * (stack_base, stack_limit, dealloc_stack) by the POOL delta, minicoro's
 * own pointers recomputed for the slot (and checked against the old value
 * plus its delta), then the conservative scan (ico_state_reloc_word) over
 * the other callee-saved registers (rbx, rbp, r12-r15, and rdi, rsi on
 * Windows; x19-x29 on arm64) and every 8-byte word of the restored stack.
 * The vector registers (xmm6-15, d8-15) are never touched. Host context
 * only (no fiber running). The asm backend on x86-64 and arm64 only: the
 * calls return -1 elsewhere. */
typedef struct IcoFiberImage {
    uint64_t slot_base; /* the slot's address in the saving process */
    uint32_t slot;
    uint32_t ctrl_bytes;  /* the header page */
    uint32_t stack_off;   /* the first saved stack byte's offset in the slot */
    uint32_t stack_bytes; /* from there to the stack's top */
    const void *ctrl;     /* the bytes; a save points into the live slot */
    const void *stack;
} IcoFiberImage;

/* Describes a suspended fiber's slot. No copy: out's pointers are the live
   slot's, valid until the fiber runs or is destroyed, so a caller copies
   them before either (and before any restore, which discards slots).
   Returns 0, or -1 for a free slot, a running or finished fiber, or a
   stack pointer outside the slot's stack. */
int ico_fiber_slot_save(int slot, IcoFiberImage *out);
/* Commits the free slot `slot` and puts `in` back into it, moved by `r`
   (above): in->slot must be `slot`, the layout this build's, r's POOL
   range the saving process's pool (in->slot_base inside it, even when the
   pool did not move) and its delta the one that takes in->slot_base to the
   slot's address now; r's MODULE range, when not empty, must cover the
   saving program's code (the return addresses are checked against it).
   Returns 0 with the fiber suspended where it was saved
   (ico_fiber_in_slot gives it), or -1 with the slot free again and nothing
   else changed. */
int ico_fiber_slot_restore(int slot, const IcoFiberImage *in, const IcoStateReloc *r);
/* Drops a slot's fiber without running anything of it (a load replaces
   every live fiber): decommits the slot and frees it. No-op on a free
   slot. */
void ico_fiber_slot_discard(int slot);

#endif /* ICO_PLATFORM_FIBER_H */
