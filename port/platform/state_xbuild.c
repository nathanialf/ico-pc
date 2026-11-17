/*
 * port/platform/state_xbuild.c
 *
 * The "syms" section of a save state (state_xbuild.h): the save's
 * collection of every word that points into the program and what it points
 * into, the section's bytes, and a later build's check that builds the
 * translation or names what blocks it.
 */
#include "state_xbuild.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_hash.h"
#include "state_registry.h"
#include "xxh3.h"

/* --- the records ------------------------------------------------------------------ */

enum {
    H_MAGIC = 0,
    H_VERSION = 4,
    H_FLAGS = 8,
    H_RULES = 12,
    H_ARCH = 16,
    H_UNITS = 20,
    H_DATA = 24,
    H_POINTS = 28,
    H_BLOCKS = 32,
    H_STR = 36,
    H_LAYOUT = 40,
    H_UNNAMED = 48,
    H_VALUES = 56,
    H_LEFT = 60,
    H_BYTES = 64,
    /* version 3 */
    H_POOL = 64,  /* the windows' pool's bytes */
    H_LIST = 68,  /* the listed words nothing names */
    H_PAGES = 72, /* the page words (arm64) */
    H_PRODS = 76, /* their producers */
    H_BYTES3 = 80
};

#define UNIT_BYTES 48u
#define DATA_BYTES 48u /* 40 in a version 1 record: no input section */
#define DATA_BYTES_V1 40u
#define POINT_BYTES_V2 48u
#define POINT_BYTES 64u /* version 3 */
#define BLOCK_BYTES 40u
#define LIST_BYTES 40u
#define PAGE_BYTES 16u /* a page word: the word, its fiber, PG_* */
#define PROD_BYTES 24u /* a producer: its unit's old start, the page, the offset, the fiber */
/* a page word's flags */
#define PG_PROD 1u /* an ADRP of a function on its fiber's stack makes it */
#define PG_ORDINARY                                                                                \
    2u /* also an ordinary address: seen outside fibers' frames, a symbol,
                          block or start-up value at it */

/* a unit record's flags */
#define XU_START 1u    /* a strict word at its start (a function pointer) */
#define XU_POSITION 2u /* a strict word inside it (a return address) */
#define XU_SOFT 4u     /* a word of a dead stack frame */
#define XU_UNIQUE 8u   /* its name is unique in the saving build */
/* a data record's flags */
#define XD_EXACT 1u
#define XD_SECTION 2u /* the input section holding it: its size and the symbol's offset */
#define XD_MASKED                                                                                  \
    4u /* version 3: one of a repeated name in data read-only after loading,
                         told apart by its masked contents' hash (at 40) and its ordinal */
/* a point's kinds */
#define XP_SNAP 1u   /* aux: the byte offset of the value in the game's data range */
#define XP_TEXT 2u   /* version 2: content, a C string of at most ICO_XB_CONTENT bytes */
#define XP_WINDOW 3u /* version 3: a window of read-only data (state_xbuild.h) */
/* a window's flags */
#define XW_TEXT 1u   /* a text */
#define XW_HASHED 2u /* the pool holds its prefix only: found by the hash */
#define XW_LSTART                                                                                  \
    4u /* a text that starts a literal (a NUL before it): found only where one starts */
/* the labels ico_xb_where keeps */
#define WHERE_MAX 1024
#define WHERE_BYTES 48

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* a byte run of a growable buffer: an empty one has a NULL pointer, and
 * memcpy's source is declared non-null (CI's UBSan on state_port) */
static void put_bytes(unsigned char *p, const void *src, size_t n)
{
    if (n != 0) {
        memcpy(p, src, n);
    }
}

static void put64(unsigned char *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const unsigned char *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

/* --- saving ------------------------------------------------------------------------ */

typedef struct SnapPair {
    uint64_t value;
    uint64_t off; /* bytes from the game data range's start */
} SnapPair;

typedef struct FrameWord {
    uint64_t w;
    uint32_t fiber;
    uint32_t kind; /* 0 in the code (a return address), 1 a page-aligned word outside it */
} FrameWord;

typedef struct BlockRec {
    const char *name;
    uint64_t size, addr, offset;
    unsigned flags;
} BlockRec;

struct IcoXbRefs {
    IcoXbProc p;   /* its image as the table places it (ico_xb_image) */
    uint64_t *key; /* open addressing over ICO_XB_MAX_VALUES slots; 0 empty */
    uint8_t *strict;
    uint16_t *lab; /* each word's first sighting: its label and address */
    uint64_t *at;
    char (*labels)[WHERE_BYTES];
    unsigned n_labels;
    uint16_t cur_label;
    uint64_t cur_at;
    uint32_t fiber; /* the scans read this fiber's registers and stack (ico_xb_frames; 0 none) */
    int a64;        /* the table's code is arm64: page words (state_xbuild.h) */
    FrameWord *fw;  /* each fiber's return addresses and page-aligned words */
    size_t n_fw, cap_fw;
    int fw_failed;
    size_t n_vals;
    int overflow;
    BlockRec block[ICO_XB_MAX_BLOCKS];
    int n_blocks;
    SnapPair *snap;
    size_t n_snap;
    /* a legacy read (IcoXbProc.legacy): where the version's read-only bytes
       lie (its addresses plus delta) and its record's units' code hashes,
       by old start */
    int64_t delta;
    uint64_t *uh; /* n_uh pairs: start, hash; sorted */
    size_t n_uh;
};

static int snap_cmp(const void *a, const void *b)
{
    const SnapPair *x = (const SnapPair *)a, *y = (const SnapPair *)b;

    if (x->value != y->value) {
        return x->value < y->value ? -1 : 1;
    }
    return x->off < y->off ? -1 : x->off > y->off;
}

static int in_image(const IcoXbProc *p, uint64_t w)
{
    return w >= p->image.lo && w <= p->image.hi;
}

static int uh_cmp(const void *a, const void *b)
{
    const uint64_t x = ((const uint64_t *)a)[0], y = ((const uint64_t *)b)[0];

    return x < y ? -1 : x > y;
}

/* a legacy read: the version's read-only data is its text runs' copy (the
   only read-only range), and the table's code hashes of the version's
   functions (rules ICO_CODE_HASH_RULES, tools/state_legacy_table.py) stand
   for the code this process cannot read */
static int legacy_begin(IcoXbRefs *r)
{
    const IcoXbLegacy *lg = r->p.legacy;
    size_t i;

    r->p.image.n_ro = 1;
    r->p.image.ro_lo[0] = lg->old_lo + lg->lt->ro_lo;
    r->p.image.ro_hi[0] = lg->old_lo + lg->lt->ro_hi;
    r->delta = (int64_t)((uint64_t)(uintptr_t)lg->lt->ro - r->p.image.ro_lo[0]);
    r->uh = (uint64_t *)malloc((lg->lt->n_hash ? lg->lt->n_hash : 1) * 2 * sizeof *r->uh);
    if (r->uh == NULL) {
        return -1;
    }
    if (lg->lt->hash_rules != ICO_CODE_HASH_RULES) {
        return -1;
    }
    for (i = 0; i < lg->lt->n_hash; i++) {
        r->uh[2 * i] = lg->old_lo + lg->lt->hash[3 * i];
        r->uh[2 * i + 1] = lg->lt->hash[3 * i + 2];
    }
    r->n_uh = lg->lt->n_hash;
    if (r->n_uh > 1) {
        qsort(r->uh, r->n_uh, 2 * sizeof *r->uh, uh_cmp);
    }
    return 0;
}

/* a unit's code hash: this process's code, or for a legacy read the
   record's (*missing set when it holds none) */
static uint64_t unit_hash(const IcoXbRefs *r, IcoSymTab *t, const IcoSymUnit *u, int *missing)
{
    size_t lo = 0, hi = r->n_uh;

    if (r->p.legacy == NULL) {
        return ico_symtab_unit_hash(t, u);
    }
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (r->uh[2 * mid] < u->start) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < r->n_uh && r->uh[2 * lo] == u->start) {
        return r->uh[2 * lo + 1];
    }
    *missing = 1;
    return 0;
}

static int old_of(const IcoXbRefs *r, const IcoSymTab *ot, uint64_t T, uint64_t *old);

/* a legacy read's start-up words: this build's, each placed back in the
   version by name (old_of).  The pairing is the name rule's own, so a word
   it names translates to what its name would give; it names a word no
   unique name does (a repeated name's data, which a legacy read cannot
   tell by contents).  A start-up word that cannot be placed back is left
   out (a word only it could name stays unnamed: refused). */
static void legacy_snap(IcoXbRefs *r)
{
    const IcoXbLegacy *lg = r->p.legacy;
    size_t i;

    if (lg->now_snap == NULL || lg->now_snap_words == 0 || lg->now_tab == NULL ||
        r->p.tab == NULL) {
        return;
    }
    r->snap = (SnapPair *)malloc(lg->now_snap_words * sizeof *r->snap);
    if (r->snap == NULL) {
        return;
    }
    for (i = 0; i < lg->now_snap_words; i++) {
        const uint64_t T = lg->now_snap[i];
        uint64_t old;

        if (T != 0 && ico_symtab_unit_at(lg->now_tab, T) == NULL &&
            ico_symtab_data_at(lg->now_tab, T) != NULL && old_of(r, r->p.tab, T, &old) &&
            in_image(&r->p, old)) {
            r->snap[r->n_snap].value = old;
            r->snap[r->n_snap].off = (uint64_t)i * 8u;
            r->n_snap++;
        }
    }
    if (r->n_snap > 1) {
        qsort(r->snap, r->n_snap, sizeof *r->snap, snap_cmp);
    }
}

IcoXbRefs *ico_xb_begin(const IcoXbProc *p)
{
    IcoXbRefs *r = (IcoXbRefs *)calloc(1, sizeof *r);
    size_t i;

    if (r == NULL) {
        return NULL;
    }
    r->p = *p;
    ico_xb_image(p->tab, &r->p.image);
    if (p->legacy != NULL && legacy_begin(r) != 0) {
        free(r);
        return NULL;
    }
    r->key = (uint64_t *)calloc(ICO_XB_MAX_VALUES, sizeof *r->key);
    r->strict = (uint8_t *)calloc(ICO_XB_MAX_VALUES, 1);
    r->lab = (uint16_t *)calloc(ICO_XB_MAX_VALUES, sizeof *r->lab);
    r->at = (uint64_t *)calloc(ICO_XB_MAX_VALUES, sizeof *r->at);
    r->labels = (char (*)[WHERE_BYTES])calloc(WHERE_MAX, WHERE_BYTES);
    if (r->key == NULL || r->strict == NULL || r->lab == NULL || r->at == NULL ||
        r->labels == NULL) {
        free(r->key);
        free(r->strict);
        free(r->lab);
        free(r->at);
        free(r->labels);
        free(r);
        return NULL;
    }
    r->n_labels = 1; /* 0: not known */
    r->a64 = p->tab != NULL && p->tab->env.arch == ICO_CODE_A64;
    /* the game data's start-up words that point into the image, by value
       (the first offset of each value wins) */
    if (p->legacy != NULL) {
        legacy_snap(r);
    } else if (p->snap != NULL && p->snap_words > 0) {
        r->snap = (SnapPair *)malloc(p->snap_words * sizeof *r->snap);
        if (r->snap != NULL) {
            for (i = 0; i < p->snap_words; i++) {
                if (p->snap[i] != 0 && in_image(p, p->snap[i])) {
                    r->snap[r->n_snap].value = p->snap[i];
                    r->snap[r->n_snap].off = (uint64_t)i * 8u;
                    r->n_snap++;
                }
            }
            if (r->n_snap > 1) { /* an empty list's array is NULL */
                qsort(r->snap, r->n_snap, sizeof *r->snap, snap_cmp);
            }
        }
    }
    return r;
}

static size_t slot_of(uint64_t w)
{
    uint64_t h = w * 0x9E3779B97F4A7C15ull;

    return (size_t)(h >> 46) & (ICO_XB_MAX_VALUES - 1);
}

