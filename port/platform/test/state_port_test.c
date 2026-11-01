/*
 * port/platform/test/state_port_test.c
 *
 * Save states across versions (state_xbuild.h): two synthetic "builds" of
 * one program, each an image in memory with its own code, constant texts,
 * data, game data ranges and registered blocks at other addresses, and its
 * own nm symbol table.  Nothing of the game starts.
 *
 *   x86      the length decoder's fields on hand-checked encodings (calls,
 *            RIP-relative loads, absolute immediates and displacements,
 *            VEX, EVEX, the 0F 38/3A maps, multi-byte nops); an unknown
 *            opcode and a cut instruction are not decoded.
 *   hash     the masked code hash, x86-64 and arm64: one function at two
 *            addresses with other call, RIP, ADRP and low-12 offsets hashes
 *            alike; a changed frame size, stack slot, internal branch,
 *            constant or a non-ADRP base's offset does not; trailing padding
 *            is left out; an absolute immediate in the image is masked, one
 *            outside it is not.
 *   symtab   an nm table, a GNU ld map (a static function before the first
 *            global becomes "<object>:<section>", sections bound the
 *            pieces, *fill* and the discarded list are ignored) and lld's
 *            map (sized symbols); names, ordinals, uniqueness, lookups by
 *            address and name; the game data layout hash equal for two
 *            builds with one layout, other when one item moves; a file of
 *            another build and one without the anchor refused.
 *   cross    build A saves the words a state holds (a return address, a
 *            function pointer, pointers into the game's data and bss with
 *            the end address, into a block, into a named variable, at a
 *            constant text, at a game table's start-up value, a dead
 *            frame's word into a function build B lacks); build B checks
 *            and translates every word to its own address (typed and
 *            scanned), leaves the dead frame's word and counts it; the
 *            blocks map by name in another order.
 *   refuse   the first blocker named: a changed function a frame sits in,
 *            a changed function only pointed at (unique name: loads), the
 *            data layout, a block missing, new or resized, a text missing,
 *            a start-up value changed, a word into a variable of a repeated
 *            name, a code address no unit holds, no symbol table, the
 *            bounds' overflow, an image below 4 GB.
 *   format   a format 1 header reads and is another build's; a format 2
 *            header of another build passes the other-build check.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/ico_check.h"
#include "code_hash.h"
#include "state.h"
#include "state_file.h"
#include "state_registry.h"
#include "state_reloc.h"
#include "state_syms.h"
#include "state_xbuild.h"

/* --- the x86 decoder ------------------------------------------------------------ */

typedef struct Vec {
    const char *hex;
    int len;
    int kind, off, size; /* the first field (kind 0: none) */
    int nop;
} Vec;

