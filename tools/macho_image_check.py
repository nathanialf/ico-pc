#!/usr/bin/env python3
"""macho_image_check.py: where an Apple program's image lies, and the save-state rule on it.

    python3 -I tools/macho_image_check.py show PROGRAM
    python3 -I tools/macho_image_check.py check PROGRAM --base ADDRESS
    python3 -I tools/macho_image_check.py selftest

A save state holds host addresses.  A load in a later process moves every
aligned 64-bit word of the game's memory that lies in a range the saving
process had (the program's image, the arena, the fiber pool) by that range's
delta (port/platform/state_reloc.h).  The game's own memory is full of
32-bit pairs read as one 64-bit word: an EE offset or a float followed by a
small integer.  When a range's addresses have a small upper half (below
ICO_STATE_SCAN_HIGH_MIN, 0x10: an address below 64 GB), such a pair reads as
an address in it and is moved too (issue 218: the Apple programs sat just
above the 4 GB __PAGEZERO, upper half 1).  The loader therefore never moves
such a range by its delta (the state continues from its checkpoint), and the
macOS program is linked with its __TEXT segment at a base whose upper half is
far above that (CMakeLists.txt ICO_APPLE_TEXT_BASE; docs/BUILDING.md,
"Apple image base").

`show` prints the Mach-O's segments (a thin arm64 or x86-64 file, or the
arm64 slice of a universal one), the linker named in LC_BUILD_VERSION and
the chained-fixups pointer format.

`check` fails (exit 1) unless the program's image starts exactly at --base
(the linker took the address it was given) and every address from the
image's start to its end plus the kernel's largest arm64 slide (80 MB:
XNU's kern_exec.c draws up to two 32 MB sections plus up to 16 MB of pages,
vm_map.c vm_map_get_max_aslr_slide_section/_pages) has an upper half of at
least 0x10.  Exit 2: the file is not a Mach-O this tool reads.

`selftest` builds minimal Mach-O headers in memory and checks the verdicts
(the ctest macho_image_check).

Nothing here runs the program; it reads the file only (untrusted data: every
size and offset is bounds-checked).
"""
import argparse
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
CPU_TYPE_ARM64 = 0x0100000C
CPU_TYPE_X86_64 = 0x01000007
LC_SEGMENT_64 = 0x19
LC_BUILD_VERSION = 0x32
LC_DYLD_CHAINED_FIXUPS = 0x80000034

# port/platform/state_reloc.h ICO_STATE_SCAN_HIGH_MIN: the smallest upper
# half a moved range may have for the conservative scan to move it
SCAN_HIGH_MIN = 0x10
# the kernel's largest slide of an arm64 program: (random % 3) sections of
# ARM_TT_TWIG_SIZE (32 MB with 16 KB pages) plus 1 .. (16 MB >> page shift)
# - 1 pages (bsd/kern/kern_exec.c, osfmk/vm/vm_map.c)
MAX_SLIDE = 2 * 0x2000000 + 0x1000000

TOOLS = {1: "clang", 2: "swift", 3: "ld", 4: "lld"}


class Bad(Exception):
    pass


def u32(b, off):
    if off < 0 or off + 4 > len(b):
        raise Bad("the file ends inside a header (offset %#x)" % off)
    return struct.unpack_from("<I", b, off)[0]


def thin(b):
    """The offset of the arm64 (else x86-64) Mach-O inside b."""
    if len(b) < 8:
        raise Bad("the file is %d bytes" % len(b))
    magic_be = struct.unpack_from(">I", b, 0)[0]
    if magic_be in (FAT_MAGIC, FAT_MAGIC_64):
        n = struct.unpack_from(">I", b, 4)[0]
        if n > 32:
            raise Bad("a universal file with %d slices" % n)
        size = 20 if magic_be == FAT_MAGIC else 32
        slices = []
        for i in range(n):
            o = 8 + i * size
            if o + size > len(b):
                raise Bad("the universal header is cut")
            if magic_be == FAT_MAGIC:
                cpu, _sub, off, ln = struct.unpack_from(">IIII", b, o)[:4]
            else:
                cpu, _sub, off, ln = struct.unpack_from(">IIQQ", b, o)
            if off + ln > len(b):
                raise Bad("a slice lies outside the file")
            slices.append((cpu, off))
        for want in (CPU_TYPE_ARM64, CPU_TYPE_X86_64):
            for cpu, off in slices:
                if cpu == want:
                    return off
        raise Bad("no arm64 or x86-64 slice")
    return 0


