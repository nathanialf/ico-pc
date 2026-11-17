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
      hash, with a change outside the game.  Exit 1 too when the map shows
      no object in those sections (a map of another format, or a link
      without them) or no reserved room (port/data/reserved/tables.c: its
      loss moved the game's bss in v0.11.9-rc4).  Allowed: a file of SRCROOT's
      ico2/ or ico/, port/data/gen/table_defs.c (the game's tables), and one
      tables.c of the frozen room's footprint (port/data/reserved/tables.c;
      in lld's map, which lists a file's statics, its symbols are the
      loader's five former statics).  An archive member is known by its file name only:
      one that names an ico2/ file passes.

  android_syms_lines.py layout CARRY SRCROOT [PLATFORM]
      Exit 1 when the game data layout hash the carry-over guard's report
      CARRY prints ("symbols ... layout <hash>") is not the one
      SRCROOT/port/state_legacy/layout.txt records for PLATFORM ("layout"
      for android, the default; "layout-windows-x64", "layout-linux-arm64"):
      the earlier versions' states resume exactly only under that hash (on
      Android the ones the legacy tables read, port/state_legacy; on Windows
      the earlier versions' own records).  A platform recorded "none" has no
      earlier version whose states resume exactly: the hash is printed, to
      record after the release that ships it.  A difference with no change to the game's data is a
      defect of the build (v0.11.9-rc4: the reserved room left out of the
      link); with one made on purpose, the maintainer records the new hash
      there knowing those states then take the checkpoint path.

  android_syms_lines.py layout CARRY SRCROOT PLATFORM MAP
      The same, and on a difference the cause read from the program's link
      map MAP (GNU ld's) against the game data sections' objects the
      recorded hash's build had (port/state_legacy/layout-PLATFORM.txt, when
      it exists): the same objects at other offsets (the range's start
      alignment, port/platform/game_data_range.c's start pad), other objects
      or sizes (the first named), or the same objects where they were (an
      item inside one changed).

  android_syms_lines.py layout-rows MAP
      Prints those objects of MAP in the form layout-PLATFORM.txt keeps:
      "start <address of the range> align <largest alignment>" and one line
      per non-empty input section, "<range> <offset> <size> <section>
      <object>", the offset from its range's start (the hash's,
      port/platform/state_syms.c); record it with the hash.
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
    objs = game_section_objects(path)
    for obj, sec, size, syms in objs:
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
    if not objs:
        print("android_syms_lines: %s shows no object in the game's data sections (ico_gdata, "
              "ico_gbss, .data$ico_m, .data$ico_p): not a link map of the program, or its game "
              "data is missing" % path)
        sys.exit(1)
    if not room:
        print("android_syms_lines: the reserved room (port/data/reserved/tables.c, %s bytes) is not "
              "in the game's data sections of %s: the game's variables after it moved" %
              (" or ".join("0x%x" % n for n in ROOM_SIZES), path))
        sys.exit(1)
    if bad:
        print("android_syms_lines: %d objects outside the game's sources hold bytes in the game's "
              "data sections:" % len(bad))
        for x in bad:
            print("  " + x)
        sys.exit(1)
    print("android_syms_lines: the game's data sections hold the game's objects only%s" %
          (" (and the loader's frozen room)" if room else ""))


PE_ALIGN = 64  # the largest alignment of a game object's PE data section (game_data_range.c)


def layout_rows(path):
    """(start, rows) of GNU ld's map: rows [(range, offset, size, section, object)] of the game's
    data sections in map order, offsets from the range's start (PE: the .data$ico_a and
    .data$ico_n brackets; ELF: the ico_gdata and ico_gbss output sections); start the data
    range's address. ([], None) for a map without them."""
    lo = {}
    secs = []
    pending = None
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.split()
        hexs = [x for x in t[:3] if re.fullmatch(r"0x[0-9a-fA-F]+", x)]
        if not line.startswith(" ") and len(t) >= 3 and t[0] in ("ico_gdata", "ico_gbss") and \
                len(hexs) == 2:
            lo.setdefault(t[0], int(t[1], 16))
            pending = None
            continue
        if line.startswith(" ") and not line.startswith("  ") and len(t) == 1:
            pending = t[0]
            continue
        if line.startswith(" ") and not line.startswith("  ") and len(t) >= 4 and \
                re.fullmatch(r"0x[0-9a-fA-F]+", t[1]) and re.fullmatch(r"0x[0-9a-fA-F]+", t[2]):
            name, addr, size, obj = t[0], int(t[1], 16), int(t[2], 16), " ".join(t[3:])
        elif pending is not None and len(t) >= 3 and re.fullmatch(r"0x[0-9a-fA-F]+", t[0]) and \
                re.fullmatch(r"0x[0-9a-fA-F]+", t[1]):
            name, addr, size, obj = pending, int(t[0], 16), int(t[1], 16), " ".join(t[2:])
        else:
            pending = None
            continue
        pending = None
        if name in (".data$ico_a", ".data$ico_n") and "game_data_range" in obj:
            lo.setdefault(name, addr)
        if name in ("ico_gdata", "ico_gbss", ".data$ico_m", ".data$ico_p") and size > 0:
            b = re.search(r"\(([^()]*)\)$", obj)
            secs.append((name, addr, size, b.group(1) if b else re.split(r"[/\\]", obj)[-1]))
    base = {".data$ico_m": lo.get(".data$ico_a"), ".data$ico_p": lo.get(".data$ico_n"),
            "ico_gdata": lo.get("ico_gdata"), "ico_gbss": lo.get("ico_gbss")}
    rng = {".data$ico_m": 0, ".data$ico_p": 1, "ico_gdata": 0, "ico_gbss": 1}
    if not secs or any(base[s] is None for s, _a, _z, _o in secs):
        return [], None
    rows = [(rng[s], a - base[s], z, s, o) for s, a, z, o in secs]
    return rows, base[secs[0][0]] if secs[0][0] in (".data$ico_m", "ico_gdata") else None


def print_layout_rows(path):
    rows, start = layout_rows(path)
    if not rows or start is None:
        sys.exit("android_syms_lines: %s shows no game data sections with their range's start" % path)
    # ELF: the output section starts at a multiple of its largest member's alignment itself
    print("start %#x align %d" % (start, PE_ALIGN if rows[0][3].startswith(".data$") else 1))
    for r, off, size, sec, obj in rows:
        print("%d %#x %#x %s %s" % (r, off, size, sec, obj))


def read_layout_rows(path):
    start = align = None
    rows = []
    for line in open(path, encoding="utf-8"):
        t = line.split()
        if not t or t[0].startswith("#"):
            continue
        if t[0] == "start" and len(t) == 4 and t[2] == "align":
            start, align = int(t[1], 16), int(t[3])
        elif len(t) == 5:
            rows.append((int(t[0]), int(t[1], 16), int(t[2], 16), t[3], t[4]))
    return start, align, rows


def layout_cause(map_path, root, platform):
    """the sentence a failing layout gate adds from the map, or None"""
    rec = os.path.join(root, "port", "state_legacy", "layout-%s.txt" % platform)
    if not os.path.isfile(rec):
        return None
    rstart, ralign, want = read_layout_rows(rec)
    got, start = layout_rows(map_path)
    name = "port/state_legacy/layout-%s.txt" % platform
    if not got:
        return "the map %s shows no game data sections to compare with %s." % (map_path, name)

    def key(r):
        return (r[0], r[2], r[3], r[4])

    if [key(r) for r in got] == [key(r) for r in want]:
        for g, w in zip(got, want):
            if g[1] != w[1]:
                where = ""
                if start is not None and rstart is not None and ralign and ralign > 1:
                    where = (" The range starts 0x%x past a multiple of %d (0x%x), the recorded "
                             "build's 0x%x past one; port/platform/game_data_range.c's start pad "
                             "sets it." % (start % ralign, ralign, start, rstart % ralign))
                return ("the same objects at other offsets: the range's start alignment. The "
                        "map's game data holds the objects of %s with their sizes; the first that "
                        "moved is %s %s (+0x%x, recorded +0x%x).%s" %
                        (name, g[4], g[3], g[1], w[1], where))
        return ("the map's game data holds the objects of %s at their offsets with their sizes: "
                "an item inside one of them changed (a variable added, removed, renamed or "
                "resized)." % name)
    for i, (g, w) in enumerate(zip(got, want)):
        if key(g) != key(w):
            return ("the map's game data differs from %s at its item %d: %s %s of 0x%x bytes "
                    "where the recorded build had %s %s of 0x%x bytes." %
                    (name, i + 1, g[4], g[3], g[2], w[4], w[3], w[2]))
    return ("the map's game data has %d objects, %s %d: the first %d agree." %
            (len(got), name, len(want), min(len(got), len(want))))


def layout(carry, root, platform="android", map_path=None):
    want_path = os.path.join(root, "port", "state_legacy", "layout.txt")
    key = "layout" if platform == "android" else "layout-" + platform
    want = None
    for line in open(want_path, encoding="utf-8"):
        t = line.split()
        if len(t) == 2 and t[0] == key:
            want = t[1].lower()
            want = want[2:] if want.startswith("0x") else want
    got = None
    for line in open(carry, encoding="utf-8", errors="replace"):
        m = re.search(r"\blayout ([0-9a-f]+)\b", line)
        if m and line.lstrip().startswith("symbols "):
            got = m.group(1)
    if want is None or got is None:
        print("android_syms_lines: layout: %s" % ("port/state_legacy/layout.txt records no %s hash" % key
                                                  if want is None else
                                                  "the guard's report %s prints no layout hash" % carry))
        sys.exit(1)
    if want == "none":
        print("android_syms_lines: the %s game data layout hash is %s; no earlier version's states "
              "resume exactly here (port/state_legacy/layout.txt %s none): record it there after "
              "the release that ships this build" % (platform, got, key))
        return
    if got != want:
        print("android_syms_lines: the game data layout hash is %s, the legacy tables' %s "
              "(port/state_legacy/layout.txt): the states the earlier versions saved could no longer "
              "resume exactly in this build, only from their checkpoint. If the game's data did not "
              "change, the link lost or moved an item (compare tools/android_syms_lines.py gamesec's "
              "line, the reserved room port/data/reserved/tables.c, the map's ico_gdata/ico_gbss); "
              "if it changed on purpose, record %s in port/state_legacy/layout.txt knowing those "
              "states then take the checkpoint path." % (got, want, got))
        cause = layout_cause(map_path, root, platform) if map_path else None
        if cause:
            print("android_syms_lines: " + cause)
        sys.exit(1)
    print("android_syms_lines: the %s game data layout hash %s is the recorded one" % (platform, got))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "relro":
        relro_lines(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        check(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "layout":
        layout(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 5 and sys.argv[1] == "layout" and \
            sys.argv[4] in ("android", "windows-x64", "linux-arm64"):
        layout(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) == 6 and sys.argv[1] == "layout" and \
            sys.argv[4] in ("android", "windows-x64", "linux-arm64"):
        layout(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif len(sys.argv) == 3 and sys.argv[1] == "layout-rows":
        print_layout_rows(sys.argv[2])
    elif len(sys.argv) == 4 and sys.argv[1] == "gamesec":
        gamesec(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
