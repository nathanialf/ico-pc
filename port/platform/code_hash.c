/*
 * port/platform/code_hash.c
 *
 * A function's code hash with the link-dependent fields masked
 * (code_hash.h): the x86-64 length decoder, the arm64 rules and the hash.
 */
#include "code_hash.h"

#include <stdlib.h>
#include <string.h>

#include "xxh3.h"

int ico_code_arch_native(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return ICO_CODE_X64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return ICO_CODE_A64;
#else
    return ICO_CODE_RAW;
#endif
}

/* --- x86-64 ------------------------------------------------------------------- */

static int x64_prefix(uint8_t b)
{
    switch (b) {
    case 0x66:
    case 0x67:
    case 0xF2:
    case 0xF3:
    case 0xF0:
    case 0x2E:
    case 0x36:
    case 0x3E:
    case 0x26:
    case 0x64:
    case 0x65:
        return 1;
    }
    return 0;
}

/* a one-byte opcode with a ModRM byte */
static int x64_modrm1(uint8_t op)
{
    if (op < 0x40) {
        return (op & 7) < 4; /* the ALU forms r/m,r and r,r/m */
    }
    if (op == 0x63 || op == 0x69 || op == 0x6B || (op >= 0x80 && op <= 0x8F) ||
        (op >= 0xD8 && op <= 0xDF)) {
        return 1;
    }
    switch (op) {
    case 0xC0:
    case 0xC1:
    case 0xC6:
    case 0xC7:
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3:
    case 0xF6:
    case 0xF7:
    case 0xFE:
    case 0xFF:
        return 1;
    }
    return 0;
}

/* a two-byte opcode (0F xx) WITHOUT a ModRM byte */
static int x64_plain2(uint8_t op)
{
    if ((op >= 0x80 && op <= 0x8F) || (op >= 0xC8 && op <= 0xCF) || (op >= 0x30 && op <= 0x35)) {
        return 1;
    }
    switch (op) {
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0B:
    case 0x0E:
    case 0x37:
    case 0x77:
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA8:
    case 0xA9:
    case 0xAA:
        return 1;
    }
    return 0;
}

/* a two-byte opcode with an imm8 after its ModRM (in the VEX/EVEX map 1
   only the first eight) */
static int x64_imm2(uint8_t op, int vex)
{
    switch (op) {
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0xC2:
    case 0xC4:
    case 0xC5:
    case 0xC6:
        return 1;
    case 0xA4:
    case 0xAC:
    case 0xBA:
        return !vex;
    }
    return 0;
}

/* a one-byte opcode with no operand bytes at all */
static int x64_bare1(uint8_t op)
{
    if ((op >= 0x50 && op <= 0x5F) || (op >= 0x90 && op <= 0x99) || (op >= 0x9B && op <= 0x9F) ||
        (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) || (op >= 0xEC && op <= 0xEF) ||
        (op >= 0xF8 && op <= 0xFD)) {
        return 1;
    }
    switch (op) {
    case 0xC3:
    case 0xC9:
    case 0xCB:
    case 0xCC:
    case 0xCF:
    case 0xD7:
    case 0xF1:
    case 0xF4:
    case 0xF5:
        return 1;
    }
    return 0;
}

/* The ModRM byte at p[0] with its SIB byte and displacement: their length;
   *rip the offset (from p) of a RIP-relative disp32 or -1, *disp that of
   another disp32 or -1, *reg the reg field. */
static int x64_modrm(const uint8_t *p, int *rip, int *disp, int *reg)
{
    const int mod = p[0] >> 6;
    const int rm = p[0] & 7;
    int n = 1;

    *rip = -1;
    *disp = -1;
    *reg = (p[0] >> 3) & 7;
    if (mod == 3) {
        return 1;
    }
    if (rm == 4) {
        n = 2;
        if (mod == 0 && (p[1] & 7) == 5) {
            *disp = n;
            n += 4;
        }
    } else if (mod == 0 && rm == 5) {
        *rip = n;
        n += 4;
    }
    if (mod == 1) {
        n += 1;
    } else if (mod == 2) {
        *disp = n;
        n += 4;
    }
    return n;
}

static void x64_field(IcoX64Insn *out, int off, int size, int kind)
{
    if (out->n < 3) {
        out->f[out->n].off = (uint8_t)off;
        out->f[out->n].size = (uint8_t)size;
        out->f[out->n].kind = (uint8_t)kind;
        out->n++;
    }
}