void ico_xb_word(IcoXbRefs *r, uint64_t w, int strict)
{
    size_t s;

    if (w == 0 || !in_image(&r->p, w)) {
        return;
    }
    s = slot_of(w);
    while (r->key[s] != 0 && r->key[s] != w) {
        s = (s + 1) & (ICO_XB_MAX_VALUES - 1);
    }
    if (r->key[s] == 0) {
        if (r->n_vals >= ICO_XB_MAX_VALUES / 4 * 3) {
            r->overflow = 1;
            return;
        }
        r->key[s] = w;
        r->lab[s] = r->cur_label;
        r->at[s] = r->cur_at;
        r->n_vals++;
    }
    if (strict) {
        /* 1 strict, 2 in a fiber's frames, 4 outside them */
        r->strict[s] |= (uint8_t)(1u | (r->fiber != 0 ? 2u : 4u));
        if (r->fiber != 0 && r->a64) {
            const int code = w >= r->p.image.text_lo && w < r->p.image.text_hi;

            if (code || (w & 0xFFFu) == 0) {
                if (r->n_fw == r->cap_fw) {
                    const size_t cap = r->cap_fw ? r->cap_fw * 2 : 1024;
                    FrameWord *q = (FrameWord *)realloc(r->fw, cap * sizeof *q);

                    if (q == NULL) {
                        r->fw_failed = 1;
                        return;
                    }
                    r->fw = q;
                    r->cap_fw = cap;
                }
                r->fw[r->n_fw].w = w;
                r->fw[r->n_fw].fiber = r->fiber;
                r->fw[r->n_fw].kind = code ? 0u : 1u;
                r->n_fw++;
            }
        }
    }
}

void ico_xb_frames(IcoXbRefs *r, int fiber)
{
    r->fiber = fiber > 0 ? (uint32_t)fiber : 0u;
}

void ico_xb_where(IcoXbRefs *r, const char *what)
{
    if (r->n_labels >= WHERE_MAX) {
        r->cur_label = 0;
        return;
    }
    snprintf(r->labels[r->n_labels], WHERE_BYTES, "%s", what);
    r->cur_label = (uint16_t)r->n_labels++;
}

void ico_xb_scan(IcoXbRefs *r, const void *p, size_t n, uint64_t addr, int strict)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t at = (size_t)(((addr + 7u) & ~(uint64_t)7u) - addr);

    if (p == NULL) {
        return;
    }
    for (; at + 8 <= n; at += 8) {
        uint64_t w;

        memcpy(&w, b + at, sizeof w);
        if (w >= r->p.image.lo && w <= r->p.image.hi) {
            r->cur_at = addr + at;
            ico_xb_word(r, w, strict);
        }
    }
    r->cur_at = 0;
}

void ico_xb_block(IcoXbRefs *r, const char *name, uint64_t size, unsigned flags, uint64_t addr,
                  uint64_t offset)
{
    BlockRec *b;

    if (r->n_blocks == ICO_XB_MAX_BLOCKS) {
        r->overflow = 1;
        return;
    }
    b = &r->block[r->n_blocks++];
    b->name = name;
    b->size = size;
    b->flags = flags;
    b->addr = addr;
    b->offset = offset;
}

static int in_game(const uint64_t lo[2], const uint64_t hi[2], uint64_t w)
{
    return (lo[0] < hi[0] && w >= lo[0] && w <= hi[0]) ||
           (lo[1] < hi[1] && w >= lo[1] && w <= hi[1]);
}

/* --- read-only data and its windows ------------------------------------------------- */

/* the read-only range holding w: 1 and the range (a range's end is the
   next section's start, Android's .text after its read-only data, so a
   word there is not the range's) */
static int ro_range(const IcoStateImage *im, uint64_t w, uint64_t *lo, uint64_t *hi)
{
    int i;

    for (i = 0; i < im->n_ro; i++) {
        if (w >= im->ro_lo[i] && w < im->ro_hi[i]) {
            *lo = im->ro_lo[i];
            *hi = im->ro_hi[i];
            return 1;
        }
    }
    return 0;
}

/* the text at w: the bytes up to the first NUL before hi, none of them a
   control character but tab, newline, carriage return and escape (a C
   string literal in any encoding); its length with the NUL, or 0 */
static size_t text_len(uint64_t w, uint64_t hi, int64_t delta)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)(w + (uint64_t)delta);
    const size_t n = (size_t)(hi - w);
    size_t i;

    for (i = 0; i < n; i++) {
        const unsigned char c = p[i];

        if (c == 0) {
            return i + 1;
        }
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != 0x1b) {
            return 0;
        }
    }
    return 0;
}

/* each byte value's count over the image's read-only data */
static void ro_freq(const IcoStateImage *im, int64_t delta, uint32_t freq[256])
{
    int i;

    memset(freq, 0, 256 * sizeof *freq);
    for (i = 0; i < im->n_ro; i++) {
        const unsigned char *p = (const unsigned char *)(uintptr_t)(im->ro_lo[i] + (uint64_t)delta);
        const unsigned char *end =
            (const unsigned char *)(uintptr_t)(im->ro_hi[i] + (uint64_t)delta);

        for (; p < end; p++) {
            freq[*p]++;
        }
    }
}

/* The places in the image's read-only data where the n bytes b start a run
   of len bytes (len >= n) inside one range, whose XXH3-64 is `hash` when
   `hashed`: their count, at most `stop`, and the first.  The search
   anchors on the window's rarest byte (freq: ro_freq).  The image's bytes
   lie at its addresses plus delta (0 but for a legacy table's copy). */
static unsigned find_in(const IcoStateImage *im, int64_t delta, const uint32_t *freq,
                        const unsigned char *b, size_t n, size_t len, int hashed, uint64_t hash,
                        int lstart, unsigned stop, uint64_t *first)
{
    size_t k = 0, i;
    unsigned c = 0;
    int r;

    *first = 0;
    if (n == 0 || len < n) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        if (freq[b[i]] < freq[b[k]]) {
            k = i;
        }
    }
    for (r = 0; r < im->n_ro; r++) {
        const unsigned char *lo =
            (const unsigned char *)(uintptr_t)(im->ro_lo[r] + (uint64_t)delta);
        const unsigned char *hi =
            (const unsigned char *)(uintptr_t)(im->ro_hi[r] + (uint64_t)delta);
        const unsigned char *p, *last;

        if (hi <= lo || (size_t)(hi - lo) < len) {
            continue;
        }
        p = lo + k;
        last = hi - len + k; /* the anchor's last place */
        while (p <= last) {
            const unsigned char *q = (const unsigned char *)memchr(p, b[k], (size_t)(last - p) + 1);
            const unsigned char *at;

            if (q == NULL) {
                break;
            }
            at = q - k;
            if (memcmp(at, b, n) == 0 && (!hashed || xxh3_64(at, len) == hash) &&
                (!lstart || (at > lo && at[-1] == 0))) {
                if (c == 0) {
                    *first = (uint64_t)(uintptr_t)at - (uint64_t)delta;
                }
                if (++c >= stop) {
                    return c;
                }
            }
            p = q + 1;
        }
    }
    return c;
}

/* The window of the word w in its read-only range [rlo, rhi) (a text of
   tl bytes, or tl 0): grown both ways, doubling, until its bytes occur
   once in the image's read-only data.  1 and [*a, *z), or 0 when not even
   the whole range is unique. */
static int grow_window(const IcoStateImage *im, int64_t delta, const uint32_t *freq, uint64_t w,
                       uint64_t rlo, uint64_t rhi, size_t tl, uint64_t *a, uint64_t *z)
{
    uint64_t lo, hi, first;

    if (tl > 0) {
        lo = w;
        hi = w + tl;
    } else {
        lo = w - rlo > 8 ? w - 8 : rlo;
        hi = rhi - w > 8 ? w + 8 : rhi; /* w < rhi: never empty */
    }
    for (;;) {
        const uint64_t ext = hi - lo;

        if (find_in(im, delta, freq, (const unsigned char *)(uintptr_t)(lo + (uint64_t)delta),
                    (size_t)ext, (size_t)ext, 0, 0, 0, 2, &first) == 1) {
            *a = lo;
            *z = hi;
            return 1;
        }
        if (lo == rlo && hi == rhi) {
            return 0;
        }
        lo = lo - rlo > ext ? lo - ext : rlo;
        hi = rhi - hi > ext ? hi + ext : rhi;
    }
}

void ico_xb_image(const IcoSymTab *t, IcoStateImage *im)
{
    uint64_t lo, hi, ro_end;

    if (t == NULL || !t->code_from || t->code_hi <= t->code_lo) {
        return;
    }
    lo = im->text_lo;
    hi = im->text_hi;
    if (t->code_lo < lo || t->code_hi > hi || (t->code_lo == lo && t->code_hi == hi)) {
        return;
    }
    im->text_lo = t->code_lo;
    im->text_hi = t->code_hi;
    ro_end = t->code_lo;
    {
        /* the dynamic linker's tables (.dynsym, .dynstr, .gnu.hash, .rela.dyn,
           the notes) before the read-only data: no program pointer aims there,
           and .dynstr spells every exported function's name, which a text
           window would otherwise find twice; and the unwind and exception
           tables after it (.eh_frame, .eh_frame_hdr, .gcc_except_table):
           the unwinder's data, which no C pointer aims into, whose bytes can
           spell a short text (v0.11.9-rc5's "/8" lay in .eh_frame too) */
        static const char *const dyn[] = {".dyn",    ".gnu",      ".rela",
                                          ".rel.",   ".note",     ".hash",
                                          ".interp", ".eh_frame", ".gcc_except_table"};
        uint64_t first = t->code_lo, end = 0;
        size_t i, j;

        for (i = 0; i < t->n_sec; i++) {
            const IcoSymSec *s = &t->sec[i];
            const char *nm = ico_symtab_name(t, s->name);
            int skip = 0;

            if (s->code || s->start < lo || s->start >= t->code_lo) {
                continue;
            }
            for (j = 0; j < sizeof dyn / sizeof dyn[0]; j++) {
                skip |= strncmp(nm, dyn[j], strlen(dyn[j])) == 0;
            }
            if (!skip && s->start < first) {
                first = s->start;
            }
            if (!skip && s->start + s->size > end) {
                end = s->start + s->size;
            }
        }
        if (first < t->code_lo) {
            lo = first;
            ro_end = end > first && end < t->code_lo ? end : t->code_lo;
        }
    }
    if (lo < t->code_lo && im->n_ro < ICO_STATE_IMAGE_RO) {
        im->ro_lo[im->n_ro] = lo;
        im->ro_hi[im->n_ro] = ro_end;
        im->n_ro++;
    }
    if (t->code_hi < hi && im->n_ro < ICO_STATE_IMAGE_RO) {
        im->ro_lo[im->n_ro] = t->code_hi;
        im->ro_hi[im->n_ro] = hi;
        im->n_ro++;
    }
}

const char *ico_xb_why_text(unsigned reason)
{
    switch (reason) {
    case ICO_XB_WHY_CODE:
        return "in the code but in no function";
    case ICO_XB_WHY_DATA:
        return "in writable data that no block or variable of a unique name holds";
    case ICO_XB_WHY_READONLY:
        return "in read-only data whose bytes repeat even across their whole range";
    case ICO_XB_WHY_IMAGE:
        return "in the image outside every section the symbol file names";
    case ICO_XB_WHY_NUMBER:
        return "in read-only data but no text, where a number can lie (kept as one)";
    }
    return "named by nothing";
}

typedef struct Out {
    unsigned char *p;
    size_t n, cap;
    int failed;
} Out;

static unsigned char *out_grow(Out *o, size_t k)
{
    if (o->failed) {
        return NULL;
    }
    if (o->n + k > o->cap) {
        size_t cap = o->cap ? o->cap : 65536;
        unsigned char *q;

        while (cap < o->n + k) {
            cap *= 2;
        }
        q = (unsigned char *)realloc(o->p, cap);
        if (q == NULL) {
            o->failed = 1;
            return NULL;
        }
        o->p = q;
        o->cap = cap;
    }
    o->n += k;
    return o->p + o->n - k;
}

static uint32_t str_put(Out *s, const char *name)
{
    const size_t k = strlen(name) + 1;
    const uint32_t at = (uint32_t)s->n;
    unsigned char *q = out_grow(s, k);

    if (q != NULL) {
        memcpy(q, name, k);
    }
    return at;
}

/* A data symbol read-only once loaded: in the image's read-only data, or in
   a section the file names .data.rel.ro* (relocated pointers, then never
   written), .rodata* or .rdata*.  Its bytes are the build's own, the same
   at every run but for the pointers. */
static int settled(const IcoSymTab *t, const IcoStateImage *im, const IcoSymData *d, int r_legacy)
{
    uint64_t lo, hi;
    const IcoSymSec *s;

    if (!d->exact || d->size == 0 || r_legacy) {
        return 0;
    }
    if (ro_range(im, d->start, &lo, &hi) && d->start + d->size <= hi) {
        return 1;
    }
    s = ico_symtab_section_at(t, d->start);
    if (s == NULL || d->start + d->size > s->start + s->size) {
        return 0;
    }
    {
        const char *n = ico_symtab_name(t, s->name);

        return strncmp(n, ".data.rel.ro", 12) == 0 || strncmp(n, ".rodata", 7) == 0 ||
               strncmp(n, ".rdata", 6) == 0;
    }
}

