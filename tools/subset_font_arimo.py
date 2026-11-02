#!/usr/bin/env python3
"""subset_font_arimo.py: the port's main font, made once from Arimo.

The port draws its own text (strings.h: the menus it adds, photo mode's help,
the messages), the game's menu words (port/ui/menu_text.h) and a community
translation's subtitles (port/ui/subtitle_text.h) with Arimo
(port/assets/fonts/Arimo-Regular.ttf), the first face font.c tries; a
character Arimo has no glyph for is drawn from Noto Sans JP's subset
(tools/subset_font_ja.py), then Noto Sans KR's (tools/subset_font_ko.py), then
Noto Sans TC's (tools/subset_font_zh.py).

    tools/subset_font_arimo.py [--src FONT] [--check]

FONT is the variable Arimo[wght].ttf from google/fonts that tools/fetch_deps.sh
(section 8c) puts in tools/toolchain/deps/arimo/ (SIL OFL 1.1). The output is
port/assets/fonts/Arimo-Regular.ttf: the wght 400 instance, subset to the code
points below, hinting dropped, the layout tables kept for the kerning feature
alone (stb_truetype reads GPOS pair adjustments, lookup type 2; an extension
lookup, type 9, is unwrapped into its subtable so stb_truetype sees it), the
name table whole, its timestamps the source's (so the same input makes the same
bytes), and the "# subset:" line of port/ui/embed_font.cmake, which
tools/font_audit.py and the three Noto scripts read. Needs fontTools (4.63.0
made the committed file: pip install fonttools==4.63.0).

--check verifies the committed files without writing anything, as
tools/subset_font_ja.py --check does: the recorded line is the code points
below (run this tool when it is not); with fontTools, the committed font's cmap
is the recorded code points; with fontTools 4.63.0 and the source font too, the
committed font is byte for byte what this tool makes. A check it cannot run (no
fontTools, no source) is named as skipped. Exit 0 when every check that ran
passed, 1 otherwise.

The code points: ASCII, Latin-1 and Latin Extended-A (U+0020-007E,
U+00A0-017F), the typographic punctuation and signs the tables use (the list
in BASE_RANGES), Cyrillic (U+0400-04FF, the Russian community translations);
and every other character of a community translation's tables
(port/ui/strings_<lang>_<translator>.c, port/game/subtitles_*.c) outside those
ranges. Arimo maps every one of them (a character it lacks fails the cmap
check below; such a character goes to a Noto script's list instead).
"""
import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import font_audit  # noqa: E402  (the literal reader, the ranges' parser)

CMAKE = os.path.join(ROOT, "port/ui/embed_font.cmake")
OUT = os.path.join(ROOT, "port/assets/fonts/Arimo-Regular.ttf")
SRC = os.path.join(ROOT, "tools/toolchain/deps/arimo/Arimo[wght].ttf")
BASE_RANGES = [(0x20, 0x7E), (0xA0, 0x17F), (0x192, 0x192), (0x2C6, 0x2C6), (0x2DC, 0x2DC),
               (0x400, 0x4FF), (0x2013, 0x2014), (0x2018, 0x201E), (0x2020, 0x2022),
               (0x2026, 0x2026), (0x2030, 0x2030), (0x2039, 0x203A), (0x20AC, 0x20AC),
               (0x2122, 0x2122)]
FONTTOOLS_VERSION = "4.63.0"
KEY = "subset"


def community_files():
    """A community translation's tables: strings_<lang>_<translator>.c (a
    two-part name; the disc languages' tables have one) and its subtitles."""
    files = [p for p in sorted(glob.glob(os.path.join(ROOT, "port/ui/strings_*_*.c")))]
    files += sorted(glob.glob(os.path.join(ROOT, "port/game/subtitles_*.c")))
    return files


def base():
    cps = set()
    for lo, hi in BASE_RANGES:
        cps.update(range(lo, hi + 1))
    return cps


