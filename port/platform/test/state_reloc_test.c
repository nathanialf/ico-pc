/*
 * port/platform/test/state_reloc_test.c
 *
 * Save states' relocation (state_reloc.h):
 *
 *   scan    a synthetic region holding pointers into each of the four
 *           ranges (first byte, middle, the end address itself), near
 *           misses one byte outside each end, null, floats, doubles, EE
 *           word pairs and all-ones: every pointer moved by its range's
 *           delta and nothing else, the count returned and added to the
 *           counter; an unaligned region scans only the whole aligned
 *           words inside it; an empty range and a delta-0 range move
 *           nothing; the one ambiguous EE pair (an int 60 above a small
 *           int reads as an arena address) is moved when the arena moved
 *           and left alone when it is pinned (delta 0)
 *   image   the program's image range holds this code (in its text), its
 *           data and its string literals, and not the heap or the stack
 *   heap    the game's allocator (fumi/ios/memory.c, compiled as the game
 *           compiles it) builds a root partition, a child partition and
 *           blocks in use, freed and aligned inside a 1 MB window of the
 *           arena; the typed walk accepts it; a copy at another address
 *           fails the walk until the scan relocates it, then walks to the
 *           same counts; a stamped-over mark, a link left unrelocated and
 *           a chain that goes backwards are each refused
 *   check   the scheduler's threads and the kernel's handlers against the
 *           image: accepted with this program's functions, refused with a
 *           data address as a thread's entry or as a handler, refused with
 *           no image
 *   hash    the masked hash (state_hash.h): with nothing named it is the
 *           plain XXH3-64 of the regions' XXH3-64s; two copies of the
 *           game's memory that differ only in a typed host pointer, a typed
 *           host id and words pointing into two different noted host
 *           blocks hash alike, and are counted (pointer words, host ids);
 *           a word one past a block's end plus one, an unaligned word and
 *           a difference anywhere else still count; a field reaching
 *           past the region's end is clipped to it; the mask callback lists
 *           each range with its kind in order; the block table widens an
 *           overlapping or touching block, ignores a null one and, full,
 *           widens the nearest
 *   misses  another build's words without a translation by place
 *           (IcoStateMiss): the dead/live split of a saved stack
 *           (ico_state_stack_dead_bytes) at every boundary; a scan of a
 *           synthetic arena and game data with translated words and words
 *           without one: each miss live, the first named with its region
 *           and offset, the verdict refusing word for word; a stack region
 *           with dead frames: dead below sp - 128, live from it, the first
 *           live one named from the stack pointer; a word read from a copy
 *           counted by its own place (the scheduler's and the registers'
 *           words); a number kept as it is (an identity point) is no
 *           miss; a same-build scan (no translation) moves the words by
 *           the delta and counts nothing
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <eekernel.h>

#include "memory.h"

#include "../arena.h"
#include "../kernel_host.h"
#include "../sched.h"
#include "../state_hash.h"
#include "../state_reloc.h"
#include "xxh3.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* What the rest of the game would define. */
void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);
void *iosMallocAlignDebug(IosMemPart *part, int size, int align, const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("FAIL debug_assertMessage %s:%d: %s\n", file, line, mes);
    fails++;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    fails++;
}

/* --- the scan ----------------------------------------------------------------- */

#define MOD_LO 0x400000ull
#define MOD_HI 0x800000ull
#define MOD_DELTA 0x555555000000ll
#define ARENA_LO 0x3C00000000ull
#define ARENA_HI (0x3C00000000ull + 0x2800000ull)
#define ARENA_DELTA (-0x1000000000ll)
#define POOL_LO 0x3C10000000ull
#define POOL_HI (0x3C10000000ull + 0x5000000ull)
#define POOL_DELTA 0x10000000000ll
#define POBJ_LO 0x7F1234560000ull
#define POBJ_HI (0x7F1234560000ull + 0x10000ull)
#define POBJ_DELTA (-0x100000ll)

static void set_range(IcoStateReloc *r, int which, uint64_t lo, uint64_t hi, int64_t delta)
{
    r->range[which].lo = lo;
    r->range[which].hi = hi;
    r->range[which].delta = delta;
}

static void four_ranges(IcoStateReloc *r)
{
    memset(r, 0, sizeof *r);
    set_range(r, ICO_RELOC_MODULE, MOD_LO, MOD_HI, MOD_DELTA);
    set_range(r, ICO_RELOC_ARENA, ARENA_LO, ARENA_HI, ARENA_DELTA);
    set_range(r, ICO_RELOC_POOL, POOL_LO, POOL_HI, POOL_DELTA);
    set_range(r, ICO_RELOC_POBJ, POBJ_LO, POBJ_HI, POBJ_DELTA);
}

static uint64_t bits_f2(float a, float b)
{
    uint32_t x, y;

    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return (uint64_t)y << 32 | x;
}

static uint64_t bits_d(double d)
{
    uint64_t x;

    memcpy(&x, &d, 8);
    return x;
}

/* two 32-bit EE words as they lie in memory, lo first */
static uint64_t ee_pair(uint32_t lo, uint32_t hi)
{
    return (uint64_t)hi << 32 | lo;
}

