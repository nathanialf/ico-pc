#!/usr/bin/env python3
"""tools/gen_ee_layout.py -- a target's EE record layouts, and its bit words' map.

The host keeps one in-memory layout for the 73 data tables, PAL's: the
port's headers. Another target of config/regions.txt (jpn) lays some
records out differently on its disc (MotionDef is 0x190 bytes with the
words from 0x144 in another order and its three bit words reordered;
TexRec is 0x30 bytes without cameraMove). The loader converts that
target's records into the PAL host structs field by field
(tools/gen_data_desc.py's second descriptor set); this script writes what
it converts from:

  config/ee_layout.<slug>.txt
      per record type the schema names (config/data_schema.<slug>.txt), its
      EE size and every field's EE offset, size and kind, read from
      upstream's headers at a recorded revision (`git show <rev>:<header>`)
      with their VERSION_<SLUG> blocks resolved for the target
      (gen_data_c.version_lines) and laid out by the EE's rules
      (gen_data_c.Header). Committed: a function of upstream's text, so the
      port's headers, which hold PAL's layout only once upstream's blocks are
      converted, never need the target's arm.

        record <type> size=0x... header=<path>
        field <path> off=0x... size=N count=N kind=int|float|bytes|bitword|funcword|dataword [signed=0|1]
        field <path> off=0x... size=N count=1 kind=bits bit=N width=N signed=0|1
        bitfield <union>.<path> bit=N width=N

      kind=bits is a struct bit-field (the loader's ICO_TF_BITS); a bitword
      is a 4-byte union of a word and named bit-fields (MotionDef's modeBits,
      flags, flags2), whose `bitfield` rows follow it with bits counted from
      the union's first byte.

  config/motion_bits.<slug>.txt (--bits)
      the bits of MotionDef's bit words that game code reads by shift or mask
      (`modeBits.word >> 16`, `->flags2 >> 3`, `modeBits.h.hi & 7`) rather than
      by a named bit-field: upstream's two-arm blocks pair the target's
      position with PAL's read for read; a read outside any block is the
      same bit in both. Named bit-fields map by name and are not listed.

        <record> <pal-word> pal=<bit> width=<n> <slug>=<word>:<bit>   # sources

Modes:

  tools/gen_ee_layout.py --version jpn --upstream upstream/main
      write config/ee_layout.jpn.txt from that revision
  tools/gen_ee_layout.py --version jpn --check
      exit 1 if the committed file differs from what its recorded revision
      gives (exit 0 with a note when that revision is not in this clone)
  tools/gen_ee_layout.py --version jpn --check --upstream REV
      exit 1 unless REV gives the committed file but for its `upstream <sha>`
      line (a newer upstream that leaves the target's layout alone, read
      with its newer condition forms), and the same for
      config/motion_bits.<slug>.txt with --bits
  tools/gen_ee_layout.py --selftest
      the version-block readers (gen_data_c.version_lines, region_lines) on
      fixed cases of upstream's condition forms
  tools/gen_ee_layout.py --version jpn --upstream REV --bits
      write config/motion_bits.jpn.txt from upstream's game sources at REV
  tools/gen_ee_layout.py --version pal --upstream REV
      the self-consistency check: upstream's PAL arm laid out against the
      port's headers (gen_data_desc's PAL records), field by field; exit 1 on
      a difference. PAL's layout file is not committed: the port's headers
      are PAL's layout, asserted against the EE by port/data/gen/
      table_desc.h's _Static_asserts, and a second copy could only drift.

The module is also the shared reader: gen_data_desc.py (the descriptor set)
and gen_data_c.py --version (the loader test's reference) map a target's
fields onto the PAL host struct through record_map(), so the loader and its
reference cannot disagree on the map; the shift-read audit
(gen_data_desc.py --check) uses shift_reads().
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_data_c as g  # noqa: E402

ROOT = g.ROOT


def fail(msg):
    sys.exit(f"gen_ee_layout: {msg}")


def layout_path(slug):
    return ROOT / f"config/ee_layout.{slug}.txt"


def bits_path(slug):
    return ROOT / f"config/motion_bits.{slug}.txt"


# ---------------------------------------------------------- flattening -------

class Field:
    """A leaf of a record in EE layout order. kind: INT, FLOAT, BYTES (a
    pointer-free union copied whole), BITWORD (a 4-byte union of a word and
    named bit-fields), BITS (a struct bit-field), FUNC, DATA. bitnames, for a
    BITWORD: [(path in the union, bit from its first byte, width)]."""

    def __init__(self, kind, path, ee_off, ee_size, count=1, signed=False, bit=0, width=0,
                 bitnames=None):
        self.kind, self.path, self.ee_off, self.ee_size = kind, path, ee_off, ee_size
        self.count, self.signed, self.bit, self.width = count, signed, bit, width
        self.bitnames = bitnames or []


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


def union_bits(ty, path="", off=0):
    """The named bit-fields of a union, [(path, bit from its start, width)]."""
    out = []
    for name, f, o, b in ty.fields:
        if f.kind == "bitfield":
            out.append((join(path, name), 8 * (off + o) + b, f.width))
        elif f.kind == "struct":
            out += union_bits(f, join(path, name), off + o)
    return out


def flatten(ty, path, off, out, words=False):
    """The leaves of a record type, in EE layout order. Arrays of scalars stay
    one field with a count; arrays of aggregates are expanded by index; a
    union (no pointers in any of the 73 types) is copied as its bytes, or,
    with words and when it is a 4-byte word with named bit-fields, is a
    BITWORD carrying them."""
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
                flatten(el, f"{path}[{i}]", off + i * el.size, out, words)
    elif k == "struct":
        if ty.union:
            if has_pointer(ty):
                fail(f"{path}: a union holding a pointer")
            names = union_bits(ty)
            if words and ty.size == 4 and names:
                out.append(Field("BITWORD", path, off, 4, bitnames=names))
            else:
                out.append(Field("BYTES", path, off, ty.size))
            return
        for name, f, o, b in ty.fields:
            if f.kind == "bitfield":
                if f.base.size > 4:
                    fail(f"{join(path, name)}: a bit-field wider than 32 bits")
                out.append(Field("BITS", join(path, name), off + o, f.size,
                                 signed=f.base.signed, bit=b, width=f.width))
            else:
                flatten(f, join(path, name), off + o, out, words)
    else:
        fail(f"{path}: cannot describe a field of kind {k}")


# --------------------------------------------------------- upstream text -----

def git_show(rev, path):
    r = subprocess.run(["git", "-C", str(ROOT), "show", f"{rev}:{path}"], capture_output=True)
    if r.returncode != 0:
        fail(f"git show {rev}:{path}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout.decode("latin-1")


def rev_sha(rev):
    r = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "-q", f"{rev}^{{commit}}"],
                       capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def schema_records(slug):
    """[(type, header)] of the schema's record rows, first use order."""
    g.set_version(slug)
    try:
        out = []
        for rows in g.parse_schema().values():
            for s in rows:
                if s["count_of"] is None and (s["type"], s["header"]) not in out:
                    out.append((s["type"], s["header"]))
        return out
    finally:
        g.set_version("pal")


