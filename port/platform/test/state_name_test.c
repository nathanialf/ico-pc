/*
 * port/platform/test/state_name_test.c
 *
 * Save states across versions: every address a state holds is named
 * (state_xbuild.h, issue 217).  Two synthetic "builds" of one program, each
 * an image in memory with headers, read-only data (a sized constant, string
 * literals, an anonymous constant table), code, port data and the game's
 * data ranges at other places, and its own nm symbol table.  Nothing of the
 * game starts.
 *
 *   android  the image as Android's library maps it: one read and execute
 *            range from the headers through the code, no read-only range;
 *            the symbol file's section lines place the code.  A state's
 *            words (a return address, a game table's start-up pointer to a
 *            literal, a run-time literal at a sized constant's end address,
 *            a 60-byte literal, an EUC-JP literal, a pointer into an
 *            anonymous constant table, two statics of one name in two
 *            files, a game data pointer) carry over into a build where each
 *            moved, every one translated to its place, nothing unnamed.
 *            Without the section lines (the code range the loader's) the
 *            literals are code no unit holds: this is the case issue 217's
 *            states were pinned by.
 *   linux    read-only data in its own range (ELF R, R+X, R loads), no
 *            section lines: the same words carry over.
 *   windows  PE sections at Windows' fixed base (an upper half of 1): the
 *            texts, the long one in a suspended fiber's stack included,
 *            carry over; the anonymous table's word, no text, is kept as a
 *            number (a float next to the integer 1 lands in read-only data
 *            too) and the state still loads.
 *   refuse   a text twice in the later build with other neighbours, a text
 *            the later build lacks, a code word no unit holds (pinned, its
 *            list entry: the reason, the section, the nearest symbol, where
 *            it was found), the list bounded by ICO_XB_LIST.
 *   hashed   a window longer than the record's pool holds: named by its
 *            length, hash and first bytes, found again; missing refused.
 *   format   a version 2 record (a text point of at most 24 bytes) loads
 *            as before; a version 2 record's unnamed words refuse with the
 *            words they always had and no list; a damaged version 3 window.
 *   sections a GNU ld map's output sections (a long name's numbers on the
 *            next line), the code range from them, the image they make.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "code_hash.h"
#include "state_reloc.h"
#include "state_syms.h"
#include "state_xbuild.h"

enum { LAY_ANDROID, LAY_LINUX, LAY_WINDOWS };

static const char *const kLayName[3] = {"android", "linux", "windows"};

#define IMG_BYTES 0xA000u
#define HDR_BYTES 0x200u /* the headers; .rodata starts here */
#define TEXT_BYTES 0x4000u
#define GDATA_BYTES 0x400u
#define GBSS_BYTES 0x800u

/* the literals (kLong longer than a version 2 text point's 24 bytes) */
static const char kSleep[] = "SleepThread";
static const char kOn[] = "On";
static const char kLong[] = "iosMcMgrDelete: request busy, the card manager waits for it\n";
static const unsigned char kEuc[] = {0xa5, 0xb5, 0xa5, 0xa6, 0xa5, 0xf3, 0xa5, 0xc9,
                                     0x20, 0xa5, 0xb9, 0xa5, 0xec, 0xa5, 0xc3, 0xa5,
                                     0xc9, 0x20, 0xb3, 0xab, 0xbb, 0xcf, '\n', 0x00};

typedef struct Nb {
    uint8_t *mem;
    uint64_t lo, hi;
    uint64_t o_text; /* .rodata is [HDR_BYTES, o_text) */
    uint64_t o_names, o_after, o_sleep, o_on, o_long, o_euc, o_tab;
    uint64_t o_data_a, o_data_b, o_gdata, o_gbss;
    uint64_t o_dup;   /* 0, or a second "SleepThread" */
    uint64_t o_relro; /* two "t.c:kTab" and a private table, read-only once loaded */
    int relro_tweak;  /* the second kTab's contents changed */
    int no_long;
    uint64_t snap[GDATA_BYTES / 8];
    IcoSymTab *tab;
} Nb;

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put64le(uint8_t *p, uint64_t v)
{
    put32le(p, (uint32_t)v);
    put32le(p + 4, (uint32_t)(v >> 32));
}

/* which: 0 the saving build, 1 the later one (everything moved) */
static void nb_make(Nb *b, int which)
{
    memset(b, 0, sizeof *b);
    b->mem = (uint8_t *)calloc(1, IMG_BYTES);
    b->lo = (uint64_t)(uintptr_t)b->mem;
    b->hi = b->lo + IMG_BYTES;
    if (which == 0) {
        b->o_text = 0x2000;
        b->o_names = 0x300;
        b->o_after = 0; /* the sized constant's end: "SleepThread" itself */
        b->o_sleep = 0x320;
        b->o_on = 0x340;
        b->o_long = 0x400;
        b->o_euc = 0x480;
        b->o_tab = 0x500;
        b->o_data_a = 0x8000;
        b->o_data_b = 0x8010;
        b->o_gdata = 0x9000;
        b->o_gbss = 0x9400;
        b->o_relro = 0x8800;
    } else {
        b->o_text = 0x2400;
        b->o_names = 0x600;
        b->o_after = 0x620; /* another literal at the constant's end */
        b->o_sleep = 0x700;
        b->o_on = 0x380;
        b->o_long = 0x900;
        b->o_euc = 0x800;
        b->o_tab = 0xA00;
        b->o_data_a = 0x8100;
        b->o_data_b = 0x8120;
        b->o_gdata = 0x9100;
        b->o_gbss = 0x9500;
        b->o_relro = 0x8900;
    }
}

static void nb_fill(Nb *b)
{
    uint8_t *m = b->mem;
    uint32_t i;
    static const uint8_t f_main[7] = {0x55, 0x48, 0x89, 0xE5, 0x90, 0xC9, 0xC3};

    memset(m, 0, IMG_BYTES);
    /* headers: bytes no constant of the test holds */
    m[0] = 0x7F;
    m[1] = 'E';
    m[2] = 'L';
    m[3] = 'F';
    for (i = 4; i < HDR_BYTES; i++) {
        m[i] = (uint8_t)(0xA0 + (i * 7u) % 0x3Fu);
    }
    memcpy(m + b->o_names, "NAMESNAMESNAMESNAMESNAMESNAMESNA", 32);
    if (b->o_after != 0) {
        memcpy(m + b->o_after, "XYZ", 4);
    }
    memcpy(m + b->o_sleep, kSleep, sizeof kSleep);
    memcpy(m + b->o_on, kOn, sizeof kOn);
    if (!b->no_long) {
        memcpy(m + b->o_long, kLong, sizeof kLong);
    }
    memcpy(m + b->o_euc, kEuc, sizeof kEuc);
    for (i = 0; i < 8; i++) {
        put32le(m + b->o_tab + 4u * i, i + 1u);
    }
    if (b->o_dup != 0) {
        m[b->o_dup - 1] = 'Q';
        memcpy(m + b->o_dup, kSleep, sizeof kSleep);
    }
    memset(m + b->o_text, 0xCC, TEXT_BYTES);
    m[b->o_text + 0x20] = 0xC3;  /* ico_diag_init */
    m[b->o_text + 0x100] = 0xC3; /* ico_diag_log */
    memcpy(m + b->o_text + 0x200, f_main, sizeof f_main);
    m[b->o_text + 0x3F00] = 0xC3; /* f_last */
    /* .data.rel.ro: each object a relocated pointer (this build's own
       address) and constants */
    put64le(m + b->o_relro, b->lo + b->o_on);
    put64le(m + b->o_relro + 8, 0x11);
    put64le(m + b->o_relro + 16, 0x12);
    put64le(m + b->o_relro + 24, 0x13);
    put64le(m + b->o_relro + 0x20, b->lo + b->o_long);
    put64le(m + b->o_relro + 0x28, 0x21);
    put64le(m + b->o_relro + 0x30, b->relro_tweak ? 0x99 : 0x22);
    put64le(m + b->o_relro + 0x38, 0x23);
    put64le(m + b->o_relro + 0x40, b->lo + b->o_euc);
    put64le(m + b->o_relro + 0x48, 0x31);
    /* tableA[0] (gdata + 0x40) holds a pointer to "On" at start-up */
    memset(b->snap, 0, sizeof b->snap);
    b->snap[0x40 / 8] = b->lo + b->o_on;
}

