#!/usr/bin/env python3
"""tools/coverage_check.py: the regression renders (ctest coverage_corpus).

Renders every dump of tools/coverage_corpus.txt with rd_replay_tool at its own
aspect (--enhanced --present 1376x896, once with --aa off, once with --aa msaa4,
and the interpolated frame of a dump that names a PREV) and compares each with
the previous run:

  draw counts   the per-mesh MESH draws of `--list --no-device`; any mesh drawn
                fewer times than before fails (more is fine: a wider picture or
                a fix adds draws)
  picture       the percentage of pixels whose largest channel differs by more
                than `tol` from the previous run's PNG; above the dump's
                `pixels` threshold fails; a changed size fails

The previous run's pictures and counts are kept in a .coverage folder next to
the corpus (ICO_COVERAGE_DIR or --state-dir).  A first sight of a dump or
variant is seeded, not compared.  A passing item rolls its baseline forward; a
failing item keeps the old baseline, so the next run still reports it.

  check     (default) render, compare, roll forward what passed
  seed      render and overwrite every baseline (after a build you accept)
  selftest  the logic on a fake tool, no dumps, no GPU

The whole run holds the shared replay lock (see native_frame_check.py).
Exit: 0 pass, 1 regression or error, 2 usage, 77 skipped (no tool, no Pillow,
or none of the corpus' dumps exists).  Run it as `python3 -I tools/coverage_check.py`.
"""
import argparse
import concurrent.futures
import glob
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CORPUS = os.path.join(ROOT, "tools", "coverage_corpus.txt")
DEFAULT_TOOL = os.path.join(ROOT, "build-host", "linux-x64", "port", "render", "rd_replay_tool")
EXIT_SKIP = 77
PRESENT = "1376x896"
AA_MODES = ["off", "msaa4"]
DEFAULT_KEYS = {"pixels": 0.5, "tol": 8}
MESH_RE = re.compile(r"^\s*\d+:\d+\s+MESH\s+key\s+\S+\s+mesh\s+(\S+)")


def die(msg, code=1):
    print("coverage_check: " + msg, file=sys.stderr)
    sys.exit(code)


def need_pillow():
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("coverage_check: SKIP: Pillow is not installed")
        sys.exit(EXIT_SKIP)


# --------------------------------------------------------------------- corpus

def parse_keys(words, where, base="."):
    keys = {}
    for w in words:
        k, sep, v = w.partition("=")
        if not sep:
            die("%s: not KEY=VALUE: %s" % (where, w), 2)
        if k in ("pixels", "tol"):
            try:
                keys[k] = float(v)
            except ValueError:
                die("%s: bad number for %s: %s" % (where, k, v), 2)
        elif k == "interp":
            t, sep, prev = v.partition(":")
            try:
                alpha = float(t)
            except ValueError:
                alpha = -1.0
            if not sep or not prev or not 0.0 <= alpha <= 1.0:
                die("%s: interp=T:PREV with T in 0..1" % where, 2)
            keys["interp"] = (alpha, prev if os.path.isabs(prev) else os.path.join(base, prev))
        else:
            die("%s: unknown key %s" % (where, k), 2)
    return keys


def read_corpus(path, entries=None, defaults=None, depth=0):
    """{dump path: keys} in file order.  Missing dumps are kept (reported by the caller)."""
    if entries is None:
        entries, defaults = {}, dict(DEFAULT_KEYS)
    if depth > 4:
        die("include nesting too deep at " + path, 2)
    base = os.path.dirname(os.path.abspath(path))

    def full(p):
        return p if os.path.isabs(p) else os.path.join(base, p)

    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            where = "%s:%d" % (os.path.basename(path), n)
            words = line.split()
            if words[0] == "default":
                defaults.update(parse_keys(words[1:], where, base))
            elif words[0] == "include" and len(words) == 2:
                inc = full(words[1])
                if os.path.exists(inc):
                    # a native_frame_dumps.txt style list: paths, no keys
                    from native_frame_check import read_list
                    for p in read_list(inc):
                        entries.setdefault(p, dict(defaults))
                else:
                    print("  missing include: " + inc)
            else:
                pat = full(words[0])
                keys = parse_keys(words[1:], where, base)
                hits = sorted(glob.glob(pat)) if any(c in pat for c in "*?[") else [pat]
                if not hits:
                    hits = [pat]
                for p in hits:
                    ent = entries.setdefault(p, dict(defaults))
                    ent.update(keys)
    return entries, defaults


# ------------------------------------------------------------------ rendering