/* the ModRM operand's displacement fields, the ModRM at b[at] */
static int x64_operand(const uint8_t *b, int at, IcoX64Insn *out, int *reg)
{
    int rip, disp;
    const int n = x64_modrm(b + at, &rip, &disp, reg);

    if (rip >= 0) {
        x64_field(out, at + rip, 4, ICO_X64_RIP);
    }
    if (disp >= 0) {
        x64_field(out, at + disp, 4, ICO_X64_DISP);
    }
    return n;
}

void ico_code_x64_decode(const uint8_t *in, size_t avail, IcoX64Insn *out)
{
    uint8_t b[40];
    const size_t k = avail < 15 ? avail : 15;
    int i = 0, len = 0, opsz = 0, adsz = 0, rexw = 0, iz, reg;
    uint8_t op;

    memset(out, 0, sizeof *out);
    memset(b, 0, sizeof b);
    memcpy(b, in, k);
    while (i < 14 && x64_prefix(b[i])) {
        opsz |= b[i] == 0x66;
        adsz |= b[i] == 0x67;
        i++;
    }
    if (b[i] >= 0x40 && b[i] <= 0x4F) {
        rexw = (b[i] & 8) != 0;
        i++;
    }
    op = b[i];
    iz = opsz ? 2 : 4;
    if (op == 0xC4 || op == 0xC5 || op == 0x62) {
        /* VEX (two or three bytes) and EVEX (four): a ModRM always, an
           imm8 in map 3 and for the few map-1 opcodes that have one */
        int map;
        uint8_t opc;
        int n;

        if (op == 0xC5) {
            map = 1;
            i += 2;
        } else if (op == 0xC4) {
            map = b[i + 1] & 0x1F;
            i += 3;
        } else {
            map = b[i + 1] & 7;
            i += 4;
        }
        opc = b[i++];
        n = x64_operand(b, i, out, &reg);
        len = i + n + ((map == 3 || (map == 1 && x64_imm2(opc, 1))) ? 1 : 0);
    } else if (op == 0x0F) {
        const uint8_t op2 = b[i + 1];

        if (op2 == 0x38 || op2 == 0x3A) {
            i += 3;
            len = i + x64_operand(b, i, out, &reg) + (op2 == 0x3A ? 1 : 0);
        } else {
            i += 2;
            if (op2 >= 0x80 && op2 <= 0x8F) {
                x64_field(out, i, 4, ICO_X64_BRANCH);
                len = i + 4;
            } else if (x64_plain2(op2)) {
                len = i;
            } else {
                len = i + x64_operand(b, i, out, &reg) + (x64_imm2(op2, 0) ? 1 : 0);
                out->nop = op2 == 0x1F;
            }
        }
    } else {
        i++;
        if (x64_modrm1(op)) {
            int imm = 0;
            const int n = x64_operand(b, i, out, &reg);

            if (op == 0x80 || op == 0x82 || op == 0x83 || op == 0x6B || op == 0xC0 || op == 0xC1 ||
                op == 0xC6 || (op == 0xF6 && reg < 2)) {
                imm = 1;
            } else if (op == 0x81 || op == 0x69 || op == 0xC7 || (op == 0xF7 && reg < 2)) {
                imm = iz;
            }
            if (imm == 4) {
                x64_field(out, i + n, 4, ICO_X64_IMM);
            }
            len = i + n + imm;
        } else if (op < 0x40 && (op & 7) == 4) {
            len = i + 1; /* ALU AL, imm8 */
        } else if (op < 0x40 && (op & 7) == 5) {
            if (iz == 4) {
                x64_field(out, i, 4, ICO_X64_IMM); /* ALU eAX, imm32 */
            }
            len = i + iz;
        } else if ((op >= 0x70 && op <= 0x7F) || op == 0xEB || (op >= 0xE0 && op <= 0xE3)) {
            x64_field(out, i, 1, ICO_X64_BRANCH);
            len = i + 1;
        } else if (op == 0xE8 || op == 0xE9) {
            x64_field(out, i, 4, ICO_X64_BRANCH);
            len = i + 4;
        } else if (op == 0x6A || op == 0xA8 || op == 0xCD || (op >= 0xB0 && op <= 0xB7) ||
                   (op >= 0xE4 && op <= 0xE7)) {
            len = i + 1;
        } else if (op == 0x68 || op == 0xA9) {
            if (iz == 4) {
                x64_field(out, i, 4, ICO_X64_IMM);
            }
            len = i + iz;
        } else if (op >= 0xB8 && op <= 0xBF) {
            const int n = rexw ? 8 : iz;

            if (n >= 4) {
                x64_field(out, i, n, ICO_X64_IMM);
            }
            len = i + n;
        } else if (op == 0xC2 || op == 0xCA) {
            len = i + 2;
        } else if (op == 0xC8) {
            len = i + 3;
        } else if (op >= 0xA0 && op <= 0xA3) {
            /* mov to or from an absolute address (moffs) */
            x64_field(out, i, adsz ? 4 : 8, ICO_X64_IMM);
            len = i + (adsz ? 4 : 8);
        } else if (x64_bare1(op)) {
            len = i;
            out->nop = op == 0x90 || op == 0xCC;
        } else {
            len = 0;
        }
    }
    if (len <= 0 || len > 15 || (size_t)len > avail) {
        memset(out, 0, sizeof *out);
        return;
    }
    out->len = (uint8_t)len;
}

