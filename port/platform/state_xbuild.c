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
    H_BYTES = 64
};

#define UNIT_BYTES 48u
#define DATA_BYTES 48u /* 40 in a version 1 record: no input section */
#define DATA_BYTES_V1 40u
#define POINT_BYTES 48u
#define BLOCK_BYTES 40u

/* a unit record's flags */
#define XU_START 1u    /* a strict word at its start (a function pointer) */
#define XU_POSITION 2u /* a strict word inside it (a return address) */
#define XU_SOFT 4u     /* a word of a dead stack frame */
#define XU_UNIQUE 8u   /* its name is unique in the saving build */
/* a data record's flags */
#define XD_EXACT 1u
#define XD_SECTION 2u /* the input section holding it: its size and the symbol's offset */
/* a point's kinds */
#define XP_SNAP 1u /* aux: the byte offset of the value in the game's data range */
#define XP_TEXT 2u /* content: a C string */

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

typedef struct BlockRec {
    const char *name;
    uint64_t size, addr, offset;
    unsigned flags;
} BlockRec;

struct IcoXbRefs {
    IcoXbProc p;
    uint64_t *key; /* open addressing over ICO_XB_MAX_VALUES slots; 0 empty */
    uint8_t *strict;
    size_t n_vals;
    int overflow;
    BlockRec block[ICO_XB_MAX_BLOCKS];
    int n_blocks;
    SnapPair *snap;
    size_t n_snap;
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

IcoXbRefs *ico_xb_begin(const IcoXbProc *p)
{
    IcoXbRefs *r = (IcoXbRefs *)calloc(1, sizeof *r);
    size_t i;

    if (r == NULL) {
        return NULL;
    }
    r->p = *p;
    r->key = (uint64_t *)calloc(ICO_XB_MAX_VALUES, sizeof *r->key);
    r->strict = (uint8_t *)calloc(ICO_XB_MAX_VALUES, 1);
    if (r->key == NULL || r->strict == NULL) {
        free(r->key);
        free(r->strict);
        free(r);
        return NULL;
    }
    /* the game data's start-up words that point into the image, by value
       (the first offset of each value wins) */
    if (p->snap != NULL && p->snap_words > 0) {
        r->snap = (SnapPair *)malloc(p->snap_words * sizeof *r->snap);
        if (r->snap != NULL) {
            for (i = 0; i < p->snap_words; i++) {
                if (p->snap[i] != 0 && in_image(p, p->snap[i])) {
                    r->snap[r->n_snap].value = p->snap[i];
                    r->snap[r->n_snap].off = (uint64_t)i * 8u;
                    r->n_snap++;
                }
            }
            qsort(r->snap, r->n_snap, sizeof *r->snap, snap_cmp);
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
        r->n_vals++;
    }
    r->strict[s] |= (uint8_t)(strict != 0);
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
            ico_xb_word(r, w, strict);
        }
    }
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

static int in_ro(const IcoStateImage *im, uint64_t lo, uint64_t hi)
{
    int i;

    for (i = 0; i < im->n_ro; i++) {
        if (lo >= im->ro_lo[i] && hi <= im->ro_hi[i]) {
            return 1;
        }
    }
    return 0;
}

/* the C string at w when it lies in read-only data: its length with the
   NUL (at most ICO_XB_CONTENT), or 0 */
static size_t c_string(const IcoStateImage *im, uint64_t w)
{
    size_t i;

    if (!in_ro(im, w, w + 1)) {
        return 0;
    }
    for (i = 0; i < ICO_XB_CONTENT; i++) {
        unsigned char c;

        if (!in_ro(im, w + i, w + i + 1)) {
            return 0;
        }
        c = *(const unsigned char *)(uintptr_t)(w + i);
        if (c == 0) {
            return i + 1;
        }
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return 0;
        }
    }
    return 0;
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

int ico_xb_finish(IcoXbRefs *r, void **out, size_t *n, IcoXbStats *st)
{
    const IcoXbProc *p = &r->p;
    IcoSymTab *t = p->tab;
    uint8_t *uflag = NULL;
    unsigned char *dflag = NULL;
    Out units = {NULL, 0, 0, 0}, data = {NULL, 0, 0, 0}, points = {NULL, 0, 0, 0},
        blocks = {NULL, 0, 0, 0}, strs = {NULL, 0, 0, 0};
    unsigned flags = 0, nu = 0, nd = 0, np = 0, npos = 0, left = 0;
    uint64_t first_unnamed = 0;
    unsigned char *sec, *q;
    size_t i, total;
    int rc = -1;

    memset(st, 0, sizeof *st);
    *out = NULL;
    *n = 0;
    if (t != NULL) {
        flags |= ICO_XB_TABLE;
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
    (void)str_put(&strs, ""); /* offset 0: no name */
    /* each distinct word: what it points into */
    for (i = 0; t != NULL && i < ICO_XB_MAX_VALUES; i++) {
        const uint64_t w = r->key[i];
        const int strict = r->strict[i] != 0;
        int b, in_block = 0;

        if (w == 0) {
            continue;
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
                flags |= ICO_XB_UNNAMED;
                left++;
                if (first_unnamed == 0) {
                    first_unnamed = w;
                }
            }
            continue;
        }
        if (in_game(p->game_lo, p->game_hi, w)) {
            continue;
        }
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

            if (d != NULL && d->unique) {
                dflag[d - t->data] = 1;
                continue;
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
        {
            const size_t len = c_string(&p->image, w);

            if (len > 0) {
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
                put32(q + 8, XP_TEXT);
                put32(q + 12, (uint32_t)len);
                memcpy(q + 24, (const void *)(uintptr_t)w, len);
                np++;
                continue;
            }
        }
        /* nothing names it: a number that lies in the image's range (a
           float next to the integer 1 under Windows' 0x140000000, a word
           into the image's headers), kept as it is */
        flags |= ICO_XB_UNNAMED;
        left++;
        if (first_unnamed == 0) {
            first_unnamed = w;
        }
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
        put64(q + 24, ico_symtab_unit_hash(t, u));
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
        put32(q + 32, (d->exact ? XD_EXACT : 0u) | (d->sec_size != 0 ? XD_SECTION : 0u));
        put32(q + 36, (uint32_t)d->sec_off);
        put64(q + 40, d->sec_size);
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
    if (units.failed || data.failed || points.failed || blocks.failed || strs.failed) {
        goto done;
    }
    total = H_BYTES + units.n + data.n + points.n + blocks.n + strs.n;
    sec = (unsigned char *)malloc(total);
    if (sec == NULL) {
        goto done;
    }
    memset(sec, 0, H_BYTES);
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
    q = sec + H_BYTES;
    put_bytes(q, units.p, units.n);
    q += units.n;
    put_bytes(q, data.p, data.n);
    q += data.n;
    put_bytes(q, points.p, points.n);
    q += points.n;
    put_bytes(q, blocks.p, blocks.n);
    q += blocks.n;
    put_bytes(q, strs.p, strs.n);
    *out = sec;
    *n = total;
    st->units = nu;
    st->positions = npos;
    st->data = nd;
    st->points = np;
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
    free(uflag);
    free(dflag);
    free(r->key);
    free(r->strict);
    free(r->snap);
    free(r);
    return rc;
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

    qsort(s->p, s->n, sizeof *s->p, span_cmp);
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

/* the first place in this image's read-only data that holds the n bytes */
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

int ico_xb_check(const void *secv, size_t n, const IcoXbNow *now, IcoXbVerdict *v)
{
    const unsigned char *sec = (const unsigned char *)secv;
    uint32_t flags, nu, nd, np, nb, ns, ver;
    size_t dbytes;
    const unsigned char *u, *d, *pt, *bl;
    const char *str;
    IcoSymTab *t = now->tab;
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
    dbytes = ver >= 2 ? DATA_BYTES : DATA_BYTES_V1;
    flags = get32(sec + H_FLAGS);
    nu = get32(sec + H_UNITS);
    nd = get32(sec + H_DATA);
    np = get32(sec + H_POINTS);
    nb = get32(sec + H_BLOCKS);
    ns = get32(sec + H_STR);
    if (nu > ICO_XB_MAX_UNITS || nd > ICO_XB_MAX_DATA || np > ICO_XB_MAX_POINTS ||
        nb > ICO_XB_MAX_BLOCKS || ns == 0 ||
        (uint64_t)H_BYTES + (uint64_t)nu * UNIT_BYTES + (uint64_t)nd * dbytes +
                (uint64_t)np * POINT_BYTES + (uint64_t)nb * BLOCK_BYTES + ns !=
            n) {
        FAIL(v, ICO_XB_BAD, "the symbols record does not hold what it says");
    }
    u = sec + H_BYTES;
    d = u + (size_t)nu * UNIT_BYTES;
    pt = d + (size_t)nd * dbytes;
    bl = pt + (size_t)np * POINT_BYTES;
    str = (const char *)(bl + (size_t)nb * BLOCK_BYTES);
    if (str[ns - 1] != '\0') {
        FAIL(v, ICO_XB_BAD, "the symbols record's names are cut");
    }
#define NAME_AT(off) ((off) < ns ? str + (off) : "")

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
        FAIL(v, ICO_XB_PINNED,
             "it holds a program address no symbol names (%s, %u such words): the version that "
             "saved it left them unnamed",
             name, v->left);
    }
    if (t == NULL) {
        FAIL(v, ICO_XB_PINNED, "this build has no symbol file");
    }
    if (get32(sec + H_RULES) != ICO_CODE_HASH_RULES || (int)get32(sec + H_ARCH) != t->env.arch) {
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
        same = ico_symtab_unit_hash(t, w) == get64(r + 24);
        if (!same && ((f & XU_POSITION) != 0 ||
                      ((f & XU_START) != 0 && !((f & XU_UNIQUE) != 0 && w->unique)))) {
            FAIL(v, ICO_XB_CODE, "the function %s changed", nm);
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
        const unsigned char *r = pt + (size_t)i * POINT_BYTES;
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
        } else if (kind == XP_TEXT) {
            const uint32_t len = get32(r + 12);
            uint64_t w;

            if (len == 0 || len > ICO_XB_CONTENT || r[24 + len - 1] != 0) {
                FAIL(v, ICO_XB_BAD, "the symbols record's text is damaged");
            }
            if (++v->searches > ICO_XB_MAX_SEARCHES) {
                FAIL(v, ICO_XB_PINNED, "it points at more texts than a load looks for");
            }
            w = find_text(&now->image, r + 24, len);
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
    v->span = spans.p;
    v->n_span = spans.n;
    v->point = points.p;
    v->n_point = points.n;
    v->kind = ICO_XB_OK;
    return ICO_XB_OK;
fail:
#undef FAIL
    free(spans.p);
    free(points.p);
    return v->kind;
}

void ico_xb_verdict_free(IcoXbVerdict *v)
{
    free(v->span);
    free(v->point);
    free(v->block_off);
    v->span = v->point = NULL;
    v->block_off = NULL;
    v->n_span = v->n_point = 0;
}
