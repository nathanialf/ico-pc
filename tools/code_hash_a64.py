"""tools/code_hash_a64.py -- port/platform/code_hash.c's arm64 code hash, rules 2, and
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


def a64_writes(w):
    """code_hash.c ico_code_a64_writes"""
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


def a64_page_uses(ws, addr, n, seed=None):
    """code_hash.c a64_page_uses (rules 2): the ADD (immediate) and load/store (unsigned offset)
    words whose base register may hold an ADRP page on some path through the function's own
    branches (a forward data-flow to a fixed point: an ADRP makes its register a page, a MOV
    copies one, any other write ends one, an unknown instruction ends all; B and B.cond, CBZ,
    TBZ inside the function lead to their target, BR (a jump table) to every word, B and RET
    nowhere else).  seed (k, rd): the page one ADRP at word k makes, alone (port/platform/
    state_xbuild.c adrp_users_page: code_hash.h ico_code_a64_flow from that word)"""
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
        wr = a64_writes(w)
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


def mask_a64(code, addr, rules=2):
    """code_hash.c mask_a64 (rules 1) and, for rules 2, mask_a64_pages: the masked words and
    the kept length in bytes"""
    n = len(code)
    words = n // 4
    ws = [rd32(code, 4 * k) for k in range(words)]
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
        wr = a64_writes(w)
        pend = 0 if wr == 0xFFFFFFFF else pend & ~wr
        if w & 0x9F000000 == 0x90000000 and (w & 31) not in (29, 31):
            pend |= 1 << (w & 31)
        if w != 0 and w != 0xD503201F:
            keep = 4 * (k + 1)
    if rules >= 2:
        for k in a64_page_uses(ws, addr, n):
            out[k] &= ~(0xFFF << 10) & 0xFFFFFFFF
    b = b"".join(struct.pack("<I", m) for m in out) + code[4 * words:]
    if n > 4 * words:
        keep = n
    return b[:keep]


def code_hash(code, addr, rules=2):
    return xxh3_64(mask_a64(code, addr, rules)) if code else xxh3_64(b"")


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
    print("code_hash_a64: %s" % ("ok" if not bad else "FAIL %s" % bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else 2)
