/*
 * port/platform/fiber.c
 *
 * minicoro's implementation, the slot pool with its guard pages, and the
 * save state image of a suspended fiber. See fiber.h.
 */
#define MINICORO_IMPL
#define MCO_NO_DEFAULT_ALLOCATOR

#include "../third_party/minicoro/minicoro.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diag_host.h"
#include "fiber.h"
#include "state_reloc.h"
#include "vm_reserve.h"

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
/* The slot layout is minicoro's one-block layout (mco_coro, the switch
   context, the storage area, the stack), which the asm and ucontext
   backends share; the Windows fiber backend allocates its stacks itself
   and is not used by any preset (minicoro picks the asm switch for x86-64
   Windows). */
#if !defined(MCO_USE_ASM) && !defined(MCO_USE_UCONTEXT)
#error "fiber.c: the slot pool needs minicoro's asm or ucontext backend"
#endif
/* Save states read the asm backend's register buffer (_mco_ctxbuf). */
#if defined(MCO_USE_ASM) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__))
#define ICO_FIBER_STATES 1
#endif

/* The record a slot starts with (the header page's first bytes). */
struct IcoFiber {
    mco_coro *co;
    void (*fn)(void *arg);
    void *arg;
    int slot;
};

/* --- The pool -------------------------------------------------------------
 *
 * minicoro lays a coroutine out in one block (minicoro.h,
 * _mco_create_context): the mco_coro record, the switch context, the
 * storage area (mco_push/mco_pop, unused here), then the stack, which grows
 * down towards the storage area. With the storage area exactly one page
 * long and the block placed so it starts on the slot's second page, the
 * storage area is the page under the stack and stays the no-access guard
 * page; the block's head (mco_coro and the context) ends the first page,
 * whose start holds the IcoFiber record.
 *
 *   slot + 0           IcoFiber ... mco_coro, _mco_context   (header page)
 *   slot + page        guard page (minicoro's storage area)
 *   slot + 2 pages     stack, ICO_FIBER_STACK_SIZE
 *   ...                16 bytes, then the round-up to 64 KB
 */
#define SLOT_ALIGN ((size_t)64 * 1024)

typedef struct Pool {
    uintptr_t base; /* 0 before the first reservation */
    size_t bytes;   /* ICO_FIBER_POOL_SLOTS * slot_bytes */
    size_t slot_bytes;
    size_t page;
    size_t co_off;    /* mco_coro's offset in a slot */
    size_t ctx_off;   /* the switch context's */
    size_t stack_off; /* the stack's (two pages) */
    size_t coro_size; /* minicoro's block size, mco_coro to the stack's end + 16 */
    int pinned;
    int live; /* slots in use */
    unsigned char used[ICO_FIBER_POOL_SLOTS];
} Pool;

static Pool pool;

static void fiber_main(mco_coro *co);
static void *slot_alloc(size_t size, void *allocator_data);
static void slot_dealloc(void *ptr, size_t size, void *allocator_data);

static size_t align_up(size_t v, size_t a)
{
    return (v + a - 1) & ~(a - 1);
}

/* minicoro's description of a pool fiber: one page of storage, which
   becomes the guard page. */
static mco_desc pool_desc(void)
{
    mco_desc desc = mco_desc_init(fiber_main, ICO_FIBER_STACK_SIZE);
    desc.coro_size += pool.page - MCO_DEFAULT_STORAGE_SIZE;
    desc.storage_size = pool.page;
    desc.alloc_cb = slot_alloc;
    desc.dealloc_cb = slot_dealloc;
    return desc;
}

/* The slot geometry, from the page size and minicoro's sizes. */
static int pool_layout(void)
{
    mco_desc desc;
    size_t head; /* mco_coro + context: the block's bytes before the storage area */
    if (pool.slot_bytes != 0) {
        return 0;
    }
    pool.page = ico_vm_page_size();
    desc = pool_desc();
    /* coro_size = head + storage (one page) + stack + 16
       (minicoro.h _mco_init_desc_sizes) */
    head = desc.coro_size - desc.stack_size - 16 - pool.page;
    if (head + align_up(sizeof(IcoFiber), 16) > pool.page ||
        head != align_up(sizeof(mco_coro), 16) + align_up(sizeof(_mco_context), 16)) {
        fprintf(stderr, "fiber: the slot header does not fit a %lu-byte page\n",
                (unsigned long)pool.page);
        return -1;
    }
    pool.co_off = pool.page - head;
    pool.ctx_off = pool.co_off + align_up(sizeof(mco_coro), 16);
    pool.stack_off = 2 * pool.page;
    pool.coro_size = desc.coro_size;
    pool.slot_bytes = align_up(pool.co_off + desc.coro_size, SLOT_ALIGN);
    pool.bytes = pool.slot_bytes * ICO_FIBER_POOL_SLOTS;
    return 0;
}

