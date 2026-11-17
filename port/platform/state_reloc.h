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

/* A state from another build (state_xbuild.h): the MODULE range's words do
   not move by one delta but by where each one's function, variable or block
   lies in this build.  A span takes an old [lo, hi] (hi included) to now +
   (word - lo); a point takes exactly one old word (lo == hi) to now. */
typedef struct IcoStateSpan {
    uint64_t lo, hi;
    uint64_t now;
} IcoStateSpan;

typedef struct IcoStateXlate {
    const IcoStateSpan *span; /* sorted by lo, disjoint */
    size_t n_span;
    const IcoStateSpan *point; /* sorted by lo, each lo == hi */
    size_t n_point;
    /* NULL, or counters: the MODULE words that had no translation (left
       as they were) and the first of them, wherever they lay */
    uint64_t *missed;
    uint64_t *first_missed;
    /* NULL, or the same words split by place (IcoStateMiss below): the
       walkers name where their words lie before they walk them */
    struct IcoStateMiss *miss;
    /* NULL, or per fiber slot its own points (sorted, each lo == hi),
       looked up before `point` while that fiber's slot is restored: an
       arm64 page word (state_xbuild.h) that two fibers hold for two
       pages.  fiber_point[s] (NULL: none) and fiber_n_point[s] for slot s
       below n_fiber. */
    const IcoStateSpan *const *fiber_point;
    const size_t *fiber_n_point;
    size_t n_fiber;
    /* the slot being restored + 1 (0: none): fiber.c sets it on its own
       copy of the translation for each slot it restores */
    int fiber;
} IcoStateXlate;

/* --- A cross-build load's words without a translation, by place ----------

   A fiber's saved stack below its saved stack pointer less the red zone
   holds dead frames: the save names nothing there ("soft" words,
   state_host.c xb_scan_stack, state_xbuild.c), so a word there without a
   translation is left as it was by design.  Everything else a load walks
   is live: the arena, the game's data and bss, the registry's scanned
   blocks, the model tables, the scheduler's table, a fiber's control page
   and saved registers and its stack from the stack pointer less the red
   zone up.  A live word without a translation would be a stale pointer in
   the resumed game: state_host.c refuses the exact resume for one (the
   checkpoint's way).

   The split point, shared by the save's soft scan and the load's count so
   the two cannot drift: of the n saved stack bytes at `base` (8-aligned)
   of a fiber whose saved stack pointer is sp, the first
   ico_state_stack_dead_bytes are dead (a multiple of 8; 0 when sp is 0 or
   sp - ICO_STATE_RED_ZONE is at or below base, n when it is at or above
   base + n).  The red zone is System V's (x86-64) and the same 128 bytes
   on arm64, where frames below sp are dead at once (fiber.c's save starts
   its image at or below sp less the same zone). */
#define ICO_STATE_RED_ZONE 128

size_t ico_state_stack_dead_bytes(uint64_t base, size_t n, uint64_t sp);

typedef struct IcoStateMiss {
    /* the walk's place, set by the walker (ico_state_miss_at) */
    const char *where;         /* "the arena", a block's name, "stack", ... */
    uint64_t base;             /* the address offsets are counted from */
    int fiber;                 /* a fiber slot + 1 (0: no fiber's) */
    int sp_rel;                /* 1: base is the fiber's stack pointer */
    uint64_t dead_lo, dead_hi; /* [dead_lo, dead_hi): the dead frames (0, 0: none) */
    /* the counts */
    uint64_t dead, first_dead; /* words in dead frames, the first's value */
    uint64_t live, first_live; /* words anywhere else, the first's value */
    const char *live_where;    /* the first live word's place */
    int live_fiber, live_sp_rel;
    int64_t live_off;
} IcoStateMiss;

/* The walk's place from here on (m NULL: nothing); the dead range is
   cleared (fiber.c sets it for a stack after this call). */
void ico_state_miss_at(IcoStateMiss *m, const char *where, uint64_t base, int fiber, int sp_rel);

/* The first live miss in words, for the log and the refusal: "fiber slot 5's
   stack, 64 bytes below its saved stack pointer, holds 0x... (offset 0x...
   in the saving build's image)" or "the arena at offset 0x10 holds ...".
   image_lo: the saving process's image (the MODULE range's lo). */
void ico_state_miss_live_text(const IcoStateMiss *m, uint64_t image_lo, char *out, size_t n);

