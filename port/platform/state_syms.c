/*
 * port/platform/state_syms.c
 *
 * The program's symbol table in memory (state_syms.h): the three symbol
 * file formats read line by line into items, placed by the anchor, then
 * sorted into the code's units, the named data and the game data layout.
 */
#include "state_syms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_hash.h"
#include "miniz.h"
#include "xxh3.h"

/* --- the items the file names --------------------------------------------------- */

enum { IT_SYM = 0, IT_SEC = 1 };

typedef struct Item {
    uint64_t addr, size; /* the file's address; size when sized */
    uint32_t name;       /* offset in Build.str */
    uint32_t seq;        /* the file's order */
    uint8_t kind, sized;
} Item;

typedef struct Build {
    Item *it;
    size_t n, cap;
    char *str;
    size_t str_n, str_cap;
    int failed;
} Build;

static uint32_t str_add(Build *b, const char *s, size_t n)
{
    uint32_t at;

    if (b->failed) {
        return 0;
    }
    if (b->str_n + n + 1 > b->str_cap) {
        size_t cap = b->str_cap ? b->str_cap * 2 : 65536;
        char *p;

        while (cap < b->str_n + n + 1) {
            cap *= 2;
        }
        if (cap > 0xFFFFFFFFu) {
            b->failed = 1;
            return 0;
        }
        p = (char *)realloc(b->str, cap);
        if (p == NULL) {
            b->failed = 1;
            return 0;
        }
        b->str = p;
        b->str_cap = cap;
    }
    at = (uint32_t)b->str_n;
    memcpy(b->str + b->str_n, s, n);
    b->str[b->str_n + n] = '\0';
    b->str_n += n + 1;
    return at;
}

static void item_add(Build *b, int kind, uint64_t addr, uint64_t size, int sized, const char *name,
                     size_t name_n, const char *name2, size_t name2_n)
{
    Item *e;

    if (b->failed || addr == 0 || name_n == 0) {
        return;
    }
    if (b->n == b->cap) {
        const size_t cap = b->cap ? b->cap * 2 : 16384;
        Item *p = (Item *)realloc(b->it, cap * sizeof *p);

        if (p == NULL) {
            b->failed = 1;
            return;
        }
        b->it = p;
        b->cap = cap;
    }
    e = &b->it[b->n];
    e->addr = addr;
    e->size = size;
    e->sized = (uint8_t)(sized != 0);
    e->kind = (uint8_t)kind;
    e->seq = (uint32_t)b->n;
    if (name2 != NULL) {
        /* an input section: "<object>:<section>" */
        char tmp[512];
        const int k =
            snprintf(tmp, sizeof tmp, "%.*s:%.*s", (int)name_n, name, (int)name2_n, name2);

        if (k <= 0) {
            return;
        }
        e->name = str_add(b, tmp, (size_t)k < sizeof tmp ? (size_t)k : sizeof tmp - 1);
    } else {
        e->name = str_add(b, name, name_n);
    }
    if (!b->failed) {
        b->n++;
    }
}

/* --- lines -------------------------------------------------------------------------- */

#define TOKENS 8

typedef struct Tok {
    const char *s[TOKENS];
    size_t n[TOKENS];
    int count; /* may exceed TOKENS */
} Tok;

static void tokenize(const char *p, const char *end, Tok *t)
{
    t->count = 0;
    for (;;) {
        const char *b;

        while (p < end && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (p == end) {
            return;
        }
        b = p;
        while (p < end && *p != ' ' && *p != '\t') {
            p++;
        }
        if (t->count < TOKENS) {
            t->s[t->count] = b;
            t->n[t->count] = (size_t)(p - b);
        }
        t->count++;
    }
}

/* a hex number (with "0x" when need_0x): 1 and *v, else 0 */
static int hexnum(const char *s, size_t n, int need_0x, uint64_t *v)
{
    uint64_t x = 0;
    size_t i = 0;

    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
    } else if (need_0x) {
        return 0;
    }
    if (i == n || n - i > 16) {
        return 0;
    }
    for (; i < n; i++) {
        const char c = s[i];
        int d;

        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return 0;
        }
        x = x << 4 | (uint64_t)d;
    }
    *v = x;
    return 1;
}