/* XXH3-64 of the n bytes at `at` with every 8-byte-aligned word that lies
   in the image (a relocated pointer: another value in every build and run)
   replaced by one mark, so the hash holds the contents and where the
   pointers are, not where they point */
static uint64_t masked_hash(uint64_t at, uint64_t n, const IcoStateImage *im)
{
    const unsigned char *src = (const unsigned char *)(uintptr_t)at;
    unsigned char *b = (unsigned char *)malloc(n ? (size_t)n : 1);
    uint64_t a, h;

    if (b == NULL) {
        return 0; /* no other build's hash is 0 but by chance: a refusal */
    }
    memcpy(b, src, (size_t)n);
    for (a = (at + 7u) & ~(uint64_t)7u; a + 8 <= at + n; a += 8) {
        uint64_t w;

        memcpy(&w, b + (a - at), sizeof w);
        if (w >= im->lo && w <= im->hi) {
            memset(b + (a - at), 0x5A, sizeof w);
        }
    }
    h = xxh3_64(b, (size_t)n);
    free(b);
    return h;
}

/* --- arm64 page words ---------------------------------------------------------- */

static int64_t sext(uint64_t v, int bits)
{
    const uint64_t m = 1ull << (bits - 1);

    return (int64_t)((v ^ m) - m);
}

/* the page an ADRP at pc makes, or 1 (never a page) when insn is no ADRP */
static uint64_t adrp_page(uint64_t pc, uint32_t insn)
{
    uint64_t imm;

    if ((insn & 0x9F000000u) != 0x90000000u) {
        return 1;
    }
    imm = (uint64_t)((insn >> 5) & 0x7FFFFu) << 2 | ((insn >> 29) & 3u);
    return (pc & ~(uint64_t)0xFFF) + (uint64_t)(sext(imm, 21) * 4096);
}

static uint32_t code32(uint64_t at)
{
    uint32_t v;

    memcpy(&v, (const void *)(uintptr_t)at, sizeof v);
    return v; /* arm64 code is little-endian, as the hosts that run it */
}

typedef struct PageEnt {
    uint64_t w;
    uint32_t fiber, flags;
} PageEnt;

typedef struct Prod {
    uint64_t ustart, page;
    uint32_t off, fiber;
} Prod;

static int fw_cmp(const void *a, const void *b)
{
    const FrameWord *x = (const FrameWord *)a, *y = (const FrameWord *)b;

    if (x->fiber != y->fiber) {
        return x->fiber < y->fiber ? -1 : 1;
    }
    if (x->kind != y->kind) {
        return x->kind < y->kind ? -1 : 1;
    }
    return x->w < y->w ? -1 : x->w > y->w;
}

static int pe_cmp(const void *a, const void *b)
{
    const PageEnt *x = (const PageEnt *)a, *y = (const PageEnt *)b;

    if (x->w != y->w) {
        return x->w < y->w ? -1 : 1;
    }
    return x->fiber < y->fiber ? -1 : x->fiber > y->fiber;
}

static int u64_cmp(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

    return x < y ? -1 : x > y;
}

static size_t slot_find(const IcoXbRefs *r, uint64_t w)
{
    size_t s = slot_of(w);

    while (r->key[s] != 0 && r->key[s] != w) {
        s = (s + 1) & (ICO_XB_MAX_VALUES - 1);
    }
    return s;
}

static int snap_has(const IcoXbRefs *r, uint64_t w)
{
    size_t lo = 0, hi = r->n_snap;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (r->snap[mid].value < w) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < r->n_snap && r->snap[lo].value == w;
}

/* The page words of every fiber and their producers: for each fiber, the
   units its return addresses lie in (past their start: functions with a
   frame on its stack), each such unit's ADRPs, and those whose page is one
   of the fiber's page-aligned words.  *pe sorted by (word, fiber).  0, or -1
   (no memory). */
static int plan_pages(IcoXbRefs *r, const IcoSymTab *t, uint8_t *uflag, PageEnt **pe, size_t *npe,
                      Prod **pr, size_t *npr)
{
    size_t a, b, i, k, cap_pe = 0, cap_pr = 0;
    size_t *units = NULL;

    *pe = NULL;
    *pr = NULL;
    *npe = *npr = 0;
    if (r->n_fw == 0) {
        return 0;
    }
    qsort(r->fw, r->n_fw, sizeof *r->fw, fw_cmp);
    units = (size_t *)malloc(r->n_fw * sizeof *units);
    if (units == NULL) {
        return -1;
    }
    for (a = 0; a < r->n_fw; a = b) {
        const uint32_t f = r->fw[a].fiber;
        size_t p0, nu = 0;

        for (b = a; b < r->n_fw && r->fw[b].fiber == f; b++) {}
        for (p0 = a; p0 < b && r->fw[p0].kind == 0; p0++) {}
        if (p0 == b) {
            continue; /* no page-aligned word on this fiber */
        }
        for (i = a; i < p0; i++) {
            const uint64_t w = r->fw[i].w;
            const IcoSymUnit *u = ico_symtab_unit_at(t, w);

            if (u != NULL && w != u->start) {
                units[nu++] = (size_t)(u - t->unit);
            } else if (u != NULL && w >= r->p.image.text_lo + 4) {
                /* a unit's start just after a call (BL, BLR) that ends the
                   unit before it: the return address of a call that does not
                   return, the caller's frame on this stack (its code must be
                   alike, as any frame's) */
                const IcoSymUnit *c = ico_symtab_unit_at(t, w - 4);
                const uint32_t insn = code32(w - 4);

                if (c != NULL &&
                    ((insn & 0xFC000000u) == 0x94000000u || (insn & 0xFFFFFC1Fu) == 0xD63F0000u)) {
                    units[nu++] = (size_t)(c - t->unit);
                    uflag[c - t->unit] |= XU_POSITION;
                }
            }
        }
        if (nu > 1) {
            qsort(units, nu, sizeof *units, u64_cmp);
        }
        for (i = 0; i < b - p0; i++) {
            const uint64_t w = r->fw[p0 + i].w;

            if (i > 0 && r->fw[p0 + i - 1].w == w) {
                continue;
            }
            if (*npe == cap_pe) {
                const size_t c = cap_pe ? cap_pe * 2 : 64;
                PageEnt *q = (PageEnt *)realloc(*pe, c * sizeof *q);

                if (q == NULL) {
                    free(units);
                    return -1;
                }
                *pe = q;
                cap_pe = c;
            }
            (*pe)[*npe].w = w;
            (*pe)[*npe].fiber = f;
            (*pe)[*npe].flags = 0;
            (*npe)++;
        }
        for (i = 0; i < nu; i++) {
            const IcoSymUnit *u;

            if (i > 0 && units[i] == units[i - 1]) {
                continue;
            }
            u = &t->unit[units[i]];
            for (k = 0; k + 4 <= u->size; k += 4) {
                const uint64_t page = adrp_page(u->start + k, code32(u->start + k));
                size_t lo = p0, hi = b;

                if (page == 1) {
                    continue;
                }
                while (lo < hi) { /* the fiber's page words, sorted */
                    const size_t mid = lo + (hi - lo) / 2;

                    if (r->fw[mid].w < page) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                if (lo == b || r->fw[lo].w != page) {
                    continue;
                }
                if (*npr == cap_pr) {
                    const size_t c = cap_pr ? cap_pr * 2 : 64;
                    Prod *q = (Prod *)realloc(*pr, c * sizeof *q);

                    if (q == NULL) {
                        free(units);
                        return -1;
                    }
                    *pr = q;
                    cap_pr = c;
                }
                (*pr)[*npr].ustart = u->start;
                (*pr)[*npr].page = page;
                (*pr)[*npr].off = (uint32_t)k;
                (*pr)[*npr].fiber = f;
                (*npr)++;
            }
        }
    }
    free(units);
    /* each page word's flags */
    for (i = 0; i < *npe; i++) {
        PageEnt *e = &(*pe)[i];
        int bl;

        for (k = 0; k < *npr; k++) {
            if ((*pr)[k].fiber == e->fiber && (*pr)[k].page == e->w) {
                e->flags |= PG_PROD;
                break;
            }
        }
        if ((r->strict[slot_find(r, e->w)] & 4u) != 0 || ico_symtab_starts_at(t, e->w) ||
            snap_has(r, e->w)) {
            e->flags |= PG_ORDINARY;
        }
        for (bl = 0; bl < r->n_blocks; bl++) {
            if (r->block[bl].addr == e->w) {
                e->flags |= PG_ORDINARY;
            }
        }
    }
    /* a page-aligned word no ADRP on its fiber makes is an ordinary address
       (an array element or a variable that starts a page): the ordinary
       rules alone name it */
    for (i = k = 0; i < *npe; i++) {
        if (((*pe)[i].flags & PG_PROD) != 0) {
            (*pe)[k++] = (*pe)[i];
        }
    }
    *npe = k;
    if (*npe > 1) {
        qsort(*pe, *npe, sizeof **pe, pe_cmp);
    }
    return 0;
}

/* --- a legacy read's page words ----------------------------------------------- */

/* the low 12 bits the first user of an ADRP's register Rd adds (an ADD or a
   load or store by unsigned offset through it), in the function's order
   after it at `at` and before `end` (a page kept across calls is used far
   from its ADRP), before anything writes Rd: 1 and *lo12, else 0 */
static int adrp_lo12(uint64_t at, uint64_t end, uint32_t rd, uint64_t *lo12)
{
    uint64_t j;

    for (j = at + 4; j + 4 <= end && j < at + 4 * 4096; j += 4) {
        const uint32_t w = code32(j);
        const uint32_t rn = (w >> 5) & 31u;
        uint32_t wr;

        if ((w & 0x7F800000u) == 0x11000000u && rn == rd) {
            *lo12 = (uint64_t)((w >> 10) & 0xFFFu) << (((w >> 22) & 1u) ? 12 : 0);
            return 1;
        }
        if ((w & 0x3B000000u) == 0x39000000u && rn == rd) {
            const uint32_t size = w >> 30, opc = (w >> 22) & 3u, v = (w >> 26) & 1u;
            const uint32_t scale = v && size == 0 && (opc & 2u) ? 4u : size;

            *lo12 = (uint64_t)((w >> 10) & 0xFFFu) << scale;
            return 1;
        }
        wr = ico_code_a64_writes(w);
        if (wr == ~0u || ((wr >> rd) & 1u) != 0) {
            return 0;
        }
    }
    return 0;
}

/* this build's address T as the saving version's: by the game ranges (one
   layout), the unit or the data symbol holding it (by name and ordinal in
   the version's table).  1 and *old, else 0. */
static int old_of(const IcoXbRefs *r, const IcoSymTab *ot, uint64_t T, uint64_t *old)
{
    const IcoXbLegacy *lg = r->p.legacy;
    const IcoSymTab *nt = lg->now_tab;
    const IcoSymUnit *u;
    const IcoSymData *d;
    int j;

    for (j = 0; j < 2; j++) {
        if (lg->now_game_lo[j] < lg->now_game_hi[j] && T >= lg->now_game_lo[j] &&
            T < lg->now_game_hi[j] && r->p.game_lo[j] < r->p.game_hi[j] &&
            T - lg->now_game_lo[j] < r->p.game_hi[j] - r->p.game_lo[j]) {
            *old = r->p.game_lo[j] + (T - lg->now_game_lo[j]);
            return 1;
        }
    }
    u = ico_symtab_unit_at(nt, T);
    if (u != NULL) {
        const IcoSymUnit *o =
            ico_symtab_unit_named(ot, u->name_hash, ico_symtab_name(nt, u->name), u->ordinal);

        if (o != NULL && T - u->start < o->size) {
            *old = o->start + (T - u->start);
            return 1;
        }
        return 0;
    }
    d = ico_symtab_data_at(nt, T);
    if (d != NULL && (T - d->start < d->size || T == d->start)) {
        const IcoSymData *o =
            ico_symtab_data_named(ot, d->name_hash, ico_symtab_name(nt, d->name), d->ordinal);

        if (o != NULL && T - d->start <= o->size) {
            *old = o->start + (T - d->start);
            return 1;
        }
    }
    return 0;
}

/* the 12-bit offset an ADD or a load/store (unsigned offset) adds to its
   base, scaled; 0 for another instruction */
static int a64_lo12(uint32_t w, uint64_t *lo12)
{
    if ((w & 0x7F800000u) == 0x11000000u) {
        *lo12 = (uint64_t)((w >> 10) & 0xFFFu) << (((w >> 22) & 1u) ? 12 : 0);
        return 1;
    }
    if ((w & 0x3B000000u) == 0x39000000u) {
        const uint32_t size = w >> 30, opc = (w >> 22) & 3u, v = (w >> 26) & 1u;

        *lo12 = (uint64_t)((w >> 10) & 0xFFFu) << (v && size == 0 && (opc & 2u) ? 4u : size);
        return 1;
    }
    return 0;
}

/* an ADRP whose first user in the function's order is not found (a page
   kept across calls, used after a branch): every user its page reaches
   through the function's own branches (code_hash.h's flow, this ADRP
   alone), each one's target here placed back in the version.  1 and the
   one old page they all lie on, else 0 (no user, one not placed back, or
   two pages) */
static int adrp_users_page(const IcoXbRefs *r, const IcoSymTab *ot, uint64_t start, uint64_t size,
                           uint64_t k, uint32_t rd, uint64_t P, uint64_t *page)
{
    const size_t words = (size_t)(size / 4);
    uint32_t *into = (uint32_t *)malloc((words ? words : 1) * sizeof *into);
    uint8_t *seen = (uint8_t *)malloc(words ? words : 1);
    size_t j;
    int any = 0, ok = 1;

    if (into == NULL || seen == NULL ||
        ico_code_a64_flow((const void *)(uintptr_t)start, words, (size_t)(k / 4), 1u << rd, 0, into,
                          seen) != 0) {
        free(into);
        free(seen);
        return 0;
    }
    for (j = 0; j < words && ok; j++) {
        const uint32_t w = code32(start + 4 * j);
        uint64_t lo12, old;

        if (!seen[j] || ((into[j] >> ((w >> 5) & 31u)) & 1u) == 0 || !a64_lo12(w, &lo12)) {
            continue;
        }
        if (!old_of(r, ot, P + lo12, &old)) {
            ok = 0;
        } else if (any && (old & ~(uint64_t)0xFFF) != *page) {
            ok = 0;
        } else {
            *page = old & ~(uint64_t)0xFFF;
            any = 1;
        }
    }
    free(into);
    free(seen);
    return ok && any;
}

/* the old pages an ADRP may have made whose old target cannot be placed:
   for a page whose target's offset is not known (no user found), the old
   pages of its first and last byte when both place back; else the pages
   of the version's section that holds what this build's T lies in.  1 and
   [*lo, *hi], else 0 (no section of that name: any page) */
static int old_pages_of(const IcoXbRefs *r, const IcoSymTab *ot, uint64_t T, int page_only,
                        uint64_t *lo, uint64_t *hi)
{
    const IcoSymSec *s = ico_symtab_section_at(r->p.legacy->now_tab, T);
    uint64_t o1, o2;
    size_t i;

    if (page_only && old_of(r, ot, T, &o1) && old_of(r, ot, T + 0xFFF, &o2) && o2 >= o1 &&
        o2 - o1 == 0xFFF) {
        *lo = o1 & ~(uint64_t)0xFFF;
        *hi = o2 & ~(uint64_t)0xFFF;
        return 1;
    }

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < ot->n_sec; i++) {
        if (strcmp(ico_symtab_name(ot, ot->sec[i].name),
                   ico_symtab_name(r->p.legacy->now_tab, s->name)) == 0) {
            *lo = ot->sec[i].start & ~(uint64_t)0xFFF;
            *hi = (ot->sec[i].start + ot->sec[i].size - 1) & ~(uint64_t)0xFFF;
            return 1;
        }
    }
    return 0;
}

