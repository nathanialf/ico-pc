#!/usr/bin/env python3
"""wide_closing_check.py: the closing planes beside the 4:3 picture, on the
player's frame of the long walkway (st08a_p4) where a flat grey block
showed through the arch at the right edge of a 16:9 picture.

The frame is a dump (rddump) the player sent; dumps hold game data and are
never committed, so this is run by hand with its path:

  tools/wide_closing_check.py REPLAY-TOOL DUMP OUT-DIR [BASELINE-TOOL]

  REPLAY-TOOL    the build's rd_replay_tool
  DUMP           the player's frame (frame-20261009-180352-v31438.rddump)
  OUT-DIR        where the pictures are written
  BASELINE-TOOL  an rd_replay_tool built before the change (optional)

Checks, at 1280x720 with the Enhanced preset:
  16:9   the block's box (x 1133..1274, y 430..514) shows the stonework
         behind it: at most 2% of its pixels are the block's flat grey
         (within 3 of 101,104,98 or 99,101,97), and at least 95% are within
         2 of the same frame drawn without the two meshes of the box
         (--nop on their draws, found by the tool's --list);
  4:3    with BASELINE-TOOL, the frame at 4:3 (960x720) is the same, byte
         for byte, from both tools.
Exit status 0 when every check passes, 1 otherwise.
"""
import os
import struct
import subprocess
import sys
import zlib

BOX = (1133, 430, 1275, 515)  # x0, y0, x1, y1 (exclusive)
FLAT = ((101, 104, 98), (99, 101, 97))


def load_png(path):
    data = open(path, 'rb').read()
    at, idat, w, h, ctype = 8, b'', 0, 0, 6
    while at < len(data):
        n, kind = struct.unpack('>I4s', data[at:at + 8])
        chunk = data[at + 8:at + 8 + n]
        if kind == b'IHDR':
            w, h, _, ctype = struct.unpack('>IIBB', chunk[:10])
        elif kind == b'IDAT':
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
        for x in range(stride):
            a = r[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                r[x] = (r[x] + a) & 255
            elif f == 2:
                r[x] = (r[x] + b) & 255
            elif f == 3:
                r[x] = (r[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                r[x] = (r[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(r))
        prev = r
    return w, h, bpp, rows


def pixel(img, x, y):
    _, _, bpp, rows = img
    return tuple(rows[y][x * bpp:x * bpp + 3])


def near(a, b, d):
    return all(abs(a[i] - b[i]) <= d for i in range(3))


def replay(tool, dump, out, *args):
    cmd = [tool, dump, out, '--present'] + list(args)
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0 or not os.path.exists(out):
        print(res.stdout)
        raise SystemExit('replay failed: %s' % ' '.join(cmd))


def box_draws(tool, dump):
    """the list:index of the draws of the box's two meshes: st08a_p4's
    4-vertex mesh and its 534-vertex one"""
    res = subprocess.run([tool, dump, os.devnull, '--list', '--no-device'],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    draws = []
    for line in res.stdout.splitlines():
        mesh = 'mesh st08a_p4 (4 vertices' in line or 'mesh st08a_p4 (534 vertices' in line
        if 'MESH' in line and mesh:
            draws.append(line.split()[0])
    return draws


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    tool, dump, out = sys.argv[1], sys.argv[2], sys.argv[3]
    base = sys.argv[4] if len(sys.argv) > 4 else None
    os.makedirs(out, exist_ok=True)
    fails = 0

    wide = os.path.join(out, 'wide.png')
    replay(tool, dump, wide, '1280x720', '--enhanced', '--aspect', '16:9')
    draws = box_draws(tool, dump)
    if len(draws) < 2:
        print('FAIL: the box\'s two draws not found in the list (%s)' % draws)
        return 1
    ref = os.path.join(out, 'wide_without_box.png')
    nops = []
    for d in draws:
        nops += ['--nop', d]
    replay(tool, dump, ref, '1280x720', '--enhanced', '--aspect', '16:9', *nops)
    a, b = load_png(wide), load_png(ref)
    x0, y0, x1, y1 = BOX
    total = flat = same = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = pixel(a, x, y)
            total += 1
            flat += any(near(p, f, 3) for f in FLAT)
            same += near(p, pixel(b, x, y), 2)
    print('16:9: box %d pixels, %d flat grey, %d as without the box (draws %s)' %
          (total, flat, same, ' '.join(draws)))
    if flat * 50 > total:
        print('FAIL: the flat grey block still shows (%d of %d)' % (flat, total))
        fails += 1
    if same * 100 < total * 95:
        print('FAIL: the box does not show what lies behind it (%d of %d)' % (same, total))
        fails += 1

    if base:
        n43 = os.path.join(out, 'narrow.png')
        b43 = os.path.join(out, 'narrow_baseline.png')
        replay(tool, dump, n43, '960x720', '--enhanced', '--aspect', '4:3')
        replay(base, dump, b43, '960x720', '--enhanced', '--aspect', '4:3')
        same43 = load_png(n43)[3] == load_png(b43)[3]
        print('4:3: %s the baseline tool\'s picture' % ('the same as' if same43 else 'NOT'))
        if not same43:
            print('FAIL: the 4:3 picture changed')
            fails += 1
    print('wide_closing_check: %s' % ('ok' if fails == 0 else '%d failures' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
