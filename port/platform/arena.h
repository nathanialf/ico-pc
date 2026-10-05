/*
 * port/platform/arena.h
 *
 * The simulated EE main RAM the game's heap lives in.
 *
 * The EE game carves its heap (fumi/ios/ios.c, iosInitialize) out of fixed
 * physical addresses: iosMallocInitPartition(0x760000, 0x1FEFFF0). On the
 * host the same range is an offset into one block allocated once at start:
 * ICO_ARENA_EE_SIZE (32 MB, the EE's RAM) plus ICO_ARENA_HEADROOM, zero
 * filled. ico_arena_ee_addr(0x760000) is where the partition starts, so the
 * partition has the EE's size and its blocks the EE's offsets.
 *
 * The base is ICO_ARENA_ALIGN (1 MB) aligned, which keeps every address's
 * residue modulo any alignment up to 1 MB equal to the EE's (the allocator
 * aligns to 16 bytes; iosMallocAlignDebug's callers use larger values).
 */
#ifndef ICO_PLATFORM_ARENA_H
#define ICO_PLATFORM_ARENA_H

#include <stddef.h>
#include <stdint.h>

#define ICO_ARENA_EE_SIZE (32u * 1024u * 1024u)
#define ICO_ARENA_HEADROOM (1u * 1024u * 1024u)
#define ICO_ARENA_ALIGN (1u * 1024u * 1024u)

/* Allocates the arena on the first call; later calls do nothing. Returns
   0, or -1 when the memory is not available. */
int ico_arena_init(void);
/* The arena's first byte (EE physical address 0); NULL before init. */
unsigned char *ico_arena_base(void);
size_t ico_arena_size(void);
/* The host address of EE physical address `ee` (an integer, for the
   allocator's integer arithmetic). Initialises the arena if needed. */
uintptr_t ico_arena_ee_addr(unsigned int ee);
/* 1 when [p, p+n) lies inside the arena. */
int ico_arena_contains(const void *p, size_t n);

/* --- Heap statistics (ICO_HEAP_STATS) ----------------------------------------
 * memory.c reports each allocation and free when built with ICO_HEAP_STATS;
 * the arena keeps the bytes in use and the high-water mark per partition
 * and logs each new high-water mark that grows it by ICO_HEAP_STATS_STEP. */
#define ICO_HEAP_STATS_STEP (64u * 1024u)

void ico_heap_stats_alloc(const void *part, const char *name, unsigned int bytes);
void ico_heap_stats_free(const void *part, unsigned int bytes);
/* The partition's bytes in use and high-water mark; 0 if never seen. */
unsigned int ico_heap_stats_used(const void *part);
unsigned int ico_heap_stats_high_water(const void *part);
/* Prints every partition's figures to stderr. */
void ico_heap_stats_dump(void);
void ico_heap_stats_reset(void);

#endif /* ICO_PLATFORM_ARENA_H */