typedef struct LProd {
    uint64_t ustart; /* the version's unit */
    uint32_t off;
    int known;     /* the old page is known */
    uint64_t page; /* known: the old page; else the old pages [page, page_hi] */
    uint64_t page_hi;
} LProd;

/* plan_pages for a legacy read: the version's code is not in this
   process, but every function with a frame must hash as this build's (the
   load checks it), so this build's same function at the same offsets
   holds the same ADRPs: each one's target here, placed back in the version
   by old_of, gives the page it made there.  A page word that an ADRP whose
   target cannot be placed back may have made goes to *bad (refused). */
static int legacy_pages(IcoXbRefs *r, const IcoSymTab *t, uint8_t *uflag, PageEnt **pe, size_t *npe,
                        Prod **pr, size_t *npr, uint64_t **bad, size_t *nbad)
{
    const IcoSymTab *nt = r->p.legacy->now_tab;
    size_t a, b, i, k, cap_pe = 0, cap_pr = 0, cap_bad = 0;
    size_t *units = NULL;
    LProd *lp = NULL;
    size_t cap_lp = 0;

    *pe = NULL;
    *pr = NULL;
    *bad = NULL;
    *npe = *npr = *nbad = 0;
    if (r->n_fw == 0 || nt == NULL) {
        return 0;
    }
    qsort(r->fw, r->n_fw, sizeof *r->fw, fw_cmp);
    units = (size_t *)malloc(r->n_fw * sizeof *units);
    if (units == NULL) {
        return -1;
    }
    for (a = 0; a < r->n_fw; a = b) {
        const uint32_t f = r->fw[a].fiber;
        size_t p0, nu = 0, nlp = 0;

        for (b = a; b < r->n_fw && r->fw[b].fiber == f; b++) {}
        for (p0 = a; p0 < b && r->fw[p0].kind == 0; p0++) {}
        if (p0 == b) {
            continue;
        }
        for (i = a; i < p0; i++) {
            const uint64_t w = r->fw[i].w;
            const IcoSymUnit *u = ico_symtab_unit_at(t, w);

            if (u != NULL && w != u->start) {
                units[nu++] = (size_t)(u - t->unit);
            } else if (u != NULL) {
                /* a unit's start just after a call that ends the unit
                   before it: read in this build's same function */
                const IcoSymUnit *c = ico_symtab_unit_at(t, w - 1);
                const IcoSymUnit *c2 =
                    c != NULL ? ico_symtab_unit_named(nt, c->name_hash, ico_symtab_name(t, c->name),
                                                      c->ordinal)
                              : NULL;

                if (c2 != NULL && w - c->start >= 4 && w - c->start <= c2->size) {
                    const uint32_t insn = code32(c2->start + (w - c->start) - 4);

                    if ((insn & 0xFC000000u) == 0x94000000u ||
                        (insn & 0xFFFFFC1Fu) == 0xD63F0000u) {
                        units[nu++] = (size_t)(c - t->unit);
                        uflag[c - t->unit] |= XU_POSITION;
                    }
                }
            }
        }
        if (nu > 1) {
            qsort(units, nu, sizeof *units, u64_cmp);
        }
        /* the fiber's frames' ADRPs and their old pages */
        for (i = 0; i < nu; i++) {
            const IcoSymUnit *u, *u2;
            uint64_t n;

            if (i > 0 && units[i] == units[i - 1]) {
                continue;
            }
            u = &t->unit[units[i]];
            u2 = ico_symtab_unit_named(nt, u->name_hash, ico_symtab_name(t, u->name), u->ordinal);
            if (u2 == NULL) {
                continue; /* the load refuses it: the function is missing here */
            }
            n = u->size < u2->size ? u->size : u2->size;
            for (k = 0; k + 4 <= n; k += 4) {
                const uint32_t insn = code32(u2->start + k);
                const uint64_t P = adrp_page(u2->start + k, insn);
                uint64_t lo12 = 0, T, old = 0;
                LProd *e;

                if (P == 1) {
                    continue;
                }
                if (nlp == cap_lp) {
                    const size_t c = cap_lp ? cap_lp * 2 : 256;
                    LProd *q = (LProd *)realloc(lp, c * sizeof *q);

                    if (q == NULL) {
                        free(units);
                        free(lp);
                        return -1;
                    }
                    lp = q;
                    cap_lp = c;
                }
                e = &lp[nlp++];
                e->ustart = u->start;
                e->off = (uint32_t)k;
                if (adrp_lo12(u2->start + k, u2->start + u2->size, insn & 31u, &lo12)) {
                    T = P + lo12;
                    e->known = old_of(r, t, T, &old);
                } else {
                    T = P;
                    e->known = adrp_users_page(r, t, u2->start, u2->size, k, insn & 31u, P, &old);
                }
                if (e->known) {
                    e->page = old & ~(uint64_t)0xFFF;
                } else if (!old_pages_of(r, t, T, T == P, &e->page, &e->page_hi)) {
                    e->page = 0;
                    e->page_hi = ~(uint64_t)0;
                }
            }
        }
        /* each page word of the fiber */
        for (i = p0; i < b; i++) {
            const uint64_t w = r->fw[i].w;
            int any = 0, unsure = 0;

            if (i > p0 && r->fw[i - 1].w == w) {
                continue;
            }
            for (k = 0; k < nlp; k++) {
                if (lp[k].known ? lp[k].page == w : (w >= lp[k].page && w <= lp[k].page_hi)) {
                    any |= lp[k].known;
                    unsure |= !lp[k].known;
                }
            }
            if (unsure) {
                if (*nbad == cap_bad) {
                    const size_t c = cap_bad ? cap_bad * 2 : 16;
                    uint64_t *q = (uint64_t *)realloc(*bad, c * sizeof *q);

                    if (q == NULL) {
                        free(units);
                        free(lp);
                        return -1;
                    }
                    *bad = q;
                    cap_bad = c;
                }
                (*bad)[(*nbad)++] = w;
                continue;
            }
            if (!any) {
                continue; /* an ordinary address */
            }
            if (*npe == cap_pe) {
                const size_t c = cap_pe ? cap_pe * 2 : 64;
                PageEnt *q = (PageEnt *)realloc(*pe, c * sizeof *q);

                if (q == NULL) {
                    free(units);
                    free(lp);
                    return -1;
                }
                *pe = q;
                cap_pe = c;
            }
            (*pe)[*npe].w = w;
            (*pe)[*npe].fiber = f;
            (*pe)[*npe].flags = PG_PROD;
            (*npe)++;
            for (k = 0; k < nlp; k++) {
                if (!lp[k].known || lp[k].page != w) {
                    continue;
                }
                if (*npr == cap_pr) {
                    const size_t c = cap_pr ? cap_pr * 2 : 64;
                    Prod *q = (Prod *)realloc(*pr, c * sizeof *q);

                    if (q == NULL) {
                        free(units);
                        free(lp);
                        return -1;
                    }
                    *pr = q;
                    cap_pr = c;
                }
                (*pr)[*npr].ustart = lp[k].ustart;
                (*pr)[*npr].page = w;
                (*pr)[*npr].off = lp[k].off;
                (*pr)[*npr].fiber = f;
                (*npr)++;
            }
        }
    }
    free(units);
    free(lp);
    for (i = 0; i < *npe; i++) {
        PageEnt *e = &(*pe)[i];
        int bl;

        if ((r->strict[slot_find(r, e->w)] & 4u) != 0 || ico_symtab_starts_at(t, e->w)) {
            e->flags |= PG_ORDINARY;
        }
        for (bl = 0; bl < r->n_blocks; bl++) {
            if (r->block[bl].addr == e->w) {
                e->flags |= PG_ORDINARY;
            }
        }
    }
    if (*npe > 1) {
        qsort(*pe, *npe, sizeof **pe, pe_cmp);
    }
    return 0;
}

/* 1 when every page word entry of w has a producer and none is ordinary:
   the page rule alone names it */
static int page_only(const PageEnt *pe, size_t npe, uint64_t w)
{
    size_t lo = 0, hi = npe;
    int any = 0;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (pe[mid].w < w) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < npe && pe[lo].w == w; lo++) {
        if ((pe[lo].flags & (PG_PROD | PG_ORDINARY)) != PG_PROD) {
            return 0;
        }
        any = 1;
    }
    return any;
}

/* a word nothing names: counted, and listed while the list has room */
static void note_unnamed(const IcoXbRefs *r, const IcoSymTab *t, size_t slot, uint64_t w,
                         unsigned reason, unsigned *flags, unsigned *left, uint64_t *first,
                         IcoXbStats *st)
{
    *flags |= ICO_XB_UNNAMED;
    (*left)++;
    if (*first == 0) {
        *first = w;
    }
    if (st->listed < ICO_XB_LIST) {
        IcoXbUnnamed *u = &st->list[st->listed++];
        const IcoSymSec *sec = ico_symtab_section_at(t, w);
        const char *sym = ico_symtab_nearest(t, w, &u->sym_off);

        u->word = w;
        u->at = slot < ICO_XB_MAX_VALUES ? r->at[slot] : 0;
        u->reason = reason;
        snprintf(u->where, sizeof u->where, "%s",
                 slot < ICO_XB_MAX_VALUES ? r->labels[r->lab[slot]] : "");
        snprintf(u->section, sizeof u->section, "%s",
                 sec != NULL ? ico_symtab_name(t, sec->name) : "");
        snprintf(u->sym, sizeof u->sym, "%s", sym != NULL ? sym : "");
    }
}

