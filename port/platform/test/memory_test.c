/*
 * port/platform/test/memory_test.c
 *
 * The game's allocator (fumi/ios/memory.c, compiled unchanged with the game's
 * options) over the host's EE RAM arena: iosInitialize's partitions land at
 * the EE's addresses (as offsets from the arena base), and allocation, free,
 * aligned allocation and realloc behave as the algorithm does on the EE.
 * The expected addresses were worked out by hand from memory.c's arithmetic
 * (docs/port/PLATFORM.md, "Heap").
 *
 * memory.c's records hold 32-bit pointers in the EE layout (0x40-byte block
 * headers), which only a 32-bit host reproduces until Phase 2, so on a
 * 64-bit host the test exits 77 (skipped).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memory.h"

#include "../arena.h"

static int fails;
static int asserts;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* The debug layer memory.c calls (common/src/debug*.c in the game). */
void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);
/* memory.c defines it; memory.h does not declare it */
void *iosMallocAlignDebug(IosMemPart *part, int size, int align, const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("debug_assertMessage %s:%d: %s\n", file, line, mes);
    asserts++;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    asserts++;
}

static unsigned int ee(const void *p)
{
    return (unsigned int)((const unsigned char *)p - ico_arena_base());
}

/* The sum of every block in the partition, headers included, in quadwords. */
static int walk_total(IosMemPart *part, int *n_free)
{
    IosMemNode *n;
    int total = 0;
    *n_free = 0;
    for (n = (IosMemNode *)part->start; n != 0; n = n->next) {
        total += n->size + 4;
        if (strcmp(n->tag, "<FREE AREA>____") == 0) {
            (*n_free)++;
        }
    }
    return total;
}

int main(void)
{
    IosMemPart *root;
    IosMemPart *part[11];
    static const int sizes[11] = {4227072, 1179648, 3145728, 262144, 327680, 1,
                                  32768,   20480,   10240,   1,      15826944};
    /* iosInitialize's partitions, carved from the top of the root: */
    static const unsigned int want[11] = {0x1BE7F60, 0x1AC7ED0, 0x17C7E40, 0x1787DB0,
                                          0x1737D20, 0x1737C80, 0x172FBF0, 0x172AB60,
                                          0x17282D0, 0x1728230, 0x08101A0};
    IosMemPart *ev;
    IosMemNode *node;
    char *p1;
    char *p2;
    char *p3;
    char *pa;
    char *pr;
    int i;
    int n_free;

    if (sizeof(void *) != 4) {
        printf("memory.c needs the EE's 32-bit layout; skipped on this host\n");
        return 77;
    }
    CHECK(ico_arena_init() == 0);
    CHECK(((uintptr_t)ico_arena_base() & (ICO_ARENA_ALIGN - 1)) == 0);

    root = iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    CHECK(root != 0 && ee(root) == 0x760000);
    CHECK(ee(root->start) == 0x760050 && ee(root->top) == 0x1FEFFF0);
    CHECK(root->total == 1609722 && root->free == 1609722);
    for (i = 0; i < 11; i++) {
        part[i] = iosMallocSetPartition(root, sizes[i], 16);
        CHECK(part[i] != 0 && ee(part[i]) == want[i]);
    }
    CHECK(root->free == 45077 && root->head->size == 45073);
    iosMallocSetPartitionName(part[3], "event");

    /* allocations in the event partition (16388 quadwords) */
    ev = part[3];
    CHECK(ev->free == 16388 && ev->head->size == 16384);
    p1 = iosMallocDebug(ev, 100, "ios/message.c", 453);
    CHECK(ee(p1) == 0x1787E40);
    node = (IosMemNode *)(p1 - 64);
    CHECK(strcmp(node->tag, "<ALLOC>________") == 0);
    CHECK(strcmp(node->name, "ios/message0453") == 0);
    CHECK(node->size == 7 && node->line == 453 && node->part == ev);
    p2 = iosMallocDebug(ev, 16, "x.c", 1);
    CHECK(ee(p2) == 0x1787EF0);
    CHECK(ico_heap_stats_used(ev) == (11 + 5) * 16);

    /* a freed block is reused best-fit */
    iosFree(p1);
    CHECK(ico_heap_stats_used(ev) == 5 * 16);
    p3 = iosMallocDebug(ev, 32, "y.c", 2);
    CHECK(p3 == p1);

    /* aligned allocation: aligned in EE terms too (the arena is 1 MB aligned) */
    pa = iosMallocAlignDebug(ev, 100, 128, "z.c", 3);
    CHECK(pa != 0 && ((uintptr_t)pa % 128) == 0 && (ee(pa) % 128) == 0);

    /* realloc shrinks in place when the next block is free */
    pr = iosMallocDebug(ev, 4096, "r.c", 4);
    CHECK(iosReallocDebug(pr, 64) == pr);
    CHECK(((IosMemNode *)(pr - 64))->size == 4);

    /* freeing everything leaves the partition whole again */
    iosFree(pa);
    iosFree(p2);
    iosFree(p3);
    iosFree(pr);
    CHECK(walk_total(ev, &n_free) == 16388);
    CHECK(ico_heap_stats_used(ev) == 0);
    CHECK(ico_heap_stats_high_water(ev) > 0);
    printf("event partition: %d free block(s) after freeing everything, head %d qw\n", n_free,
           ev->head->size);
    CHECK(asserts == 0);

    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
