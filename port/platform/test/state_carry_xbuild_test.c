/*
 * port/platform/test/state_carry_xbuild_test.c
 *
 * Save states across versions on REAL links (docs/port/TESTING.md,
 * "state_carry_xbuild"): this file is linked twice, as two builds of one
 * program.  Build A (state_carry_xbuild_a) is linked as the desktop programs
 * are, read-only data in a segment of its own; build B (state_carry_xbuild_b,
 * CARRY_B) has more code and other texts ahead of A's, so every function and
 * text lies elsewhere, and is linked with its read-only data inside the
 * executable segment (lld --no-rosegment, else GNU ld -z noseparate-code):
 * issue 217's layout, Android's libmain.so below API 30.  Each reads its own
 * link map as its symbol file, finds its image with the product's
 * ico_state_image_range and its game data ranges with ico_game_data_ranges,
 * and runs the product's save (ico_xb_begin, ico_xb_block, ico_xb_scan,
 * ico_xb_finish) and load (ico_xb_check, then ico_state_reloc_word with the
 * verdict's translation) over:
 *
 *   the game's data   an initialised table (class A of issue 217): two short
 *                     texts, a text of 60 bytes, an EUC-JP text, a pointer
 *                     into a constant table, a function
 *   a block           words the game's memory holds at run time (classes B1
 *                     and B2): a short text, a 46-byte text and a 36-byte
 *                     EUC-JP text no game table holds, a pointer into the
 *                     constant table, a function, the game's data, a port
 *                     variable, the block itself, a start-up value copied
 *
 *   state_carry_xbuild_X save STATE MAP   writes the state
 *   state_carry_xbuild_X load STATE MAP   checks and translates it: every
 *                                         word must come out as this build's
 *                                         own address of the same thing
 *
 * tools/state_carry_check.py xbuild runs A's state into B and B's into A
 * and checks each program's layout line ("layout separate" for A, "layout
 * merged" for B: a text's address inside the image's code range).
 * Nothing of the game starts.  POSIX ELF only (the CMake file says where).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "code_hash.h"
#include "game_data_range.h"
#include "state_registry.h"
#include "state_reloc.h"
#include "state_syms.h"
#include "state_xbuild.h"

#if defined(CARRY_B)
/* build B's own code and texts ahead of A's: everything after moves */
__attribute__((noinline)) int carry_b_extra(int x)
{
    static const char pad[] = "build B's own text, which moves every text after it";

    return x * 11 + (int)pad[x & 31];
}

#define CARRY_LAYOUT "merged"
#else
#define CARRY_LAYOUT "separate"
#endif

/* the symbol file's anchor: a function the link map names */
__attribute__((noinline)) int carry_anchor(int x)
{
    return x ^ 0x5a;
}

__attribute__((noinline)) int carry_fn(int x)
{
    return x * 7 + 3;
}

static const int carry_consts[8] = {11, 22, 33, 44, 55, 66, 77, 88};

/* a port variable (a unique name in the map) */
int carry_port_var[4] = {1, 2, 3, 4};

/* the game's data: the same items at the same offsets in both builds (the
   data layout hash) */
__attribute__((used, section("ico_gdata"))) const void *carry_gtable[6] = {
    "carry:On",
    "a text of sixty bytes: longer than the old record's 24 bytes",
    "\xa5\xb5\xa5\xa6\xa5\xf3\xa5\xc9\xa4\xce\xc0\xdf\xc4\xea",
    &carry_consts[3],
    (const void *)carry_fn,
    "carry:Off",
};
__attribute__((used, section("ico_gdata"))) int carry_gvar[4] = {5, 6, 7, 8};

#define BLOCK_WORDS 9
static uint64_t carry_block[BLOCK_WORDS];

