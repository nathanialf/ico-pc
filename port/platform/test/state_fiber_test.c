/*
 * port/platform/test/state_fiber_test.c
 *
 * Save states' fiber layer (fiber.h "The pool", "Save states";
 * state_reloc.h's word functions):
 *
 *   words   ico_state_reloc_word / ico_state_reloc_typed on hand-made
 *           ranges: the end address included, empty ranges, delta 0.
 *   pool    the pool's geometry; all 256 slots created, run and destroyed;
 *           create_in_slot refuses a used slot and ico_fiber_create a full
 *           pool; the pool cannot move while a slot is used; a destroyed
 *           slot's pages read as zero when it is used again (decommitted).
 *   states  three fibers recursing with frames that point at their own
 *           stack (the caller's frame) and at a global, saved while
 *           suspended mid-recursion, copied out, destroyed; a second pool
 *           at another address; restored there with the POOL delta (MODULE
 *           delta 0: same process); resumed to the end with every frame
 *           checked. On Windows the restored fibers check the TEB's stack
 *           bounds lie in their new stack. Refusals: a running fiber's
 *           save, a restore into a used slot, a restore with the wrong
 *           delta (the slot stays free).
 *
 * `state_fiber_test guard` (not on Windows): a fiber saved before it
 * overflows its stack, restored into a second pool and resumed in a child
 * process must hit the restored slot's guard page.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "state_reloc.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* --- words -------------------------------------------------------------------- */

static void test_words(void)
{
    IcoStateReloc r;
    uint64_t w;
    memset(&r, 0, sizeof r);
    r.range[ICO_RELOC_ARENA].lo = 0x1000;
    r.range[ICO_RELOC_ARENA].hi = 0x2000;
    r.range[ICO_RELOC_ARENA].delta = 0x10000;
    r.range[ICO_RELOC_POOL].lo = 0x8000;
    r.range[ICO_RELOC_POOL].hi = 0x9000;
    r.range[ICO_RELOC_POOL].delta = -0x4000;
    w = 0x1000; /* first byte */
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == 0x11000);
    w = 0x2000; /* the end address itself */
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == 0x12000);
    w = 0x2001; /* past it */
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0x2001);
    w = 0xFFF;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0xFFF);
    w = 0x8008;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == 0x4008);
    w = 0; /* the empty MODULE range matches nothing */
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0);
    /* a range that did not move: matched, not rewritten */
    r.range[ICO_RELOC_POOL].delta = 0;
    w = 0x8008;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0x8008);
    /* typed: inside moves, 0 stays, outside a non-empty range refuses */
    w = 0x1800;
    CHECK(ico_state_reloc_typed(&r, ICO_RELOC_ARENA, &w) == 0 && w == 0x11800);
    w = 0;
    CHECK(ico_state_reloc_typed(&r, ICO_RELOC_ARENA, &w) == 0 && w == 0);
    w = 0x3000;
    CHECK(ico_state_reloc_typed(&r, ICO_RELOC_ARENA, &w) == -1 && w == 0x3000);
    /* typed in an empty range: moved by its delta, unchecked */
    r.range[ICO_RELOC_MODULE].delta = 0x100;
    w = 0x5555;
    CHECK(ico_state_reloc_typed(&r, ICO_RELOC_MODULE, &w) == 0 && w == 0x5655);
    CHECK(ico_state_reloc_typed(&r, ICO_RELOC_RANGES, &w) == -1);
}

/* --- pool --------------------------------------------------------------------- */

#define DIRTY_BYTES 4096

/* Writes the bottom of its own stack (far below its frame) and yields. */
static void dirty(void *arg)
{
    void *lo;
    void *hi;
    (void)arg;
    if (ico_fiber_current_stack(&lo, &hi) == 0) {
        memset(lo, 0xA5, DIRTY_BYTES);
    }
    ico_fiber_yield();
}

static int zero_seen;

/* 1 in zero_seen when the bottom of its stack reads as zero. */
static void read_bottom(void *arg)
{
    void *lo;
    void *hi;
    size_t i;
    (void)arg;
    zero_seen = ico_fiber_current_stack(&lo, &hi) == 0;
    for (i = 0; zero_seen && i < DIRTY_BYTES; i++) {
        zero_seen = ((unsigned char *)lo)[i] == 0;
    }
}