static int pool_reserve(uintptr_t at)
{
    void *m;
    if (pool_layout() != 0) {
        return -1;
    }
    m = ico_vm_reserve(at, pool.bytes, 0);
    if (m == NULL) {
        return -1;
    }
    if (pool.base != 0) {
        ico_vm_release((void *)pool.base, pool.bytes);
    }
    pool.base = (uintptr_t)m;
    pool.pinned = pool.base == (uintptr_t)ICO_FIBER_POOL_BASE;
    return 0;
}

/* The pool at the pinned base, or anywhere with the one line that says so. */
static int pool_ensure(void)
{
    if (pool.base != 0) {
        return 0;
    }
    if (pool_reserve((uintptr_t)ICO_FIBER_POOL_BASE) == 0) {
        return 0;
    }
    if (pool_reserve(0) != 0) {
        fprintf(stderr, "fiber: cannot reserve the pool (%lu bytes)\n", (unsigned long)pool.bytes);
        return -1;
    }
    fprintf(stderr, "fiber: pool reserved at %p (the pinned base was taken)\n", (void *)pool.base);
    return 0;
}

static uintptr_t slot_addr(int slot)
{
    return pool.base + (size_t)slot * pool.slot_bytes;
}

/* allocator_data: the slot's address. Commits the header page and the
   stack; the guard page between them stays no-access. */
static void *slot_alloc(size_t size, void *allocator_data)
{
    uintptr_t s = (uintptr_t)allocator_data;
    size_t stack_bytes = align_up(size - (pool.stack_off - pool.co_off), pool.page);
    if (size != pool.coro_size || ico_vm_commit((void *)s, pool.page) != 0) {
        return NULL;
    }
    if (ico_vm_commit((void *)(s + pool.stack_off), stack_bytes) != 0) {
        ico_vm_decommit((void *)s, pool.page);
        return NULL;
    }
    return (void *)(s + pool.co_off);
}

/* Drops a slot's pages and frees it. */
static void slot_release(int slot)
{
    uintptr_t s = slot_addr(slot);
#ifdef ICO_FIBER_ASAN
    /* frames left on a dropped fiber's stack keep their redzones */
    __asan_unpoison_memory_region((void *)s, pool.slot_bytes);
#endif
    ico_vm_decommit((void *)s, pool.slot_bytes);
    if (pool.used[slot]) {
        pool.used[slot] = 0;
        pool.live--;
    }
}

static void slot_dealloc(void *ptr, size_t size, void *allocator_data)
{
    uintptr_t s = (uintptr_t)allocator_data;
    (void)ptr;
    (void)size;
    slot_release((int)((s - pool.base) / pool.slot_bytes));
}

static int slot_ok(int slot)
{
    return slot >= 0 && slot < ICO_FIBER_POOL_SLOTS;
}

/* --- Fibers ----------------------------------------------------------------- */

static void fiber_main(mco_coro *co)
{
    IcoFiber *f = (IcoFiber *)mco_get_user_data(co);
    f->fn(f->arg);
}

IcoFiber *ico_fiber_create_in_slot(int slot, void (*fn)(void *arg), void *arg)
{
    IcoFiber *f;
    mco_coro *co;
    mco_desc desc;
    mco_result res;
    uintptr_t s;

    if (pool_ensure() != 0) {
        return NULL;
    }
    if (!slot_ok(slot) || pool.used[slot]) {
        fprintf(stderr, "fiber: slot %d is %s\n", slot, slot_ok(slot) ? "in use" : "out of range");
        return NULL;
    }
    s = slot_addr(slot);
    f = (IcoFiber *)s;
    desc = pool_desc();
    desc.user_data = f;
    desc.allocator_data = (void *)s;
    pool.used[slot] = 1;
    pool.live++;
    res = mco_create(&co, &desc);
    if (res != MCO_SUCCESS) {
        fprintf(stderr, "fiber: mco_create: %s\n", mco_result_description(res));
        if (pool.used[slot]) { /* the allocation failed: nothing to give back */
            pool.used[slot] = 0;
            pool.live--;
        }
        return NULL;
    }
    f->co = co;
    f->fn = fn;
    f->arg = arg;
    f->slot = slot;
    return f;
}

