#!/usr/bin/env python3
"""Convert upstream's compile-time region blocks into run-time forms.

The decompilation builds one ELF per disc from one tree: every difference
between the discs is an inline block on the target's macro,

  #ifdef VERSION_JPN  J  #else  P  #endif       (P is the European disc's text)
  #ifdef VERSION_JPN  J  #endif
  #ifndef VERSION_JPN P  #endif
  #if defined(VERSION_JPN) || defined(VERSION_KOR)  JK  #else  P  #endif
  #if !defined(VERSION_JPN) && !defined(VERSION_KOR)  P  #endif
  #ifdef VERSION_US U #elif defined(VERSION_JPN) || defined(VERSION_KOR) J #else P #endif

A condition is any boolean expression over defined(VERSION_X) with ||, &&,
! and parentheses (#ifdef/#ifndef are sugar for it); a region block may hold
one more region block in an arm (upstream's `#ifdef VERSION_KOR` inside a
JPN||KOR arm, whose #else is the Japanese text). A row no condition names
takes the arm the PAL row takes, which is upstream's own rule. The tool reads
a block as per-row texts (the lines each row's build compiles) and its arms
as the distinct texts, each keyed by the SET of rows that compile it (a
tuple of slugs in table order; the PAL row's arm is "pal"). A nested block
is rebuilt flat in memory (`flatten_blocks`), its shared lines repeated in
each arm; a block no class converts goes back as written.

The port compiles one game core that plays every disc in config/regions.txt
(one row per target: slug, upstream macro, disc id, boot ELF, product name;
row 0 is the European disc, the PAL arm). The region is the run-time index
`ico_region`; ico2/common/include/region.h (written by `gen-header`) gives
ICO_<SLUG>, ICO_<SET> for every set of two or more non-PAL rows (a set's
name is its rows' slugs concatenated in table order: ICO_JPNKOR is
ICO_JPN || ICO_KOR), and one selector macro per grouping of non-PAL rows
into arms, named PAL_ and the arms' set names joined by _: PAL_JPN(pal, jpn),
PAL_JPNKOR(pal, jpnkor), PAL_JPN_KOR(pal, jpn, kor). The first argument is
the PAL arm (upstream's #else), then one per arm in table order; a region a
macro does not name takes the PAL arm. A selector names the rows whose value
differs from PAL's, rows with one value sharing an argument. LN(pal, jpn,
kor) takes one value per row (its arity is the row count).

`convert` rewrites each block by the first class that fits, keeping the PAL
arm's bytes and indentation (the class names are the `auto` lines of
`report --all`):

  case-shift  a switch whose every top-level label lies in a block with one
              uniform delta d keeps the PAL labels and gets its head
              rewritten once, `switch (e + PAL_JPN(0, d)) { /* JPN <kind>
              ids d lower */` (per row: PAL_JPN_KOR(0, d, e), rows with one
              delta sharing it); any other mix is the `case-mixed` hand item
  token       equal token counts, the differing tokens all literals: each
              becomes PAL_JPN(p, j). The PAL token stays alone, with a
              `layout` line, after >> or <<, in an array dimension, inside
              sizeof, after + or - in a statement with a (char *)-style
              cast, inside a struct, on a member name (the structs differ),
              and on a delta of exactly 16 on a value of 0x200 and up (the
              ActWork shift); a static local's initialiser is the
              `data-twin` hand item; a delta outside DELTAS_KNOWN is a
              `review` line
  initialised an initialised against an uninitialised local: the
              initialised arm
  span        the arms differ in one balanced span, widened until it is a
              whole operand: PAL_JPN(span_pal, span_jpn); a span that reads
              a motion record's bit words keeps PAL's (`layout` bit-word)
  if-else     whole statements: if (ICO_JPN) { J } else { P }, if
              (ICO_JPNKOR) for a shared arm (named arms
              first in upstream's order, PAL last; three arms give an
              else-if chain), the arms indented one step
  one-region  if (ICO_JPN) { J } / if (!ICO_JPN) { P } / if (!ICO_JPNKOR); an arm of
              declarations alone (no call or assignment in an initialiser)
              is kept for every region
  file-scope  one region at file scope: includes, prototypes, externs,
              typedefs and definitions no other file-scope block defines
              are kept for every region (the directives go)

The in-memory layout is PAL's on every disc (the JPN tables are converted
into it when they load), so every layout difference takes the PAL arm.

What no class fits stays a raw block and is a `hand` line of `report`, with
a subkind naming the pattern docs/port/GAME_CODE.md "Regions" gives for it:
layout-typedef, define, data-twin, extern-init, array-size, function-twin,
rename, decl, unbalanced, large-variant, case-mixed, case-in-oneregion. A
block inside a function config/region_twins.txt lists is also a `twin`
line. The report lines are `path:line<TAB>kind<TAB>subkind<TAB>detail`.

The files are EUC-JP: the tool reads and writes bytes and never decodes.

  tools/region_blocks.py convert [--root DIR] [PATH...]  rewrite the blocks
  tools/region_blocks.py report [--root DIR] [--all] [--bits]
                         the hand list (and layout/review/twin lines); --all
                         adds one `auto` line per converted block; --bits
                         prints candidate rows for config/motion_bits.<slug>.txt
  tools/region_blocks.py check [--root DIR]
                         exit 0: no region block, every rule holds;
                         exit 2: raw blocks remain and every one is a hand
                         item (listed); exit 1: a rule misfired (listed)
  tools/region_blocks.py rename [--root DIR] [--check] [--set SLUGS]
                         rewrite the two-row spelling under ico2/ and sce/
                         for the rows sharing the Japanese arm (every named
                         row, or --set): PAL_JPN( -> PAL_JPNKOR(, ICO_JPN ->
                         ICO_JPNKOR, LN(a, b) -> LN(a, b, b); tokens only,
                         idempotent; --check reports and exits 1 if any
  tools/region_blocks.py gen-header [--root DIR]
                         write ico2/common/include/region.h from
                         config/regions.txt
  tools/region_blocks.py gen-table [--root DIR]
                         write port/core/region_table.c (ico_region_info[]
                         in the table's order) from config/regions.txt
  tools/region_blocks.py --selftest
                         convert tools/test/region_blocks/fixture.c and
                         compare with expected.c, expected_report.tsv
                         (report --all) and expected_bits.txt; check
                         check_contexts.c beside it (exactly its lines
                         marked "refused" refused), rename.c with
                         rename_expected.c, and gen-header's two-row output
                         with region_two_rows.h
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from strip_host_gates import split_lines, comment_state, logical_directives, split_comment  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("ico2", "sce")
EXTS = (".c", ".h", ".inc", ".s", ".S")
REGIONS_TXT = "config/regions.txt"
TWINS_TXT = "config/region_twins.txt"
HEADER = "ico2/common/include/region.h"
REGION_TABLE = "port/core/region_table.c"
TABLE_SETS = "port/data/gen/table_sets.c"
DISCS_C = "port/data/discs.c"
FIXTURE_DIR = os.path.join(ROOT, "tools", "test", "region_blocks")
# the programmer directories in ico_include_dirs' order (CMakeLists.txt)
INCLUDE_ORDER = ("sugipon", "omori", "common", "ito", "fumi", "seki", "script")
COMPAT_DIR = "port/compat"

# |PAL - JPN| of the id families planning established (anim +4, layout +3,
# object/property +23..+30, stage +2, the +19 above ~930, pad bits 32/48),
# and |PAL - KOR| of the Korean disc's property ids, measured on upstream
# c2f4ed40e (`report` over its tree with the kor row; every literal whose
# KOR value differs from JPN's: PAL - KOR 29 x60, 30 x16, 34 x10, 35 x52,
# 36 x2, JPN - KOR 6 x140; the switch heads of layout_action.c 29..36):
# the Japanese deltas plus the six rows the Korean tex-property table lacks
DELTAS_KNOWN = {2, 3, 4, 19, 23, 24, 28, 29, 30, 32, 34, 35, 36, 48}
# an arm longer than this is a large in-function variant: hand work
LARGE_LINES = 40

DIRECTIVE = re.compile(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", re.S)
VERSION_WORD = re.compile(rb"\bVERSION_\w*")
MACRO_ONLY = re.compile(rb"^[ \t]*(VERSION_\w+)[ \t]*$")


class RegionError(Exception):
    """Malformed input or a rule the tool cannot apply: names the line."""


# --------------------------------------------------------------------------
# The regions table and region.h


class Region:
    def __init__(self, index, slug, macro, disc_id, boot_elf, product):
        self.index = index
        self.slug = slug
        self.macro = macro
        self.disc_id = disc_id
        self.boot_elf = boot_elf
        self.product = product

    @property
    def upper(self):
        return self.slug.upper()


def load_regions(root):
    """The rows of config/regions.txt, or None when the file is missing."""
    path = os.path.join(root, REGIONS_TXT)
    if not os.path.exists(path):
        return None
    rows = []
    with open(path, "rb") as fh:
        for n, raw in enumerate(fh.read().split(b"\n"), 1):
            line = raw.split(b"#", 1)[0].strip()
            if not line:
                continue
            cols = line.split(None, 4)
            if len(cols) != 5:
                raise RegionError("%s:%d: want `slug macro disc-id boot-elf product`" % (REGIONS_TXT, n))
            slug, macro, disc, boot, product = (c.decode("ascii") for c in cols)
            rows.append(Region(len(rows), slug, macro, disc, boot, product))
    if not rows or rows[0].macro != "-":
        raise RegionError("%s: row 0 is the PAL row, with `-` for its macro" % REGIONS_TXT)
    for r in rows[1:]:
        if r.macro != "VERSION_" + r.upper:
            raise RegionError("%s: row %s: the macro must be VERSION_%s" % (REGIONS_TXT, r.slug, r.upper))
    return rows


def default_regions():
    """The two rows main carries, for scanning a tree that has no table yet."""
    return [Region(0, "pal", "-", "SCES-50760", "SCES_507.60", "the European disc"),
            Region(1, "jpn", "VERSION_JPN", "SCPS-11003", "SCPS_110.03", "the Japanese disc")]


class Group:
    """A set of regions that share one arm: its slugs in table order. The
    set's name is the slugs concatenated (`jpnkor`, `JPNKOR`); a set of one
    row is named as the row, so a Group stands wherever a Region does."""

    def __init__(self, regions):
        self.regions = tuple(sorted(regions, key=lambda r: r.index))
        self.slugs = tuple(r.slug for r in self.regions)
        self.slug = "".join(self.slugs)
        self.upper = self.slug.upper()
        self.index = self.regions[0].index

    def __repr__(self):
        return "Group(%s)" % self.slug


def selector_subsets(regions):
    """Every non-empty set of non-PAL rows, each in table order, in mask order
    (JPN, US, JPN_US for rows pal, jpn, us)."""
    named = regions[1:]
    subsets = []
    for mask in range(1, 1 << len(named)):
        subsets.append([r for i, r in enumerate(named) if mask & (1 << i)])
    return subsets


def partitions(items):
    """Every partition of items (a list in table order) into sets, each set
    in table order and the sets ordered by their first item."""
    if not items:
        return [[]]
    first, rest = items[0], items[1:]
    out = []
    for p in partitions(rest):
        out.append([[first]] + p)
        for k in range(len(p)):
            out.append(p[:k] + [[first] + p[k]] + p[k + 1:])
    for p in out:
        p.sort(key=lambda s: s[0].index)
    return out


def selector_groupings(regions):
    """Every selector's arms: for each set of non-PAL rows (mask order), each
    way to group those rows into shared arms, coarsest first (rows pal, jpn,
    kor: [JPN], [KOR], [JPNKOR], [JPN, KOR]). Each grouping is a list of
    Groups ordered by their first row."""
    out = []
    for subset in selector_subsets(regions):
        parts = partitions(subset)
        parts.sort(key=lambda p: (len(p), [[r.index for r in s] for s in p]))
        for p in parts:
            out.append([Group(s) for s in p])
    return out


def shared_sets(regions):
    """The sets of two or more non-PAL rows (mask order): each has a
    predicate ICO_<SET>."""
    return [Group(s) for s in selector_subsets(regions) if len(s) > 1]


def selector_name(subset):
    """PAL_ and the arms' set names joined by _: PAL_JPN, PAL_JPNKOR,
    PAL_JPN_KOR (subset: Regions or Groups)."""
    return "PAL_" + "_".join(r.upper for r in subset)


def ln_params(regions):
    return ", ".join(r.slug for r in regions)


def gen_header(regions):
    """The text of region.h for these rows."""
    index_list = ", ".join("%d %s" % (r.index, r.slug) for r in regions)
    shared = shared_sets(regions)
    out = []
    out.append("/* ico2/common/include/region.h -- GENERATED by tools/region_blocks.py gen-header from")
    out.append(" * config/regions.txt; do not edit by hand.")
    out.append(" *")
    out.append(" * The game region at run time.  One program plays every disc in config/regions.txt; the")
    out.append(" * region is chosen once, when the disc image is identified, before any game code runs.")
    out.append(" * Upstream's compile-time `#ifdef VERSION_<SLUG>` blocks become these run-time forms")
    out.append(' * (docs/port/GAME_CODE.md "Regions"). */')
    out.append("#ifndef ICO_REGION_H")
    out.append("#define ICO_REGION_H")
    out.append("")
    out.append("extern int ico_region; /* row index into config/regions.txt: %s; set once at disc mount */"
               % index_list)
    out.append("")
    for r in regions:
        out.append("#define ICO_REGION_%s %d" % (r.upper, r.index))
    out.append("#define ICO_REGION_COUNT %d" % len(regions))
    out.append("")
    for r in regions:
        out.append("#define ICO_%s (ico_region == ICO_REGION_%s)" % (r.upper, r.upper))
    if shared:
        out.append("")
        out.append("/* A set of rows that share one arm upstream (`#if defined(VERSION_A) || defined(VERSION_B)`),")
        out.append(" * named by its slugs concatenated in table order. */")
        for g in shared:
            out.append("#define ICO_%s (%s)" % (g.upper, " || ".join("ICO_%s" % r.upper for r in g.regions)))
    out.append("")
    out.append("/* Selector macros: the first argument is the PAL arm (upstream's #else), then one arm per")
    if shared:
        out.append(" * named region set in table order (a set of rows sharing an arm is one argument:")
        out.append(" * PAL_%s(pal, %s)); a region a macro does not name takes the PAL arm. */"
                   % (shared[0].upper, shared[0].slug))
    else:
        out.append(" * named region in table order; a region a macro does not name takes the PAL arm. */")
    for grouping in selector_groupings(regions):
        params = ", ".join(["pal"] + [g.slug for g in grouping])
        chain = "".join("ICO_%s ? (%s) : " % (g.upper, g.slug) for g in reversed(grouping))
        out.append("#define %s(%s) (%s(pal))" % (selector_name(grouping), params, chain))
    out.append("")
    out.append("/* LN(%s): upstream's per-target __LINE__ values for the debug allocators and asserts. */"
               % ln_params(regions))
    if len(regions) > 1:
        out.append("#define LN(%s) %s(%s)" % (ln_params(regions), selector_name(regions[1:]), ln_params(regions)))
    else:
        out.append("#define LN(pal) (pal)")
    out.append("")
    out.append("#endif /* ICO_REGION_H */")
    return ("\n".join(out) + "\n").encode("ascii")


def gen_table(regions):
    """The text of port/core/region_table.c for these rows."""
    out = []
    out.append("/*")
    out.append(" * port/core/region_table.c -- GENERATED by tools/region_blocks.py gen-table from")
    out.append(" * config/regions.txt; do not edit by hand.")
    out.append(" *")
    out.append(" * Every row's IcoRegionInfo (port/core/region_<slug>.c) in the table's order;")
    out.append(" * ico_region indexes it (region.h).")
    out.append(" */")
    out.append('#include "region.h"')
    out.append("")
    for r in regions:
        out.append("extern const IcoRegionInfo ico_region_info_%s;" % r.slug)
    out.append("")
    out.append("const IcoRegionInfo *const ico_region_info[ICO_REGION_COUNT] = {")
    for r in regions:
        out.append("    &ico_region_info_%s," % r.slug)
    out.append("};")
    return ("\n".join(out) + "\n").encode("ascii")


def glue_problems(root, regions):
    """The pieces "Adding a build target" names, per row of the table. A
    piece is looked for only once its mechanism is in the tree: the region
    files once port/core/ exists, the EE layouts once any
    config/ee_layout.<slug>.txt exists, the descriptor sets once
    port/data/gen/table_sets.c exists."""
    problems = []
    recipe = 'docs/port/GAME_CODE.md "Adding a build target"'
    core = os.path.join(root, "port", "core")
    if os.path.isdir(core):
        for r in regions:
            path = os.path.join(core, "region_%s.c" % r.slug)
            want = re.compile(rb"\bconst\s+IcoRegionInfo\s+ico_region_info_%s\s*=" % r.slug.encode())
            if not os.path.exists(path):
                problems.append("port/core/region_%s.c is missing (%s, step 2)" % (r.slug, recipe))
            elif not want.search(read(path)):
                problems.append("port/core/region_%s.c defines no ico_region_info_%s (%s, step 2)"
                                % (r.slug, r.slug, recipe))
        table = os.path.join(root, REGION_TABLE)
        if not os.path.exists(table) or read(table) != gen_table(regions):
            problems.append("%s is %s: run tools/region_blocks.py gen-table (%s, step 1)"
                            % (REGION_TABLE, "stale" if os.path.exists(table) else "missing", recipe))
    config = os.path.join(root, "config")
    if os.path.isdir(config) and any(re.match(r"ee_layout\.\w+\.txt$", f) for f in os.listdir(config)):
        for r in regions[1:]:
            if not os.path.exists(os.path.join(config, "ee_layout.%s.txt" % r.slug)):
                problems.append("config/ee_layout.%s.txt is missing (%s, step 3)" % (r.slug, recipe))
    discs = os.path.join(root, DISCS_C)
    if os.path.exists(discs):
        text = read(discs)
        m = re.search(rb"\bico_discs\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\}\s*;", text, re.S)
        body = m.group(1) if m else b""
        for r in regions:
            entry = re.search(rb"\[\s*ICO_REGION_%s\s*\]\s*=\s*\{(.*?)(?=\[\s*ICO_REGION_|\Z)"
                              % r.upper.encode(), body, re.S)
            if entry is None or b'"%s"' % r.slug.encode() not in entry.group(1):
                problems.append("%s: ico_discs[] has no [ICO_REGION_%s] entry naming \"%s\" (%s, step 2)"
                                % (DISCS_C, r.upper, r.slug, recipe))
    sets = os.path.join(root, TABLE_SETS)
    if os.path.exists(sets):
        for r in regions[1:]:
            if not os.path.exists(os.path.join(root, "port", "data", "gen", r.slug, "table_desc.c")):
                problems.append("port/data/gen/%s/table_desc.c is missing (%s, step 3)" % (r.slug, recipe))
        m = re.search(rb"ico_table_sets\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", read(sets), re.S)
        order = [x.decode() for x in re.findall(rb"ico_table_set_(\w+)", m.group(1))] if m else None
        if order != [r.slug for r in regions]:
            problems.append("%s: ico_table_sets[] is %s, not the table's order %s (%s, step 3)"
                            % (TABLE_SETS, order, [r.slug for r in regions], recipe))
    return problems


def region_names(regions):
    """The identifiers region.h defines (for `check`)."""
    names = {"LN", "ico_region", "ICO_REGION_COUNT"}
    for r in regions:
        names.add("ICO_" + r.upper)
        names.add("ICO_REGION_" + r.upper)
    for g in shared_sets(regions):
        names.add("ICO_" + g.upper)
    for grouping in selector_groupings(regions):
        names.add(selector_name(grouping))
    return names


def load_twins(root):
    """config/region_twins.txt rows: [(path, function, twin, line)]."""
    path = os.path.join(root, TWINS_TXT)
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, "rb") as fh:
        for n, raw in enumerate(fh.read().split(b"\n"), 1):
            line = raw.split(b"#", 1)[0].strip()
            if not line:
                continue
            cols = line.split(b"\t")
            if len(cols) != 3:
                raise RegionError("%s:%d: want `path<TAB>function<TAB>twin`" % (TWINS_TXT, n))
            rows.append((cols[0].decode(), cols[1].decode(), cols[2].decode(), n))
    return rows


# --------------------------------------------------------------------------
# Tokens


class Token:
    __slots__ = ("kind", "text", "start", "end", "space", "owner", "vi")

    def __init__(self, kind, text, start, end, space):
        self.kind = kind      # ident number string char punct directive other
        self.text = text      # bytes
        self.start = start    # byte offsets into the file
        self.end = end
        self.space = space    # whitespace or a comment came before it
        self.owner = None     # (block, arm key) or None
        self.vi = None        # index in the PAL view, or None

    def __repr__(self):
        return "Token(%r)" % self.text


PUNCTS = [b">>=", b"<<=", b"...", b"->", b"++", b"--", b"<<", b">>", b"<=", b">=", b"==", b"!=",
          b"&&", b"||", b"*=", b"/=", b"%=", b"+=", b"-=", b"&=", b"^=", b"|=", b"##"]
IDENT_RE = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*")
NUMBER_RE = re.compile(rb"\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.])*")


def tokenize(data):
    """C tokens of the file; comments and whitespace are skipped, a
    preprocessor line (with its continuations) is one `directive` token."""
    toks = []
    i = 0
    n = len(data)
    space = True
    line_start = True
    while i < n:
        c = data[i:i + 1]
        if c in b" \t\r\f\v":
            i += 1
            space = True
            continue
        if c == b"\n":
            i += 1
            space = True
            line_start = True
            continue
        if data.startswith(b"/*", i):
            j = data.find(b"*/", i + 2)
            i = n if j < 0 else j + 2
            space = True
            continue
        if data.startswith(b"//", i):
            j = data.find(b"\n", i)
            i = n if j < 0 else j
            space = True
            continue
        if c == b"#" and line_start:
            j = i
            while True:
                k = data.find(b"\n", j)
                if k < 0:
                    k = n
                    break
                if data[k - 1:k] == b"\\" or data[k - 2:k] == b"\\\r":
                    j = k + 1
                    continue
                # a block comment opened on the directive line runs on
                seg = data[i:k]
                opened = seg.rfind(b"/*")
                if opened >= 0 and seg.find(b"*/", opened) < 0:
                    close = data.find(b"*/", i + opened)
                    if close >= 0:
                        j = close + 2
                        continue
                break
            toks.append(Token("directive", data[i:k], i, k, space))
            i = k
            space = True
            continue
        line_start = False
        if c in (b'"', b"'"):
            j = i + 1
            while j < n and data[j:j + 1] != c and data[j:j + 1] != b"\n":
                j += 2 if data[j:j + 1] == b"\\" else 1
            j = min(j + 1, n)
            toks.append(Token("string" if c == b'"' else "char", data[i:j], i, j, space))
            i = j
            space = False
            continue
        m = IDENT_RE.match(data, i)
        if m:
            toks.append(Token("ident", m.group(0), i, m.end(), space))
            i = m.end()
            space = False
            continue
        m = NUMBER_RE.match(data, i)
        if m:
            toks.append(Token("number", m.group(0), i, m.end(), space))
            i = m.end()
            space = False
            continue
        for p in PUNCTS:
            if data.startswith(p, i):
                toks.append(Token("punct", p, i, i + len(p), space))
                i += len(p)
                break
        else:
            toks.append(Token("punct" if c < b"\x80" else "other", c, i, i + 1, space))
            i += 1
        space = False
    return toks


def is_literal(tok):
    return tok.kind in ("number", "string", "char")


def literal_value(tok):
    """The value of a numeric literal, or None."""
    if tok.kind != "number":
        return None
    t = tok.text.decode("ascii", "replace").rstrip("uUlL")
    try:
        if t.lower().startswith("0x"):
            return int(t, 16)
        if re.match(r"^0[0-7]+$", t):
            return int(t, 8)
        if re.match(r"^[0-9]+$", t):
            return int(t)
        return float(t.rstrip("fF"))
    except ValueError:
        return None


def spaced_text(toks):
    """Tokens rejoined on one line, one space wherever the source had any."""
    out = b""
    for k, t in enumerate(toks):
        if k and t.space:
            out += b" "
        out += t.text
    return out


# --------------------------------------------------------------------------
# Region blocks


class Arm:
    def __init__(self, key, lo, hi=None):
        self.key = key   # the slugs of the rows that compile it, a tuple in table order;
        #                  "pal" for the PAL arm (with every row that takes PAL's text)
        self.lo = lo     # content lines [lo, hi)
        self.hi = lo if hi is None else hi

    @property
    def name(self):
        """The arm's set name: `pal`, `jpn`, `jpnkor`."""
        return "pal" if self.key == "pal" else "".join(self.key)

    @property
    def upper(self):
        return self.name.upper()

    def empty(self, lines):
        return all(lines[k].strip() == b"" for k in range(self.lo, self.hi))


