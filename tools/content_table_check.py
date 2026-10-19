#!/usr/bin/env python3
"""tools/content_table_check.py: the content table (port/render/rd_content_hide.c,
rd_content_extend.c) against saved frames.

The renderer draws one camera on the native frame (vu_common.hlsli
vu_region_rule): a triangle that fails a VU program's region test is drawn,
clipped by the GPU, when it lies wholly beside the camera's picture (GS X
2048 -+ 256; seen only on a frame wider than 4:3); one that reaches into
the picture is not drawn out of the box (Original, 4:3, 1x), as the PS2
does not draw it, and in any other state (Enhanced among them) is drawn,
clipped, when its vertices are in front of the eye and it is not part of a
card, so the report below can show such pixels inside the picture in
Enhanced (a card, a free-standing strip none of whose triangles pass,
shows none).
The stage geometry made
for the PS2's 4:3 picture that passes the region test beside it (closing
planes) is listed in the content table with the verb HIDE (drawn
nowhere).  The stage overlays made to cover the PS2's 4:3 picture (a sheet
of tiles in front of the camera) are listed in rd_content_extend.c with the
verb TILES (tiles appended outward, so the sheet covers a wider frame).
This tool drives port/render/rd_replay_tool over dumps; the table's verbs
are overridden per run through ICO_RD_CONTENT_HIDE (rd_mesh.c
rd__content_hide_set: "N=off", "N=hide") and ICO_RD_CONTENT_EXTEND
(rd__content_extend_set: "N=off", "N=tiles").

  decide  for every entry of the table, on every 4:3 dump whose list draws
          the entry's mesh: the frame at 4:3 (every target and the present,
          Original 1x and Enhanced 2x) with the entry off and HIDE.  HIDE
          the same as off, byte for byte, on every such dump -> HIDE (the
          triangles never reach the 4:3 picture); else the faces show in
          the 4:3 picture and the entry must go: the mesh stays unlisted,
          drawn as geometry.  Exit 1 when an entry is listed HIDE and a
          dump shows its faces at 4:3.
          Then the sheets, the entries of one mesh together, on every dump
          whose list draws the mesh: with the entries off and TILES, the
          present cut to 4:3 (Original 1x and Enhanced 2x) is the same byte
          for byte and so is every target's 4:3 band (the centre 512 GS
          columns; on a 4:3 dump the whole target); on a dump wider than
          4:3, SCENE at 1x without the mesh's draws (--nop) is the
          reference: the columns the sheet leaves untouched with the
          entries off (the sides) must, with TILES, be changed in at least
          90 % of their pixels and by a mean within 1.5 levels of what the
          sheet does to the 4:3 band.  Exit 1 when one fails.
  report  the meshes that are NOT listed and are expected not to need an
          entry (the region rule covers them): by default st26a_p1 and
          st26a_near (the opening's cliff and tree card) on the opening
          dumps and the waterfall mist wf_smoke1 on the patched
          waterfall dump.  For each
          dump at each aspect it holds (a v10 dump: its own aspect; an
          aspect sweep zip, Shift+F12 in photo mode: 4:3, 16:9, 32:9, 48:9
          and 60:9; a dump before v10 is 4:3 only, the wider aspects are
          reported as needing a sweep) and each mesh it draws: SCENE at 1x
          in Enhanced and in Original, each with and without the mesh's
          draws (--nop).  The pixels the mesh adds in Enhanced but not in
          Original are what its clipped triangles draw: their count inside
          the camera's picture and beside it, their box and mean colour,
          and a picture with them in magenta (OUT/report/...png).  Inside
          the picture there should be none.  A report, not a gate.
  all     decide over the dump list, then report (the default)
  chains  the chains table (port/render/rd_content_chain.c, Natural chains)
          against the disc image of --version SLUG (a row of
          config/regions.txt, pal by default: its boot ELF, its
          port/data/gen/SLUG/table_desc.c and its groups,
          port/core/region_SLUG.c chain_group_*; the image --disc, else
          ICO_DISC_IMAGE (pal) or ICO_DISC_IMAGE_JPN (jpn), else baserom/
          Ico_PAL.iso or Ico_JPN.iso; an .iso only): first how many of the
          stage animation files the disc's stageTable names its packs hold
          (a report; --missing lists the others), then each entry's stage
          animation row
          (its group, unique), its Null (type 1, the keyed scale changing on
          the entry's axis alone, below 1 and up to 1), the links under it
          (one model, still, unturned and unscaled, evenly from the anchor to
          the end along the axis) and the model's reach past a node toward
          the anchor and away from it; then every stage animation of every pack scanned for a
          Null scaled on one axis over three or more links of one model laid
          along it: the ones found must be the table's.  Needs no replay
          tool and no dump.

  python3 -I tools/content_table_check.py [--tool T] [--out DIR] [--dumps LIST]
          [decide | report [--mesh NAME ...] [SOURCE ...] | all]
  python3 -I tools/content_table_check.py [--version SLUG] [--disc ISO] [--missing] chains

The dump list is tools/native_frame_dumps.txt (or ICO_DUMPS, or --dumps):
machine-local paths, dumps are never committed.  The report's default
sources are the list's v722, v934 and v1251 dumps (opening) and its
fwdump/patched dump (waterfall), plus any sweep zip given as a SOURCE.  The
whole run holds the shared replay lock (build-host/tmp/replay.lock, or
ICO_REPLAY_LOCK).  Python 3 only (a PNG reader of its own).
Exit: 0 done, 1 a decide failure or an error, 2 usage, 77 nothing to run.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "port", "render", "rd_content_hide.c")
EXTEND_TABLE = os.path.join(ROOT, "port", "render", "rd_content_extend.c")
DEFAULT_LIST = os.path.join(ROOT, "tools", "native_frame_dumps.txt")
DEFAULT_TOOL = os.path.join(ROOT, "build-host", "linux-x64", "port", "render", "rd_replay_tool")
DECIDE_SETTINGS = {
    "original_4x3_1x": ["--aspect", "4:3", "--present", "1440x1080"],
    "enhanced_4x3_2x": ["--enhanced", "--aspect", "4:3", "--resolution", "2x", "--present",
                        "2880x2160"],
}
REPORT_MESHES = {"opening": ["st26a_p1", "st26a_near"], "waterfall": ["wf_smoke1"]}
OPENING_DUMPS = ("v722", "v934", "v1251")
WATERFALL_DUMPS = ("fwdump/patched",)
SWEEP_ASPECTS = ["4x3", "16x9", "32x9", "48x9", "60x9"]
REPORT_ASPECTS = ["4x3", "16x9", "32x9", "48x9"]
ASPECT_VALUE = {"4x3": 4 / 3, "16x9": 16 / 9, "32x9": 32 / 9, "48x9": 48 / 9, "60x9": 60 / 9}
EXIT_SKIP = 77


def say(msg):
    print("content_table_check: " + msg, flush=True)


# ------------------------------------------------------------------ the table

def read_table(path=TABLE):
    """[(index, mesh, note, verb)] in the table's order."""
    src = open(path, encoding="utf-8").read()
    body = src[src.index("rd__content_hide[] = {"):]
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*RD_(HIDE)\s*,', body):
        note = m.group(1)
        out.append((len(out), note.split(":", 1)[0].strip(), note, m.group(2)))
    return out


