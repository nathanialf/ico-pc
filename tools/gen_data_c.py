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

A symbol the member defines in .rodata is written `const`, as the developers
compiled it, unless its header declares it `extern` without const: a table
the game writes (the EE has no page protection, so those stores worked on
the PS2), which must not be const on the host either (clang deletes stores
to an object declared const, issue 19; motionOrientManager.h declares
motionLimitDef without const for that).

The record type is read from the header the schema row names: a typedef of a
struct, or a struct tag (`struct Name { ... };`, which the C then spells
`struct Name`), whose fields are integers, enums, floats, pointers (object or
function), arrays, nested structs and bit-fields, laid out by the EE's rules (4-byte
pointers and ints, 8-byte long long and double, bit-fields from the low bit
of their declared type's unit). A base type with a trailing [N] is an array
element (char[32]: the C defines `char name[count][32]`).
"""

import argparse
import re
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = ROOT / "config/data_schema.pal.txt"
TABLE = ROOT / "config/data_members.pal.txt"
BASE_ELF = ROOT / "baserom/pal/baseelf.elf"


def fail(msg):
    sys.exit(f"gen_data_c: {msg}")


# ----------------------------------------------------------------- table ----

def parse_table(path=TABLE):
    """config/data_members.pal.txt's rows: one per (member, section)."""
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

def parse_schema(path=SCHEMA):
    """The schema's rows, by member: one row per section the member's C defines."""
    rows = {}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f:
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


def strip_c(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))


def tokenize(text):
    return re.findall(r"0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|\.\.\.|[{}()\[\];,*:=+\-/]", text)


class Header:
    def __init__(self, path):
        self.path = path
        self.toks = tokenize(strip_c((ROOT / path).read_text(encoding="latin-1")))
        self.cache = {}

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
        fail(f"{self.path}: no typedef or struct tag {name}")

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


SYMBOL_LISTS = [ROOT / "config/symbol_addrs.pal.txt", ROOT / "config/symbol_addrs.pal.data.txt"]

# Globals a data member points at that neither symbol list names, at the
# address the layout link gives them (the fact `nm build/ico.layout.elf`
# prints). Each entry belongs in config/symbol_addrs.pal.data.txt; it is here
# until that file, which another work package owns, takes it.
SUPPLEMENT = [
    ("scpDummyGObj", 0x0063AA20, False),  # ico2/script/src/script.c .sdata
]

SPLAT_LINE = re.compile(r"\s*([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)")


class SymbolMap(Layout):
    """The layout link's view of addresses, built without the period link:
    every name the committed symbol lists (splat's `name = 0xADDR; // type:func`
    lines; a line marked can_be_referenced:False is skipped), the data
    members' own symbols (config/data_members.pal.txt), the SUPPLEMENT above
    and any extra list place at a retail address. The retail ELF is stripped
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
        for name, addr, func in SUPPLEMENT:
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
    if ty.kind == "struct" and getattr(ty, "name", None):
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

    def value(self, ty, off, path, bitpos=0):
        d = self.data
        if ty.kind == "bitfield":
            v = int.from_bytes(d[off:off + ty.size], "little") >> bitpos & ((1 << ty.width) - 1)
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


def c_type(ty, spelled):
    """The C spelling of an element type the schema names: its base, the stars
    before the object's name and the bounds after its own."""
    if ty.kind in ("int", "float"):
        return ty.name, "", ""
    if ty.kind == "ptr":
        inner, star, dims = c_type(ty.target, spelled[:-1])
        return inner, star + "*", dims
    if ty.kind == "array":
        inner, star, dims = c_type(ty.elem, spelled[:spelled.rindex("[")])
        return inner, star, f"[{ty.n}]" + dims
    return getattr(ty, "name", None) or spelled, "", ""


def declared_writable(header, name):
    """Whether header declares name `extern` without const: a .rodata table
    the game writes, which the host declares writable where it is written
    (motionOrientManager.h's motionLimitDef: clang deletes stores to an
    object declared const, issue 19). Its host definition must agree."""
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


def write_c(member, rows, datas, layout):
    pool = [(r["lo"], r["hi"], datas[r["section"]]) for _, r in rows]
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
        ty = Header(s["header"]).typedef(s["type"])
        start = start_of(s, row)
        n = s["count"] or 1
        size = ty.size * n
        span = row["hi"] - row["lo"] - start
        if size > span or any(data[start + size:]):
            fail(f"{member}: {n} x {s['type']} ({ty.size} B) = {size} B from offset {start}, "
                 f"but the ROM's .{row['section']} range holds {span} B" +
                 (" with nonzero bytes after it" if size <= span else ""))
        w = Writer(data[start:], row["lo"] + start, layout, s["hex"], pool)
        bounds = [i for _, i in s["syms"]] + [n]
        tname, star, dims = c_type(ty, s["type"])
        for (name, first), last in zip(s["syms"], bounds[1:]):
            # a header's declaration without const (declared_writable): the
            # definition drops const to agree with it
            const = ("const " if row["section"] == "rodata" and
                     not declared_writable(s["header"], name) else "")
            if s["count"] is None:
                defs.append(f"{const}{tname} {star}{name}{dims} = {w.value(ty, 0, '')};")
            else:
                arrays[name] = last - first
                defs.append(f"{const}{tname} {star}{name}[{last - first}]{dims} = {{")
                defs.extend(f"    {w.value(ty, i * ty.size, '')}," for i in range(first, last))
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
        f"/* {member}.o, written by tools/gen_data_c.py from baserom/pal/baseelf.elf",
        " * and config/data_schema.pal.txt. Generated: do not commit. */",
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--c", required=True, metavar="MEMBER")
    ap.add_argument("--elf", type=Path, default=BASE_ELF)
    ap.add_argument("--symbol-map", action="store_true", required=True,
                    help="name pointers from the committed symbol lists")
    ap.add_argument("--extra-symbols", type=Path, action="append", default=[],
                    help="another splat-format list (repeatable)")
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
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
