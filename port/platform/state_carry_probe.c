/*
 * port/platform/state_carry_probe.c
 *
 * The save side's verdict on a program image in a file (tools/
 * state_carry_check.py; docs/port/PLATFORM.md "Save states", "The carry-over
 * guard"): which addresses a state of that program could hold that the
 * saving build cannot name (state_xbuild.h), judged by the product's own
 * ico_symtab_parse and ico_xb_begin / ico_xb_word / ico_xb_finish, never a
 * copy of their rules.
 *
 *   state_carry_probe DESC         the verdict (below)
 *   state_carry_probe --self-image this process's ico_state_image_range,
 *                                  relative to its start (built with
 *                                  ICO_CARRY_PROBE_SELF: the CMake target,
 *                                  which links state_check.c), for the
 *                                  script's drift check of its loader mirror
 *
 * DESC is the script's text, one item a line, numbers in hex, every address
 * an offset from the image's start:
 *
 *   image PATH SIZE     the image as its loader maps it (segments or
 *                       sections at their offsets, zeros past the file)
 *   range LO HI         ico_state_image_range's lo and hi
 *   text LO HI          its code range
 *   ro LO HI            each read-only range (at most ICO_STATE_IMAGE_RO)
 *   game I LO HI        the game's data (0) and bss (1) ranges
 *   relocs PATH N       N little-endian 64-bit offsets of the image's
 *                       relocated words: each holds its target's offset,
 *                       and the image's address here is added (the
 *                       loader's relocation)
 *   syms PATH           the symbol file's text (inflated)
 *   arch N              code_hash.h ICO_CODE_*
 *   anchor NAME OFF     a function the symbol file names, at OFF
 *   words PATH          lines "<class> <offset>": the init and table words
 *   list N              the unnamed words listed per class at most (the
 *                       init class: all of them)
 *
 * The image is mapped at an address aligned to 64 KB (an arm64 ADRP's page
 * and every offset below 64 KB stay what they are in the program), the
 * relocations applied, the game data range's words copied as the start-up
 * snapshot (state_host.c snap_game_data), the symbol file read against the
 * anchor, and each class's words handed to the save in batches of
 * ICO_XB_MAX_POINTS / 2 (no batch can pass the record's bounds): a batch
 * whose ico_xb_finish counts no unnamed word is named whole; one that does
 * is halved until each unnamed word stands alone.  The third class, code,
 * is the addresses every unit's instructions compute (arm64 ADRP+ADD and
 * ADR, x86-64 RIP-relative operands and 64-bit immediates in the image),
 * decoded with code_hash.c's decoders.
 *
 * Output lines:
 *   symbols units N data N layout HASH
 *   class CLS words N named N unnamed N overflow 0|1
 *   kind CLS KIND N     where the class's words point (unit, game, data,
 *                       start-up, content): a description, not the verdict
 *   unnamed CLS OFF     each unnamed word (offset from the image's start)
 * Status 0, or 2 with the reason on a line of its own.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_hash.h"
#include "state_reloc.h"
#include "state_syms.h"
#include "state_xbuild.h"

#if defined(ICO_CARRY_PROBE_SELF) && defined(__linux__)
#include <unistd.h>
#endif

#define PROBE_ALIGN 65536u

enum { CLS_INIT = 0, CLS_TABLE, CLS_CODE, CLS_N };

static const char *const CLS_NAME[CLS_N] = {"init", "table", "code"};

typedef struct Words {
    uint64_t *w;
    size_t n, cap;
} Words;

typedef struct Probe {
    unsigned char *raw, *base; /* the image: raw allocation, its aligned start */
    uint64_t size;
    IcoXbProc proc;
    IcoSymTab *tab;
    uint64_t *snap_sorted;
    size_t n_snap;
    Words cls[CLS_N];
    size_t list;
    char *anchor; /* the anchor's name (the table's env points at it) */
} Probe;

static int die(const char *what, const char *arg)
{
    printf("error %s%s%s\n", what, arg != NULL ? ": " : "", arg != NULL ? arg : "");
    return 2;
}

