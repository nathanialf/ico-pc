/*
 * port/platform/test/state_guard_test.c
 *
 * Save states' two process values (state_reloc.h, "The saving process's own
 * values"): the game thread's thread pointer and the stack guard, which a
 * suspended fiber's registers and frames hold and a later process has
 * others of.  The rewrite is platform-neutral, so the x86 host simulates
 * the two processes' values:
 *
 *   words   ico_state_reloc_word with the values on: an exact match is
 *           rewritten and counted, near misses (one off, one bit off, the
 *           other value's word) and range words are not counted as values,
 *           a value is checked before the ranges; off when its old word is
 *           0, its new word is 0, or the two are equal.
 *   scan    ico_state_reloc_scan over a synthetic region: every old word
 *           rewritten, the counters exact, everything else untouched; a
 *           second scan rewrites no value again.
 *   fiber   a real fiber holding the old thread pointer and guard in a live
 *           local (a callee-saved register or a spill) and in its frame,
 *           saved, destroyed and restored with the values on: resumed, it
 *           finds the new values, its other words as they were; restored
 *           with old == new, nothing changes and nothing is counted.
 *   needs   ico_state_proc_check per platform tag: android-arm64 refuses a
 *           state without either value, linux-arm64 without the thread
 *           pointer, macOS and iOS without the guard, the others need none.
 *   read    ico_state_proc_read on this host: both 0 on x86-64; the thread
 *           pointer on arm64 Linux and Android (the guard on Android), the
 *           guard on Apple.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "state_reloc.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#if defined(__SANITIZE_ADDRESS__)
#define GUARD_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define GUARD_ASAN 1
#endif
#endif

#ifdef GUARD_ASAN
#define NO_ASAN __attribute__((no_sanitize_address))
#else
#define NO_ASAN
#endif

/* the saving process's values and this one's: no address in any range
   below (a thread pointer is a high user address, a guard random) */
#define OLD_TLS 0x00007f5a3c2d1700ull
#define NEW_TLS 0x00007e0011223300ull
#define OLD_GUARD 0x5a17c0ffee12ab00ull
#define NEW_GUARD 0x2bd4e1f00d9c7700ull

/* one range that moves, away from the values */
#define ARENA_LO 0x3C00000000ull
#define ARENA_HI 0x3C02000000ull
#define ARENA_DELTA 0x100000000ll

static void arena_reloc(IcoStateReloc *r)
{
    memset(r, 0, sizeof *r);
    r->range[ICO_RELOC_ARENA].lo = ARENA_LO;
    r->range[ICO_RELOC_ARENA].hi = ARENA_HI;
    r->range[ICO_RELOC_ARENA].delta = ARENA_DELTA;
}

/* --- words -------------------------------------------------------------------- */

static void test_words(void)
{
    IcoStateReloc r;
    uint64_t n[ICO_RELOC_VALUES] = {99, 99};
    uint64_t w;

    arena_reloc(&r);
    CHECK(!ico_state_reloc_values_on(&r));
    ico_state_reloc_set_values(&r, OLD_TLS, NEW_TLS, OLD_GUARD, NEW_GUARD, n);
    CHECK(n[ICO_RELOC_TLS] == 0 && n[ICO_RELOC_GUARD] == 0); /* zeroed */
    CHECK(ico_state_reloc_values_on(&r));

    w = OLD_TLS;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == NEW_TLS);
    w = OLD_GUARD;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == NEW_GUARD);
    CHECK(n[ICO_RELOC_TLS] == 1 && n[ICO_RELOC_GUARD] == 1);
    /* near misses stay */
    w = OLD_TLS + 1;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_TLS + 1);
    w = OLD_TLS - 8;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_TLS - 8);
    w = OLD_GUARD ^ 0x8000000000000000ull;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == (OLD_GUARD ^ 0x8000000000000000ull));
    w = OLD_TLS + 0x28; /* the guard's slot address is not the thread pointer */
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_TLS + 0x28);
    /* a word already this process's is not touched */
    w = NEW_TLS;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == NEW_TLS);
    w = NEW_GUARD;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == NEW_GUARD);
    w = 0;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0);
    CHECK(n[ICO_RELOC_TLS] == 1 && n[ICO_RELOC_GUARD] == 1);
    /* the range still moves its words, and they are not counted as values */
    w = ARENA_LO + 0x40;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == ARENA_LO + 0x40 + (uint64_t)ARENA_DELTA);
    CHECK(n[ICO_RELOC_TLS] == 1 && n[ICO_RELOC_GUARD] == 1);
    /* a value is matched before the ranges: one inside a range that did not
       move is still rewritten */
    ico_state_reloc_set_values(&r, ARENA_LO + 0x80, NEW_TLS, 0, 0, n);
    r.range[ICO_RELOC_ARENA].delta = 0;
    w = ARENA_LO + 0x80;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == NEW_TLS && n[ICO_RELOC_TLS] == 1);
    r.range[ICO_RELOC_ARENA].delta = ARENA_DELTA;

    /* off: an old word 0 (a state without the record), a new word 0, the
       same value in both processes (one session); counters NULL are fine */
    ico_state_reloc_set_values(&r, 0, NEW_TLS, OLD_GUARD, 0, n);
    CHECK(!ico_state_reloc_values_on(&r));
    w = OLD_GUARD;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_GUARD);
    w = 0;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == 0);
    ico_state_reloc_set_values(&r, OLD_TLS, OLD_TLS, OLD_GUARD, OLD_GUARD, n);
    CHECK(!ico_state_reloc_values_on(&r));
    w = OLD_TLS;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_TLS);
    w = OLD_GUARD;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_GUARD);
    CHECK(n[ICO_RELOC_TLS] == 0 && n[ICO_RELOC_GUARD] == 0);
    /* one on, the other off */
    ico_state_reloc_set_values(&r, OLD_TLS, NEW_TLS, OLD_GUARD, OLD_GUARD, NULL);
    CHECK(ico_state_reloc_values_on(&r));
    w = OLD_TLS;
    CHECK(ico_state_reloc_word(&r, &w) == 1 && w == NEW_TLS);
    w = OLD_GUARD;
    CHECK(ico_state_reloc_word(&r, &w) == 0 && w == OLD_GUARD);
}