def upstream_type(rev, slug, header, typ, cache):
    if header not in cache:
        cache[header] = g.Header(header, g.version_lines(git_show(rev, header), slug))
    return cache[header].typedef(typ)


KIND_WORD = {"INT": "int", "FLOAT": "float", "BYTES": "bytes", "BITWORD": "bitword",
             "FUNC": "funcword", "DATA": "dataword", "BITS": "bits"}
WORD_KIND = {v: k for k, v in KIND_WORD.items()}


def layout_text(slug, rev):
    sha = rev_sha(rev)
    if sha is None:
        fail(f"{rev} is not a commit in this clone")
    product = {r["slug"]: r for r in g.regions()}[slug]
    cache, lines = {}, []
    recs = schema_records(slug)
    for typ, header in recs:
        ty = upstream_type(sha, slug, header, typ, cache)
        fields = []
        flatten(ty, "", 0, fields, words=True)
        lines.append(f"record {typ} size=0x{ty.size:X} header={header}")
        for f in fields:
            line = (f"field {f.path or '-'} off=0x{f.ee_off:X} size={f.ee_size} count={f.count} "
                    f"kind={KIND_WORD[f.kind]}")
            if f.kind == "INT":
                line += f" signed={int(f.signed)}"
            if f.kind == "BITS":
                line += f" bit={f.bit} width={f.width} signed={int(f.signed)}"
            lines.append(line)
            for p, b, w in f.bitnames:
                lines.append(f"bitfield {join(f.path, p)} bit={b} width={w}")
    head = [
        f"# config/ee_layout.{slug}.txt -- the EE layout of the data tables' record types on",
        f"# {product['product']} ({product['disc_id']}): per record type of",
        f"# config/data_schema.{slug}.txt, its size and every field's offset, size and kind.",
        f"# Written by tools/gen_ee_layout.py --version {slug} from upstream's headers with",
        f"# their VERSION_ blocks resolved for {product['macro']}; types and offsets only, no",
        "# disc bytes. Do not edit: rerun the script at each upstream merge.",
        "#   record <type> size=<bytes> header=<path>",
        "#   field <path> off=<offset> size=<element bytes> count=<elements> kind=<kind> ...",
        "#       kind: int float bytes (a union copied whole) bitword (a 4-byte union of a",
        "#       word and named bit-fields) bits (a struct bit-field) funcword dataword",
        "#   bitfield <union>.<path> bit=<from the union's first bit> width=<bits>",
        f"# {len(recs)} record types.",
        f"upstream {sha}",
    ]
    return "\n".join(head + lines) + "\n"


# ------------------------------------------------------------ the reader -----

class EeRecord:
    def __init__(self, typ, size, header):
        self.type, self.size, self.header = typ, size, header
        self.fields = []  # Field, EE order
        self.by_path = {}


def parse_layout(slug):
    """config/ee_layout.<slug>.txt: ({type: EeRecord}, the upstream sha)."""
    path = layout_path(slug)
    if not path.exists():
        fail(f"{path.relative_to(ROOT)} is missing: run tools/gen_ee_layout.py --version {slug} "
             "--upstream <rev>")
    recs, cur, sha = {}, None, None
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f:
            continue
        kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        if f[0] == "upstream" and len(f) == 2:
            sha = f[1]
        elif f[0] == "record":
            cur = recs[f[1]] = EeRecord(f[1], int(kv["size"], 16), kv["header"])
        elif f[0] == "field" and cur is not None:
            p = "" if f[1] == "-" else f[1]
            fld = Field(WORD_KIND[kv["kind"]], p, int(kv["off"], 16), int(kv["size"]),
                        count=int(kv["count"]), signed=kv.get("signed") == "1",
                        bit=int(kv.get("bit", 0)), width=int(kv.get("width", 0)))
            cur.fields.append(fld)
            cur.by_path[p] = fld
        elif f[0] == "bitfield" and cur is not None and cur.fields and \
                cur.fields[-1].kind == "BITWORD":
            u = cur.fields[-1]
            u.bitnames.append((f[1][len(u.path) + 1:], int(kv["bit"]), int(kv["width"])))
        else:
            fail(f"{path}:{n}: cannot read '{line}'")
    return recs, sha


