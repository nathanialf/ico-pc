/*
 * port/data/iop_ram.c
 *
 * ico_iop_ram and the IOP heap over it (iop_ram.h).  The heap is a small
 * first-fit block table: the game holds at most a handful of IOP buffers at
 * once (the sound bank area, the ADPCM stream rings, the movie stream
 * buffer and the movie audio buffer).
 */
#include "iop_ram.h"

#include <string.h>

unsigned char ico_iop_ram[ICO_IOP_RAM_SIZE] __attribute__((aligned(64)));

int ico_iop_range_ok(uint32_t iop_addr, uint32_t len)
{
    uint32_t a = ico_iop_phys(iop_addr);

    return a < ICO_IOP_RAM_SIZE && len <= ICO_IOP_RAM_SIZE - a;
}

void *ico_iop_ptr(uint32_t iop_addr)
{
    uint32_t a = ico_iop_phys(iop_addr);

    if (a >= ICO_IOP_RAM_SIZE) {
        return NULL;
    }
    return &ico_iop_ram[a];
}

uint32_t ico_iop_addr(const void *p)
{
    const unsigned char *c = p;

    if (c < ico_iop_ram || c >= ico_iop_ram + ICO_IOP_RAM_SIZE) {
        return 0;
    }
    return (uint32_t)(c - ico_iop_ram);
}

/* --- the heap -------------------------------------------------------------- */

#define HEAP_MAX_BLOCKS 64

typedef struct {
    uint32_t addr;
    uint32_t size;
} HeapBlock;

/* allocated blocks, kept sorted by address */
static HeapBlock heapBlocks[HEAP_MAX_BLOCKS];
static int heapCount;

void ico_iop_heap_reset(void)
{
    heapCount = 0;
    memset(heapBlocks, 0, sizeof(heapBlocks));
}

uint32_t ico_iop_heap_alloc(uint32_t size)
{
    uint32_t cur = ICO_IOP_HEAP_BASE;
    int i;

    if (size == 0 || size > ICO_IOP_HEAP_END - ICO_IOP_HEAP_BASE || heapCount >= HEAP_MAX_BLOCKS) {
        return 0;
    }
    size = (size + 15u) & ~15u;
    for (i = 0; i <= heapCount; i++) {
        uint32_t next = (i < heapCount) ? heapBlocks[i].addr : ICO_IOP_HEAP_END;

        if (next - cur >= size) {
            memmove(&heapBlocks[i + 1], &heapBlocks[i],
                    (size_t)(heapCount - i) * sizeof(HeapBlock));
            heapBlocks[i].addr = cur;
            heapBlocks[i].size = size;
            heapCount++;
            return cur;
        }
        if (i < heapCount) {
            cur = heapBlocks[i].addr + heapBlocks[i].size;
        }
    }
    return 0;
}

int ico_iop_heap_free(uint32_t iop_addr)
{
    uint32_t a = ico_iop_phys(iop_addr);
    int i;

    for (i = 0; i < heapCount; i++) {
        if (heapBlocks[i].addr == a) {
            memmove(&heapBlocks[i], &heapBlocks[i + 1],
                    (size_t)(heapCount - i - 1) * sizeof(HeapBlock));
            heapCount--;
            return 0;
        }
    }
    return -1;
}

uint32_t ico_iop_heap_largest_free(void)
{
    uint32_t cur = ICO_IOP_HEAP_BASE;
    uint32_t best = 0;
    int i;

    for (i = 0; i <= heapCount; i++) {
        uint32_t next = (i < heapCount) ? heapBlocks[i].addr : ICO_IOP_HEAP_END;

        if (next - cur > best) {
            best = next - cur;
        }
        if (i < heapCount) {
            cur = heapBlocks[i].addr + heapBlocks[i].size;
        }
    }
    return best;
}
