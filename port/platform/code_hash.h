/*
 * port/platform/code_hash.h
 *
 * A function's code hash for save states that cross versions
 * (state_xbuild.h): XXH3-64 over the function's machine code with every
 * field that depends on WHERE the code and its data were linked set to
 * zero, so the same source compiled the same way hashes alike in two
 * builds whose layout differs.  What the hash protects is a suspended frame
 * resuming in the other build's copy of the function: the same instructions
 * at the same offsets, the same frame and the same registers.  A changed
 * displacement or address constant changes none of these, so it is masked;
 * every other byte (the operations, the registers, the frame offsets, the
 * constants that are no address) is hashed as it is.
 *
 *   x86-64  each instruction is decoded for its length (legacy prefixes,
 *           REX, VEX, EVEX, the one-, two- and three-byte maps) and its
 *           fields: a branch displacement whose target is outside the
 *           function, every RIP-relative displacement, and a 32-bit
 *           displacement or a 32- or 64-bit immediate whose value lies in
 *           the program's image (an absolute address: the Linux program is
 *           linked at a fixed address below 4 GB) are zeroed.  A byte the
 *           decoder does not know hashes the rest of the function raw.
 *   arm64   B/BL, B.cond, CBZ/CBNZ, TBZ/TBNZ, LDR (literal) and ADR whose
 *           target is outside the function have their offset zeroed; ADRP's
 *           page offset is always zeroed, and so is the 12-bit offset of an
 *           ADD (immediate) or a load/store (unsigned offset) based on a
 *           register an ADRP set and nothing wrote since (approximately:
 *           ico_code_a64_writes).  x29 and sp are never such a register, so
 *           a frame offset is never masked.
 *   other   the bytes raw.
 *
 * Trailing padding (x86: nop, int3; arm64: zero words, nop) is left out, so
 * a function's hash does not depend on how far the next one was aligned.
 * The masking rules have a version (ICO_CODE_HASH_RULES): a state whose
 * hashes were made under other rules compares none of them.
 */
#ifndef ICO_PLATFORM_CODE_HASH_H
#define ICO_PLATFORM_CODE_HASH_H

#include <stddef.h>
#include <stdint.h>

#define ICO_CODE_HASH_RULES 1

enum { ICO_CODE_RAW = 0, ICO_CODE_X64 = 1, ICO_CODE_A64 = 2 };

/* This program's architecture (ICO_CODE_X64, ICO_CODE_A64 or RAW). */
int ico_code_arch_native(void);

/* The masked hash of the n code bytes at `code`, which the running program
   has at address `addr` (the function's start: a branch target in
   [addr, addr + n) is inside it), with the program's image at
   [image_lo, image_hi). */
uint64_t ico_code_hash(int arch, const void *code, size_t n, uint64_t addr, uint64_t image_lo,
                       uint64_t image_hi);

/* --- the decoders (tests) ----------------------------------------------------- */

enum { ICO_X64_BRANCH = 1, ICO_X64_RIP, ICO_X64_DISP, ICO_X64_IMM };

typedef struct IcoX64Field {
    uint8_t off;  /* from the instruction's first byte */
    uint8_t size; /* 1, 2, 4 or 8 */
    uint8_t kind; /* ICO_X64_* */
} IcoX64Field;

typedef struct IcoX64Insn {
    uint8_t len; /* 0: not decoded */
    uint8_t nop; /* nop, a multi-byte nop or int3 (padding) */
    uint8_t n;
    IcoX64Field f[3];
} IcoX64Insn;

/* One x86-64 instruction at p (avail bytes readable): its length and
   fields, or len 0 for an opcode the decoder does not know or one longer
   than avail. */
void ico_code_x64_decode(const uint8_t *p, size_t avail, IcoX64Insn *out);

/* The general registers an arm64 instruction writes, as a mask of bits
   0..31 (bit 31 is sp or xzr); ~0u when the decoder does not know (every
   register then counts as written). */
uint32_t ico_code_a64_writes(uint32_t w);

#endif /* ICO_PLATFORM_CODE_HASH_H */