static char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *p = NULL;
    long len;

    *n = 0;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        p = (char *)malloc((size_t)len + 1);
        if (p != NULL && fread(p, 1, (size_t)len, f) != (size_t)len) {
            free(p);
            p = NULL;
        }
        if (p != NULL) {
            p[len] = '\0';
            *n = (size_t)len;
        }
    }
    fclose(f);
    return p;
}

static char *dup_text(const char *t)
{
    const size_t n = strlen(t) + 1;
    char *q = (char *)malloc(n);

    if (q != NULL) {
        memcpy(q, t, n);
    }
    return q;
}

static int words_add(Words *s, uint64_t w)
{
    if (s->n == s->cap) {
        const size_t cap = s->cap ? s->cap * 2 : 4096;
        uint64_t *q = (uint64_t *)realloc(s->w, cap * sizeof *q);

        if (q == NULL) {
            return -1;
        }
        s->w = q;
        s->cap = cap;
    }
    s->w[s->n++] = w;
    return 0;
}

static int u64_cmp(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

    return x < y ? -1 : x > y;
}

static void words_unique(Words *s)
{
    size_t i, k = 0;

    if (s->n < 2) {
        return;
    }
    qsort(s->w, s->n, sizeof *s->w, u64_cmp);
    for (i = 0; i < s->n; i++) {
        if (k == 0 || s->w[k - 1] != s->w[i]) {
            s->w[k++] = s->w[i];
        }
    }
    s->n = k;
}

static int in_image(const Probe *p, uint64_t w)
{
    return w >= p->proc.image.lo && w <= p->proc.image.hi;
}

/* --- the code class ---------------------------------------------------------------- */

static int64_t sext(uint64_t v, int bits)
{
    const uint64_t m = 1ull << (bits - 1);

    v &= bits == 64 ? ~0ull : ((1ull << bits) - 1);
    return (int64_t)((v ^ m) - m);
}

static uint64_t field(const uint8_t *q, int size)
{
    uint64_t v = 0;
    int i;

    for (i = size - 1; i >= 0; i--) {
        v = v << 8 | q[i];
    }
    return v;
}

static int code_x64(Probe *p, const uint8_t *c, uint64_t at, uint64_t n)
{
    uint64_t k = 0;

    while (k < n) {
        IcoX64Insn ins;
        int j;

        ico_code_x64_decode(c + k, (size_t)(n - k), &ins);
        if (ins.len == 0) {
            break;
        }
        for (j = 0; j < ins.n; j++) {
            const IcoX64Field *f = &ins.f[j];
            const uint64_t v = field(c + k + f->off, f->size);
            uint64_t t = 0;

            if (f->kind == ICO_X64_RIP) {
                t = at + k + ins.len + (uint64_t)sext(v, f->size * 8);
            } else if ((f->kind == ICO_X64_IMM || f->kind == ICO_X64_DISP) && f->size == 8) {
                t = v;
            }
            if (t != 0 && in_image(p, t) && words_add(&p->cls[CLS_CODE], t) != 0) {
                return -1;
            }
        }
        k += ins.len;
    }
    return 0;
}

static int code_a64(Probe *p, const uint8_t *c, uint64_t at, uint64_t n)
{
    uint64_t page[32];
    uint32_t pend = 0; /* registers an ADRP set and nothing wrote since */
    uint64_t k;

    memset(page, 0, sizeof page);
    for (k = 0; k + 4 <= n; k += 4) {
        const uint32_t w = (uint32_t)field(c + k, 4);
        const uint64_t pc = at + k;
        uint64_t t = 0;
        uint32_t wr;

        if ((w & 0x9F000000u) == 0x10000000u) { /* ADR */
            t = pc + (uint64_t)sext(((w >> 5) & 0x7FFFFu) << 2 | ((w >> 29) & 3u), 21);
        } else if ((w & 0xFF800000u) == 0x91000000u && ((pend >> ((w >> 5) & 31)) & 1u) != 0) {
            /* ADD Xd, Xn, #imm{, lsl 12} on an ADRP's page */
            t = page[(w >> 5) & 31] + (((w >> 10) & 0xFFFu) << (((w >> 22) & 1u) ? 12 : 0));
        }
        if (t != 0 && in_image(p, t) && words_add(&p->cls[CLS_CODE], t) != 0) {
            return -1;
        }
        wr = ico_code_a64_writes(w);
        pend = wr == ~0u ? 0u : pend & ~wr;
        if ((w & 0x9F000000u) == 0x90000000u && (w & 31) != 31) {
            const uint64_t imm = (((uint64_t)(w >> 5) & 0x7FFFFu) << 2 | ((w >> 29) & 3u)) << 12;

            page[w & 31] = (pc & ~(uint64_t)0xFFF) + (uint64_t)sext(imm, 33);
            pend |= 1u << (w & 31);
        }
    }
    return 0;
}