def parse_bits(slug):
    """config/motion_bits.<slug>.txt: [(record, pal word, pal bit, width,
    target word or None, target bit)]; a target word '-' drops the PAL bits
    (no source: written 0) and a pal '-' row (pal word '-') declares a target
    bit nothing on the host reads."""
    path = bits_path(slug)
    if not path.exists():
        return []
    out = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        f = line.split("#", 1)[0].split()
        if not f or f[0] == "upstream":
            continue
        kv = dict(x.split("=", 1) for x in f[2:] if "=" in x)
        if len(f) != 5 or "pal" not in kv or "width" not in kv or slug not in kv:
            fail(f"{path}:{n}: expected '<record> <word> pal=<bit> width=<n> {slug}=<word>:<bit>'")
        src = kv[slug]
        if src == "-":
            tw, tb = None, 0
        else:
            tw, tb = src.split(":")
            tb = int(tb)
        out.append((f[0], f[1], None if kv["pal"] == "-" else int(kv["pal"]), int(kv["width"]),
                    tw, tb, n))
    return out


# --------------------------------------------------------------- the map -----

class Mapped:
    """One host field of a target's record, mapped from the target's layout.
    how: 'copy' (src the target's Field), 'default' (value: the text of the
    schema's default row), 'bitmap' (base: the target's first byte the map
    reads, span: bytes, bits: 32 entries, each the target's bit from base or
    None)."""

    def __init__(self, host, how, src=None, value=None, base=0, span=0, bits=None):
        self.host, self.how, self.src, self.value = host, how, src, value
        self.base, self.span, self.bits = base, span, bits


def record_map(slug, typ, member, host_fields, recs=None, defaults=None, bits_rows=None,
               report=None):
    """The host (PAL) fields of a record type, each mapped onto the target's
    layout BY PATH NAME: [Mapped] in host order. host_fields: the PAL
    record's flatten(..., words=True). A target field with no PAL name is an
    error; a PAL field the target lacks takes the schema's default row (an
    error without one); a bit word is a 32-entry bit map from the named
    bit-fields (by path, else by name over the record's bit words) and the
    rows of config/motion_bits.<slug>.txt. report, a list, collects notes
    (a named bit that moved words, a target bit no host bit takes)."""
    if recs is None:
        recs, _ = parse_layout(slug)
    if typ not in recs:
        fail(f"config/ee_layout.{slug}.txt has no record {typ}: rerun tools/gen_ee_layout.py")
    rec = recs[typ]
    if defaults is None:
        g.set_version(slug)
        defaults = g.parse_defaults()
        g.set_version("pal")
    if bits_rows is None:
        bits_rows = parse_bits(slug)
    used, out = set(), []
    tgt_words = [f for f in rec.fields if f.kind == "BITWORD"]
    # every named target bit, by its full path and by its name
    by_full, by_leaf = {}, {}
    for w in tgt_words:
        for p, b, wd in w.bitnames:
            key = (w.path, p)
            by_full[join(w.path, p)] = (w, b, wd, key)
            by_leaf.setdefault(p.rsplit(".", 1)[-1], []).append((w, b, wd, key))
    taken = {}  # (target word path, target bit) -> host word/bit that takes it
    named_used = set()
    rows = [r for r in bits_rows if r[0] == typ]
    for hf in host_fields:
        if hf.kind == "BITWORD":
            words = {w.path: w for w in tgt_words}
            if hf.path not in words:
                fail(f"{typ}.{hf.path}: a bit word on the host, not one in "
                     f"config/ee_layout.{slug}.txt")
            used.add(hf.path)
            bits = [None] * 32
            abs_bits = [None] * 32  # (target word, bit)

            def put(hbit, w, b, why):
                if abs_bits[hbit] is not None:
                    fail(f"{typ}.{hf.path} bit {hbit}: mapped twice ({why})")
                abs_bits[hbit] = (w, b)
                k = (w.path, b)
                if k in taken:
                    fail(f"{typ}: {slug} {w.path} bit {b} feeds {taken[k]} and "
                         f"{hf.path} bit {hbit}")
                taken[k] = f"{hf.path} bit {hbit}"
            for p, hb, wd in hf.bitnames:
                full = join(hf.path, p)
                hit = by_full.get(full)
                if hit is None:
                    leaf = by_leaf.get(p.rsplit(".", 1)[-1], [])
                    if len(leaf) == 1:
                        hit = leaf[0]
                        if report is not None:
                            report.append(f"{typ}: {full} (PAL) is {join(hit[0].path, hit[3][1])} "
                                          f"on {slug}: mapped by name")
                if hit is None:
                    covered = [r for r in rows if r[1] == hf.path and r[2] is not None
                               and r[2] <= hb < r[2] + r[3]]
                    if covered:
                        continue  # a motion_bits row supplies it
                    fail(f"{typ}.{full}: a PAL bit-field with no field of that name on {slug} "
                         f"and no config/motion_bits.{slug}.txt row")
                w, tb, twd, key = hit
                if twd != wd:
                    fail(f"{typ}.{full}: {wd} bits on PAL, {twd} on {slug}")
                named_used.add(key)
                for i in range(wd):
                    put(hb + i, w, tb + i, full)
            for r in rows:
                _, pw, pb, wd, tw, tb, n = r
                if pw != hf.path or pb is None:
                    continue
                if tw is None:
                    for i in range(wd):
                        if abs_bits[pb + i] is not None:
                            fail(f"config/motion_bits.{slug}.txt:{n}: bit {pb + i} of {pw} is named")
                        abs_bits[pb + i] = "zero"
                    continue
                if tw not in words:
                    fail(f"config/motion_bits.{slug}.txt:{n}: {tw} is not a bit word of {typ}")
                for i in range(wd):
                    put(pb + i, words[tw], tb + i, f"motion_bits.{slug}.txt:{n}")
            base = min(w.ee_off for w in tgt_words)
            span = max(w.ee_off + 4 for w in tgt_words) - base
            for i, a in enumerate(abs_bits):
                if a is not None and a != "zero":
                    bits[i] = 8 * (a[0].ee_off - base) + a[1]
                    if bits[i] >= 255:
                        fail(f"{typ}.{hf.path}: bit map entry {bits[i]} does not fit a byte")
            out.append(Mapped(hf, "bitmap", base=base, span=span, bits=bits))
            continue
        src = rec.by_path.get(hf.path)
        if src is None:
            if (member, hf.path) in defaults:
                out.append(Mapped(hf, "default", value=defaults[(member, hf.path)]))
                continue
            fail(f"{typ}.{hf.path or '(element)'}: a PAL field {slug} lacks, and no "
                 f"'default {member} {hf.path}=<value>' row in config/data_schema.{slug}.txt")
        hk = "BYTES" if hf.kind == "BITWORD" else hf.kind
        if src.kind != hk or src.ee_size != hf.ee_size or src.count != hf.count or \
                src.signed != hf.signed or (hk == "BITS" and (src.bit, src.width) != (hf.bit, hf.width)):
            fail(f"{typ}.{hf.path or '(element)'}: {KIND_WORD[hf.kind]} of {hf.ee_size} x {hf.count} "
                 f"on PAL, {KIND_WORD[src.kind]} of {src.ee_size} x {src.count} on {slug}")
        used.add(hf.path)
        out.append(Mapped(hf, "copy", src=src))
    extra = [f.path for f in rec.fields if f.path not in used]
    if extra:
        fail(f"{typ}: {slug} fields with no PAL field of that name: {', '.join(extra)}")
    # every named target bit and every motion_bits source must land somewhere
    for w in tgt_words:
        for p, b, wd in w.bitnames:
            if (w.path, p) in named_used:
                continue
            if all((w.path, b + i) in taken for i in range(wd)):
                continue
            declared = [r for r in rows if r[2] is None and r[4] == w.path and
                        r[5] <= b < r[5] + r[3]]
            if declared:
                if report is not None:
                    report.append(f"{typ}: {slug} {join(w.path, p)} has no PAL reader (declared)")
                continue
            fail(f"{typ}: {slug} bit-field {join(w.path, p)} (bit {b}) has no PAL field of that "
                 f"name and no config/motion_bits.{slug}.txt row")
    return out


