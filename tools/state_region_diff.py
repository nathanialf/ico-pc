#!/usr/bin/env python3
"""tools/state_region_diff.py A_DIR B_DIR MAP [--elf PROGRAM] [--max N] [--selftest]

Compares two dumps of the game's memory that [dev] state_dump_at wrote
(port/game/state_host.c dev_dump: <states folder>/dump-<tick>-<k>/ with
arena.bin, gdata.bin, gbss.bin, regions.txt and masked.txt) and names where
they differ, outside the ranges either dump's hash left out (masked.txt: the
host's pointers and handles, docs/port/PLATFORM.md "Save states", the hash
rule).

The game's data and bss are mapped to symbols through the program's link map
(MAP: the .map the link writes beside ico_pc, GNU ld's or lld's format, the
one port/platform/symmap.c reads): the map's address of a byte is its address
in the dump less the program's load bias, which regions.txt gives through the
run-time address of systemStatus. A link map lists global symbols only; with
--elf the program's own symbol table (nm -S, when nm is on the PATH) adds the
static ones and their sizes. The arena has no symbols: its ranges are given
as offsets, which are the EE addresses.

Prints the regions' hashes, then the first N (default 10) differing symbols
of the game's data and bss in address order (each with its object file, the
bytes that differ, the first offset and the first bytes of each dump), then
the arena's first N differing ranges. Exit 0 when nothing differs outside the
masked ranges, 1 when something does, 2 on a usage or read error.

--selftest builds two dumps and a map in a temporary folder and checks the
report (no game needed).
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

REGIONS = ("arena", "gdata", "gbss")
SECTION_OF = {"gdata": ("ico_gdata", ".data$ico_m"), "gbss": ("ico_gbss", ".data$ico_p")}
BIAS_SYMBOLS = ("systemStatus", "stage_no")


def die(msg):
    print("state_region_diff: " + msg, file=sys.stderr)
    sys.exit(2)


def read_regions(d):
    """regions.txt: {name: (base, bytes, hash)}, {symbol: address}, header"""
    path = os.path.join(d, "regions.txt")
    regions, symbols, head = {}, {}, {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                w = line.split()
                if not w or w[0].startswith("#"):
                    continue
                if w[0] == "region" and len(w) >= 5:
                    regions[w[1]] = (int(w[2], 16), int(w[3]), w[4])
                elif w[0] == "symbol" and len(w) >= 3:
                    symbols[w[1]] = int(w[2], 16)
                elif len(w) >= 2:
                    head[w[0]] = " ".join(w[1:])
    except OSError as e:
        die(f"cannot read {path}: {e}")
    for r in REGIONS:
        if r not in regions:
            die(f"{path} has no region {r}")
    return regions, symbols, head


def read_masked(d):
    """masked.txt: {region: [(off, end)]}"""
    out = {r: [] for r in REGIONS}
    path = os.path.join(d, "masked.txt")
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                w = line.split()
                if len(w) >= 3 and w[0] in out:
                    off = int(w[1], 16)
                    out[w[0]].append((off, off + int(w[2])))
    except OSError as e:
        die(f"cannot read {path}: {e}")
    return out


def merge(ranges):
    ranges = sorted(ranges)
    out = []
    for lo, hi in ranges:
        if out and lo <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], hi))
        else:
            out.append((lo, hi))
    return out


def diff_ranges(a, b, masked, gap=8):
    """the differing byte ranges [lo, hi) of a and b outside masked (sorted,
    merged), close ones (gap bytes apart or less) joined"""
    n = min(len(a), len(b))
    found = []

    def walk(lo, hi):
        if a[lo:hi] == b[lo:hi]:
            return
        if hi - lo <= 64:
            i = lo
            while i < hi:
                if a[i] != b[i]:
                    j = i
                    while j < hi and a[j] != b[j]:
                        j += 1
                    found.append((i, j))
                    i = j
                else:
                    i += 1
            return
        mid = (lo + hi) // 2
        walk(lo, mid)
        walk(mid, hi)

    step = 1 << 16
    for lo in range(0, n, step):
        walk(lo, min(n, lo + step))
    if len(a) != len(b):
        found.append((n, max(len(a), len(b))))
    # take the masked bytes out
    out = []
    m = merge(masked)
    k = 0
    for lo, hi in found:
        while k < len(m) and m[k][1] <= lo:
            k += 1
        j = k
        cur = lo
        while cur < hi:
            if j < len(m) and m[j][0] <= cur < m[j][1]:
                cur = m[j][1]
                j += 1
                continue
            end = hi
            if j < len(m) and m[j][0] < hi:
                end = max(cur, m[j][0])
            if end > cur:
                out.append((cur, end))
            cur = end
    joined = []
    for lo, hi in out:
        if joined and lo - joined[-1][1] <= gap:
            joined[-1] = (joined[-1][0], hi)
        else:
            joined.append((lo, hi))
    return joined


MAP_IN_GNU = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
MAP_IN_GNU_NAME = re.compile(r"^ (\S+)$")
MAP_IN_GNU_REST = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$")
MAP_SYM_GNU = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_.$][\w.$@]*)\s*$")
MAP_LLD = re.compile(r"^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\d+)(\s+)(\S.*)$")


def short_obj(text):
    """an archive member's name (libico_game.a(main.c.o) -> main.c.o), else
    the file's"""
    m = re.search(r"\(([^()]+)\)$", text.strip())
    return m.group(1) if m else os.path.basename(text.strip())


