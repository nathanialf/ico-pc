#!/usr/bin/env python3
"""Fill and refresh CI's cost tables (tools/ci_costs/<suite>.txt) for tools/ci_shard.py.

    tools/ci_costs_update.py fetch-ci <run id> <dir>
    tools/ci_costs_update.py update (--build-dir <dir>... | --tests <json>...) \\
        [--ci-logs <dir>] [--local-log <log>...] [--assume <test>=<seconds>...] \\
        [--floor S] [--write]
    tools/ci_costs_update.py plan (--build-dir <dir>... | --tests <json>...) \\
        [--target-min M] [--parallel P]
    tools/ci_costs_update.py --selftest

`fetch-ci` saves the `tests` and `linux-arm64*` job logs of a finished CI run
as <dir>/<suite>_<k>_<n>.log and the run's id, date and commit as
<dir>/run.txt (read-only GitHub queries through `gh`).

`update` makes each suite's table:

1. the base: with --ci-logs, every test's seconds from that suite's job logs
   (`ci_shard.py costs`, the largest time of a test that appears twice);
   without, the committed table;
2. the tests the build registers (--build-dir: its CTestTestfile.cmake files
   are read, nothing is run; --tests: `ctest --show-only=json-v1` output;
   several give the union) that the base lacks, and the tests that replay the
   committed fixtures (a `-DFIXTURES=` argument) that the base holds below
   --floor seconds (the 0.00 of the days they skipped in CI for want of a
   machine-local dump), get an estimate: the test's newest passing time in the
   local logs (--local-log; by default the merge checks' logs
   build-host/tmp/release/logs/merge-*.{first,rest,lvp}.log, newest file
   last) times the suite's host-to-CI ratio. The ratio is measured on the
   tests both hold (at least 5 s in each): the sum of their CI seconds over
   the sum of their local seconds, one ratio for the tests labelled `render`
   and one for the rest (the render ratio when the rest have fewer than 5).
   --assume NAME=SECONDS gives a local time for a test no log has (a hand
   measurement); a fixture test with neither gets the median of the suite's
   other fixture estimates, any other test the table's median, and both are
   listed as unmeasured;
3. each estimated line carries its source as a comment, so a later CI refresh
   (step 1) replaces it with the measured time.

Without --write it prints what it would change; with --write it rewrites the
tables. Run it after the last package that adds or converts a test, and again
with --ci-logs after the first green CI run that runs them
(docs/port/TESTING.md, "The CI shards").

`plan` prints, per suite, the tests' total, the shards the workflow gives it
(the `shards:` of its `tests` matrix lines in .github/workflows/ci.yml), each
shard's estimated minutes at --parallel P (ci_shard.partition), and the
fewest shards that keep every shard under --target-min minutes of tests (or,
when the suite's longest test is longer, the fewest that reach that floor).
"""

import argparse
import glob
import json
import os
import re
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ci_shard  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SUITES = ("linux-x64", "linux-x64-window", "linux-x64-clang", "asan", "fptrap", "linux-arm64")
# the jobs of a CI run whose logs feed each table (the job name's prefix)
JOB_SUITE = (("linux-arm64", "linux-arm64"), ("linux-x64-window ", "linux-x64-window"),
             ("linux-x64-clang ", "linux-x64-clang"), ("linux-x64 ", "linux-x64"),
             ("asan ", "asan"), ("fptrap ", "fptrap"))
