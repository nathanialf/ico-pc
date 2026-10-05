#!/usr/bin/env python3
"""tools/gen_data_desc.py -- the runtime table loader's descriptors.

The host build holds no disc data (plan "Game data", docs/LEGAL.md): the 73
data-only members tools/gen_data_c.py writes as C for the PS2 build are, on
the host, empty arrays that port/data/tables.c fills at boot from the ELF on
the user's disc. This script writes what that loader needs to know, all of it
types, offsets, sizes and names, from the same inputs gen_data_c.py reads:

  config/data_members.pal.txt   each member's rows: section and EE range
  config/data_schema.pal.txt    each row's record type, header and count
  the record headers            the record's fields, laid out by the EE's
                                rules (gen_data_c.Header)
  config/symbol_addrs.pal*.txt  the names of the addresses pointer words hold
  config/tables_manifest.txt    per row, a CRC-32 of the ROM range (to catch a
                                wrong ELF) and the symbols the pointer words
                                name (written by --manifest, the one mode that
                                reads the ELF)

Outputs (committed; port/data/gen/):

  table_desc.h   the descriptor types and the X-macro list of table symbols
  table_desc.c   one field list per record type (host offset by offsetof, EE
                 offset from the header, sizes, kind), bit-field setters, the
                 EE-layout asserts, and one row per member section
  table_defs.c   the 73 tables as uninitialised host arrays, the string-pool
                 buffers and staffRollNameDataNum (a count, not disc data)
  ee_symbols.c   the address -> host function / object registry for the
                 pointer words (sorted by EE address)
  tables.cmake   ICO_TABLE_SYMBOLS, for the loader test's renamed reference

Modes:

  tools/gen_data_desc.py               rewrite port/data/gen/
  tools/gen_data_desc.py --check       exit 1 if port/data/gen/ is stale; never
                                       opens the ELF, so the files are shown to
                                       be a function of committed text only
  tools/gen_data_desc.py --manifest [--elf ELF]
                                       rewrite config/tables_manifest.txt from
                                       the user's ELF (CRCs and symbol names)
  tools/gen_data_desc.py --check-manifest [--elf ELF]
                                       exit 1 if the manifest disagrees with ELF
"""

import argparse
import re
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_data_c as g  # noqa: E402

ROOT = g.ROOT
OUT_DIR = ROOT / "port/data/gen"
MANIFEST = ROOT / "config/tables_manifest.txt"
CLANG_FORMAT = ROOT / ".venv/bin/clang-format"
SECTIONS = {"data": 0, "rodata": 1, "sdata": 2}


def fail(msg):
    sys.exit(f"gen_data_desc: {msg}")


# ------------------------------------------------------------ the rows -------

def members():
    """Every member in table order: (name, [(schema row or None, table row)])."""
    order = []
    for r in g.parse_table():
        if r["member"] not in order:
            order.append(r["member"])
    sch = g.parse_schema()
    missing = sorted(set(sch) - set(order))
    if missing:
        fail(f"schema members with no table row: {', '.join(missing)}")
    return [(m, g.member_rows(m)) for m in order if m in sch]


def is_pool(s):
    """A row the loader copies whole as the member's string pool: no schema
    row (staffroll_dat's .rodata), or a count-of row (its .sdata head)."""
    return s is None or s["count_of"] is not None


# ---------------------------------------------------------- flattening -------

class Field:
    def __init__(self, kind, path, ee_off, ee_size, count=1, signed=False, bit=0, width=0):
        self.kind, self.path, self.ee_off, self.ee_size = kind, path, ee_off, ee_size
        self.count, self.signed, self.bit, self.width = count, signed, bit, width


def has_pointer(ty):
    if ty.kind in ("ptr", "fptr"):
        return True
    if ty.kind == "array":
        return has_pointer(ty.elem)
    if ty.kind == "struct":
        return any(has_pointer(f) for _, f, _, _ in ty.fields)
    return False


def join(path, name):
    return f"{path}.{name}" if path else name