static void test_scan(void)
{
    struct {
        uint64_t in;
        int64_t delta; /* the expected move, 0 for none */
    } cases[] = {
        {MOD_LO, MOD_DELTA},
        {MOD_LO + 0x1234, MOD_DELTA},
        {MOD_HI, MOD_DELTA}, /* the end address itself: one past the end */
        {MOD_LO - 1, 0},
        {MOD_HI + 1, 0},
        {ARENA_LO, ARENA_DELTA},
        {ARENA_LO + 0x760000, ARENA_DELTA},
        {ARENA_HI, ARENA_DELTA},
        {ARENA_LO - 1, 0},
        {ARENA_HI + 1, 0},
        {POOL_LO, POOL_DELTA},
        {POOL_LO + 0x40000 * 7 + 0x3F0, POOL_DELTA},
        {POOL_HI, POOL_DELTA},
        {POOL_LO - 1, 0},
        {POOL_HI + 1, 0},
        {POBJ_LO + 0x10, POBJ_DELTA},
        {POBJ_HI, POBJ_DELTA},
        {POBJ_LO - 1, 0},
        {POBJ_HI + 1, 0},
        {0, 0},
        {1, 0},
        {~0ull, 0},
        {0x8000000000000000ull, 0},
        {bits_f2(1.0f, 1.0f), 0},
        {bits_f2(-0.5f, 1024.0f), 0},
        {bits_f2(0.0f, 1.0e-30f), 0},
        {bits_d(1.0), 0},
        {bits_d(-2.5), 0},
        {bits_d(6.02e23), 0},
        {ee_pair(0x00100000u, 0x01F00000u), 0}, /* two EE addresses */
        {ee_pair(0x00760000u, 0x00000001u), 0},
        {ee_pair(0xFFFFFFFFu, 0x0000003Bu), 0},           /* -1 under 59: below the arena */
        {ee_pair(0x00000003u, 0x0000003Cu), ARENA_DELTA}, /* 3 under 60: reads as the arena */
    };

    enum { N = sizeof cases / sizeof cases[0] };

    static uint64_t buf[N + 8];
    IcoStateReloc r;
    uint64_t counter = 5;
    size_t moved;
    size_t want = 0;
    int i;

    four_ranges(&r);
    for (i = 0; i < N; i++) {
        buf[i] = cases[i].in;
        want += cases[i].delta != 0;
    }
    moved = ico_state_reloc_scan(&r, buf, N * sizeof buf[0], &counter);
    CHECK(moved == want);
    CHECK(counter == 5 + want);
    for (i = 0; i < N; i++) {
        const uint64_t expect = cases[i].in + (uint64_t)cases[i].delta;

        if (buf[i] != expect) {
            printf("FAIL scan case %d: %#llx -> %#llx, want %#llx\n", i,
                   (unsigned long long)cases[i].in, (unsigned long long)buf[i],
                   (unsigned long long)expect);
            fails++;
        }
    }
    /* a second scan moves the moved words again where they land in a range
       (none of these do): the caller scans each region once */
    counter = 0;
    moved = ico_state_reloc_scan(&r, buf, N * sizeof buf[0], &counter);
    CHECK(moved == 0 && counter == 0);

    /* an unaligned region: only the whole aligned words inside it */
    for (i = 0; i < 5; i++) {
        buf[i] = ARENA_LO;
    }
    moved = ico_state_reloc_scan(&r, (char *)buf + 4, 32, NULL);
    CHECK(moved == 3);
    CHECK(buf[0] == ARENA_LO && buf[4] == ARENA_LO);
    CHECK(buf[1] == ARENA_LO + (uint64_t)ARENA_DELTA && buf[3] == ARENA_LO + (uint64_t)ARENA_DELTA);
    /* nothing to scan */
    CHECK(ico_state_reloc_scan(&r, buf, 0, NULL) == 0);
    CHECK(ico_state_reloc_scan(&r, buf, 7, NULL) == 0);
    CHECK(ico_state_reloc_scan(&r, NULL, 64, NULL) == 0);

    /* the pinned arena (delta 0) leaves the ambiguous pair and every arena
       pointer alone and counts nothing */
    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_ARENA, ARENA_LO, ARENA_HI, 0);
    buf[0] = ee_pair(3, 0x3C);
    buf[1] = ARENA_LO + 0x100;
    counter = 0;
    CHECK(ico_state_reloc_scan(&r, buf, 16, &counter) == 0 && counter == 0);
    CHECK(buf[0] == ee_pair(3, 0x3C) && buf[1] == ARENA_LO + 0x100);
    /* an empty range moves nothing, its address included */
    set_range(&r, ICO_RELOC_POBJ, POBJ_LO, POBJ_LO, POBJ_DELTA);
    buf[0] = POBJ_LO;
    CHECK(ico_state_reloc_scan(&r, buf, 8, NULL) == 0 && buf[0] == POBJ_LO);

    /* the developer log: one line per moved word (on stdout's neighbour) */
    four_ranges(&r);
    buf[0] = MOD_LO;
    ico_state_reloc_set_log(1);
    CHECK(ico_state_reloc_scan(&r, buf, 8, NULL) == 1);
    ico_state_reloc_set_log(0);
}

/* --- what the scan may move (issue 218) ----------------------------------------- */

/* The reporter's iOS state: the image saved at 0x10027c000-0x102f6c000 (just
   above the 4 GB __PAGEZERO, upper half 1), loaded in a later process with
   the image 0x2964000 higher and the arena and pool elsewhere. */
#define IOS_MOD_LO 0x10027c000ull
#define IOS_MOD_HI 0x102f6c000ull
#define IOS_MOD_DELTA 0x2964000ll
#define IOS_ARENA_LO 0xd2d400000ull
#define IOS_ARENA_HI (0xd2d400000ull + 0x2800000ull)
#define IOS_POOL_LO 0xd2fcb4000ull
#define IOS_POOL_HI 0xd34cb4000ull
/* the macOS program at its new base (CMakeLists.txt ICO_APPLE_TEXT_BASE) */
#define MAC_MOD_LO 0x2A1D60000000ull
#define MAC_MOD_HI (0x2A1D60000000ull + 0x2e24000ull)

