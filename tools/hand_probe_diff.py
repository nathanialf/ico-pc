#!/usr/bin/env python3
"""Read the hand probe's lines (issue 19) from one or two ico-pc.log files.

The probe ([dev] hand_probe = true, port/platform/diag_host.h) writes
"probe:" lines while Ico holds Yorda's hand:

  probe: on version=1 every=30
  probe: tick t=<tick> boy=<x,y,z> girl=<x,y,z> bdir=<x,z> gdir=<x,z>
         bmot=<n> gmot=<n> bact=<n> gact=<n> vu0r=<hex> rand=<hex>
         bframe=<f> gframe=<f>                       (every Main tick)
  probe: ik t=<tick> hand=<0|1> mode=<5|6> na=<n> nb=<n> nc=<n> girl=<0|1>
         own=<x,y,z> tb=<x,y,z> tc=<x,y,z> len=<f> sa=<f> sb=<f>
         scale=<f> tscale=<f> ikdir=<x,y,z>          (every 30 ticks)
  probe: girl t=<tick> bmode=<n> p1=<n> p2=<n> p3=<n> hflags=<hex> st=<n>
         req=<n> mot=<n> spri=<n> hdist=<f> hheight=<f> dist=<f>
         pulllen=<f> pullturn=<f> speed=<f>          (every 30 ticks)

(each one line), every float written value/bits (the bits are the float's
32 bits in hex, so two machines' numbers compare exactly).

  tools/hand_probe_diff.py phone.log
      summarise one log: the shoulder distance against the arms' reach,
      how often Yorda is closer than a fifth of the reach (at Ico's
      shoulder), and how far up the arms point.
  tools/hand_probe_diff.py phone.log pc.log [--align first]
      both summaries, then the first tick where any field differs (by
      bits). The ticks are compared as numbered (two runs of the same
      input recording); --align first lines up the first "tick" line of
      each log instead (two runs by hand: only the summaries mean much).
"""

import argparse
import math
import struct
import sys

KINDS = ("tick", "ik", "girl")


def parse_value(text):
    """A field: a list of (float, bits) for value/bits parts, else the text."""
    if "/" not in text:
        return text
    out = []
    for part in text.split(","):
        val, _, bits = part.partition("/")
        try:
            b = int(bits, 16)
            out.append((struct.unpack("<f", struct.pack("<I", b))[0], b))
        except ValueError:
            return text
    return out


def parse(path):
    """The probe records of a log: {kind: [dict]} and the version line."""
    recs = {k: [] for k in KINDS}
    version = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            i = line.find("probe: ")
            if i < 0:
                continue
            words = line[i + 7:].split()
            if not words:
                continue
            kind = words[0]
            fields = {}
            order = []
            for w in words[1:]:
                k, sep, v = w.partition("=")
                if sep:
                    fields[k] = parse_value(v)
                    order.append(k)
            if kind == "on":
                version = fields.get("version")
                continue
            if kind not in recs or "t" not in fields:
                continue
            fields["_order"] = order
            fields["_line"] = line.rstrip("\n")[i:]
            recs[kind].append(fields)
    return recs, version


def f1(rec, key):
    v = rec.get(key)
    return v[0][0] if isinstance(v, list) and v else float("nan")


def vec(rec, key):
    v = rec.get(key)
    return [x[0] for x in v] if isinstance(v, list) else []


def finite(rec):
    for k, v in rec.items():
        if not k.startswith("_") and isinstance(v, list):
            for x, _ in v:
                if not math.isfinite(x):
                    return False
    return True


def elevation(frm, to):
    """Degrees from frm up to to; the game's world has -y up (the shoulders
    are about 100 below the root's y), so a smaller y is higher."""
    dx, dy, dz = to[0] - frm[0], to[1] - frm[1], to[2] - frm[2]
    n = math.sqrt(dx * dx + dy * dy + dz * dz)
    if n <= 0:
        return float("nan")
    return math.degrees(math.asin(max(-1.0, min(1.0, -dy / n))))


def stats(xs):
    xs = [x for x in xs if math.isfinite(x)]
    if not xs:
        return "none"
    return "min %.4g, mean %.4g, max %.4g" % (min(xs), sum(xs) / len(xs), max(xs))


