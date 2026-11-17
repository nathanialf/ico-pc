/*
 * port/platform/state_syms.h
 *
 * The program's symbol table in memory, for save states that cross
 * versions (state_xbuild.h): the symbol file shipped with the program
 * (symmap.h: a GNU ld link map, lld's ELF link map, or an nm table such as
 * Android's libmain.syms.gz) read once, whole, into sorted tables.  Not the
 * crash reports' streaming reader: this one allocates and is never used
 * inside a signal handler.
 *
 * The file's addresses are placed against the running program by an anchor
 * (a function whose address the caller knows, ico_diag_init) and checked by
 * a second function (ico_diag_log must come out where it is); a file of
 * another build fails the check and is not used.  Every symbol and input
 * section is then sorted by where it lies NOW:
 *
 *   units   the code, in pieces: a function symbol up to the next symbol,
 *           the end of its input section or its own size when the file
 *           gives one; an input section's start with no symbol on it starts
 *           a piece named "<object>:<section>" (a GNU ld map names global
 *           functions only, so a static function is part of the piece of
 *           the global before it, or of its section's first piece).  Each
 *           has its name, its ordinal among the pieces of that name (in
 *           address order) and, computed when first asked, its code hash
 *           (code_hash.h).
 *   data    the named symbols elsewhere in the image (the port's data, the
 *           read-only data), outside the game's data ranges: start, size
 *           (exact when the file gives it), name and ordinal.
 *   layout  the symbols and input sections inside the game's two data
 *           ranges, as (range, offset from the range's start, size, kind,
 *           name; a symbol's own name, without the nm file column's
 *           "<file>:"), sorted: their XXH3-64 is the game's data layout hash.
 *           Two builds with the same hash hold their game globals at the
 *           same offsets.
 *   sections  the program's output sections the file names: a GNU ld map's
 *           and lld's map's own output section lines (code when named
 *           .text, .text.*, .init, .fini or .plt*, symmap.c's rule), and an
 *           nm table's "section <address> <size> <flags> <name>" lines,
 *           which tools/package_android.sh writes from the library's
 *           section headers (code when the ELF flags hold X).  The code
 *           sections bound the CODE RANGE: where the code is, whatever the
 *           loader's segments say (Android's library has one read and
 *           execute segment that holds its read-only data and string
 *           literals with the code, -Wl,--no-rosegment below API 30).
 *           Without a code section the code range is the caller's text
 *           range, as before (no derivation from the symbols: code before
 *           a build's first symbol would be judged data).
 *
 * An nm data line may carry a fifth column, the source file of a local
 * symbol whose name is not unique (tools/package_android.sh, from the
 * symbol table's FILE entries): the data symbol is then named
 * "<file>:<name>", so a static of one file is told from its namesakes.
 */
#ifndef ICO_PLATFORM_STATE_SYMS_H
#define ICO_PLATFORM_STATE_SYMS_H

#include <stddef.h>
#include <stdint.h>

/* Where the running program is, and how the file is placed on it. */
typedef struct IcoSymTabEnv {
    const char *anchor;   /* a function the file names, now at anchor_addr */
    uint64_t anchor_addr; /* (a leading '_' on either side ignored) */
    const char *check;    /* NULL, or a second function that must come out */
    uint64_t check_addr;  /* at check_addr */
    uint64_t image_lo, image_hi;
    uint64_t text_lo, text_hi;
    uint64_t game_lo[2], game_hi[2]; /* the game's data and bss ranges */
    int arch;                        /* code_hash.h ICO_CODE_*: how units are hashed */
} IcoSymTabEnv;

typedef struct IcoSymUnit {
    uint64_t start, size; /* now */
    uint64_t name_hash;   /* XXH3-64 of the name */
    uint64_t code_hash;   /* when hashed */
    uint32_t name;        /* offset of the name in the table's strings */
    uint32_t ordinal;     /* among the units of this name, from 0 in address order */
    uint8_t hashed;
    uint8_t unique;      /* the only unit of its name */
    uint8_t hashed1;     /* code_hash1 made */
    uint64_t code_hash1; /* under code hash rules 1 (a record of those rules) */
} IcoSymUnit;

