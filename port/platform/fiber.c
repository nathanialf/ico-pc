/*
 * port/platform/fiber.c
 *
 * minicoro's implementation and the stack allocator with a guard page. See
 * fiber.h.
 */
#define MINICORO_IMPL
#define MCO_NO_DEFAULT_ALLOCATOR

#include "../third_party/minicoro/minicoro.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "diag_host.h"
#include "fiber.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#else

#include <sys/mman.h>
#include <unistd.h>

#endif
#if defined(__SANITIZE_ADDRESS__)
#define ICO_FIBER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ICO_FIBER_ASAN 1
#endif
#endif
#ifdef ICO_FIBER_ASAN

#include <sanitizer/asan_interface.h>

#endif

struct IcoFiber {
    mco_coro *co;
    void (*fn)(void *arg);
    void *arg;
};

/* --- Stack allocation ------------------------------------------------------
 *
 * minicoro lays a coroutine out in one block (minicoro.h,
 * _mco_create_context): the mco_coro record, the switch context, the
 * storage area (mco_push/mco_pop, unused here), then the stack, which grows
 * down towards the storage area. With the storage area exactly one page
 * long and the block placed so the stack starts on a page boundary, the
 * storage area is the page under the stack and becomes the guard page.
 * The Windows fiber backend allocates its stacks itself (CreateFiberEx).
 */
#if defined(MCO_USE_ASM) || defined(MCO_USE_UCONTEXT)
#define ICO_FIBER_GUARD 1
#endif
#ifdef ICO_FIBER_GUARD

static size_t page_size(void)
{
    static size_t ps;
    if (ps == 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        ps = si.dwPageSize;
#else
        long v = sysconf(_SC_PAGESIZE);
        ps = v > 0 ? (size_t)v : 4096;
#endif
    }
    return ps;
}

/* The stack size every fiber is created with, the guarded allocator's
   allocator_data (ico_fiber_create sets it before each mco_create). */
static size_t fiber_stack_size;

/* allocator_data: the stack size, from which the stack's offset in the block
   follows (coro_size = header + stack_size + 16, minicoro.h
   _mco_init_desc_sizes). */
static void *guarded_alloc(size_t size, void *allocator_data)
{
    size_t stack_size = *(size_t *)allocator_data;
    size_t ps = page_size();
    size_t header = size - stack_size - 16; /* offset of the stack in the block */
    size_t total = size + ps;
    uintptr_t base;
    uintptr_t p;
#if defined(_WIN32)
    DWORD old;
    void *m = VirtualAlloc(NULL, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (m == NULL) {
        return NULL;
    }
#else
    void *m = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        return NULL;
    }
#endif
    base = (uintptr_t)m;
    /* the block's start, chosen so the stack (block + header) is page aligned */
    p = ((base + header + ps - 1) & ~(uintptr_t)(ps - 1)) - header;
#if defined(_WIN32)
    VirtualProtect((void *)(p + header - ps), ps, PAGE_NOACCESS, &old);
#else
    mprotect((void *)(p + header - ps), ps, PROT_NONE);
#endif
    return (void *)p;
}

static void guarded_dealloc(void *ptr, size_t size, void *allocator_data)
{
    size_t ps = page_size();
    uintptr_t base = (uintptr_t)ptr & ~(uintptr_t)(ps - 1);
    (void)allocator_data;
#ifdef ICO_FIBER_ASAN
    /* frames left on a destroyed fiber's stack keep their redzones */
    __asan_unpoison_memory_region((void *)base, size + ps);
#endif
#if defined(_WIN32)
    (void)size;
    VirtualFree((void *)base, 0, MEM_RELEASE);
#else
    munmap((void *)base, size + ps);
#endif
}

#else

static void *plain_alloc(size_t size, void *allocator_data)
{
    (void)allocator_data;
    return calloc(1, size);
}

static void plain_dealloc(void *ptr, size_t size, void *allocator_data)
{
    (void)size;
    (void)allocator_data;
    free(ptr);
}