static int decnum(const char *s, size_t n)
{
    size_t i;

    if (n == 0 || n > 18) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }
    return 1;
}

static int tok_is(const char *s, size_t n, const char *w)
{
    return strlen(w) == n && memcmp(s, w, n) == 0;
}

/* "libico_game.a(main.c.o)" -> "main.c.o"; "dir/x.dir/rd_blur.c.o" ->
   "rd_blur.c.o" (symmap.c's obj_name) */
static void obj_base(const char *s, size_t n, const char **out, size_t *out_n)
{
    const char *open = (const char *)memchr(s, '(', n);
    size_t i;

    if (open != NULL && n > 0 && s[n - 1] == ')') {
        *out = open + 1;
        *out_n = (size_t)(s + n - 1 - (open + 1));
        return;
    }
    for (i = n; i > 0; i--) {
        if (s[i - 1] == '/' || s[i - 1] == '\\') {
            break;
        }
    }
    *out = s + i;
    *out_n = n - i;
}

/* the nm symbol types kept: code, data, read-only data, bss, weak */
static int nm_type(char c)
{
    return c != '\0' && strchr("TtWwDdBbRrVvSsGg", c) != NULL;
}

typedef struct ParseState {
    int lld;
    size_t lld_in_col, lld_sym_col;
    int discarded;
    char pending[256];
} ParseState;

static void parse_line(Build *b, ParseState *st, const char *line, const char *end)
{
    Tok t;
    uint64_t a, s;
    const int lead = line < end && (line[0] == ' ' || line[0] == '\t');

    tokenize(line, end, &t);
    if (t.count == 0) {
        st->pending[0] = '\0';
        return;
    }
    if (st->lld) {
        /* lld's ELF map: VMA LMA Size Align and a name whose column says
           what it is (an output section, an input section, a symbol) */
        uint64_t lma;
        size_t col;

        if (t.count < 5 || !hexnum(t.s[0], t.n[0], 0, &a) || !hexnum(t.s[1], t.n[1], 0, &lma) ||
            !hexnum(t.s[2], t.n[2], 0, &s) || !decnum(t.s[3], t.n[3])) {
            return;
        }
        col = (size_t)(t.s[4] - line);
        if (col >= st->lld_sym_col) {
            if (t.count == 5) {
                item_add(b, IT_SYM, a, s, s > 0, t.s[4], t.n[4], NULL, 0);
            }
        } else if (col >= st->lld_in_col) {
            /* "file:(section)" */
            const char *q = (const char *)memchr(t.s[4], ':', t.n[4]);
            const char *o;
            size_t on;

            if (q != NULL && q + 2 < t.s[4] + t.n[4] && q[1] == '(' && t.s[4][t.n[4] - 1] == ')') {
                obj_base(t.s[4], (size_t)(q - t.s[4]), &o, &on);
                item_add(b, IT_SEC, a, s, 1, o, on, q + 2, (size_t)(t.s[4] + t.n[4] - 1 - (q + 2)));
            }
        }
        return;
    }
    if (t.count == 7 && tok_is(t.s[0], t.n[0], "VMA") && tok_is(t.s[1], t.n[1], "LMA") &&
        tok_is(t.s[2], t.n[2], "Size") && tok_is(t.s[3], t.n[3], "Align") &&
        tok_is(t.s[4], t.n[4], "Out") && tok_is(t.s[5], t.n[5], "In") &&
        tok_is(t.s[6], t.n[6], "Symbol")) {
        st->lld = 1;
        st->lld_in_col = (size_t)(t.s[5] - line);
        st->lld_sym_col = (size_t)(t.s[6] - line);
        return;
    }
    if (!lead) {
        /* column 0: nm's lines, or a GNU map's headings and output
           sections (not needed: their input sections and symbols are) */
        st->pending[0] = '\0';
        if (t.count >= 3 && tok_is(t.s[0], t.n[0], "Discarded") &&
            tok_is(t.s[1], t.n[1], "input")) {
            st->discarded = 1;
        } else if ((t.count >= 2 && tok_is(t.s[0], t.n[0], "Memory") &&
                    tok_is(t.s[1], t.n[1], "Configuration")) ||
                   (t.count >= 3 && tok_is(t.s[0], t.n[0], "Linker") &&
                    tok_is(t.s[1], t.n[1], "script"))) {
            st->discarded = 0;
        } else if (t.count == 3 && t.n[1] == 1 && hexnum(t.s[0], t.n[0], 0, &a) &&
                   nm_type(t.s[1][0])) {
            item_add(b, IT_SYM, a, 0, 0, t.s[2], t.n[2], NULL, 0);
        } else if (t.count == 4 && t.n[2] == 1 && hexnum(t.s[0], t.n[0], 0, &a) &&
                   hexnum(t.s[1], t.n[1], 0, &s) && nm_type(t.s[2][0])) {
            item_add(b, IT_SYM, a, s, 1, t.s[3], t.n[3], NULL, 0);
        }
        return;
    }
    if (st->discarded || t.s[0][0] == '*') {
        st->pending[0] = '\0';
        return;
    }
    /* a GNU map's input section: " .text  0xADDR  0xSIZE  object", or its
       name alone with the numbers on the next line */
    if (t.count == 1 && !hexnum(t.s[0], t.n[0], 1, &a)) {
        snprintf(st->pending, sizeof st->pending, "%.*s", (int)t.n[0], t.s[0]);
        return;
    }
    if (t.count >= 4 && !hexnum(t.s[0], t.n[0], 1, &a) && hexnum(t.s[1], t.n[1], 1, &a) &&
        hexnum(t.s[2], t.n[2], 1, &s)) {
        const char *o;
        size_t on;

        obj_base(t.s[3], t.n[3], &o, &on);
        item_add(b, IT_SEC, a, s, 1, o, on, t.s[0], t.n[0]);
        st->pending[0] = '\0';
        return;
    }
    if (st->pending[0] != '\0' && t.count >= 3 && hexnum(t.s[0], t.n[0], 1, &a) &&
        hexnum(t.s[1], t.n[1], 1, &s)) {
        const char *o;
        size_t on;

        obj_base(t.s[2], t.n[2], &o, &on);
        item_add(b, IT_SEC, a, s, 1, o, on, st->pending, strlen(st->pending));
        st->pending[0] = '\0';
        return;
    }
    st->pending[0] = '\0';
    /* a symbol: "                0xADDR                name" */
    if (t.count == 2 && hexnum(t.s[0], t.n[0], 1, &a) && !hexnum(t.s[1], t.n[1], 1, &s)) {
        item_add(b, IT_SYM, a, 0, 0, t.s[1], t.n[1], NULL, 0);
    }
}