class Block:
    """One region conditional: named arms in upstream's order, then PAL."""

    def __init__(self, start, end, named, pal, orig_start):
        self.start = start        # line index of the opening directive
        self.end = end            # line index after #endif
        self.named = named        # Arm per set of rows with its own text, upstream's order
        self.pal = pal            # the PAL arm (may be empty)
        self.directive_lines = set()
        self.orig_start = orig_start
        self.synth = None         # the source bytes when the block was rebuilt flat

    @property
    def line(self):
        return self.orig_start + 1

    def arms(self):
        """Named arms in upstream's order, then PAL."""
        return self.named + [self.pal]


class RawBlock:
    """A region conditional as parsed: per row, the source lines it compiles."""

    def __init__(self, start):
        self.start = start
        self.end = None
        self.directive_lines = set()   # the region directives of the block and the one nested in it
        self.texts = {}                # slug -> [line index]


COND_TOKEN = re.compile(rb"[ \t]*(defined|VERSION_\w+|\|\||&&|!|\(|\)|\S+)")


def parse_condition(code, by_macro, every, where):
    """The rows an #if/#elif condition holds for: a boolean expression over
    defined(VERSION_X) (also `defined VERSION_X`) with ||, && and !, and
    parentheses. Anything else that names a VERSION_ macro is refused."""
    toks = []
    pos = 0
    while pos < len(code):
        m = COND_TOKEN.match(code, pos)
        if not m or m.end() == pos:
            break
        toks.append(m.group(1))
        pos = m.end()
    k = [0]

    def bad(why):
        raise RegionError("%s: %s: a region condition is defined(VERSION_<SLUG>) joined by ||, && and !"
                          % (where, why))

    def peek():
        return toks[k[0]] if k[0] < len(toks) else None

    def take():
        t = peek()
        k[0] += 1
        return t

    def primary():
        t = take()
        if t == b"!":
            return every - primary()
        if t == b"(":
            v = disjunction()
            if take() != b")":
                bad("an unclosed parenthesis")
            return v
        if t == b"defined":
            paren = peek() == b"("
            if paren:
                take()
            name = take()
            if name is None or not name.startswith(b"VERSION_"):
                bad("defined() of %s" % (name or b"nothing").decode("latin-1"))
            if paren and take() != b")":
                bad("defined( without its )")
            macro = name.decode("ascii", "replace")
            if macro not in by_macro:
                raise RegionError("%s: %s is not a row of %s: add the row first" % (where, macro, REGIONS_TXT))
            return {by_macro[macro].slug}
        bad("`%s`" % (t or b"").decode("latin-1"))

    def conjunction():
        v = primary()
        while peek() == b"&&":
            take()
            v = v & primary()
        return v

    def disjunction():
        v = conjunction()
        while peek() == b"||":
            take()
            v = v | conjunction()
        return v

    result = disjunction()
    if peek() is not None:
        bad("`%s` after the condition" % peek().decode("latin-1"))
    return result


def find_blocks(lines, states, regions, path):
    """Parse every region conditional of the file: [RawBlock]. A region block
    may hold one more region block in an arm (upstream's `#ifdef VERSION_KOR`
    inside a `defined(VERSION_JPN) || defined(VERSION_KOR)` arm); a row its
    conditions do not name takes the arm the PAL row takes."""
    by_macro = {r.macro: r for r in regions[1:]}
    every = frozenset(r.slug for r in regions)
    blocks = []
    stack = []    # per open conditional: None (on something else) or [arm rows, rows taken, after #else]

    def live():
        rows = set(every)
        for f in stack:
            if f is not None:
                rows &= f[0]
        return rows

    def content(lo, hi):
        if raw is None or lo >= hi:
            return
        rows = live()
        for slug in rows:
            raw.texts.setdefault(slug, []).extend(range(lo, hi))

    raw = None
    prev = 0
    for s, e, text in logical_directives(lines, states):
        where = "%s:%d" % (path, s + 1)
        content(prev, s)
        prev = e
        m = DIRECTIVE.match(text)
        if not m:
            if VERSION_WORD.search(split_comment(text)[0]):
                raise RegionError("%s: a directive on a VERSION_ macro the tool does not know" % where)
            content(s, e)
            continue
        kind = m.group(1)
        code, _ = split_comment(m.group(2))
        mentions = VERSION_WORD.search(code) is not None
        top = stack[-1] if stack else None
        depth = sum(1 for f in stack if f is not None)
        if kind in (b"if", b"ifdef", b"ifndef"):
            if not mentions:
                content(s, e)
                stack.append(None)
                continue
            if depth >= 2:
                raise RegionError("%s: a region block nested two deep (one level of nesting is read)" % where)
            if kind == b"if":
                rows = parse_condition(code.strip(), by_macro, every, where)
            else:
                mm = MACRO_ONLY.match(code)
                if not mm:
                    raise RegionError("%s: `#%s` takes one VERSION_ macro" % (where, kind.decode()))
                rows = parse_condition(b"defined(" + mm.group(1) + b")", by_macro, every, where)
                if kind == b"ifndef":
                    rows = every - rows
            if depth == 0:
                raw = RawBlock(s)
            raw.directive_lines.update(range(s, e))
            stack.append([set(rows), set(rows), False])
        elif kind == b"elif":
            if top is None:
                if mentions:
                    raise RegionError("%s: #elif on a VERSION_ macro outside a region block" % where)
                content(s, e)
                continue
            if top[2]:
                raise RegionError("%s: #elif after #else" % where)
            if not mentions:
                raise RegionError("%s: a region block's #elif names no VERSION_ macro" % where)
            rows = parse_condition(code.strip(), by_macro, every, where)
            if rows & top[1]:
                raise RegionError("%s: #elif names %s, taken by an earlier arm"
                                  % (where, ", ".join(sorted(rows & top[1]))))
            top[0] = set(rows)
            top[1] |= rows
            raw.directive_lines.update(range(s, e))
        elif kind == b"else":
            if not stack:
                raise RegionError("%s: #else without #if" % where)
            if top is None:
                content(s, e)
                continue
            if top[2]:
                raise RegionError("%s: two #else" % where)
            top[0] = set(every - top[1])
            top[1] = set(every)
            top[2] = True
            raw.directive_lines.update(range(s, e))
        elif kind == b"endif":
            if not stack:
                raise RegionError("%s: #endif without #if" % where)
            stack.pop()
            if top is None:
                content(s, e)
                continue
            raw.directive_lines.update(range(s, e))
            if not any(f is not None for f in stack):
                raw.end = e
                blocks.append(raw)
                raw = None
    if stack:
        raise RegionError("%s: unterminated conditional" % path)
    return blocks


def condition_text(slugs):
    return b" || ".join(b"defined(VERSION_%s)" % s.upper().encode() for s in slugs)


