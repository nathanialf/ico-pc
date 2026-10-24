#!/usr/bin/env python3
"""tools/gen_data_c.py -- write a data-only member as C from the base ELF.

The members config/data_schema.pal.txt lists are written in the form the
developers compiled: one C translation unit per member, an initialized array
(or a single object) of a record type per section the member occupies, which
the build compiles with the game's flags. The values come
from the user's own baserom/pal/baseelf.elf at build time and are never
committed (docs/LEGAL.md); the schema rows and the records' header hold only
types. A member row with no schema row is the member's own string pool: the
char pointers into it are written as string literals, and the compiler lays
the pool out (8-aligned literals, in .rodata, or .sdata under -G 8 when they
are 8 bytes or less, in the order it emits them). A row marked count-of= is
a count of an array the member defines before it, written as sizeof over that
array, so its value is computed by the compiler and not read from the ROM.

One member per run (MEMBER is the name in the schema):

  --c MEMBER --symbol-map [--elf ELF] --out F.c
      the member's C: every pointer word resolved to the symbol the committed
      symbol lists (config/symbol_addrs.pal.txt,
      config/symbol_addrs.pal.data.txt), the data members' own symbols
      (config/data_members.pal.txt), SUPPLEMENT below and any
      --extra-symbols FILE place at that address, floats as the shortest
      decimal that reads back to the same bits, strings as literals.
      port/data/CMakeLists.txt writes the loader test's reference copy of
      each member this way (tables_test).

  --version SLUG --c MEMBER --symbol-map [--elf ELF] --out F.c
      the same for another target of config/regions.txt (jpn): its member
      table, schema and symbol lists are config/data_members.<slug>.txt,
      config/data_schema.<slug>.txt and config/symbol_addrs.<slug>*.txt, its
      ELF baserom/<slug>/baseelf.elf. The host keeps one layout, PAL's (the
      port's headers), so the C is written in the PAL record's form: each
      field's value is read from the target's bytes where
      config/ee_layout.<slug>.txt places the field of that name, a field the
      target lacks takes the schema's `default` row, and the bit words of
      MotionDef are permuted by the same map the loader uses
      (tools/gen_ee_layout.py, config/motion_bits.<slug>.txt). This is the
      reference of the loader's other descriptor sets (tables_loader_<slug>).

A symbol the member defines in .rodata is written `const`, as the developers
compiled it, unless its header declares it `extern` without const: a table
the game writes (the EE has no page protection, so those stores worked on
the PS2), which must not be const on the host either (clang deletes stores
to an object declared const; motionOrientManager.h declares
motionLimitDef without const for that).

The record type is read from the header the schema row names: a typedef of a
struct, or a struct tag (`struct Name { ... };`, which the C then spells
`struct Name`), whose fields are integers, enums, floats, pointers (object or
function), arrays, nested structs and bit-fields, laid out by the EE's rules (4-byte
pointers and ints, 8-byte long long and double, bit-fields from the low bit
of their declared type's unit). A base type with a trailing [N] is an array
element (char[32]: the C defines `char name[][32]`, the initializer giving
the count). A type the named header does not define is looked up in the
headers it includes, found by upstream's compile include paths (include_dirs); an array
typedef (sceVu0FVECTOR, `float [4]` aligned to 16) keeps its alignment and is
spelled by its name (`sceVu0FVECTOR name[] = { {...}, ... }`).

The port's headers carry no `#ifdef VERSION_<SLUG>` block: the converter
(tools/region_blocks.py) removes them at each upstream merge, so strip_c,
which drops every `#` line, sees one layout. Upstream's headers, read from
git by tools/gen_ee_layout.py, go through version_lines first.
"""

import argparse
import os
import re
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent
REGIONS = ROOT / "config/regions.txt"


# The rows a target adds to config/regions.txt before its data set exists
# (the pre-commit hook's gen_data_desc.py --check refuses a listed row
# without its set): a set's files are written and checked against these rows
# (--version <slug>), spelt as the row will be, until the row lands with its
# set (docs/port/DATA.md "Adding a target's set"). None is pending: the three
# 2001 targets' rows (ntsc, aug6, jun26) and the Asian disc's (cn) are in
# config/regions.txt.
PENDING_ROWS = ""


def region_row(line, where):
    f = line.split(None, 4)
    if len(f) != 5:
        fail(f"{where}: expected 'slug macro disc-id boot-elf product name'")
    return dict(slug=f[0], macro=f[1], disc_id=f[2], boot_elf=f[3], product=f[4].strip())


