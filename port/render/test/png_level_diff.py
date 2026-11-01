"""port/render/test/png_level_diff.py A.png B.png: how far apart two 8-bit
RGB or RGBA PNGs of one size are, channel by channel (alpha included), for
the A/B tests whose rule is "within a level" (rd_fades_fold_ab.cmake).

Prints one line:
    png_level_diff: W x H, C channels: E equal, O one level apart, M more (largest D), P pixels differ
and exits 0; 2 when the files cannot be read or their sizes or formats
differ (the reason printed).  Run it with python3 -I.
"""
import struct
import sys
import zlib


def load(path):
    """(w, h, channels, rows of bytes) of an 8-bit RGB or RGBA PNG"""
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
                r[x] = (r[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f != 0:
            raise ValueError("%s: filter %d" % (path, f))
        rows.append(bytes(r))
        prev = r
    return w, h, bpp, rows


def main(argv):
    if len(argv) != 3:
        print("usage: png_level_diff.py A.png B.png")
        return 2
    try:
        wa, ha, ca, ra = load(argv[1])
        wb, hb, cb, rb = load(argv[2])
    except (OSError, ValueError, zlib.error) as e:
        print("png_level_diff: %s" % e)
        return 2
    if (wa, ha, ca) != (wb, hb, cb):
        print("png_level_diff: %ux%u (%u channels) against %ux%u (%u channels)" %
              (wa, ha, ca, wb, hb, cb))
        return 2
    one = more = largest = pixels = 0
    for a, b in zip(ra, rb):
        if a == b:
            continue
        for x in range(0, len(a), ca):
            pa, pb = a[x:x + ca], b[x:x + ca]
            if pa == pb:
                continue
            pixels += 1
            for u, v in zip(pa, pb):
                d = u - v if u > v else v - u
                if d == 1:
                    one += 1
                elif d > 1:
                    more += 1
                largest = d if d > largest else largest
    total = wa * ha * ca
    print("png_level_diff: %u x %u, %u channels: %u equal, %u one level apart, %u more "
          "(largest %u), %u pixels differ" %
          (wa, ha, total, total - one - more, one, more, largest, pixels))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