static void test_ambiguous(void)
{
    /* a synthetic arena: a ClusterPoly table {run, matrix, 0, 0} whose
       fourth entry is {0x2640e60, 1} (the reporter's arena+0x263fe20), a
       keyframe's {X, 1} half, a bga record's {word, 1}, and two real program
       pointers among them */
    static uint64_t arena[32];
    const uint64_t real_fn = IOS_MOD_LO + 0x1234560u;
    const uint64_t real_data = IOS_MOD_LO + 0x2a00000u;
    static const uint32_t polys[5][2] = {{0x2640b60u, 0x16u},
                                         {0x2640be0u, 0x27u},
                                         {0x2640d20u, 0x2bu},
                                         {0x2640e60u, 0x1u},
                                         {0x2640f70u, 0x10u}};
    uint64_t before[32];
    IcoStateReloc r;
    IcoStateXlate xl;
    int i;

    memset(arena, 0, sizeof arena);
    for (i = 0; i < 5; i++) {
        arena[i * 2] = ee_pair(polys[i][0], polys[i][1]); /* {run, matrix}, then {0, 0} */
    }
    arena[12] = ee_pair(0x00f12da0u, 1u); /* a bga record {word, 1} */
    arena[13] = real_fn;
    arena[14] = ee_pair(0x01e0aa10u, 1u); /* a keyframe's {X, Y} with Y 1 */
    arena[15] = real_data;
    memcpy(before, arena, sizeof before);
    CHECK(ee_pair(0x2640e60u, 1u) == 0x0000000102640e60ull && arena[6] == 0x0000000102640e60ull);

    /* a later process: the image moved, upper half 1: the scan may not
       move it, and the words a scan would take for addresses are counted */
    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_MODULE, IOS_MOD_LO, IOS_MOD_HI, IOS_MOD_DELTA);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_MODULE);
    /* the pairs {0x2640e60, 1}, {0xf12da0, 1}, {0x1e0aa10, 1} and the two
       real pointers: 5 (the pairs with 0x16, 0x27, 0x2b, 0x10 above are not
       in the range) */
    CHECK(ico_state_reloc_count(&r, ICO_RELOC_MODULE, arena, sizeof before) == 5);
    /* the caller moves nothing (state_host.c perform_load returns to the
       checkpoint's way before its restore): the arena is the saved one */
    CHECK(memcmp(arena, before, sizeof before) == 0);
    /* what the scan would have done (issue 218's crash): the run moved by
       the module delta, an EE offset 0x4fa4e60 past a 40 MB arena */
    {
        uint64_t w = arena[6];

        CHECK(ico_state_reloc_word(&r, &w) == 1 && (uint32_t)w == 0x4fa4e60u);
    }

    /* the same process (every delta 0): nothing to judge, nothing moves */
    set_range(&r, ICO_RELOC_MODULE, IOS_MOD_LO, IOS_MOD_HI, 0);
    set_range(&r, ICO_RELOC_ARENA, IOS_ARENA_LO, IOS_ARENA_HI, 0);
    set_range(&r, ICO_RELOC_POOL, IOS_POOL_LO, IOS_POOL_HI, 0);
    CHECK(ico_state_reloc_ambiguous(&r) == -1);
    CHECK(ico_state_reloc_scan(&r, arena, sizeof before, NULL) == 0);
    CHECK(memcmp(arena, before, sizeof before) == 0);

    /* the image in place but the arena (upper half 0xd) or the pool moved:
       the same judgement, the first range named */
    set_range(&r, ICO_RELOC_ARENA, IOS_ARENA_LO, IOS_ARENA_HI, -0x2c5f00000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_ARENA);
    set_range(&r, ICO_RELOC_ARENA, IOS_ARENA_LO, IOS_ARENA_HI, 0);
    set_range(&r, ICO_RELOC_POOL, IOS_POOL_LO, IOS_POOL_HI, -0x2c5f84000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_POOL);
    set_range(&r, ICO_RELOC_MODULE, IOS_MOD_LO, IOS_MOD_HI, IOS_MOD_DELTA);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_MODULE);
    /* the model tables are not judged */
    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_POBJ, 0x2c0000000ull, 0x2c0010000ull, 0x100000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == -1);
    /* another build's image goes by its translation, not a delta: the
       image is not judged, a moved arena still is */
    memset(&r, 0, sizeof r);
    memset(&xl, 0, sizeof xl);
    set_range(&r, ICO_RELOC_MODULE, 0x140000000ull, 0x1430ef800ull, 0x1000ll);
    r.xlate = &xl;
    CHECK(ico_state_reloc_ambiguous(&r) == -1);
    set_range(&r, ICO_RELOC_ARENA, ARENA_LO, ARENA_HI, ARENA_DELTA);
    CHECK(ico_state_reloc_ambiguous(&r) == -1); /* 0x3C: at the bound's right side */
    set_range(&r, ICO_RELOC_ARENA, 0xF00000000ull, 0xF02800000ull, 0x1000000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_ARENA);
    /* the bound itself: 0x10 may move, 0xf may not */
    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_MODULE, 0x1000000000ull, 0x1002000000ull, 0x4000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == -1);
    set_range(&r, ICO_RELOC_MODULE, 0xFFE000000ull, 0x1000000000ull, 0x4000ll);
    CHECK(ico_state_reloc_ambiguous(&r) == ICO_RELOC_MODULE);

    /* the macOS image at its new base, moved by a slide: the scan moves the
       real pointers and leaves every pair, which cannot reach the range */
    memcpy(arena, before, sizeof before);
    arena[13] = MAC_MOD_LO + 0x1234560u;
    arena[15] = MAC_MOD_LO + 0x2a00000u;
    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_MODULE, MAC_MOD_LO, MAC_MOD_HI, IOS_MOD_DELTA);
    CHECK(ico_state_reloc_ambiguous(&r) == -1);
    CHECK(ico_state_reloc_count(&r, ICO_RELOC_MODULE, arena, sizeof before) == 2);
    CHECK(ico_state_reloc_scan(&r, arena, sizeof before, NULL) == 2);
    CHECK(arena[13] == MAC_MOD_LO + 0x1234560u + (uint64_t)IOS_MOD_DELTA &&
          arena[15] == MAC_MOD_LO + 0x2a00000u + (uint64_t)IOS_MOD_DELTA);
    CHECK(arena[6] == 0x0000000102640e60ull && arena[12] == ee_pair(0x00f12da0u, 1u) &&
          arena[14] == ee_pair(0x01e0aa10u, 1u));
    /* counting: an empty range, an index outside, no region */
    CHECK(ico_state_reloc_count(&r, ICO_RELOC_POBJ, arena, sizeof before) == 0);
    CHECK(ico_state_reloc_count(&r, ICO_RELOC_RANGES, arena, sizeof before) == 0);
    CHECK(ico_state_reloc_count(&r, -1, arena, sizeof before) == 0);
    CHECK(ico_state_reloc_count(&r, ICO_RELOC_MODULE, NULL, 64) == 0);
}

/* Another build's Windows state (image 0x140000000, upper half 1): the
   words inside the old code at no function's start are numbers (issue 218:
   a keyframe's {X, 1} with X from 2.0 to about 142 lands in .text) */
