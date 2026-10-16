/*
 * port/platform/symmap.c
 *
 * The crash and watchdog reports' symbolizer (symmap.h): a GNU ld link map
 * or an nm table, streamed through static buffers, no allocation.
 */
#include "symmap.h"

#include <stdio.h>
#include <string.h>

#include "miniz_tinfl.h"

/* --- The line stream --------------------------------------------------------- */

static char s_chunk[65536];
static size_t s_chunk_len, s_chunk_pos;
static char s_line[ICO_SYM_LINE];

/* The next line into s_line (cut at ICO_SYM_LINE - 1 bytes, its '\r' and
   '\n' dropped): 1, or 0 at the end, -1 on a read error. */
static int next_line(const IcoSymSource *src)
{
    size_t n = 0;
    int any = 0;

    for (;;) {
        if (s_chunk_pos == s_chunk_len) {
            const long r = src->read(src->user, s_chunk, sizeof s_chunk);

            if (r < 0) {
                return -1;
            }
            if (r == 0) {
                s_line[n] = '\0';
                return any ? 1 : 0;
            }
            s_chunk_len = (size_t)r;
            s_chunk_pos = 0;
        }
        while (s_chunk_pos < s_chunk_len) {
            const char c = s_chunk[s_chunk_pos++];

            any = 1;
            if (c == '\n') {
                s_line[n] = '\0';
                return 1;
            }
            if (c != '\r' && n < sizeof s_line - 1) {
                s_line[n++] = c;
            }
        }
    }
}

static int stream_start(const IcoSymSource *src)
{
    s_chunk_len = s_chunk_pos = 0;
    return src->rewind != NULL ? src->rewind(src->user) : 0;
}

/* --- Lines --------------------------------------------------------------------- */

#define TOKENS 5

typedef struct Tok {
    const char *s[TOKENS];
    size_t n[TOKENS];
    int count; /* may exceed TOKENS (the rest not kept) */
} Tok;

static void tokenize(const char *line, Tok *t)
{
    const char *p = line;

    t->count = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            return;
        }
        const char *b = p;

        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (t->count < TOKENS) {
            t->s[t->count] = b;
            t->n[t->count] = (size_t)(p - b);
        }
        t->count++;
    }
}

/* a hex number, with "0x" when need_0x: 1 and *v, else 0 */
static int hex(const char *s, size_t n, int need_0x, uint64_t *v)
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

static int tok_is(const char *s, size_t n, const char *w)
{
    return strlen(w) == n && memcmp(s, w, n) == 0;
}

static int tok_starts(const char *s, size_t n, const char *w)
{
    const size_t k = strlen(w);
    return n >= k && memcmp(s, w, k) == 0;
}

/* the output sections that hold code */
static int code_section(const char *s, size_t n)
{
    return tok_is(s, n, ".text") || tok_starts(s, n, ".text.") || tok_is(s, n, ".init") ||
           tok_is(s, n, ".fini") || tok_starts(s, n, ".plt");
}

/* the names equal, a leading '_' on either ignored */
static int same_name(const char *s, size_t n, const char *name)
{
    size_t k = strlen(name);

    if (n > 0 && s[0] == '_' && !(k > 0 && name[0] == '_')) {
        s++;
        n--;
    } else if (k > 0 && name[0] == '_' && !(n > 0 && s[0] == '_')) {
        name++;
        k--;
    }
    return n == k && memcmp(s, name, n) == 0;
}

/* One line's meaning */
enum { L_NONE, L_SYMBOL, L_SECTION };

typedef struct Parsed {
    int kind;
    uint64_t addr, size;
    const char *name; /* the symbol, or the section's function (.text.<name>; NULL) */
    size_t name_n;
    const char *obj; /* the section's object file */
    size_t obj_n;
    uint64_t end; /* a map symbol's input section's end (0: none known) */
} Parsed;