def record_maps(slug, host_records):
    """{type: [Mapped]} for every record of the target's schema.
    host_records: {type: (member, PAL host fields with words=True)}."""
    recs, _ = parse_layout(slug)
    g.set_version(slug)
    defaults = g.parse_defaults()
    g.set_version("pal")
    rows = parse_bits(slug)
    report = []
    maps = {t: record_map(slug, t, m, hf, recs, defaults, rows, report)
            for t, (m, hf) in host_records.items()}
    return maps, recs, report


# ------------------------------------------------------------ shift reads ----

WORDS = ("modeBits", "flags2", "flags")

READ_FORMS = [
    # (x.modeBits.word << a) >> b: a field of 32 - b bits from b - a
    ("shl", re.compile(r"\b(modeBits|flags2|flags)\s*(?:\.\s*(?:word|w)\b)?\s*<<\s*(\d+)\s*\)\s*>>\s*(\d+)")),
    # (x.modeBits.word >> n) & mask, ->flags2 >> n) & mask
    ("shr", re.compile(r"\b(modeBits|flags2|flags)\s*(?:\.\s*(?:word|w)\b)?\s*>>\s*(\d+)\s*\)?\s*"
                       r"(?:&\s*(0[xX][0-9A-Fa-f]+|\d+)\b)?")),
    # modeBits.h.hi & mask, .h.lo, .b
    ("half", re.compile(r"\b(modeBits)\s*\.\s*h\s*\.\s*(hi|lo)\b\s*(?:&\s*(0[xX][0-9A-Fa-f]+|\d+)\b)?")),
    ("byte", re.compile(r"\b(modeBits)\s*\.\s*b\b")),
    # (int)x->flags >= 0, < 0: the sign bit
    ("sign", re.compile(r"\b(modeBits|flags2|flags)\s*(?:\.\s*(?:word|w)\b)?\s*\)?\s*(?:>=|<)\s*0\b(?!\.)")),
    # x.flags.word & mask, ->flags2 & mask (no shift)
    ("and", re.compile(r"\b(modeBits|flags2|flags)\s*(?:\.\s*(?:word|w)\b)?\s*\)?\s*&\s*(0[xX][0-9A-Fa-f]+|\d+)\b")),
    # x.flags.bits.loop
    ("named", re.compile(r"\b(modeBits|flags2|flags)\s*\.\s*bits\s*\.\s*(\w+)")),
]


