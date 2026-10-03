#!/usr/bin/env python3
"""tools/gen_ninja.py: emit build.ninja from config/link_order.pal.txt.

Every object from its own source, linked by the hand-written config/link.pal.ld
in the one object order of config/link_order.pal.txt.

Rules: cc (tools/compile_c.sh), as (the .s sources, assembler and -G per
archive as compile_c.sh chooses them for C), vucpp and vu (each VU1
microprogram is two files: ico2/vusrc/<stem>.vsm, the program text, which the
period cpp reads from standard input in ico2/ with -Ivusrc and writes to
build/ico2/vusrc/<stem>.i, and ico2/vusrc/<stem>.dsm, the DMA tags around
`.include "vusrc/<stem>.i"`, which dvp-as, ps2dev's DVP assembler built by
tools/setup.sh under tools/cc/dvp-as/, assembles from ico2/ on the path
vusrc/<stem>.dsm with -I../build/ico2. The overlay section names dvp-as writes
hash the file and line it is reading: the .dsm path for the first, and the
name cpp gives the text after that, "" for standard input and vusrc/<name>.h
for a shared include; -no-abicalls -mabi=64 leave the ABI bits of e_flags
clear, the one setting the link merges with the game's EABI64 objects), the
members config/data_schema.pal.txt lists as C (tools/gen_data_c.py: a zero
stand-in per member, a layout link with the stand-ins that fixes every other
address, the member's C written from the base ELF with its pointers named from
that link, compiled by ccdata (compile_c.sh run on it as a game TU, from a
programmer directory, so it reaches every ico2 include directory as the game's
TUs do), checked against the ROM range, and the placeholder
labels inside it bound by a linker assignment), labels (the D_<VMA>
placeholders a tracked source spells inside a data member's row, which
gen_data_c.py binds), link (the period linker, GNU ld 2.10 with
tools/binutils-2.10-ee.patch, built by tools/setup.sh under
tools/cc/binutils-2.10-ee/, writing the IRIX-compatible elf32-littlemips output
MAIN.MAP names: once to ico.syms.elf, which keeps the symbols, and once with -s
to ico.elf, since the base carries no .symtab or .strtab but 2.10's -s keeps
their names in .shstrtab as the base does; ld 2.10 has no INCLUDE inside a
section, so the script it reads is build/link.ld, config/link.pal.ld with its
INCLUDE lines expanded).

    tools/build.sh setup && ninja
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
# The member table's and the schema's one parser each.
from gen_data_c import parse_schema, parse_table  # noqa: E402

LIST = "config/link_order.pal.txt"
SCRIPT = "config/link.pal.ld"
TABLE = "config/data_members.pal.txt"
SCHEMA = "config/data_schema.pal.txt"
BASE_ELF = "baserom/pal/baseelf.elf"
OUT = "build"
NINJA = "build.ninja"
LD = "tools/cc/binutils-2.10-ee/bin/ld"
DVP_AS = "tools/cc/dvp-as/bin/dvp-as"
CPP = "tools/cc/ee-gcc2.9-991111/gcc-lib/ee/2.9-ee-991111-01/cpp"
LINK_LD = f"{OUT}/link.ld"
# The layout link: the same objects and script with a zero stand-in for each
# member written as C, so the C's pointers can be named before it exists.
LAYOUT_LD = f"{OUT}/layout.ld"
LAYOUT_ELF = f"{OUT}/ico.layout.elf"
LABELS = f"{OUT}/data_labels.txt"
# Output sections the script can take input by input (its INCLUDE lines).
LISTED = {"data": ".data .data.*", "rodata": ".rodata .rodata.*"}


def fail(msg):
    sys.exit(f"gen_ninja: {msg}")


def parse_list():
    entries = []
    for n, line in enumerate((ROOT / LIST).read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f:
            continue
        kind, _, name = f[0].rpartition(":")
        kind = kind or "src"
        if kind not in ("src", "data"):
            fail(f"{LIST}:{n}: unknown line form '{f[0]}'")
        align = {}
        for tok in f[1:]:
            m = re.fullmatch(r"align\.(data|rodata)=(\d+)", tok)
            if not m or kind != "src":
                fail(f"{LIST}:{n}: bad token '{tok}'")
            align[m.group(1)] = int(m.group(2))
        entries.append(dict(kind=kind, name=name, align=align, line=n))
    return entries


def table_members():
    """The members config/data_members.pal.txt has rows for."""
    return {r["member"] for r in parse_table(ROOT / TABLE)}


def schema_members():
    """The members tools/gen_data_c.py writes as C, with the headers their rows name."""
    return {m: " ".join(dict.fromkeys(r["header"] for r in rs))
            for m, rs in parse_schema(ROOT / SCHEMA).items()}


def obj_of(src):
    return f"{OUT}/{src.rsplit('.', 1)[0]}.o"


def archive_as(src):
    """compile_c.sh's per-archive assembler and -G for a source path."""
    if re.match(r"sce/(libc|libm|libgcc)/", src):
        return "as_old", "0"
    if src.startswith("sce/"):
        return "as_sdk", "0"
    return "as_old", "8"