def flatten(ty, path, off, out):
    """The leaves of a record type, in EE layout order. Arrays of scalars stay
    one field with a count; arrays of aggregates are expanded by index; a
    union (no pointers in any of the 73 types) is copied as its bytes."""
    k = ty.kind
    if k == "int":
        out.append(Field("INT", path, off, ty.size, signed=ty.signed))
    elif k == "float":
        if ty.size != 4:
            fail(f"{path}: double fields are not supported")
        out.append(Field("FLOAT", path, off, 4))
    elif k == "ptr":
        out.append(Field("DATA", path, off, 4))
    elif k == "fptr":
        out.append(Field("FUNC", path, off, 4))
    elif k == "array":
        el = ty.elem
        if el.kind in ("int", "float"):
            if el.kind == "float" and el.size != 4:
                fail(f"{path}: double fields are not supported")
            out.append(Field("INT" if el.kind == "int" else "FLOAT", path, off, el.size,
                             count=ty.n, signed=getattr(el, "signed", False)))
        else:
            for i in range(ty.n):
                flatten(el, f"{path}[{i}]", off + i * el.size, out)
    elif k == "struct":
        if ty.union:
            if has_pointer(ty):
                fail(f"{path}: a union holding a pointer")
            out.append(Field("BYTES", path, off, ty.size))
            return
        for name, f, o, b in ty.fields:
            if f.kind == "bitfield":
                if f.base.size > 4:
                    fail(f"{join(path, name)}: a bit-field wider than 32 bits")
                out.append(Field("BITS", join(path, name), off + o, f.size,
                                 signed=f.base.signed, bit=b, width=f.width))
            else:
                flatten(f, join(path, name), off + o, out)
    else:
        fail(f"{path}: cannot describe a field of kind {k}")


# ------------------------------------------------------------- types ---------

def c_ident(s):
    return re.sub(r"\W", "_", s)


class Record:
    """A record type the schema names, with the C spelling the descriptors use."""

    def __init__(self, s):
        self.schema = s
        self.ty = g.Header(s["header"]).typedef(s["type"])
        tname, star, dims = g.c_type(self.ty, s["type"])
        self.base, self.star, self.dims = tname, star, dims
        self.key = c_ident(s["type"])
        if self.ty.kind == "struct" and getattr(self.ty, "name", None):
            self.ctype = self.ty.name
            self.typedef = None
        else:
            # a base type, pointer or array element: named by a local typedef
            self.ctype = f"ico_td_rec_{self.key}"
            self.typedef = f"typedef {tname} {star}{self.ctype}{dims};"
        self.fields = []
        if self.ty.kind == "struct":
            flatten(self.ty, "", 0, self.fields)
        else:
            flatten(self.ty, "", 0, self.fields)
        self.pointers = has_pointer(self.ty)


# ------------------------------------------------------------ manifest -------

def pointer_words(rec, data, base_lo, start, n):
    """(path, ee address of the word, value, func, char_target) of every
    pointer word of n records from start in a row's bytes."""
    out = []

    def walk(ty, off, path):
        if ty.kind == "ptr":
            v = int.from_bytes(data[off:off + 4], "little")
            chars = ty.target.kind == "int" and ty.target.size == 1
            out.append((path, base_lo + off, v, False, chars))
        elif ty.kind == "fptr":
            out.append((path, base_lo + off, int.from_bytes(data[off:off + 4], "little"), True, False))
        elif ty.kind == "array":
            if has_pointer(ty.elem):
                for i in range(ty.n):
                    walk(ty.elem, off + i * ty.elem.size, f"{path}[{i}]")
        elif ty.kind == "struct":
            for name, f, o, _ in ty.fields:
                if f.kind != "bitfield":
                    walk(f, off + o, join(path, name))

    for i in range(n):
        walk(rec.ty, start + i * rec.ty.size, f"[{i}]")
    return out


