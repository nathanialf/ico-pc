#!/usr/bin/env python3
"""tools/region_camera_report.py: the camera rule against the PS2's region
rule on saved frames (docs/port/VU1_PROGRAMS.md "The region rule",
docs/port/DIVERGENCES.md VD-29; port/shaders/vu_common.hlsli vu_drop_fate).

  python3 -I tools/region_camera_report.py --tool RD_REPLAY_TOOL --out DIR
          [--list FILE | DUMP...] [--setting NAME=ARGS]... [--own-aspect]
          [--keep-camera DIR] [--report FILE] [--dilate N] [--only TEXT]

For every dump and setting, rd_replay_tool twice, serially:
  A  ICO_RD_REGION_RULE=ps2:    --pass-dump DIR/<dump>-<setting>/ps2 and
     --region-mask (where the two rules draw differently, on the CPU:
     red a triangle the PS2's rule drops, or draws beside the 4:3 picture
     only, that the camera rule draws; green one the PS2's card test draws
     that the camera rule drops, a back face; blue a sprite the PS2 drops
     out of the box; port/render/tools/rd_replay_region.c);
  B  ICO_RD_REGION_RULE=camera: --pass-dump DIR/<dump>-<setting>/camera.
Then every PNG of A against B's, and:
  - SCENE (the 3D frame; its width a whole multiple S of the mask's GS
    width, its height of the GS height): every pixel that differs must lie
    in the mask, scaled by S and grown by --dilate N GS pixels (default 1:
    a pixel the triangle's edge rasterises on the other side of a centre).
    A differing pixel outside it is a FAILURE: the camera rule changed
    something the CPU oracle says it does not;
  - with an empty mask (no triangle or sprite differs) every target must be
    byte for byte the same (FAILURE otherwise);
  - every other target: the differing pixels counted and reported, and how
    many of them lie in the scaled, grown mask where its size allows (the
    post effects, the shadows and the reflections read the scene: their
    changes are reported, not held).
--keep-camera DIR copies B's PNGs to DIR/<name>-<setting>/ (the camera
rule's new baselines, named as main-chain.sh's pass-dump compare names
them). --report FILE writes a markdown table: a row a dump and setting with
the pixels changed in SCENE and in the present, the mask's triangles and
sprites, the SCENE pixels outside the mask, and the targets that changed.

Settings: NAME=ARGS (ARGS split on spaces), by default
  orig     --aspect 4:3 --present 1280x960   (the Original preset at 1x)
  enh2x43  --enhanced --aspect 4:3 --resolution 2x --present 1280x960
(the pass-dump compare's two). --own-aspect appends --aspect A from a list
line "path A" (tools/sideband_dumps.txt's form) to every setting instead.

The replays hold the shared replay lock (build-host/tmp/replay.lock or
ICO_REPLAY_LOCK) while they run.  Exit 0 when every hard rule holds, 1 on
a failure, 2 on a usage error, 77 when nothing could run (no dump, no
device).  Standard library only (the PNG reader is tools/cmp_dirs_band.py's)."""
import fcntl
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SETTINGS = [
    ("orig", "--aspect 4:3 --present 1280x960"),
    ("enh2x43", "--enhanced --aspect 4:3 --resolution 2x --present 1280x960"),
]