def check(entries, members):
    listed = set()
    for e in entries:
        where = f"{LIST}:{e['line']}"
        if e["kind"] == "src":
            if not (ROOT / e["name"]).is_file():
                fail(f"{where}: {e['name']} does not exist")
            if e["name"] in listed:
                fail(f"{where}: {e['name']} is listed twice")
            listed.add(e["name"])
        elif e["name"] not in members:
            fail(f"{where}: {e['name']} is not a member in {TABLE}")
    tracked = subprocess.run(["git", "ls-files", "--", "ico2", "sce"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split()
    missing = [p for p in tracked if p.endswith((".c", ".s", ".S", ".dsm")) and p not in listed]
    if missing:
        fail(f"tracked sources missing from {LIST}:\n  " + "\n  ".join(missing))


def label_sources():
    """The files write_labels scans: every tracked C, header and assembly source."""
    tracked = subprocess.run(["git", "ls-files", "--", "ico2", "sce"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split()
    return [p for p in tracked if p.endswith((".c", ".h", ".inc", ".s", ".S"))]


def write_labels(out):
    """The address-named labels (D_<VMA>) sources use inside the data
    members. config/data_members.pal.txt carries MAIN.MAP's names and the few
    derived names the C reads a table by; a C or assembly file that reads a
    table at an interior offset, where MAIN.MAP names no symbol, spells the
    address as the name, and gen_data_c.py --alias binds that label. Only
    names some source spells are written."""
    rows = [(r["lo"], r["hi"], {name for name, _ in r["syms"]}) for r in parse_table(ROOT / TABLE)]
    idents = set()
    for p in label_sources():
        idents.update(re.findall(r"\bD_[0-9A-F]{8}\b", (ROOT / p).read_text(errors="replace")))
    lines = []
    for name in sorted(idents):
        addr = int(name[2:], 16)
        if any(lo <= addr < hi and name not in own for lo, hi, own in rows):
            lines.append(f"{name} {addr:08X}")
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_text("".join(l + "\n" for l in lines))


def vu_headers():
    """The VU programs' shared includes (ninja does not track cpp's includes)."""
    return sorted(str(p.relative_to(ROOT)) for p in (ROOT / "ico2/vusrc").glob("*.h"))


def objects(e, layout=False):
    """The objects a line links: a data member is one object (in the layout
    link, its zero stand-in)."""
    if e["kind"] == "src":
        return [obj_of(e["name"])]
    return [f"{OUT}/data/stub/{e['name']}.o" if layout else f"{OUT}/data/{e['name']}.o"]


def inputs_ld(entries, sec, layout=False):
    """build/<sec>.inputs.ld: the section input by input, or empty when no
    line needs more than the script's wildcard."""
    if not any(sec in e["align"] for e in entries):
        return ""
    out = []
    for e in entries:
        for o in objects(e, layout):
            if sec in e["align"]:
                out.append(f". = ALIGN({e['align'][sec]});")
            out.append(f"*{o}({LISTED[sec]})")
    return "\n".join(out) + "\n"


def expand_includes(script, swap=None):
    """config/link.pal.ld with each INCLUDE line replaced by the file it names
    (or by the text swap gives for that path): ld 2.10 reads INCLUDE only at
    the top level of a script."""
    out = []
    for line in (ROOT / script).read_text().splitlines(keepends=True):
        m = re.fullmatch(r"(\s*)INCLUDE\s+(\S+)\s*", line)
        if m and swap is not None and m.group(2) in swap:
            out.append(swap[m.group(2)])
        elif m:
            out.append((ROOT / m.group(2)).read_text())
        else:
            out.append(line)
    return "".join(out)


def main():
    if sys.argv[1:2] == ["--labels"]:
        write_labels(sys.argv[2])
        return 0
    entries = parse_list()
    members = table_members()
    cmembers = schema_members()
    check(entries, members)
    for m in cmembers:
        if m not in members:
            fail(f"{SCHEMA}: {m} is not a member in {TABLE}")
    for m in members:
        if m not in cmembers:
            fail(f"{TABLE}: {m} has no row in {SCHEMA}")
    swap = {}
    for sec in LISTED:
        p = ROOT / OUT / f"{sec}.inputs.ld"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(inputs_ld(entries, sec))
        swap[f"{OUT}/{sec}.inputs.ld"] = inputs_ld(entries, sec, layout=True)
    (ROOT / LINK_LD).write_text(expand_includes(SCRIPT))
    (ROOT / LAYOUT_LD).write_text(expand_includes(SCRIPT, swap))
    w = []
    w.append(f"# Generated by tools/gen_ninja.py from {LIST}; do not edit.\n"
             f"ninja_required_version = 1.10\nbuilddir = {OUT}\n"
             f"ld = {LD}\ndvp_as = {DVP_AS}\ncpp = {CPP}\npy = .venv/bin/python\n"
             "as_old = tools/period_env.sh tools/cc/ee-gcc2.9-991111/bin/as\n"
             "as_sdk = tools/period_env.sh tools/cc/ee-gcc2.96/bin/as\n"
             "asflags = -EL -mcpu=5900 -mabi=eabi\n"
             f"ldcmd = $ld -EL --oformat elf32-littlemips -T {LINK_LD} --no-warn-mismatch\n"
             f"ldlayout = $ld -EL --oformat elf32-littlemips -T {LAYOUT_LD} --no-warn-mismatch\n\n")
    w.append(f"rule gen\n  command = $py tools/gen_ninja.py\n  generator = 1\n"
             "  description = GEN $out\n\n"
             "rule cc\n  command = tools/compile_c.sh $in $out\n  description = CC $out\n\n"
             # A data member's generated C is a game TU and compiles as one:
             # from inside a programmer directory (common, which owns
             # typedef.h), so compile_c.sh gives it the game's flags,
             # assembler and the relative -I list of every ico2 include
             # directory its headers reach into. No header name repeats
             # across the include directories, so the directory chosen
             # changes no lookup.
             "rule ccdata\n  command = tools/compile_c.sh ico2/common/../../$in $out\n"
             "  description = CC $out\n\n")
    for r in ("as_old", "as_sdk"):
        w.append(f"rule {r}\n  command = ${r} $asflags -G $gnum -o $out $in\n  description = AS $out\n\n")
    w.append("rule vucpp\n  command = cd ico2 && ../tools/period_env.sh ../$cpp -Ivusrc < $vsm > ../$out\n"
             "  description = CPP $out\n\n"
             "rule vu\n  command = cd ico2 && ../$dvp_as -no-abicalls -mabi=64 -I../build/ico2 -o ../$out $dsm\n"
             "  description = VU $out\n\n"
             "rule datastub\n  command = $py tools/gen_data_c.py --stub $member --out $out\n"
             "  description = STUB $out\n  restat = 1\n\n"
             f"rule dataalias\n  command = $py tools/gen_data_c.py --alias $member --labels {LABELS} --out $out\n"
             "  description = ALIAS $out\n  restat = 1\n\n"
             f"rule datac\n  command = $py tools/gen_data_c.py --c $member --layout {LAYOUT_ELF} --out $out\n"
             "  description = DATAC $out\n  restat = 1\n\n"
             f"rule datacheck\n  command = $py tools/gen_data_c.py --check $member --obj $obj"
             f" --layout {LAYOUT_ELF} --out $out\n  description = CHECK $obj\n\n"
             "rule layout\n  command = $ldlayout -o $out $in\n  description = LD $out\n\n"
             "rule labels\n  command = $py tools/gen_ninja.py --labels $out\n"
             "  description = LABELS $out\n\n"
             f"rule link\n  command = $ldcmd -Map {OUT}/ico.pal.map -o {OUT}/ico.syms.elf $in"
             f" && $ldcmd -s -o {OUT}/ico.elf $in\n  description = LD $out\n\n")
    link = []
    layout = []
    stamps = []
    gen_deps = f"{SCHEMA} {TABLE} tools/gen_data_c.py"
    for e in entries:
        if e["kind"] == "data":
            m = e["name"]
            stub, obj = f"{OUT}/data/stub/{m}.o", f"{OUT}/data/{m}.o"
            src, alias, ok = f"{OUT}/data/{m}.c", f"{OUT}/data/{m}.alias.ld", f"{OUT}/data/{m}.ok"
            w.append(f"build {OUT}/data/stub/{m}.s: datastub | {gen_deps} {cmembers[m]}\n  member = {m}\n"
                     f"build {stub}: as_old {OUT}/data/stub/{m}.s\n  gnum = 8\n"
                     f"build {alias}: dataalias | {LABELS} {gen_deps}\n  member = {m}\n"
                     f"build {src}: datac | {LAYOUT_ELF} {BASE_ELF} {gen_deps} {cmembers[m]}\n  member = {m}\n"
                     f"build {obj}: ccdata {src}\n"
                     f"build {ok}: datacheck | {obj} {LAYOUT_ELF} {BASE_ELF} {gen_deps}\n"
                     f"  member = {m}\n  obj = {obj}\n")
            link.append(obj)
            layout.append(stub)
            stamps.append(ok)
            continue
        for o in objects(e):
            link.append(o)
            layout.append(o)
            if e["name"].endswith(".c"):
                w.append(f"build {o}: cc {e['name']}\n")
            elif e["name"].endswith(".dsm"):
                dsm = Path(e["name"]).relative_to("ico2")
                vsm = dsm.with_suffix(".vsm")
                pre = f"{OUT}/ico2/{dsm.with_suffix('.i')}"
                w.append(f"build {pre}: vucpp ico2/{vsm} | {CPP} {' '.join(vu_headers())}\n"
                         f"  vsm = {vsm}\n"
                         f"build {o}: vu {e['name']} | {pre} {DVP_AS}\n"
                         f"  dsm = {dsm}\n")
            else:
                rule, gnum = archive_as(e["name"])
                w.append(f"build {o}: {rule} {e['name']}\n  gnum = {gnum}\n")
    w.append(f"build {LABELS}: labels {' '.join(label_sources())} | {LIST} {TABLE}"
             " tools/gen_ninja.py\n")
    aliases = [f"{OUT}/data/{m}.alias.ld" for m in cmembers]
    w.append(f"\nbuild {LAYOUT_ELF}: layout {' '.join(layout + aliases)} | {LAYOUT_LD} {LD}\n")
    w.append(f"\nbuild {OUT}/ico.syms.elf {OUT}/ico.elf: link {' '.join(link + aliases)} | {LINK_LD} {LD}"
             f"{''.join(' ' + s for s in stamps)}\n"
             f"default {OUT}/ico.elf\n"
             f"build {NINJA} {OUT}/data.inputs.ld {OUT}/rodata.inputs.ld {LINK_LD} {LAYOUT_LD}: gen | "
             f"tools/gen_ninja.py tools/gen_data_c.py {LIST} {TABLE} {SCHEMA} {SCRIPT}\n")
    (ROOT / NINJA).write_text("".join(w))
    n = {k: sum(e["kind"] == k for e in entries) for k in ("src", "data")}
    toks = sum(len(e["align"]) for e in entries)
    print(f"gen_ninja: wrote {NINJA} ({len(link)} objects: {n['src']} sources, "
          f"{n['data']} data members, {toks} align tokens)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
