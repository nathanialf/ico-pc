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

  android_syms_lines.py gamesec MAP SRCROOT
      Exit 1, naming them, when an object outside the game's sources puts
      bytes into the game's data sections (lld's or GNU ld's link map, ELF
      ico_gdata / ico_gbss, PE .data$ico_m / .data$ico_p): a save state copies
      those sections whole and their layout hash covers every item in them,
      so a port object there would move the game's variables, or change the
      hash, with a change outside the game.  Allowed: a file of SRCROOT's
      ico2/ or ico/, port/data/gen/table_defs.c (the game's tables), and one
      tables.c of the frozen room's footprint (port/data/reserved/tables.c;
      in lld's map, which lists a file's statics, its symbols are the
      loader's five former statics).  An archive member is known by its file name only:
      one that names an ico2/ file passes.
"""
import os
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


ROOM_SIZES = (0x600, 0x614, 0x620)  # port/data/reserved/tables.c, per compiler
# its objects under the loader's former names (clang ELF), or its array
ROOM_SYMS = {"loaded_count", "loaded_set", "loaded_rows", "loaded_records", "ico_tables_crc32.table",
             "ico_tables_room_bytes"}


def game_section_objects(path):
    """[(object, input section, size, [symbols])] of the game's data sections"""
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    res = []
    if lines and " In " in lines[0] and "Symbol" in lines[0]:
        hdr = lines[0]
        inc, symc = hdr.index(" In ") + 1, hdr.index("Symbol")
        out, cur = "", None
        for line in lines[1:]:
            t = line.split()
            if len(t) < 5 or not re.fullmatch(r"[0-9a-f]+", t[0]):
                continue
            pos = line.find(t[4], line.find(t[3]) + len(t[3]))
            if pos >= symc:
                if cur is not None and not t[4].startswith("$"):
                    cur[3].append(t[4])
                continue
            cur = None
            if pos >= inc:
                m = re.fullmatch(r"(.*):\((.*)\)", line[pos:].strip())
                if m and out in ("ico_gdata", "ico_gbss") and int(t[2], 16) > 0:
                    cur = [m.group(1), m.group(2), int(t[2], 16), []]
                    res.append(cur)
            else:
                out = t[4]
        return res
    # GNU ld: " <section> 0xADDR 0xSIZE <object>", the name alone on its line
    # when long
    pending = None
    for line in lines:
        if not line.startswith(" ") or line.startswith("  "):
            if not (line.startswith(" ") and pending is not None):
                pending = None
        t = line.split()
        if line.startswith(" ") and not line.startswith("  ") and len(t) == 1:
            pending = t[0]
            continue
        if len(t) >= 4 and line.startswith(" ") and not line.startswith("  ") \
                and re.fullmatch(r"0x[0-9a-fA-F]+", t[1]) and re.fullmatch(r"0x[0-9a-fA-F]+", t[2]):
            name, size, obj = t[0], int(t[2], 16), " ".join(t[3:])
        elif pending is not None and len(t) >= 3 and re.fullmatch(r"0x[0-9a-fA-F]+", t[0]) \
                and re.fullmatch(r"0x[0-9a-fA-F]+", t[1]):
            name, size, obj = pending, int(t[1], 16), " ".join(t[2:])
        else:
            pending = None
            continue
        pending = None
        if name in ("ico_gdata", "ico_gbss", ".data$ico_m", ".data$ico_p") and size > 0:
            res.append([obj, name, size, None])
    return res


def gamesec(path, root):
    game = set()
    for top in ("ico2", "ico"):
        for d, _, files in os.walk(os.path.join(root, top)):
            game.update(f for f in files if f.endswith(".c"))
    bad, room = [], []
    for obj, sec, size, syms in game_section_objects(path):
        b = re.search(r"\(([^()]*)\)$", obj)
        member = b.group(1) if b else re.split(r"[/\\]", obj)[-1]
        src = re.sub(r"\.o(bj)?$", "", member)
        full = obj.replace("\\", "/")
        if b is None and ("/ico2/" in full or "/ico/" in full):
            continue
        if src in game or src == "table_defs.c":
            continue
        if src == "tables.c" and size in ROOM_SIZES and (syms is None or set(syms) <= ROOM_SYMS):
            room.append(obj)
            continue
        bad.append("%s %s 0x%x%s" % (obj, sec, size, (" (" + ", ".join(syms[:4]) + ")") if syms else ""))
    if len(room) > 1:
        bad += ["%s (a second tables.c)" % o for o in room[1:]]
    if bad:
        print("android_syms_lines: %d objects outside the game's sources hold bytes in the game's "
              "data sections:" % len(bad))
        for x in bad:
            print("  " + x)
        sys.exit(1)
    print("android_syms_lines: the game's data sections hold the game's objects only%s" %
          (" (and the loader's frozen room)" if room else ""))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "relro":
        relro_lines(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        check(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "gamesec":
        gamesec(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