def read_map(path):
    """the link map's game data input sections [(addr, size, object)] and
    global symbols {name: addr}, [(addr, name, object)]"""
    sections, syms, names = [], [], {}
    try:
        f = open(path, encoding="utf-8", errors="replace")
    except OSError as e:
        die(f"cannot read the map {path}: {e}")
    wanted = {s for pair in SECTION_OF.values() for s in pair}
    with f:
        lines = f.read().splitlines()
    lld = any(l.lstrip().startswith("VMA ") for l in lines[:5])
    cur_obj = None
    pending = None
    for line in lines:
        if lld:
            m = MAP_LLD.match(line)
            if not m:
                continue
            addr, size, rest = int(m.group(1), 16), int(m.group(3), 16), m.group(6).strip()
            sec = re.search(r":\(([^)]+)\)$", rest)
            if sec:
                cur_obj = short_obj(rest[: sec.start()]) if sec.group(1) in wanted else None
                if cur_obj is not None:
                    sections.append((addr, size, cur_obj))
            elif cur_obj is not None and len(m.group(5)) > 8 and re.match(r"^[A-Za-z_.$][\w.$@]*$", rest):
                syms.append((addr, rest, cur_obj))
                names[rest] = addr
            continue
        if pending is not None:
            m = MAP_IN_GNU_REST.match(line)
            if m:
                cur_obj = short_obj(m.group(3))
                sections.append((int(m.group(1), 16), int(m.group(2), 16), cur_obj))
            pending = None
            continue
        m = MAP_IN_GNU.match(line)
        if m:
            cur_obj = None
            if m.group(1) in wanted:
                cur_obj = short_obj(m.group(4))
                sections.append((int(m.group(2), 16), int(m.group(3), 16), cur_obj))
            continue
        m = MAP_IN_GNU_NAME.match(line)
        if m:
            cur_obj = None
            pending = m.group(1) if m.group(1) in wanted else None
            continue
        if not line.startswith(" ") and line.strip():
            cur_obj = None  # a new output section
            continue
        m = MAP_SYM_GNU.match(line)
        if m and cur_obj is not None:
            a = int(m.group(1), 16)
            syms.append((a, m.group(2), cur_obj))
            names[m.group(2)] = a
    # symbols outside the game's data sections still give the bias
    if not all(s in names for s in BIAS_SYMBOLS):
        for line in lines:
            m = MAP_SYM_GNU.match(line)
            if m and m.group(2) in BIAS_SYMBOLS:
                names.setdefault(m.group(2), int(m.group(1), 16))
    sections.sort()
    syms.sort()
    return sections, syms, names


def read_nm(program):
    """the program's data symbols with sizes [(addr, size, name)] (nm -S),
    statics included; [] without nm or symbols"""
    nm = shutil.which("nm")
    if not program or nm is None or not os.path.exists(program):
        return []
    try:
        p = subprocess.run([nm, "-S", "-n", "--defined-only", program], capture_output=True,
                           text=True, timeout=120)
    except (OSError, subprocess.SubprocessError):
        return []
    out = []
    for line in p.stdout.splitlines():
        w = line.split()
        if len(w) == 4 and w[2] in "bBdDsS":
            try:
                out.append((int(w[0], 16), int(w[1], 16), w[3]))
            except ValueError:
                pass
    return out