static size_t nb_nm(const Nb *b, int sections, char *out, size_t size)
{
    size_t n = 0;

#define LINE3(off, ty, name)                                                                       \
    n += (size_t)snprintf(out + n, size - n, "%016llx %c %s\n", (unsigned long long)(off), ty, name)
#define LINE4(off, sz, ty, name)                                                                   \
    n += (size_t)snprintf(out + n, size - n, "%016llx %016llx %c %s\n", (unsigned long long)(off), \
                          (unsigned long long)(sz), ty, name)
#define LINE5(off, sz, ty, name, file)                                                             \
    n += (size_t)snprintf(out + n, size - n, "%016llx %016llx %c %s %s\n",                         \
                          (unsigned long long)(off), (unsigned long long)(sz), ty, name, file)
#define SECTION(off, sz, fl, name)                                                                 \
    n += (size_t)snprintf(out + n, size - n, "section %016llx %06llx %s %s\n",                     \
                          (unsigned long long)(off), (unsigned long long)(sz), fl, name)
    LINE3(b->o_text + 0x20, 'T', "ico_diag_init");
    LINE3(b->o_text + 0x100, 'T', "ico_diag_log");
    LINE3(b->o_text + 0x200, 'T', "f_main");
    LINE3(b->o_text + 0x3F00, 'T', "f_last");
    LINE4(b->o_names, 0x20, 'r', "kNames");
    LINE5(b->o_data_a, 8, 'd', "s_count", "a.c");
    LINE5(b->o_data_b, 8, 'd', "s_count", "b.c");
    /* two statics of one name in two files of one name, and a compiler's
       private table as tools/android_syms_lines.py names it from the map */
    LINE5(b->o_relro, 0x20, 'd', "kTab", "t.c");
    LINE5(b->o_relro + 0x20, 0x20, 'd', "kTab", "t.c");
    LINE5(b->o_relro + 0x40, 0x10, 'd', ".Lswitch.table.f_main", "t");
    LINE4(b->o_gdata, 0x40, 'D', "systemStatus");
    LINE4(b->o_gdata + 0x40, 0x40, 'D', "tableA");
    LINE4(b->o_gbss + 0x10, 4, 'B', "stage_no");
    if (sections) {
        /* as tools/package_android.sh writes them from the section headers */
        SECTION(HDR_BYTES, b->o_text - HDR_BYTES, "AMS", ".rodata");
        SECTION(b->o_text, TEXT_BYTES, "AX", ".text");
        SECTION(0x8000, 0x800, "WA", ".data");
        SECTION(0x8800, 0x800, "WA", ".data.rel.ro");
        SECTION(b->o_gdata, GDATA_BYTES, "WA", "ico_gdata");
        SECTION(b->o_gbss, GBSS_BYTES, "WA", "ico_gbss");
    }
#undef LINE3
#undef LINE4
#undef LINE5
#undef SECTION
    return n;
}

/* the image as ico_state_image_range reports it for the layout */
static void nb_image(const Nb *b, int lay, IcoStateImage *im)
{
    memset(im, 0, sizeof *im);
    im->lo = b->lo;
    im->hi = b->hi;
    im->text_hi = b->lo + b->o_text + TEXT_BYTES;
    if (lay == LAY_ANDROID) {
        /* one R+X load from the headers: no read-only load */
        im->text_lo = b->lo;
        return;
    }
    im->text_lo = b->lo + b->o_text;
    im->n_ro = 1;
    im->ro_lo[0] = b->lo + (lay == LAY_WINDOWS ? HDR_BYTES : 0u); /* .rdata; ELF's R load */
    im->ro_hi[0] = b->lo + b->o_text;
}

/* the image start the number rule and the load's pin rule judge by */
static uint64_t judge(int lay)
{
    return lay == LAY_WINDOWS ? 0x0000000140000000ull
           : lay == LAY_LINUX ? 0x0000555555554000ull
                              : 0x0000007400000000ull;
}

static void nb_parse(Nb *b, int lay, int sections)
{
    char text[4096], why[160];
    IcoSymTabEnv e;
    IcoStateImage im;
    const size_t n = nb_nm(b, sections, text, sizeof text);

    nb_image(b, lay, &im);
    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = b->lo + b->o_text + 0x20;
    e.check = "ico_diag_log";
    e.check_addr = b->lo + b->o_text + 0x100;
    e.image_lo = b->lo;
    e.image_hi = b->hi;
    e.text_lo = im.text_lo;
    e.text_hi = im.text_hi;
    e.game_lo[0] = b->lo + b->o_gdata;
    e.game_hi[0] = e.game_lo[0] + GDATA_BYTES;
    e.game_lo[1] = b->lo + b->o_gbss;
    e.game_hi[1] = e.game_lo[1] + GBSS_BYTES;
    e.arch = ICO_CODE_RAW;
    ico_symtab_free(b->tab);
    b->tab = ico_symtab_parse(text, n, &e, why, sizeof why);
    CHECK(b->tab != NULL, "%s: the synthetic build's table: %s", kLayName[lay], why);
}

static void nb_free(Nb *b)
{
    ico_symtab_free(b->tab);
    free(b->mem);
    b->tab = NULL;
    b->mem = NULL;
}

/* A saves the n words (strict), read from `where`; the section */
static void *save_words(const Nb *a, int lay, const uint64_t *w, int n, const char *where,
                        size_t *bytes, IcoXbStats *st)
{
    IcoXbProc p;
    IcoXbRefs *r;
    void *sec = NULL;

    memset(&p, 0, sizeof p);
    p.tab = a->tab;
    nb_image(a, lay, &p.image);
    p.game_lo[0] = a->lo + a->o_gdata;
    p.game_hi[0] = p.game_lo[0] + GDATA_BYTES;
    p.game_lo[1] = a->lo + a->o_gbss;
    p.game_hi[1] = p.game_lo[1] + GBSS_BYTES;
    p.snap = a->snap;
    p.snap_words = GDATA_BYTES / 8;
    p.any_address = 1;
    p.judge_lo = judge(lay);
    *bytes = 0;
    memset(st, 0, sizeof *st);
    r = ico_xb_begin(&p);
    CHECK(r != NULL, "ico_xb_begin");
    if (r == NULL) {
        return NULL;
    }
    ico_xb_where(r, where);
    ico_xb_scan(r, w, (size_t)n * 8u, (uint64_t)(uintptr_t)w, 1);
    CHECK(ico_xb_finish(r, &sec, bytes, st) == 0, "ico_xb_finish");
    return sec;
}

static int check_words(const void *sec, size_t n, const Nb *b, const Nb *a, int lay,
                       IcoXbVerdict *v)
{
    IcoXbNow now;

    memset(&now, 0, sizeof now);
    now.tab = b->tab;
    nb_image(b, lay, &now.image);
    now.game_lo[0] = b->lo + b->o_gdata;
    now.game_hi[0] = now.game_lo[0] + GDATA_BYTES;
    now.game_lo[1] = b->lo + b->o_gbss;
    now.game_hi[1] = now.game_lo[1] + GBSS_BYTES;
    now.snap = b->snap;
    now.snap_words = GDATA_BYTES / 8;
    now.old_game_lo[0] = a->lo + a->o_gdata;
    now.old_game_hi[0] = now.old_game_lo[0] + GDATA_BYTES;
    now.old_game_lo[1] = a->lo + a->o_gbss;
    now.old_game_hi[1] = now.old_game_lo[1] + GBSS_BYTES;
    now.old_image_lo = judge(lay);
    return ico_xb_check(sec, n, &now, v);
}