def read_extend_table(path=EXTEND_TABLE):
    """[(index, mesh, note, verb)] of the sheets, in the table's order."""
    src = open(path, encoding="utf-8").read()
    body = src[src.index("rd__content_extend[] = {"):]
    out = []
    for m in re.finditer(r'\{\s*"([^"]*)"\s*,\s*RD_EXTEND_(TILES)\s*,', body):
        note = m.group(1)
        out.append((len(out), note.split(":", 1)[0].strip(), note, m.group(2)))
    return out


# -------------------------------------------------------------------- PNGs

def load_png(path):
    """(w, h, rows of RGB bytes) of an 8-bit RGB or RGBA PNG."""
    data = open(path, "rb").read()
    at, idat, w, h, ctype = 8, b"", 0, 0, 6
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            if depth != 8 or ctype not in (2, 6):
                raise SystemExit("%s: not an 8-bit RGB(A) PNG" % path)
        elif kind == b"IDAT":
            idat += chunk
        at += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
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
            for x in range(stride):
                r[x] = (r[x] + prev[x]) & 255
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
    if bpp == 4:
        rows = [bytes(b for i, b in enumerate(row) if i % 4 != 3) for row in rows]
    return w, h, rows


def save_png(path, w, h, rows):
    """rows of RGB bytes to an 8-bit RGB PNG."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def same_png(a, b):
    if open(a, "rb").read() == open(b, "rb").read():
        return True
    return load_png(a) == load_png(b)


# -------------------------------------------------------------- the replays

class Lock:
    """The shared replay lock, held for the whole run."""
    def __init__(self):
        self.f = None

    def __enter__(self):
        p = os.environ.get("ICO_REPLAY_LOCK") or os.path.join(ROOT, "build-host", "tmp", "replay.lock")
        try:
            import fcntl
            os.makedirs(os.path.dirname(p), exist_ok=True)
            self.f = open(p, "a")
            fcntl.flock(self.f, fcntl.LOCK_EX)
        except (ImportError, OSError):
            self.f = None
        return self

    def __exit__(self, *a):
        if self.f:
            self.f.close()


def run_tool(tool, args, hide=None, extend=None):
    env = dict(os.environ)
    env.pop("ICO_RD_CONTENT_HIDE", None)
    env.pop("ICO_RD_CONTENT_EXTEND", None)
    if hide:
        env["ICO_RD_CONTENT_HIDE"] = hide
    if extend:
        env["ICO_RD_CONTENT_EXTEND"] = extend
    r = subprocess.run([tool] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       env=env)
    return r.returncode, r.stdout


class DumpInfo:
    def __init__(self, path, version, gs_w, aspect, lines):
        self.path, self.version, self.gs_w, self.aspect, self.lines = path, version, gs_w, aspect, lines

    def draws_of(self, mesh):
        """the list:index of every draw of a mesh whose name holds `mesh`"""
        pat = re.compile(r"mesh (\S*%s\S*) \(" % re.escape(mesh))
        return [ln.split()[0] for ln in self.lines if "MESH" in ln or "SKINNED" in ln
                if pat.search(ln)]


def dump_info(tool, path):
    rc, out = run_tool(tool, [path, os.devnull, "--list", "--no-device"])
    m = re.search(r"dump: version (\d+), GS (\d+)x(\d+), aspect ([0-9.]+)", out)
    if rc != 0 or not m:
        say("%s: --list failed:\n%s" % (path, out[-2000:]))
        return None
    return DumpInfo(path, int(m.group(1)), int(m.group(2)), float(m.group(4)), out.splitlines())


def sweep_members(src, tmp):
    """[(aspect token, dump path)] of a source: an .rddump or F12 zip is one
    member (its own aspect, found later); a sweep zip (members named
    4x3.rddump ...) or a folder of them one per aspect."""
    names = []
    if os.path.isdir(src):
        names = [(f, os.path.join(src, f)) for f in sorted(os.listdir(src))]
    elif src.endswith(".zip"):
        z = zipfile.ZipFile(src)
        rd = [n for n in z.namelist() if n.endswith(".rddump")]
        if len(rd) > 1:
            d = tempfile.mkdtemp(dir=tmp)
            for n in rd:
                z.extract(n, d)
            names = [(n, os.path.join(d, n)) for n in rd]
        else:
            return [(None, src)]
    else:
        return [(None, src)]
    out = []
    for n, p in names:
        for a in SWEEP_ASPECTS:
            if re.search(r"(?<![0-9])%s(?![0-9])" % a, os.path.basename(n)) and p.endswith(".rddump"):
                out.append((a, p))
    return out


def aspect_token(value):
    best = min(ASPECT_VALUE, key=lambda a: abs(ASPECT_VALUE[a] - value))
    return best if abs(ASPECT_VALUE[best] - value) < 0.01 else "%.3f" % value


# ------------------------------------------------------------------ decide

def cmd_decide(args, dumps):
    table = read_table()
    if not table:
        say("the table is empty: nothing to decide")
        return decide_sheets(args, dumps)
    os.makedirs(args.out, exist_ok=True)
    tmp = tempfile.mkdtemp(dir=args.out)
    evidence = {i: [] for i, _, _, _ in table}  # (dump, setting, same hide/off)
    fails = 0
    for src in dumps:
        for tok, path in sweep_members(src, tmp):
            if tok not in (None, "4x3"):
                continue
            info = dump_info(args.tool, path)
            if not info:
                fails += 1
                continue
            if aspect_token(info.aspect) != "4x3":
                continue  # a wide frame: the 4:3 picture is a sweep's 4x3 member
            for i, mesh, _, _ in table:
                if not info.draws_of(mesh):
                    continue
                for setting, sargs in DECIDE_SETTINGS.items():
                    dirs = {}
                    for verb in ("off", "hide"):
                        d = os.path.join(tmp, "%d-%s-%s-%s" % (i, os.path.basename(path), setting, verb))
                        os.makedirs(d, exist_ok=True)
                        rc, out = run_tool(args.tool, [path, "-", "--pass-dump", d] + sargs,
                                           "%d=%s" % (i, verb))
                        if rc != 0:
                            say("%s %s %s: replay failed:\n%s" % (path, setting, verb, out[-2000:]))
                            fails += 1
                        dirs[verb] = d
                    pngs = sorted(f for f in os.listdir(dirs["off"]) if f.endswith(".png"))
                    diffh = [f for f in pngs if not same_png(os.path.join(dirs["off"], f),
                                                             os.path.join(dirs["hide"], f))]
                    evidence[i].append((os.path.basename(path), setting, not diffh, diffh))
    shutil.rmtree(tmp, ignore_errors=True)
    for i, mesh, note, verb in table:
        ev = evidence[i]
        if not ev:
            say("entry %d %s (%s): no 4:3 dump draws it; no evidence" % (i, verb, mesh))
            continue
        shows = [e for e in ev if not e[2]]
        for d, setting, same, diff in ev:
            say("entry %d (%s) %s %s: HIDE %s" %
                (i, mesh, d, setting, "is the same picture" if same else "changes " + " ".join(diff)))
        if shows:
            say("entry %d (%s): FAIL: its faces show in the 4:3 picture; leave the mesh unlisted "
                "(drawn as geometry)" % (i, mesh))
            fails += 1
        else:
            say("entry %d (%s): HIDE (never in the 4:3 picture)" % (i, mesh))
    # both tables decided, the sheets also when a hide entry fails
    sheet_fails = decide_sheets(args, dumps)
    return 1 if (fails or sheet_fails) else 0


def band_rows(img, x0, x1):
    """the rows of an RGB image cut to columns x0..x1 - 1"""
    return [row[x0 * 3:x1 * 3] for row in img[2]]


def sheet_effect(nov, img, cols):
    """(fraction of pixels changed, mean change per channel) of img against
    nov over the given columns"""
    changed = total = 0
    acc = 0
    for rn, ri in zip(nov[2], img[2]):
        for x in cols:
            a, b = rn[x * 3:x * 3 + 3], ri[x * 3:x * 3 + 3]
            total += 1
            if a != b:
                changed += 1
                acc += (b[0] - a[0]) + (b[1] - a[1]) + (b[2] - a[2])
    return (changed / total if total else 0.0), (acc / (3.0 * total) if total else 0.0)


def decide_sheets(args, dumps):
    """the sheets (rd_content_extend.c): see the module comment's decide"""
    table = read_extend_table()
    groups = {}
    for i, mesh, _, _ in table:
        groups.setdefault(mesh, []).append(i)
    tmp = tempfile.mkdtemp(dir=args.out)
    fails = 0
    seen = {mesh: 0 for mesh in groups}
    for src in dumps:
        for tok, path in sweep_members(src, tmp):
            info = dump_info(args.tool, path)
            if not info:
                fails += 1
                continue
            for mesh, idx in groups.items():
                nops = info.draws_of(mesh)
                if not nops:
                    continue
                seen[mesh] += 1
                spec = {v: ",".join("%d=%s" % (i, v) for i in idx) for v in ("off", "tiles")}
                wide = aspect_token(info.aspect) != "4x3"
                for setting, sargs in DECIDE_SETTINGS.items():
                    dirs = {}
                    for verb in ("off", "tiles"):
                        d = os.path.join(tmp, "x-%s-%s-%s" % (os.path.basename(path), setting, verb))
                        os.makedirs(d, exist_ok=True)
                        rc, out = run_tool(args.tool, [path, "-", "--pass-dump", d] + sargs,
                                           extend=spec[verb])
                        if rc != 0:
                            say("%s %s %s: replay failed:\n%s" % (path, setting, verb, out[-2000:]))
                            fails += 1
                        dirs[verb] = d
                    diff = []
                    for f in sorted(f for f in os.listdir(dirs["off"]) if f.endswith(".png")):
                        a, b = os.path.join(dirs["off"], f), os.path.join(dirs["tiles"], f)
                        if same_png(a, b):
                            continue
                        if f == "PRESENT.png" or not wide:
                            diff.append(f)
                            continue
                        ia, ib = load_png(a), load_png(b)
                        s = ia[0] / info.gs_w  # the target's width over the GS width
                        x0 = int((info.gs_w // 2 - 256) * s)
                        x1 = int((info.gs_w // 2 + 256) * s)
                        if ia[0] != ib[0] or band_rows(ia, x0, x1) != band_rows(ib, x0, x1):
                            diff.append(f)
                    say("sheets %s %s %s: TILES %s at 4:3" %
                        (mesh, os.path.basename(path), setting,
                         "the same picture" if not diff else "changes " + " ".join(diff)))
                    if diff:
                        fails += 1
                if not wide:
                    continue
                pics = {}
                for name, nop, verb in (("none", nops, "off"), ("off", [], "off"),
                                        ("tiles", [], "tiles")):
                    out = os.path.join(tmp, "s-%s-%s.png" % (os.path.basename(path), name))
                    a = [path, out, "--target", "SCENE", "--resolution", "1x"]
                    for n in nop:
                        a += ["--nop", n]
                    rc, text = run_tool(args.tool, a, extend=spec[verb])
                    if rc != 0 or not os.path.exists(out):
                        say("%s: replay failed (%s):\n%s" % (path, " ".join(a[2:]), text[-2000:]))
                        fails += 1
                        break
                    pics[name] = load_png(out)
                if len(pics) != 3:
                    continue
                w = pics["none"][0]
                band = list(range(w // 2 - 256, w // 2 + 256))
                touched = [x for x in range(w)
                           if any(rn[x * 3:x * 3 + 3] != ro[x * 3:x * 3 + 3]
                                  for rn, ro in zip(pics["none"][2], pics["off"][2]))]
                touched = set(touched)
                sides = [x for x in range(w) if x not in touched]
                fb, mb = sheet_effect(pics["none"], pics["tiles"], band)
                fs, ms = sheet_effect(pics["none"], pics["tiles"], sides)
                say("sheets %s %s at %s: the 4:3 band %.1f %% changed, mean %+.2f; the %d side "
                    "columns %.1f %% changed, mean %+.2f" %
                    (mesh, os.path.basename(path), aspect_token(info.aspect), 100 * fb, mb,
                     len(sides), 100 * fs, ms))
                if sides and (fs < 0.9 or abs(ms - mb) > 1.5):
                    say("sheets %s %s: FAIL: the sides do not show the sheet as the middle does" %
                        (mesh, os.path.basename(path)))
                    fails += 1
    shutil.rmtree(tmp, ignore_errors=True)
    for mesh, n in seen.items():
        if not n:
            say("sheets %s: no dump draws it; no evidence" % mesh)
    return 1 if fails else 0


# ------------------------------------------------------------------ report

def scene(args, path, extra, nops, out):
    a = [path, out, "--target", "SCENE", "--resolution", "1x"] + extra
    for n in nops:
        a += ["--nop", n]
    rc, text = run_tool(args.tool, a)
    if rc != 0 or not os.path.exists(out):
        say("%s: replay failed (%s):\n%s" % (path, " ".join(a[2:]), text[-2000:]))
        return None
    return load_png(out)


def report_one(args, info, tok, mesh, rdir):
    nops = info.draws_of(mesh)
    if not nops:
        return None
    base = "%s-%s-%s" % (os.path.splitext(os.path.basename(info.path))[0], tok, mesh)
    imgs = {}
    for key, extra, n in (("E", ["--enhanced"], []), ("En", ["--enhanced"], nops),
                          ("O", [], []), ("On", [], nops)):
        imgs[key] = scene(args, info.path, extra, n, os.path.join(rdir, base + "-" + key + ".png"))
        if not imgs[key]:
            return "replay failed"
    w, h, e = imgs["E"]
    if any(imgs[k][0] != w or imgs[k][1] != h for k in imgs):
        return "the four pictures differ in size"
    en, o, on = imgs["En"][2], imgs["O"][2], imgs["On"][2]
    s = w / float(info.gs_w)
    c0, c1 = int(w / 2 - 256 * s), int(w / 2 + 256 * s + 0.999)
    n_e = n_o = n_in = n_out = 0
    box = [w, h, -1, -1]
    acc = [0, 0, 0]
    hl = []
    for y in range(h):
        re_, rn, ro, rp = e[y], en[y], o[y], on[y]
        row = bytearray(re_)
        for x in range(w):
            i = 3 * x
            add_e = re_[i:i + 3] != rn[i:i + 3]
            add_o = ro[i:i + 3] != rp[i:i + 3]
            n_e += add_e
            n_o += add_o
            if add_e and not add_o:
                if c0 <= x < c1:
                    n_in += 1
                else:
                    n_out += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
                for k in range(3):
                    acc[k] += re_[i + k]
                row[i:i + 3] = b"\xff\x00\xff"
        hl.append(bytes(row))
    clip = n_in + n_out
    for k in ("E", "En", "O", "On"):
        os.remove(os.path.join(rdir, base + "-" + k + ".png"))
    msg = ("%d draws; Enhanced adds %d pixels, Original %d; drawn only clipped: %d (%d in the 4:3 "
           "columns %d..%d, %d beside)" % (len(nops), n_e, n_o, clip, n_in, c0, c1 - 1, n_out))
    if clip:
        save_png(os.path.join(rdir, base + "-clipped.png"), w, h, hl)
        msg += ", box (%d,%d)-(%d,%d), mean colour %d,%d,%d -> %s-clipped.png" % (
            box[0], box[1], box[2], box[3], acc[0] // clip, acc[1] // clip, acc[2] // clip, base)
    return msg


def cmd_report(args, groups):
    rdir = os.path.join(args.out, "report")
    os.makedirs(rdir, exist_ok=True)
    tmp = tempfile.mkdtemp(dir=args.out)
    ran = 0
    for meshes, sources in groups:
        for src in sources:
            members = sweep_members(src, tmp)
            seen = set()
            for tok, path in members:
                info = dump_info(args.tool, path)
                if not info:
                    continue
                tok = aspect_token(info.aspect) if tok is None else tok
                seen.add(tok)
                for mesh in meshes:
                    msg = report_one(args, info, tok, mesh, rdir)
                    ran += msg is not None
                    say("%s %s %s: %s" % (os.path.basename(src), tok, mesh,
                                          msg if msg else "not drawn in this frame"))
            missing = [a for a in REPORT_ASPECTS if a not in seen]
            if missing:
                say("%s: no frame at %s: capture an aspect sweep (Shift+F12 in photo mode) of "
                    "this state%s" % (os.path.basename(src), ", ".join(missing),
                                      " (a dump before v10 is 4:3 only)" if seen == {"4x3"} else ""))
    shutil.rmtree(tmp, ignore_errors=True)
    say("report: %d mesh pictures in %s" % (ran, rdir))
    return 0


# -------------------------------------------------------------------- main

# ------------------------------------------------------------------ chains

CHAIN_TABLE = os.path.join(ROOT, "port", "render", "rd_content_chain.c")
REGIONS = os.path.join(ROOT, "config", "regions.txt")
# the chains table's rows in order, by the region.h field holding each one's
# group on a disc (rd_content_chain.c chainGroup)
CHAIN_GROUP_FIELDS = ["chain_group_st10l", "chain_group_st19a"]


def table_desc(slug):
    return os.path.join(ROOT, "port", "data", "gen", slug, "table_desc.c")


def region_row(slug):
    """(disc id, boot ELF) of a row of config/regions.txt."""
    for line in open(REGIONS, encoding="utf-8"):
        f = line.split("#", 1)[0].split()
        if len(f) >= 4 and f[0] == slug:
            return f[2], f[3]
    raise SystemExit("content_table_check: no row %s in %s" % (slug, REGIONS))


DISCS_C = os.path.join(ROOT, "port", "data", "discs.c")


def disc_df_shift(slug):
    """IcoDiscInfo.df_lsn_shift of the row of port/data/discs.c whose slug this is: the
    sectors DATA.DF's directory LSN is short of the archive (the Korean disc, 153600);
    0 for a row without one. Read from the row's initializer, the field after the
    directory CRC (archive.h)."""
    src = open(DISCS_C, encoding="utf-8").read()
    m = re.search(r'\[ICO_REGION_%s\]\s*=\s*\{(.*?)\},' % slug.upper(), src, re.S)
    if not m:
        raise SystemExit("content_table_check: no ico_discs row for %s in %s" % (slug, DISCS_C))
    f = re.search(r"_DIR_CRC,\s*(\d+)u?\s*,", m.group(1))
    return int(f.group(1)) if f else 0


def region_groups(slug):
    """The chain groups port/core/region_SLUG.c gives, in the table's order."""
    path = os.path.join(ROOT, "port", "core", "region_%s.c" % slug)
    src = open(path, encoding="utf-8").read()
    out = []
    for f in CHAIN_GROUP_FIELDS:
        m = re.search(r"\.%s\s*=\s*(\d+)" % f, src)
        if not m:
            raise SystemExit("content_table_check: no .%s in %s" % (f, path))
        out.append(int(m.group(1)))
    return out


def read_chains(path=CHAIN_TABLE):
    """[{bga, group, null, axis, anchor, end, reach, fall}] in the table's order."""
    src = open(path, encoding="utf-8").read()
    body = src[src.index("rd__chains[] = {"):]
    body = body[:body.index("};")]
    num = r"\s*(-?[\d.]+)f?\s*"
    out = []
    for m in re.finditer(r'\{"([^"]+)",' + ",".join([num] * 7) + r"\}", body):
        v = [float(x) for x in m.groups()[1:]]
        out.append(dict(bga=m.group(1), group=int(v[0]), null=int(v[1]), axis=int(v[2]),
                        anchor=v[3], end=v[4], reach=v[5], fall=v[6]))
    return out


def table_range(name, slug="pal"):
    """(EE address, record count) of a table in port/data/gen/SLUG/table_desc.c."""
    path = table_desc(slug)
    src = open(path, encoding="utf-8").read()
    m = re.search(r'\{"[^"]*",\s*"%s",\s*\w+,\s*(0x[0-9A-Fa-f]+)u,\s*0x[0-9A-Fa-f]+u,\s*'
                  r'0x[0-9A-Fa-f]+u,\s*\d+,\s*(\d+)' % name, src)
    if not m:
        raise SystemExit("content_table_check: %s not in %s" % (name, path))
    return int(m.group(1), 16), int(m.group(2))


class Disc:
    """A disc image (ISO 9660): its files, DATA.DF's packs, the boot ELF."""

    def __init__(self, path, boot_elf="SCES_507.60", df_shift=0):
        self.boot_elf = boot_elf
        self.f = open(path, "rb")
        pvd = self.sectors(16, 1)
        if pvd[1:6] != b"CD001":
            raise ValueError("%s: not an ISO 9660 image" % path)
        self.root = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
        lsn, size = self.find("DFDATAS/DATA.DF")
        # the Korean disc's archive lies df_shift sectors past the directory's LSN
        # (IcoDiscInfo.df_lsn_shift, port/data/archive.h); the span shrinks by as much
        if size <= df_shift * 2048:
            raise ValueError("%s: DATA.DF (%d bytes) is not longer than its shift" % (path, size))
        self.df = lsn + df_shift, size - df_shift * 2048
        n = struct.unpack("<I", self.read_df(0, 4))[0]
        raw = self.read_df(4, n * 40)
        self.entries = [(raw[i * 40:i * 40 + 32].split(b"\0")[0].decode("latin-1"),
                         struct.unpack_from("<II", raw, i * 40 + 32)) for i in range(n)]

    def sectors(self, lsn, n):
        self.f.seek(lsn * 2048)
        return self.f.read(n * 2048)

    def find(self, path):
        lsn, size = self.root
        for comp in path.split("/"):
            d = self.sectors(lsn, (size + 2047) // 2048)
            at, hit = 0, None
            while at < len(d):
                ln = d[at]
                if ln == 0:
                    at = (at // 2048 + 1) * 2048
                    continue
                name = d[at + 33:at + 33 + d[at + 32]].decode("latin-1").split(";")[0]
                if name.upper() == comp.upper():
                    hit = struct.unpack_from("<I", d, at + 2)[0], struct.unpack_from("<I", d, at + 10)[0]
                    break
                at += ln
            if hit is None:
                raise ValueError("%s: no %s on the disc" % (path, comp))
            lsn, size = hit
        return lsn, size

    def read_df(self, off, n):
        self.f.seek(self.df[0] * 2048 + off)
        return self.f.read(n)

    def packs(self):
        """(member name, id, bytes) of every member of every pack, pack by pack."""
        for name, (off, size) in self.entries:
            if not name.upper().endswith(".DF"):
                continue
            data = zlib.decompressobj(-15).decompress(self.read_df(off, size))
            n = struct.unpack_from("<I", data, 0)[0]
            at = 16 + n * 0x224
            for k in range(n):
                e = data[16 + k * 0x224:16 + (k + 1) * 0x224]
                mid, _kind, _w, sz = struct.unpack_from("<IIII", e, 0)
                yield e[16:].split(b"\0")[0].decode("latin-1"), mid, data[at:at + sz]
                at += sz

    def elf(self):
        lsn, size = self.find(self.boot_elf)
        return self.sectors(lsn, (size + 2047) // 2048)[:size]


def elf_reader(elf):
    phoff, = struct.unpack_from("<I", elf, 0x1C)
    phsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    segs = []
    for i in range(phnum):
        kind, off, va, _pa, fsz = struct.unpack_from("<IIIII", elf, phoff + i * phsize)
        if kind == 1:
            segs.append((va, off, fsz))

    def read(va, n):
        for s0, off, fsz in segs:
            if s0 <= va and va + n <= s0 + fsz:
                return elf[off + va - s0:off + va - s0 + n]
        raise ValueError("EE address %#x not in the ELF" % va)
    return read


def bga_nodes(b):
    """The nodes of a BGA file: {num, type, name, parent, keys: [(pos, rot, scale)]}."""
    if b[:3] != b"BGA":
        return []
    off = struct.unpack_from("<I", b, 0x0C)[0]
    nodes = []
    while off and off + 0x48 <= len(b):
        kind, num = struct.unpack_from("<HH", b, off)
        name = b[off + 4:off + 36].split(b"\0")[0].decode("latin-1")
        nxt = struct.unpack_from("<I", b, off + 0x24)[0]
        key, n = struct.unpack_from("<Ii", b, off + 0x34)
        parent = struct.unpack_from("<h", b, off + 0x44)[0]
        keys = []
        if kind != 13 and 0 <= n < 4096 and key + n * 0x34 <= len(b):
            for k in range(n):
                v = struct.unpack_from("<9f", b, key + k * 0x34)
                keys.append((v[0:3], v[3:6], v[6:9]))
        nodes.append(dict(num=num, type=kind, name=name, parent=parent, keys=keys))
        off = nxt
    return nodes


def chain_links(nodes, null, axis):
    """The links under node null when they are a chain along axis: one model,
    three or more, each still (one place over its keys), unturned, unscaled,
    the same off-axis place, evenly spaced along the axis; else None."""
    kids = [d for d in nodes if d["parent"] == null]
    if len(kids) < 3 or any(d["type"] != 0 or not d["keys"] for d in kids):
        return None
    if len(set(d["name"] for d in kids)) != 1:
        return None
    places = []
    for d in kids:
        p0 = d["keys"][0][0]
        for pos, rot, sc in d["keys"]:
            if pos != p0 or any(abs(r) > 1e-6 for r in rot) or any(abs(x - 1) > 1e-6 for x in sc):
                return None
        places.append(p0)
    off = [tuple(p[i] for i in range(3) if i != axis) for p in places]
    if len(set(off)) != 1:
        return None
    along = sorted(p[axis] for p in places)
    step = along[1] - along[0]
    if step <= 0 or any(abs(along[i + 1] - along[i] - step) > 1e-3 for i in range(len(along) - 1)):
        return None
    return kids[0]["name"], along


def scaled_axes(node):
    """The axes on which a node's keyed scale changes."""
    return [a for a in range(3) if len(set(k[2][a] for k in node["keys"])) > 1]


def p2o_reach(b, axis, toward_minus):
    """How far a model's vertices reach past its origin on axis, toward minus or plus."""
    tbl, n = struct.unpack_from("<iI", b, 4)
    lo, hi = 0.0, 0.0
    for i in range(n):
        part = struct.unpack_from("<i", b, tbl + 4 * i)[0]
        vtx, cnt = struct.unpack_from("<iI", b, part + 0x90)
        for k in range(cnt):
            c = struct.unpack_from("<f", b, vtx + 16 * k + 4 * axis)[0]
            lo, hi = min(lo, c), max(hi, c)
    return -lo if toward_minus else hi


def cmd_chains(args):
    slug = args.version
    disc_id, boot_elf = region_row(slug)
    env = "ICO_DISC_IMAGE" if slug == "pal" else "ICO_DISC_IMAGE_%s" % slug.upper()
    path = args.disc or os.environ.get(env) or os.path.join(ROOT, "baserom", "Ico_%s.iso" % slug.upper())
    if not path.lower().endswith(".iso") or not os.path.exists(path):
        say("SKIP chains: no disc image (.iso) at %s" % path)
        return EXIT_SKIP
    disc = Disc(path, boot_elf, disc_df_shift(slug))
    read = elf_reader(disc.elf())
    base, count = table_range("stageTable", slug)
    rows = []
    for i in range(count):
        r = read(base + i * 0x5C, 0x5C)
        rows.append((r[:64].split(b"\0")[0].decode("latin-1"), struct.unpack_from("<i", r, 0x48)[0]))
    say("chains: %s (%s, %s), stageTable %d rows from %s" % (disc_id, slug, boot_elf, count,
                                                            os.path.relpath(table_desc(slug), ROOT)))
    table = read_chains()
    rc = 0
    if not table:
        say("FAIL chains: no entry read from %s" % CHAIN_TABLE)
        return 1
    groups = region_groups(slug)
    if len(groups) != len(table):
        say("FAIL chains: %d rows in the table, %d groups in region_%s.c" % (len(table), len(groups), slug))
        return 1
    for e, g in zip(table, groups):
        e["group"] = g
    bgas, models, found, members = {}, {}, set(), set()
    for name, _mid, data in disc.packs():
        members.add(name)
        if name.endswith(".bga") and name not in bgas:
            bgas[name] = data
            nodes = bga_nodes(data)
            for d in nodes:
                if d["type"] != 1 or len(d["keys"]) < 2:
                    continue
                ax = scaled_axes(d)
                if len(ax) == 1 and chain_links(nodes, d["num"], ax[0]):
                    found.add((name.rsplit("/", 1)[-1][:-4], d["num"]))
        elif name.endswith(".p2o"):
            models.setdefault(name.rsplit("/", 1)[-1][:-4], data)
    # a report, not a gate: both retail discs keep rows whose files they do
    # not hold (the development stages' and unused cameras': 164 distinct on
    # the European disc, 145 on the Japanese one, every one of those also
    # missing from the European disc)
    named = [p for p, _g in rows if p and p != "NULL"]
    missing = sorted(set(p for p in named if p not in members))
    say("chains: stageTable names %d stage animation files (%d distinct): %d in the disc's packs, "
        "%d not (--missing lists them)" % (len(named), len(set(named)), len(set(named)) - len(missing),
                                          len(missing)))
    for p in missing if args.missing else []:
        say("chains: not on the disc: %s" % p)
    for e in table:
        where = "chains: %s" % e["bga"]
        hits = [(p, g) for p, g in rows if p.endswith("/%s.bga" % e["bga"])]
        same = [p for p, g in rows if g == e["group"] and p != "NULL"]
        if len(hits) != 1 or hits[0][1] != e["group"] or len(same) != 1:
            say("FAIL %s: the stage animation rows %s, the rows of group %d %s" % (where, hits, e["group"], same))
            rc = 1
            continue
        nodes = bga_nodes(bgas.get(hits[0][0], b""))
        null = [d for d in nodes if d["num"] == e["null"]]
        if len(null) != 1 or null[0]["type"] != 1:
            say("FAIL %s: no Null (type 1) numbered %d" % (where, e["null"]))
            rc = 1
            continue
        sc = [k[2][e["axis"]] for k in null[0]["keys"]]
        if scaled_axes(null[0]) != [e["axis"]] or not (min(sc) < 1.0 and abs(max(sc) - 1.0) < 1e-6):
            say("FAIL %s: the Null's keyed scale (%s) does not lengthen the chain on axis %d"
                % (where, sorted(set(sc)), e["axis"]))
            rc = 1
            continue
        links = chain_links(nodes, e["null"], e["axis"])
        lo, hi = min(e["anchor"], e["end"]), max(e["anchor"], e["end"])
        if not links or abs(links[1][0] - lo) > 1e-3 or abs(links[1][-1] - hi) > 1e-3:
            say("FAIL %s: the links under the Null are not a chain from %g to %g (%s)"
                % (where, e["anchor"], e["end"], links and (links[0], links[1][0], links[1][-1])))
            rc = 1
            continue
        model = models.get(links[0])
        down = e["end"] >= e["anchor"]
        reach = p2o_reach(model, e["axis"], down) if model else None
        fall = p2o_reach(model, e["axis"], not down) if model else None
        if reach is None or abs(reach - e["reach"]) > 0.01 or abs(fall - e["fall"]) > 0.01:
            say("FAIL %s: %s reaches %s past a node toward the anchor and %s away, the table %g "
                "and %g" % (where, links[0], reach, fall, e["reach"], e["fall"]))
            rc = 1
            continue
        say("%s: group %d, Null %d scaled %g..%g on axis %d, %d links of %s from %g to %g, "
            "reach %.2f and %.2f: ok" % (where, e["group"], e["null"], min(sc), max(sc), e["axis"],
                                         len(links[1]), links[0], e["anchor"], e["end"], reach,
                                         fall))
    listed = set((e["bga"], e["null"]) for e in table)
    for extra in sorted(found - listed):
        say("FAIL chains: %s Null %d lets a chain down by scaling and is not listed" % extra)
        rc = 1
    for missing in sorted(listed - found):
        say("FAIL chains: %s Null %d is listed but the scan does not find it" % missing)
        rc = 1
    say("chains: %d stage animations scanned, %d chains found, %d listed"
        % (len(bgas), len(found), len(table)))
    return rc


def read_list(path):
    if os.path.isdir(path):
        return sorted(os.path.join(path, f) for f in os.listdir(path)
                      if f.endswith((".rddump", ".zip")))
    if path.endswith((".rddump", ".zip")):
        return [path]
    import glob
    base = os.path.dirname(os.path.abspath(path))
    out = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        p = line if os.path.isabs(line) else os.path.join(base, line)
        out.extend(sorted(glob.glob(p)) if ("*" in p or "?" in p) else [p])
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--tool", default=os.environ.get("ICO_REPLAY_TOOL", DEFAULT_TOOL))
    p.add_argument("--out", default=os.path.join(ROOT, "build-host", "tmp", "content_table_check"))
    p.add_argument("--dumps", default=os.environ.get("ICO_DUMPS") or DEFAULT_LIST)
    p.add_argument("--mesh", action="append", help="report: a mesh name (repeatable)")
    p.add_argument("--disc", help="chains: the disc image (.iso)")
    p.add_argument("--version", default="pal",
                   help="chains: the disc, a row of config/regions.txt (pal, jpn)")
    p.add_argument("--missing", action="store_true",
                   help="chains: list the stageTable files the disc's packs do not hold")
    p.add_argument("mode", nargs="?", default="all", choices=["decide", "report", "all", "chains"])
    p.add_argument("sources", nargs="*", help="report: dumps, F12 zips or aspect sweep zips")
    args = p.parse_args()
    if args.mode == "chains":
        rc = cmd_chains(args)
        if rc != EXIT_SKIP:
            say("ok" if rc == 0 else "FAILED")
        return rc
    if not os.path.exists(args.tool):
        say("no replay tool at %s (build rd_replay_tool, or --tool)" % args.tool)
        return 2
    dumps = [d for d in read_list(args.dumps) if os.path.exists(d)]
    if args.mesh or args.sources:
        groups = [(args.mesh or REPORT_MESHES["opening"] + REPORT_MESHES["waterfall"],
                   args.sources or dumps)]
    else:
        opening = [d for d in dumps if any(re.search(r"-%s\.(rddump|zip)$" % t, d) for t in OPENING_DUMPS)]
        water = [d for d in dumps if any(t in d for t in WATERFALL_DUMPS)]
        groups = [(REPORT_MESHES["opening"], opening), (REPORT_MESHES["waterfall"], water)]
    if not dumps and not args.sources:
        say("SKIP: no dump of %s exists" % args.dumps)
        return EXIT_SKIP
    rc = 0
    with Lock():
        if args.mode in ("decide", "all"):
            rc |= cmd_decide(args, dumps)
        if args.mode in ("report", "all"):
            rc |= cmd_report(args, groups)
    say("ok" if rc == 0 else "FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main())