static void test_pool(void)
{
    static IcoFiber *f[ICO_FIBER_POOL_SLOTS];
    unsigned char *base = (unsigned char *)ico_fiber_pool_base();
    size_t sb = ico_fiber_pool_slot_bytes();
    int i;

    CHECK(base != NULL);
    CHECK(sb != 0 && sb % (64 * 1024) == 0);
    CHECK(sb >= ICO_FIBER_STACK_SIZE + 2 * 4096);
    CHECK(ico_fiber_pool_pinned() == ((uintptr_t)base == (uintptr_t)ICO_FIBER_POOL_BASE));
    printf("pool at %p (%s), %lu bytes a slot\n", (void *)base,
           ico_fiber_pool_pinned() ? "pinned" : "not pinned", (unsigned long)sb);
    for (i = 0; i < ICO_FIBER_POOL_SLOTS; i++) {
        f[i] = ico_fiber_create_in_slot(i, dirty, NULL);
        CHECK(f[i] != NULL);
        if (f[i] == NULL) {
            return;
        }
        CHECK((unsigned char *)f[i] == base + (size_t)i * sb);
        CHECK(ico_fiber_in_slot(i) == f[i] && ico_fiber_slot(f[i]) == i);
    }
    /* a used slot, a full pool, a slot out of range */
    CHECK(ico_fiber_create_in_slot(5, dirty, NULL) == NULL);
    CHECK(ico_fiber_create(dirty, NULL, ICO_FIBER_STACK_SIZE) == NULL);
    CHECK(ico_fiber_create_in_slot(ICO_FIBER_POOL_SLOTS, dirty, NULL) == NULL);
    CHECK(ico_fiber_create_in_slot(-1, dirty, NULL) == NULL);
    /* the pool stays where it is while slots are used */
    CHECK(ico_fiber_pool_init_at(NULL) == -1);
    for (i = 0; i < ICO_FIBER_POOL_SLOTS; i++) {
        CHECK(ico_fiber_resume(f[i]) == 0); /* commits the stack's bottom too */
    }
    for (i = 0; i < ICO_FIBER_POOL_SLOTS; i++) {
        ico_fiber_destroy(f[i]);
        CHECK(ico_fiber_in_slot(i) == NULL);
    }
    /* a slot used again starts from zero pages */
    f[7] = ico_fiber_create_in_slot(7, read_bottom, NULL);
    CHECK(f[7] != NULL);
    if (f[7] != NULL) {
        CHECK(ico_fiber_resume(f[7]) == 0);
        CHECK(ico_fiber_finished(f[7]));
        CHECK(zero_seen == 1);
        ico_fiber_destroy(f[7]);
    }
    /* ico_fiber_create takes the highest free slot */
    f[0] = ico_fiber_create(read_bottom, NULL, 0);
    CHECK(f[0] != NULL && ico_fiber_slot(f[0]) == ICO_FIBER_POOL_SLOTS - 1);
    CHECK(ico_fiber_create(read_bottom, NULL, ICO_FIBER_STACK_SIZE + 16) == NULL);
    ico_fiber_destroy(f[0]);
}

/* --- states ------------------------------------------------------------------- */

#define N_REC 3

static int globals[N_REC];
static int results[N_REC];
static const int depths[N_REC] = {12, 27, 41};

typedef struct Frame {
    struct Frame *up; /* the caller's frame: a pointer into this stack */
    int *global;      /* a pointer into the program's data */
    int idx;
    int depth;
    unsigned char fill[96];
} Frame;

static int frame_ok(const Frame *f, int idx, int depth)
{
    size_t i;
    if (f->global != &globals[idx] || f->idx != idx || f->depth != depth) {
        return 0;
    }
    for (i = 0; i < sizeof f->fill; i++) {
        if (f->fill[i] != (unsigned char)(idx * 7 + depth + i)) {
            return 0;
        }
    }
    return 1;
}

#if defined(_WIN32)
/* The TEB's stack words are the running fiber's (the switch swaps them):
   NT_TIB StackBase (+0x08) and StackLimit (+0x10), DeallocationStack
   (+0x1478), read through gs:0x30 (the TEB's own address) as minicoro's
   switch reads them. */
static int teb_in_stack(void)
{
    char *teb;
    char *w[3];
    char *lo;
    char *hi;
    int i;
    __asm__ volatile("movq %%gs:0x30, %0" : "=r"(teb));
    memcpy(&w[0], teb + 0x08, sizeof w[0]);
    memcpy(&w[1], teb + 0x10, sizeof w[1]);
    memcpy(&w[2], teb + 0x1478, sizeof w[2]);
    if (ico_fiber_current_stack((void **)&lo, (void **)&hi) != 0) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (w[i] < lo || w[i] > hi) {
            return 0;
        }
    }
    return 1;
}
#endif