def compute_manifest(elf_path):
    """The manifest text: each row's CRC and the symbols the tables name."""
    from elftools.elf.elffile import ELFFile
    layout = g.SymbolMap()
    lines, refs = [], {}
    with open(elf_path, "rb") as fh:
        elf = ELFFile(fh)
        for m, rows in members():
            datas = {r["section"]: g.rom_bytes(elf, r["lo"], r["hi"], r["section"]) for _, r in rows}
            pools = [(r["lo"], r["hi"]) for s, r in rows if is_pool(s)]
            for s, r in rows:
                d = datas[r["section"]]
                lines.append(f"row {m} {r['section']} 0x{r['lo']:08x} 0x{r['hi']:08x} "
                             f"crc32=0x{zlib.crc32(d):08x}")
                if is_pool(s):
                    continue
                rec = Record(s)
                if not rec.pointers:
                    continue
                start = g.start_of(s, r)
                for path, at, v, func, chars in pointer_words(rec, d, r["lo"], start, s["count"] or 1):
                    if v == 0:
                        continue
                    if chars and any(lo <= v < hi for lo, hi in pools):
                        continue
                    name, err = layout.name_at(v, func)
                    if name is None:
                        fail(f"{m} {path} at 0x{at:08X}: {err}")
                    kind = "func" if func else "obj"
                    if refs.setdefault((v, kind), name) != name:
                        fail(f"0x{v:08X} names both {refs[(v, kind)]} and {name}")
    for (v, kind), name in sorted(refs.items()):
        lines.append(f"{kind} 0x{v:08x} {name}")
    head = [
        "# config/tables_manifest.txt -- the runtime table loader's check list.",
        "# Written by tools/gen_data_desc.py --manifest from the user's base ELF;",
        "# holds no disc bytes (docs/LEGAL.md, docs/port/DATA.md):",
        "#   row <member> <section> <lo> <hi> crc32=<crc>",
        "#       the CRC-32 (zlib) of the ELF's bytes [lo, hi) of the row: the loader",
        "#       refuses an ELF whose table bytes differ (a wrong or modified disc)",
        "#   func|obj <address> <name>",
        "#       an address a table's pointer word holds and the symbol the",
        "#       committed symbol lists name there (port/data/gen/ee_symbols.c)",
        f"# {sum(1 for x in lines if x.startswith('row'))} rows, "
        f"{sum(1 for k in refs if k[1] == 'func')} functions, "
        f"{sum(1 for k in refs if k[1] == 'obj')} objects.",
    ]
    return "\n".join(head + lines) + "\n"


def parse_manifest(path=MANIFEST):
    if not path.exists():
        fail(f"{path.relative_to(ROOT)} is missing: run tools/gen_data_desc.py --manifest")
    rows, refs = {}, []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f:
            continue
        if f[0] == "row" and len(f) == 6 and f[5].startswith("crc32="):
            rows[(f[1], f[2], int(f[3], 16), int(f[4], 16))] = int(f[5][6:], 16)
        elif f[0] in ("func", "obj") and len(f) == 3:
            refs.append((int(f[1], 16), f[0], f[2]))
        else:
            fail(f"{path}:{n}: cannot read '{line}'")
    return rows, refs


# ------------------------------------------------------------- C text --------

HEADER_NOTE = ("/* Generated by tools/gen_data_desc.py from config/data_members.pal.txt,\n"
               " * config/data_schema.pal.txt, the record headers, the symbol lists and\n"
               " * config/tables_manifest.txt. Types, offsets, sizes and names only: no\n"
               " * disc data (docs/port/DATA.md). Do not edit; rerun the script. */\n")


def const_declared(headers, names):
    """The names some header declares `extern const`: the definition must not
    see that declaration (its type would conflict with the non-const array)."""
    out = set()
    for h in headers:
        toks = g.Header(h).toks
        for i, t in enumerate(toks):
            if t != "extern":
                continue
            j = i
            while toks[j] != ";":
                j += 1
            decl = toks[i:j]
            if "const" in decl and "(" not in decl:
                out.update(x for x in decl if x in names)
    return out


def include_block(headers, renamed):
    out = [f"#define {n} {n}_header_decl" for n in sorted(renamed)]
    out += [f'#include "{h}"' for h in headers]
    out += [f"#undef {n}" for n in sorted(renamed)]
    return out


