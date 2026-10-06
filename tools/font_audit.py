#!/usr/bin/env python3
"""font_audit.py: every code point the port's text uses is inside the unicode
ranges the embedded font subset was built with (docs/port/UI.md, "The font").

Collects the code points of the string literals of port/ui/strings_*.c,
port/ui/test/subtitles.c and port/game/model_viewer_table.c, and of the corpus
(port/ui/test/font_corpus/*.txt, '#' lines are provenance), and compares them
with the ranges recorded in port/ui/embed_font.cmake ("# subset:" line).
UI.md's recipe must list the same ranges.  The check on the font's own cmap
is font_coverage (it asks the embedded font for each code point); this one
runs without a build and names the code points to add to the recipe.

Exit 0 when every code point is covered, 1 otherwise, 2 for a bad setup.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SIMPLE = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11,
          "\\": 92, "'": 39, '"': 34, "?": 63, "e": 27}


def decode_c_string(body):
    """The bytes of a C string literal's body (escapes resolved), as UTF-8
    text; \\xNN and octal escapes are bytes."""
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            out += c.encode("utf-8")
            i += 1
            continue
        i += 1
        c = body[i]
        if c in SIMPLE:
            out.append(SIMPLE[c])
            i += 1
        elif c == "x":
            j = i + 1
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(int(body[i + 1:j], 16) & 0xFF)
            i = j
        elif c in "01234567":
            j = i
            while j < len(body) and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(int(body[i:j], 8) & 0xFF)
            i = j
        elif c in "uU":
            n = 4 if c == "u" else 8
            out += chr(int(body[i + 1:i + 1 + n], 16)).encode("utf-8")
            i += 1 + n
        else:
            raise ValueError("unknown escape \\%s" % c)
    return out.decode("utf-8", errors="replace")


TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"((?:[^"\\\n]|\\.)*)"|\'(?:[^\'\\\n]|\\.)+\'|#[^\n]*',
                   re.S)


def literals(path):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    for m in TOKEN.finditer(src):
        if m.group(1) is not None:
            yield decode_c_string(m.group(1))


def parse_ranges(text):
    out = set()
    for part in text.replace(" ", "").split(","):
        m = re.fullmatch(r"U\+([0-9A-Fa-f]+)(?:-([0-9A-Fa-f]+))?", part)
        if not m:
            raise ValueError("bad range %r" % part)
        lo = int(m.group(1), 16)
        hi = int(m.group(2), 16) if m.group(2) else lo
        out.update(range(lo, hi + 1))
    return out


def cmake_ranges(path):
    with open(path, encoding="utf-8") as f:
        lines = [l for l in f if l.startswith("# subset:")]
    if len(lines) != 1:
        raise ValueError("%s: expected one '# subset:' line" % path)
    return lines[0].split(":", 1)[1].strip()


def doc_ranges(path):
    """The recipe's populate(unicodes=parse_unicodes("..." "...")) string."""
    with open(path, encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'parse_unicodes\(\s*((?:"[^"]*"\s*)+)\)', src)
    if not m:
        raise ValueError("%s: the recipe's parse_unicodes is missing" % path)
    return "".join(re.findall(r'"([^"]*)"', m.group(1)))


def main():
    cmake = os.path.join(ROOT, "port/ui/embed_font.cmake")
    doc = os.path.join(ROOT, "docs/port/UI.md")
    try:
        ranges = cmake_ranges(cmake)
        covered = parse_ranges(ranges)
        if parse_ranges(doc_ranges(doc)) != covered:
            print("font_audit: UI.md's recipe ranges differ from embed_font.cmake's")
            return 1
    except (ValueError, OSError) as e:
        print("font_audit: %s" % e)
        return 2

    where = {}  # code point -> first file

    def add(text, name):
        for ch in text:
            where.setdefault(ord(ch), name)

    files = sorted(glob.glob(os.path.join(ROOT, "port/ui/strings_*.c")))
    files += [os.path.join(ROOT, "port/ui/test/subtitles.c"),
              os.path.join(ROOT, "port/game/model_viewer_table.c")]
    for p in files:
        for lit in literals(p):
            add(lit, os.path.relpath(p, ROOT))
    corpus = sorted(glob.glob(os.path.join(ROOT, "port/ui/test/font_corpus/*.txt")))
    if len(corpus) < 5:
        print("font_audit: %d corpus files, expected one per language" % len(corpus))
        return 1
    for p in corpus:
        with open(p, encoding="utf-8") as f:
            for line in f:
                if not line.startswith("#"):
                    add(line.rstrip("\r\n"), os.path.relpath(p, ROOT))

    bad = False
    for cp in sorted(where):
        if cp == 0xFFFD or (cp < 0x20 and cp != 0x0A) or 0x7F <= cp < 0xA0:
            print("font_audit: U+%04X (%s) is a control or a replacement character"
                  % (cp, where[cp]))
            bad = True
        elif cp not in covered and cp != 0x0A:
            print("font_audit: U+%04X %r is not in the subset (first used in %s)"
                  % (cp, chr(cp), where[cp]))
            bad = True
    if bad:
        return 1
    print("font_audit: %d distinct code points, all inside the subset's ranges" % len(where))
    return 0


if __name__ == "__main__":
    sys.exit(main())