def summarise(name, recs):
    print("== %s" % name)
    ticks, iks, girls = recs["tick"], recs["ik"], recs["girl"]
    print("  %d tick lines, %d arm lines, %d Yorda lines" % (len(ticks), len(iks), len(girls)))
    if not (ticks or iks or girls):
        print("  no probe lines: was hand_probe on, and did Ico hold Yorda's hand?")
        return
    bad = sum(1 for r in ticks + iks + girls if not finite(r))
    if bad:
        print("  %d lines hold a NaN or an infinity" % bad)
    if ticks:
        d = []
        for r in ticks:
            b, g = vec(r, "boy"), vec(r, "girl")
            if len(b) == 3 and len(g) == 3:
                d.append(math.hypot(b[0] - g[0], b[2] - g[2]))
        print("  Ico to Yorda on the ground (root positions): %s" % stats(d))
    reach = [r for r in iks if r.get("mode") == "5"]
    if reach:
        lens, sums, ratios, lift_code, lift_seen = [], [], [], [], []
        near = 0
        for r in reach:
            ln, sa, sb = f1(r, "len"), f1(r, "sa"), f1(r, "sb")
            lens.append(ln)
            sums.append(sa + sb)
            if sa + sb > 0:
                ratios.append(ln / (sa + sb))
                if ln < 0.2 * (sa + sb):
                    near += 1
            # connectToTarget's angle (law of cosines: the hands meet where
            # two arms of sa and sb reach across len); 0 is level, 90 up
            if ln > 0 and sa > 0 and ln <= sa + sb:
                c = (ln * ln + sa * sa - sb * sb) / (2 * ln * sa)
                lift_code.append(math.degrees(math.acos(max(-1.0, min(1.0, c)))))
            own, ik = vec(r, "own"), vec(r, "ikdir")
            if len(own) == 3 and len(ik) == 3:
                lift_seen.append(elevation(own, ik))
        print("  Ico's reach for Yorda (%d samples):" % len(reach))
        print("    shoulder distance len: %s" % stats(lens))
        print("    both arms' reach sa+sb: %s" % stats(sums))
        print("    len / reach: %s" % stats(ratios))
        print("    Yorda within a fifth of the reach (at Ico's shoulder): %d of %d" %
              (near, len(reach)))
        print("    arm angle the code works out (0 level, 90 straight up or down): %s" % stats(lift_code))
        print("    hand target above the shoulder, degrees (below if negative): %s" %
              stats(lift_seen))
        # Yorda's line of the same tick: what she reaches for is Ico's hand
        # (his focus-6 node, the wrist), so it says where his arm really is
        hand = []
        for r in reach:
            for y in iks:
                if y.get("mode") == "6" and y.get("t") == r.get("t") and y.get("girl") == "0":
                    own, tb = vec(r, "own"), vec(y, "tb")
                    if len(own) == 3 and len(tb) == 3:
                        hand.append(elevation(own, tb))
        if hand:
            print("    Ico's hand (what Yorda reaches for) above his shoulder, degrees "
                  "(below if negative): %s" % stats(hand))
        sc = sorted({(f1(r, "scale"), f1(r, "tscale")) for r in reach})
        print("    scales (Ico, Yorda): %s" % ", ".join("%.4g %.4g" % s for s in sc[:4]))
        tg = sorted({r.get("girl") for r in reach})
        print("    target is Yorda: %s" % ", ".join(tg))
    if girls:
        sts = {}
        for r in girls:
            sts[r.get("st")] = sts.get(r.get("st"), 0) + 1
        print("  Yorda: hand distance %s" % stats([f1(r, "hdist") for r in girls]))
        print("    pull distance dist %s" % stats([f1(r, "dist") for r in girls]))
        print("    speed ratio %s; steps %s" %
              (stats([f1(r, "speed") for r in girls]),
               ", ".join("st %s x%d" % (k, v) for k, v in sorted(sts.items()))))


def keyed(recs, offset):
    out = {}
    for kind in KINDS:
        for r in recs[kind]:
            try:
                t = int(r["t"]) - offset
            except (TypeError, ValueError):
                continue
            out[(t, kind, r.get("hand", ""), r.get("mode", ""))] = r
    return out


def same(a, b):
    if isinstance(a, list) and isinstance(b, list):
        return [x[1] for x in a] == [x[1] for x in b]
    return a == b


def show(v):
    if isinstance(v, list):
        return ",".join("%.9g/%08x" % x for x in v)
    return str(v)


def diff(recs_a, recs_b, name_a, name_b, align):
    off_a = off_b = 0
    if align == "first":
        if not recs_a["tick"] or not recs_b["tick"]:
            print("--align first: a log has no tick lines")
            return 1
        off_a, off_b = int(recs_a["tick"][0]["t"]), int(recs_b["tick"][0]["t"])
    ka, kb = keyed(recs_a, off_a), keyed(recs_b, off_b)
    common = sorted(set(ka) & set(kb))
    print("== compare (%d records in both; ticks %s)" %
          (len(common), "from each log's first tick line" if align == "first" else "as numbered"))
    if not common:
        print("  no tick in both logs: try --align first")
        return 1
    for key in common:
        a, b = ka[key], kb[key]
        fields = [k for k in a["_order"] if not same(a.get(k), b.get(k))]
        fields += [k for k in b["_order"] if k not in a["_order"]]
        if fields:
            print("  first difference: tick %d (%s), fields %s" % (key[0], key[1], " ".join(fields)))
            print("  %s: %s" % (name_a, a["_line"]))
            print("  %s: %s" % (name_b, b["_line"]))
            for k in fields:
                print("    %s: %s | %s" % (k, show(a.get(k)), show(b.get(k))))
            return 2
    print("  every common record is the same, bit for bit")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("logs", nargs="+", help="one or two ico-pc.log files")
    ap.add_argument("--align", choices=("tick", "first"), default="tick",
                    help="match records by tick number (default) or from each log's first tick")
    args = ap.parse_args()
    if len(args.logs) > 2:
        ap.error("one or two logs")
    parsed = []
    for p in args.logs:
        recs, version = parse(p)
        if version is not None and version != "1":
            print("%s: probe version %s, this script reads 1" % (p, version))
        summarise(p, recs)
        parsed.append(recs)
    if len(parsed) == 2:
        return diff(parsed[0], parsed[1], args.logs[0], args.logs[1], args.align)
    return 0


if __name__ == "__main__":
    sys.exit(main())