def load_png(path):
    """(w, h, bpp, rows of bytes) of an 8-bit RGB or RGBA PNG."""
    data = open(path, "rb").read()
    at, idat, w, h, ctype = 8, [], 0, 0, 6
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk[:13])
            if depth != 8 or ctype not in (2, 6) or interlace != 0:
                raise ValueError("%s: not an 8-bit non-interlaced RGB(A) PNG" % path)
        elif kind == b"IDAT":
            idat.append(chunk)
        at += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    rows, prev, k = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[k]
        k += 1
        r = bytearray(raw[k:k + stride])
        k += stride
        if f == 1:
            for x in range(bpp, stride):
                r[x] = (r[x] + r[x - bpp]) & 255
        elif f == 2:
            r = bytearray((a + b) & 255 for a, b in zip(r, prev))
        elif f == 3:
            for x in range(stride):
                a = r[x - bpp] if x >= bpp else 0
                r[x] = (r[x] + (a + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(stride):
                a = r[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                r[x] = (r[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(r))
        prev = r
    return w, h, bpp, rows


def read_list(path):
    """[(dump, aspect or None)] of a list file (comments and blanks skipped,
    relative paths against the list's folder)."""
    out = []
    base = os.path.dirname(os.path.abspath(path))
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        p = parts[0]
        if not os.path.isabs(p):
            # a relative path is the list's folder's, else the repo's (the
            # pass-dump corpus list names its dumps from the repo root, as
            # gate.sh reads it), else the working folder's
            for cand in (os.path.join(base, p), os.path.join(ROOT, p), p):
                if os.path.exists(cand):
                    p = cand
                    break
        out.append((p, parts[1] if len(parts) > 1 else None))
    return out


def dump_name(path):
    """main-chain.sh's name(): parent folder and stem, its prefixes cut."""
    parent = os.path.basename(os.path.dirname(path))
    stem = os.path.splitext(os.path.basename(path))[0]
    n = "%s-%s" % (parent, stem)
    for pre in ("ico-dumps-", "102-", "95-", "deja-", "keep-"):
        if n.startswith(pre):
            n = n[len(pre):]
            break
    return n


class Lock:
    def __enter__(self):
        path = os.environ.get("ICO_REPLAY_LOCK", os.path.join(ROOT, "build-host", "tmp", "replay.lock"))
        try:
            self.f = open(path, "a")
            fcntl.flock(self.f, fcntl.LOCK_EX)
        except OSError:
            self.f = None
        return self

    def __exit__(self, *a):
        if self.f:
            self.f.close()


def run(tool, dump, args, rule, extra):
    env = os.environ.copy()
    env["ICO_RD_REGION_RULE"] = rule
    cmd = [tool, dump, "-"] + args + extra
    with Lock():
        r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=1800)
    return r.returncode, r.stdout + r.stderr


def grown_mask(mrows, mbpp, gw, gh, dilate):
    """the mask's set pixels (any of R, G, B) grown by dilate GS pixels: a
    list of bytearrays, 1 where set"""
    m = [bytearray(gw) for _ in range(gh)]
    for y in range(gh):
        row = mrows[y]
        for x in range(gw):
            o = x * mbpp
            if row[o] or row[o + 1] or row[o + 2]:
                m[y][x] = 1
    if dilate <= 0:
        return m
    g = [bytearray(gw) for _ in range(gh)]
    for y in range(gh):
        for x in range(gw):
            if m[y][x]:
                for yy in range(max(0, y - dilate), min(gh, y + dilate + 1)):
                    r = g[yy]
                    for xx in range(max(0, x - dilate), min(gw, x + dilate + 1)):
                        r[xx] = 1
    return g


def diff_count(ra, rb, w, h, bpp, mask=None, sx=0, sy=0):
    """(differing pixels, of them outside the mask, bbox of the outside ones)"""
    n = out = 0
    box = [w, h, -1, -1]
    for y in range(h):
        a, b = ra[y], rb[y]
        if a == b:
            continue
        mrow = mask[y // sy] if mask is not None else None
        for x in range(w):
            o = x * bpp
            if a[o:o + bpp] != b[o:o + bpp]:
                n += 1
                if mrow is not None and not mrow[x // sx]:
                    out += 1
                    box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
    return n, out, box


def main():
    args = sys.argv[1:]
    tool = out = report = keep = only = listf = None
    dilate, own = 1, False
    settings, dumps = [], []
    i = 0
    try:
        while i < len(args):
            a = args[i]
            if a == "--tool":
                tool = args[i + 1]; i += 2
            elif a == "--out":
                out = args[i + 1]; i += 2
            elif a == "--report":
                report = args[i + 1]; i += 2
            elif a == "--keep-camera":
                keep = args[i + 1]; i += 2
            elif a == "--list":
                listf = args[i + 1]; i += 2
            elif a == "--only":
                only = args[i + 1]; i += 2
            elif a == "--dilate":
                dilate = int(args[i + 1]); i += 2
            elif a == "--setting":
                name, _, rest = args[i + 1].partition("=")
                settings.append((name, rest)); i += 2
            elif a == "--own-aspect":
                own = True; i += 1
            elif a.startswith("--"):
                raise ValueError(a)
            # (one option a branch; the values follow their switch)
            else:
                dumps.append((a, None)); i += 1
    except (IndexError, ValueError) as e:
        print("region_camera_report: bad arguments (%s); see the docstring" % e, file=sys.stderr)
        return 2
    if not tool or not out:
        print("region_camera_report: --tool and --out are needed", file=sys.stderr)
        return 2
    if listf:
        if not os.path.exists(listf):
            print("region_camera_report: SKIP: no list %s" % listf)
            return 77
        dumps += read_list(listf)
    settings = settings or DEFAULT_SETTINGS
    have = [(d, a) for d, a in dumps if os.path.exists(d) and (not only or only in d)]
    for d, _ in dumps:
        if not os.path.exists(d):
            print("  %s: no such dump, skipped" % d)
    if not have:
        print("region_camera_report: SKIP: no dump here")
        return 77
    os.makedirs(out, exist_ok=True)
    rows, fails, ran = [], 0, 0
    for dump, aspect in have:
        name = dump_name(dump)
        for sname, sargs in settings:
            sa = sargs.split()
            if own and aspect:
                sa = [x for j, x in enumerate(sa) if x != "--aspect" and (j == 0 or sa[j - 1] != "--aspect")]
                sa += ["--aspect", aspect]
            tag = "%s-%s" % (name, sname)
            d = os.path.join(out, tag)
            da, db, mask = os.path.join(d, "ps2"), os.path.join(d, "camera"), os.path.join(d, "mask.png")
            for x in (da, db):
                shutil.rmtree(x, ignore_errors=True)
                os.makedirs(x)
            rc, txt = run(tool, dump, sa + ["--pass-dump", da], "ps2", ["--region-mask", mask])
            open(os.path.join(d, "ps2.log"), "w").write(txt)
            if rc == 77:
                print("region_camera_report: SKIP: no device")
                return 77
            rc2, txt2 = run(tool, dump, sa + ["--pass-dump", db, "--stats"], "camera", [])
            open(os.path.join(d, "camera.log"), "w").write(txt2)
            if rc != 0 or rc2 != 0 or not os.path.exists(mask):
                print("FAIL %s: the replays exited %d / %d (logs in %s)" % (tag, rc, rc2, d))
                fails += 1
                continue
            ran += 1
            mline = next((l for l in txt.splitlines() if "region: mask:" in l), "")
            gw, gh, mb, mrows = load_png(mask)
            g = grown_mask(mrows, mb, gw, gh, dilate)
            empty = not any(any(r) for r in grown_mask(mrows, mb, gw, gh, 0))
            changed, scene, present, outside, hard = [], (0, 0), 0, 0, True
            notes = []
            for png in sorted(os.listdir(da)):
                if not png.endswith(".png"):
                    continue
                pa, pb = os.path.join(da, png), os.path.join(db, png)
                if not os.path.exists(pb):
                    notes.append("%s missing under the camera rule" % png)
                    hard = False
                    continue
                if open(pa, "rb").read() == open(pb, "rb").read():
                    continue
                wa, ha, ba, ra = load_png(pa)
                wb, hb, bb, rb = load_png(pb)
                if (wa, ha, ba) != (wb, hb, bb):
                    notes.append("%s size changed" % png)
                    hard = False
                    continue
                sx = wa // gw if wa % gw == 0 else 0
                sy = ha // gh if ha % gh == 0 else 0
                fits = sx >= 1 and sy >= 1
                n, o, box = diff_count(ra, rb, wa, ha, ba, g if fits else None, sx, sy)
                if n == 0:
                    continue
                stem = png[:-4]
                changed.append("%s %d%s" % (stem, n, (" (%d outside the mask)" % o) if fits and o else ""))
                if stem == "SCENE":
                    scene = (n, o)
                    if not fits:
                        notes.append("SCENE %dx%d is not a whole multiple of the mask's %dx%d" % (wa, ha, gw, gh))
                        hard = False
                    elif o:
                        outside = o
                        notes.append("SCENE: %d differing pixels outside the mask, box (%d,%d)-(%d,%d)" %
                                     (o, box[0], box[1], box[2], box[3]))
                        hard = False
                if stem == "PRESENT":
                    present = n
                if empty:
                    notes.append("%s differs with an empty mask" % stem)
                    hard = False
            if keep:
                kd = os.path.join(keep, tag)
                shutil.rmtree(kd, ignore_errors=True)
                shutil.copytree(db, kd)
                shutil.copy(os.path.join(d, "camera.log"), kd + ".log")
            status = "ok" if hard else "FAIL"
            fails += 0 if hard else 1
            mtext = mline.split("region: mask: ")[-1].split(" -> ")[0] if mline else "no mask line"
            print("%s %s: SCENE %d pixels changed (%d outside the mask); present %d; mask: %s; %s%s" %
                  (status, tag, scene[0], outside, present, mtext,
                   ", ".join(changed) or "every target the same",
                   ("; " + "; ".join(notes)) if notes else ""))
            rows.append((tag, scene[0], outside, present, mline, changed, notes, hard))
    if report:
        with open(report, "w", encoding="utf-8") as f:
            f.write("# The camera rule against the PS2's region rule\n\n")
            f.write("Made by `tools/region_camera_report.py` (rd_replay_tool %s, dilate %d GS pixel%s).\n"
                    "SCENE's differing pixels must lie in the CPU oracle's mask "
                    "(`--region-mask`); the other targets are reported.\n\n" %
                    (tool, dilate, "" if dilate == 1 else "s"))
            f.write("| dump-setting | SCENE pixels changed | outside the mask | present pixels changed | mask | targets changed | result |\n")
            f.write("| --- | --- | --- | --- | --- | --- | --- |\n")
            for tag, n, o, p, ml, ch, notes, hard in rows:
                m = ml.split("region: mask: ")[-1] if ml else ""
                m = m.split(" -> ")[0]
                f.write("| %s | %d | %d | %d | %s | %s | %s |\n" %
                        (tag, n, o, p, m, ", ".join(ch) or "none",
                         "ok" if hard else "FAIL: " + "; ".join(notes)))
            f.write("\n%d replays compared, %d failed.\n" % (len(rows), fails))
    print("region_camera_report: %d dump settings compared, %d failed" % (ran, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