def flatten_blocks(lines, raws, regions):
    """The file with every region block in the flat shape the classes read:
    one arm per set of rows with the same text (upstream's order, the PAL arm
    last), each arm one run of lines. A block already in that shape keeps its
    lines; a nested one is rebuilt, its shared lines repeated in each arm.
    Returns (lines, line map to the source's line indices, [Block])."""
    out = []
    line_map = []
    blocks = []
    at = 0

    def copy(lo, hi):
        for k in range(lo, hi):
            out.append(lines[k])
            line_map.append(k)

    for raw in raws:
        copy(at, raw.start)
        at = raw.end
        texts = {}
        order = []
        for r in regions:
            ks = tuple(raw.texts.get(r.slug, ()))
            body = b"".join(lines[k] for k in ks)
            if body not in texts:
                texts[body] = []
                order.append(body)
            texts[body].append((r, ks))
        groups = []
        for body in order:
            members = texts[body]
            ks = members[0][1]
            groups.append(([r.slug for r, _ in members], ks))
        pal_group = next(g for g in groups if "pal" in g[0])
        named = [g for g in groups if g is not pal_group]
        # upstream's order: by the lines each set compiles (a set whose own
        # nested arm comes first leads), the empty sets last in table order
        index = {r.slug: r.index for r in regions}
        named.sort(key=lambda g: (0, g[1], 0) if g[1] else (1, (), index[g[0][0]]))
        flat = True
        used = set()
        for _, ks in groups:
            if ks and list(ks) != list(range(ks[0], ks[-1] + 1)):
                flat = False
            if used & set(ks):
                flat = False
            used |= set(ks)
        base = len(out)
        if flat:
            shift = base - raw.start
            copy(raw.start, raw.end)
            empty_at = max(raw.directive_lines) + shift
            arms = []
            for slugs, ks in named + [pal_group]:
                key = "pal" if "pal" in slugs else tuple(slugs)
                if ks:
                    arms.append(Arm(key, ks[0] + shift, ks[-1] + 1 + shift))
                else:
                    arms.append(Arm(key, empty_at))
            blk = Block(base, base + raw.end - raw.start, arms[:-1], arms[-1], raw.start)
            owned = set()
            for a in arms:
                owned.update(range(a.lo, a.hi))
            blk.directive_lines = set(range(blk.start, blk.end)) - owned
        else:
            arms = []
            for n, (slugs, ks) in enumerate(named):
                out.append((b"#if " if n == 0 else b"#elif ") + condition_text(slugs) + b"\n")
                line_map.append(raw.start)
                lo = len(out)
                copy_lines = list(ks)
                for k in copy_lines:
                    out.append(lines[k])
                    line_map.append(k)
                arms.append(Arm(tuple(slugs), lo, len(out)))
            out.append(b"#else\n" if named else b"#if 1\n")
            line_map.append(raw.start)
            lo = len(out)
            for k in pal_group[1]:
                out.append(lines[k])
                line_map.append(k)
            arms.append(Arm("pal", lo, len(out)))
            out.append(b"#endif\n")
            line_map.append(raw.end - 1)
            blk = Block(base, len(out), arms[:-1], arms[-1], raw.start)
            owned = set()
            for a in arms:
                owned.update(range(a.lo, a.hi))
            blk.directive_lines = set(range(blk.start, blk.end)) - owned
            blk.synth = b"".join(lines[raw.start:raw.end])
        blk.orig_directive_lines = raw.directive_lines
        blocks.append(blk)
    copy(at, len(lines))
    return out, line_map, blocks


# --------------------------------------------------------------------------
# The PAL view: the file as the PAL build sees it, with the brace frames


class Frame:
    __slots__ = ("kind", "open", "name", "head")

    def __init__(self, kind, open_vi, name=None, head=None):
        self.kind = kind      # func aggregate init switch block other
        self.open = open_vi   # view index of the `{`
        self.name = name      # a function's name
        self.head = head      # a switch's (switch, lparen, rparen) view indices


AGGREGATE_KEYS = (b"struct", b"union", b"enum")
OPENERS = {b"(": b")", b"[": b"]", b"{": b"}"}
CLOSERS = {b")": b"(", b"]": b"[", b"}": b"{"}


class View:
    """Tokens of the PAL view with their bracket matches and brace frames."""

    def __init__(self, toks):
        self.toks = toks
        for k, t in enumerate(toks):
            t.vi = k
        self.match = {}
        self.parent = [None] * len(toks)
        self.frames = [()] * len(toks)
        self._brackets()
        self._frames()

    def _brackets(self):
        stack = []
        for k, t in enumerate(self.toks):
            self.parent[k] = stack[-1] if stack else None
            if t.kind != "punct":
                continue
            if t.text in OPENERS:
                stack.append(k)
            elif t.text in CLOSERS:
                # tolerate a stray closer (a conditional on something else)
                for depth in range(len(stack) - 1, -1, -1):
                    if self.toks[stack[depth]].text == CLOSERS[t.text]:
                        self.match[stack[depth]] = k
                        self.match[k] = stack[depth]
                        del stack[depth:]
                        break
                self.parent[k] = stack[-1] if stack else None

    def text(self, k):
        return self.toks[k].text if 0 <= k < len(self.toks) else None

    def in_function(self, k):
        return any(f.kind == "func" for f in self.frames[k])

    def function_name(self, k):
        for f in reversed(self.frames[k]):
            if f.kind == "func":
                return f.name
        return None

    def _frames(self):
        stack = ()
        for k, t in enumerate(self.toks):
            if t.kind == "punct" and t.text == b"{":
                self.frames[k] = stack
                stack = stack + (self._open_frame(k, stack),)
                continue
            self.frames[k] = stack
            if t.kind == "punct" and t.text == b"}" and stack:
                stack = stack[:-1]

    def _open_frame(self, k, stack):
        prev = self.text(k - 1)
        top = stack[-1] if stack else None
        in_func = any(f.kind == "func" for f in stack)
        if top is not None and top.kind == "init":
            return Frame("init", k)
        if prev == b"=":
            return Frame("init", k)
        if prev in AGGREGATE_KEYS or (self.toks[k - 1].kind == "ident" and self.text(k - 2) in AGGREGATE_KEYS):
            return Frame("aggregate", k)
        if top is not None and top.kind == "aggregate":
            return Frame("aggregate", k)
        if not in_func:
            j = k - 1
            # skip __attribute__((...)) between the parameters and the body
            while j > 0 and self.text(j) == b")" and j in self.match and \
                    self.text(self.match[j] - 2) == b"__attribute__":
                j = self.match[j] - 3
            if self.text(j) == b")" and j in self.match:
                lp = self.match[j]
                name_tok = self.toks[lp - 1] if lp > 0 else None
                name = name_tok.text.decode("ascii") if name_tok is not None and name_tok.kind == "ident" else None
                return Frame("func", k, name=name)
            return Frame("other", k)
        if prev == b")" and (k - 1) in self.match:
            lp = self.match[k - 1]
            if self.text(lp - 1) == b"switch":
                return Frame("switch", k, head=(lp - 1, lp, k - 1))
        return Frame("block", k)

    def outer_paren(self, k):
        """The outermost ( or [ around k inside its brace, or k itself."""
        q = k
        p = self.parent[k]
        while p is not None and self.toks[p].text in (b"(", b"["):
            q = p
            p = self.parent[p]
        return q

    def statement(self, k):
        """View indices [lo, hi) of the statement (or declaration) holding k."""
        q = self.outer_paren(k)
        lo = q
        j = q - 1
        while j >= 0:
            t = self.toks[j].text
            if t in (b")", b"]") and j in self.match:
                lo = self.match[j]
                j = lo - 1
                continue
            if t in (b";", b"{", b"}"):
                break
            lo = j
            j -= 1
        hi = q
        j = q
        n = len(self.toks)
        while j < n:
            t = self.toks[j].text
            if t in (b"(", b"[") and j in self.match:
                j = self.match[j] + 1
                hi = j
                continue
            if t in (b";", b"{", b"}"):
                hi = j + 1 if t == b";" else j
                break
            j += 1
            hi = j
        return lo, hi


TYPE_WORDS = {b"int", b"char", b"short", b"long", b"float", b"double", b"void", b"unsigned", b"signed",
              b"const", b"volatile", b"static", b"register", b"struct", b"union", b"enum", b"extern",
              b"typedef", b"inline", b"_Bool", b"auto"}
CAST_BASES = {b"char", b"int", b"float", b"short"}
KEYWORDS = {b"else", b"return", b"goto", b"case", b"default", b"do", b"if", b"while", b"for", b"switch",
            b"sizeof", b"break", b"continue"}
ASSIGN_OPS = {b"=", b"+=", b"-=", b"*=", b"/=", b"%=", b"&=", b"|=", b"^=", b"<<=", b">>="}


def is_declaration(toks):
    """Do these tokens (a statement from its start) declare something?"""
    if not toks:
        return False
    first = toks[0]
    if first.kind == "ident" and first.text in TYPE_WORDS:
        return True
    if first.kind != "ident" or first.text in KEYWORDS or len(toks) < 2:
        return False
    if toks[1].kind == "ident" and toks[1].text not in TYPE_WORDS and toks[1].text not in KEYWORDS:
        return True
    k = 1
    while k < len(toks) and toks[k].text == b"*":
        k += 1
    if k > 1 and k + 1 < len(toks) and toks[k].kind == "ident" and \
            toks[k + 1].text in (b"=", b";", b"[", b",", b"("):
        return True
    return False


def has_pointer_cast(toks):
    """A (char *), (int *), (float *) or (short *) cast among the tokens."""
    for k in range(len(toks)):
        if toks[k].text != b"(":
            continue
        j = k + 1
        while j < len(toks) and toks[j].text in (b"unsigned", b"signed", b"const", b"volatile"):
            j += 1
        if j < len(toks) and toks[j].text in CAST_BASES:
            j += 1
            stars = 0
            while j < len(toks) and toks[j].text == b"*":
                j += 1
                stars += 1
            if stars and j < len(toks) and toks[j].text == b")":
                return True
    return False


def stmt_start(view, k):
    """The first token of the declaration holding an initialiser's token."""
    f = next((f for f in view.frames[k] if f.kind == "init"), None)
    return view.statement(f.open)[0] if f is not None else view.statement(k)[0]


def offset16(pal_tok, vals):
    """The ActWork shift: PAL - JPN is 16 on a raw offset or size of 0x200
    and up (JPN's ActWork has no boxDir; the port keeps PAL's layout)."""
    pv = literal_value(pal_tok)
    if not isinstance(pv, int) or pv < 0x200:
        return False
    for _, o in vals:
        ov = literal_value(o)
        if not isinstance(ov, int) or pv - ov != 16:
            return False
    return True


def deny_reason(view, k):
    """Why the PAL token at view index k must stay alone, or None."""
    prev = view.text(k - 1)
    if prev in (b">>", b"<<", b">>=", b"<<="):
        return "shift"
    if prev == b"sizeof":
        return "sizeof"
    p = view.parent[k]
    while p is not None and view.toks[p].text in (b"(", b"["):
        if view.toks[p].text == b"(" and view.text(p - 1) == b"sizeof":
            return "sizeof"
        p = view.parent[p]
    for f in view.frames[k]:
        if f.kind == "aggregate":
            return "aggregate"
    lo, hi = view.statement(k)
    stmt = view.toks[lo:hi]
    if is_declaration(stmt):
        first_assign = next((j for j in range(lo, hi) if view.toks[j].text == b"=" and
                             view.outer_paren(j) == j), hi)
        p = view.parent[k]
        while p is not None and view.toks[p].text in (b"(", b"["):
            if view.toks[p].text == b"[" and p < first_assign:
                return "dimension"
            p = view.parent[p]
        if stmt[0].text == b"static" and k > first_assign:
            return "static-init"
    if any(f.kind == "init" for f in view.frames[k]) and is_declaration(view.toks[stmt_start(view, k):]) and \
            view.toks[stmt_start(view, k)].text == b"static":
        return "static-init"
    if prev in (b"+", b"-") and has_pointer_cast(stmt):
        return "cast-offset"
    return None


# --------------------------------------------------------------------------
# One file: tokens, blocks, the view


class Report:
    def __init__(self, path, line, kind, subkind, detail):
        self.path = path
        self.line = line
        self.kind = kind
        self.subkind = subkind
        self.detail = detail

    def row(self):
        return "%s:%d\t%s\t%s\t%s" % (self.path, self.line, self.kind, self.subkind, self.detail)

    def key(self):
        return (self.path, self.line, self.kind, self.subkind, self.detail)


class Outcome:
    """What a class makes of one block: replacement bytes (None: stays raw)."""

    def __init__(self, cls, text=None, detail=""):
        self.cls = cls
        self.text = text
        self.detail = detail


class SourceFile:
    def __init__(self, path, data, regions):
        self.path = path
        self.orig = data
        self.regions = regions
        self.slugs = [r.slug for r in regions]
        self.groups = {}
        orig_lines = split_lines(data)
        raws = find_blocks(orig_lines, comment_state(orig_lines), regions, path)
        self.lines, self.line_map, self.blocks = flatten_blocks(orig_lines, raws, regions)
        self.data = b"".join(self.lines)
        self.states = comment_state(self.lines)
        self.line_start = []
        off = 0
        for line in self.lines:
            self.line_start.append(off)
            off += len(line)
        self.line_start.append(off)
        self.tokens = tokenize(self.data)
        self._own()
        self.view = View([t for t in self.tokens if t.owner is None or t.owner[1] == "pal"])

    def line_of(self, offset):
        lo, hi = 0, len(self.lines)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.line_start[mid + 1] <= offset:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def lineno(self, offset):
        """The source's 1-based line of an offset into the flat text."""
        return self.line_map[min(self.line_of(offset), len(self.line_map) - 1)] + 1 if self.line_map else 1

    def group(self, slugs):
        """The Group of these rows (slugs in any order)."""
        key = tuple(sorted(slugs, key=self.slugs.index))
        if key not in self.groups:
            self.groups[key] = Group([self.regions[self.slugs.index(x)] for x in key])
        return self.groups[key]

    def output(self, an):
        """The source with the analysis's edits: a rebuilt block no class
        converted goes back as it was written."""
        edits = list(an.edits)
        converted = set(id(b) for b, o in an.outcomes if o is not None)
        for b in self.blocks:
            if b.synth is not None and id(b) not in converted:
                start, end = self.block_range(b)
                edits.append((start, end, b.synth))
        return apply_edits(self.data, edits)

    def _own(self):
        owner = {}
        for b in self.blocks:
            for k in b.directive_lines:
                owner[k] = (b, "directive")
            for arm in b.arms():
                for k in range(arm.lo, arm.hi):
                    owner[k] = (b, arm.key)
        for t in self.tokens:
            t.owner = owner.get(self.line_of(t.start))
        self.arm_tokens = {}
        for t in self.tokens:
            if t.owner is not None and t.owner[1] != "directive":
                self.arm_tokens.setdefault((id(t.owner[0]), t.owner[1]), []).append(t)

    def toks(self, block, arm):
        return self.arm_tokens.get((id(block), arm.key), [])

    def arm_bytes(self, arm):
        return self.data[self.line_start[arm.lo]:self.line_start[arm.hi]]

    def block_range(self, block):
        return self.line_start[block.start], self.line_start[block.end]

    def anchor(self, block):
        """View index where the block stands: its first PAL token, else the
        first view token after it."""
        pal = self.toks(block, block.pal)
        if pal:
            return pal[0].vi
        end = self.line_start[block.end]
        for t in self.tokens:
            if t.start >= end and t.vi is not None:
                return t.vi
        return len(self.view.toks) - 1

    def prev_view_token(self, block):
        """The view token before the block, or None."""
        start = self.line_start[block.start]
        best = None
        for t in self.view.toks:
            if t.start >= start:
                break
            best = t
        return best

    def next_view_token(self, block):
        end = self.line_start[block.end]
        for t in self.view.toks:
            if t.start >= end:
                return t
        return None


# --------------------------------------------------------------------------
# Helpers the classes share


def arms_in_table_order(sf, block):
    """[(group, arm)] for the named arms in config/regions.txt order (of
    each set's first row)."""
    arms = [(sf.group(a.key), a) for a in block.named]
    return sorted(arms, key=lambda x: x[0].index)