def list_counts(tool, dump):
    """{mesh name: draws} from `--list --no-device`, or an error string."""
    try:
        r = subprocess.run([tool, dump, "-", "--list", "--no-device"], capture_output=True,
                           text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return "list timed out"
    if r.returncode != 0:
        msg = (r.stdout + r.stderr).strip().splitlines()
        return "list failed: " + (msg[-1] if msg else "rc=%d" % r.returncode)
    counts = {}
    for ln in r.stdout.splitlines():
        m = MESH_RE.match(ln)
        if m:
            counts[m.group(1)] = counts.get(m.group(1), 0) + 1
    return counts


def render(tool, dump, out, aa, interp_prev=None, alpha=None):
    """None on success, else the error text."""
    cmd = [tool, dump, out, "--enhanced", "--present", PRESENT, "--aa", aa]
    if interp_prev:
        cmd += ["--interp", repr(alpha), interp_prev]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=1200)
    except subprocess.TimeoutExpired:
        return "render timed out"
    if r.returncode != 0 or not os.path.exists(out):
        msg = (r.stdout + r.stderr).strip().splitlines()
        return "render failed: " + (msg[-1] if msg else "rc=%d" % r.returncode)
    return None


def as_rddump(path, tmp):
    """The dump file itself: a zip's .rddump is extracted to tmp (--interp takes no zip)."""
    if not path.endswith(".zip"):
        return path
    with zipfile.ZipFile(path) as z:
        names = [n for n in z.namelist() if n.endswith(".rddump")]
        if not names:
            raise RuntimeError("no .rddump in " + path)
        dst = os.path.join(tmp, os.path.basename(path) + ".rddump")
        with z.open(names[0]) as src, open(dst, "wb") as out:
            shutil.copyfileobj(src, out)
    return dst


# ---------------------------------------------------------------- comparisons

def diff_percent(old_png, new_png, tol):
    """(percentage of pixels whose largest channel difference is above tol, size note or None)."""
    from PIL import Image, ImageChops
    a = Image.open(old_png).convert("RGB")
    b = Image.open(new_png).convert("RGB")
    if a.size != b.size:
        return 100.0, "size %dx%d -> %dx%d" % (a.size + b.size)
    d = ImageChops.difference(a, b)
    r, g, bl = d.split()
    m = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    hist = m.histogram()
    over = sum(hist[int(tol) + 1:])
    return 100.0 * over / float(a.size[0] * a.size[1]), None


def count_drops(old, new):
    """[(mesh, before, now)] for every mesh drawn fewer times than before."""
    return sorted((k, v, new.get(k, 0)) for k, v in old.items() if new.get(k, 0) < v)


def key_of(dump, variant):
    return os.path.basename(dump) + "." + variant


def variants(keys):
    out = [("aa-" + aa, aa, None) for aa in AA_MODES]
    if "interp" in keys:
        out += [("interp-aa-" + aa, aa, keys["interp"]) for aa in AA_MODES]
    return out


# ------------------------------------------------------------------------ run

def run_dump(tool, dump, keys, state, seed, tmp):
    """Renders and compares one dump; returns (fails [str], notes [str], new_state_ops)."""
    from PIL import Image  # noqa: F401
    name = os.path.basename(dump)
    fails, notes, promote = [], [], []
    counts = list_counts(tool, dump)
    cpath = os.path.join(state, "counts.json")
    if isinstance(counts, str):
        return ["%s: %s" % (name, counts)], notes, promote, None
    old_counts = None
    if os.path.exists(cpath) and not seed:
        with open(cpath, encoding="utf-8") as f:
            old_counts = json.load(f).get(name)
    ok = True
    if old_counts is None:
        notes.append("%s: draw counts seeded (%d meshes, %d draws)" % (name, len(counts), sum(counts.values())))
    else:
        drops = count_drops(old_counts, counts)
        if drops:
            ok = False
            for mesh, was, now in drops[:12]:
                fails.append("%s: mesh %s drawn %d times, was %d" % (name, mesh, now, was))
            if len(drops) > 12:
                fails.append("%s: ... and %d more meshes drawn fewer times" % (name, len(drops) - 12))
    for label, aa, interp in variants(keys):
        new_png = os.path.join(tmp, key_of(dump, label) + ".png")
        if interp:
            prev = as_rddump(interp[1], tmp)
            err = render(tool, dump if not dump.endswith(".zip") else as_rddump(dump, tmp), new_png, aa, prev, interp[0])
        else:
            err = render(tool, dump, new_png, aa)
        if err:
            fails.append("%s [%s]: %s" % (name, label, err))
            ok = False
            continue
        base = os.path.join(state, key_of(dump, label) + ".png")
        if seed or not os.path.exists(base):
            notes.append("%s [%s]: picture seeded" % (name, label))
            promote.append((new_png, base))
            continue
        pct, note = diff_percent(base, new_png, keys["tol"])
        if pct > keys["pixels"]:
            ok = False
            fails.append("%s [%s]: %s differs from the previous run: %.3f%% of pixels (limit %.3f%%, tol %d)"
                         % (name, label, note or "picture", pct, keys["pixels"], keys["tol"]))
        else:
            promote.append((new_png, base))
    return fails, notes, promote, (counts if ok else None)