static void test_code_numbers(void)
{
    /* the old build's translation: three functions and the game's bss as
       spans, one start-up value as a point */
    static const IcoStateSpan spans[] = {
        {0x140001000ull, 0x1400010ffull, 0x140002000ull},
        {0x140498000ull, 0x140499fffull, 0x14049a000ull},
        {0x1405f8000ull, 0x1405f8fffull, 0x1405fa000ull},
        {0x140a00000ull, 0x140d1cbefull, 0x140a01000ull},
    };
    static const IcoStateSpan points[] = {{0x140600010ull, 0x140600010ull, 0x140601010ull}};
    const uint64_t text_lo = 0x140001000ull, text_hi = 0x1406d9000ull;
    static uint64_t data[16], data2[4], stack[4];
    uint64_t out[8], v;
    IcoStateSpan merged[4];
    IcoStateXlate x;
    size_t k = 0;

    memset(&x, 0, sizeof x);
    x.span = spans;
    x.n_span = sizeof spans / sizeof spans[0];
    x.point = points;
    x.n_point = 1;
    memset(data, 0, sizeof data);
    data[0] = ee_pair(0x40498c94u, 1u); /* {3.1492..., 1} (a real state's keyframe) */
    data[1] = 0x140498000ull;           /* a function's address */
    data[2] = 0x140600010ull;           /* a point */
    data[3] = 0x1405f82feull;           /* {3.4923..., 1}, twice: one value */
    data[4] = 0x1405f82feull;
    data[5] = 0x140a00000ull; /* {5.0, 1}: in the game's bss, outside the code */
    data[6] = 0x1406d9000ull; /* the code's end: not inside */
    data[7] = 0x140001000ull; /* the code's first function */
    CHECK(data[0] == 0x140498c94ull);
    CHECK(ico_state_code_numbers(&x, text_lo, text_hi, data, sizeof data, out, 8, &k) == 2);
    CHECK(k == 2 && out[0] == 0x140498c94ull && out[1] == 0x1405f82feull);
    /* a second region adds to the one sorted list */
    data2[0] = 0x140498c94ull;
    data2[1] = 0x140100000ull;
    CHECK(ico_state_code_numbers(&x, text_lo, text_hi, data2, sizeof data2, out, 8, &k) == 1);
    CHECK(k == 3 && out[0] == 0x140100000ull && out[1] == 0x140498c94ull &&
          out[2] == 0x1405f82feull);
    /* a full list: the words past it are counted (a repeat again: it is
       not in the list), not written; the caller sees more found than kept */
    k = 0;
    CHECK(ico_state_code_numbers(&x, text_lo, text_hi, data, sizeof data, out, 1, &k) == 3);
    CHECK(k == 1 && out[0] == 0x140498c94ull);
    /* nothing to look at */
    k = 0;
    CHECK(ico_state_code_numbers(NULL, text_lo, text_hi, data, sizeof data, out, 8, &k) == 0);
    CHECK(ico_state_code_numbers(&x, text_lo, text_lo, data, sizeof data, out, 8, &k) == 0);
    CHECK(ico_state_code_numbers(&x, text_lo, text_hi, NULL, 64, out, 8, &k) == 0 && k == 0);

    /* a kept number that a thread's stack holds too (a return address
       there, perhaps): the load does not keep it */
    memset(stack, 0, sizeof stack);
    stack[2] = 0x1405f82feull;
    {
        const uint64_t one[1] = {0x140498c94ull};
        uint64_t which = 0;

        /* out[] holds 0x140100000, 0x140498c94, 0x1405f82fe (sorted) after
           the full-list call above wrote only its first: rebuild it */
        out[0] = 0x140100000ull;
        out[1] = 0x140498c94ull;
        out[2] = 0x1405f82feull;
        CHECK(ico_state_region_holds(stack, sizeof stack, out, 3, &which) == 1 &&
              which == 0x1405f82feull);
        CHECK(ico_state_region_holds(stack, sizeof stack, one, 1, &which) == 0);
        CHECK(ico_state_region_holds(stack, sizeof stack, out, 0, NULL) == 0);
        CHECK(ico_state_region_holds(NULL, 32, out, 3, NULL) == 0);
        stack[0] = 0x140100000ull; /* the list's first value, found by its bounds */
        CHECK(ico_state_region_holds(stack, sizeof stack, out, 3, NULL) == 1);
    }

    /* the translation with a number as an identity point (state_host.c
       keep_code_numbers' merge): the number stays, the function's address
       and the point move */
    merged[0].lo = merged[0].hi = merged[0].now = 0x140498c94ull;
    merged[1] = points[0];
    x.point = merged;
    x.n_point = 2;
    CHECK(ico_state_xlate(&x, 0x140498c94ull, &v) == 1 && v == 0x140498c94ull);
    CHECK(ico_state_xlate(&x, 0x140498000ull, &v) == 1 && v == 0x14049a000ull);
    CHECK(ico_state_xlate(&x, 0x140600010ull, &v) == 1 && v == 0x140601010ull);
    /* without it the number moves with its function (the corruption) */
    x.point = points;
    x.n_point = 1;
    CHECK(ico_state_xlate(&x, 0x140498c94ull, &v) == 1 && v == 0x14049ac94ull);

    /* the verdict the load and the Save states page's listing share
       (ico_state_move_check), one row per outcome */
    {
        const void *regions[2] = {data, data2};
        const size_t regions_n[2] = {sizeof data, sizeof data2};
        const void *threads[1] = {stack};
        const size_t threads_n[1] = {sizeof stack};
        IcoStateReloc r;
        IcoStateMoveIn in;
        uint64_t held = 0;

        memset(&r, 0, sizeof r);
        memset(&in, 0, sizeof in);
        in.rel = &r;
        in.saved_image_lo = 0x140000000ull;
        in.text_lo = text_lo;
        in.text_hi = text_hi;
        in.data = regions;
        in.data_n = regions_n;
        in.n_data = 2;
        in.held = threads;
        in.held_n = threads_n;
        in.n_held = 1;
        /* an iOS state after a restart (this build: no translation): the
           image the scan may not move */
        set_range(&r, ICO_RELOC_MODULE, IOS_MOD_LO, IOS_MOD_HI, IOS_MOD_DELTA);
        in.saved_image_lo = IOS_MOD_LO;
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_RELOC_MODULE && k == 0);
        /* the same state in its own run: OK, nothing kept */
        set_range(&r, ICO_RELOC_MODULE, IOS_MOD_LO, IOS_MOD_HI, 0);
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_STATE_MOVE_OK && k == 0);
        /* another build's Windows state: its numbers inside the code, two of
           them on a thread's stack (stack[0] 0x140100000, stack[2]
           0x1405f82fe): the checkpoint, the first found named */
        memset(&r, 0, sizeof r);
        set_range(&r, ICO_RELOC_MODULE, 0x140000000ull, 0x1430ef800ull, 0);
        r.xlate = &x;
        in.saved_image_lo = 0x140000000ull;
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_STATE_MOVE_HELD &&
              held == 0x140100000ull && k == 3);
        /* no thread holds them: OK, the three kept */
        in.n_held = 0;
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_STATE_MOVE_OK && k == 3 &&
              out[0] == 0x140100000ull && out[1] == 0x140498c94ull && out[2] == 0x1405f82feull);
        /* more than the list holds: the checkpoint */
        CHECK(ico_state_move_check(&in, out, 2, &k, &held) == ICO_STATE_MOVE_TOO_MANY);
        /* an image above the bound (Android's): no numbers looked for */
        in.saved_image_lo = 0x7768720000ull;
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_STATE_MOVE_OK && k == 0);
        /* another build's state whose arena moved below the bound */
        set_range(&r, ICO_RELOC_ARENA, IOS_ARENA_LO, IOS_ARENA_HI, -0x2c5f00000ll);
        CHECK(ico_state_move_check(&in, out, 8, &k, &held) == ICO_RELOC_ARENA);
    }
}

/* --- the image ---------------------------------------------------------------- */

static int image_data = 42;

