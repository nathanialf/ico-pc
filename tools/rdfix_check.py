#!/usr/bin/env python3
"""rdfix_check.py: a committed fixture (.rdfix) holds no art section.

A fixture (port/render/rd_fixture.h, docs/port/TESTING_FIXTURES.md) is the
skeleton of a frame dump: its commands, state and structure, every texture a
pattern NAME and every mesh a generator NAME, with no texel array and no
vertex stream.  This check reads the file alone (no build): the head, the
notes (at least one declared property, expect=), the skeleton inflated to its
stated size, then the skeleton's sections walked to their end: an art array
in place of a name or count breaks the walk.  The C check (rd_fixture_tool
check, the rd_fixtures_check ctest) also proves every unnamed payload byte
zero.  tools/check_no_rom.sh runs it on every tracked (or staged) .rdfix.

  python3 -I tools/rdfix_check.py FILE...   check these files
  python3 -I tools/rdfix_check.py --all     every port/render/test/fixtures/*.rdfix
  python3 -I tools/rdfix_check.py --stdin NAME   the bytes on stdin (a staged blob)
Exit 0 clean, 1 on any problem.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATTERNS = {"grey", "gradient", "checker", "bright", "alpha_ramp", "date_split", "opaque",
            "clear", "ramp", "glyphs"}
MESH_GEN = "strips"
CMD, STATE, VTX, CAMERA, REGION = 40, 88, 28, 148, 8
LISTS = 13
DUMP_MIN, DUMP_MAX = 3, 17


class Bad(Exception):
    pass


class Cur:
    def __init__(self, b):
        self.b, self.o = b, 0

    def u32(self):
        if self.o + 4 > len(self.b):
            raise Bad("cut at byte %d" % self.o)
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def raw(self, n):
        if n < 0 or self.o + n > len(self.b):
            raise Bad("cut at byte %d (%d more wanted)" % (self.o, n))
        v = self.b[self.o:self.o + n]
        self.o += n
        return v

    def name(self):
        n = self.raw(1)[0]
        return self.raw(n).decode("ascii", "replace")


def state_bytes(ver):
    return 80 if ver == 3 else (84 if ver <= 8 else STATE)


def head_bytes(ver):
    return 8 + 16 + 8 + (12 if ver >= 10 else 0) + (REGION if ver >= 12 else 0) + 12 + CAMERA + \
        2 * state_bytes(ver)


def skeleton(sk, ver):
    c = Cur(sk)
    h = c.raw(head_bytes(ver))
    if h[:8] != b"ICORDMP\0" or struct.unpack_from("<4I", h, 8) != (ver, CMD, state_bytes(ver), VTX):
        raise Bad("the skeleton's dump head is not a version %d head of this layout" % ver)
    for _ in range(LISTS):
        c.raw(c.u32() * CMD)
    c.raw(c.u32())  # the payload (its unnamed bytes: the C check)
    images = 0
    for _ in range(c.u32()):
        hdr = struct.unpack("<8I", c.raw(32))
        if hdr[1] == 1:
            p = c.name()
            if p not in PATTERNS:
                raise Bad("a texture names '%s', no pattern" % p)
            levels = c.u32()
            if not 1 <= levels <= 16:
                raise Bad("a texture has %d levels" % levels)
            images += 1
    c.raw(c.u32() * 20)
    meshes = c.u32()
    for _ in range(meshes):
        _id, nv, qpv, ni, nb = struct.unpack("<5I", c.raw(20))
        g = c.name()
        if g != MESH_GEN:
            raise Bad("a mesh names '%s', no generator" % g)
        c.raw(8)  # size exponent, bones
        c.raw(ni * 4 + nb * 28)
    if ver >= 14:
        c.raw(4 + 128)
    if ver >= 15:
        c.raw(c.u32() * 8)
    c.raw(c.u32() * 8)
    tag = c.u32() if c.o != len(sk) else None
    if tag == 0x34505846:
        # the drop sheets' section (rd_fixture.h, @scene drops / cards): per sheet the
        # mesh id, its role, the exponent, four corners and the normal as small integers
        sheets = c.u32()
        if sheets == 0 or sheets > meshes:
            raise Bad("%d drop sheets of %d meshes" % (sheets, meshes))
        for _ in range(sheets):
            _id, role, e = struct.unpack("<IIi", c.raw(12))
            words = struct.unpack("<15i", c.raw(60))
            if role >= 4 or not -60 <= e <= 60:
                raise Bad("a drop sheet's role or exponent is out of range")
            corners, normal = words[:12], words[12:]
            if max(abs(w) for w in corners) >= 1 << 21 or max(abs(w) for w in normal) > 1 << 20:
                raise Bad("a drop sheet's corner or normal is past its range")
    elif tag is not None:
        # the placement section (rd_fixture.h): centres in lattice steps, payload offsets
        if tag != 0x31505846:
            raise Bad("bytes after the skeleton's end that are no placement section")
        placed = c.u32()
        if placed > meshes:
            raise Bad("%d meshes placed of %d" % (placed, meshes))
        for _ in range(placed):
            _id, x, y, z = struct.unpack("<I3i", c.raw(16))
            if max(abs(x), abs(y), abs(z)) > (1 << 23) - 16:
                raise Bad("a placed mesh's centre is past the lattice's range")
        prims = c.u32()
        if placed == 0 and prims == 0:
            raise Bad("an empty placement section")
        c.raw(prims * 4)
    if c.o != len(sk):
        raise Bad("%d bytes after the skeleton's end" % (len(sk) - c.o))
    return images, meshes


def check(path, b=None):
    if b is None:
        b = open(path, "rb").read()
    c = Cur(b)
    if c.raw(8) != b"ICORDFX1":
        raise Bad("no ICORDFX1 head")
    fmt, ver = c.u32(), c.u32()
    if fmt != 1 or not DUMP_MIN <= ver <= DUMP_MAX:
        raise Bad("format %d, dump version %d" % (fmt, ver))
    c.raw(32 + 8)
    notes = []
    for _ in range(c.u32()):
        n = struct.unpack("<H", c.raw(2))[0]
        notes.append(c.raw(n).decode("utf-8", "replace"))
    if not any(n.startswith("expect=") for n in notes):
        raise Bad("declares no property (expect=)")
    raw, z = c.u32(), c.u32()
    data = c.raw(z)
    if c.o != len(b):
        raise Bad("bytes after the deflated skeleton")
    sk = zlib.decompress(data, -15)
    if len(sk) != raw:
        raise Bad("the skeleton inflates to %d bytes, %d stated" % (len(sk), raw))
    images, meshes = skeleton(sk, ver)
    return "%d bytes, dump version %d, %d images as patterns, %d meshes generated, %d notes" % (
        len(b), ver, images, meshes, len(notes))


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--stdin":
        try:
            print("%s: %s" % (args[1], check(args[1], sys.stdin.buffer.read())))
            return 0
        except (Bad, zlib.error, struct.error) as e:
            print("%s: not a fixture without art: %s" % (args[1], e))
            return 1
    if args == ["--all"]:
        d = os.path.join(ROOT, "port", "render", "test", "fixtures")
        args = sorted(os.path.join(d, f) for f in os.listdir(d) if f.endswith(".rdfix")) \
            if os.path.isdir(d) else []
    bad = 0
    for p in args:
        try:
            print("%s: %s" % (os.path.basename(p), check(p)))
        except (Bad, OSError, zlib.error, struct.error) as e:
            print("%s: not a fixture without art: %s" % (p, e))
            bad += 1
    print("rdfix_check: %d files, %d problems" % (len(args), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
