#!/usr/bin/env python3
"""state_recover.py: the progress in an old save state, as a memory-card save.

    python3 -I tools/state_recover.py SLOT.ics SYMBOLS [--disc SLUG] [--side OUT]
        [--card CARD_FOLDER [--slot N] [--overwrite] [--tool PATH]]

A save state (states/<slug>/slotN.ics) loads only into the build that made it
(format 1) or into a later one whose code it
points into is unchanged (format 2).  This tool takes what a state holds
regardless of the build: the game's save image gameSysMainSaveBuff, which
the game's own gamesysMemorySave wrote at its last checkpoint (the scripts'
checkpoints, the idol doors, every sofa save; ico2/common/src/StageManager.c
CheckPoint) and which is what a sofa save writes to the card, plus the few
words a card save adds (the option words, the play time, the card's record).

SLOT.ics      the state file (format 1 or 2).
SYMBOLS       the symbol table of the build that wrote it:
                the Android package (.apk): its lib/arm64-v8a/libmain.so,
                  whose dynamic symbol table names the game's globals with
                  their sizes (the package's assets/libmain.syms.gz names
                  only functions in the packages that wrote format 1);
                an ELF program or library (libmain.so, the Linux program);
                the linker map beside a desktop program (ico_pc.map on Linux,
                  x64/ico_pc_x64.map on Windows: GNU ld's map);
                an nm table (plain or .gz; the Android packages that write
                  format 2 carry the data symbols with sizes in libmain.syms.gz).
--disc        the state's disc (pal, jpn, kor, ntsc, aug6, jun26, cn); a state
              of another disc is refused.  Default: the state's own.
--side        where the side file goes (default: SLOT.ics's name + .rescue in
              the current folder).
--card        then runs state_recover_tool on the side file into this card
              folder ([paths] saves; memcard/ in the user folder by default),
              at the lowest free slot or --slot N (0-9; a slot that holds a
              save needs --overwrite).
--tool        the state_recover_tool program (default: the newest one under
              build-host/*/port/save/, then PATH).

The tool prints what it found (the state's stage, version and play time, the
checkpoint's stage, each global's address and size) and exits 0, 1 when it
refuses (the reason printed), 2 on a usage or file error.  It never changes
the state file or the symbol table.
"""

import argparse
import bisect
import glob
import gzip
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
import zlib

MAGIC = b"ICOSTATE"
HEADER_BYTES = 512
ENTRY_BYTES = 64
FORMATS = (1, 2)  # port/platform/state.h ICO_STATE_FORMAT_OLDEST .. ICO_STATE_FORMAT_VERSION
SIDE_MAGIC = "ICORESCUE 1"  # port/save/state_recover.h

# the globals the side file carries: name, size in the state's build (the
# host layouts, port/save/state_recover.c s_globals), required
GLOBALS = [
    ("gameSysMainSaveBuff", 25596, True),
    ("systemStatus", 48, True),
    ("IosMcPreviewInfo", 24, True),
    ("optionScreenMode", 4, False),  # required on the discs whose game block carries it
    ("girlControlMode", 4, False),
    ("IosMcProductFile", 992, False),
]
CHECK_ONLY = [("stage_no", 4)]  # read to check the table against the header

# gamesys.c stamp_str_*: the first 18 bytes of a save image (cn: kor's)
STAMPS = {
    "pal": b"12/12/01 17:53:37\0",
    "jpn": b"10/23/01 02:02:43\0",
    "kor": b"10/23/01 17:47:13\0",
    "cn": b"10/23/01 17:47:13\0",
    "ntsc": b"08/18/01 22:22:20\0",
    "aug6": b"08/07/01 14:51:53\0",
    "jun26": b"06/26/01 20:00:06\0",
}


class Refused(Exception):
    pass


# --- the state file ---------------------------------------------------------------


def text(b):
    return b.split(b"\0", 1)[0].decode("utf-8", "replace")