def find_le(items, addr):
    """the last item whose first field is <= addr (items sorted), or None"""
    lo, hi = 0, len(items)
    while lo < hi:
        mid = (lo + hi) // 2
        if items[mid][0] <= addr:
            lo = mid + 1
        else:
            hi = mid
    return items[lo - 1] if lo else None


def name_of(addr, sections, syms, nm_syms):
    """(name, offset in it) for a link-time address: a symbol with its
    object file, an object's static data, or the bare address"""
    obj = ""
    s = find_le(sections, addr)
    if s and addr < s[0] + s[1]:
        obj = s[2]
    tail = f" ({obj})" if obj else ""
    n = find_le(nm_syms, addr)
    if n and addr < n[0] + max(n[1], 1):
        return n[2] + tail, addr - n[0]
    g = find_le(syms, addr)
    if g and (not s or g[0] >= s[0]):
        return g[1] + tail, addr - g[0]
    if s and obj:
        return f"{obj}'s static data", addr - s[0]
    return f"{addr:#x}", 0


def hexbytes(b, n=16):
    return b[:n].hex() + ("..." if len(b) > n else "")


def report(a_dir, b_dir, map_path, program=None, maxn=10, out=sys.stdout):
    ra, sa, ha = read_regions(a_dir)
    rb, sb, hb = read_regions(b_dir)
    ma, mb = read_masked(a_dir), read_masked(b_dir)
    sections, syms, names = read_map(map_path)
    nm_syms = read_nm(program)
    bias = None
    for s in BIAS_SYMBOLS:
        if s in sa and s in names:
            bias = sa[s] - names[s]
            break
    print(f"state_region_diff: {a_dir} (tick {ha.get('tick', '?')}, hash {ha.get('hash', '?')}) "
          f"against {b_dir} (tick {hb.get('tick', '?')}, hash {hb.get('hash', '?')})", file=out)
    any_diff = False
    sym_lines = []
    arena_lines = []
    for r in REGIONS:
        try:
            with open(os.path.join(a_dir, r + ".bin"), "rb") as f:
                da = f.read()
            with open(os.path.join(b_dir, r + ".bin"), "rb") as f:
                db = f.read()
        except OSError as e:
            die(f"cannot read a dump: {e}")
        diffs = diff_ranges(da, db, ma[r] + mb[r])
        nbytes = sum(hi - lo for lo, hi in diffs)
        print(f"  {r}: {len(da)} bytes, masked hash {ra[r][2]} / {rb[r][2]}: "
              f"{len(diffs)} differing range(s), {nbytes} byte(s)", file=out)
        if not diffs:
            continue
        any_diff = True
        if r == "arena":
            for lo, hi in diffs[:maxn]:
                arena_lines.append(f"    arena+{lo:#x} (EE {lo:#010x}): {hi - lo} byte(s): "
                                   f"{hexbytes(da[lo:hi])} / {hexbytes(db[lo:hi])}")
            continue
        seen = {}
        for lo, hi in diffs:
            if bias is None:
                key, at = f"{r}+{lo:#x} (no bias: neither systemStatus nor stage_no in the map)", 0
            else:
                key, at = name_of(ra[r][0] + lo - bias, sections, syms, nm_syms)
            ent = seen.setdefault(key, [lo, at, 0, da[lo:hi], db[lo:hi]])
            ent[2] += hi - lo
        for key, (lo, at, nb, xa, xb) in seen.items():
            sym_lines.append(f"    {r} {key}: {nb} byte(s) differ, the first at +{at:#x} "
                             f"({r}+{lo:#x}): {hexbytes(xa)} / {hexbytes(xb)}")
    if sym_lines:
        print(f"  the first {min(maxn, len(sym_lines))} differing symbols of the game's data "
              f"and bss ({len(sym_lines)} in all):", file=out)
        for line in sym_lines[:maxn]:
            print(line, file=out)
    if arena_lines:
        print(f"  the arena's first {len(arena_lines)} differing ranges:", file=out)
        for line in arena_lines:
            print(line, file=out)
    if not any_diff:
        print("  nothing differs outside the masked ranges", file=out)
    return 1 if any_diff else 0