static int unhex(const char *h, uint8_t *out)
{
    int n = 0;

    while (h[0] != '\0' && h[1] != '\0') {
        unsigned v;

        if (sscanf(h, "%2x", &v) != 1) {
            break;
        }
        out[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}

static void test_x86_decoder(void)
{
    static const Vec v[] = {
        {"e801000000", 5, ICO_X64_BRANCH, 1, 4, 0},
        {"488b0501000000", 7, ICO_X64_RIP, 3, 4, 0},
        {"bf78563412", 5, ICO_X64_IMM, 1, 4, 0},
        {"48b88877665544332211", 10, ICO_X64_IMM, 2, 8, 0},
        {"8b04c578563412", 7, ICO_X64_DISP, 3, 4, 0},
        {"c744240878563412", 8, ICO_X64_IMM, 4, 4, 0},
        {"0f8501000000", 6, ICO_X64_BRANCH, 2, 4, 0},
        {"7401", 2, ICO_X64_BRANCH, 1, 1, 0},
        {"660f1f440000", 6, 0, 0, 0, 1},
        {"c5f8280501000000", 8, ICO_X64_RIP, 4, 4, 0},
        {"0f3a0fc108", 5, 0, 0, 0, 0},
        {"f30f10442410", 6, 0, 0, 0, 0},
        {"4881ec00010000", 7, ICO_X64_IMM, 3, 4, 0},
        {"c3", 1, 0, 0, 0, 0},
        {"cc", 1, 0, 0, 0, 1},
        {"62f17c48280501000000", 10, ICO_X64_RIP, 6, 4, 0},
        {"f6c101", 3, 0, 0, 0, 0},
        {"f7c178563412", 6, ICO_X64_IMM, 2, 4, 0},
        {"a12211000000000000", 9, ICO_X64_IMM, 1, 8, 0},
        {"ff1501000000", 6, ICO_X64_RIP, 2, 4, 0},
        {"4883ec28", 4, 0, 0, 0, 0},
        {"55", 1, 0, 0, 0, 0},
        {"4889e5", 3, 0, 0, 0, 0},
        {"c9", 1, 0, 0, 0, 0},
        {"31c0", 2, 0, 0, 0, 0},
        {"90", 1, 0, 0, 0, 1},
    };
    uint8_t b[32];
    size_t i;

    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        IcoX64Insn ins;
        const int n = unhex(v[i].hex, b);

        ico_code_x64_decode(b, (size_t)n, &ins);
        CHECK(ins.len == v[i].len, "x86 %s: length %d, want %d", v[i].hex, ins.len, v[i].len);
        CHECK(ins.nop == v[i].nop, "x86 %s: nop %d", v[i].hex, ins.nop);
        if (v[i].kind != 0) {
            CHECK(ins.n >= 1 && ins.f[0].kind == v[i].kind && ins.f[0].off == v[i].off &&
                      ins.f[0].size == v[i].size,
                  "x86 %s: field %d kind %d off %d size %d", v[i].hex, ins.n,
                  ins.n ? ins.f[0].kind : -1, ins.n ? ins.f[0].off : -1,
                  ins.n ? ins.f[0].size : -1);
        } else {
            CHECK(ins.n == 0, "x86 %s: %d fields, want none", v[i].hex, ins.n);
        }
    }
    {
        IcoX64Insn ins;

        b[0] = 0x06; /* push es: not in 64-bit code */
        ico_code_x64_decode(b, 1, &ins);
        CHECK(ins.len == 0, "x86 06 is not decoded");
        (void)unhex("e8000000", b);
        ico_code_x64_decode(b, 3, &ins);
        CHECK(ins.len == 0, "x86 a call cut after 3 bytes is not decoded");
    }
}

/* --- the masked hash ---------------------------------------------------------------- */

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* f: push rbp; mov rbp,rsp; sub rsp,frame; mov rax,[rip+d]; call rel;
   je +2 (je); mov edi,imm; leave; ret; pad int3 */
static size_t x86_fn(uint8_t *p, int frame, uint32_t rip, uint32_t call, uint8_t je, uint32_t imm,
                     int pad)
{
    size_t n = 0;
    int i;

    p[n++] = 0x55;
    p[n++] = 0x48;
    p[n++] = 0x89;
    p[n++] = 0xE5;
    p[n++] = 0x48;
    p[n++] = 0x83;
    p[n++] = 0xEC;
    p[n++] = (uint8_t)frame;
    p[n++] = 0x48;
    p[n++] = 0x8B;
    p[n++] = 0x05;
    put32le(p + n, rip);
    n += 4;
    p[n++] = 0xE8;
    put32le(p + n, call);
    n += 4;
    p[n++] = 0x74;
    p[n++] = je;
    p[n++] = 0xBF;
    put32le(p + n, imm);
    n += 4;
    p[n++] = 0x90;
    p[n++] = 0x90;
    p[n++] = 0xC9;
    p[n++] = 0xC3;
    for (i = 0; i < pad; i++) {
        p[n++] = 0xCC;
    }
    return n;
}

static void test_hash_x86(void)
{
    uint8_t a[64], b[64];
    const uint64_t lo = 0x400000, hi = 0x500000;
    size_t na, nb;
    uint64_t ha;

    na = x86_fn(a, 0x28, 0x1000, 0x5000, 2, 0x401234, 3);
    ha = ico_code_hash(ICO_CODE_X64, a, na, 0x401000, lo, hi);
    /* elsewhere, other displacements, another absolute address, more padding */
    nb = x86_fn(b, 0x28, 0x2340, 0xFFFF0000u, 2, 0x488888, 9);
    CHECK(ico_code_hash(ICO_CODE_X64, b, nb, 0x402000, lo, hi) == ha,
          "x86: the same function at another address hashes alike");
    nb = x86_fn(b, 0x38, 0x1000, 0x5000, 2, 0x401234, 3);
    CHECK(ico_code_hash(ICO_CODE_X64, b, nb, 0x401000, lo, hi) != ha, "x86: another frame size");
    nb = x86_fn(b, 0x28, 0x1000, 0x5000, 1, 0x401234, 3);
    CHECK(ico_code_hash(ICO_CODE_X64, b, nb, 0x401000, lo, hi) != ha,
          "x86: an internal branch elsewhere");
    na = x86_fn(a, 0x28, 0x1000, 0x5000, 2, 0x12345678, 0);
    nb = x86_fn(b, 0x28, 0x1000, 0x5000, 2, 0x12345679, 0);
    CHECK(ico_code_hash(ICO_CODE_X64, a, na, 0x401000, lo, hi) !=
              ico_code_hash(ICO_CODE_X64, b, nb, 0x401000, lo, hi),
          "x86: a constant outside the image is hashed");
    nb = x86_fn(b, 0x28, 0x1000, 0x5000, 2, 0x12345678, 0);
    CHECK(ico_code_hash(ICO_CODE_X64, b, nb, 0x401000, lo, hi) ==
              ico_code_hash(ICO_CODE_X64, a, na, 0x401000, lo, hi),
          "x86: the same constant outside the image hashes alike");
    b[0] = 0x53; /* push rbx */
    CHECK(ico_code_hash(ICO_CODE_X64, b, nb, 0x401000, lo, hi) !=
              ico_code_hash(ICO_CODE_X64, a, na, 0x401000, lo, hi),
          "x86: another register");
}

/* stp; mov x29,sp; adrp x8,page; ldr w0,[x8,#lo]; bl; mov x8,x0;
   ldr w1,[x8,#field]; ldr x0,[sp,#slot]; b.ne +8; ldp; ret; pads */
static size_t a64_fn(uint32_t *w, uint32_t page, uint32_t lo12, uint32_t bl, uint32_t field,
                     uint32_t slot, uint32_t bne, int pads)
{
    size_t n = 0;
    int i;

    w[n++] = 0xA9BF7BFDu;
    w[n++] = 0x910003FDu;
    w[n++] = 0x90000008u | (page & 3u) << 29 | ((page >> 2) & 0x7FFFFu) << 5;
    w[n++] = 0xB9400000u | (lo12 & 0xFFFu) << 10 | 8u << 5;
    w[n++] = 0x94000000u | (bl & 0x3FFFFFFu);
    w[n++] = 0xAA0003E8u;
    w[n++] = 0xB9400000u | (field & 0xFFFu) << 10 | 8u << 5 | 1u;
    w[n++] = 0xF9400000u | (slot & 0xFFFu) << 10 | 31u << 5;
    w[n++] = 0x54000001u | (bne & 0x7FFFFu) << 5;
    w[n++] = 0xA8C17BFDu;
    w[n++] = 0xD65F03C0u;
    for (i = 0; i < pads; i++) {
        w[n++] = (i & 1) ? 0u : 0xD503201Fu;
    }
    return n * 4;
}

static void test_hash_a64(void)
{
    uint32_t a[32], b[32];
    size_t na, nb;
    uint64_t ha;

    na = a64_fn(a, 5, 9, 0x40, 9, 1, 2, 1);
    ha = ico_code_hash(ICO_CODE_A64, a, na, 0x10000, 0, 0);
    nb = a64_fn(b, 77, 0x3F0, 0x3FFF00, 9, 1, 2, 4);
    CHECK(ico_code_hash(ICO_CODE_A64, b, nb, 0x90000, 0, 0) == ha,
          "a64: the same function at another address hashes alike (ADRP, low 12, BL, padding)");
    nb = a64_fn(b, 5, 9, 0x40, 10, 1, 2, 1);
    CHECK(ico_code_hash(ICO_CODE_A64, b, nb, 0x10000, 0, 0) != ha,
          "a64: a field offset off a register no ADRP set");
    nb = a64_fn(b, 5, 9, 0x40, 9, 2, 2, 1);
    CHECK(ico_code_hash(ICO_CODE_A64, b, nb, 0x10000, 0, 0) != ha, "a64: another stack slot");
    nb = a64_fn(b, 5, 9, 0x40, 9, 1, 1, 1);
    CHECK(ico_code_hash(ICO_CODE_A64, b, nb, 0x10000, 0, 0) != ha,
          "a64: an internal branch elsewhere");
    CHECK(ico_code_a64_writes(0xAA0003E8u) == 1u << 8, "a64: mov x8, x0 writes x8");
    CHECK(ico_code_a64_writes(0xB94000E0u) == 1u << 0, "a64: a load writes its target");
    CHECK(ico_code_a64_writes(0xB90000E0u) == 0u, "a64: a store writes nothing");
    CHECK((ico_code_a64_writes(0x94000040u) & (1u << 30)) != 0, "a64: bl writes x30");
    CHECK(ico_code_a64_writes(0xD53BD048u) == 1u << 8, "a64: mrs writes its target");
}

/* --- the synthetic builds --------------------------------------------------------------- */

#define IMG_BYTES (256u * 1024u)
#define TEXT_LO 0x1000u
#define TEXT_HI 0x8000u
#define RO_LO 0x8000u
#define RO_HI 0xA000u
#define GDATA_BYTES 0x400u
#define GBSS_BYTES 0x800u
#define N_BLOCKS 3

typedef struct Build {
    uint8_t *mem;
    uint64_t lo, hi;
    uint64_t o_anchor, o_check, o_walk, o_cb, o_gone;
    uint64_t o_wait, o_ico;
    uint64_t o_tab, o_said;
    uint64_t o_gdata, o_gbss;
    uint64_t o_blk[N_BLOCKS];
    const char *blk_name[N_BLOCKS];
    uint64_t blk_size[N_BLOCKS];
    unsigned blk_flags[N_BLOCKS];
    int n_blocks;
    uint64_t stage_off;
    int frame, cb_variant, has_gone, has_wait;
    uint64_t snap[GDATA_BYTES / 8];
    IcoSymTab *tab;
} Build;

static const char *const kBlkName[N_BLOCKS] = {"kernel.intc", "rand", "spu2"};
static const uint64_t kBlkSize[N_BLOCKS] = {0x100, 8, 0x40};
static const unsigned kBlkFlags[N_BLOCKS] = {ICO_STATE_SCAN, 0, ICO_STATE_AUDIO | ICO_STATE_SCAN};

/* f_walk's return address after its call */
#define WALK_RET 20u

static void write_walk(Build *b)
{
    uint8_t *p = b->mem + b->o_walk;
    const uint64_t at = b->lo + b->o_walk;
    const uint64_t rip_end = at + 15, call_end = at + 20;
    const uint32_t rip = (uint32_t)((b->lo + b->o_tab) - rip_end);
    const uint32_t call = (uint32_t)((b->lo + b->o_cb) - call_end);
    size_t n = 0;

    p[n++] = 0x55;
    p[n++] = 0x48;
    p[n++] = 0x89;
    p[n++] = 0xE5;
    p[n++] = 0x48;
    p[n++] = 0x83;
    p[n++] = 0xEC;
    p[n++] = (uint8_t)b->frame;
    p[n++] = 0x48;
    p[n++] = 0x8B;
    p[n++] = 0x05;
    put32le(p + n, rip);
    n += 4;
    p[n++] = 0xE8;
    put32le(p + n, call);
    n += 4;
    p[n++] = 0xC9;
    p[n++] = 0xC3;
}

static void make_build(Build *b, int which)
{
    int i;

    memset(b, 0, sizeof *b);
    b->mem = (uint8_t *)calloc(1, IMG_BYTES);
    b->lo = (uint64_t)(uintptr_t)b->mem;
    b->hi = b->lo + IMG_BYTES;
    memset(b->mem + TEXT_LO, 0xCC, TEXT_HI - TEXT_LO);
    b->frame = 0x28;
    b->has_gone = which == 0;
    b->has_wait = 1;
    b->stage_off = 0x10;
    if (which == 0) {
        b->o_anchor = 0x1020; /* [0x1000, 0x1020) of the code: no unit's */
        b->o_check = 0x1100;
        b->o_walk = 0x1200;
        b->o_cb = 0x1400;
        b->o_gone = 0x1600;
        b->o_wait = 0x8010;
        b->o_ico = 0x8030;
        b->o_tab = 0xA000;
        b->o_said = 0xA100;
        b->o_gdata = 0x10000;
        b->o_gbss = 0x10400;
        for (i = 0; i < N_BLOCKS; i++) {
            b->blk_name[i] = kBlkName[i];
            b->blk_size[i] = kBlkSize[i];
            b->blk_flags[i] = kBlkFlags[i];
            b->o_blk[i] = 0xC000 + 0x100u * (uint64_t)i;
        }
    } else {
        /* another layout of the same program, the registry in another order */
        static const int order[N_BLOCKS] = {2, 0, 1};

        b->o_anchor = 0x1040;
        b->o_check = 0x1180;
        b->o_walk = 0x2300;
        b->o_cb = 0x1800;
        b->o_wait = 0x8100;
        b->o_ico = 0x8200;
        b->o_tab = 0xA400;
        b->o_said = 0xA600;
        b->o_gdata = 0x11000;
        b->o_gbss = 0x11800;
        for (i = 0; i < N_BLOCKS; i++) {
            b->blk_name[i] = kBlkName[order[i]];
            b->blk_size[i] = kBlkSize[order[i]];
            b->blk_flags[i] = kBlkFlags[order[i]];
            b->o_blk[i] = 0xD000 + 0x200u * (uint64_t)i;
        }
    }
    b->n_blocks = N_BLOCKS;
}

/* the code, the texts and the start-up values from the build's fields */
static void fill_build(Build *b, int which)
{
    uint8_t *m = b->mem;

    m[b->o_anchor] = 0xC3;
    m[b->o_check] = 0xC3;
    write_walk(b);
    m[b->o_cb] = 0x31;
    m[b->o_cb + 1] = b->cb_variant ? 0xC9 : 0xC0;
    m[b->o_cb + 2] = 0xC3;
    if (b->has_gone) {
        m[b->o_gone] = 0x90;
        m[b->o_gone + 1] = 0xC3;
    }
    memset(m + RO_LO, 'x', 0x10);
    if (b->has_wait) {
        memcpy(m + b->o_wait, "WaitSema", 9);
    }
    memcpy(m + b->o_ico, "Ico", 4);
    /* tableA[0] (gdata + 0x40) holds a pointer to "Ico" at start-up */
    memset(b->snap, 0, sizeof b->snap);
    b->snap[0x40 / 8] = b->lo + b->o_ico;
    (void)which;
}

static size_t nm_text(const Build *b, char *out, size_t size)
{
    size_t n = 0;

#define LINE3(off, ty, name)                                                                       \
    n += (size_t)snprintf(out + n, size - n, "%016llx %c %s\n", (unsigned long long)(off), ty, name)
#define LINE4(off, sz, ty, name)                                                                   \
    n += (size_t)snprintf(out + n, size - n, "%016llx %016llx %c %s\n", (unsigned long long)(off), \
                          (unsigned long long)(sz), ty, name)
    LINE3(b->o_anchor, 'T', "ico_diag_init");
    LINE3(b->o_check, 'T', "ico_diag_log");
    LINE3(b->o_walk, 'T', "f_walk");
    LINE3(b->o_cb, 't', "f_cb");
    if (b->has_gone) {
        LINE3(b->o_gone, 't', "f_gone");
    }
    LINE3(TEXT_HI - 0x10, 'T', "f_last");
    LINE4(b->o_tab, 0x40, 'D', "port_tab");
    LINE4(b->o_said, 8, 'b', "said.0");
    LINE4(b->o_said + 8, 8, 'b', "said.0");
    LINE4(b->o_gdata, 0x40, 'D', "systemStatus");
    LINE4(b->o_gdata + 0x40, 0x80, 'D', "tableA");
    LINE4(b->o_gbss + b->stage_off, 4, 'B', "stage_no");
    LINE4(b->o_gbss + 0x100, 0x20, 'b', "s_local");
#undef LINE3
#undef LINE4
    return n;
}

static void env_of(const Build *b, IcoSymTabEnv *e)
{
    memset(e, 0, sizeof *e);
    e->anchor = "ico_diag_init";
    e->anchor_addr = b->lo + b->o_anchor;
    e->check = "ico_diag_log";
    e->check_addr = b->lo + b->o_check;
    e->image_lo = b->lo;
    e->image_hi = b->hi;
    e->text_lo = b->lo + TEXT_LO;
    e->text_hi = b->lo + TEXT_HI;
    e->game_lo[0] = b->lo + b->o_gdata;
    e->game_hi[0] = e->game_lo[0] + GDATA_BYTES;
    e->game_lo[1] = b->lo + b->o_gbss;
    e->game_hi[1] = e->game_lo[1] + GBSS_BYTES;
    e->arch = ICO_CODE_X64;
}

static void parse_build(Build *b)
{
    char text[4096], why[160];
    IcoSymTabEnv e;
    const size_t n = nm_text(b, text, sizeof text);

    env_of(b, &e);
    b->tab = ico_symtab_parse(text, n, &e, why, sizeof why);
    CHECK(b->tab != NULL, "the synthetic build's table: %s", why);
}

static void image_of(const Build *b, IcoStateImage *im)
{
    memset(im, 0, sizeof *im);
    im->lo = b->lo;
    im->hi = b->hi;
    im->text_lo = b->lo + TEXT_LO;
    im->text_hi = b->lo + TEXT_HI;
    im->n_ro = 1;
    im->ro_lo[0] = b->lo + RO_LO;
    im->ro_hi[0] = b->lo + RO_HI;
}

static void proc_of(const Build *b, IcoXbProc *p)
{
    memset(p, 0, sizeof *p);
    p->tab = b->tab;
    image_of(b, &p->image);
    p->game_lo[0] = b->lo + b->o_gdata;
    p->game_hi[0] = p->game_lo[0] + GDATA_BYTES;
    p->game_lo[1] = b->lo + b->o_gbss;
    p->game_hi[1] = p->game_lo[1] + GBSS_BYTES;
    p->snap = b->snap;
    p->snap_words = GDATA_BYTES / 8;
    p->any_address = 1;
}

static uint64_t s_addr[N_BLOCKS];

static void now_of(const Build *b, const Build *old, IcoXbNow *n)
{
    int i;

    memset(n, 0, sizeof *n);
    n->tab = b->tab;
    image_of(b, &n->image);
    n->game_lo[0] = b->lo + b->o_gdata;
    n->game_hi[0] = n->game_lo[0] + GDATA_BYTES;
    n->game_lo[1] = b->lo + b->o_gbss;
    n->game_hi[1] = n->game_lo[1] + GBSS_BYTES;
    n->snap = b->snap;
    n->snap_words = GDATA_BYTES / 8;
    n->n_blocks = b->n_blocks;
    n->block_name = b->blk_name;
    n->block_size = b->blk_size;
    n->block_flags = b->blk_flags;
    for (i = 0; i < b->n_blocks; i++) {
        s_addr[i] = b->lo + b->o_blk[i];
    }
    n->block_addr = s_addr;
    n->old_game_lo[0] = old->lo + old->o_gdata;
    n->old_game_hi[0] = n->old_game_lo[0] + GDATA_BYTES;
    n->old_game_lo[1] = old->lo + old->o_gbss;
    n->old_game_hi[1] = n->old_game_lo[1] + GBSS_BYTES;
}

static void free_build(Build *b)
{
    ico_symtab_free(b->tab);
    free(b->mem);
    b->tab = NULL;
    b->mem = NULL;
}

/* the words a state of build A holds, in the order the checks name them */
enum { W_RET, W_CB, W_GDATA, W_GBSS_END, W_BLOCK, W_TAB, W_WAIT, W_ICO, W_PLAIN, W_COUNT };

static void state_words(const Build *a, uint64_t w[W_COUNT])
{
    w[W_RET] = a->lo + a->o_walk + WALK_RET;
    w[W_CB] = a->lo + a->o_cb;
    w[W_GDATA] = a->lo + a->o_gdata + 0x48;
    w[W_GBSS_END] = a->lo + a->o_gbss + GBSS_BYTES;
    w[W_BLOCK] = a->lo + a->o_blk[1] + 4; /* inside "rand" */
    w[W_TAB] = a->lo + a->o_tab + 8;
    w[W_WAIT] = a->lo + a->o_wait;
    w[W_ICO] = a->lo + a->o_ico;
    w[W_PLAIN] = 12345;
}

/* A saves (its blocks in its order, offsets packed as state_host.c packs
   them), with `extra` words strict and `soft` words dead; the section */
static void *save(const Build *a, const uint64_t *extra, int n_extra, const uint64_t *soft,
                  int n_soft, size_t *n, IcoXbStats *st, int tab)
{
    IcoXbProc p;
    IcoXbRefs *r;
    uint64_t w[W_COUNT];
    uint64_t at[2] = {0, 0};
    void *sec = NULL;
    int i;

    proc_of(a, &p);
    if (!tab) {
        p.tab = NULL;
    }
    r = ico_xb_begin(&p);
    CHECK(r != NULL, "ico_xb_begin");
    if (r == NULL) {
        return NULL;
    }
    state_words(a, w);
    ico_xb_scan(r, w, sizeof w, (uint64_t)(uintptr_t)w, 1);
    if (n_extra > 0) {
        ico_xb_scan(r, extra, (size_t)n_extra * 8, (uint64_t)(uintptr_t)extra, 1);
    }
    if (n_soft > 0) {
        ico_xb_scan(r, soft, (size_t)n_soft * 8, (uint64_t)(uintptr_t)soft, 0);
    }
    for (i = 0; i < a->n_blocks; i++) {
        const int audio = (a->blk_flags[i] & ICO_STATE_AUDIO) != 0;

        ico_xb_block(r, a->blk_name[i], a->blk_size[i], a->blk_flags[i], a->lo + a->o_blk[i],
                     at[audio]);
        at[audio] += a->blk_size[i];
    }
    CHECK(ico_xb_finish(r, &sec, n, st) == 0, "ico_xb_finish");
    return sec;
}

static int check_into(const void *sec, size_t n, const Build *b, const Build *a, IcoXbVerdict *v)
{
    IcoXbNow now;

    now_of(b, a, &now);
    return ico_xb_check(sec, n, &now, v);
}

static void test_symtab(void)
{
    static const char gnu[] =
        "Discarded input sections\n"
        "\n"
        " .text          0x0000000000000000       0x40 libico_game.a(gone.c.o)\n"
        "\n"
        "Linker script and memory map\n"
        "\n"
        ".text           0x0000000000401000     0x2000\n"
        " *(.text)\n"
        " .text          0x0000000000401000      0x100 libico_game.a(main.c.o)\n"
        "                0x0000000000401000                ico_diag_init\n"
        "                0x0000000000401080                ico_diag_log\n"
        " .text          0x0000000000401100      0x200 port/x/CMakeFiles/w.dir/walk.c.o\n"
        "                0x0000000000401180                actWalk\n"
        " .text.unlikely\n"
        "                0x0000000000401300       0x40 libico_game.a(walk.c.o)\n"
        " *fill*         0x0000000000401340       0x10 \n"
        " .text          0x0000000000401350       0x50 libico_game.a(init.c.o)\n"
        "                0x0000000000401350                init\n"
        " .text          0x00000000004013a0       0x50 libico_game.a(init2.c.o)\n"
        "                0x00000000004013a0                init\n"
        ".rodata         0x0000000000403000      0x100\n"
        " .rodata        0x0000000000403000      0x100 libico_game.a(main.c.o)\n"
        "                0x0000000000403040                kTable\n"
        "ico_gdata       0x0000000000405000      0x100\n"
        " ico_gdata      0x0000000000405000       0x80 libico_game.a(main.c.o)\n"
        "                0x0000000000405010                systemStatus\n"
        " ico_gdata      0x0000000000405080       0x80 libico_game.a(walk.c.o)\n";
    static const char lld[] =
        "             VMA              LMA     Size Align Out     In      Symbol\n"
        "          401000           401000     2000    16 .text\n"
        "          401000           401000      100    16         main.c.o:(.text)\n"
        "          401000           401000       80     1                 ico_diag_init\n"
        "          401080           401080       80     1                 ico_diag_log\n"
        "          401100           401100      200    16         walk.c.o:(.text)\n"
        "          401100           401100       40     1                 walkStatic\n"
        "          401180           401180      100     1                 actWalk\n";
    IcoSymTabEnv e;
    IcoSymTab *t;
    const IcoSymUnit *u;
    char why[160];
    uint64_t layout = 0;

    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = 0x401000;
    e.check = "ico_diag_log";
    e.check_addr = 0x401080;
    e.image_lo = 0x400000;
    e.image_hi = 0x500000;
    e.text_lo = 0x401000;
    e.text_hi = 0x403000;
    e.game_lo[0] = 0x405000;
    e.game_hi[0] = 0x405100;
    e.arch = ICO_CODE_RAW;

    t = ico_symtab_parse(gnu, sizeof gnu - 1, &e, why, sizeof why);
    CHECK(t != NULL, "the GNU map parses: %s", why);
    if (t != NULL) {
        u = ico_symtab_unit_at(t, 0x401150);
        CHECK(u != NULL && strcmp(ico_symtab_name(t, u->name), "walk.c.o:.text") == 0 &&
                  u->start == 0x401100 && u->size == 0x80,
              "GNU map: a static function before the first global is its section's piece");
        u = ico_symtab_unit_at(t, 0x401200);
        CHECK(u != NULL && strcmp(ico_symtab_name(t, u->name), "actWalk") == 0 &&
                  u->size == 0x180 && u->unique,
              "GNU map: a global up to its section's end");
        u = ico_symtab_unit_at(t, 0x401320);
        CHECK(u != NULL && strcmp(ico_symtab_name(t, u->name), "walk.c.o:.text.unlikely") == 0 &&
                  u->size == 0x40,
              "GNU map: a section named on its own line");
        CHECK(ico_symtab_unit_at(t, 0x401345) == NULL, "GNU map: *fill* belongs to nobody");
        u = ico_symtab_unit_named(t, ico_symtab_name_hash("init"), "init", 1);
        CHECK(u != NULL && u->start == 0x4013a0 && !u->unique,
              "GNU map: the second unit of a repeated name is ordinal 1");
        CHECK(ico_symtab_unit_named(t, ico_symtab_name_hash("init"), "init", 2) == NULL,
              "GNU map: no third init");
        CHECK(ico_symtab_unit_at(t, 0x10) == NULL, "GNU map: the discarded list is ignored");
        CHECK(ico_symtab_data_at(t, 0x403048) != NULL &&
                  strcmp(ico_symtab_name(t, ico_symtab_data_at(t, 0x403048)->name), "kTable") == 0,
              "GNU map: read-only data named");
        CHECK(t->n_layout == 3, "GNU map: the game data's three items (%zu)", t->n_layout);
        layout = t->layout_hash;
        ico_symtab_free(t);
        /* the same map one page higher, placed by its anchor: one layout */
        e.anchor_addr = 0x402000;
        e.check_addr = 0x402080;
        e.text_lo = 0x402000;
        e.text_hi = 0x404000;
        e.game_lo[0] = 0x406000;
        e.game_hi[0] = 0x406100;
        t = ico_symtab_parse(gnu, sizeof gnu - 1, &e, why, sizeof why);
        CHECK(t != NULL && t->layout_hash == layout, "GNU map: the layout hash moves with it");
        ico_symtab_free(t);
        e.anchor_addr = 0x401000;
        e.check_addr = 0x401080;
        e.text_lo = 0x401000;
        e.text_hi = 0x403000;
        e.game_lo[0] = 0x405000;
        e.game_hi[0] = 0x405100;
    }
    {
        char moved[sizeof gnu];
        char *p;

        memcpy(moved, gnu, sizeof gnu);
        p = strstr(moved, "0000000000405010");
        CHECK(p != NULL);
        if (p != NULL) {
            p[15] = '8'; /* systemStatus at 0x405018 */
        }
        t = ico_symtab_parse(moved, sizeof moved - 1, &e, why, sizeof why);
        CHECK(t != NULL && t->layout_hash != layout,
              "GNU map: a moved game global: another layout");
        ico_symtab_free(t);
    }
    e.check_addr = 0x401088;
    t = ico_symtab_parse(gnu, sizeof gnu - 1, &e, why, sizeof why);
    CHECK(t == NULL && strstr(why, "another build") != NULL, "another build's map: %s", why);
    e.check_addr = 0x401080;
    e.anchor = "not_there";
    t = ico_symtab_parse(gnu, sizeof gnu - 1, &e, why, sizeof why);
    CHECK(t == NULL && strstr(why, "anchor") != NULL, "no anchor: %s", why);
    e.anchor = "_ico_diag_init"; /* a leading '_' on either side */
    t = ico_symtab_parse(lld, sizeof lld - 1, &e, why, sizeof why);
    CHECK(t != NULL, "lld's map parses: %s", why);
    if (t != NULL) {
        u = ico_symtab_unit_at(t, 0x401120);
        CHECK(u != NULL && strcmp(ico_symtab_name(t, u->name), "walkStatic") == 0 &&
                  u->size == 0x40,
              "lld map: a sized local");
        CHECK(ico_symtab_unit_at(t, 0x401150) == NULL, "lld map: past a sized symbol, nobody");
        u = ico_symtab_unit_at(t, 0x401200);
        CHECK(u != NULL && u->size == 0x100, "lld map: actWalk's own size");
        ico_symtab_free(t);
    }
}

static void test_cross(void)
{
    Build a, b;
    IcoXbStats st;
    IcoXbVerdict v;
    IcoStateReloc rel;
    IcoStateXlate xl;
    uint64_t w[W_COUNT], soft[2];
    uint64_t missed = 0, first = 0;
    void *sec;
    size_t n = 0;
    int k;

    make_build(&a, 0);
    make_build(&b, 1);
    fill_build(&a, 0);
    fill_build(&b, 1);
    parse_build(&a);
    parse_build(&b);
    if (a.tab == NULL || b.tab == NULL) {
        free_build(&a);
        free_build(&b);
        return;
    }
    soft[0] = a.lo + a.o_gone + 1; /* a dead frame's word into a function B has not */
    soft[1] = a.lo + a.o_walk + 3;
    sec = save(&a, NULL, 0, soft, 2, &n, &st, 1);
    CHECK(sec != NULL && (st.flags & ICO_XB_TABLE) != 0 &&
              (st.flags & (ICO_XB_OVERFLOW | ICO_XB_UNNAMED | ICO_XB_LOW_IMAGE)) == 0,
          "the save's record: flags %#x", st.flags);
    CHECK(st.units == 3 && st.positions == 1 && st.data == 1 && st.points == 2 &&
              st.blocks == N_BLOCKS,
          "the record: %u units (%u positions), %u data, %u points, %u blocks", st.units,
          st.positions, st.data, st.points, st.blocks);
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_OK, "B loads A's state: %s", v.why);
    if (k == ICO_XB_OK) {
        /* the blocks by name: B's order is spu2, kernel.intc, rand */
        CHECK(v.block_off[0] == 0 && v.block_off[1] == 0 && v.block_off[2] == 0x100,
              "the blocks' saved offsets in B's order: %llu %llu %llu",
              (unsigned long long)v.block_off[0], (unsigned long long)v.block_off[1],
              (unsigned long long)v.block_off[2]);
        CHECK(v.soft_left == 1, "one dead-frame unit is not here (%u)", v.soft_left);
        memset(&rel, 0, sizeof rel);
        rel.range[ICO_RELOC_MODULE].lo = a.lo;
        rel.range[ICO_RELOC_MODULE].hi = a.hi;
        rel.range[ICO_RELOC_MODULE].delta = (int64_t)(b.lo - a.lo);
        memset(&xl, 0, sizeof xl);
        xl.span = v.span;
        xl.n_span = v.n_span;
        xl.point = v.point;
        xl.n_point = v.n_point;
        xl.missed = &missed;
        xl.first_missed = &first;
        rel.xlate = &xl;
        state_words(&a, w);
        CHECK(ico_state_reloc_scan(&rel, w, sizeof w, NULL) == W_COUNT - 1,
              "every program word moved, the number not");
        CHECK(w[W_RET] == b.lo + b.o_walk + WALK_RET, "the return address into B's f_walk");
        CHECK(w[W_CB] == b.lo + b.o_cb, "the function pointer to B's f_cb");
        CHECK(w[W_GDATA] == b.lo + b.o_gdata + 0x48, "the game data pointer");
        CHECK(w[W_GBSS_END] == b.lo + b.o_gbss + GBSS_BYTES, "the game bss's end address");
        CHECK(w[W_BLOCK] == b.lo + b.o_blk[2] + 4, "the pointer into the block rand");
        CHECK(w[W_TAB] == b.lo + b.o_tab + 8, "the pointer into port_tab");
        CHECK(w[W_WAIT] == b.lo + b.o_wait, "the text found again in B");
        CHECK(w[W_ICO] == b.lo + b.o_ico, "the start-up value of the game's table");
        CHECK(w[W_PLAIN] == 12345, "a number stays");
        /* typed: a return address translates, a word nothing names fails */
        {
            uint64_t t = a.lo + a.o_walk + WALK_RET;

            CHECK(ico_state_reloc_typed(&rel, ICO_RELOC_MODULE, &t) == 0 &&
                      t == b.lo + b.o_walk + WALK_RET,
                  "typed: the return address");
            t = a.lo + 0x7000;
            CHECK(ico_state_reloc_typed(&rel, ICO_RELOC_MODULE, &t) == -1 && t == a.lo + 0x7000,
                  "typed: no translation refuses");
        }
        /* the dead frame's word into f_gone is left and counted */
        missed = first = 0;
        soft[0] = a.lo + a.o_gone + 1;
        CHECK(ico_state_reloc_word(&rel, &soft[0]) == 0 && soft[0] == a.lo + a.o_gone + 1 &&
                  missed == 1 && first == soft[0],
              "a dead frame's word without a translation is left");
        soft[1] = a.lo + a.o_walk + 3;
        CHECK(ico_state_reloc_word(&rel, &soft[1]) == 1 && soft[1] == b.lo + b.o_walk + 3,
              "a dead frame's word into an unchanged function moves");
    }
    ico_xb_verdict_free(&v);

    /* a changed function a frame sits in */
    free_build(&b);
    make_build(&b, 1);
    b.frame = 0x38;
    fill_build(&b, 1);
    parse_build(&b);
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_CODE && strstr(v.why, "function f_walk changed") != NULL,
          "a changed f_walk: %d %s", k, v.why);
    ico_xb_verdict_free(&v);

    /* a changed function only pointed at, of a unique name: loads */
    free_build(&b);
    make_build(&b, 1);
    b.cb_variant = 1;
    fill_build(&b, 1);
    parse_build(&b);
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_OK, "a changed f_cb only pointed at loads: %s", v.why);
    ico_xb_verdict_free(&v);

    /* the game data layout */
    free_build(&b);
    make_build(&b, 1);
    b.stage_off = 0x18;
    fill_build(&b, 1);
    parse_build(&b);
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_DATA && strstr(v.why, "data layout") != NULL, "a moved game global: %s",
          v.why);
    ico_xb_verdict_free(&v);

    /* the blocks: one missing, one new, one resized */
    free_build(&b);
    make_build(&b, 1);
    fill_build(&b, 1);
    parse_build(&b);
    b.n_blocks = 2; /* spu2, kernel.intc: no rand */
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_BLOCKS && strstr(v.why, "rand is missing here") != NULL,
          "a block missing: %s", v.why);
    ico_xb_verdict_free(&v);
    b.n_blocks = 3;
    b.blk_name[2] = "door";
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_BLOCKS && strstr(v.why, "door exists only here") != NULL, "a new block: %s",
          v.why);
    ico_xb_verdict_free(&v);
    b.blk_name[2] = "rand";
    b.blk_size[2] = 16;
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_BLOCKS && strstr(v.why, "rand changed size") != NULL, "a resized block: %s",
          v.why);
    ico_xb_verdict_free(&v);
    b.blk_size[2] = 8;

    /* a text and a start-up value B has not */
    free_build(&b);
    make_build(&b, 1);
    b.has_wait = 0;
    fill_build(&b, 1);
    parse_build(&b);
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_DATA && strstr(v.why, "\"WaitSema\"") != NULL, "a missing text: %s", v.why);
    ico_xb_verdict_free(&v);
    b.has_wait = 1;
    fill_build(&b, 1);
    b.snap[0x40 / 8] = 0;
    k = check_into(sec, n, &b, &a, &v);
    CHECK(k == ICO_XB_DATA && strstr(v.why, "start-up") != NULL, "a changed start-up value: %s",
          v.why);
    ico_xb_verdict_free(&v);
    free(sec);

    /* states that load only into their own build */
    {
        uint64_t extra[1];

        fill_build(&b, 1);
        extra[0] = a.lo + a.o_said + 8; /* said.0 is two variables */
        sec = save(&a, extra, 1, NULL, 0, &n, &st, 1);
        CHECK((st.flags & ICO_XB_UNNAMED) != 0, "a word into a repeated name: unnamed");
        k = check_into(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_PINNED && strstr(v.why, "no symbol names") != NULL, "pinned: %s", v.why);
        ico_xb_verdict_free(&v);
        free(sec);
        extra[0] = a.lo + TEXT_LO + 0x8; /* in the code before the first unit */
        sec = save(&a, extra, 1, NULL, 0, &n, &st, 1);
        CHECK((st.flags & ICO_XB_UNNAMED) != 0 && st.first_unnamed == extra[0],
              "a code address no unit holds: unnamed");
        free(sec);
        extra[0] = a.lo + a.o_anchor + 0x10; /* inside ico_diag_init's padding: its unit */
        sec = save(&a, extra, 1, NULL, 0, &n, &st, 1);
        CHECK((st.flags & ICO_XB_UNNAMED) == 0, "a word in a unit's padding has its unit");
        free(sec);
        sec = save(&a, NULL, 0, NULL, 0, &n, &st, 0);
        k = check_into(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_PINNED && strstr(v.why, "no symbol file") != NULL,
              "a build without a symbol file: %s", v.why);
        ico_xb_verdict_free(&v);
        free(sec);
    }
    /* the bounds: more distinct program words than the record holds */
    {
        IcoXbProc p;
        IcoXbRefs *r;
        uint64_t i;

        proc_of(&a, &p);
        r = ico_xb_begin(&p);
        /* every byte address of the image's last 0x3F000 bytes: more than
           three quarters of ICO_XB_MAX_VALUES */
        for (i = 0; r != NULL && i < 0x3F000u; i++) {
            ico_xb_word(r, a.hi - 0x3F000u + i, 1);
        }
        /* the blocks as a save records them: the check compares them before
           it reads the record's flags */
        if (r != NULL) {
            uint64_t at[2] = {0, 0};
            int bi;

            for (bi = 0; bi < a.n_blocks; bi++) {
                const int audio = (a.blk_flags[bi] & ICO_STATE_AUDIO) != 0;

                ico_xb_block(r, a.blk_name[bi], a.blk_size[bi], a.blk_flags[bi], a.lo + a.o_blk[bi],
                             at[audio]);
                at[audio] += a.blk_size[bi];
            }
        }
        sec = NULL;
        CHECK(r != NULL && ico_xb_finish(r, &sec, &n, &st) == 0 &&
                  (st.flags & ICO_XB_OVERFLOW) != 0,
              "overflow flagged (%u values)", st.values);
        k = check_into(sec, n, &b, &a, &v);
        CHECK(k == ICO_XB_PINNED && strstr(v.why, "more places") != NULL, "overflow: %s", v.why);
        ico_xb_verdict_free(&v);
        free(sec);
    }
    free_build(&a);
    free_build(&b);
}

