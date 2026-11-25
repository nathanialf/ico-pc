"""tools/code_hash_a64.py -- port/platform/code_hash.c's arm64 code hash, rules 1 to 3, and
port/render/xxh3.c's XXH3-64, in Python, for tools/state_legacy_table.py (an earlier
version's functions hashed from its released library, as this build hashes its own).
`python3 -I tools/code_hash_a64.py --selftest` checks XXH3-64 against xxHash's sanity vectors
(port/render/test/texpack_name_test.c's) and the masking against hand-made functions."""
import struct
import sys

M64 = (1 << 64) - 1
P32_1, P32_2, P32_3 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D
P64_1, P64_2, P64_3, P64_4, P64_5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                                     0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)
PMX_1, PMX_2 = 0x165667919E3779F9, 0x9FB21C651E98DF25
SECRET = bytes.fromhex(
    "b8fe6c3923a44bbe7c01812cf721ad1cded46de9839097db7240a4a4b7b3671f"
    "cb79e64eccc0e578825ad07dccff7221b8084674f743248ee03590e6813a264c"
    "3c2852bb91c300cb88d0658b1b532ea371644897a20df94e3819ef46a9deacd8"
    "a8fa763fe39c343ff9dcbbc7c70b4f1d8a51e04bcdb45931c89f7ec9d9787364"
    "eac5ac8334d3ebc3c581a0fffa1363eb170ddd51b7f0da49d316552629d4689e"
    "2b16be587d47a1fc8ff8b8d17ad031ce45cb3a8f95160428afd7fbcabb4b407e")


def rd32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def rd64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M64


def swap64(x):
    return int.from_bytes(x.to_bytes(8, "little"), "big")


def fold(a, b):
    p = a * b
    return (p & M64) ^ (p >> 64)


def av64(h):
    h ^= h >> 33
    h = h * P64_2 & M64
    h ^= h >> 29
    h = h * P64_3 & M64
    return h ^ (h >> 32)


def av(h):
    h ^= h >> 37
    h = h * PMX_1 & M64
    return h ^ (h >> 32)


def rrmxmx(h, n):
    h ^= rotl(h, 49) ^ rotl(h, 24)
    h = h * PMX_2 & M64
    h ^= (h >> 35) + n
    h = h * PMX_2 & M64
    return h ^ (h >> 28)


def mix16(b, o, s):
    return fold(rd64(b, o) ^ rd64(SECRET, s), rd64(b, o + 8) ^ rd64(SECRET, s + 8))