static void test_image(void)
{
    IcoStateImage im;
    const uint64_t code = (uint64_t)(uintptr_t)test_image;
    const uint64_t data = (uint64_t)(uintptr_t)&image_data;
    const uint64_t text = (uint64_t)(uintptr_t)"a string literal";
    void *big = malloc(4u << 20); /* mapped apart from the image */
    int local = 0;

    CHECK(ico_state_image_range(&im) == 0);
    CHECK(im.lo <= im.text_lo && im.text_lo < im.text_hi && im.text_hi <= im.hi);
    CHECK(code >= im.text_lo && code < im.text_hi);
    CHECK(data >= im.lo && data < im.hi);
    CHECK(!(data >= im.text_lo && data < im.text_hi));
    CHECK(text >= im.lo && text < im.hi);
    CHECK(big != NULL && !((uint64_t)(uintptr_t)big >= im.lo && (uint64_t)(uintptr_t)big < im.hi));
    CHECK(!((uint64_t)(uintptr_t)&local >= im.lo && (uint64_t)(uintptr_t)&local < im.hi));
    free(big);
}

/* --- the heap ----------------------------------------------------------------- */

#define WINDOW_EE 0x100000u
#define WINDOW_BYTES 0x100000u

static IosMemPart *root;
static IosMemPart *child;
static uint64_t win_lo, win_hi;

static void build_heap(void)
{
    void *a, *b, *c, *d, *e;

    CHECK(ico_arena_init() == 0);
    win_lo = (uint64_t)ico_arena_ee_addr(WINDOW_EE);
    win_hi = win_lo + WINDOW_BYTES;
    memset((void *)(uintptr_t)win_lo, 0, WINDOW_BYTES);
    root = iosMallocInitPartition((IosMemAddr)win_lo, (IosMemAddr)(win_hi - 16));
    CHECK(root != NULL);
    child = iosMallocSetPartition(root, 128 * 1024, 16);
    CHECK(child != NULL);
    a = iosMallocDebug(root, 1000, __FILE__, __LINE__);
    b = iosMallocDebug(child, 5000, __FILE__, __LINE__);
    c = iosMallocDebug(root, 64, __FILE__, __LINE__);
    d = iosMallocAlignDebug(root, 300, 256, __FILE__, __LINE__);
    e = iosMallocDebug(child, 48, __FILE__, __LINE__);
    CHECK(a != NULL && b != NULL && c != NULL && d != NULL && e != NULL);
    iosFree(a); /* a free block between two in use */
    iosFree(e);
}

/* the 16-byte aligned copy of the window */
static unsigned char *copy_raw;
static uint64_t copy_lo, copy_hi;

static void *moved(const void *p)
{
    return (void *)(uintptr_t)((uint64_t)(uintptr_t)p - win_lo + copy_lo);
}

static void test_heap(void)
{
    unsigned parts = 0, nodes = 0, parts2 = 0, nodes2 = 0;
    char why[160];
    IcoStateReloc r;
    IosMemNode *first;
    IosMemNode saved;
    size_t n;

    build_heap();
    CHECK(ico_state_heap_walk(root, win_lo, win_hi, &parts, &nodes, why, sizeof why) == 0);
    CHECK(parts == 2);
    CHECK(nodes >= 6);
    if (why[0] != '\0') {
        printf("walk: %s\n", why);
    }

    copy_raw = (unsigned char *)malloc(WINDOW_BYTES + 16);
    CHECK(copy_raw != NULL);
    if (copy_raw == NULL) {
        return;
    }
    copy_lo = ((uint64_t)(uintptr_t)copy_raw + 15u) & ~(uint64_t)15u;
    copy_hi = copy_lo + WINDOW_BYTES;
    memcpy((void *)(uintptr_t)copy_lo, (const void *)(uintptr_t)win_lo, WINDOW_BYTES);

    /* not relocated: the links still point into the original window */
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, NULL, NULL, why, sizeof why) != 0);
    CHECK(why[0] != '\0');

    memset(&r, 0, sizeof r);
    set_range(&r, ICO_RELOC_ARENA, win_lo, win_hi, (int64_t)(copy_lo - win_lo));
    n = ico_state_reloc_scan(&r, (void *)(uintptr_t)copy_lo, WINDOW_BYTES, NULL);
    CHECK(n > 0);
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, &parts2, &nodes2, why, sizeof why) ==
          0);
    CHECK(parts2 == parts && nodes2 == nodes);
    /* the original is untouched */
    CHECK(ico_state_heap_walk(root, win_lo, win_hi, NULL, NULL, why, sizeof why) == 0);

    /* a block whose mark was written over */
    first = (IosMemNode *)moved(root->start);
    saved = *first;
    memcpy(first->tag, "not a mark here", 16);
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, NULL, NULL, why, sizeof why) != 0);
    CHECK(strstr(why, "mark") != NULL);
    *first = saved;
    /* a link the relocation missed */
    ((IosMemPart *)moved(child))->parent = root;
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, NULL, NULL, why, sizeof why) != 0);
    ((IosMemPart *)moved(child))->parent = (IosMemPart *)moved(root);
    /* a chain that goes backwards */
    if (first->next != NULL) {
        IosMemNode *second = first->next;
        IosMemNode *keep = second->next;

        second->next = first;
        CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, NULL, NULL, why, sizeof why) != 0);
        CHECK(strstr(why, "backwards") != NULL);
        second->next = keep;
    }
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_hi, NULL, NULL, why, sizeof why) == 0);
    /* nothing to walk */
    CHECK(ico_state_heap_walk(NULL, copy_lo, copy_hi, NULL, NULL, why, sizeof why) != 0);
    CHECK(ico_state_heap_walk(moved(root), copy_lo, copy_lo, NULL, NULL, why, sizeof why) != 0);
    free(copy_raw);
}

/* --- the threads and handlers ---------------------------------------------------- */

static int check_data[4];

static void thread_fn(void *arg)
{
    (void)arg;
}

static int handler_fn(int cause)
{
    return cause;
}

