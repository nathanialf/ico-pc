#!/usr/bin/env python3
"""tools/check_hlsl_fxc.py [--allow FILE] [DIR_OR_FILE ...]

Flags a word FXC reserves (D3DCompile, which the Direct3D 11 backend uses at
run time, vs_5_0 / ps_5_0) used as a declared identifier in port/shaders'
HLSL.  DXC (Vulkan, OpenGL, Direct3D 12) accepts such names, so only a
Windows player's log showed the error (v0.9.0: `bool pass = ...` made every
Direct3D 11 shader fail with "error X3000: syntax error: unexpected token
'pass'").

A declaration is `<type> <word>` followed by `=`, `;`, `,`, `)`, `[` or `:`
(a struct member or a parameter with a semantic), with comments and strings
removed.  A reserved word used as a keyword (precise, nointerpolation,
typedef, static, ...) is not a declaration and is not flagged.

The allow-list file (default tools/check_hlsl_fxc_allow.txt when present)
holds `file:line: word` or `file: word` lines, `#` comments.

Not caught: FXC's X4000 "use of potentially uninitialized variable" error
class (it needs FXC's own flow analysis; a scanner cannot see it), and any
other FXC-only error; those show first in a Windows player's log.

Exit status 1 when there is a finding.
"""
import os
import re
import sys

RESERVED = set("""
pass technique technique10 technique11 sample line point precise linear
centroid matrix vector string texture sampler shared export interface class
template typedef stateblock stateblock_state compile compile_fragment asm
asm_fragment fxgroup pixelfragment vertexfragment pixelshader vertexshader
geometryshader inline packoffset register cbuffer tbuffer static extern
uniform volatile in out inout groupshared nointerpolation noperspective
snorm unorm dword half double min16float min10float min16int min12int
min16uint void auto case catch char const_cast delete dynamic_cast enum
explicit friend goto long mutable namespace new operator private protected
public reinterpret_cast short signed sizeof static_cast throw try typename
union unsigned using virtual
""".split())

# a "type" token that is a statement keyword, not a type: `return pass;`
NOT_TYPES = {"return", "else", "case", "goto", "discard", "do", "typedef"}

DECL = re.compile(
    r"\b([A-Za-z_]\w*)(?:\s*<[^<>;(){}]*>)?\s+([A-Za-z_]\w*)\s*(?=[=;,)\[:])")


def strip(text):
    """Comments, strings and preprocessor lines blanked, newlines kept."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    text = re.sub(r"//[^\n]*", blank, text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', blank, text)
    # preprocessor lines (with continuations)
    text = re.sub(r"^[ \t]*#(?:[^\n\\]|\\\n|\\.)*", blank, text, flags=re.M)
    return text


def scan(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = strip(f.read())
    out = []
    for m in DECL.finditer(text):
        typ, word = m.group(1), m.group(2)
        if word not in RESERVED or typ in NOT_TYPES:
            continue
        line = text.count("\n", 0, m.start(2)) + 1
        out.append((line, word, typ))
    return out


def load_allow(path):
    allow = set()
    if path and os.path.exists(path):
        for raw in open(path, encoding="utf-8"):
            raw = raw.split("#", 1)[0].strip()
            if raw:
                allow.add(raw)
    return allow


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    allow_path = os.path.join(root, "tools", "check_hlsl_fxc_allow.txt")
    args = argv[1:]
    if args[:1] == ["--allow"]:
        allow_path, args = args[1], args[2:]
    if not args:
        args = [os.path.join(root, "port", "shaders")]
    files = []
    for a in args:
        if os.path.isdir(a):
            for dp, _, fn in os.walk(a):
                for n in sorted(fn):
                    if n.endswith((".hlsl", ".hlsli")):
                        files.append(os.path.join(dp, n))
        else:
            files.append(a)
    allow = load_allow(allow_path)
    bad = 0
    for p in sorted(files):
        rel = os.path.relpath(p, root)
        for line, word, typ in scan(p):
            if (f"{rel}:{line}: {word}" in allow or f"{rel}: {word}" in allow):
                continue
            print(f"{rel}:{line}: FXC reserves '{word}' (declared as '{typ} {word}'); rename it")
            bad += 1
    if bad:
        print(f"check_hlsl_fxc: {bad} reserved word(s) used as identifiers")
        return 1
    print(f"check_hlsl_fxc: {len(files)} file(s), no reserved word used as an identifier")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