static uint64_t field_value(const uint8_t *p, int size)
{
    uint64_t v = 0;
    int i;

    for (i = size - 1; i >= 0; i--) {
        v = v << 8 | p[i];
    }
    return v;
}

static int64_t sign_extend(uint64_t v, int bits)
{
    const uint64_t m = 1ull << (bits - 1);

    v &= bits == 64 ? ~0ull : ((1ull << bits) - 1);
    return (int64_t)((v ^ m) - m);
}

/* the masked bytes into buf; the length up to the last instruction that is
   not padding */
static size_t mask_x64(const uint8_t *p, size_t n, uint8_t *buf, uint64_t addr, uint64_t lo,
                       uint64_t hi)
{
    size_t at = 0, keep = 0;

    while (at < n) {
        IcoX64Insn ins;
        int j;

        ico_code_x64_decode(p + at, n - at, &ins);
        if (ins.len == 0) {
            memcpy(buf + at, p + at, n - at);
            return n;
        }
        memcpy(buf + at, p + at, ins.len);
        for (j = 0; j < ins.n; j++) {
            const IcoX64Field *f = &ins.f[j];
            uint8_t *q = buf + at + f->off;
            const uint64_t v = field_value(q, f->size);
            int zero = 0;

            if (f->kind == ICO_X64_BRANCH) {
                const uint64_t t = addr + at + ins.len + (uint64_t)sign_extend(v, f->size * 8);

                zero = t < addr || t - addr >= n;
            } else if (f->kind == ICO_X64_RIP) {
                zero = 1;
            } else {
                const uint64_t a =
                    f->kind == ICO_X64_DISP ? (uint64_t)sign_extend(v, f->size * 8) : v;

                zero = a >= lo && a < hi;
            }
            if (zero) {
                memset(q, 0, f->size);
            }
        }
        at += ins.len;
        if (!ins.nop) {
            keep = at;
        }
    }
    return keep;
}

/* --- arm64 -------------------------------------------------------------------- */