def read_state(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_BYTES or data[:8] != MAGIC:
        raise Refused("%s is not a save state (no ICOSTATE header)" % path)
    (version, hbytes) = struct.unpack_from("<II", data, 8)
    if version not in FORMATS:
        raise Refused("%s is format %d, which this tool does not know (it reads %s)"
                      % (path, version, " and ".join(str(v) for v in FORMATS)))
    (n, keys_bytes, file_bytes, crc, ebytes) = struct.unpack_from("<IIQII", data, 144)
    if hbytes != HEADER_BYTES or ebytes != ENTRY_BYTES or n > 32 or keys_bytes >= 4096:
        raise Refused("%s: the header is damaged" % path)
    if file_bytes != len(data):
        raise Refused("%s is %d bytes, its header says %d: the file is cut short or padded"
                      % (path, len(data), file_bytes))
    prefix = HEADER_BYTES + n * ENTRY_BYTES + keys_bytes
    if prefix > len(data):
        raise Refused("%s: the section table does not fit the file" % path)
    p = bytearray(data[:prefix])
    struct.pack_into("<I", p, 160, 0)
    if zlib.crc32(bytes(p)) & 0xFFFFFFFF != crc:
        raise Refused("%s: the header's CRC is wrong (the file is damaged)" % path)
    h = {
        "version": version,
        "commit": text(data[32:80]),
        "platform": text(data[80:96]),
        "disc": text(data[96:112]),
        "saved_at": struct.unpack_from("<q", data, 120)[0],
        "play_frames": struct.unpack_from("<I", data, 128)[0],
        "stage_no": struct.unpack_from("<i", data, 132)[0],
        "main_tick": struct.unpack_from("<I", data, 136)[0],
        "vsync": struct.unpack_from("<I", data, 140)[0],
        "keys": data[prefix - keys_bytes:prefix].decode("utf-8", "replace"),
        "sections": {},
        "order": [],
    }
    for i in range(n):
        e = HEADER_BYTES + i * ENTRY_BYTES
        name = text(data[e:e + 16])
        (off, packed, raw, addr, scrc, flags) = struct.unpack_from("<QQQQII", data, e + 16)
        if not name or off < prefix or off > len(data) or packed > len(data) - off:
            raise Refused("%s: section %d of the table is damaged" % (path, i))
        h["sections"][name] = {"offset": off, "packed": packed, "raw": raw, "addr": addr,
                               "crc": scrc, "flags": flags}
        h["order"].append(name)
    h["data"] = data
    return h


def section_bytes(h, name):
    s = h["sections"].get(name)
    if s is None:
        raise Refused("the state has no %s section" % name)
    if "bytes" not in s:
        packed = h["data"][s["offset"]:s["offset"] + s["packed"]]
        try:
            raw = zlib.decompressobj(-15).decompress(packed) if s["raw"] else b""
        except zlib.error as e:
            raise Refused("the state's %s section does not inflate: %s" % (name, e))
        if len(raw) != s["raw"] or zlib.crc32(raw) & 0xFFFFFFFF != s["crc"]:
            raise Refused("the state's %s section is damaged (size or CRC)" % name)
        s["bytes"] = raw
    return s["bytes"]


def ranges(h):
    """state_host.c StateRanges' common prefix (RANG and RAN2), or None."""
    if "ranges" not in h["sections"]:
        return None
    b = section_bytes(h, "ranges")
    if len(b) < 8 + 8 * 15:
        return None
    (magic, flags) = struct.unpack_from("<II", b, 0)
    v = struct.unpack_from("<15Q", b, 8)
    names = ("image_lo", "image_hi", "text_lo", "text_hi", "arena_lo", "arena_hi", "pool_lo",
             "pool_hi", "slot_bytes", "gdata_lo", "gdata_hi", "gbss_lo", "gbss_hi", "pobj_lo",
             "pobj_bytes")
    r = dict(zip(names, v))
    r["magic"] = magic
    r["flags"] = flags
    return r


# --- the symbol table --------------------------------------------------------------


class Table:
    def __init__(self, what):
        self.what = what
        self.syms = {}  # name -> (address, size or None)
        self.anchor = {}  # "gdata"/"gbss" -> link address of the range's start
        self.link_base = None
        self.data_symbols = 0

    def add(self, name, addr, size=None, data=True):
        if name in self.syms and self.syms[name][1] is not None and size is None:
            return
        self.syms[name] = (addr, size)
        if data:
            self.data_symbols += 1


def read_elf(blob, what):
    t = Table(what)
    if blob[:4] != b"\x7fELF" or blob[4] != 2 or blob[5] != 1:
        raise Refused("%s is not a 64-bit little-endian ELF file" % what)
    (phoff, shoff) = struct.unpack_from("<QQ", blob, 32)
    (phentsize, phnum, shentsize, shnum, shstrndx) = struct.unpack_from("<HHHHH", blob, 54)
    loads = []
    for i in range(phnum):
        (ptype, _flags, _off, vaddr) = struct.unpack_from("<IIQQ", blob, phoff + i * phentsize)
        if ptype == 1:
            loads.append(vaddr)
    t.link_base = min(loads) if loads else 0
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        (nm, stype, _fl, addr, off, size, link, _info, _al, entsize) = struct.unpack_from(
            "<IIQQQQIIQQ", blob, o)
        secs.append((nm, stype, addr, off, size, link, entsize))
    if not secs:
        raise Refused("%s has no section headers" % what)
    shstr = secs[shstrndx]

    def sname(off, strsec):
        base = strsec[3] + off
        end = blob.index(b"\0", base)
        return blob[base:end].decode("utf-8", "replace")

    for s in secs:
        n = sname(s[0], shstr)
        if n in ("ico_gdata", "ico_gbss"):
            t.anchor[n[4:]] = s[2]
    for s in secs:
        if s[1] not in (2, 11):  # SHT_SYMTAB, SHT_DYNSYM
            continue
        strsec = secs[s[5]]
        for k in range(s[4] // 24):
            (nm, info, _other, shndx, value, size) = struct.unpack_from("<IBBHQQ", blob,
                                                                         s[3] + k * 24)
            if nm == 0 or shndx == 0:
                continue
            typ = info & 0xF
            if typ in (1, 0):  # STT_OBJECT, STT_NOTYPE
                t.add(sname(nm, strsec), value, size if size else None, data=(typ == 1))
            elif typ == 2:
                t.add(sname(nm, strsec), value, size, data=False)
    return t


SYM_LINE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_.$][\w.$@]*)\s*$")
ASSIGN_LINE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_.$][\w.$@]*)\s*=")
LONE_NAME = re.compile(r"^( ?)(\S+)\s*$")
SEC_LINE = re.compile(r"^(\s?)(\S+)?\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(\s+\S.*)?$")


