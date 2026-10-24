#!/usr/bin/env python3
"""python3 -I tools/test/texture_catalog_test.py

Runs tools/texture_catalog.py on a 12-line fixture: the grouping, the Same as
link, the European column, the split at the size cap and --check.
"""
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "texture_catalog.py")

H = "0123456789abcdef-0123456789abcdef-%08x"


def name(n):
    return H % n


# a 12-line names file of a made-up disc: two objects, a menu folder, a subtitle file
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
    with open(p, encoding="utf-8") as f:
        return f.read()


def main():
    with tempfile.TemporaryDirectory() as d:
        names = os.path.join(d, "names.txt")
        euro = os.path.join(d, "euro.txt")
        open(names, "w").write(FIXTURE)
        open(euro, "w").write(EURO)
        out = os.path.join(d, "out")

        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro)
        check(r.returncode == 0, "generation exits 0: " + r.stderr)
        files = sorted(os.listdir(out))
        check(files == ["Font.md", "README.md", "Tex_menu01.md", "data_EG01-jim.md",
                        "object-boy.md", "object-girl.md", "object-sky.md",
                        "text-menu_PAL_FR.md"], "group files: %r" % files)

        boy = read(os.path.join(out, "object-boy.md"))
        girl = read(os.path.join(out, "object-girl.md"))
        # `model/..` collapses and only level-alone names are listed
        check("object/sdf/boy/texture/arm.tm2" in boy, "path without model/..")
        check("/chain" not in boy and name(4) not in boy, "chain names left out")
        check("`%s@1`" % name(5) in boy and "`%s@2`" % name(6) in boy, "one name per level")
        check("several sizes" in boy, "note for levels")
        # Same as: the first holder says first, the later ones link to it
        rows = {l.split(" | ")[0]: l for l in boy.splitlines() if l.startswith("| object")}
        check("| first |" in rows["| object/sdf/boy/texture/arm.tm2"], "first holder")
        check("[arm.tm2](object-boy.md)" in rows["| object/sdf/boy/texture/arm2.tm2"], "same-group link")
        skin = [l for l in girl.splitlines() if "skin.tm2" in l][0]
        check("[arm.tm2](object-boy.md)<br>first" in skin, "link per name: " + skin)
        check("colours cycle" in girl, "colours cycle note")
        sky = read(os.path.join(out, "object-sky.md"))
        check("no name (24-bit palette)" in sky and "| none |" in sky, "refused row")
        # the European column
        check(" same |" in [l for l in boy.splitlines() if "arm.tm2" in l][0], "European same")
        check(" partly |" in skin, "European partly: " + skin)
        check(" new |" in read(os.path.join(out, "data_EG01-jim.md")), "European new")
        check("| European |" in boy, "European header")
        readme = read(os.path.join(out, "README.md"))
        check("- Textures: 12\n" in readme, "texture count")
        check("- Different names: 12\n" in readme, "unique names")
        check("- Names that an earlier texture already carries: 2\n" in readme, "covered names")
        check("- Textures with no name: 1\n" in readme, "unnamed")
        check("(same): 6\n" in readme and "(partly): 1\n" in readme, "European counts")
        check("Dump textures" in readme, "not-on-disc list")

        # without --european there is no European column
        out2 = os.path.join(d, "out2")
        r = run(names, "--serial", "TEST-00001", "--out", out2)
        check(r.returncode == 0 and "European" not in read(os.path.join(out2, "object-boy.md")),
              "no European column without the option")

        # the split at the cap
        out3 = os.path.join(d, "out3")
        r = run(names, "--serial", "TEST-00001", "--out", out3, "--cap", "500")
        check(r.returncode == 0, "split run: " + r.stderr)
        f3 = sorted(os.listdir(out3))
        check("object-boy-2.md" in f3, "split file exists: %r" % f3)
        for fn in f3:
            if fn != "README.md":
                check(len(open(os.path.join(out3, fn), "rb").read()) < 800, "part %s small" % fn)
        # a later link points to the part that holds the texture
        check("(object-boy.md)" in read(os.path.join(out3, "object-girl-2.md")), "link to part one")
        check("object-boy-2.md" in read(os.path.join(out3, "README.md")), "README lists the parts")

        # --check
        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro, "--check")
        check(r.returncode == 0, "check is clean")
        with open(os.path.join(out, "object-boy.md"), "a") as f:
            f.write("x\n")
        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro, "--check")
        check(r.returncode == 1, "check fails on a changed file")
        run(names, "--serial", "TEST-00001", "--out", out, "--european", euro)
        open(os.path.join(out, "stray.md"), "w").write("x")
        r = run(names, "--serial", "TEST-00001", "--out", out, "--european", euro, "--check")
        check(r.returncode == 1, "check fails on a left-over file")
        # the same input gives the same bytes
        run(names, "--serial", "TEST-00001", "--out", out2 + "b", "--european", euro)
        run(names, "--serial", "TEST-00001", "--out", out2 + "c", "--european", euro)
        check(all(read(os.path.join(out2 + "b", f)) == read(os.path.join(out2 + "c", f))
                  for f in os.listdir(out2 + "b")), "repeatable")
    print("texture_catalog_test: %s" % ("FAILED (%d)" % fails if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