uint32_t ico_code_a64_writes(uint32_t w)
{
    const uint32_t rd = 1u << (w & 31);
    const uint32_t rn = 1u << ((w >> 5) & 31);
    const unsigned opc = (w >> 22) & 3;
    const unsigned v = (w >> 26) & 1;

    if ((w & 0x7C000000u) == 0x14000000u) {
        /* B writes nothing; BL x30 and, through the callee, x0-x18 */
        return (w & 0x80000000u) != 0 ? (0x7FFFFu | 1u << 30) : 0u;
    }
    if ((w & 0x0A000000u) == 0x08000000u) {
        /* loads and stores */
        if ((w & 0x3A000000u) == 0x28000000u) {
            /* pairs: a load writes both; pre/post index write the base */
            uint32_t out = 0;
            const unsigned idx = (w >> 23) & 3;

            if (((w >> 22) & 1) != 0) {
                out |= rd | 1u << ((w >> 10) & 31);
            }
            if (idx == 1 || idx == 3) {
                out |= rn;
            }
            return out;
        }
        if ((w & 0x3B000000u) == 0x39000000u) {
            return opc != 0 && !v ? rd : 0u; /* unsigned offset */
        }
        if ((w & 0x3B200000u) == 0x38000000u) {
            /* the 9-bit forms; pre/post index write the base */
            const unsigned idx = (w >> 10) & 3;
            uint32_t out = opc != 0 && !v ? rd : 0u;

            if (idx == 1 || idx == 3) {
                out |= rn;
            }
            return out;
        }
        if ((w & 0x3B200C00u) == 0x38200800u) {
            return opc != 0 && !v ? rd : 0u; /* register offset */
        }
        if ((w & 0x3B000000u) == 0x18000000u) {
            return !v ? rd : 0u; /* literal */
        }
        return rd;
    }
    if ((w & 0x1C000000u) == 0x10000000u || (w & 0x0E000000u) == 0x0A000000u ||
        (w & 0x1E000000u) == 0x1A000000u) {
        return rd; /* data processing, immediate and register */
    }
    if ((w & 0xFFF00000u) == 0xD5300000u) {
        return rd; /* MRS */
    }
    if ((w & 0xFE000000u) == 0xD4000000u || (w & 0xFFC00000u) == 0xD5000000u) {
        return 0; /* exceptions and system */
    }
    if ((w & 0xFE000000u) == 0xD6000000u || (w & 0x7E000000u) == 0x34000000u ||
        (w & 0x7E000000u) == 0x36000000u || (w & 0xFF000010u) == 0x54000000u) {
        return 0; /* the other branches */
    }
    if ((w & 0x0E000000u) == 0x0E000000u) {
        /* SIMD and floating point: FMOV to a general register writes one */
        return (w & 0x5F000000u) == 0x1E000000u && ((w >> 16) & 7) == 6 ? rd : 0u;
    }
    return ~0u;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static size_t mask_a64(const uint8_t *p, size_t n, uint8_t *buf, uint64_t addr)
{
    const size_t words = n / 4;
    uint32_t pend = 0; /* registers an ADRP set and nothing wrote since */
    size_t k, keep = 0;

    for (k = 0; k < words; k++) {
        const uint32_t w = get32(p + 4 * k);
        const uint64_t pc = addr + 4 * k;
        uint32_t m = w;
        uint64_t t;
        uint32_t wr;

        if ((w & 0x7C000000u) == 0x14000000u) {
            t = pc + (uint64_t)(sign_extend(w & 0x3FFFFFFu, 26) * 4);
            if (t < addr || t - addr >= n) {
                m = w & 0xFC000000u;
            }
        } else if ((w & 0xFF000010u) == 0x54000000u || (w & 0x7E000000u) == 0x34000000u ||
                   (w & 0x3B000000u) == 0x18000000u) {
            t = pc + (uint64_t)(sign_extend((w >> 5) & 0x7FFFFu, 19) * 4);
            if (t < addr || t - addr >= n) {
                m = w & ~(0x7FFFFu << 5);
            }
        } else if ((w & 0x7E000000u) == 0x36000000u) {
            t = pc + (uint64_t)(sign_extend((w >> 5) & 0x3FFFu, 14) * 4);
            if (t < addr || t - addr >= n) {
                m = w & ~(0x3FFFu << 5);
            }
        } else if ((w & 0x9F000000u) == 0x10000000u) {
            const uint32_t imm = ((w >> 5) & 0x7FFFFu) << 2 | ((w >> 29) & 3u);

            t = pc + (uint64_t)sign_extend(imm, 21);
            if (t < addr || t - addr >= n) {
                m = w & 0x9F00001Fu; /* ADR */
            }
        } else if ((w & 0x9F000000u) == 0x90000000u) {
            m = w & 0x9F00001Fu; /* ADRP */
        } else if (((w & 0x7F800000u) == 0x11000000u || (w & 0x3B000000u) == 0x39000000u) &&
                   ((pend >> ((w >> 5) & 31)) & 1u) != 0) {
            m = w & ~(0xFFFu << 10); /* the low 12 bits of an ADRP's address */
        }
        put32(buf + 4 * k, m);
        wr = ico_code_a64_writes(w);
        pend = wr == ~0u ? 0u : pend & ~wr;
        if ((w & 0x9F000000u) == 0x90000000u && (w & 31) != 29 && (w & 31) != 31) {
            pend |= 1u << (w & 31);
        }
        if (w != 0 && w != 0xD503201Fu) {
            keep = 4 * (k + 1);
        }
    }
    if (n > 4 * words) {
        memcpy(buf + 4 * words, p + 4 * words, n - 4 * words);
        keep = n;
    }
    return keep;
}

/* --- the hash ----------------------------------------------------------------- */

uint64_t ico_code_hash(int arch, const void *code, size_t n, uint64_t addr, uint64_t image_lo,
                       uint64_t image_hi)
{
    const uint8_t *p = (const uint8_t *)code;
    uint8_t *buf;
    size_t keep;
    uint64_t h;

    if (arch == ICO_CODE_RAW || n == 0) {
        return xxh3_64(p, n);
    }
    buf = (uint8_t *)malloc(n);
    if (buf == NULL) {
        /* no copy to mask: a hash no other build's masked hash equals */
        return xxh3_64(p, n) ^ 0x9E3779B97F4A7C15ull;
    }
    keep = arch == ICO_CODE_X64 ? mask_x64(p, n, buf, addr, image_lo, image_hi)
                                : mask_a64(p, n, buf, addr);
    h = xxh3_64(buf, keep);
    free(buf);
    return h;
}