static void fill_block(void)
{
    carry_block[0] = (uint64_t)(uintptr_t)"carry:busy";
    carry_block[1] = (uint64_t)(uintptr_t)"iosMcMgrDelete: request busy, try again later\n";
    carry_block[2] = (uint64_t)(uintptr_t)"\xa5\xb5\xa5\xa6\xa5\xf3\xa5\xc9\xa5\xb9\xa5\xec\xa5\xc3"
                                          "\xa5\xc9\xa4\xce\xa5\xa8\xa5\xe9\xa1\xbc\x0a";
    carry_block[3] = (uint64_t)(uintptr_t)&carry_consts[5];
    carry_block[4] = (uint64_t)(uintptr_t)carry_fn;
    carry_block[5] = (uint64_t)(uintptr_t)&carry_gvar[2];
    carry_block[6] = (uint64_t)(uintptr_t)&carry_port_var[1];
    carry_block[7] = (uint64_t)(uintptr_t)&carry_block[3];
    carry_block[8] = (uint64_t)(uintptr_t)carry_gtable[1];
}

/* the state file: this header, the syms section, the block, the game data */
typedef struct CarryHead {
    uint64_t magic;
    uint64_t image_lo, image_hi;
    uint64_t game_lo[2], game_hi[2];
    uint64_t sec_n, block_n, gdata_n;
} CarryHead;

#define CARRY_MAGIC 0x5952524143444cull

static uint64_t *s_snap;
static size_t s_snap_words;
static IcoMemRange s_game[2];
static IcoStateImage s_image;
static IcoSymTab *s_tab;

static int fail(const char *what, const char *detail)
{
    printf("FAIL %s%s%s\n", what, detail != NULL ? ": " : "", detail != NULL ? detail : "");
    return 1;
}

static char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *p = NULL;
    long len;

    *n = 0;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        p = (char *)malloc((size_t)len);
        if (p != NULL && fread(p, 1, (size_t)len, f) != (size_t)len) {
            free(p);
            p = NULL;
        }
        if (p != NULL) {
            *n = (size_t)len;
        }
    }
    fclose(f);
    return p;
}

/* this process: its image, game ranges, start-up words and symbol table */
static int setup(const char *map)
{
    IcoSymTabEnv env;
    char why[160];
    char *text;
    size_t n;
    int i;

    if (ico_game_data_ranges(s_game) != 0) {
        return fail("no game data range in this program", NULL);
    }
    s_snap_words = (size_t)(s_game[ICO_GAME_DATA].hi - s_game[ICO_GAME_DATA].lo) / 8;
    s_snap = (uint64_t *)malloc(s_snap_words ? s_snap_words * 8 : 8);
    if (s_snap == NULL) {
        return fail("no memory", NULL);
    }
    memcpy(s_snap, (const void *)s_game[ICO_GAME_DATA].lo, s_snap_words * 8);
    if (ico_state_image_range(&s_image) != 0) {
        return fail("ico_state_image_range found no image", NULL);
    }
    {
        const uint64_t t = (uint64_t)(uintptr_t)carry_gtable[0];
        const int merged = t >= s_image.text_lo && t < s_image.text_hi;

        printf("layout %s (built %s): image %llx-%llx, code %llx-%llx, %d read-only ranges\n",
               merged ? "merged" : "separate", CARRY_LAYOUT, (unsigned long long)s_image.lo,
               (unsigned long long)s_image.hi, (unsigned long long)s_image.text_lo,
               (unsigned long long)s_image.text_hi, s_image.n_ro);
    }
    text = read_all(map, &n);
    if (text == NULL) {
        return fail("the link map could not be read", map);
    }
    memset(&env, 0, sizeof env);
    env.anchor = "carry_anchor";
    env.anchor_addr = (uint64_t)(uintptr_t)carry_anchor;
    env.check = "carry_fn";
    env.check_addr = (uint64_t)(uintptr_t)carry_fn;
    env.image_lo = s_image.lo;
    env.image_hi = s_image.hi;
    env.text_lo = s_image.text_lo;
    env.text_hi = s_image.text_hi;
    for (i = 0; i < 2; i++) {
        env.game_lo[i] = s_game[i].lo;
        env.game_hi[i] = s_game[i].hi;
    }
    env.arch = ico_code_arch_native();
    s_tab = ico_symtab_parse(text, n, &env, why, sizeof why);
    free(text);
    if (s_tab == NULL) {
        return fail("the link map was refused", why);
    }
    fill_block();
    return 0;
}