def read_map(textdata, what):
    """GNU ld's map (the Linux program's ELF map, the Windows program's PE
    map): the game data ranges' starts (the ico_gdata and ico_gbss output
    sections; on PE the .data$ico_a and .data$ico_n brackets,
    port/platform/game_data_range.c) and every symbol line."""
    t = Table(what)
    pending = None
    syms = []
    insec = []  # (start, end) of input sections, for symbol sizes
    for line in textdata.splitlines():
        m = ASSIGN_LINE.match(line)
        if m:
            if m.group(2) in ("__ImageBase", "__executable_start"):
                t.link_base = int(m.group(1), 16)
            continue
        m = SYM_LINE.match(line)
        if m:
            syms.append((int(m.group(1), 16), m.group(2)))
            continue
        m = LONE_NAME.match(line)
        if m and not m.group(2).startswith(("*", "0x")):
            pending = (m.group(1) == " ", m.group(2))
            continue
        m = SEC_LINE.match(line)
        if m:
            name = m.group(2)
            inner = m.group(1) == " "
            if name is None and pending is not None:
                inner, name = pending
            pending = None
            if name is None:
                continue
            addr = int(m.group(3), 16)
            size = int(m.group(4), 16)
            if not inner and name in ("ico_gdata", "ico_gbss"):
                t.anchor.setdefault(name[4:], addr)
            if inner and name == ".data$ico_a":
                t.anchor.setdefault("gdata", addr)
            if inner and name == ".data$ico_n":
                t.anchor.setdefault("gbss", addr)
            if inner and size > 0:
                insec.append((addr, addr + size))
            continue
        pending = None
    syms.sort()
    insec.sort()
    starts = [s[0] for s in insec]
    for i, (addr, name) in enumerate(syms):
        bound = None
        k = bisect.bisect_right(starts, addr) - 1
        if k >= 0 and insec[k][0] <= addr < insec[k][1]:
            bound = insec[k][1]
        if i + 1 < len(syms) and syms[i + 1][0] > addr:
            nxt = syms[i + 1][0]
            bound = nxt if bound is None else min(bound, nxt)
        t.add(name, addr, None, data=True)
        t.syms[name] = (addr, None, bound)
    if t.link_base is None and "__ImageBase" in t.syms:
        t.link_base = t.syms["__ImageBase"][0]
    return t