/* The load's verdict on its misses: 0 with out empty when none lies in
   live memory (the dead frames' are left as they were, by design), else 1
   with out "N program words in live memory had no translation, the first:
   <ico_state_miss_live_text>": state_host.c rolls the restore back and
   the state continues from its checkpoint (ICO_STATE_SELF_CHECK). */
int ico_state_miss_refuses(const IcoStateMiss *m, uint64_t image_lo, char *out, size_t n);

/* The loaded line's counts: "N program words in dead stack frames had no
   translation and were left as they were (first 0x...); L in live
   memory". */
void ico_state_miss_counts(const IcoStateMiss *m, char *out, size_t n);

/* 1 and *now when `old` has a translation (the restored fiber's own point
   first, then a point, then a span), else 0. */
int ico_state_xlate(const IcoStateXlate *x, uint64_t old, uint64_t *now);

typedef struct IcoStateReloc {
    IcoStateRange range[ICO_RELOC_RANGES]; /* indexed by ICO_RELOC_* */
    /* NULL (one build: the MODULE range moves by its delta), or the
       translation of every word in the MODULE range (another build) */
    const IcoStateXlate *xlate;
    IcoStateValue value[ICO_RELOC_VALUES]; /* indexed by ICO_RELOC_TLS/GUARD */
    /* NULL, or ICO_RELOC_VALUES counters each value's rewrites are added
       to (the words are counted wherever they are found: the fibers'
       registers and stacks, the scheduler's table, the scanned regions) */
    uint64_t *value_words;
} IcoStateReloc;

/* The conservative scan's step: when *w equals an enabled value's old word
   (above), *w becomes its new word, the value's counter goes up and the
   result is 1; else when *w lies in a range and that range moved,
   *w += delta and the result is 1 (with xlate set, a word in the MODULE
   range becomes its translation instead, the result 1 when it changed; a
   word without one is left and counted); otherwise *w is left alone and
   the result is 0. */
int ico_state_reloc_word(const IcoStateReloc *r, uint64_t *w);
/* The same for a word read from the address `at` (a copy in *w): a miss
   is counted by `at`'s place (IcoStateMiss).  ico_state_reloc_word is this
   with at = w. */
int ico_state_reloc_word_at(const IcoStateReloc *r, uint64_t *w, uint64_t at);

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
   moved by that range's delta (with xlate set and `which` the MODULE
   range: becomes its translation).  Returns 0, or -1 (and *w unchanged)
   when the range is not empty and a nonzero *w lies outside it, or has no
   translation: the word is not what its type says, and the caller refuses
   the state. */
int ico_state_reloc_typed(const IcoStateReloc *r, int which, uint64_t *w);

/* --- The scan over a state's regions (state_reloc.c) and the image range
   and self-checks (state_check.c) ------------------------------------------ */

/* The conservative scan: every 8-byte-aligned word inside [region,
   region + n) goes through ico_state_reloc_word (a word that straddles
   either end is not read).  Returns the number of words moved and adds it
   to *rewritten when that is not NULL.  With the developer log on, each
   moved word is logged ("state: moved the word at <addr>: <old> -> <new>"). */
size_t ico_state_reloc_scan(const IcoStateReloc *r, void *region, size_t n, uint64_t *rewritten);

/* The smallest upper half (bits 32..63) a moved range's saved addresses may
   have for the conservative scan to move it by its delta: 16, an address of
   64 GB or more.  The game's memory is made of 32-bit words, and two of them
   read as one 64-bit word are an address whose upper half is the second: an
   EE offset, a float or any value next to a small integer (a ClusterPoly's
   {run, matrix = 1}, a keyframe's {X, 1}).  In five real states' arenas a
   second word of 1 to 15 follows 8,556 to 11,870 words each, 570 to 790 per
   value (6371 with 1 in issue 218's), against 47 to 70 per value from 16 to
   255: with an upper half that small a range's
   words cannot be told from the game's numbers, and moving them corrupts its
   data (issue 218: the Apple programs just above the 4 GB __PAGEZERO, upper
   half 1, about 520 of the arena's 3575 such words not addresses).  Larger
   second words are rarer but not absent: the scan's accepted risk where an
   image moves above them (Android's library at an upper half of 0x5x to
   0x7f; docs/port/PLATFORM.md, "Relocation"). */
#define ICO_STATE_SCAN_HIGH_MIN 0x10u