/* the parser's state across lines */
static int s_in_code;
/* the code input section the next symbol lines are in: its end (0 none) */
static uint64_t s_sec_end;
static char s_pending_sec[256];
/* the section line's name, kept past the line (Parsed.name points here) */
static char s_sec_name[256];

static void parse_line(const char *line, Parsed *out)
{
    Tok t;
    uint64_t a, b;
    const int lead = line[0] == ' ' || line[0] == '\t';

    out->kind = L_NONE;
    tokenize(line, &t);
    if (t.count == 0) {
        s_pending_sec[0] = '\0';
        return;
    }
    /* an output section (column 0): whether its symbols are code */
    if (!lead && t.s[0][0] == '.') {
        s_in_code = code_section(t.s[0], t.n[0]);
        s_sec_end = 0;
        s_pending_sec[0] = '\0';
        return;
    }
    /* nm: "0000000000123456 T name" (no sections; the type says code) */
    if (!lead && t.count == 3 && t.n[1] == 1 && hex(t.s[0], t.n[0], 0, &a)) {
        const char ty = t.s[1][0];

        if (ty == 'T' || ty == 't' || ty == 'W' || ty == 'w') {
            out->kind = L_SYMBOL;
            out->addr = a;
            out->name = t.s[2];
            out->name_n = t.n[2];
            out->end = 0;
        }
        return;
    }
    if (!s_in_code) {
        s_pending_sec[0] = '\0';
        return;
    }
    /* an input section: " .text  0xADDR  0xSIZE  file", or its name alone
       on a line with the numbers on the next */
    if (lead && t.s[0][0] == '.' && t.count == 1) {
        snprintf(s_pending_sec, sizeof s_pending_sec, "%.*s", (int)t.n[0], t.s[0]);
        return;
    }
    {
        const char *sec = NULL;
        size_t sec_n = 0;
        int first = -1;

        if (lead && t.s[0][0] == '.' && t.count >= 4) {
            sec = t.s[0];
            sec_n = t.n[0];
            first = 1;
        } else if (lead && s_pending_sec[0] != '\0' && t.count >= 3) {
            sec = s_pending_sec;
            sec_n = strlen(s_pending_sec);
            first = 0;
        }
        if (first >= 0 && hex(t.s[first], t.n[first], 1, &a) &&
            hex(t.s[first + 1], t.n[first + 1], 1, &b)) {
            snprintf(s_sec_name, sizeof s_sec_name, "%.*s", (int)sec_n, sec);
            sec = s_sec_name;
            sec_n = strlen(s_sec_name);
            s_pending_sec[0] = '\0';
            s_sec_end = 0;
            if (!code_section(sec, sec_n)) {
                return;
            }
            s_sec_end = a + b;
            out->kind = L_SECTION;
            out->addr = a;
            out->size = b;
            out->name = NULL;
            out->name_n = 0;
            if (tok_starts(sec, sec_n, ".text.") && sec_n > 6) {
                /* -ffunction-sections: the function's own section
                   (.text.<fn>, GCC's .text.unlikely.<fn> and the like);
                   .text.startup alone is no function's name */
                static const char *const kinds[] = {"unlikely", "hot", "startup", "exit"};
                const char *f = sec + 6;
                size_t fn = sec_n - 6;

                for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
                    const size_t kn = strlen(kinds[k]);

                    if (fn >= kn && memcmp(f, kinds[k], kn) == 0 && (fn == kn || f[kn] == '.')) {
                        f += fn == kn ? kn : kn + 1;
                        fn -= fn == kn ? kn : kn + 1;
                        break;
                    }
                }
                if (fn > 0) {
                    out->name = f;
                    out->name_n = fn;
                }
            }
            out->obj = t.s[first + 2];
            out->obj_n = t.n[first + 2];
            return;
        }
    }
    s_pending_sec[0] = '\0';
    /* a symbol: "                0xADDR                name" */
    if (lead && t.count == 2 && hex(t.s[0], t.n[0], 1, &a) && !hex(t.s[1], t.n[1], 1, &b)) {
        out->kind = L_SYMBOL;
        out->addr = a;
        out->name = t.s[1];
        out->name_n = t.n[1];
        out->end = s_sec_end;
    }
}

