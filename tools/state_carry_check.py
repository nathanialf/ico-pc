#!/usr/bin/env python3
"""state_carry_check.py: can a shipped program's save states carry over to a later version?

    python3 -I tools/state_carry_check.py check --binary PROGRAM --syms SYMBOLS
        --workdir DIR [--probe PATH | --cc CC] [--report FILE] [--list N]
    python3 -I tools/state_carry_check.py check ... --rules format2
    python3 -I tools/state_carry_check.py write-syms --binary ELF --out FILE
    python3 -I tools/state_carry_check.py selftest --workdir DIR --probe PATH --cc CC
        [--lld-clang CLANG] [--mingw-gcc GCC]
    python3 -I tools/state_carry_check.py self-image --probe PATH
    python3 -I tools/state_carry_check.py xbuild --a A --b B --a-map MAP --b-map MAP --workdir DIR

A save state holds addresses into the program that saved it.  A later version
loads it only when the saving build could NAME every such address: a function
(a unit of its symbol file), a variable, a registered block, the game's data
ranges, a start-up value of the game's data, or the content it points at
(port/platform/state_xbuild.h).  A word nothing names pins the state to its
build (issue 217: every Android state from v0.10.0 to v0.11.8).

`check` takes the SHIPPED program and the SHIPPED symbol file:
  Android   libmain.so (the unstripped one staged by tools/package_android.sh,
            or the APK's own) and libmain.syms.gz
  Windows   ico_pc_x64.exe (or build-host/win-x64/ico_pc.exe) and its link map
  Linux     ico_pc and ico_pc.map: reported, see "never" below
and lays the program out as its loader would (its loadable segments or
sections, its relocations applied), then asks the product's own code: a small
host program, state_carry_probe (port/platform/state_carry_probe.c), built
from the same port/platform/state_xbuild.c, state_syms.c and code_hash.c the
game links, maps that image, reads the symbol file with ico_symtab_parse and
hands each word to ico_xb_begin / ico_xb_word / ico_xb_finish exactly as a
save does: what ico_xb_finish counts as unnamed is unnamed.  The rules are
therefore never re-implemented here and cannot drift; the one thing this
script mirrors is the loader's view the game reads at run time (the image,
code and read-only ranges of port/platform/state_check.c
ico_state_image_range, the game data ranges of game_data_range.c and the
start-up words state_host.c snap_game_data copies), and the ctest
state_carry_image checks that mirror against the real function on the probe's
own executable.

The words asked about, by class:
  init    every initialised pointer in the game's data range (the start-up
          snapshot: the range's relocated words that point into the image).
          Every state holds all of them, so ONE unnamed init word pins EVERY
          state: the exit status is 1.
  table   every other relocated pointer in the image (constant tables,
          .data.rel.ro, the port's own initialised pointers): a state holds
          one when the game copies it into its memory.  Reported.
  code    every address the code computes (arm64 ADRP+ADD and ADR, x86-64
          RIP-relative operands, decoded by the probe with code_hash.c's own
          decoders over every unit): string literals and tables the code
          hands out.  Reported (the first --list of each class listed).
What it cannot see: addresses made only at run time (heap blocks, pointers
computed by arithmetic) and whether a given state holds a reported word.

Exit status: 0 every init word is named; 1 an init word is unnamed (each
listed: target section, nearest symbol + offset, the game variable holding
it); 2 the program or symbol file could not be judged (the reason printed);
3 the program's states never carry over by design (an image fixed below
4 GB: the Linux x86-64 program, ICO_XB_LOW_IMAGE).

`--rules format2` judges with a Python copy of the rules the releases that
write format 2 records (v0.10.0 to v0.11.8) applied, frozen: it answers what
an OLD release's states could do, never the current code.

`write-syms` writes an ELF's nm table the way tools/package_android.sh writes
libmain.syms (code symbols "addr T name", data symbols "addr size D name").
`selftest` compiles port/platform/test/state_carry_fixture.c in each layout
the compilers given can make and checks the verdicts (docs/port/TESTING.md).
`self-image` compares the probe's ico_state_image_range with this script's
reading of the probe's own file.
`xbuild` runs port/platform/test/state_carry_xbuild_test.c's two builds (a
state of each loaded by the other through the product's save and load).
"""
import argparse
import bisect
import gzip
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- ELF -------------------------------------------------------------------------

PT_LOAD, PT_DYNAMIC = 1, 2
PF_X, PF_W = 1, 2
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4
EM_X86_64, EM_AARCH64 = 62, 183
ARCH_X64, ARCH_A64 = 1, 2  # code_hash.h ICO_CODE_*
ICO_STATE_IMAGE_RO = 8  # state_reloc.h

# relocation types: value = target (link address), None = not an address in
# the image (TLS, IRELATIVE: the resolver's answer is not known here)
R_X64 = {8: "rel", 1: "abs", 6: "sym", 7: "sym"}
R_A64 = {1027: "rel", 257: "abs", 1025: "abs", 1026: "sym"}


class Fail(Exception):
    """the program or symbol file cannot be judged (exit 2)"""


