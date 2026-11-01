#!/usr/bin/env python3
"""The hidden share of list 0 over the corpus, and the occlusion culling gate.

For every dump of the list (tools/occlusion_corpus.txt by default: one
path or glob a line, '#' comments, relative to the list's folder), at each
setting (Original 4:3 1x and Enhanced 4:3 2x, the sizes gate-wave.sh
takes, each with a present so PRESENT.png is written), five replays of
rd_replay_tool (docs/port/RENDER.md, "Culling"):

  1. --occlusion --occlusion-out HIDDEN --occlusion-csv OCC: the list-0
     draw commands whose draws all passed 0 samples (hidden for any
     reason) and their --nop spec;
  2. the same under ICO_VK_DEPTH_ALWAYS=1 (every depth test passes): the
     commands that draw nothing whatever the depth says (outside the view
     or the scissor, every fragment discarded). Hidden in 1 and not here =
     hidden by the depth, what an occlusion culler can save; DEPTH, their
     --nop spec, one command an item;
  3. the plain replay: --pass-dump PLAIN --stats --gpu-repeat K under
     ICO_RD_BLEND=fixed (every target; list 0's GPU median of K warm
     replays);
  4. --pass-dump with --nop @HIDDEN (every hidden command left out);
  5. --pass-dump --stats --gpu-repeat K with --nop @DEPTH (what the culler
     would leave out): its list-0 GPU time against 3's.

Each replay runs under the replay lock (flock build-host/tmp/replay.lock,
taken here for each run, never around ctest) on lavapipe
(VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json unless the
environment names a driver). The report (default
build-host/tmp/v098/evidence/perf/occlusion.md) has a row per dump and
setting, then the medians over the rows that measured a list-0 draw, and
the gate (the plan's rule, judged on what the depth hid):

  PASS when the median share hidden by the depth is >= 15 % of the list-0
  draws AND >= 10 % of the list-0 triangles AND the median drop of list
  0's GPU time with those commands left out is >= 10 %, every pass dump of
  4 and 5 equal to 3's byte for byte.  A row whose pictures differ makes
  the verdict INVALID (something counted hidden drew a pixel).  Otherwise
  FAIL.  The report also gives the medians of the share hidden for any
  reason and the verdict that share would give.

The per-run logs stay beside the report (occlusion/<n>-<setting>/).

    python3 -I tools/occlusion_corpus.py [--tool PATH] [--list FILE]
        [--out FILE] [--repeat K] [--settings orig,enh2x43] [--limit N]
"""

import argparse
import filecmp
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
}

LINE_RE = re.compile(
    r"occlusion: hidden (\d+) of (\d+) list-0 draws, (\d+) of (\d+) triangles "
    r"\(index count / 3\); hidden commands: (\S+)"
)
CMDS_RE = re.compile(r"occlusion: (\d+) of (\d+) draw commands hidden")
GPU_RE = re.compile(
    r"gpu median of (\d+) replays: total ([0-9.]+) ms, list 0 ([0-9.]+) ms, "
    r"VU pre-pass ([0-9.]+) ms"
)

GATE_DRAWS = 15.0
GATE_TRIS = 10.0
GATE_GPU = 10.0


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


def run(cmd, env, lock, log):
    """One replay under the lock; (exit code, stdout + stderr), the log written."""
    full = ["flock", str(lock)] + cmd
    try:
        p = subprocess.run(full, env=env, capture_output=True, text=True, timeout=1800)
        out, rc = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or "") if isinstance(e.stdout, str) else ""
        out, rc = out + "\n(timed out after 1800 s)\n", -1
    log.write_text(" ".join(full) + "\n\n" + out)
    return rc, out


def same_dirs(a, b):
    """(targets compared, targets that differ or are missing, the gate's DISPLAY/PRESENT same)"""
    names = sorted(n for n in os.listdir(a) if n.endswith(".png")) if os.path.isdir(a) else []
    differ = []
    for n in names:
        other = os.path.join(b, n)
        if not os.path.exists(other) or not filecmp.cmp(os.path.join(a, n), other, shallow=False):
            differ.append(n)
    gate = all(
        os.path.exists(os.path.join(a, n))
        and os.path.exists(os.path.join(b, n))
        and n not in differ
        for n in ("DISPLAY.png", "PRESENT.png")
    )
    return len(names), differ, gate