def merged_values(sf, pal_text, values):
    """values ([(group, text)]) without the ones equal to PAL's, the groups
    with one text merged into one set: [(group, text)] in table order."""
    by_text = {}
    order = []
    for g, v in values:
        if v == pal_text:
            continue
        if v not in by_text:
            by_text[v] = []
            order.append(v)
        by_text[v].extend(g.slugs)
    merged = [(sf.group(by_text[v]), v) for v in order]
    return sorted(merged, key=lambda x: x[0].index)


def selector_call(sf, pal_text, values):
    """SEL(pal, ...) over the sets of regions whose value differs from PAL,
    rows with one value sharing an argument (PAL_JPNKOR(p, j)).
    values: [(group or region, text)]."""
    subset = merged_values(sf, pal_text, [(sf.group(g.slugs if isinstance(g, Group) else (g.slug,)), v)
                                          for g, v in values])
    if not subset:
        return pal_text
    name = selector_name([g for g, _ in subset])
    return b"%s(%s)" % (name.encode(), b", ".join([pal_text] + [v for _, v in subset]))


def aligned_arms(sf, block):
    """For blocks whose arms all have the PAL arm's token count: the list of
    differing positions [(pal index, [(region, token)])], else None."""
    pal = sf.toks(block, block.pal)
    others = []
    for r, arm in arms_in_table_order(sf, block):
        toks = sf.toks(block, arm)
        if len(toks) != len(pal):
            return None
        others.append((r, toks))
    diffs = []
    for i, t in enumerate(pal):
        vals = [(r, toks[i]) for r, toks in others]
        if any(o.text != t.text for _, o in vals):
            diffs.append((i, vals))
    return diffs


def delta_note(pal_tok, vals):
    """`review` text when a numeric delta is outside DELTAS_KNOWN, else None."""
    pv = literal_value(pal_tok)
    if pv is None:
        return None
    for r, o in vals:
        ov = literal_value(o)
        if ov is None:
            return None
        d = pv - ov
        if d == 0:
            continue
        if d != int(d) or abs(int(d)) not in DELTAS_KNOWN:
            return "delta %s" % (("%g" % d) if isinstance(d, float) else d)
    return None


def substitute(sf, block, subs):
    """The PAL arm's bytes with token ranges replaced: subs = [(first tok,
    last tok, new bytes)]."""
    base = sf.line_start[block.pal.lo]
    text = sf.arm_bytes(block.pal)
    for first, last, new in sorted(subs, key=lambda s: -s[0].start):
        text = text[:first.start - base] + new + text[last.end - base:]
    return text


def indent_of(line):
    return re.match(rb"^[ \t]*", line).group(0)


def code_lines(sf, arm):
    return [sf.lines[k] for k in range(arm.lo, arm.hi)]


def reindent(lines, by=b"    "):
    out = []
    for line in lines:
        if line.strip() == b"" or line.lstrip().startswith(b"#"):
            out.append(line)
        else:
            out.append(by + line)
    return b"".join(out)


def top_level_statements(toks):
    """Start indices of the statements at brace depth 0 of an arm."""
    starts = []
    depth = 0
    at_start = True
    for i, t in enumerate(toks):
        if t.kind == "directive":
            at_start = True
            continue
        if at_start and depth == 0:
            starts.append(i)
        at_start = False
        if t.text in (b"(", b"[", b"{"):
            depth += 1
        elif t.text in (b")", b"]", b"}"):
            depth -= 1
            if t.text == b"}" and depth == 0:
                at_start = True
        elif t.text == b";" and depth == 0:
            at_start = True
        elif t.text == b":" and depth == 0 and i >= 1 and toks[i - 1].kind == "ident" and \
                (i == 1 or toks[i - 2].text in (b";", b"{", b"}", b":")):
            at_start = True
    return starts


def balance(toks):
    """(unclosed openers, unmatched closers) of a token run."""
    stack = []
    unmatched = 0
    for t in toks:
        if t.text in OPENERS:
            stack.append(t.text)
        elif t.text in CLOSERS:
            if stack and stack[-1] == CLOSERS[t.text]:
                stack.pop()
            else:
                unmatched += 1
    return len(stack), unmatched


def case_labels(toks):
    """Indices of the `case` tokens at brace depth 0 of an arm."""
    out = []
    depth = 0
    for i, t in enumerate(toks):
        if t.text == b"{":
            depth += 1
        elif t.text == b"}":
            depth -= 1
        elif t.text == b"case" and depth == 0:
            out.append(i)
    return out


def label_end(toks, i):
    """The index of the `:` ending the case label that starts at i."""
    depth = 0
    for j in range(i + 1, len(toks)):
        t = toks[j].text
        if t in (b"(", b"["):
            depth += 1
        elif t in (b")", b"]"):
            depth -= 1
        elif t == b":" and depth == 0:
            return j
    return len(toks)


# --------------------------------------------------------------------------
# The classes, one function each


def class_a2_switches(sf):
    """Decide every switch whose labels lie in region blocks.
    Returns {id(block): ("keep-labels"|"mixed"|"oneregion", switch frame)}
    and the head edits [(frame, {slug: delta})]."""
    view = sf.view
    in_switch = {}
    for b in sf.blocks:
        k = sf.anchor(b)
        frames = view.frames[k] if 0 <= k < len(view.toks) else ()
        top = frames[-1] if frames else None
        if top is None or top.kind != "switch":
            continue
        if any(case_labels(sf.toks(b, a)) for a in b.arms()):
            in_switch.setdefault(id(top), (top, []))[1].append(b)
    verdicts = {}
    heads = []
    for top, blocks in in_switch.values():
        deltas = {}
        mixed = None
        # labels outside every block keep their value on every disc
        for k in range(top.open + 1, view.match.get(top.open, len(view.toks))):
            t = view.toks[k]
            if t.text == b"case" and view.frames[k] and view.frames[k][-1] is top and t.owner is None:
                deltas.setdefault("outside", set()).add(0)
        for b in blocks:
            arms = [a for a in b.arms() if not a.empty(sf.lines)]
            if len(arms) < 2 or b.pal.empty(sf.lines):
                mixed = mixed or "oneregion"
                verdicts[id(b)] = ("oneregion", top)
                continue
            diffs = aligned_arms(sf, b)
            if diffs is None or any(not is_literal(sf.toks(b, b.pal)[i]) for i, _ in diffs):
                mixed = mixed or "mixed"
                verdicts[id(b)] = ("mixed", top)
                continue
            pal = sf.toks(b, b.pal)
            diff_at = dict(diffs)
            for c in case_labels(pal):
                end = label_end(pal, c)
                inside = [i for i in range(c + 1, end) if i in diff_at]
                if len(inside) > 1:
                    mixed = mixed or "mixed"
                    verdicts[id(b)] = ("mixed", top)
                    break
                for r in sf.regions[1:]:
                    d = 0
                    if inside:
                        vals = dict((x, o) for g, o in diff_at[inside[0]] for x in g.slugs)
                        if r.slug in vals:
                            pv, ov = literal_value(pal[inside[0]]), literal_value(vals[r.slug])
                            d = None if pv is None or ov is None else pv - ov
                    deltas.setdefault(r.slug, set()).add(d)
            verdicts.setdefault(id(b), ("keep-labels", top))
        uniform = mixed is None and all(len(v) == 1 and None not in v for v in deltas.values())
        per_slug = {s: next(iter(v)) for s, v in deltas.items() if s != "outside"} if uniform else {}
        if uniform and "outside" in deltas and any(d != 0 for d in per_slug.values()):
            uniform = False
        head_in_block = view.toks[top.head[0]].owner is not None
        if uniform and head_in_block and any(d != 0 for d in per_slug.values()):
            uniform = False
        if not uniform:
            for b in blocks:
                v = verdicts[id(b)][0]
                verdicts[id(b)] = ("oneregion" if v == "oneregion" else "mixed", top)
            continue
        if any(d != 0 for d in per_slug.values()):
            heads.append((top, per_slug))
    return verdicts, heads


def switch_kind(expr):
    for word in (b"property", b"layout", b"anim", b"stage"):
        if word in expr.lower():
            return word.decode() + " "
    return ""


def switch_head_edit(sf, frame, per_slug):
    """Byte edits that add + SEL(0, d) to the switch's scrutinee and the
    comment after its brace."""
    view = sf.view
    _, lp, rp = frame.head
    expr_toks = view.toks[lp + 1:rp]
    expr = sf.data[view.toks[lp].end:view.toks[rp].start]
    simple = True
    depth = 0
    for i, t in enumerate(expr_toks):
        if t.text in OPENERS:
            if depth == 0 and t.text == b"(" and (i == 0 or expr_toks[i - 1].kind not in ("ident",) and
                                                    expr_toks[i - 1].text not in (b")", b"]")):
                simple = False
            depth += 1
        elif t.text in CLOSERS:
            depth -= 1
        elif depth == 0 and t.kind == "punct" and t.text not in (b".", b"->"):
            simple = False
    values = [(r, str(per_slug.get(r.slug, 0)).encode()) for r in sf.regions[1:]]
    sel = selector_call(sf, b"0", values)
    new_expr = (expr if simple else b"(" + expr + b")") + b" + " + sel
    parts = []
    for g, v in merged_values(sf, b"0", [(sf.group((r.slug,)), v) for r, v in values]):
        d = int(v)
        parts.append("%s %sids %d %s" % (g.upper, switch_kind(expr), abs(d), "lower" if d > 0 else "higher"))
    comment = b" /* " + ", ".join(parts).encode() + b" */"
    brace = view.toks[frame.open]
    return [(view.toks[lp].end, view.toks[rp].start, new_expr), (brace.end, brace.end, comment)]


def is_member(toks, i):
    """Is toks[i] a member name (after . or ->)? A member that differs names
    a field of a struct whose layout differs: the port keeps PAL's layout."""
    return toks[i].kind == "ident" and i > 0 and toks[i - 1].text in (b".", b"->")


def class_a1_tokens(sf, block, keep_labels):
    """A1: equal token counts, differing tokens literals."""
    diffs = aligned_arms(sf, block)
    if diffs is None or block.pal.empty(sf.lines):
        return None
    pal = sf.toks(block, block.pal)
    if not diffs:
        return Outcome("token", sf.arm_bytes(block.pal), "arms equal"), []
    label_positions = set()
    if keep_labels:
        for c in case_labels(pal):
            label_positions.update(range(c + 1, label_end(pal, c)))
    for i, vals in diffs:
        if is_member(pal, i) and all(o.kind == "ident" for _, o in vals):
            continue
        if not is_literal(pal[i]) or any(not is_literal(o) for _, o in vals):
            return None
        if pal[i].kind == "string":
            for nb in (i - 1, i + 1):
                if 0 <= nb < len(pal) and pal[nb].kind == "string":
                    return None
    subs = []
    reports = []
    for i, vals in diffs:
        t = pal[i]
        if i in label_positions:
            continue
        reason = "member" if is_member(pal, i) else deny_reason(sf.view, t.vi)
        if reason is None and offset16(t, vals):
            reason = "offset-16"
        shown = selector_call(sf, t.text, [(r, o.text) for r, o in vals]).decode("latin-1")
        if reason == "static-init":
            return "data-twin", "a static local's initialiser differs: %s" % declared_name(
                sf.view.toks[stmt_start(sf.view, t.vi):])
        if reason:
            detail = "PAL %s kept (%s): %s" % (t.text.decode("latin-1"), reason, shown)
            reports.append(Report(sf.path, sf.lineno(t.start), "layout", reason, detail))
            if reason == "dimension" and any((literal_value(o) or 0) > (literal_value(t) or 0) for _, o in vals):
                reports.append(Report(sf.path, sf.lineno(t.start), "review", "dimension",
                                      "a region's dimension is larger than PAL's: %s" % shown))
            continue
        note = delta_note(t, vals)
        if note:
            reports.append(Report(sf.path, sf.lineno(t.start), "review", "delta", "%s %s" % (shown, note)))
        subs.append((t, t, selector_call(sf, t.text, [(r, o.text) for r, o in vals])))
    cls = "case-shift" if keep_labels else "token"
    kept = sum(1 for r in reports if r.kind == "layout")
    detail = "%d substituted, %d kept as PAL" % (len(subs), kept)
    return Outcome(cls, substitute(sf, block, subs), detail), reports


def class_a1p_initialised(sf, block):
    """A1': one arm declares a local with an initialiser, the other without."""
    arms = block.arms()
    if any(a.empty(sf.lines) for a in arms):
        return None
    forms = [sf.toks(block, a) for a in arms]
    longest = max(forms, key=len)
    if not is_declaration(longest) or longest[-1].text != b";":
        return None
    eq = next((i for i, t in enumerate(longest) if t.text == b"="), None)
    if eq is None:
        return None
    short = [t.text for t in longest[:eq]] + [b";"]
    full = [t.text for t in longest]
    for f in forms:
        texts = [t.text for t in f]
        if texts != full and texts != short:
            return None
    if all([t.text for t in f] == full for f in forms):
        return None
    arm = arms[forms.index(longest)]
    return Outcome("initialised", sf.arm_bytes(arm), "the initialised arm (%s)" % arm.name), []


# binary operators by how tightly they bind
PRECEDENCE = {b"*": 13, b"/": 13, b"%": 13, b"+": 12, b"-": 12, b"<<": 11, b">>": 11,
              b"<": 10, b"<=": 10, b">": 10, b">=": 10, b"==": 9, b"!=": 9, b"&": 8, b"^": 7,
              b"|": 6, b"&&": 5, b"||": 4, b"?": 3, b":": 3, b",": 1}
for _op in ASSIGN_OPS:
    PRECEDENCE[_op] = 2
OPERAND_END = ("ident", "number", "string", "char")
# a token before the span that closes nothing to its left: the span is a
# whole expression whatever it holds (but a comma, which no macro argument
# may hold at its top level)
OPEN_LEFT = {b"(", b"[", b",", b"return", b"?", b";", b"{", b"}"}
OPEN_RIGHT = {b")", b"]", b",", b";", b":"}
POSTFIX_RIGHT = {b"(", b"[", b".", b"->", b"++", b"--"}


def ends_operand(tok):
    return tok is not None and (tok.kind in OPERAND_END and tok.text not in KEYWORDS or
                                tok.text in (b")", b"]", b"++", b"--"))


def loosest_operator(span):
    """The precedence of the loosest binary operator at the span's top level
    (99 when it has none)."""
    loosest = 99
    depth = 0
    prev = None
    for t in span:
        if t.text in OPENERS:
            depth += 1
        elif t.text in CLOSERS:
            depth -= 1
        elif depth == 0 and t.text in PRECEDENCE and (ends_operand(prev) or t.text in (b"?", b":")):
            loosest = min(loosest, PRECEDENCE[t.text])
        prev = t
    return loosest


def left_binding(toks, lo):
    """How tightly the token before the span binds to its right: 0 when it
    opens an expression, the operator's precedence when it is a binary
    operator, 100 when it is a unary operator or a cast, 1000 when the span
    cannot start there (after an operand, a member access, or an operator
    that wants an lvalue: unary &, ++, --)."""
    t = toks[lo - 1]
    operand_before = lo >= 2 and ends_operand(toks[lo - 2]) and \
        not (toks[lo - 2].text == b")" and is_cast_close(toks, lo - 2))
    if t.text in (b"++", b"--") or (t.text == b"&" and not operand_before):
        return 1000
    if t.kind != "punct" and ends_operand(t) or t.text == b"]":
        return 1000
    if t.text == b")" and not is_cast_close(toks, lo - 1):
        return 1000
    if t.text in OPEN_LEFT:
        return 0
    if t.text in ASSIGN_OPS:
        return 1
    if t.text == b":":
        return 2
    if t.text in PRECEDENCE and operand_before:
        return PRECEDENCE[t.text]
    if t.text in (b".", b"->"):
        return 1000
    return 100


