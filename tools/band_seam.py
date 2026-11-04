#!/usr/bin/env python3
"""tools/band_seam.py: the edges of the 4:3 picture on a wide present (issue
189: stage 32's mist quads drawn only beside the picture, lighter bars with
an edge at the picture's; port/shaders/vu_common.hlsli vu_rule_beside).

  python3 -I tools/band_seam.py PNG [--band X0,X1] [--max N] [--cols N]

PNG is a wide present at full height (rd_replay_tool --pass-dump's
PRESENT.png): the picture is its centre columns X0..X1-1, by default the
4:3 box of the image's height (X0 = (W - round(H * 4 / 3)) / 2).  For each
edge, the mean over every row of the luminance (0.299 R + 0.587 G +
0.114 B) of the N columns (default 4) just outside the picture less that
of the N just inside it, skipping the edge's own column on each side (the
present's filter blends across it): a veil drawn on one side of the edge
only moves it by the veil's step (7.3 and 7.2 levels on the 189 frame at
Enhanced 16:10 with the cards drawn beside the picture: the sides
lighter), a picture continuous across it keeps it at the image's own
column-to-column step (0.1 there).  The same measure at eleven
inner columns, 100 apart from column 400, is printed as the typical step.

One line; exit 0 when both edges' magnitudes are at most --max (default
1.5 levels), 1 otherwise, 2 on a usage error.  Standard library only (the
PNG reader is tools/cmp_dirs_band.py's)."""
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


def column_means(rows, bpp, x0, x1):
    """per row, the mean luminance of columns x0..x1-1"""
    out = []
    for r in rows:
        s = 0.0
        for x in range(x0, x1):
            o = x * bpp
            s += 0.299 * r[o] + 0.587 * r[o + 1] + 0.114 * r[o + 2]
        out.append(s / (x1 - x0))
    return out


def step(rows, bpp, left, right, n):
    """mean over the rows of (columns right..right+n-1) - (left-n+1..left)"""
    a = column_means(rows, bpp, left - n + 1, left + 1)
    b = column_means(rows, bpp, right, right + n)
    return sum(q - p for p, q in zip(a, b)) / len(a)


def main(argv):
    path, band, most, n = None, None, 1.5, 4
    i = 0
    try:
        while i < len(argv):
            a = argv[i]
            if a == "--band":
                band = tuple(int(v) for v in argv[i + 1].split(","))
                i += 2
            elif a == "--max":
                most = float(argv[i + 1])
                i += 2
            elif a == "--cols":
                n = int(argv[i + 1])
                i += 2
            elif path is None and not a.startswith("--"):
                path = a
                i += 1
            else:
                raise ValueError(a)
        if path is None or n < 1 or (band is not None and len(band) != 2):
            raise ValueError("no PNG")
    except (ValueError, IndexError) as e:
        sys.stderr.write("usage: band_seam.py PNG [--band X0,X1] [--max N] [--cols N] (%s)\n" % e)
        return 2
    w, h, bpp, rows = load_png(path)
    x0, x1 = band if band else ((w - round(h * 4 / 3)) // 2, w - (w - round(h * 4 / 3)) // 2)
    if x0 - n - 1 < 0 or x1 + n + 1 > w or x1 - x0 < 2 * n + 2:
        sys.stderr.write("band_seam: %s: %dx%d has no room for columns %d..%d and %d beside\n"
                         % (path, w, h, x0, x1 - 1, n))
        return 2
    # outside less inside at each edge, the edge's own column left out on each side
    left = -step(rows, bpp, x0 - 2, x0 + 1, n)
    right = step(rows, bpp, x1 - 2, x1 + 1, n)
    inner = [abs(step(rows, bpp, x, x + 1, n)) for x in range(400, 1500, 100) if x + n + 1 < x1]
    typical = sum(inner) / len(inner) if inner else 0.0
    ok = abs(left) <= most and abs(right) <= most
    print("band_seam: %s: %dx%d, picture columns %d..%d: left edge %.2f, right edge %.2f "
          "(outside less inside), typical step %.2f, bound %.2f: %s"
          % (path, w, h, x0, x1 - 1, left, right, typical, most, "ok" if ok else "SEAM"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
