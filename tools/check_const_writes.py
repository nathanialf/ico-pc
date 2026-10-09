#!/usr/bin/env python3
"""check_const_writes.py: no game unit writes an object it declares const.

The PS2 build keeps several tables in .rodata that the game writes at run
time (the EE has no page protection): SetNodeRotationLimitDataTable
reorders motionLimitDef's rows in place, for one.  gcc keeps such a store;
clang deletes it, because a store to an object declared const is undefined
behaviour and LLVM marks the object `constant`.  Issue 19 was that: on
Android (the NDK's clang) the shoulder limits stayed in the disc's order and
Ico's and Yorda's arms went straight up while they held hands, where the gcc
builds were right.

This tool compiles each C unit of a build's compile_commands.json with the
build's own clang to unoptimised LLVM IR (the stores are still there) and
reports every store, memcpy, memmove or memset whose destination is derived
from a global the unit declares `constant`: through the address itself,
getelementptr and casts, and pointer locals (an -O0 alloca a derived
pointer was stored to, then loaded back).

usage: check_const_writes.py --build DIR [--cc CLANG] [--jobs N] [--all]
  --build DIR  a configured build tree with compile_commands.json
  --cc CLANG   the clang to use (default: each entry's own compiler, which
               must be clang)
  --all        every C unit of the tree, not only the game's (ico2/, sce/
               and the generated tables, port/data/gen/)
Prints `file:line: function writes const global` per finding and exits 1
when there is one; exits 77 when the build's compiler is not clang and no
--cc is given (the hazard and the IR are clang's).
"""
import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

GAME_DIRS = ("/ico2/", "/sce/", "/port/data/gen/")
DROP_WITH_ARG = {"-o", "-MF", "-MT", "-MQ"}
DROP = {"-c", "-MD", "-MMD", "-MP"}

GLOBAL_CONST = re.compile(r"^@([\w.$]+) = [^\"]*?\bconstant\b")
DEFINE = re.compile(r"^define .*?@([\w.$\"]+)\(")
SSA = re.compile(r"^\s*(%[\w.]+) = (.*)$")
DBG = re.compile(r"!dbg !(\d+)")
LOC = re.compile(r"^!(\d+) = !DILocation\(line: (\d+)")
MEMCALL = re.compile(r"call .*?@llvm\.(memcpy|memmove|memset)\.[\w.]*\((.*)\)")


def split_top(s):
    """Comma-separated operands at nesting depth 0."""
    out, depth, cur = [], 0, []
    for c in s:
        if c in "([{<":
            depth += 1
        elif c in ")]}>":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if cur:
        out.append("".join(cur).strip())
    return out


def scan_ir(text):
    """[(function, global, line)] of the writes into `constant` globals."""
    lines = text.split("\n")
    consts = {m.group(1) for m in map(GLOBAL_CONST.match, lines) if m}
    if not consts:
        return []
    locs = {}
    for l in lines:
        m = LOC.match(l)
        if m:
            locs[m.group(1)] = int(m.group(2))
    hits = []
    func = None
    base = {}  # SSA value -> the constant global it points into
    slot = {}  # alloca -> the constant global a pointer stored to it points into

    def origin(operand):
        for name in re.findall(r"%[\w.]+", operand):
            if name in base:
                return base[name]
        for name in re.findall(r"@([\w.$]+)", operand):
            if name in consts:
                return name
        return None

    for l in lines:
        m = DEFINE.match(l)
        if m:
            func, base, slot = m.group(1).strip('"'), {}, {}
            continue
        if func is None:
            continue
        body = DBG.sub("", l.split(", !", 1)[0] if ", !" in l else l)
        m = SSA.match(body)
        if m:
            name, rhs = m.group(1), m.group(2)
            op = rhs.split(None, 1)[0] if rhs else ""
            if op in ("getelementptr", "bitcast", "addrspacecast", "select", "phi"):
                g = origin(rhs)
                if g:
                    base[name] = g
            elif op == "load":
                ops = split_top(rhs.split(None, 1)[1]) if " " in rhs else []
                if ops and ops[0].startswith("ptr") and len(ops) > 1:
                    src = re.findall(r"%[\w.]+", ops[1])
                    if src and src[0] in slot:
                        base[name] = slot[src[0]]
            continue
        s = body.strip()
        dest = None
        if s.startswith("store "):
            ops = split_top(s[len("store "):])
            ops = [o for o in ops if not o.startswith("align ")]
            if len(ops) >= 2:
                dest, value = ops[-1], ops[0]
                g = origin(value) if value.startswith("ptr") else None
                d = re.findall(r"%[\w.]+", dest)
                if g and d:
                    # a pointer into a constant global kept in a local
                    slot[d[0]] = g
        else:
            m = MEMCALL.search(s)
            if m:
                dest = split_top(m.group(2))[0]
        if dest is None:
            continue
        g = origin(dest)
        if g:
            dm = DBG.search(l)
            hits.append((func, g, locs.get(dm.group(1)) if dm else None))
    return hits


