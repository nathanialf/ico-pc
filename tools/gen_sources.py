#!/usr/bin/env python3
"""Write cmake/IcoSources.cmake, the host build's source lists.

The lists come from config/link_order.pal.txt, the one inventory of the
game's translation units (the PS2 build's tools/gen_ninja.py, now retired,
failed on a tracked source it did not list). The host build takes its ico2/ C sources and its data-only
members, never sce/ (Sony's libraries are replaced by port/) or the VU1
microprograms (ico2/vusrc/, replaced by shaders).

Each C source lands in one list per programmer directory (the directory
decides its include path, as the period build had it), or in
ICO_EE_ONLY_SOURCES when port/ replaces it for good (the ito/mpeg movie
player: port/fmv). Since renderer wave 6 (package R6a) there is one host
source list: the renderer-owned files the renderer waves rewrote (all of
seki/src, the sugipon and ito effect files, common/src/debug.c and
debug_exception.c) compile like every other game source, in both the
headless and the window build (docs/BUILDING.md, "How the game is compiled").

    tools/gen_sources.py           rewrite cmake/IcoSources.cmake
    tools/gen_sources.py --check   exit 1 if it is out of date
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINK_ORDER = ROOT / "config" / "link_order.pal.txt"
OUT = ROOT / "cmake" / "IcoSources.cmake"

PROGRAMMERS = ["common", "fumi", "ito", "omori", "script", "seki", "sugipon"]

# Sources the host build never compiles because port/ replaces them for good
# (they stay in the PS2 build): the ito/mpeg movie player, replaced by
# port/fmv (Phase 4E, docs/port/FMV.md). Listed in ICO_EE_ONLY_SOURCES so the
# inventory stays complete.
EE_ONLY_DIRS = ("ico2/ito/mpeg/",)


def is_ee_only(path):
    return path.startswith(EE_ONLY_DIRS)


def read_link_order():
    sources, members = [], []
    for line in LINK_ORDER.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()[0]
        if tok.startswith("data:"):
            members.append(tok[len("data:"):])
        elif tok.startswith("ico2/") and tok.endswith(".c"):
            sources.append(tok)
    return sources, members


def cmake_list(name, items, comment):
    out = [f"# {comment}", f"set({name}"]
    out += [f"    {i}" for i in items]
    out.append(")")
    return "\n".join(out) + "\n"


def render():
    sources, members = read_link_order()
    by_prog = {p: [] for p in PROGRAMMERS}
    ee_only = []
    for s in sources:
        if is_ee_only(s):
            ee_only.append(s)
            continue
        prog = s.split("/")[1]
        if prog not in by_prog:
            sys.exit(f"gen_sources: {s}: unknown programmer directory {prog}")
        by_prog[prog].append(s)
    parts = [
        "# cmake/IcoSources.cmake: written by tools/gen_sources.py from\n"
        "# config/link_order.pal.txt. Do not edit; rerun the script.\n"
        "# Order is the retail link's.\n",
        f"set(ICO_PROGRAMMERS {' '.join(PROGRAMMERS)})\n",
    ]
    for p in PROGRAMMERS:
        parts.append(cmake_list(f"ICO_SOURCES_{p}", by_prog[p],
                                f"ico2/{p}: {len(by_prog[p])} non-renderer sources"))
    parts.append(cmake_list("ICO_EE_ONLY_SOURCES", ee_only,
                            f"{len(ee_only)} PS2-only sources (replaced on the host by port/; never "
                            "compiled here)"))
    parts.append(cmake_list("ICO_DATA_MEMBERS", members,
                            f"{len(members)} data-only members, build/data/<member>.c "
                            "(tools/gen_data_c.py)"))
    return "\n".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = render()
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"gen_sources: {OUT.relative_to(ROOT)} is out of date; "
                  "run tools/gen_sources.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
