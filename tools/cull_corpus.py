#!/usr/bin/env python3
"""The scene draw culler over the corpus: what it takes, what it leaves, and why.

For every dump of the lists (tools/occlusion_corpus.txt and
tools/native_frame_dumps.txt by default: one path or glob a line, '#'
comments, relative to the list's folder; the dumps that are not here are
named and skipped), at Original 4:3 1x, Enhanced 4:3 2x and, for a dump drawn
wider than 4:3, Enhanced 2x at its own aspect, rd_replay_tool
(docs/port/RENDER.md, "Culling") is run

  - once under each of --cull off, frustum and full (ICO_RD_BLEND=fixed, the
    renderer's draw merges on, as it plays): --stats --gpu-repeat K --time K,
    giving list 0's GPU median of K warm replays, the CPU walk's mean of K
    replays, the culler's counts (scene draws walked, skipped by the frustum
    test, by the occlusion test, its own ms) and the "cull why" line (one
    reason for each scene draw command: skipped by the stream's box or by
    the batches' box, exempt, unbounded, or replayed because its bound
    reaches the target, rd_vis.h RD_VIS_WHY_*);
  - once under --occlusion: the list-0 draws that put no sample on the screen
    (hidden for any reason), the ceiling the culler is measured against.

The report (default build-host/tmp/v0101/evidence/perf/cull.md) has a row a
dump and setting (draws walked, skipped and its share, list 0 GPU ms and
walk ms by mode, the culler's ms, the share hidden for any reason and what
the frustum skip leaves of it, the largest reasons a command was replayed), a
table of the reasons summed over the rows, the medians, and the verdict line.
Units differ: the culler counts the VU draw commands of lists 0 to 10, the
occlusion tool the GPU draws of list 0, so the share left is a difference of
two shares, not a count.

Each replay runs under the replay lock (flock build-host/tmp/replay.lock,
taken here for each run, never around ctest) on lavapipe
(VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json unless the
environment names a driver).  The per-run logs stay beside the report
(cull/<n>-<setting>/).  --selftest parses built-in sample lines and exits.

    python3 -I tools/cull_corpus.py [--tool PATH] [--list FILE ...]
        [--out FILE] [--repeat K] [--settings orig,enh2x43,own] [--limit N]
        [--modes off,frustum,full]
"""

import argparse
import glob
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SETTINGS = {
    "orig": ["--aspect", "4:3", "--present", "640x480"],
    "enh2x43": ["--enhanced", "--aspect", "4:3", "--resolution", "2x", "--present", "1280x960"],
    # the dump's own aspect: only for a dump drawn wider than 4:3
    "own": ["--enhanced", "--resolution", "2x"],
}
WIDE_ABOVE = 1.34  # rd_cull_ab.cmake's threshold

ASPECT_RE = re.compile(r"dump: version \d+, GS \d+x\d+, aspect ([0-9.]+)")
GPU_RE = re.compile(
    r"gpu median of (\d+) replays: total ([0-9.]+) ms, list 0 ([0-9.]+) ms, "
    r"VU pre-pass ([0-9.]+) ms"
)
WALK_RE = re.compile(r"time: \d+ replays, CPU mean \(min\) ms: .*?walk ([0-9.]+) \(([0-9.]+)\)")
CULL_RE = re.compile(
    r"stats: cull (\w+): (\d+) of (\d+) scene draws skipped \((\d+) frustum, (\d+) occluded\), "
    r"([0-9.]+) ms; not tested: (\d+) exempt, (\d+) unbounded"
)
WHY_RE = re.compile(r"stats: cull why:(.*)$", re.M)
OCCW_RE = re.compile(r"stats: cull occlusion:(.*)$", re.M)
OCC_RE = re.compile(
    r"occlusion: hidden (\d+) of (\d+) list-0 draws, (\d+) of (\d+) triangles "
    r"\(index count / 3\); hidden commands: (\S+)"
)


def read_list(path):
    """The dumps the list names that are here, and the entries that are not."""
    base = path.parent
    dumps, missing = [], []
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        p = line if os.path.isabs(line) else str(base / line)
        if "*" in p:
            got = sorted(glob.glob(p))
            if got:
                dumps.extend(got)
            else:
                missing.append(p)
        elif os.path.exists(p):
            dumps.append(p)
        else:
            missing.append(p)
    return dumps, missing


def run(cmd, env, lock, log, timeout=3600):
    """One replay under the lock; (exit code, stdout + stderr), the log written."""
    full = ["flock", str(lock)] + cmd
    try:
        p = subprocess.run(full, env=env, capture_output=True, text=True, timeout=timeout)
        out, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or "") if isinstance(e.stdout, str) else ""
        out, rc = out + f"\n(timed out after {timeout} s)\n", -1
    log.write_text(" ".join(full) + "\n\n" + out)
    return rc, out