static int code_words(Probe *p, int arch)
{
    size_t i;

    for (i = 0; i < p->tab->n_unit; i++) {
        const IcoSymUnit *u = &p->tab->unit[i];
        const uint8_t *c = (const uint8_t *)(uintptr_t)u->start;

        if (u->start < p->proc.image.text_lo || u->start + u->size > p->proc.image.text_hi) {
            continue;
        }
        if ((arch == ICO_CODE_X64   ? code_x64(p, c, u->start, u->size)
             : arch == ICO_CODE_A64 ? code_a64(p, c, u->start, u->size)
                                    : 0) != 0) {
            return -1;
        }
    }
    words_unique(&p->cls[CLS_CODE]);
    return 0;
}

/* --- the verdict -------------------------------------------------------------------- */

/* the words nothing names among n (ico_xb_finish's count), or -1 */
static long run_left(const Probe *p, const uint64_t *w, size_t n, int *overflow)
{
    IcoXbRefs *r = ico_xb_begin(&p->proc);
    IcoXbStats st;
    void *sec = NULL;
    size_t sn = 0, i;

    if (r == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        ico_xb_word(r, w[i], 1);
    }
    if (ico_xb_finish(r, &sec, &sn, &st) != 0) {
        return -1;
    }
    free(sec);
    if ((st.flags & ICO_XB_OVERFLOW) != 0) {
        *overflow = 1;
    }
    return (long)st.left;
}

typedef struct Found {
    uint64_t *w;
    size_t n, cap; /* cap: the list's bound */
    unsigned long long count;
    int overflow, failed;
} Found;

static void isolate(const Probe *p, const uint64_t *w, size_t n, long left, Found *f)
{
    long l1, l2;
    size_t h;

    if (left <= 0 || f->failed || f->n >= f->cap) {
        return;
    }
    if (n == 1) {
        f->w[f->n++] = w[0];
        return;
    }
    h = n / 2;
    l1 = run_left(p, w, h, &f->overflow);
    l2 = run_left(p, w + h, n - h, &f->overflow);
    if (l1 < 0 || l2 < 0) {
        f->failed = 1;
        return;
    }
    isolate(p, w, h, l1, f);
    isolate(p, w + h, n - h, l2, f);
}

static const char *kind_of(const Probe *p, uint64_t w)
{
    const IcoXbProc *x = &p->proc;
    int i;

    if (w >= x->image.text_lo && w < x->image.text_hi && ico_symtab_unit_at(p->tab, w) != NULL) {
        return "unit";
    }
    for (i = 0; i < 2; i++) {
        if (x->game_lo[i] < x->game_hi[i] && w >= x->game_lo[i] && w <= x->game_hi[i]) {
            return "game";
        }
    }
    {
        const IcoSymData *d = ico_symtab_data_at(p->tab, w);

        if (d != NULL && d->unique) {
            return "data";
        }
    }
    if (p->n_snap > 0 && bsearch(&w, p->snap_sorted, p->n_snap, sizeof w, u64_cmp) != NULL) {
        return "start-up";
    }
    return "content";
}

