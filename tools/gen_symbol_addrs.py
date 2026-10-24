#!/usr/bin/env python3
"""tools/gen_symbol_addrs.py -- a target's symbol lists from a linked ELF.

The PAL lists (config/symbol_addrs.pal.txt, symbol_addrs.pal.data.txt) were
recovered by the decompilation from a stripped retail ELF and a map of an
older link. A target the decompilation builds byte for byte has a better
source: its link with the symbol table kept (the decompilation's
build/<slug>/ico.syms.elf, the same link as build/<slug>/ico.elf before
stripping) names every function and global at its retail address. This
script writes that table in the PAL lists' splat format, which
tools/gen_data_c.py and tools/gen_data_desc.py read (SymbolMap):

  config/symbol_addrs.<slug>.txt
      name = 0xADDR; // type:func  // <object>
      every function (global and static) in .text, and the VU1 microprogram
      labels in .vutext the tables' pointer words name
  config/symbol_addrs.<slug>.data.txt
      name = 0xADDR; // size:0xN  // <object>
      every object (global and static) in .data, .rodata, .sdata, .lit4,
      .sbss and .bss

Names and addresses only (with each symbol's object from the link map and
its size): no byte of the ELF is written (docs/LEGAL.md). A static whose
name a global also has is written `can_be_referenced:False`, so the global
keeps its name in SymbolMap (two statics of one name both stay: SymbolMap
treats a name at two addresses as a pair of statics no other file names).
Assembler labels (gcc2_compiled., the VU overlays' local labels, section
starts) and absolute symbols are left out.

Checks before writing:
  --base-elf ELF  every loadable section of the symbol ELF is byte-equal to
                  the retail base ELF's (the names belong to the user's ELF)
  every symbol of config/data_members.<slug>.txt is at the address the
  member table gives it

Usage:
  tools/gen_symbol_addrs.py --region jpn \\
      --elf /primary/dev/ico/build/jpn/ico.syms.elf \\
      --map /primary/dev/ico/build/jpn/ico.jpn.map \\
      --base-elf /primary/dev/ico/baserom/jpn/baseelf.elf
  tools/gen_symbol_addrs.py --region kor \\
      --elf /primary/dev/ico/build/kor/ico.syms.elf \\
      --map /primary/dev/ico/build/kor/ico.kor.map \\
      --base-elf /primary/dev/ico-pc/baserom/kor/baseelf.elf
"""

import argparse
import re
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent


def config_path(root, slug, stem, ext):
    """config/<stem>.<slug>.<ext> (PAL's files carry pal as well)."""
    return root / "config" / f"{stem}.{slug}.{ext}"


FUNC_SECTIONS = (".text",)
DATA_SECTIONS = (".data", ".rodata", ".sdata", ".lit4", ".sbss", ".bss")
IDENT = re.compile(r"^[A-Za-z_]\w*$")
MAP_LINE = re.compile(r"^ (\.\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$")
TITLE = {
    "jpn": "Japanese retail target (SCPS_110.03)",
    "kor": "Korean retail target (SCPS_560.01)",
    "cn": "Asian retail target (SCPS_550.01)",
    "ntsc": "USA retail target (SCUS_971.13)",
    "aug6": "Aug-6-2001 prototype target (SCUS_971.13)",
    "jun26": "Jun-26-2001 demo target (CUS97113/SCPS_000.00)",
}  # the comment line of the lists it writes


def fail(msg):
    sys.exit(f"gen_symbol_addrs: {msg}")


def map_objects(path):
    """(section, lo, hi, object) of every input section the link map places,
    the object as its source path without the build prefix and .o."""
    out = []
    for line in path.read_text(encoding="latin-1").splitlines():
        m = MAP_LINE.match(line)
        if not m:
            continue
        lo, size = int(m.group(2), 16), int(m.group(3), 16)
        if size == 0:
            continue
        obj = re.sub(r"^build/[^/]+/", "", m.group(4))[:-2]
        out.append((m.group(1), lo, lo + size, obj))
    out.sort(key=lambda r: r[1])
    return out


def object_at(ranges, sec, addr):
    for s, lo, hi, obj in ranges:
        if lo <= addr < hi and (s == sec or s.startswith(sec)):
            return obj
    return None


