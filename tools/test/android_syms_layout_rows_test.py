"""tools/test/android_syms_layout_rows_test.py -- the Windows layout gate's cause line
(tools/android_syms_lines.py layout ... MAP against port/state_legacy/layout-windows-x64.txt)
and the start pad that keeps the game's data sections where v0.11.8 had them
(port/platform/game_data_range.c).

The recorded rows parse (a start, the largest alignment, ascending offsets); the pad's
alignment is that largest alignment and, wherever the port's .data before it ends, it puts
.data$ico_a at the recorded start's residue. A GNU ld map made from the rows reads back as
the rows (layout-rows). Against a failing hash: the same map names "the same objects at
other offsets: the range's start alignment" only when the bss sections after a 64-aligned
one moved (v0.11.9-rc8's 0x20, kanbanBoot.c.obj first), another size names the object that
differs, the same offsets say an item inside an object changed; a matching hash passes with
no cause line. The files are written under $TMPDIR."""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "android_syms_lines.py")
ROWS = os.path.join(ROOT, "port", "state_legacy", "layout-windows-x64.txt")
RANGE_C = os.path.join(ROOT, "port", "platform", "game_data_range.c")
SECTION_ALIGN = 16  # mingw gas: a .data section with no .align of its own (the brackets')


def recorded(key):
    for line in open(os.path.join(ROOT, "port", "state_legacy", "layout.txt")):
        t = line.split()
        if len(t) == 2 and t[0] == key:
            return t[1]
    sys.exit("android_syms_layout_rows_test: port/state_legacy/layout.txt records no %s hash" % key)


def read_rows():
    start = align = None
    rows = []
    for line in open(ROWS):
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "start":
            start, align = int(t[1], 16), int(t[3])
        else:
            rows.append((int(t[0]), int(t[1], 16), int(t[2], 16), t[3], t[4]))
    return start, align, rows