IcoFiber *ico_fiber_create(void (*fn)(void *arg), void *arg, size_t stack_size)
{
    int slot;
    if (stack_size > ICO_FIBER_STACK_SIZE) {
        fprintf(stderr, "fiber: a %lu-byte stack is larger than the pool's\n",
                (unsigned long)stack_size);
        return NULL;
    }
    if (pool_ensure() != 0) {
        return NULL;
    }
    /* from the top: the scheduler takes slot N for thread N, from 1 up */
    for (slot = ICO_FIBER_POOL_SLOTS - 1; slot >= 0; slot--) {
        if (!pool.used[slot]) {
            return ico_fiber_create_in_slot(slot, fn, arg);
        }
    }
    fprintf(stderr, "fiber: all %d slots are in use\n", ICO_FIBER_POOL_SLOTS);
    return NULL;
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
    /* the slot (and f with it) is decommitted by slot_dealloc */
    if (mco_destroy(f->co) != MCO_SUCCESS) {
        fprintf(stderr, "fiber: destroying a running fiber\n");
        abort();
    }
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
#else
    return "ucontext";
#endif
}

int ico_fiber_has_guard_page(void)
{
    return 1;
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

/* --- The pool's API ---------------------------------------------------------- */

int ico_fiber_pool_init_at(void *base_or_NULL)
{
    if (pool.live != 0) {
        fprintf(stderr, "fiber: the pool cannot move while %d slot(s) are in use\n", pool.live);
        return -1;
    }
    return pool_reserve((uintptr_t)base_or_NULL);
}

void *ico_fiber_pool_base(void)
{
    return pool_ensure() == 0 ? (void *)pool.base : NULL;
}

size_t ico_fiber_pool_slot_bytes(void)
{
    return pool_layout() == 0 ? pool.slot_bytes : 0;
}

int ico_fiber_pool_pinned(void)
{
    return pool.base != 0 && pool.pinned;
}

IcoFiber *ico_fiber_in_slot(int slot)
{
    if (pool.base == 0 || !slot_ok(slot) || !pool.used[slot]) {
        return NULL;
    }
    return (IcoFiber *)slot_addr(slot);
}

int ico_fiber_slot(const IcoFiber *f)
{
    return f->slot;
}

/* --- Save states --------------------------------------------------------------- */

#ifdef ICO_FIBER_STATES

static uintptr_t ctx_sp(const _mco_ctxbuf *c)
{
#if defined(__aarch64__)
    return (uintptr_t)c->sp;
#else
    return (uintptr_t)c->rsp;
#endif
}

/* The conservative scan's step on one pointer-sized word. */
static void scan_word(void **p, const IcoStateReloc *r)
{
    uint64_t w = (uint64_t)(uintptr_t)*p;
    if (ico_state_reloc_word(r, &w)) {
        *p = (void *)(uintptr_t)w;
    }
}

/* A typed word (ico_state_reloc_typed); *bad set when it is not one. */
static void typed_word(void **p, const IcoStateReloc *r, int which, int *bad)
{
    uint64_t w = (uint64_t)(uintptr_t)*p;
    if (ico_state_reloc_typed(r, which, &w) != 0) {
        *bad = 1;
        return;
    }
    *p = (void *)(uintptr_t)w;
}

/* A pointer minicoro or this file set for the slot: the old value moved by
   the POOL delta must be the value for the slot now. */
static void expect_pool(void **p, uintptr_t now, int64_t pool_delta, int *bad)
{
    if ((uintptr_t)*p + (uintptr_t)pool_delta != now) {
        *bad = 1;
    }
    *p = (void *)now;
}

/* The same for a function of this file, by the MODULE delta. */
static void expect_module(uintptr_t old, uintptr_t now, const IcoStateReloc *r, int *bad)
{
    if (old + (uintptr_t)r->range[ICO_RELOC_MODULE].delta != now) {
        *bad = 1;
    }
}

/* The switch context's typed words and the callee-saved registers' scan.
   Stack words must land inside [lo, hi] of the slot's stack. */
static void fix_ctxbuf(_mco_ctxbuf *c, uintptr_t lo, uintptr_t hi, const IcoStateReloc *r, int *bad)
{
#if defined(__aarch64__)
    int i;
    for (i = 0; i < 11; i++) { /* x19-x29 (x29 is the frame pointer) */
        scan_word(&c->x[i], r);
    }
    typed_word(&c->x[11], r, ICO_RELOC_MODULE, bad); /* x30, the return address */
    typed_word(&c->lr, r, ICO_RELOC_MODULE, bad);
    typed_word(&c->sp, r, ICO_RELOC_POOL, bad);
    /* d8-d15: never */
#else
    typed_word(&c->rip, r, ICO_RELOC_MODULE, bad);
    typed_word(&c->rsp, r, ICO_RELOC_POOL, bad);
    /* rbp is only a frame pointer when the code keeps one: scanned like
       the other callee-saved registers, which moves a stack address by the
       POOL delta all the same */
    scan_word(&c->rbp, r);
    scan_word(&c->rbx, r);
    scan_word(&c->r12, r);
    scan_word(&c->r13, r);
    scan_word(&c->r14, r);
    scan_word(&c->r15, r);
#ifdef _WIN32
    scan_word(&c->rdi, r);
    scan_word(&c->rsi, r);
    /* the TEB words the switch swaps (NT_TIB StackBase, StackLimit and
       DeallocationStack) are the slot's stack; FiberData is the fiber's
       own (0) */
    typed_word(&c->stack_base, r, ICO_RELOC_POOL, bad);
    typed_word(&c->stack_limit, r, ICO_RELOC_POOL, bad);
    typed_word(&c->dealloc_stack, r, ICO_RELOC_POOL, bad);
    scan_word(&c->fiber_storage, r);
    if ((uintptr_t)c->stack_base < lo || (uintptr_t)c->stack_base > hi ||
        (uintptr_t)c->stack_limit < lo || (uintptr_t)c->stack_limit > hi ||
        (uintptr_t)c->dealloc_stack < lo || (uintptr_t)c->dealloc_stack > hi) {
        *bad = 1;
    }
    /* xmm6-xmm15: never */
#endif
#endif
    if (ctx_sp(c) < lo || ctx_sp(c) > hi) {
        *bad = 1;
    }
}

int ico_fiber_slot_save(int slot, IcoFiberImage *out)
{
    uintptr_t s;
    uintptr_t lo;
    uintptr_t hi;
    uintptr_t sp;
    uintptr_t from;
    IcoFiber *f;
    mco_coro *co;
    _mco_context *cx;

    memset(out, 0, sizeof *out);
    if (pool.base == 0 || !slot_ok(slot) || !pool.used[slot]) {
        return -1;
    }
    s = slot_addr(slot);
    f = (IcoFiber *)s;
    co = f->co;
    if ((uintptr_t)co != s + pool.co_off || co == mco_running() || co->state != MCO_SUSPENDED ||
        co->prev_co != NULL) {
        return -1;
    }
    cx = (_mco_context *)co->context;
    lo = s + pool.stack_off;
    hi = lo + co->stack_size;
    sp = ctx_sp(&cx->ctx);
    if (sp < lo || sp > hi) {
        return -1;
    }
    from = sp - lo >= 128 ? sp - 128 : lo;
    from &= ~(uintptr_t)15;
    out->slot_base = (uint64_t)s;
    out->slot = (uint32_t)slot;
    out->ctrl_bytes = (uint32_t)pool.page;
    out->stack_off = (uint32_t)(from - s);
    out->stack_bytes = (uint32_t)(hi - from);
    out->ctrl = (const void *)s;
    out->stack = (const void *)from;
    return 0;
}

int ico_fiber_slot_restore(int slot, const IcoFiberImage *in, const IcoStateReloc *r)
{
    const IcoStateRange *pr = &r->range[ICO_RELOC_POOL];
    uintptr_t s;
    uintptr_t lo;
    uintptr_t hi;
    int64_t pool_delta;
    IcoFiber *f;
    mco_coro *co;
    _mco_context *cx;
    uint64_t *w;
    uint64_t *end;
    void *p;
    int bad = 0;

    if (pool_ensure() != 0 || !slot_ok(slot) || pool.used[slot] || in->slot != (uint32_t)slot) {
        return -1;
    }
    s = slot_addr(slot);
    lo = s + pool.stack_off;
    hi = lo + ICO_FIBER_STACK_SIZE;
    pool_delta = (int64_t)(s - (uintptr_t)in->slot_base);
    /* this build's layout, and r's POOL range the one the slot came from */
    if (in->ctrl_bytes != pool.page || in->stack_off < pool.stack_off ||
        (in->stack_off & 15) != 0 ||
        (size_t)in->stack_off + in->stack_bytes != pool.stack_off + ICO_FIBER_STACK_SIZE ||
        pr->delta != pool_delta || in->slot_base < pr->lo || in->slot_base >= pr->hi) {
        fprintf(stderr, "fiber: slot %d: the saved image does not fit this pool\n", slot);
        return -1;
    }
    if (ico_vm_commit((void *)s, pool.page) != 0 ||
        ico_vm_commit((void *)lo, align_up(ICO_FIBER_STACK_SIZE + 16, pool.page)) != 0) {
        ico_vm_decommit((void *)s, pool.slot_bytes);
        return -1;
    }
    pool.used[slot] = 1;
    pool.live++;
    memcpy((void *)s, in->ctrl, in->ctrl_bytes);
    memcpy((void *)(s + in->stack_off), in->stack, in->stack_bytes);

    /* IcoFiber */
    f = (IcoFiber *)s;
    co = (mco_coro *)(s + pool.co_off);
    cx = (_mco_context *)(s + pool.ctx_off);
    p = f->co;
    expect_pool(&p, (uintptr_t)co, pool_delta, &bad);
    f->co = co;
    p = (void *)(uintptr_t)f->fn;
    typed_word(&p, r, ICO_RELOC_MODULE, &bad);
    f->fn = (void (*)(void *))(uintptr_t)p;
    scan_word(&f->arg, r);
    if (f->slot != slot) {
        bad = 1;
    }
    /* mco_coro: a suspended coroutine of this pool's shape */
    if (co->state != MCO_SUSPENDED || co->prev_co != NULL || co->magic_number != MCO_MAGIC_NUMBER ||
        co->coro_size != pool.coro_size || co->stack_size != ICO_FIBER_STACK_SIZE ||
        co->storage_size != pool.page) {
        bad = 1;
    }
    expect_pool(&co->context, (uintptr_t)cx, pool_delta, &bad);
    expect_pool(&co->stack_base, lo, pool_delta, &bad);
    p = co->storage;
    expect_pool(&p, s + pool.page, pool_delta, &bad);
    co->storage = (unsigned char *)p;
    expect_pool(&co->user_data, s, pool_delta, &bad);
    expect_pool(&co->allocator_data, s, pool_delta, &bad);
    expect_module((uintptr_t)co->func, (uintptr_t)fiber_main, r, &bad);
    co->func = fiber_main;
    expect_module((uintptr_t)co->dealloc_cb, (uintptr_t)slot_dealloc, r, &bad);
    co->dealloc_cb = slot_dealloc;
    /* the sanitizers' per-coroutine handles belong to the saving process */
    co->asan_prev_stack = NULL;
    co->tsan_prev_fiber = NULL;
#ifdef _MCO_USE_TSAN
    co->tsan_fiber = __tsan_create_fiber(0);
#else
    co->tsan_fiber = NULL;
#endif
    /* the switch context; back_ctx is written by the next resume */
    fix_ctxbuf(&cx->ctx, lo, hi, r, &bad);
    memset(&cx->back_ctx, 0, sizeof cx->back_ctx);
    if (ctx_sp(&cx->ctx) < s + in->stack_off) {
        bad = 1; /* the stack pointer below the saved bytes */
    }
    /* the stack */
    w = (uint64_t *)(s + in->stack_off);
    end = (uint64_t *)hi;
    for (; w < end; w++) {
        ico_state_reloc_word(r, w);
    }
    if (bad) {
        fprintf(stderr, "fiber: slot %d: the saved control block failed its checks\n", slot);
        slot_release(slot);
        return -1;
    }
    return 0;
}

#else

int ico_fiber_slot_save(int slot, IcoFiberImage *out)
{
    (void)slot;
    memset(out, 0, sizeof *out);
    return -1;
}

int ico_fiber_slot_restore(int slot, const IcoFiberImage *in, const IcoStateReloc *r)
{
    (void)slot;
    (void)in;
    (void)r;
    return -1;
}

#endif

void ico_fiber_slot_discard(int slot)
{
    IcoFiber *f = ico_fiber_in_slot(slot);
    if (f == NULL) {
        return;
    }
    if (mco_running() == (mco_coro *)(slot_addr(slot) + pool.co_off)) {
        fprintf(stderr, "fiber: discarding the running fiber\n");
        ico_diag_set_failure("fiber: discarding the running fiber");
        abort();
    }
    slot_release(slot);
}
