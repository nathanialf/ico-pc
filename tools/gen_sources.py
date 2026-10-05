#!/usr/bin/env python3
"""Write cmake/IcoSources.cmake, the host build's source lists.

The lists come from config/link_order.pal.txt, the one inventory of the
game's translation units (tools/gen_ninja.py fails on a tracked source it
does not list). The host build takes its ico2/ C sources and its data-only
members, never sce/ (Sony's libraries are replaced by port/) or the VU1
microprograms (ico2/vusrc/, replaced by shaders).

Each C source lands in one list per programmer directory (the directory
decides its include path, as tools/compile_c.sh has it), or in
ICO_RENDERER_SOURCES when it is one of the files the renderer packages
rewrite (docs/port/BUILD_STATUS.md). The renderer files are compiled only
when ICO_HEADLESS is off, except HEADLESS_SIM: renderer-owned files whose
plain C the simulation needs, compiled in both modes.

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

# The renderer-owned files: they build GS/VIF packets or run VU0 inline
# assembly that the renderer packages replace (the plan's Phase 3).
RENDERER = {
    *(f"ico2/seki/src/{n}.c" for n in (
        "GsBase", "GifPacket", "DmaPacket", "DisplayList", "DisplayFont",
        "RegistPacket", "Packet", "MicroCode", "Texture", "Shadow", "ZFog",
        "Primitive", "Light", "DisplayP2O", "Matrix")),
    *(f"ico2/sugipon/src/{n}.c" for n in (
        "staticBlur", "darkVolume", "particleEffect", "matrixDrive",
        "quaternion", "clothAnimation", "lineManager")),
    "ico2/ito/src/lightning.c",
    "ico2/ito/src/queen_barrier_disp.c",
    "ico2/common/src/debug.c",
}
RENDERER_DIRS = ("ico2/ito/mpeg/",)

# Renderer-owned files the headless build compiles anyway, because the
# simulation needs their plain C (the matrix and quaternion stacks, cloth and
# chain physics, lights, shadow model data, particle state, the game-over
# dark volume, the R-register reseed in DrawLightningN). Their drawing runs
# into port/null/gfx_null.c's stubs and packet sink; the renderer waves still
# own them (docs/port/HEADLESS_STUBS.md).
HEADLESS_SIM = {
    *(f"ico2/seki/src/{n}.c" for n in (
        "GsBase", "GifPacket", "DmaPacket", "DisplayList", "DisplayFont",
        "RegistPacket", "Packet", "MicroCode", "Texture", "Shadow", "ZFog",
        "Primitive", "Light", "DisplayP2O", "Matrix")),
    *(f"ico2/sugipon/src/{n}.c" for n in (
        "matrixDrive", "quaternion", "clothAnimation", "lineManager",
        "particleEffect", "darkVolume", "staticBlur")),
    "ico2/ito/src/lightning.c",
    "ico2/ito/src/queen_barrier_disp.c",
}
RENDERER_PREFIXES = ("ico2/common/src/debug_exception",)


def is_renderer(path):
    if path in HEADLESS_SIM:
        return False
    return (path in RENDERER or path.startswith(RENDERER_DIRS)
            or path.startswith(RENDERER_PREFIXES))


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
    renderer = []
    for s in sources:
        if is_renderer(s):
            renderer.append(s)
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
    parts.append(cmake_list("ICO_RENDERER_SOURCES", renderer,
                            f"{len(renderer)} renderer-owned sources (compiled when ICO_HEADLESS is off)"))
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
