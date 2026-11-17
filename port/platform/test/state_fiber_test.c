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
 *   dead    a fiber that wrote a pattern in a deep frame, returned and was
 *           saved at a shallower depth: the image starts at or below the
 *           pattern (the dead frames under the stack pointer are saved)
 *           and, restored into a fresh slot, the stack's bytes from the
 *           image's start to its top are the saved ones byte for byte.
 *   misses  another build's load (state_reloc.h IcoStateMiss): the same
 *           fiber's image with one program word that has no translation
 *           (a word into a hole the translation leaves in this program's
 *           image) planted at a time, restored with the translation and
 *           counted by place: two words in the dead frames (the deepest
 *           saved word and the last word below the stack pointer less the
 *           red zone, the split ico_state_stack_dead_bytes gives the save's
 *           soft scan) are dead, the restore resumes to the end and the
 *           verdict keeps the load; the first word of the red zone, a word
 *           64 bytes below the stack pointer, one 8 bytes above it and a
 *           callee-saved register (x19 / rbx) are each live, named with
 *           their place word for word, and the verdict refuses the load; a
 *           same-build restore (no translation) leaves the planted word as
 *           it is and counts nothing.
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

/* --- dead frames ---------------------------------------------------------------- */

#define DEAD_BYTES 8192
#define DEAD_KEPT 1024 /* its deepest bytes, below anything the yield's frames reach */

static uintptr_t dead_at; /* the pattern's address in the fiber's stack */
static int dead_done;

/* a deep frame's local array filled with a pattern, then returned from */
static NO_ASAN __attribute__((noinline)) void dead_scribble(void)
{
    volatile unsigned char buf[DEAD_BYTES];
    size_t i;
    for (i = 0; i < DEAD_BYTES; i++) {
        buf[i] = 0xA5;
    }
    dead_at = (uintptr_t)&buf[0];
}

static void dead_main(void *arg)
{
    (void)arg;
    dead_scribble();
    ico_fiber_yield(); /* saved here, the pattern below the stack pointer */
    dead_done = 1;
}

static NO_ASAN size_t count_pattern(const void *p, size_t n)
{
    volatile const unsigned char *b = (volatile const unsigned char *)p;
    size_t i;
    size_t k = 0;
    for (i = 0; i < n; i++) {
        k += b[i] == 0xA5;
    }
    return k;
}

static NO_ASAN int same_bytes(const void *a, const void *b, size_t n)
{
    volatile const unsigned char *x = (volatile const unsigned char *)a;
    volatile const unsigned char *y = (volatile const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return 0;
        }
    }
    return 1;
}

static void test_dead_frames(void)
{
    IcoFiberImage img;
    IcoStateReloc r;
    void *ctrl = NULL;
    void *stack = NULL;
    uintptr_t base;
    IcoFiber *g = ico_fiber_create_in_slot(5, dead_main, NULL);

    CHECK(g != NULL);
    if (g == NULL) {
        return;
    }
    dead_done = 0;
    CHECK(ico_fiber_resume(g) == 0);
    CHECK(ico_fiber_slot_save(5, &img) == 0);
    base = (uintptr_t)img.slot_base;
    if (dead_at < base || dead_at >= base + ico_fiber_pool_slot_bytes()) {
        /* the address sanitizer's fake stack holds the frame elsewhere */
        printf("dead frames: the frame is off the fiber's stack (a fake stack); skipped\n");
        CHECK(ico_fiber_resume(g) == 0);
        ico_fiber_destroy(g);
        return;
    }
    /* the image reaches down to the dead frame's pattern */
    CHECK(base + img.stack_off <= dead_at);
    CHECK(count_pattern((const void *)dead_at, DEAD_KEPT) == DEAD_KEPT);
    copy_image(&img, &ctrl, &stack);
    ico_fiber_destroy(g);
    pool_reloc(&r, (uintptr_t)ico_fiber_pool_base()); /* the same pool: delta 0 */
    CHECK(ico_fiber_slot_restore(5, &img, &r) == 0);
    {
        /* the bytes the save leaves unscanned (back_ctx): the saved ones held
           the resumer's registers, the restore zeroes them */
        static const unsigned char zero[256];
        size_t off = 0, n = 0;

        /* the rewritten span lies inside the control page */
        CHECK(ico_fiber_image_rewritten(&img, &off, &n) == 0 && n > 0 && n <= sizeof zero &&
              off + n <= img.ctrl_bytes);
        if (n > 0 && n <= sizeof zero && off + n <= img.ctrl_bytes) {
            /* the saved back_ctx held the resumer's registers; the restore
               zeroed it */
            CHECK(memcmp((const unsigned char *)img.ctrl + off, zero, n) != 0);
            CHECK(memcmp((const void *)(base + off), zero, n) == 0);
        }
    }
    g = ico_fiber_in_slot(5);
    CHECK(g != NULL);
    if (g != NULL) {
        /* the stack as saved, the dead frames included (nothing moved:
           delta 0) */
        CHECK(same_bytes((const void *)(base + img.stack_off), stack, img.stack_bytes));
        CHECK(count_pattern((const void *)dead_at, DEAD_KEPT) == DEAD_KEPT);
        CHECK(ico_fiber_resume(g) == 0);
        CHECK(ico_fiber_finished(g) && dead_done == 1);
        ico_fiber_destroy(g);
    }
    free(ctrl);
    free(stack);
}

