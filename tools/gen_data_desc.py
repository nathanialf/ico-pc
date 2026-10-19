#!/usr/bin/env python3
"""tools/gen_data_desc.py -- the runtime table loader's descriptors.

The host build holds no disc data (docs/LEGAL.md): the 73 data-only
members (tools/gen_data_c.py writes them as C for the loader test's
reference copy) are, on the host, empty arrays that port/data/tables.c
fills at boot from the ELF on the user's disc. This script writes what
that loader needs to know, all of it types, offsets, sizes and names, from
the same inputs gen_data_c.py reads, once per disc of config/regions.txt
(a descriptor set; the slug is pal or jpn):

  config/data_members.<slug>.txt  each member's rows: section and EE range
  config/data_schema.<slug>.txt   each row's record type, header and count,
                                  and (non-PAL) the defaults of the PAL
                                  fields the disc's records lack
  the record headers              the host records, PAL's layout: the PAL set
                                  is the identity, laid out by the EE's rules
                                  (gen_data_c.Header)
  config/ee_layout.<slug>.txt     (non-PAL) the disc's own record layouts
                                  (tools/gen_ee_layout.py, from upstream's
                                  headers); each field maps BY PATH NAME onto
                                  the PAL host struct
  config/motion_bits.<slug>.txt   (non-PAL) MotionDef's bits read by shift
  config/symbol_addrs.<slug>*.txt the names of the addresses pointer words hold
  config/tables_manifest[.<slug>].txt
                                  per row, a CRC-32 of the ROM range (to catch a
                                  wrong ELF) and the symbols the pointer words
                                  name (written by --manifest, the one mode that
                                  reads the ELF; PAL's file has no slug)

Outputs (committed; port/data/gen/):

  table_desc.h        the descriptor types, the sets, the table ids and the
                      X-macro list of table symbols (shared)
  table_defs.c        the 73 tables as uninitialised host arrays at PAL's
                      counts (the largest of every set), the string-pool
                      buffers at the largest set's size and
                      staffRollNameDataNum (shared: one host layout)
  table_sets.c        ico_table_sets[], one per row of config/regions.txt
  tables.cmake        ICO_TABLE_SYMBOLS, ICO_TABLE_SLUGS and the sources
                      (ICO_TABLE_SOURCES, ICO_TABLE_EE_SYMBOLS) through each
                      set's own tables.cmake
  <slug>/table_desc.c one field list per record type (host offset by offsetof,
                      EE offset from the disc's layout, sizes, kind), bit-field
                      setters and bit maps, the EE-layout asserts (PAL), one
                      row per member section and the set ico_table_set_<slug>
  <slug>/ee_symbols.c the address -> host function / object registry of the
                      set's pointer words (sorted by EE address)
  <slug>/tables.cmake the set's two sources

Field kinds beyond a copy: a PAL field the disc lacks is ICO_TF_DEFAULT (the
schema's `default` row); a 4-byte bit word of MotionDef is, on a non-PAL set,
ICO_TF_BITMAP, its 32 host bits each taken from a bit of the disc's three
bit words (PAL's own set copies the union's bytes, as it always has: the
identity map would change nothing and cost a loop per word).

Modes:

  tools/gen_data_desc.py               rewrite port/data/gen/ (every set)
  tools/gen_data_desc.py --check       exit 1 if port/data/gen/ is stale; never
                                       opens an ELF, so the files are shown to
                                       be a function of committed text only;
                                       also the shift-read audit below
  tools/gen_data_desc.py --manifest [--version SLUG] [--elf ELF]
                                       rewrite the set's manifest from the
                                       user's ELF (CRCs and symbol names)
  tools/gen_data_desc.py --check-manifest [--version SLUG] [--elf ELF]
                                       exit 1 if the manifest disagrees with ELF

The audit (every run): each read of MotionDef's bit words in ico2/ by shift
or mask (`modeBits.word >> 19 & 7`, `->flags2 >> 3 & 1`, a view's
`modeBits.h.hi`) names host bits every non-PAL set fills from the disc (a
named bit-field or a motion_bits row); no `>> PAL_JPN(` (the shift is a
layout: the host's is PAL's); and no such read sits in a region's own arm
(`if (ICO_JPN) {...}`, the arguments after the first of `PAL_JPN(`), which
would read the disc's bit position in the PAL host word.
"""

import argparse
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_data_c as g  # noqa: E402
import gen_ee_layout as L  # noqa: E402

ROOT = g.ROOT
OUT_DIR = ROOT / "port/data/gen"
CLANG_FORMAT = ROOT / ".venv/bin/clang-format"


def fail(msg):
    sys.exit(f"gen_data_desc: {msg}")


def manifest_path(slug):
    return ROOT / ("config/tables_manifest.txt" if slug == "pal" else
                   f"config/tables_manifest.{slug}.txt")


class version:
    """with version(slug): gen_data_c's paths point at that set."""

    def __init__(self, slug):
        self.slug = slug

    def __enter__(self):
        g.set_version(self.slug)

    def __exit__(self, *a):
        g.set_version("pal")


# ------------------------------------------------------------ the rows -------