static int save(const char *path)
{
    IcoXbProc p;
    IcoXbRefs *r;
    IcoXbStats st;
    CarryHead h;
    void *sec = NULL;
    size_t n = 0;
    FILE *f;
    const uint64_t glo = s_game[ICO_GAME_DATA].lo, gn = s_game[ICO_GAME_DATA].hi - glo;
    int i;

    memset(&p, 0, sizeof p);
    p.tab = s_tab;
    p.image = s_image;
    for (i = 0; i < 2; i++) {
        p.game_lo[i] = s_game[i].lo;
        p.game_hi[i] = s_game[i].hi;
    }
    p.snap = s_snap;
    p.snap_words = s_snap_words;
    p.any_address = 1; /* a test program may lie below 4 GB */
    r = ico_xb_begin(&p);
    if (r == NULL) {
        return fail("no memory", NULL);
    }
    ico_xb_block(r, "carry.block", sizeof carry_block, ICO_STATE_SCAN,
                 (uint64_t)(uintptr_t)carry_block, 0);
    ico_xb_scan(r, carry_block, sizeof carry_block, (uint64_t)(uintptr_t)carry_block, 1);
    ico_xb_scan(r, (const void *)(uintptr_t)glo, (size_t)gn, glo, 1);
    if (ico_xb_finish(r, &sec, &n, &st) != 0) {
        return fail("ico_xb_finish ran out of memory", NULL);
    }
    printf("save: %u values, %u units, %u data, %u points, %u blocks, %u unnamed, flags %#x\n",
           st.values, st.units, st.data, st.points, st.blocks, st.left, st.flags);
    memset(&h, 0, sizeof h);
    h.magic = CARRY_MAGIC;
    h.image_lo = s_image.lo;
    h.image_hi = s_image.hi;
    for (i = 0; i < 2; i++) {
        h.game_lo[i] = s_game[i].lo;
        h.game_hi[i] = s_game[i].hi;
    }
    h.sec_n = n;
    h.block_n = sizeof carry_block;
    h.gdata_n = gn;
    f = fopen(path, "wb");
    if (f == NULL || fwrite(&h, sizeof h, 1, f) != 1 || fwrite(sec, 1, n, f) != n ||
        fwrite(carry_block, 1, sizeof carry_block, f) != sizeof carry_block ||
        fwrite((const void *)(uintptr_t)glo, 1, (size_t)gn, f) != (size_t)gn) {
        if (f != NULL) {
            fclose(f);
        }
        free(sec);
        return fail("the state could not be written", path);
    }
    fclose(f);
    free(sec);
    if (st.left != 0) {
        printf("save: %u words nothing names (the first %#llx): this state is pinned to its "
               "build\n",
               st.left, (unsigned long long)st.first_unnamed);
    }
    return 0;
}