def pct(a, b):
    return 100.0 * a / b if b else 0.0


DRAW_TYPES = {"SCREEN", "MESH", "SKINNED", "GRID", "PARTICLES"}


def read_csv(path):
    """{command: (type, draws, counted, triangles, samples)} from --occlusion-csv"""
    rows = {}
    if not path.exists():
        return rows
    for line in path.read_text().splitlines()[1:]:
        f = line.split(",")
        if len(f) == 6:
            rows[int(f[0])] = (f[1], int(f[2]), int(f[3]), int(f[4]), int(f[5]))
    return rows


def hidden_of(e):
    return e[2] == e[1] and e[4] == 0


def gate(md, mt, mg):
    return md >= GATE_DRAWS and mt >= GATE_TRIS and mg >= GATE_GPU


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--tool", default=str(ROOT / "build-host/linux-x64/port/render/rd_replay_tool"))
    ap.add_argument("--list", default=str(ROOT / "tools/occlusion_corpus.txt"))
    ap.add_argument("--out", default=str(ROOT / "build-host/tmp/v098/evidence/perf/occlusion.md"))
    ap.add_argument("--lock", default=str(ROOT / "build-host/tmp/replay.lock"))
    ap.add_argument("--repeat", type=int, default=5, help="warm GPU replays a timing (--gpu-repeat)")
    ap.add_argument("--settings", default="orig,enh2x43")
    ap.add_argument("--limit", type=int, default=0, help="the first N dumps only (0: all)")
    args = ap.parse_args()

    settings = [s for s in args.settings.split(",") if s]
    for s in settings:
        if s not in SETTINGS:
            sys.exit(f"unknown setting {s} (have {', '.join(SETTINGS)})")
    dumps, missing = read_list(Path(args.list))
    if args.limit:
        dumps = dumps[: args.limit]
    if not dumps:
        sys.exit(f"{args.list}: no dump is here")
    out = Path(args.out)
    logs = out.parent / "occlusion"
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.setdefault("VK_ICD_FILENAMES", "/usr/share/vulkan/icd.d/lvp_icd.json")
    nodepth = dict(env, ICO_VK_DEPTH_ALWAYS="1")
    fixed = dict(env, ICO_RD_BLEND="fixed")
    nan = float("nan")

    rows = []
    for i, dump in enumerate(dumps, 1):
        for s in settings:
            d = logs / f"{i:02d}-{s}"
            d.mkdir(parents=True, exist_ok=True)
            dirs = {k: d / k for k in ("plain", "nopall", "nopdepth")}
            for p in dirs.values():
                p.mkdir(exist_ok=True)
                for f in p.glob("*.png"):
                    f.unlink()
            hidden, depth = d / "hidden.txt", d / "depth.txt"
            occ_csv, nd_csv = d / "occ.csv", d / "nodepth.csv"
            for f in (occ_csv, nd_csv):
                if f.exists():
                    f.unlink()
            row = {"dump": dump, "setting": s, "note": ""}
            rows.append(row)
            rc, txt = run([args.tool, dump, str(d / "occlusion.png"), "--occlusion",
                           "--occlusion-out", str(hidden), "--occlusion-csv", str(occ_csv)]
                          + SETTINGS[s], env, args.lock, d / "occlusion.log")
            if rc == 77:
                sys.exit(f"{dump}: no device or no occlusion queries (exit 77)")
            if rc != 0 or not LINE_RE.search(txt):
                row["note"] = ("cannot show this aspect" if "cannot show" in txt
                               else f"--occlusion exit {rc}")
                print(f"{dump} {s}: {row['note']}", flush=True)
                continue
            rc2, _ = run([args.tool, dump, str(d / "nodepth.png"), "--occlusion",
                          "--occlusion-out", str(d / "nodepth.txt"), "--occlusion-csv",
                          str(nd_csv)] + SETTINGS[s], nodepth, args.lock, d / "nodepth.log")
            occ, nd = read_csv(occ_csv), read_csv(nd_csv)
            if rc2 != 0:
                row["note"] = f"the run without the depth exit {rc2}"
            draws = tris = hd = ht = dd = dt = nn = 0
            depth_cmds = []
            for c, e in sorted(occ.items()):
                if e[0] not in DRAW_TYPES:
                    continue
                draws += e[1]
                tris += e[3]
                if not hidden_of(e):
                    continue
                hd += e[1]
                ht += e[3]
                o = nd.get(c)
                if o is not None and hidden_of(o):
                    nn += e[1]  # draws nothing whatever the depth says
                elif o is not None:
                    dd += e[1]
                    dt += e[3]
                    depth_cmds.append(c)
            depth.write_text((",".join(f"0:{c}" for c in depth_cmds) or "none") + "\n")
            row.update(draws=draws, tris=tris, hidden=hd, htris=ht, ddraws=dd, dtris=dt,
                       nothing=nn, dcmds=len(depth_cmds))
            stat = ["--stats", "--gpu-repeat", str(args.repeat)] + SETTINGS[s]
            rcp, tp = run([args.tool, dump, "-", "--pass-dump", str(dirs["plain"])] + stat,
                          fixed, args.lock, d / "plain.log")
            rca, _ = run([args.tool, dump, "-", "--pass-dump", str(dirs["nopall"]), "--nop",
                          "@" + str(hidden)] + SETTINGS[s], fixed, args.lock, d / "nopall.log")
            rcd, td = run([args.tool, dump, "-", "--pass-dump", str(dirs["nopdepth"]), "--nop",
                           "@" + str(depth)] + stat, fixed, args.lock, d / "nopdepth.log")
            gp, gd = GPU_RE.search(tp), GPU_RE.search(td)
            if rcp or rca or rcd or not gp or not gd:
                row["note"] += f" timing/pass-dump exit {rcp}/{rca}/{rcd}"
                row["note"] += "" if gp and gd else ", no GPU median"
            if gp and gd:
                row.update(l0=float(gp.group(3)), l0nop=float(gd.group(3)),
                           pre=float(gp.group(4)), prenop=float(gd.group(4)))
            same = True
            for k in ("nopall", "nopdepth"):
                compared, differ, ok = same_dirs(str(dirs["plain"]), str(dirs[k]))
                row[k] = (compared, differ, ok and not differ and compared > 0)
                same = same and row[k][2]
            row["same"] = same
            drop = pct(row["l0"] - row["l0nop"], row["l0"]) if "l0" in row else nan
            print(f"{dump} {s}: hidden {hd}/{draws} draws ({pct(hd, draws):.1f} %), by the depth "
                  f"{dd} ({pct(dd, draws):.1f} %), triangles {dt}/{tris} ({pct(dt, tris):.1f} %); "
                  f"list 0 GPU {row.get('l0', nan):.3f} -> {row.get('l0nop', nan):.3f} ms "
                  f"({drop:.1f} %); identical {'yes' if same else 'NO'}", flush=True)

    measured = [r for r in rows if r.get("draws")]
    timed = [r for r in measured if r.get("l0", 0) > 0]

    def med(v):
        return statistics.median(v) if v else nan

    def medians(rs, ts):
        return (med([pct(r["ddraws"], r["draws"]) for r in rs]),
                med([pct(r["dtris"], r["tris"]) for r in rs]),
                med([pct(r["l0"] - r["l0nop"], r["l0"]) for r in ts]),
                med([pct(r["hidden"], r["draws"]) for r in rs]),
                med([pct(r["htris"], r["tris"]) for r in rs]))

    md, mt, mg, ma, mat = medians(measured, timed)
    bad = [r for r in measured if not r.get("same")]
    if not measured or not timed:
        verdict = "NO DATA: no row measured a list-0 draw and its GPU time"
    elif bad:
        verdict = (f"INVALID: {len(bad)} rows' pass dumps differ with the hidden commands left "
                   "out (something counted hidden drew a pixel)")
    elif gate(md, mt, mg):
        verdict = "PASS: the occlusion culler is built"
    else:
        verdict = "FAIL: occlusion culling is not built; the numbers go into RENDER.md"
    any_verdict = "would pass" if gate(ma, mat, mg) else "would fail"

    lines = [
        "# Occlusion: the hidden share of list 0",
        "",
        f"Tool `{args.tool}`, list `{args.list}`, {len(dumps)} dumps here "
        f"({len(missing)} entries of the list not here), settings {', '.join(settings)}, "
        f"GPU medians of {args.repeat} warm replays on `{env.get('VK_ICD_FILENAMES', '')}`. "
        "Measured by `rd_replay_tool --occlusion` (ICO_RD_VU_MERGE=0, ICO_RD_BLEND=fixed, the "
        "screen-prim runs off: one GPU draw a command), once as drawn and once under "
        "ICO_VK_DEPTH_ALWAYS=1. A draw command is hidden when every draw it made passed 0 "
        "samples; hidden by the depth when it is hidden as drawn and not with every depth test "
        "passing; it draws nothing when it is hidden either way (outside the view or the "
        "scissor, every fragment discarded: no occlusion test saves it). Timed and compared "
        "under ICO_RD_BLEND=fixed with the draw merges on. The drop is (plain - NOP) / plain of "
        "list 0's GPU time with the commands hidden by the depth left out; the VU pre-pass is "
        "timed apart (the NOPs shorten it too). Identical: every target of `--pass-dump` with "
        "all hidden commands left out, and with those hidden by the depth, equal to the plain "
        "one byte for byte.",
        "",
        "| # | dump | setting | draws | hidden | % | by the depth | % | draw nothing | "
        "triangles | by the depth | % | list 0 ms | NOP ms | drop % | pre-pass ms | NOP ms | "
        "identical | note |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for k, r in enumerate(rows, 1):
        name = os.path.relpath(r["dump"], ROOT) if r["dump"].startswith(str(ROOT)) else r["dump"]
        if not r.get("draws"):
            lines.append(f"| {k} | {name} | {r['setting']} |" + " |" * 15
                         + f" {r['note'] or 'no list-0 draw'} |")
            continue
        g = (f"{r['l0']:.3f} | {r['l0nop']:.3f} | {pct(r['l0'] - r['l0nop'], r['l0']):.1f} | "
             f"{r['pre']:.3f} | {r['prenop']:.3f}") if "l0" in r else " | | | | "
        if r.get("same"):
            ident = "yes"
        else:
            ident = "NO: " + "; ".join(
                f"{k2} {', '.join(r[k2][1]) or 'DISPLAY/PRESENT missing'}"
                for k2 in ("nopall", "nopdepth") if k2 in r and not r[k2][2])
        lines.append(
            f"| {k} | {name} | {r['setting']} | {r['draws']} | {r['hidden']} | "
            f"{pct(r['hidden'], r['draws']):.1f} | {r['ddraws']} | "
            f"{pct(r['ddraws'], r['draws']):.1f} | {r['nothing']} | {r['tris']} | {r['dtris']} | "
            f"{pct(r['dtris'], r['tris']):.1f} | {g} | {ident} | {r['note'].strip()} |")
    lines += ["", "## Medians", "",
              "| rows | by the depth: draws % | triangles % | list 0 GPU drop % | "
              "any reason: draws % | triangles % |",
              "|---|---|---|---|---|---|",
              f"| all ({len(measured)} measured, {len(timed)} timed) | {md:.1f} | {mt:.1f} | "
              f"{mg:.1f} | {ma:.1f} | {mat:.1f} |"]
    for s in settings:
        rs = [r for r in measured if r["setting"] == s]
        ts = [r for r in timed if r["setting"] == s]
        a, b, c, e, f = medians(rs, ts)
        lines.append(f"| {s} ({len(rs)} measured, {len(ts)} timed) | {a:.1f} | {b:.1f} | "
                     f"{c:.1f} | {e:.1f} | {f:.1f} |")
    lines += ["", "## The gate", "",
              f"The median share hidden by the depth >= {GATE_DRAWS:g} % of the list-0 draws AND "
              f">= {GATE_TRIS:g} % of the list-0 triangles over the gameplay dumps AND the NOP "
              f"replay's list-0 GPU time on lavapipe drops >= {GATE_GPU:g} %, every compared pass "
              "dump identical (the medians over all measured rows, both settings).", "",
              f"**{verdict}** (by the depth: draws {md:.1f} %, triangles {mt:.1f} %; list 0 GPU "
              f"drop {mg:.1f} %)", "",
              f"Judged on the share hidden for any reason (draws {ma:.1f} %, triangles "
              f"{mat:.1f} %) the gate {any_verdict}.", ""]
    if missing:
        lines += ["Not here:", ""] + [f"- `{p}`" for p in missing] + [""]
    out.write_text("\n".join(lines))
    print(f"{out}: {verdict}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
