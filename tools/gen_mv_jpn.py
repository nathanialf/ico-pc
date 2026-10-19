#!/usr/bin/env python3
"""tools/gen_mv_jpn.py -- the model viewer's Japanese model ids.

Extras > Models names each model by its modelData row (the model-path
table, port/game/model_viewer_table.c MvModel.pal_id), and the Japanese
disc's table is not the European one: 1621 rows against 1637, with rows
taken out in two places.  A model's Japanese row (MvModel.jpn_id) is found
by matching the two tables' rows: each disc's modelData is written from its
boot ELF by tools/gen_data_c.py (--c model-path --version <slug>) and the
two row lists aligned (difflib, rows compared whole: the skeleton path, the
collision path, the offset, the light and the packet bits).  A row whose
text is unique on both discs matches by name; one of several equal rows
(the many "NULL" rows of the props) by its place in the aligned run around
it.  A row that falls in a run the Japanese disc does not have has no
Japanese row (the tool says so and fails).

  gen_mv_jpn.py --pal-elf PAL.elf --jpn-elf JPN.elf          prints the column
  gen_mv_jpn.py --pal-elf PAL.elf --jpn-elf JPN.elf --check  ctest model_viewer_jpn:
                                                             the table's jpn_id column
                                                             is the derived one
  gen_mv_jpn.py --pal-elf PAL.elf --jpn-elf JPN.elf --write  rewrites the column

Exit 0, 1 on a difference or an unmatched row, 77 without an ELF.
"""

import argparse
import difflib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "port" / "game" / "model_viewer_table.c"
# {UI_STR_MV_<NAME>, pal_id, jpn_id, ...
ROW = re.compile(r"\{(UI_STR_MV_\w+),\s*(\d+),\s*(-?\d+),")


def model_rows(slug, elf, tmp):
    out = Path(tmp) / f"model-path.{slug}.c"
    subprocess.run([sys.executable, str(ROOT / "tools" / "gen_data_c.py"), "--c", "model-path",
                    "--version", slug, "--elf", str(elf), "--symbol-map", "--out", str(out)],
                   check=True, cwd=ROOT)
    rows = [line.strip().rstrip(",") for line in out.read_text().splitlines()
            if line.strip().startswith('{"')]
    if not rows:
        sys.exit(f"gen_mv_jpn: no modelData rows from {elf}")
    return rows


def align(pal, jpn):
    """PAL row -> JPN row (or None) and how it matched ('name' or 'place')."""
    m = {}
    sm = difflib.SequenceMatcher(None, pal, jpn, autojunk=False)
    for tag, i1, i2, j1, _j2 in sm.get_opcodes():
        for k in range(i1, i2):
            if tag == "equal":
                how = "name" if pal.count(pal[k]) == 1 and jpn.count(pal[k]) == 1 else "place"
                m[k] = (j1 + (k - i1), how)
            else:
                m[k] = (None, tag)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pal-elf", type=Path, required=True)
    ap.add_argument("--jpn-elf", type=Path, required=True)
    ap.add_argument("--table", type=Path, default=TABLE)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true")
    g.add_argument("--write", action="store_true")
    a = ap.parse_args()
    for e in (a.pal_elf, a.jpn_elf):
        if not e.is_file():
            print(f"gen_mv_jpn: no base ELF {e}: skipped")
            return 77
    with tempfile.TemporaryDirectory() as tmp:
        pal = model_rows("pal", a.pal_elf, tmp)
        jpn = model_rows("jpn", a.jpn_elf, tmp)
    m = align(pal, jpn)
    text = a.table.read_text()
    rows = ROW.findall(text)
    if not rows:
        print(f"gen_mv_jpn: no MvModel rows in {a.table}")
        return 1
    bad = 0
    named = placed = 0
    derived = {}
    for name, pal_id, jpn_id in rows:
        p = int(pal_id)
        j, how = m.get(p, (None, "out of the table"))
        if j is None:
            print(f"gen_mv_jpn: {name}: modelData[{p}] has no Japanese row ({how})")
            bad += 1
            continue
        derived[name] = j
        named += how == "name"
        placed += how == "place"
        print(f"gen_mv_jpn: {name}: modelData[{p}] -> [{j}] by {how} ({pal[p][:60]})")
        if a.check and int(jpn_id) != j:
            print(f"gen_mv_jpn: FAIL {name}: the table has {jpn_id}, derived {j}")
            bad += 1
    print(f"gen_mv_jpn: {len(rows)} models, {named} matched by name, {placed} by place, "
          f"{len(rows) - named - placed} unmatched; modelData {len(pal)} rows, {len(jpn)} on the "
          "Japanese disc")
    if a.write and not bad:
        def repl(mo):
            return f"{{{mo.group(1)}, {mo.group(2)}, {derived[mo.group(1)]},"
        a.table.write_text(ROW.sub(repl, text))
        print(f"gen_mv_jpn: wrote {a.table}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