def generate():
    ms = members()
    crcs, refs = parse_manifest()
    layout = g.SymbolMap()
    records, tables, headers = {}, [], []
    for m, rows in ms:
        for s, r in rows:
            key = (m, r["section"], r["lo"], r["hi"])
            if key not in crcs:
                fail(f"{m} .{r['section']} 0x{r['lo']:x}: no manifest row (run --manifest)")
            if s is not None and s["header"] not in headers:
                headers.append(s["header"])
            rec = None
            if not is_pool(s):
                rec = records.get(s["type"])
                if rec is None:
                    rec = records[s["type"]] = Record(s)
                n = s["count"] or 1
                start = g.start_of(s, r)
                if start + n * rec.ty.size > r["hi"] - r["lo"]:
                    fail(f"{m}: {n} records do not fit its .{r['section']} row")
            tables.append((m, s, r, rec, crcs[key]))
    if len(crcs) != len(tables):
        fail("config/tables_manifest.txt has rows the table does not")
    for v, kind, name in refs:
        got = layout.by_name.get(name)
        if got != v:
            fail(f"manifest {kind} {name}: the symbol lists place it at "
                 f"{'nowhere' if got is None else f'0x{got:08X}'}, not 0x{v:08X}")
    symbols = [s["syms"][0][0] for _, s, _, _, _ in tables if s is not None]
    defined = [(s["syms"][0][0], rec, s) for _, s, _, rec, _ in tables if rec is not None]
    renamed = const_declared(headers, set(symbols))

    # ---- table_desc.h
    h = [HEADER_NOTE, "#ifndef ICO_PORT_DATA_TABLE_DESC_H", "#define ICO_PORT_DATA_TABLE_DESC_H", "",
         "#include <stdint.h>", "",
         "/* What a field of a record holds (port/data/tables.c decodes each kind). */",
         "enum {",
         "    ICO_TF_INT,   /* an integer of ee_size bytes (count of them), little-endian */",
         "    ICO_TF_FLOAT, /* an IEEE single (count of them), copied as its bits */",
         "    ICO_TF_BYTES, /* a pointer-free union, copied as its ee_size bytes */",
         "    ICO_TF_BITS,  /* a bit-field: width bits from bit of the ee_size-byte unit */",
         "    ICO_TF_FUNC,  /* a function pointer word: the registry's function */",
         "    ICO_TF_DATA   /* an object pointer word: the member's string pool or the",
         "                     registry's object */",
         "};",
         "",
         "typedef struct IcoTableField {",
         "    const char *path;     /* the C member designator, \"\" for the element */",
         "    uint32_t host_offset; /* offsetof on this host (unused for ICO_TF_BITS) */",
         "    uint16_t host_size;   /* one element's size on this host */",
         "    uint16_t ee_offset;   /* the offset in the EE record */",
         "    uint16_t ee_size;     /* one element's size in the EE record */",
         "    uint16_t count;       /* elements: an array of scalars is one field */",
         "    uint8_t kind;         /* ICO_TF_* */",
         "    uint8_t is_signed;    /* ICO_TF_INT, ICO_TF_BITS */",
         "    uint8_t bit, width;   /* ICO_TF_BITS */",
         "    void (*set)(void *record, uint32_t value); /* ICO_TF_BITS: stores the field */",
         "} IcoTableField;",
         "",
         "typedef struct IcoTableRecord {",
         "    const char *type;",
         "    uint32_t host_size, ee_size;",
         "    const IcoTableField *fields;",
         "    uint32_t field_count;",
         "} IcoTableRecord;",
         "",
         "#define ICO_TABLE_DATA 0",
         "#define ICO_TABLE_RODATA 1",
         "#define ICO_TABLE_SDATA 2",
         "",
         "/* One row per (member, section) of config/data_members.pal.txt. A row",
         "   with a record type fills `count` records of `host` from the record at",
         "   `start` bytes into the EE range; a string-pool row (record NULL) is",
         "   copied whole into `host`, `ee_hi - ee_lo` bytes. */",
         "typedef struct IcoTableRow {",
         "    const char *member;",
         "    const char *symbol; /* the table's C name; NULL for a pool row */",
         "    uint32_t section;   /* ICO_TABLE_* */",
         "    uint32_t ee_lo, ee_hi;",
         "    uint32_t crc32; /* config/tables_manifest.txt */",
         "    uint32_t start, count;",
         "    const IcoTableRecord *record;",
         "    void *host;",
         "} IcoTableRow;",
         "",
         "extern const IcoTableRow ico_table_rows[];",
         "extern const uint32_t ico_table_row_count;",
         "",
         "/* The registry: sorted by EE address (port/data/gen/ee_symbols.c). */",
         "typedef struct IcoEeFunc {",
         "    uint32_t addr;",
         "    void (*fn)(void);",
         "    const char *name;",
         "} IcoEeFunc;",
         "",
         "typedef struct IcoEeObject {",
         "    uint32_t addr;",
         "    void *obj;",
         "    const char *name;",
         "} IcoEeObject;",
         "",
         "extern const IcoEeFunc ico_ee_funcs[];",
         "extern const uint32_t ico_ee_func_count;",
         "extern const IcoEeObject ico_ee_objects[];",
         "extern const uint32_t ico_ee_object_count;",
         "",
         "/* The tables' C names, for X(name) (port/data/test/tables_test.c). */",
         "#define ICO_TABLE_SYMBOLS(X) \\"]
    h += [f"    X({n}) \\" for n in symbols]
    h += ["", "#endif /* ICO_PORT_DATA_TABLE_DESC_H */", ""]

    # ---- table_defs.c
    d = [HEADER_NOTE,
         "/* The 73 data tables, defined on the host without initialisers: they",
         " * land in .bss and port/data/tables.c fills them from the user's ELF",
         " * before the game starts. None is const (the loader writes them all; the",
         " * PS2 build's const .rodata tables were written by the game in three",
         " * places, docs/port/DATA.md), so a header's `extern const` declaration",
         " * of one is renamed around the #includes. */",
         ""]
    d += include_block(headers, renamed)
    d.append("")
    pools = []
    for m, s, r, rec, _ in tables:
        if rec is None:
            pools.append((m, r))
            continue
        name = s["syms"][0][0]
        if s["count"] is None:
            d.append(f"{rec.base} {rec.star}{name}{rec.dims};")
        else:
            d.append(f"{rec.base} {rec.star}{name}[{s['count']}]{rec.dims};")
    d.append("")
    d.append("/* The members' string pools: copied from the ELF, the targets of their")
    d.append("   char pointers (staffroll_dat). */")
    for m, r in pools:
        d.append(f"char ico_table_pool_{c_ident(m)}_{r['section']}[{r['hi'] - r['lo']}];")
    for m, s, r, rec, _ in tables:
        if s is not None and s["count_of"]:
            arr, less = s["count_of"]
            d.append("")
            d.append(f"/* {m}: the count of {arr}, derived as the PS2 build derives it */")
            d.append(f"int {s['syms'][0][0]} = sizeof({arr}) / sizeof({arr}[0])"
                     + (f" - {less};" if less else ";"))
    d.append("")

    # ---- table_desc.c
    c = [HEADER_NOTE,
         "#include <stddef.h>",
         "#include <stdint.h>",
         "",
         '#include "port/data/gen/table_desc.h"',
         ""]
    c += include_block(headers, renamed)
    c.append("")
    for name, rec, s in defined:
        if s["count"] is None:
            c.append(f"extern {rec.base} {rec.star}{name}{rec.dims};")
        else:
            c.append(f"extern {rec.base} {rec.star}{name}[]{rec.dims};")
    for m, r in pools:
        c.append(f"extern char ico_table_pool_{c_ident(m)}_{r['section']}[];")
    c.append("")
    c.append("/* The EE layout of each record type: pointer-free records keep it on")
    c.append("   every host (they are copied field by field all the same); records")
    c.append("   with pointers keep it on a 32-bit host only. */")
    c.append("#if UINTPTR_MAX == 0xFFFFFFFFu")
    c.append("#define ICO_TD_EE_HOST 1")
    c.append("#else")
    c.append("#define ICO_TD_EE_HOST 0")
    c.append("#endif")
    c.append("")
    for rec in records.values():
        if rec.typedef:
            c.append(rec.typedef)
    c.append("")
    for rec in records.values():
        T = rec.ctype
        guard = rec.pointers
        if guard:
            c.append("#if ICO_TD_EE_HOST")
        c.append(f"_Static_assert(sizeof({T}) == {rec.ty.size}, \"{rec.schema['type']}: EE size\");")
        for f in rec.fields:
            if f.kind == "BITS" or not f.path:
                continue
            c.append(f"_Static_assert(offsetof({T}, {f.path}) == {f.ee_off}, "
                     f"\"{rec.schema['type']}.{f.path}: EE offset\");")
        if guard:
            c.append("#endif")
        for f in rec.fields:
            if f.kind in ("INT", "FLOAT", "BYTES"):
                sz = f"sizeof({member_expr(T, f.path)})"
                c.append(f"_Static_assert({sz} == {f.ee_size * f.count}, "
                         f"\"{rec.schema['type']}.{f.path or '(element)'}: size\");")
        c.append("")
    setters = {}
    for rec in records.values():
        for i, f in enumerate(rec.fields):
            if f.kind != "BITS":
                continue
            fn = f"set_{rec.key}_{i}"
            setters[(rec.key, i)] = fn
            cast = "(int32_t)" if f.signed else ""
            c.append(f"static void {fn}(void *r, uint32_t v)")
            c.append("{")
            c.append(f"    (({rec.ctype} *)r)->{f.path} = {cast}v;")
            c.append("}")
            c.append("")
    for rec in records.values():
        T = rec.ctype
        c.append(f"static const IcoTableField fields_{rec.key}[] = {{")
        for i, f in enumerate(rec.fields):
            path = f.path
            if f.kind == "BITS":
                hoff, hsize, setter = "0", "0", setters[(rec.key, i)]
            else:
                hoff = f"offsetof({T}, {path})" if path else "0"
                el = member_expr(T, path)
                hsize = f"sizeof({el}[0])" if f.count > 1 else f"sizeof({el})"
                setter = "NULL"
            c.append(f"    {{\"{path}\", {hoff}, {hsize}, {f.ee_off}, {f.ee_size}, {f.count}, "
                     f"ICO_TF_{f.kind}, {int(f.signed)}, {f.bit}, {f.width}, {setter}}},")
        c.append("};")
        c.append("")
    for rec in records.values():
        T = rec.ctype
        c.append(f"static const IcoTableRecord record_{rec.key} = {{\"{rec.schema['type']}\", "
                 f"sizeof({T}), {rec.ty.size}, fields_{rec.key}, "
                 f"sizeof(fields_{rec.key}) / sizeof(fields_{rec.key}[0])}};")
    c.append("")
    c.append("const IcoTableRow ico_table_rows[] = {")
    for m, s, r, rec, crc in tables:
        sec = f"ICO_TABLE_{r['section'].upper()}"
        if rec is None:
            c.append(f"    {{\"{m}\", NULL, {sec}, 0x{r['lo']:08X}u, 0x{r['hi']:08X}u, 0x{crc:08X}u, 0, 0, "
                     f"NULL, ico_table_pool_{c_ident(m)}_{r['section']}}},")
        else:
            name = s["syms"][0][0]
            host = f"&{name}" if s["count"] is None else name
            c.append(f"    {{\"{m}\", \"{name}\", {sec}, 0x{r['lo']:08X}u, 0x{r['hi']:08X}u, 0x{crc:08X}u, "
                     f"{g.start_of(s, r)}, {s['count'] or 1}, &record_{rec.key}, (void *){host}}},")
    c.append("};")
    c.append("")
    c.append("const uint32_t ico_table_row_count = sizeof(ico_table_rows) / sizeof(ico_table_rows[0]);")
    c.append("")

    # ---- ee_symbols.c
    funcs = sorted((v, n) for v, k, n in refs if k == "func")
    objs = sorted((v, n) for v, k, n in refs if k == "obj")
    e = [HEADER_NOTE,
         "/* The registry of the EE addresses the tables' pointer words hold: each",
         " * names a game function or object by the committed symbol lists, and the",
         " * loader stores the host's address of that symbol (port/data/tables.c).",
         " * The declarations are deliberately loose (the real prototypes live in",
         " * the game's headers); only the addresses are used.",
         " *",
         " * ICO_EE_SYMBOLS_STUBS (the loader test only) also defines every symbol,",
         " * each a distinct function or object, so the test links without the game. */",
         "",
         "#include <stddef.h>",
         "#include <stdint.h>",
         "",
         '#include "port/data/gen/table_desc.h"',
         ""]
    for _, n in funcs:
        e.append(f"void {n}(void);")
    for _, n in objs:
        e.append(f"extern char {n}[];")
    e.append("")
    e.append("#ifdef ICO_EE_SYMBOLS_STUBS")
    e.append("volatile int ico_ee_stub_hit;")
    for i, (_, n) in enumerate(funcs):
        e.append(f"void {n}(void)")
        e.append("{")
        e.append(f"    ico_ee_stub_hit = {i + 1};")
        e.append("}")
    for _, n in objs:
        e.append(f"char {n}[16] __attribute__((aligned(16)));")
    e.append("#endif")
    e.append("")
    e.append("const IcoEeFunc ico_ee_funcs[] = {")
    for v, n in funcs:
        e.append(f"    {{0x{v:08X}u, {n}, \"{n}\"}},")
    e.append("};")
    e.append("")
    e.append("const uint32_t ico_ee_func_count = sizeof(ico_ee_funcs) / sizeof(ico_ee_funcs[0]);")
    e.append("")
    e.append("const IcoEeObject ico_ee_objects[] = {")
    for v, n in objs:
        e.append(f"    {{0x{v:08X}u, {n}, \"{n}\"}},")
    e.append("};")
    e.append("")
    e.append("const uint32_t ico_ee_object_count = sizeof(ico_ee_objects) / sizeof(ico_ee_objects[0]);")
    e.append("")

    cm = ["# Generated by tools/gen_data_desc.py. The data tables' C names, for the",
          "# loader test's renamed reference copies (port/data/CMakeLists.txt).",
          "set(ICO_TABLE_SYMBOLS"]
    cm += [f"    {n}" for n in symbols]
    cm += [")", ""]

    return {
        "table_desc.h": fmt("\n".join(h), "table_desc.h"),
        "table_desc.c": fmt("\n".join(c), "table_desc.c"),
        "table_defs.c": fmt("\n".join(d), "table_defs.c"),
        "ee_symbols.c": fmt("\n".join(e), "ee_symbols.c"),
        "tables.cmake": "\n".join(cm),
    }