def is_cast_close(toks, k):
    """Is toks[k], a `)`, the end of a cast (a type name in parentheses)?"""
    depth = 0
    j = k
    while j >= 0:
        if toks[j].text == b")":
            depth += 1
        elif toks[j].text == b"(":
            depth -= 1
            if depth == 0:
                break
        j -= 1
    inner = toks[j + 1:k]
    if not inner or j < 0:
        return False
    if ends_operand(toks[j - 1] if j >= 1 else None) and toks[j - 1].text not in TYPE_WORDS:
        return False
    return all(t.kind == "ident" or t.text == b"*" for t in inner) and inner[-1].text == b"*" or \
        all(t.text in TYPE_WORDS for t in inner)


def span_in_declarator(form, lo):
    """Does the span starting at form[lo] sit in a declaration before its `=`?"""
    j = lo - 1
    while j >= 0 and form[j].text not in (b";", b"{", b"}") and form[j].kind != "directive":
        j -= 1
    stmt = form[j + 1:]
    head = form[j + 1:lo]
    return is_declaration(stmt) and b"=" not in [t.text for t in head]


def bit_word_span(spans):
    """Do the spans read a motion record's bit words (laid out as PAL's in
    memory on every disc)?"""
    for sp in spans:
        texts = [t.text for t in sp]
        for k, t in enumerate(texts):
            if t in (b"modeBits", b"flags2") and k and texts[k - 1] in (b".", b"->"):
                return True
            if t == b"flags" and k and texts[k - 1] in (b".", b"->") and \
                    (b"motionKind" in texts or (k + 1 < len(texts) and texts[k + 1] in (b".", b">>", b"<<"))):
                return True
    return False


def span_ok(forms, lo, hi):
    """None when the spans forms[i][lo:len-hi] are whole expressions in their
    place, else which way to widen them ("left", "right") or "fail"."""
    spans = [f[lo:len(f) - hi] for f in forms]
    for sp in spans:
        if any(t.text in (b";", b"{", b"}") or t.kind == "directive" for t in sp):
            return "fail"
    for sp in spans:
        opened, unmatched = balance(sp)
        if unmatched:
            return "left"
        if opened:
            return "right"
    for sp in spans:
        if not sp:
            return "left" if forms[0][lo - 1].text in (b"(", b"[") else "right"
    loosest = min(loosest_operator(sp) for sp in spans)
    if loosest <= PRECEDENCE[b","]:
        return "left"
    lb = left_binding(forms[0], lo)
    if lb >= 1000 or (lb >= 100 and loosest < 99) or (lb < 100 and loosest <= lb):
        return "left"
    right = forms[0][len(forms[0]) - hi]
    if right.text in POSTFIX_RIGHT or right.text in ASSIGN_OPS:
        return "right"
    if right.text not in OPEN_RIGHT and right.text in PRECEDENCE and loosest < PRECEDENCE[right.text]:
        return "right"
    if right.text not in OPEN_RIGHT and right.text not in PRECEDENCE:
        return "right"
    return None


def class_a3p_span(sf, block):
    """A3': the arms differ in one balanced span."""
    if not sf.view.in_function(sf.anchor(block)):
        return None
    arms = [block.pal] + [a for _, a in arms_in_table_order(sf, block)]
    if any(a.empty(sf.lines) for a in arms):
        return None
    forms = [sf.toks(block, a) for a in arms]
    if any(t.kind == "directive" for f in forms for t in f):
        return None
    n_min = min(len(f) for f in forms)
    lo = 0
    while lo < n_min and all(f[lo].text == forms[0][lo].text for f in forms):
        lo += 1
    hi = 0
    while hi < n_min - lo and all(f[len(f) - 1 - hi].text == forms[0][len(forms[0]) - 1 - hi].text for f in forms):
        hi += 1
    while True:
        if lo < 1 or hi < 1:
            return None
        spans = [f[lo:len(f) - hi] for f in forms]
        why = span_ok(forms, lo, hi)
        if why is None:
            break
        if why == "fail":
            return None
        if why == "left":
            lo -= 1
        else:
            hi -= 1
    if any(span_in_declarator(f, lo) for f in forms):
        return None
    pal_span = spans[0]
    reports = []
    reason = deny_reason(sf.view, pal_span[0].vi)
    if reason is None and bit_word_span(spans):
        reason = "bit-word"
    pal_text = sf.data[pal_span[0].start:pal_span[-1].end]
    values = [(r, spaced_text(sp)) for (r, _), sp in zip(arms_in_table_order(sf, block), spans[1:])]
    call = selector_call(sf, spaced_text(pal_span), values)
    if reason == "static-init":
        return "data-twin", "a static local's initialiser differs"
    if reason:
        reports.append(Report(sf.path, block.line, "layout", reason,
                              "PAL span kept (%s): %s" % (reason, call.decode("latin-1"))))
        return Outcome("span", sf.arm_bytes(block.pal), "PAL span kept"), reports
    new = selector_call(sf, pal_text, values)
    return Outcome("span", substitute(sf, block, [(pal_span[0], pal_span[-1], new)]), "one span"), reports


def statement_problem(sf, block):
    """Why the block's arms are not whole statements, as (subkind, detail),
    or None."""
    prev = sf.prev_view_token(block)
    nxt = sf.next_view_token(block)
    if prev is not None and prev.text not in (b";", b"{", b"}", b":") and prev.kind != "directive":
        return "unbalanced", "not at a statement boundary (after `%s`)" % prev.text.decode("latin-1")
    if nxt is not None and nxt.text == b"else":
        return "unbalanced", "an `else` follows the block"
    for arm in block.arms():
        toks = sf.toks(block, arm)
        if not toks:
            continue
        if balance(toks) != (0, 0):
            return "unbalanced", "arm %s opens or closes a bracket it does not close or open" % arm.name
        code = [t for t in toks if t.kind != "directive"]
        if code and code[-1].text not in (b";", b"}"):
            return "unbalanced", "arm %s does not end a statement" % arm.name
        if any(t.kind == "directive" and re.match(rb"#\s*(define|undef)\b", t.text) for t in toks):
            return "define", "arm %s defines a macro" % arm.name
        if case_labels(toks):
            return "case-mixed", "arm %s holds a case label" % arm.name
        for s in top_level_statements(toks):
            rest = toks[s:]
            if rest[0].kind == "ident" and len(rest) > 1 and rest[1].text == b":" and \
                    rest[0].text not in (b"default",):
                return "unbalanced", "arm %s has a label at its top level" % arm.name
            if is_declaration(rest):
                return "decl", "arm %s declares `%s` at its top level" % (arm.name, declared_name(rest))
    for arm in block.arms():
        if arm.hi - arm.lo > LARGE_LINES:
            return "large-variant", "arm %s is %d lines" % (arm.name, arm.hi - arm.lo)
    return None


def declared_name(toks):
    depth = 0
    name = b"?"
    for t in toks:
        if t.text in (b"(", b"["):
            if depth == 0 and name != b"?":
                break
            depth += 1
        elif t.text in (b")", b"]"):
            depth -= 1
        elif t.text in (b"=", b";", b","):
            break
        elif t.kind == "ident" and depth == 0 and t.text not in TYPE_WORDS:
            name = t.text
    return name.decode("latin-1")


def if_chain(sf, block, conds):
    """if (c1) { A1 } else if (c2) { A2 } ... [else { PAL }] with the arms'
    lines re-indented one step; conds = [(condition, arm)]."""
    first = None
    for _, arm in conds:
        for line in code_lines(sf, arm):
            if line.strip() and not line.lstrip().startswith(b"#"):
                first = line
                break
        if first is not None:
            break
    ind = indent_of(first or b"")
    lead = b""
    tail = b""
    out = b""
    for n, (cond, arm) in enumerate(conds):
        lines = code_lines(sf, arm)
        # blank lines at an arm's ends stay outside the braces
        while lines and lines[0].strip() == b"":
            if n == 0:
                lead += lines[0]
            lines = lines[1:]
        while lines and lines[-1].strip() == b"":
            if n == len(conds) - 1:
                tail = lines[-1] + tail
            lines = lines[:-1]
        if cond is None:
            out += ind + b"} else {\n"
        elif n == 0:
            out += ind + b"if (" + cond + b") {\n"
        else:
            out += ind + b"} else if (" + cond + b") {\n"
        out += reindent(lines)
    out += ind + b"}\n"
    return lead + out + tail


def class_a3_statements(sf, block):
    """A3: whole statements in two or more arms."""
    filled = [a for a in block.arms() if not a.empty(sf.lines)]
    if len(filled) < 2:
        return None
    problem = statement_problem(sf, block)
    if problem:
        return problem
    conds = [(b"ICO_" + a.upper.encode(), a) for a in block.named]
    if not block.pal.empty(sf.lines):
        conds.append((None, block.pal))
    return Outcome("if-else", if_chain(sf, block, conds), "%d arms" % len(conds)), []


def pure_declarations(toks):
    """The names when the tokens are only declarations whose initialisers
    call nothing and assign nothing, else None."""
    names = []
    starts = top_level_statements(toks)
    for n, s in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(toks)
        stmt = [t for t in toks[s:end] if t.kind != "directive"]
        if not stmt:
            continue
        if not is_declaration(stmt) or stmt[-1].text != b";" or stmt[0].text in (b"typedef", b"extern"):
            return None
        for k, t in enumerate(stmt):
            if t.text in (b"++", b"--") or (t.text in ASSIGN_OPS and t.text != b"="):
                return None
            if t.text == b"(" and k and stmt[k - 1].kind == "ident" and stmt[k - 1].text != b"sizeof" and \
                    b"=" in [x.text for x in stmt[:k]]:
                return None
        names.append(declared_name(stmt))
    return names or None


def names_in_other_blocks(sf, block):
    """Top-level names the other blocks of the same function declare."""
    fn = sf.view.function_name(sf.anchor(block))
    names = set()
    for b in sf.blocks:
        if b is block or sf.view.function_name(sf.anchor(b)) != fn:
            continue
        for a in b.arms():
            toks = sf.toks(b, a)
            for s in top_level_statements(toks):
                if is_declaration(toks[s:]):
                    names.add(declared_name(toks[s:]))
    return names


def class_a4_one_region(sf, block):
    """A4: statements in one region's arm only. An arm of declarations alone
    (initialisers that call and assign nothing) is kept for every region:
    the hoist the `decl` pattern asks for, with no statement left to gate."""
    filled = [a for a in block.arms() if not a.empty(sf.lines)]
    if len(filled) != 1:
        return None
    problem = statement_problem(sf, block)
    arm = filled[0]
    if problem and problem[0] == "decl":
        names = pure_declarations(sf.toks(block, arm))
        if names and not set(names) & names_in_other_blocks(sf, block):
            text = b"".join(code_lines(sf, arm))
            return Outcome("one-region", text, "%s only, declarations kept: %s" % (arm.name, ", ".join(names))), []
    if problem:
        return problem
    if arm.key == "pal":
        # every other row has one text (empty): one set, `!ICO_JPN`, `!ICO_JPNKOR`
        named = [b"ICO_" + a.upper.encode() for a in block.named]
        if not named:
            return None
        cond = b"!" + named[0] if len(named) == 1 else b"!(" + b" || ".join(named) + b")"
    else:
        cond = b"ICO_" + arm.upper.encode()
    return Outcome("one-region", if_chain(sf, block, [(cond, arm)]), cond.decode()), []


class Item:
    """A top-level item of a file-scope arm."""

    def __init__(self, kind, name, toks):
        self.kind = kind    # include define directive typedef aggregate function prototype data head
        self.name = name
        self.toks = toks


def file_scope_items(toks):
    """Split a file-scope arm into items; None when it does not split."""
    items = []
    i = 0
    n = len(toks)
    while i < n:
        t = toks[i]
        if t.kind == "directive":
            if re.match(rb"#\s*include\b", t.text):
                items.append(Item("include", t.text, [t]))
            elif re.match(rb"#\s*(define|undef)\b", t.text):
                items.append(Item("define", t.text, [t]))
            else:
                items.append(Item("directive", t.text, [t]))
            i += 1
            continue
        depth = 0
        j = i
        while j < n:
            x = toks[j]
            if x.kind == "directive":
                break
            if x.text in (b"(", b"["):
                depth += 1
            elif x.text in (b")", b"]"):
                depth -= 1
            elif x.text == b"{":
                if depth == 0 and j > i and toks[j - 1].text == b")" and b"=" not in [y.text for y in toks[i:j]]:
                    end = brace_end(toks, j)
                    if end is None:
                        items.append(Item("head", function_name(toks[i:j]), toks[i:]))
                        return items
                    items.append(Item("function", function_name(toks[i:j]), toks[i:end + 1]))
                    j = end + 1
                    break
                end = brace_end(toks, j)
                if end is None:
                    return None
                j = end
            elif x.text == b";" and depth == 0:
                items.append(classify_declaration(toks[i:j + 1]))
                j += 1
                break
            j += 1
        else:
            if depth == 0 and toks[i:] and toks[-1].text == b")":
                items.append(Item("head", function_name(toks[i:]), toks[i:]))
                return items
            return None
        if j < n and toks[j - 1].kind == "directive":
            return None
        i = j
    return items


def brace_end(toks, j):
    depth = 0
    for k in range(j, len(toks)):
        if toks[k].text == b"{":
            depth += 1
        elif toks[k].text == b"}":
            depth -= 1
            if depth == 0:
                return k
    return None


def function_name(toks):
    """The name before the parameter list of a declarator."""
    depth = 0
    for k in range(len(toks) - 1, -1, -1):
        t = toks[k].text
        if t == b")":
            depth += 1
        elif t == b"(":
            depth -= 1
            if depth == 0 and k > 0 and toks[k - 1].kind == "ident":
                return toks[k - 1].text.decode("latin-1")
    return None


def typedef_name(toks):
    """The last identifier at depth 0, past any __attribute__((...))."""
    name = None
    depth = 0
    k = 0
    while k < len(toks):
        t = toks[k]
        if t.text == b"__attribute__":
            k += 1
            if k < len(toks) and toks[k].text == b"(":
                j = k
                d = 0
                while j < len(toks):
                    d += {b"(": 1, b")": -1}.get(toks[j].text, 0)
                    if d == 0:
                        break
                    j += 1
                k = j + 1
            continue
        if t.text in (b"(", b"[", b"{"):
            depth += 1
        elif t.text in (b")", b"]", b"}"):
            depth -= 1
        elif t.kind == "ident" and depth == 0:
            name = t.text.decode("latin-1")
        k += 1
    return name


def classify_declaration(toks):
    texts = [t.text for t in toks]
    if b"typedef" in texts:
        return Item("typedef", typedef_name(toks), toks)
    if texts and texts[0] in AGGREGATE_KEYS and b"{" in texts and texts[-2] == b"}":
        return Item("aggregate", None, toks)
    eq = texts.index(b"=") if b"=" in texts else len(texts)
    head = toks[:eq]
    paren = next((k for k, t in enumerate(head) if t.text == b"("), None)
    if paren is not None and paren > 0 and head[paren - 1].kind == "ident" and eq == len(texts):
        return Item("prototype", head[paren - 1].text.decode("latin-1"), toks)
    return Item("data", declared_name(toks), toks)


def class_a5a_file_scope(sf, block, defined_elsewhere):
    """A5a: a one-region file-scope arm whose items no other block defines."""
    filled = [a for a in block.arms() if not a.empty(sf.lines)]
    if len(filled) != 1:
        return None
    arm = filled[0]
    items = file_scope_items(sf.toks(block, arm))
    if items is None or any(it.kind == "head" for it in items):
        return "unbalanced", "arm %s is not whole declarations" % arm.name
    if any(it.kind == "define" for it in items):
        return "define", "arm %s defines a macro" % arm.name
    for it in items:
        if it.name and it.name in defined_elsewhere and it.kind in ("function", "data", "prototype"):
            sub = "function-twin" if it.kind == "function" else "data-twin"
            return sub, "`%s` is defined in another block of the file too" % it.name
    names = ", ".join(sorted(set(str(it.name) for it in items if it.name))) or "includes"
    text = b"".join(code_lines(sf, arm))
    return Outcome("file-scope", text, "%s only: %s" % (arm.name, names)), []


