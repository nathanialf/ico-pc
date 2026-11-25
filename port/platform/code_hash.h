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
 *           a frame offset is never masked.  Rules 2 also zero that offset
 *           where the base is any register an ADRP of the function writes,
 *           or a MOV copy of one, wherever the ADRP lies (clang keeps a page
 *           in a callee-saved register across calls and uses it after a
 *           branch, before its ADRP in the function's order: rules 1 left
 *           those offsets in, so 521 of the 13,857 functions two Android
 *           builds share hashed apart with no change of their source; 81
 *           with rules 2, each a real change).  A register that is a page
 *           in one path and a pointer in another has its offsets masked in
 *           both.  Rules 3 close two gaps that made identical game code hash
 *           apart between two Android builds (about two functions with a
 *           thread inside a release):
 *           - the linker's Cortex-A53 erratum 843419 patch (lld
 *             --fix-cortex-a53-843419, the NDK's default): where an ADRP
 *             lands at page offset 0xff8 or 0xffc and a load or store
 *             through its register follows 8 or 12 bytes later, the linker
 *             puts that load in a stub (the load, then a B back to the word
 *             after it) and a B to the stub in its place, by address alone.
 *             Such a B hashes as the stub's load in its place (masked as
 *             that load would be), when its shape is exactly that: a B (not
 *             BL) out of the function, 8 or 12 bytes after an ADRP at page
 *             offset 0xff8 or 0xffc, to a stub in the code range whose first
 *             word is a load or store (unsigned offset) based on that
 *             ADRP's register and whose second is a B to the word after the
 *             patch.  Any other branch hashes as itself.  The stub is read
 *             from the code range the caller gives (IcoA64Text: the running
 *             program's own code; tools/code_hash_a64.py reads it from the
 *             library);
 *           - the registers an instruction writes (ico_code_a64_writes)
 *             are exact where rules 1 and 2 were not: an FMOV (immediate) or
 *             an LDP of SIMD registers, a CCMP's flags field and the SIMD
 *             structure loads no longer count as general register writes
 *             (each ended a page early, so its later use kept its link-time
 *             offset), and the conversions to a general register (FCVTZS
 *             and the like, fixed point too), UMOV and SMOV, BLR (a call),
 *             an exclusive store's status register, the PAC hints and the
 *             write-back of a PAC load or a SIMD structure's base now count
 *             (rules 1 and 2 missed them: a page register overwritten so
 *             kept its uses masked).  A load or store of a kind the decoder
 *             does not know counts as writing every register.
 *           Rules 1 and 2 are computed as they were (their decoder is kept
 *           as it was), so a record of those rules compares as before.
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

#define ICO_CODE_HASH_RULES 3 /* 1 and 2 are still computed: a record says which it used */

enum { ICO_CODE_RAW = 0, ICO_CODE_X64 = 1, ICO_CODE_A64 = 2 };

/* This program's architecture (ICO_CODE_X64, ICO_CODE_A64 or RAW). */
int ico_code_arch_native(void);

/* The masked hash of the n code bytes at `code`, which the running program
   has at address `addr` (the function's start: a branch target in
   [addr, addr + n) is inside it), with the program's image at
   [image_lo, image_hi). */
uint64_t ico_code_hash(int arch, const void *code, size_t n, uint64_t addr, uint64_t image_lo,
                       uint64_t image_hi);
/* The same under the given rules (1 to ICO_CODE_HASH_RULES), with no code
   range to read a patch stub from (rules 3: an erratum patch's B hashes as
   itself). */
uint64_t ico_code_hash_rules(int rules, int arch, const void *code, size_t n, uint64_t addr,
                             uint64_t image_lo, uint64_t image_hi);

/* The program's code range a patch stub may be read from: `bytes` holds the
   bytes of addresses [lo, hi) (the running program: its own code, bytes ==
   lo). */
typedef struct IcoA64Text {
    const void *bytes;
    uint64_t lo, hi;
} IcoA64Text;

/* ico_code_hash_rules with the code range `tx` (NULL: none) for rules 3's
   erratum patch sites (arm64 only; the other architectures ignore it). */
uint64_t ico_code_hash_text(int rules, int arch, const void *code, size_t n, uint64_t addr,
                            uint64_t image_lo, uint64_t image_hi, const IcoA64Text *tx);

/* The function's `words` arm64 words at `code` (at address addr) into ws
   with every erratum 843419 patch site (rules 3 above; tx NULL: none can be
   read) replaced by the stub's instruction it stands for: the number of
   sites replaced. */
size_t ico_code_a64_unpatch(const void *code, size_t words, uint64_t addr, const IcoA64Text *tx,
                            uint32_t *ws);

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
   0..31 (bit 31 is sp or xzr), under the current rules (exact: rules 3
   above); ~0u when the decoder does not know (every register then counts as
   written).  A call (BL, BLR, SVC) writes x30 and x0-x18. */
uint32_t ico_code_a64_writes(uint32_t w);
/* The same as the given rules' hashes decode it (1 and 2: the decoder those
   rules were made with; 3: ico_code_a64_writes). */
uint32_t ico_code_a64_writes_rules(int rules, uint32_t w);

/* The forward flow rules 2 and 3 mask by, under the current rules, over the
   `words` arm64 words at code (the function from its start, at address
   addr; its erratum patch sites read through tx, NULL: none): into[k] gets
   the registers that may hold a tracked value at word k's start on some
   path through the function's own branches, seen[k] whether a path
   reaches it.  from == words: the function's entry, every ADRP tracked
   (adrp 1); from < words: the value in `regs` as after word `from` (one
   ADRP's page; adrp 0 tracks no other).  A MOV copies a tracked register,
   any other write ends it.  0, or -1 (no memory). */
int ico_code_a64_flow(const void *code, size_t words, uint64_t addr, const IcoA64Text *tx,
                      size_t from, uint32_t regs, int adrp, uint32_t *into, uint8_t *seen);

#endif /* ICO_PLATFORM_CODE_HASH_H */
