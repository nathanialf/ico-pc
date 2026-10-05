/*
 * port/platform/arena.c
 *
 * The simulated EE main RAM (arena.h) and the heap statistics.
 */
#include <stdio.h>
#include <string.h>

#include "arena.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static unsigned char *arena;

/* Where a 32-bit Linux host is asked to put the arena: low, so addresses
   stay below 2 GB (mmap would otherwise take them from under the stack, near
   3 GB). Only a hint; any address works. */
#define ARENA_HINT_32 ((void *)(uintptr_t)0x10000000u)

int ico_arena_init(void)
{
    size_t want = (size_t)ICO_ARENA_EE_SIZE + ICO_ARENA_HEADROOM;
    size_t total = want + ICO_ARENA_ALIGN;
    uintptr_t p;
    void *m;

    if (arena != NULL) {
        return 0;
    }
#if defined(_WIN32)
    /* 32-bit Windows gives addresses below 2 GB unless the program is
       large-address aware; VirtualAlloc memory is zero filled. */
    m = VirtualAlloc(NULL, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (m == NULL) {
        fprintf(stderr, "arena: cannot allocate %u bytes\n", (unsigned int)total);
        return -1;
    }
#else
    m = mmap(sizeof(void *) == 4 ? ARENA_HINT_32 : NULL, total, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        fprintf(stderr, "arena: cannot map %lu bytes\n", (unsigned long)total);
        return -1;
    }
#endif
    p = ((uintptr_t)m + ICO_ARENA_ALIGN - 1) & ~(uintptr_t)(ICO_ARENA_ALIGN - 1);
    arena = (unsigned char *)p;
    if (sizeof(void *) == 4 && p + want > 0x80000000u) {
        fprintf(stderr, "arena: placed at %p, above 2 GB; int-held addresses are negative\n",
                (void *)arena);
    }
    return 0;
}

unsigned char *ico_arena_base(void)
{
    return arena;
}

size_t ico_arena_size(void)
{
    return arena != NULL ? (size_t)ICO_ARENA_EE_SIZE + ICO_ARENA_HEADROOM : 0;
}

uintptr_t ico_arena_ee_addr(unsigned int ee)
{
    if (arena == NULL && ico_arena_init() != 0) {
        return 0;
    }
    return (uintptr_t)arena + ee;
}

int ico_arena_contains(const void *p, size_t n)
{
    uintptr_t a = (uintptr_t)p;
    uintptr_t b = (uintptr_t)arena;
    return arena != NULL && a >= b && n <= ico_arena_size() && a - b <= ico_arena_size() - n;
}

/* --- Heap statistics ------------------------------------------------------- */

#define STATS_PARTS 32

typedef struct PartStats {
    const void *part;
    const char *name;
    unsigned int used;
    unsigned int high_water;
    unsigned int logged;
} PartStats;

static PartStats stats[STATS_PARTS];
static int n_stats;

static PartStats *stats_of(const void *part, int add)
{
    int i;
    for (i = 0; i < n_stats; i++) {
        if (stats[i].part == part) {
            return &stats[i];
        }
    }
    if (!add || n_stats == STATS_PARTS) {
        return NULL;
    }
    memset(&stats[n_stats], 0, sizeof stats[n_stats]);
    stats[n_stats].part = part;
    return &stats[n_stats++];
}

void ico_heap_stats_alloc(const void *part, const char *name, unsigned int bytes)
{
    PartStats *s = stats_of(part, 1);
    if (s == NULL) {
        return;
    }
    s->name = name;
    s->used += bytes;
    if (s->used > s->high_water) {
        s->high_water = s->used;
        if (s->high_water >= s->logged + ICO_HEAP_STATS_STEP) {
            s->logged = s->high_water;
            fprintf(stderr, "heap: \"%s\" high water %u KB\n", name != NULL ? name : "?",
                    s->high_water / 1024u);
        }
    }
}

void ico_heap_stats_free(const void *part, unsigned int bytes)
{
    PartStats *s = stats_of(part, 0);
    if (s == NULL) {
        return;
    }
    s->used = bytes > s->used ? 0 : s->used - bytes;
}

unsigned int ico_heap_stats_used(const void *part)
{
    PartStats *s = stats_of(part, 0);
    return s != NULL ? s->used : 0;
}

unsigned int ico_heap_stats_high_water(const void *part)
{
    PartStats *s = stats_of(part, 0);
    return s != NULL ? s->high_water : 0;
}

void ico_heap_stats_dump(void)
{
    int i;
    for (i = 0; i < n_stats; i++) {
        fprintf(stderr, "heap: \"%s\" in use %u KB, high water %u KB\n",
                stats[i].name != NULL ? stats[i].name : "?", stats[i].used / 1024u,
                stats[i].high_water / 1024u);
    }
}

void ico_heap_stats_reset(void)
{
    n_stats = 0;
}