def ir_command(entry, cc, out):
    args = entry.get("arguments") or shlex.split(entry["command"])
    res = [cc or args[0]]
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in DROP_WITH_ARG:
            skip = True
            continue
        if a in DROP or a.startswith("-O") or a.startswith("-g"):
            continue
        if a == entry["file"]:
            continue
        res.append(a)
    res += ["-O0", "-gline-tables-only", "-w", "-S", "-emit-llvm", "-o", out, entry["file"]]
    return res


def check(job, cc, tmp):
    n, entry = job
    out = os.path.join(tmp, "%d.ll" % n)
    r = subprocess.run(ir_command(entry, cc, out), cwd=entry["directory"], capture_output=True,
                       text=True)
    if r.returncode != 0:
        return entry["file"], None, r.stderr.strip().splitlines()[-1:] or ["failed"]
    with open(out, encoding="utf-8", errors="replace") as f:
        hits = scan_ir(f.read())
    os.remove(out)
    return entry["file"], hits, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", required=True)
    ap.add_argument("--cc")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    db = os.path.join(a.build, "compile_commands.json")
    if not os.path.isfile(db):
        print("check_const_writes: no %s" % db)
        return 1
    entries, seen = [], set()
    for e in json.load(open(db)):
        f = e["file"]
        path = f if os.path.isabs(f) else os.path.join(e["directory"], f)
        if not f.endswith(".c") or f in seen or "third_party" in f or not os.path.isfile(path):
            continue
        if not a.all and not any(d in f for d in GAME_DIRS):
            continue
        seen.add(f)
        entries.append(e)
    if not entries:
        print("check_const_writes: no unit to check in %s" % db)
        return 1
    if not a.cc:
        first = entries[0].get("arguments") or shlex.split(entries[0]["command"])
        probe = subprocess.run([first[0], "--version"], capture_output=True, text=True)
        if "clang" not in probe.stdout.lower():
            print("check_const_writes: SKIP (%s is not clang; pass --cc)" % first[0])
            return 77
    bad = failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        with concurrent.futures.ThreadPoolExecutor(max(1, a.jobs)) as pool:
            for f, hits, err in pool.map(lambda j: check(j, a.cc, tmp), enumerate(entries)):
                if err is not None:
                    failed += 1
                    print("%s: could not be compiled to IR: %s" % (f, err[0]))
                    continue
                for func, g, line in sorted(set(hits), key=lambda h: (h[2] or 0, h[0], h[1])):
                    bad += 1
                    print("%s:%s: %s writes %s, which the unit declares const" %
                          (f, line if line else "?", func, g))
    print("check_const_writes: %d units, %d writes to const objects%s" %
          (len(entries), bad, ", %d units not compiled" % failed if failed else ""))
    return 1 if bad or failed else 0


if __name__ == "__main__":
    sys.exit(main())