/* "libico_platform.a(hotkeys.c.obj)" -> "hotkeys.c.obj";
   "port/render/CMakeFiles/x.dir/rd_blur.c.o" -> "rd_blur.c.o" */
static void obj_name(const char *s, size_t n, char *out, size_t size)
{
    const char *open = memchr(s, '(', n);
    size_t i;

    if (open != NULL && n > 0 && s[n - 1] == ')') {
        const size_t k = (size_t)(s + n - 1 - (open + 1));

        snprintf(out, size, "%.*s", (int)k, open + 1);
        return;
    }
    for (i = n; i > 0; i--) {
        if (s[i - 1] == '/' || s[i - 1] == '\\') {
            break;
        }
    }
    snprintf(out, size, "%.*s", (int)(n - i), s + i);
}

/* --- Resolve --------------------------------------------------------------------- */

static struct {
    uint64_t sym_addr; /* the best symbol's address */
    int sym;           /* 1 when one was found */
    uint64_t sec_addr;
    int sec; /* 1 when a code section holds the offset */
    char sec_fn[ICO_SYM_NAME];
} s_best[ICO_SYM_MAX];

int ico_sym_resolve(const IcoSymSource *src, const char *anchor, uint64_t anchor_off,
                    IcoSymHit *hits, int n)
{
    Parsed p;
    uint64_t base = 0;
    int found = 0, r, named = 0, i;

    if (src == NULL || src->read == NULL || anchor == NULL || n < 0) {
        return -1;
    }
    if (n > ICO_SYM_MAX) {
        n = ICO_SYM_MAX;
    }
    for (i = 0; i < n; i++) {
        hits[i].name[0] = '\0';
        hits[i].obj[0] = '\0';
        hits[i].delta = hits[i].obj_delta = 0;
        hits[i].named_by_section = 0;
        memset(&s_best[i], 0, sizeof s_best[i]);
    }
    /* pass 1: the anchor */
    if (stream_start(src) != 0) {
        return -1;
    }
    s_in_code = 0;
    s_sec_end = 0;
    s_pending_sec[0] = '\0';
    while (!found && (r = next_line(src)) > 0) {
        parse_line(s_line, &p);
        if (p.kind == L_SYMBOL && same_name(p.name, p.name_n, anchor)) {
            base = p.addr - anchor_off;
            found = 1;
        }
    }
    if (!found) {
        return -1;
    }
    /* pass 2: each offset's symbol and section */
    if (stream_start(src) != 0) {
        return -1;
    }
    s_in_code = 0;
    s_sec_end = 0;
    s_pending_sec[0] = '\0';
    while ((r = next_line(src)) > 0) {
        parse_line(s_line, &p);
        if (p.kind == L_NONE) {
            continue;
        }
        for (i = 0; i < n; i++) {
            const uint64_t t = base + hits[i].off;

            if (p.kind == L_SYMBOL) {
                /* a map's symbol names only what is in its own object's
                   section: past its end is code the map does not name */
                if (p.addr <= t && (p.end == 0 || t < p.end) &&
                    (!s_best[i].sym || p.addr >= s_best[i].sym_addr)) {
                    s_best[i].sym = 1;
                    s_best[i].sym_addr = p.addr;
                    snprintf(hits[i].name, sizeof hits[i].name, "%.*s", (int)p.name_n, p.name);
                    hits[i].delta = t - p.addr;
                }
            } else if (p.addr <= t && t - p.addr < p.size) {
                s_best[i].sec = 1;
                s_best[i].sec_addr = p.addr;
                obj_name(p.obj, p.obj_n, hits[i].obj, sizeof hits[i].obj);
                hits[i].obj_delta = t - p.addr;
                if (p.name != NULL) {
                    snprintf(s_best[i].sec_fn, sizeof s_best[i].sec_fn, "%.*s", (int)p.name_n,
                             p.name);
                } else {
                    s_best[i].sec_fn[0] = '\0';
                }
            }
        }
    }
    if (r < 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (s_best[i].sec && s_best[i].sec_fn[0] != '\0' &&
            (!s_best[i].sym || s_best[i].sym_addr < s_best[i].sec_addr)) {
            /* a function section with no global symbol: a static function */
            snprintf(hits[i].name, sizeof hits[i].name, "%s", s_best[i].sec_fn);
            hits[i].delta = hits[i].obj_delta;
            hits[i].named_by_section = 1;
        } else if (s_best[i].sec && s_best[i].sym && s_best[i].sym_addr < s_best[i].sec_addr) {
            /* the nearest global function is another object's: the
               offset is in one of this object's static functions */
            hits[i].name[0] = '\0';
            hits[i].delta = 0;
        }
        named += hits[i].name[0] != '\0' || hits[i].obj[0] != '\0';
    }
    return named;
}

