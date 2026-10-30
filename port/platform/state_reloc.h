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

#include <stddef.h>
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

/* --- The scan over a state's regions (state_reloc.c) and the image range
   and self-checks (state_check.c) ------------------------------------------ */

/* The conservative scan: every 8-byte-aligned word inside [region,
   region + n) goes through ico_state_reloc_word (a word that straddles
   either end is not read).  Returns the number of words moved and adds it
   to *rewritten when that is not NULL.  With the developer log on, each
   moved word is logged ("state: moved the word at <addr>: <old> -> <new>"). */
size_t ico_state_reloc_scan(const IcoStateReloc *r, void *region, size_t n, uint64_t *rewritten);

/* Developer mode's per-word log of the scan (gameplay.developer_mode;
   state_host.c sets it before a load).  Off by default. */
void ico_state_reloc_set_log(int on);

/* The program's image in this process (the module the save state code is
   linked into: ico_pc, or libmain.so on Android): [lo, hi) spans every
   segment it loaded (code, read-only data, .data, .bss and the game's data
   ranges inside them), [text_lo, text_hi) its code.  Windows: the module's
   headers (SizeOfImage, the executable sections); Linux and Android:
   dl_iterate_phdr's PT_LOAD segments of the object holding this code;
   Apple: the image's LC_SEGMENT_64 commands through _dyld. */
typedef struct IcoStateImage {
    uint64_t lo, hi;           /* the whole image */
    uint64_t text_lo, text_hi; /* its code */
} IcoStateImage;

/* 0, or -1 (out zeroed) when the image cannot be found. */
int ico_state_image_range(IcoStateImage *out);

/* The game heap's typed walk (ico2/fumi/include/memory.h): from the
   partition `root` (ios_partition_root) through every child partition and
   every block of each, checking that each partition record and block
   header lies inside [lo, hi) (the arena now), every link (parent, next,
   child, start, end, top, head; a block's prev, next, free list links and,
   for a block in use, its partition) is 0 or inside it, the block chain
   only moves forward, every partition's tag is the allocator's partition
   mark and every block's an allocated or free mark (the three memory.c
   writes into a partition's chain), and every block on a free list is
   marked free.  0, or -1 with `why` naming the first failure.  The counts
   (when not NULL) get the partitions and blocks walked. */
int ico_state_heap_walk(const void *root, uint64_t lo, uint64_t hi, unsigned *partitions,
                        unsigned *nodes, char *why, size_t n);

/* The self-checks after a load's restore and relocation, all against the
   ranges as they are NOW: the heap walk above (skipped when heap_root is
   NULL), every scheduler thread's entry and last kernel call's caller in
   image.text, its last call's name in image, and every INTC and DMAC
   handler in image.text.  A failure means a pointer the relocation did not
   move (or moved wrongly): state_host.c restores its backup and refuses
   the state (ICO_STATE_SELF_CHECK). */
typedef struct IcoStateCheck {
    /* in */
    const void *heap_root;       /* ios_partition_root after the restore, or NULL */
    uint64_t arena_lo, arena_hi; /* the arena now */
    IcoStateImage image;         /* ico_state_image_range */
    /* out */
    unsigned partitions, nodes, threads, handlers;
    char why[160];
} IcoStateCheck;

/* 0, or -1 with c->why filled. */
int ico_state_reloc_check(IcoStateCheck *c);

#endif /* ICO_PLATFORM_STATE_RELOC_H */