def members(slug="pal"):
    """Every member in table order: (name, [(schema row or None, table row)])."""
    with version(slug):
        order = []
        for r in g.parse_table():
            if r["member"] not in order:
                order.append(r["member"])
        sch = g.parse_schema()
        missing = sorted(set(sch) - set(order))
        if missing:
            fail(f"{slug}: schema members with no table row: {', '.join(missing)}")
        return [(m, g.member_rows(m)) for m in order if m in sch]


def is_pool(s):
    """A row the loader copies whole as the member's string pool: no schema
    row (staffroll_dat's .rodata), or a count-of row (its .sdata head)."""
    return s is None or s["count_of"] is not None


# ------------------------------------------------------------- types ---------

Field, flatten, has_pointer, join = L.Field, L.flatten, L.has_pointer, L.join


def c_ident(s):
    return re.sub(r"\W", "_", s)


class Record:
    """A record type the schema names, with the C spelling the descriptors
    use: the host (PAL) layout, the port's headers."""

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
        flatten(self.ty, "", 0, self.fields)
        self.word_fields = []
        flatten(self.ty, "", 0, self.word_fields, words=True)
        self.pointers = has_pointer(self.ty)


# ------------------------------------------------------------ manifest -------

def char_targets(ty, path="", out=None):
    """{path: is a char pointer} of the pointer leaves of a host type."""
    out = {} if out is None else out
    if ty.kind == "ptr":
        out[path] = ty.target.kind == "int" and ty.target.size == 1
    elif ty.kind == "array" and ty.elem.kind not in ("int", "float"):
        for i in range(ty.n):
            char_targets(ty.elem, f"{path}[{i}]", out)
    elif ty.kind == "struct" and not ty.union:
        for name, f, o, _ in ty.fields:
            if f.kind != "bitfield":
                char_targets(f, join(path, name), out)
    return out


def set_pointer_fields(slug, typ, host_rec, ee_recs):
    """The pointer words of a set's record type at the disc's offsets,
    [(path, offset, func, chars)], and its EE size. A pointer word keeps its
    path and kind on every set (record_map checks it), so the host type says
    which point at chars."""
    chars = char_targets(host_rec.ty)
    fields = host_rec.fields if slug == "pal" else ee_recs[typ].fields
    size = host_rec.ty.size if slug == "pal" else ee_recs[typ].size
    return [(f.path, f.ee_off, f.kind == "FUNC", chars.get(f.path, False))
            for f in fields if f.kind in ("FUNC", "DATA")], size


def compute_manifest(elf_path, slug="pal"):
    """The manifest text: each row's CRC and the symbols the tables name."""
    from elftools.elf.elffile import ELFFile
    with version(slug):
        layout = g.SymbolMap()
    ee_recs = L.parse_layout(slug)[0] if slug != "pal" else None
    host = host_records()
    lines, refs = [], {}
    with open(elf_path, "rb") as fh:
        elf = ELFFile(fh)
        for m, rows in members(slug):
            datas = {r["section"]: g.rom_bytes(elf, r["lo"], r["hi"], r["section"]) for _, r in rows}
            pools = [(r["lo"], r["hi"]) for s, r in rows if is_pool(s)]
            for s, r in rows:
                d = datas[r["section"]]
                lines.append(f"row {m} {r['section']} 0x{r['lo']:08x} 0x{r['hi']:08x} "
                             f"crc32=0x{zlib.crc32(d):08x}")
                if is_pool(s):
                    continue
                hrec = host[s["type"]]
                if not hrec.pointers:
                    continue
                ptrs, size = set_pointer_fields(slug, s["type"], hrec, ee_recs)
                start = g.start_of(s, r)
                for i in range(s["count"] or 1):
                    for path, off, func, chars in ptrs:
                        at = start + i * size + off
                        v = int.from_bytes(d[at:at + 4], "little")
                        if v == 0:
                            continue
                        if chars and any(lo <= v < hi for lo, hi in pools):
                            continue
                        name, err = layout.name_at(v, func)
                        if name is None:
                            fail(f"{m} [{i}].{path} at 0x{r['lo'] + at:08X}: {err}")
                        kind = "func" if func else "obj"
                        if refs.setdefault((v, kind), name) != name:
                            fail(f"0x{v:08X} names both {refs[(v, kind)]} and {name}")
    for (v, kind), name in sorted(refs.items()):
        lines.append(f"{kind} 0x{v:08x} {name}")
    rel = manifest_path(slug).relative_to(ROOT).as_posix()
    head = [
        f"# {rel} -- the runtime table loader's check list.",
        "# Written by tools/gen_data_desc.py --manifest" +
        ("" if slug == "pal" else f" --version {slug}") + " from the user's base ELF;",
        "# holds no disc bytes (docs/LEGAL.md):",
        "#   row <member> <section> <lo> <hi> crc32=<crc>",
        "#       the CRC-32 (zlib) of the ELF's bytes [lo, hi) of the row: the loader",
        "#       refuses an ELF whose table bytes differ (a wrong or modified disc)",
        "#   func|obj <address> <name>",
        "#       an address a table's pointer word holds and the symbol the",
        f"#       committed symbol lists name there (port/data/gen/{slug}/ee_symbols.c)",
        f"# {sum(1 for x in lines if x.startswith('row'))} rows, "
        f"{sum(1 for k in refs if k[1] == 'func')} functions, "
        f"{sum(1 for k in refs if k[1] == 'obj')} objects.",
    ]
    return "\n".join(head + lines) + "\n"