def cmd_run(args, seed):
    need_pillow()
    tool = args.tool or os.environ.get("ICO_REPLAY_TOOL") or DEFAULT_TOOL
    if not os.access(tool, os.X_OK):
        print("coverage_check: SKIP: no rd_replay_tool at %s" % tool)
        return EXIT_SKIP
    if getattr(args, "ctest", False) and not args.corpus and not os.environ.get("ICO_COVERAGE_CORPUS"):
        # the ctest coverage_corpus is local-only validation: it renders dumps kept outside
        # the repository, which a test never reads by default (docs/port/TESTING.md)
        print("coverage_check: SKIP: local-only validation: ICO_COVERAGE_CORPUS names no corpus "
              "list (0 dumps to render)")
        return EXIT_SKIP
    corpus = args.corpus or os.environ.get("ICO_COVERAGE_CORPUS") or DEFAULT_CORPUS
    if not os.path.exists(corpus):
        print("coverage_check: SKIP: no corpus (%s)" % corpus)
        return EXIT_SKIP
    entries, _ = read_corpus(corpus)
    have = {}
    for p, k in entries.items():
        if os.path.isfile(p):
            have[p] = k
        else:
            print("  missing dump: " + p)
    if args.only:
        have = {p: k for p, k in have.items() if args.only in os.path.basename(p)}
    if not have:
        print("coverage_check: SKIP: none of the corpus' %d dumps exists" % len(entries))
        return EXIT_SKIP
    names = [os.path.basename(p) for p in have]
    if len(set(names)) != len(names):
        die("two dumps share a file name; the state keys on the name")
    state = args.state_dir or os.environ.get("ICO_COVERAGE_DIR") or os.path.join(os.path.dirname(os.path.abspath(corpus)), ".coverage")
    os.makedirs(state, exist_ok=True)
    cpath = os.path.join(state, "counts.json")
    allcounts = {}
    if os.path.exists(cpath):
        with open(cpath, encoding="utf-8") as f:
            allcounts = json.load(f)
    from native_frame_check import Lock
    fails, total = [], 0
    with Lock(), tempfile.TemporaryDirectory(prefix="cov") as tmp:
        for dump, keys in have.items():
            print("  %s" % os.path.basename(dump), flush=True)
            try:
                f, notes, promote, newcounts = run_dump(tool, dump, keys, state, seed, tmp)
            except (OSError, RuntimeError, zipfile.BadZipFile) as e:
                f, notes, promote, newcounts = ["%s: %s" % (os.path.basename(dump), e)], [], [], None
            total += 1
            fails += f
            for n in notes:
                print("    " + n)
            for src, dst in promote:
                shutil.copyfile(src, dst + ".new")
                os.replace(dst + ".new", dst)
            if newcounts is not None:
                allcounts[os.path.basename(dump)] = newcounts
                with open(cpath + ".new", "w", encoding="utf-8") as fh:
                    json.dump(allcounts, fh, indent=0, sort_keys=True)
                os.replace(cpath + ".new", cpath)
    for line in fails:
        print("REGRESSION " + line)
    print("coverage_check: %d dumps, %d problems%s" % (total, len(fails), " (seeded)" if seed else ""))
    return 1 if fails else 0


# ------------------------------------------------------------------- selftest

FAKE_TOOL = """#!%s
import json, sys
from PIL import Image
dump, out = sys.argv[1], sys.argv[2]
if dump.endswith(".zip"):
    import zipfile
    z = zipfile.ZipFile(dump)
    spec = json.loads(z.read([n for n in z.namelist() if n.endswith(".rddump")][0]))
else:
    spec = json.load(open(dump))
if "--list" in sys.argv:
    for i, (m, n) in enumerate(sorted(spec["meshes"].items())):
        for j in range(n):
            print(" 0:%%d    MESH           key 00 mesh %%s (3 vertices, 1 batches) prog 1" %% (i * 100 + j, m))
    sys.exit(0)
if spec.get("fail"):
    print("boom")
    sys.exit(3)
c = tuple(spec["colour"])
im = Image.new("RGB", (spec.get("w", 40), 30), c)
if "--interp" in sys.argv:
    im.putpixel((0, 0), (255, 255, 255))
im.save(out)
"""