def mask_bits(mask, base):
    m = int(mask, 0)
    return [base + i for i in range(32) if m >> i & 1 and base + i < 32]


def find_reads(text):
    """Every read of a bit word in text (one statement or a whole file with
    newlines folded): [(pos, word, bits or None, name or None)], left to
    right; overlapping matches of later forms are skipped."""
    hits, spans = [], []
    for form, rx in READ_FORMS:
        for m in rx.finditer(text):
            if any(a <= m.start() < b for a, b in spans):
                continue
            w = m.group(1)
            if form == "shl":
                a, b = int(m.group(2)), int(m.group(3))
                bits = list(range(b - a, 32 - a))
            elif form == "shr":
                n = int(m.group(2))
                bits = mask_bits(m.group(3), n) if m.group(3) else list(range(n, 32))
            elif form == "half":
                base = 16 if m.group(2) == "hi" else 0
                bits = mask_bits(m.group(3), base) if m.group(3) else list(range(base, base + 16))
            elif form == "byte":
                bits = list(range(0, 8))
            elif form == "sign":
                bits = [31]
            elif form == "and":
                bits = mask_bits(m.group(2), 0)
            else:
                hits.append((m.start(), w, None, m.group(2)))
                spans.append((m.start(), m.end()))
                continue
            hits.append((m.start(), w, bits, None))
            spans.append((m.start(), m.end()))
    return sorted(hits)


def fold(text):
    """text without comments and string literals; its line breaks are kept,
    so a line number counted in it is the source's."""
    text = re.sub(r"/\*.*?\*/", lambda m: " " + "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    return text


SUBJECT = re.compile(r"\b(MotionDef|MotionRec)\s*\*\s*(?:const\s+)?(\w+)")


def motion_reads(text, positions=False):
    """The reads of MotionDef's bit words in a C file's text:
    [(line, word, bits, name)]. A read counts when its subject names
    motionKind, CHAINROW or a variable the file declares MotionDef * or
    MotionRec * (the view struct act-game.c reads motionKind through), within
    the statement before the read. With positions, each tuple also carries
    the read's offset in fold(text): (line, pos, word, bits, name)."""
    folded = fold(text)
    names = {m.group(2) for m in SUBJECT.finditer(folded)} | {"motionKind", "CHAINROW"}
    subj = re.compile(r"\b(" + "|".join(sorted(names)) + r")\b")
    out = []
    for pos, w, bits, name in find_reads(folded):
        start = max(folded.rfind(";", 0, pos), folded.rfind("{", 0, pos), folded.rfind("}", 0, pos))
        if not subj.search(folded[start + 1:pos]):
            continue
        line = folded.count("\n", 0, pos) + 1
        out.append((line, pos, w, bits, name) if positions else (line, w, bits, name))
    return out


# ----------------------------------------------------- upstream's blocks -----

def region_lines(text, slug):
    """Each line of text tagged 'both' (compiled by the target and by PAL
    alike), 'J' (the target's text of a VERSION_ block), 'P' (PAL's), '#' (a
    version block's directive) or '-' (an arm neither compiles), and the
    blocks whose two texts differ as [(first line, J text, P text)]. The
    condition forms are upstream's (gen_data_c.version_condition: || of
    defined(), the !defined pair, #elif, one block nested in another's arm);
    other conditionals pass through. A block whose target text is PAL's (a
    Korean-only block read for jpn) is 'both'."""
    tags, blocks = [], []
    stack = []   # (a VERSION_ block, target live, pal live, target took an arm, pal took an arm)
    cur = None   # [first line, [(tag lines)]] of the outermost version block
    for n, line in enumerate(text.splitlines(), 1):
        v = g.version_condition(line)
        if v and v[1] is not None or v and stack and stack[-1][0] and v[0] in ("elif", "else", "endif"):
            kw, slugs, positive = v
            g.refuse_plain_elif(kw, slugs, bool(stack and stack[-1][0]), n, line)
            if kw in ("if", "ifdef", "ifndef"):
                t = g.condition_holds(slugs, positive, slug)
                p = g.condition_holds(slugs, positive, "pal")
                if not any(f[0] for f in stack):
                    cur = [n, []]
                stack.append((True, t, p, t, p))
            elif kw == "elif":
                _, _, _, tt, pt = stack[-1]
                t = not tt and g.condition_holds(slugs, positive, slug)
                p = not pt and g.condition_holds(slugs, positive, "pal")
                stack[-1] = (True, t, p, tt or t, pt or p)
            elif kw == "else":
                _, _, _, tt, pt = stack[-1]
                stack[-1] = (True, not tt, not pt, True, True)
            elif kw == "endif":
                stack.pop()
            tags.append("#")
            if cur is not None:
                cur[1].append(("#", line))
            if kw == "endif" and cur is not None and not any(f[0] for f in stack):
                jt = "\n".join(x for k, x in cur[1] if k in ("J", "both"))
                pt = "\n".join(x for k, x in cur[1] if k in ("P", "both"))
                first, body = cur[0], cur[1]
                start = len(tags) - len(body)
                if jt == pt:
                    for i, (k, _) in enumerate(body):
                        if k != "#":
                            tags[start + i] = "both" if k in ("J", "P", "both") else "-"
                else:
                    blocks.append((first, jt, pt))
                cur = None
            continue
        if v and v[0] in ("if", "ifdef", "ifndef"):
            stack.append((False, True, True, True, True))
        elif v and v[0] == "endif" and stack:
            stack.pop()
        t = all(f[1] for f in stack if f[0])
        p = all(f[2] for f in stack if f[0])
        tag = "both" if t and p else "J" if t else "P" if p else "-"
        if cur is None:
            tag = "both"
        else:
            cur[1].append((tag, line))
        tags.append(tag)
    return tags, blocks


def upstream_files(rev):
    """The game sources of upstream's tree at rev."""
    r = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", rev, "--", "ico2"],
                       capture_output=True, text=True)
    return [p for p in r.stdout.split() if p.endswith((".c", ".h", ".inc"))]