/* -1 when the conservative scan may move every range that moves, else the
   first that it may not (ICO_RELOC_MODULE, _ARENA or _POOL): a range that
   moved (delta != 0) whose saved addresses' upper half (lo's or hi's) is
   below ICO_STATE_SCAN_HIGH_MIN.  The MODULE range is not judged when the
   translation is set (another build's words go by what they name, not by a
   delta), nor the POBJ range (a small host block, its delta non-zero on
   every load).  A load gets -1 when every delta is 0 (the same process, or
   a fixed image with the arena and pool pinned). */
int ico_state_reloc_ambiguous(const IcoStateReloc *r);

/* Another build's state whose image's upper half is below
   ICO_STATE_SCAN_HIGH_MIN (Windows' fixed base 0x140000000): a float from
   2.0 to about 142 next to the integer 1 lies in that image, in its code
   (issue 218's measurement: 1 to 15 keyframe {X, 1} halves of each of five
   real states' arenas fall in the Windows v0.11.8 program's .text), and the
   translation would move it by its function's displacement.  A data
   structure holds a function's address, which is a span's start in the
   translation, never a byte inside its code: the words of [region, region +
   n) in [text_lo, text_hi) that are no span's start and no point are
   numbers.  Their values go into the sorted distinct list out[0..*k) (cap
   entries; *k grows), so calls over several regions build one list; the
   result is the number of values this call found that the list did not
   hold (counted also when the list was full and they were not written, so
   *k below the sum of the results means some were dropped).  The load
   keeps them as they are (an identity point each). */
size_t ico_state_code_numbers(const IcoStateXlate *x, uint64_t text_lo, uint64_t text_hi,
                              const void *region, size_t n, uint64_t *out, size_t cap, size_t *k);

/* 1 when an 8-byte-aligned word inside [region, region + n) is one of the
   nv values of the sorted list vals, and *which (not NULL) that value; else
   0 (a kept number that a fiber's stack or the scheduler's table also holds
   may be a return address there: the load does not keep it).  One pass
   over the region, a binary search a word. */
int ico_state_region_holds(const void *region, size_t n, const uint64_t *vals, size_t nv,
                           uint64_t *which);

/* The two rules above as one verdict, for the load (state_host.c
   perform_load, which applies them to its staged sections) and the Save
   states page's listing (vd_compute, from the file): ICO_STATE_MOVE_OK; a
   range the scan may not move (ico_state_reloc_ambiguous: its index,
   ICO_RELOC_MODULE, _ARENA or _POOL); or, for another build's state from an
   image whose upper half is below the bound (saved_image_lo; rel->xlate
   set), the numbers inside the old code [text_lo, text_hi) of the n_data
   data regions (ico_state_code_numbers into nums, cap, *nk) when there are
   more than cap (ICO_STATE_MOVE_TOO_MANY) or one of them is a word of the
   n_held regions (the fibers, the scheduler's table: ICO_STATE_MOVE_HELD,
   *held_value that value).  Both later outcomes send the state to its
   checkpoint; an OK with *nk > 0 keeps those numbers as identity points. */
enum { ICO_STATE_MOVE_OK = -1, ICO_STATE_MOVE_HELD = ICO_RELOC_RANGES, ICO_STATE_MOVE_TOO_MANY };

typedef struct IcoStateMoveIn {
    const IcoStateReloc *rel;
    uint64_t saved_image_lo, text_lo, text_hi;
    const void *const *data; /* n_data regions (NULL entries skipped) */
    const size_t *data_n;
    int n_data;
    const void *const *held; /* n_held regions */
    const size_t *held_n;
    int n_held;
} IcoStateMoveIn;

int ico_state_move_check(const IcoStateMoveIn *in, uint64_t *nums, size_t cap, size_t *nk,
                         uint64_t *held_value);

/* The 8-byte-aligned words inside [region, region + n) that lie in range
   `which` (lo <= word <= hi): what a scan would take for its addresses.
   0 for an empty range or an index outside the ranges. */
size_t ico_state_reloc_count(const IcoStateReloc *r, int which, const void *region, size_t n);

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
#define ICO_STATE_IMAGE_RO 8

typedef struct IcoStateImage {
    uint64_t lo, hi;           /* the whole image */
    uint64_t text_lo, text_hi; /* its code */
    /* its read-only data (mapped, neither writable nor executable: ELF
       PT_LOAD segments without PF_W and PF_X, PE sections readable only,
       Mach-O segments mapped read-only), at most ICO_STATE_IMAGE_RO */
    int n_ro;
    uint64_t ro_lo[ICO_STATE_IMAGE_RO], ro_hi[ICO_STATE_IMAGE_RO];
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
