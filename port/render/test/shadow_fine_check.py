"""port/render/test/shadow_high_check.py ORIG_A ORIG_B HIGH_A --scale S:
Shadow detail: High's change confined to the shadow (rd_shadow_high_ab.cmake).

ORIG_A is SCENE right after list 3 at Original's levels, ORIG_B the same
without the three composites (the darkening Original draws is where A and B
differ), HIGH_A SCENE right after list 3 with the levels at the scene's
scale.  Every texel High changes must lie within D SCENE texels of a texel
Original darkens, D = ceil(8 (3 + 1 / L) S): a SHADOW2 texel is 8 GS pixels,
8 S SCENE texels, and a fine level reaches past the named one by one level
texel of the step, one of the tent and 1 / L of one of the composite's tap
(L the levels' odd whole scale, rd_shadowres.c rd_sr_level_scale), plus the
one texel of Original's own tap whose darkening rounds to nothing.

Prints one line:
    shadow_high_check: W x H, D texels: S darkened by Original, C changed by High, O outside
and exits 0; 1 when O > 0 or C is 0; 2 when the files cannot be read or
their sizes differ.  Run it with python3 -I.
"""
import math
import struct
import sys
import zlib


def load(path):
    """(w, h, bytes per pixel, rows of bytes) of an 8-bit RGB or RGBA PNG"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    at, idat, w, h, ctype = 8, [], 0, 0, 6
    while at + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        chunk = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", chunk[:10])
            if depth != 8 or ctype not in (2, 6) or chunk[12] != 0:
                raise ValueError("%s: not an 8-bit RGB(A) PNG without interlace" % path)
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break
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
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                r[x] = (r[x] + pr) & 255
        rows.append(r)
        prev = r
    return w, h, bpp, rows


def differ_mask(a, b, w, bpp):
    """per row, the columns whose pixels differ"""
    out = []
    for ra, rb in zip(a, b):
        if ra == rb:
            out.append([])
            continue
        cols = [x for x in range(w) if ra[x * bpp:(x + 1) * bpp] != rb[x * bpp:(x + 1) * bpp]]
        out.append(cols)
    return out


def bits(cols):
    """a row's columns as one integer's bits"""
    m = 0
    for x in cols:
        m |= 1 << x
    return m


def dilate(mask, w, h, d):
    """each row's bits within d (Chebyshev) of a marked texel"""
    full = (1 << w) - 1
    across = []
    for cols in mask:
        m = bits(cols)
        o = m
        for k in range(1, d + 1):
            o |= (m << k) | (m >> k)
        across.append(o & full)
    out = []
    for y in range(h):
        o = 0
        for i in range(max(0, y - d), min(h, y + d + 1)):
            o |= across[i]
        out.append(o)
    return out


def main(argv):
    args = argv[1:]
    scale = None
    if "--scale" in args:
        k = args.index("--scale")
        scale = float(args[k + 1])
        del args[k:k + 2]
    if len(args) != 3 or scale is None or not scale > 0.0:
        print(__doc__)
        return 2
    try:
        oa, ob, ha = (load(p) for p in args)
    except (OSError, ValueError, zlib.error) as e:
        print("shadow_high_check: %s" % e)
        return 2
    w, h, bpp = oa[0], oa[1], oa[2]
    for o in (ob, ha):
        if o[0] != w or o[1] != h or o[2] != bpp:
            print("shadow_high_check: the sizes or formats differ")
            return 2
    n = int(math.ceil(scale - 1.0e-3))
    lvl = n if n % 2 == 1 else n + 1
    d = int(math.ceil(8.0 * (3.0 + 1.0 / lvl) * scale))
    dark = differ_mask(oa[3], ob[3], w, bpp)
    changed = differ_mask(oa[3], ha[3], w, bpp)
    near = dilate(dark, w, h, d)
    s = sum(len(c) for c in dark)
    c = sum(len(x) for x in changed)
    o = sum(bin(bits(cols) & ~near[y]).count("1") for y, cols in enumerate(changed) if cols)
    print("shadow_high_check: %d x %d, D %d texels: %d darkened by Original, %d changed by High, "
          "%d outside" % (w, h, d, s, c, o))
    return 1 if o or c == 0 else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