/* --- sorting ------------------------------------------------------------------------- */

typedef struct Pos {
    uint64_t at; /* now */
    uint64_t end;
    uint32_t name;
    uint32_t seq;
    uint8_t kind, sized;
} Pos;

static int pos_cmp(const void *a, const void *b)
{
    const Pos *x = (const Pos *)a, *y = (const Pos *)b;

    if (x->at != y->at) {
        return x->at < y->at ? -1 : 1;
    }
    if (x->kind != y->kind) {
        return x->kind < y->kind ? -1 : 1; /* a symbol before a section at one address */
    }
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

static void pos_sort(Pos *p, size_t n)
{
    if (n > 1) {
        qsort(p, n, sizeof(Pos), pos_cmp);
    }
}

typedef struct NameKey {
    uint64_t hash;
    const char *name;
    uint64_t at;
    uint32_t idx;
} NameKey;

static int name_cmp(const void *a, const void *b)
{
    const NameKey *x = (const NameKey *)a, *y = (const NameKey *)b;
    int c;

    if (x->hash != y->hash) {
        return x->hash < y->hash ? -1 : 1;
    }
    c = strcmp(x->name, y->name);
    if (c != 0) {
        return c;
    }
    return x->at < y->at ? -1 : x->at > y->at;
}

uint64_t ico_symtab_name_hash(const char *name)
{
    return xxh3_64(name, strlen(name));
}

/* The names' index: entry indices sorted by (name hash, name, address),
   each entry's hash, ordinal among its name's entries and uniqueness out. */
static uint32_t *index_names(const char *str, size_t count, const uint32_t *names,
                             const uint64_t *starts, uint64_t *hash_out, uint32_t *ord_out,
                             uint8_t *uniq_out)
{
    NameKey *k = (NameKey *)malloc((count ? count : 1) * sizeof *k);
    uint32_t *idx = (uint32_t *)malloc((count ? count : 1) * sizeof *idx);
    size_t i, j, m;

    if (k == NULL || idx == NULL) {
        free(k);
        free(idx);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        k[i].hash = ico_symtab_name_hash(str + names[i]);
        k[i].name = str + names[i];
        k[i].at = starts[i];
        k[i].idx = (uint32_t)i;
        hash_out[i] = k[i].hash;
    }
    qsort(k, count, sizeof *k, name_cmp);
    for (i = 0; i < count; i = j) {
        for (j = i + 1; j < count && k[j].hash == k[i].hash && strcmp(k[j].name, k[i].name) == 0;
             j++) {}
        for (m = i; m < j; m++) {
            ord_out[k[m].idx] = (uint32_t)(m - i);
            uniq_out[k[m].idx] = (uint8_t)(j - i == 1);
            idx[m] = k[m].idx;
        }
    }
    free(k);
    return idx;
}

/* --- the table ------------------------------------------------------------------------ */

static int same_name(const char *s, const char *name)
{
    if (s[0] == '_' && name[0] != '_') {
        s++;
    } else if (name[0] == '_' && s[0] != '_') {
        name++;
    }
    return strcmp(s, name) == 0;
}

static int find_sym(const Build *b, const char *name, uint64_t *addr)
{
    size_t i;

    for (i = 0; i < b->n; i++) {
        if (b->it[i].kind == IT_SYM && same_name(b->str + b->it[i].name, name)) {
            *addr = b->it[i].addr;
            return 1;
        }
    }
    return 0;
}

static int in_game(const IcoSymTabEnv *e, uint64_t a, int *r)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (e->game_lo[i] < e->game_hi[i] && a >= e->game_lo[i] && a < e->game_hi[i]) {
            *r = i;
            return 1;
        }
    }
    return 0;
}

