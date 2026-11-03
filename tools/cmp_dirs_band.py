#!/usr/bin/env python3
"""tools/cmp_dirs_band.py: the region rule's side bands (issue 29;
port/shaders/vu_common.hlsli VU_RULE_CLIP_BESIDE, docs/port/VU1_PROGRAMS.md
"The region rule") as two folders of PNGs show them.

  python3 -I tools/cmp_dirs_band.py A B --gs-width W [--band-files F,..]
                                    [--clear RRGGBB[AA],..] [--fog-from-a]
                                    [--margin N]

A holds the targets of a replay that keeps the PS2's drop on every VU draw
(ICO_RD_VU_BESIDE=0), B the same replay with the side bands (rd_replay_tool
--pass-dump, the same names).  W is the frame's width in GS pixels (the
replay's "frame W GS wide" line).  The camera's picture is the centre 512
GS pixels of the frame (GS 2048 -+ 256), so on an image whose width is a
whole multiple S of W (the scene's scale) the band is the centre 512 * S
columns.  For such an image named in --band-files (every one when not
given):

  - the band identical byte for byte (hard: a triangle beside the picture
    never draws a pixel inside it; the pixel shaders discard there);
  - the side columns reported: the pixels B changes, the largest channel
    difference and their bounding box;
  - never lose: every side pixel B changes is one A left at the clear or
    fog colour (a hole the PS2's drop left, now filled); any other changed
    side pixel is a loss (hard).  The colours are --clear's (RGB, or RGBA
    when eight digits), and with --fog-from-a also A's most common colour
    in that image's side columns.  Without either, no side change is held
    to it (reported only).

--margin N leaves N columns at each inner edge of the band out of the hard
rule (reported apart: a post-effect that reads across the band's edge).
Every other image (another width, not named, or a frame no wider than the
picture: no side columns, the whole image is the band) is compared byte for
byte and only reported when not in --band-files' set; a named one must be
identical.  A name in A missing from B, or a size change, fails.

One line an image and a total line; exit 0 when every hard rule holds, 1
otherwise, 2 on a usage error.  Standard library only (the PNG reader is
tools/content_table_check.py's, kept with its alpha)."""
import os
import struct
import sys
import zlib


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


def parse_colours(text):
    out = []
    for c in text.split(","):
        c = c.strip().lstrip("#")
        if not c:
            continue
        if len(c) not in (6, 8):
            raise ValueError("--clear: %s is not RRGGBB or RRGGBBAA" % c)
        out.append(bytes.fromhex(c))
    return out


def pixel(rows, bpp, x, y):
    return rows[y][x * bpp:(x + 1) * bpp]


def compare(name, pa, pb, gsw, banded, clears, fog, margin):
    """(hard ok, line)"""
    da, db = open(pa, "rb").read(), open(pb, "rb").read()
    if da == db:
        return True, "%-18s identical (files equal)" % name
    try:
        wa, ha, ba, ra = load_png(pa)
        wb, hb, bb, rb = load_png(pb)
    except ValueError as e:
        return not banded, "%-18s %s" % (name, e)
    if (wa, ha, ba) != (wb, hb, bb):
        return False, "%-18s size %dx%dx%d vs %dx%dx%d" % (name, wa, ha, ba, wb, hb, bb)
    if ra == rb:
        return True, "%-18s identical (%dx%d)" % (name, wa, ha)
    scale = wa // gsw if gsw and wa % gsw == 0 else 0
    wide = scale >= 1 and gsw > 512
    if not banded or not wide:
        n = sum(1 for y in range(ha) if ra[y] != rb[y])
        what = "differs" if banded else "differs (not band-checked)"
        if banded:
            what = "DIFFERS (no side columns: the whole image is the band)"
        return not banded, "%-18s %s: %d of %d rows (%dx%d)" % (name, what, n, ha, wa, ha)
    bl = (wa - 512 * scale) // 2
    br = bl + 512 * scale  # one past the band's last column
    band = edge = side = lost = 0
    maxd = 0
    bbox = [wa, ha, -1, -1]
    colours = list(clears)
    if fog:
        count = {}
        for y in range(ha):
            row = ra[y]
            for x in list(range(0, bl)) + list(range(br, wa)):
                p = row[x * ba:(x + 1) * ba]
                count[p] = count.get(p, 0) + 1
        if count:
            colours.append(max(count, key=count.get))
    firstLoss = None
    for y in range(ha):
        a, b = ra[y], rb[y]
        if a == b:
            continue
        for x in range(wa):
            o = x * ba
            if a[o:o + ba] == b[o:o + ba]:
                continue
            if bl <= x < br:
                if x < bl + margin or x >= br - margin:
                    edge += 1
                else:
                    band += 1
                continue
            side += 1
            maxd = max(maxd, max(abs(a[o + i] - b[o + i]) for i in range(ba)))
            bbox = [min(bbox[0], x), min(bbox[1], y), max(bbox[2], x), max(bbox[3], y)]
            if colours:
                pa_ = a[o:o + ba]
                if not any(pa_[:len(c)] == c for c in colours):
                    lost += 1
                    if firstLoss is None:
                        firstLoss = (x, y, pa_.hex(), b[o:o + ba].hex())
    ok = band == 0 and lost == 0
    text = "%-18s %s: band %d..%d of %d (scale %d): %d band pixels differ" % (
        name, "ok" if ok else "FAIL", bl, br - 1, wa, scale, band)
    if margin:
        text += " (%d within %d of its edges, not held)" % (edge, margin)
    if side:
        text += "; side %d pixels changed, max |d| %d, bbox (%d,%d)-(%d,%d)" % (
            side, maxd, bbox[0], bbox[1], bbox[2], bbox[3])
        if colours:
            text += ", %d not over the clear/fog colour (%s)" % (
                lost, ",".join(c.hex() for c in colours))
            if firstLoss:
                text += ", first at (%d,%d): A %s B %s" % firstLoss
        else:
            text += " (no clear colour given: not held)"
    else:
        text += "; side unchanged"
    return ok, text


def main(argv):
    args, opts = [], {"--gs-width": "0", "--band-files": "", "--clear": "", "--margin": "0"}
    fog = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in opts and i + 1 < len(argv):
            opts[a] = argv[i + 1]
            i += 2
        elif a == "--fog-from-a":
            fog = True
            i += 1
        elif a.startswith("--"):
            print(__doc__)
            return 2
        else:
            args.append(a)
            i += 1
    if len(args) != 2 or not os.path.isdir(args[0]) or not os.path.isdir(args[1]):
        print(__doc__)
        return 2
    try:
        gsw, margin = int(opts["--gs-width"]), int(opts["--margin"])
        clears = parse_colours(opts["--clear"])
    except ValueError as e:
        print("cmp_dirs_band: %s" % e)
        return 2
    named = {n.strip() for n in opts["--band-files"].split(",") if n.strip()}
    a, b = args
    names = sorted(n for n in os.listdir(a) if n.endswith(".png"))
    if not names:
        print("cmp_dirs_band: no PNG in %s" % a)
        return 1
    fails = held = 0
    for n in names:
        pb = os.path.join(b, n)
        if not os.path.exists(pb):
            print("%-18s only in %s" % (n, a))
            fails += 1
            continue
        banded = not named or n in named
        held += banded
        ok, line = compare(n, os.path.join(a, n), pb, gsw, banded, clears, fog, margin)
        print(line)
        fails += not ok
    for n in named:
        if n not in names:
            print("%-18s not in %s" % (n, a))
            fails += 1
    print("cmp_dirs_band: %d images (%d held to the band), %d failed, GS width %d" %
          (len(names), held, fails, gsw))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