def regions(pending=False):
    """config/regions.txt's rows in order: dicts of slug, macro, disc_id,
    boot_elf and product (the rest of the line). pending: followed by the
    PENDING_ROWS config/regions.txt does not list yet (each marked
    pending=True), for the generators' --version of such a slug."""
    out = []
    for n, line in enumerate(REGIONS.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        out.append(region_row(line, f"{REGIONS}:{n}"))
    if not out or out[0]["slug"] != "pal":
        fail(f"{REGIONS}: row 0 must be pal (the host layout)")
    if pending:
        listed = {r["slug"] for r in out}
        for line in PENDING_ROWS.strip().splitlines():
            r = region_row(line, "gen_data_c.PENDING_ROWS")
            if r["slug"] not in listed:
                out.append(dict(r, pending=True))
    return out


def set_version(version):
    """Point the module's per-target paths at a slug of config/regions.txt.
    PAL's files keep their names (the host layout's own); every other
    target's carry its slug."""
    global VERSION, SCHEMA, TABLE, BASE_ELF, SYMBOL_LISTS
    VERSION = version
    SCHEMA = ROOT / f"config/data_schema.{version}.txt"
    TABLE = ROOT / f"config/data_members.{version}.txt"
    BASE_ELF = ROOT / f"baserom/{version}/baseelf.elf"
    SYMBOL_LISTS = [ROOT / f"config/symbol_addrs.{version}.txt",
                    ROOT / f"config/symbol_addrs.{version}.data.txt"]


set_version("pal")


def fail(msg):
    sys.exit(f"gen_data_c: {msg}")


# ----------------------------------------------------------------- table ----

def parse_table(path=None):
    """The member table's rows (config/data_members.<slug>.txt): one per
    (member, section)."""
    path = TABLE if path is None else path
    rows = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split()
        if len(f) != 5 or f[0] not in ("data", "rodata", "sdata"):
            fail(f"{path}:{n}: expected '<section> <member> <rom_lo> <rom_hi> <symbols>'")
        lo, hi = int(f[2], 16), int(f[3], 16)
        if not lo < hi:
            fail(f"{path}:{n}: empty range")
        syms = []
        if f[4] != "-":
            for s in f[4].split(","):
                name, off = s.split("@")
                syms.append((name, int(off, 16)))
        rows.append(dict(section=f[0], member=f[1], lo=lo, hi=hi, syms=syms, line=n))
    return rows


def rom_bytes(elf, lo, hi, section):
    """The base ELF's bytes [lo, hi) of the section."""
    s = elf.get_section_by_name("." + section)
    if s is None:
        fail(f"base ELF has no .{section}")
    base, size = s["sh_addr"], s["sh_size"]
    if not (base <= lo and hi <= base + size):
        fail(f"0x{lo:x}..0x{hi:x} is outside the base ELF's .{section} (0x{base:x}..0x{base + size:x})")
    return s.data()[lo - base:hi - base]


# ---------------------------------------------------------------- schema ----

def parse_schema(path=None):
    """The schema's rows, by member: one row per section the member's C defines.
    The port's map rows (PORT_VERBS: parse_port_rows) are skipped."""
    path = SCHEMA if path is None else path
    rows = {}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f or f[0] in PORT_VERBS:
            continue
        if len(f) not in (6, 7) or f[1] not in ("data", "rodata", "sdata"):
            fail(f"{path}:{n}: expected '<member> <section> <type> <header> <count> <symbols> [hex=... | count-of=...]'")
        syms = []
        for x in f[5].split(","):
            name, idx = x.split("@")
            syms.append((name, int(idx)))
        if syms[0][1] != 0 or any(b[1] <= a[1] for a, b in zip(syms, syms[1:])):
            fail(f"{path}:{n}: symbols must start at element 0 and ascend")
        if f[4] == "-" and len(syms) != 1:
            fail(f"{path}:{n}: a single object ('-' count) takes one symbol")
        hexf, count_of = set(), None
        if len(f) == 7 and f[6].startswith("hex="):
            hexf = set(f[6][4:].split(","))
        elif len(f) == 7:
            m = re.fullmatch(r"count-of=(\w+)(?:-(\d+))?", f[6])
            if not m or f[2] != "int" or f[4] != "-":
                fail(f"{path}:{n}: bad column '{f[6]}' (count-of= takes a single int)")
            count_of = (m.group(1), int(m.group(2) or 0))
        r = dict(member=f[0], section=f[1], type=f[2], header=f[3],
                 count=None if f[4] == "-" else int(f[4]), syms=syms, hex=hexf,
                 count_of=count_of, line=n)
        if any(o["section"] == r["section"] for o in rows.get(f[0], [])):
            fail(f"{path}:{n}: {f[0]} has a second .{f[1]} row")
        rows.setdefault(f[0], []).append(r)
    return rows


# The port's rows of a target's schema (its "---- The port" section): how the
# target's records map onto the host (PAL) records where a field's name alone
# does not (tools/gen_ee_layout.py record_map; docs/port/DATA.md "The map
# rows"). Each names a member and a field path of its record type:
#   default <member> <field>=<value>  a PAL field the disc's record lacks
#                                     takes the value; for a scalar array the
#                                     disc holds fewer elements of, the rest
#                                     take it (0 only: the loader's record
#                                     clear)
#   extend <member> <field>           a field the disc has and PAL lacks that
#                                     converted code reads: the port's header
#                                     holds it after PAL's fields ("PC port:
#                                     host-only"), every disc without it leaves
#                                     it 0
#   drop <member> <field>             a field the disc has and no code the
#                                     port compiles reads: not loaded
#   narrow <member> <field>           an integer the disc holds wider than
#                                     PAL's field: the low bits, which every
#                                     record's value fits (--manifest checks)
#   rename <member> <field>=<host>    a field the disc has that PAL holds
#                                     under another name (host: a PAL field,
#                                     or one element of a PAL scalar array,
#                                     path[i], every element then named by a
#                                     row): copied into that field
#   elf <path>                        the disc file the set's tables are read
#                                     from when it is not the BOOT2 file
#                                     (config/regions.txt's boot-elf): the June
#                                     2001 demo boots the magazine's menu, its
#                                     game is CUS97113/SCPS_000.00
#   extra <member> <type> <count>     a table PAL has none of: the host array
#                                     holds count records of type (- for a
#                                     single object; the member's own row
#                                     names its header and symbol); a disc
#                                     without it loads none
PORT_VERBS = ("default", "extend", "drop", "narrow", "rename", "elf", "extra")


def parse_port_rows(path=None):
    """The schema's port rows: {'default': {(member, field): value text},
    'extend', 'drop', 'narrow': {(member, field)}, 'rename': {(member, host
    field): the disc's field}, 'extra': {member: (type, count)}, 'elf': the
    path or None}."""
    path = SCHEMA if path is None else path
    out = {"default": {}, "extend": set(), "drop": set(), "narrow": set(), "rename": {},
           "extra": {}, "elf": None}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f or f[0] not in PORT_VERBS:
            continue
        verb = f[0]
        if verb == "default":
            if len(f) != 3 or "=" not in f[2]:
                fail(f"{path}:{n}: expected 'default <member> <field>=<value>'")
            field, value = f[2].split("=", 1)
            out["default"][(f[1], field)] = value
        elif verb == "elf":
            if len(f) != 2 or out["elf"] is not None:
                fail(f"{path}:{n}: expected one 'elf <path on the disc>'")
            out["elf"] = f[1]
        elif verb == "rename":
            if len(f) != 3 or "=" not in f[2]:
                fail(f"{path}:{n}: expected 'rename <member> <field>=<host field>'")
            field, host = f[2].split("=", 1)
            if (f[1], host) in out["rename"]:
                fail(f"{path}:{n}: {f[1]} {host} has two rename rows")
            out["rename"][(f[1], host)] = field
        elif verb == "extra":
            if len(f) != 4 or not (f[3].isdigit() or f[3] == "-"):
                fail(f"{path}:{n}: expected 'extra <member> <type> <count>' (- for one object)")
            out["extra"][f[1]] = (f[2], None if f[3] == "-" else int(f[3]))
        else:
            if len(f) != 3 or "=" in f[2]:
                fail(f"{path}:{n}: expected '{verb} <member> <field>'")
            out[verb].add((f[1], f[2]))
    clash = [k for k in out["extend"] | out["drop"] | out["narrow"] if k in out["default"]]
    clash += [k for k in out["extend"] if k in out["drop"] | out["narrow"]]
    clash += [k for k in out["drop"] if k in out["narrow"]]
    clash += [k for k in out["rename"] if k in out["default"] or k in out["narrow"]]
    if clash:
        fail(f"{path}: {clash[0][0]} {clash[0][1]} has two map rows")
    return out


def parse_defaults(path=None):
    """The schema's `default <member> <field>=<value>` rows: the value a field
    of the host (PAL) record takes when the target's record has no field of
    that name. {(member, field): value text}."""
    return parse_port_rows(path)["default"]


def schema_paths():
    """Every target's config/data_schema.<slug>.txt, PAL's first."""
    return sorted((ROOT / "config").glob("data_schema.*.txt"),
                  key=lambda p: (p.name != "data_schema.pal.txt", p.name))


def host_only():
    """{record type: {top-level field names}}: the host-only fields of the
    port's headers, every schema's `extend` rows (a field the host holds after
    PAL's own for the disc that has it; every other disc's record, PAL's among
    them, leaves it 0). Read from every config/data_schema.*.txt, so the PAL
    set and the other sets agree whatever config/regions.txt lists."""
    out = {}
    for path in schema_paths():
        rows = parse_schema(path)
        for member, field in parse_port_rows(path)["extend"]:
            if member not in rows:
                fail(f"{path.relative_to(ROOT)}: extend {member}: no row of that member")
            typ = rows[member][0]["type"]
            if not re.fullmatch(r"[A-Za-z_]\w*", field):
                fail(f"{path.relative_to(ROOT)}: extend {member} {field}: a top-level field name")
            out.setdefault(typ, set()).add(field)
    return out


def member_rows(name):
    """The member's rows of config/data_members.pal.txt, in table order, each
    with its schema row (None for a row only the C's string literals fill: a
    member's own string pool, which the compiler lays out)."""
    sch = parse_schema()
    if name not in sch:
        fail(f"{name} is not in {SCHEMA.relative_to(ROOT)}")
    rows = [r for r in parse_table(TABLE) if r["member"] == name]
    out = []
    for r in rows:
        s = [x for x in sch[name] if x["section"] == r["section"]]
        if not s and r["section"] not in ("rodata", "sdata"):
            fail(f"{name}: its .{r['section']} row has no schema row")
        out.append((s[0] if s else None, r))
    for x in sch[name]:
        if not any(r["section"] == x["section"] for r in rows):
            fail(f"{name}: needs a .{x['section']} row in {TABLE.relative_to(ROOT)}")
    return out


def start_of(s, row):
    """Where the schema row's first array starts in its table row: the offset
    the table gives that symbol (0 when it names none)."""
    table = dict(row["syms"])
    return table.get(s["syms"][0][0], 0)


# ------------------------------------------------------------ C types -------

class T:
    """A C type: kind is int, float, ptr, fptr, array or struct."""

    def __init__(self, kind, size, align, **kw):
        self.kind, self.size, self.align = kind, size, align
        self.__dict__.update(kw)


BASE_WORDS = {"unsigned", "signed", "char", "short", "int", "long", "float", "double",
              "void", "const", "volatile"}

INTS = {
    "char": (1, True), "signed char": (1, True), "unsigned char": (1, False),
    "short": (2, True), "short int": (2, True), "signed short": (2, True),
    "unsigned short": (2, False), "unsigned short int": (2, False),
    "int": (4, True), "signed int": (4, True), "signed": (4, True),
    "unsigned int": (4, False), "unsigned": (4, False),
    "long long": (8, True), "long long int": (8, True),
    "unsigned long long": (8, False), "unsigned long long int": (8, False),
}


def c_base(name):
    """A schema type column spelling a C base type: words joined by '_'
    (unsigned_short), since the column is one whitespace-free word; a
    trailing '*' makes it a pointer (char*)."""
    return name.replace("_", " ")


# Upstream's version-condition forms (tools/check_targets.py at upstream
# e8a8ebf90, version_condition / condition_holds, adopted as they are):
#     #ifdef VERSION_A                     #ifndef VERSION_A
#     #if defined(VERSION_A) || defined(VERSION_B)
#     #if !defined(VERSION_A) && !defined(VERSION_B)
#     #elif defined(VERSION_A) [|| defined(VERSION_B)]
#     #else                                #endif
_DIRECTIVE = re.compile(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")
_DEFINED = r"defined\(VERSION_([A-Z0-9_]+)\)"
_ANY_OF = re.compile(rf"{_DEFINED}(?:\s+\|\|\s+{_DEFINED})*")
_NONE_OF = re.compile(rf"!{_DEFINED}(?:\s+&&\s+!{_DEFINED})*")


def version_condition(line):
    """A preprocessor line read as a version block's directive: (kw, slugs,
    positive) for a version condition (kw "if" or "elif", slugs the
    lower-case targets it tests, positive False when the arm is taken for
    the targets it does not name: #ifndef, the !defined form); (kw, None,
    None) for #else and #endif and for a conditional that names no VERSION_;
    None for a line that is no conditional directive. Raises ValueError for a
    conditional naming a VERSION_ in any other form."""
    m = _DIRECTIVE.match(line)
    if not m:
        return None
    kw, rest = m.group(1), m.group(2).split("/*", 1)[0].strip()
    if "VERSION_" not in rest:
        return (kw, None, None)
    names = re.findall(r"VERSION_([A-Z0-9_]+)", rest)
    if kw in ("ifdef", "ifndef") and re.fullmatch(r"VERSION_[A-Z0-9_]+", rest):
        return ("if", frozenset(n.lower() for n in names), kw == "ifdef")
    if kw in ("if", "elif") and _ANY_OF.fullmatch(rest):
        return (kw, frozenset(n.lower() for n in names), True)
    if kw == "if" and _NONE_OF.fullmatch(rest):
        return (kw, frozenset(n.lower() for n in names), False)
    raise ValueError(f"#{kw} {rest}: not a version condition form")


def refuse_plain_elif(kw, slugs, in_version_block, n, line):
    """An #elif whose condition names no VERSION_ (#elif 1, #elif FOO) as the
    next arm of a version block mixes a version test with another one: the
    readers cannot say which target compiles it, so they stop on it (line n
    of the text) rather than guess. Upstream has none."""
    if kw == "elif" and slugs is None and in_version_block:
        raise ValueError(f"line {n}: {line.strip()}: an #elif naming no VERSION_ inside a "
                         "version block; write the version test and the other one as two blocks")


def condition_holds(slugs, positive, slug):
    """Whether a version condition's arm is the one target `slug` compiles
    (pal is named by no condition: it takes every #else arm)."""
    return (slug in slugs) == positive


def version_lines(text, version):
    """text with its version blocks resolved for the target `version` (a slug
    of config/regions.txt): the arm it compiles is kept, the other arms and
    the blocks' directives go; a nested block resolves inside its arm. The
    condition forms are upstream's (version_condition). Other conditionals
    are left for strip_c. Adopted from upstream's tools/gen_data_c.py, with
    the target an argument (the port compiles no VERSION_ macro). Raises
    ValueError for an #elif that names no VERSION_ inside a version block
    (refuse_plain_elif): no target's arm could be told from it."""
    out = []
    stack = []  # (is a VERSION_ block, this arm is live, an arm was taken)
    for n, line in enumerate(text.splitlines(), 1):
        v = version_condition(line)
        live = all(f[1] for f in stack)
        if v:
            kw, slugs, positive = v
            refuse_plain_elif(kw, slugs, bool(stack and stack[-1][0]), n, line)
            if kw in ("if", "ifdef", "ifndef") and slugs is not None:
                taken = condition_holds(slugs, positive, version)
                stack.append((True, taken, taken))
                continue
            if kw in ("if", "ifdef", "ifndef"):
                stack.append((False, True, True))
            elif kw == "elif" and stack and stack[-1][0]:
                taken = not stack[-1][2] and condition_holds(slugs, positive, version)
                stack[-1] = (True, taken, stack[-1][2] or taken)
                continue
            elif kw == "else" and stack and stack[-1][0]:
                stack[-1] = (True, not stack[-1][2], True)
                continue
            elif kw == "endif" and stack:
                if stack.pop()[0]:
                    continue
        if live:
            out.append(line)
    return "\n".join(out)


# (text, {target: the kept lines joined by spaces}): upstream's cases
VERSION_LINES_SELF_TEST = (
    ("a\n#ifdef VERSION_JPN\nj\n#else\np\n#endif\nz",
     {"pal": "a p z", "jpn": "a j z", "kor": "a p z"}),
    ("#ifndef VERSION_JPN\np\n#endif", {"pal": "p", "jpn": "", "kor": "p"}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#else\np\n#endif",
     {"pal": "p", "jpn": "jk", "kor": "jk"}),
    ("#if !defined(VERSION_JPN) && !defined(VERSION_KOR)\np\n#endif",
     {"pal": "p", "jpn": "", "kor": ""}),
    ("#ifdef VERSION_KOR\nk\n#elif defined(VERSION_JPN)\nj\n#else\np\n#endif",
     {"pal": "p", "jpn": "j", "kor": "k"}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\nx\n#ifdef VERSION_KOR\nk\n#else\nj\n#endif\n"
     "#else\np\n#endif", {"pal": "p", "jpn": "x j", "kor": "x k"}),
    ("#if 0\nx\n#ifdef VERSION_JPN\nj\n#endif\n#elif 1\ny\n#else\nw\n#endif",
     {"pal": "#if 0 x #elif 1 y #else w #endif", "jpn": "#if 0 x j #elif 1 y #else w #endif"}),
    ("#ifdef VERSION_JPN\n#if 1\nj\n#else\nq\n#endif\n#endif",
     {"pal": "", "jpn": "#if 1 j #else q #endif"}),
    # a fourth target nested in the PAL arm, and named in the shared conditions
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#else\n#ifdef VERSION_NTSC\nn\n#else\np\n#endif\n#endif",
     {"pal": "p", "jpn": "jk", "kor": "jk", "ntsc": "n"}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR) || defined(VERSION_NTSC)\njkn\n#else\np\n#endif",
     {"pal": "p", "jpn": "jkn", "kor": "jkn", "ntsc": "jkn"}),
    ("#if !defined(VERSION_JPN) && !defined(VERSION_KOR) && !defined(VERSION_NTSC)\np\n#endif",
     {"pal": "p", "jpn": "", "kor": "", "ntsc": ""}),
    # a fifth target named beside the fourth, with its own block inside that arm, or nested in the PAL arm of both
    ("#if defined(VERSION_NTSC) || defined(VERSION_AUG6)\n#ifdef VERSION_AUG6\na\n#else\nn\n#endif\nna\n#else\np\n#endif",
     {"pal": "p", "jpn": "p", "ntsc": "n na", "aug6": "a na"}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#else\n#ifdef VERSION_NTSC\nn\n#else\n#ifdef VERSION_AUG6\na\n#else\np\n#endif\n#endif\n#endif",
     {"pal": "p", "kor": "jk", "ntsc": "n", "aug6": "a"}),
    ("#if !defined(VERSION_NTSC) && !defined(VERSION_AUG6)\np\n#endif", {"pal": "p", "jpn": "p", "ntsc": "", "aug6": ""}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR) || defined(VERSION_AUG6)\njka\n#else\np\n#endif",
     {"pal": "p", "jpn": "jka", "ntsc": "p", "aug6": "jka"}),
    # a sixth target named beside the fifth, with its own block inside that arm, excluded inside the
    # three-name PAL-only block, or nested in the PAL arm of all the others
    ("#if defined(VERSION_AUG6) || defined(VERSION_JUN26)\n#ifdef VERSION_JUN26\nd\n#else\na\n#endif\nad\n#else\np\n#endif",
     {"pal": "p", "ntsc": "p", "aug6": "a ad", "jun26": "d ad"}),
    ("#if defined(VERSION_NTSC) || defined(VERSION_AUG6) || defined(VERSION_JUN26)\nnad\n#else\np\n#endif",
     {"pal": "p", "jpn": "p", "ntsc": "nad", "aug6": "nad", "jun26": "nad"}),
    ("#if !defined(VERSION_JPN) && !defined(VERSION_KOR) && !defined(VERSION_NTSC)\n#if !defined(VERSION_AUG6) && !defined(VERSION_JUN26)\np\n#endif\n#endif",
     {"pal": "p", "jpn": "", "ntsc": "", "aug6": "", "jun26": ""}),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#else\n#ifdef VERSION_NTSC\nn\n#else\n#ifdef VERSION_AUG6\na\n#else\n#ifdef VERSION_JUN26\nd\n#else\np\n#endif\n#endif\n#endif\n#endif",
     {"pal": "p", "kor": "jk", "ntsc": "n", "aug6": "a", "jun26": "d"}),
)