/* a window point (state_xbuild.h): its record, its bytes (whole, or the
   prefix when the pool has no room for them) and its nearest symbol's
   name.  A text starts a literal when the byte before it is zero; at the
   range's own start that byte is not readable here, and rlo_lstart says
   (a legacy run's flag from its table, else 0).  0, or -1 (no memory). */
static int put_window(Out *points, Out *pool, Out *strs, const IcoSymTab *t, int64_t delta,
                      uint64_t w, uint64_t a, uint64_t z, int text, uint64_t rlo, int rlo_lstart)
{
    const size_t len = (size_t)(z - a);
    const unsigned char *b = (const unsigned char *)(uintptr_t)(a + (uint64_t)delta);
    const int whole =
        pool->n + len <= (size_t)ICO_XB_POOL_BYTES - (size_t)ICO_XB_MAX_POINTS * ICO_XB_CONTENT;
    const size_t stored = whole ? len : len < ICO_XB_CONTENT ? len : ICO_XB_CONTENT;
    uint64_t off = 0;
    const char *sym = ico_symtab_nearest(t, w, &off);
    const uint32_t sym_at = sym != NULL ? str_put(strs, sym) : 0u;
    const uint32_t pool_at = (uint32_t)pool->n;
    unsigned char *q = out_grow(points, POINT_BYTES);
    unsigned char *pq;

    if (q == NULL) {
        return -1;
    }
    memset(q, 0, POINT_BYTES);
    put64(q, w);
    put32(q + 8, XP_WINDOW);
    put32(q + 12, (uint32_t)len);
    put32(q + 16, (uint32_t)(w - a));
    put32(q + 20, (text ? XW_TEXT : 0u) | (whole ? 0u : XW_HASHED) |
                      (text && (a > rlo ? b[-1] == 0 : rlo_lstart) ? XW_LSTART : 0u));
    put64(q + 24, xxh3_64(b, len));
    put32(q + 32, pool_at);
    put32(q + 36, (uint32_t)stored);
    put32(q + 40, sym_at);
    put64(q + 48, sym != NULL ? off : 0);
    pq = out_grow(pool, stored);
    if (pq == NULL) {
        return -1;
    }
    memcpy(pq, b, stored);
    return 0;
}

int ico_xb_finish(IcoXbRefs *r, void **out, size_t *n, IcoXbStats *st)
{
    const IcoXbProc *p = &r->p;
    IcoSymTab *t = p->tab;
    uint8_t *uflag = NULL;
    unsigned char *dflag = NULL;
    Out units = {NULL, 0, 0, 0}, data = {NULL, 0, 0, 0}, points = {NULL, 0, 0, 0},
        blocks = {NULL, 0, 0, 0}, strs = {NULL, 0, 0, 0}, lst = {NULL, 0, 0, 0},
        pool = {NULL, 0, 0, 0}, pages = {NULL, 0, 0, 0}, prods = {NULL, 0, 0, 0};
    unsigned flags = 0, nu = 0, nd = 0, np = 0, npos = 0, left = 0, nwin = 0, ntext = 0, nmask = 0;
    /* arm64: the fibers' page words and the ADRPs that make them */
    PageEnt *pe = NULL;
    Prod *pr = NULL;
    size_t npe = 0, npr = 0, npbad = 0;
    uint64_t *pbad = NULL; /* a legacy read's page words no ADRP can be told for */
    uint64_t first_unnamed = 0;
    uint32_t *freq = NULL;
    unsigned char *sec, *q;
    size_t i, total;
    int rc = -1;
    /* the image's upper half is 0 or 1: a number can lie in it (above) */
    const int numbers_reach = ((p->judge_lo != 0 ? p->judge_lo : p->image.lo) >> 32) <= 1u;

    memset(st, 0, sizeof *st);
    *out = NULL;
    *n = 0;
    if (t != NULL) {
        flags |= ICO_XB_TABLE;
        st->code_from = t->code_from;
        uflag = (uint8_t *)calloc(t->n_unit ? t->n_unit : 1, 1);
        dflag = (unsigned char *)calloc(t->n_data ? t->n_data : 1, 1);
        if (uflag == NULL || dflag == NULL) {
            goto done;
        }
    }
    if (!p->any_address && p->image.lo < 0x100000000ull) {
        flags |= ICO_XB_LOW_IMAGE;
    }
    if (r->overflow) {
        flags |= ICO_XB_OVERFLOW;
    }
    if (r->a64) {
        flags |= ICO_XB_PAGES;
    }
    if (r->fw_failed ||
        (t != NULL && p->legacy == NULL && plan_pages(r, t, uflag, &pe, &npe, &pr, &npr) != 0) ||
        (t != NULL && p->legacy != NULL &&
         legacy_pages(r, t, uflag, &pe, &npe, &pr, &npr, &pbad, &npbad) != 0)) {
        goto done;
    }
    for (i = 0; i < npbad; i++) {
        note_unnamed(r, t, SIZE_MAX, pbad[i], ICO_XB_WHY_PAGE, &flags, &left, &first_unnamed, st);
    }
    (void)str_put(&strs, ""); /* offset 0: no name */
    /* each distinct word: what it points into */
    for (i = 0; t != NULL && i < ICO_XB_MAX_VALUES; i++) {
        const uint64_t w = r->key[i];
        const int strict = (r->strict[i] & 1) != 0;
        uint64_t rlo = 0, rhi = 0;
        int b, in_block = 0, ro, rlo_lstart = 0;

        if (w == 0) {
            continue;
        }
        if (npe > 0 && (w & 0xFFFu) == 0 && page_only(pe, npe, w)) {
            continue; /* a page an ADRP on its fiber's stack makes: the page rule names it */
        }
        if (w >= p->image.text_lo && w < p->image.text_hi) {
            const IcoSymUnit *u = ico_symtab_unit_at(t, w);

            if (u != NULL) {
                uflag[u - t->unit] |= (uint8_t)(!strict         ? XU_SOFT
                                                : w == u->start ? XU_START
                                                                : XU_POSITION);
            } else if (strict) {
                /* in the code but in no unit: nothing confirms it is an
                   address (a float next to the integer 1 under Windows'
                   0x140000000 is such a word), so it stays a number */
                note_unnamed(r, t, i, w, ICO_XB_WHY_CODE, &flags, &left, &first_unnamed, st);
            }
            continue;
        }
        if (in_game(p->game_lo, p->game_hi, w)) {
            continue;
        }
        ro = ro_range(&p->image, w, &rlo, &rhi);
        for (b = 0; b < r->n_blocks; b++) {
            if (w >= r->block[b].addr && w <= r->block[b].addr + r->block[b].size) {
                in_block = 1;
                break;
            }
        }
        if (in_block || !strict) {
            continue; /* a dead frame's word into data is left as it is at a load */
        }
        {
            const IcoSymData *d = ico_symtab_data_at(t, w);

            /* in read-only data the end address of a sized constant that no
               symbol starts at may be the next, unnamed one's start (a
               string literal after a table): its window names it */
            if (d != NULL && !(ro && d->exact && w != d->start && w == d->start + d->size)) {
                if (d->unique) {
                    dflag[d - t->data] = 1;
                    continue;
                }
                if (settled(t, &p->image, d, p->legacy != NULL)) {
                    dflag[d - t->data] = 2; /* by its ordinal and contents */
                    continue;
                }
            }
        }
        {
            /* a start-up value of the game's data */
            size_t lo = 0, hi = r->n_snap;

            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;

                if (r->snap[mid].value < w) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            if (lo < r->n_snap && r->snap[lo].value == w) {
                if (np == ICO_XB_MAX_POINTS) {
                    flags |= ICO_XB_OVERFLOW;
                    continue;
                }
                q = out_grow(&points, POINT_BYTES);
                if (q == NULL) {
                    goto done;
                }
                memset(q, 0, POINT_BYTES);
                put64(q, w);
                put32(q + 8, XP_SNAP);
                put64(q + 16, r->snap[lo].off);
                np++;
                continue;
            }
        }
        if (ro && numbers_reach && text_len(w, rhi, r->delta) < 2) {
            /* read-only data that is no text (or the empty one: any zero
               byte), where a float next to the integer 1 reaches the image
               (Windows): nothing tells such a word from a number, so it is
               kept as one */
            note_unnamed(r, t, i, w, ICO_XB_WHY_NUMBER, &flags, &left, &first_unnamed, st);
            continue;
        }
        if (ro && p->legacy != NULL) {
            /* a legacy read: only the version's text runs are known; the
               window stays inside the run holding the word */
            uint64_t a0, z0;

            if (!ico_legacy_run(p->legacy->lt, w - p->legacy->old_lo, &a0, &z0, &rlo_lstart)) {
                note_unnamed(r, t, i, w, ICO_XB_WHY_NOBYTES, &flags, &left, &first_unnamed, st);
                continue;
            }
            rlo = p->legacy->old_lo + a0;
            rhi = p->legacy->old_lo + z0;
        }
        if (ro) {
            /* read-only data: its window */
            const size_t tl = text_len(w, rhi, r->delta);
            uint64_t a = 0, z = 0;

            if (np == ICO_XB_MAX_POINTS) {
                flags |= ICO_XB_OVERFLOW;
                continue;
            }
            if (freq == NULL) {
                freq = (uint32_t *)malloc(256 * sizeof *freq);
                if (freq == NULL) {
                    goto done;
                }
                ro_freq(&p->image, r->delta, freq);
            }
            if (grow_window(&p->image, r->delta, freq, w, rlo, rhi, tl, &a, &z)) {
                if (put_window(&points, &pool, &strs, t, r->delta, w, a, z, tl > 0, rlo,
                               rlo_lstart) != 0) {
                    goto done;
                }
                np++;
                nwin++;
                ntext += tl > 0;
                continue;
            }
            note_unnamed(r, t, i, w, ICO_XB_WHY_READONLY, &flags, &left, &first_unnamed, st);
            continue;
        }
        /* nothing names it: a number that lies in the image's range (a
           float next to the integer 1 under Windows' 0x140000000, a word
           into the image's headers), kept as it is */
        note_unnamed(r, t, i, w,
                     t->n_sec > 0 && ico_symtab_section_at(t, w) == NULL ? ICO_XB_WHY_IMAGE
                                                                         : ICO_XB_WHY_DATA,
                     &flags, &left, &first_unnamed, st);
    }
    for (i = 0; t != NULL && i < t->n_unit; i++) {
        const IcoSymUnit *u = &t->unit[i];
        unsigned f = uflag[i];

        if (f == 0) {
            continue;
        }
        if (nu == ICO_XB_MAX_UNITS) {
            flags |= ICO_XB_OVERFLOW;
            break;
        }
        if (u->unique) {
            f |= XU_UNIQUE;
        }
        npos += (f & XU_POSITION) != 0;
        q = out_grow(&units, UNIT_BYTES);
        if (q == NULL) {
            goto done;
        }
        memset(q, 0, UNIT_BYTES);
        put64(q, u->start);
        put64(q + 8, u->size);
        put64(q + 16, u->name_hash);
        {
            int missing = 0;

            put64(q + 24, unit_hash(r, t, u, &missing));
            if (missing) {
                /* a function the version's record never hashed */
                note_unnamed(r, t, SIZE_MAX, u->start, ICO_XB_WHY_NOHASH, &flags, &left,
                             &first_unnamed, st);
            }
        }
        put32(q + 32, str_put(&strs, ico_symtab_name(t, u->name)));
        put32(q + 36, u->ordinal);
        put32(q + 40, f);
        nu++;
    }
    for (i = 0; t != NULL && i < t->n_data; i++) {
        const IcoSymData *d = &t->data[i];

        if (!dflag[i]) {
            continue;
        }
        if (nd == ICO_XB_MAX_DATA) {
            flags |= ICO_XB_OVERFLOW;
            break;
        }
        q = out_grow(&data, DATA_BYTES);
        if (q == NULL) {
            goto done;
        }
        memset(q, 0, DATA_BYTES);
        put64(q, d->start);
        put64(q + 8, d->size);
        put64(q + 16, d->name_hash);
        put32(q + 24, str_put(&strs, ico_symtab_name(t, d->name)));
        put32(q + 28, d->ordinal);
        if (dflag[i] == 2) {
            put32(q + 32, XD_EXACT | XD_MASKED);
            put64(q + 40, masked_hash(d->start, d->size, &p->image));
            nmask++;
        } else {
            put32(q + 32, (d->exact ? XD_EXACT : 0u) | (d->sec_size != 0 ? XD_SECTION : 0u));
            put32(q + 36, (uint32_t)d->sec_off);
            put64(q + 40, d->sec_size);
        }
        nd++;
    }
    for (i = 0; i < (size_t)r->n_blocks; i++) {
        const BlockRec *b = &r->block[i];

        q = out_grow(&blocks, BLOCK_BYTES);
        if (q == NULL) {
            goto done;
        }
        memset(q, 0, BLOCK_BYTES);
        put64(q, b->addr);
        put64(q + 8, b->size);
        put64(q + 16, b->offset);
        put32(q + 24, b->flags);
        put32(q + 28, str_put(&strs, b->name));
    }
    if (npe > ICO_XB_MAX_PAGES || npr > ICO_XB_MAX_PRODS) {
        flags |= ICO_XB_OVERFLOW;
        npe = npr = 0;
    }
    for (i = 0; i < npe; i++) {
        q = out_grow(&pages, PAGE_BYTES);
        if (q == NULL) {
            goto done;
        }
        put64(q, pe[i].w);
        put32(q + 8, pe[i].fiber);
        put32(q + 12, pe[i].flags);
    }
    for (i = 0; i < npr; i++) {
        q = out_grow(&prods, PROD_BYTES);
        if (q == NULL) {
            goto done;
        }
        put64(q, pr[i].ustart);
        put64(q + 8, pr[i].page);
        put32(q + 16, pr[i].off);
        put32(q + 20, pr[i].fiber);
    }
    for (i = 0; i < st->listed; i++) {
        const IcoXbUnnamed *u = &st->list[i];

        q = out_grow(&lst, LIST_BYTES);
        if (q == NULL) {
            goto done;
        }
        memset(q, 0, LIST_BYTES);
        put64(q, u->word);
        put64(q + 8, u->at);
        put64(q + 16, u->sym_off);
        put32(q + 24, str_put(&strs, u->where));
        put32(q + 28, str_put(&strs, u->section));
        put32(q + 32, str_put(&strs, u->sym));
        put32(q + 36, u->reason);
    }
    if (units.failed || data.failed || points.failed || blocks.failed || strs.failed ||
        lst.failed || pool.failed || pages.failed || prods.failed) {
        goto done;
    }
    total = H_BYTES3 + units.n + data.n + points.n + blocks.n + lst.n + pages.n + prods.n + strs.n +
            pool.n;
    sec = (unsigned char *)malloc(total);
    if (sec == NULL) {
        goto done;
    }
    memset(sec, 0, H_BYTES3);
    put32(sec + H_MAGIC, ICO_XB_MAGIC);
    put32(sec + H_VERSION, ICO_XB_VERSION);
    put32(sec + H_FLAGS, flags);
    put32(sec + H_RULES, ICO_CODE_HASH_RULES);
    put32(sec + H_ARCH, t != NULL ? (uint32_t)t->env.arch : 0u);
    put32(sec + H_UNITS, nu);
    put32(sec + H_DATA, nd);
    put32(sec + H_POINTS, np);
    put32(sec + H_BLOCKS, (uint32_t)r->n_blocks);
    put32(sec + H_STR, (uint32_t)strs.n);
    put64(sec + H_LAYOUT, t != NULL ? t->layout_hash : 0);
    put64(sec + H_UNNAMED, first_unnamed);
    put32(sec + H_VALUES, (uint32_t)r->n_vals);
    put32(sec + H_LEFT, left);
    put32(sec + H_POOL, (uint32_t)pool.n);
    put32(sec + H_LIST, st->listed);
    put32(sec + H_PAGES, (uint32_t)npe);
    put32(sec + H_PRODS, (uint32_t)npr);
    q = sec + H_BYTES3;
    put_bytes(q, units.p, units.n);
    q += units.n;
    put_bytes(q, data.p, data.n);
    q += data.n;
    put_bytes(q, points.p, points.n);
    q += points.n;
    put_bytes(q, blocks.p, blocks.n);
    q += blocks.n;
    put_bytes(q, lst.p, lst.n);
    q += lst.n;
    put_bytes(q, pages.p, pages.n);
    q += pages.n;
    put_bytes(q, prods.p, prods.n);
    q += prods.n;
    put_bytes(q, strs.p, strs.n);
    q += strs.n;
    put_bytes(q, pool.p, pool.n);
    *out = sec;
    *n = total;
    st->units = nu;
    st->positions = npos;
    st->data = nd;
    st->points = np;
    st->windows = nwin;
    st->pages = (unsigned)npe;
    st->producers = (unsigned)npr;
    st->data_masked = nmask;
    st->texts = ntext;
    st->blocks = (unsigned)r->n_blocks;
    st->values = (unsigned)r->n_vals;
    st->flags = flags;
    st->left = left;
    st->first_unnamed = first_unnamed;
    st->bytes = total;
    rc = 0;
done:
    free(units.p);
    free(data.p);
    free(points.p);
    free(blocks.p);
    free(strs.p);
    free(lst.p);
    free(pages.p);
    free(prods.p);
    free(pe);
    free(pr);
    free(pbad);
    free(r->fw);
    free(r->uh);
    free(pool.p);
    free(freq);
    free(uflag);
    free(dflag);
    free(r->key);
    free(r->strict);
    free(r->lab);
    free(r->at);
    free(r->labels);
    free(r->snap);
    free(r);
    return rc;
}