def up(x, a):
    return -(-x // a) * a


def make_map(start, rows, shift_from=None, shift=0):
    """GNU ld's PE map lines of the rows: the brackets at start, the bss bracket after the
    last data row; rows from index shift_from on moved by shift"""
    data = [r for r in rows if r[0] == 0]
    n = up(start + max(r[1] + r[2] for r in data), SECTION_ALIGN)
    out = ["Linker script and memory map\n", ".data           0x%016x   0x100000\n" % (start - 0x100),
           " .data$ico_a    0x%016x       0x10 libico_platform.a(game_data_range.c.obj)\n" % start]
    for i, (r, off, size, sec, obj) in enumerate(rows):
        if r == 1 and (i == 0 or rows[i - 1][0] == 0):
            out.append(" .data$ico_n    0x%016x       0x10 libico_platform.a(game_data_range.c.obj)\n" % n)
        d = shift if shift_from is not None and i >= shift_from else 0
        out.append(" %-14s 0x%016x %10s libico_game.a(%s)\n" % (sec, (start if r == 0 else n) + off + d,
                                                             "0x%x" % size, obj))
    return "".join(out)


def run(args, files):
    paths = []
    try:
        for text, suffix in files:
            fd, path = tempfile.mkstemp(suffix=suffix)
            with os.fdopen(fd, "w") as f:
                f.write(text)
            paths.append(path)
        p = subprocess.run([sys.executable, "-I", TOOL] + [a(paths) if callable(a) else a for a in args],
                           capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr
    finally:
        for path in paths:
            os.remove(path)


def gate(carry_hash, map_text):
    report = "state_carry_check: x\n  symbols units 1 data 1 layout %s sections 1 code 1 2\n" % carry_hash
    return run(["layout", lambda p: p[0], ROOT, "windows-x64", lambda p: p[1]],
               [(report, "-carry.txt"), (map_text, ".map")])


def main():
    bad = []
    start, align, rows = read_rows()
    if start is None or not align or len(rows) < 2:
        sys.exit("android_syms_layout_rows_test: %s has no start line or rows" % ROWS)
    for r in (0, 1):
        offs = [x[1] for x in rows if x[0] == r]
        if offs != sorted(offs) or not offs:
            bad.append("the recorded rows of range %d are not in ascending order" % r)
    # the pad (game_data_range.c): the largest alignment, and .data$ico_a at the recorded residue
    src = open(RANGE_C).read()
    m_al = re.search(r"#define ICO_GDR_PE_PAD_ALIGN (\d+)", src)
    m_sz = re.search(r"#define ICO_GDR_PE_PAD_SIZE (\d+)", src)
    if not m_al or not m_sz or not re.search(r'section\("\.data\$ico_0"\)', src):
        bad.append("game_data_range.c: no start pad (ICO_GDR_PE_PAD_ALIGN/_SIZE in .data$ico_0)")
    else:
        pa, ps = int(m_al.group(1)), int(m_sz.group(1))
        if pa != align:
            bad.append("the pad's alignment %d is not the recorded largest alignment %d" % (pa, align))
        for end in range(0, 2 * pa, 8):  # wherever the port's .data before the pad ends
            a = up(up(end, pa) + ps, SECTION_ALIGN)
            if a % align != start % align:
                bad.append("with the port's .data ending at +%d, .data$ico_a starts %d past a "
                           "multiple of %d, not the recorded %d" % (end, a % align, align, start % align))
                break
    want = recorded("layout-windows-x64")
    same = make_map(start, rows)
    # the map made from the rows reads back as the rows
    rc, out = run(["layout-rows", lambda p: p[0]], [(same, ".map")])
    got = [tuple(int(x, 0) if i < 3 else x for i, x in enumerate(line.split()))
           for line in out.splitlines()[1:]]
    if rc != 0 or got != rows or out.split()[1] != "%#x" % start:
        bad.append("layout-rows on the rows' map: exit %d, %d rows of %d" % (rc, len(got), len(rows)))
    # a failing hash with the bss sections from the first 64-aligned one 0x20 earlier
    data = [r for r in rows if r[0] == 0]
    n = up(start + max(r[1] + r[2] for r in data), SECTION_ALIGN)
    first = next((i for i, r in enumerate(rows) if i > 0 and r[0] == 1 and rows[i - 1][0] == 1 and
                  (n + r[1]) % align == 0 and r[1] - 0x20 >= rows[i - 1][1] + rows[i - 1][2]), None)
    if first is None:
        bad.append("the recorded rows have no 64-aligned bss section after a 0x20 gap")
    else:
        rc, out = gate("43e0c1ff1eb9a6d0", make_map(start + 0x20, rows, first, -0x20))
        if rc == 0 or "the same objects at other offsets: the range's start alignment" not in out or \
                rows[first][4] not in out or "0x20 past a multiple of %d" % align not in out:
            bad.append("the moved objects: exit %d: %s" % (rc, out))
    # another size
    bigger = [r if i != 1 else (r[0], r[1], r[2] + 0x10, r[3], r[4]) for i, r in enumerate(rows)]
    rc, out = gate("43e0c1ff1eb9a6d0", make_map(start, bigger))
    if rc == 0 or "differs from" not in out or rows[1][4] not in out:
        bad.append("another size: exit %d: %s" % (rc, out))
    # the same offsets and sizes
    rc, out = gate("43e0c1ff1eb9a6d0", same)
    if rc == 0 or "an item inside one of them changed" not in out:
        bad.append("the same objects where they were: exit %d: %s" % (rc, out))
    # the recorded hash passes and names no cause
    rc, out = gate(want, make_map(start + 0x20, rows, first or 0, -0x20))
    if rc != 0 or "other offsets" in out:
        bad.append("the recorded hash: exit %d: %s" % (rc, out))
    for b in bad:
        print("android_syms_layout_rows_test: FAIL " + b)
    print("android_syms_layout_rows_test: %s (%d rows, start 0x%x, align %d)" %
          ("ok" if not bad else "%d failed" % len(bad), len(rows), start, align))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
