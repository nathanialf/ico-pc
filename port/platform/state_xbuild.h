/*
 * port/platform/state_xbuild.h
 *
 * Save states that cross versions: the state's "syms" section, written at
 * every save, read when a state of another build is loaded.
 *
 * One build's state holds host addresses into that build's program: return
 * addresses and the callee-saved registers of every suspended fiber,
 * function pointers in the game's memory and the port's blocks, pointers
 * to the game's globals, the port's blocks and the program's constant
 * strings.  The same build moves them all by one delta (state_reloc.h).  A
 * later build has its functions and variables elsewhere, so each word is
 * translated by WHAT it points into, which the save records:
 *
 *   units   a word inside the code: the unit holding it (state_syms.h) by
 *           name, ordinal, old start, size and code hash (code_hash.h),
 *           with what pointed into it: a POSITION (a return address, any
 *           word past the unit's start: the unit must hash alike in the
 *           later build, or the frame would resume in other code) or only
 *           its START (a function pointer: the unit must exist, and when its
 *           name is not unique it must also hash alike).  Words in the dead
 *           part of a fiber's stack (below its saved stack pointer less the
 *           128-byte red zone) are SOFT: translated when the unit is still
 *           there and alike, left as they were otherwise, never a refusal.
 *   game    a word inside the game's data or bss range: moved with its
 *           range, valid when the game data layout hash (state_syms.h) is
 *           the same.
 *   blocks  a word inside a registered block (state_registry.h): moved with
 *           the block, matched by name.  Every block's name, size, flags,
 *           old address and offset in its packed section are recorded, so a
 *           later build maps the reg and audio sections by name (the order
 *           may differ); a block missing on either side or of another size
 *           refuses the state.
 *   data    a word inside a named data symbol whose name is unique: by name,
 *           old start and size (exact when the file gave it).
 *   points  any other word: when it equals one of the game data's values at
 *           start-up (an initialised pointer of a game table, a string or a
 *           function the game copied from one), the offset of that value,
 *           taken in the later build from its own start-up values; else,
 *           when it points at a C string (printable bytes ending in NUL
 *           within ICO_XB_CONTENT), those bytes, found again in the later
 *           build's image outside the code and the game ranges.
 *
 * A word only these confirm is translated.  A word in the code that no
 * unit holds, or in the image that nothing above names, is confirmed by
 * nothing: it is kept as the number it is (ICO_XB_UNNAMED, counted, the
 * first one recorded; the load has no span or point for it).  Such words
 * are ordinary data where the image's addresses have a small upper half:
 * under Windows' fixed base 0x140000000 any float from 2.0 to about 572
 * followed by the integer 1 lies in the image.  A state is pinned to its
 * build by more references than the bounds (ICO_XB_OVERFLOW), a saving
 * build without a symbol file (no ICO_XB_TABLE) and a program image below
 * 4 GB (ICO_XB_LOW_IMAGE: there a program address cannot be told from a
 * number, such as the PS2 addresses the game keeps, which then land on
 * named code and data too, so translating every word that looks like one
 * is not safe).  The same build never reads this section: its build id
 * pins the one-delta path.
 *
 * The section (little-endian): a 64-byte header, then the units (48 bytes
 * each), data (40), points (48) and blocks (40) records, then the names.
 * The header's last word counts the words nothing names (0 in a record
 * from before it was counted).
 */
#ifndef ICO_PLATFORM_STATE_XBUILD_H
#define ICO_PLATFORM_STATE_XBUILD_H

#include <stddef.h>
#include <stdint.h>

#include "state_reloc.h"
#include "state_syms.h"

#define ICO_XB_MAGIC 0x534D5953u /* "SYMS" */
#define ICO_XB_VERSION 1
#define ICO_XB_MAX_UNITS 16384
#define ICO_XB_MAX_DATA 4096
#define ICO_XB_MAX_POINTS 4096
#define ICO_XB_MAX_BLOCKS 128
#define ICO_XB_MAX_VALUES 262144 /* distinct words looked at (a power of two) */
#define ICO_XB_MAX_SEARCHES 512  /* content points found again at a load */
#define ICO_XB_CONTENT 24
/* the section's size at most (a load's bound before it inflates it) */
#define ICO_XB_MAX_BYTES                                                                           \
    (64u + ICO_XB_MAX_UNITS * 48u + ICO_XB_MAX_DATA * 40u + ICO_XB_MAX_POINTS * 48u +              \
     ICO_XB_MAX_BLOCKS * 40u + (4u << 20))

