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
 *           old start and size (exact when the file gave it); in data that
 *           is read-only once loaded (read-only data, .data.rel.ro*,
 *           .rodata*, .rdata*) also one whose name repeats (two statics of
 *           one name in files of one name, a compiler's private table the
 *           symbol file names by its section, "<file>:.Lswitch.table.<fn>"):
 *           by name and ordinal, its exact size and the XXH3-64 of its bytes
 *           with every word that lies in the image masked (XD_MASKED: a
 *           relocated pointer differs in every build and run, its place in
 *           the object does not).  A symbol the
 *           file gave no size (a GNU ld map names globals only) spans to
 *           the next item, so a word past it may point at a static of the
 *           same object: its input section's size and the symbol's offset
 *           in it are recorded and must be the same in the later build.
 *   points  any other word: when it equals one of the game data's values at
 *           start-up (an initialised pointer of a game table, a string or a
 *           function the game copied from one), the offset of that value,
 *           taken in the later build from its own start-up values; else,
 *           when it points into read-only data, its WINDOW: bytes around it
 *           and its offset in them, found again in the later build's
 *           read-only data (format 3; below).
 *
 * The code and the read-only data are where the symbol file's sections say
 * (ico_xb_image, state_syms.h's code range), not where the loader's segment
 * flags put them: Android's library maps .rodata and its string literals
 * in its one executable segment (issue 217).  A word in the code is a
 * unit's or nothing's; a word in read-only data that no unique variable
 * holds goes on to the start-up values and the windows.
 *
 * A window.  A text (the bytes from the word to the first NUL inside the
 * read-only range, none of them a control character but tab, newline,
 * carriage return and escape: a C string literal in any encoding, EUC-JP,
 * UTF-8 or Korean included) starts as its whole content, any length; any
 * other word starts with the 8 bytes either side of it (only where the
 * image's upper half is above 1: under Windows' fixed base a float next to
 * the integer 1 lands in read-only data as well, and only a text of at
 * least one character is taken for an address there, as before; the rest
 * is kept as a number, ICO_XB_WHY_NUMBER).  The save grows the
 * window, doubling both ways inside its read-only range, until its bytes
 * occur exactly once in the saving image's read-only data, and records the
 * bytes whole while they fit the record's pool (ICO_XB_POOL_BYTES less the
 * room every point's prefix may need), else their length, XXH3-64 and first
 * ICO_XB_CONTENT bytes.  A load finds every place in its own read-only data
 * that holds them (by the prefix and the hash for the second form): one
 * place translates the word to it plus the offset; none refuses ("the text
 * ... is missing here"), more than one refuses ("... is in more than one
 * place here"): a load never picks one.  The window holds the bytes before
 * the word too: the word may name an object starting at it or one ending at
 * it (C's end pointer), and only the same bytes on both sides here prove
 * one translation serves both.  The linker merges and orders the program's
 * anonymous constants (.rodata.cst8, .rodata.cst16) anew in every link, so
 * a word at the start of one (most likely a stale page value in a live
 * frame: 0x294000 in the owner's v0.11.15-rc6 state) finds no window in a
 * later build.  Only then a second attempt (the owner's choice, by
 * content, NOT proven): the window's bytes from the word on (at least 8)
 * found exactly once in this build's read-only data place the word there,
 * when they are not one repeated byte (fill: zeros, 0xff, padding, whose
 * place says nothing of an object) and the word is no page word of the
 * record (a page an ADRP of a function with a live frame makes: the page
 * rule decides those, never this).  Each such placement is counted and
 * logged (IcoXbVerdict.fwd); an 8-byte coincidence could misplace a live
 * pointer, which would show as wrong behaviour after the resume.  Else the
 * refusal says where the save found the word and whether its bytes from the
 * word on lie here once (or twice).  The points do not stop at the first
 * that blocks: every one is counted and named (IcoXbVerdict.more).
 *
 * Page words (arm64).  clang keeps an ADRP's page address in a
 * callee-saved register across calls and adds a variable's offset in its
 * page (the next instruction's low 12 bits) to it; a suspended fiber's saved
 * registers and live stack hold several.  The later build's code adds its
 * own offsets, so the word must become the page the later build's ADRP
 * makes, which a range's or a symbol's move does not give.  C forms a page
 * address no other way, and a callee-saved register's value was set by a
 * function still on that fiber's stack (a returned function restored it),
 * so the save takes, per fiber (ico_xb_frames), the units its return
 * addresses lie in (positions: the code hash, code_hash.h, requires them
 * alike, ADRP immediates and the low 12 bits after them masked, so every
 * instruction keeps its offset) and decodes their ADRPs: each one whose page
 * is a page-aligned word of that fiber outside the code is a PRODUCER
 * (unit, offset, page).  The load decodes the same offsets in its units:
 * the producers of one word on one fiber must make one page, which the word
 * becomes ("... which ADRPs in f+0x.. and g+0x.. make: here they make two
 * pages ..." refuses otherwise: no frame tells which one set the register
 * without unwinding).  A page-aligned word with no producer on its fiber is
 * an ordinary address (in the owner's state: an element of texTable and of
 * stageAnimTable, sgHeadContext's start), named by the ordinary rules and
 * not recorded as a page word.  A word that
 * is also an ordinary address (seen outside the fibers' frames, or a symbol,
 * block or start-up value starts at it) keeps both readings and must get
 * the same from both.  Every reading of one word must agree.  Bounds
 * ICO_XB_MAX_PAGES words, ICO_XB_MAX_PRODS producers (more pins the state:
 * ICO_XB_OVERFLOW).  Other architectures record none.
 *
 * A word only these confirm is translated.  A word in the code that no
 * unit holds, or in the image that nothing above names, is confirmed by
 * nothing (ICO_XB_UNNAMED, counted, the first one recorded, and in format
 * 3 the first ICO_XB_LIST of them with where they were found, their
 * section and their nearest symbol, ico_xb_where).  Where the
 * saving image's addresses have an upper half of 0 or 1 such words are
 * ordinary data (under Windows' fixed base 0x140000000 any float from 2.0
 * to about 572 followed by the integer 1 lies in the image), so they are
 * kept as the numbers they are: the load has no span or point for them.
 * Above that (Android's library at a randomised 64-bit base) no float
 * next to a small integer reaches the image, so such a word is an address
 * the table does not name and the state is refused, pinned to its build.
 * A state is pinned to its
 * build by more references than the bounds (ICO_XB_OVERFLOW), a saving
 * build without a symbol file (no ICO_XB_TABLE) and a program image below
 * 4 GB (ICO_XB_LOW_IMAGE: there a program address cannot be told from a
 * number, such as the PS2 addresses the game keeps, which then land on
 * named code and data too, so translating every word that looks like one
 * is not safe).  The same build never reads this section: its build id
 * pins the one-delta path.
 *
 * The section (little-endian): a 64-byte header (80 in version 3: the
 * pool's size and the list's count after it), then the units (48 bytes
 * each), data (48; 40 in a version 1 record, without the input section),
 * points (48; 64 in version 3, a window's length, offset, hash, pool place
 * and nearest symbol, and since rules 3 where the save found the word: its
 * region's name at 44 and its address at 56, zeros before) and blocks (40) records, in version 3 the list of
 * words nothing names (40 each), then the names, and in version 3 the pool
 * of the windows' bytes.  The header's word at 60 counts the words nothing
 * names (0 in a record from before it was counted).  Versions 1 and 2 are
 * read as they were written (a version 2 text point holds at most 24
 * bytes, found at the first place that holds them).
 */
#ifndef ICO_PLATFORM_STATE_XBUILD_H
#define ICO_PLATFORM_STATE_XBUILD_H

#include <stddef.h>
#include <stdint.h>

#include "state_legacy.h"
#include "state_reloc.h"
#include "state_syms.h"

#define ICO_XB_MAGIC 0x534D5953u /* "SYMS" */
#define ICO_XB_VERSION 3         /* 1 and 2 are read too (above) */
#define ICO_XB_MAX_UNITS 16384
#define ICO_XB_MAX_DATA 4096
#define ICO_XB_MAX_POINTS 4096
#define ICO_XB_MAX_BLOCKS 128
#define ICO_XB_MAX_VALUES 262144 /* distinct words looked at (a power of two) */
#define ICO_XB_MAX_SEARCHES 512  /* content points found again at a load */
#define ICO_XB_CONTENT 24 /* a version 2 text point's bytes; a version 3 hashed window's prefix */
#define ICO_XB_LIST 16    /* words nothing names, listed with where they are */
#define ICO_XB_MAX_PAGES 4096        /* arm64 page words (ADRP's) a state holds */
#define ICO_XB_MAX_PRODS 16384       /* the ADRPs that make them */
#define ICO_XB_POOL_BYTES (4u << 20) /* the windows' bytes at most */
/* the section's size at most (a load's bound before it inflates it): the
   records, 4 MB of names and the pool */
#define ICO_XB_MAX_BYTES                                                                           \
    (80u + ICO_XB_MAX_UNITS * 48u + ICO_XB_MAX_DATA * 48u + ICO_XB_MAX_POINTS * 64u +              \
     ICO_XB_MAX_BLOCKS * 40u + ICO_XB_LIST * 40u + ICO_XB_MAX_PAGES * 16u +                        \
     ICO_XB_MAX_PRODS * 24u + (4u << 20) + ICO_XB_POOL_BYTES)

/* the header's flags */
#define ICO_XB_TABLE 1u     /* the saving build had a symbol table */
#define ICO_XB_OVERFLOW 2u  /* more references than the bounds */
#define ICO_XB_UNNAMED 4u   /* words nothing in the table names: kept as numbers */
#define ICO_XB_LOW_IMAGE 8u /* the saving program's image lies below 4 GB */
#define ICO_XB_PAGES 16u    /* arm64: the fibers' page words were recorded (state_xbuild.c) */

/* why a word is named by nothing (IcoXbUnnamed.reason) */
enum {
    ICO_XB_WHY_CODE = 1, /* in the code, in no unit */
    ICO_XB_WHY_DATA,     /* in writable data no block or unique variable holds */
    ICO_XB_WHY_READONLY, /* in read-only data whose window is not unique even whole */
    ICO_XB_WHY_IMAGE,    /* in the image outside every section the file names */
    ICO_XB_WHY_NUMBER,   /* in read-only data, no text (or ""), under an upper half of 0 or 1 */
    ICO_XB_WHY_NOBYTES,  /* in read-only data the version's legacy table keeps no bytes of */
    ICO_XB_WHY_NOHASH,   /* a function with a thread inside whose code hash its record lacks */
    ICO_XB_WHY_PAGE      /* a page a frame's ADRP of a target this build cannot place may make */
};

/* A word named by nothing, as the save found it. */
typedef struct IcoXbUnnamed {
    uint64_t word;
    uint64_t at;      /* where it was found (its address then; 0: not known) */
    uint64_t sym_off; /* word minus the nearest symbol's start */
    unsigned reason;  /* ICO_XB_WHY_* */
    char where[48];   /* what held it (ico_xb_where; "" not known) */
    char section[32]; /* the section holding the word ("" none named) */
    char sym[80];     /* the nearest symbol at or below it ("" none) */
} IcoXbUnnamed;

/* the reason's words for a log line */
const char *ico_xb_why_text(unsigned reason);

/* The image as the symbol table places it: [text_lo, text_hi) narrowed to
   the table's code range (state_syms.h) when the file names its code
   sections inside it, the rest of the loader's code range added as
   read-only data.  Unchanged without a table or code sections. */
void ico_xb_image(const IcoSymTab *t, IcoStateImage *im);

/* --- saving ------------------------------------------------------------------- */

/* A format 2 record of an earlier version read again (state_legacy.h): the
   save's rules run at the load against that version's program as its
   legacy table describes it.  IcoXbProc.tab is then that version's table
   (ico_legacy_symtab), image its ranges from the state's own record, game
   its game ranges, snap NULL. */
typedef struct IcoXbLegacy {
    const IcoLegacyTable *lt;
    uint64_t old_lo; /* the saving image's start */
    const void *v2;  /* the record the version saved: its units' code hashes */
    size_t v2_n;
    IcoSymTab *now_tab; /* this build's table: what the page words' ADRPs aim at */
    uint64_t now_game_lo[2], now_game_hi[2];
    /* this build's game data words at start-up (IcoXbProc.snap), or NULL:
       the version's are not in its state; each one that points into this
       image and that the version's table places back by name stands for
       the version's start-up word at the same offset (one layout) */
    const uint64_t *now_snap;
    size_t now_snap_words;
} IcoXbLegacy;

/* The saving process (this one, at a save). */
typedef struct IcoXbProc {
    IcoSymTab *tab;      /* NULL: no symbol table (the section says so) */
    IcoStateImage image; /* ico_state_image_range (its read-only data holds the strings) */
    uint64_t game_lo[2], game_hi[2];
    const uint64_t *snap; /* the game data range's words at start-up, or NULL */
    size_t snap_words;
    int any_address; /* tests: a synthetic image below 4 GB is not ICO_XB_LOW_IMAGE */
    /* tests: the image start the number rule judges by (0: image.lo; a
       synthetic image on the heap stands for Windows' 0x140000000) */
    uint64_t judge_lo;
    const IcoXbLegacy *legacy; /* NULL, or the version whose record is read again */
} IcoXbProc;

typedef struct IcoXbRefs IcoXbRefs;

/* NULL when no memory. */
IcoXbRefs *ico_xb_begin(const IcoXbProc *p);
/* The n bytes at p, a copy of what lies (or lay) at `addr`: every word at
   an 8-byte-aligned address that lies in the image; strict 0 for the dead
   part of a stack (above). */
void ico_xb_scan(IcoXbRefs *r, const void *p, size_t n, uint64_t addr, int strict);
void ico_xb_word(IcoXbRefs *r, uint64_t w, int strict);
/* What the next scans read (a block's name, "the arena", "fiber 5's
   stack"): kept with each word's first sighting for the list of words
   nothing names (at most 1024 such texts; later ones are not kept). */
void ico_xb_where(IcoXbRefs *r, const char *what);
/* The next scans read a suspended fiber's saved registers and live stack:
   `fiber` 1 and up names the fiber (state_host.c: its slot + 1), 0 ends
   them (the page words, above). */
void ico_xb_frames(IcoXbRefs *r, int fiber);
/* A registered block: its name, size, flags, address now and offset in
   its packed section (reg or audio, by the AUDIO flag). */
void ico_xb_block(IcoXbRefs *r, const char *name, uint64_t size, unsigned flags, uint64_t addr,
                  uint64_t offset);

typedef struct IcoXbStats {
    unsigned units, positions, data, points, blocks, values;
    unsigned windows;     /* of the points: windows into read-only data */
    unsigned data_masked; /* of the data: of a repeated name, by ordinal and contents */
    unsigned pages;       /* arm64 page words recorded (one per word and fiber) */
    unsigned producers;   /* the ADRPs that make them */
    unsigned texts;       /* of the windows: texts */
    unsigned flags;
    unsigned left;          /* the words nothing names (ICO_XB_UNNAMED) */
    uint64_t first_unnamed; /* the first of them */
    unsigned listed;        /* the first ICO_XB_LIST of them */
    IcoXbUnnamed list[ICO_XB_LIST];
    int code_from; /* the table's code range from its sections (1) or the loader (0) */
    size_t bytes;
} IcoXbStats;

/* The section's bytes (malloc'd, *n long) and what it holds.  0, or -1
   (no memory).  r is freed. */
int ico_xb_finish(IcoXbRefs *r, void **out, size_t *n, IcoXbStats *st);

/* An earlier version's format 2 record against that version's legacy
   table: 0, or -1 and why (the table is another version's: another layout
   hash, a unit the record names that the table does not hold at its place,
   or one whose code hash is not the table's for that function; the record
   is no format 2 arm64 one, or its rules are not verify_rules).  hash: the
   table's n_hash triples (state_legacy.h: offset from the image start, the
   hash under verify_rules (the record's own, 1), under the table's rules). */
int ico_xb_legacy_verify(const void *v2, size_t n, const IcoSymTab *old_tab, uint64_t old_lo,
                         const uint64_t *hash, size_t n_hash, int verify_rules, char *why,
                         size_t why_n);
/* Each block a record holds (any format): its name, size, flags, old
   address and offset in its packed section.  0, or -1 (damaged). */
typedef void (*IcoXbBlockFn)(void *user, const char *name, uint64_t size, unsigned flags,
                             uint64_t addr, uint64_t offset);
int ico_xb_record_blocks(const void *sec, size_t n, IcoXbBlockFn fn, void *user);

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
    /* the saving process's image start (0: not known, the first word
       nothing names stands for it) */
    uint64_t old_image_lo;
    /* a format 3 record of earlier code hash rules (an Android version
       before this build's rules): its build's legacy table's hashes
       (state_legacy.h IcoLegacyTable.hash, n_xlate triples: offset from the
       image start, the hash under xlate_rules, under the current rules), or
       NULL.  When every function of the record has its row there with the
       record's own hash (so the table is the saving build's), the check
       compares the table's current-rules hash with this build's; else the
       record's rules with this build's code, as without it. */
    const uint64_t *xlate;
    size_t n_xlate;
    int xlate_rules;
    const char *xlate_name; /* the table's name, for the log */
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

/* a read-only word placed by its forward bytes alone (ico_xb_check: its
   two-sided window found nowhere here; not proven, logged) */
#define ICO_XB_FORWARD_LOG 16

typedef struct IcoXbForward {
    uint64_t old, now; /* the word in the saving build, its place here */
    char old_sym[48];  /* the symbol nearest at or below old there ("" none) */
    uint64_t old_off;  /* old minus that symbol */
    char now_sym[48];  /* the same here */
    uint64_t now_off;
    char where[48];         /* where the save found the word ("" not recorded) */
    uint64_t where_at;      /* the word's address there */
    unsigned char bytes[8]; /* the first forward bytes (n_bytes of them shown) */
    unsigned n_bytes;       /* how many forward bytes matched in all */
} IcoXbForward;

/* a page word whose producers make two pages here, taken from the ones
   whose page can outlive their own uses (ico_xb_check; proven, logged) */
#define ICO_XB_PAGE_LOG 16

typedef struct IcoXbPageRes {
    uint32_t fiber;    /* the fiber's slot */
    uint64_t old, now; /* the page in the saving build, here */
    char by[64];       /* the ADRP it is taken from (function+offset) */
    char all[160];     /* every producer, those whose page dies in place marked */
} IcoXbPageRes;

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
    unsigned version;     /* the record's */
    int by_unnamed;       /* refused (ICO_XB_PINNED) for the words nothing names */
    unsigned windows;     /* the points that were windows */
    unsigned data_masked; /* the data matched by ordinal and contents */
    unsigned pages;       /* page words translated by their producers */
    /* IcoXbNow.xlate: 1 when its table compared the record's functions
       under the current rules, -1 when it was given but refused (why in
       xlate_why), 0 when none was given */
    int xlated;
    char xlate_why[120];
    /* a refusal for a changed function (ICO_XB_CODE): 1 when it was
       compared under rules that hash an erratum patch site as a branch and
       this build's copy holds one (the difference may be the linker's
       alone), 2 when compared under the current rules (the instructions
       differ: a patch site compares as the load it stands for), 0 else */
    int code_patch;
    /* the words placed by their forward bytes alone (all counted; the first
       ICO_XB_FORWARD_LOG kept for the log) */
    unsigned forward;
    IcoXbForward fwd[ICO_XB_FORWARD_LOG];
    /* the points that blocked it: their count, the first in why, the others
       (as many as fit) in more */
    unsigned blocked;
    char more[480];
    /* page words resolved among producers that make two pages here */
    unsigned page_resolved;
    IcoXbPageRes pres[ICO_XB_PAGE_LOG];
    /* text windows of whole literals found at several literal starts, all
       the same bytes: the first in address order taken (state_xbuild.c) */
    unsigned twins;
    /* their points per fiber slot (state_reloc.h IcoStateXlate.fiber_point:
       the translation's own lists; freed by ico_xb_verdict_free) */
    IcoStateSpan **fiber_point;
    size_t *fiber_n_point;
    size_t n_fiber;
    /* a version 3 record's list of those words (filled before any check,
       so a refusal has it too) */
    unsigned listed;
    IcoXbUnnamed list[ICO_XB_LIST];
} IcoXbVerdict;

#define ICO_XB_BLOCK_ABSENT UINT64_MAX

/* Checks a saved section against this build and, when nothing blocks it,
   builds the translation.  Returns out->kind. */
int ico_xb_check(const void *sec, size_t n, const IcoXbNow *now, IcoXbVerdict *out);
void ico_xb_verdict_free(IcoXbVerdict *v);

/* the kind's words for a log line: "the code", "the game's data", ... */
const char *ico_xb_kind_text(int kind);

#endif /* ICO_PLATFORM_STATE_XBUILD_H */