def wanted():
    cps = base()
    for p in community_files():
        for lit in font_audit.literals(p):
            cps.update(ord(ch) for ch in lit if ord(ch) >= 0x20)
    return cps


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
    if not re.search(pat, src, re.M):
        raise SystemExit("subset_font_arimo: no '# %s:' line in %s" % (KEY, CMAKE))
    src = re.sub(pat, lambda m: line, src, count=1, flags=re.M)
    with open(CMAKE, "w", encoding="utf-8") as f:
        f.write(src)


def unwrap_extensions(font):
    """GPOS extension lookups (type 9) as their subtables' own type, which
    stb_truetype reads (it skips type 9)."""
    if "GPOS" not in font:
        return
    for lookup in font["GPOS"].table.LookupList.Lookup:
        if lookup.LookupType != 9:
            continue
        subs = [s.ExtSubTable for s in lookup.SubTable]
        lookup.LookupType = subs[0].LookupType
        lookup.SubTable = subs
        lookup.SubTableCount = len(subs)


def make(src, cps):
    """The subset font (a TTFont) of src for the code points cps."""
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    font = TTFont(src, recalcTimestamp=False)
    font = instancer.instantiateVariableFont(font, {"wght": 400})
    font.recalcTimestamp = False
    opts = subset.Options()
    opts.layout_features = ["kern"]
    opts.hinting = False
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    sub = subset.Subsetter(options=opts)
    sub.populate(unicodes=sorted(cps))
    sub.subset(font)
    unwrap_extensions(font)
    return font


def check(src, text):
    bad = 0
    if recorded() != text:
        print("subset_font_arimo: FAIL the recorded subset is not this tool's ranges and the"
              " community tables' characters: run tools/subset_font_arimo.py")
        bad = 1
    else:
        print("subset_font_arimo: the recorded subset is the ranges' and the community tables'"
              " (%d code points)" % len(font_audit.parse_ranges(text)))
    try:
        import fontTools
        from fontTools.ttLib import TTFont
    except ImportError:
        print("subset_font_arimo: skipped the font's cmap and bytes (no fontTools)")
        return bad
    want = font_audit.parse_ranges(text)
    have = set(TTFont(OUT).getBestCmap())
    if have != want:
        print("subset_font_arimo: FAIL the committed font's cmap is not the recorded code points"
              " (%d missing, %d extra): run tools/subset_font_arimo.py"
              % (len(want - have), len(have - want)))
        bad = 1
    else:
        print("subset_font_arimo: the committed font's cmap is the recorded code points"
              " (%d mapped)" % len(have))
    if not os.path.exists(src):
        print("subset_font_arimo: skipped the byte comparison (no %s: tools/fetch_deps.sh)"
              % os.path.relpath(src, ROOT))
    elif fontTools.version != FONTTOOLS_VERSION:
        print("subset_font_arimo: skipped the byte comparison (fontTools %s, the file was made"
              " with %s)" % (fontTools.version, FONTTOOLS_VERSION))
    else:
        import io

        buf = io.BytesIO()
        make(src, want).save(buf)
        with open(OUT, "rb") as f:
            same = f.read() == buf.getvalue()
        if not same:
            print("subset_font_arimo: FAIL the committed font is not what this tool makes from"
                  " the source: run tools/subset_font_arimo.py")
            bad = 1
        else:
            print("subset_font_arimo: the committed font is byte for byte this tool's output")
    return bad


def main():
    ap = argparse.ArgumentParser(description="Make the port's Arimo subset.")
    ap.add_argument("--src", default=SRC, help="the variable Arimo[wght].ttf")
    ap.add_argument("--check", action="store_true", help="verify, write nothing")
    a = ap.parse_args()
    text = ranges_text(wanted())
    if a.check:
        return check(a.src, text)
    make(a.src, font_audit.parse_ranges(text)).save(OUT)
    write_cmake_line(text)
    print("subset_font_arimo: %s, %d bytes, %d code points"
          % (os.path.relpath(OUT, ROOT), os.path.getsize(OUT), len(font_audit.parse_ranges(text))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