/* the end of the section in secs (sorted, non-overlapping) holding a, or 0 */
static uint64_t sec_end(const Pos *secs, size_t n, uint64_t a)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (secs[mid].at <= a) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo > 0 && a < secs[lo - 1].end) {
        return secs[lo - 1].end;
    }
    return 0;
}

/* the section holding a: 1 and its [*lo, *hi), or 0 */
static int sec_at(const Pos *secs, size_t n, uint64_t a, uint64_t *lo, uint64_t *hi)
{
    size_t l = 0, h = n;

    while (l < h) {
        const size_t mid = l + (h - l) / 2;

        if (secs[mid].at <= a) {
            l = mid + 1;
        } else {
            h = mid;
        }
    }
    if (l > 0 && a < secs[l - 1].end) {
        *lo = secs[l - 1].at;
        *hi = secs[l - 1].end;
        return 1;
    }
    return 0;
}

static void say(char *why, size_t n, const char *s)
{
    if (why != NULL && n > 0) {
        snprintf(why, n, "%s", s);
    }
}

typedef struct Lists {
    Pos *code, *code_sec, *data, *data_sec, *game;
    size_t n_code, n_code_sec, n_data, n_data_sec, n_game;
} Lists;

static void lists_free(Lists *l)
{
    free(l->code);
    free(l->code_sec);
    free(l->data);
    free(l->data_sec);
    free(l->game);
    memset(l, 0, sizeof *l);
}

/* drops the second of two symbols at one address (the file's first wins) */
static size_t dedupe(Pos *p, size_t n)
{
    size_t i, k = 0;

    for (i = 0; i < n; i++) {
        if (k > 0 && p[k - 1].at == p[i].at) {
            continue;
        }
        p[k++] = p[i];
    }
    return k;
}