def xxh3_64(b):
    n = len(b)
    if n <= 16:
        if n > 8:
            lo = rd64(b, 0) ^ (rd64(SECRET, 24) ^ rd64(SECRET, 32))
            hi = rd64(b, n - 8) ^ (rd64(SECRET, 40) ^ rd64(SECRET, 48))
            return av((n + swap64(lo) + hi + fold(lo, hi)) & M64)
        if n >= 4:
            in64 = (rd32(b, n - 4) + (rd32(b, 0) << 32)) & M64
            return rrmxmx(in64 ^ (rd64(SECRET, 8) ^ rd64(SECRET, 16)), n)
        if n > 0:
            c = b[0] << 16 | b[n >> 1] << 24 | b[n - 1] | n << 8
            return av64(c ^ (rd32(SECRET, 0) ^ rd32(SECRET, 4)))
        return av64(rd64(SECRET, 56) ^ rd64(SECRET, 64))
    if n <= 128:
        acc = n * P64_1 & M64
        if n > 32:
            if n > 64:
                if n > 96:
                    acc += mix16(b, 48, 96) + mix16(b, n - 64, 112)
                acc += mix16(b, 32, 64) + mix16(b, n - 48, 80)
            acc += mix16(b, 16, 32) + mix16(b, n - 32, 48)
        acc += mix16(b, 0, 0) + mix16(b, n - 16, 16)
        return av(acc & M64)
    if n <= 240:
        acc = n * P64_1 & M64
        for i in range(8):
            acc += mix16(b, 16 * i, 16 * i)
        acc = av(acc & M64)
        for i in range(8, n // 16):
            acc += mix16(b, 16 * i, 16 * (i - 8) + 3)
        acc += mix16(b, n - 16, 136 - 17)
        return av(acc & M64)
    acc = [P32_3, P64_1, P64_2, P64_3, P64_4, P32_2, P64_5, P32_1]

    def accumulate(o, s):
        for i in range(8):
            v = rd64(b, o + 8 * i)
            k = v ^ rd64(SECRET, s + 8 * i)
            acc[i ^ 1] = (acc[i ^ 1] + v) & M64
            acc[i] = (acc[i] + (k & 0xFFFFFFFF) * (k >> 32)) & M64

    blocks = (n - 1) // 1024
    for blk in range(blocks):
        for s in range(16):
            accumulate(blk * 1024 + s * 64, s * 8)
        for i in range(8):
            a = acc[i]
            a ^= a >> 47
            a ^= rd64(SECRET, 192 - 64 + 8 * i)
            acc[i] = a * P32_1 & M64
    stripes = ((n - 1) - 1024 * blocks) // 64
    for s in range(stripes):
        accumulate(blocks * 1024 + s * 64, s * 8)
    accumulate(n - 64, 192 - 64 - 7)
    r = n * P64_1 & M64
    for i in range(4):
        r += fold(acc[2 * i] ^ rd64(SECRET, 11 + 16 * i), acc[2 * i + 1] ^ rd64(SECRET, 11 + 16 * i + 8))
    return av(r & M64)


def sext(v, bits):
    m = 1 << (bits - 1)
    return (v ^ m) - m


def a64_writes_v2(w):
    """code_hash.c a64_writes_v2: rules 1 and 2's decoder, kept as those rules' hashes were made"""
    rd = 1 << (w & 31)
    rn = 1 << ((w >> 5) & 31)
    opc = (w >> 22) & 3
    v = (w >> 26) & 1
    if w & 0x7C000000 == 0x14000000:
        return (0x7FFFF | 1 << 30) if w & 0x80000000 else 0
    if w & 0x0A000000 == 0x08000000:
        if w & 0x3A000000 == 0x28000000:
            out, idx = 0, (w >> 23) & 3
            if (w >> 22) & 1:
                out |= rd | 1 << ((w >> 10) & 31)
            if idx in (1, 3):
                out |= rn
            return out
        if w & 0x3B000000 == 0x39000000:
            return rd if opc and not v else 0
        if w & 0x3B200000 == 0x38000000:
            idx = (w >> 10) & 3
            out = rd if opc and not v else 0
            if idx in (1, 3):
                out |= rn
            return out
        if w & 0x3B200C00 == 0x38200800:
            return rd if opc and not v else 0
        if w & 0x3B000000 == 0x18000000:
            return rd if not v else 0
        return rd
    if w & 0x1C000000 == 0x10000000 or w & 0x0E000000 == 0x0A000000 or w & 0x1E000000 == 0x1A000000:
        return rd
    if w & 0xFFF00000 == 0xD5300000:
        return rd
    if w & 0xFE000000 == 0xD4000000 or w & 0xFFC00000 == 0xD5000000:
        return 0
    if (w & 0xFE000000 == 0xD6000000 or w & 0x7E000000 == 0x34000000 or
            w & 0x7E000000 == 0x36000000 or w & 0xFF000010 == 0x54000000):
        return 0
    if w & 0x0E000000 == 0x0E000000:
        return rd if (w & 0x5F000000 == 0x1E000000 and ((w >> 16) & 7) == 6) else 0
    return 0xFFFFFFFF


def a64_writes_v3(w):
    """code_hash.c a64_writes_v3 (rules 3, ico_code_a64_writes): the general registers written,
    by the A64 encoding tables' classes; an unknown load or store, an unallocated or SVE / SME
    word: every register"""
    rd = 1 << (w & 31)
    rn = 1 << ((w >> 5) & 31)
    rt2 = 1 << ((w >> 10) & 31)
    rs = 1 << ((w >> 16) & 31)
    call = 0x7FFFF | 1 << 30
    opc = (w >> 22) & 3
    v = (w >> 26) & 1
    if w & 0x7C000000 == 0x14000000:
        return call if w & 0x80000000 else 0
    if w & 0x7E000000 in (0x34000000, 0x36000000) or w & 0xFF000000 == 0x54000000:
        return 0
    if w & 0xFE000000 == 0xD6000000:
        return call if (w >> 21) & 3 == 1 else 0
    if w & 0xFF000000 == 0xD4000000:
        return call if (w >> 21) & 7 == 0 and w & 3 else 0
    if w & 0xFFC00000 == 0xD5000000:
        if w & 0x00200000:
            return rd
        if w & 0xFFFFF01F == 0xD503201F:
            h = (w >> 5) & 0x7F
            if h >> 3 == 1:
                return 1 << 16 | 1 << 17
            if h >> 3 == 3 or h == 7:
                return 1 << 30
            return 1 << 16 if h == 0x28 else 0
        return 0
    if w & 0x1C000000 == 0x10000000:
        return rd
    if w & 0x0A000000 == 0x08000000:
        if w & 0xBE000000 == 0x0C000000:
            return rn if w & 0x00800000 else 0
        if w & 0x3E000000 == 0x08000000:
            return rd | rt2 | rs | 1 << (((w >> 16) + 1) & 31) | rn
        if w & 0x3B000000 == 0x18000000:
            return 0 if v else rd
        if w & 0x3B000000 == 0x19000000:
            return rd | rn | rs
        if w & 0x3A000000 == 0x28000000:
            idx = (w >> 23) & 3
            out = rd | rt2 if (w >> 22) & 1 and not v else 0
            if idx in (1, 3):
                out |= rn
            return out
        if w & 0x3B000000 == 0x39000000:
            return rd if opc and not v else 0
        if w & 0x3B200000 == 0x38000000:
            idx = (w >> 10) & 3
            out = rd if opc and not v else 0
            if idx in (1, 3):
                out |= rn
            return out
        if w & 0x3B200C00 == 0x38200800:
            return rd if opc and not v else 0
        if w & 0x3B200C00 == 0x38200000 and not v:
            if w & 0xFFFFFC00 == 0xF83FD000:
                return 0xFFFFFFFF
            return rd | rs if w & 0xFFE0E000 == 0xF820A000 else rd
        if w & 0x3B200400 == 0x38200400 and not v:
            return rd | (rn if w & 0x800 else 0)
        return 0xFFFFFFFF
    if w & 0x0E000000 == 0x0A000000:
        if w & 0x3FE00000 == 0x3A400000:
            return 0
        if w & 0xFFE07C10 == 0xBA000400 or w & 0xFFFFBC1F == 0x3A00080D:
            return 0
        return rd
    if w & 0x0E000000 == 0x0E000000:
        if w & 0x5F20FC00 == 0x1E200000:
            op = (w >> 16) & 7
            return rd if op <= 1 or 4 <= op <= 6 else 0
        if w & 0x5F200000 == 0x1E000000:
            return rd if (w >> 16) & 6 == 0 else 0
        if w & 0xBFE0FC00 in (0x0E003C00, 0x0E002C00):
            return rd
        return 0
    return 0xFFFFFFFF


def a64_writes_rules(rules, w):
    return a64_writes_v3(w) if rules >= 3 else a64_writes_v2(w)


a64_writes = a64_writes_v3  # code_hash.c ico_code_a64_writes: the current rules


class Text:
    """the program's code range a patch stub may be read from (code_hash.h IcoA64Text): the
    bytes of addresses [lo, hi)"""

    def __init__(self, data, lo, hi=None):
        self.data, self.lo = data, lo
        self.hi = lo + len(data) if hi is None else hi

    def word(self, a):
        if a < self.lo or a + 4 > self.hi or a - self.lo + 4 > len(self.data):
            return None
        return rd32(self.data, a - self.lo)


def patch_site(ws, k, addr, text):
    """code_hash.c a64_patch_site (rules 3): the stub's instruction word k of the function at
    addr stands for, when it is an erratum 843419 patch site, else None"""
    w = ws[k]
    pc = addr + 4 * k
    if text is None or w & 0xFC000000 != 0x14000000:
        return None
    t = pc + sext(w & 0x3FFFFFF, 26) * 4
    if addr <= t < addr + 4 * len(ws) or t < text.lo or t > text.hi or text.hi - t < 8:
        return None
    s0, s1 = text.word(t), text.word(t + 4)
    if s0 is None or s1 is None:
        return None
    if s0 & 0x3B000000 != 0x39000000 or s1 & 0xFC000000 != 0x14000000 or \
            t + 4 + sext(s1 & 0x3FFFFFF, 26) * 4 != pc + 4:
        return None
    for back in (2, 3):
        if k >= back:
            a, apc = ws[k - back], pc - 4 * back
            if a & 0x9F000000 == 0x90000000 and apc & 0xFFF in (0xFF8, 0xFFC) and a & 31 == (s0 >> 5) & 31:
                return s0
    return None


def unpatch(ws, addr, text):
    """code_hash.c ico_code_a64_unpatch: the words with every patch site replaced, and the
    number of sites"""
    out, sites = list(ws), 0
    if text is None:
        return out, 0
    for k in range(2, len(ws)):
        s0 = patch_site(out, k, addr, text)
        if s0 is not None:
            out[k] = s0
            sites += 1
    return out, sites


def a64_page_uses(ws, addr, n, seed=None, writes=a64_writes_v2):
    """code_hash.c a64_flow and mask_a64_pages (rules 2 and 3, by the rules' decoder `writes`):
    the ADD (immediate) and load/store (unsigned offset) words whose base register may hold an
    ADRP page on some path through the function's own branches (a forward data-flow to a fixed
    point: an ADRP makes its register a page, a MOV copies one, any other write ends one, an
    unknown instruction ends all; B and B.cond, CBZ, TBZ inside the function lead to their
    target, BR (a jump table) to every word, B and RET nowhere else).  seed (k, rd): the page one
    ADRP at word k makes, alone (port/platform/state_xbuild.c adrp_users_page: code_hash.h
    ico_code_a64_flow from that word)"""
    words = len(ws)
    succ = []
    for k, w in enumerate(ws):
        pc, nxt, t = addr + 4 * k, [k + 1], None
        if w & 0xFC000000 == 0x14000000:
            t, nxt = pc + sext(w & 0x3FFFFFF, 26) * 4, []
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000:
            t = pc + sext((w >> 5) & 0x7FFFF, 19) * 4
            if w & 0xFF00001F == 0x5400000E:  # b.al
                nxt = []
        elif w & 0x7E000000 == 0x36000000:
            t = pc + sext((w >> 5) & 0x3FFF, 14) * 4
        elif w & 0xFE000000 == 0xD6000000 and (w >> 21) & 3 != 1:  # br, ret, eret (not blr)
            # br: a jump table's case, any word of the function (what a page register holds
            # there reaches every case); ret, eret: none
            nxt = list(range(words)) if ((w >> 21) & 0xF) == 0 else []
        if t is not None and addr <= t < addr + n and (t - addr) % 4 == 0:
            nxt.append((t - addr) // 4)
        succ.append([x for x in nxt if x < words])
    into = [0] * words
    seen = [False] * words
    if seed is None and words:
        seen[0] = True
    work = list(range(words)) if seed is None else [seed[0]]
    first = seed
    while work:
        k = work.pop()
        if first is None and not seen[k]:
            continue
        w, v = ws[k], into[k]
        wr = writes(w)
        if first is not None:
            out, first = 1 << seed[1], None
        elif wr == 0xFFFFFFFF:
            out = 0
        else:
            out = v & ~wr
            d = w & 31
            if seed is None and w & 0x9F000000 == 0x90000000 and d not in (29, 31):
                out |= 1 << d
            elif w & 0xFFE0FFE0 == 0xAA0003E0 and (v >> ((w >> 16) & 31)) & 1 and d not in (29, 31):
                out |= 1 << d
        for j in succ[k]:
            if not seen[j] or out & ~into[j]:
                seen[j] = True
                into[j] |= out
                work.append(j)
    return [k for k, w in enumerate(ws)
            if seen[k] and (w & 0x7F800000 == 0x11000000 or w & 0x3B000000 == 0x39000000)
            and (into[k] >> ((w >> 5) & 31)) & 1]


def mask_a64(code, addr, rules=3, text=None):
    """code_hash.c ico_code_hash_text's arm64 part: mask_a64 (rules 1) and, for rules 2 and 3,
    mask_a64_pages, by the rules' decoder; rules 3 over the words with the erratum patch sites
    read through text (a Text, None: none) replaced.  The masked words and the kept length in
    bytes"""
    n = len(code)
    words = n // 4
    raw = [rd32(code, 4 * k) for k in range(words)]
    ws = unpatch(raw, addr, text if rules >= 3 else None)[0]
    writes = a64_writes_v3 if rules >= 3 else a64_writes_v2
    pend, keep, out = 0, 0, []
    for k, w in enumerate(ws):
        pc, m = addr + 4 * k, w
        if w & 0x7C000000 == 0x14000000:
            t = pc + sext(w & 0x3FFFFFF, 26) * 4
            if t < addr or t - addr >= n:
                m = w & 0xFC000000
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000 or w & 0x3B000000 == 0x18000000:
            t = pc + sext((w >> 5) & 0x7FFFF, 19) * 4
            if t < addr or t - addr >= n:
                m = w & ~(0x7FFFF << 5) & 0xFFFFFFFF
        elif w & 0x7E000000 == 0x36000000:
            t = pc + sext((w >> 5) & 0x3FFF, 14) * 4
            if t < addr or t - addr >= n:
                m = w & ~(0x3FFF << 5) & 0xFFFFFFFF
        elif w & 0x9F000000 == 0x10000000:
            t = pc + sext(((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3), 21)
            if t < addr or t - addr >= n:
                m = w & 0x9F00001F
        elif w & 0x9F000000 == 0x90000000:
            m = w & 0x9F00001F
        elif (w & 0x7F800000 == 0x11000000 or w & 0x3B000000 == 0x39000000) and (pend >> ((w >> 5) & 31)) & 1:
            m = w & ~(0xFFF << 10) & 0xFFFFFFFF
        out.append(m)
        wr = writes(w)
        pend = 0 if wr == 0xFFFFFFFF else pend & ~wr
        if w & 0x9F000000 == 0x90000000 and (w & 31) not in (29, 31):
            pend |= 1 << (w & 31)
    for k, w in enumerate(raw):
        if w != 0 and w != 0xD503201F:
            keep = 4 * (k + 1)
    if rules >= 2:
        for k in a64_page_uses(ws, addr, n, writes=writes):
            out[k] &= ~(0xFFF << 10) & 0xFFFFFFFF
    b = b"".join(struct.pack("<I", m) for m in out) + code[4 * words:]
    if n > 4 * words:
        keep = n
    return b[:keep]


def code_hash(code, addr, rules=3, text=None):
    return xxh3_64(mask_a64(code, addr, rules, text)) if code else xxh3_64(b"")


# --- what a paused thread can hold (code_hash.c ico_code_a64_page_carried) ---

A64_CARRY = 0x1FF | (0x3FF << 19)  # x0-x8 (arguments, indirect result) and x19-x28
A64_CALL_KILL = 0x7FFFF | 1 << 30   # a call: x0-x18 and x30 hold nothing of the caller's after it

def a64_must_writes(w):
    """the registers the word surely writes (liveness: an over-claimed write would end tracking
    of a page that lives); 0 for the classes the hash's decoder over-claims"""
    if w & 0x7C000000 == 0x14000000:
        return 0
    if w & 0x0A000000 == 0x08000000:
        if w & 0x3E000000 == 0x08000000 or w & 0x3B000000 == 0x19000000:
            return 0  # exclusive, ordered, CAS; RCpc unscaled, MTE, MOPS
        if (w & 0xFFC00000) == 0xF9800000 or (w & 0xFFE00400) == 0xF8800000 or \
                (w & 0xFFE00C00) == 0xF8A00800 or (w & 0xFF000000) == 0xD8000000:
            return 0  # PRFM, PRFUM
        if w & 0x3B200C00 == 0x38200000:
            return 0  # atomics
    r = a64_writes_v3(w)
    return 0 if r == 0xFFFFFFFF else r

def a64_carries(w, regs, n, j):
    """1 when word j (entered with the page in regs) may hand it to a paused thread"""
    f = lambda b: (regs >> b) & 1
    rt, rn, rt2, rm = w & 31, (w >> 5) & 31, (w >> 10) & 31, (w >> 16) & 31
    carry = regs & A64_CARRY
    if a64_writes_v3(w) == 0xFFFFFFFF:
        return regs != 0  # not understood
    if w & 0x7C000000 == 0x14000000:
        if w & 0x80000000:
            return carry != 0  # BL
        t = j + sext(w & 0x3FFFFFF, 26)
        return not (0 <= t < n) and carry != 0  # B out of the function: a tail call
    if w & 0xFF000000 == 0x54000000 or w & 0x7E000000 == 0x34000000:
        t = j + sext((w >> 5) & 0x7FFFF, 19)
        return not (0 <= t < n) and carry != 0
    if w & 0x7E000000 == 0x36000000:
        t = j + sext((w >> 5) & 0x3FFF, 14)
        return not (0 <= t < n) and carry != 0
    if w & 0xFE000000 == 0xD6000000:
        return carry != 0  # BLR, BR (tail call or jump table), RET
    if w & 0xFFE0FFE0 == 0xAA0003E0:
        return False  # MOV: followed
    if w & 0x7F800000 == 0x11000000:  # ADD/SUB (immediate), 64-bit
        if f(rn) and ((w >> 10) & 0xFFF) == 0 and not (w >> 30) & 1:
            return False  # ADD #0: a copy, followed (copies)
        return False      # a base use: the result is no page
    if w & 0x9F000000 in (0x90000000, 0x10000000):
        return False
    if w & 0x3B000000 == 0x39000000 or w & 0x3B200000 == 0x38000000:
        store = ((w >> 22) & 3) == 0
        return bool(store and not (w >> 26) & 1 and f(rt))
    if w & 0x3B200C00 == 0x38200800:
        store = ((w >> 22) & 3) == 0
        return bool(store and not (w >> 26) & 1 and f(rt)) or bool(f(rm))
    if w & 0x3A000000 == 0x28000000:
        store = not (w >> 22) & 1
        return bool(store and not (w >> 26) & 1 and (f(rt) or f(rt2)))
    if w & 0x0A000000 == 0x08000000:
        return bool(f(rt) or f(rt2) or f(rm) or f(rn))  # other loads and stores
    if w & 0x1C000000 == 0x10000000:
        if w & 0x1F800000 == 0x12800000:
            return ((w >> 29) & 3) == 3 and bool(f(rt))
        return bool(f(rn) or (w & 0x1F800000 == 0x13800000 and f(rm)))
    if w & 0x0E000000 == 0x0A000000:
        return bool(f(rn) or f(rm) or (w & 0x1F000000 == 0x1B000000 and f(rt2)))
    if w & 0x0E000000 == 0x0E000000:
        return bool(f(rn))
    if w & 0xFFC00000 == 0xD5000000:
        return bool(f(rt))  # MSR, SYS read Rt
    return bool(f(rt) or f(rn) or f(rt2) or f(rm))

def page_carried(ws, k):
    """code_hash.c ico_code_a64_page_carried: 1 when the page the ADRP at word k of the function
    (its words ws, patch sites replaced) makes may be held by a paused thread (code_hash.h)"""
    n = len(ws)
    rd = ws[k] & 31
    into = [0] * n
    seen = [False] * n
    queued = [False] * n
    work = []

    def push(j, v):
        if 0 <= j < n and (not seen[j] or v & ~into[j]):
            seen[j] = True
            into[j] |= v
            if not queued[j]:
                queued[j] = True
                work.append(j)
    # after the ADRP
    push(k + 1, 1 << rd)
    while work:
        j = work.pop()
        queued[j] = False
        w, v = ws[j], into[j]
        if v == 0:
            continue
        if a64_carries(w, v, n, j):
            return True
        if w & 0xFC000000 == 0x94000000 or (w & 0xFE000000 == 0xD6000000 and (w >> 21) & 3 == 1):
            out = v & ~A64_CALL_KILL & ~a64_must_writes(w)
        else:
            out = v & ~a64_must_writes(w)
            d = w & 31
            if w & 0xFFE0FFE0 == 0xAA0003E0 and (v >> ((w >> 16) & 31)) & 1 and d not in (29, 31):
                out |= 1 << d
            if w & 0xFF800000 == 0x91000000 and (v >> ((w >> 5) & 31)) & 1 and ((w >> 10) & 0xFFF) == 0 and d not in (29, 31):
                out |= 1 << d  # ADD Xd, Xn, #0
        nxt = [j + 1]
        if w & 0xFC000000 == 0x14000000:
            nxt = [j + sext(w & 0x3FFFFFF, 26)]
        elif w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000:
            nxt.append(j + sext((w >> 5) & 0x7FFFF, 19))
            if w & 0xFF00001F == 0x5400000E: nxt = nxt[1:]
        elif w & 0x7E000000 == 0x36000000:
            nxt.append(j + sext((w >> 5) & 0x3FFF, 14))
        elif w & 0xFE000000 == 0xD6000000 and (w >> 21) & 3 != 1:
            # BR: a jump table's case, any word (a carrying register there escaped already);
            # RET, ERET: none
            nxt = list(range(n)) if ((w >> 21) & 0xF) == 0 else []
        for t in nxt:
            push(t, out)
    return False


CORPUS = "test/code_hash_a64_corpus.txt"  # beside this tool: the twins' corpus


def corpus_text(stubs):
    """the code range a corpus entry's stubs make: [lowest, highest + 8), zeros between"""
    if not stubs:
        return None
    lo, hi = min(stubs), max(stubs) + 8
    b = bytearray(hi - lo)
    for t, (w0, w1) in stubs.items():
        struct.pack_into("<II", b, t - lo, w0, w1)
    return Text(bytes(b), lo, hi)


def corpus_hashes(addr, ws, stubs):
    code = b"".join(struct.pack("<I", w) for w in ws)
    text = corpus_text(stubs)
    return [code_hash(code, addr, r, text) for r in (1, 2, 3)]


def read_corpus(path):
    """the corpus' entries {name: (addr, words, stubs, hashes)} and its relations"""
    ent, rel, cur = {}, [], None
    for line in open(path):
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "fn":
            cur = ent[t[1]] = [int(t[2], 16), [], {}, None]
        elif t[0] == "from":
            b = ent[t[2]]
            cur = ent[t[1]] = [int(t[3], 16), list(b[1]), dict(b[2]), None]
        elif t[0] == "w":
            cur[1].extend(int(x, 16) for x in t[1:])
        elif t[0] == "set":
            cur[1][int(t[1])] = int(t[2], 16)
        elif t[0] == "stubs":
            cur[2] = {}
        elif t[0] == "stub":
            cur[2][int(t[1], 16)] = (int(t[2], 16), int(t[3], 16))
        elif t[0] == "hash":
            cur[3] = [int(x, 16) for x in t[1:4]]
        elif t[0] in ("same", "differ"):
            rel.append((t[0], t[1], t[2], int(t[3])))
        elif t[0] == "writes":
            rel.append((t[0], int(t[1], 16), int(t[2], 16), int(t[3], 16)))
        elif t[0] == "carried":
            rel.append((t[0], t[1], int(t[2]), int(t[3])))
        else:
            raise ValueError("code_hash_a64: corpus line %r" % line)
    return ent, rel


def check_corpus(path):
    bad = []
    ent, rel = read_corpus(path)
    got = {}
    for nm, (addr, ws, stubs, want) in ent.items():
        got[nm] = corpus_hashes(addr, ws, stubs)
        if got[nm] != want:
            bad.append("corpus %s: %s, the file says %s" % (nm, got[nm], want))
    for kind, x, y, r in rel:
        if kind == "carried":
            addr, ws, stubs, _ = ent[x]
            uw = unpatch(ws, addr, corpus_text(stubs))[0]
            if int(page_carried(uw, y)) != r:
                bad.append("corpus: carried %s %d: %d, the file says %d" % (x, y, int(page_carried(uw, y)), r))
            continue
        if kind == "writes":
            if a64_writes_v3(x) != y or a64_writes_v2(x) != r:
                bad.append("corpus: writes %08x: %08x %08x, the file says %08x %08x" %
                           (x, a64_writes_v3(x), a64_writes_v2(x), y, r))
            continue
        if (got[x][r - 1] == got[y][r - 1]) != (kind == "same"):
            bad.append("corpus: rules %d should find %s and %s %s" % (r, x, y, kind))
    return bad, len(ent), len(rel)


def selftest():
    vec = {0: 0x2D06800538D394C2, 1: 0xC44BDFF4074EECDB, 6: 0x27B56A84CD2D7325, 12: 0xA713DAF0DFBB77E7,
           24: 0xA3FE70BF9D3510EB, 48: 0x397DA259ECBA1F11, 80: 0xBCDEFBBB2C47C90A, 195: 0xCD94217EE362EC3A,
           403: 0xCDEB804D65C6DEA4, 512: 0x617E49599013CB6B, 2048: 0xDD59E2C3A5F038E0,
           2099: 0xC6B9D9B3FC9AC765, 2240: 0x6E73A90539CF2948, 2367: 0xCB37AEB9E5D361ED}
    buf = bytearray(2367)
    gen = 2654435761
    for i in range(2367):
        buf[i] = gen >> 56
        gen = gen * 11400714785074694797 & M64
    bad = [n for n, h in vec.items() if xxh3_64(bytes(buf[:n])) != h]
    # a page kept in x19 across a loop: used before its ADRP's place, reached by the backward
    # branch; masked by rules 2 only
    f = struct.pack("<5I", 0x14000002, 0xF9400260, 0x90000013, 0x17FFFFFE, 0)  # b +8; ldr x0,[x19]; adrp x19; b -8
    g = struct.pack("<5I", 0x14000002, 0xF9400660, 0x90000013, 0x17FFFFFE, 0)  # ldr x0,[x19,#8]
    if mask_a64(f, 0x1000, 1) == mask_a64(g, 0x1000, 1) or mask_a64(f, 0x1000, 2) != mask_a64(g, 0x1000, 2):
        bad.append("rules 2")
    import os
    cb, ne, nr = check_corpus(os.path.join(os.path.dirname(os.path.abspath(__file__)), CORPUS))
    bad += cb
    print("code_hash_a64: the corpus' %d functions and %d relations under rules 1 to 3" % (ne, nr))
    print("code_hash_a64: %s" % ("ok" if not bad else "FAIL %s" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else 2)
