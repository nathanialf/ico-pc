#!/usr/bin/env python3
"""Two phone logs side by side: the culler's piece, list 0's GPU time and the walk.

For the baseline log and the new one (a log of the game's own lines, as the
window host writes them to its log file), the 10 s lines give, per run, the
medians over the lines of

  - "cull: N of M scene draws skipped a replay (F frustum, O occluded), T ms"
    (the backend line, rd_perf.c; a baseline from before the culler has none);
  - "window: GPU ms per replay: lists 0-12 a b ..." (a is list 0's ms);
  - "walk W ms" of the render thread's line (the CPU walk a picture);
  - "render thread: CPU C ms a picture" (the whole CPU time a picture).

A table of the medians and the change, one row a metric.  A room is a pair
of logs: --pair NAME=BASELINE:NEW, repeatable (the three rooms of the phone
recipe), or two positional paths for one pair.

    python3 -I tools/cull_log_compare.py BASELINE.log NEW.log
    python3 -I tools/cull_log_compare.py --pair forest=b1.log:n1.log --pair ...

--selftest parses built-in sample lines and exits.
"""

import argparse
import re
import statistics
import sys
from pathlib import Path

CULL_RE = re.compile(
    r"cull: ([0-9.]+) of ([0-9.]+) scene draws skipped a replay \(([0-9.]+) frustum, "
    r"([0-9.]+) occluded\), ([0-9.]+) ms"
)
LIST0_RE = re.compile(r"window: GPU ms per replay: lists 0-12 ([0-9.]+)")
WALK_RE = re.compile(r"render thread: CPU ([0-9.]+) ms a picture.*?walk ([0-9.]+) ms")

METRICS = [
    ("scene draws a replay", "draws", ""),
    ("skipped a replay", "skipped", ""),
    ("culler ms", "cullms", ""),
    ("list 0 GPU ms", "list0", ""),
    ("walk ms", "walk", ""),
    ("render thread CPU ms", "cpu", ""),
]


def read_log(text):
    """{metric: [values per 10 s line]} from a log's text."""
    out = {k: [] for _, k, _ in METRICS}
    for line in text.splitlines():
        m = CULL_RE.search(line)
        if m:
            out["skipped"].append(float(m.group(1)))
            out["draws"].append(float(m.group(2)))
            out["cullms"].append(float(m.group(5)))
        m = LIST0_RE.search(line)
        if m:
            out["list0"].append(float(m.group(1)))
        m = WALK_RE.search(line)
        if m:
            out["cpu"].append(float(m.group(1)))
            out["walk"].append(float(m.group(2)))
    return out


def med(v):
    return statistics.median(v) if v else None


def table(name, base, new):
    rows = [f"### {name}", "", "| metric | baseline | new | change | lines (base / new) |",
            "|---|---|---|---|---|"]
    for label, key, _ in METRICS:
        b, n = med(base[key]), med(new[key])
        if b is None and n is None:
            continue
        ch = ""
        if b is not None and n is not None:
            ch = f"{n - b:+.2f}" + (f" ({100.0 * (n - b) / b:+.1f} %)" if b else "")
        rows.append(f"| {label} | {'' if b is None else f'{b:.2f}'} | "
                    f"{'' if n is None else f'{n:.2f}'} | {ch} | "
                    f"{len(base[key])} / {len(new[key])} |")
    return "\n".join(rows)


SAMPLE = """\
rd: backend: x; cull: 310 of 980 scene draws skipped a replay (310 frustum, 0 occluded), 0.21 ms ([dev] cull); y
window: GPU ms per replay: lists 0-12 6.25 1.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.00 0.10 0.20; effects: a 1
window: replays: render thread: CPU 9.3 ms a picture, idle 21 %, waited for a frame 14.2 ms, heartbeat max 13.9 ms (590 pictures); interp 3.8 ms, walk 3.9 ms, submit 2.6 ms
"""


def selftest():
    r = read_log(SAMPLE)
    ok = (r["skipped"] == [310.0] and r["draws"] == [980.0] and r["cullms"] == [0.21]
          and r["list0"] == [6.25] and r["walk"] == [3.9] and r["cpu"] == [9.3])
    print("selftest:", "ok" if ok else "FAILED", r)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="*", help="BASELINE NEW")
    ap.add_argument("--pair", action="append", default=[], help="NAME=BASELINE:NEW")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    pairs = []
    for p in args.pair:
        name, _, rest = p.partition("=")
        b, _, n = rest.partition(":")
        if not name or not b or not n:
            sys.exit(f"bad --pair {p} (NAME=BASELINE:NEW)")
        pairs.append((name, b, n))
    if len(args.logs) == 2:
        pairs.append(("run", args.logs[0], args.logs[1]))
    elif args.logs:
        sys.exit("give BASELINE and NEW, or --pair")
    if not pairs:
        ap.error("no logs")
    for name, b, n in pairs:
        print(table(name, read_log(Path(b).read_text(errors="replace")),
                    read_log(Path(n).read_text(errors="replace"))))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