static int judge(Probe *p, int c)
{
    const Words *s = &p->cls[c];
    const size_t batch = ICO_XB_MAX_POINTS / 2;
    static const char *const kinds[] = {"unit", "game", "data", "start-up", "content"};
    unsigned long long count[5] = {0, 0, 0, 0, 0};
    Found f;
    size_t i, k;

    memset(&f, 0, sizeof f);
    f.cap = c == CLS_INIT ? s->n : (p->list < s->n ? p->list : s->n);
    f.w = (uint64_t *)malloc((f.cap ? f.cap : 1) * sizeof *f.w);
    if (f.w == NULL) {
        return -1;
    }
    for (i = 0; i < s->n; i += batch) {
        const size_t n = s->n - i < batch ? s->n - i : batch;
        const long left = run_left(p, s->w + i, n, &f.overflow);

        if (left < 0) {
            free(f.w);
            return -1;
        }
        f.count += (unsigned long long)left;
        isolate(p, s->w + i, n, left, &f);
        if (f.failed) {
            free(f.w);
            return -1;
        }
    }
    for (i = 0; i < s->n; i++) {
        const char *kd = kind_of(p, s->w[i]);

        for (k = 0; k < 5; k++) {
            if (strcmp(kd, kinds[k]) == 0) {
                count[k]++;
            }
        }
    }
    printf("class %s words %zu named %llu unnamed %llu overflow %d\n", CLS_NAME[c], s->n,
           (unsigned long long)s->n - f.count, f.count, f.overflow);
    for (k = 0; k < 5; k++) {
        if (count[k] != 0) {
            printf("kind %s %s %llu\n", CLS_NAME[c], kinds[k], count[k]);
        }
    }
    for (i = 0; i < f.n; i++) {
        printf("unnamed %s %llx\n", CLS_NAME[c],
               (unsigned long long)(f.w[i] - (uint64_t)(uintptr_t)p->base));
    }
    free(f.w);
    return 0;
}

/* --- the description ---------------------------------------------------------------- */

