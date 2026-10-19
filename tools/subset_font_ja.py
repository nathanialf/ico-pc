#!/usr/bin/env python3
"""subset_font_ja.py: the Japanese text's font, made once from Noto Sans JP.

The port draws its own text (strings.h: the menus it adds, photo mode's help,
the messages) with Arimo (port/assets/fonts/Arimo-Regular.ttf); a character
Arimo has no glyph for is drawn from this second font (port/ui/font.c), which
holds the Japanese table's characters (port/ui/strings_ja.c) and the few
other non-Latin ones the tables use (the language names, the PlayStation
button marks). The game's own text is never drawn with either: it is the
disc's.

    tools/subset_font_ja.py [--src FONT] [--check]

FONT is the variable NotoSansJP[wght].ttf from google/fonts that
tools/fetch_deps.sh (section 7) puts in tools/toolchain/deps/noto-sans-jp/
(SIL OFL 1.1). The output is port/assets/fonts/NotoSansJP-Regular-subset.ttf:
the wght 400 instance, subset to the code points below, hinting and the
layout tables dropped (stb_truetype reads the outlines, the cmap and the
metrics), its timestamps the source's (so the same input makes the same
bytes), and the "# subset-ja:" line of port/ui/embed_font.cmake, which
tools/font_audit.py checks the tables against. Needs fontTools (4.63.0 made
the committed file: pip install fonttools==4.63.0).

--check verifies the committed files without writing anything: the
recorded line is the code points the tables and the corpus use now (run
this tool when it is not); with fontTools, the committed font's cmap is the
recorded code points (the assigned ones); with fontTools 4.63.0 and the source font too, the
committed font is byte for byte what this tool makes. A check it cannot run
(no fontTools, no source) is named as skipped. Exit 0 when every check that
ran passed, 1 otherwise.

The code points: ASCII, the CJK symbols and punctuation (U+3000-303F),
hiragana and katakana (U+3041-30FF), the fullwidth forms (U+FF01-FF5E), the
PlayStation button marks (U+25A1 square, U+25B3 triangle, U+25CB circle),
and every character outside Arimo's subset (embed_font.cmake's "# subset:"
line) of the string tables (port/ui/strings_*.c), the model viewer's names
(port/game/model_viewer_table.c) and the corpus (port/ui/test/font_corpus/
*.txt, '#' lines are provenance).
"""
import argparse
import glob
import os
import re
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import font_audit  # noqa: E402  (the literal reader and the ranges' parser)

CMAKE = os.path.join(ROOT, "port/ui/embed_font.cmake")
OUT = os.path.join(ROOT, "port/assets/fonts/NotoSansJP-Regular-subset.ttf")
SRC = os.path.join(ROOT, "tools/toolchain/deps/noto-sans-jp/NotoSansJP[wght].ttf")
BASE_RANGES = [(0x20, 0x7E), (0x3000, 0x303F), (0x3041, 0x30FF), (0xFF01, 0xFF5E),
               (0x25A1, 0x25A1), (0x25B3, 0x25B3), (0x25CB, 0x25CB)]
FONTTOOLS_VERSION = "4.63.0"


def used_outside_arimo():
    arimo = font_audit.parse_ranges(font_audit.cmake_ranges(CMAKE))
    files = sorted(glob.glob(os.path.join(ROOT, "port/ui/strings_*.c")))
    files.append(os.path.join(ROOT, "port/game/model_viewer_table.c"))
    texts = [lit for p in files for lit in font_audit.literals(p)]
    for p in sorted(glob.glob(os.path.join(ROOT, "port/ui/test/font_corpus/*.txt"))):
        with open(p, encoding="utf-8") as f:
            texts += [l.rstrip("\r\n") for l in f if not l.startswith("#")]
    out = set()
    for text in texts:
        for ch in text:
            cp = ord(ch)
            if cp >= 0x20 and cp not in arimo:
                out.add(cp)
    return out


def wanted():
    cps = set()
    for lo, hi in BASE_RANGES:
        cps.update(range(lo, hi + 1))
    return cps | used_outside_arimo()