def parse_manifest(slug="pal"):
    path = manifest_path(slug)
    if not path.exists():
        fail(f"{path.relative_to(ROOT)} is missing: run tools/gen_data_desc.py --manifest"
             + ("" if slug == "pal" else f" --version {slug}"))
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


# ------------------------------------------------------------- the sets ------

_HOST = {}


def host_records():
    """{type: Record} of the PAL schema: the host layout every set fills."""
    if not _HOST:
        for m, rows in members("pal"):
            for s, r in rows:
                if not is_pool(s) and s["type"] not in _HOST:
                    _HOST[s["type"]] = Record(s)
    return _HOST


class Set:
    """One descriptor set: a row of config/regions.txt and its tables."""

    def __init__(self, region):
        self.region = region
        self.slug = slug = region["slug"]
        crcs, self.refs = parse_manifest(slug)
        with version(slug):
            layout = g.SymbolMap()
        host = host_records()
        self.tables = []  # (member, schema row, table row, host Record or None, crc)
        self.headers = []
        used = set()
        for m, rows in members(slug):
            for s, r in rows:
                key = (m, r["section"], r["lo"], r["hi"])
                if key not in crcs:
                    fail(f"{slug}: {m} .{r['section']} 0x{r['lo']:x}: no manifest row "
                         f"(run --manifest{'' if slug == 'pal' else ' --version ' + slug})")
                used.add(key)
                if s is not None and s["header"] not in self.headers:
                    self.headers.append(s["header"])
                rec = None
                if not is_pool(s):
                    rec = host.get(s["type"])
                    if rec is None:
                        fail(f"{slug}: {m}: record type {s['type']} is not one of PAL's")
                self.tables.append((m, s, r, rec, crcs[key]))
        if len(crcs) != len(used):
            fail(f"{manifest_path(slug).relative_to(ROOT)} has rows the table does not")
        for v, kind, name in self.refs:
            got = layout.by_name.get(name)
            if got != v:
                fail(f"{slug}: manifest {kind} {name}: the symbol lists place it at "
                     f"{'nowhere' if got is None else f'0x{got:08X}'}, not 0x{v:08X}")
        self.maps, self.ee_recs, self.notes = {}, None, []
        if slug != "pal":
            want = {}
            for m, s, r, rec, _ in self.tables:
                if rec is not None:
                    want.setdefault(s["type"], (m, rec.word_fields))
            self.maps, self.ee_recs, self.notes = L.record_maps(slug, want)
        for m, s, r, rec, _ in self.tables:
            if rec is None:
                continue
            n = s["count"] or 1
            if g.start_of(s, r) + n * self.ee_size(rec) > r["hi"] - r["lo"]:
                fail(f"{slug}: {m}: {n} records do not fit its .{r['section']} row")

    def ee_size(self, rec):
        if self.slug == "pal":
            return rec.ty.size
        return self.ee_recs[rec.schema["type"]].size

    def record_types(self):
        out = []
        for m, s, r, rec, _ in self.tables:
            if rec is not None and rec not in out:
                out.append(rec)
        return out


def all_sets():
    return [Set(r) for r in g.regions()]


# ------------------------------------------------------- the bit audit -------

def region_arm_spans(text):
    """Spans of a non-PAL region's own text: `if (ICO_<SLUG>) {...}` bodies and
    the arguments after the first of a selector macro (PAL_JPN(p, j))."""
    spans = []
    for m in re.finditer(r"\bif\s*\(\s*ICO_(?!PAL\b)[A-Z]+\s*\)\s*\{", text):
        depth, k = 0, m.end() - 1
        while k < len(text):
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        spans.append((m.end(), k))
    for m in re.finditer(r"\bPAL_[A-Z_]+\s*\(", text):
        depth, k, comma = 0, m.end() - 1, None
        while k < len(text):
            ch = text[k]
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
                if depth == 0:
                    break
            elif ch == "," and depth == 1 and comma is None:
                comma = k
            k += 1
        if comma is not None:
            spans.append((comma, k))
    return spans


def audit_bits(sets, root=ROOT):
    """The shift-read audit over the game sources: [error lines]."""
    mapped = {}
    for st in sets:
        if st.slug == "pal" or "MotionDef" not in st.maps:
            continue
        for mp in st.maps["MotionDef"]:
            if mp.how == "bitmap":
                mapped[(st.slug, mp.host.path)] = {i for i, b in enumerate(mp.bits) if b is not None}
    errs = []
    files = sorted(p for p in (root / "ico2").rglob("*") if p.suffix in (".c", ".h", ".inc"))
    for p in files:
        text = p.read_text(encoding="latin-1")
        if "modeBits" not in text and "flags" not in text and "PAL_" not in text:
            continue
        rel = p.relative_to(root).as_posix()
        folded = L.fold(text)
        for m in re.finditer(r">>\s*PAL_[A-Z_]+\s*\(", folded):
            errs.append(f"{rel}:{folded.count(chr(10), 0, m.start()) + 1}: a shift by PAL_JPN(): "
                        "a bit position is a layout, and the host's is PAL's (take the PAL arm)")
        spans = region_arm_spans(folded)
        for line, pos, w, bits, name in L.motion_reads(text, positions=True):
            if any(a <= pos < b for a, b in spans):
                errs.append(f"{rel}:{line}: a read of MotionDef's {w} in a region's own arm: the "
                            "host word is PAL's layout (take the PAL arm)")
                continue
            if bits is None:
                continue  # a named bit-field: record_map maps every one
            for (slug, word), have in mapped.items():
                if word != w:
                    continue
                miss = [b for b in bits if b not in have]
                if miss:
                    errs.append(f"{rel}:{line}: reads {w} bits {miss}, which the {slug} set does not "
                                f"fill from the disc (config/motion_bits.{slug}.txt)")
    return errs


