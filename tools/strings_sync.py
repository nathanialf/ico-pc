#!/usr/bin/env python3
"""List and append the string ids a community translation table does not carry yet.

The community translation tables (port/ui/strings_*.c beyond the eight required
languages) are designated-initialiser tables: an id left out is NULL, which the
program shows in English.  A new id therefore never breaks them, and nothing told a
translator which rows are new.  This tool lists the ids absent from each community
file (--check, also ctest strings_sync) and appends them as one dated block at the end
of the table (--write), each as `[UI_STR_X] = NULL, /* "<english>" */`.

The quoted English of a block is the translator's source text, so --check also fails
when a block's quote differs from strings_en.c (an English row reworded after the block
was written); --refresh-comments rewrites those quotes to the current English.  --check
first checks its own parse of the enum: an id read twice (a comment line inside the
enum that starts with an id) would make --write append a duplicate initialiser.

  python3 -I tools/strings_sync.py --check
  python3 -I tools/strings_sync.py --write --since v0.11.2 [FILE...]
  python3 -I tools/strings_sync.py --refresh-comments [FILE...]

The community files are every port/ui/strings_*.c except the eight required ones
(en, fr, de, it, es, ja, ko, zh); FILE... names some explicitly.  --since defaults to
the newest git tag.  An id already present in a file (translated or NULL) is never
written again: a duplicate designated initialiser is a build error (-Werror).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "port" / "ui"
REQUIRED = {"en", "fr", "de", "it", "es", "ja", "ko", "zh"}
ID_RE = re.compile(r"^\s+(UI_STR_[A-Z0-9_]+)\b")
ENTRY_RE = re.compile(r"^\s*\[(UI_STR_\w+)\]")
BLOCK_RE = re.compile(r"^\s*/\* ---- new since ")
# a block entry: `[UI_STR_X] = NULL, /* "<english>" */` on one line, or the quote on
# its own comment line above `[UI_STR_X] = NULL,`
QUOTE_INLINE_RE = re.compile(r'^\s*\[(UI_STR_\w+)\]\s*=\s*NULL,\s*/\* "(.*)" \*/\s*$')
QUOTE_LINE_RE = re.compile(r'^\s*/\* "(.*)" \*/\s*$')
NULL_ENTRY_RE = re.compile(r"^\s*\[(UI_STR_\w+)\]\s*=\s*NULL,\s*$")
COLS = 100


def enum_ids(text=None):
    if text is None:
        text = (UI / "strings.h").read_text(encoding="utf-8")
    start = text.index("typedef enum UiStrId")
    end = text.index("} UiStrId;", start)
    # comments out first: a comment's continuation line inside the enum can start with
    # an id ("   UI_STR_OFF) and its note */"), which is not an enumerator
    body = re.sub(r"/\*.*?\*/", " ", text[start:end], flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    ids = []
    for line in body.splitlines():
        m = ID_RE.match(line)
        if m and m.group(1) not in ("UI_STR_COUNT", "UI_STR_NONE"):
            ids.append(m.group(1))
    return ids


def duplicates(ids):
    seen, dup = set(), []
    for i in ids:
        if i in seen and i not in dup:
            dup.append(i)
        seen.add(i)
    return dup


def c_literals(s):
    """The concatenated value of the adjacent C string literals in s."""
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', s):
        out.append(m.group(1))
    return "".join(out)


def english():
    """id -> the English words as written in the C source (escapes kept)."""
    text = (UI / "strings_en.c").read_text(encoding="utf-8")
    words = {}
    for m in re.finditer(r"\[(UI_STR_\w+)\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)", text):
        words[m.group(1)] = c_literals(m.group(2))
    return words


def community_files(explicit):
    if explicit:
        return [Path(f).resolve() for f in explicit]
    files = []
    for p in sorted(UI.glob("strings_*.c")):
        if p.stem[len("strings_"):] not in REQUIRED:
            files.append(p)
    return files


def present(path):
    ids = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        m = ENTRY_RE.match(line)
        if m:
            ids.add(m.group(1))
    return ids


def block_quotes(lines):
    """(first line index, line count, id, quoted English) of every NULL entry after a
    file's first `---- new since` header (the blocks --write appends)."""
    out = []
    inside = False
    k = 0
    while k < len(lines):
        line = lines[k]
        if BLOCK_RE.match(line):
            inside = True
        elif inside:
            m = QUOTE_INLINE_RE.match(line)
            if m:
                out.append((k, 1, m.group(1), m.group(2)))
            else:
                q = QUOTE_LINE_RE.match(line)
                n = NULL_ENTRY_RE.match(lines[k + 1]) if q and k + 1 < len(lines) else None
                if n:
                    out.append((k, 2, n.group(1), q.group(1)))
                    k += 1
        k += 1
    return out


def stale_quotes(path, en, text=None):
    """The block entries whose quoted English is not strings_en.c's words any more."""
    if text is None:
        text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    return [(k, n, i, q) for (k, n, i, q) in block_quotes(lines) if i in en and en[i] != q]