static int parse_desc(Probe *p, char *text, int *arch)
{
    char *line = text, *nl;
    char *image_path = NULL, *relocs_path = NULL, *syms_path = NULL, *words_path = NULL;
    char *anchor = NULL;
    unsigned long long a, b, anchor_off = 0, n_relocs = 0, image_size = 0;
    unsigned long long range_hi = 0, text_lo = 0, text_hi = 0, game[2][2] = {{0, 0}, {0, 0}};
    unsigned long long ro[ICO_STATE_IMAGE_RO][2];
    int n_ro = 0, i;
    char *syms = NULL, *words = NULL, *relocs = NULL;
    size_t syms_n = 0, words_n = 0, relocs_n = 0, k;
    IcoSymTabEnv env;
    char why[160];
    FILE *f;

    *arch = 0;
    p->list = 64;
    for (; line != NULL && *line != '\0'; line = nl) {
        char key[16], path[1024];
        unsigned u;

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl++ = '\0';
        }
        if (sscanf(line, "%15s", key) != 1) {
            continue;
        }
        if (strcmp(key, "image") == 0 && sscanf(line, "image %1023s %llx", path, &a) == 2) {
            image_path = dup_text(path);
            image_size = a;
        } else if (strcmp(key, "range") == 0 && sscanf(line, "range %llx %llx", &a, &b) == 2) {
            if (a != 0) {
                return die("the image range must start at offset 0", line);
            }
            range_hi = b;
        } else if (strcmp(key, "text") == 0 && sscanf(line, "text %llx %llx", &a, &b) == 2) {
            text_lo = a;
            text_hi = b;
        } else if (strcmp(key, "ro") == 0 && sscanf(line, "ro %llx %llx", &a, &b) == 2) {
            if (n_ro == ICO_STATE_IMAGE_RO) {
                return die("more read-only ranges than ICO_STATE_IMAGE_RO", line);
            }
            ro[n_ro][0] = a;
            ro[n_ro][1] = b;
            n_ro++;
        } else if (strcmp(key, "game") == 0 && sscanf(line, "game %u %llx %llx", &u, &a, &b) == 3 &&
                   u < 2) {
            game[u][0] = a;
            game[u][1] = b;
        } else if (strcmp(key, "relocs") == 0 &&
                   sscanf(line, "relocs %1023s %llu", path, &a) == 2) {
            relocs_path = dup_text(path);
            n_relocs = a;
        } else if (strcmp(key, "syms") == 0 && sscanf(line, "syms %1023s", path) == 1) {
            syms_path = dup_text(path);
        } else if (strcmp(key, "words") == 0 && sscanf(line, "words %1023s", path) == 1) {
            words_path = dup_text(path);
        } else if (strcmp(key, "arch") == 0 && sscanf(line, "arch %u", &u) == 1) {
            *arch = (int)u;
        } else if (strcmp(key, "anchor") == 0 &&
                   sscanf(line, "anchor %1023s %llx", path, &a) == 2) {
            free(anchor);
            anchor = dup_text(path);
            p->anchor = anchor;
            anchor_off = a;
        } else if (strcmp(key, "list") == 0 && sscanf(line, "list %u", &u) == 1) {
            p->list = u;
        } else {
            return die("a line the probe does not read", line);
        }
    }
    if (image_path == NULL || relocs_path == NULL || syms_path == NULL || words_path == NULL ||
        anchor == NULL || range_hi == 0 || text_hi <= text_lo || game[0][1] <= game[0][0]) {
        return die("the description lacks an item", NULL);
    }
    /* the image at a 64 KB aligned address */
    p->size = image_size > range_hi ? image_size : range_hi + 8;
    p->raw = (unsigned char *)calloc((size_t)p->size + PROBE_ALIGN, 1);
    if (p->raw == NULL) {
        return die("no memory for the image", NULL);
    }
    p->base =
        (unsigned char *)(((uintptr_t)p->raw + PROBE_ALIGN - 1) & ~(uintptr_t)(PROBE_ALIGN - 1));
    f = fopen(image_path, "rb");
    if (f == NULL || fread(p->base, 1, (size_t)image_size, f) != (size_t)image_size) {
        if (f != NULL) {
            fclose(f);
        }
        return die("the image could not be read", image_path);
    }
    fclose(f);
    relocs = read_file(relocs_path, &relocs_n);
    if (relocs == NULL || relocs_n != n_relocs * 8) {
        return die("the relocations could not be read", relocs_path);
    }
    for (k = 0; k < (size_t)n_relocs; k++) {
        uint64_t off, v;

        memcpy(&off, relocs + 8 * k, 8);
        if (off + 8 > p->size) {
            return die("a relocation outside the image", relocs_path);
        }
        memcpy(&v, p->base + off, 8);
        v += (uint64_t)(uintptr_t)p->base;
        memcpy(p->base + off, &v, 8);
    }
    free(relocs);
    /* the process the save sees */
    {
        const uint64_t B = (uint64_t)(uintptr_t)p->base;
        IcoXbProc *x = &p->proc;

        memset(x, 0, sizeof *x);
        x->image.lo = B;
        x->image.hi = B + range_hi;
        x->image.text_lo = B + text_lo;
        x->image.text_hi = B + text_hi;
        x->image.n_ro = n_ro;
        for (i = 0; i < n_ro; i++) {
            x->image.ro_lo[i] = B + ro[i][0];
            x->image.ro_hi[i] = B + ro[i][1];
        }
        for (i = 0; i < 2; i++) {
            x->game_lo[i] = B + game[i][0];
            x->game_hi[i] = B + game[i][1];
        }
        /* the start-up snapshot: the game data range's words as they are
           now, before anything ran (state_host.c snap_game_data) */
        x->snap_words = (size_t)(game[0][1] - game[0][0]) / 8;
        p->snap_sorted = (uint64_t *)malloc(x->snap_words ? x->snap_words * 8 : 8);
        if (p->snap_sorted == NULL) {
            return die("no memory for the snapshot", NULL);
        }
        memcpy(p->snap_sorted, p->base + game[0][0], x->snap_words * 8);
        {
            uint64_t *copy = (uint64_t *)malloc(x->snap_words ? x->snap_words * 8 : 8);

            if (copy == NULL) {
                return die("no memory for the snapshot", NULL);
            }
            memcpy(copy, p->snap_sorted, x->snap_words * 8);
            x->snap = copy;
        }
        p->n_snap = x->snap_words;
        qsort(p->snap_sorted, p->n_snap, sizeof *p->snap_sorted, u64_cmp);
        x->any_address = 1; /* the mapping's own address is not the program's */

        memset(&env, 0, sizeof env);
        env.anchor = anchor;
        env.anchor_addr = B + anchor_off;
        env.check = NULL;
        env.image_lo = x->image.lo;
        env.image_hi = x->image.hi;
        env.text_lo = x->image.text_lo;
        env.text_hi = x->image.text_hi;
        for (i = 0; i < 2; i++) {
            env.game_lo[i] = x->game_lo[i];
            env.game_hi[i] = x->game_hi[i];
        }
        env.arch = *arch;
    }
    syms = read_file(syms_path, &syms_n);
    if (syms == NULL) {
        return die("the symbol file could not be read", syms_path);
    }
    p->tab = ico_symtab_parse(syms, syms_n, &env, why, sizeof why);
    free(syms);
    if (p->tab == NULL) {
        return die("the symbol file was refused", why);
    }
    p->proc.tab = p->tab;
    printf("symbols units %zu data %zu layout %016llx\n", p->tab->n_unit, p->tab->n_data,
           (unsigned long long)p->tab->layout_hash);
    /* the init and table words */
    words = read_file(words_path, &words_n);
    if (words == NULL) {
        return die("the words could not be read", words_path);
    }
    for (line = words; line != NULL && *line != '\0'; line = nl) {
        char cls[16];

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl++ = '\0';
        }
        if (sscanf(line, "%15s %llx", cls, &a) != 2) {
            continue;
        }
        for (i = 0; i < CLS_CODE; i++) {
            if (strcmp(cls, CLS_NAME[i]) == 0 &&
                words_add(&p->cls[i], (uint64_t)(uintptr_t)p->base + a) != 0) {
                return die("no memory for the words", NULL);
            }
        }
    }
    free(words);
    words_unique(&p->cls[CLS_INIT]);
    words_unique(&p->cls[CLS_TABLE]);
    free(image_path);
    free(relocs_path);
    free(syms_path);
    free(words_path);
    return 0;
}