def read_nm(textdata, what):
    t = Table(what)
    t.link_base = 0
    for line in textdata.splitlines():
        f = line.split()
        try:
            if len(f) == 3:
                addr, typ, name, size = int(f[0], 16), f[1], f[2], None
            elif len(f) == 4:
                addr, size, typ, name = int(f[0], 16), int(f[1], 16), f[2], f[3]
            else:
                continue
        except ValueError:
            continue
        if name in ("__start_ico_gdata", "__start_ico_gbss"):
            t.anchor[name[12:]] = addr
        if name in ("__executable_start", "__ImageBase"):
            t.link_base = addr
        t.add(name, addr, size, data=typ in "DdBbRrVvGgSs")
    return t


def read_symbols(path):
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] == b"PK\x03\x04":
        with zipfile.ZipFile(io.BytesIO(blob)) as z:
            names = z.namelist()
            libs = sorted(n for n in names if re.match(r"lib/[^/]+/libmain\.so$", n))
            libs.sort(key=lambda n: 0 if "/arm64-v8a/" in n else 1)
            if libs:
                return read_elf(z.read(libs[0]), "%s:%s" % (path, libs[0]))
            if "assets/libmain.syms.gz" in names:
                return read_text(z.read("assets/libmain.syms.gz"),
                                 "%s:assets/libmain.syms.gz" % path)
        raise Refused("%s holds neither lib/<abi>/libmain.so nor assets/libmain.syms.gz" % path)
    if blob[:4] == b"\x7fELF":
        return read_elf(blob, path)
    return read_text(blob, path)


def read_text(blob, what):
    if blob[:2] == b"\x1f\x8b":
        blob = gzip.decompress(blob)
    s = blob.decode("utf-8", "replace")
    if "Linker script and memory map" in s or "Memory Configuration" in s:
        return read_map(s, what)
    return read_nm(s, what)


# --- the globals -------------------------------------------------------------------


def place(h, table, rg):
    """The difference between the table's link addresses and the state's
    run-time ones, from the game data ranges' starts (else the image's)."""
    gd = h["sections"].get("gdata")
    gb = h["sections"].get("gbss")
    if gd is None:
        raise Refused("the state has no gdata section")
    deltas = []
    how = None
    if "gdata" in table.anchor:
        deltas.append(gd["addr"] - table.anchor["gdata"])
        how = "the game data range's start"
    if "gbss" in table.anchor and gb is not None and gb["raw"] > 0:
        deltas.append(gb["addr"] - table.anchor["gbss"])
        how = how or "the game bss range's start"
    if deltas:
        if any(d != deltas[0] for d in deltas):
            raise Refused("%s is not the build that wrote this state (its game data and bss "
                          "ranges lie %+#x and %+#x from the state's)"
                          % (table.what, deltas[0], deltas[1]))
        return deltas[0], how
    if rg is not None and table.link_base is not None:
        return rg["image_lo"] - table.link_base, "the program's load address"
    raise Refused("%s names neither the game data ranges nor a load address, and the state "
                  "has no ranges record: cannot place its symbols" % table.what)