def named_bits(rec, word, name):
    w = rec.by_path.get(word)
    if w is None or w.kind != "BITWORD":
        return None
    for p, b, wd in w.bitnames:
        if p.rsplit(".", 1)[-1] == name:
            return list(range(b, b + wd))
    return None


# The blocks the scan cannot pair, each checked by hand: the PAL arm reads a
# named bit through a byte view (its EE offset), or masks after the read.
# A new unpaired block stops --bits until it is understood and listed here.
UNPAIRED = [
    ("rec[399] & 1", "PAL flags bit 24 (byte 0x18F bit 0) = intr10Live, named on both"),
    ("f18E & 1", "PAL flags bit 16 (MotRecJ's halfword 0x18E) = jumpHit, named on both"),
    ("->f18C, fl >> 7", "PAL flags bit 7 (CarryMot's byte 0x18C) = carried, named on both"),
    ("modeBits.w >> 19;", "masked by `mode &= 7` after: PAL bits 19-21, the row pal=19 width=3"),
]


# Rows no read gives, written by hand with their evidence: a bit no code reads
# on either disc, set in the same records on both (the host word then equals
# the PAL disc's). (slug, pal word, pal bit, width, target word, target bit,
# why)
HAND_ROWS = [
    ("jpn", "flags", 2, 1, "flags", 6,
     "no reader in either tree; the same 7 motions (834, 839, 840, 848-851) set PAL bit 2 "
     "and JPN bit 6 in the two ELFs"),
    ("kor", "flags", 2, 1, "flags", 6,
     "no reader in either tree; the Korean ELF's 1148 flags words equal the Japanese "
     "ELF's, so the same 7 motions (834, 839, 840, 848-851) set KOR bit 6"),
]


def explain(pal_text):
    flat = " ".join(pal_text.split())
    for key, why in UNPAIRED:
        if key in flat:
            return why
    return None


def scan_bits(slug, rev):
    """The rows of config/motion_bits.<slug>.txt from upstream's sources at
    rev, and the notes (pairs that agree with the named bits, reads that do
    not pair)."""
    sha = rev_sha(rev)
    if sha is None:
        fail(f"{rev} is not a commit in this clone")
    tgt, _ = parse_layout(slug)
    # PAL's MotionDef from upstream's PAL arm (the port's header holds the same)
    cache = {}
    pal_ty = upstream_type(sha, "pal", "ico2/sugipon/include/motionOrientManager.h", "MotionDef", cache)
    pal_fields = []
    flatten(pal_ty, "", 0, pal_fields, words=True)
    pal = EeRecord("MotionDef", pal_ty.size, "")
    for f in pal_fields:
        pal.fields.append(f)
        pal.by_path[f.path] = f
    jrec = tgt["MotionDef"]
    pairs = {}  # (pal word, pal bit) -> (target word, target bit, source)
    notes = []

    def add(pw, pb, tw, tb, where):
        k = (pw, pb)
        if k in pairs and pairs[k][:2] != (tw, tb):
            fail(f"{where}: PAL {pw} bit {pb} is {slug} {tw}:{tb} here but "
                 f"{pairs[k][0]}:{pairs[k][1]} at {pairs[k][2]}")
        if k not in pairs:
            pairs[k] = (tw, tb, where)
        else:
            pairs[k] = (tw, tb, pairs[k][2] if where in pairs[k][2] else f"{pairs[k][2]}, {where}")

    def bits_of(rec, w, bits, name):
        return bits if bits is not None else named_bits(rec, w, name)

    files = upstream_files(sha)
    for path in files:
        text = git_show(sha, path)
        if "modeBits" not in text and "flags" not in text:
            continue
        tags, blocks = region_lines(text, slug)
        lines = text.splitlines()
        both = "\n".join(l if t == "both" else "" for l, t in zip(lines, tags))
        for line, w, bits, name in motion_reads(both):
            if bits is None:
                continue  # a named read: mapped by name
            for b in bits:
                add(w, b, w, b, f"{path}:{line}")
        for first, jt, pt in blocks:
            jr = [r for r in find_reads(fold(jt)) if r[1] in WORDS]
            pr = [r for r in find_reads(fold(pt)) if r[1] in WORDS]
            if not jr and not pr:
                continue
            if not (re.search(r"motionKind|CHAINROW|\brow\b|\brec\d?\b", jt + pt)):
                continue
            if len(jr) != len(pr):
                notes.append((f"{path}:{first}", f"{len(jr)} {slug} reads against {len(pr)} PAL "
                              "reads", pt))
                continue
            for (_, jw, jb, jn), (_, pw, pb, pn) in zip(jr, pr):
                jb = bits_of(jrec, jw, jb, jn)
                pb = bits_of(pal, pw, pb, pn)
                if jb is None or pb is None or len(jb) != len(pb):
                    notes.append((f"{path}:{first}", f"{jw} bits {jb} against {pw} bits {pb}", pt))
                    continue
                for a, b in zip(pb, jb):
                    add(pw, a, jw, b, f"{path}:{first}")
    for hs, pw, pb, wd, tw, tb, why in HAND_ROWS:
        if hs == slug:
            for i in range(wd):
                add(pw, pb + i, tw, tb + i, f"by hand: {why}")
    # drop what the named bits already give
    named = {}
    for hf in pal.fields:
        if hf.kind != "BITWORD":
            continue
        for p, hb, wd in hf.bitnames:
            leaf = p.rsplit(".", 1)[-1]
            src = None
            for w in jrec.fields:
                if w.kind == "BITWORD":
                    for q, tb, twd in w.bitnames:
                        if q.rsplit(".", 1)[-1] == leaf:
                            src = (w.path, tb)
            if src:
                for i in range(wd):
                    named[(hf.path, hb + i)] = (src[0], src[1] + i)
    rows = []
    for (pw, pb), (tw, tb, where) in sorted(pairs.items(), key=lambda x: (WORDS.index(x[0][0]), x[0][1])):
        if (pw, pb) in named:
            if named[(pw, pb)] != (tw, tb):
                fail(f"{where}: PAL {pw} bit {pb} reads {slug} {tw}:{tb}, but the named bit-field "
                     f"there is {named[(pw, pb)][0]}:{named[(pw, pb)][1]}")
            continue
        rows.append([pw, pb, 1, tw, tb, where])
    # target bits named on the target with no PAL name and no read pairing:
    # chainIkRate-like bits a PAL read by shift takes are in rows already
    # merge runs of consecutive bits with one source
    merged = []
    for r in rows:
        if merged and merged[-1][0] == r[0] and merged[-1][3] == r[3] and \
                merged[-1][1] + merged[-1][2] == r[1] and merged[-1][4] + merged[-1][2] == r[4] and \
                merged[-1][5] == r[5]:
            merged[-1][2] += 1
        else:
            merged.append(list(r))
    return merged, notes, sha