/* --- scan --------------------------------------------------------------------- */

#define SCAN_WORDS 64

static void test_scan(void)
{
    static uint64_t buf[SCAN_WORDS + 1];
    uint64_t want[SCAN_WORDS + 1];
    IcoStateReloc r;
    uint64_t n[ICO_RELOC_VALUES];
    uint64_t total = 0;
    size_t moved;
    int i;

    for (i = 0; i <= SCAN_WORDS; i++) {
        buf[i] = 0x1000u + (uint64_t)i * 0x11u; /* small ints: nothing to move */
    }
    buf[3] = OLD_TLS;
    buf[10] = OLD_TLS;
    buf[33] = OLD_TLS;
    buf[7] = OLD_GUARD;
    buf[40] = OLD_GUARD;
    buf[11] = OLD_TLS + 8;
    buf[12] = OLD_GUARD - 1;
    buf[13] = NEW_TLS;
    buf[20] = ARENA_LO + 0x100; /* a range word */
    buf[SCAN_WORDS] = OLD_TLS;  /* outside the scanned bytes */
    memcpy(want, buf, sizeof want);
    want[3] = want[10] = want[33] = NEW_TLS;
    want[7] = want[40] = NEW_GUARD;
    want[20] = ARENA_LO + 0x100 + (uint64_t)ARENA_DELTA;

    arena_reloc(&r);
    ico_state_reloc_set_values(&r, OLD_TLS, NEW_TLS, OLD_GUARD, NEW_GUARD, n);
    moved = ico_state_reloc_scan(&r, buf, SCAN_WORDS * sizeof buf[0], &total);
    CHECK(moved == 6 && total == 6); /* 3 + 2 values, 1 range word */
    CHECK(n[ICO_RELOC_TLS] == 3 && n[ICO_RELOC_GUARD] == 2);
    CHECK(memcmp(buf, want, sizeof want) == 0);
    for (i = 0; i <= SCAN_WORDS; i++) {
        if (buf[i] != want[i]) {
            printf("  word %d: %#llx, want %#llx\n", i, (unsigned long long)buf[i],
                   (unsigned long long)want[i]);
        }
    }
    /* the values do not come back: a second scan finds none (the range
       word moved out of the old range) */
    moved = ico_state_reloc_scan(&r, buf, SCAN_WORDS * sizeof buf[0], NULL);
    CHECK(moved == 0);
    CHECK(n[ICO_RELOC_TLS] == 3 && n[ICO_RELOC_GUARD] == 2);
}

/* --- fiber -------------------------------------------------------------------- */

/* the values the fiber picks up (volatile: read once before the yield, so
   the compiler must keep them, not read them again after it) */
static volatile uint64_t seed_tls = OLD_TLS;
static volatile uint64_t seed_guard = OLD_GUARD;

#define FRAME_WORDS 6
static uint64_t seen_tls, seen_guard;
static uint64_t seen_frame[FRAME_WORDS];
static uintptr_t frame_at;
static int holder_done;

