/*
 * port/platform/state_legacy.h
 *
 * A legacy table: what an earlier Android version's save states lack to be
 * read by this one (issue 217).  Versions before v0.11.9 saved format 2
 * records whose program words into read-only data and arm64 pages they
 * could not name (state_xbuild.h); a state of such a version is read here
 * by naming its words again, at the load, with today's rules against that
 * version's program as the table describes it (ico_xb_legacy_*).  Later
 * versions save format 3 records, which name their words themselves, but
 * compare their code under the code hash rules of their day: a table of
 * such a version ("record 3" in its manifest) holds its functions' code
 * hashes under those rules and today's (IcoXbNow.xlate), and its symbol
 * file (for the package gate, tools/state_legacy_table.py gate) alone.  Per
 * version, under port/state_legacy/<name>/ (tools/state_legacy_table.py
 * builds them from the released APK; Android packages them as assets):
 *
 *   syms.gzip     the version's own libmain.syms.gz, as released (not
 *                 ".gz": Android's asset packaging un-gzips a .gz asset and
 *                 drops the extension, which left v0.11.9-rc5's tables
 *                 without their symbol file)
 *   sections.txt  its library's allocated sections ("section" lines)
 *   texts.idx     its .rodata's text runs (every run of C string literals):
 *                 per run the offset from the image start and the length,
 *                 32-bit little-endian pairs, raw deflate; the length's top
 *                 bit (ICO_LEGACY_LSTART) is set when the run starts a
 *                 literal: the byte before it in the version's .rodata is
 *                 zero, or it is the section's first (the table holds the
 *                 runs' bytes only, so the byte before a run is not here)
 *   texts.<k>.z   the runs' bytes in order, raw deflate
 *   hashes.z      every function's code hashes (code_hash.h) as this
 *                 build's table would cut and hash it: per function its
 *                 offset from the image start (32 bits), its hash under
 *                 the version's records' rules and under the table's (64 bits each),
 *                 little-endian, raw deflate.  The first hash is under the
 *                 rules the version's records hold (hash_verify_rules: 1
 *                 for a format 2 record, 2 for v0.11.9 to v0.11.15): every
 *                 unit of a record must match it, which proves the table
 *                 is the saver's build.
 *   manifest.txt  the commit text its states carry, the platform, the kind
 *                 (release, rc), the source APK and its SHA-256, the files,
 *                 "text_index 2" (texts.idx's format: a table without it,
 *                 whose lengths carry no literal starts, is refused) and
 *                 "lstarts N" (the runs that start a literal); a format 3
 *                 version's: "record 3", "hash_verify_rules 2", its
 *                 syms.gzip and hashes.z, no sections or texts
 *
 * and index.txt: one line per table, "platform kind name commit".
 */
#ifndef ICO_PLATFORM_STATE_LEGACY_H
#define ICO_PLATFORM_STATE_LEGACY_H

#include <stddef.h>
#include <stdint.h>

#define ICO_LEGACY_SYMS "syms.gzip"
#define ICO_LEGACY_TEXT_INDEX 2       /* the manifest's text_index */
#define ICO_LEGACY_LSTART 0x80000000u /* a texts.idx length's top bit */

/* Reads the packaged file `path` (relative to the tables' root,
   "index.txt", "v0.11.8-android/syms.gzip"): 0 and *data (malloc'd), *n,
   or -1 with the reason in err (the reader's error text, at most err_n
   bytes with the NUL). */
typedef int (*IcoLegacyRead)(void *user, const char *path, void **data, size_t *n, char *err,
                             size_t err_n);

typedef struct IcoLegacyTable {
    char name[64];
    char commit[96];
    int rc;     /* a release candidate's table */
    char *syms; /* the symbol file's text with the section lines after it */
    size_t syms_n;
    /* the version's .rodata from the image start: [ro_lo, ro_hi), its text
       runs' bytes in place, zeros elsewhere */
    uint8_t *ro;
    uint64_t ro_lo, ro_hi;
    uint32_t *run;   /* n_run pairs: offset from the image start, length */
    uint8_t *lstart; /* n_run: 1 when the run starts a literal (NULL: none does) */
    size_t n_run;
    uint64_t *hash; /* n_hash triples: a function's offset from the image start, its code hash
                       under verify_rules, under hash_rules */
    size_t n_hash;
    int hash_rules;   /* the code hash rules the table's hashes follow */
    int verify_rules; /* the rules of the second column: the version's records' own */
    int record;       /* the version's records' format: 2 (everything above), 3 (the hashes
                         alone: such a record names its own words) */
} IcoLegacyTable;

/* The table of states whose header names `commit` on `platform`: 1 and
   its name, 0 when the index has none, -1 when the index cannot be read. */
int ico_legacy_find(IcoLegacyRead rd, void *user, const char *platform, const char *commit,
                    char *name, size_t n);
/* NULL and why when a file is missing, short or damaged. */
IcoLegacyTable *ico_legacy_load(IcoLegacyRead rd, void *user, const char *name, char *why,
                                size_t why_n);
void ico_legacy_free(IcoLegacyTable *t);

/* The version's symbol table placed at its saving image (env: the image,
   text and game ranges the state's record gives, the architecture; the
   anchor is the table's own ico_diag_init at image_lo plus its address).
   NULL and why. */
struct IcoSymTabEnv;
struct IcoSymTab *ico_legacy_symtab(const IcoLegacyTable *t, const struct IcoSymTabEnv *env,
                                    char *why, size_t why_n);

/* The text run holding the image offset `off`: 1 and [*lo, *hi) (image
   offsets) and *lstart (1 when the run starts a literal: the byte before
   *lo was zero), else 0. */
int ico_legacy_run(const IcoLegacyTable *t, uint64_t off, uint64_t *lo, uint64_t *hi, int *lstart);

#endif /* ICO_PLATFORM_STATE_LEGACY_H */
