/*
 * ico2/ito/include/mv_defs.h, the `ito` programmer's movie/MPEG private
 * header: the address masks and the zeroed allocation the mpeg files share.
 *
 * alloc_zeroed passes __FILE__ and the line of each call to the debug
 * allocator and to the assert, so the name the file is included under
 * ("../ito/include/mv_defs.h") and those lines are part of the program's
 * data.  The original header had its two calls at lines 43 and 44 in the
 * PAL build and 42 and 43 in the Japanese one; LN gives each build its own.
 *
 * phys_addr strips the segment bits from an EE address, the form the DMA
 * controller and the IOP heap calls take; uncached_accel_addr puts the same
 * address in the uncached-accelerated segment.
 *
 * alloc_zeroed takes a block of the movie heap and clears it.  Every caller
 * aligns to 64 bytes except viBufCreate's third call, which aligns to 4.
 *
 * Free, the release that goes with it, is defined in each mpeg file that
 * calls it.
 */
#include "typedef.h" /* ICO_PHYS, ICO_UNCACHED */



#ifndef MV_DEFS_H
#define MV_DEFS_H

/* the physical address under a segment-mapped one, which the DMA
   controller takes */
static __inline__ int phys_addr(int p) /* derived name */
{ return ICO_PHYS(p); }

/* the same address in the uncached-accelerated segment, ORed onto the
   physical one */
static __inline__ int uncached_accel_addr(int p) /* derived name */
{ return ICO_UNCACHED(ICO_PHYS(p)); }
#include <string.h>
#include <assert.h>
#include "ios.h"
void *iosMallocAlignDebug(struct IosMemPart *part, int size, int align, const char *file, int line);
void debug_assert(const char *file, int line);   /* assert reporter */
static __inline__ int alloc_zeroed(int size, int align) /* derived name */
{ int p = (int)iosMallocAlignDebug(ios_partition_mpeg, size, align, __FILE__, LN(43, 42, 42));
  if (p == 0) { debug_assert(__FILE__, LN(44, 43, 43)); __assert(__FILE__, LN(44, 43, 43), "p != NULL"); }
  memset((void *)p, 0, size); return p; }

#endif /* MV_DEFS_H */