/* the verdict's translation over the n words (A's MODULE range); the
   words without one counted in *missed */
static void translate(const IcoXbVerdict *v, const Nb *a, const Nb *b, uint64_t *w, int n,
                      uint64_t *missed)
{
    IcoStateReloc rel;
    IcoStateXlate xl;
    uint64_t first = 0;

    memset(&rel, 0, sizeof rel);
    rel.range[ICO_RELOC_MODULE].lo = a->lo;
    rel.range[ICO_RELOC_MODULE].hi = a->hi;
    rel.range[ICO_RELOC_MODULE].delta = (int64_t)(b->lo - a->lo);
    memset(&xl, 0, sizeof xl);
    xl.span = v->span;
    xl.n_span = v->n_span;
    xl.point = v->point;
    xl.n_point = v->n_point;
    xl.missed = missed;
    xl.first_missed = &first;
    rel.xlate = &xl;
    *missed = 0;
    (void)ico_state_reloc_scan(&rel, w, (size_t)n * 8u, NULL);
}

/* the words a state of A holds */
enum { W_RET, W_SNAP, W_SLEEP, W_LONG, W_EUC, W_TAB, W_A, W_B, W_GAME, W_COUNT };

static void state_words(const Nb *a, uint64_t w[W_COUNT])
{
    w[W_RET] = a->lo + a->o_text + 0x200 + 4; /* inside f_main */
    w[W_SNAP] = a->lo + a->o_on;              /* tableA[0]'s start-up value */
    w[W_SLEEP] = a->lo + a->o_sleep;          /* at kNames' end address */
    w[W_LONG] = a->lo + a->o_long;
    w[W_EUC] = a->lo + a->o_euc;
    w[W_TAB] = a->lo + a->o_tab + 12; /* the table's fourth entry */
    w[W_A] = a->lo + a->o_data_a + 4; /* a.c's s_count */
    w[W_B] = a->lo + a->o_data_b;     /* b.c's s_count */
    w[W_GAME] = a->lo + a->o_gdata + 8;
}

static void want_words(const Nb *b, uint64_t w[W_COUNT])
{
    state_words(b, w);
}

static void test_carry(int lay)
{
    const char *const L = kLayName[lay];
    const int sections = lay == LAY_ANDROID;
    Nb a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    uint64_t w[W_COUNT], want[W_COUNT], missed = 0;
    void *sec;
    size_t n = 0;
    int k, i;

    CHECK(sizeof kLong > ICO_XB_CONTENT + 30, "the long literal is past a version 2 point");
    nb_make(&a, 0);
    nb_make(&b, 1);
    nb_fill(&a);
    nb_fill(&b);
    nb_parse(&a, lay, sections);
    nb_parse(&b, lay, sections);
    if (a.tab == NULL || b.tab == NULL) {
        nb_free(&a);
        nb_free(&b);
        return;
    }
    if (sections) {
        CHECK(a.tab->code_from == 1 && a.tab->code_lo == a.lo + a.o_text &&
                  a.tab->code_hi == a.lo + a.o_text + TEXT_BYTES,
              "%s: the code range from the .text line", L);
        CHECK(ico_symtab_data_named(a.tab, ico_symtab_name_hash("kNames"), "kNames", 0) != NULL,
              "%s: kNames, in the executable range, is data", L);
    } else {
        CHECK(a.tab->code_from == 0, "%s: no section line: the loader's code range", L);
    }
    CHECK(ico_symtab_data_named(a.tab, ico_symtab_name_hash("a.c:s_count"), "a.c:s_count", 0) !=
                  NULL &&
              ico_symtab_data_named(a.tab, ico_symtab_name_hash("a.c:s_count"), "a.c:s_count", 0)
                  ->unique,
          "%s: a static of a repeated name, named by its file", L);
    state_words(&a, w);
    /* Windows: the long text as a suspended fiber's stack holds it */
    sec = save_words(&a, lay, w, W_COUNT,
                     lay == LAY_WINDOWS ? "fiber slot 5's stack" : "the test's words", &n, &st);
    if (lay == LAY_WINDOWS) {
        CHECK(sec != NULL && (st.flags & ICO_XB_UNNAMED) != 0 && st.left == 1 &&
                  st.first_unnamed == w[W_TAB] && st.listed == 1 &&
                  st.list[0].reason == ICO_XB_WHY_NUMBER,
              "%s: the table's word, no text, is kept as a number (%u left, flags %#x)", L, st.left,
              st.flags);
        CHECK(st.windows == 3 && st.texts == 3 && st.points == 4,
              "%s: three texts by their windows, the start-up value (%u windows, %u texts, %u "
              "points)",
              L, st.windows, st.texts, st.points);
    } else {
        CHECK(sec != NULL && (st.flags & ICO_XB_UNNAMED) == 0 && st.left == 0,
              "%s: nothing unnamed (%u left, first 0x%llx, flags %#x)", L, st.left,
              (unsigned long long)st.first_unnamed, st.flags);
        CHECK(
            st.windows == 4 && st.texts == 3 && st.points == 5,
            "%s: four windows (three texts), the start-up value (%u windows, %u texts, %u points)",
            L, st.windows, st.texts, st.points);
    }
    CHECK(st.units == 1 && st.positions == 1 && st.data == 2,
          "%s: f_main with a frame inside, the two statics (%u units, %u data)", L, st.units,
          st.data);
    CHECK(st.code_from == sections, "%s: the record's code range source", L);
    k = sec != NULL ? check_words(sec, n, &b, &a, lay, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_OK && v.version == ICO_XB_VERSION, "%s: B loads A's state: %d %s", L, k,
          v.why);
    if (k == ICO_XB_OK) {
        CHECK(v.windows == (lay == LAY_WINDOWS ? 3u : 4u), "%s: the windows found again (%u)", L,
              v.windows);
        translate(&v, &a, &b, w, W_COUNT, &missed);
        want_words(&b, want);
        if (lay == LAY_WINDOWS) {
            want[W_TAB] = a.lo + a.o_tab + 12; /* kept as the number it may be */
        }
        for (i = 0; i < W_COUNT; i++) {
            CHECK(w[i] == want[i], "%s: word %d translated to 0x%llx, wanted 0x%llx", L, i,
                  (unsigned long long)w[i], (unsigned long long)want[i]);
        }
        CHECK(missed == (lay == LAY_WINDOWS ? 1u : 0u), "%s: words without a translation: %llu", L,
              (unsigned long long)missed);
    }
    ico_xb_verdict_free(&v);
    free(sec);
    nb_free(&a);
    nb_free(&b);
}