static int rec(int idx, int depth, Frame *up)
{
    Frame fr;
    volatile Frame *vf = &fr;
    size_t i;
    int r;
    vf->up = up;
    vf->global = &globals[idx];
    vf->idx = idx;
    vf->depth = depth;
    for (i = 0; i < sizeof fr.fill; i++) {
        vf->fill[i] = (unsigned char)(idx * 7 + depth + i);
    }
    if (depth == 0) {
        const Frame *f;
        int d = 0;
        ico_fiber_yield(); /* saved, moved and restored here */
#if defined(_WIN32)
        if (!teb_in_stack()) {
            return -1000;
        }
#endif
        /* the chain of callers' frames, through the moved pointers */
        for (f = &fr; f != NULL; f = f->up, d++) {
            if (!frame_ok(f, idx, d)) {
                return -1;
            }
            (*f->global)++;
        }
        return d;
    }
    r = rec(idx, depth - 1, &fr);
    return r + frame_ok(&fr, idx, depth);
}

static void rec_main(void *arg)
{
    int idx = (int)(intptr_t)arg;
    results[idx] = rec(idx, depths[idx], NULL);
}

static int self_save;

static void save_self(void *arg)
{
    IcoFiberImage img;
    self_save = ico_fiber_slot_save(ico_fiber_slot(ico_fiber_current()), &img);
    (void)arg;
}

#if defined(__SANITIZE_ADDRESS__)
#define NO_ASAN __attribute__((no_sanitize_address))
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NO_ASAN __attribute__((no_sanitize_address))
#endif
#endif
#ifndef NO_ASAN
#define NO_ASAN
#endif

/* A suspended fiber's frames keep their redzones poisoned under the
   address sanitizer, which an intercepted memcpy would report: a plain
   byte loop the sanitizer does not see (volatile, so it stays a loop). */