def cmd_selftest(args):
    need_pillow()
    from PIL import Image
    ok = True

    def expect(label, cond):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + label)
        ok = ok and cond

    expect("count drop found", count_drops({"a": 3, "b": 2}, {"a": 3, "b": 1}) == [("b", 2, 1)])
    expect("count gain and a new mesh pass", count_drops({"a": 3}, {"a": 5, "c": 1}) == [])
    expect("a vanished mesh is a drop", count_drops({"a": 3}, {}) == [("a", 3, 0)])
    with tempfile.TemporaryDirectory(prefix="covst") as td:
        tool = os.path.join(td, "fake_tool.py")
        with open(tool, "w", encoding="utf-8") as f:
            f.write(FAKE_TOOL % sys.executable)
        os.chmod(tool, os.stat(tool).st_mode | stat.S_IXUSR)
        dumps = os.path.join(td, "dumps")
        os.makedirs(dumps)

        def put(name, **spec):
            with open(os.path.join(dumps, name), "w", encoding="utf-8") as f:
                json.dump(spec, f)

        put("one.rddump", meshes={"m1": 3, "m2": 2}, colour=[10, 20, 30])
        corpus = os.path.join(td, "corpus.txt")
        with open(corpus, "w", encoding="utf-8") as f:
            f.write("default pixels=1 tol=4\ndumps/one.rddump\ndumps/gone.rddump\n")
        state = os.path.join(td, "state")

        class A:
            pass

        def run(seed=False):
            a = A()
            a.tool, a.corpus, a.only, a.state_dir = tool, corpus, None, state
            os.environ["ICO_REPLAY_LOCK"] = os.path.join(td, "lock")
            return cmd_run(a, seed)

        expect("first run seeds and passes", run() == 0)
        expect("baselines written", os.path.exists(os.path.join(state, "counts.json"))
               and os.path.exists(os.path.join(state, "one.rddump.aa-off.png")))
        expect("an unchanged run passes", run() == 0)
        put("one.rddump", meshes={"m1": 3, "m2": 2, "m3": 1}, colour=[12, 20, 30])
        expect("more draws and a change inside tol pass", run() == 0)
        put("one.rddump", meshes={"m1": 3, "m2": 1, "m3": 1}, colour=[12, 20, 30])
        expect("a draw-count drop fails", run() == 1)
        expect("the failing run kept the old counts", json.load(open(os.path.join(state, "counts.json")))["one.rddump"]["m2"] == 2)
        put("one.rddump", meshes={"m1": 3, "m2": 2, "m3": 1}, colour=[100, 20, 30])
        expect("a picture change above the limit fails", run() == 1)
        put("one.rddump", meshes={"m1": 3, "m2": 2, "m3": 1}, colour=[12, 20, 30], w=44)
        expect("a size change fails", run() == 1)
        put("one.rddump", meshes={"m1": 3, "m2": 2, "m3": 1}, colour=[12, 20, 30], fail=True)
        expect("a failing render fails", run() == 1)
        put("one.rddump", meshes={"m1": 1}, colour=[100, 20, 30])
        expect("seed overwrites", run(True) == 0 and
               json.load(open(os.path.join(state, "counts.json")))["one.rddump"] == {"m1": 1})
        a, _ = read_corpus(corpus)
        expect("corpus keys", a[os.path.join(td, "dumps", "one.rddump")]["pixels"] == 1.0)
        # interp variants and the zip extraction
        with zipfile.ZipFile(os.path.join(dumps, "z.zip"), "w") as z:
            z.write(os.path.join(dumps, "one.rddump"), "inner.rddump")
        with open(corpus, "w", encoding="utf-8") as f:
            f.write("dumps/z.zip interp=0.5:dumps/z.zip pixels=0\n")
        expect("zip dumps and an interp variant", run(True) == 0
               and os.path.exists(os.path.join(state, "z.zip.interp-aa-msaa4.png")))
        os.environ.pop("ICO_REPLAY_LOCK", None)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="mode")

    def common(p):
        p.add_argument("--tool", help="rd_replay_tool (default: ICO_REPLAY_TOOL or build-host/linux-x64/...)")
        p.add_argument("--corpus", help="corpus file (default: ICO_COVERAGE_CORPUS, then tools/coverage_corpus.txt)")
        p.add_argument("--state-dir", help="the previous run's pictures (default: ICO_COVERAGE_DIR, then .coverage beside the corpus)")
        p.add_argument("--only", help="only dumps whose file name contains this text")
        p.add_argument("--ctest", action="store_true",
                       help="the ctest: skip (77) unless --corpus or ICO_COVERAGE_CORPUS names a local list")

    common(sub.add_parser("check", help="render, compare with the previous run, roll forward what passed"))
    common(sub.add_parser("seed", help="render and overwrite every baseline"))
    sub.add_parser("selftest", help="the logic on a fake tool")
    args = ap.parse_args()
    mode = args.mode or "check"
    if args.mode is None:
        args = ap.parse_args(["check"])
    if mode == "selftest":
        rc = cmd_selftest(args)
    else:
        rc = cmd_run(args, mode == "seed")
    if rc == EXIT_SKIP:
        print("coverage_check: SKIP")
    sys.exit(rc)


if __name__ == "__main__":
    main()