def ranges_text(cps):
    out = []
    cps = sorted(cps)
    i = 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        out.append("U+%04X" % cps[i] if i == j else "U+%04X-%04X" % (cps[i], cps[j]))
        i = j + 1
    return ",".join(out)


def recorded():
    with open(CMAKE, encoding="utf-8") as f:
        lines = [l for l in f if l.startswith("# subset-ja:")]
    if len(lines) != 1:
        return None
    return lines[0].split(":", 1)[1].strip()


def write_cmake_line(text):
    with open(CMAKE, encoding="utf-8") as f:
        src = f.read()
    line = "# subset-ja: %s\n" % text
    if re.search(r"^# subset-ja:.*\n", src, re.M):
        src = re.sub(r"^# subset-ja:.*\n", lambda m: line, src, flags=re.M)
    else:
        src = src.replace("\nif(NOT IN OR NOT OUT)", "\n" + line.rstrip("\n") + "\nif(NOT IN OR NOT OUT)", 1)
    with open(CMAKE, "w", encoding="utf-8") as f:
        f.write(src)


def make(src, cps):
    """The subset font (a TTFont) of src for the code points cps."""
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    font = TTFont(src, recalcTimestamp=False)
    font = instancer.instantiateVariableFont(font, {"wght": 400})
    font.recalcTimestamp = False
    opts = subset.Options()
    opts.layout_features = []
    opts.hinting = False
    opts.drop_tables += ["GSUB", "GPOS", "GDEF", "BASE", "vhea", "vmtx", "VORG", "DSIG"]
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    opts.notdef_outline = True
    sub = subset.Subsetter(options=opts)
    sub.populate(unicodes=sorted(cps))
    sub.subset(font)
    return font


def check(src, text):
    bad = 0
    if recorded() != text:
        print("subset_font_ja: FAIL the tables' characters changed: run tools/subset_font_ja.py")
        bad = 1
    else:
        print("subset_font_ja: the recorded subset is the tables' (%d code points)"
              % len(font_audit.parse_ranges(text)))
    try:
        import fontTools
        from fontTools.ttLib import TTFont
    except ImportError:
        print("subset_font_ja: skipped the font's cmap and bytes (no fontTools)")
        return bad
    want = font_audit.parse_ranges(text)
    # the ranges hold two unassigned code points (U+3097, U+3098), which no
    # font maps
    assigned = {cp for cp in want if unicodedata.category(chr(cp)) != "Cn"}
    have = set(TTFont(OUT).getBestCmap())
    if have != assigned:
        print("subset_font_ja: FAIL the committed font's cmap is not the recorded code points"
              " (%d missing, %d extra): run tools/subset_font_ja.py"
              % (len(assigned - have), len(have - assigned)))
        bad = 1
    else:
        print("subset_font_ja: the committed font's cmap is the recorded code points (%d mapped)"
              % len(have))
    if not os.path.exists(src):
        print("subset_font_ja: skipped the byte comparison (no %s: tools/fetch_deps.sh)"
              % os.path.relpath(src, ROOT))
    elif fontTools.version != FONTTOOLS_VERSION:
        print("subset_font_ja: skipped the byte comparison (fontTools %s, the file was made"
              " with %s)" % (fontTools.version, FONTTOOLS_VERSION))
    else:
        import io

        buf = io.BytesIO()
        make(src, want).save(buf)
        with open(OUT, "rb") as f:
            same = f.read() == buf.getvalue()
        if not same:
            print("subset_font_ja: FAIL the committed font is not what this tool makes from"
                  " the source: run tools/subset_font_ja.py")
            bad = 1
        else:
            print("subset_font_ja: the committed font is byte for byte this tool's output")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = ranges_text(wanted())
    if a.check:
        return check(a.src, text)
    make(a.src, font_audit.parse_ranges(text)).save(OUT)
    write_cmake_line(text)
    print("subset_font_ja: %s, %d bytes, %d code points"
          % (os.path.relpath(OUT, ROOT), os.path.getsize(OUT), len(font_audit.parse_ranges(text))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