static void test_refuse(void)
{
    Nb a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    uint64_t w[W_COUNT], code[20];
    void *sec;
    size_t n = 0;
    int k, i;

    nb_make(&a, 0);
    nb_make(&b, 1);
    nb_fill(&a);
    b.o_dup = 0x1000; /* a second "SleepThread", after a 'Q' */
    nb_fill(&b);
    nb_parse(&a, LAY_ANDROID, 1);
    nb_parse(&b, LAY_ANDROID, 1);
    if (a.tab == NULL || b.tab == NULL) {
        nb_free(&a);
        nb_free(&b);
        return;
    }
    state_words(&a, w);
    sec = save_words(&a, LAY_ANDROID, w, W_COUNT, "the test's words", &n, &st);
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_DATA && strstr(v.why, "the text \"SleepThread\" is in more than one place "
                                            "here") != NULL,
          "a text twice in the later build: %d %s", k, v.why);
    ico_xb_verdict_free(&v);
    b.o_dup = 0;
    b.no_long = 1;
    nb_fill(&b);
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_DATA && strstr(v.why, "\"iosMcMgrDelete: request busy") != NULL &&
              strstr(v.why, "is missing here") != NULL,
          "a text the later build lacks: %d %s", k, v.why);
    ico_xb_verdict_free(&v);
    b.no_long = 0;
    nb_fill(&b);
    free(sec);

    /* a code word no unit holds (.text before ico_diag_init): pinned, listed */
    code[0] = a.lo + a.o_text + 8;
    sec = save_words(&a, LAY_ANDROID, code, 1, "the test's words", &n, &st);
    CHECK(sec != NULL && (st.flags & ICO_XB_UNNAMED) != 0 && st.left == 1 && st.listed == 1,
          "a code word no unit holds: unnamed (%u)", st.left);
    if (st.listed == 1) {
        const IcoXbUnnamed *u = &st.list[0];

        CHECK(u->word == code[0] && u->reason == ICO_XB_WHY_CODE &&
                  strcmp(u->section, ".text") == 0 && strcmp(u->sym, "kNames") == 0 &&
                  u->sym_off == code[0] - (a.lo + a.o_names) &&
                  strcmp(u->where, "the test's words") == 0 &&
                  u->at == (uint64_t)(uintptr_t)&code[0],
              "the save's list: 0x%llx %u %s %s+0x%llx %s at 0x%llx", (unsigned long long)u->word,
              u->reason, u->section, u->sym, (unsigned long long)u->sym_off, u->where,
              (unsigned long long)u->at);
    }
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_PINNED && v.by_unnamed && strstr(v.why, "no symbol names") != NULL &&
              v.listed == 1 && v.list[0].word == code[0] && v.list[0].reason == ICO_XB_WHY_CODE &&
              strcmp(v.list[0].section, ".text") == 0 &&
              strcmp(v.list[0].where, "the test's words") == 0,
          "the load's refusal and the state's list: %d %s (%u listed)", k, v.why, v.listed);
    ico_xb_verdict_free(&v);
    free(sec);

    /* more such words than the list holds: all counted, ICO_XB_LIST listed */
    for (i = 0; i < 20; i++) {
        code[i] = a.lo + a.o_text + (uint64_t)i; /* [.text, ico_diag_init) */
    }
    sec = save_words(&a, LAY_ANDROID, code, 20, "the test's words", &n, &st);
    CHECK(sec != NULL && st.left == 20 && st.listed == ICO_XB_LIST,
          "twenty unnamed words: %u counted, %u listed", st.left, st.listed);
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_PINNED && v.left == 20 && v.listed == ICO_XB_LIST,
          "the load reads the bounded list: %u of %u", v.listed, v.left);
    ico_xb_verdict_free(&v);
    free(sec);
    nb_free(&a);
    nb_free(&b);
}

/* --- a window longer than the pool ------------------------------------------------- */

#define BIG_RO 0x500000u /* above ICO_XB_POOL_BYTES less every point's prefix */
#define BIG_TEXT 0x1000u

typedef struct Big {
    uint8_t *mem;
    uint64_t lo, hi, ro_bytes;
    IcoSymTab *tab;
} Big;

/* read-only zeros with one marker at their end, `lead` more zeros ahead;
   the code after them */
static int big_make(Big *g, uint64_t lead, int marker)
{
    char nm[160], why[160];
    IcoSymTabEnv e;

    memset(g, 0, sizeof *g);
    g->ro_bytes = BIG_RO + lead;
    g->mem = (uint8_t *)calloc(1, g->ro_bytes + BIG_TEXT);
    if (g->mem == NULL) {
        return -1;
    }
    g->lo = (uint64_t)(uintptr_t)g->mem;
    g->hi = g->lo + g->ro_bytes + BIG_TEXT;
    if (marker) {
        g->mem[g->ro_bytes - 1] = 0x5A;
    }
    memset(g->mem + g->ro_bytes, 0xCC, BIG_TEXT);
    g->mem[g->ro_bytes + 0x20] = 0xC3;
    g->mem[g->ro_bytes + 0x100] = 0xC3;
    snprintf(nm, sizeof nm, "%016llx T ico_diag_init\n%016llx T ico_diag_log\n",
             (unsigned long long)(g->ro_bytes + 0x20), (unsigned long long)(g->ro_bytes + 0x100));
    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = g->lo + g->ro_bytes + 0x20;
    e.check = "ico_diag_log";
    e.check_addr = g->lo + g->ro_bytes + 0x100;
    e.image_lo = g->lo;
    e.image_hi = g->hi;
    e.text_lo = g->lo + g->ro_bytes;
    e.text_hi = g->hi;
    e.arch = ICO_CODE_RAW;
    g->tab = ico_symtab_parse(nm, strlen(nm), &e, why, sizeof why);
    CHECK(g->tab != NULL, "the big table: %s", why);
    return g->tab != NULL ? 0 : -1;
}

static void big_image(const Big *g, IcoStateImage *im)
{
    memset(im, 0, sizeof *im);
    im->lo = g->lo;
    im->hi = g->hi;
    im->text_lo = g->lo + g->ro_bytes;
    im->text_hi = g->hi;
    im->n_ro = 1;
    im->ro_lo[0] = g->lo;
    im->ro_hi[0] = g->lo + g->ro_bytes;
}

static void big_free(Big *g)
{
    ico_symtab_free(g->tab);
    free(g->mem);
}

static void test_hashed(void)
{
    Big a, b;
    IcoXbProc p;
    IcoXbRefs *r;
    IcoXbStats st;
    IcoXbNow now;
    IcoXbVerdict v;
    uint64_t w[1], missed = 0;
    void *sec = NULL;
    size_t n = 0;
    int k;

    if (big_make(&a, 0, 1) != 0 || big_make(&b, 16, 1) != 0) {
        big_free(&a);
        big_free(&b);
        return;
    }
    memset(&p, 0, sizeof p);
    p.tab = a.tab;
    big_image(&a, &p.image);
    p.any_address = 1;
    p.judge_lo = 0x0000007400000000ull;
    w[0] = a.lo + 8; /* in the zeros: unique only with the marker */
    r = ico_xb_begin(&p);
    if (r != NULL) {
        ico_xb_scan(r, w, sizeof w, (uint64_t)(uintptr_t)w, 1);
        CHECK(ico_xb_finish(r, &sec, &n, &st) == 0, "ico_xb_finish");
    }
    CHECK(sec != NULL && st.windows == 1 && (st.flags & ICO_XB_UNNAMED) == 0 && n < BIG_RO / 4,
          "a window of the whole range, its bytes not stored (%u windows, %zu bytes)", st.windows,
          n);
    memset(&now, 0, sizeof now);
    now.tab = b.tab;
    big_image(&b, &now.image);
    now.old_image_lo = 0x0000007400000000ull;
    k = sec != NULL ? ico_xb_check(sec, n, &now, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_OK && v.windows == 1, "found again by its hash: %d %s", k, v.why);
    if (k == ICO_XB_OK) {
        IcoStateReloc rel;
        IcoStateXlate xl;
        uint64_t first = 0;

        memset(&rel, 0, sizeof rel);
        rel.range[ICO_RELOC_MODULE].lo = a.lo;
        rel.range[ICO_RELOC_MODULE].hi = a.hi;
        memset(&xl, 0, sizeof xl);
        xl.point = v.point;
        xl.n_point = v.n_point;
        xl.span = v.span;
        xl.n_span = v.n_span;
        xl.missed = &missed;
        xl.first_missed = &first;
        rel.xlate = &xl;
        (void)ico_state_reloc_scan(&rel, w, sizeof w, NULL);
        CHECK(w[0] == b.lo + 16 + 8, "the word at its place past the 16 more zeros");
    }
    ico_xb_verdict_free(&v);
    b.mem[b.ro_bytes - 1] = 0; /* no marker: missing */
    k = sec != NULL ? ico_xb_check(sec, n, &now, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_DATA && strstr(v.why, "is missing here") != NULL, "hashed, missing: %d %s", k,
          v.why);
    ico_xb_verdict_free(&v);
    free(sec);
    big_free(&a);
    big_free(&b);
}