def parse(b):
    base = thin(b)
    if u32(b, base) != MH_MAGIC_64:
        raise Bad("not a 64-bit Mach-O (magic %#x)" % u32(b, base))
    cpu = u32(b, base + 4)
    ncmds = u32(b, base + 16)
    sizeofcmds = u32(b, base + 20)
    if ncmds > 4096 or base + 32 + sizeofcmds > len(b):
        raise Bad("the load commands do not fit the file")
    out = {"cpu": cpu, "segments": [], "tools": [], "minos": None, "platform": None,
           "chained": None}
    off = base + 32
    end = off + sizeofcmds
    for _ in range(ncmds):
        cmd = u32(b, off)
        size = u32(b, off + 4)
        if size < 8 or off + size > end:
            raise Bad("a load command at %#x runs past the commands" % off)
        if cmd == LC_SEGMENT_64:
            if size < 72:
                raise Bad("a segment command of %d bytes" % size)
            name = b[off + 8:off + 24].split(b"\0", 1)[0].decode("ascii", "replace")
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", b, off + 24)
            maxprot, initprot = struct.unpack_from("<ii", b, off + 56)
            out["segments"].append({"name": name, "vmaddr": vmaddr, "vmsize": vmsize,
                                    "fileoff": fileoff, "filesize": filesize,
                                    "maxprot": maxprot, "initprot": initprot})
        elif cmd == LC_BUILD_VERSION and size >= 24:
            platform, minos, _sdk, ntools = struct.unpack_from("<IIII", b, off + 8)
            out["platform"] = platform
            out["minos"] = "%d.%d" % (minos >> 16, (minos >> 8) & 0xFF)
            for i in range(min(ntools, (size - 24) // 8)):
                tool, ver = struct.unpack_from("<II", b, off + 24 + i * 8)
                out["tools"].append("%s %d.%d.%d" % (TOOLS.get(tool, "tool %d" % tool), ver >> 16,
                                                     (ver >> 8) & 0xFF, ver & 0xFF))
        elif cmd == LC_DYLD_CHAINED_FIXUPS and size >= 16:
            dataoff, datasize = struct.unpack_from("<II", b, off + 8)
            fmt = None
            if datasize >= 28 and base + dataoff + datasize <= len(b):
                h = base + dataoff
                starts = u32(b, h + 4)
                s = h + starts
                nseg = u32(b, s)
                for i in range(min(nseg, 64)):
                    so = u32(b, s + 4 + i * 4)
                    if so:
                        # dyld_chained_starts_in_segment: size, page_size, pointer_format
                        fmt = struct.unpack_from("<H", b, s + so + 6)[0]
                        break
            out["chained"] = fmt
        off += size
    return out


def image(m):
    """[lo, hi) of the mapped segments other than __PAGEZERO."""
    segs = [s for s in m["segments"] if s["name"] != "__PAGEZERO" and s["vmsize"] > 0]
    if not segs:
        raise Bad("no mapped segment")
    return min(s["vmaddr"] for s in segs), max(s["vmaddr"] + s["vmsize"] for s in segs)


def verdict(m, want_base):
    """None when the image passes, else why it does not."""
    lo, hi = image(m)
    if lo != want_base:
        return ("the image starts at %#x, not at %#x (the linker did not take the base it was given)"
                % (lo, want_base))
    last = hi + MAX_SLIDE - 1
    if (lo >> 32) < SCAN_HIGH_MIN or (last >> 32) < SCAN_HIGH_MIN:
        return ("the image %#x-%#x (with the largest slide, to %#x) has an upper half below %#x: "
                "a game value next to a small integer reads as an address in it"
                % (lo, hi, last, SCAN_HIGH_MIN))
    if (lo >> 32) != (last >> 32):
        return ("the image %#x-%#x with the largest slide (to %#x) crosses a 4 GB boundary"
                % (lo, hi, last))
    return None


def read(path):
    with open(path, "rb") as f:
        return f.read()


def show(a):
    m = parse(read(a.program))
    lo, hi = image(m)
    print("%s: cpu %#x, platform %s, minimum OS %s, built by %s, chained fixups pointer format %s"
          % (a.program, m["cpu"], m["platform"], m["minos"], ", ".join(m["tools"]) or "?",
             m["chained"]))
    for s in m["segments"]:
        print("  %-16s vmaddr %#014x size %#011x file %#010x+%#x prot %d/%d"
              % (s["name"], s["vmaddr"], s["vmsize"], s["fileoff"], s["filesize"], s["initprot"],
                 s["maxprot"]))
    print("  image %#x-%#x (%d MB), upper half %#x; with the largest slide to %#x"
          % (lo, hi, (hi - lo) >> 20, lo >> 32, hi + MAX_SLIDE - 1))
    return 0


def check(a):
    m = parse(read(a.program))
    why = verdict(m, int(a.base, 0))
    lo, hi = image(m)
    if why is not None:
        print("macho_image_check: FAIL: %s: %s" % (a.program, why))
        return 1
    print("macho_image_check: %s: the image %#x-%#x (upper half %#x, %d MB; the largest slide "
          "keeps it there): save states made by it can be moved in a later process"
          % (a.program, lo, hi, lo >> 32, (hi - lo) >> 20))
    return 0


# --- the self-test: hand-built headers -----------------------------------------------


def segment(name, vmaddr, vmsize, fileoff=0, filesize=0, prot=5):
    return struct.pack("<II16sQQQQiiII", LC_SEGMENT_64, 72, name.encode("ascii"), vmaddr, vmsize,
                       fileoff, filesize, prot, prot, 0, 0)


def build(cmds, cpu=CPU_TYPE_ARM64, fat=False):
    body = b"".join(cmds)
    mh = struct.pack("<IIIIIIII", MH_MAGIC_64, cpu, 0, 2, len(cmds), len(body), 0, 0) + body
    if not fat:
        return mh
    off = 0x1000
    hdr = struct.pack(">II", FAT_MAGIC, 2)
    hdr += struct.pack(">IIIII", CPU_TYPE_X86_64, 3, off, len(mh), 12)
    hdr += struct.pack(">IIIII", cpu, 0, off * 2, len(mh), 12)
    return hdr.ljust(off, b"\0") + mh.ljust(off, b"\0") + mh


def selftest(_a):
    base = 0x0000_2A1D_6000_0000
    fails = 0

    def expect(what, blob, want_base, ok, needle=None):
        nonlocal fails
        try:
            why = verdict(parse(blob), want_base)
        except Bad as e:
            why = "bad: %s" % e
        good = (why is None) == ok and (needle is None or (why is not None and needle in why))
        print("%s: %s -> %s" % ("ok  " if good else "FAIL", what, why or "passes"))
        if not good:
            fails += 1

    pz4 = segment("__PAGEZERO", 0, 0x100000000, prot=0)
    expect("the v0.11.6 layout: 4 GB __PAGEZERO, __TEXT at 0x100000000", build(
        [pz4, segment("__TEXT", 0x100000000, 0x1800000), segment("__LINKEDIT", 0x102f00000, 0x6c000)]),
        0x100000000, False, "upper half below")
    expect("__TEXT at the chosen base", build(
        [pz4, segment("__TEXT", base, 0x1800000), segment("__DATA", base + 0x1800000, 0x1700000),
         segment("__LINKEDIT", base + 0x2f00000, 0x6c000)]), base, True)
    expect("the arm64 slice of a universal file", build(
        [pz4, segment("__TEXT", base, 0x1800000)], fat=True), base, True)
    expect("the linker ignored the base (__TEXT right after __PAGEZERO)", build(
        [pz4, segment("__TEXT", 0x100000000, 0x1800000)]), base, False, "did not take")
    expect("a base below 64 GB (upper half 0xf)", build(
        [pz4, segment("__TEXT", 0xF_6000_0000, 0x1800000)]), 0xF_6000_0000, False,
        "upper half below")
    expect("an image that the largest slide carries over a 4 GB boundary", build(
        [pz4, segment("__TEXT", 0x2A1D_FC00_0000, 0x1800000)]), 0x2A1D_FC00_0000, False,
        "crosses")
    expect("a 32-bit header", struct.pack("<I", 0xFEEDFACE) + b"\0" * 28, base, False, "bad:")
    expect("load commands past the end", build([pz4])[:40], base, False, "bad:")
    if fails:
        print("macho_image_check selftest: %d FAILED" % fails)
        return 1
    print("macho_image_check selftest: all passed")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("show")
    s.add_argument("program")
    c = sub.add_parser("check")
    c.add_argument("program")
    c.add_argument("--base", required=True)
    sub.add_parser("selftest")
    a = p.parse_args()
    try:
        return {"show": show, "check": check, "selftest": selftest}[a.cmd](a)
    except (Bad, OSError) as e:
        print("macho_image_check: %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main())