static void test_check(void)
{
    static unsigned char stack[4096];
    IcoStateCheck c;
    int id, bad, h;

    ico_sched_reset();
    ico_kernel_reset();
    id = ico_sched_create_thread(thread_fn, stack, sizeof stack, NULL, 10, 0, 0);
    CHECK(id > 0);
    h = AddIntcHandler(2, handler_fn, 0);
    CHECK(h > 0);
    memset(&c, 0, sizeof c);
    c.heap_root = root;
    c.arena_lo = win_lo;
    c.arena_hi = win_hi;
    CHECK(ico_state_image_range(&c.image) == 0);
    CHECK(ico_state_reloc_check(&c) == 0);
    if (c.why[0] != '\0') {
        printf("check: %s\n", c.why);
    }
    CHECK(c.threads == 1 && c.handlers == 1 && c.partitions == 2 && c.nodes > 0);

    /* a thread whose entry is data */
    bad = ico_sched_create_thread((void (*)(void *))(uintptr_t)&check_data[0], stack, sizeof stack,
                                  NULL, 10, 0, 0);
    CHECK(bad > 0);
    CHECK(ico_state_reloc_check(&c) != 0);
    CHECK(strstr(c.why, "entry") != NULL);
    CHECK(ico_sched_delete_thread(bad) == bad);
    CHECK(ico_state_reloc_check(&c) == 0);

    /* a handler that is data */
    bad = AddDmacHandler(3, (int (*)(int))(uintptr_t)&check_data[1], 0);
    CHECK(bad > 0);
    CHECK(ico_state_reloc_check(&c) != 0);
    CHECK(strstr(c.why, "handler") != NULL);
    CHECK(RemoveDmacHandler(3, bad) == 0);
    CHECK(ico_state_reloc_check(&c) == 0 && c.handlers == 1);

    /* no heap: the walk is skipped */
    c.heap_root = NULL;
    CHECK(ico_state_reloc_check(&c) == 0 && c.partitions == 0);
    /* no image */
    memset(&c.image, 0, sizeof c.image);
    CHECK(ico_state_reloc_check(&c) != 0);
    ico_sched_reset();
    ico_kernel_reset();
}

/* --- the masked hash --------------------------------------------------------- */

typedef struct MaskLog {
    int n;
    int region[16], kind[16];
    size_t off[16], bytes[16];
} MaskLog;

static void mask_log(int region, size_t off, size_t n, int kind, void *user)
{
    MaskLog *m = (MaskLog *)user;
    if (m->n < 16) {
        m->region[m->n] = region;
        m->off[m->n] = off;
        m->bytes[m->n] = n;
        m->kind[m->n] = kind;
    }
    m->n++;
}

static void put64(unsigned char *p, uint64_t w)
{
    memcpy(p, &w, sizeof w);
}

static void test_hash(void)
{
    /* two copies of a "game memory" of 256 bytes (8-aligned) and a small
       second region; host blocks at made-up addresses */
    static uint64_t a_words[32], b_words[32];
    static uint64_t a2_words[4], b2_words[4];
    unsigned char *a = (unsigned char *)a_words;
    unsigned char *b = (unsigned char *)b_words;
    IcoStateHashRegion ra[2], rb[2];
    IcoStateHashField fa[2], fb[2];
    IcoStateHashBlocks blocks;
    IcoStateHashResult x, y;
    MaskLog log;
    uint64_t want[2];
    size_t i;

    for (i = 0; i < 256; i++) {
        a[i] = b[i] = (unsigned char)(i * 13 + 1);
    }
    for (i = 0; i < 4; i++) {
        a2_words[i] = b2_words[i] = 0x1111111111111111ull * (i + 1);
    }
    ra[0].name = rb[0].name = "arena";
    ra[0].p = a;
    rb[0].p = b;
    ra[0].n = rb[0].n = 256;
    ra[1].name = rb[1].name = "gbss";
    ra[1].p = a2_words;
    rb[1].p = b2_words;
    ra[1].n = rb[1].n = sizeof a2_words;

    /* nothing named: the plain hash, the same for equal copies */
    CHECK(ico_state_hash_regions(ra, 2, NULL, 0, NULL, NULL, NULL, &x) == 0);
    want[0] = xxh3_64(a, 256);
    want[1] = xxh3_64(a2_words, sizeof a2_words);
    CHECK(x.region_hash[0] == want[0] && x.region_hash[1] == want[1]);
    CHECK(x.hash == xxh3_64(want, sizeof want));
    CHECK(x.ptr_words == 0 && x.host_ids == 0);
    CHECK(ico_state_hash_regions(rb, 2, NULL, 0, NULL, NULL, NULL, &y) == 0 && y.hash == x.hash);

    /* a typed pointer at 16 and a typed id (4 bytes) at 40 differ */
    put64(a + 16, 0x00005555deadbe00ull);
    put64(b + 16, 0x00005555feedf000ull);
    a[40] = 1;
    b[40] = 2;
    /* words pointing into each run's own host block, at 64 and in gbss */
    put64(a + 64, 0x0000600000001010ull);
    put64(b + 64, 0x0000700000002020ull);
    a2_words[2] = 0x0000600000001000ull; /* the block's first byte */
    b2_words[2] = 0x0000700000002100ull; /* its end address itself */
    memset(&blocks, 0, sizeof blocks);
    ico_state_hash_note_block(&blocks, 0x0000600000001000ull, 0x100);
    ico_state_hash_note_block(&blocks, 0x0000700000002000ull, 0x100);
    ico_state_hash_note_block(&blocks, 0, 0x100); /* null: ignored */
    CHECK(blocks.n == 2);
    fa[0].p = a + 16;
    fa[0].n = 8;
    fa[0].kind = ICO_STATE_MASK_PTR;
    fa[1].p = a + 40;
    fa[1].n = 4;
    fa[1].kind = ICO_STATE_MASK_ID;
    fb[0] = fa[0];
    fb[0].p = b + 16;
    fb[1] = fa[1];
    fb[1].p = b + 40;

    /* without the masks they differ */
    CHECK(ico_state_hash_regions(ra, 2, NULL, 0, NULL, NULL, NULL, &x) == 0);
    CHECK(ico_state_hash_regions(rb, 2, NULL, 0, NULL, NULL, NULL, &y) == 0);
    CHECK(x.hash != y.hash);
    /* with them, alike, and counted */
    memset(&log, 0, sizeof log);
    CHECK(ico_state_hash_regions(ra, 2, fa, 2, &blocks, mask_log, &log, &x) == 0);
    CHECK(ico_state_hash_regions(rb, 2, fb, 2, &blocks, NULL, NULL, &y) == 0);
    CHECK(x.hash == y.hash);
    CHECK(x.ptr_words == 3 && x.host_ids == 1); /* the typed pointer, two block words */
    CHECK(y.ptr_words == 3 && y.host_ids == 1);
    /* the callback: the typed fields in order, then the block words */
    CHECK(log.n == 4);
    CHECK(log.region[0] == 0 && log.off[0] == 16 && log.bytes[0] == 8 &&
          log.kind[0] == ICO_STATE_MASK_PTR);
    CHECK(log.region[1] == 0 && log.off[1] == 40 && log.bytes[1] == 4 &&
          log.kind[1] == ICO_STATE_MASK_ID);
    CHECK(log.region[2] == 0 && log.off[2] == 64 && log.bytes[2] == 8 &&
          log.kind[2] == ICO_STATE_MASK_BLOCK);
    CHECK(log.region[3] == 1 && log.off[3] == 16 && log.kind[3] == ICO_STATE_MASK_BLOCK);
    /* the live memory is untouched */
    CHECK(a[40] == 1 && a2_words[2] == 0x0000600000001000ull);

    /* one past the end address: a difference that counts */
    put64(b + 64, 0x0000700000002101ull);
    CHECK(ico_state_hash_regions(rb, 2, fb, 2, &blocks, NULL, NULL, &y) == 0);
    CHECK(y.hash != x.hash && y.ptr_words == 2);
    put64(b + 64, 0x0000700000002020ull);
    /* a block address in an unaligned word is not a pointer */
    put64(b + 68, 0x0000700000002020ull);
    CHECK(ico_state_hash_regions(rb, 2, fb, 2, &blocks, NULL, NULL, &y) == 0);
    CHECK(y.hash != x.hash);
    memcpy(b + 64, a + 64, 16);
    put64(b + 64, 0x0000700000002020ull);
    /* any other byte still counts */
    b[200] ^= 0x40;
    CHECK(ico_state_hash_regions(rb, 2, fb, 2, &blocks, NULL, NULL, &y) == 0);
    CHECK(y.hash != x.hash);
    b[200] ^= 0x40;
    CHECK(ico_state_hash_regions(rb, 2, fb, 2, &blocks, NULL, NULL, &y) == 0);
    CHECK(y.hash == x.hash);

    /* a field reaching past the region's end: only its part inside is
       masked */
    fb[0].p = b + 248;
    fb[0].n = 16;
    memset(&log, 0, sizeof log);
    CHECK(ico_state_hash_regions(rb, 1, fb, 1, NULL, mask_log, &log, &y) == 0);
    CHECK(log.n == 1 && log.off[0] == 248 && log.bytes[0] == 8);
    /* too many regions: refused */
    CHECK(ico_state_hash_regions(ra, ICO_STATE_HASH_MAX_REGIONS + 1, NULL, 0, NULL, NULL, NULL,
                                 &x) == -1);

    /* the block table: overlap and touch widen, full widens the nearest */
    memset(&blocks, 0, sizeof blocks);
    ico_state_hash_note_block(&blocks, 0x1000, 0x100);
    ico_state_hash_note_block(&blocks, 0x1080, 0x100); /* overlaps */
    CHECK(blocks.n == 1 && blocks.lo[0] == 0x1000 && blocks.hi[0] == 0x1180);
    ico_state_hash_note_block(&blocks, 0x1180, 0x80); /* touches the end address */
    CHECK(blocks.n == 1 && blocks.hi[0] == 0x1200);
    for (i = 1; i < ICO_STATE_HASH_MAX_BLOCKS; i++) {
        ico_state_hash_note_block(&blocks, 0x100000 * (uint64_t)(i + 1), 0x10);
    }
    CHECK(blocks.n == ICO_STATE_HASH_MAX_BLOCKS);
    ico_state_hash_note_block(&blocks, 0x1400, 0x10); /* nearest: the first */
    CHECK(blocks.n == ICO_STATE_HASH_MAX_BLOCKS && blocks.lo[0] == 0x1000 &&
          blocks.hi[0] == 0x1410);
}