static __attribute__((noinline)) void holder(void *arg)
{
    const uint64_t t = seed_tls;
    const uint64_t g = seed_guard;
    volatile uint64_t frame[FRAME_WORDS];
    int i;

    (void)arg;
    frame[0] = t;
    frame[1] = g;
    frame[2] = t ^ 1u;
    frame[3] = t + 8u;
    frame[4] = g - 1u;
    frame[5] = 0x123456789abcdef0ull;
    frame_at = (uintptr_t)&frame[0];
    ico_fiber_yield(); /* saved, destroyed and restored here */
    seen_tls = t;
    seen_guard = g;
    for (i = 0; i < FRAME_WORDS; i++) {
        seen_frame[i] = frame[i];
    }
    holder_done = 1;
}

/* A suspended fiber's frames keep their redzones poisoned under the
   address sanitizer: a plain byte loop it does not see. */
static NO_ASAN void raw_copy(void *dst, const void *src, size_t n)
{
    volatile const unsigned char *s = (volatile const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* Runs holder in `slot` to its yield, saves it, destroys it and restores
   it into the same slot (the same pool: the POOL delta 0) with the values
   (tls_old -> tls_now, guard_old -> guard_now), then resumes it to the
   end.  0, or -1 when a step failed. */
static int round_trip(int slot, uint64_t tls_old, uint64_t tls_now, uint64_t guard_old,
                      uint64_t guard_now, uint64_t n[ICO_RELOC_VALUES], int *on_stack)
{
    IcoFiber *f = ico_fiber_create_in_slot(slot, holder, NULL);
    IcoFiberImage img;
    IcoStateReloc r;
    void *ctrl;
    void *stack;
    uintptr_t base;
    int rc = 0;

    if (f == NULL) {
        return -1;
    }
    holder_done = 0;
    if (ico_fiber_resume(f) != 0 || ico_fiber_slot_save(slot, &img) != 0) {
        return -1;
    }
    base = (uintptr_t)img.slot_base;
    /* the address sanitizer's fake stack can hold the frame elsewhere */
    *on_stack = frame_at >= base + img.stack_off && frame_at < base + ico_fiber_pool_slot_bytes();
    ctrl = malloc(img.ctrl_bytes);
    stack = malloc(img.stack_bytes);
    if (ctrl == NULL || stack == NULL) {
        free(ctrl);
        free(stack);
        return -1;
    }
    raw_copy(ctrl, img.ctrl, img.ctrl_bytes);
    raw_copy(stack, img.stack, img.stack_bytes);
    img.ctrl = ctrl;
    img.stack = stack;
    ico_fiber_destroy(f);

    memset(&r, 0, sizeof r);
    r.range[ICO_RELOC_POOL].lo = (uint64_t)(uintptr_t)ico_fiber_pool_base();
    r.range[ICO_RELOC_POOL].hi =
        r.range[ICO_RELOC_POOL].lo + ICO_FIBER_POOL_SLOTS * ico_fiber_pool_slot_bytes();
    ico_state_reloc_set_values(&r, tls_old, tls_now, guard_old, guard_now, n);
    if (ico_fiber_slot_restore(slot, &img, &r) != 0 || (f = ico_fiber_in_slot(slot)) == NULL) {
        rc = -1;
    } else {
        if (ico_fiber_resume(f) != 0 || !ico_fiber_finished(f) || !holder_done) {
            rc = -1;
        }
        ico_fiber_destroy(f);
    }
    free(ctrl);
    free(stack);
    return rc;
}

static void test_fiber(void)
{
    uint64_t n[ICO_RELOC_VALUES];
    int on_stack = 0;

    /* another process: the values rewritten wherever the fiber kept them */
    CHECK(round_trip(6, OLD_TLS, NEW_TLS, OLD_GUARD, NEW_GUARD, n, &on_stack) == 0);
    printf("fiber: %llu thread pointer word(s), %llu guard word(s) rewritten%s\n",
           (unsigned long long)n[ICO_RELOC_TLS], (unsigned long long)n[ICO_RELOC_GUARD],
           on_stack ? "" : " (the frame was off the fiber's stack: its words not checked)");
    /* the live locals: in a callee-saved register of the switch context or
       spilled to the fiber's stack, either way restored with the new value */
    CHECK(seen_tls == NEW_TLS);
    CHECK(seen_guard == NEW_GUARD);
    CHECK(n[ICO_RELOC_TLS] >= 1 && n[ICO_RELOC_GUARD] >= 1);
    if (on_stack) {
        CHECK(seen_frame[0] == NEW_TLS);
        CHECK(seen_frame[1] == NEW_GUARD);
        CHECK(seen_frame[2] == (OLD_TLS ^ 1u));
        CHECK(seen_frame[3] == OLD_TLS + 8u);
        CHECK(seen_frame[4] == OLD_GUARD - 1u);
        CHECK(seen_frame[5] == 0x123456789abcdef0ull);
        CHECK(n[ICO_RELOC_TLS] >= 2 && n[ICO_RELOC_GUARD] >= 2);
    }

    /* the same process (old == new): nothing rewritten, nothing counted */
    CHECK(round_trip(6, OLD_TLS, OLD_TLS, OLD_GUARD, OLD_GUARD, n, &on_stack) == 0);
    CHECK(seen_tls == OLD_TLS && seen_guard == OLD_GUARD);
    CHECK(n[ICO_RELOC_TLS] == 0 && n[ICO_RELOC_GUARD] == 0);
    if (on_stack) {
        CHECK(seen_frame[0] == OLD_TLS && seen_frame[1] == OLD_GUARD);
    }
    /* no record (old 0): the same */
    CHECK(round_trip(6, 0, NEW_TLS, 0, NEW_GUARD, n, &on_stack) == 0);
    CHECK(seen_tls == OLD_TLS && seen_guard == OLD_GUARD);
    CHECK(n[ICO_RELOC_TLS] == 0 && n[ICO_RELOC_GUARD] == 0);
}

/* --- needs -------------------------------------------------------------------- */

static void test_needs(void)
{
    const uint64_t T = OLD_TLS;
    const uint64_t G = OLD_GUARD;

    CHECK(ico_state_proc_needs("android-arm64") == (ICO_STATE_PROC_TLS | ICO_STATE_PROC_GUARD));
    CHECK(ico_state_proc_check("android-arm64", T, G) == 0);
    CHECK(ico_state_proc_check("android-arm64", 0, G) == -1);
    CHECK(ico_state_proc_check("android-arm64", T, 0) == -1);
    CHECK(ico_state_proc_check("android-arm64", 0, 0) == -1);

    CHECK(ico_state_proc_needs("linux-arm64") == ICO_STATE_PROC_TLS);
    CHECK(ico_state_proc_check("linux-arm64", T, 0) == 0);
    CHECK(ico_state_proc_check("linux-arm64", 0, 0) == -1);

    CHECK(ico_state_proc_needs("macos-arm64") == ICO_STATE_PROC_GUARD);
    CHECK(ico_state_proc_needs("macos-x64") == ICO_STATE_PROC_GUARD);
    CHECK(ico_state_proc_needs("ios-arm64") == ICO_STATE_PROC_GUARD);
    CHECK(ico_state_proc_check("macos-arm64", 0, G) == 0);
    CHECK(ico_state_proc_check("macos-arm64", 0, 0) == -1);
    CHECK(ico_state_proc_check("ios-arm64", 0, 0) == -1);

    CHECK(ico_state_proc_needs("linux-x64") == 0);
    CHECK(ico_state_proc_needs("windows-x64") == 0);
    CHECK(ico_state_proc_needs("android-x64") == 0);
    CHECK(ico_state_proc_needs("unknown") == 0);
    CHECK(ico_state_proc_needs(NULL) == 0);
    CHECK(ico_state_proc_check("linux-x64", 0, 0) == 0);
    CHECK(ico_state_proc_check("windows-x64", 0, 0) == 0);
}

/* --- read --------------------------------------------------------------------- */

static void test_read(void)
{
    IcoStateProc a;
    IcoStateProc b;

    memset(&a, 0xA5, sizeof a);
    ico_state_proc_read(&a);
    ico_state_proc_read(&b);
    CHECK(a.tls_base == b.tls_base && a.stack_guard == b.stack_guard); /* one thread */
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    CHECK(a.tls_base != 0);
#if defined(__ANDROID__)
    CHECK(a.stack_guard != 0);
#else
    CHECK(a.stack_guard == 0);
#endif
#elif defined(__APPLE__)
    CHECK(a.tls_base == 0 && a.stack_guard != 0);
#else
    CHECK(a.tls_base == 0 && a.stack_guard == 0);
#endif
    printf("this host: thread pointer %s, stack guard %s\n", a.tls_base != 0 ? "read" : "none",
           a.stack_guard != 0 ? "read" : "none");
}

int main(void)
{
    IcoFiberImage probe;

    test_words();
    test_scan();
    test_needs();
    test_read();
    /* the save calls exist on the asm backend's x86-64 and arm64 only */
    if (ico_fiber_slot_save(0, &probe) == -1 && strcmp(ico_fiber_backend(), "asm") != 0) {
        printf("no save states on this backend (%s)\n", ico_fiber_backend());
    } else {
        test_fiber();
    }
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