static int load(const char *path)
{
    static const char *const names[1] = {"carry.block"};
    const uint64_t sizes[1] = {sizeof carry_block};
    const unsigned flags[1] = {ICO_STATE_SCAN};
    const uint64_t addrs[1] = {(uint64_t)(uintptr_t)carry_block};
    IcoXbNow now;
    IcoXbVerdict v;
    IcoStateXlate xl;
    IcoStateReloc rel;
    CarryHead h;
    uint64_t missed = 0, first_missed = 0;
    const unsigned char *sec, *blk, *gd;
    const uint64_t gtable_off =
        (uint64_t)(uintptr_t)carry_gtable - (uint64_t)s_game[ICO_GAME_DATA].lo;
    unsigned bad = 0, words = 0;
    char *file;
    size_t n, i;
    int k;

    file = read_all(path, &n);
    if (file == NULL || n < sizeof h) {
        return fail("the state could not be read", path);
    }
    memcpy(&h, file, sizeof h);
    if (h.magic != CARRY_MAGIC || sizeof h + h.sec_n + h.block_n + h.gdata_n != n ||
        h.block_n != sizeof carry_block) {
        free(file);
        return fail("the state is damaged", path);
    }
    sec = (const unsigned char *)file + sizeof h;
    blk = sec + h.sec_n;
    gd = blk + h.block_n;
    memset(&now, 0, sizeof now);
    now.tab = s_tab;
    now.image = s_image;
    for (k = 0; k < 2; k++) {
        now.game_lo[k] = s_game[k].lo;
        now.game_hi[k] = s_game[k].hi;
        now.old_game_lo[k] = h.game_lo[k];
        now.old_game_hi[k] = h.game_hi[k];
    }
    now.snap = s_snap;
    now.snap_words = s_snap_words;
    now.n_blocks = 1;
    now.block_name = names;
    now.block_size = sizes;
    now.block_flags = flags;
    now.block_addr = addrs;
    now.old_image_lo = h.image_lo;
    if (ico_xb_check(sec, (size_t)h.sec_n, &now, &v) != ICO_XB_OK) {
        printf("load: refused (%s): %s\n", ico_xb_kind_text(v.kind), v.why);
        ico_xb_verdict_free(&v);
        free(file);
        return fail("the other build's state was refused", NULL);
    }
    printf("load: %u units, %u data, %u points, %u searches, %u left as numbers\n", v.units, v.data,
           v.points, v.searches, v.left);
    memset(&xl, 0, sizeof xl);
    xl.span = v.span;
    xl.n_span = v.n_span;
    xl.point = v.point;
    xl.n_point = v.n_point;
    xl.missed = &missed;
    xl.first_missed = &first_missed;
    memset(&rel, 0, sizeof rel);
    rel.range[ICO_RELOC_MODULE].lo = h.image_lo;
    rel.range[ICO_RELOC_MODULE].hi = h.image_hi;
    rel.range[ICO_RELOC_MODULE].delta = (int64_t)(s_image.lo - h.image_lo);
    rel.xlate = &xl;
    /* the block's words and the game table's, each against this build's own */
    for (i = 0; i < BLOCK_WORDS + 6; i++) {
        uint64_t w, want;

        if (i < BLOCK_WORDS) {
            memcpy(&w, blk + 8 * i, 8);
            want = carry_block[i];
        } else {
            if (gtable_off + 8 * (i - BLOCK_WORDS) + 8 > h.gdata_n) {
                break;
            }
            memcpy(&w, gd + gtable_off + 8 * (i - BLOCK_WORDS), 8);
            want = (uint64_t)(uintptr_t)carry_gtable[i - BLOCK_WORDS];
        }
        (void)ico_state_reloc_word(&rel, &w);
        words++;
        if (w != want) {
            printf("load: word %zu is %#llx after the translation, this build's own is %#llx\n", i,
                   (unsigned long long)w, (unsigned long long)want);
            bad++;
        }
    }
    ico_xb_verdict_free(&v);
    free(file);
    if (bad != 0 || words != BLOCK_WORDS + 6 || missed != 0 || v.left != 0) {
        printf("load: %u of %u words wrong, %llu without a translation, %u left as numbers\n", bad,
               words, (unsigned long long)missed, v.left);
        return fail("the other build's state did not carry over", NULL);
    }
    printf("load: all %u words translated to this build's own addresses\n", words);
    return 0;
}

int main(int argc, char **argv)
{
    int rc;

    if (argc != 4 || (strcmp(argv[1], "save") != 0 && strcmp(argv[1], "load") != 0)) {
        fprintf(stderr, "usage: %s save|load STATE MAP\n", argv[0]);
        return 2;
    }
    rc = setup(argv[3]);
    if (rc == 0) {
        rc = strcmp(argv[1], "save") == 0 ? save(argv[2]) : load(argv[2]);
    }
    ico_symtab_free(s_tab);
    free(s_snap);
    return rc;
}