def bits_text(slug, rows, sha, notes=()):
    product = {r["slug"]: r for r in g.regions()}[slug]
    head = [
        f"# config/motion_bits.{slug}.txt -- the bits of MotionDef's bit words (modeBits, flags,",
        f"# flags2) that game code reads by shift or mask, where {product['product']}",
        f"# ({product['disc_id']}) keeps them. Written by tools/gen_ee_layout.py --bits from",
        f"# upstream's sources: a {product['macro']} block's reads are paired with its PAL arm's,",
        "# read for read; a read outside any block is the same bit on both discs. The named",
        f"# bit-fields map by name (config/ee_layout.{slug}.txt) and are not listed. The loader's",
        "# bit map (ICO_TF_BITMAP, tools/gen_data_desc.py) builds each PAL host word from these.",
        "#   MotionDef <pal word> pal=<first bit> width=<bits> " + slug + "=<word>:<first bit>",
        "#   (pal=- declares a target bit no host bit takes; " + slug + "=- a PAL bit written 0)",
        f"upstream {sha}",
    ]
    head += ["# Blocks not paired read for read, checked by hand (UNPAIRED in the script):"]
    head += [f"#   {where}: {explain(pt)}" for where, _, pt in notes]
    body = [f"MotionDef {pw} pal={pb} width={wd} {slug}={tw}:{tb}  # {where}"
            for pw, pb, wd, tw, tb, where in rows]
    return "\n".join(head + body) + "\n"


# ---------------------------------------------------- the PAL self-check -----

def pal_self_check(rev):
    """Upstream's PAL arm against the port's headers: [differences]."""
    sha = rev_sha(rev)
    if sha is None:
        fail(f"{rev} is not a commit in this clone")
    cache, diffs = {}, []
    for typ, header in schema_records("pal"):
        up = upstream_type(sha, "pal", header, typ, cache)
        port = g.Header(header).typedef(typ)
        a, b = [], []
        flatten(up, "", 0, a, words=True)
        flatten(port, "", 0, b, words=True)
        if up.size != port.size:
            diffs.append(f"{typ}: size 0x{up.size:X} upstream, 0x{port.size:X} in the port")
        ka = [(f.path, f.ee_off, f.ee_size, f.count, f.kind, f.signed, f.bit, f.width,
               tuple(f.bitnames)) for f in a]
        kb = [(f.path, f.ee_off, f.ee_size, f.count, f.kind, f.signed, f.bit, f.width,
               tuple(f.bitnames)) for f in b]
        for x, y in zip(ka, kb):
            if x != y:
                diffs.append(f"{typ}: upstream {x[:8]} / port {y[:8]}")
        if len(ka) != len(kb):
            diffs.append(f"{typ}: {len(ka)} fields upstream, {len(kb)} in the port")
    return diffs


def drop_sha(text):
    return [line for line in text.splitlines() if not line.startswith("upstream ")]