#endif
/* 32-bit Windows runs fibers through CreateFiberEx, which promises only
   4-byte stack alignment at the fiber's entry, while GCC assumes 16 and
   keeps SSE spills in aligned stack slots (movaps, cvtdq2ps on (%esp)):
   realign here, once per fiber, and everything the fiber calls inherits
   it. */
#if defined(__i386__) && defined(__GNUC__)

__attribute__((force_align_arg_pointer))
#endif
static void fiber_main(mco_coro *co)
{
    IcoFiber *f = (IcoFiber *)mco_get_user_data(co);
    f->fn(f->arg);
}

IcoFiber *ico_fiber_create(void (*fn)(void *arg), void *arg, size_t stack_size)
{
    IcoFiber *f;
    mco_desc desc;
    mco_result res;

    f = (IcoFiber *)calloc(1, sizeof *f);
    if (f == NULL) {
        return NULL;
    }
    f->fn = fn;
    f->arg = arg;
    desc = mco_desc_init(fiber_main, stack_size);
    desc.user_data = f;
#ifdef ICO_FIBER_GUARD
    /* one page of storage, which becomes the guard page */
    desc.coro_size += page_size() - MCO_DEFAULT_STORAGE_SIZE;
    desc.storage_size = page_size();
    fiber_stack_size = desc.stack_size;
    desc.alloc_cb = guarded_alloc;
    desc.dealloc_cb = guarded_dealloc;
    desc.allocator_data = &fiber_stack_size;
#else
    desc.alloc_cb = plain_alloc;
    desc.dealloc_cb = plain_dealloc;
#endif
    res = mco_create(&f->co, &desc);
    if (res != MCO_SUCCESS) {
        fprintf(stderr, "fiber: mco_create: %s\n", mco_result_description(res));
        free(f);
        return NULL;
    }
    return f;
}

int ico_fiber_resume(IcoFiber *f)
{
    mco_result res = mco_resume(f->co);
    if (res != MCO_SUCCESS) {
        fprintf(stderr, "fiber: mco_resume: %s\n", mco_result_description(res));
        return -1;
    }
    return 0;
}

void ico_fiber_yield(void)
{
    mco_coro *co = mco_running();
    mco_result res;
    if (co == NULL) {
        fprintf(stderr, "fiber: yield outside a fiber\n");
        ico_diag_set_failure("fiber: yield outside a fiber");
        abort();
    }
    res = mco_yield(co);
    if (res != MCO_SUCCESS) {
        /* MCO_STACK_OVERFLOW: the stack pointer left the fiber's stack */
        fprintf(stderr, "fiber: mco_yield: %s\n", mco_result_description(res));
        ico_diag_set_failure("fiber: mco_yield: %s", mco_result_description(res));
        abort();
    }
}

void ico_fiber_destroy(IcoFiber *f)
{
    if (f == NULL) {
        return;
    }
    if (mco_destroy(f->co) != MCO_SUCCESS) {
        fprintf(stderr, "fiber: destroying a running fiber\n");
        abort();
    }
    free(f);
}

int ico_fiber_finished(IcoFiber *f)
{
    return mco_status(f->co) == MCO_DEAD;
}

IcoFiber *ico_fiber_current(void)
{
    mco_coro *co = mco_running();
    return co != NULL ? (IcoFiber *)mco_get_user_data(co) : NULL;
}

const char *ico_fiber_backend(void)
{
#if defined(MCO_USE_ASM)
    return "asm";
#elif defined(MCO_USE_UCONTEXT)
    return "ucontext";
#elif defined(MCO_USE_FIBERS)
    return "windows-fibers";
#else
    return "unknown";
#endif
}

int ico_fiber_has_guard_page(void)
{
#ifdef ICO_FIBER_GUARD
    return 1;
#else
    return 0;
#endif
}

int ico_fiber_current_stack(void **lo, void **hi)
{
    mco_coro *co = mco_running();
    if (co == NULL || co->stack_base == NULL || co->stack_size == 0) {
        return -1;
    }
    *lo = co->stack_base;
    *hi = (char *)co->stack_base + co->stack_size;
    return 0;
}