def parse_run(text):
    """The numbers of one --stats --gpu-repeat --time run (missing ones absent)."""
    r = {}
    m = GPU_RE.search(text)
    if m:
        r["l0"] = float(m.group(3))
    m = WALK_RE.search(text)
    if m:
        r["walk"], r["walkmin"] = float(m.group(1)), float(m.group(2))
    m = CULL_RE.search(text)
    if m:
        r.update(mode=m.group(1), skipped=int(m.group(2)), draws=int(m.group(3)),
                 frustum=int(m.group(4)), occluded=int(m.group(5)), cullms=float(m.group(6)),
                 exempt=int(m.group(7)), unbounded=int(m.group(8)))
    for key, rx in (("why", WHY_RE), ("occ", OCCW_RE)):
        m = rx.search(text)
        if m:
            d = {}
            for part in m.group(1).split(";"):
                k = part.strip().rsplit(" ", 1)
                if len(k) == 2 and k[1].isdigit():
                    d[k[0]] = int(k[1])
            r[key] = d
    return r


def parse_occlusion(text):
    """(list-0 draws hidden, list-0 draws) of an --occlusion run, or None."""
    m = OCC_RE.search(text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def med(v):
    v = [x for x in v if x == x]
    return statistics.median(v) if v else float("nan")


def fmt(x, nd=3):
    return "" if x is None or x != x else f"{x:.{nd}f}"


SELFTEST_TEXT = """\
dump.rddump: stats: cull frustum: 312 of 980 scene draws skipped (312 frustum, 0 occluded), 0.210 ms; not tested: 40 exempt, 61 unbounded; skipped beyond the left 100, right 90, top 20, bottom 30, behind the eye 72
dump.rddump: stats: cull why: skipped by the stream's box 300; skipped by the batches' box 12; particles 30; 2D lists 10; no target 0; clip none 0; no payload 0; clip unknown 0; no mesh 5; no bound 6; skinned refused 50; grid refused 0; skinned reaches 80; grid reaches 3; scissor mesh reaches 120; mesh of batches reaches 200; one-batch mesh reaches 164
dump.rddump: stats: cull occlusion: occluder used 3; occluder refused: state 4; occluded 2; kept: no occluder yet 7
dump.rddump: time: 5 replays, CPU mean (min) ms: total 9.000 (8.000), interp 0.000 (0.000), wait 0.000 (0.000), acquire 0.000 (0.000), upload 1.000 (1.000), walk 3.250 (3.100), bind 0.1 (0.1), submit 1.0 (1.0), present 0.1 (0.1); per replay: ring 1.0 KB
dump.rddump: gpu median of 5 replays: total 12.500 ms, list 0 6.250 ms, VU pre-pass 0.500 ms
dump.rddump: occlusion: hidden 400 of 900 list-0 draws, 5000 of 90000 triangles (index count / 3); hidden commands: 0:1,0:2
"""


def selftest():
    r = parse_run(SELFTEST_TEXT)
    ok = (r.get("l0") == 6.25 and r.get("walk") == 3.25 and r.get("walkmin") == 3.1
          and r.get("draws") == 980 and r.get("skipped") == 312 and r.get("cullms") == 0.21
          and r.get("exempt") == 40 and r.get("unbounded") == 61
          and sum(r["why"].values()) == 980 and len(r["why"]) == 17 and r["occ"]["occluded"] == 2
          and r["why"]["skipped by the batches' box"] == 12
          and parse_occlusion(SELFTEST_TEXT) == (400, 900))
    print("selftest:", "ok" if ok else "FAILED", r)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tool", default=str(ROOT / "build-host/linux-x64/port/render/rd_replay_tool"))
    ap.add_argument("--list", action="append",
                    help="a dump list (repeatable; default: occlusion_corpus.txt and "
                    "native_frame_dumps.txt)")
    ap.add_argument("--out", default=str(ROOT / "build-host/tmp/v0101/evidence/perf/cull.md"))
    ap.add_argument("--lock", default=str(ROOT / "build-host/tmp/replay.lock"))
    ap.add_argument("--repeat", type=int, default=5, help="warm GPU and CPU replays a timing")
    ap.add_argument("--settings", default="orig,enh2x43,own")
    ap.add_argument("--modes", default="off,frustum,full")
    ap.add_argument("--limit", type=int, default=0, help="the first N dumps only (0: all)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

    settings = [s for s in args.settings.split(",") if s]
    modes = [m for m in args.modes.split(",") if m]
    for s in settings:
        if s not in SETTINGS:
            sys.exit(f"unknown setting {s} (have {', '.join(SETTINGS)})")
    for m in modes:
        if m not in ("off", "frustum", "full"):
            sys.exit(f"unknown mode {m}")
    lists = args.list or [str(ROOT / "tools/occlusion_corpus.txt"),
                          str(ROOT / "tools/native_frame_dumps.txt")]
    dumps, missing, seen = [], [], set()
    for lp in lists:
        if not os.path.exists(lp):
            missing.append(lp)
            continue
        d, mi = read_list(Path(lp))
        missing += mi
        for x in d:
            if x not in seen:
                seen.add(x)
                dumps.append(x)
    if args.limit:
        dumps = dumps[: args.limit]
    if not dumps:
        sys.exit("no dump is here")
    out = Path(args.out)
    logs = out.parent / "cull"
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("VK_ICD_FILENAMES", "/usr/share/vulkan/icd.d/lvp_icd.json")
    fixed = dict(env, ICO_RD_BLEND="fixed")
    nan = float("nan")
    k = str(args.repeat)

    rows = []
    for i, dump in enumerate(dumps, 1):
        wide = False
        p = subprocess.run([args.tool, dump, "-", "--no-device", "--list"], capture_output=True,
                           text=True, env=env)
        m = ASPECT_RE.search(p.stdout + p.stderr)
        if m:
            wide = float(m.group(1)) > WIDE_ABOVE
        for s in settings:
            if s == "own" and not wide:
                continue
            d = logs / f"{i:02d}-{s}"
            d.mkdir(parents=True, exist_ok=True)
            row = {"dump": dump, "setting": s, "note": "", "runs": {}}
            rows.append(row)
            for mode in modes:
                rc, txt = run([args.tool, dump, "-", "--stats", "--gpu-repeat", k, "--time", k,
                               "--cull", mode] + SETTINGS[s], fixed, args.lock,
                              d / f"{mode}.log")
                if rc == 77:
                    sys.exit(f"{dump}: no device (exit 77)")
                r = parse_run(txt)
                if rc != 0 or "l0" not in r or "draws" not in r:
                    row["note"] += f" {mode}: exit {rc}" + ("" if "l0" in r else ", no GPU median")
                row["runs"][mode] = r
            rc, txt = run([args.tool, dump, str(d / "occlusion.png"), "--occlusion"]
                          + SETTINGS[s], env, args.lock, d / "occlusion.log")
            occ = parse_occlusion(txt) if rc == 0 else None
            if occ:
                row["hidden"], row["occdraws"] = occ
            else:
                row["note"] += f" occlusion: exit {rc}"
            base = row["runs"].get("frustum") or row["runs"].get("full") or {}
            print(f"{dump} {s}: skipped {base.get('skipped')} of {base.get('draws')} scene draws; "
                  f"hidden for any reason {row.get('hidden')} of {row.get('occdraws')} list-0 "
                  f"draws", flush=True)

    def run_of(r, mode):
        return r["runs"].get(mode) or {}

    cull_mode = "frustum" if "frustum" in modes else (modes[0] if modes else "off")
    measured = [r for r in rows if run_of(r, cull_mode).get("draws")]
    reasons = {}
    for r in measured:
        for name, n in run_of(r, cull_mode).get("why", {}).items():
            reasons[name] = reasons.get(name, 0) + n
    occ_reasons = {}
    for r in rows:
        for name, n in run_of(r, "full").get("occ", {}).items():
            occ_reasons[name] = occ_reasons.get(name, 0) + n
    tot_draws = sum(run_of(r, cull_mode)["draws"] for r in measured)
    tot_skip = sum(run_of(r, cull_mode)["skipped"] for r in measured)

    lines = [
        "# The scene draw culler over the corpus",
        "",
        f"Tool `{args.tool}`, lists {', '.join('`%s`' % x for x in lists)}, {len(dumps)} dumps "
        f"here ({len(missing)} entries not here), settings {', '.join(settings)} (`own`: Enhanced "
        f"2x at the dump's own aspect, dumps wider than {WIDE_ABOVE} only), modes "
        f"{', '.join(modes)}, GPU medians and CPU means of {args.repeat} warm replays on "
        f"`{env.get('VK_ICD_FILENAMES', '')}` under ICO_RD_BLEND=fixed. Scene draws: the VU draw "
        "commands of lists 0 to 10 the culler walks. Hidden for any reason: the share of list 0's "
        "GPU draws that put no sample on the screen (`--occlusion`, a different unit, so 'left' "
        "is a difference of shares).",
        "",
        "| # | dump | setting | scene draws | skipped | % | frustum | occluded | culler ms | "
        + " | ".join(f"list 0 ms {m}" for m in modes) + " | "
        + " | ".join(f"walk ms {m}" for m in modes)
        + " | hidden for any reason % | left (points) | largest reasons replayed | note |",
        "|" + "---|" * (9 + 2 * len(modes) + 4),
    ]
    for n, r in enumerate(rows, 1):
        name = os.path.relpath(r["dump"], ROOT) if r["dump"].startswith(str(ROOT)) else r["dump"]
        b = run_of(r, cull_mode)
        if not b.get("draws"):
            lines.append(f"| {n} | {name} | {r['setting']} |" + " |" * (6 + 2 * len(modes) + 4)
                         + f" {r['note'].strip() or 'no scene draw'} |")
            continue
        share = pct(b["skipped"], b["draws"])
        hid = pct(r["hidden"], r["occdraws"]) if "hidden" in r else nan
        why = sorted(((v, k) for k, v in b.get("why", {}).items()
                      if k.split(" ")[0] != "skipped" and v), reverse=True)[:3]
        lines.append(
            f"| {n} | {name} | {r['setting']} | {b['draws']} | {b['skipped']} | {share:.1f} | "
            f"{b['frustum']} | {b['occluded']} | {b['cullms']:.3f} | "
            + " | ".join(fmt(run_of(r, m).get("l0")) for m in modes) + " | "
            + " | ".join(fmt(run_of(r, m).get("walk")) for m in modes) + " | "
            + f"{fmt(hid, 1)} | {fmt(hid - share if hid == hid else nan, 1)} | "
            + ", ".join(f"{k} {v}" for v, k in why) + f" | {r['note'].strip()} |")

    lines += ["", "## Why a scene draw command was replayed (summed over the rows, "
              f"`--cull {cull_mode}`)", "", "| reason | commands | % of scene draws |", "|---|---|---|"]
    for name, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
        lines.append(f"| {name} | {v} | {pct(v, tot_draws):.1f} |")

    if occ_reasons:
        lines += ["", "## The occlusion test (`--cull full`, summed over the rows)", "",
                  "| outcome | commands |", "|---|---|"]
        for name, v in occ_reasons.items():
            lines.append(f"| {name} | {v} |")

    def drop(r, mode):
        a, b = run_of(r, "off").get("l0"), run_of(r, mode).get("l0")
        return pct(a - b, a) if a and b is not None else nan

    lines += ["", "## Medians", "",
              "| rows | skipped % | hidden for any reason % | list 0 GPU drop % (" + cull_mode
              + " vs off) | walk change ms | culler ms |", "|---|---|---|---|---|---|"]

    def med_row(label, rs):
        sk = med([pct(run_of(r, cull_mode)["skipped"], run_of(r, cull_mode)["draws"])
                  for r in rs])
        hd = med([pct(r["hidden"], r["occdraws"]) for r in rs if "hidden" in r])
        dr = med([drop(r, cull_mode) for r in rs])
        wk = med([run_of(r, cull_mode)["walk"] - run_of(r, "off")["walk"] for r in rs
                  if "walk" in run_of(r, cull_mode) and "walk" in run_of(r, "off")])
        cm = med([run_of(r, cull_mode)["cullms"] for r in rs])
        lines.append(f"| {label} ({len(rs)}) | {sk:.1f} | {hd:.1f} | {dr:.1f} | {fmt(wk)} | "
                     f"{cm:.3f} |")
        return sk, hd, dr, wk

    sk, hd, dr, wk = med_row("all", measured)
    for s in settings:
        rs = [r for r in measured if r["setting"] == s]
        if rs:
            med_row(s, rs)
    if "full" in modes and cull_mode != "full":
        lines += ["", f"`full` against `off`: median list 0 GPU drop "
                  f"{med([drop(r, 'full') for r in measured]):.1f} %, occluded "
                  f"{sum(run_of(r, 'full').get('occluded', 0) for r in measured)} of "
                  f"{sum(run_of(r, 'full').get('draws', 0) for r in measured)} scene draws."]
    if not measured:
        verdict = "NO DATA: no row measured a scene draw"
    else:
        take = sk / hd * 100.0 if hd == hd and hd > 0 else nan
        verdict = (f"{'IMPROVES' if dr == dr and dr > 0 and (wk != wk or wk <= 0.05) else 'NO GAIN'}"
                   f": the {cull_mode} skip takes {tot_skip} of {tot_draws} scene draws "
                   f"({pct(tot_skip, tot_draws):.1f} % summed, median {sk:.1f} %) against a median "
                   f"{hd:.1f} % hidden for any reason ({fmt(take, 0)} % of it, as shares); median "
                   f"list 0 GPU drop {dr:.1f} %, median walk change {fmt(wk)} ms")
    lines += ["", "## The verdict", "", f"**{verdict}**", ""]
    if missing:
        lines += ["Not here:", ""] + [f"- `{p}`" for p in missing] + [""]
    out.write_text("\n".join(lines))
    print(f"{out}: {verdict}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
