#!/usr/bin/env python3
"""tools/softdouble_gate.py [--root DIR] [--build BUILD_DIR]

The gate for the soft doubles: the game functions that did
`double` arithmetic through the EE's soft float (33 functions, and the two
static inline helpers expanded into them) must do it through port/math/softdouble.h, so
that no host double arithmetic, under whatever rounding mode, reaches them
again.

Checked in each function's source text (comments and strings removed):
  - no `double` keyword, except `va_arg(ap, double)` (a variadic double must
    be read as one; the bits then go through ico_dbits);
  - no unsuffixed floating constant (a double) outside ICO_D(...).
With --build (a native build directory with compile_commands.json), the
files are also compiled with -Wdouble-promotion -Wfloat-conversion, and a
warning that mentions `double` inside one of the functions fails the gate
(an implicit float-to-double promotion, a variadic float argument included).

Exit 0 when clean, 1 with one line per finding.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys

# (source file under ico2/, function); a .c.inc is compiled through its .c
FUNCTIONS = [
    ("common/src/debug.c", "debug_PrintFontf"),
    ("common/src/layout_texture.c", "display_texture"),
    ("common/src/layout_texture.c", "lt_glow_sprite"),
    ("fumi/src/act-wish.c", "ACTGetWish_FromPad"),
    ("fumi/src/boyact.c", "actBoyRun"),
    ("fumi/src/boyact.c", "actBoyWalk"),
    ("fumi/src/commonact.c", "WithMailFunc_FallDead"),
    ("fumi/src/commonact.c", "actCommonFall"),
    ("fumi/src/enemy_act.c", "Battle_isCurrentStatus"),
    ("fumi/src/enemy_act.c", "battleRangeScale"),
    ("fumi/src/enemy_act.c", "NakaBoss"),
    ("fumi/src/enemy_act.c", "actEnemyKidnapEnd"),
    ("fumi/src/girl_act_hand.c.inc", "HandMgr_Speed"),
    ("fumi/src/girl_brain_attract.c.inc", "subGirlBrain_Attract"),
    ("omori/src/attackhit.c", "inner_check"),
    ("omori/src/brain.c", "brainLevelProcess"),
    ("omori/src/camera-ico2.c", "monitorMonitorCamera"),
    ("omori/src/camera-root.c", "SetCameraMatrix"),
    ("omori/src/chain.c", "chain_simulate_term_down"),
    ("omori/src/chain.c", "chain_simulate_term_moveup"),
    ("omori/src/chain.c", "chain_simulate_term_free"),
    ("omori/src/chain.c", "chain_simulate_term_loop"),
    ("omori/src/chain.c", "chain_simulate_term_swingready"),
    ("omori/src/chain.c", "chain_simulate_term_swingstart"),
    ("omori/src/chain.c", "pendulum_Process"),
    ("script/src/script.c", "scpWoodSrh"),
    ("script/src/st04a.c", "actSt04aGateChk"),
    ("script/src/st04e.c", "actSt04eSeChk"),
    ("script/src/st04e.c", "actSt04eWaterFlagOn"),
    ("script/src/st05e.c", "actSt05eWaterFlagOn"),
    ("script/src/st06a.c", "actSt06aSuimonFlagOn"),
    ("script/src/st13b.c", "actSt13bConte02"),
    ("script/src/st13b.c", "actSt13bElev2Chk"),
    ("script/src/st25a.c", "actSt25aElevChk"),
    ("sugipon/src/waterDot.c", "setWaterDot"),
]
TU_OF = {
    "fumi/src/girl_act_hand.c.inc": "fumi/src/girl_act.c",
    "fumi/src/girl_brain_attract.c.inc": "fumi/src/girl_act.c",
}

FLOAT_LIT = re.compile(
    r"(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?|\d+[eE][-+]?\d+)([fFlL]?)(?![\w.])"
)


def blank(text):
    """Comments and string/char literals to spaces, newlines kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def find_function(text, name):
    """(start, end) offsets of the function's body braces, or None."""
    for m in re.finditer(r"^[A-Za-z_][^\n;]*\b%s\s*\(" % re.escape(name), text, re.M):
        line_end = text.find("\n", m.start())
        line = text[m.start() : line_end]
        if line.rstrip().endswith(");"):
            continue  # a prototype
        b = text.find("{", m.end())
        if b < 0:
            return None
        depth = 0
        for k in range(b, len(text)):
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0:
                    return (m.start(), k + 1)
    return None


