#!/usr/bin/env python3
"""Split a build's ctest suite into N shards of about equal measured cost.

    tools/ci_shard.py split --tests <ctest --show-only=json-v1 output> \
        --costs tools/ci_costs/<suite>.txt --shards N --index K [--include RE] --out <file>
    tools/ci_shard.py costs <ctest output log>... > tools/ci_costs/<suite>.txt
    tools/ci_shard.py --selftest

`split` writes the names of shard K (1..N), one a line, for
`ctest --tests-from-file <file>`. Every shard of the same build computes the
same partition from the same two inputs, so the N shards together run every
test exactly once:

- tests that share a fixture (FIXTURES_SETUP / FIXTURES_REQUIRED /
  FIXTURES_CLEANUP: `rd_pixel` and the `rd_replay_tool*` tests that read its
  dump, `state_recover_files` and its readers, `texpack_disc` and its readers)
  form one group and go to one shard, so ctest never runs a fixture's setup
  test twice;
- groups go heaviest first to the shard with the least cost so far (ties to
  the lower shard, then by the group's first test name), so the result is a
  pure function of the test list, the cost table and N;
- with --include RE only the tests whose names match the regular expression
  (re.search) are split (the linux-arm64 job's filtered tests); the other
  tests are not in any shard;
- a test the cost table does not name (a new test) costs the table's median.

The cost table is "<name> <seconds>" lines ('#' starts a comment); a
CTestCostData.txt ("<name> <runs> <average>" up to a '---' line) is read too.
`costs` makes a table from ctest's own output ("Test #12: name ...   Passed
1.23 sec", with or without GitHub's timestamp prefix), the largest time when a
test appears twice. docs/port/TESTING.md ("CI") says where the committed table
comes from.
"""

import argparse
import glob
import json
import os
import re
import statistics
import sys

_TEST_LINE = re.compile(r"Test +#\d+: (\S+) \.+[^0-9\n]*?([0-9]+(?:\.[0-9]+)?) sec")


def read_costs(path):
    costs = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line == "---":
                break  # CTestCostData.txt: the failed tests follow
            parts = line.split()
            try:
                if len(parts) == 2:
                    costs[parts[0]] = float(parts[1])
                elif len(parts) == 3:
                    costs[parts[0]] = float(parts[2])
                else:
                    raise ValueError
            except ValueError:
                sys.exit(f"ci_shard: {path}: not a cost line: {line!r}")
    return costs


def costs_from_logs(paths):
    costs = {}
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if "Test " not in line or " sec" not in line:
                    continue
                m = _TEST_LINE.search(line)
                if m:
                    name, sec = m.group(1), float(m.group(2))
                    costs[name] = max(sec, costs.get(name, 0.0))
    return costs


def _prop_list(value):
    if isinstance(value, list):
        out = []
        for v in value:
            out.extend(str(v).split(";"))
        return [v for v in out if v]
    return [v for v in str(value).split(";") if v]


def read_tests(path):
    """[(name, [fixture names])] in ctest's order, from --show-only=json-v1."""
    with open(path, encoding="utf-8") as f:
        info = json.load(f)
    tests = []
    for t in info.get("tests", []):
        fixtures = []
        for p in t.get("properties", []):
            if p.get("name") in ("FIXTURES_SETUP", "FIXTURES_REQUIRED", "FIXTURES_CLEANUP"):
                fixtures.extend(_prop_list(p.get("value", [])))
        tests.append((t["name"], fixtures))
    return tests


def partition(tests, costs, shards):
    """The N shards: a list of N lists of test names, and each shard's cost."""
    if shards < 1:
        raise ValueError("shards must be at least 1")
    known = [costs[n] for n, _ in tests if n in costs]
    default = statistics.median(known) if known else 1.0
    # union-find over test names and fixture keys
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb)] = min(ra, rb)

    for name, fixtures in tests:
        find("t:" + name)
        for fx in fixtures:
            union("t:" + name, "f:" + fx)
    order = {name: i for i, (name, _) in enumerate(tests)}
    groups = {}
    for name, _ in tests:
        groups.setdefault(find("t:" + name), []).append(name)
    weighted = []
    for members in groups.values():
        members.sort(key=order.get)
        cost = sum(costs.get(n, default) for n in members)
        weighted.append((cost, members))
    weighted.sort(key=lambda g: (-g[0], g[1][0]))
    load = [0.0] * shards
    out = [[] for _ in range(shards)]
    for cost, members in weighted:
        k = min(range(shards), key=lambda i: (load[i], i))
        load[k] += cost
        out[k].extend(members)
    for names in out:
        names.sort(key=order.get)
    unknown = sorted(n for n, _ in tests if n not in costs)
    return out, load, unknown, default