#if defined(ICO_CARRY_PROBE_SELF)
static int self_image(void)
{
    IcoStateImage im;
    int i;

    if (ico_state_image_range(&im) != 0) {
        return die("ico_state_image_range found no image", NULL);
    }
#if defined(__linux__)
    {
        char exe[1024];
        const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);

        if (n > 0) {
            exe[n] = '\0';
            printf("exe %s\n", exe);
        }
    }
#endif
    printf("range 0 %llx\n", (unsigned long long)(im.hi - im.lo));
    printf("text %llx %llx\n", (unsigned long long)(im.text_lo - im.lo),
           (unsigned long long)(im.text_hi - im.lo));
    for (i = 0; i < im.n_ro; i++) {
        printf("ro %llx %llx\n", (unsigned long long)(im.ro_lo[i] - im.lo),
               (unsigned long long)(im.ro_hi[i] - im.lo));
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    Probe p;
    char *desc;
    size_t n;
    int arch, c, rc;

    if (argc == 2 && strcmp(argv[1], "--self-image") == 0) {
#if defined(ICO_CARRY_PROBE_SELF)
        return self_image();
#else
        return die("--self-image needs the CMake build (ICO_CARRY_PROBE_SELF)", NULL);
#endif
    }
    if (argc != 2) {
        fprintf(stderr, "usage: state_carry_probe DESC | --self-image\n");
        return 2;
    }
    memset(&p, 0, sizeof p);
    desc = read_file(argv[1], &n);
    if (desc == NULL) {
        return die("the description could not be read", argv[1]);
    }
    rc = parse_desc(&p, desc, &arch);
    free(desc);
    if (rc != 0) {
        return rc;
    }
    if (code_words(&p, arch) != 0) {
        return die("no memory for the code's words", NULL);
    }
    for (c = 0; c < CLS_N; c++) {
        if (judge(&p, c) != 0) {
            return die("the save ran out of memory", NULL);
        }
    }
    ico_symtab_free(p.tab);
    free(p.anchor);
    free((void *)(uintptr_t)p.proc.snap);
    free(p.snap_sorted);
    free(p.raw);
    for (c = 0; c < CLS_N; c++) {
        free(p.cls[c].w);
    }
    return 0;
}