DEFAULT_LOCAL = "build-host/tmp/release/logs/merge-*.%s.log"
_RESULT = re.compile(r"Test +#\d+: (\S+) \.+\s*(\S+)\s+([0-9]+(?:\.[0-9]+)?) sec")
_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|\[(=*)\[(.*?)\]\2\]|([^\s()"]+)', re.S)
MIN_OVERLAP = 5.0


# ---- the build's tests, without running ctest ----

def _tokens(text):
    out = []
    for m in _TOKEN.finditer(text):
        if m.group(1) is not None:
            out.append(re.sub(r"\\(.)", r"\1", m.group(1)))
        elif m.group(3) is not None:
            out.append(m.group(3))
        else:
            out.append(m.group(4))
    return out


def _calls(text, name):
    """The argument lists of each `name(...)` call in a CTestTestfile.cmake."""
    for m in re.finditer(r"^%s\(" % re.escape(name), text, re.M):
        i, depth, q = m.end(), 1, None
        j = i
        while j < len(text) and depth:
            c = text[j]
            if q:
                if c == "\\":
                    j += 1
                elif c == q:
                    q = None
            elif c == '"':
                q = '"'
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            j += 1
        yield _tokens(text[i:j - 1])


def _testfiles(build_dir):
    """The build's CTestTestfile.cmake files, ctest's own walk: the top one,
    then each `subdirs(...)` it names, in order."""
    out, todo = [], [os.path.abspath(build_dir)]
    while todo:
        d = todo.pop(0)
        path = os.path.join(d, "CTestTestfile.cmake")
        if not os.path.exists(path):
            continue
        out.append(path)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        todo[0:0] = [os.path.join(d, sub) for args in _calls(text, "subdirs") for sub in args]
    if not out:
        sys.exit(f"ci_costs_update: no CTestTestfile.cmake in {build_dir}")
    return out


def read_build_dir(build_dir):
    """[{name, fixtures, labels, command}] from a build's CTestTestfile.cmake files."""
    tests, props = {}, {}
    order = []
    for path in _testfiles(build_dir):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for args in _calls(text, "add_test"):
            if args:
                if args[0] not in tests:
                    order.append(args[0])
                tests[args[0]] = args[1:]
        for args in _calls(text, "set_tests_properties"):
            if "PROPERTIES" not in args:
                continue
            k = args.index("PROPERTIES")
            names, kv = args[:k], args[k + 1:]
            for n in names:
                d = props.setdefault(n, {})
                for a in range(0, len(kv) - 1, 2):
                    d[kv[a]] = kv[a + 1]
    out = []
    for n in order:
        p = props.get(n, {})
        fixtures = []
        for key in ("FIXTURES_SETUP", "FIXTURES_REQUIRED", "FIXTURES_CLEANUP"):
            fixtures += [v for v in p.get(key, "").split(";") if v]
        out.append({"name": n, "fixtures": fixtures,
                    "labels": [v for v in p.get("LABELS", "").split(";") if v],
                    "command": tests[n]})
    return out


def read_all_tests(build_dirs, jsons):
    seen, out = set(), []
    for t in [t for d in build_dirs or [] for t in read_build_dir(d)] + \
             [t for j in jsons or [] for t in ci_shard.read_tests_full(j)]:
        if t["name"] not in seen:
            seen.add(t["name"])
            out.append(t)
    if not out:
        sys.exit("ci_costs_update: no test in " + " ".join((build_dirs or []) + (jsons or [])))
    return out


# ---- times ----

def local_times(paths):
    """{test: (seconds, log)}: each test's newest passing time (the files by
    modification time, oldest first, so a later log wins)."""
    times = {}
    for path in sorted(paths, key=lambda p: (os.path.getmtime(p), p)):
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if " sec" not in line or "Test " not in line:
                    continue
                m = _RESULT.search(line)
                if m and m.group(2) == "Passed":
                    times[m.group(1)] = (float(m.group(3)), os.path.basename(path))
    return times


def ratios(table, local, tests):
    """{'render': r, 'other': r} and their evidence, CI seconds over local seconds."""
    labels = {t["name"]: t["labels"] for t in tests}
    pairs = {"render": [], "other": []}
    for name, ci in table.items():
        if name in local and ci >= MIN_OVERLAP and local[name][0] >= MIN_OVERLAP:
            cls = "render" if "render" in labels.get(name, []) or name.startswith("rd_") else "other"
            pairs[cls].append((ci, local[name][0]))
    out, info = {}, {}
    for cls, ps in pairs.items():
        if ps:
            r = [c / l for c, l in ps]
            q = statistics.quantiles(r, n=4) if len(r) >= 2 else [r[0], r[0], r[0]]
            out[cls] = sum(c for c, _ in ps) / sum(l for _, l in ps)
            info[cls] = (len(ps), out[cls], statistics.median(r), q[0], q[2])
    if "render" not in out:
        sys.exit("ci_costs_update: no test is in both the table and the local logs: no ratio")
    if len(pairs["other"]) < 5:
        out["other"] = out["render"]
    return out, info


def read_table_text(path):
    """The table's (comment header lines, {name: seconds})."""
    header = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                header.append(line.rstrip("\n"))
            else:
                break
    return header, ci_shard.read_costs(path)


def estimate_suite(suite, base, tests, local, assume, floor):
    """The suite's new table {name: (seconds, comment)} and a report."""
    r, info = ratios(base, local, tests)
    table = {n: (s, "") for n, s in base.items()}
    want, measured, unmeasured = [], [], []
    for t in tests:
        n = t["name"]
        fx = ci_shard.reads_fixtures(t["command"])
        if n not in base or (fx and base[n] < floor):
            want.append((n, fx, t))
    for n, fx, t in want:
        cls = "render" if "render" in t["labels"] or n.startswith("rd_") else "other"
        if n in assume:
            sec, src = assume[n], "assumed"
        elif n in local:
            sec, src = local[n]
        else:
            unmeasured.append((n, fx))
            continue
        est = sec * r[cls]
        if fx:
            est = max(est, floor)  # the guard's floor: a fixture test always runs
        table[n] = (round(est, 2), f"estimate: {sec:.1f} s local ({src}) x {r[cls]:.2f}")
        measured.append(n)
    fx_est = [table[n][0] for n, fx, _ in want if fx and n in measured]
    fx_default = statistics.median(fx_est) if fx_est else statistics.median(base.values())
    for n, fx in unmeasured:
        if fx:
            table[n] = (round(fx_default, 2), "estimate: unmeasured, the median of the fixture estimates")
        else:
            table[n] = (round(statistics.median(base.values()), 2), "estimate: unmeasured, the table's median")
    return table, {"ratio": r, "info": info, "measured": measured, "unmeasured": unmeasured}


def write_table(path, suite, table, source):
    lines = [
        f"# Seconds per test for tools/ci_shard.py, suite {suite} (CI's sharding of the ctest suites, docs/port/TESTING.md \"CI\").",
        f"# {source}; only this suite's own times balance this suite's shards.",
        "# Lines with \"# estimate\" are a local time times the host-to-CI ratio (tools/ci_costs_update.py); a CI refresh replaces them.",
        f"# Refresh: tools/ci_costs_update.py update --build-dir <build> [--ci-logs <dir of a run's job logs>] --write",
    ]
    for n in sorted(table):
        sec, why = table[n]
        lines.append(f"{n} {sec:.2f}" + (f"  # {why}" if why else ""))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def ci_log_paths(ci_dir, suite):
    if suite == "linux-arm64":
        return sorted(glob.glob(os.path.join(ci_dir, "linux-arm64*.log")))
    return sorted(p for p in glob.glob(os.path.join(ci_dir, suite + "_*.log"))
                  if re.fullmatch(re.escape(suite) + r"_\d+_\d+\.log", os.path.basename(p)))


def cmd_update(args):
    tests = read_all_tests(args.build_dir, args.tests)
    logs = args.local_log or [p for k in ("first", "rest", "lvp")
                              for p in glob.glob(os.path.join(REPO, DEFAULT_LOCAL % k))]
    local = local_times(logs)
    assume = {}
    for a in args.assume or []:
        name, _, sec = a.partition("=")
        assume[name] = float(sec)
    nfx = sum(1 for t in tests if ci_shard.reads_fixtures(t["command"]))
    print(f"ci_costs_update: {len(tests)} tests ({nfx} replay the committed fixtures), "
          f"{len(local)} with a local time from {len(logs)} logs")
    for suite in args.suite or SUITES:
        path = os.path.join(HERE, "ci_costs", suite + ".txt")
        if args.ci_logs:
            paths = ci_log_paths(args.ci_logs, suite)
            if not paths:
                sys.exit(f"ci_costs_update: no {suite} job log in {args.ci_logs}")
            base = ci_shard.costs_from_logs(paths)
            run = os.path.join(args.ci_logs, "run.txt")
            what = open(run, encoding="utf-8").read().strip() if os.path.exists(run) else args.ci_logs
            source = f"From the {suite} job logs of {what}, ctest --parallel 4 on the hosted runner"
        else:
            header, base = read_table_text(path)
            source = next((h[2:] for h in header if h.startswith("# From")), "From the committed table")
            source = source.split(";")[0]
        table, rep = estimate_suite(suite, base, tests, local, assume, args.floor)
        info = rep["info"]["render"]
        print(f"{suite}: ratio CI/local {rep['ratio']['render']:.2f} for render tests "
              f"({info[0]} tests; median {info[2]:.2f}, quartiles {info[3]:.2f} to {info[4]:.2f}), "
              f"{rep['ratio']['other']:.2f} for the rest; {len(rep['measured'])} lines estimated, "
              f"{len(rep['unmeasured'])} unmeasured")
        for n, fx in rep["unmeasured"]:
            print(f"  unmeasured{' fixture test' if fx else ''}: {n} = {table[n][0]:.2f}")
        add = sum(table[n][0] for n in rep["measured"]) + sum(table[n][0] for n, _ in rep["unmeasured"])
        print(f"  the estimated lines: {add:.0f} s ({add / 240:.1f} min at --parallel 4)")
        if args.write:
            write_table(path, suite, table, source)
            print(f"  wrote {os.path.relpath(path, REPO)}")
        bad = ci_shard.check_table(tests, {n: s for n, (s, _) in table.items()}, args.floor)
        if bad:
            print(f"  still failing the guard: {', '.join(n for n, _ in bad)}")
    return 0


def workflow_shards():
    """{suite: shards} from the `tests` matrix lines of ci.yml."""
    out = {}
    with open(os.path.join(REPO, ".github", "workflows", "ci.yml"), encoding="utf-8") as f:
        for line in f:
            m = re.search(r"\{suite: ([\w-]+),.*shards: (\d+), shard: \d+", line)
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


def plan_suite(tests, costs, n, parallel):
    pairs = [(t["name"], t["fixtures"]) for t in tests]
    out, load, _, default = ci_shard.partition(pairs, costs, n, parallel)
    times = [ci_shard.shard_time(load[k], max((costs.get(x, default) for x in out[k]), default=0), parallel)
             for k in range(n)]
    return times


def cmd_plan(args):
    tests = read_all_tests(args.build_dir, args.tests)
    shards = workflow_shards()
    for suite in args.suite or SUITES:
        costs = ci_shard.read_costs(os.path.join(HERE, "ci_costs", suite + ".txt"))
        mine = [t for t in tests if not (args.fixture_tests == "skip" and ci_shard.reads_fixtures(t["command"]))]
        total = sum(costs.get(t["name"], 0) for t in mine)
        top = max((costs.get(t["name"], 0), t["name"]) for t in mine)
        n = shards.get(suite)
        line = f"{suite}: {total:.0f} s ({total / 60 / args.parallel:.1f} min at --parallel {args.parallel}), longest {top[1]} {top[0] / 60:.1f} min"
        if n:
            times = plan_suite(mine, costs, n, args.parallel)
            line += f"; {n} shards: " + " ".join(f"{t / 60:.1f}" for t in times) + " min"
        print(line)
        target = args.target_min * 60
        best = None
        for k in range(1, 41):
            m = max(plan_suite(mine, costs, k, args.parallel))
            if best and m >= best[1] - 1:
                break  # the longest test's floor: more shards no longer help
            best = (k, m)
            if m <= target:
                break
        print(f"  fewest shards under {args.target_min:g} min (or at the longest test's floor): "
              f"{best[0]} ({best[1] / 60:.1f} min)")
    return 0


def cmd_fetch(args):
    os.makedirs(args.dir, exist_ok=True)
    repo = args.repo
    run = json.loads(subprocess.check_output(["gh", "api", f"repos/{repo}/actions/runs/{args.run}"]))
    with open(os.path.join(args.dir, "run.txt"), "w", encoding="utf-8") as f:
        f.write(f"CI run {args.run} ({run['created_at'][:10]}, {run['head_sha'][:9]}, {run['conclusion']})\n")
    jobs = json.loads(subprocess.check_output(
        ["gh", "api", f"repos/{repo}/actions/runs/{args.run}/jobs?per_page=100"]))["jobs"]
    for j in jobs:
        suite = next((s for p, s in JOB_SUITE if j["name"].startswith(p)), None)
        if not suite:
            continue
        m = re.search(r"(\d+)/(\d+)$", j["name"])
        k, n = (m.group(1), m.group(2)) if m else ("1", "1")
        tag = j["name"].replace(" ", "_").replace("/", "_") if suite == "linux-arm64" else f"{suite}_{k}_{n}"
        out = os.path.join(args.dir, tag + ".log")
        with open(out, "wb") as f:
            subprocess.check_call(["gh", "api", f"repos/{repo}/actions/jobs/{j['id']}/logs"], stdout=f)
        print(f"ci_costs_update: {j['name']} ({j['conclusion']}) -> {out}")
    return 0


def selftest():
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            print("ci_costs_update selftest: FAIL " + what)
            fails += 1

    text = ('add_test("rd_a_ab" "/x/cmake" "-DFIXTURES=/r/fixtures" "-DLIST=a b.txt" "-P" "s.cmake")\n'
            'set_tests_properties("rd_a_ab" PROPERTIES  LABELS "render;tools" TIMEOUT "7200" _BACKTRACE_TRIPLES "x;1;y")\n'
            'add_test([=[unit]=] "/x/unit" "--q=\\"a\\"")\n'
            'set_tests_properties([=[unit]=] PROPERTIES  FIXTURES_REQUIRED "f1" LABELS "core")\n'
            'subdirs("sub")\n')
    calls = list(_calls(text, "add_test"))
    check(calls[0][:3] == ["rd_a_ab", "/x/cmake", "-DFIXTURES=/r/fixtures"], f"add_test parse {calls[0]}")
    check(calls[0][3] == "-DLIST=a b.txt" and calls[1] == ["unit", "/x/unit", '--q="a"'],
          f"quoted arguments {calls}")
    props = list(_calls(text, "set_tests_properties"))
    check(props[1][:2] == ["unit", "PROPERTIES"] and "f1" in props[1], f"properties {props[1]}")
    tests = [{"name": "rd_a_ab", "fixtures": [], "labels": ["render"], "command": ["-DFIXTURES=/f"]},
             {"name": "rd_old", "fixtures": [], "labels": ["render"], "command": ["t"]},
             {"name": "rd_old2", "fixtures": [], "labels": ["render"], "command": ["t"]},
             {"name": "rd_b_ab", "fixtures": [], "labels": ["render"], "command": ["-DFIXTURES=/f"]},
             {"name": "unit", "fixtures": [], "labels": ["core"], "command": ["u"]},
             {"name": "rd_new", "fixtures": [], "labels": ["render"], "command": ["t"]}]
    base = {"rd_a_ab": 0.0, "rd_old": 20.0, "rd_old2": 40.0, "rd_b_ab": 0.01, "unit": 0.5}
    local = {"rd_a_ab": (100.0, "l"), "rd_old": (10.0, "l"), "rd_old2": (20.0, "l"), "rd_new": (8.0, "l")}
    table, rep = estimate_suite("s", base, tests, local, {}, 1.0)
    check(abs(rep["ratio"]["render"] - 2.0) < 1e-9, f"ratio {rep['ratio']}")
    check(table["rd_a_ab"][0] == 200.0 and table["rd_new"][0] == 16.0, f"estimates {table}")
    check(table["rd_old"][0] == 20.0 and table["unit"][0] == 0.5, "measured lines kept")
    check(rep["unmeasured"] == [("rd_b_ab", True)] and table["rd_b_ab"][0] == 200.0,
          f"an unmeasured fixture test at the fixture median {rep['unmeasured']}")
    table2, _ = estimate_suite("s", base, tests, local, {"rd_b_ab": 50.0}, 1.0)
    check(table2["rd_b_ab"][0] == 100.0, "--assume")
    check(not ci_shard.check_table(tests, {n: s for n, (s, _) in table.items()}, 1.0),
          "the updated table passes the guard")
    print("ci_costs_update selftest: " + ("ok" if not fails else f"{fails} failed"))
    return 1 if fails else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, func in (("update", cmd_update), ("plan", cmd_plan)):
        p = sub.add_parser(name)
        p.add_argument("--build-dir", action="append", help="a configured build tree (read, not run)")
        p.add_argument("--tests", action="append", help="ctest --show-only=json-v1 output")
        p.add_argument("--suite", action="append", choices=SUITES, help="only this suite (repeatable)")
        p.set_defaults(func=func)
        if name == "update":
            p.add_argument("--ci-logs", help="a directory of CI job logs (fetch-ci) to measure the tables from")
            p.add_argument("--local-log", action="append", help="a local ctest log (default: the merge checks')")
            p.add_argument("--assume", action="append", help="NAME=SECONDS: a local time no log has")
            p.add_argument("--floor", type=float, default=1.0, help="the guard's floor for fixture tests")
            p.add_argument("--write", action="store_true", help="rewrite tools/ci_costs/<suite>.txt")
        else:
            p.add_argument("--parallel", type=int, default=4)
            p.add_argument("--target-min", type=float, default=25.0)
            p.add_argument("--fixture-tests", choices=("run", "skip"), default="run")
    f = sub.add_parser("fetch-ci")
    f.add_argument("run", help="the CI run's id (gh -R nathanialf/ico-pc run list --workflow ci.yml)")
    f.add_argument("dir")
    f.add_argument("--repo", default="nathanialf/ico-pc")
    f.set_defaults(func=cmd_fetch)
    args = ap.parse_args()
    if args.cmd in ("update", "plan") and not (args.build_dir or args.tests):
        ap.error("--build-dir or --tests is required")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