class Elf:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.b = b = f.read()
        if b[:4] != b"\x7fELF" or b[4] != 2 or b[5] != 1:
            raise Fail("%s: not a little-endian ELF64 file" % path)
        (self.type, self.machine) = struct.unpack_from("<HH", b, 16)
        phoff, shoff = struct.unpack_from("<QQ", b, 32)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", b, 54)
        self.ph = []
        for i in range(phnum):
            t, fl, off, va, _pa, fs, ms, _al = struct.unpack_from("<IIQQQQQQ", b, phoff + i * phentsize)
            self.ph.append(dict(type=t, flags=fl, off=off, vaddr=va, filesz=fs, memsz=ms))
        self.sh = []
        if shoff and shnum:
            for i in range(shnum):
                nm, t, fl, a, off, sz, link, info, _al, es = struct.unpack_from(
                    "<IIQQQQIIQQ", b, shoff + i * shentsize)
                self.sh.append(dict(nm=nm, type=t, flags=fl, addr=a, off=off, size=sz, link=link,
                                    info=info, entsize=es))
            if shstrndx < len(self.sh):
                so = self.sh[shstrndx]["off"]
                for s in self.sh:
                    s["name"] = self.cstr(so + s["nm"])
        for s in self.sh:
            s.setdefault("name", "")
        self.by = {s["name"]: s for s in self.sh}
        loads = [p for p in self.ph if p["type"] == PT_LOAD and p["memsz"] != 0]
        if not loads:
            raise Fail("%s: no loadable segment" % path)
        self.link_lo = min(p["vaddr"] for p in loads)

    def cstr(self, o):
        e = self.b.index(b"\0", o)
        return self.b[o:e].decode("utf-8", "replace")

    def arch(self):
        return {EM_X86_64: ARCH_X64, EM_AARCH64: ARCH_A64}.get(self.machine, 0)

    def file_off(self, vaddr):
        for p in self.ph:
            if p["type"] == PT_LOAD and p["vaddr"] <= vaddr < p["vaddr"] + p["filesz"]:
                return p["off"] + vaddr - p["vaddr"]
        return None

    def symbols(self):
        """(name, value, size, shndx, type, bind) of .symtab, else .dynsym"""
        s = self.by.get(".symtab") or self.by.get(".dynsym")
        if s is None:
            return []
        st = self.sh[s["link"]]
        out = []
        for k in range(s["size"] // 24):
            nm, info, _other, shndx, value, size = struct.unpack_from("<IBBHQQ", self.b, s["off"] + k * 24)
            out.append((self.cstr(st["off"] + nm), value, size, shndx, info & 0xF, info >> 4))
        return out

    def dynamic(self):
        d = {}
        for p in self.ph:
            if p["type"] == PT_DYNAMIC:
                o = p["off"]
                for k in range(p["filesz"] // 16):
                    tag, val = struct.unpack_from("<qQ", self.b, o + 16 * k)
                    if tag == 0:
                        break
                    d.setdefault(tag, val)
        return d

    def relocations(self):
        """[(slot, target or None)]: every dynamic relocation's slot and the
        link address it receives (None: not an address in this image)"""
        d = self.dynamic()
        kinds = R_X64 if self.machine == EM_X86_64 else R_A64
        dsyms = None
        out = []

        def dynsym(i):
            nonlocal dsyms
            if dsyms is None:
                dsyms = []
                if 6 in d and 5 in d:
                    symo, stro = self.file_off(d[6]), self.file_off(d[5])
                    # the dynamic symbol count: up to the string table (lld and
                    # GNU ld both place .dynstr after .dynsym)
                    n = (d[5] - d[6]) // 24 if d[5] > d[6] else 0
                    for k in range(n):
                        nm, _info, _o, shndx, value, _size = struct.unpack_from("<IBBHQQ", self.b, symo + 24 * k)
                        dsyms.append((shndx, value))
            return dsyms[i] if i < len(dsyms) else (0, 0)

        def rela(addr, size):
            o = self.file_off(addr)
            if o is None:
                return
            for k in range(size // 24):
                off, info, add = struct.unpack_from("<QQq", self.b, o + 24 * k)
                typ, sym = info & 0xFFFFFFFF, info >> 32
                kind = kinds.get(typ)
                if kind == "rel":
                    out.append((off, add & 0xFFFFFFFFFFFFFFFF))
                elif kind in ("abs", "sym"):
                    shndx, value = dynsym(sym)
                    if sym == 0 or shndx == 0:
                        out.append((off, None))  # another library's
                    else:
                        out.append((off, (value + (add if kind == "abs" else 0)) & 0xFFFFFFFFFFFFFFFF))
                else:
                    out.append((off, None))

        if 7 in d:
            rela(d[7], d.get(8, 0))
        if 23 in d and d.get(20, 7) == 7:
            rela(d[23], d.get(2, 0))
        if 36 in d:  # DT_RELR: the slots hold their link address already
            o = self.file_off(d[36])
            where = 0
            for k in range(d.get(35, 0) // 8):
                e = struct.unpack_from("<Q", self.b, o + 8 * k)[0]
                if e & 1 == 0:
                    where = e
                    out.append((where, self.word(where)))
                    where += 8
                else:
                    bits = e >> 1
                    for i in range(63):
                        if bits >> i & 1:
                            out.append((where + 8 * i, self.word(where + 8 * i)))
                    where += 63 * 8
        return out

    def word(self, vaddr):
        o = self.file_off(vaddr)
        return struct.unpack_from("<Q", self.b, o)[0] if o is not None else 0


def elf_image(e):
    """state_check.c image_phdr over the file: offsets from the lowest
    segment (the image's start, lo = 0)"""
    lo = e.link_lo
    im = dict(hi=0, text_lo=None, text_hi=0, ro=[])
    for p in e.ph:
        if p["type"] != PT_LOAD or p["memsz"] == 0:
            continue
        a, z = p["vaddr"] - lo, p["vaddr"] + p["memsz"] - lo
        im["hi"] = max(im["hi"], z)
        if p["flags"] & (PF_W | PF_X) == 0 and len(im["ro"]) < ICO_STATE_IMAGE_RO:
            im["ro"].append((a, z))
        if p["flags"] & PF_X:
            im["text_lo"] = a if im["text_lo"] is None else min(im["text_lo"], a)
            im["text_hi"] = max(im["text_hi"], z)
    if im["text_lo"] is None:
        raise Fail("no executable segment")
    return im


def elf_flat(e):
    """the image's bytes as the loader maps them, from offset 0 = link_lo"""
    im = elf_image(e)
    buf = bytearray(im["hi"])
    for p in e.ph:
        if p["type"] == PT_LOAD and p["filesz"]:
            a = p["vaddr"] - e.link_lo
            buf[a:a + p["filesz"]] = e.b[p["off"]:p["off"] + p["filesz"]]
    return im, buf


def elf_game_ranges(e, syms):
    """game_data_range.c (ELF): ico_gdata and ico_gbss (__start_/__stop_),
    as offsets from the image's start"""
    lo = e.link_lo
    g = e.by.get("ico_gdata")
    b = e.by.get("ico_gbss")
    if g is not None:
        d0, d1 = g["addr"], g["addr"] + g["size"]
        b0, b1 = (b["addr"], b["addr"] + b["size"]) if b is not None else (d1, d1)
    else:
        named = {}
        for name, addr in syms.items:
            if name in ("__start_ico_gdata", "__stop_ico_gdata", "__start_ico_gbss", "__stop_ico_gbss"):
                named.setdefault(name, addr)
        if "__start_ico_gdata" not in named or "__stop_ico_gdata" not in named:
            raise Fail("no game data range: neither an ico_gdata section nor its __start_/__stop_ symbols")
        d0, d1 = named["__start_ico_gdata"], named["__stop_ico_gdata"]
        b0, b1 = named.get("__start_ico_gbss", d1), named.get("__stop_ico_gbss", d1)
    if d1 <= d0 or b1 < b0:
        raise Fail("the game data ranges are inconsistent")
    return [(d0 - lo, d1 - lo), (b0 - lo, b1 - lo)]


# --- PE ---------------------------------------------------------------------------

IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000


class Pe:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.b = b = f.read()
        if b[:2] != b"MZ":
            raise Fail("%s: not a PE file" % path)
        pe = struct.unpack_from("<I", b, 0x3C)[0]
        if b[pe:pe + 4] != b"PE\0\0":
            raise Fail("%s: no PE signature" % path)
        self.machine, nsec = struct.unpack_from("<HH", b, pe + 4)
        optsz = struct.unpack_from("<H", b, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", b, opt)[0] != 0x20B:
            raise Fail("%s: not a PE32+ program" % path)
        self.base = struct.unpack_from("<Q", b, opt + 24)[0]
        self.size_of_image = struct.unpack_from("<I", b, opt + 56)[0]
        self.size_of_headers = struct.unpack_from("<I", b, opt + 60)[0]
        ndir = struct.unpack_from("<I", b, opt + 108)[0]
        self.dirs = [struct.unpack_from("<II", b, opt + 112 + 8 * i) for i in range(min(ndir, 16))]
        self.sec = []
        so = opt + optsz
        for i in range(nsec):
            name = b[so + 40 * i:so + 40 * i + 8].rstrip(b"\0").decode("latin-1")
            vs, va, rs, rp = struct.unpack_from("<IIII", b, so + 40 * i + 8)
            ch = struct.unpack_from("<I", b, so + 40 * i + 36)[0]
            self.sec.append(dict(name=name, vsize=vs, va=va, rsize=rs, rptr=rp, ch=ch))
        self.link_lo = self.base

    def arch(self):
        return {0x8664: ARCH_X64, 0xAA64: ARCH_A64}.get(self.machine, 0)

    def relocations(self):
        """[(slot rva + base, target link address)] from the base relocations
        (IMAGE_REL_BASED_DIR64)"""
        out = []
        if len(self.dirs) <= 5 or self.dirs[5][1] == 0:
            return out
        rva, size = self.dirs[5]
        o = self.rva_off(rva)
        end = o + size
        while o + 8 <= end:
            page, bs = struct.unpack_from("<II", self.b, o)
            if bs < 8:
                break
            for k in range((bs - 8) // 2):
                ent = struct.unpack_from("<H", self.b, o + 8 + 2 * k)[0]
                if ent >> 12 == 10:
                    slot = page + (ent & 0xFFF)
                    so = self.rva_off(slot)
                    out.append((self.base + slot, struct.unpack_from("<Q", self.b, so)[0] if so is not None else None))
            o += bs
        return out

    def rva_off(self, rva):
        for s in self.sec:
            if s["va"] <= rva < s["va"] + min(s["rsize"], max(s["vsize"], s["rsize"])):
                return s["rptr"] + rva - s["va"]
        if rva < self.size_of_headers:
            return rva
        return None


def pe_image(p):
    """state_check.c's Windows ico_state_image_range over the file"""
    im = dict(hi=p.size_of_headers, text_lo=None, text_hi=0, ro=[])
    rwx = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE
    for s in p.sec:
        if s["ch"] & IMAGE_SCN_MEM_DISCARDABLE:
            continue
        im["hi"] = max(im["hi"], s["va"] + s["vsize"])
        if s["ch"] & rwx == IMAGE_SCN_MEM_READ and len(im["ro"]) < ICO_STATE_IMAGE_RO and s["vsize"] != 0:
            im["ro"].append((s["va"], s["va"] + s["vsize"]))
        if s["ch"] & IMAGE_SCN_MEM_EXECUTE:
            a, z = s["va"], s["va"] + s["vsize"]
            im["text_lo"] = a if im["text_lo"] is None else min(im["text_lo"], a)
            im["text_hi"] = max(im["text_hi"], z)
    if im["text_lo"] is None or im["text_lo"] == 0:
        raise Fail("no executable section")
    return im


def pe_flat(p):
    im = pe_image(p)
    buf = bytearray(max(im["hi"], p.size_of_image))
    buf[:p.size_of_headers] = p.b[:p.size_of_headers]
    for s in p.sec:
        n = min(s["rsize"], s["vsize"]) if s["vsize"] else s["rsize"]
        if n and s["va"] + n <= len(buf):
            buf[s["va"]:s["va"] + n] = p.b[s["rptr"]:s["rptr"] + n]
    return im, buf


def pe_game_ranges(p, syms):
    """game_data_range.c (PE): the one-byte brackets in .data$ico_a,
    .data$ico_n and .data$ico_z of game_data_range.c's object, found as
    input sections in the link map (they are statics: no symbol line)"""
    found = {"a": [], "n": [], "z": []}
    for name, addr in syms.sections:
        m = re.match(r"^\.data\$ico_([anz]) (.*)$", name)
        if m:
            found[m.group(1)].append((addr, m.group(2)))
    br = {}
    for k, lst in found.items():
        own = [a for a, obj in lst if "game_data_range" in obj]
        if own:
            br[k] = own[0]
        elif len(lst) == 1:
            br[k] = lst[0][0]  # a program of one object (the self-test's)
    if set(br) != {"a", "n", "z"}:
        raise Fail("the link map does not show game_data_range.c's .data$ico_a/_n/_z brackets")
    lo = p.base
    return [(br["a"] - lo, br["n"] - lo), (br["n"] - lo, br["z"] - lo)]


# --- the symbol file (for the anchor and the report's names only: the probe
# reads it with ico_symtab_parse itself) ------------------------------------------

class SymFile:
    NM3 = re.compile(r"^([0-9A-Fa-f]+) ([A-Za-z]) (\S+)$")
    NM4 = re.compile(r"^([0-9A-Fa-f]+) ([0-9A-Fa-f]+) ([A-Za-z]) (\S+)$")
    GNU_SYM = re.compile(r"^\s+0x([0-9A-Fa-f]+)\s+(\S+)$")
    GNU_SEC = re.compile(r"^ (\S+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(\S.*)$")
    GNU_SEC2 = re.compile(r"^\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(\S.*)$")
    LLD = re.compile(r"^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(\S.*)$")

    def __init__(self, path):
        with open(path, "rb") as f:
            raw = f.read()
        if raw[:2] == b"\x1f\x8b":
            raw = gzip.decompress(raw)
        self.text = raw
        self.items = []  # (name, link address): symbols
        self.sizes = {}  # (name, link address) -> size, when the file gives one
        self.sections = []  # (input section name + object, link address)
        self.section_sizes = {}
        pending = None
        for line in raw.decode("utf-8", "replace").splitlines():
            m = self.NM3.match(line) or None
            if m:
                self.items.append((m.group(3), int(m.group(1), 16)))
                continue
            m = self.NM4.match(line)
            if m:
                self.items.append((m.group(4), int(m.group(1), 16)))
                self.sizes[(m.group(4), int(m.group(1), 16))] = int(m.group(2), 16)
                continue
            m = self.LLD.match(line)
            if m:
                rest = m.group(5).strip()
                if " " not in rest and not rest.startswith("."):
                    self.items.append((rest, int(m.group(1), 16)))
                continue
            m = self.GNU_SEC.match(line)
            if m:
                key = (m.group(1) + " " + m.group(4), int(m.group(2), 16))
                self.sections.append(key)
                self.section_sizes[key] = int(m.group(3), 16)
                pending = None
                continue
            if re.match(r"^ \S+$", line):
                pending = line.strip()
                continue
            m = self.GNU_SEC2.match(line)
            if m and pending is not None:
                key = (pending + " " + m.group(3), int(m.group(1), 16))
                self.sections.append(key)
                self.section_sizes[key] = int(m.group(2), 16)
                pending = None
                continue
            m = self.GNU_SYM.match(line)
            if m:
                self.items.append((m.group(2), int(m.group(1), 16)))
                pending = None

    def find(self, name):
        for n, a in self.items:
            if n == name or n.lstrip("_") == name.lstrip("_"):
                if a:
                    return a
        return None

    def nearest_table(self):
        allp = sorted({(a, n, self.sizes.get((n, a))) for n, a in self.items if a})
        return [a for a, _, _ in allp], [n for _, n, _ in allp], [z for _, _, z in allp]


def nearest(table, addr):
    """the symbol holding addr (+ offset), or the nearest one below it and
    how far past its end addr lies"""
    keys, names, sizes = table
    k = bisect.bisect_right(keys, addr) - 1
    if k < 0:
        return "no symbol below it"
    off = addr - keys[k]
    if sizes[k] is not None and off >= max(sizes[k], 1):
        return "no symbol (0x%x past the end of %s)" % (off - sizes[k], names[k])
    return "%s+0x%x" % (names[k], off)


# --- the probe -------------------------------------------------------------------

PROBE_SOURCES = [
    "port/platform/state_carry_probe.c", "port/platform/state_xbuild.c",
    "port/platform/state_syms.c", "port/platform/code_hash.c", "port/render/xxh3.c",
    "port/third_party/miniz/miniz.c", "port/third_party/miniz/miniz_tdef.c",
    "port/third_party/miniz/miniz_tinfl.c",
]


def build_probe(cc, workdir):
    """the probe from the repo's own sources with the host compiler (the
    packaging hosts and CI jobs without a CMake host build)"""
    out = os.path.join(workdir, "state_carry_probe")
    cmd = [cc, "-std=gnu11", "-O1", "-w", "-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1",
           "-I" + os.path.join(ROOT, "port/platform"), "-I" + os.path.join(ROOT, "port/render"),
           "-I" + os.path.join(ROOT, "port/third_party/miniz")]
    cmd += [os.path.join(ROOT, s) for s in PROBE_SOURCES] + ["-o", out]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise Fail("the probe did not compile (%s):\n%s" % (" ".join(cmd), r.stdout.decode("utf-8", "replace")))
    return out


def load_program(path, syms):
    with open(path, "rb") as f:
        head = f.read(4)
    if head == b"\x7fELF":
        e = Elf(path)
        im, buf = elf_flat(e)
        relocs = e.relocations()
        return dict(kind="elf", obj=e, im=im, buf=buf, relocs=relocs, link_lo=e.link_lo,
                    arch=e.arch(), game=elf_game_ranges(e, syms), fixed=e.type == 2, sections=elf_sections(e))
    if head[:2] == b"MZ":
        p = Pe(path)
        im, buf = pe_flat(p)
        return dict(kind="pe", obj=p, im=im, buf=buf, relocs=p.relocations(), link_lo=p.base, arch=p.arch(),
                    game=pe_game_ranges(p, syms), fixed=False, sections=pe_sections(p))
    raise Fail("%s: neither ELF nor PE" % path)


def elf_sections(e):
    return sorted((s["addr"] - e.link_lo, s["addr"] + s["size"] - e.link_lo, s["name"])
                  for s in e.sh if s["flags"] & SHF_ALLOC and s["size"])


def pe_sections(p):
    return sorted((s["va"], s["va"] + s["vsize"], s["name"]) for s in p.sec if s["vsize"])


def section_of(prog, off):
    for a, z, n in prog["sections"]:
        if a <= off < z:
            return "%s+0x%x" % (n, off - a)
    return "(no section)"


def collect_words(prog):
    """the init and table classes: {target offset: [holder slot offsets]}"""
    im, buf, lo = prog["im"], prog["buf"], prog["link_lo"]
    g0, g1 = prog["game"][0]
    init, table = {}, {}
    slots = []
    if not prog["relocs"]:
        # a program without relocations is fixed at its link address: below
        # 4 GB that is the verdict NEVER (check); above it nothing here says
        # which of its words are pointers
        raise Fail("the program has no relocations (a fixed address): its pointers cannot be told "
                   "from its numbers here")
    for slot, target in prog["relocs"]:
        s = slot - lo
        if s < 0 or s + 8 > len(buf):
            continue
        if target is None:
            struct.pack_into("<Q", buf, s, 0)
            continue
        v = target - lo
        struct.pack_into("<Q", buf, s, v & 0xFFFFFFFFFFFFFFFF)
        slots.append(s)
        if not (0 <= v <= im["hi"]):
            continue
        if g0 <= s < g1 and (s - g0) % 8 == 0:
            init.setdefault(v, []).append(s)
        else:
            table.setdefault(v, []).append(s)
    return init, table, slots


def run_probe(probe, prog, syms_path, init, table, workdir, list_n):
    os.makedirs(workdir, exist_ok=True)
    img = os.path.join(workdir, "image.bin")
    with open(img, "wb") as f:
        f.write(prog["buf"])
    rel = os.path.join(workdir, "relocs.bin")
    with open(rel, "wb") as f:
        f.write(struct.pack("<%dQ" % len(prog["slots"]), *prog["slots"]))
    text = os.path.join(workdir, "syms.txt")
    with open(text, "wb") as f:
        f.write(prog["symtext"])
    words = os.path.join(workdir, "words.txt")
    with open(words, "w") as f:
        for cls, d in (("init", init), ("table", table)):
            for v in sorted(d):
                f.write("%s %x\n" % (cls, v))
    im = prog["im"]
    desc = os.path.join(workdir, "probe.txt")
    with open(desc, "w") as f:
        f.write("image %s %x\n" % (img, len(prog["buf"])))
        f.write("range %x %x\n" % (0, im["hi"]))
        f.write("text %x %x\n" % (im["text_lo"], im["text_hi"]))
        for a, z in im["ro"]:
            f.write("ro %x %x\n" % (a, z))
        for i, (a, z) in enumerate(prog["game"]):
            f.write("game %d %x %x\n" % (i, a, z))
        f.write("relocs %s %d\n" % (rel, len(prog["slots"])))
        f.write("syms %s\n" % text)
        f.write("arch %d\n" % prog["arch"])
        f.write("anchor ico_diag_init %x\n" % prog["anchor"])
        f.write("words %s\n" % words)
        f.write("list %d\n" % list_n)
    r = subprocess.run([probe, desc], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    if r.returncode != 0:
        raise Fail("the probe failed (status %d):\n%s" % (r.returncode, out))
    res = dict(classes={}, unnamed={"init": [], "table": [], "code": []}, kinds={}, lines=out)
    for line in out.splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "class":
            res["classes"][t[1]] = dict(words=int(t[3]), named=int(t[5]), unnamed=int(t[7]),
                                        overflow=int(t[9]))
        elif t[0] == "unnamed":
            res["unnamed"][t[1]].append(int(t[2], 16))
        elif t[0] == "kind":
            res["kinds"].setdefault(t[1], {})[t[2]] = int(t[3])
        elif t[0] == "symbols":
            res["table_line"] = line
    if set(res["classes"]) != {"init", "table", "code"}:
        raise Fail("the probe's answer lacks a class:\n" + out)
    return res


# --- the v0.11.8 rules, frozen (nm tables: what old Android releases did) ----------

def emulate_v0118(prog, syms, init, table):
    """state_xbuild.c ico_xb_finish and state_syms.c ico_symtab_parse as
    v0.10.0 to v0.11.8 shipped them, over the words alone (offsets from the
    image's start).  Exact for nm tables (derived from and checked against
    the v0.11.8 sources and three Android states' headers); for a GNU ld map
    the units are its symbols and input sections in the code (the code's
    every byte lies in an input section, so the init class is exact there
    too: an init word outside the code is a start-up value by definition)"""
    im, buf, lo = prog["im"], prog["buf"], prog["link_lo"]
    tlo, thi, hi = im["text_lo"], im["text_hi"], im["hi"]
    game = prog["game"]
    items = []
    is_map = False
    for seq, line in enumerate(prog["symtext"].decode("utf-8", "replace").splitlines()):
        t = line.split()
        if len(t) == 3 and len(t[1]) == 1 and t[1] in "TtWwDdBbRrVvSsGg" and re.match(r"^[0-9A-Fa-f]+$", t[0]):
            items.append((int(t[0], 16) - lo, 0, 0, t[2], seq))
        elif len(t) == 4 and len(t[2]) == 1 and t[2] in "TtWwDdBbRrVvSsGg" and re.match(r"^[0-9A-Fa-f]+$", t[0]):
            items.append((int(t[0], 16) - lo, int(t[1], 16), 1, t[3], seq))
        elif t:
            is_map = True
    if is_map:
        items = [(a - lo, 0, 0, n, k) for k, (n, a) in enumerate(syms.items)]
        k0 = len(items)
        sec = re.compile(r"^ (\S+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+(\S.*)$")
        for k, (n, a) in enumerate(syms.sections):
            if tlo <= a - lo < thi:
                size = syms.section_sizes.get((n, a), 0)
                items.append((a - lo, size, 1, n.replace(" ", ":"), k0 + k))
        del sec
    items = [it for it in items if it[0] + lo != 0]
    code, dat = [], []
    for a, s, sized, nm, seq in items:
        if a < 0 or a >= hi:
            continue
        rec = (a, seq, a + s, sized, nm)
        if tlo <= a < thi:
            code.append(rec)
        elif any(g0 <= a < g1 for g0, g1 in game):
            pass
        else:
            dat.append(rec)

    def dedupe(lst):
        lst.sort()
        out = []
        for r in lst:
            if out and out[-1][0] == r[0]:
                continue
            out.append(r)
        return out
    code, dat = dedupe(code), dedupe(dat)
    units = []
    for i, r in enumerate(code):
        end = code[i + 1][0] if i + 1 < len(code) else thi
        if r[3] and r[0] < r[2] < end:
            end = r[2]
        end = min(end, thi)
        if end > r[0]:
            units.append((r[0], end - r[0]))
    data = []
    for i, r in enumerate(dat):
        end = dat[i + 1][0] if i + 1 < len(dat) else hi
        if r[3]:
            end = r[2]
        if end >= r[0]:
            data.append((r[0], end - r[0], r[4]))
    count = {}
    for d in data:
        count[d[2]] = count.get(d[2], 0) + 1
    us, ds = [u[0] for u in units], [d[0] for d in data]
    g0, g1 = game[0]
    snap = set()
    for s in range(g0, g1 - 7, 8):
        v = struct.unpack_from("<Q", buf, s)[0]
        if s in prog["slotset"] and 0 <= v <= hi:
            snap.add(v)

    def c_string(w):
        def ro(x):
            return any(a <= x and x + 1 <= z for a, z in im["ro"])
        if not ro(w):
            return False
        for i in range(24):
            if not ro(w + i):
                return False
            c = buf[w + i]
            if c == 0:
                return True
            if c < 0x20 and c not in (9, 10, 13):
                return False
        return False

    def named(w):
        if tlo <= w < thi:
            k = bisect.bisect_right(us, w) - 1
            return k >= 0 and w - units[k][0] < units[k][1]
        if any(a <= w <= z for a, z in game if a < z):
            return True
        k = bisect.bisect_right(ds, w) - 1
        if k >= 0 and w - data[k][0] <= data[k][1] and count[data[k][2]] == 1:
            return True
        return w in snap or c_string(w)

    res = dict(classes={}, unnamed={"init": [], "table": [], "code": []}, kinds={}, lines="")
    for cls, d in (("init", init), ("table", table)):
        bad = [w for w in sorted(d) if not named(w)]
        res["classes"][cls] = dict(words=len(d), named=len(d) - len(bad), unnamed=len(bad), overflow=0)
        res["unnamed"][cls] = bad
    res["classes"]["code"] = None
    return res


# --- check -------------------------------------------------------------------------

def check(a):
    syms = SymFile(a.syms)
    prog = load_program(a.binary, syms)
    prog["symtext"] = syms.text
    anchor = syms.find("ico_diag_init")
    if anchor is None:
        raise Fail("the symbol file does not name ico_diag_init (the anchor)")
    prog["anchor"] = anchor - prog["link_lo"]
    if not prog["arch"]:
        raise Fail("the program's machine is neither x86-64 nor arm64")
    out = []
    say = out.append
    im = prog["im"]
    say("state_carry_check: %s (%s, %s) with %s" % (a.binary, prog["kind"], {1: "x86-64", 2: "arm64"}[prog["arch"]],
                                                    a.syms))
    say("  image 0x%x bytes; code [0x%x, 0x%x); read-only data %s; game data [0x%x, 0x%x), bss [0x%x, 0x%x)" % (
        im["hi"], im["text_lo"], im["text_hi"],
        ", ".join("[0x%x, 0x%x)" % r for r in im["ro"]) or "none (inside the code range)",
        prog["game"][0][0], prog["game"][0][1], prog["game"][1][0], prog["game"][1][1]))
    if prog["fixed"] and prog["link_lo"] < 0x100000000:
        say("  verdict: NEVER - the program is fixed at 0x%x, below 4 GB: its states carry the "
            "low-image flag and load only into the build that saved them (state_xbuild.h ICO_XB_LOW_IMAGE)"
            % prog["link_lo"])
        emit(a, out)
        return 3
    init, table, slots = collect_words(prog)
    prog["slots"] = slots
    prog["slotset"] = set(slots)
    nwords = sum(len(v) for v in init.values())
    say("  init: %d initialised pointers in the game data, %d distinct targets; table: %d other relocated "
        "pointers, %d distinct targets" % (nwords, len(init), sum(len(v) for v in table.values()), len(table)))
    if a.rules == "format2":
        res = emulate_v0118(prog, syms, init, table)
        say("  rules: the format 2 releases' copy in this script (frozen; the code class not judged)")
    else:
        probe = a.probe
        workdir = os.path.abspath(a.workdir)
        os.makedirs(workdir, exist_ok=True)
        if probe is None:
            probe = build_probe(a.cc, workdir)
        res = run_probe(probe, prog, a.syms, init, table, workdir, a.list)
        say("  rules: this tree's port/platform/state_xbuild.c through %s" % probe)
        if "table_line" in res:
            say("  " + res["table_line"])
    names = syms.nearest_table()
    lo = prog["link_lo"]
    for cls in ("init", "table", "code"):
        c = res["classes"].get(cls)
        if c is None:
            say("  %-5s not judged" % cls)
            continue
        kinds = res["kinds"].get(cls, {})
        say("  %-5s %6d targets: %6d named, %6d unnamed%s%s" % (
            cls, c["words"], c["named"], c["unnamed"], "  (OVERFLOW: a batch passed the record's bounds)"
            if c["overflow"] else "",
            ("  [" + ", ".join("%s %d" % kv for kv in sorted(kinds.items())) + "]") if kinds else ""))
        holders = init if cls == "init" else table if cls == "table" else {}
        shown = res["unnamed"][cls] if cls == "init" else res["unnamed"][cls][:a.list]
        if cls == "init" and c["unnamed"]:
            say("      (%d words of the game data hold them)" % sum(len(init.get(w, ())) for w in res["unnamed"][cls]))
        for w in shown:
            line = "      unnamed 0x%x in %s, %s%s" % (w + lo, section_of(prog, w), nearest(names, w + lo),
                                                   preview(prog["buf"], w))
            if holders.get(w):
                hs = holders[w]
                line += "; held by %s" % ", ".join(nearest(names, s + lo) for s in hs[:4])
                if len(hs) > 4:
                    line += " and %d more" % (len(hs) - 4)
            say(line)
        if cls != "init" and c["unnamed"] > len(shown):
            say("      ... %d more not listed (--list)" % (c["unnamed"] - len(shown)))
    bad = res["classes"]["init"]["unnamed"]
    over = any(c and c["overflow"] for c in res["classes"].values())
    if over:
        say("  verdict: UNJUDGED - a probe batch overflowed the record's bounds")
        emit(a, out)
        return 2
    if bad:
        say("  verdict: PINNED - %d initialised pointer%s of the game data %s no name: every state this "
            "program saves holds %s and loads only into this build" % (bad, "s" if bad != 1 else "",
                                                                       "have" if bad != 1 else "has",
                                                                       "them" if bad != 1 else "it"))
        emit(a, out)
        return 1
    say("  verdict: CARRIES - every initialised pointer of the game data is named")
    emit(a, out)
    return 0


def preview(buf, w):
    """the bytes at w when they are a C string (the usual unnamed target)"""
    if not 0 <= w < len(buf):
        return ""
    raw = bytes(buf[w:w + 40])
    z = raw.find(b"\0")
    if z < 0:
        return ' "%s"...' % raw[:24].decode("latin-1").encode("unicode_escape").decode("ascii")
    if z == 0 or any(c < 0x20 and c not in (9, 10, 13) for c in raw[:z]):
        return ""
    return ' "%s" (%d bytes)' % (raw[:z].decode("latin-1").encode("unicode_escape").decode("ascii"), z + 1)


def emit(a, out):
    text = "\n".join(out) + "\n"
    sys.stdout.write(text)
    if a.report:
        with open(a.report, "w") as f:
            f.write(text)


# --- write-syms (tools/package_android.sh's table from an ELF's .symtab) -------------

def nm_letter(e, name, value, size, shndx, typ, bind):
    """GNU/llvm nm's type letter for the classes package_android.sh keeps"""
    if shndx == 0 or shndx >= 0xFF00 and shndx != 0xFFF1 or typ in (3, 4, 6):  # SECTION, FILE, TLS
        return None
    if bind == 2:  # weak
        return "W" if typ == 2 else "V"
    if shndx == 0xFFF1:
        return None
    s = e.sh[shndx]
    local = bind == 0
    if s["flags"] & SHF_EXECINSTR:
        c = "T"
    elif s["type"] == 8:  # NOBITS
        c = "B"
    elif s["flags"] & SHF_WRITE:
        c = "D"
    elif s["flags"] & SHF_ALLOC:
        c = "R"
    else:
        return None
    return c.lower() if local else c


def write_syms(a):
    e = Elf(a.binary)
    if ".symtab" not in e.by:
        raise Fail("%s has no .symtab (an unstripped program is needed)" % a.binary)
    code, data = [], []
    for name, value, size, shndx, typ, bind in e.symbols():
        if not name or name.startswith("$"):
            continue
        c = nm_letter(e, name, value, size, shndx, typ, bind)
        if c is None:
            continue
        if c in "TtWw":
            code.append((value, c, name))
        if c in "DdBbRrVv":
            data.append((value, size, c, name))
    code.sort(key=lambda r: (r[0], r[2]))
    data.sort(key=lambda r: (r[0], r[3]))
    with open(a.out, "w") as f:
        for v, c, n in code:
            f.write("%016x %s %s\n" % (v, c, n))
        for v, s, c, n in data:
            f.write("%016x %016x %s %s\n" % (v, s, c, n))
    return 0


# --- selftest ------------------------------------------------------------------------

FIXTURE = "port/platform/test/state_carry_fixture.c"
FIXTURE_TARGETS = 6


def sh(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise Fail("%s:\n%s" % (" ".join(cmd), r.stdout.decode("utf-8", "replace")))
    return r.stdout.decode("utf-8", "replace")


def selftest(a):
    """each layout the given compilers make, its verdict and its counts"""
    wd = os.path.abspath(a.workdir)
    if os.path.isdir(wd):
        shutil.rmtree(wd)
    os.makedirs(wd)
    src = os.path.join(ROOT, FIXTURE)
    cases = []
    # ELF, the host compiler: read-only data in its own segment, then inside
    # the executable one (GNU ld: -z noseparate-code puts .rodata after
    # .text in the R+E segment; lld: --no-rosegment puts it before, as the
    # NDK links libmain.so below API 30)
    # the file's order kept: gcc needs -fno-toplevel-reorder, clang keeps it
    # and has no such option
    ver = subprocess.run([a.cc, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    gcc = ["-O1", "-fPIC"] + ([] if b"clang" in ver.stdout else ["-fno-toplevel-reorder"])
    cases.append(("elf-separate", [a.cc] + gcc, ["-shared", "-Wl,-z,separate-code"], "syms"))
    cases.append(("elf-merged", [a.cc] + gcc, ["-shared", "-Wl,-z,noseparate-code"], "syms"))
    if a.lld_clang:
        for tgt in ("aarch64-linux-android26", "x86_64-linux-gnu"):
            cc = [a.lld_clang, "--target=" + tgt, "-O1", "-fPIC"]
            ld = ["-fuse-ld=lld", "-nostdlib", "-shared"]
            cases.append(("lld-%s-rosegment" % tgt.split("-")[0], cc, ld + ["-Wl,--rosegment"], "syms"))
            cases.append(("lld-%s-no-rosegment" % tgt.split("-")[0], cc, ld + ["-Wl,--no-rosegment"], "syms"))
    if a.mingw_gcc:
        cases.append(("pe", [a.mingw_gcc, "-O1", "-fno-toplevel-reorder", "-ffunction-sections"],
                      ["-nostartfiles", "-Wl,-e,carry_entry", "-Wl,--image-base,0x140000000",
                       "-Wl,--dynamicbase", "-Wl,--high-entropy-va"], "map"))
    # the layout each case is for: read-only data in a segment of its own, or
    # none (inside the executable segment)
    separate = {"elf-separate": True, "elf-merged": False, "lld-aarch64-rosegment": True,
                "lld-aarch64-no-rosegment": False, "lld-x86_64-rosegment": True,
                "lld-x86_64-no-rosegment": False}
    ran = 0
    failures = []
    for name, cc, ld, symkind in cases:
        d = os.path.join(wd, name)
        os.makedirs(d)
        binary = os.path.join(d, "fixture.exe" if name == "pe" else "fixture.so")
        obj = os.path.join(d, "fixture.o")
        mapf = os.path.join(d, "fixture.map")
        # compiled into the case's own folder, then linked (no compiler
        # temporary: the map names this object)
        sh(cc + ["-c", "-o", obj, src])
        driver = cc[:2] if name.startswith("lld") else cc[:1]  # the compiler (and its --target)
        sh(driver + ld + ["-o", binary, obj, "-Wl,-Map=" + mapf])
        if name in separate:
            im = elf_image(Elf(binary))
            text = Elf(binary).by.get(".rodata") or {}
            print("selftest %s: read-only segments %d" % (name, len(im["ro"])))
            if bool(im["ro"]) != separate[name] or (not separate[name] and text and not (
                    im["text_lo"] <= text.get("addr", 0) < im["text_hi"])):
                failures.append("%s: the link did not make the layout the case is for (%d read-only segments)" % (
                    name, len(im["ro"])))
        for neg in (False, True):
            syms = os.path.join(d, "fixture.syms" if symkind == "syms" else "fixture.map")
            if symkind == "syms":
                write_syms(argparse.Namespace(binary=binary, out=syms))
            if neg:
                syms = negative_syms(binary, syms, symkind, d)
            ns = argparse.Namespace(binary=binary, syms=syms, probe=a.probe, cc=a.cc, workdir=os.path.join(
                d, "neg" if neg else "pos"), report=os.path.join(d, "neg.txt" if neg else "pos.txt"), list=64,
                rules=None)
            status = check(ns)
            with open(ns.report) as f:
                rep = f.read()
            want = 1 if neg else 0
            m = re.search(r"init\s+(\d+) targets:\s+(\d+) named,\s+(\d+) unnamed", rep)
            nt = int(m.group(1)) if m else -1
            nu = int(m.group(3)) if m else -1
            # the fixture's game table (state_carry_fixture.c): 7 pointers, 6
            # targets (two hold one text); the negative case leaves exactly
            # one of them unnamed (carry_hidden)
            if status != want or nt != FIXTURE_TARGETS or nu != (1 if neg else 0):
                failures.append("%s%s: status %d (want %d), %d init targets (want %d), %d unnamed (want %d)" % (
                    name, " (negative)" if neg else "", status, want, nt, FIXTURE_TARGETS, nu, 1 if neg else 0))
            print("selftest %s%s: status %d, %d init targets, %d unnamed" % (
                name, " (negative)" if neg else "", status, nt, nu))
        ran += 1
    print("selftest: %d layouts checked" % ran)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    return 0


def negative_syms(binary, syms, symkind, d):
    """the symbol file without carry_hidden, and carry_first bounded by its
    own size (an nm -S line; in a GNU ld map its own input section, the
    fixture being compiled with -ffunction-sections): the game table's
    pointer to carry_hidden then lies in code no unit holds, between two
    units, inside the code range however it is derived (the one thing a
    save cannot name)"""
    with open(syms, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    sf = SymFile(syms)
    first, hid = sf.find("carry_first"), sf.find("carry_hidden")
    later = [sf.find("ico_diag_init"), sf.find("ico_diag_log")]
    if first is None or hid is None or not first < hid or any(x is None or x <= hid for x in later):
        raise Fail("the fixture's layout is not as the negative case needs (carry_first < carry_hidden < "
                   "ico_diag_init, ico_diag_log) in %s" % binary)
    keep = []
    if symkind == "syms":
        size = None
        for name, value, sz, _shndx, _typ, _bind in Elf(binary).symbols():
            if name == "carry_first" and value == first:
                size = sz
        if not size or first + size > hid:
            raise Fail("carry_first's size does not end before carry_hidden in %s" % binary)
        for line in text.splitlines():
            t = line.split()
            if len(t) == 3 and t[2] == "carry_hidden":
                continue
            if len(t) == 3 and t[2] == "carry_first":
                line = "%s %016x %s %s" % (t[0], size, t[1], t[2])
            keep.append(line)
    else:
        dropped = 0
        lines = text.splitlines()
        k = 0
        while k < len(lines):
            line = lines[k]
            t = line.split()
            k += 1
            if len(t) == 2 and t[1] == "carry_hidden" and t[0].startswith("0x"):
                dropped += 1
                continue
            if t and t[0] in (".text$carry_hidden", ".text.carry_hidden"):
                # its input section; a long name stands alone, its numbers on
                # the next line
                if len(t) == 1 and k < len(lines) and SymFile.GNU_SEC2.match(lines[k]):
                    k += 1
                dropped += 1
                continue
            keep.append(line)
        if dropped != 2:
            raise Fail("the map does not show carry_hidden's symbol and input section once each (%d lines)" % dropped)
    out = os.path.join(d, "negative." + symkind)
    with open(out, "w") as f:
        f.write("\n".join(keep) + "\n")
    return out


# --- xbuild ----------------------------------------------------------------------------

def xbuild(a):
    """port/platform/test/state_carry_xbuild_test.c's two builds: A's state
    into B and B's into A, each program's own layout line checked"""
    os.makedirs(a.workdir, exist_ok=True)
    steps = [
        ("A saves", a.a, "save", "a.state", a.a_map, "layout separate"),
        ("B loads A's state", a.b, "load", "a.state", a.b_map, "layout merged"),
        ("B saves", a.b, "save", "b.state", a.b_map, "layout merged"),
        ("A loads B's state", a.a, "load", "b.state", a.a_map, "layout separate"),
    ]
    bad = 0
    for what, prog, verb, state, mapf, layout in steps:
        r = subprocess.run([prog, verb, os.path.join(a.workdir, state), mapf], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        out = r.stdout.decode("utf-8", "replace")
        print("== %s (status %d)" % (what, r.returncode))
        print(out.rstrip())
        ok = r.returncode == 0 and any(line.startswith(layout + " ") for line in out.splitlines())
        if verb == "load":
            ok = ok and "all 15 words translated" in out
        if not ok:
            print("FAIL %s" % what)
            bad += 1
    print("xbuild: %s" % ("every word of each build's state translated into the other build" if not bad
                          else "%d of 4 steps failed" % bad))
    return 1 if bad else 0


# --- self-image ------------------------------------------------------------------------

def self_image(a):
    """ico_state_image_range (the probe's own process) against this script's
    reading of the probe's file: the loader mirror's drift check"""
    r = subprocess.run([a.probe, "--self-image"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    if r.returncode != 0:
        print(out)
        print("FAIL: the probe's --self-image failed")
        return 1
    got = {}
    for line in out.splitlines():
        t = line.split()
        if t and t[0] in ("range", "text"):
            got[t[0]] = (int(t[1], 16), int(t[2], 16))
        elif t and t[0] == "ro":
            got.setdefault("ro", []).append((int(t[1], 16), int(t[2], 16)))
    exe = None
    for line in out.splitlines():
        if line.startswith("exe "):
            exe = line[4:].strip()
    with open(exe or a.probe, "rb") as f:
        head = f.read(4)
    im = elf_image(Elf(exe or a.probe)) if head == b"\x7fELF" else pe_image(Pe(exe or a.probe))
    want = {"range": (0, im["hi"]), "text": (im["text_lo"], im["text_hi"]), "ro": im["ro"]}
    print(out.rstrip())
    print("file: range %x %x text %x %x ro %s" % (0, im["hi"], im["text_lo"], im["text_hi"],
                                                 " ".join("%x-%x" % r for r in im["ro"])))
    if got.get("range") != want["range"] or got.get("text") != want["text"] or \
            sorted(got.get("ro", [])) != sorted(want["ro"]):
        print("FAIL: tools/state_carry_check.py's image rule differs from ico_state_image_range")
        return 1
    print("self-image: the file's image, code and read-only ranges equal ico_state_image_range's")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--binary", required=True)
    c.add_argument("--syms", required=True)
    c.add_argument("--workdir", default=None)
    c.add_argument("--probe", default=None)
    c.add_argument("--cc", default="cc")
    c.add_argument("--report", default=None)
    c.add_argument("--list", type=int, default=64)
    c.add_argument("--rules", choices=["format2"], default=None)
    w = sub.add_parser("write-syms")
    w.add_argument("--binary", required=True)
    w.add_argument("--out", required=True)
    s = sub.add_parser("selftest")
    s.add_argument("--workdir", required=True)
    s.add_argument("--probe", required=True)
    s.add_argument("--cc", default="cc")
    s.add_argument("--lld-clang", default=None)
    s.add_argument("--mingw-gcc", default=None)
    i = sub.add_parser("self-image")
    i.add_argument("--probe", required=True)
    x = sub.add_parser("xbuild")
    x.add_argument("--a", required=True)
    x.add_argument("--b", required=True)
    x.add_argument("--a-map", required=True)
    x.add_argument("--b-map", required=True)
    x.add_argument("--workdir", required=True)
    a = ap.parse_args()
    try:
        if a.cmd == "check":
            if a.rules is None and a.workdir is None:
                ap.error("check needs --workdir (the probe's files go there)")
            return check(a)
        if a.cmd == "write-syms":
            return write_syms(a)
        if a.cmd == "selftest":
            return selftest(a)
        if a.cmd == "xbuild":
            return xbuild(a)
        return self_image(a)
    except Fail as e:
        print("state_carry_check: cannot judge: %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main())