static int build_units(IcoSymTab *t, const Lists *l)
{
    const IcoSymTabEnv *e = &t->env;
    Pos *c = (Pos *)malloc((l->n_code + l->n_code_sec + 1) * sizeof *c);
    size_t n = 0, i, k = 0, si = 0;

    if (c == NULL) {
        return -1;
    }
    /* every symbol, and every section start that has none */
    for (i = 0; i < l->n_code; i++) {
        c[n++] = l->code[i];
    }
    for (i = 0; i < l->n_code_sec; i++) {
        const Pos *s = &l->code_sec[i];

        while (si < l->n_code && l->code[si].at < s->at) {
            si++;
        }
        if (si < l->n_code && l->code[si].at == s->at) {
            continue;
        }
        c[n] = *s;
        c[n].sized = 0;
        n++;
    }
    qsort(c, n, sizeof *c, pos_cmp);
    t->unit = (IcoSymUnit *)calloc(n ? n : 1, sizeof *t->unit);
    if (t->unit == NULL) {
        free(c);
        return -1;
    }
    for (i = 0; i < n; i++) {
        uint64_t end = i + 1 < n ? c[i + 1].at : e->text_hi;
        const uint64_t se = sec_end(l->code_sec, l->n_code_sec, c[i].at);
        IcoSymUnit *u = &t->unit[k];

        if (c[i].kind == IT_SYM && c[i].sized && c[i].end > c[i].at && c[i].end < end) {
            end = c[i].end;
        }
        if (se != 0 && se < end) {
            end = se;
        }
        if (end > e->text_hi) {
            end = e->text_hi;
        }
        if (end <= c[i].at) {
            continue;
        }
        u->start = c[i].at;
        u->size = end - c[i].at;
        u->name = c[i].name;
        k++;
    }
    free(c);
    t->n_unit = k;
    {
        uint32_t *names = (uint32_t *)calloc(k ? k : 1, sizeof *names);
        uint64_t *starts = (uint64_t *)calloc(k ? k : 1, sizeof *starts);
        uint64_t *hashes = (uint64_t *)malloc((k ? k : 1) * sizeof *hashes);
        uint32_t *ords = (uint32_t *)malloc((k ? k : 1) * sizeof *ords);
        uint8_t *uniq = (uint8_t *)malloc(k ? k : 1);

        if (names == NULL || starts == NULL || hashes == NULL || ords == NULL || uniq == NULL) {
            free(names);
            free(starts);
            free(hashes);
            free(ords);
            free(uniq);
            return -1;
        }
        for (i = 0; i < k; i++) {
            names[i] = t->unit[i].name;
            starts[i] = t->unit[i].start;
        }
        t->unit_by_name = index_names(t->str, k, names, starts, hashes, ords, uniq);
        for (i = 0; i < k; i++) {
            t->unit[i].name_hash = hashes[i];
            t->unit[i].ordinal = ords[i];
            t->unit[i].unique = uniq[i];
        }
        free(names);
        free(starts);
        free(hashes);
        free(ords);
        free(uniq);
        if (t->unit_by_name == NULL) {
            return -1;
        }
    }
    return 0;
}