/* --- the format --------------------------------------------------------------------- */

/* a version 2 record: one text point and the flags and words given */
static size_t v2_record(uint8_t *out, unsigned flags, uint64_t layout, int arch, uint64_t old,
                        const char *text, uint64_t first_unnamed, unsigned left)
{
    const unsigned np = text != NULL ? 1u : 0u;
    const size_t total = 64u + np * 48u + 1u;

    memset(out, 0, total);
    put32le(out, ICO_XB_MAGIC);
    put32le(out + 4, 2);
    put32le(out + 8, flags);
    put32le(out + 12, ICO_CODE_HASH_RULES);
    put32le(out + 16, (uint32_t)arch);
    put32le(out + 28, np);
    put32le(out + 36, 1); /* the names: "" */
    put64le(out + 40, layout);
    put64le(out + 48, first_unnamed);
    put32le(out + 56, 1);
    put32le(out + 60, left);
    if (np != 0) {
        uint8_t *q = out + 64;

        put64le(q, old);
        put32le(q + 8, 2); /* a text */
        put32le(q + 12, (uint32_t)strlen(text) + 1u);
        memcpy(q + 24, text, strlen(text) + 1u);
    }
    return total;
}

static void test_format(void)
{
    Nb a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    uint8_t rec[160];
    uint64_t w[W_COUNT];
    void *sec;
    size_t n;
    int k;

    nb_make(&a, 0);
    nb_make(&b, 1);
    nb_fill(&a);
    nb_fill(&b);
    nb_parse(&a, LAY_LINUX, 0);
    nb_parse(&b, LAY_LINUX, 0);
    if (a.tab == NULL || b.tab == NULL) {
        nb_free(&a);
        nb_free(&b);
        return;
    }
    /* version 2: a text point, found at the first place that holds it */
    n = v2_record(rec, ICO_XB_TABLE, b.tab->layout_hash, b.tab->env.arch, a.lo + a.o_sleep, kSleep,
                  0, 0);
    k = check_words(rec, n, &b, &a, LAY_LINUX, &v);
    CHECK(k == ICO_XB_OK && v.version == 2 && v.points == 1 && v.windows == 0 && v.listed == 0,
          "a version 2 record loads: %d %s", k, v.why);
    if (k == ICO_XB_OK) {
        uint64_t to = 0;
        IcoStateXlate xl;

        memset(&xl, 0, sizeof xl);
        xl.point = v.point;
        xl.n_point = v.n_point;
        CHECK(ico_state_xlate(&xl, a.lo + a.o_sleep, &to) && to == b.lo + b.o_sleep,
              "the version 2 text point translated");
    }
    ico_xb_verdict_free(&v);
    /* version 2: unnamed words above an upper half of 1, as v0.10.0 to
       v0.11.8 saved every Android state: the same refusal, no list */
    n = v2_record(rec, ICO_XB_TABLE | ICO_XB_UNNAMED, b.tab->layout_hash, b.tab->env.arch, 0, NULL,
                  0x0000007400001234ull, 3);
    k = check_words(rec, n, &b, &a, LAY_ANDROID, &v);
    CHECK(k == ICO_XB_PINNED && v.by_unnamed && v.listed == 0 &&
              strcmp(v.why, "it holds a program address no symbol names (0x7400001234, 3 such "
                            "words): the version that saved it left them unnamed") == 0,
          "a version 2 state with unnamed words: %d %s", k, v.why);
    ico_xb_verdict_free(&v);
    /* an arm64 record made without the page words: refused */
    n = v2_record(rec, ICO_XB_TABLE, b.tab->layout_hash, ICO_CODE_A64, a.lo + a.o_sleep, kSleep, 0,
                  0);
    k = check_words(rec, n, &b, &a, LAY_LINUX, &v);
    CHECK(k == ICO_XB_PINNED && strstr(v.why, "pages its threads keep in registers") != NULL,
          "an arm64 record without page words: %d %s", k, v.why);
    ico_xb_verdict_free(&v);
    /* version 2: a text point longer than it can be */
    n = v2_record(rec, ICO_XB_TABLE, b.tab->layout_hash, b.tab->env.arch, a.lo + a.o_sleep, kSleep,
                  0, 0);
    put32le(rec + 64 + 12, ICO_XB_CONTENT + 1u);
    k = check_words(rec, n, &b, &a, LAY_LINUX, &v);
    CHECK(k == ICO_XB_BAD && strstr(v.why, "text is damaged") != NULL,
          "a damaged version 2 text: %s", v.why);
    ico_xb_verdict_free(&v);
    /* version 3: a window that says it stored more than its length */
    state_words(&a, w);
    sec = save_words(&a, LAY_LINUX, w, W_COUNT, "the test's words", &n, &st);
    if (sec != NULL) {
        uint8_t *s = (uint8_t *)sec;
        const size_t pts = 80u + (size_t)get32le(s + 20) * 48u + (size_t)get32le(s + 24) * 48u;
        const unsigned np = get32le(s + 28);
        unsigned i, hit = 0;

        CHECK(get32le(s + 4) == 3 && n >= 80, "the record is version 3");
        for (i = 0; i < np && !hit; i++) {
            uint8_t *q = s + pts + (size_t)i * 64u;

            if (get32le(q + 8) == 3) {
                put32le(q + 36, get32le(q + 12) + 1u);
                hit = 1;
            }
        }
        CHECK(hit, "the record holds a window");
        k = check_words(sec, n, &b, &a, LAY_LINUX, &v);
        CHECK(k == ICO_XB_BAD && strstr(v.why, "window is damaged") != NULL,
              "a damaged version 3 window: %d %s", k, v.why);
        ico_xb_verdict_free(&v);
        free(sec);
    }
    nb_free(&a);
    nb_free(&b);
}

/* --- a GNU ld map's output sections ------------------------------------------------- */

static void test_sections(void)
{
    static const char map[] = "Memory Configuration\n"
                              "\n"
                              "*default*        0x0000000000000000 0xffffffffffffffff\n"
                              "\n"
                              "Linker script and memory map\n"
                              "\n"
                              ".text           0x0000000000401000     0x2000\n"
                              " .text          0x0000000000401000      0x100 main.c.o\n"
                              "                0x0000000000401000                ico_diag_init\n"
                              "                0x0000000000401080                ico_diag_log\n"
                              ".gcc_except_table\n"
                              "                0x0000000000403000       0x40\n"
                              ".rdata          0x0000000000404000      0x200\n"
                              " .rdata         0x0000000000404000      0x200 main.c.o\n";
    IcoSymTabEnv e;
    IcoSymTab *t;
    IcoStateImage im;
    const IcoSymSec *s;
    const char *nm;
    uint64_t off = 0;
    char why[160];

    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = 0x401000;
    e.check = "ico_diag_log";
    e.check_addr = 0x401080;
    e.image_lo = 0x400000;
    e.image_hi = 0x500000;
    e.text_lo = 0x401000; /* the executable range runs past .text */
    e.text_hi = 0x404000;
    e.arch = ICO_CODE_RAW;
    t = ico_symtab_parse(map, sizeof map - 1, &e, why, sizeof why);
    CHECK(t != NULL, "the map parses: %s", why);
    if (t == NULL) {
        return;
    }
    CHECK(t->n_sec == 3 && t->code_from == 1 && t->code_lo == 0x401000 && t->code_hi == 0x403000,
          "three output sections (*default* is not one), the code .text's: %zu %d 0x%llx-0x%llx",
          t->n_sec, t->code_from, (unsigned long long)t->code_lo, (unsigned long long)t->code_hi);
    s = ico_symtab_section_at(t, 0x403010);
    CHECK(s != NULL && strcmp(ico_symtab_name(t, s->name), ".gcc_except_table") == 0 && !s->code,
          "a long section name with its numbers on the next line");
    s = ico_symtab_section_at(t, 0x4041ff);
    CHECK(s != NULL && strcmp(ico_symtab_name(t, s->name), ".rdata") == 0, ".rdata");
    CHECK(ico_symtab_section_at(t, 0x403800) == NULL, "between sections: none");
    nm = ico_symtab_nearest(t, 0x401090, &off);
    CHECK(nm != NULL && strcmp(nm, "ico_diag_log") == 0 && off == 0x10, "the nearest symbol");
    memset(&im, 0, sizeof im);
    im.lo = e.image_lo;
    im.hi = e.image_hi;
    im.text_lo = e.text_lo;
    im.text_hi = e.text_hi;
    ico_xb_image(t, &im);
    CHECK(im.text_lo == 0x401000 && im.text_hi == 0x403000 && im.n_ro == 1 &&
              im.ro_lo[0] == 0x403000 && im.ro_hi[0] == 0x404000,
          "the image: the code .text's, the rest of the executable range read-only data");
    ico_xb_image(t, &im);
    CHECK(im.n_ro == 1, "placed twice: unchanged");
    ico_symtab_free(t);
}

