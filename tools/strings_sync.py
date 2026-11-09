#!/usr/bin/env python3
"""List and append the string ids a community translation table does not carry yet.

The community translation tables (port/ui/strings_*.c beyond the eight required
languages) are designated-initialiser tables: an id left out is NULL, which the
program shows in English.  A new id therefore never breaks them, and nothing told a
translator which rows are new.  This tool lists the ids absent from each community
file (--check, also ctest strings_sync) and appends them as one dated block at the end
of the table (--write), each as `[UI_STR_X] = NULL, /* "<english>" */`.

  python3 -I tools/strings_sync.py --check
  python3 -I tools/strings_sync.py --write --since v0.11.2 [FILE...]

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
COLS = 100


def enum_ids():
    text = (UI / "strings.h").read_text(encoding="utf-8")
    start = text.index("typedef enum UiStrId")
    end = text.index("} UiStrId;", start)
    ids = []
    for line in text[start:end].splitlines():
        m = ID_RE.match(line)
        if m and m.group(1) not in ("UI_STR_COUNT", "UI_STR_NONE"):
            ids.append(m.group(1))
    return ids


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
    ap.add_argument("--since", help="release the block is headed with (default: newest git tag)")
    ap.add_argument("--note", help="extra words for the block's header comment (--write)")
    ap.add_argument("files", nargs="*", help="community files (default: all of them)")
    a = ap.parse_args()

    ids = enum_ids()
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
        print("strings_sync: a community table lacks a new id's line: append it with --write")
    return rc


if __name__ == "__main__":
    sys.exit(main())