/* the table's triple for the function at offset off from the image start */
static const uint64_t *legacy_hash_at(const uint64_t *hash, size_t n_hash, uint64_t off)
{
    size_t lo = 0, hi = n_hash;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (hash[3 * mid] < off) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < n_hash && hash[3 * lo] == off ? hash + 3 * lo : NULL;
}

int ico_xb_legacy_verify(const void *v2, size_t n, const IcoSymTab *old_tab, uint64_t old_lo,
                         const uint64_t *hash, size_t n_hash, char *why, size_t why_n)
{
    const unsigned char *s = (const unsigned char *)v2;
    uint32_t ver, nu, i;

    if (s == NULL || n < H_BYTES || get32(s + H_MAGIC) != ICO_XB_MAGIC) {
        snprintf(why, why_n, "its symbols record is missing");
        return -1;
    }
    ver = get32(s + H_VERSION);
    if (ver != 2 || (int)get32(s + H_ARCH) != ICO_CODE_A64) {
        snprintf(why, why_n, "its symbols record is not one a legacy table reads");
        return -1;
    }
    if (get64(s + H_LAYOUT) != old_tab->layout_hash) {
        snprintf(why, why_n, "the legacy table is another version's (another game data layout)");
        return -1;
    }
    nu = get32(s + H_UNITS);
    if ((uint64_t)H_BYTES + (uint64_t)nu * UNIT_BYTES > n) {
        snprintf(why, why_n, "its symbols record does not hold what it says");
        return -1;
    }
    for (i = 0; i < nu; i++) {
        const unsigned char *u = s + H_BYTES + (size_t)i * UNIT_BYTES;
        const IcoSymUnit *o = ico_symtab_unit_at(old_tab, get64(u));

        /* every function the record names lies where the table puts one of
           the same start (a unit of the version's own table, a read-only
           symbol of its, which the record's version took for code) */
        if (o == NULL ? ico_symtab_data_at(old_tab, get64(u)) == NULL : o->start != get64(u)) {
            snprintf(why, why_n,
                     "the legacy table is another version's (a function of the record is not "
                     "where the table puts it)");
            return -1;
        }
        /* and its code is the table's: the record's hash (rules 1, the
           saver's own) is the table's rules-1 hash of that function */
        if (o != NULL && get64(u) >= old_lo) {
            const uint64_t *h = legacy_hash_at(hash, n_hash, get64(u) - old_lo);

            if (h == NULL || h[1] != get64(u + 24)) {
                snprintf(why, why_n,
                         "the legacy table is another build's (the code of a function of the "
                         "record differs from the table's)");
                return -1;
            }
        }
    }
    return 0;
}

int ico_xb_record_blocks(const void *secv, size_t n, IcoXbBlockFn fn, void *user)
{
    const unsigned char *s = (const unsigned char *)secv;
    uint32_t ver, nu, nd, np, nb, ns, i;
    size_t dbytes, pbytes, hbytes, at;

    if (s == NULL || n < H_BYTES || get32(s + H_MAGIC) != ICO_XB_MAGIC) {
        return -1;
    }
    ver = get32(s + H_VERSION);
    if (ver < 1 || ver > ICO_XB_VERSION || (ver >= 3 && n < H_BYTES3)) {
        return -1;
    }
    dbytes = ver >= 2 ? DATA_BYTES : DATA_BYTES_V1;
    pbytes = ver >= 3 ? POINT_BYTES : POINT_BYTES_V2;
    hbytes = ver >= 3 ? H_BYTES3 : H_BYTES;
    nu = get32(s + H_UNITS);
    nd = get32(s + H_DATA);
    np = get32(s + H_POINTS);
    nb = get32(s + H_BLOCKS);
    ns = get32(s + H_STR);
    at = hbytes + (size_t)nu * UNIT_BYTES + (size_t)nd * dbytes + (size_t)np * pbytes;
    if (nb > ICO_XB_MAX_BLOCKS || at + (size_t)nb * BLOCK_BYTES > n || ns == 0) {
        return -1;
    }
    {
        /* the names: after the blocks, the version 3 list and pages */
        size_t str_at = at + (size_t)nb * BLOCK_BYTES;

        if (ver >= 3) {
            str_at += (size_t)get32(s + H_LIST) * LIST_BYTES +
                      (size_t)get32(s + H_PAGES) * PAGE_BYTES +
                      (size_t)get32(s + H_PRODS) * PROD_BYTES;
        }
        if (str_at + ns > n || s[str_at + ns - 1] != 0) {
            return -1;
        }
        for (i = 0; i < nb; i++) {
            const unsigned char *b = s + at + (size_t)i * BLOCK_BYTES;
            const uint32_t nm = get32(b + 28);

            fn(user, nm < ns ? (const char *)s + str_at + nm : "", get64(b + 8), get32(b + 24),
               get64(b), get64(b + 16));
        }
    }
    return 0;
}

/* --- loading ------------------------------------------------------------------------ */

const char *ico_xb_kind_text(int kind)
{
    switch (kind) {
    case ICO_XB_OK:
        return "nothing";
    case ICO_XB_CODE:
        return "the game's code";
    case ICO_XB_DATA:
        return "the game's data";
    case ICO_XB_BLOCKS:
        return "the port's saved blocks";
    case ICO_XB_PINNED:
        return "nothing it can be translated with";
    }
    return "a damaged record";
}

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void verdict(IcoXbVerdict *v, int kind, const char *fmt, ...)
{
    va_list ap;

    v->kind = kind;
    va_start(ap, fmt);
    vsnprintf(v->why, sizeof v->why, fmt, ap);
    va_end(ap);
}

typedef struct Spans {
    IcoStateSpan *p;
    size_t n, cap;
    int failed;
} Spans;

static void span_add(Spans *s, uint64_t lo, uint64_t hi, uint64_t now)
{
    if (s->failed) {
        return;
    }
    if (s->n == s->cap) {
        const size_t cap = s->cap ? s->cap * 2 : 256;
        IcoStateSpan *q = (IcoStateSpan *)realloc(s->p, cap * sizeof *q);

        if (q == NULL) {
            s->failed = 1;
            return;
        }
        s->p = q;
        s->cap = cap;
    }
    s->p[s->n].lo = lo;
    s->p[s->n].hi = hi;
    s->p[s->n].now = now;
    s->n++;
}

static int span_cmp(const void *a, const void *b)
{
    const IcoStateSpan *x = (const IcoStateSpan *)a, *y = (const IcoStateSpan *)b;

    if (x->lo != y->lo) {
        return x->lo < y->lo ? -1 : 1;
    }
    return x->hi < y->hi ? -1 : x->hi > y->hi;
}

/* Sorted, so a word lands in one span only: where a span starts inside
   the one before it (a variable's included end address is the next one's
   start), the later start wins; a span wholly inside the one before it
   is dropped; a point repeated keeps its first translation. */
