/*
 * port/platform/state_reloc.h   (v0.9.6 save states; A1 wrote this header
 * and the two word functions, A3's state_reloc.c adds the scan over the
 * regions on top of them; change only by agreement with the orchestrator)
 *
 * A save state's bytes hold host addresses.  When they are put back in a
 * process whose memory sits elsewhere, every address that pointed into one
 * of the moved ranges is moved by that range's delta:
 *
 *   MODULE  the program's image (code, read-only data, .data/.bss, the
 *           game's GDATA/GBSS inside it); delta 0 on a fixed image build
 *           (ICO_STATE_FIXED_IMAGE, docs/BUILDING.md)
 *   ARENA   the EE RAM arena (arena.h); delta 0 while it is pinned
 *   POOL    the fiber pool (fiber.h); delta 0 while it is pinned
 *   POBJ    the host block of pobjImageTables (PObj.c)
 *
 * A range is the OLD address range [lo, hi) as the saving process had it.
 * A word matches a range when lo <= word <= hi: the end address itself is
 * included, since C code keeps one-past-the-end pointers (a stack top, a
 * buffer's end).  The ranges never overlap; when a word equals one range's
 * hi and another's lo, the first range in enum order takes it.  An empty
 * range (lo == hi) matches nothing in the scan.
 */
#ifndef ICO_PLATFORM_STATE_RELOC_H
#define ICO_PLATFORM_STATE_RELOC_H

#include <stdint.h>

enum { ICO_RELOC_MODULE = 0, ICO_RELOC_ARENA, ICO_RELOC_POOL, ICO_RELOC_POBJ, ICO_RELOC_RANGES };

typedef struct IcoStateRange {
    uint64_t lo;   /* the range's first byte, in the saving process */
    uint64_t hi;   /* one past its last byte */
    int64_t delta; /* the address now minus the address then */
} IcoStateRange;

typedef struct IcoStateReloc {
    IcoStateRange range[ICO_RELOC_RANGES]; /* indexed by ICO_RELOC_* */
} IcoStateReloc;

/* The conservative scan's step: when *w lies in a range (see above) and
   that range moved, *w += delta and the result is 1; otherwise *w is left
   alone and the result is 0. */
int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w);

/* A typed fix-up: *w is known to be an address in range `which` (a saved
   return address, stack pointer, function pointer) or 0.  A nonzero *w is
   moved by that range's delta.  Returns 0, or -1 (and *w unchanged) when
   the range is not empty and a nonzero *w lies outside it: the word is not
   what its type says, and the caller refuses the state. */
int ico_state_reloc_typed(const IcoStateReloc *r, int which, uint64_t *w);

#endif /* ICO_PLATFORM_STATE_RELOC_H */