def strip_icod(body):
    """Remove ICO_D(...) spans (balanced parentheses)."""
    out = []
    i = 0
    while True:
        j = body.find("ICO_D(", i)
        if j < 0:
            out.append(body[i:])
            return "".join(out)
        out.append(body[i:j])
        depth = 0
        k = j + len("ICO_D")
        while k < len(body):
            if body[k] == "(":
                depth += 1
            elif body[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        out.append(" " * (k + 1 - j))
        i = k + 1


def line_of(text, off):
    return text.count("\n", 0, off) + 1


def source_checks(root):
    findings = []
    ranges = {}
    for rel, fn in FUNCTIONS:
        path = os.path.join(root, "ico2", rel)
        with open(path, "rb") as f:
            text = blank(f.read().decode("latin-1"))
        span = find_function(text, fn)
        if span is None:
            findings.append("%s: %s: definition not found" % (rel, fn))
            continue
        s, e = span
        ranges.setdefault(rel, []).append((line_of(text, s), line_of(text, e), fn))
        body = text[s:e]
        body_v = re.sub(r"va_arg\s*\(\s*\w+\s*,\s*double\s*\)", lambda m: " " * len(m.group()), body)
        for m in re.finditer(r"\bdouble\b", body_v):
            findings.append("%s:%d: %s: `double`" % (rel, line_of(text, s + m.start()), fn))
        body_d = strip_icod(body)
        for m in FLOAT_LIT.finditer(body_d):
            if m.group(2) not in ("f", "F"):
                findings.append(
                    "%s:%d: %s: double constant %s outside ICO_D()"
                    % (rel, line_of(text, s + m.start()), fn, m.group(0))
                )
    return findings, ranges


def warning_checks(root, build, ranges):
    findings = []
    with open(os.path.join(build, "compile_commands.json")) as f:
        db = json.load(f)
    by_file = {os.path.realpath(e["file"]): e for e in db}
    tus = sorted({TU_OF.get(rel, rel) for rel, _ in FUNCTIONS})
    for tu in tus:
        path = os.path.realpath(os.path.join(root, "ico2", tu))
        e = by_file.get(path)
        if e is None:
            findings.append("%s: not in compile_commands.json" % tu)
            continue
        cmd = e["arguments"] if "arguments" in e else shlex.split(e["command"])
        if "-o" in cmd:
            i = cmd.index("-o")
            del cmd[i : i + 2]
        cmd = [c for c in cmd if c != "-c"]
        cmd += ["-fsyntax-only", "-Wdouble-promotion", "-Wfloat-conversion"]
        r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, text=True, errors="replace")
        for m in re.finditer(r"^(.*?):(\d+):\d+: warning: (.*)$", r.stderr, re.M):
            wfile, wline, msg = m.group(1), int(m.group(2)), m.group(3)
            if "double" not in msg:
                continue
            wpath = os.path.realpath(os.path.join(e["directory"], wfile))
            rel = os.path.relpath(wpath, os.path.join(root, "ico2"))
            for lo, hi, fn in ranges.get(rel, []):
                if lo <= wline <= hi:
                    findings.append("%s:%d: %s: %s" % (rel, wline, fn, msg))
    return findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), ".."))
    ap.add_argument("--build")
    a = ap.parse_args()
    root = os.path.realpath(a.root)
    findings, ranges = source_checks(root)
    if a.build:
        findings += warning_checks(root, a.build, ranges)
    for f in findings:
        print(f)
    print(
        "softdouble_gate: %d functions, %d findings%s"
        % (len(FUNCTIONS), len(findings), "" if a.build else " (source checks only)")
    )
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