/* --- misses (another build's load) ------------------------------------------------ */

/* the translation's hole: a word pointing here has none (nothing of the
   fiber's code points here) */
static unsigned char s_hole[64];

/* where the switch context's stack pointer and a callee-saved register sit
   in it (minicoro's _mco_ctxbuf: arm64 x19-x30, sp; x86-64 rip, rsp, rbp,
   rbx) */
#if defined(__aarch64__)
#define CTX_SP_OFF 96
#define CTX_REG_OFF 0 /* x19 */
#else
#define CTX_SP_OFF 8
#define CTX_REG_OFF 24 /* rbx */
#endif

#define MISS_SLOT 7

typedef struct MissRun {
    const IcoFiberImage *img; /* the saved image (copied out) */
    const IcoStateReloc *r;
    IcoStateMiss m;
    uint64_t total; /* the translation's own counter: every miss */
    uint64_t first_total;
} MissRun;

/* restores the image with `word` written at the stack address `stack_at`
   (0: none) or at the control page's offset `ctrl_off` (SIZE_MAX: none);
   0 when the restore succeeded (the slot then holds the fiber) */
static int miss_restore(MissRun *k, uint64_t word, uint64_t stack_at, size_t ctrl_off,
                        int translate)
{
    IcoFiberImage in = *k->img;
    IcoStateReloc r = *k->r;
    IcoStateXlate xl = *k->r->xlate;
    unsigned char *ctrl = (unsigned char *)malloc(in.ctrl_bytes);
    unsigned char *stack = (unsigned char *)malloc(in.stack_bytes);
    const uint64_t base = in.slot_base + in.stack_off;
    int rc;

    if (ctrl == NULL || stack == NULL) {
        free(ctrl);
        free(stack);
        return -1;
    }
    memcpy(ctrl, in.ctrl, in.ctrl_bytes);
    memcpy(stack, in.stack, in.stack_bytes);
    if (stack_at != 0) {
        memcpy(stack + (size_t)(stack_at - base), &word, sizeof word);
    }
    if (ctrl_off != SIZE_MAX) {
        memcpy(ctrl + ctrl_off, &word, sizeof word);
    }
    in.ctrl = ctrl;
    in.stack = stack;
    memset(&k->m, 0, sizeof k->m);
    k->total = 0;
    k->first_total = 0;
    xl.miss = &k->m;
    xl.missed = &k->total;
    xl.first_missed = &k->first_total;
    r.xlate = translate ? &xl : NULL;
    rc = ico_fiber_slot_restore(MISS_SLOT, &in, &r);
    free(ctrl);
    free(stack);
    return rc;
}