def symbols(elf):
    """(name, addr, size, kind, local, section) of every symbol written."""
    secs = [s.name for s in elf.iter_sections()]
    out = []
    for s in elf.get_section_by_name(".symtab").iter_symbols():
        name, typ, bind = s.name, s["st_info"]["type"], s["st_info"]["bind"]
        shndx = s["st_shndx"]
        if not IDENT.match(name) or not isinstance(shndx, int):
            continue  # labels with dots or dollars, absolute and undefined symbols
        sec = secs[shndx]
        local = bind == "STB_LOCAL"
        if sec in FUNC_SECTIONS and typ == "STT_FUNC":
            out.append((name, s["st_value"], s["st_size"], "func", local, sec))
        elif sec == ".vutext" and not local:
            out.append((name, s["st_value"], s["st_size"], "func", local, sec))
        elif sec in DATA_SECTIONS and (typ == "STT_OBJECT" or (local and typ == "STT_NOTYPE")):
            out.append((name, s["st_value"], s["st_size"], "data", local, sec))
    return out


def check_base(sym_elf, base_path):
    with open(base_path, "rb") as fh:
        base = ELFFile(fh)
        for s in sym_elf.iter_sections():
            if not s["sh_flags"] & 2 or s["sh_type"] == "SHT_NOBITS" or s["sh_size"] == 0:
                continue  # not loaded, or no bytes in the file
            b = base.get_section_by_name(s.name)
            if b is None or b["sh_addr"] != s["sh_addr"] or b.data() != s.data():
                fail(f"{s.name} differs from {base_path}'s: not the same link")


def check_members(region, by_name):
    path = config_path(ROOT, region, "data_members", "txt")
    n = 0
    for line in path.read_text().splitlines():
        f = line.split("#", 1)[0].split()
        if len(f) != 5 or f[4] == "-":
            continue
        lo = int(f[2], 16)
        for x in f[4].split(","):
            name, off = x.split("@")
            addrs = by_name.get(name, set())
            if lo + int(off, 16) not in addrs:
                fail(f"{path.name}: {name} at 0x{lo + int(off, 16):08X}, the ELF has it at "
                     + (", ".join(f"0x{a:08X}" for a in sorted(addrs)) or "no address"))
            n += 1
    return n


def write(region, rows, ranges, elf_name, kind):
    globals_ = {name for name, _, _, _, local, _ in rows if not local}
    lines = []
    for name, addr, size, k, local, sec in sorted(rows, key=lambda r: (r[1], r[0])):
        if k != kind:
            continue
        obj = object_at(ranges, sec, addr)
        tail = "type:func" if kind == "func" else (f"size:0x{size:X}" if size else "")
        if local and name in globals_:
            tail = "can_be_referenced:False"
        where = (obj or "") + (" (static)" if local else "")
        parts = [p for p in (tail, where.strip()) if p]
        lines.append(f"{name} = 0x{addr:08X}; // " + "  // ".join(parts))
    what = "function and VU1 microprogram" if kind == "func" else "data"
    head = [
        f"// The {what} symbols of the {TITLE.get(region, region)}:",
        f"// names and addresses only,",
        f"// written by tools/gen_symbol_addrs.py from the symbol table of the",
        f"// decompilation's byte-matching link ({elf_name}) and its link map",
        f"// (the object after the address). tools/gen_data_c.py and",
        f"// tools/gen_data_desc.py read it for the names of the addresses the data",
        f"// tables' pointer words hold. Do not edit; rerun the script.",
        f"// {len(lines)} symbols.",
    ]
    stem = "symbol_addrs"
    path = config_path(ROOT, region, stem, "txt" if kind == "func" else "data.txt")
    path.write_text("\n".join(head + lines) + "\n")
    return path, len(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--region", required=True, metavar="SLUG")
    ap.add_argument("--elf", type=Path, required=True, help="the link with its symbol table")
    ap.add_argument("--map", type=Path, required=True, help="that link's map")
    ap.add_argument("--base-elf", type=Path, help="the retail ELF the link must equal")
    a = ap.parse_args()
    if a.region == "pal":
        fail("the PAL lists are the decompilation's recovered names; this script writes "
             "the lists of a target built with its symbol table")
    with open(a.elf, "rb") as fh:
        elf = ELFFile(fh)
        if elf.get_section_by_name(".symtab") is None:
            fail(f"{a.elf} has no symbol table (use build/<slug>/ico.syms.elf)")
        if a.base_elf:
            check_base(elf, a.base_elf)
        rows = symbols(elf)
    by_name = {}
    for name, addr, _, _, _, _ in rows:
        by_name.setdefault(name, set()).add(addr)
    n = check_members(a.region, by_name)
    ranges = map_objects(a.map)
    for kind in ("func", "data"):
        path, count = write(a.region, rows, ranges, a.elf.name, kind)
        print(f"{path.relative_to(ROOT)}: {count} symbols")
    print(f"{n} data-member symbols at their member-table addresses")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