static void spans_settle(Spans *s, int points)
{
    size_t i, k = 0;

    if (s->n > 1) { /* an empty list's array is NULL: qsort's base is declared
                     * non-null (CI's UBSan on state_port) */
        qsort(s->p, s->n, sizeof *s->p, span_cmp);
    }
    for (i = 0; i < s->n; i++) {
        if (k > 0 && s->p[i].lo <= s->p[k - 1].hi) {
            if (points || s->p[i].hi <= s->p[k - 1].hi) {
                continue;
            }
            if (s->p[i].lo == s->p[k - 1].lo) {
                k--;
            } else {
                s->p[k - 1].hi = s->p[i].lo - 1;
            }
        }
        s->p[k++] = s->p[i];
    }
    s->n = k;
}

/* a window's text for a log line: from its word's offset to the NUL or
   the stored bytes' end, at most 40 bytes, a byte outside printable ASCII
   as '?' */
static void show_text(char *out, size_t n, const unsigned char *b, size_t stored, size_t at)
{
    size_t k = 0;

    for (; at < stored && b[at] != 0 && k + 1 < n && k < 40; at++) {
        out[k++] = b[at] >= 0x20 && b[at] < 0x7f ? (char)b[at] : '?';
    }
    out[k < n ? k : n - 1] = '\0';
}

/* the first place in this image's read-only data that holds the n bytes
   (a version 2 text point) */
static uint64_t find_text(const IcoStateImage *im, const unsigned char *b, size_t n)
{
    int i;

    for (i = 0; i < im->n_ro; i++) {
        const unsigned char *p = (const unsigned char *)(uintptr_t)im->ro_lo[i];
        const unsigned char *end = (const unsigned char *)(uintptr_t)im->ro_hi[i];

        while (p + n <= end) {
            const unsigned char *q = (const unsigned char *)memchr(p, b[0], (size_t)(end - p));

            if (q == NULL || q + n > end) {
                break;
            }
            if (memcmp(q, b, n) == 0) {
                return (uint64_t)(uintptr_t)q;
            }
            p = q + 1;
        }
    }
    return 0;
}

static void fiber_points_free(IcoXbVerdict *v)
{
    size_t i;

    for (i = 0; v->fiber_point != NULL && i < v->n_fiber; i++) {
        free(v->fiber_point[i]);
    }
    free(v->fiber_point);
    free(v->fiber_n_point);
    v->fiber_point = NULL;
    v->fiber_n_point = NULL;
    v->n_fiber = 0;
}

/* a unit with a thread inside, then and now (the page words' producers) */
typedef struct UMap {
    uint64_t old, now;
    const char *name;
} UMap;

static int umap_cmp(const void *a, const void *b)
{
    const UMap *x = (const UMap *)a, *y = (const UMap *)b;

    return x->old < y->old ? -1 : x->old > y->old;
}

static const UMap *umap_find(const UMap *m, size_t n, uint64_t old)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (m[mid].old < old) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < n && m[lo].old == old ? &m[lo] : NULL;
}

