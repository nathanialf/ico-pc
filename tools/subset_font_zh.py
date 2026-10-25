#!/usr/bin/env python3
"""subset_font_zh.py: the Chinese text's font, made once from Noto Sans TC.

The port draws its own text (strings.h: the menus it adds, photo mode's help,
the messages) with Arimo (port/assets/fonts/Arimo-Regular.ttf); a character
Arimo has no glyph for is drawn from Noto Sans JP's subset
(tools/subset_font_ja.py), then Noto Sans KR's (tools/subset_font_ko.py) and
last from this fourth font (port/ui/font.c), which holds the ideographs and
marks of the Chinese table (port/ui/strings_zh.c, Traditional Chinese, the
language of the Asian disc's own menu sheets).  While the port's language is
Chinese this font comes before the Japanese one, so each ideograph is drawn in
its Traditional form, not the Japanese one the Japanese font holds for the
same code point.  The game's own text is never drawn with any of them: it is
the disc's.

    tools/subset_font_zh.py [--src FONT] [--check]

FONT is the variable NotoSansTC[wght].ttf from google/fonts that
tools/fetch_deps.sh (section 8b) puts in tools/toolchain/deps/noto-sans-tc/
(SIL OFL 1.1). The output is port/assets/fonts/NotoSansTC-Regular-subset.ttf:
the wght 400 instance, subset to the code points below, hinting and the
layout tables dropped (stb_truetype reads the outlines, the cmap and the
metrics), its timestamps the source's (so the same input makes the same
bytes), and the "# subset-zh:" line of port/ui/embed_font.cmake, which
tools/font_audit.py checks the tables against. Needs fontTools (4.63.0 made
the committed file: pip install fonttools==4.63.0).

--check verifies the committed files without writing anything, as
tools/subset_font_ja.py --check does: the recorded line is the code points
the tables and the corpus use now (run this tool when it is not); with
fontTools, the committed font's cmap is the recorded code points; with
fontTools 4.63.0 and the source font too, the committed font is byte for
byte what this tool makes. A check it cannot run (no fontTools, no source)
is named as skipped. Exit 0 when every check that ran passed, 1 otherwise.

The code points: ASCII, the CJK symbols and punctuation (U+3000-303F), the
fullwidth forms (U+FF01-FF5E) and the PlayStation button marks (U+25A1
square, U+25B3 triangle, U+25CB circle), which the Japanese font draws first
(font.c tries Arimo, then it, then Noto Sans KR's) but which this font holds
so the Chinese text has its own; and every other character of the Chinese
table (port/ui/strings_zh.c) and the Chinese corpus
(port/ui/test/font_corpus/zh.txt, '#' lines are provenance) outside Arimo's
subset (embed_font.cmake's "# subset:" line), but Hangul (the language names
put 한국어 in every table; Noto Sans KR draws it).  The language name
繁體中文, which every table holds, is also in the Japanese font's subset
(tools/subset_font_ja.py reads every table but the Chinese one), so it is
drawn from one font in every language.  Noto Sans TC maps every one of the
code points (a character it lacks fails the cmap check below).
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import font_audit  # noqa: E402  (the literal reader, the ranges' parser, HANGUL)

CMAKE = os.path.join(ROOT, "port/ui/embed_font.cmake")
OUT = os.path.join(ROOT, "port/assets/fonts/NotoSansTC-Regular-subset.ttf")
SRC = os.path.join(ROOT, "tools/toolchain/deps/noto-sans-tc/NotoSansTC[wght].ttf")
BASE_RANGES = [(0x20, 0x7E), (0x3000, 0x303F), (0xFF01, 0xFF5E), (0x25A1, 0x25A1),
               (0x25B3, 0x25B3), (0x25CB, 0x25CB)]
FONTTOOLS_VERSION = "4.63.0"
KEY = "subset-zh"


def texts(paths, corpus):
    out = [lit for p in paths for lit in font_audit.literals(p)]
    for p in corpus:
        with open(p, encoding="utf-8") as f:
            out += [l.rstrip("\r\n") for l in f if not l.startswith("#")]
    return out


def used():
    arimo = font_audit.parse_ranges(font_audit.cmake_ranges(CMAKE))
    zh = texts([os.path.join(ROOT, "port/ui/strings_zh.c")],
               [os.path.join(ROOT, "port/ui/test/font_corpus/zh.txt")])
    out = set()
    for text in zh:
        out.update(ord(ch) for ch in text
                   if ord(ch) >= 0x20 and ord(ch) not in arimo and not font_audit.is_hangul(ord(ch)))
    return out


def wanted():
    cps = set()
    for lo, hi in BASE_RANGES:
        cps.update(range(lo, hi + 1))
    return cps | used()


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
        lines = [l for l in f if l.startswith("# %s:" % KEY)]
    if len(lines) != 1:
        return None
    return lines[0].split(":", 1)[1].strip()


def write_cmake_line(text):
    with open(CMAKE, encoding="utf-8") as f:
        src = f.read()
    line = "# %s: %s\n" % (KEY, text)
    pat = r"^# %s:.*\n" % KEY
    if re.search(pat, src, re.M):
        src = re.sub(pat, lambda m: line, src, flags=re.M)
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
        print("subset_font_zh: FAIL the tables' characters changed: run tools/subset_font_zh.py")
        bad = 1
    else:
        print("subset_font_zh: the recorded subset is the tables' (%d code points)"
              % len(font_audit.parse_ranges(text)))
    try:
        import fontTools
        from fontTools.ttLib import TTFont
    except ImportError:
        print("subset_font_zh: skipped the font's cmap and bytes (no fontTools)")
        return bad
    want = font_audit.parse_ranges(text)
    have = set(TTFont(OUT).getBestCmap())
    if have != want:
        print("subset_font_zh: FAIL the committed font's cmap is not the recorded code points"
              " (%d missing, %d extra): run tools/subset_font_zh.py"
              % (len(want - have), len(have - want)))
        bad = 1
    else:
        print("subset_font_zh: the committed font's cmap is the recorded code points (%d mapped)"
              % len(have))
    if not os.path.exists(src):
        print("subset_font_zh: skipped the byte comparison (no %s: tools/fetch_deps.sh)"
              % os.path.relpath(src, ROOT))
    elif fontTools.version != FONTTOOLS_VERSION:
        print("subset_font_zh: skipped the byte comparison (fontTools %s, the file was made"
              " with %s)" % (fontTools.version, FONTTOOLS_VERSION))
    else:
        import io

        buf = io.BytesIO()
        make(src, want).save(buf)
        with open(OUT, "rb") as f:
            same = f.read() == buf.getvalue()
        if not same:
            print("subset_font_zh: FAIL the committed font is not what this tool makes from"
                  " the source: run tools/subset_font_zh.py")
            bad = 1
        else:
            print("subset_font_zh: the committed font is byte for byte this tool's output")
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
    print("subset_font_zh: %s, %d bytes, %d code points"
          % (os.path.relpath(OUT, ROOT), os.path.getsize(OUT), len(font_audit.parse_ranges(text))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
