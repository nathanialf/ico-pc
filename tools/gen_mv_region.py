#!/usr/bin/env python3
"""tools/gen_mv_region.py -- the model viewer's model ids on another disc.

Extras > Models names each model by its modelData row (the model-path
table, port/game/model_viewer_table.c MvModel.pal_id), and the Japanese
disc's table is not the European one: 1621 rows against 1637, with rows
taken out in two places.  A model's row on another disc (the column the
disc reads: MvModel.jpn_id for jpn and for kor, model_viewer_table.c
mv_id_column) is found by matching the two tables' rows: each disc's
modelData is written from its boot ELF by tools/gen_data_c.py (--c
model-path --version <slug>) and the two row lists aligned (difflib, rows
compared whole: the skeleton path, the collision path, the offset, the
light and the packet bits).  A row whose text is unique on both discs
matches by name; one of several equal rows (the many "NULL" rows of the
props) by its place in the aligned run around it.  A row that falls in a
run the other disc does not have has no row there (the tool says so and
fails).

The Korean disc's modelData is the Japanese one row for row (the two ELFs'
model-path members are equal), so kor reads the jpn_id column; --check
--region kor holds the table to that (a Korean id that differed would need
a column of its own, and --write refuses to rewrite the Japanese column
from the Korean disc).

  gen_mv_region.py --region SLUG --pal-elf PAL.elf --elf SLUG.elf          prints the column
  gen_mv_region.py --region SLUG --pal-elf PAL.elf --elf SLUG.elf --check  ctest
      model_viewer_<slug>: the column the disc reads is the derived one
  gen_mv_region.py --region jpn --pal-elf PAL.elf --elf JPN.elf --write    rewrites the column

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
# the column each non-PAL disc reads (model_viewer_table.c mv_id_column), and
# the slug whose disc --write may rewrite it from
COLUMN = {"jpn": "jpn_id", "kor": "jpn_id"}
COLUMN_OWNER = {"jpn_id": "jpn"}


def model_rows(slug, elf, tmp):
    out = Path(tmp) / f"model-path.{slug}.c"
    subprocess.run([sys.executable, str(ROOT / "tools" / "gen_data_c.py"), "--c", "model-path",
                    "--version", slug, "--elf", str(elf), "--symbol-map", "--out", str(out)],
                   check=True, cwd=ROOT)
    rows = [line.strip().rstrip(",") for line in out.read_text().splitlines()
            if line.strip().startswith('{"')]
    if not rows:
        sys.exit(f"gen_mv_region: no modelData rows from {elf}")
    return rows


def align(pal, other):
    """PAL row -> the other disc's row (or None) and how it matched ('name' or
    'place')."""
    m = {}
    sm = difflib.SequenceMatcher(None, pal, other, autojunk=False)
    for tag, i1, i2, j1, _j2 in sm.get_opcodes():
        for k in range(i1, i2):
            if tag == "equal":
                how = "name" if pal.count(pal[k]) == 1 and other.count(pal[k]) == 1 else "place"
                m[k] = (j1 + (k - i1), how)
            else:
                m[k] = (None, tag)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--region", required=True, choices=sorted(COLUMN), metavar="SLUG")
    ap.add_argument("--pal-elf", type=Path, required=True)
    ap.add_argument("--elf", type=Path, required=True, help="the other disc's base ELF")
    ap.add_argument("--table", type=Path, default=TABLE)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true")
    g.add_argument("--write", action="store_true")
    a = ap.parse_args()
    column = COLUMN[a.region]
    if a.write and COLUMN_OWNER.get(column) != a.region:
        print(f"gen_mv_region: {a.region} reads the {column} column, which "
              f"{COLUMN_OWNER.get(column)}'s disc writes: --check only")
        return 1
    for e in (a.pal_elf, a.elf):
        if not e.is_file():
            print(f"gen_mv_region: no base ELF {e}: skipped")
            return 77
    with tempfile.TemporaryDirectory() as tmp:
        pal = model_rows("pal", a.pal_elf, tmp)
        other = model_rows(a.region, a.elf, tmp)
    m = align(pal, other)
    text = a.table.read_text()
    rows = ROW.findall(text)
    if not rows:
        print(f"gen_mv_region: no MvModel rows in {a.table}")
        return 1
    bad = 0
    named = placed = 0
    derived = {}
    for name, pal_id, col_id in rows:
        p = int(pal_id)
        j, how = m.get(p, (None, "out of the table"))
        if j is None:
            print(f"gen_mv_region: {name}: modelData[{p}] has no {a.region} row ({how})")
            bad += 1
            continue
        derived[name] = j
        named += how == "name"
        placed += how == "place"
        print(f"gen_mv_region: {name}: modelData[{p}] -> [{j}] by {how} ({pal[p][:60]})")
        if a.check and int(col_id) != j:
            print(f"gen_mv_region: FAIL {name}: the table's {column} has {col_id}, "
                  f"{a.region} derived {j}")
            bad += 1
    print(f"gen_mv_region: {len(rows)} models, {named} matched by name, {placed} by place, "
          f"{len(rows) - named - placed} unmatched; modelData {len(pal)} rows, {len(other)} on "
          f"the {a.region} disc")
    if a.write and not bad:
        def repl(mo):
            return f"{{{mo.group(1)}, {mo.group(2)}, {derived[mo.group(1)]},"
        a.table.write_text(ROW.sub(repl, text))
        print(f"gen_mv_region: wrote {a.table}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