int ico_xb_check(const void *secv, size_t n, const IcoXbNow *now, IcoXbVerdict *v)
{
    const unsigned char *sec = (const unsigned char *)secv;
    uint32_t flags, nu, nd, np, nb, ns, ver, npool = 0, nl = 0, npg = 0, npr = 0;
    size_t dbytes, pbytes, hbytes, n_umap = 0;
    const unsigned char *u, *d, *pt, *bl, *ls, *pg, *prd, *pool;
    UMap *umap = NULL;
    Spans *fsp = NULL; /* per fiber slot (fiber id - 1): its page words' points */
    size_t n_fsp = 0, f_i;
    const char *str;
    IcoSymTab *t = now->tab;
    IcoStateImage im = now->image;
    uint32_t freq[256];
    int have_freq = 0;
    Spans spans = {NULL, 0, 0, 0}, points = {NULL, 0, 0, 0};
    char name[160];
    uint32_t i;
    int j;

    memset(v, 0, sizeof *v);
#define FAIL(...)                                                                                  \
    do {                                                                                           \
        verdict(__VA_ARGS__);                                                                      \
        goto fail;                                                                                 \
    } while (0)
    ver = sec != NULL && n >= H_BYTES ? get32(sec + H_VERSION) : 0;
    if (sec == NULL || n < H_BYTES || get32(sec + H_MAGIC) != ICO_XB_MAGIC || ver < 1 ||
        ver > ICO_XB_VERSION) {
        FAIL(v, ICO_XB_BAD, "the symbols record is missing or of another kind");
    }
    v->version = ver;
    dbytes = ver >= 2 ? DATA_BYTES : DATA_BYTES_V1;
    pbytes = ver >= 3 ? POINT_BYTES : POINT_BYTES_V2;
    hbytes = ver >= 3 ? H_BYTES3 : H_BYTES;
    if (ver >= 3 && n < H_BYTES3) {
        FAIL(v, ICO_XB_BAD, "the symbols record does not hold what it says");
    }
    flags = get32(sec + H_FLAGS);
    nu = get32(sec + H_UNITS);
    nd = get32(sec + H_DATA);
    np = get32(sec + H_POINTS);
    nb = get32(sec + H_BLOCKS);
    ns = get32(sec + H_STR);
    if (ver >= 3) {
        npool = get32(sec + H_POOL);
        nl = get32(sec + H_LIST);
        npg = get32(sec + H_PAGES);
        npr = get32(sec + H_PRODS);
    }
    if (nu > ICO_XB_MAX_UNITS || nd > ICO_XB_MAX_DATA || np > ICO_XB_MAX_POINTS ||
        nb > ICO_XB_MAX_BLOCKS || nl > ICO_XB_LIST || npg > ICO_XB_MAX_PAGES ||
        npr > ICO_XB_MAX_PRODS || npool > ICO_XB_POOL_BYTES || ns == 0 ||
        (uint64_t)hbytes + (uint64_t)nu * UNIT_BYTES + (uint64_t)nd * dbytes +
                (uint64_t)np * pbytes + (uint64_t)nb * BLOCK_BYTES + (uint64_t)nl * LIST_BYTES +
                (uint64_t)npg * PAGE_BYTES + (uint64_t)npr * PROD_BYTES + ns + npool !=
            n) {
        FAIL(v, ICO_XB_BAD, "the symbols record does not hold what it says");
    }
    u = sec + hbytes;
    d = u + (size_t)nu * UNIT_BYTES;
    pt = d + (size_t)nd * dbytes;
    bl = pt + (size_t)np * pbytes;
    ls = bl + (size_t)nb * BLOCK_BYTES;
    pg = ls + (size_t)nl * LIST_BYTES;
    prd = pg + (size_t)npg * PAGE_BYTES;
    str = (const char *)(prd + (size_t)npr * PROD_BYTES);
    pool = (const unsigned char *)str + ns;
    if (str[ns - 1] != '\0') {
        FAIL(v, ICO_XB_BAD, "the symbols record's names are cut");
    }
#define NAME_AT(off) ((off) < ns ? str + (off) : "")
    /* the words the save found nothing to name, as it listed them */
    for (i = 0; i < nl; i++) {
        const unsigned char *r = ls + (size_t)i * LIST_BYTES;
        IcoXbUnnamed *e = &v->list[v->listed++];

        e->word = get64(r);
        e->at = get64(r + 8);
        e->sym_off = get64(r + 16);
        snprintf(e->where, sizeof e->where, "%s", NAME_AT(get32(r + 24)));
        snprintf(e->section, sizeof e->section, "%s", NAME_AT(get32(r + 28)));
        snprintf(e->sym, sizeof e->sym, "%s", NAME_AT(get32(r + 32)));
        e->reason = get32(r + 36);
    }
    ico_xb_image(t, &im);

    /* another game data layout first: the plainest reason, and the one an
       earlier version's state of another layout meets whatever else it
       lacks (the checks below repeat it) */
    if (t != NULL && (flags & ICO_XB_TABLE) != 0 && get64(sec + H_LAYOUT) != 0 &&
        get64(sec + H_LAYOUT) != t->layout_hash) {
        FAIL(v, ICO_XB_DATA, "the game's data layout changed");
    }

    /* the blocks, by name, both ways */
    v->block_off =
        (uint64_t *)calloc(now->n_blocks > 0 ? (size_t)now->n_blocks : 1, sizeof(uint64_t));
    if (v->block_off == NULL) {
        FAIL(v, ICO_XB_BAD, "no memory for the block map");
    }
    for (j = 0; j < now->n_blocks; j++) {
        int found = 0;

        for (i = 0; i < nb; i++) {
            const unsigned char *b = bl + (size_t)i * BLOCK_BYTES;

            if (strcmp(NAME_AT(get32(b + 28)), now->block_name[j]) != 0) {
                continue;
            }
            found = 1;
            if (get64(b + 8) != now->block_size[j] ||
                (get32(b + 24) & ICO_STATE_AUDIO) != (now->block_flags[j] & ICO_STATE_AUDIO)) {
                FAIL(v, ICO_XB_BLOCKS, "the block %s changed size", now->block_name[j]);
            }
            v->block_off[j] = get64(b + 16);
            span_add(&spans, get64(b), get64(b) + get64(b + 8), now->block_addr[j]);
            break;
        }
        if (!found &&
            (now->block_flags[j] & (ICO_STATE_FRESH | ICO_STATE_SCAN)) == ICO_STATE_FRESH) {
            /* a block the version that saved it did not have, which starts
               from its initial value: this run's bytes are kept */
            v->block_off[j] = ICO_XB_BLOCK_ABSENT;
            v->fresh++;
            continue;
        }
        if (!found) {
            FAIL(v, ICO_XB_BLOCKS, "the block %s exists only here", now->block_name[j]);
        }
    }
    for (i = 0; i < nb; i++) {
        const char *nm = NAME_AT(get32(bl + (size_t)i * BLOCK_BYTES + 28));
        int found = 0;

        for (j = 0; j < now->n_blocks && !found; j++) {
            found = strcmp(nm, now->block_name[j]) == 0;
        }
        if (!found) {
            FAIL(v, ICO_XB_BLOCKS, "the block %s is missing here", nm);
        }
    }
    /* what the state needs to be translated at all */
    if ((flags & ICO_XB_TABLE) == 0) {
        FAIL(v, ICO_XB_PINNED, "the version that made it had no symbol file");
    }
    if ((flags & ICO_XB_LOW_IMAGE) != 0) {
        FAIL(v, ICO_XB_PINNED,
             "its program lay below 4 GB, where a program address cannot be told from "
             "a number");
    }
    if ((flags & ICO_XB_OVERFLOW) != 0) {
        FAIL(v, ICO_XB_PINNED,
             "it points into the program in more places than the "
             "record holds");
    }
    /* words nothing names (ICO_XB_UNNAMED): numbers where the saving
       image's upper half is 0 or 1 (no span or point holds them, so the
       load leaves them as they are); above it, where no float next to a
       small integer reaches the image, an address the table does not name */
    v->left = get32(sec + H_LEFT);
    v->first_left = get64(sec + H_UNNAMED);
    if ((flags & ICO_XB_UNNAMED) != 0 &&
        ((now->old_image_lo != 0 ? now->old_image_lo : v->first_left) >> 32) > 1u) {
        snprintf(name, sizeof name, "0x%llx", (unsigned long long)v->first_left);
        v->by_unnamed = 1;
        FAIL(v, ICO_XB_PINNED,
             "it holds a program address no symbol names (%s, %u such words): the version that "
             "saved it left them unnamed",
             name, v->left);
    }
    /* an arm64 record without the page words (a format 2 one, an rc of this
       format): its fibers' ADRP pages cannot be told from addresses */
    if ((int)get32(sec + H_ARCH) == ICO_CODE_A64 && (flags & ICO_XB_PAGES) == 0) {
        FAIL(v, ICO_XB_PINNED,
             "it was made before the pages its threads keep in registers were recorded");
    }
    if (t == NULL) {
        FAIL(v, ICO_XB_PINNED, "this build has no symbol file");
    }
    if (get32(sec + H_RULES) < 1 || get32(sec + H_RULES) > ICO_CODE_HASH_RULES ||
        (int)get32(sec + H_ARCH) != t->env.arch) {
        FAIL(v, ICO_XB_PINNED, "its code was compared under other rules");
    }
    if (get64(sec + H_LAYOUT) != t->layout_hash) {
        FAIL(v, ICO_XB_DATA, "the game's data layout changed");
    }
    /* the game's two ranges move whole */
    for (j = 0; j < 2; j++) {
        if (now->old_game_lo[j] < now->old_game_hi[j]) {
            span_add(&spans, now->old_game_lo[j], now->old_game_hi[j], now->game_lo[j]);
        }
    }
    /* the units */
    umap = (UMap *)malloc((nu ? nu : 1) * sizeof *umap);
    if (umap == NULL) {
        FAIL(v, ICO_XB_BAD, "no memory for the units' map");
    }
    for (i = 0; i < nu; i++) {
        const unsigned char *r = u + (size_t)i * UNIT_BYTES;
        const uint64_t start = get64(r), size = get64(r + 8);
        const unsigned f = get32(r + 40);
        const char *nm = NAME_AT(get32(r + 32));
        const IcoSymUnit *w = ico_symtab_unit_named(t, get64(r + 16), nm, get32(r + 36));
        const int strict = (f & (XU_START | XU_POSITION)) != 0;
        int same = 0;

        if (w == NULL) {
            if (strict) {
                FAIL(v, ICO_XB_CODE, "the function %s is missing here", nm);
            }
            v->soft_left++;
            continue;
        }
        same = ico_symtab_unit_hash_rules(t, w, (int)get32(sec + H_RULES)) == get64(r + 24);
        if (!same && ((f & XU_POSITION) != 0 ||
                      ((f & XU_START) != 0 && !((f & XU_UNIQUE) != 0 && w->unique)))) {
            FAIL(v, ICO_XB_CODE, "the function %s changed", nm);
        }
        if (same && strict) {
            umap[n_umap].old = start;
            umap[n_umap].now = w->start;
            umap[n_umap].name = nm;
            n_umap++;
        }
        if (same && size > 0) {
            span_add(&spans, start, start + size - 1, w->start);
        } else if ((f & XU_START) != 0) {
            span_add(&points, start, start, w->start);
        } else {
            v->soft_left++;
        }
        v->units++;
    }
    /* the named data */
    for (i = 0; i < nd; i++) {
        const unsigned char *r = d + (size_t)i * dbytes;
        const char *nm = NAME_AT(get32(r + 24));
        const IcoSymData *w = ico_symtab_data_named(t, get64(r + 16), nm, get32(r + 28));
        const unsigned df = get32(r + 32);

        if (w == NULL) {
            FAIL(v, ICO_XB_DATA, "the variable %s is missing here", nm);
        }
        if (ver >= 3 && (df & XD_MASKED) != 0) {
            /* one of a repeated name in data read-only after loading: the
               same place among its namesakes, the same size and the same
               contents, its pointers masked */
            if (!w->exact || w->size != get64(r + 8) ||
                masked_hash(w->start, w->size, &im) != get64(r + 40)) {
                FAIL(v, ICO_XB_DATA, "the read-only data %s (number %u of its name) changed", nm,
                     (unsigned)get32(r + 28) + 1u);
            }
            span_add(&spans, get64(r), get64(r) + get64(r + 8), w->start);
            v->data++;
            v->data_masked++;
            continue;
        }
        if (!w->unique) {
            FAIL(v, ICO_XB_DATA, "the variable %s is not the only one of its name here", nm);
        }
        if ((df & XD_EXACT) != 0 && w->exact && w->size != get64(r + 8)) {
            FAIL(v, ICO_XB_DATA, "the variable %s changed size", nm);
        }
        /* a size the file did not give: a word past the symbol may point at
           a static of its object, at the same offset only while the object's
           section is laid out alike */
        if (ver >= 2 && (df & XD_SECTION) != 0 &&
            (w->sec_size != get64(r + 40) || w->sec_off != get32(r + 36))) {
            FAIL(v, ICO_XB_DATA, "the data around the variable %s changed", nm);
        }
        span_add(&spans, get64(r), get64(r) + get64(r + 8), w->start);
        v->data++;
    }
    /* the points */
    for (i = 0; i < np; i++) {
        const unsigned char *r = pt + (size_t)i * pbytes;
        const uint64_t old = get64(r);
        const unsigned kind = get32(r + 8);

        if (kind == XP_SNAP) {
            const uint64_t off = get64(r + 16);
            uint64_t w;

            if (now->snap == NULL || off / 8 >= now->snap_words) {
                FAIL(v, ICO_XB_DATA, "the game's start-up data changed");
            }
            w = now->snap[off / 8];
            if (w == 0 || w < now->image.lo || w > now->image.hi) {
                FAIL(v, ICO_XB_DATA, "the game's start-up data changed");
            }
            span_add(&points, old, old, w);
        } else if (kind == XP_WINDOW && ver >= 3) {
            const uint32_t len = get32(r + 12), at = get32(r + 16), wf = get32(r + 20);
            const uint32_t pool_at = get32(r + 32), stored = get32(r + 36);
            const int text = (wf & XW_TEXT) != 0, hashed = (wf & XW_HASHED) != 0;
            uint64_t w = 0;
            unsigned found;
            char what[120];

            if (len == 0 || at > len || stored == 0 || (uint64_t)pool_at + stored > npool ||
                (hashed ? stored > len || stored > ICO_XB_CONTENT : stored != len)) {
                FAIL(v, ICO_XB_BAD, "the symbols record's window is damaged");
            }
            if (++v->searches > ICO_XB_MAX_SEARCHES) {
                FAIL(v, ICO_XB_PINNED, "it points at more texts than a load looks for");
            }
            if (!have_freq) {
                ro_freq(&im, 0, freq);
                have_freq = 1;
            }
            found = find_in(&im, 0, freq, pool + pool_at, stored, len, hashed, get64(r + 24),
                            (wf & XW_LSTART) != 0, 2, &w);
            /* whole literals (the window starts where a literal starts and
               ends in its zero byte) found at several literal starts: the
               copies hold the same bytes, which C code reads alike; a word
               naming one is told from its twin only by comparing it with
               another pointer, and every word of one value takes the same
               twin (the translation maps by value), so the first in address
               order is taken and the twins counted */
            if (found == 2 && text && !hashed && (wf & XW_LSTART) != 0 && stored == len &&
                pool[pool_at + stored - 1] == 0) {
                found = 1;
                v->twins++;
            }
            if (found != 1) {
                if (text) {
                    char shown[48];

                    show_text(shown, sizeof shown, pool + pool_at, stored, at);
                    snprintf(what, sizeof what, "the text \"%s\"", shown);
                } else {
                    const char *sym = NAME_AT(get32(r + 40));

                    snprintf(what, sizeof what, "the read-only data at %s+0x%llx (%u bytes)",
                             sym[0] != '\0' ? sym : "?", (unsigned long long)get64(r + 48),
                             (unsigned)len);
                }
                FAIL(v, ICO_XB_DATA, "%s is %s here", what,
                     found == 0 ? "missing" : "in more than one place");
            }
            span_add(&points, old, old, w + at);
            v->windows++;
        } else if (kind == XP_TEXT && ver < 3) {
            const uint32_t len = get32(r + 12);
            uint64_t w;

            if (len == 0 || len > ICO_XB_CONTENT || r[24 + len - 1] != 0) {
                FAIL(v, ICO_XB_BAD, "the symbols record's text is damaged");
            }
            if (++v->searches > ICO_XB_MAX_SEARCHES) {
                FAIL(v, ICO_XB_PINNED, "it points at more texts than a load looks for");
            }
            w = find_text(&im, r + 24, len);
            if (w == 0) {
                snprintf(name, sizeof name, "%s", (const char *)(r + 24));
                FAIL(v, ICO_XB_DATA, "the text \"%s\" is missing here", name);
            }
            span_add(&points, old, old, w);
        } else {
            FAIL(v, ICO_XB_BAD, "the symbols record has a point of another kind");
        }
        v->points++;
    }
#undef NAME_AT
    if (spans.failed || points.failed) {
        FAIL(v, ICO_XB_BAD, "no memory for the translation");
    }
    spans_settle(&spans, 0);
    spans_settle(&points, 1);
    /* the page words (arm64, state_xbuild.h): each entry's reading; a word
       its producers name becomes a point of its own fiber */
    if (npg > 0) {
        IcoStateXlate xl;

        if (n_umap > 1) {
            qsort(umap, n_umap, sizeof *umap, umap_cmp);
        }
        for (i = 0; i < npg; i++) {
            const uint32_t fib = get32(pg + (size_t)i * PAGE_BYTES + 8);

            if (fib == 0 || fib > 65536u) {
                FAIL(v, ICO_XB_BAD, "the symbols record's page word names no fiber");
            }
            if ((size_t)fib > n_fsp) {
                n_fsp = fib;
            }
        }
        fsp = (Spans *)calloc(n_fsp, sizeof *fsp);
        if (fsp == NULL) {
            FAIL(v, ICO_XB_BAD, "no memory for the page words");
        }
        memset(&xl, 0, sizeof xl);
        xl.span = spans.p;
        xl.n_span = spans.n;
        xl.point = points.p;
        xl.n_point = points.n;
        for (i = 0; i < npg; i++) {
            const unsigned char *e = pg + (size_t)i * PAGE_BYTES;
            const uint64_t w = get64(e);
            const uint32_t fib = get32(e + 8), fl = get32(e + 12);
            uint64_t q = 0, to = 0, k;
            const char *nm0 = "";
            uint32_t off0 = 0;
            int have_q = 0, have_n;

            for (k = 0; k < npr; k++) {
                const unsigned char *pp = prd + (size_t)k * PROD_BYTES;
                const UMap *m;
                uint64_t np2;

                if (get32(pp + 20) != fib || get64(pp + 8) != w) {
                    continue;
                }
                m = umap_find(umap, n_umap, get64(pp));
                if (m == NULL) {
                    FAIL(v, ICO_XB_BAD,
                         "the symbols record's page word names no function with a "
                         "thread inside");
                }
                np2 = adrp_page(m->now + get32(pp + 16), code32(m->now + get32(pp + 16)));
                if (np2 == 1) {
                    FAIL(v, ICO_XB_CODE, "the function %s changed", m->name);
                }
                if (!have_q) {
                    q = np2;
                    have_q = 1;
                    nm0 = m->name;
                    off0 = get32(pp + 16);
                } else if (np2 != q) {
                    snprintf(name, sizeof name, "%s+0x%x and %s+0x%x", nm0, (unsigned)off0, m->name,
                             (unsigned)get32(pp + 16));
                    FAIL(v, ICO_XB_DATA,
                         "fiber slot %u holds the page 0x%llx, which ADRPs in %s make: here they "
                         "make two pages, and which one it came from cannot be told",
                         (unsigned)(fib - 1u), (unsigned long long)w, name);
                }
            }
            have_n = ico_state_xlate(&xl, w, &to);
            if (have_q) {
                if ((fl & PG_ORDINARY) != 0 && have_n && to != q) {
                    snprintf(name, sizeof name, "%s+0x%x", nm0, (unsigned)off0);
                    FAIL(v, ICO_XB_DATA,
                         "0x%llx is both the page an ADRP in %s makes and an address of its own, "
                         "and here the two lie apart (0x%llx, 0x%llx)",
                         (unsigned long long)w, name, (unsigned long long)q,
                         (unsigned long long)to);
                }
                span_add(&fsp[fib - 1], w, w, q);
                v->pages++;
            } else {
                FAIL(v, ICO_XB_BAD, "the symbols record's page word has no producer");
            }
        }
        v->fiber_point = (IcoStateSpan **)calloc(n_fsp, sizeof *v->fiber_point);
        v->fiber_n_point = (size_t *)calloc(n_fsp, sizeof *v->fiber_n_point);
        if (v->fiber_point == NULL || v->fiber_n_point == NULL) {
            FAIL(v, ICO_XB_BAD, "no memory for the page words");
        }
        v->n_fiber = n_fsp;
        for (f_i = 0; f_i < n_fsp; f_i++) {
            if (fsp[f_i].failed) {
                FAIL(v, ICO_XB_BAD, "no memory for the page words");
            }
            spans_settle(&fsp[f_i], 1);
            v->fiber_point[f_i] = fsp[f_i].p;
            v->fiber_n_point[f_i] = fsp[f_i].n;
            fsp[f_i].p = NULL;
        }
    }
    v->span = spans.p;
    v->n_span = spans.n;
    v->point = points.p;
    v->n_point = points.n;
    v->kind = ICO_XB_OK;
    free(umap);
    free(fsp);
    return ICO_XB_OK;
fail:
#undef FAIL
    free(umap);
    for (f_i = 0; f_i < n_fsp; f_i++) {
        free(fsp[f_i].p);
    }
    free(fsp);
    fiber_points_free(v);
    free(spans.p);
    free(points.p);
    return v->kind;
}

void ico_xb_verdict_free(IcoXbVerdict *v)
{
    fiber_points_free(v);
    free(v->span);
    free(v->point);
    free(v->block_off);
    v->span = v->point = NULL;
    v->block_off = NULL;
    v->n_span = v->n_point = 0;
}