def class_a5b_hand(sf, block):
    """A5b: the file-scope hand list, as (subkind, detail)."""
    arms = [a for a in block.arms() if not a.empty(sf.lines)]
    item_sets = []
    for a in arms:
        items = file_scope_items(sf.toks(block, a))
        if items is None:
            return "unbalanced", "arm %s is not whole declarations" % a.name
        item_sets.append((a, items))
    kinds = set(it.kind for _, items in item_sets for it in items)
    if "define" in kinds:
        names = [it.name for _, items in item_sets for it in items if it.kind == "define"]
        return "define", names[0].split(None, 2)[1].decode("latin-1") if names else "a macro"
    if kinds & {"typedef", "aggregate"}:
        names = sorted(set(str(it.name) for _, items in item_sets for it in items
                           if it.kind in ("typedef", "aggregate")))
        return "layout-typedef", "a type differs: %s" % ", ".join(names)
    names = [sorted(set(str(it.name) for it in items if it.name)) for _, items in item_sets]
    if any(n != names[0] for n in names):
        return "rename", " vs ".join("%s: %s" % (a.name, ", ".join(n)) for (a, _), n in zip(item_sets, names))
    if "head" in kinds:
        return "decl", "a function's declarator differs: %s" % ", ".join(names[0])
    if kinds <= {"function"}:
        return "function-twin", ", ".join(names[0])
    if kinds <= {"prototype"} or "prototype" in kinds:
        return "decl", "a declaration differs: %s" % ", ".join(names[0])
    if kinds <= {"data"}:
        return data_subkind(sf, block, item_sets)
    return "decl", "mixed items: %s" % ", ".join(names[0])


def data_subkind(sf, block, item_sets):
    """array-size, extern-init, data-twin or decl for two data arms."""
    forms = [[t.text for it in items for t in it.toks] for _, items in item_sets]
    name = ", ".join(sorted(set(str(it.name) for _, items in item_sets for it in items)))
    if len(set(len(f) for f in forms)) == 1:
        diffs = [i for i in range(len(forms[0])) if any(f[i] != forms[0][i] for f in forms)]
        f0 = forms[0]
        eq = f0.index(b"=") if b"=" in f0 else len(f0)
        dims = set()
        depth = 0
        for i, t in enumerate(f0[:eq]):
            if t == b"[":
                depth += 1
            elif t == b"]":
                depth -= 1
            elif depth > 0:
                dims.add(i)
        if diffs and all(i in dims for i in diffs):
            return "array-size", "%s: take the larger" % name
        if diffs and all(i > eq for i in diffs):
            if b"{" in f0[eq:] or b"[" in f0[:eq]:
                return "data-twin", "%s: initialisers differ" % name
            return "extern-init", "%s: initialisers differ" % name
    texts = [b"".join(f) for f in forms]
    if all(b"=" not in t for t in texts):
        return "decl", "%s: declarations differ" % name
    return "data-twin", "%s: definitions differ" % name


# --------------------------------------------------------------------------
# A file's analysis and conversion


class Analysis:
    def __init__(self, sf):
        self.sf = sf
        self.outcomes = []   # (block, Outcome or None)
        self.reports = []
        self.edits = []      # (start, end, bytes)


def file_scope_names(sf, block):
    """Names declared at the top level of every other file-scope block."""
    names = set()
    for b in sf.blocks:
        k = sf.anchor(b)
        if b is block or (0 <= k < len(sf.view.toks) and sf.view.in_function(k)):
            continue
        for a in b.arms():
            items = file_scope_items(sf.toks(b, a)) or []
            for it in items:
                if it.name and it.kind in ("function", "data", "prototype", "head"):
                    names.add(it.name)
    return names


def twin_names(twins, path):
    names = {}
    for p, fn, tw, _ in twins:
        if p == path:
            names[fn] = tw
            names[tw] = fn
    return names


def analyse(sf, twins=()):
    """Classify every block of the file."""
    an = Analysis(sf)
    view = sf.view
    verdicts, heads = class_a2_switches(sf)
    twinned = twin_names(twins, sf.path)
    for block in sf.blocks:
        k = sf.anchor(block)
        in_func = view.in_function(k) if 0 <= k < len(view.toks) else False
        frames = view.frames[k] if 0 <= k < len(view.toks) else ()
        result = None
        verdict = verdicts.get(id(block))
        if not in_func:
            result = classify_file_scope(sf, block, frames)
        elif verdict and verdict[0] == "oneregion":
            result = ("case-in-oneregion", "a case label in one region's arm only")
        elif verdict and verdict[0] == "mixed":
            result = ("case-mixed", "the switch's labels do not shift by one delta")
        else:
            result = classify_in_function(sf, block, frames, keep_labels=bool(verdict))
        if isinstance(result, tuple) and isinstance(result[0], Outcome):
            outcome, reports = result
            an.outcomes.append((block, outcome))
            an.reports.extend(reports)
            start, end = sf.block_range(block)
            an.edits.append((start, end, outcome.text))
        else:
            subkind, detail = result
            an.outcomes.append((block, None))
            an.reports.append(Report(sf.path, block.line, "hand", subkind, detail))
        fn = view.function_name(k) if in_func else None
        defined = [fn] if fn else [it.name for a in block.arms()
                                   for it in (file_scope_items(sf.toks(block, a)) or []) if it.kind == "function"]
        for name in dict.fromkeys(defined):
            if name in twinned:
                an.reports.append(Report(sf.path, block.line, "twin", name,
                                         "inside a region twin (%s / %s): mirror the change into the other body"
                                         % (name, twinned[name])))
    for frame, per_slug in heads:
        an.edits.extend(switch_head_edit(sf, frame, per_slug))
    return an


def classify_file_scope(sf, block, frames):
    if any(f.kind == "aggregate" for f in frames):
        return "layout-typedef", "inside a struct or union"
    if any(f.kind == "init" for f in frames):
        return "data-twin", "inside a file-scope initialiser"
    result = class_a5a_file_scope(sf, block, file_scope_names(sf, block))
    if result is not None:
        return result
    return class_a5b_hand(sf, block)


def classify_in_function(sf, block, frames, keep_labels):
    for cls in (lambda: class_a1_tokens(sf, block, keep_labels),
                lambda: class_a1p_initialised(sf, block),
                lambda: class_a3p_span(sf, block)):
        result = cls()
        if result is not None:
            return result
    if any(f.kind in ("aggregate", "init") for f in frames):
        return "data-twin", "inside a local initialiser or struct, the arms do not line up"
    if keep_labels:
        return "case-mixed", "the arms around the labels do not line up"
    result = class_a3_statements(sf, block) or class_a4_one_region(sf, block)
    if result is not None:
        return result
    return "unbalanced", "no class fits"


def apply_edits(data, edits):
    out = data
    for start, end, new in sorted(edits, key=lambda e: (-e[0], -e[1])):
        out = out[:start] + new + out[end:]
    return out


# --------------------------------------------------------------------------
# Trees


def tree_files(root, paths=None):
    if paths:
        for p in paths:
            full = p if os.path.isabs(p) else os.path.join(root, p)
            if os.path.isdir(full):
                for f in walk(full):
                    yield f
            else:
                yield full
        return
    for d in DIRS:
        top = os.path.join(root, d)
        if os.path.isdir(top):
            for f in walk(top):
                yield f


def walk(top):
    for dirpath, dirnames, filenames in os.walk(top):
        dirnames.sort()
        for f in sorted(filenames):
            if f.endswith(EXTS):
                yield os.path.join(dirpath, f)