static void test_misses(void)
{
    IcoFiberImage img;
    IcoStateImage im;
    IcoStateReloc r;
    IcoStateXlate xl;
    IcoStateSpan span[2];
    MissRun k;
    void *ctrl = NULL;
    void *stack = NULL;
    uint64_t sp, base, word, at;
    size_t dead, back_off = 0, back_n = 0, ctx_off;
    char text[320], want[320];
    IcoFiber *g = ico_fiber_create_in_slot(MISS_SLOT, dead_main, NULL);

    CHECK(g != NULL);
    if (g == NULL) {
        return;
    }
    CHECK(ico_state_image_range(&im) == 0);
    dead_done = 0;
    CHECK(ico_fiber_resume(g) == 0);
    CHECK(ico_fiber_slot_save(MISS_SLOT, &img) == 0);
    copy_image(&img, &ctrl, &stack);
    ico_fiber_destroy(g);

    /* the translation: this program's image onto itself, but for s_hole */
    pool_reloc(&r, (uintptr_t)ico_fiber_pool_base()); /* the same pool: delta 0 */
    r.range[ICO_RELOC_MODULE].lo = im.lo;
    r.range[ICO_RELOC_MODULE].hi = im.hi;
    span[0].lo = im.lo;
    span[0].hi = (uint64_t)(uintptr_t)s_hole - 1;
    span[0].now = span[0].lo;
    span[1].lo = (uint64_t)(uintptr_t)s_hole + sizeof s_hole;
    span[1].hi = im.hi;
    span[1].now = span[1].lo;
    memset(&xl, 0, sizeof xl);
    xl.span = span;
    xl.n_span = 2;
    r.xlate = &xl;
    word = (uint64_t)(uintptr_t)&s_hole[8];
    CHECK(word > im.lo && word < im.hi);
    k.img = &img;
    k.r = &r;

    sp = ico_fiber_image_sp(&img);
    base = img.slot_base + img.stack_off;
    dead = ico_state_stack_dead_bytes(base, img.stack_bytes, sp);
    printf("misses: saved stack 0x%llx + %u bytes, sp 0x%llx, %zu bytes dead\n",
           (unsigned long long)base, (unsigned)img.stack_bytes, (unsigned long long)sp, dead);
    /* the dead frames reach down to the pattern: some dead bytes, and the
       first live word inside the red zone */
    CHECK(sp != 0 && dead < img.stack_bytes);
    CHECK(base + dead <= sp - ICO_STATE_RED_ZONE && base + dead + 8 > sp - ICO_STATE_RED_ZONE);

    /* nothing planted: no miss anywhere (the fiber's own words translate) */
    CHECK(miss_restore(&k, 0, 0, SIZE_MAX, 1) == 0);
    CHECK(k.m.dead == 0 && k.m.live == 0 && k.total == 0);
    ico_fiber_slot_discard(MISS_SLOT);

    /* dead: the deepest saved word, then the last word below sp - 128;
       the load is kept and the fiber resumes to its end.  Under the
       address sanitizer the deep frame may lie on its fake stack, leaving
       too few dead bytes saved */
    if (dead < 16) {
        printf("misses: %zu dead bytes saved (a fake stack): the dead cases skipped\n", dead);
    } else {
        CHECK(miss_restore(&k, word, base, SIZE_MAX, 1) == 0);
        CHECK(k.m.dead == 1 && k.m.first_dead == word && k.m.live == 0 && k.total == 1);
        ico_fiber_slot_discard(MISS_SLOT);
        CHECK(miss_restore(&k, word, base + dead - 8, SIZE_MAX, 1) == 0);
        CHECK(k.m.dead == 1 && k.m.live == 0 && k.total == 1);
        CHECK(ico_state_miss_refuses(&k.m, im.lo, text, sizeof text) == 0 && text[0] == '\0');
        ico_state_miss_counts(&k.m, text, sizeof text);
        snprintf(
            want, sizeof want,
            "1 program words in dead stack frames had no translation and were left as they were "
            "(first 0x%llx); 0 in live memory",
            (unsigned long long)word);
        CHECK(strcmp(text, want) == 0);
        g = ico_fiber_in_slot(MISS_SLOT);
        CHECK(g != NULL);
        if (g != NULL) {
            dead_done = 0;
            CHECK(ico_fiber_resume(g) == 0);
            CHECK(ico_fiber_finished(g) && dead_done == 1);
            ico_fiber_destroy(g);
        }
    }

    /* live: the first word of the red zone (sp - 128 when sp is 8-aligned) */
    at = base + dead;
    CHECK(miss_restore(&k, word, at, SIZE_MAX, 1) == 0);
    CHECK(k.m.dead == 0 && k.m.live == 1 && k.m.first_live == word && k.total == 1);
    CHECK(k.m.live_fiber == MISS_SLOT + 1 && k.m.live_sp_rel == 1 &&
          k.m.live_off == (int64_t)(at - sp));
    CHECK(k.m.live_where != NULL && strcmp(k.m.live_where, "stack") == 0);
    CHECK(ico_state_miss_refuses(&k.m, im.lo, text, sizeof text) == 1);
    snprintf(want, sizeof want,
             "1 program words in live memory had no translation, the first: fiber slot %d's "
             "stack, %llu bytes below its saved stack pointer, holds 0x%llx (offset 0x%llx in "
             "the saving build's image)",
             MISS_SLOT, (unsigned long long)(sp - at), (unsigned long long)word,
             (unsigned long long)(word - im.lo));
    CHECK(strcmp(text, want) == 0);
    if (strcmp(text, want) != 0) {
        printf("  got:  %s\n  want: %s\n", text, want);
    }
    ico_fiber_slot_discard(MISS_SLOT);

    /* live: 64 bytes below sp, inside the red zone */
    CHECK(miss_restore(&k, word, sp - 64, SIZE_MAX, 1) == 0);
    CHECK(k.m.dead == 0 && k.m.live == 1 && k.m.live_off == -64);
    ico_fiber_slot_discard(MISS_SLOT);

    /* live: 8 bytes above sp (the yield's own frame) */
    CHECK(sp + 16 <= base + img.stack_bytes);
    CHECK(miss_restore(&k, word, sp + 8, SIZE_MAX, 1) == 0);
    CHECK(k.m.dead == 0 && k.m.live == 1 && k.m.live_off == 8);
    ico_state_miss_live_text(&k.m, im.lo, text, sizeof text);
    snprintf(want, sizeof want,
             "fiber slot %d's stack, 8 bytes above its saved stack pointer, holds 0x%llx (offset "
             "0x%llx in the saving build's image)",
             MISS_SLOT, (unsigned long long)word, (unsigned long long)(word - im.lo));
    CHECK(strcmp(text, want) == 0);
    ico_fiber_slot_discard(MISS_SLOT);

    /* live: a callee-saved register of the switch context, which lies
       just before back_ctx (fiber.h ico_fiber_image_rewritten) */
    CHECK(ico_fiber_image_rewritten(&img, &back_off, &back_n) == 0 && back_off >= back_n);
    ctx_off = back_off - back_n;
    {
        uint64_t sp_word;

        memcpy(&sp_word, (const unsigned char *)img.ctrl + ctx_off + CTX_SP_OFF, sizeof sp_word);
        CHECK(sp_word == sp); /* the layout this test assumes */
        if (sp_word == sp) {
            CHECK(miss_restore(&k, word, 0, ctx_off + CTX_REG_OFF, 1) == 0);
            CHECK(k.m.dead == 0 && k.m.live == 1 && k.m.live_off == CTX_REG_OFF &&
                  k.m.live_sp_rel == 0 && k.m.live_fiber == MISS_SLOT + 1);
            CHECK(k.m.live_where != NULL && strcmp(k.m.live_where, "saved registers") == 0);
            ico_state_miss_live_text(&k.m, im.lo, text, sizeof text);
            snprintf(want, sizeof want,
                     "fiber slot %d's saved registers at offset 0x%x holds 0x%llx (offset 0x%llx "
                     "in the saving build's image)",
                     MISS_SLOT, (unsigned)CTX_REG_OFF, (unsigned long long)word,
                     (unsigned long long)(word - im.lo));
            CHECK(strcmp(text, want) == 0);
            ico_fiber_slot_discard(MISS_SLOT);
        }
    }

    /* the same build: no translation, nothing counted, the word kept */
    CHECK(miss_restore(&k, word, sp - 64, SIZE_MAX, 0) == 0);
    CHECK(k.m.dead == 0 && k.m.live == 0 && k.total == 0);
    {
        uint64_t kept = 0;

        raw_copy(&kept, (const void *)(uintptr_t)(sp - 64), sizeof kept);
        CHECK(kept == word);
    }
    ico_fiber_slot_discard(MISS_SLOT);
    free(ctrl);
    free(stack);
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
        test_dead_frames();
        test_misses();
    }
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