def selftest():
    tmp = tempfile.mkdtemp(prefix="state_region_diff_")
    try:
        mp = os.path.join(tmp, "ico_pc.map")
        with open(mp, "w") as f:
            f.write("ico_gdata       0x0000000000001000      0x40\n"
                    " ico_gdata      0x0000000000001000       0x20 libico_game.a(main.c.o)\n"
                    "                0x0000000000001000                systemStatus\n"
                    "                0x0000000000001010                stage_no\n"
                    " ico_gdata      0x0000000000001020       0x20 libico_game.a(camera.c.o)\n"
                    "\n"
                    "ico_gbss        0x0000000000002000      0x40\n"
                    " ico_gbss       0x0000000000002000       0x40 libico_game.a(PObj.c.o)\n"
                    "                0x0000000000002000                pobjThing\n")
        bias = 0x555500000000
        for k, tweak in ((1, False), (2, True)):
            d = os.path.join(tmp, f"dump-700-{k}")
            os.makedirs(d)
            arena = bytearray(4096)
            gdata = bytearray(0x40)
            gbss = bytearray(0x40)
            if tweak:
                arena[0x100:0x104] = b"\x01\x02\x03\x04"
                gdata[0x12] = 7          # stage_no
                gdata[0x28] = 9          # camera.c.o's static data
                gbss[0x8:0x10] = b"\xff" * 8  # masked in this dump only
            for name, data in (("arena", arena), ("gdata", gdata), ("gbss", gbss)):
                with open(os.path.join(d, name + ".bin"), "wb") as f:
                    f.write(data)
            with open(os.path.join(d, "regions.txt"), "w") as f:
                f.write(f"tick 700\nhash {k:016x}\n"
                        f"region arena 0x3c00000000 4096 {k:016x}\n"
                        f"region gdata {bias + 0x1000:#x} 64 {k:016x}\n"
                        f"region gbss {bias + 0x2000:#x} 64 {k:016x}\n"
                        f"symbol systemStatus {bias + 0x1000:#x}\n")
            with open(os.path.join(d, "masked.txt"), "w") as f:
                if tweak:
                    f.write("gbss 0x8 8 block-pointer\n")
        buf = io.StringIO()
        rc = report(os.path.join(tmp, "dump-700-1"), os.path.join(tmp, "dump-700-2"), mp,
                    None, 10, buf)
        text = buf.getvalue()
        ok = (rc == 1 and "gdata stage_no (main.c.o): 1 byte(s) differ, the first at +0x2" in text
              and "gdata camera.c.o's static data: 1 byte(s) differ, the first at +0x8" in text
              and "arena+0x100 (EE 0x00000100): 4 byte(s)" in text
              and "gbss: 64 bytes, masked hash 0000000000000001 / 0000000000000002: "
                  "0 differing range(s)" in text
              and "pobjThing" not in text)
        buf2 = io.StringIO()
        rc2 = report(os.path.join(tmp, "dump-700-1"), os.path.join(tmp, "dump-700-1"), mp, None,
                     10, buf2)
        ok = ok and rc2 == 0 and "nothing differs" in buf2.getvalue()
        # the range arithmetic: masked pieces cut out, close ranges joined
        a = bytes(64)
        b = bytearray(64)
        b[0:4] = b"\x01" * 4
        b[10:12] = b"\x01" * 2
        b[40:48] = b"\x01" * 8
        ok = ok and diff_ranges(a, bytes(b), [(40, 48)]) == [(0, 12)]
        ok = ok and diff_ranges(a, bytes(b), [(2, 3)]) == [(0, 12), (40, 48)]
        print(text if not ok else "", end="")
        print("state_region_diff selftest: " + ("ok" if ok else "FAILED"))
        return 0 if ok else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    ap = argparse.ArgumentParser(
        prog="state_region_diff.py",
        description="Compares two [dev] state_dump_at dumps of the game's memory outside the "
                    "ranges their hash left out and names the differing bytes by symbol.")
    ap.add_argument("a_dir", nargs="?", help="the first dump's folder (dump-<tick>-1)")
    ap.add_argument("b_dir", nargs="?", help="the second dump's folder (dump-<tick>-2)")
    ap.add_argument("map", nargs="?", help="the program's link map (ico_pc.map)")
    ap.add_argument("--elf", metavar="PROGRAM", help="the program: nm -S adds the statics")
    ap.add_argument("--max", type=int, default=10, help="symbols and ranges listed (10)")
    ap.add_argument("--selftest", action="store_true", help="check the tool on made-up dumps")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not (a.a_dir and a.b_dir and a.map):
        ap.print_usage(sys.stderr)
        return 2
    return report(a.a_dir, a.b_dir, a.map, a.elf, a.max)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