def member_expr(T, path):
    """An expression of the member at path of T (the element itself for "")."""
    if not path:
        return f"(*({T} *)0)"
    return f"((({T} *)0)->{path})"


def fmt(text, name):
    """The text as clang-format (the repo's .clang-format) writes it."""
    if not CLANG_FORMAT.exists():
        fail(f"{CLANG_FORMAT.relative_to(ROOT)} not found: run tools/setup.sh")
    r = subprocess.run([str(CLANG_FORMAT), f"--assume-filename={OUT_DIR / name}"],
                       input=text.encode(), capture_output=True, cwd=ROOT, check=False)
    if r.returncode != 0:
        fail(f"clang-format failed on {name}: {r.stderr.decode()}")
    return r.stdout.decode()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if port/data/gen/ is stale")
    ap.add_argument("--manifest", action="store_true", help="rewrite the manifest from the ELF")
    ap.add_argument("--check-manifest", action="store_true", help="exit 1 if the manifest disagrees")
    ap.add_argument("--elf", type=Path, default=g.BASE_ELF)
    ap.add_argument("--out-dir", type=Path, default=OUT_DIR)
    a = ap.parse_args()
    if a.manifest or a.check_manifest:
        if not a.elf.exists():
            fail(f"{a.elf}: no base ELF (tools/extract_elf.sh)")
        text = compute_manifest(a.elf)
        if a.check_manifest:
            if MANIFEST.read_text() != text:
                print("gen_data_desc: config/tables_manifest.txt disagrees with "
                      f"{a.elf}", file=sys.stderr)
                return 1
            return 0
        MANIFEST.write_text(text)
        return 0
    files = generate()
    if a.check:
        stale = [n for n, t in files.items()
                 if not (a.out_dir / n).exists() or (a.out_dir / n).read_text() != t]
        if stale:
            print(f"gen_data_desc: stale in {a.out_dir}: {', '.join(stale)} "
                  "(run tools/gen_data_desc.py)", file=sys.stderr)
            return 1
        return 0
    a.out_dir.mkdir(parents=True, exist_ok=True)
    for n, t in files.items():
        p = a.out_dir / n
        if not p.exists() or p.read_text() != t:
            p.write_text(t)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
