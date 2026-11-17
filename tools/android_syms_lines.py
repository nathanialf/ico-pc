#!/usr/bin/env python3
"""tools/android_syms_lines.py -- two steps of libmain.syms (tools/package_android.sh)
for the save states' symbol table (port/platform/state_syms.h).

  android_syms_lines.py relro MAP
      Prints an nm data line ("<address> <size> d <name> <file>") for every
      input section of .data.rel.ro in lld's link map that holds no symbol:
      the compiler's private objects (switch lookup tables
      .Lswitch.table.<function>, aggregate initialisers .L__const.<function>.<var>,
      compound literals), which nm cannot name.  The name is the section's
      without ".data.rel.ro." and its trailing ".<number>" (a counter of the
      object file that moves when the file changes), the file the object's
      (its archive member's) without ".o"; two of one name in one file are
      told apart by their order and their contents (state_xbuild.h).

  android_syms_lines.py check SYMS
      Exit 1, naming them, when two data symbols of one name ("<file>:<name>"
      when the line carries a file) lie at different addresses in writable
      data that is not read-only after relocation (a section with W, not
      .data.rel.ro*, not the game's two ranges, which move whole): their
      contents change while the game runs, so nothing can tell them apart
      and a save state holding a pointer into one could not carry over.
      Give one of them another name.
"""
import re
import sys


def relro_lines(path):
    it = iter(open(path, encoding="utf-8", errors="replace"))
    hdr = next(it, "")
    if " In " not in hdr or "Symbol" not in hdr:
        sys.exit("android_syms_lines: %s is not lld's map" % path)
    inc, symc = hdr.index(" In ") + 1, hdr.index("Symbol")
    out, cur, res = "", None, []

    def flush():
        if cur is not None and not cur[4] and cur[1] > 0 and out.startswith(".data.rel.ro"):
            res.append(cur[:4])

    for line in it:
        t = line.split()
        if len(t) < 5 or not re.fullmatch(r"[0-9a-f]+", t[0]):
            continue
        pos = line.find(t[4], line.find(t[3]) + len(t[3]))
        if pos >= symc:
            if cur is not None and not t[4].startswith("$"):
                cur[4] = True  # a symbol (not an ARM mapping symbol) names it
            continue
        flush()
        cur = None
        if pos >= inc:
            m = re.fullmatch(r"(.*):\((.*)\)", line[pos:].strip())
            if m:
                obj, sec = m.groups()
                b = re.search(r"\(([^()]*)\)$", obj)
                obj = b.group(1) if b else re.split(r"[/\\]", obj)[-1]
                obj = re.sub(r"\.o(bj)?$", "", obj)
                stem = re.sub(r"\.\d+$", "", re.sub(r"^\.data\.rel\.ro\.", "", sec))
                cur = [int(t[0], 16), int(t[2], 16), stem, obj, False]
        else:
            out = t[4]
    flush()
    for a, s, stem, obj in res:
        print("%016x %016x d %s %s" % (a, s, stem, obj))


def check(path):
    secs, data = [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.split()
        if len(t) == 5 and t[0] == "section":
            secs.append((int(t[1], 16), int(t[2], 16), t[3], t[4]))
        elif len(t) in (4, 5) and len(t[2]) == 1 and t[2] in "DdBbVv":
            name = t[4] + ":" + t[3] if len(t) == 5 else t[3]
            data.append((int(t[0], 16), name))
    secs.sort()
    starts = [s[0] for s in secs]
    import bisect
    where = {}
    for a, name in data:
        i = bisect.bisect_right(starts, a) - 1
        if i < 0 or a >= secs[i][0] + secs[i][1]:
            continue
        flags, sname = secs[i][2], secs[i][3]
        if "W" not in flags or sname.startswith(".data.rel.ro") or sname in ("ico_gdata",
                                                                            "ico_gbss"):
            continue
        where.setdefault(name, set()).add(a)
    bad = sorted(n for n, a in where.items() if len(a) > 1)
    if not secs:
        sys.exit("android_syms_lines: %s has no section lines" % path)
    if bad:
        print("android_syms_lines: %d writable data names repeat (a save state could not tell "
              "them apart): %s" % (len(bad), ", ".join(bad[:20]) + (", ..." if len(bad) > 20
                                                                     else "")))
        sys.exit(1)
    print("android_syms_lines: %d writable data symbols, every name unique" % len(where))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "relro":
        relro_lines(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        check(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
