#!/usr/bin/env python3
"""Write cmake/IcoSources.cmake, the host build's source lists.

The lists come from the union of config/link_order.*.txt, every target's
inventory of the game's translation units (pal's order first, then each
other target's, in tools/ico_version.py's VERSIONS order: a source only a
later target links is inserted after the source it follows in that target's
own order). A target is in the union when its link order exists; the rows of
config/regions.txt are not consulted. The host build takes its ico2/ C
sources and its data-only members, never sce/ (Sony's libraries are replaced
by port/) or the VU1 microprograms (ico2/vusrc/, replaced by shaders).

A source that only some targets link is compiled for every one of them (its
body is inside `if (ICO_<SLUG>)` or a whole-file arm). A source a link order
lists that the tree does not hold yet (a target's file that arrives with a
merge) is "pending the merge": it is written to ICO_PENDING_SOURCES, which
nothing compiles, and `--check` names it on stderr without failing. Once the
file exists the generated list is out of date and `--check` fails until the
script is run again, which moves it into its programmer's list.

Each C source lands in one list per programmer directory (the directory
decides its include path, as the period build had it), or in
ICO_EE_ONLY_SOURCES when port/ replaces it for good (the ito/mpeg movie
player: port/fmv; the demo disc's menu hand-back, demoDisc.c). There is one host source list: the renderer-owned
files the port's renderer rewrote (all of
seki/src, the sugipon and ito effect files, common/src/debug.c and
debug_exception.c) compile like every other game source, in both the
headless and the window build (docs/port/GAME_CODE.md, "What is compiled").

    tools/gen_sources.py           rewrite cmake/IcoSources.cmake
    tools/gen_sources.py --check   exit 1 if it is out of date
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from ico_version import VERSIONS  # noqa: E402

OUT = ROOT / "cmake" / "IcoSources.cmake"

PROGRAMMERS = ["common", "fumi", "ito", "omori", "script", "seki", "sugipon"]

# Sources the host build never compiles because port/ replaces them for
# good: the ito/mpeg movie player, replaced by port/fmv. Listed in
# ICO_EE_ONLY_SOURCES so the inventory stays complete.
EE_ONLY_DIRS = ("ico2/ito/mpeg/",)
# The June 2001 demo's hand-back to the magazine menu (sceDemoStart reads the menu's
# arguments, sceDemoEnd reloads the menu's ELF): the port has no menu to start it or to go
# back to: the demo's start takes the port's own settings instead (a port stub that comes
# with the demo's game rows), and sceDemoEnd is never called.
EE_ONLY_FILES = ("ico2/common/src/demoDisc.c",)


def is_ee_only(path):
    return path.startswith(EE_ONLY_DIRS) or path in EE_ONLY_FILES


def link_orders():
    """[(slug, path)] of every config/link_order.<slug>.txt: the slugs of
    ico_version.VERSIONS in that order, any other slug after them."""
    found = {p.name[len("link_order."):-len(".txt")]: p
             for p in (ROOT / "config").glob("link_order.*.txt")}
    slugs = [v for v in VERSIONS if v in found] + sorted(set(found) - set(VERSIONS))
    return [(v, found[v]) for v in slugs]


def read_link_order(path):
    sources, members = [], []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()[0]
        if tok.startswith("data:"):
            members.append(tok[len("data:"):])
        elif tok.startswith("ico2/") and tok.endswith(".c"):
            sources.append(tok)
    return sources, members


def union(lists):
    """The lists as one: the first as it is, then each later list's items it
    lacks, each placed after the item that precedes it in its own list."""
    out = list(lists[0]) if lists else []
    for lst in lists[1:]:
        for i, item in enumerate(lst):
            if item in out:
                continue
            at = out.index(lst[i - 1]) + 1 if i and lst[i - 1] in out else 0
            out.insert(at, item)
    return out


def read_all():
    """(slugs, sources, members) over every link order."""
    orders = [(v, read_link_order(p)) for v, p in link_orders()]
    if not orders:
        sys.exit("gen_sources: no config/link_order.<slug>.txt")
    return ([v for v, _ in orders],
            union([s for _, (s, _) in orders]),
            union([m for _, (_, m) in orders]))


def cmake_list(name, items, comment):
    out = [f"# {comment}", f"set({name}"]
    out += [f"    {i}" for i in items]
    out.append(")")
    return "\n".join(out) + "\n"


def pending(sources):
    """The listed sources the tree does not hold yet."""
    return [s for s in sources if not (ROOT / s).is_file()]


def render():
    slugs, sources, members = read_all()
    waiting = set(pending(sources))
    by_prog = {p: [] for p in PROGRAMMERS}
    ee_only = []
    pend = []
    for s in sources:
        if s in waiting:
            pend.append(s)
            continue
        if is_ee_only(s):
            ee_only.append(s)
            continue
        prog = s.split("/")[1]
        if prog not in by_prog:
            sys.exit(f"gen_sources: {s}: unknown programmer directory {prog}")
        by_prog[prog].append(s)
    parts = [
        "# cmake/IcoSources.cmake: written by tools/gen_sources.py from the union of\n"
        f"# config/link_order.*.txt ({' '.join(slugs)}). Do not edit; rerun the script.\n"
        "# Order is the retail link's (pal's first).\n",
        f"set(ICO_PROGRAMMERS {' '.join(PROGRAMMERS)})\n",
    ]
    for p in PROGRAMMERS:
        parts.append(cmake_list(f"ICO_SOURCES_{p}", by_prog[p],
                                f"ico2/{p}: {len(by_prog[p])} non-renderer sources"))
    parts.append(cmake_list("ICO_EE_ONLY_SOURCES", ee_only,
                            f"{len(ee_only)} PS2-only sources (replaced on the host by port/; never "
                            "compiled here)"))
    parts.append(cmake_list("ICO_PENDING_SOURCES", pend,
                            f"{len(pend)} sources a link order lists that the tree does not hold "
                            "yet (pending the merge; never compiled)"))
    parts.append(cmake_list("ICO_DATA_MEMBERS", members,
                            f"{len(members)} data-only members of the union, build/data/<member>.c "
                            "(tools/gen_data_c.py)"))
    return "\n".join(parts)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = render()
    waiting = pending(read_all()[1])
    if waiting:
        print(f"gen_sources: {len(waiting)} listed source(s) pending the merge, not compiled: "
              + ", ".join(waiting), file=sys.stderr)
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