# ------------------------------------------------------------- C text --------

def header_note(slug):
    if slug == "pal":
        src = ("config/data_members.pal.txt,\n * config/data_schema.pal.txt, the record headers, "
               "the symbol lists and\n * config/tables_manifest.txt")
    else:
        src = (f"config/data_members.{slug}.txt,\n * config/data_schema.{slug}.txt, "
               f"config/ee_layout.{slug}.txt,\n * config/motion_bits.{slug}.txt, the record headers, "
               f"the symbol lists and\n * config/tables_manifest.{slug}.txt")
    return (f"/* Generated by tools/gen_data_desc.py from {src}. Types, offsets, sizes and names "
            "only: no\n * disc data. Do not edit; rerun the script. */\n")


SHARED_NOTE = ("/* Generated by tools/gen_data_desc.py from config/regions.txt and every set's\n"
               " * schema, member table, symbol lists and manifest. Types, offsets, sizes and\n"
               " * names only: no disc data. Do not edit; rerun the script. */\n")


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


def member_expr(T, path):
    """An expression of the member at path of T (the element itself for "")."""
    if not path:
        return f"(*({T} *)0)"
    return f"((({T} *)0)->{path})"


def default_bits(value, f):
    """The 32-bit pattern a default row's value stores in a host field."""
    if f.count != 1 or f.ee_size > 4:
        fail(f"{f.path}: a default needs a single scalar of 4 bytes or less")
    if f.kind == "FLOAT":
        return struct.unpack("<I", struct.pack("<f", float(value)))[0]
    if f.kind == "INT":
        return int(value, 0) & ((1 << (8 * f.ee_size)) - 1)
    fail(f"{f.path}: a default for a {f.kind} field")


def gen_header(sets, pal):
    symbols = [s["syms"][0][0] for _, s, _, _, _ in pal.tables if s is not None]
    ids = [s["syms"][0][0] for _, s, _, rec, _ in pal.tables if rec is not None]
    h = [SHARED_NOTE, "#ifndef ICO_PORT_DATA_TABLE_DESC_H", "#define ICO_PORT_DATA_TABLE_DESC_H", "",
         "#include <stdint.h>", "",
         "/* What a field of a record holds (port/data/tables.c decodes each kind). */",
         "enum {",
         "    ICO_TF_INT,    /* an integer of ee_size bytes (count of them), little-endian */",
         "    ICO_TF_FLOAT,  /* an IEEE single (count of them), copied as its bits */",
         "    ICO_TF_BYTES,  /* a pointer-free union, copied as its ee_size bytes */",
         "    ICO_TF_BITS,   /* a bit-field: width bits from bit of the ee_size-byte unit */",
         "    ICO_TF_FUNC,   /* a function pointer word: the registry's function */",
         "    ICO_TF_DATA,   /* an object pointer word: the member's string pool or the",
         "                      registry's object */",
         "    ICO_TF_BITMAP, /* a 4-byte host bit word built bit by bit: host bit i is bit",
         "                      map[i] of the ee_size bytes at ee_offset (0xFF: 0) */",
         "    ICO_TF_DEFAULT /* a host field the disc's record lacks: the low host_size",
         "                      bytes of value */",
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
         "    const uint8_t *map;   /* ICO_TF_BITMAP: 32 entries, host bit 0 first */",
         "    uint32_t value;       /* ICO_TF_DEFAULT */",
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
         "/* A table with records, for ico_tables_count (port/data/tables.h). */",
         "typedef enum IcoTableId {"]
    h += [f"    ICO_TID_{n}," for n in ids]
    h += ["    ICO_TID_COUNT", "} IcoTableId;", "",
          "/* One row per (member, section) of a set's config/data_members.<slug>.txt.",
          "   A row with a record type fills `count` records of `host` from the record",
          "   at `start` bytes into the EE range and zeroes the host array's records",
          "   from there to `host_count` (a disc with fewer than PAL's); a string-pool",
          "   row (record NULL) is copied whole into `host`, `ee_hi - ee_lo` bytes of",
          "   its `host_count`. */",
          "typedef struct IcoTableRow {",
          "    const char *member;",
          "    const char *symbol; /* the table's C name; NULL for a pool row */",
          "    uint32_t section;   /* ICO_TABLE_* */",
          "    uint32_t ee_lo, ee_hi;",
          "    uint32_t crc32; /* the set's config/tables_manifest*.txt */",
          "    uint32_t start, count;",
          "    const IcoTableRecord *record;",
          "    void *host;",
          "    uint32_t host_count; /* records of the host array, or the pool's bytes */",
          "    uint32_t id;         /* IcoTableId; ICO_TID_COUNT for a pool row */",
          "} IcoTableRow;",
          "",
          "/* The registry: sorted by EE address (port/data/gen/<slug>/ee_symbols.c). */",
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
          "/* One descriptor set per row of config/regions.txt: what the loader reads",
          "   from that disc's boot ELF (port/data/gen/<slug>/). */",
          "typedef struct IcoTableSet {",
          "    const char *slug;     /* config/regions.txt's */",
          "    const char *disc_id;  /* SCES-50760 */",
          "    const char *boot_elf; /* the boot ELF's path on the disc (SYSTEM.CNF's BOOT2) */",
          "    const char *product;  /* the disc as messages name it: \"the European disc\" */",
          "    const IcoTableRow *rows;",
          "    uint32_t row_count;",
          "    const IcoEeFunc *funcs;",
          "    uint32_t func_count;",
          "    const IcoEeObject *objects;",
          "    uint32_t object_count;",
          "} IcoTableSet;",
          "",
          f"#define ICO_TABLE_SET_COUNT {len(sets)}",
          ""]
    h += [f"extern const IcoTableSet ico_table_set_{st.slug};" for st in sets]
    h += ["",
          "/* The sets in config/regions.txt's order (port/data/gen/table_sets.c): the",
          "   index is the region's row. */",
          "extern const IcoTableSet *const ico_table_sets[ICO_TABLE_SET_COUNT];",
          "",
          "/* The tables' C names, for X(name) (port/data/test/tables_test.c). */",
          "#define ICO_TABLE_SYMBOLS(X) \\"]
    h += [f"    X({n}) \\" for n in symbols]
    h += ["", "#endif /* ICO_PORT_DATA_TABLE_DESC_H */", ""]
    return "\n".join(h), symbols


