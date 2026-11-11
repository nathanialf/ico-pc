"""port/render/test/png_level_diff.py A.png B.png: how far apart two 8-bit
RGB or RGBA PNGs of one size are, channel by channel (alpha included), for
the A/B tests whose rule is "within a level" (rd_fades_fold_ab.cmake).

Prints one line:
    png_level_diff: W x H, C channels: E equal, O one level apart, M more (largest D), P pixels differ
and exits 0; 2 when the files cannot be read or their sizes or formats
differ (the reason printed).  Run it with python3 -I.

--channels SET (letters of RGBA, e.g. RGB or A) compares only those
channels (C counts them; a pixel differs when one of them does): the glow
light plan's A/B (rd_glow_light_ab.cmake) holds RGB at 0 levels everywhere
and the alpha only where the plan may change it.  An RGB file has no A.

--count-alpha V FILE prints
    png_level_diff: W x H, N pixels with alpha V
(0 for an RGB file): whether a frame's WORK2 holds the fill the mask kept.
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
    argv = list(argv)
    if len(argv) == 4 and argv[1] == "--count-alpha":
        try:
            v = int(argv[2])
            w, h, c, rows = load(argv[3])
        except (OSError, ValueError, zlib.error) as e:
            print("png_level_diff: %s" % e)
            return 2
        n = 0
        if c == 4:
            for r in rows:
                n += sum(1 for x in range(3, len(r), 4) if r[x] == v)
        print("png_level_diff: %u x %u, %u pixels with alpha %u" % (w, h, n, v))
        return 0
    chans = "RGBA"
    if len(argv) == 5 and argv[1] == "--channels":
        chans = argv[2].upper()
        argv = [argv[0]] + argv[3:]
        if not chans or any(ch not in "RGBA" for ch in chans):
            print("png_level_diff: --channels takes letters of RGBA")
            return 2
    if len(argv) != 3:
        print("usage: png_level_diff.py [--channels RGBA] A.png B.png | --count-alpha V A.png")
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
    pick = [k for k in range(ca) if "RGBA"[k] in chans]
    one = more = largest = pixels = 0
    for a, b in zip(ra, rb):
        if a == b:
            continue
        for x in range(0, len(a), ca):
            pa, pb = a[x:x + ca], b[x:x + ca]
            if pa == pb:
                continue
            differ = False
            for k in pick:
                u, v = pa[k], pb[k]
                d = u - v if u > v else v - u
                if d == 0:
                    continue
                differ = True
                if d == 1:
                    one += 1
                else:
                    more += 1
                largest = d if d > largest else largest
            pixels += 1 if differ else 0
    total = wa * ha * len(pick)
    print("png_level_diff: %u x %u, %u channels: %u equal, %u one level apart, %u more "
          "(largest %u), %u pixels differ" %
          (wa, ha, total, total - one - more, one, more, largest, pixels))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