static int build_data(IcoSymTab *t, const Lists *l)
{
    const IcoSymTabEnv *e = &t->env;
    size_t i, k = 0;

    t->data = (IcoSymData *)calloc(l->n_data ? l->n_data : 1, sizeof *t->data);
    if (t->data == NULL) {
        return -1;
    }
    for (i = 0; i < l->n_data; i++) {
        const Pos *p = &l->data[i];
        uint64_t end = i + 1 < l->n_data ? l->data[i + 1].at : e->image_hi;
        const uint64_t se = sec_end(l->data_sec, l->n_data_sec, p->at);
        IcoSymData *d = &t->data[k];
        int exact = 0;

        /* the next section's start bounds a symbol the file gave no size */
        {
            size_t lo = 0, hi = l->n_data_sec;

            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;

                if (l->data_sec[mid].at <= p->at) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            if (lo < l->n_data_sec && l->data_sec[lo].at < end) {
                end = l->data_sec[lo].at;
            }
        }
        if (se != 0 && se < end) {
            end = se;
        }
        if (p->sized) {
            end = p->at + (p->end - p->at);
            exact = 1;
        }
        if (end < p->at) {
            continue;
        }
        d->start = p->at;
        d->size = end - p->at;
        d->name = p->name;
        d->exact = (uint8_t)exact;
        d->sec_size = d->sec_off = 0;
        {
            uint64_t slo, shi;

            if (!exact && sec_at(l->data_sec, l->n_data_sec, p->at, &slo, &shi)) {
                d->sec_size = shi - slo;
                d->sec_off = p->at - slo;
            }
        }
        k++;
    }
    t->n_data = k;
    {
        uint32_t *names = (uint32_t *)calloc(k ? k : 1, sizeof *names);
        uint64_t *starts = (uint64_t *)calloc(k ? k : 1, sizeof *starts);
        uint64_t *hashes = (uint64_t *)malloc((k ? k : 1) * sizeof *hashes);
        uint32_t *ords = (uint32_t *)malloc((k ? k : 1) * sizeof *ords);
        uint8_t *uniq = (uint8_t *)malloc(k ? k : 1);

        if (names == NULL || starts == NULL || hashes == NULL || ords == NULL || uniq == NULL) {
            free(names);
            free(starts);
            free(hashes);
            free(ords);
            free(uniq);
            return -1;
        }
        for (i = 0; i < k; i++) {
            names[i] = t->data[i].name;
            starts[i] = t->data[i].start;
        }
        t->data_by_name = index_names(t->str, k, names, starts, hashes, ords, uniq);
        for (i = 0; i < k; i++) {
            t->data[i].name_hash = hashes[i];
            t->data[i].ordinal = ords[i];
            t->data[i].unique = uniq[i];
        }
        free(names);
        free(starts);
        free(hashes);
        free(ords);
        free(uniq);
        if (t->data_by_name == NULL) {
            return -1;
        }
    }
    return 0;
}

/* the game data layout's hash: "range offset size kind name\n" per item,
   sorted (pos_cmp put them in address order: symbol before section) */
static int build_layout(IcoSymTab *t, const Lists *l)
{
    const IcoSymTabEnv *e = &t->env;
    size_t cap = 4096, len = 0, i;
    char *txt = (char *)malloc(cap);

    if (txt == NULL) {
        return -1;
    }
    for (i = 0; i < l->n_game; i++) {
        const Pos *p = &l->game[i];
        int r = 0;
        uint64_t end, size;
        const char *nm = t->str + p->name;
        const size_t need = strlen(nm) + 80;

        (void)in_game(e, p->at, &r);
        end = e->game_hi[r];
        if (p->sized) {
            size = p->end - p->at;
        } else {
            size_t j;

            for (j = i + 1; j < l->n_game; j++) {
                if (l->game[j].at > p->at) {
                    break;
                }
            }
            if (j < l->n_game && l->game[j].at < end) {
                end = l->game[j].at;
            }
            size = end - p->at;
        }
        if (len + need > cap) {
            char *q;

            while (len + need > cap) {
                cap *= 2;
            }
            q = (char *)realloc(txt, cap);
            if (q == NULL) {
                free(txt);
                return -1;
            }
            txt = q;
        }
        len += (size_t)snprintf(txt + len, cap - len, "%d %llx %llx %c %s\n", r,
                                (unsigned long long)(p->at - e->game_lo[r]),
                                (unsigned long long)size, p->kind == IT_SYM ? 'S' : 'C', nm);
    }
    t->n_layout = l->n_game;
    t->layout_hash = xxh3_64(txt, len);
    free(txt);
    return 0;
}

static Pos *list_push(Pos **p, size_t *n, size_t *cap)
{
    if (*n == *cap) {
        const size_t c = *cap ? *cap * 2 : 1024;
        Pos *q = (Pos *)realloc(*p, c * sizeof *q);

        if (q == NULL) {
            return NULL;
        }
        *p = q;
        *cap = c;
    }
    return &(*p)[(*n)++];
}

