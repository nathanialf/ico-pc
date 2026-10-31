/*
 * port/platform/state_hash.h
 *
 * The hash of the game's state a save state's round trip compares
 * (port/game/state_host.c, [dev] state_hash_at): XXH3-64 over a canonical
 * copy of each region (the arena, the game's data, the game's bss), in which
 * the words that hold the host's values rather than the game's are zero:
 *
 *   fields  typed: bytes the caller names (a game file's host pointer, a
 *           renderer handle, a host cache's bookkeeping), each with its kind:
 *           a host pointer or a host id
 *   blocks  the catch-all: every 8-byte-aligned word whose value lies in a
 *           host block the caller noted (a malloc'd block the game's memory
 *           may point into, such as a model's decoded tables): lo <= word <=
 *           lo + bytes, the end address included as the relocation does
 *           (state_reloc.h)
 *
 * Two runs that hold the same game state then hash alike although the host
 * gave them other addresses and handles.  With nothing named and no block,
 * the hash is the one before masking existed: XXH3-64 of the regions' own
 * XXH3-64s, in order.
 */
#ifndef ICO_PLATFORM_STATE_HASH_H
#define ICO_PLATFORM_STATE_HASH_H

#include <stddef.h>
#include <stdint.h>

#define ICO_STATE_HASH_MAX_REGIONS 8
#define ICO_STATE_HASH_MAX_BLOCKS 64

typedef struct IcoStateHashRegion {
    const char *name; /* "arena", "gdata", "gbss" */
    const void *p;
    size_t n;
} IcoStateHashRegion;

/* a typed field's kind (and the mask callback's) */
enum {
    ICO_STATE_MASK_PTR = 0,  /* a host pointer the game's memory holds */
    ICO_STATE_MASK_ID = 1,   /* a host id, handle or the host's bookkeeping */
    ICO_STATE_MASK_BLOCK = 2 /* the catch-all: a word pointing into a noted block */
};

typedef struct IcoStateHashField {
    const void *p; /* where in a region (a field outside every region is ignored) */
    size_t n;
    int kind; /* ICO_STATE_MASK_PTR or ICO_STATE_MASK_ID */
} IcoStateHashField;

typedef struct IcoStateHashBlocks {
    int n;
    uint64_t lo[ICO_STATE_HASH_MAX_BLOCKS];
    uint64_t hi[ICO_STATE_HASH_MAX_BLOCKS]; /* inclusive: lo + bytes */
} IcoStateHashBlocks;

/* Notes the block [p, p + bytes] (no-op for a null p).  A block that
   overlaps or touches a noted one widens it; when the table is full the
   new block widens the nearest one, so nothing is ever forgotten (a wider
   range only masks more). */
void ico_state_hash_note_block(IcoStateHashBlocks *b, uint64_t p, uint64_t bytes);

typedef struct IcoStateHashResult {
    uint64_t hash;
    uint64_t region_hash[ICO_STATE_HASH_MAX_REGIONS];
    uint64_t ptr_words; /* typed pointer fields and catch-all words zeroed */
    uint64_t host_ids;  /* typed id fields zeroed */
} IcoStateHashResult;

/* Called for every masked range, in region order and, within a region,
   typed fields first (in the order given), then the catch-all words by
   offset: the region's index, the byte offset in it, the bytes and the
   kind.  May be NULL. */
typedef void (*IcoStateMaskFn)(int region, size_t off, size_t n, int kind, void *user);

/* The hash of nrg regions (at most ICO_STATE_HASH_MAX_REGIONS) with nf
   typed fields zeroed, then the words pointing into b's blocks (b may be
   NULL).  0, or -1 when a region's copy could not be allocated (out
   zeroed). */
int ico_state_hash_regions(const IcoStateHashRegion *rg, int nrg, const IcoStateHashField *f,
                           int nf, const IcoStateHashBlocks *b, IcoStateMaskFn fn, void *user,
                           IcoStateHashResult *out);

#endif /* ICO_PLATFORM_STATE_HASH_H */