/* the header's flags */
#define ICO_XB_TABLE 1u     /* the saving build had a symbol table */
#define ICO_XB_OVERFLOW 2u  /* more references than the bounds */
#define ICO_XB_UNNAMED 4u   /* words nothing in the table names: kept as numbers */
#define ICO_XB_LOW_IMAGE 8u /* the saving program's image lies below 4 GB */

/* --- saving ------------------------------------------------------------------- */

/* The saving process (this one, at a save). */
typedef struct IcoXbProc {
    IcoSymTab *tab;      /* NULL: no symbol table (the section says so) */
    IcoStateImage image; /* ico_state_image_range (its read-only data holds the strings) */
    uint64_t game_lo[2], game_hi[2];
    const uint64_t *snap; /* the game data range's words at start-up, or NULL */
    size_t snap_words;
    int any_address; /* tests: a synthetic image below 4 GB is not ICO_XB_LOW_IMAGE */
} IcoXbProc;

typedef struct IcoXbRefs IcoXbRefs;

/* NULL when no memory. */
IcoXbRefs *ico_xb_begin(const IcoXbProc *p);
/* The n bytes at p, a copy of what lies (or lay) at `addr`: every word at
   an 8-byte-aligned address that lies in the image; strict 0 for the dead
   part of a stack (above). */
void ico_xb_scan(IcoXbRefs *r, const void *p, size_t n, uint64_t addr, int strict);
void ico_xb_word(IcoXbRefs *r, uint64_t w, int strict);
/* A registered block: its name, size, flags, address now and offset in
   its packed section (reg or audio, by the AUDIO flag). */
void ico_xb_block(IcoXbRefs *r, const char *name, uint64_t size, unsigned flags, uint64_t addr,
                  uint64_t offset);

typedef struct IcoXbStats {
    unsigned units, positions, data, points, blocks, values;
    unsigned flags;
    unsigned left;          /* the words nothing names (ICO_XB_UNNAMED) */
    uint64_t first_unnamed; /* the first of them */
    size_t bytes;
} IcoXbStats;

/* The section's bytes (malloc'd, *n long) and what it holds.  0, or -1
   (no memory).  r is freed. */
int ico_xb_finish(IcoXbRefs *r, void **out, size_t *n, IcoXbStats *st);

/* --- loading ------------------------------------------------------------------ */

/* This process and the saving one's ranges (from the state's own record). */
typedef struct IcoXbNow {
    IcoSymTab *tab; /* NULL: no symbol table here */
    IcoStateImage image;
    uint64_t game_lo[2], game_hi[2];
    const uint64_t *snap;
    size_t snap_words;
    int n_blocks; /* this build's registry, in its order */
    const char *const *block_name;
    const uint64_t *block_size;
    const unsigned *block_flags;
    const uint64_t *block_addr;
    uint64_t old_game_lo[2], old_game_hi[2]; /* the saving process's game ranges */
} IcoXbNow;

/* what blocks a state (the first blocker found) */
enum {
    ICO_XB_OK = 0,
    ICO_XB_CODE,   /* a function a frame sits in (or a pointer needs) changed or is gone */
    ICO_XB_DATA,   /* the game data layout, a variable or a start-up value changed */
    ICO_XB_BLOCKS, /* a registered block is missing, new or of another size */
    ICO_XB_PINNED, /* nothing to translate with: the state loads only into its own build */
    ICO_XB_BAD     /* the section is damaged */
};

typedef struct IcoXbVerdict {
    int kind;      /* ICO_XB_* */
    char why[200]; /* the log's words for the blocker ("" when ok) */
    /* when ok: the translation (state_reloc.h IcoStateXlate) */
    IcoStateSpan *span;
    size_t n_span;
    IcoStateSpan *point;
    size_t n_point;
    /* per this build's block: its offset in the saved packed section, or
       ICO_XB_BLOCK_ABSENT for a FRESH block the state lacks (it keeps this
       run's bytes) */
    uint64_t *block_off;
    unsigned units, data, points, searches, soft_left;
    unsigned fresh; /* the FRESH blocks the state lacks */
    /* the words the save found nothing to name (kept as numbers), the
       first of them */
    unsigned left;
    uint64_t first_left;
} IcoXbVerdict;

#define ICO_XB_BLOCK_ABSENT UINT64_MAX

/* Checks a saved section against this build and, when nothing blocks it,
   builds the translation.  Returns out->kind. */
int ico_xb_check(const void *sec, size_t n, const IcoXbNow *now, IcoXbVerdict *out);
void ico_xb_verdict_free(IcoXbVerdict *v);

/* the kind's words for a log line: "the code", "the game's data", ... */
const char *ico_xb_kind_text(int kind);

#endif /* ICO_PLATFORM_STATE_XBUILD_H */