def extract(h, table, delta, name, want):
    if name not in table.syms:
        return None, "not in the table"
    ent = table.syms[name]
    addr, size = ent[0], ent[1]
    bound = ent[2] if len(ent) > 2 else None
    # a size the table gives is carried as it is (state_recover_tool refuses one
    # that is not its global's); a map gives none: the bytes up to the next
    # symbol must hold the global
    if size is None:
        if bound is not None and bound - addr < want:
            return None, "the table leaves it %d bytes, %d wanted" % (bound - addr, want)
        size = want
    rt = addr + delta
    for sec in ("gdata", "gbss"):
        s = h["sections"].get(sec)
        if s is None or s["raw"] == 0:
            continue
        if s["addr"] <= rt and rt + size <= s["addr"] + s["raw"]:
            b = section_bytes(h, sec)
            off = rt - s["addr"]
            return (b[off:off + size], sec, off, rt, size), None
    return None, "its address %#x is outside the state's game data" % rt


def write_side(path, h, src, blobs):
    with open(path, "wb") as f:
        lines = [SIDE_MAGIC, "disc " + h["disc"], "commit " + h["commit"],
                 "platform " + h["platform"], "format %d" % h["version"],
                 "state_stage %d" % h["stage_no"], "play_frames %d" % h["play_frames"],
                 "main_tick %d" % h["main_tick"], "saved_at %d" % h["saved_at"],
                 "source " + os.path.basename(src)]
        f.write(("\n".join(lines) + "\n").encode("utf-8"))
        for name, data in blobs:
            f.write(("blob %s %d\n" % (name, len(data))).encode("ascii"))
            f.write(data)
            f.write(b"\n")
        f.write(b"end\n")


def find_tool(explicit):
    if explicit:
        return explicit
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    names = ("state_recover_tool", "state_recover_tool.exe")
    found = []
    for n in names:
        found += glob.glob(os.path.join(root, "build-host", "*", "port", "save", n))
    if found:
        return max(found, key=os.path.getmtime)
    for n in names:
        w = shutil.which(n)
        if w:
            return w
    return None