void ico_sym_describe(const IcoSymHit *h, char *out, size_t size)
{
    if (h->name[0] != '\0' && h->obj[0] != '\0') {
        snprintf(out, size, "%s+0x%llx (%s)", h->name, (unsigned long long)h->delta, h->obj);
    } else if (h->name[0] != '\0') {
        snprintf(out, size, "%s+0x%llx", h->name, (unsigned long long)h->delta);
    } else if (h->obj[0] != '\0') {
        snprintf(out, size, "a static function in %s, +0x%llx into its code", h->obj,
                 (unsigned long long)h->obj_delta);
    } else if (size > 0) {
        out[0] = '\0';
    }
}

/* --- Memory ---------------------------------------------------------------------- */

static long mem_read(void *user, char *buf, size_t size)
{
    IcoSymMem *m = (IcoSymMem *)user;
    size_t k = m->size - m->pos;

    if (k > size) {
        k = size;
    }
    memcpy(buf, m->data + m->pos, k);
    m->pos += k;
    return (long)k;
}

static int mem_rewind(void *user)
{
    ((IcoSymMem *)user)->pos = 0;
    return 0;
}

void ico_sym_mem_source(IcoSymSource *out, IcoSymMem *m, const void *data, size_t size)
{
    m->data = (const char *)data;
    m->size = size;
    m->pos = 0;
    out->read = mem_read;
    out->rewind = mem_rewind;
    out->user = m;
}

/* --- gzip -------------------------------------------------------------------------- */

static IcoSymSource s_gz_raw;
static tinfl_decompressor s_inf;
static unsigned char s_gz_in[16384];
static size_t s_gz_in_pos, s_gz_in_len;
static int s_gz_in_eof;
static unsigned char s_gz_dict[TINFL_LZ_DICT_SIZE];
static size_t s_gz_dict_pos;  /* where tinfl writes next */
static size_t s_gz_out_start; /* inflated bytes not yet handed out */
static size_t s_gz_out_len;
static int s_gz_state; /* 0 the header, 1 inflating, 2 done, -1 broken */

static int gz_fill(void)
{
    long r;

    if (s_gz_in_pos > 0) {
        memmove(s_gz_in, s_gz_in + s_gz_in_pos, s_gz_in_len - s_gz_in_pos);
        s_gz_in_len -= s_gz_in_pos;
        s_gz_in_pos = 0;
    }
    if (s_gz_in_eof || s_gz_in_len == sizeof s_gz_in) {
        return 0;
    }
    r = s_gz_raw.read(s_gz_raw.user, (char *)s_gz_in + s_gz_in_len, sizeof s_gz_in - s_gz_in_len);
    if (r < 0) {
        return -1;
    }
    if (r == 0) {
        s_gz_in_eof = 1;
    }
    s_gz_in_len += (size_t)r;
    return 0;
}