# texts the readers refuse with ValueError (refuse_plain_elif)
VERSION_LINES_REFUSED = (
    "#ifdef VERSION_JPN\nj\n#elif 1\nq\n#endif",
    "#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#elif FOO\nf\n#else\np\n#endif",
)


def version_lines_self_test():
    """[failures] of version_lines on VERSION_LINES_SELF_TEST and
    VERSION_LINES_REFUSED."""
    bad = []
    for text, want in VERSION_LINES_SELF_TEST:
        for slug, kept in want.items():
            got = " ".join(version_lines(text, slug).split())
            if got != kept:
                bad.append(f"version_lines({text!r}, {slug}) kept {got!r}, want {kept!r}")
    for text in VERSION_LINES_REFUSED:
        try:
            version_lines(text, "jpn")
            bad.append(f"version_lines({text!r}) read it, want a ValueError")
        except ValueError:
            pass
    return bad


def strip_c(text):
    """text without comments and preprocessor lines. The port's headers hold
    no VERSION_ block (the converter removes them); a header read from
    upstream goes through version_lines first."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))


def tokenize(text):
    return re.findall(r"0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|\.\.\.|[{}()\[\];,*:=+\-/]", text)


# Where an #include in a header is searched, in the order upstream's
# tools/compile_c.sh gives the compile (the port carries no compile_c.sh):
# the including file's programmer directory first, then these programmers'
# include directories, then the sce/ archives'.
INCLUDE_PROGRAMMERS = ("sugipon", "omori", "common", "ito", "fumi", "seki", "script")
INCLUDE_ARCHIVES = ("libc", "libm", "libvu0", "libkernl", "libpkt", "libgraph", "libdma",
                    "libpad", "libscf", "libmpeg", "libmc", "libipu", "libcdvd", "libsndn2")


def include_dirs(path):
    """The directories an #include in the header at path (repo-relative) is
    searched in, in upstream's tools/compile_c.sh order: the including file's
    programmer's include dir, the cross-programmer dirs, then the sce/
    archive dirs."""
    parts = Path(path).parts
    own = [parts[1]] if parts[0] == "ico2" and len(parts) > 2 else []
    dirs = [f"ico2/{p}/include" for p in dict.fromkeys(own + list(INCLUDE_PROGRAMMERS))]
    return [d for d in dirs if (ROOT / d).is_dir()] + [f"sce/{a}" for a in INCLUDE_ARCHIVES]


INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
HEADERS = {}


def header(path):
    if path not in HEADERS:
        HEADERS[path] = Header(path)
    return HEADERS[path]


class Header:
    def __init__(self, path, text=None):
        """The header at path in the tree, or (text given) that text, read as
        latin-1 under the name path (tools/gen_ee_layout.py reads upstream's
        headers from git)."""
        self.path = path
        if text is None:
            text = (ROOT / path).read_text(encoding="latin-1")
        self.toks = tokenize(strip_c(text))
        self.cache = {}
        # the headers it includes, as the compile finds them: "x.h" beside
        # it first, then the include paths; one the tree lacks is skipped
        self.includes = []
        for quote, name in INCLUDE.findall(re.sub(r"/\*.*?\*/", " ", text, flags=re.S)):
            here = [str(Path(path).parent)] if quote == '"' else []
            for d in here + include_dirs(path):
                if (ROOT / d / name).is_file():
                    self.includes.append(os.path.normpath(f"{d}/{name}"))
                    break

    def typedef(self, name):
        if name in self.cache:
            return self.cache[name]
        if name.endswith("*"):
            return T("ptr", 4, 4, target=self.typedef(name[:-1]))
        m = re.fullmatch(r"(.+)\[(\d+)\]", name)
        if m:
            el = self.typedef(m.group(1))
            n = int(m.group(2))
            return T("array", el.size * n, el.align, elem=el, n=n)
        if c_base(name) in INTS or c_base(name) in ("float", "double"):
            return self.base_type(c_base(name).split())
        t = self.toks
        for i, tok in enumerate(t):
            if tok != name or i == 0:
                continue
            j = i + 1
            # `} name [attrs] ;` closing a typedef struct/union
            if t[i - 1] == "}" or t[i - 1] == ")":
                k = i - 1
                while t[k] == ")":  # an __attribute__ before the name
                    k = self._skip_back_parens(k)
                    k -= 1  # the __attribute__ token
                    k -= 1
                if t[k] != "}":
                    continue
                start = self._match_back(k)
                if "enum" in (t[start - 1], t[start - 2]) and t[start - 3:start].count("typedef"):
                    ty = self._enum(start, k, name)
                    self.cache[name] = ty
                    return ty
                kw = start - 1
                while t[kw] not in ("struct", "union"):
                    kw -= 1
                if t[kw - 1] != "typedef":
                    continue
                ty = self._aggregate(kw, start)
                ty.name = name
                while t[j] == "__attribute__":
                    j, al = self._attribute(j)
                    if al:
                        ty.align = max(ty.align, al)
                        ty.size = -(-ty.size // ty.align) * ty.align
                self.cache[name] = ty
                return ty
            # `typedef <type> name[N] [attrs];`: an array type, which the C
            # spells by its name; aligned(N) raises its alignment
            if t[j] == "[" and t[i - 1] != "*":
                k = i - 1
                while k >= 0 and t[k] not in (";", "}", "{"):
                    k -= 1
                e = t.index("]", j)
                while t[e + 1] == "[":
                    e = t.index("]", e + 1)
                if t[k + 1] == "typedef" and t[k + 2] not in ("struct", "union"):
                    _, ty = self._declarator([name] + t[j:e + 1], self.base_type(t[k + 2:i]))
                    j = e + 1
                    while t[j] == "__attribute__":
                        j, al = self._attribute(j)
                        if al:
                            ty.align = max(ty.align, al)
                    if t[j] == ";":
                        ty.name = name
                        self.cache[name] = ty
                        return ty
            # `typedef <type> name;`
            if t[j] == ";":
                k = i - 1
                while k >= 0 and t[k] not in (";", "}", "{"):
                    k -= 1
                if t[k + 1] == "typedef" and t[k + 2] not in ("struct", "union"):
                    ty = self.base_type(t[k + 2:i])
                    self.cache[name] = ty
                    return ty
        # `struct name { ... };`: a record known by its tag alone
        for i in range(1, len(t) - 1):
            if t[i] == name and t[i - 1] in ("struct", "union") and t[i + 1] == "{":
                ty = self._aggregate(i - 1, i + 1)
                ty.name = f"{t[i - 1]} {name}"
                self.cache[name] = ty
                return ty
        # a type a header it includes defines
        ty = self.included(name, {self.path})
        if ty is not None:
            self.cache[name] = ty
            return ty
        fail(f"{self.path}: no typedef or struct tag {name}")

    def included(self, name, seen):
        """name as the first header this one includes (and so on, depth first)
        that defines it, or None."""
        for path in self.includes:
            if path in seen:
                continue
            seen.add(path)
            h = header(path)
            if name in h.cache or h.defines(name):
                return h.typedef(name)
            ty = h.included(name, seen)
            if ty is not None:
                return ty
        return None

    def defines(self, name):
        """Whether this header's own text declares name as a typedef or tag."""
        t = self.toks
        for i in range(1, len(t)):
            if t[i] != name:
                continue
            if t[i - 1] in ("struct", "union") and i + 1 < len(t) and t[i + 1] == "{":
                return True
            k = i - 1
            depth = 0
            while k >= 0 and not (depth == 0 and t[k] in (";", "{")):
                depth += t[k] == "}"
                depth -= t[k] == "{"
                k -= 1
            if t[k + 1] == "typedef" and t[i + 1] in (";", "[", "__attribute__"):
                return True
        return False

    def _match_back(self, k):
        depth = 0
        while True:
            if self.toks[k] == "}":
                depth += 1
            elif self.toks[k] == "{":
                depth -= 1
                if depth == 0:
                    return k
            k -= 1

    def _match_fwd(self, k):
        depth = 0
        while True:
            if self.toks[k] == "{":
                depth += 1
            elif self.toks[k] == "}":
                depth -= 1
                if depth == 0:
                    return k
            k += 1

    def _enum(self, ob, cb, name):
        """An enum whose body spans tokens ob..cb: an int, as gcc lays it out (4
        bytes; unsigned unless an enumerator is negative). Its values are
        written as numbers, since an enumerator constant is an int."""
        signed = "-" in self.toks[ob:cb]
        return T("int", 4, 4, signed=signed, name=name)

    def _skip_back_parens(self, k):
        depth = 0
        while True:
            if self.toks[k] == ")":
                depth += 1
            elif self.toks[k] == "(":
                depth -= 1
                if depth == 0:
                    return k
            k -= 1

    def _attribute(self, j):
        """__attribute__((...)) at j: the index after it and any aligned(N)."""
        t = self.toks
        depth, k, al = 0, j + 1, 0
        while True:
            if t[k] == "(":
                depth += 1
            elif t[k] == ")":
                depth -= 1
                if depth == 0:
                    break
            elif t[k] in ("aligned", "__aligned__"):
                al = int(t[k + 2], 0)
            elif t[k] in ("packed", "__packed__"):
                fail(f"{self.path}: packed records are not supported")
            k += 1
        return k + 1, al

    def base_type(self, toks):
        toks = [x for x in toks if x not in ("const", "volatile", "signed") or len(toks) == 1]
        key = " ".join(toks)
        if key in INTS:
            n, sg = INTS[key]
            return T("int", n, n, signed=sg, name=key)
        if key == "float":
            return T("float", 4, 4, name=key)
        if key == "double":
            return T("float", 8, 8, name=key)
        if key == "void":
            return T("void", 0, 1, name=key)
        if len(toks) == 2 and toks[0] in ("struct", "union"):
            return T("opaque", 0, 1, name=key)
        if len(toks) == 2 and toks[0] == "enum":
            t = self.toks
            for i in range(len(t) - 2):
                if t[i] == "enum" and t[i + 1] == toks[1] and t[i + 2] == "{":
                    return self._enum(i + 2, self._match_fwd(i + 2), key)
            fail(f"{self.path}: no enum {toks[1]}")
        if len(toks) == 1:
            ty = self.typedef(toks[0])
            return ty
        fail(f"{self.path}: unknown type '{key}'")

    def _aggregate(self, kw, start):
        """Parse struct/union at keyword index kw, body opening at start."""
        t = self.toks
        is_union = t[kw] == "union"
        k = start + 1
        fields = []
        while t[k] != "}":
            if t[k] in ("struct", "union") and (t[k + 1] == "{" or t[k + 2] == "{"):
                ob = k + 1 if t[k + 1] == "{" else k + 2
                end = ob
                depth = 0
                while True:
                    if t[end] == "{":
                        depth += 1
                    elif t[end] == "}":
                        depth -= 1
                        if depth == 0:
                            break
                    end += 1
                base = self._aggregate(k, ob)
                k = end + 1
            else:
                b = k
                if t[k] in ("struct", "union", "enum"):
                    k += 2
                elif t[k] in BASE_WORDS:
                    while t[k] in BASE_WORDS:
                        k += 1
                else:
                    k += 1
                base = self.base_type(t[b:k])
            # declarators up to ';'
            while True:
                d0 = k
                depth = 0
                while not (depth == 0 and t[k] in (",", ";")):
                    if t[k] in ("(", "["):
                        depth += 1
                    elif t[k] in (")", "]"):
                        depth -= 1
                    k += 1
                name, ty = self._declarator(t[d0:k], base)
                fields.append((name, ty))
                k += 1
                if t[k - 1] == ";":
                    break
        # Laid out in bits, by the EE's rules: a field starts at its type's
        # alignment; a bit-field takes the next bits of the current unit of
        # its declared type (from the low bit, little-endian) unless it would
        # cross that unit's boundary, when it starts the next unit, and a
        # zero-width one closes the unit. A bit-field's declared type aligns
        # the record (PCC_BITFIELD_TYPE_MATTERS). An unnamed bit-field holds
        # no value: an initializer skips it, so its bits must be zero.
        bit = 0
        size = 0
        align = 1
        laid = []
        for name, ty in fields:
            align = max(align, ty.align)
            unit = 8 * ty.size
            if ty.kind == "bitfield":
                if is_union:
                    bit = 0
                if ty.width == 0:
                    bit = -(-bit // unit) * unit
                    continue
                if bit % unit + ty.width > unit:
                    bit = -(-bit // unit) * unit
                start = bit
                bit += ty.width
            else:
                if is_union:
                    bit = 0
                bit = -(-bit // (8 * ty.align)) * 8 * ty.align
                start = bit
                bit += unit
            if name is not None:
                laid.append((name, ty, start // 8 if ty.kind != "bitfield" else start // unit * ty.size,
                             start % unit if ty.kind == "bitfield" else 0))
            size = max(size, -(-bit // 8))
        size = -(-size // align) * align
        return T("struct", size, align, fields=laid, union=is_union)

    def _declarator(self, toks, base):
        if ":" in toks:
            c = toks.index(":")
            if base.kind != "int" or c > 1:
                fail(f"{self.path}: cannot read bit-field '{' '.join(toks)}'")
            width = int(eval(" ".join(toks[c + 1:]), {"__builtins__": {}}))
            return (toks[0] if c else None), T("bitfield", base.size, base.align, base=base, width=width)
        attrs = [i for i, x in enumerate(toks) if x == "__attribute__"]
        if attrs:
            toks = toks[:attrs[0]]
        lead = 0
        while lead < len(toks) and toks[lead] == "*":
            lead += 1
        if toks[lead:lead + 2] == ["(", "*"]:
            # function pointer: * ... ( * name ) ( params ), the leading stars
            # belonging to the type it returns
            for _ in range(lead):
                base = T("ptr", 4, 4, target=base)
            toks = toks[lead:]
            name = toks[2]
            close = toks.index(")")
            params = " ".join(toks[close + 2:-1])
            return name, T("fptr", 4, 4, ret=base, params=params)
        ptr = 0
        while toks and toks[0] == "*":
            ptr += 1
            toks = toks[1:]
        name = toks[0]
        dims = []
        rest = toks[1:]
        while rest:
            if rest[0] != "[":
                fail(f"{self.path}: cannot read declarator '{' '.join(toks)}'")
            e = rest.index("]")
            dims.append(int(eval(" ".join(rest[1:e]), {"__builtins__": {}})))
            rest = rest[e + 1:]
        ty = base
        for _ in range(ptr):
            ty = T("ptr", 4, 4, target=ty)
        for n in reversed(dims):
            ty = T("array", ty.size * n, ty.align, elem=ty, n=n)
        return name, ty


# ------------------------------------------------------------ symbols -------

PLACEHOLDER = re.compile(r"^(D_|func_|jtbl_)[0-9A-F]{8}$")


class Layout:
    """Addresses as the period layout link had them: every defined symbol, by
    address (filled by SymbolMap)."""

    def add(self, name, addr, func):
        """A global the symbol map names (SymbolMap); shndx "MAP" stands for
        any defined, non-absolute section."""
        typ = "STT_FUNC" if func else "STT_OBJECT"
        if name in self.local:
            self.by_addr.setdefault(addr, []).append((name, "STB_LOCAL", typ, "MAP"))
            return
        if self.by_name.setdefault(name, addr) != addr:
            # one name at two addresses: two files' statics (allow_duplicated),
            # which no other file can name, as the layout link has them
            first = self.by_name.pop(name)
            self.local.add(name)
            for a in (first, addr):
                self.by_addr[a] = [(n, "STB_LOCAL" if n == name else b, t, s)
                                   for n, b, t, s in self.by_addr.get(a, [])]
            self.by_addr.setdefault(addr, []).append((name, "STB_LOCAL", typ, "MAP"))
            return
        rec = (name, "STB_GLOBAL", typ, "MAP")
        if rec not in self.by_addr.setdefault(addr, []):
            self.by_addr[addr].append(rec)

    def name_at(self, addr, func):
        """The global a source can name at addr: a function when func, else an object."""
        cands = self.by_addr.get(addr, [])
        glob = [c for c in cands if c[1] == "STB_GLOBAL" and c[3] != "SHN_ABS"
                and not PLACEHOLDER.match(c[0])]
        want = "STT_FUNC" if func else "STT_OBJECT"
        typed = [c for c in glob if c[2] == want] or glob
        names = sorted({c[0] for c in typed})
        if len(names) == 1:
            return names[0], None
        if not names:
            local = sorted({c[0] for c in cands})
            return None, (f"no global symbol at 0x{addr:08X}" +
                          (f" (only {', '.join(local)}, not visible to another file)" if local else ""))
        return None, f"several globals at 0x{addr:08X}: {', '.join(names)}"


# Globals a data member points at that neither symbol list names, at the
# address the period layout link gave them, per target. Each PAL entry
# belongs in config/symbol_addrs.pal.data.txt (not yet listed there); a
# target whose lists come from its own link's symbol table
# (tools/gen_symbol_addrs.py) needs none.
SUPPLEMENT = {
    "pal": [
        ("scpDummyGObj", 0x0063AA20, False),  # ico2/script/src/script.c .sdata
    ],
}

SPLAT_LINE = re.compile(r"\s*([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)")


class SymbolMap(Layout):
    """The layout link's view of addresses, built without the period link:
    every name the committed symbol lists (splat's `name = 0xADDR; // type:func`
    lines; a line marked can_be_referenced:False is skipped), the data
    members' own symbols (config/data_members.<slug>.txt), the target's
    SUPPLEMENT entries and any extra list place at a retail address. The retail ELF is stripped
    (no .symtab), so it has no symbol information of its own to read."""

    def __init__(self, extra=()):
        self.by_addr, self.by_name, self.local = {}, {}, set()
        for path in list(SYMBOL_LISTS) + [Path(p) for p in extra]:
            for n, line in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
                m = SPLAT_LINE.match(line)
                if not m or "can_be_referenced:False" in m.group(3):
                    continue
                self.add(m.group(1), int(m.group(2), 16), "type:func" in m.group(3))
        for r in parse_table(TABLE):
            for name, off in r["syms"]:
                self.add(name, r["lo"] + off, False)
        for name, addr, func in SUPPLEMENT.get(VERSION, ()):
            self.add(name, addr, func)


# ----------------------------------------------------------- spelling -------

def float_text(bits, double=False):
    if double:
        v = struct.unpack("<d", bits)[0]
        r = repr(v)
        return r if ("e" in r or "." in r or "n" in r) else r + ".0"
    v = struct.unpack("<f", bits)[0]
    if v != v or v in (float("inf"), float("-inf")):
        return None
    for d in range(1, 10):
        s = f"{v:.{d}g}"
        if abs(float(s)) <= 3.4028234663852886e38 and struct.pack("<f", float(s)) == bits:
            break  # (a shorter spelling of FLT_MAX can round past it)
    m, _, e = s.partition("e")
    if e:
        exp = int(e)
        if -5 <= exp < 9:
            s = f"{float(s):.{max(0, len(m.replace('-', '').replace('.', '')) - 1 - exp)}f}"
        else:
            s = f"{m}e{exp}"
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def int_text(v, ty, hexed):
    if hexed:
        v &= (1 << (8 * ty.size)) - 1
        return f"0x{v:X}" if v else "0"
    if ty.signed and v >= 1 << (8 * ty.size - 1):
        v -= 1 << (8 * ty.size)
    if ty.size == 8:
        return f"{v}LL"
    return str(v)


def c_decl(ty, name):
    """The declaration of an object of type ty named name, or None for void."""
    if ty.kind == "void" or ty.kind == "opaque":
        return None
    if ty.kind in ("int", "float"):
        return f"{ty.name} {name}"
    if ty.kind == "ptr":
        inner = c_decl(ty.target, "*" + name)
        return inner if inner is not None else f"void *{name}"
    if ty.kind in ("struct", "array") and getattr(ty, "name", None):
        return f"{ty.name} {name}"
    return None


def c_string(bs):
    out = []
    for b in bs:
        c = chr(b)
        if c == '"' or c == "\\":
            out.append("\\" + c)
        elif 0x20 <= b < 0x7F or b >= 0xA1:
            out.append(c)  # ASCII, or an EUC-JP byte as the developer typed it
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


class Writer:
    def __init__(self, data, base, layout, hexf, pool=()):
        self.data, self.base, self.layout, self.hexf = data, base, layout, hexf
        self.pool = pool  # the member's own rows, (lo, hi, bytes): its strings
        self.funcs, self.objs, self.errors = {}, {}, []

    def literal(self, v, path):
        """The string literal a char pointer into the member's own rows names."""
        for lo, hi, bs in self.pool:
            if lo <= v < hi:
                nul = bs.find(0, v - lo)
                if nul < 0:
                    self.errors.append(f"{path}: the string at 0x{v:08X} has no terminator")
                    return "0"
                return c_string(bs[v - lo:nul])
        return None

    def value(self, ty, off, path, bitpos=0, width=None):
        d = self.data
        if ty.kind == "bitfield":
            # width: the disc's, when it holds the field narrower (unsigned)
            width = ty.width if width is None else width
            v = int.from_bytes(d[off:off + ty.size], "little") >> bitpos & ((1 << width) - 1)
            if path in self.hexf:
                return f"0x{v:X}" if v else "0"
            if ty.base.signed and v >= 1 << (ty.width - 1):
                v -= 1 << ty.width
            return str(v)
        if ty.kind == "int":
            v = int.from_bytes(d[off:off + ty.size], "little")
            return int_text(v, ty, path in self.hexf)
        if ty.kind == "float":
            s = float_text(d[off:off + ty.size], ty.size == 8)
            if s is None:
                self.errors.append(f"0x{self.base + off:08X} {path}: NaN or infinity in a float field")
                return "0.0f"
            return s
        if ty.kind in ("ptr", "fptr"):
            v = int.from_bytes(d[off:off + 4], "little")
            if v == 0:
                return "0"
            func = ty.kind == "fptr"
            if not func and ty.target.kind == "int" and ty.target.size == 1:
                lit = self.literal(v, f"0x{self.base + off:08X} {path}")
                if lit is not None:
                    return lit
            name, err = self.layout.name_at(v, func)
            if name is None:
                self.errors.append(f"0x{self.base + off:08X} {path}: {err}")
                return f"0 /* 0x{v:08X} */"
            if not func and name in ALIASES:
                # a name inside an extra table written as one array (write_c)
                first, idx = ALIASES[name]
                decl = c_decl(ty.target, f"{first}[]")
                self.objs[first] = decl if decl is not None else f"char {first}[]"
                return f"&{first}[{idx}]"
            if func:
                ret = c_decl(ty.ret, "") or "void "
                if self.funcs.setdefault(name, ret) != ret:
                    self.errors.append(f"0x{self.base + off:08X} {path}: {name} is held by fields "
                                       f"returning {self.funcs[name].strip()} and {ret.strip()}")
                return name
            decl = c_decl(ty.target, name)
            if decl is None:
                self.objs[name] = f"char {name}[]"
                return name
            self.objs[name] = decl
            return "&" + name
        if ty.kind == "array":
            el = ty.elem
            if el.kind == "int" and el.size == 1:
                bs = d[off:off + ty.n]
                nul = bs.find(0)
                if nul < 0 or not any(bs[nul:]):
                    return c_string(bs[:nul] if nul >= 0 else bs)
            return "{" + ", ".join(self.value(el, off + i * el.size, f"{path}[]")
                                   for i in range(ty.n)) + "}"
        if ty.kind == "struct":
            if ty.union:
                name, f, o, b = ty.fields[0]
                return "{" + self.value(f, off + o, f"{path}.{name}" if path else name, b) + "}"
            parts = []
            held = 0  # the record's bits its named fields hold
            for name, f, o, b in ty.fields:
                bits = f.width if f.kind == "bitfield" else 8 * f.size
                held |= ((1 << bits) - 1) << (8 * o + b)
                parts.append(self.value(f, off + o, f"{path}.{name}" if path else name, b))
            loose = int.from_bytes(d[off:off + ty.size], "little") & ~held
            if loose:
                at = (loose & -loose).bit_length() - 1
                after = [n for n, f, o, b in ty.fields if 8 * o + b <= at]
                self.errors.append(f"0x{self.base + off + at // 8:08X} {path}: nonzero padding" +
                                   (f" after {after[-1]}" if after else " before the first field"))
            return "{" + ", ".join(parts) + "}"
        fail(f"cannot write a value of kind {ty.kind} ({path})")


class MappedWriter(Writer):
    """A Writer of the host (PAL) record's form from another target's bytes:
    each leaf of the PAL type is read where config/ee_layout.<slug>.txt puts
    the field of that path (gen_ee_layout.record_map, the map the loader's
    descriptor set is written from), a field the target lacks takes its
    default, and a bit word is built by its bit map."""

    def __init__(self, data, base, layout, hexf, pool, maps):
        super().__init__(data, base, layout, hexf, pool)
        self.maps = maps  # {host path: gen_ee_layout.Mapped}

    def record(self, ty, rbase):
        return self.mapped(ty, rbase, "", "")

    def mapped(self, ty, rbase, path, lpath):
        mp = self.maps.get(lpath)
        if mp is not None:
            if mp.how == "copy" and mp.count != mp.host.count:
                # the disc's shorter array: its elements, the rest 0
                el = ty.elem
                sub = T("array", el.size * mp.count, ty.align, elem=el, n=mp.count)
                return self.value(sub, rbase + mp.src.ee_off, path)
            if mp.how == "copy":
                return self.value(ty, rbase + mp.src.ee_off, path, mp.src.bit,
                                  mp.src.width if mp.src.kind == "BITS" else None)
            if mp.how == "gather":
                # a scalar array the disc holds as separately named fields
                return "{" + ", ".join(self.value(ty.elem, rbase + s.ee_off, path)
                                       for s in mp.srcs) + "}"
            if mp.how == "narrow":
                # the disc's wider integer: its low bits, which must hold it
                src, off = mp.src, rbase + mp.src.ee_off
                v = int.from_bytes(self.data[off:off + src.ee_size], "little", signed=src.signed)
                lo, hi = (-(1 << (8 * ty.size - 1)), 1 << (8 * ty.size - 1)) if ty.signed else \
                    (0, 1 << (8 * ty.size))
                if not lo <= v < hi:
                    self.errors.append(f"0x{self.base + off:08X} {path}: {v} does not fit the "
                                       f"host's {ty.size}-byte field (narrow)")
                return int_text(v & ((1 << (8 * ty.size)) - 1), ty, path in self.hexf)
            if mp.how == "clear" or mp.how == "default" and ty.kind not in ("int", "float"):
                # a host-only field the disc lacks, or an array's default 0:
                # the record clear's value
                return "0" if ty.kind in ("int", "float", "bitfield", "ptr", "fptr") else "{0}"
            if mp.how == "default":
                if ty.kind == "float":
                    return float_text(struct.pack("<f", float(mp.value)))
                return int_text(int(mp.value, 0), ty, path in self.hexf)
            src = self.data[rbase + mp.base:rbase + mp.base + mp.span]
            word = 0
            for i, b in enumerate(mp.bits):
                if b is not None and src[b >> 3] >> (b & 7) & 1:
                    word |= 1 << i
            name, f, _, _ = ty.fields[0]
            sub = f"{path}.{name}" if path else name
            return "{" + int_text(word, f, sub in self.hexf) + "}"
        if ty.kind == "struct" and not ty.union:
            return "{" + ", ".join(self.mapped(f, rbase, f"{path}.{name}" if path else name,
                                               f"{lpath}.{name}" if lpath else name)
                                   for name, f, o, b in ty.fields) + "}"
        if ty.kind == "array" and ty.elem.kind not in ("int", "float"):
            return "{" + ", ".join(self.mapped(ty.elem, rbase, f"{path}[]", f"{lpath}[{i}]")
                                   for i in range(ty.n)) + "}"
        fail(f"{lpath or '(element)'}: no field of the host record's map")


def pal_maps(rows):
    """{type: ({host path: Mapped}, the EE size)} of the member's record rows
    whose type has host-only fields (a schema's `extend` rows), for PAL: every
    other field copied from its own offset, the host-only ones 0, the record
    read at PAL's EE size."""
    import gen_ee_layout as L
    only = host_only()
    out = {}
    for s, _ in rows:
        if s is None or s["count_of"] or s["type"] in out or s["type"] not in only:
            continue
        ty = Header(s["header"]).typedef(s["type"])
        hf = []
        L.flatten(ty, "", 0, hf, words=True)
        maps = {f.path: L.Mapped(f, "clear" if L.top_name(f.path) in only[s["type"]] else "copy",
                                 src=f) for f in hf}
        out[s["type"]] = (maps, L.ee_part_size(ty, only[s["type"]], s["type"]))
    return out


def target_maps(rows):
    """{type: ({host path: Mapped}, the target's record size)} of the
    member's record rows, for the current non-PAL VERSION."""
    import gen_ee_layout as L
    recs, _ = L.parse_layout(VERSION)
    port, bits, only = parse_port_rows(), L.parse_bits(VERSION), host_only()
    out = {}
    for s, _ in rows:
        if s is None or s["count_of"] or s["type"] in out:
            continue
        ty = Header(s["header"]).typedef(s["type"])
        hf = []
        L.flatten(ty, "", 0, hf, words=True)
        maps = L.record_map(VERSION, s["type"], s["member"], hf, recs, port, bits, host_only=only)
        out[s["type"]] = ({m.host.path: m for m in maps}, recs[s["type"]].size)
    return out


def c_type(ty, spelled):
    """The C spelling of an element type the schema names: its base, the stars
    before the object's name and the bounds after its own."""
    if ty.kind in ("int", "float"):
        return ty.name, "", ""
    if ty.kind == "ptr":
        inner, star, dims = c_type(ty.target, spelled[:-1])
        return inner, star + "*", dims
    if ty.kind == "array" and getattr(ty, "name", None):
        return ty.name, "", ""  # an array typedef (sceVu0FVECTOR), by its name
    if ty.kind == "array":
        inner, star, dims = c_type(ty.elem, spelled[:spelled.rindex("[")])
        return inner, star, f"[{ty.n}]" + dims
    return getattr(ty, "name", None) or spelled, "", ""


def declared_writable(header, name):
    """Whether header declares name `extern` without const: a .rodata table
    the game writes, which the host declares writable where it is written
    (motionOrientManager.h's motionLimitDef: clang deletes stores to an
    object declared const). Its host definition must agree."""
    toks = Header(header).toks
    for i, t in enumerate(toks):
        if t != "extern":
            continue
        j = i
        while j < len(toks) and toks[j] != ";":
            j += 1
        decl = toks[i:j]
        if name in decl and "(" not in decl:
            return "const" not in decl
    return False


# PC port: {name: (first name, element index)} of the target's extra tables
# (its schema's `extra` rows) that hold several names: the reference writes
# such a table as one array named by its first name, as the host defines it
# (port/data/gen/table_defs.c), and a pointer to another of its names as that
# array's element, so tables_test compares it with the host's by its offset in
# the table (the June 2001 demo's enemy sets point into each other's tables).
ALIASES = {}


def extra_aliases():
    """ALIASES for the current VERSION."""
    ALIASES.clear()
    if VERSION == "pal":
        return
    extra = parse_port_rows()["extra"]
    for member, rows in parse_schema().items():
        if member not in extra:
            continue
        for s in rows:
            if len(s["syms"]) > 1:
                first = s["syms"][0][0]
                for name, idx in s["syms"]:
                    ALIASES[name] = (first, idx)


def write_c(member, rows, datas, layout):
    extra_aliases()
    pool = [(r["lo"], r["hi"], datas[r["section"]]) for _, r in rows]
    tmaps = target_maps(rows) if VERSION != "pal" else pal_maps(rows)
    defs, headers, funcs, objs = [], [], {}, {}
    arrays = {}
    for s, row in rows:
        if s is None:
            continue
        if s["count_of"]:
            # A count of a table the member defines before it: the compiler
            # computes it from the array's declared size, so the word is not
            # read from the ROM (the check still compares it).
            arr, less = s["count_of"]
            if arr not in arrays:
                fail(f"{member}: count-of={arr} names no array defined before {s['syms'][0][0]}")
            defs.append(f"int {s['syms'][0][0]} = sizeof({arr}) / sizeof({arr}[0])"
                        + (f" - {less};" if less else ";"))
            defs.append("")
            if s["header"] not in headers:
                headers.append(s["header"])
            continue
        data = datas[row["section"]]
        ty = header(s["header"]).typedef(s["type"])
        start = start_of(s, row)
        n = s["count"] or 1
        stride = tmaps[s["type"]][1] if s["type"] in tmaps else ty.size
        size = stride * n
        span = row["hi"] - row["lo"] - start
        if size > span or any(data[start + size:]):
            fail(f"{member}: {n} x {s['type']} ({stride} B) = {size} B from offset {start}, "
                 f"but the ROM's .{row['section']} range holds {span} B" +
                 (" with nonzero bytes after it" if size <= span else ""))
        if s["type"] in tmaps:
            w = MappedWriter(data[start:], row["lo"] + start, layout, s["hex"], pool,
                             tmaps[s["type"]][0])

            def one(off, w=w, ty=ty):
                return w.record(ty, off)
        else:
            w = Writer(data[start:], row["lo"] + start, layout, s["hex"], pool)

            def one(off, w=w, ty=ty):
                return w.value(ty, off, '')
        bounds = [i for _, i in s["syms"]] + [n]
        tname, star, dims = c_type(ty, s["type"])
        syms = s["syms"]
        if syms[0][0] in ALIASES:
            # an extra table of several names: one array (ALIASES)
            syms, bounds = syms[:1], [0, n]
        for (name, first), last in zip(syms, bounds[1:]):
            # a header's declaration without const (declared_writable): the
            # definition drops const to agree with it
            const = ("const " if row["section"] == "rodata" and
                     not declared_writable(s["header"], name) else "")
            if s["count"] is None:
                defs.append(f"{const}{tname} {star}{name}{dims} = {one(0)};")
            else:
                # The bound is left to the initializer, as the developers
                # wrote their tables (`[]`): under -G 8 gcc puts an array of
                # 8 bytes or less declared with its bound in .sdata, and one
                # declared `[]` in .data (EnemyInit.o's 8-byte tables).
                arrays[name] = last - first
                defs.append(f"{const}{tname} {star}{name}[]{dims} = {{")
                defs.extend(f"    {one(i * stride)}," for i in range(first, last))
                defs.append("};")
            defs.append("")
        if w.errors:
            fail(f"{member}: the record {s['type']} does not hold the ROM's bytes:\n  " +
                 "\n  ".join(w.errors[:20]) + (f"\n  ... {len(w.errors) - 20} more" if len(w.errors) > 20 else ""))
        for f, ret in w.funcs.items():
            if funcs.setdefault(f, ret) != ret:
                fail(f"{member}: {f} is held by fields returning {funcs[f].strip()} and {ret.strip()}")
        objs.update(w.objs)
        if s["header"] not in headers:
            headers.append(s["header"])
    out = [
        f"/* {member}.o, written by tools/gen_data_c.py from {BASE_ELF.relative_to(ROOT).as_posix()}",
        f" * and {SCHEMA.relative_to(ROOT).as_posix()}"
        + ("" if VERSION == "pal" else f", in the PAL record's form (config/ee_layout.{VERSION}.txt)")
        + ". Generated: do not commit. */",
        "",
    ]
    out += [f'#include "{(Path("../..") / h).as_posix()}"' for h in headers]
    out.append("")
    for f in sorted(funcs):
        out.append(f"extern {funcs[f]}{f}();")
    for o in sorted(objs):
        out.append(f"extern {objs[o]};")
    if funcs or objs:
        out.append("")
    return "\n".join(out + defs)


def write_if_changed(path, text):
    """Write text (bytes 0..255 as themselves) unless the file already holds
    it, so ninja's restat skips the compile and link that depend on it."""
    raw = text.encode("latin-1")
    if path.exists() and path.read_bytes() == raw:
        return
    path.write_bytes(raw)


def array_typedef_self_test():
    """[failures] of the array typedef reader: an array typedef with
    aligned(N) that a header provides only through its #include (Light.h
    includes <libvu0.h>) is read with its element, count and alignment, and
    spelled by its name with the array's initializer."""
    h = header("ico2/seki/include/Light.h")
    got = []
    v = h.typedef("sceVu0FVECTOR")
    got.append((v.kind, v.size, v.align, v.n, v.elem.name, v.name))
    m = h.typedef("sceVu0FMATRIX")
    got.append((m.kind, m.size, m.align, m.n, m.elem.n, m.name))
    got.append(c_type(v, "sceVu0FVECTOR") + (c_decl(h.typedef("sceVu0FVECTOR*").target, "p"),))
    got.append(Writer(struct.pack("<4f", 1, 0.5, -2, 0), 0, None, set()).value(v, 0, ""))
    want = [("array", 16, 16, 4, "float", "sceVu0FVECTOR"),
            ("array", 64, 16, 4, 4, "sceVu0FMATRIX"),
            ("sceVu0FVECTOR", "", "", "sceVu0FVECTOR p"),
            "{1.0f, 0.5f, -2.0f, 0.0f}"]
    return [f"array typedef: got {g!r}, want {w!r}" for g, w in zip(got, want) if g != w]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--c", required=True, metavar="MEMBER")
    ap.add_argument("--version", default="pal", metavar="SLUG",
                    help="the target, a slug of config/regions.txt or PENDING_ROWS (default pal)")
    ap.add_argument("--elf", type=Path, default=None,
                    help="the target's base ELF (default baserom/<slug>/baseelf.elf)")
    ap.add_argument("--symbol-map", action="store_true", required=True,
                    help="name pointers from the committed symbol lists")
    ap.add_argument("--extra-symbols", type=Path, action="append", default=[],
                    help="another splat-format list (repeatable)")
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    if a.version not in [r["slug"] for r in regions(pending=True)]:
        fail(f"{a.version} is not a slug of {REGIONS.relative_to(ROOT)} (or of PENDING_ROWS)")
    set_version(a.version)
    if a.elf is None:
        a.elf = BASE_ELF
    member = a.c
    rows = member_rows(member)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    with open(a.elf, "rb") as fh:
        elf = ELFFile(fh)
        datas = {r["section"]: rom_bytes(elf, r["lo"], r["hi"], r["section"])
                 for _, r in rows}
    layout = SymbolMap(a.extra_symbols)
    write_if_changed(a.out, write_c(member, rows, datas, layout))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