def play_time(frames, pal):
    s = frames // (50 if pal else 60)
    return "%d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def main(argv):
    ap = argparse.ArgumentParser(description="An old save state's progress as a memory-card "
                                 "save (see the docstring).")
    ap.add_argument("state", help="the state file, slotN.ics")
    ap.add_argument("symbols", help="the symbol table of the build that wrote it (.apk, ELF, "
                    "GNU ld map, nm table)")
    ap.add_argument("--disc", help="the state's disc slug (default: the state's own)")
    ap.add_argument("--side", help="the side file to write (default: <state>.rescue here)")
    ap.add_argument("--card", help="then write the card save into this card folder")
    ap.add_argument("--slot", type=int, help="the slot (0-9; default: the lowest free one)")
    ap.add_argument("--overwrite", action="store_true", help="replace a save in --slot")
    ap.add_argument("--tool", help="the state_recover_tool program")
    a = ap.parse_args(argv)
    if a.slot is not None and not 0 <= a.slot <= 9:
        ap.error("--slot takes 0 to 9")
    try:
        h = read_state(a.state)
        if a.disc is not None and a.disc != h["disc"]:
            raise Refused("the state is of the %s disc, not %s" % (h["disc"], a.disc))
        if h["disc"] not in STAMPS:
            raise Refused("the state's disc %r is not one this tool knows" % h["disc"])
        when = time.strftime("%Y-%m-%d %H:%M", time.localtime(h["saved_at"])) \
            if h["saved_at"] > 0 else "an unknown time"
        print("state_recover: %s: format %d, version %s, %s, the %s disc, saved %s at Main "
              "tick %d in stage %d; sections %s"
              % (a.state, h["version"], h["commit"], h["platform"], h["disc"], when,
                 h["main_tick"], h["stage_no"], " ".join(h["order"])))
        table = read_symbols(a.symbols)
        if table.data_symbols == 0:
            raise Refused("%s names no data symbols (an nm table of functions only, as the "
                          "libmain.syms.gz of the packages that wrote format 1): give the package "
                          "(.apk) itself, whose libmain.so names them" % table.what)
        rg = ranges(h)
        delta, how = place(h, table, rg)
        print("state_recover: %s: %d symbols; placed by %s, %+#x from its link addresses"
              % (table.what, len(table.syms), how, delta))
        blobs = []
        found = {}
        option_words = h["disc"] not in ("ntsc", "aug6", "jun26")  # gameblock_write's two words
        for name, want, need in GLOBALS + [(n, w, False) for n, w in CHECK_ONLY]:
            if name in ("optionScreenMode", "girlControlMode"):
                need = option_words
            got, why = extract(h, table, delta, name, want)
            if got is None:
                if need:
                    raise Refused("%s: %s" % (name, why))
                print("state_recover:   %-20s %s" % (name, why))
                continue
            data, sec, off, rt, size = got
            note = "" if size == want else " (this tool expects %d: the card tool refuses it)" % want
            print("state_recover:   %-20s %6d bytes at %#x (%s + %#x)%s"
                  % (name, size, rt, sec, off, note))
            found[name] = data
            if (name, want) not in CHECK_ONLY:
                blobs.append((name, data))
        if "stage_no" in found:
            stage = struct.unpack("<i", found["stage_no"])[0]
            if stage != h["stage_no"]:
                raise Refused("stage_no in the state reads %d where its header says %d: %s is "
                              "not the build that wrote it" % (stage, h["stage_no"], table.what))
        img = found["gameSysMainSaveBuff"]
        status = struct.unpack("<12i", found["systemStatus"])
        stamp = STAMPS[h["disc"]]
        if img[:18] != stamp and img[:18] != b"\0" * 18:
            raise Refused("the save image does not start with the %s disc's version stamp: %s "
                          "is not the build that wrote the state" % (h["disc"], table.what))
        if status[3] == 0:
            raise Refused("the state holds no checkpoint since the game was started or loaded "
                          "(systemStatus[3] is 0): there is no progress to save")
        cp_stage = struct.unpack_from("<i", img, 18)[0]
        print("state_recover: the state: stage %d, play time %s (%d frames); the game's last "
              "checkpoint (its save image): stage %d"
              % (h["stage_no"], play_time(h["play_frames"], status[0] != 0), h["play_frames"],
                 cp_stage))
        side = a.side or (os.path.basename(a.state) + ".rescue")
        write_side(side, h, a.state, blobs)
        print("state_recover: wrote %s (%d globals)" % (side, len(blobs)))
    except Refused as e:
        print("state_recover: refused: %s" % e)
        return 1
    except OSError as e:
        print("state_recover: %s" % e)
        return 2
    if a.card is None:
        return 0
    tool = find_tool(a.tool)
    if tool is None:
        print("state_recover: no state_recover_tool found (build it, or give --tool); the side "
              "file is %s" % side)
        return 2
    cmd = [tool, side, "--card", a.card]
    if a.slot is not None:
        cmd += ["--slot", str(a.slot)]
    if a.overwrite:
        cmd.append("--overwrite")
    print("state_recover: running %s" % " ".join(cmd))
    sys.stdout.flush()
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
