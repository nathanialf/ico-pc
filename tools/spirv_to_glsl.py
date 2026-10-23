#!/usr/bin/env python3
"""tools/spirv_to_glsl.py: translate one SPIR-V module to GLSL 4.30 for the OpenGL backend.

usage: spirv_to_glsl.py --spirv-cross EXE --in NAME.spv --out NAME.glsl [--list]
       spirv_to_glsl.py --list --in A.spv [B.spv ...]      (no --out: table only)

Runs `spirv-cross --version 430 --no-es --combined-samplers-inherit-bindings`
(SPIRV-Cross from tools/fetch_deps.sh section 9; a build tool, never linked
or shipped). The SPIR-V binding map the HLSL sources are compiled to
(cmake/IcoShaders.cmake: b at 0, t at 16, s at 32, u at 48) carries over:

  uniform block        layout(binding = b, std140)         unit b
  storage buffer       layout(binding = 16 + t, std430)    SSBO binding 16 + t
  sampled texture      layout(binding = 16 + t) uniform sampler2D ...
                       (a combined sampler inherits its texture's binding; a
                       texture only read with Load gets a combined sampler with
                       a dummy sampler at the texture's unit; samplers s# have
                       no GLSL object)
  storage image        layout(binding = 48 + u)            image unit 48 + u
  dual-source output   layout(location = 0, index = 1)

A module GLSL cannot express (an input attachment: an OpTypeImage whose Dim is
SubpassData) gets an EMPTY output file, which embed_shaders.py reads as "no
GLSL" (the file must exist: it is a custom command's OUTPUT). Checks on the
text: it starts with #version 430, and every NoContraction decoration of the
SPIR-V (OpDecorate ... 42) surfaces as `precise` (a module with the
decoration and no `precise` fails). The extensions a module needs are listed
on stderr (--list: a table); a name with a trailing ? is behind an
`#if defined(...)` guard (the control-flow attributes are optional; the
fragment-shader-interlock modules, the *_dstrov builds, #error without it).
"""
import argparse
import os
import re
import struct
import subprocess
import sys

FLAGS = ["--version", "430", "--no-es", "--combined-samplers-inherit-bindings"]
OP_TYPE_IMAGE = 25
OP_DECORATE = 71
DIM_SUBPASS_DATA = 6
DEC_NO_CONTRACTION = 42


def scan(spv):
    """(has_subpass_input, no_contraction_count) of a SPIR-V module."""
    if len(spv) < 20 or len(spv) % 4 or struct.unpack("<I", spv[:4])[0] != 0x07230203:
        raise ValueError("not a SPIR-V module")
    w = struct.unpack(f"<{len(spv) // 4}I", spv)
    i, subpass, nocontr = 5, False, 0
    while i < len(w):
        n, op = w[i] >> 16, w[i] & 0xFFFF
        if n == 0 or i + n > len(w):
            raise ValueError("truncated SPIR-V instruction")
        if op == OP_TYPE_IMAGE and n >= 4 and w[i + 3] == DIM_SUBPASS_DATA:
            subpass = True
        elif op == OP_DECORATE and n >= 3 and w[i + 2] == DEC_NO_CONTRACTION:
            nocontr += 1
        i += n
    return subpass, nocontr


def extensions(text):
    """The #extension names: bare when unconditional, with a trailing ? when
    inside an #if/#ifdef guard (SPIRV-Cross guards the optional ones; the
    interlock guard ends in an #error, so there it is required in effect),
    plus GL_ARB_enhanced_layouts(offset) when a layout uses offset."""
    exts, depth = set(), 0
    for line in text.splitlines():
        s = line.strip()
        if s.startswith(("#if", "#ifdef", "#ifndef")):
            depth += 1
        elif s.startswith("#endif"):
            depth -= 1
        elif s.startswith("#extension"):
            exts.add(s.split()[1] + ("" if depth == 0 else "?"))
    if re.search(r"layout\s*\([^)]*\boffset\b", text):
        exts.add("GL_ARB_enhanced_layouts(offset)")
    return sorted(exts)


def translate(exe, src, out):
    """Returns (text, nocontraction, precise, extensions) or None for a module
    GLSL cannot express (the output is then an empty file)."""
    with open(src, "rb") as f:
        spv = f.read()
    subpass, nocontr = scan(spv)
    if subpass:
        if out:
            open(out, "wb").close()
        return None
    cmd = [exe] + FLAGS + [src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"spirv_to_glsl: {src}: spirv-cross failed: {r.stderr.strip()}")
    text = r.stdout
    if not text.startswith("#version 430"):
        sys.exit(f"spirv_to_glsl: {src}: output does not start with #version 430")
    precise = len(re.findall(r"\bprecise\b", text))
    if nocontr and not precise:
        sys.exit(f"spirv_to_glsl: {src}: {nocontr} NoContraction decorations, no `precise` in the GLSL")
    if out:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return text, nocontr, precise, extensions(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--spirv-cross", default=None, help="the spirv-cross executable")
    ap.add_argument("--in", dest="inp", nargs="+", required=True, help="SPIR-V module(s)")
    ap.add_argument("--out", help="the GLSL file (empty for a module GLSL cannot express)")
    ap.add_argument("--list", action="store_true", help="print name, bytes, extensions per module")
    a = ap.parse_args()
    if a.out and len(a.inp) != 1:
        sys.exit("spirv_to_glsl: --out takes exactly one --in")
    if not a.out and not a.list:
        sys.exit("spirv_to_glsl: nothing to do: give --out or --list")
    exe = a.spirv_cross
    if not exe or not os.access(exe, os.X_OK):
        sys.exit("spirv_to_glsl: --spirv-cross must name the spirv-cross executable")
    for src in a.inp:
        name = os.path.splitext(os.path.basename(src))[0]
        res = translate(exe, src, a.out)
        if res is None:
            msg = f"{name}\tno GLSL (input attachment)"
            print(msg, file=sys.stdout if a.list else sys.stderr)
            continue
        text, nocontr, precise, exts = res
        ex = ",".join(exts) if exts else "-"
        if a.list:
            print(f"{name}\t{len(text.encode())}\t{ex}\tnocontraction={nocontr}\tprecise={precise}")
        elif exts:
            print(f"spirv_to_glsl: {name}: needs {ex}", file=sys.stderr)


if __name__ == "__main__":
    main()