/* --- the layout hash: the game's variables, their own names ----------------------- */

static uint64_t layout_of(const char *game_lines)
{
    char nm[1024], why[160];
    IcoSymTabEnv e;
    IcoSymTab *t;
    uint64_t h = 0;

    snprintf(nm, sizeof nm, "0000000000001000 T ico_diag_init\n0000000000001080 T ico_diag_log\n%s",
             game_lines);
    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = 0x7400001000ull;
    e.check = "ico_diag_log";
    e.check_addr = 0x7400001080ull;
    e.image_lo = 0x7400000000ull;
    e.image_hi = 0x7400040000ull;
    e.text_lo = 0x7400001000ull;
    e.text_hi = 0x7400008000ull;
    e.game_lo[0] = 0x7400009000ull;
    e.game_hi[0] = 0x7400009400ull;
    e.arch = ICO_CODE_RAW;
    t = ico_symtab_parse(nm, strlen(nm), &e, why, sizeof why);
    CHECK(t != NULL && t->n_layout == 3, "the layout table: %s", why);
    if (t != NULL) {
        h = t->layout_hash;
    }
    ico_symtab_free(t);
    return h;
}

static void test_layout(void)
{
    /* two statics of one name: with the file column (a library where the
       name repeats) and without it (one where it does not) */
    const uint64_t a = layout_of("0000000000009000 0000000000000040 D systemStatus\n"
                                 "0000000000009040 0000000000000008 b count Matrix.c\n"
                                 "0000000000009048 0000000000000008 b count matrixDrive.c\n");
    const uint64_t b = layout_of("0000000000009000 0000000000000040 D systemStatus\n"
                                 "0000000000009040 0000000000000008 b count\n"
                                 "0000000000009048 0000000000000008 b count\n");
    const uint64_t moved = layout_of("0000000000009000 0000000000000040 D systemStatus\n"
                                     "0000000000009040 0000000000000008 b count Matrix.c\n"
                                     "0000000000009050 0000000000000008 b count matrixDrive.c\n");
    const uint64_t resized = layout_of("0000000000009000 0000000000000040 D systemStatus\n"
                                       "0000000000009040 0000000000000008 b count Matrix.c\n"
                                       "0000000000009048 0000000000000010 b count matrixDrive.c\n");

    CHECK(a != 0 && a == b, "the file column leaves the layout hash alone");
    CHECK(moved != a, "a moved game variable: another layout hash");
    CHECK(resized != a, "a resized game variable: another layout hash");
}

/* --- read-only once loaded: names that repeat ----------------------------------------- */

static void test_masked(void)
{
    Nb a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    uint64_t w[3], want[3], missed = 0;
    void *sec;
    size_t n = 0;
    int k, i;

    nb_make(&a, 0);
    nb_make(&b, 1);
    nb_fill(&a);
    nb_fill(&b);
    nb_parse(&a, LAY_ANDROID, 1);
    nb_parse(&b, LAY_ANDROID, 1);
    if (a.tab == NULL || b.tab == NULL) {
        nb_free(&a);
        nb_free(&b);
        return;
    }
    CHECK(
        ico_symtab_data_named(a.tab, ico_symtab_name_hash("t.c:kTab"), "t.c:kTab", 1) != NULL &&
            !ico_symtab_data_named(a.tab, ico_symtab_name_hash("t.c:kTab"), "t.c:kTab", 1)->unique,
        "two t.c:kTab");
    w[0] = a.lo + a.o_relro + 8;    /* into the first kTab */
    w[1] = a.lo + a.o_relro + 0x30; /* into the second */
    w[2] = a.lo + a.o_relro + 0x40; /* the private table's start */
    want[0] = b.lo + b.o_relro + 8;
    want[1] = b.lo + b.o_relro + 0x30;
    want[2] = b.lo + b.o_relro + 0x40;
    sec = save_words(&a, LAY_ANDROID, w, 3, "the test's words", &n, &st);
    CHECK(sec != NULL && st.left == 0 && st.data == 3 && st.data_masked == 2,
          "the two of one name by their contents, the private table by its name (%u data, %u by "
          "contents, %u left)",
          st.data, st.data_masked, st.left);
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_OK && v.data_masked == 2,
          "B, its pointers its own: the masked contents match: %d %s", k, v.why);
    if (k == ICO_XB_OK) {
        translate(&v, &a, &b, w, 3, &missed);
        for (i = 0; i < 3; i++) {
            CHECK(w[i] == want[i], "relro word %d translated to 0x%llx, wanted 0x%llx", i,
                  (unsigned long long)w[i], (unsigned long long)want[i]);
        }
    }
    ico_xb_verdict_free(&v);
    b.relro_tweak = 1;
    nb_fill(&b);
    k = sec != NULL ? check_words(sec, n, &b, &a, LAY_ANDROID, &v) : ICO_XB_BAD;
    CHECK(k == ICO_XB_DATA &&
              strstr(v.why, "the read-only data t.c:kTab (number 2 of its name) changed") != NULL,
          "the second kTab's constant changed: %d %s", k, v.why);
    ico_xb_verdict_free(&v);
    free(sec);
    /* writable as far as the table knows (no section lines, not in the
       read-only range): a repeated name stays unnamed */
    nb_parse(&a, LAY_LINUX, 0);
    w[0] = a.lo + a.o_relro + 8;
    w[1] = a.lo + a.o_relro + 0x30;
    w[2] = a.lo + a.o_relro + 0x40;
    sec = save_words(&a, LAY_LINUX, w, 3, "the test's words", &n, &st);
    CHECK(sec != NULL && st.left == 2 && st.data_masked == 0 && st.listed == 2 &&
              st.list[0].reason == ICO_XB_WHY_DATA,
          "writable data of a repeated name: unnamed (%u left)", st.left);
    free(sec);
    nb_free(&a);
    nb_free(&b);
}

/* --- arm64 page words ---------------------------------------------------------------- */

#define PG_IMG 0x20000u
#define PG_BSS 0x10000u /* the game bss range's place in A (a page's start) */
#define PG_BSS_N 0x2000u
#define PG_VAR1 0x10u /* in the bss: VAR1 and VAR2 share a page in A */
#define PG_VAR2 0x800u
#define PG_VAR3 0x1020u /* the next page */

typedef struct Pg {
    uint8_t *raw; /* malloc'd; mem is its first page start */
    uint8_t *mem;
    uint64_t lo, hi;
    uint64_t f_a, f_b, bss, buf;
    IcoSymTab *tab;
} Pg;

