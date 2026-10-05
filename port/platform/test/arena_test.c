/*
 * port/platform/test/arena_test.c
 *
 * The EE RAM arena (arena.h): allocated once, aligned, zero filled, the EE
 * address mapping, and the heap statistics bookkeeping memory.c feeds.
 */
#include <stdint.h>
#include <stdio.h>

#include "arena.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    unsigned char *base;
    size_t i;
    /* three partition identities: two used, one never seen */
    static int parts[3];

    CHECK(ico_arena_base() == NULL && ico_arena_size() == 0);
    CHECK(ico_arena_init() == 0);
    base = ico_arena_base();
    CHECK(base != NULL);
    CHECK(((uintptr_t)base & (ICO_ARENA_ALIGN - 1)) == 0);
    CHECK(ico_arena_size() == ICO_ARENA_EE_SIZE + ICO_ARENA_HEADROOM);
    /* allocated once */
    CHECK(ico_arena_init() == 0 && ico_arena_base() == base);
    /* zero filled, every page */
    for (i = 0; i < ico_arena_size(); i += 4096) {
        if (base[i] != 0) {
            fails++;
            printf("FAIL: arena byte %lu is not zero\n", (unsigned long)i);
            break;
        }
    }
    base[ico_arena_size() - 1] = 1; /* the headroom is writable */
    /* EE physical addresses map to offsets */
    CHECK(ico_arena_ee_addr(0) == (uintptr_t)base);
    CHECK(ico_arena_ee_addr(0x760000) == (uintptr_t)base + 0x760000);
    CHECK(ico_arena_ee_addr(0x1FEFFF0) - ico_arena_ee_addr(0x760000) == 0x188FFF0);
    CHECK(ico_arena_contains(base, 16));
    CHECK(ico_arena_contains(base + ico_arena_size() - 16, 16));
    CHECK(!ico_arena_contains(base + ico_arena_size() - 15, 16));
    CHECK(!ico_arena_contains(base - 1, 1));

    /* heap statistics */
    ico_heap_stats_alloc(&parts[0], "a", 1000);
    ico_heap_stats_alloc(&parts[0], "a", 500);
    ico_heap_stats_free(&parts[0], 1000);
    ico_heap_stats_alloc(&parts[1], "b", 64);
    CHECK(ico_heap_stats_used(&parts[0]) == 500);
    CHECK(ico_heap_stats_high_water(&parts[0]) == 1500);
    CHECK(ico_heap_stats_used(&parts[1]) == 64);
    CHECK(ico_heap_stats_used(&parts[2]) == 0);
    ico_heap_stats_alloc(&parts[0], "a", ICO_HEAP_STATS_STEP); /* logs a new mark */
    ico_heap_stats_dump();
    ico_heap_stats_reset();
    CHECK(ico_heap_stats_high_water(&parts[0]) == 0);

    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