IcoSymTab *ico_symtab_parse(const char *text, size_t n, const IcoSymTabEnv *env, char *why,
                            size_t why_n)
{
    Build b;
    ParseState st;
    IcoSymTab *t = NULL;
    Lists l;
    size_t caps[5] = {0, 0, 0, 0, 0};
    const char *p = text, *end = text + n;
    uint64_t anchor = 0, check = 0;
    size_t i;

    memset(&b, 0, sizeof b);
    memset(&st, 0, sizeof st);
    memset(&l, 0, sizeof l);
    say(why, why_n, "");
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const char *le = nl != NULL ? nl : end;

        if (le > p && le[-1] == '\r') {
            le--;
        }
        parse_line(&b, &st, p, le);
        p = nl != NULL ? nl + 1 : end;
    }
    if (b.failed) {
        say(why, why_n, "no memory for the symbol file's names");
        goto fail;
    }
    if (env->anchor == NULL || !find_sym(&b, env->anchor, &anchor)) {
        say(why, why_n, "the symbol file does not name the anchor function");
        goto fail;
    }
    t = (IcoSymTab *)calloc(1, sizeof *t);
    if (t == NULL) {
        say(why, why_n, "no memory for the table");
        goto fail;
    }
    t->env = *env;
    t->bias = (int64_t)(env->anchor_addr - anchor);
    if (env->check != NULL &&
        (!find_sym(&b, env->check, &check) || check + (uint64_t)t->bias != env->check_addr)) {
        say(why, why_n, "the symbol file is another build's");
        goto fail;
    }
    /* each item where it lies now, by kind and place */
    for (i = 0; i < b.n; i++) {
        const Item *it = &b.it[i];
        const uint64_t at = it->addr + (uint64_t)t->bias;
        Pos *q;
        int r;

        if (at < env->image_lo || at >= env->image_hi || (it->kind == IT_SEC && it->size == 0)) {
            continue;
        }
        if (at >= env->text_lo && at < env->text_hi) {
            q = it->kind == IT_SYM ? list_push(&l.code, &l.n_code, &caps[0])
                                   : list_push(&l.code_sec, &l.n_code_sec, &caps[1]);
        } else if (in_game(env, at, &r)) {
            q = list_push(&l.game, &l.n_game, &caps[4]);
        } else {
            q = it->kind == IT_SYM ? list_push(&l.data, &l.n_data, &caps[2])
                                   : list_push(&l.data_sec, &l.n_data_sec, &caps[3]);
        }
        if (q == NULL) {
            say(why, why_n, "no memory for the table");
            goto fail;
        }
        q->at = at;
        q->end = at + it->size;
        q->name = it->name;
        q->seq = it->seq;
        q->kind = it->kind;
        q->sized = it->sized;
    }
    /* an empty list has no array (NULL): qsort's base is declared non-null,
       so an empty one is left alone (UBSan in CI's asan job, state_port) */
    pos_sort(l.code, l.n_code);
    pos_sort(l.code_sec, l.n_code_sec);
    pos_sort(l.data, l.n_data);
    pos_sort(l.data_sec, l.n_data_sec);
    pos_sort(l.game, l.n_game);
    l.n_code = dedupe(l.code, l.n_code);
    l.n_code_sec = dedupe(l.code_sec, l.n_code_sec);
    l.n_data = dedupe(l.data, l.n_data);
    l.n_data_sec = dedupe(l.data_sec, l.n_data_sec);
    t->str = b.str;
    t->str_n = b.str_n;
    b.str = NULL;
    if (build_units(t, &l) != 0 || build_data(t, &l) != 0 || build_layout(t, &l) != 0) {
        say(why, why_n, "no memory for the table");
        goto fail;
    }
    if (t->n_unit == 0) {
        say(why, why_n, "the symbol file names nothing in the code");
        goto fail;
    }
    lists_free(&l);
    free(b.it);
    return t;
fail:
    lists_free(&l);
    free(b.it);
    free(b.str);
    ico_symtab_free(t);
    return NULL;
}

void ico_symtab_free(IcoSymTab *t)
{
    if (t == NULL) {
        return;
    }
    free(t->unit);
    free(t->data);
    free(t->str);
    free(t->unit_by_name);
    free(t->data_by_name);
    free(t);
}