static uint32_t adrp(uint64_t pc, uint64_t target, unsigned rd)
{
    const int64_t imm = (int64_t)((target & ~(uint64_t)0xFFF) - (pc & ~(uint64_t)0xFFF)) / 4096;
    const uint32_t u = (uint32_t)imm & 0x1FFFFFu;

    return 0x90000000u | (u & 3u) << 29 | (u >> 2) << 5 | rd;
}

/* f_a: ADRP x19 of VAR1, ADRP x20 of VAR2 (one page in A), f_b: ADRP x21 of
   VAR3; the bss at `bss`, the functions at f_a and f_b (moved in B) */
static int pg_make(Pg *g, uint64_t f_a, uint64_t f_b, uint64_t bss, uint64_t buf)
{
    char nm[1024], why[160];
    IcoSymTabEnv e;
    size_t n = 0;
    uint64_t at;

    memset(g, 0, sizeof *g);
    g->raw = (uint8_t *)malloc(PG_IMG + 4096u);
    if (g->raw == NULL) {
        return -1;
    }
    g->mem = g->raw + ((4096u - ((uintptr_t)g->raw & 4095u)) & 4095u);
    memset(g->mem, 0, PG_IMG);
    g->lo = (uint64_t)(uintptr_t)g->mem;
    g->hi = g->lo + PG_IMG;
    g->f_a = f_a;
    g->f_b = f_b;
    g->bss = bss;
    g->buf = buf;
    at = g->lo + f_a;
    put32le(g->mem + f_a, adrp(at, g->lo + bss + PG_VAR1, 19));
    put32le(g->mem + f_a + 4, 0xD503201Fu); /* nop */
    put32le(g->mem + f_a + 8, adrp(at + 8, g->lo + bss + PG_VAR2, 20));
    put32le(g->mem + f_a + 12, 0xD65F03C0u); /* ret */
    /* f_c (f_a + 0x40): an ADRP of VAR1 alone; f_d (f_a + 0x80): of VAR2 alone */
    at = g->lo + f_a + 0x40;
    put32le(g->mem + f_a + 0x40, adrp(at, g->lo + bss + PG_VAR1, 22));
    put32le(g->mem + f_a + 0x44, 0xD65F03C0u);
    at = g->lo + f_a + 0x80;
    put32le(g->mem + f_a + 0x80, adrp(at, g->lo + bss + PG_VAR2, 23));
    put32le(g->mem + f_a + 0x84, 0xD65F03C0u);
    at = g->lo + f_b;
    put32le(g->mem + f_b, adrp(at, g->lo + bss + PG_VAR3, 21));
    put32le(g->mem + f_b + 4, 0xD65F03C0u);
    put32le(g->mem + 0x1800, 0xD65F03C0u);
    put32le(g->mem + 0x1880, 0xD65F03C0u);
#define L3(off, ty, name)                                                                          \
    n += (size_t)snprintf(nm + n, sizeof nm - n, "%016llx %c %s\n", (unsigned long long)(off), ty, \
                          name)
#define L4(off, sz, ty, name)                                                                      \
    n += (size_t)snprintf(nm + n, sizeof nm - n, "%016llx %016llx %c %s\n",                        \
                          (unsigned long long)(off), (unsigned long long)(sz), ty, name)
    L3(f_a, 'T', "f_a");
    L3(f_a + 0x40, 'T', "f_c");
    L3(f_a + 0x80, 'T', "f_d");
    L3(f_b, 'T', "f_b");
    L3(0x1800, 'T', "ico_diag_init");
    L3(0x1880, 'T', "ico_diag_log");
    L3(0x4F00, 'T', "f_last");
    L4(bss + PG_VAR1, 8, 'b', "var1");
    L4(bss + PG_VAR2, 8, 'b', "var2");
    L4(bss + PG_VAR3, 8, 'b', "var3");
    L4(buf, 0x100, 'b', "s_buf");
#undef L3
#undef L4
    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = g->lo + 0x1800;
    e.check = "ico_diag_log";
    e.check_addr = g->lo + 0x1880;
    e.image_lo = g->lo;
    e.image_hi = g->hi;
    e.text_lo = g->lo + 0x1000;
    e.text_hi = g->lo + 0x5000;
    e.game_lo[1] = g->lo + bss;
    e.game_hi[1] = e.game_lo[1] + PG_BSS_N;
    e.arch = ICO_CODE_A64;
    g->tab = ico_symtab_parse(nm, n, &e, why, sizeof why);
    CHECK(g->tab != NULL, "the arm64 table: %s", why);
    return g->tab != NULL ? 0 : -1;
}

static void pg_free(Pg *g)
{
    ico_symtab_free(g->tab);
    free(g->raw);
    memset(g, 0, sizeof *g);
}

static void pg_image(const Pg *g, IcoStateImage *im)
{
    memset(im, 0, sizeof *im);
    im->lo = g->lo;
    im->hi = g->hi;
    im->text_lo = g->lo + 0x1000;
    im->text_hi = g->lo + 0x5000;
}

/* fiber 1's control page (saved registers) and live stack, fiber 2's
   registers, and the arena */
static void *pg_save2(const Pg *a, const uint64_t *regs, int nr, const uint64_t *stack, int ns,
                      const uint64_t *regs2, int nr2, const uint64_t *arena, int na, size_t *n,
                      IcoXbStats *st)
{
    IcoXbProc p;
    IcoXbRefs *r;
    void *sec = NULL;

    memset(&p, 0, sizeof p);
    p.tab = a->tab;
    pg_image(a, &p.image);
    p.game_lo[1] = a->lo + a->bss;
    p.game_hi[1] = p.game_lo[1] + PG_BSS_N;
    p.any_address = 1;
    p.judge_lo = 0x0000007400000000ull;
    *n = 0;
    memset(st, 0, sizeof *st);
    r = ico_xb_begin(&p);
    if (r == NULL) {
        return NULL;
    }
    ico_xb_frames(r, 1);
    ico_xb_where(r, "fiber slot 0's control page");
    ico_xb_scan(r, regs, (size_t)nr * 8u, (uint64_t)(uintptr_t)regs, 1);
    ico_xb_where(r, "fiber slot 0's stack");
    ico_xb_scan(r, stack, (size_t)ns * 8u, (uint64_t)(uintptr_t)stack, 1);
    if (nr2 > 0) {
        ico_xb_frames(r, 2);
        ico_xb_where(r, "fiber slot 1's control page");
        ico_xb_scan(r, regs2, (size_t)nr2 * 8u, (uint64_t)(uintptr_t)regs2, 1);
    }
    ico_xb_frames(r, 0);
    ico_xb_where(r, "the arena");
    ico_xb_scan(r, arena, (size_t)na * 8u, (uint64_t)(uintptr_t)arena, 1);
    CHECK(ico_xb_finish(r, &sec, n, st) == 0, "ico_xb_finish");
    return sec;
}

static void *pg_save(const Pg *a, const uint64_t *regs, int nr, const uint64_t *stack, int ns,
                     const uint64_t *arena, int na, size_t *n, IcoXbStats *st)
{
    return pg_save2(a, regs, nr, stack, ns, NULL, 0, arena, na, n, st);
}

static int pg_check(const void *sec, size_t n, const Pg *b, const Pg *a, IcoXbVerdict *v)
{
    IcoXbNow now;

    memset(&now, 0, sizeof now);
    now.tab = b->tab;
    pg_image(b, &now.image);
    now.game_lo[1] = b->lo + b->bss;
    now.game_hi[1] = now.game_lo[1] + PG_BSS_N;
    now.old_game_lo[1] = a->lo + a->bss;
    now.old_game_hi[1] = now.old_game_lo[1] + PG_BSS_N;
    now.old_image_lo = 0x0000007400000000ull;
    return ico_xb_check(sec, n, &now, v);
}

