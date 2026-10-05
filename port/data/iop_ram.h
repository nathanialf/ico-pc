/*
 * port/data/iop_ram.h
 *
 * The host's stand-in for the IOP's 2 MB of RAM.  The game names IOP memory
 * by IOP address (an int it gets from sceSifAllocIopHeap and hands to
 * sceCdReadIOPm, SgStAdpcmOpen, sceSifSetDma and the sound driver); on the
 * host those addresses index ico_iop_ram.  Address 0 is never handed out, so
 * the game's "0 means the allocation failed" tests keep working.
 */
#ifndef ICO_PORT_IOP_RAM_H
#define ICO_PORT_IOP_RAM_H

#include <stddef.h>
#include <stdint.h>

#define ICO_IOP_RAM_SIZE 0x200000u

extern unsigned char ico_iop_ram[ICO_IOP_RAM_SIZE];

/* An IOP address with its KSEG bits stripped (the IOP's kseg0/kseg1 views
   0x80000000 and 0xA0000000 alias the same RAM). */
static inline uint32_t ico_iop_phys(uint32_t iop_addr)
{
    return iop_addr & 0x1FFFFFFFu;
}

/* Nonzero when [iop_addr, iop_addr + len) lies inside IOP RAM. */
int ico_iop_range_ok(uint32_t iop_addr, uint32_t len);

/* The host pointer for an IOP address, or NULL outside IOP RAM. */
void *ico_iop_ptr(uint32_t iop_addr);

/* The IOP address of a host pointer into ico_iop_ram; 0 for any other
   pointer (0 is never a valid result for a pointer into the heap). */
uint32_t ico_iop_addr(const void *p);

/* The IOP heap sceSifAllocIopHeap draws from (port/data/sif_host.c).  The
   region below ICO_IOP_HEAP_BASE stands in for the IOP kernel and modules. */
#define ICO_IOP_HEAP_BASE 0x10000u
#define ICO_IOP_HEAP_END ICO_IOP_RAM_SIZE

/* Forget every IOP heap block (tests, and a full restart). */
void ico_iop_heap_reset(void);

/* First-fit allocation of `size` bytes (rounded up to 16) from the IOP heap;
   returns the IOP address or 0. */
uint32_t ico_iop_heap_alloc(uint32_t size);

/* Free a block ico_iop_heap_alloc returned: 0, or -1 for an unknown
   address. */
int ico_iop_heap_free(uint32_t iop_addr);

/* Bytes in the largest free block, for diagnostics and tests. */
uint32_t ico_iop_heap_largest_free(void);

#endif /* ICO_PORT_IOP_RAM_H */