typedef struct IcoSymData {
    uint64_t start, size;
    uint64_t name_hash;
    uint32_t name, ordinal;
    uint8_t exact; /* the file gave the size (else: to the next item) */
    uint8_t unique;
    /* a symbol the file gave no size: the input section holding it, its
       size (0: none found, or exact) and the symbol's offset in it; what
       its span holds past the symbol (the file's statics, a GNU map naming
       globals only) lies at the same offsets only while these are equal */
    uint64_t sec_size, sec_off;
} IcoSymData;

/* An output section where it lies now. */
typedef struct IcoSymSec {
    uint64_t start, size;
    uint32_t name;
    uint8_t code; /* holds code (above) */
} IcoSymSec;

typedef struct IcoSymTab {
    IcoSymUnit *unit;
    size_t n_unit;
    IcoSymData *data;
    size_t n_data;
    IcoSymSec *sec; /* sorted by start; NULL when the file names none */
    size_t n_sec;
    /* the code range: [code_lo, code_hi) of the code sections inside the
       caller's text range (code_from 1), or the caller's text range itself
       (code_from 0: the file names no code section there).  env.text_lo
       and env.text_hi are set to it: the units lie inside it, and every
       item outside it is data. */
    uint64_t code_lo, code_hi;
    int code_from;
    /* the game data symbols' starts where they lie now, sorted (the layout's
       symbols; for ico_symtab_starts_at) */
    uint64_t *game_start;
    size_t n_game_start;
    size_t n_layout; /* the game data items the layout hash covers */
    uint64_t layout_hash;
    char *str; /* the names, NUL-terminated */
    size_t str_n;
    uint32_t *unit_by_name; /* indices into unit, by (name_hash, name, ordinal) */
    uint32_t *data_by_name;
    IcoSymTabEnv env;
    int64_t bias; /* the running address minus the file's */
} IcoSymTab;

/* The table from the symbol file's text (n bytes, not NUL-terminated).
   NULL with `why` naming the reason (no anchor, another build's file, no
   memory, nothing named in the code). */
IcoSymTab *ico_symtab_parse(const char *text, size_t n, const IcoSymTabEnv *env, char *why,
                            size_t why_n);
void ico_symtab_free(IcoSymTab *t);

/* A gzip member's text (one member, the deflate data inflated whole):
   malloc'd, *out_n bytes, or NULL. */
char *ico_symtab_gunzip(const void *gz, size_t n, size_t *out_n);

/* The unit holding `addr` (start <= addr < start + size), or NULL. */
const IcoSymUnit *ico_symtab_unit_at(const IcoSymTab *t, uint64_t addr);
/* The data symbol holding `addr` (start <= addr <= start + size, the end
   address included as the relocation does, state_reloc.h; a symbol that
   starts there wins), or NULL. */
const IcoSymData *ico_symtab_data_at(const IcoSymTab *t, uint64_t addr);
/* By name hash, name and ordinal; NULL when this build has none. */
const IcoSymUnit *ico_symtab_unit_named(const IcoSymTab *t, uint64_t name_hash, const char *name,
                                        uint32_t ordinal);
const IcoSymData *ico_symtab_data_named(const IcoSymTab *t, uint64_t name_hash, const char *name,
                                        uint32_t ordinal);
/* 1 when a named symbol (a unit, a data symbol or a game data symbol)
   starts exactly at `addr`. */
int ico_symtab_starts_at(const IcoSymTab *t, uint64_t addr);

/* The output section holding `addr`, or NULL. */
const IcoSymSec *ico_symtab_section_at(const IcoSymTab *t, uint64_t addr);
/* The name of the unit or data symbol that starts nearest at or below
   `addr` (*off: addr minus its start), or NULL when none does. */
const char *ico_symtab_nearest(const IcoSymTab *t, uint64_t addr, uint64_t *off);

/* The unit's code hash (computed from the running program's code at the
   first ask, then kept). */
uint64_t ico_symtab_unit_hash(IcoSymTab *t, const IcoSymUnit *u);
/* The same under the given rules (code_hash.h; 1 or ICO_CODE_HASH_RULES). */
uint64_t ico_symtab_unit_hash_rules(IcoSymTab *t, const IcoSymUnit *u, int rules);

static inline const char *ico_symtab_name(const IcoSymTab *t, uint32_t off)
{
    return t->str + off;
}

/* XXH3-64 of a name (what IcoSymUnit.name_hash holds). */
uint64_t ico_symtab_name_hash(const char *name);

#endif /* ICO_PLATFORM_STATE_SYMS_H */