static NO_ASAN void raw_copy(void *dst, const void *src, size_t n)
{
    volatile const unsigned char *s = (volatile const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* Copies a saved image's bytes out of its slot. */
static void copy_image(IcoFiberImage *img, void **ctrl, void **stack)
{
    *ctrl = malloc(img->ctrl_bytes);
    *stack = malloc(img->stack_bytes);
    raw_copy(*ctrl, img->ctrl, img->ctrl_bytes);
    raw_copy(*stack, img->stack, img->stack_bytes);
    img->ctrl = *ctrl;
    img->stack = *stack;
}

/* The POOL range of the pool at `old_base` and its delta to the pool now. */
static void pool_reloc(IcoStateReloc *r, uintptr_t old_base)
{
    uintptr_t now = (uintptr_t)ico_fiber_pool_base();
    memset(r, 0, sizeof *r);
    r->range[ICO_RELOC_POOL].lo = old_base;
    r->range[ICO_RELOC_POOL].hi = old_base + ICO_FIBER_POOL_SLOTS * ico_fiber_pool_slot_bytes();
    r->range[ICO_RELOC_POOL].delta = (int64_t)(now - old_base);
}

static void test_states(void)
{
    IcoFiber *f[N_REC];
    IcoFiberImage img[N_REC];
    void *ctrl[N_REC];
    void *stack[N_REC];
    IcoStateReloc r;
    IcoStateReloc wrong;
    uintptr_t old_base;
    IcoFiber *g;
    int i;

    /* a running fiber is refused */
    g = ico_fiber_create_in_slot(9, save_self, NULL);
    CHECK(g != NULL);
    if (g != NULL) {
        self_save = 0;
        CHECK(ico_fiber_resume(g) == 0);
        CHECK(self_save == -1);
        /* and a finished one */
        CHECK(ico_fiber_slot_save(9, &img[0]) == -1);
        ico_fiber_destroy(g);
    }
    CHECK(ico_fiber_slot_save(9, &img[0]) == -1); /* a free slot */

    for (i = 0; i < N_REC; i++) {
        f[i] = ico_fiber_create_in_slot(i + 1, rec_main, (void *)(intptr_t)i);
        CHECK(f[i] != NULL);
        if (f[i] == NULL) {
            return;
        }
        CHECK(ico_fiber_resume(f[i]) == 0); /* down to the yield */
        CHECK(!ico_fiber_finished(f[i]));
    }
    for (i = 0; i < N_REC; i++) {
        CHECK(ico_fiber_slot_save(i + 1, &img[i]) == 0);
        CHECK(img[i].slot == (uint32_t)(i + 1));
        CHECK(img[i].slot_base == (uint64_t)(uintptr_t)f[i]);
        /* the used stack only: deeper recursion, more bytes */
        CHECK(img[i].stack_bytes > 0 && img[i].stack_bytes < ICO_FIBER_STACK_SIZE);
        CHECK(i == 0 || img[i].stack_bytes > img[i - 1].stack_bytes);
        copy_image(&img[i], &ctrl[i], &stack[i]);
        ico_fiber_destroy(f[i]);
    }
    old_base = (uintptr_t)ico_fiber_pool_base();
    CHECK(ico_fiber_pool_init_at(NULL) == 0);
    CHECK((uintptr_t)ico_fiber_pool_base() != old_base);
    printf("pool moved from %p to %p\n", (void *)old_base, ico_fiber_pool_base());
    pool_reloc(&r, old_base);

    /* a wrong POOL delta: refused, the slot stays free */
    wrong = r;
    wrong.range[ICO_RELOC_POOL].delta += 0x10000;
    CHECK(ico_fiber_slot_restore(1, &img[0], &wrong) == -1);
    CHECK(ico_fiber_in_slot(1) == NULL);
    /* the image into another slot: refused */
    CHECK(ico_fiber_slot_restore(2, &img[0], &r) == -1);

    for (i = 0; i < N_REC; i++) {
        CHECK(ico_fiber_slot_restore(i + 1, &img[i], &r) == 0);
        f[i] = ico_fiber_in_slot(i + 1);
        CHECK(f[i] != NULL && ico_fiber_slot(f[i]) == i + 1);
    }
    /* a restore into a used slot: refused */
    CHECK(ico_fiber_slot_restore(1, &img[0], &r) == -1);
    for (i = 0; i < N_REC; i++) {
        if (f[i] == NULL) {
            continue;
        }
        CHECK(ico_fiber_resume(f[i]) == 0);
        CHECK(ico_fiber_finished(f[i]));
        /* depth + 1 frames walked at the bottom, each frame checked again
           on the way up */
        CHECK(results[i] == 2 * (depths[i] + 1) - 1);
        CHECK(globals[i] == depths[i] + 1);
        ico_fiber_destroy(f[i]);
        CHECK(ico_fiber_in_slot(i + 1) == NULL);
    }
    for (i = 0; i < N_REC; i++) {
        free(ctrl[i]);
        free(stack[i]);
    }
}

/* --- guard (a child process) ----------------------------------------------------- */

#if !defined(_WIN32)
static int overflow(int n)
{
    volatile char frame[1024];
    frame[0] = (char)n;
    if (n == -1) { /* never: keeps the compiler from calling this endless */
        return 0;
    }
    return overflow(n + 1) + frame[0];
}

static void late_runaway(void *arg)
{
    (void)arg;
    ico_fiber_yield(); /* saved here */
    printf("unreachable %d\n", overflow(0));
}

static int guard_test(void)
{
    IcoFiber *f = ico_fiber_create_in_slot(4, late_runaway, NULL);
    IcoFiberImage img;
    IcoStateReloc r;
    void *ctrl;
    void *stack;
    uintptr_t old_base;
    pid_t pid;
    int st;
    if (f == NULL || ico_fiber_resume(f) != 0 || ico_fiber_slot_save(4, &img) != 0) {
        printf("FAIL: setting up the saved fiber\n");
        return 1;
    }
    copy_image(&img, &ctrl, &stack);
    ico_fiber_destroy(f);
    old_base = (uintptr_t)ico_fiber_pool_base();
    if (ico_fiber_pool_init_at(NULL) != 0) {
        printf("FAIL: a second pool\n");
        return 1;
    }
    pool_reloc(&r, old_base);
    if (ico_fiber_slot_restore(4, &img, &r) != 0) {
        printf("FAIL: the restore\n");
        return 1;
    }
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        ico_fiber_resume(ico_fiber_in_slot(4));
        _exit(0); /* the overflow went unnoticed */
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) {
        printf("FAIL: fork\n");
        return 1;
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        printf("FAIL: a restored fiber's stack overflow was not caught\n");
        return 1;
    }
    printf("overflow stopped (%s %d)\n", WIFSIGNALED(st) ? "signal" : "exit status",
           WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
    ico_fiber_slot_discard(4);
    free(ctrl);
    free(stack);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    IcoFiberImage probe;
    if (argc > 1 && strcmp(argv[1], "guard") == 0) {
#if !defined(_WIN32)
        return guard_test();
#else
        return 77;
#endif
    }
    test_words();
    test_pool();
    /* the save calls exist on the asm backend's x86-64 and arm64 only */
    if (ico_fiber_slot_save(0, &probe) == -1 && strcmp(ico_fiber_backend(), "asm") != 0) {
        printf("no save states on this backend (%s)\n", ico_fiber_backend());
    } else {
        test_states();
    }
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