def same_but_sha(slug, rev, bits):
    """Exit status of --check --upstream: REV gives the committed file but
    for its upstream line."""
    if rev_sha(rev) is None:
        fail(f"{rev} is not a commit in this clone")
    if bits:
        path = bits_path(slug)
        rows, notes, sha = scan_bits(slug, rev)
        got = bits_text(slug, rows, sha, notes)
    else:
        path = layout_path(slug)
        got = layout_text(slug, rev)
    want = path.read_text()
    name = path.relative_to(ROOT)
    if drop_sha(got) == drop_sha(want):
        print(f"gen_ee_layout: {rev} gives {name} but for its upstream line")
        return 0
    import difflib
    sys.stderr.writelines(difflib.unified_diff(want.splitlines(True), got.splitlines(True),
                                               str(name), f"{rev}", n=1))
    print(f"gen_ee_layout: {rev} does not give {name}", file=sys.stderr)
    return 1


# (text, slug, the 'both' lines, [(first line, J text, P text)])
REGION_LINES_SELF_TEST = (
    ("a\n#ifdef VERSION_JPN\nj\n#else\np\n#endif\nz", "jpn", "a z", [(2, "j", "p")]),
    ("a\n#ifdef VERSION_JPN\nj\n#else\np\n#endif\nz", "kor", "a p z", []),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\njk\n#else\np\n#endif", "kor", "", [(1, "jk", "p")]),
    ("#if !defined(VERSION_JPN) && !defined(VERSION_KOR)\np\n#endif", "jpn", "", [(1, "", "p")]),
    ("#ifdef VERSION_KOR\nk\n#else\np\n#endif", "jpn", "p", []),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\nx\n#ifdef VERSION_KOR\nk\n#else\nj\n#endif\n"
     "#else\np\n#endif", "kor", "", [(1, "x\nk", "p")]),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\nx\n#ifdef VERSION_KOR\nk\n#else\nj\n#endif\n"
     "#else\np\n#endif", "jpn", "", [(1, "x\nj", "p")]),
    ("#ifdef VERSION_KOR\nk\n#elif defined(VERSION_JPN)\nj\n#else\np\n#endif", "jpn", "", [(1, "j", "p")]),
    ("#ifdef VERSION_JPN\n#if 1\nj\n#endif\n#endif\nz", "jpn", "z", [(1, "#if 1\nj\n#endif", "")]),
)


def region_lines_self_test():
    bad = []
    for text, slug, both, blocks in REGION_LINES_SELF_TEST:
        tags, got = region_lines(text, slug)
        kept = " ".join(l for l, t in zip(text.splitlines(), tags) if t == "both")
        if kept != both or got != blocks:
            bad.append(f"region_lines({text!r}, {slug}): both {kept!r} blocks {got}, want {both!r} {blocks}")
    for text in g.VERSION_LINES_REFUSED:
        try:
            region_lines(text, "jpn")
            bad.append(f"region_lines({text!r}) read it, want a ValueError")
        except ValueError:
            pass
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", help="a slug of config/regions.txt")
    ap.add_argument("--selftest", action="store_true", help="test the version-block readers")
    ap.add_argument("--upstream", help="the upstream revision to read (upstream/main)")
    ap.add_argument("--check", action="store_true", help="exit 1 if the layout file is stale")
    ap.add_argument("--bits", action="store_true", help="write config/motion_bits.<slug>.txt")
    a = ap.parse_args()
    if a.selftest:
        bad = g.version_lines_self_test() + region_lines_self_test()
        for x in bad:
            print(f"gen_ee_layout: selftest: {x}", file=sys.stderr)
        print(f"gen_ee_layout: selftest {'FAILED' if bad else 'passed'}")
        return 1 if bad else 0
    if not a.version:
        fail("give --version <slug> (or --selftest)")
    slugs = [r["slug"] for r in g.regions()]
    if a.version not in slugs:
        fail(f"{a.version} is not a slug of config/regions.txt ({', '.join(slugs)})")
    if a.version == "pal":
        if not a.upstream:
            fail("--version pal compares upstream's PAL arm with the port's headers: give --upstream")
        diffs = pal_self_check(a.upstream)
        for x in diffs:
            print(f"gen_ee_layout: {x}", file=sys.stderr)
        print(f"gen_ee_layout: upstream's PAL arm and the port's headers: "
              f"{'agree' if not diffs else f'{len(diffs)} differences'} "
              f"over {len(schema_records('pal'))} record types")
        return 1 if diffs else 0
    if a.check and a.upstream:
        return same_but_sha(a.version, a.upstream, a.bits)
    if a.bits:
        if not a.upstream:
            fail("--bits needs --upstream")
        rows, notes, sha = scan_bits(a.version, a.upstream)
        unknown = [n for n in notes if explain(n[2]) is None]
        for where, what, pt in unknown:
            print(f"gen_ee_layout: {where}: {what}: not paired, and not in UNPAIRED", file=sys.stderr)
        if unknown:
            return 1
        bits_path(a.version).write_text(bits_text(a.version, rows, sha, notes))
        return 0
    if a.check:
        _, sha = parse_layout(a.version)
        if sha is None or rev_sha(sha) is None:
            print(f"gen_ee_layout: upstream {sha} is not in this clone: "
                  f"config/ee_layout.{a.version}.txt not checked")
            return 0
        if layout_path(a.version).read_text() != layout_text(a.version, sha):
            print(f"gen_ee_layout: config/ee_layout.{a.version}.txt is stale against {sha} "
                  "(rerun tools/gen_ee_layout.py)", file=sys.stderr)
            return 1
        return 0
    if not a.upstream:
        fail("give --upstream <rev> (or --check)")
    layout_path(a.version).write_text(layout_text(a.version, a.upstream))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