/* an image below 4 GB (the Linux program's): pinned */
static void test_low_image(void)
{
    static const char nm[] = "0000000000401000 T ico_diag_init\n"
                             "0000000000401080 T ico_diag_log\n"
                             "0000000000405000 0000000000000040 D systemStatus\n";
    IcoSymTabEnv e;
    IcoXbProc p;
    IcoXbRefs *r;
    IcoXbStats st;
    IcoXbNow now;
    IcoXbVerdict v;
    char why[160];
    void *sec = NULL;
    size_t n = 0;

    memset(&e, 0, sizeof e);
    e.anchor = "ico_diag_init";
    e.anchor_addr = 0x401000;
    e.image_lo = 0x400000;
    e.image_hi = 0x500000;
    e.text_lo = 0x401000;
    e.text_hi = 0x403000;
    e.game_lo[0] = 0x405000;
    e.game_hi[0] = 0x405100;
    memset(&p, 0, sizeof p);
    p.tab = ico_symtab_parse(nm, sizeof nm - 1, &e, why, sizeof why);
    CHECK(p.tab != NULL, "the low table: %s", why);
    p.image.lo = e.image_lo;
    p.image.hi = e.image_hi;
    p.image.text_lo = e.text_lo;
    p.image.text_hi = e.text_hi;
    p.game_lo[0] = e.game_lo[0];
    p.game_hi[0] = e.game_hi[0];
    r = ico_xb_begin(&p);
    if (r != NULL) {
        ico_xb_word(r, 0x405010, 1); /* a game global: no memory read */
        CHECK(ico_xb_finish(r, &sec, &n, &st) == 0 && (st.flags & ICO_XB_LOW_IMAGE) != 0,
              "an image below 4 GB is flagged");
    }
    memset(&now, 0, sizeof now);
    now.tab = p.tab;
    now.image = p.image;
    CHECK(ico_xb_check(sec, n, &now, &v) == ICO_XB_PINNED && strstr(v.why, "4 GB") != NULL,
          "below 4 GB: %s", v.why);
    ico_xb_verdict_free(&v);
    free(sec);
    ico_symtab_free(p.tab);
    /* a damaged record */
    CHECK(ico_xb_check("SYMS", 4, &now, &v) == ICO_XB_BAD, "a short record is damaged");
    ico_xb_verdict_free(&v);
}