/* RFC 1952's member header, from the first bytes */
static int gz_header(void)
{
    size_t p = 10;
    unsigned char fl;

    if (gz_fill() != 0 || s_gz_in_len < 10 || s_gz_in[0] != 0x1f || s_gz_in[1] != 0x8b ||
        s_gz_in[2] != 8) {
        return -1;
    }
    fl = s_gz_in[3];
    if (fl & 4) { /* FEXTRA */
        if (p + 2 > s_gz_in_len) {
            return -1;
        }
        p += 2 + (size_t)(s_gz_in[p] | s_gz_in[p + 1] << 8);
    }
    for (int bit = 8; bit <= 16; bit <<= 1) { /* FNAME, FCOMMENT */
        if (fl & bit) {
            while (p < s_gz_in_len && s_gz_in[p] != 0) {
                p++;
            }
            p++;
        }
    }
    if (fl & 2) { /* FHCRC */
        p += 2;
    }
    if (p > s_gz_in_len) {
        return -1;
    }
    s_gz_in_pos = p;
    return 0;
}

static long gz_read(void *user, char *buf, size_t size)
{
    (void)user;
    if (s_gz_state == 0) {
        s_gz_state = gz_header() == 0 ? 1 : -1;
    }
    for (;;) {
        if (s_gz_out_len > 0) {
            size_t k = s_gz_out_len < size ? s_gz_out_len : size;

            memcpy(buf, s_gz_dict + s_gz_out_start, k);
            s_gz_out_start += k;
            s_gz_out_len -= k;
            return (long)k;
        }
        if (s_gz_state != 1) {
            return s_gz_state == 2 ? 0 : -1;
        }
        if (s_gz_in_pos == s_gz_in_len || !s_gz_in_eof) {
            if (gz_fill() != 0) {
                s_gz_state = -1;
                continue;
            }
        }
        {
            size_t in_n = s_gz_in_len - s_gz_in_pos;
            size_t out_n = sizeof s_gz_dict - s_gz_dict_pos;
            const tinfl_status st = tinfl_decompress(&s_inf, s_gz_in + s_gz_in_pos, &in_n,
                                                     s_gz_dict, s_gz_dict + s_gz_dict_pos, &out_n,
                                                     s_gz_in_eof ? 0 : TINFL_FLAG_HAS_MORE_INPUT);

            s_gz_in_pos += in_n;
            s_gz_out_start = s_gz_dict_pos;
            s_gz_out_len = out_n;
            s_gz_dict_pos = (s_gz_dict_pos + out_n) & (sizeof s_gz_dict - 1);
            if (st == TINFL_STATUS_DONE) {
                s_gz_state = 2;
            } else if (st < 0 || (st == TINFL_STATUS_NEEDS_MORE_INPUT && s_gz_in_eof &&
                                  s_gz_in_pos == s_gz_in_len && out_n == 0)) {
                s_gz_state = -1;
            }
        }
    }
}

static int gz_rewind(void *user)
{
    (void)user;
    if (s_gz_raw.rewind == NULL || s_gz_raw.rewind(s_gz_raw.user) != 0) {
        return -1;
    }
    tinfl_init(&s_inf);
    s_gz_in_pos = s_gz_in_len = 0;
    s_gz_in_eof = 0;
    s_gz_dict_pos = s_gz_out_start = s_gz_out_len = 0;
    s_gz_state = 0;
    return 0;
}

void ico_sym_gz_source(IcoSymSource *out, const IcoSymSource *raw)
{
    s_gz_raw = *raw;
    out->read = gz_read;
    out->rewind = gz_rewind;
    out->user = NULL;
    (void)gz_rewind(NULL);
}