def gen_defs(sets, pal):
    headers = pal.headers
    symbols = [s["syms"][0][0] for _, s, _, _, _ in pal.tables if s is not None]
    renamed = const_declared(headers, set(symbols))
    counts = {s["syms"][0][0]: s["count"] for _, s, _, rec, _ in pal.tables if rec is not None}
    pal_pools = {(m, r["section"]) for m, s, r, rec, _ in pal.tables if rec is None}
    pool_size, count_of = {}, {}
    for st in sets:
        for m, s, r, rec, _ in st.tables:
            if rec is not None:
                name = s["syms"][0][0]
                if name not in counts:
                    fail(f"{st.slug}: {m}: {name} is not one of PAL's tables")
                if (s["count"] is None) != (counts[name] is None) or \
                        (s["count"] or 0) > (counts[name] or 0):
                    fail(f"{st.slug}: {m}: {s['count']} records of {name}, PAL's array holds "
                         f"{counts[name]}: the host arrays are PAL's (the largest)")
            else:
                k = (m, r["section"])
                if k not in pal_pools:
                    fail(f"{st.slug}: {m} .{r['section']}: a string pool PAL does not have")
                pool_size[k] = max(pool_size.get(k, 0), r["hi"] - r["lo"])
            if s is not None and s["count_of"]:
                arr, less = s["count_of"]
                cnt = [x["count"] for _, x, _, rr, _ in st.tables
                       if x is not None and rr is not None and x["syms"][0][0] == arr]
                count_of.setdefault(s["syms"][0][0], set()).add((tuple(cnt), less))
    for name, vals in count_of.items():
        if len(vals) != 1:
            fail(f"{name}: derived from its array's host size, but the sets' counts differ "
                 f"({sorted(vals)})")
    d = [SHARED_NOTE,
         "/* The 73 data tables, defined on the host without initialisers: they",
         " * land in .bss and port/data/tables.c fills them from the user's ELF",
         " * before the game starts. One host layout, PAL's, for every disc's set:",
         " * each array holds PAL's count, the largest of every set. None is const",
         " * (the loader writes them all; the PS2 build's const .rodata tables were",
         " * written by the game in three places), so a header's `extern const`",
         " * declaration of one is renamed around the #includes. */",
         ""]
    d += include_block(headers, renamed)
    d.append("")
    pools = []
    for m, s, r, rec, _ in pal.tables:
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
    d.append("   char pointers (staffroll_dat), as large as the largest set's. */")
    for m, r in pools:
        d.append(f"char ico_table_pool_{c_ident(m)}_{r['section']}[{pool_size[(m, r['section'])]}];")
    for m, s, r, rec, _ in pal.tables:
        if s is not None and s["count_of"]:
            arr, less = s["count_of"]
            d.append("")
            d.append(f"/* {m}: the count of {arr}, derived as the PS2 build derives it (the")
            d.append("   same count on every set) */")
            d.append(f"int {s['syms'][0][0]} = sizeof({arr}) / sizeof({arr}[0])"
                     + (f" - {less};" if less else ";"))
    d.append("")
    return "\n".join(d), pool_size


def field_list(st, rec):
    """[(host Field, ee_off, ee_size, kind, extra)] of a record in a set."""
    if st.slug == "pal":
        return [(f, f.ee_off, f.ee_size, f.kind, None) for f in rec.fields]
    out = []
    for mp in st.maps[rec.schema["type"]]:
        f = mp.host
        if mp.how == "copy":
            out.append((f, mp.src.ee_off, mp.src.ee_size, mp.src.kind, None))
        elif mp.how == "default":
            out.append((f, 0, 0, "DEFAULT", default_bits(mp.value, f)))
        else:
            out.append((f, mp.base, mp.span, "BITMAP", mp.bits))
    return out