/* --- the format ---------------------------------------------------------------------- */

static void test_format(void)
{
    const char *path = "state_port_test.ics";
    static unsigned char bytes[64];
    IcoStateHeader h, r, now;
    IcoStateSection s;
    IcoStateWriteResult res;
    char detail[160];

    ico_state_header_init(&h, "build-a");
    CHECK(h.version == ICO_STATE_FORMAT_VERSION && ICO_STATE_FORMAT_VERSION == 2,
          "the format is 2");
    snprintf(h.disc, sizeof h.disc, "pal");
    h.version = 1; /* a format 1 header */
    s.name = "arena";
    s.data = bytes;
    s.bytes = sizeof bytes;
    s.addr = 0;
    s.flags = 0;
    remove(path);
    CHECK(ico_state_file_write(path, &h, &s, 1) == ICO_STATE_OK);
    CHECK(ico_state_file_flush(&res) == 1 && res.result == ICO_STATE_OK);
    CHECK(ico_state_file_read_header(path, &r) == ICO_STATE_OK && r.version == 1,
          "a format 1 header still reads");
    ico_state_header_init(&now, "build-b");
    snprintf(now.disc, sizeof now.disc, "pal");
    CHECK(ico_state_header_check(&r, &now, detail, sizeof detail) == ICO_STATE_OTHER_BUILD &&
              strcmp(detail, "build-a") == 0,
          "format 1: another build (%s)", detail);
    CHECK(ico_state_header_check_other_build(&r, &now, detail, sizeof detail) ==
              ICO_STATE_OTHER_BUILD,
          "format 1 fails the other-build check too");
    r.version = 2;
    CHECK(ico_state_header_check(&r, &now, NULL, 0) == ICO_STATE_OTHER_BUILD,
          "another build id of format 2");
    CHECK(ico_state_header_check_other_build(&r, &now, NULL, 0) == ICO_STATE_OK,
          "format 2 of another build goes on to its symbols record");
    snprintf(r.disc, sizeof r.disc, "jpn");
    CHECK(ico_state_header_check_other_build(&r, &now, detail, sizeof detail) ==
              ICO_STATE_OTHER_DISC,
          "another disc still refused");
    remove(path);
}

int main(void)
{
    test_x86_decoder();
    test_hash_x86();
    test_hash_a64();
    test_symtab();
    test_cross();
    test_low_image();
    test_format();
    return ico_check_summary("state_port");
}