def rel(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def read(path):
    with open(path, "rb") as fh:
        return fh.read()


def need_regions(root):
    """The root's table; a root without one (upstream's tree, converted
    before a merge) takes the table of the tree this tool is in."""
    regions = load_regions(root)
    if regions is None and os.path.abspath(root) != ROOT:
        regions = load_regions(ROOT)
    if regions is None:
        raise RegionError("%s is missing: the table of regions drives every command" % REGIONS_TXT)
    return regions


def need_twins(root):
    """The root's twins table; a root without one (upstream's tree) takes the
    table of the tree this tool is in, so `report --root` still prints the
    `twin` lines for the port's hand-made twins."""
    if os.path.abspath(root) != ROOT and not os.path.exists(os.path.join(root, TWINS_TXT)):
        return load_twins(ROOT)
    return load_twins(root)


def analyse_tree(root, regions, twins, paths=None):
    """[(path, SourceFile, Analysis)] for the files that carry a block, and
    the parse errors."""
    results = []
    errors = []
    for p in tree_files(root, paths):
        data = read(p)
        if b"VERSION_" not in data:
            continue
        r = rel(root, p)
        try:
            sf = SourceFile(r, data, regions)
        except RegionError as e:
            errors.append(str(e))
            continue
        if not sf.blocks:
            continue
        results.append((p, sf, analyse(sf, twins)))
    return results, errors


def cmd_convert(root, paths):
    regions = need_regions(root)
    twins = need_twins(root)
    results, errors = analyse_tree(root, regions, twins, paths)
    counts = {}
    for p, sf, an in results:
        new = sf.output(an)
        if new != sf.orig:
            with open(p, "wb") as fh:
                fh.write(new)
        for _, outcome in an.outcomes:
            cls = outcome.cls if outcome else "hand"
            counts[cls] = counts.get(cls, 0) + 1
    for e in errors:
        print("error: %s" % e)
    for cls in sorted(counts):
        print("%6d  %s" % (counts[cls], cls))
    print("%6d  blocks in %d files" % (sum(counts.values()), len(results)))
    hand = counts.get("hand", 0)
    if hand:
        print("region_blocks: %d block(s) stay raw: `tools/region_blocks.py report` lists them" % hand)
    return 1 if errors else 0


def cmd_report(root, show_all, bits):
    regions = need_regions(root)
    twins = need_twins(root)
    results, errors = analyse_tree(root, regions, twins)
    if bits:
        for row in bits_rows(results, regions):
            print(row)
        for e in errors:
            print("error: %s" % e, file=sys.stderr)
        return 1 if errors else 0
    rows = []
    for p, sf, an in results:
        rows.extend(an.reports)
        if show_all:
            for block, outcome in an.outcomes:
                if outcome is not None:
                    rows.append(Report(sf.path, block.line, "auto", outcome.cls, outcome.detail))
    for r in sorted(rows, key=lambda r: (r.path, r.line, r.kind != "hand", r.kind, r.subkind, r.detail)):
        print(r.row())
    for e in errors:
        print("error: %s" % e, file=sys.stderr)
    return 1 if errors else 0


# --------------------------------------------------------------------------
# report --bits


BIT_FIELDS = (("modeBits", "MotionModeBits"), ("flags2", "MotionFlags2"), ("flags", "MotionFlags"))


def bit_field_type(line_bytes, before):
    """The struct type of the last bit word named on the line before `before`."""
    best = None
    for word, type_name in BIT_FIELDS:
        for m in re.finditer(rb"\b%s\b" % word.encode(), line_bytes):
            if m.start() < before and (best is None or m.start() > best[0]):
                best = (m.start(), type_name)
    return best[1] if best else None


def bit_position(toks, i):
    """(position, width) of the shift whose count is toks[i]."""
    n = literal_value(toks[i])
    op = toks[i - 1].text
    nxt = [t.text for t in toks[i + 1:i + 4]]
    if op == b">>":
        if i >= 4 and toks[i - 2].text == b")" and toks[i - 4].text == b"<<":
            a = literal_value(toks[i - 3])
            if a is not None:
                return n - a, 32 - n
        if len(nxt) >= 3 and nxt[0] == b")" and nxt[1] == b"&":
            mask = literal_value(toks[i + 3])
            if isinstance(mask, int) and mask > 0 and (mask & (mask + 1)) == 0:
                return n, mask.bit_length()
        return n, None
    if op == b"<<":
        if len(nxt) >= 3 and nxt[0] == b")" and nxt[1] == b">>":
            b = literal_value(toks[i + 3])
            if b is not None:
                return b - n, 32 - b
        if i >= 2 and literal_value(toks[i - 2]) == 1:
            return n, 1
    return n, None


def bits_rows(results, regions):
    rows = []
    for p, sf, an in results:
        base = os.path.basename(sf.path)
        for block in sf.blocks:
            diffs = aligned_arms(sf, block)
            if not diffs:
                continue
            pal = sf.toks(block, block.pal)
            for i, vals in diffs:
                t = pal[i]
                if t.kind != "number" or i == 0 or pal[i - 1].text not in (b">>", b"<<"):
                    continue
                ln = sf.line_of(t.start)
                line = sf.lines[ln]
                type_name = bit_field_type(line, t.start - sf.line_start[ln])
                if type_name is None:
                    continue
                pal_pos, width = bit_position(pal, i)
                for g, o in vals:
                    if o.text == t.text:
                        continue
                    toks = sf.toks(block, [a for a in block.named if a.key == g.slugs][0])
                    if o.kind == "number" and toks[i - 1].text == pal[i - 1].text:
                        other_pos, other_width = bit_position(toks, i)
                        note = ""
                    else:
                        other_pos, other_width = None, None
                        k = sf.line_of(o.start)
                        note = " (%s arm: %s)" % (g.slug, sf.lines[k].strip().decode("latin-1"))
                    w = width if width is not None else other_width
                    # one row per row of a shared arm: config/motion_bits.<slug>.txt is per slug
                    for slug in g.slugs:
                        rows.append("%s %s=%s pal=%s width=%s # %s:%d%s" % (
                            type_name, slug, "?" if other_pos is None else other_pos, pal_pos,
                            w if w is not None else "?", base, sf.line_map[ln] + 1, note))
    return rows


# --------------------------------------------------------------------------
# check


def include_targets(data):
    out = []
    for m in re.finditer(rb'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', data, re.M):
        out.append((m.group(1) == b'"', m.group(2).decode("latin-1")))
    return out


def search_dirs(root, path):
    parts = rel(root, path).split("/")
    dirs = []
    if parts[0] == "ico2" and len(parts) > 2:
        prog = parts[1]
        for p in (prog,) + INCLUDE_ORDER:
            d = os.path.join(root, "ico2", p, "include")
            if os.path.isdir(d) and d not in dirs:
                dirs.append(d)
    else:
        for p in INCLUDE_ORDER:
            d = os.path.join(root, "ico2", p, "include")
            if os.path.isdir(d) and d not in dirs:
                dirs.append(d)
    dirs.append(os.path.join(root, COMPAT_DIR))
    return dirs


def resolve_include(root, path, quoted, name):
    cands = []
    if quoted:
        cands.append(os.path.join(os.path.dirname(path), name))
    cands.extend(os.path.join(d, name) for d in search_dirs(root, path))
    for c in cands:
        if os.path.isfile(c):
            return os.path.normpath(c)
    return None


class IncludeGraph:
    def __init__(self, root, files):
        self.root = root
        self.edges = {}
        self.includers = {}
        for f in files:
            self._add(os.path.normpath(f))

    def _add(self, f):
        if f in self.edges:
            return
        self.edges[f] = []
        try:
            data = read(f)
        except OSError:
            return
        for quoted, name in include_targets(data):
            t = resolve_include(self.root, f, quoted, name)
            if t is None:
                continue
            self.edges[f].append(t)
            self.includers.setdefault(t, set()).add(f)
            self._add(t)

    def reaches(self, f, target):
        seen = set()
        todo = [f]
        while todo:
            x = todo.pop()
            if x == target:
                return True
            if x in seen:
                continue
            seen.add(x)
            todo.extend(self.edges.get(x, []))
        return False

    def covered(self, f, target, seen=None):
        """f reaches target, or every file including f is covered."""
        seen = seen or set()
        if self.reaches(f, target):
            return True
        if f in seen:
            return False
        seen = seen | {f}
        incl = self.includers.get(f, set())
        return bool(incl) and all(self.covered(i, target, seen) for i in incl)


SHIFTS = (b">>", b"<<", b">>=", b"<<=")


def shift_count(view, k):
    """Whether the expression at view index k is (part of) a shift's count:
    the token before it, or before a grouping parenthesis around it, is a
    shift (`w >> PAL_JPN(16, 19)`, `w >> (PAL_JPN(16, 19))`,
    `w >> (8 + PAL_JPN(8, 11))`)."""
    if view.text(k - 1) in SHIFTS:
        return True
    p = view.parent[k]
    while p is not None and view.text(p) == b"(":
        before = view.toks[p - 1] if p > 0 else None
        # a call's or a cast's parenthesis ends the walk
        if before is not None and (before.kind == "ident" or before.text in (b")", b"]")):
            return False
        if view.text(p - 1) in SHIFTS:
            return True
        p = view.parent[p]
    return False


def unary_amp(view, k):
    """Whether the `&` at view index k is the address-of operator (no operand before it)."""
    before = view.toks[k - 1] if k >= 1 else None
    if before is not None and before.text == b")" and (k - 1) in view.match:
        # after a cast, `(Item *)&x`: a parenthesis of type words and `*`
        # that is no call's
        o = view.match[k - 1]
        inner = view.toks[o + 1:k - 1]
        head = view.toks[o - 1] if o >= 1 else None
        if inner and all(t.kind == "ident" or t.text == b"*" for t in inner) and \
                (head is None or not (head.kind == "ident" or head.text in (b")", b"]"))) and \
                (len(inner) > 1 or inner[0].text in TYPE_WORDS or inner[0].text.endswith(b"_t")):
            return True
    return before is None or not (before.kind in ("ident", "number", "string", "char") or
                                  before.text in (b")", b"]"))


def constant_context(view, k):
    """Why a selector at view index k sits in a constant context, or None."""
    if shift_count(view, k):
        return "after a shift"
    if view.text(k - 1) == b"sizeof":
        return "inside sizeof"
    p = view.parent[k]
    while p is not None:
        tp = view.toks[p].text
        if tp == b"(" and view.text(p - 1) == b"sizeof":
            return "inside sizeof"
        if tp == b"[":
            lo, hi = view.statement(p)
            stmt = view.toks[lo:hi]
            first_assign = next((j for j in range(lo, hi) if view.toks[j].text == b"=" and
                                 view.outer_paren(j) == j), hi)
            if is_declaration(stmt) and p < first_assign:
                return "in an array dimension"
        p = view.parent[p]
    j = k - 1
    while j >= 0 and view.toks[j].text not in (b";", b"{", b"}", b":"):
        if view.toks[j].text == b"case":
            return "in a case label"
        j -= 1
    if not view.in_function(k):
        return "outside a function (a static initialiser)"
    lo, hi = view.statement(k)
    if lo < hi and view.toks[lo].text == b"static":
        return "in a static local's initialiser"
    return None


def cmd_check(root, quiet=False):
    """0: clean; 2: raw blocks remain, all hand items; 1: a rule misfired."""
    problems = []
    hand = []
    regions = None
    try:
        regions = load_regions(root)
    except RegionError as e:
        problems.append(str(e))
    have_table = regions is not None
    if regions is None:
        regions = default_regions()
    names = region_names(regions)
    twins = []
    try:
        twins = load_twins(root)
    except RegionError as e:
        problems.append(str(e))
    header = os.path.normpath(os.path.join(root, HEADER))
    spellers = []
    files = list(tree_files(root))
    defined_functions = {}
    mentions = re.compile(rb"\bVERSION_|\b(%s)\b" % "|".join(sorted(names)).encode())
    twin_paths = set(t[0] for t in twins)
    for p in files:
        r = rel(root, p)
        data = read(p)
        is_header = os.path.normpath(p) == header
        if p.endswith((".s", ".S")):
            for m in VERSION_WORD.finditer(data):
                problems.append("%s: %s in an assembly file" % (r, m.group(0).decode("latin-1")))
            continue
        if not mentions.search(data) and r not in twin_paths:
            continue
        try:
            sf = SourceFile(r, data, regions)
        except RegionError as e:
            if not have_table and b"VERSION_" in data:
                problems.append("%s: %s is missing, so no VERSION_ block can be read" % (r, REGIONS_TXT))
            else:
                problems.append(str(e))
            continue
        defined_functions[r] = set(f.name for fr in sf.view.frames for f in fr if f.kind == "func" and f.name)
        # VERSION_ only on the directive lines of raw blocks
        directive_lines = set()
        for b in sf.blocks:
            directive_lines.update(b.orig_directive_lines)
        for m in VERSION_WORD.finditer(b"" if is_header else data):
            ln = data.count(b"\n", 0, m.start())
            if ln not in directive_lines:
                problems.append("%s:%d: %s outside a region directive" % (r, ln + 1, m.group(0).decode("latin-1")))
        if sf.blocks:
            an = analyse(sf, twins)
            if sf.output(an) != data:
                converted = sum(1 for _, o in an.outcomes if o is not None)
                problems.append("%s: convert would rewrite %d block(s): run tools/region_blocks.py convert"
                                % (r, converted))
            hand.extend(x for x in an.reports if x.kind == "hand")
        if is_header:
            continue
        # no conditional on the region and no second definition of its macros
        for s, e, text in logical_directives(sf.lines, sf.states):
            code = split_comment(text)[0]
            words = set(w.decode("latin-1") for w in re.findall(rb"\b\w+\b", code))
            if re.match(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)\b", code) and words & names:
                problems.append("%s:%d: a conditional on the region (%s): the region is a run-time value"
                                % (r, sf.line_map[s] + 1, ", ".join(sorted(words & names))))
            m = re.match(rb"^[ \t]*#[ \t]*(define|undef)[ \t]+(\w+)", code)
            if m and m.group(2).decode("latin-1") in names:
                problems.append("%s:%d: #%s %s outside %s" % (r, sf.line_map[s] + 1, m.group(1).decode(),
                                                              m.group(2).decode(), HEADER))
        view = sf.view
        uses = [k for k, t in enumerate(view.toks) if t.kind == "ident" and t.text.decode("latin-1") in names and
                (t.text != b"LN" or view.text(k + 1) == b"(")]
        if any(t.kind == "directive" and re.search(rb"\b(%s)\b" % b"|".join(n.encode() for n in names), t.text)
               for t in sf.tokens) or uses:
            spellers.append(p)
        for k in uses:
            word = view.toks[k].text.decode("latin-1")
            if word.startswith("PAL_") or word == "LN":
                why = constant_context(view, k)
                if why:
                    problems.append("%s:%d: %s %s" % (r, sf.lineno(view.toks[k].start), word, why))
        # a name that stands for a selector: a data twin (`#define NAME
        # PAL_JPN(NAME_pal, NAME_jpn)` or `(*PAL_JPN(&..., &...))` in this
        # file) is a selector in every rule above, and a plain one is no
        # lvalue (`&NAME` would take the address of the PAL array's
        # pointer, or fail); a function of the twins table has no size and
        # no place in a shift count
        aliases = {}
        for s_, e_, text in logical_directives(sf.lines, sf.states):
            code = split_comment(text)[0]
            m = re.match(rb"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(\(?[ \t]*\*[ \t]*)?(\w+)[ \t]*\(", code)
            if m and (m.group(3).startswith(b"PAL_") or m.group(3) == b"LN") and \
                    m.group(3).decode("latin-1") in names:
                aliases[m.group(1)] = "deref" if m.group(2) else "plain"
        twin_names = set(n.encode() for t in twins for n in (t[1], t[2]))
        for k, t in enumerate(view.toks):
            if t.kind != "ident" or (t.text not in aliases and t.text not in twin_names):
                continue
            if view.text(k + 1) == b"(" and t.text in aliases:
                continue  # a function-like use of the same spelling: not this macro
            word = t.text.decode("latin-1")
            line = sf.lineno(t.start)
            if t.text in aliases:
                why = constant_context(view, k)
                # `&NAME[i]` and `&NAME->f` take the element's address: fine
                if why is None and aliases[t.text] == "plain" and view.text(k - 1) == b"&" and \
                        view.text(k + 1) not in (b"[", b".", b"->") and unary_amp(view, k - 1):
                    why = "after a unary & (the data twin is a selector, not an lvalue)"
                if why:
                    problems.append("%s:%d: the data twin %s %s" % (r, line, word, why))
            else:
                if shift_count(view, k):
                    why = "after a shift"
                elif view.text(k - 1) == b"sizeof" or (
                        view.parent[k] is not None and view.text(view.parent[k]) == b"(" and
                        view.text(view.parent[k] - 1) == b"sizeof"):
                    why = "inside sizeof"
                elif view.text(k - 1) == b"&" and view.text(k + 1) != b"(" and unary_amp(view, k - 1):
                    why = "after a unary & (take the selected function, not one disc's)"
                else:
                    why = None
                if why:
                    problems.append("%s:%d: the twin %s %s" % (r, line, word, why))
    # region.h
    if have_table:
        want = gen_header(regions)
        if os.path.exists(header):
            have = read(header)
            if have != want:
                problems.append("%s is stale: run tools/region_blocks.py gen-header" % HEADER)
            m = re.search(rb"^[ \t]*#[ \t]*define[ \t]+LN\(([^)]*)\)", have, re.M)
            arity = len(m.group(1).split(b",")) if m else 0
            if arity != len(regions):
                problems.append("%s: LN takes %d argument(s), %s has %d rows (one value per row)"
                                % (HEADER, arity, REGIONS_TXT, len(regions)))
        elif spellers:
            problems.append("%s is missing and the game code names its macros: run gen-header" % HEADER)
    elif spellers:
        problems.append("%s is missing and the game code names the region macros" % REGIONS_TXT)
    # every file naming the macros reaches region.h
    if spellers and os.path.exists(header):
        graph = IncludeGraph(root, files)
        for p in spellers:
            if not graph.covered(os.path.normpath(p), header):
                problems.append("%s: names the region macros but does not reach %s (typedef.h includes it)"
                                % (rel(root, p), HEADER))
    if have_table:
        problems.extend(glue_problems(root, regions))
    # the twins table
    for path, fn, tw, n in twins:
        have = defined_functions.get(path)
        if have is None:
            problems.append("%s:%d: %s is not a game source" % (TWINS_TXT, n, path))
            continue
        for name in (fn, tw):
            if name not in have:
                problems.append("%s:%d: %s defines no function %s" % (TWINS_TXT, n, path, name))
    if not quiet:
        for p in problems:
            print("problem: %s" % p)
        for h in sorted(hand, key=lambda x: (x.path, x.line)):
            print(h.row())
    if problems:
        if not quiet:
            print("region_blocks: %d problem(s): a rule misfired or the tree needs `convert`/`gen-header`"
                  % len(problems))
        return 1
    if hand:
        if not quiet:
            print("region_blocks: %d raw region block(s) remain, every one a hand item "
                  "(tools/region_blocks.py report lists them)" % len(hand))
        return 2
    if not quiet:
        print("region_blocks: no region block under %s; the region forms are in order" % ", ".join(DIRS))
    return 0


def cmd_gen_table(root):
    regions = load_regions(root)
    if regions is None:
        raise RegionError("%s is missing: gen-table writes %s from it" % (REGIONS_TXT, REGION_TABLE))
    path = os.path.join(root, REGION_TABLE)
    want = gen_table(regions)
    if os.path.exists(path) and read(path) == want:
        print("region_blocks: %s is up to date" % REGION_TABLE)
        return 0
    with open(path, "wb") as fh:
        fh.write(want)
    print("region_blocks: wrote %s (%d regions)" % (REGION_TABLE, len(regions)))
    return 0


def cmd_gen_header(root):
    regions = load_regions(root)
    if regions is None:
        raise RegionError("%s is missing: gen-header writes region.h from it" % REGIONS_TXT)
    path = os.path.join(root, HEADER)
    want = gen_header(regions)
    if os.path.exists(path) and read(path) == want:
        print("region_blocks: %s is up to date" % HEADER)
        return 0
    with open(path, "wb") as fh:
        fh.write(want)
    print("region_blocks: wrote %s (%d regions)" % (HEADER, len(regions)))
    return 0


# --------------------------------------------------------------------------
# rename: the two-row spelling to the shared-arm spelling


def rename_tokens(data):
    """The file's tokens with every directive's own tokens in place of the
    directive (a data twin's `#define name PAL_JPN(...)` is renamed too)."""
    out = []
    for t in tokenize(data):
        if t.kind != "directive":
            out.append(t)
            continue
        base = t.start + 1
        for u in tokenize(t.text[1:]):
            u.start += base
            u.end += base
            out.append(u)
    return out


def rename_bytes(data, regions, shared):
    """data with the two-row spelling rewritten for the row set `shared`
    (the rows that share the Japanese arm, jpn first): PAL_JPN( -> PAL_<SET>(,
    the identifier ICO_JPN -> ICO_<SET>, and LN(a, b) -> LN(a, b, ...) with
    one value per row of the table (b for a row of the set, a for any
    other). Comments and strings are never touched. Returns (bytes,
    {spelling: count})."""
    group = Group(shared)
    sel = selector_name([group]).encode()
    pred = b"ICO_" + group.upper.encode()
    toks = rename_tokens(data)
    edits = []
    counts = {"PAL_JPN": 0, "ICO_JPN": 0, "LN": 0}
    for k, t in enumerate(toks):
        if t.kind != "ident":
            continue
        nxt = toks[k + 1] if k + 1 < len(toks) else None
        paren = nxt is not None and nxt.text == b"("
        if t.text == b"PAL_JPN" and paren and sel != b"PAL_JPN":
            edits.append((t.start, t.end, sel))
            counts["PAL_JPN"] += 1
        elif t.text == b"ICO_JPN" and pred != b"ICO_JPN":
            edits.append((t.start, t.end, pred))
            counts["ICO_JPN"] += 1
        elif t.text == b"LN" and paren and len(regions) > 2:
            depth = 0
            commas = []
            close = None
            for j in range(k + 1, len(toks)):
                x = toks[j].text
                if x in OPENERS:
                    depth += 1
                elif x in CLOSERS:
                    depth -= 1
                    if depth == 0:
                        close = j
                        break
                elif x == b"," and depth == 1:
                    commas.append(j)
            if close is None or len(commas) != 1 or commas[0] == k + 2 or close == commas[0] + 1:
                continue
            first = data[toks[k + 2].start:toks[commas[0] - 1].end]
            second = data[toks[commas[0] + 1].start:toks[close - 1].end]
            first = rename_bytes(first, regions, shared)[0]
            second = rename_bytes(second, regions, shared)[0]
            extra = [second if r.slug in group.slugs else first for r in regions[2:]]
            at = toks[close - 1].end
            edits.append((at, at, b"".join(b", " + v for v in extra)))
            counts["LN"] += 1
    return apply_edits(data, edits), counts


def shared_rows(regions, spec=None):
    """The rows that share the Japanese arm: `spec` (comma-separated slugs),
    else every named row of the table (the Korean build is the Japanese
    program; a later target that is not names its rows with --set)."""
    slugs = [r.slug for r in regions]
    if spec:
        want = [s.strip() for s in spec.split(",") if s.strip()]
        for s in want:
            if s not in slugs[1:]:
                raise RegionError("--set: %s is not a named row of %s" % (s, REGIONS_TXT))
        if "jpn" not in want:
            raise RegionError("--set: the set renames the Japanese spelling, so it holds jpn")
        return [r for r in regions if r.slug in want]
    if "jpn" not in slugs[1:2]:
        raise RegionError("%s: rename maps the Japanese row's spelling; row 1 is not jpn" % REGIONS_TXT)
    return list(regions[1:])


def cmd_rename(root, check, spec=None):
    """Rewrite the two-row spelling under ico2/ and sce/ (region.h aside:
    gen-header writes it). --check: report, write nothing, exit 1 when
    anything would change."""
    regions = need_regions(root)
    shared = shared_rows(regions, spec)
    header = os.path.normpath(os.path.join(root, HEADER))
    totals = {"PAL_JPN": 0, "ICO_JPN": 0, "LN": 0}
    files = 0
    for p in tree_files(root):
        if os.path.normpath(p) == header or p.endswith((".s", ".S")):
            continue
        data = read(p)
        if b"PAL_JPN" not in data and b"ICO_JPN" not in data and b"LN" not in data:
            continue
        new, counts = rename_bytes(data, regions, shared)
        if new == data:
            continue
        files += 1
        for k, v in counts.items():
            totals[k] += v
        print("%s: %s" % (rel(root, p), ", ".join("%d %s" % (counts[k], k) for k in ("PAL_JPN", "ICO_JPN", "LN"))))
        if not check:
            with open(p, "wb") as fh:
                fh.write(new)
    group = Group(shared)
    print("region_blocks: %s %d file(s): %d PAL_JPN( -> %s(, %d ICO_JPN -> ICO_%s, %d LN(a, b) -> LN(%s)"
          % ("rename would rewrite" if check else "renamed in", files, totals["PAL_JPN"],
             selector_name([group]), totals["ICO_JPN"], group.upper, totals["LN"], ln_params(regions)))
    if not check and os.path.exists(header) and read(header) != gen_header(regions):
        print("region_blocks: %s is stale: run tools/region_blocks.py gen-header" % HEADER)
    return 1 if check and files else 0


# --------------------------------------------------------------------------
# --selftest


def selftest():
    failures = []
    tmp = tempfile.mkdtemp(prefix="region_blocks_")
    try:
        os.makedirs(os.path.join(tmp, "config"))
        os.makedirs(os.path.join(tmp, "ico2", "common", "include"))
        os.makedirs(os.path.join(tmp, "ico2", "common", "src"))
        shutil.copy(os.path.join(FIXTURE_DIR, "regions.txt"), os.path.join(tmp, REGIONS_TXT))
        shutil.copy(os.path.join(FIXTURE_DIR, "region_twins.txt"), os.path.join(tmp, TWINS_TXT))
        target = os.path.join(tmp, "ico2", "common", "src", "fixture.c")
        shutil.copy(os.path.join(FIXTURE_DIR, "fixture.c"), target)
        quiet = Quiet()

        # gen-header over the four-row table (pal, jpn, kor, us)
        with quiet:
            cmd_gen_header(tmp)
        header = read(os.path.join(tmp, HEADER))
        rows = load_regions(tmp)
        for want in (b"#define ICO_REGION_US 3", b"#define ICO_US (ico_region == ICO_REGION_US)",
                     b"#define ICO_JPNKORUS (ICO_JPN || ICO_KOR || ICO_US)",
                     b"#define PAL_US(pal, us) (ICO_US ? (us) : (pal))",
                     b"#define PAL_JPN_US(pal, jpn, us) (ICO_US ? (us) : ICO_JPN ? (jpn) : (pal))",
                     b"#define PAL_JPNKOR_US(pal, jpnkor, us) (ICO_US ? (us) : ICO_JPNKOR ? (jpnkor) : (pal))",
                     b"#define PAL_JPN(pal, jpn) (ICO_JPN ? (jpn) : (pal))",
                     b"#define LN(pal, jpn, kor, us) PAL_JPN_KOR_US(pal, jpn, kor, us)"):
            if want not in header:
                failures.append("gen-header: no line %r" % want)
        selectors = re.findall(rb"^#define (PAL_\w+)\(", header, re.M)
        if len(selectors) != 14:
            failures.append("gen-header: %d selectors for three named rows, want 14" % len(selectors))
        # the three-row shape (pal, jpn, kor), in order
        three = gen_header(rows[:3])
        want = b"\n".join((b"#define ICO_JPN (ico_region == ICO_REGION_JPN)",
                            b"#define ICO_KOR (ico_region == ICO_REGION_KOR)", b"",
                            b"/* A set of rows that share one arm upstream (`#if defined(VERSION_A) || "
                            b"defined(VERSION_B)`),",
                            b" * named by its slugs concatenated in table order. */",
                            b"#define ICO_JPNKOR (ICO_JPN || ICO_KOR)", b"",
                            b"/* Selector macros: the first argument is the PAL arm (upstream's #else), then one "
                            b"arm per",
                            b" * named region set in table order (a set of rows sharing an arm is one argument:",
                            b" * PAL_JPNKOR(pal, jpnkor)); a region a macro does not name takes the PAL arm. */",
                            b"#define PAL_JPN(pal, jpn) (ICO_JPN ? (jpn) : (pal))",
                            b"#define PAL_KOR(pal, kor) (ICO_KOR ? (kor) : (pal))",
                            b"#define PAL_JPNKOR(pal, jpnkor) (ICO_JPNKOR ? (jpnkor) : (pal))",
                            b"#define PAL_JPN_KOR(pal, jpn, kor) (ICO_KOR ? (kor) : ICO_JPN ? (jpn) : (pal))", b"",
                            b"/* LN(pal, jpn, kor): upstream's per-target __LINE__ values for the debug allocators "
                            b"and asserts. */",
                            b"#define LN(pal, jpn, kor) PAL_JPN_KOR(pal, jpn, kor)"))
        if want not in three:
            failures.append("gen-header: the three-row header is not the documented shape:\n%s"
                            % three.decode("ascii"))
        # two rows: the header main carried before the Korean row, byte for byte
        if gen_header(rows[:2]) != read(os.path.join(FIXTURE_DIR, "region_two_rows.h")):
            failures.append("gen-header: the two-row header differs from region_two_rows.h:\n" +
                            diff_text(read(os.path.join(FIXTURE_DIR, "region_two_rows.h")).decode("ascii"),
                                      gen_header(rows[:2]).decode("ascii")))
        table = gen_table(rows)
        for want in (b"extern const IcoRegionInfo ico_region_info_us;",
                     b"    &ico_region_info_pal,\n    &ico_region_info_jpn,\n    &ico_region_info_kor,\n"
                     b"    &ico_region_info_us,\n};"):
            if want not in table:
                failures.append("gen-table: no line %r" % want)

        # report --all and report --bits before conversion
        for flags, name in (((True, False), "expected_report.tsv"), ((False, True), "expected_bits.txt")):
            with quiet:
                cmd_report(tmp, *flags)
            got = quiet.text
            want = read(os.path.join(FIXTURE_DIR, name)).decode("latin-1")
            if got != want:
                failures.append("report differs from %s:\n%s" % (name, diff_text(want, got)))

        # convert, then again
        with quiet:
            cmd_convert(tmp, None)
        got = read(target)
        want_bytes = read(os.path.join(FIXTURE_DIR, "expected.c"))
        if got != want_bytes:
            failures.append("convert differs from expected.c:\n" +
                            diff_text(want_bytes.decode("latin-1"), got.decode("latin-1")))
        with quiet:
            cmd_convert(tmp, None)
        if read(target) != got:
            failures.append("convert is not idempotent")

        # check on the result: the hand blocks remain, all reported
        with quiet:
            code = cmd_check(tmp)
        if code != 2:
            failures.append("check on the converted fixture returned %d, want 2:\n%s" % (code, quiet.text))

        # check's constant contexts: exactly the lines marked "refused"
        ctx = os.path.join(tmp, "ico2", "common", "src", "check_contexts.c")
        shutil.copy(os.path.join(FIXTURE_DIR, "check_contexts.c"), ctx)
        with quiet:
            cmd_check(tmp)
        got = set()
        for line in quiet.text.splitlines():
            m = re.match(r"problem: ico2/common/src/check_contexts\.c:(\d+): ", line)
            if m:
                got.add(int(m.group(1)))
            elif "check_contexts.c" in line:
                failures.append("check: unexpected problem: %s" % line)
        want = set(n for n, line in enumerate(read(ctx).split(b"\n"), 1) if line.rstrip().endswith(b"/* refused */"))
        if got != want:
            failures.append("check on check_contexts.c: refused lines %s, want %s" % (sorted(got), sorted(want)))
        os.remove(ctx)

        # check: LN's arity in region.h, and the ico_discs[] row of every slug
        hdr = os.path.join(tmp, HEADER)
        kept = read(hdr)
        with open(hdr, "wb") as fh:
            fh.write(gen_header(rows[:2]))
        discs = os.path.join(tmp, DISCS_C)
        os.makedirs(os.path.dirname(discs))
        with open(discs, "wb") as fh:
            fh.write(b'const IcoDiscInfo ico_discs[ICO_REGION_COUNT] = {\n'
                     b'    [ICO_REGION_PAL] = {"SCES-50760", "pal", "SCES_507.60"},\n'
                     b'    [ICO_REGION_JPN] = {"SCPS-11003", "jpn", "SCPS_110.03"},\n'
                     b'    [ICO_REGION_KOR] = {"SCPS-56001", "jpn", "SCPS_560.01"},\n};\n')
        with quiet:
            cmd_check(tmp)
        for want in ("LN takes 2 argument(s), config/regions.txt has 4 rows",
                     'no [ICO_REGION_KOR] entry naming "kor"', 'no [ICO_REGION_US] entry naming "us"'):
            if want not in quiet.text:
                failures.append("check: no problem %r" % want)
        if 'entry naming "jpn"' in quiet.text or 'entry naming "pal"' in quiet.text:
            failures.append("check: a present ico_discs[] row reported missing")
        with open(hdr, "wb") as fh:
            fh.write(kept)
        shutil.rmtree(os.path.join(tmp, "port"))

        # the parser's tripwires, and the forms it reads
        for name, text in (("unknown macro", b"#ifdef VERSION_EU\nint a;\n#endif\n"),
                           ("nesting two deep", b"#ifdef VERSION_JPN\n#ifdef VERSION_US\n#ifdef VERSION_KOR\n"
                            b"#endif\n#endif\n#endif\n"),
                           ("a bare macro", b"#if VERSION_JPN\nint a;\n#endif\n"),
                           ("#elif form", b"#ifdef VERSION_JPN\nint a;\n#elif VERSION_US\nint b;\n#endif\n"),
                           ("another macro", b"#if X && defined(VERSION_JPN)\nint a;\n#endif\n"),
                           ("a taken row", b"#ifdef VERSION_JPN\nint a;\n#elif defined(VERSION_JPN) || "
                            b"defined(VERSION_KOR)\nint b;\n#endif\n"),
                           ("#elif after #else", b"#ifdef VERSION_JPN\nint a;\n#else\nint b;\n"
                            b"#elif defined(VERSION_KOR)\n#endif\n")):
            try:
                SourceFile("t.c", text, load_regions(tmp))
                failures.append("parser accepted %s" % name)
            except RegionError:
                pass
        for name, text, want in (
                ("#if defined", b"#if defined(VERSION_JPN)\nint a;\n#endif\n", [("jpn",)]),
                ("defined without parentheses", b"#if defined VERSION_KOR\nint a;\n#endif\n", [("kor",)]),
                ("a negated group", b"#if !(defined(VERSION_JPN) || defined(VERSION_KOR))\nint a;\n#endif\n",
                 [("jpn", "kor")]),
                ("one level of nesting", b"#ifdef VERSION_JPN\n#ifdef VERSION_US\nint a;\n#endif\n#endif\n",
                 []),
                ("&&", b"#if defined(VERSION_JPN) || defined(VERSION_KOR) && !defined(VERSION_US)\nint a;\n"
                 b"#endif\n", [("jpn", "kor")])):
            try:
                got = [a.key for b in SourceFile("t.c", text, load_regions(tmp)).blocks for a in b.named]
                if got != want:
                    failures.append("parser: %s: named arms %s, want %s" % (name, got, want))
            except RegionError as e:
                failures.append("parser refused %s: %s" % (name, e))

        # rename: the two-row spelling for the three-row table (pal, jpn, kor)
        src = read(os.path.join(FIXTURE_DIR, "rename.c"))
        got, _ = rename_bytes(src, rows[:3], shared_rows(rows[:3]))
        want_bytes = read(os.path.join(FIXTURE_DIR, "rename_expected.c"))
        if got != want_bytes:
            failures.append("rename differs from rename_expected.c:\n" +
                            diff_text(want_bytes.decode("latin-1"), got.decode("latin-1")))
        if rename_bytes(got, rows[:3], shared_rows(rows[:3]))[0] != got:
            failures.append("rename is not idempotent")
        if rename_bytes(src, rows[:2], shared_rows(rows[:2]))[0] != src:
            failures.append("rename changed a file for the two-row table")

        # the emitted forms are C
        gcc = shutil.which("gcc") or shutil.which("cc")
        if gcc:
            for extra in ([], ["-DVERSION_JPN"], ["-DVERSION_KOR"]):
                cmd = [gcc, "-fsyntax-only", "-std=gnu99", "-Werror=implicit-function-declaration",
                       "-I", os.path.join(tmp, "ico2", "common", "include")] + extra + [target]
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if proc.returncode != 0:
                    failures.append("%s failed:\n%s" % (" ".join(cmd), proc.stdout.decode("latin-1")))
        else:
            print("region_blocks: selftest: no C compiler, the -fsyntax-only step is skipped")
    finally:
        shutil.rmtree(tmp)
    for f in failures:
        print("selftest: %s" % f)
    if failures:
        print("region_blocks: selftest FAILED (%d)" % len(failures))
        return 1
    print("region_blocks: selftest passed")
    return 0


class Quiet:
    """Capture stdout while a command runs."""

    def __enter__(self):
        import io
        self.saved = sys.stdout
        self.buf = io.StringIO()
        sys.stdout = self.buf
        return self

    def __exit__(self, *exc):
        sys.stdout = self.saved
        self.text = self.buf.getvalue()
        return False


def diff_text(want, got):
    import difflib
    return "".join(difflib.unified_diff(want.splitlines(True), got.splitlines(True), "expected", "got"))


# --------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--selftest", action="store_true", help="run the fixture test")
    sub = ap.add_subparsers(dest="cmd")
    p = sub.add_parser("convert", help="rewrite the region blocks")
    p.add_argument("--root", default=ROOT)
    p.add_argument("paths", nargs="*")
    p = sub.add_parser("report", help="print the hand list")
    p.add_argument("--root", default=ROOT)
    p.add_argument("--all", action="store_true", help="also one `auto` line per converted block")
    p.add_argument("--bits", action="store_true", help="candidate rows for config/motion_bits.<slug>.txt")
    p = sub.add_parser("check", help="verify the region rules")
    p.add_argument("--root", default=ROOT)
    p = sub.add_parser("rename", help="rewrite the two-row spelling (PAL_JPN, ICO_JPN, LN(a, b)) for the rows "
                       "that share the Japanese arm",
                       description="Rewrite, under ico2/ and sce/ only, the two-row spelling for the set of "
                       "rows that share the Japanese arm: PAL_JPN( -> PAL_<SET>(, the identifier ICO_JPN -> "
                       "ICO_<SET>, LN(a, b) -> LN(a, b, b) (one value per row of the table). The set is every "
                       "named row of config/regions.txt (the assumption: each target after the Japanese one "
                       "is a build of the Japanese program, as the Korean one is) unless --set names it. "
                       "Tokens only: comments and strings stay; idempotent. region.h is left to gen-header.")
    p.add_argument("--root", default=ROOT)
    p.add_argument("--check", action="store_true", help="report what would change, write nothing (exit 1 if any)")
    p.add_argument("--set", dest="set_spec", help="the rows sharing the Japanese arm, comma-separated (jpn,kor)")
    p = sub.add_parser("gen-table", help="write %s" % REGION_TABLE)
    p.add_argument("--root", default=ROOT)
    p = sub.add_parser("gen-header", help="write %s" % HEADER)
    p.add_argument("--root", default=ROOT)
    args = ap.parse_args()
    try:
        if args.selftest:
            return selftest()
        if args.cmd == "convert":
            return cmd_convert(os.path.abspath(args.root), args.paths)
        if args.cmd == "report":
            return cmd_report(os.path.abspath(args.root), args.all, args.bits)
        if args.cmd == "check":
            return cmd_check(os.path.abspath(args.root))
        if args.cmd == "rename":
            return cmd_rename(os.path.abspath(args.root), args.check, args.set_spec)
        if args.cmd == "gen-table":
            return cmd_gen_table(os.path.abspath(args.root))
        if args.cmd == "gen-header":
            return cmd_gen_header(os.path.abspath(args.root))
    except RegionError as e:
        print("error: %s" % e)
        return 1
    ap.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