/* w translated as fiber `fiber` (its slot + 1; 0: outside the fibers)
   holds it */
static uint64_t pg_xlate(const IcoXbVerdict *v, uint64_t w, int fiber)
{
    IcoStateXlate xl;
    uint64_t to = 0;

    memset(&xl, 0, sizeof xl);
    xl.span = v->span;
    xl.n_span = v->n_span;
    xl.point = v->point;
    xl.n_point = v->n_point;
    xl.fiber_point = (const IcoStateSpan *const *)v->fiber_point;
    xl.fiber_n_point = v->fiber_n_point;
    xl.n_fiber = v->n_fiber;
    xl.fiber = fiber;
    return ico_state_xlate(&xl, w, &to) ? to : w;
}

static void test_pages(void)
{
    Pg a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    uint64_t regs[4], stack[2], arena[1];
    void *sec = NULL;
    size_t n = 0;
    int k;

    if (pg_make(&a, 0x1000, 0x1100, PG_BSS, 0x13000) != 0) {
        pg_free(&a);
        return;
    }
    regs[0] = a.lo + a.f_a + 8;         /* a return address in f_a */
    regs[1] = a.lo + a.f_b + 4;         /* one in f_b */
    regs[2] = a.lo + PG_BSS;            /* x19: VAR1's (and VAR2's) page */
    regs[3] = a.lo + PG_BSS + 0x1000;   /* x21: VAR3's page */
    stack[0] = a.lo + PG_BSS + 0x1000;  /* VAR3's page spilled */
    stack[1] = a.lo + PG_BSS + PG_VAR1; /* an ordinary address of VAR1 */
    arena[0] = a.lo + 0x13000;          /* s_buf: page-aligned, ordinary */
    sec = pg_save(&a, regs, 4, stack, 2, arena, 1, &n, &st);
    CHECK(sec != NULL && st.pages == 2 && st.producers == 3 && st.left == 0,
          "two page words of fiber 1, three ADRPs make them (%u, %u; %u unnamed)", st.pages,
          st.producers, st.left);
    /* B: the functions elsewhere, the bss 0x400 into its page (VAR1 and VAR2
       still share one page), s_buf at a page's start too */
    if (pg_make(&b, 0x1200, 0x1300, PG_BSS + 0x400, 0x14000) == 0 && sec != NULL) {
        k = pg_check(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_OK && v.pages == 2, "B loads, two page words by their ADRPs: %d %s", k,
              v.why);
        if (k == ICO_XB_OK) {
            CHECK(pg_xlate(&v, regs[2], 1) == b.lo + PG_BSS,
                  "VAR1's page is B's page of VAR1 (not the range's move, +0x400): 0x%llx",
                  (unsigned long long)(pg_xlate(&v, regs[2], 1) - b.lo));
            CHECK(pg_xlate(&v, regs[3], 1) == b.lo + PG_BSS + 0x1000, "VAR3's page in B");
            CHECK(pg_xlate(&v, regs[2], 0) == b.lo + PG_BSS + 0x400,
                  "outside the fiber the same value is the range's address");
            CHECK(pg_xlate(&v, stack[1], 1) == b.lo + PG_BSS + 0x400 + PG_VAR1,
                  "an ordinary address of VAR1 moves with the range");
            CHECK(pg_xlate(&v, arena[0], 0) == b.lo + 0x14000,
                  "the arena's page-aligned s_buf: an ordinary address, by its symbol");
            CHECK(pg_xlate(&v, regs[0], 1) == b.lo + 0x1200 + 8, "the return address in f_a");
        }
        ico_xb_verdict_free(&v);
    }
    pg_free(&b);
    /* B2: the bss 0x900 into a page: VAR1 stays on the first page, VAR2
       moves to the next: f_a's two ADRPs make two pages */
    if (pg_make(&b, 0x1200, 0x1300, PG_BSS + 0x900, 0x14000) == 0 && sec != NULL) {
        k = pg_check(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_DATA && strstr(v.why, "f_a+0x0 and f_a+0x8") != NULL &&
                  strstr(v.why, "cannot be told") != NULL,
              "the two producers disagree: refused: %d %s", k, v.why);
        ico_xb_verdict_free(&v);
    }
    pg_free(&b);
    free(sec);
    /* one page word on two fibers whose frames' ADRPs aim at VAR1 (f_c) and
       VAR2 (f_d): in B2 the two variables lie on two pages, and each fiber
       gets its own (the owner's rc4 state holds such a word: 0x2589000 on
       fibers 87, 90 and 94) */
    {
        uint64_t r1[2], r2[2];

        r1[0] = a.lo + a.f_a + 0x44; /* a return address in f_c */
        r1[1] = a.lo + PG_BSS;
        r2[0] = a.lo + a.f_a + 0x84; /* in f_d */
        r2[1] = a.lo + PG_BSS;
        sec = pg_save2(&a, r1, 2, NULL, 0, r2, 2, NULL, 0, &n, &st);
        CHECK(sec != NULL && st.pages == 2 && st.producers == 2 && st.left == 0,
              "one page word on two fibers (%u entries, %u producers)", st.pages, st.producers);
        if (pg_make(&b, 0x1200, 0x1300, PG_BSS + 0x900, 0x14000) == 0 && sec != NULL) {
            k = pg_check(sec, n, &b, &a, &v);
            CHECK(k == ICO_XB_OK && v.pages == 2, "B2 loads it: %d %s", k, v.why);
            if (k == ICO_XB_OK) {
                CHECK(pg_xlate(&v, r1[1], 1) == b.lo + PG_BSS, "fiber 1: VAR1's page");
                CHECK(pg_xlate(&v, r2[1], 2) == b.lo + PG_BSS + 0x1000, "fiber 2: VAR2's page");
            }
            ico_xb_verdict_free(&v);
        }
        pg_free(&b);
        free(sec);
    }
    /* a page word also seen outside the frames (the arena holds VAR3's page
       as an address): the two readings differ in B, refused */
    arena[0] = a.lo + PG_BSS + 0x1000;
    sec = pg_save(&a, regs, 4, stack, 1, arena, 1, &n, &st);
    if (pg_make(&b, 0x1200, 0x1300, PG_BSS + 0x400, 0x14000) == 0 && sec != NULL) {
        k = pg_check(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_DATA && strstr(v.why, "both the page an ADRP in f_b+0x0 makes") != NULL,
              "a page word that is an ordinary address too: refused: %d %s", k, v.why);
        ico_xb_verdict_free(&v);
    }
    pg_free(&b);
    free(sec);
    /* a page-aligned word on the fiber that no ADRP there makes (s_buf's
       start), which B's s_buf moves into a page: an ordinary address */
    regs[2] = a.lo + 0x13000;
    sec = pg_save(&a, regs, 3, stack, 0, arena, 0, &n, &st);
    CHECK(sec != NULL && st.pages == 0 && st.producers == 0 && st.left == 0,
          "a page-aligned word no ADRP on its fiber makes: no page word (%u)", st.pages);
    if (pg_make(&b, 0x1200, 0x1300, PG_BSS + 0x400, 0x14040) == 0 && sec != NULL) {
        k = pg_check(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_OK && pg_xlate(&v, regs[2], 1) == b.lo + 0x14040,
              "translated as the ordinary address of s_buf, into a page: %d %s", k, v.why);
        ico_xb_verdict_free(&v);
    }
    pg_free(&b);
    free(sec);
    pg_free(&a);
}

int main(void)
{
    test_sections();
    test_carry(LAY_ANDROID);
    test_carry(LAY_LINUX);
    test_carry(LAY_WINDOWS);
    test_refuse();
    test_hashed();
    test_format();
    test_layout();
    test_masked();
    test_pages();
    return ico_check_summary("state_name");
}