char *ico_symtab_gunzip(const void *gz, size_t n, size_t *out_n)
{
    const unsigned char *g = (const unsigned char *)gz;
    size_t p = 10;
    unsigned char fl;
    void *out;

    *out_n = 0;
    if (n < 18 || g[0] != 0x1f || g[1] != 0x8b || g[2] != 8) {
        return NULL;
    }
    fl = g[3];
    if (fl & 4) { /* FEXTRA */
        if (p + 2 > n) {
            return NULL;
        }
        p += 2 + (size_t)(g[p] | g[p + 1] << 8);
    }
    for (int bit = 8; bit <= 16; bit <<= 1) { /* FNAME, FCOMMENT */
        if (fl & bit) {
            while (p < n && g[p] != 0) {
                p++;
            }
            p++;
        }
    }
    if (fl & 2) { /* FHCRC */
        p += 2;
    }
    if (p >= n) {
        return NULL;
    }
    out = tinfl_decompress_mem_to_heap(g + p, n - p, out_n, 0);
    return (char *)out;
}

/* --- lookups -------------------------------------------------------------------------- */

const IcoSymUnit *ico_symtab_unit_at(const IcoSymTab *t, uint64_t addr)
{
    size_t lo = 0, hi;

    if (t == NULL) {
        return NULL;
    }
    hi = t->n_unit;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (t->unit[mid].start <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo > 0 && addr - t->unit[lo - 1].start < t->unit[lo - 1].size) {
        return &t->unit[lo - 1];
    }
    return NULL;
}

const IcoSymData *ico_symtab_data_at(const IcoSymTab *t, uint64_t addr)
{
    size_t lo = 0, hi;

    if (t == NULL) {
        return NULL;
    }
    hi = t->n_data;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (t->data[mid].start <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo > 0 && addr - t->data[lo - 1].start <= t->data[lo - 1].size) {
        return &t->data[lo - 1];
    }
    return NULL;
}

static long find_named(const uint32_t *idx, size_t n, const void *base, size_t stride,
                       size_t hash_off, size_t name_off, size_t ord_off, const char *str,
                       uint64_t hash, const char *name, uint32_t ordinal)
{
    size_t lo = 0, hi = n;

    if (idx == NULL) {
        return -1;
    }
    /* the first entry whose hash is not below */
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        uint64_t h;

        memcpy(&h, (const char *)base + idx[mid] * stride + hash_off, sizeof h);
        if (h < hash) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < n; lo++) {
        const char *e = (const char *)base + idx[lo] * stride;
        uint64_t h;
        uint32_t nm, ord;

        memcpy(&h, e + hash_off, sizeof h);
        if (h != hash) {
            break;
        }
        memcpy(&nm, e + name_off, sizeof nm);
        memcpy(&ord, e + ord_off, sizeof ord);
        if (ord == ordinal && strcmp(str + nm, name) == 0) {
            return (long)idx[lo];
        }
    }
    return -1;
}

const IcoSymUnit *ico_symtab_unit_named(const IcoSymTab *t, uint64_t name_hash, const char *name,
                                        uint32_t ordinal)
{
    long i;

    if (t == NULL) {
        return NULL;
    }
    i = find_named(t->unit_by_name, t->n_unit, t->unit, sizeof(IcoSymUnit),
                   offsetof(IcoSymUnit, name_hash), offsetof(IcoSymUnit, name),
                   offsetof(IcoSymUnit, ordinal), t->str, name_hash, name, ordinal);
    return i >= 0 ? &t->unit[i] : NULL;
}

const IcoSymData *ico_symtab_data_named(const IcoSymTab *t, uint64_t name_hash, const char *name,
                                        uint32_t ordinal)
{
    long i;

    if (t == NULL) {
        return NULL;
    }
    i = find_named(t->data_by_name, t->n_data, t->data, sizeof(IcoSymData),
                   offsetof(IcoSymData, name_hash), offsetof(IcoSymData, name),
                   offsetof(IcoSymData, ordinal), t->str, name_hash, name, ordinal);
    return i >= 0 ? &t->data[i] : NULL;
}

uint64_t ico_symtab_unit_hash(IcoSymTab *t, const IcoSymUnit *cu)
{
    IcoSymUnit *u = &t->unit[cu - t->unit];

    if (!u->hashed) {
        u->code_hash = ico_code_hash(t->env.arch, (const void *)(uintptr_t)u->start,
                                     (size_t)u->size, u->start, t->env.image_lo, t->env.image_hi);
        u->hashed = 1;
    }
    return u->code_hash;
}