/* --- misses ------------------------------------------------------------------ */

static void test_misses(void)
{
    enum { TR = 0x1000, HI = 0x100000 };

    const uint64_t lo = 0x7700000000ull;
    IcoStateReloc r;
    IcoStateXlate xl;
    IcoStateSpan span;
    IcoStateMiss m;
    uint64_t total = 0, first_total = 0;
    uint64_t arena[8], gdata[4], stk[64], w;
    char text[320];
    size_t moved;
    uintptr_t sbase;
    uint64_t sp;

    /* the split: of n bytes at base with stack pointer sp, the dead part */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0) == 0);             /* no sp */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 100) == 0);           /* sp < 128 */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 128) == 0); /* live == base */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 135) == 0); /* 7 bytes */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 136) == 8); /* one word */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 141) == 8); /* rounded down */
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 0x400) == 0x400 - 128);
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x10000 + 0x400 + 128) == 0x400);
    CHECK(ico_state_stack_dead_bytes(0x10000, 0x400, 0x90000) == 0x400); /* all below */

    /* the old image [lo, lo + HI]; its first TR bytes translate (+0x1000000),
       the rest has no translation */
    memset(&r, 0, sizeof r);
    r.range[ICO_RELOC_MODULE].lo = lo;
    r.range[ICO_RELOC_MODULE].hi = lo + HI;
    span.lo = lo;
    span.hi = lo + TR - 1;
    span.now = lo + 0x1000000;
    memset(&xl, 0, sizeof xl);
    xl.span = &span;
    xl.n_span = 1;
    xl.missed = &total;
    xl.first_missed = &first_total;
    memset(&m, 0, sizeof m);
    xl.miss = &m;
    r.xlate = &xl;

    /* the arena: one word translated, two without, the first at offset 0x18 */
    memset(arena, 0, sizeof arena);
    arena[0] = lo + 0x10;
    arena[3] = lo + 0x2000;
    arena[5] = lo + 0x3000;
    arena[6] = lo + HI + 1; /* outside the image: no program word */
    ico_state_miss_at(&m, "the arena", (uint64_t)(uintptr_t)arena, 0, 0);
    moved = ico_state_reloc_scan(&r, arena, sizeof arena, NULL);
    CHECK(moved == 1 && arena[0] == lo + 0x1000010 && arena[3] == lo + 0x2000);
    CHECK(m.live == 2 && m.dead == 0 && m.first_live == lo + 0x2000 && m.live_off == 0x18);
    CHECK(m.live_where != NULL && strcmp(m.live_where, "the arena") == 0 && m.live_fiber == 0);
    CHECK(total == 2 && first_total == lo + 0x2000);
    CHECK(ico_state_miss_refuses(&m, lo, text, sizeof text) == 1);
    CHECK(strcmp(text, "2 program words in live memory had no translation, the first: the arena "
                       "at offset 0x18 holds 0x7700002000 (offset 0x2000 in the saving build's "
                       "image)") == 0);
    ico_state_miss_counts(&m, text, sizeof text);
    CHECK(strcmp(text, "0 program words in dead stack frames had no translation and were left "
                       "as they were (first 0x0); 2 in live memory") == 0);

    /* the game's data: the first live miss stays the arena's, the count grows */
    memset(gdata, 0, sizeof gdata);
    gdata[1] = lo + 0x4000;
    ico_state_miss_at(&m, "the game's data", (uint64_t)(uintptr_t)gdata, 0, 0);
    (void)ico_state_reloc_scan(&r, gdata, sizeof gdata, NULL);
    CHECK(m.live == 3 && m.live_off == 0x18 && strcmp(m.live_where, "the arena") == 0);
    /* alone, it is named with its own place */
    memset(&m, 0, sizeof m);
    ico_state_miss_at(&m, "the game's data", (uint64_t)(uintptr_t)gdata, 0, 0);
    (void)ico_state_reloc_scan(&r, gdata, sizeof gdata, NULL);
    CHECK(m.live == 1 && m.live_off == 8);
    ico_state_miss_live_text(&m, lo, text, sizeof text);
    CHECK(strcmp(text, "the game's data at offset 0x8 holds 0x7700004000 (offset 0x4000 in the "
                       "saving build's image)") == 0);

    /* a fiber's stack: 512 bytes at sbase, sp 384 bytes up: 256 dead */
    memset(&m, 0, sizeof m);
    memset(stk, 0, sizeof stk);
    sbase = (uintptr_t)stk;
    sp = (uint64_t)sbase + 384;
    stk[0] = lo + 0x5000;  /* the deepest word: dead */
    stk[31] = lo + 0x5008; /* the last below sp - 128: dead */
    stk[32] = lo + 0x5010; /* sp - 128: live (the red zone) */
    stk[50] = lo + 0x5018; /* sp + 16: live */
    stk[40] = lo + 0x20;   /* translated wherever it is */
    ico_state_miss_at(&m, "stack", sp, 6, 1);
    m.dead_lo = (uint64_t)sbase;
    m.dead_hi = m.dead_lo + ico_state_stack_dead_bytes(m.dead_lo, sizeof stk, sp);
    CHECK(m.dead_hi - m.dead_lo == 256);
    moved = ico_state_reloc_scan(&r, stk, sizeof stk, NULL);
    CHECK(moved == 1 && stk[40] == lo + 0x1000020);
    CHECK(m.dead == 2 && m.first_dead == lo + 0x5000);
    CHECK(m.live == 2 && m.first_live == lo + 0x5010 && m.live_off == -128);
    ico_state_miss_live_text(&m, lo, text, sizeof text);
    CHECK(strcmp(text, "fiber slot 5's stack, 128 bytes below its saved stack pointer, holds "
                       "0x7700005010 (offset 0x5010 in the saving build's image)") == 0);
    /* dead only: kept */
    memset(&m, 0, sizeof m);
    stk[32] = 0;
    stk[50] = 0;
    ico_state_miss_at(&m, "stack", sp, 6, 1);
    m.dead_lo = (uint64_t)sbase;
    m.dead_hi = m.dead_lo + ico_state_stack_dead_bytes(m.dead_lo, sizeof stk, sp);
    (void)ico_state_reloc_scan(&r, stk, sizeof stk, NULL);
    CHECK(m.dead == 2 && m.live == 0);
    CHECK(ico_state_miss_refuses(&m, lo, text, sizeof text) == 0 && text[0] == '\0');
    ico_state_miss_counts(&m, text, sizeof text);
    CHECK(strcmp(text, "2 program words in dead stack frames had no translation and were left "
                       "as they were (first 0x7700005000); 0 in live memory") == 0);
    /* a new place clears the dead range: the same words are live there */
    ico_state_miss_at(&m, "the arena", (uint64_t)sbase, 0, 0);
    (void)ico_state_reloc_scan(&r, stk, sizeof stk, NULL);
    CHECK(m.dead == 2 && m.live == 2 && m.live_off == 0);

    /* a word read from a copy (the scheduler's table, the saved
       registers): counted by its own place */
    memset(&m, 0, sizeof m);
    ico_state_miss_at(&m, "saved registers", 0x1000, 3, 0);
    w = lo + 0x6000;
    CHECK(ico_state_reloc_word_at(&r, &w, 0x1018) == 0 && w == lo + 0x6000);
    CHECK(m.live == 1 && m.live_off == 0x18);
    ico_state_miss_live_text(&m, lo, text, sizeof text);
    CHECK(strcmp(text, "fiber slot 2's saved registers at offset 0x18 holds 0x7700006000 "
                       "(offset 0x6000 in the saving build's image)") == 0);
    memset(&m, 0, sizeof m);
    ico_state_miss_at(&m, "the scheduler's table", 0x2000, 0, 0);
    w = lo + 0x6000;
    (void)ico_state_reloc_word_at(&r, &w, 0x2000 + 0x48);
    ico_state_miss_live_text(&m, lo, text, sizeof text);
    CHECK(strcmp(text, "the scheduler's table at offset 0x48 holds 0x7700006000 (offset 0x6000 "
                       "in the saving build's image)") == 0);
    /* a number kept as it is (keep_code_numbers' identity point: a value
       inside the old code at no function's start, next to a small integer)
       translates to itself and is no miss; the word next to it is */
    {
        IcoStateSpan kept;
        uint64_t two[2];

        kept.lo = kept.hi = kept.now = lo + 0x7000;
        xl.point = &kept;
        xl.n_point = 1;
        memset(&m, 0, sizeof m);
        two[0] = lo + 0x7000;
        two[1] = lo + 0x7008;
        ico_state_miss_at(&m, "the arena", (uint64_t)(uintptr_t)two, 0, 0);
        CHECK(ico_state_reloc_scan(&r, two, sizeof two, NULL) == 0);
        CHECK(two[0] == lo + 0x7000 && m.live == 1 && m.first_live == lo + 0x7008 &&
              m.live_off == 8 && m.dead == 0);
        xl.point = NULL;
        xl.n_point = 0;
    }
    /* no miss struct: nothing to count, the word left */
    CHECK(ico_state_miss_refuses(NULL, lo, text, sizeof text) == 0 && text[0] == '\0');

    /* the same build: no translation, the MODULE range moved by its delta */
    memset(&m, 0, sizeof m);
    r.xlate = NULL;
    r.range[ICO_RELOC_MODULE].delta = 0x100;
    memset(arena, 0, sizeof arena);
    arena[3] = lo + 0x2000;
    ico_state_miss_at(&m, "the arena", (uint64_t)(uintptr_t)arena, 0, 0);
    moved = ico_state_reloc_scan(&r, arena, sizeof arena, NULL);
    CHECK(moved == 1 && arena[3] == lo + 0x2100);
    CHECK(m.live == 0 && m.dead == 0);
    CHECK(ico_state_miss_refuses(&m, lo, text, sizeof text) == 0);
}

int main(void)
{
    test_scan();
    test_ambiguous();
    test_code_numbers();
    test_image();
    test_heap();
    test_check();
    test_hash();
    test_misses();
    if (fails != 0) {
        printf("state_reloc_test: %d failure(s)\n", fails);
        return 1;
    }
    printf("state_reloc_test: ok\n");
    return 0;
}
