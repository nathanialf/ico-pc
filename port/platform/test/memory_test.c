/*
 * port/platform/test/memory_test.c
 *
 * The game's allocator (fumi/ios/memory.c, compiled unchanged with the game's
 * options) over the host's EE RAM arena: iosInitialize's partitions land at
 * the EE's offsets from the arena base, and allocation, free, aligned
 * allocation and realloc behave as the algorithm does on the EE.
 *
 * memory.c's records hold host pointers, so on x64 the block header is 0x50
 * bytes (the EE's is 0x40) and the partition record 0x70 (the EE's 0x50):
 * every partition lands lower than on the EE by the sum of the differences.
 * The expected layout is therefore computed from the algorithm's arithmetic
 * (partition_layout below) for the host's record sizes, and the same
 * function with the EE's sizes is checked against the addresses worked out
 * by hand for the EE (the 32-bit oracle's values, retired with it at
 * Phase 2 exit, 36a1d73e; docs/port/PLATFORM.md, "Heap").
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
        total += n->size + (int)(sizeof(IosMemNode) >> 4);
        if (strcmp(n->tag, "<FREE AREA>____") == 0) {
            (*n_free)++;
        }
    }
    return total;
}

static const int sizes[11] = {4227072, 1179648, 3145728, 262144, 327680, 1,
                              32768,   20480,   10240,   1,      15826944};

/* what the root partition and iosInitialize's eleven carved partitions look
   like, from memory.c's arithmetic: a partition record of `part` bytes and a
   block header of `node` bytes, the root over EE 0x760000..0x1FEFFF0 */
typedef struct Layout {
    unsigned int off[11]; /* partition record offsets, from the arena base */
    int root_total;       /* in quadwords */
    int root_free;
    int root_head;
} Layout;

static void partition_layout(Layout *l, unsigned int part, unsigned int node)
{
    unsigned int top = 0x1FEFFF0;
    int total = (int)((top - (0x760000 + part)) >> 4);
    int free = total;
    int head = total - (int)(node >> 4);
    int i;

    l->root_total = total;
    for (i = 0; i < 11; i++) {
        int need = (int)((((unsigned int)sizes[i] + 15u) & ~15u) + part + node) >> 4;
        top -= (unsigned int)need << 4;
        l->off[i] = top;
        free -= need;
        head -= need;
    }
    l->root_free = free;
    l->root_head = head;
}

int main(void)
{
    IosMemPart *root;
    IosMemPart *part[11];
    /* the EE's addresses, worked out by hand (0x40-byte headers, 0x50 records) */
    static const unsigned int want_ee[11] = {0x1BE7F60, 0x1AC7ED0, 0x17C7E40, 0x1787DB0,
                                             0x1737D20, 0x1737C80, 0x172FBF0, 0x172AB60,
                                             0x17282D0, 0x1728230, 0x08101A0};
    const unsigned int node_sz = (unsigned int)sizeof(IosMemNode);
    const unsigned int part_sz = (unsigned int)((sizeof(IosMemPart) + 15) & ~(size_t)15);
    Layout ee_l;
    Layout host_l;
    IosMemPart *ev;
    IosMemNode *node;
    char *p1;
    char *p2;
    char *p3;
    char *pa;
    char *pr;
    int i;
    int n_free;

    partition_layout(&ee_l, 0x50, 0x40);
    for (i = 0; i < 11; i++) {
        CHECK(ee_l.off[i] == want_ee[i]);
    }
    CHECK(ee_l.root_total == 1609722 && ee_l.root_free == 45077 && ee_l.root_head == 45073);
    partition_layout(&host_l, part_sz, node_sz);
    printf("host records: block header 0x%x, partition record 0x%x\n", node_sz, part_sz);

    CHECK(ico_arena_init() == 0);
    CHECK(((uintptr_t)ico_arena_base() & (ICO_ARENA_ALIGN - 1)) == 0);

    root = iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    CHECK(root != 0 && ee(root) == 0x760000);
    CHECK(ee(root->start) == 0x760000 + part_sz && ee(root->top) == 0x1FEFFF0);
    CHECK(root->total == host_l.root_total && root->free == host_l.root_total);
    for (i = 0; i < 11; i++) {
        part[i] = iosMallocSetPartition(root, sizes[i], 16);
        CHECK(part[i] != 0 && ee(part[i]) == host_l.off[i]);
    }
    CHECK(root->free == host_l.root_free && root->head->size == host_l.root_head);
    iosMallocSetPartitionName(part[3], "event");

    /* allocations in the event partition (16384 quadwords plus one block header) */
    ev = part[3];
    CHECK(ev->free == 16384 + (int)(node_sz >> 4) && ev->head->size == 16384);
    p1 = iosMallocDebug(ev, 100, "ios/message.c", 453);
    CHECK(ee(p1) == host_l.off[3] + part_sz + node_sz);
    node = (IosMemNode *)(p1 - node_sz);
    CHECK(strcmp(node->tag, "<ALLOC>________") == 0);
    CHECK(strcmp(node->name, "ios/message0453") == 0);
    CHECK(node->size == 7 && node->line == 453 && node->part == ev);
    p2 = iosMallocDebug(ev, 16, "x.c", 1);
    CHECK(ee(p2) == ee(p1) + 112 + node_sz);
    CHECK(ico_heap_stats_used(ev) == (112 + node_sz) + (16 + node_sz));

    /* a freed block is reused best-fit */
    iosFree(p1);
    CHECK(ico_heap_stats_used(ev) == 16 + node_sz);
    p3 = iosMallocDebug(ev, 32, "y.c", 2);
    CHECK(p3 == p1);

    /* aligned allocation: aligned in EE terms too (the arena is 1 MB aligned) */
    pa = iosMallocAlignDebug(ev, 100, 128, "z.c", 3);
    CHECK(pa != 0 && ((uintptr_t)pa % 128) == 0 && (ee(pa) % 128) == 0);

    /* realloc shrinks in place when the next block is free */
    pr = iosMallocDebug(ev, 4096, "r.c", 4);
    CHECK(iosReallocDebug(pr, 64) == pr);
    CHECK(((IosMemNode *)(pr - node_sz))->size == 4);

    /* freeing everything leaves the partition whole again */
    iosFree(pa);
    iosFree(p2);
    iosFree(p3);
    iosFree(pr);
    CHECK(walk_total(ev, &n_free) == 16384 + (int)(node_sz >> 4));
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