def gen_set(st, pal, pool_size, earlier):
    """<slug>/table_desc.c, <slug>/ee_symbols.c and <slug>/tables.cmake."""
    slug = st.slug
    note = header_note(slug)
    headers = pal.headers
    symbols = [s["syms"][0][0] for _, s, _, _, _ in pal.tables if s is not None]
    renamed = const_declared(headers, set(symbols))
    host_counts = {s["syms"][0][0]: (s["count"] or 1) for _, s, _, rec, _ in pal.tables
                   if rec is not None}
    defined = [(s["syms"][0][0], rec, s) for _, s, _, rec, _ in pal.tables if rec is not None]
    pools = [(m, r) for m, s, r, rec, _ in pal.tables if rec is None]
    records = st.record_types()

    c = [note,
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
    c.append(f"extern const IcoEeFunc ico_ee_funcs_{slug}[];")
    c.append(f"extern const IcoEeObject ico_ee_objects_{slug}[];")
    c.append("")
    for rec in records:
        if rec.typedef:
            c.append(rec.typedef)
    c.append("")
    if slug == "pal":
        c.append("/* The EE layout of each record type: pointer-free records keep it on")
        c.append("   every host (they are copied field by field all the same); records")
        c.append("   with pointers keep it on a 32-bit host only. */")
        c.append("#if UINTPTR_MAX == 0xFFFFFFFFu")
        c.append("#define ICO_TD_EE_HOST 1")
        c.append("#else")
        c.append("#define ICO_TD_EE_HOST 0")
        c.append("#endif")
        c.append("")
        for rec in records:
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
    else:
        c.append(f"/* The host records are PAL's (port/data/gen/pal/table_desc.c asserts their EE")
        c.append(f"   layout); the {slug} disc's own (config/ee_layout.{slug}.txt) gives each")
        c.append("   field's EE offset, and each record's EE size is a number in its record")
        c.append("   row. A field copied whole, a default and a bit word have their host size: */")
        for rec in records:
            T = rec.ctype
            for f, eo, es, kind, extra in field_list(st, rec):
                if kind in ("INT", "FLOAT", "BYTES"):
                    want = es * f.count
                elif kind in ("DEFAULT", "BITMAP"):
                    want = f.ee_size
                else:
                    continue
                c.append(f"_Static_assert(sizeof({member_expr(T, f.path)}) == {want}, "
                         f"\"{rec.schema['type']}.{f.path or '(element)'}: {slug} size\");")
        c.append("")

    setters, maps = {}, {}
    for rec in records:
        for i, (f, eo, es, kind, extra) in enumerate(field_list(st, rec)):
            if kind == "BITS":
                fn = f"set_{rec.key}_{i}"
                setters[(rec.key, i)] = fn
                cast = "(int32_t)" if f.signed else ""
                c.append(f"static void {fn}(void *r, uint32_t v)")
                c.append("{")
                c.append(f"    (({rec.ctype} *)r)->{f.path} = {cast}v;")
                c.append("}")
                c.append("")
            elif kind == "BITMAP":
                nm = f"map_{rec.key}_{i}"
                maps[(rec.key, i)] = nm
                vals = ", ".join("0xFF" if b is None else str(b) for b in extra)
                c.append(f"/* {rec.schema['type']}.{f.path}: host bit i is bit map[i] of the {es} "
                         f"bytes at the {slug} record's 0x{eo:X} (config/motion_bits.{slug}.txt) */")
                c.append(f"static const uint8_t {nm}[32] = {{{vals}}};")
                c.append("")
    for rec in records:
        T = rec.ctype
        c.append(f"static const IcoTableField fields_{rec.key}[] = {{")
        for i, (f, eo, es, kind, extra) in enumerate(field_list(st, rec)):
            path = f.path
            mp, val = "NULL", "0"
            if kind == "BITS":
                hoff, hsize, setter = "0", "0", setters[(rec.key, i)]
            else:
                hoff = f"offsetof({T}, {path})" if path else "0"
                el = member_expr(T, path)
                hsize = f"sizeof({el}[0])" if f.count > 1 else f"sizeof({el})"
                setter = "NULL"
            if kind == "BITMAP":
                mp = maps[(rec.key, i)]
            if kind == "DEFAULT":
                val = f"0x{extra:08X}u"
            c.append(f"    {{\"{path}\", {hoff}, {hsize}, {eo}, {es}, {f.count}, "
                     f"ICO_TF_{kind}, {int(f.signed)}, {f.bit}, {f.width}, {setter}, {mp}, {val}}},")
        c.append("};")
        c.append("")
    for rec in records:
        T = rec.ctype
        size = rec.ty.size if slug == "pal" else f"0x{st.ee_size(rec):X}"
        c.append(f"static const IcoTableRecord record_{rec.key} = {{\"{rec.schema['type']}\", "
                 f"sizeof({T}), {size}, fields_{rec.key}, "
                 f"sizeof(fields_{rec.key}) / sizeof(fields_{rec.key}[0])}};")
    c.append("")
    c.append(f"static const IcoTableRow rows_{slug}[] = {{")
    for m, s, r, rec, crc in st.tables:
        sec = f"ICO_TABLE_{r['section'].upper()}"
        if rec is None:
            c.append(f"    {{\"{m}\", NULL, {sec}, 0x{r['lo']:08X}u, 0x{r['hi']:08X}u, 0x{crc:08X}u, 0, 0, "
                     f"NULL, ico_table_pool_{c_ident(m)}_{r['section']}, "
                     f"{pool_size[(m, r['section'])]}, ICO_TID_COUNT}},")
        else:
            name = s["syms"][0][0]
            host = f"&{name}" if s["count"] is None else name
            c.append(f"    {{\"{m}\", \"{name}\", {sec}, 0x{r['lo']:08X}u, 0x{r['hi']:08X}u, 0x{crc:08X}u, "
                     f"{g.start_of(s, r)}, {s['count'] or 1}, &record_{rec.key}, (void *){host}, "
                     f"{host_counts[name]}, ICO_TID_{name}}},")
    c.append("};")
    c.append("")
    funcs = sorted((v, n) for v, k, n in st.refs if k == "func")
    objs = sorted((v, n) for v, k, n in st.refs if k == "obj")
    c.append(f"_Static_assert(sizeof(rows_{slug}) / sizeof(rows_{slug}[0]) == {len(st.tables)}, "
             f"\"{slug}: rows\");")
    c.append("")
    reg = st.region
    index = [r["slug"] for r in g.regions()].index(slug)
    c.append(f"/* {reg['product']} ({reg['disc_id']}): config/regions.txt's row {index} */")
    c.append(f"const IcoTableSet ico_table_set_{slug} = {{\"{slug}\", \"{reg['disc_id']}\", "
             f"\"{reg['boot_elf']}\", \"{reg['product']}\", rows_{slug}, {len(st.tables)}, "
             f"ico_ee_funcs_{slug}, {len(funcs)}, ico_ee_objects_{slug}, {len(objs)}}};")
    c.append("")

    # ---- ee_symbols.c
    func_names, obj_names = earlier
    first = not func_names and not obj_names
    e = [note,
         "/* The registry of the EE addresses this set's tables' pointer words hold:",
         " * each names a game function or object by the committed symbol lists, and",
         " * the loader stores the host's address of that symbol (port/data/tables.c).",
         " * The declarations are deliberately loose (the real prototypes live in",
         " * the game's headers); only the addresses are used.",
         " *",
         " * ICO_EE_SYMBOLS_STUBS (the loader test only) also defines every symbol",
         " * no earlier set's registry defines (config/regions.txt's order), each a",
         " * distinct function or object, so the test links without the game. */",
         "",
         "#include <stddef.h>",
         "#include <stdint.h>",
         "",
         '#include "port/data/gen/table_desc.h"',
         ""]
    for _, n in funcs:
        if n in obj_names:
            fail(f"{slug}: {n} is a function here and an object in an earlier set")
        e.append(f"void {n}(void);")
    for _, n in objs:
        if n in func_names:
            fail(f"{slug}: {n} is an object here and a function in an earlier set")
        e.append(f"extern char {n}[];")
    e.append("")
    e.append("#ifdef ICO_EE_SYMBOLS_STUBS")
    e.append("volatile int ico_ee_stub_hit;" if first else "extern volatile int ico_ee_stub_hit;")
    k = len(func_names)
    for _, n in funcs:
        if n in func_names:
            continue
        k += 1
        e.append(f"void {n}(void)")
        e.append("{")
        e.append(f"    ico_ee_stub_hit = {k};")
        e.append("}")
        func_names.add(n)
    for _, n in objs:
        if n in obj_names:
            continue
        e.append(f"char {n}[16] __attribute__((aligned(16)));")
        obj_names.add(n)
    e.append("#endif")
    e.append("")
    for what, arr, items, typ in (("functions", f"ico_ee_funcs_{slug}", funcs, "IcoEeFunc"),
                                  ("objects", f"ico_ee_objects_{slug}", objs, "IcoEeObject")):
        e.append(f"const {typ} {arr}[] = {{")
        for v, n in items:
            e.append(f"    {{0x{v:08X}u, {n}, \"{n}\"}},")
        if not items:
            e.append("    {0, NULL, NULL}, /* none: the set's count is 0 */")
        e.append("};")
        e.append("")
        e.append(f"_Static_assert(sizeof({arr}) / sizeof({arr}[0]) == {max(1, len(items))}, "
                 f"\"{slug}: {what}\");")
        e.append("")
    cm = [f"# Generated by tools/gen_data_desc.py. The {slug} descriptor set's sources",
          "# (port/data/gen/tables.cmake includes this file).",
          "list(APPEND ICO_TABLE_SOURCES \"${CMAKE_CURRENT_LIST_DIR}/table_desc.c\"",
          "     \"${CMAKE_CURRENT_LIST_DIR}/ee_symbols.c\")",
          "list(APPEND ICO_TABLE_EE_SYMBOLS \"${CMAKE_CURRENT_LIST_DIR}/ee_symbols.c\")",
          ""]
    return "\n".join(c), "\n".join(e), "\n".join(cm)


def generate():
    """{path under port/data/gen: text}, the sets and the audit's errors."""
    sets = all_sets()
    pal = sets[0]
    h, symbols = gen_header(sets, pal)
    defs, pool_size = gen_defs(sets, pal)
    files = {"table_desc.h": fmt(h, "table_desc.h"),
             "table_defs.c": fmt(defs, "table_defs.c")}
    names = (set(), set())
    for st in sets:
        c, e, cm = gen_set(st, pal, pool_size, names)
        files[f"{st.slug}/table_desc.c"] = fmt(c, f"{st.slug}/table_desc.c")
        files[f"{st.slug}/ee_symbols.c"] = fmt(e, f"{st.slug}/ee_symbols.c")
        files[f"{st.slug}/tables.cmake"] = cm
    ts = [SHARED_NOTE,
          '#include "port/data/gen/table_desc.h"',
          "",
          "/* config/regions.txt's rows in order */",
          "const IcoTableSet *const ico_table_sets[ICO_TABLE_SET_COUNT] = {"]
    ts += [f"    &ico_table_set_{st.slug}," for st in sets]
    ts += ["};", ""]
    files["table_sets.c"] = fmt("\n".join(ts), "table_sets.c")
    cm = ["# Generated by tools/gen_data_desc.py. The data tables' C names, for the",
          "# loader test's renamed reference copies (port/data/CMakeLists.txt); the",
          "# descriptor sets, one per row of config/regions.txt; and the sources every",
          "# program that loads the tables compiles: ICO_TABLE_SOURCES (ee_symbols.c",
          "# among them, ICO_TABLE_EE_SYMBOLS, which a test without the game compiles",
          "# with ICO_EE_SYMBOLS_STUBS).",
          "set(ICO_TABLE_SYMBOLS"]
    cm += [f"    {n}" for n in symbols]
    cm += [")",
           "set(ICO_TABLE_SLUGS " + " ".join(st.slug for st in sets) + ")",
           "set(ICO_TABLE_SOURCES \"${CMAKE_CURRENT_LIST_DIR}/table_defs.c\"",
           "    \"${CMAKE_CURRENT_LIST_DIR}/table_sets.c\")",
           "set(ICO_TABLE_EE_SYMBOLS \"\")",
           "foreach(_ico_set ${ICO_TABLE_SLUGS})",
           "    include(\"${CMAKE_CURRENT_LIST_DIR}/${_ico_set}/tables.cmake\")",
           "endforeach()",
           ""]
    files["tables.cmake"] = "\n".join(cm)
    return files, sets, audit_bits(sets)


def fmt(text, name):
    """The text as clang-format (the repo's .clang-format) writes it."""
    if not CLANG_FORMAT.exists():
        fail(f"{CLANG_FORMAT.relative_to(ROOT)} not found: run tools/setup.sh")
    r = subprocess.run([str(CLANG_FORMAT), f"--assume-filename={OUT_DIR / name}"],
                       input=text.encode(), capture_output=True, cwd=ROOT, check=False)
    if r.returncode != 0:
        fail(f"clang-format failed on {name}: {r.stderr.decode()}")
    return r.stdout.decode()


# the single set's files before there were sets: a leftover is stale
FORMER = ("table_desc.c", "ee_symbols.c")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if port/data/gen/ is stale")
    ap.add_argument("--manifest", action="store_true", help="rewrite the manifest from the ELF")
    ap.add_argument("--check-manifest", action="store_true", help="exit 1 if the manifest disagrees")
    ap.add_argument("--version", default="pal", help="the set (a slug of config/regions.txt)")
    ap.add_argument("--elf", type=Path, default=None,
                    help="the set's base ELF (default baserom/<slug>/baseelf.elf)")
    ap.add_argument("--out-dir", type=Path, default=OUT_DIR)
    ap.add_argument("-v", "--verbose", action="store_true", help="print the sets' mapping notes")
    a = ap.parse_args()
    slugs = [r["slug"] for r in g.regions()]
    if a.version not in slugs:
        fail(f"{a.version} is not a slug of config/regions.txt ({', '.join(slugs)})")
    if a.manifest or a.check_manifest:
        elf = a.elf or ROOT / f"baserom/{a.version}/baseelf.elf"
        if not elf.exists():
            fail(f"{elf}: no base ELF (tools/extract_elf.sh)")
        text = compute_manifest(elf, a.version)
        path = manifest_path(a.version)
        if a.check_manifest:
            if not path.exists() or path.read_text() != text:
                print(f"gen_data_desc: {path.relative_to(ROOT)} disagrees with {elf}",
                      file=sys.stderr)
                return 1
            return 0
        path.write_text(text)
        return 0
    files, sets, errs = generate()
    if a.verbose:
        for st in sets:
            for n in st.notes:
                print(f"gen_data_desc: {st.slug}: {n}")
    for x in errs:
        print(f"gen_data_desc: {x}", file=sys.stderr)
    if a.check:
        stale = [n for n, t in files.items()
                 if not (a.out_dir / n).exists() or (a.out_dir / n).read_text() != t]
        stale += [n for n in FORMER if (a.out_dir / n).exists()]
        if stale:
            print(f"gen_data_desc: stale in {a.out_dir}: {', '.join(stale)} "
                  "(run tools/gen_data_desc.py)", file=sys.stderr)
            return 1
        return 1 if errs else 0
    for n, t in files.items():
        p = a.out_dir / n
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.exists() or p.read_text() != t:
            p.write_text(t)
    for n in FORMER:
        if (a.out_dir / n).exists():
            (a.out_dir / n).unlink()
    return 1 if errs else 0


if __name__ == "__main__":
    raise SystemExit(main())