def self_test():
    """The parse rules on fixed inputs (ctest strings_sync_selftest)."""
    bad = []
    enum = ("typedef enum UiStrId {\n    UI_STR_NONE = 0,\n    UI_STR_A, /* a note that\n"
            "   UI_STR_B) goes on */\n    UI_STR_B,\n    /* UI_STR_C on its own */\n"
            "    UI_STR_C, // UI_STR_A again\n    UI_STR_COUNT\n} UiStrId;\n")
    ids = enum_ids(enum)
    if ids != ["UI_STR_A", "UI_STR_B", "UI_STR_C"]:
        bad.append(f"enum parse: {ids}")
    if duplicates(["UI_STR_A", "UI_STR_B", "UI_STR_A"]) != ["UI_STR_A"]:
        bad.append("duplicates")
    table = ('    [UI_STR_A] = "translated", /* "Old words" */\n'
             "    /* ---- new since v9.9.9: English until translated ---- */\n"
             '    [UI_STR_B] = NULL, /* "Old words" */\n'
             '    /* "Long old words" */\n    [UI_STR_C] = NULL,\n'
             '    [UI_STR_D] = NULL, /* "Same words" */\n};\n')
    en = {"UI_STR_A": "New", "UI_STR_B": "New", "UI_STR_C": "New long", "UI_STR_D": "Same words"}
    got = [(k, n, i) for (k, n, i, _q) in stale_quotes(None, en, table)]
    if got != [(2, 1, "UI_STR_B"), (3, 2, "UI_STR_C")]:
        bad.append(f"stale quotes: {got}")
    for b in bad:
        print(f"strings_sync self-test: {b}", file=sys.stderr)
    print("strings_sync self-test: " + ("FAILED" if bad else "ok"))
    return 1 if bad else 0


def newest_tag():
    try:
        r = subprocess.run(["git", "-C", str(ROOT), "describe", "--tags", "--abbrev=0"],
                           capture_output=True, text=True, check=True)
        return r.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def entry_lines(i, en):
    one = f'    [{i}] = NULL, /* "{en}" */'
    if len(one) <= COLS:
        return [one]
    # the English on its own comment line above the entry; a line the formatter may
    # not break is left as it is (clang-format does not wrap comments here)
    return [f'    /* "{en}" */', f"    [{i}] = NULL,"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true", help="list absent ids, exit 1 if any")
    g.add_argument("--write", action="store_true", help="append the absent ids as a block")
    g.add_argument("--refresh-comments", action="store_true",
                   help="rewrite the blocks' quoted English to strings_en.c's current words")
    g.add_argument("--self-test", action="store_true", help="check the parse rules, exit 1 on a miss")
    ap.add_argument("--since", help="release the block is headed with (default: newest git tag)")
    ap.add_argument("--note", help="extra words for the block's header comment (--write)")
    ap.add_argument("files", nargs="*", help="community files (default: all of them)")
    a = ap.parse_args()
    if a.self_test:
        return self_test()

    ids = enum_ids()
    dup = duplicates(ids)
    if dup:
        print(f"strings_sync: the enum parse read {', '.join(dup)} more than once "
              f"(a comment line inside UiStrId read as an id?)", file=sys.stderr)
        return 2
    en = english()
    files = community_files(a.files)
    if not files:
        print("strings_sync: no community tables found", file=sys.stderr)
        return 2
    rc = 0
    for f in files:
        have = present(f)
        absent = [i for i in ids if i not in have]
        name = f.relative_to(ROOT) if f.is_relative_to(ROOT) else f
        stale = stale_quotes(f, en)
        if a.refresh_comments:
            if not stale:
                print(f"{name}: every quoted English is current")
                continue
            lines = f.read_text(encoding="utf-8").splitlines()
            for k, n, i, _q in reversed(stale):
                lines[k:k + n] = entry_lines(i, en[i])
            f.write_text("\n".join(lines) + "\n", encoding="utf-8")
            subprocess.run([str(ROOT / "tools" / "format.sh"), str(f)], check=True)
            print(f"{name}: refreshed {len(stale)} quoted English lines")
            continue
        if a.check and stale:
            rc = 1
            print(f"{name}: {len(stale)} quoted English lines differ from strings_en.c "
                  f"(run tools/strings_sync.py --refresh-comments):")
            for k, _n, i, q in stale:
                print(f'  line {k + 1}: {i}  "{q}"  ->  "{en[i]}"')
        if a.check:
            if absent:
                rc = 1
                print(f"{name}: {len(absent)} ids without a line "
                      f"(run tools/strings_sync.py --write --since vX.Y.Z):")
                for i in absent:
                    print(f'  {i}  "{en.get(i, "?")}"')
            else:
                print(f"{name}: every id has a line")
            continue
        if not absent:
            print(f"{name}: nothing to add")
            continue
        since = a.since or newest_tag()
        if not since:
            print("strings_sync: --since is needed (no git tag found)", file=sys.stderr)
            return 2
        text = f.read_text(encoding="utf-8")
        end = text.rindex("};")
        block = [f"    /* ---- new since {since}: English until translated ---- */"]
        if a.note:
            block.append(f"    /* {a.note} */")
        for i in absent:
            block += entry_lines(i, en.get(i, ""))
        text = text[:end] + "\n".join(block) + "\n" + text[end:]
        f.write_text(text, encoding="utf-8")
        subprocess.run([str(ROOT / "tools" / "format.sh"), str(f)], check=True)
        print(f"{name}: appended {len(absent)} ids")
    if a.check and rc:
        print("strings_sync: a community table lacks a new id's line or quotes old English: "
              "--write appends the lines, --refresh-comments renews the quotes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
