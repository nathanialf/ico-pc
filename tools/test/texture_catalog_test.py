#!/usr/bin/env python3
"""python3 -I tools/test/texture_catalog_test.py

Runs tools/texture_catalog.py on a 12-line names file: one row per name, the
first user and the count of the others, the notes, the European column,
--check and repeatable output.
"""
import csv
import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "texture_catalog.py")

H = "0123456789abcdef-0123456789abcdef-%08x"


def name(n):
    return H % n


# a made-up disc: two characters, a menu folder, a subtitle file
FIXTURE = "\n".join([
    "Font/font.tm2 PSMT4 0x03 256x256 1 clut 16/64B scroll 0 0 : %s@0" % name(1),
    "object/sdf/boy/model/../texture/arm.tm2 PSMT4 0x03 64x64 1 clut 16/64B scroll 0 0 : %s@0" % name(2),
    "object/sdf/boy/model/../texture/arm2.tm2 PSMT4 0x03 64x64 1 clut 16/64B scroll 0 0 : %s@0" % name(2),
    "object/sdf/boy/model/../texture/hand.tm2 PSMT8 0x03 32x32 3 clut 256/1024B scroll 0 0 : %s@0 %s@0/chain %s@1 %s@2" % (name(3), name(4), name(5), name(6)),
    "object/sdf/girl/texture/hair.tm2 PSMT4 0x03 64x64 1 clut 16/64B scroll 1 0 : %s@0" % name(7),
    "object/sdf/girl/texture/skin.tm2 PSMT4 0x03 64x64 1 clut 16/64B scroll 0 0 : %s@0 %s@0" % (name(2), name(8)),
    "object/sdf/sky/model/../texture/fog1.tm2 PSMT8 0x02 64x64 1 - : refused",
    "Tex_menu01/title.tm2 PSMT4 0x03 256x256 1 clut 16/64B scroll 0 0 : %s@0" % name(9),
    "text/menu_PAL_FR/menu_PAL_01.tm2 PSMT4 0x03 512x256 1 clut 16/64B scroll 0 0 : %s@0" % name(10),
    "text/menu_PAL_FR/menu_PAL_02.tm2 PSMT4 0x03 512x256 1 clut 16/64B scroll 0 0 : %s@0" % name(11),
    "data_EG01.jim@0x0 PSMT8 0x03 256x128 1 clut 256/1024B scroll 0 0 : %s@0" % name(12),
    "data_EG01.jim@0x8800 PSMT8 0x03 256x128 1 clut 256/1024B scroll 0 0 : %s@0" % name(13),
]) + "\n"

# the European disc has names 1, 2, 3, 5, 6, 7 and 10 (and 99, which no row has)
EURO = "\n".join(
    "x%d.tm2 PSMT4 0x03 64x64 1 clut 16/64B scroll 0 0 : %s@0" % (n, name(n))
    for n in (1, 2, 3, 5, 6, 7, 10, 99)) + "\n"

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL: " + what)


def run(*args):
    return subprocess.run([sys.executable, "-I", TOOL] + list(args),
                          capture_output=True, text=True)


def read(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def rows_of(p):
    return list(csv.DictReader(io.StringIO(read(p))))


def main():
    with tempfile.TemporaryDirectory() as d:
        names = os.path.join(d, "names.txt")
        euro = os.path.join(d, "euro.txt")
        open(names, "w").write(FIXTURE)
        open(euro, "w").write(EURO)
        out = os.path.join(d, "japan.csv")

        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro)
        check(r.returncode == 0, "generation exits 0: " + r.stderr)
        rows = rows_of(out)
        check(list(rows[0].keys()) == ["file", "size", "format", "first used by", "where",
                                       "also used by", "in the European pack", "notes"],
              "columns: %r" % list(rows[0].keys()))
        # one row per name (12 names) plus the one texture with no name
        check(len(rows) == 13, "13 rows (%d)" % len(rows))
        by = {r["file"]: r for r in rows}
        arm = by[name(2) + ".png"]
        check(arm["first used by"] == "boy/texture/arm.tm2" and arm["also used by"] == "2" and
              arm["where"] == "boy girl", "first user and the count: %r" % arm)
        check(name(4) + ".png" not in by, "chain names left out")
        check(by[name(5) + ".png"]["size"] == "16x16" and
              by[name(5) + ".png"]["notes"] == "size 1 of a 32x32 texture", "a smaller size's row")
        check(by[name(6) + ".png"]["size"] == "8x8", "size 2")
        check(by[name(3) + ".png"]["notes"] == "has 2 smaller sizes (their own rows)", "levels note")
        check(by[name(7) + ".png"]["notes"].startswith("colours cycle"), "colours cycle note")
        none = [r for r in rows if r["file"] == ""]
        check(len(none) == 1 and none[0]["first used by"] == "sky/texture/fog1.tm2" and
              "24-bit palette" in none[0]["notes"] and none[0]["in the European pack"] == "",
              "the texture with no name: %r" % none)
        check(by[name(12) + ".png"]["where"] == "data_EG01.jim", "a subtitle record's place")
        check(by[name(10) + ".png"]["where"] == "text/menu_PAL_FR", "a menu sheet's place")
        check(arm["in the European pack"] == "yes" and by[name(8) + ".png"]["in the European pack"] == "no",
              "the European column")
        check(by[name(1) + ".png"]["format"] == "16 colours" and
              by[name(3) + ".png"]["format"] == "256 colours", "format words")
        # the first appearance keeps the disc's order
        check([r["file"] for r in rows[:3]] == [name(1) + ".png", name(2) + ".png", name(3) + ".png"],
              "the disc's order")

        # without --european there is no European column
        out2 = os.path.join(d, "europe.csv")
        r = run(names, "--serial", "TEST-00001", "--out", out2)
        check(r.returncode == 0 and "in the European pack" not in rows_of(out2)[0], "no European column")

        # --check
        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro, "--check")
        check(r.returncode == 0, "check is clean")
        with open(out, "a") as f:
            f.write("x\n")
        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro, "--check")
        check(r.returncode == 1, "check fails on a changed file")
        # the same input gives the same bytes
        run(names, "--serial", "TEST-00001", "--out", out2 + "b", "--european", euro)
        run(names, "--serial", "TEST-00001", "--out", out2 + "c", "--european", euro)
        check(read(out2 + "b") == read(out2 + "c"), "repeatable output")
    print("texture_catalog_test: %s" % ("OK" if fails == 0 else "%d failures" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
