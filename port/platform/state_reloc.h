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
 *
 * The saving process's own values.  Two words a suspended fiber holds are
 * no address in any range and still belong to the process that saved it:
 *
 *   TLS     the game thread's thread pointer (arm64 TPIDR_EL0).  With the
 *           stack protector on, clang keeps it in a callee-saved register
 *           (x20 in ico_fiber_yield) across the switch, to read the guard
 *           through it when the frame returns.
 *   GUARD   the stack guard: the word every protected frame holds and
 *           compares on return (Android: bionic's thread slot at the thread
 *           pointer + 0x28; Apple: __stack_chk_guard).
 *
 * A later process has other values, so a frame resumed with the old ones
 * reads through an old thread pointer (a fault) or finds a guard that is
 * not this process's (__stack_chk_fail).  ico_state_reloc_word rewrites a
 * word EQUAL to a value's old word to its new word, before it looks at the
 * ranges.  Exact 64-bit matches only: a thread pointer or a random guard is
 * never a legitimate game word by accident, as a range's span can be.
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

enum { ICO_RELOC_TLS = 0, ICO_RELOC_GUARD, ICO_RELOC_VALUES };

typedef struct IcoStateValue {
    uint64_t old; /* in the saving process; 0: off */
    uint64_t now; /* in this one */
} IcoStateValue;

typedef struct IcoStateReloc {
    IcoStateRange range[ICO_RELOC_RANGES]; /* indexed by ICO_RELOC_* */
    IcoStateValue value[ICO_RELOC_VALUES]; /* indexed by ICO_RELOC_TLS/GUARD */
    /* NULL, or ICO_RELOC_VALUES counters each value's rewrites are added
       to (the words are counted wherever they are found: the fibers'
       registers and stacks, the scheduler's table, the scanned regions) */
    uint64_t *value_words;
} IcoStateReloc;

/* The conservative scan's step: when *w equals an enabled value's old word
   (above), *w becomes its new word, the value's counter goes up and the
   result is 1; else when *w lies in a range and that range moved,
   *w += delta and the result is 1; otherwise *w is left alone and the
   result is 0. */
int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w);

/* Sets the two values (old -> now) and their counters (`words`: NULL, or
   ICO_RELOC_VALUES counters, zeroed here).  A value whose old or new word
   is 0, or whose two words are equal, is left off: nothing to rewrite. */
void ico_state_reloc_set_values(IcoStateReloc *r, uint64_t tls_old, uint64_t tls_now,
                                uint64_t guard_old, uint64_t guard_now, uint64_t *words);

/* 1 when a value is on (a load must scan even when no range moved). */
int ico_state_reloc_values_on(const IcoStateReloc *r);

/* --- The process's values (state_reloc.c) ---------------------------------- */

typedef struct IcoStateProc {
    uint64_t tls_base;    /* arm64 Linux and Android: TPIDR_EL0; else 0 */
    uint64_t stack_guard; /* Android arm64: the word at TPIDR_EL0 + 0x28;
                             Apple: __stack_chk_guard; else 0 */
} IcoStateProc;

/* The calling thread's values (a save records the game thread's: the one
   that runs every fiber). */
void ico_state_proc_read(IcoStateProc *out);

/* Which values a state made on platform `tag` (state_file.h's platform
   tags) must carry: android-arm64 both, linux-arm64 the thread pointer,
   macos-* and ios-* the guard, every other tag none. */
#define ICO_STATE_PROC_TLS 1u
#define ICO_STATE_PROC_GUARD 2u
unsigned ico_state_proc_needs(const char *tag);

/* 0, or -1 when a value `tag` needs is 0 (a state without the record,
   which a load refuses before it changes anything). */
int ico_state_proc_check(const char *tag, uint64_t tls_base, uint64_t stack_guard);

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