def cmd_split(args):
    tests = read_tests(args.tests)
    if args.include:
        keep = re.compile(args.include)
        tests = [t for t in tests if keep.search(t[0])]
    if not tests:
        sys.exit(f"ci_shard: {args.tests} lists no test")
    costs = read_costs(args.costs)
    if not 1 <= args.index <= args.shards:
        sys.exit(f"ci_shard: --index {args.index} is outside 1..{args.shards}")
    out, load, unknown, default = partition(tests, costs, args.shards)
    for k, names in enumerate(out, 1):
        if not names:
            sys.exit(f"ci_shard: shard {k} of {args.shards} would be empty ({len(tests)} tests)")
    mine = out[args.index - 1]
    with open(args.out, "w", encoding="utf-8") as f:
        for name in mine:
            f.write(name + "\n")
    print(f"ci_shard: {len(tests)} tests in {args.shards} shards by the cost table {args.costs}")
    for k, names in enumerate(out, 1):
        mark = "  <- this shard" if k == args.index else ""
        print(f"ci_shard: shard {k}: {len(names)} tests, {load[k - 1]:.0f} s of test time{mark}")
    if unknown:
        shown = ", ".join(unknown[:12]) + (", ..." if len(unknown) > 12 else "")
        print(f"ci_shard: {len(unknown)} tests not in the cost table, counted at the median "
              f"{default:.2f} s: {shown}")
    return 0


def cmd_costs(args):
    costs = costs_from_logs(args.logs)
    if not costs:
        sys.exit("ci_shard: no 'Test #N: name ... sec' line in " + " ".join(args.logs))
    print("# ctest seconds per test (tools/ci_shard.py costs " + " ".join(args.logs) + ")")
    for name in sorted(costs):
        print(f"{name} {costs[name]:.2f}")
    return 0


def selftest():
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            print("ci_shard selftest: FAIL " + what)
            fails += 1

    tests = [("a", []), ("b", []), ("pix", ["dump"]), ("rep1", ["dump"]), ("rep2", ["dump"]),
             ("c", []), ("new", []), ("d", [])]
    costs = {"a": 100, "b": 90, "pix": 50, "rep1": 10, "rep2": 10, "c": 40, "d": 5}
    for n in (1, 2, 3, 5):
        out, load, unknown, default = partition(tests, costs, n)
        names = [x for shard in out for x in shard]
        check(sorted(names) == sorted(t for t, _ in tests), f"N={n}: every test exactly once")
        check(len(names) == len(set(names)), f"N={n}: no test twice")
        owner = {x: k for k, shard in enumerate(out) for x in shard}
        check(owner["pix"] == owner["rep1"] == owner["rep2"], f"N={n}: a fixture's tests together")
        check(unknown == ["new"] and default == 40, f"N={n}: the unknown test at the median")
        again = partition(list(tests), dict(costs), n)[0]
        check(again == out, f"N={n}: the same partition twice")
    out, load, _, _ = partition(tests, costs, 2)
    check(abs(load[0] - load[1]) <= 40, f"N=2: loads {load} within the largest small group")
    # the order inside a shard is ctest's
    check(out[0] == sorted(out[0], key=[t for t, _ in tests].index), "shard order is ctest's")
    m = _TEST_LINE.search("2026-11-03T11:57:39.1Z   2/748 Test #158: texpack_disc ....***Skipped   0.07 sec")
    check(m is not None and m.group(1) == "texpack_disc" and m.group(2) == "0.07", "log line parse")
    m = _TEST_LINE.search("  1/9 Test   #1: fpenv ......   Passed    12.50 sec")
    check(m is not None and m.group(1) == "fpenv" and m.group(2) == "12.50", "log line parse, padded")
    # the committed tables (one a suite) parse
    here = os.path.dirname(os.path.abspath(__file__))
    tables = sorted(glob.glob(os.path.join(here, "ci_costs", "*.txt")))
    check(len(tables) >= 6, f"{len(tables)} cost tables in tools/ci_costs")
    for t in tables:
        check(len(read_costs(t)) > 100, f"{os.path.basename(t)} names more than 100 tests")
    print("ci_shard selftest: " + ("ok" if not fails else f"{fails} failed"))
    return 1 if fails else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("split", help="write one shard's test names")
    sp.add_argument("--tests", required=True, help="ctest --show-only=json-v1 output")
    sp.add_argument("--costs", required=True, help="cost table (or CTestCostData.txt)")
    sp.add_argument("--shards", type=int, required=True)
    sp.add_argument("--index", type=int, required=True, help="1-based shard number")
    sp.add_argument("--include", help="split only the tests whose name matches this regex")
    sp.add_argument("--out", required=True, help="file for ctest --tests-from-file")
    sp.set_defaults(func=cmd_split)
    cp = sub.add_parser("costs", help="a cost table from ctest output logs")
    cp.add_argument("logs", nargs="+")
    cp.set_defaults(func=cmd_costs)
    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
