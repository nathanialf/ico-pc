#!/usr/bin/env python3
"""tools/template_audit.py: find whole-record copies between two record types.

The decompiled game copies a file-local template record over a game record
of another declared type (`*(MotionStateInfo *)self = motionStateInfoTemplate`
over a `struct MotCtrl`, `*(MotionGeoInfo *)self = motionGeoInfoTemplate` over
a `struct MotRoot`). On the EE the two layouts coincide; on a 64-bit host they
differ as soon as one side has a pointer where the other has an `int`, a
`Vec4` (8-byte aligned through its `long long` view) where the other has a
`float[4]`, or other padding. The offset audit (tools/offset_audit.py) checks
member accesses, not whole copies, so it cannot see this class.

Method (the unit handling is offset_audit's: compile_commands.json, the host
flags, gcc's diagnostics for the operand types):

1. In every function body of an ico2/ source, a whole-record view is
     - `*(T *)E` or `((T *)E)[k]` used as a value or assigned to (not under
       `&`, `sizeof` or a member access), T a struct or union;
     - `*p` or `p[k]` used whole, p a local assigned `(T *)E` in the function;
     - a `memcpy`/`memmove`/`bcopy` whose destination and source (casts to
       `void *`/`char *` removed) point to records;
     - `*(T *)a = *(T *)b`: also the pair (b's storage over a's).
2. The compiler gives E's type. The storage U is E's pointee, or, for
   `&x.m` when the pointee is not a record or is smaller than T, the record
   whose member m is (`&sub->root.wall` is MotRoot from wall). U compatible
   with T is SAME; U not a record (`char *`, an `int` array), or E pointer
   arithmetic (a buffer carved up), is RAW (listed with --all).
3. Every other site needs registered layout assertions in its unit: a
   `_Static_assert` naming `sizeof(T)` and U, and for every named member m
   of T (pad<HEX>/unk<HEX> members excepted) one naming `offsetof(T, m)` and
   U (ee_view.h's ICO_LAYOUT_AT, ICO_LAYOUT_AT_FROM, ICO_LAYOUT_SIZE). When T
   or U is a block mover (a union, one member, 64-bit words only: ICO_QW,
   Blob64) the size is enough. A site without them is UNREGISTERED.
4. The audit compiles the host layouts itself: the offsets of the members T
   and U share by name and the sizes (T past U's end), the room after U's
   member m for T, and T's alignment against the storage's (a 16-byte
   aligned T, which the host compiler copies with aligned SSE moves, over a
   variable or type with less). A difference is a MISMATCH (also when
   registered; a registered layout difference does not compile).

5. Views through `void *` (the mail data, `void *` members): the writers'
   storage and the readers' record types are paired across the units and
   each pair's host layout is checked against the EE's (the section
   "views through `void *`" below).

MISMATCH, UNREGISTERED, UNTYPED, UNPAIRED and STALE fail the run (exit 1).

Usage:
    tools/template_audit.py --build build-host/<preset>   # report, exit 1 on findings
    tools/template_audit.py --build DIR --all             # every site (VIEW, RAW, SAME)
    tools/template_audit.py --build DIR FILE.c...          # only these units
"""

import argparse
import concurrent.futures
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import offset_audit as oa  # noqa: E402

COPY_FUNCS = {"memcpy": (0, 1), "memmove": (0, 1), "bcopy": (1, 0), "__builtin_memcpy": (0, 1),
              "__builtin_memmove": (0, 1)}
QUALS = {"const", "volatile", "restrict", "__restrict", "__restrict__", "struct", "union", "enum",
         "signed", "unsigned", "static", "extern", "register", "__extension__"}
PAD_RE = re.compile(r"^_?(pad|unk)[0-9A-Fa-f]*$")


class Site:
    def __init__(self, file, line, kind, snippet):
        self.file, self.line, self.kind, self.snippet = file, line, kind, snippet
        self.view = None  # T: the record type the copy uses
        self.store = None  # U: the record type of the storage
        self.verdict = None
        self.detail = ""

    def key(self):
        return (self.file, self.line, self.snippet)


def norm_type(s):
    s = oa.strip_cv(s or "")
    return re.sub(r"\s*\*", " *", s).strip()


def bare(s):
    """'struct MotCtrl' -> 'MotCtrl' (the spelling static asserts may use)."""
    return re.sub(r"^(struct|union)\s+", "", norm_type(s))


# --- the members of a record, from the unit's own definition --------------------
def strip_attrs(u, a, b):
    """Token indices in [a, b] with __attribute__((...)) and __asm__(...) left out."""
    toks = u.toks
    out = []
    k = a
    while k <= b:
        s = toks[k].s
        if s in ("__attribute__", "__attribute", "__asm__", "asm", "__asm") and k + 1 <= b and \
                toks[k + 1].s == "(":
            k = u.match.get(k + 1, b) + 1
            continue
        out.append(k)
        k += 1
    return out


def declarator_name(u, idx):
    """The declared name among token indices idx (one declarator)."""
    toks = u.toks
    for j in range(len(idx) - 2):
        if toks[idx[j]].s == "(" and toks[idx[j + 1]].s == "*" and toks[idx[j + 2]].k == "id":
            return toks[idx[j + 2]].s
    name = None
    for k in idx:
        s = toks[k].s
        if s in ("[", ":"):
            break
        if toks[k].k == "id" and s not in QUALS:
            name = s
    return name


def body_members(u, a, b):
    """Named members of the record body between braces a and b (exclusive);
    an anonymous struct or union member contributes its own members."""
    toks = u.toks
    out = []
    k = a + 1
    while k < b:
        st = k
        nested = None
        while k < b and toks[k].s != ";":
            if toks[k].s == "{":
                nested = (k, u.match[k])
                k = u.match[k]
            elif toks[k].s in ("(", "["):
                k = u.match.get(k, k)
            k += 1
        end = k - 1
        k += 1
        if nested:
            tail = [j for j in strip_attrs(u, nested[1] + 1, end)]
            if not tail:
                out.extend(body_members(u, nested[0], nested[1]))
                continue

            decls = split_commas(u, tail)
        else:
            decls = split_commas(u, strip_attrs(u, st, end))
        words = None
        for d in decls:
            n = declarator_name(u, d)
            if words is None:  # the base type: the first declarator's words but its name
                words = " ".join(toks[j].s for j in d if toks[j].k == "id" and toks[j].s != n)
                if nested:
                    words = "{...}"
            if n and not (len(d) >= 2 and u.toks[d[-2]].s == ":" and n == u.toks[d[0]].s):
                out.append((n, words))
    return out


def split_commas(u, idx):
    toks = u.toks
    out = [[]]
    depth = 0
    for k in idx:
        s = toks[k].s
        if s in "([{":
            depth += 1
        elif s in ")]}":
            depth -= 1
        if s == "," and depth == 0:
            out.append([])
            continue
        out[-1].append(k)
    return [d for d in out if d]


def find_record_body(u, typ):
    """(open, close) brace indices of the definition of typ ('struct X' or a
    typedef name), or None."""
    toks = u.toks
    typ = norm_type(typ)
    seen = set()
    while True:
        if typ in seen:
            return None
        seen.add(typ)
        m = re.match(r"^(struct|union)\s+(\w+)$", typ)
        if m:
            kw, tag = m.groups()
            for i in range(len(toks) - 2):
                if toks[i].s == kw and toks[i + 1].s == tag and toks[i + 2].s == "{":
                    return i + 2, u.match[i + 2], kw
            return None
        if not re.match(r"^\w+$", typ):
            return None
        # typedef ... { body } typ ;   or   typedef struct X typ ;
        nxt = None
        for i in range(1, len(toks) - 1):
            if toks[i].s != typ or toks[i + 1].s not in (";", ",", "__attribute__"):
                continue
            j = i - 1
            while toks[j].s == ")" and toks[u.match.get(j, j) - 1].s in ("__attribute__",
                                                                         "__attribute"):
                j = u.match[j] - 2
            if toks[j].s == "}":
                o = u.match[j]
                # walk back to the typedef keyword
                k = o - 1
                while k >= 0 and toks[k].s not in ("typedef", ";", "}", "{"):
                    k -= 1
                if k >= 0 and toks[k].s == "typedef":
                    kw = "union" if any(toks[x].s == "union" for x in range(k, o)) else "struct"
                    return o, j, kw
            elif toks[j].k == "id" and toks[j - 1].s in ("struct", "union") and \
                    toks[j - 2].s == "typedef":
                nxt = "%s %s" % (toks[j - 1].s, toks[j].s)
            elif toks[j].k == "id" and toks[j - 1].s == "typedef":
                nxt = toks[j].s
        if nxt is None:
            return None
        typ = nxt


MOVER_WORDS = {"long long", "long long int", "unsigned long long", "long long unsigned int",
               "unsigned long long int", "u_long", "u_long128", "ICO_QW", "ICO_UQW"}


def record_members(u, typ, cache):
    """(kind, [(member, base type words)]) of the record typ, or None."""
    if typ in cache:
        return cache[typ]
    rb = find_record_body(u, typ)
    cache[typ] = (rb[2], body_members(u, rb[0], rb[1])) if rb else None
    return cache[typ]


def is_mover(rec):
    """A union, a record of one member, or one of 64-bit words only: a block
    the code moves by its size (DObjBlk40, ICO_QW, Blob64); its registration
    is the size."""
    kind, mem = rec
    named = [m for m in mem if not PAD_RE.match(m[0])]
    return kind == "union" or len(named) <= 1 or all(w in MOVER_WORDS for _, w in named)


# --- registered assertions ------------------------------------------------------


def static_asserts(u):
    """The conditions of every _Static_assert in the unit, with blanks and the
    struct/union keywords removed."""
    out = []
    toks = u.toks
    for i, t in enumerate(toks):
        if t.s == "_Static_assert" and i + 1 < len(toks) and toks[i + 1].s == "(":
            c = u.match.get(i + 1)
            if c is None:
                continue
            txt = "".join(tk.s for tk in toks[i + 2:c] if tk.s not in ("struct", "union"))
            out.append(txt)
    return out


def registration(asserts, T, U, members):
    """(size registered, [members of T without an offset assertion]): the
    size assertion names sizeof(T) and U, each member's names offsetof(T, m)
    and U (a view of U from one of its members subtracts that member's
    offset). A record of one member needs only the size."""
    t, uu = re.escape(bare(T)), re.escape(bare(U))
    mine = [a for a in asserts if re.search(r"\b%s\b" % uu, a)]
    size = any(re.search(r"sizeof\(%s\)" % t, a) for a in mine)
    missing = []
    if len(members) > 1:
        for m in members:
            pat = re.compile(r"offsetof\(%s,%s\)" % (t, re.escape(m)))
            if not any(pat.search(a) for a in mine):
                missing.append(m)
    return size, missing


# --- sites ------------------------------------------------------------------------
def expr_end(u, i, fb):
    """End index of the assignment-free operand starting at i (a cast expression)."""
    e = u.unary_end(i)
    return e if e is not None and e < fb else None


def is_unary_ctx(u, k):
    """Is the token at k (a `*`) a prefix operator?"""
    p = u.toks[k - 1]
    if p.k in ("id", "num", "str"):
        return p.s in ("return", "case", "sizeof")
    return p.s not in (")", "]", "++", "--")


def whole_use(u, a, b):
    """Is the expression [a, b] used as a whole value (not &, sizeof, a member)?"""
    toks = u.toks
    # widen over redundant parentheses
    while toks[a - 1].s == "(" and u.match.get(a - 1) == b + 1 and \
            not (toks[a - 2].k == "id" and toks[a - 2].s not in ("return", "sizeof")):
        a -= 1
        b += 1
    if toks[a - 1].s in ("&", "sizeof") or toks[a - 1].s == "(" and toks[a - 2].s == "sizeof":
        return False
    if toks[b + 1].s in (".", "->", "["):
        return False
    return True


def find_sites(u):
    toks = u.toks
    found = []  # (site, kind, cast type, [operand spans])

    def mk(kind, i0, a, b):
        t = toks[i0]
        f = os.path.normpath(t.f) if os.path.isabs(t.f) else t.f
        s = Site(f, t.ln, kind, re.sub(r"\s+", " ", u.text_of(a, b))[:200])
        s.span = (a, b)
        return s

    for fa, fb in u.funcs:
        views = None
        for i in range(fa + 1, fb):
            t = toks[i]
            if not oa.in_game(t):
                continue
            # *(T *)E
            if t.s == "*" and t.k == "punct" and is_unary_ctx(u, i) and toks[i + 1].s == "(":
                c = u.cast_at(i + 1)
                if c and c[0].endswith("*") and not c[0].endswith("* *"):
                    e = expr_end(u, c[1] + 1, fb)
                    if e is not None and whole_use(u, i, e):
                        found.append((mk("deref", i, i, e), c[0][:-1].strip(), (c[1] + 1, e)))
                continue
            # ((T *)E)[k]
            if t.s == "(" and toks[i + 1].s == "(":
                c = u.cast_at(i + 1)
                if c and c[0].endswith("*") and not c[0].endswith("* *"):
                    e = expr_end(u, c[1] + 1, fb)
                    cl = u.match.get(i)
                    if e is not None and cl == e + 1 and toks[cl + 1].s == "[":
                        rb = u.match[cl + 1]
                        if whole_use(u, i, rb):
                            found.append((mk("index", i, i, rb), c[0][:-1].strip(), (c[1] + 1, e)))
                continue
            # *p, p[k] with p a local view `p = (T *)E`
            if t.k == "id" and toks[i - 1].s not in (".", "->"):
                if views is None:
                    views = {}
                    vv, _ = oa.scan_func_locals(u, fa, fb)
                    for name, spans in vv.items():
                        for a, b in spans:
                            c = u.cast_at(a)
                            if c and c[0].endswith("*") and not c[0].endswith("* *") and \
                                    u.unary_end(c[1] + 1) == b:
                                views.setdefault(name, []).append((c[0][:-1].strip(), (c[1] + 1, b)))
                if t.s in views:
                    if toks[i - 1].s == "*" and is_unary_ctx(u, i - 1) and whole_use(u, i - 1, i):
                        for ct, sp in views[t.s]:
                            found.append((mk("lview", i, i - 1, i), ct, sp))
                    elif toks[i + 1].s == "[" and whole_use(u, i, u.match[i + 1]):
                        for ct, sp in views[t.s]:
                            found.append((mk("lview", i, i, u.match[i + 1]), ct, sp))
                if t.s in COPY_FUNCS and toks[i + 1].s == "(":
                    cl = u.match[i + 1]
                    args = []
                    st = i + 2
                    depth = 0
                    for k in range(i + 2, cl):
                        s = toks[k].s
                        if s in "([{":
                            depth += 1
                        elif s in ")]}":
                            depth -= 1
                        elif s == "," and depth == 0:
                            args.append((st, k - 1))
                            st = k + 1
                    args.append((st, cl - 1))
                    if len(args) != 3:
                        continue
                    di, si = COPY_FUNCS[t.s]
                    spans = []
                    for a, b in (args[di], args[si]):
                        a, b = oa.strip_parens(u, a, b)
                        while True:  # (void *)x, (char *)x: the copy does not care
                            c = u.cast_at(a)
                            if c and u.unary_end(c[1] + 1) == b:
                                a, b = oa.strip_parens(u, c[1] + 1, b)
                                continue
                            break
                        spans.append((a, b))
                    found.append((mk("memcpy", i, i, cl), None, tuple(spans)))
    return found


def member_base(u, a, b):
    """`&base.path` / `&base->path` (casts and parentheses around it removed):
    ((base span), path, op), else None."""
    while True:
        a, b = oa.strip_parens(u, a, b)
        c = u.cast_at(a)
        if c and u.unary_end(c[1] + 1) == b:
            a = c[1] + 1
            continue
        break
    if u.toks[a].s != "&":
        return None
    return oa.field_split(u, a + 1, b)


def carved(u, a, b):
    """Is [a, b] (casts and parentheses removed) pointer arithmetic: storage
    carved from a buffer (`(PinRec *)(groups + n)`), not a record's own?"""
    while True:
        a, b = oa.strip_parens(u, a, b)
        c = u.cast_at(a)
        if c and u.unary_end(c[1] + 1) == b:
            a = c[1] + 1
            continue
        break
    depth = 0
    for k in range(a, b + 1):
        t = u.toks[k].s
        if t in "([{":
            depth += 1
        elif t in ")]}":
            depth -= 1
        elif depth == 0 and k > a and t in ("+", "-") and u.toks[k - 1].s not in (
                "(", ",", "=", "+", "-", "*", "/"):
            return True
    return False


def plain_var(u, a, b):
    """`&name` (casts and parentheses removed): the name, else None."""
    while True:
        a, b = oa.strip_parens(u, a, b)
        c = u.cast_at(a)
        if c and u.unary_end(c[1] + 1) == b:
            a = c[1] + 1
            continue
        break
    if b == a + 1 and u.toks[a].s == "&" and u.toks[b].k == "id":
        return u.toks[b].s
    return None


def probe_exprs(u, exprs, workdir):
    """{key: type string} of expressions compiled at the end of the unit."""
    if not exprs:
        return {}
    lines = ["", '# 1 "__ico_template_audit__"']
    for k, e in exprs.items():
        lines.append("void __icoE_%d(void) { struct __icoA_%d *__icoa = (%s); (void)__icoa; }" % (
            k, k, e))
    path = os.path.join(workdir, "%s.%d.types.i" % (os.path.basename(u.src), os.getpid()))
    with open(path + ".tmp", "w", encoding="latin-1") as f:
        f.write(u.text + "\n".join(lines) + "\n")
    os.replace(path + ".tmp", path)
    r = oa.run([u.flags["cc"], "-x", "cpp-output", "-fsyntax-only", "-fdiagnostics-plain-output",
                "-fmax-errors=0", "-Wno-error"] + u.flags["opts"] + [path])
    out = {}
    for line in r.stderr.splitlines():
        m = oa.DIAG_RE.search(line)
        if m:
            out.setdefault(int(m.group(1)), m.group(2))
    return out


def audit_unit(src, fl, workdir):
    text = oa.preprocess(src, fl, audit=False)
    u = oa.Unit(src, text, fl)
    events = channel_events(u, workdir)
    return audit_copies(u, workdir), events


def audit_copies(u, workdir):
    found = find_sites(u)
    if not found:
        return []
    toks = u.toks
    spans = []
    bases = {}
    for s, ct, sp in found:
        if s.kind == "memcpy":
            spans.extend(sp)
        else:
            spans.append(sp)
            mb = member_base(u, *sp)
            if mb:
                bases[sp] = mb
                spans.append(mb[0])
    types = oa.probe_types(u, spans, workdir)

    def pointee(t):
        p = oa.ptr_target(t)
        return norm_type(p) if p else None

    # the record each view's storage is a member of: the innermost record of
    # the member path (`root.wall` of a Sub15C is MotRoot's wall)
    inner = {}
    for s, ct, sp in found:
        s.base = None
        s.carved = s.kind != "memcpy" and carved(u, *sp)
        s.var = None if s.kind == "memcpy" else plain_var(u, *sp)
        if s.kind == "memcpy":
            s.store, s.view = pointee(types[sp[0]]), pointee(types[sp[1]])
            continue
        s.view, s.store = norm_type(ct), pointee(types[sp])
        mb = bases.get(sp)
        if mb:
            r = norm_type(types[mb[0]]) if mb[2] == "." else pointee(types[mb[0]])
            if r:
                path = mb[1]
                if "." in path:
                    pre, last = path.rsplit(".", 1)
                    inner.setdefault((r, pre), len(inner))
                    s.base = (r, path, pre, last)
                else:
                    s.base = (r, path, None, path)
    itypes = probe_exprs(u, {k: "&((%s *)0)->%s" % rp for rp, k in inner.items()}, workdir)
    for s, ct, sp in found:
        if s.base and s.base[2] is not None:
            r = pointee(itypes.get(inner[(s.base[0], s.base[2])], ""))
            s.base = (r, s.base[3]) if r else None
        elif s.base:
            s.base = (s.base[0], s.base[3])
    # pairs: `*(T *)a = *(T *)b`, a template moved between two other records
    by_start = {s.span[0]: (s, ct, sp) for s, ct, sp in found if s.kind != "memcpy"}
    pairs = []
    for s, ct, sp in found:
        if s.kind == "memcpy" or toks[s.span[1] + 1].s != "=":
            continue
        o = by_start.get(s.span[1] + 2)
        if o and toks[o[0].span[1] + 1].s in (";", ",", ")", "="):
            pairs.append((s, o[0]))
    names = set()
    for s, ct, sp in found:
        for x in (s.view, s.store, s.base[0] if s.base else None):
            if x:
                names.add(x)
    names = sorted(names)
    ex = {}
    for k, n in enumerate(names):
        ex[3 * k] = "__builtin_classify_type(*(%s *)0)" % n
        ex[3 * k + 1] = "sizeof(%s)" % n
        ex[3 * k + 2] = "__alignof__(%s)" % n
    v = oa.probe_values(u, ex, workdir)
    isrec = {n: v.get(3 * k) in (12, 13) for k, n in enumerate(names)}
    size = {n: v.get(3 * k + 1) for k, n in enumerate(names)}
    align = {n: v.get(3 * k + 2) for k, n in enumerate(names)}
    want = set()
    for s, ct, sp in found:
        if s.view and s.store and isrec.get(s.view) and isrec.get(s.store):
            want.add((s.view, s.store))
    for d, r in pairs:
        if d.store and r.store and isrec.get(d.store) and isrec.get(r.store):
            want.add((r.store, d.store))
    want = sorted(want)
    compat = oa.probe_values(u, {k: "__builtin_types_compatible_p(%s, %s)" % p
                                 for k, p in enumerate(want)}, workdir)
    same = {p: compat.get(k) == 1 for k, p in enumerate(want)}
    varal = oa.probe_values(u, {k: "__alignof__(%s)" % n for k, n in enumerate(
        sorted({s.var for s, _, _ in found if s.var}))}, workdir)
    varal = {n: varal.get(k) for k, n in enumerate(sorted({s.var for s, _, _ in found if s.var}))}
    asserts = static_asserts(u)
    cache = {}
    layout = {}
    out = []

    def check(s, T, U, path):
        """The verdict of T copied over U (from member path, or whole)."""
        where = "%s from .%s" % (U, path) if path else U
        rec = record_members(u, T, cache)
        if rec is None:
            return "UNREGISTERED", "%s over %s: no definition of %s in the unit" % (T, where, T)
        mem = [m for m, _ in rec[1] if not PAD_RE.match(m)]
        urec = record_members(u, U, cache)
        mover = is_mover(rec) or (urec is not None and is_mover(urec))
        key = (T, U, path)
        if key not in layout:
            bad = []
            if path:
                e = {0: "sizeof(%s)" % T,
                     1: "sizeof(%s) - __builtin_offsetof(%s, %s)" % (U, U, path),
                     2: "__builtin_offsetof(%s, %s) %% __alignof__(%s)" % (U, path, T)}
                r = oa.probe_values(u, e, workdir)
                if r.get(0) is None or r.get(1) is None or r[0] > r[1]:
                    bad.append("size %s past the end (%s left)" % (r.get(0), r.get(1)))
                if (align.get(T) or 1) >= 16 and r.get(2):
                    bad.append("%s is not at a multiple of its %d-byte alignment" % (path, align[T]))
            else:
                umem = {m for m, _ in urec[1]} if urec else set()
                both = [] if mover else [m for m in mem if m in umem]
                e = {0: "sizeof(%s)" % T, 1: "sizeof(%s)" % U}
                for m in both:
                    e[len(e)] = "__builtin_offsetof(%s, %s)" % (T, m)
                    e[len(e)] = "__builtin_offsetof(%s, %s)" % (U, m)
                r = oa.probe_values(u, e, workdir)
                if (r.get(0) or 0) > (r.get(1) or 0):
                    bad.append("size %s/%s" % (r.get(0), r.get(1)))
                for k, m in enumerate(both):
                    if r.get(2 + 2 * k) != r.get(3 + 2 * k):
                        bad.append("%s %s/%s" % (m, r.get(2 + 2 * k), r.get(3 + 2 * k)))
            layout[key] = bad
        bad = list(layout[key])
        sal = varal.get(s.var) if s.var else None
        if sal is None:
            sal = align.get(U) or 1
        if (align.get(T) or 1) >= 16 and sal < align[T] and not path and s.kind != "memcpy":
            bad.append("%s is %d-byte aligned, %s's copy needs %d" % (s.var or U, sal, T, align[T]))
        sized, missing = registration(asserts, T, U, [] if mover else mem)
        if bad:
            return "MISMATCH", "%s over %s (host view/storage): %s" % (T, where, ", ".join(bad[:12]))
        if not sized or missing:
            return "UNREGISTERED", "%s over %s:%s%s" % (
                T, where, "" if sized else " no sizeof assertion;",
                (" no offsetof assertion for %d of %d members (%s)" % (
                    len(missing), len(mem), ", ".join(missing[:8]))) if missing else "")
        return "OK", "%s over %s: %s" % (T, where, "the size asserted" if mover else
                                         "%d members and the size asserted" % len(mem))

    for s, ct, sp in found:
        if not (s.view and isrec.get(s.view)) and (s.kind != "memcpy" or not (
                s.store and isrec.get(s.store))):
            continue  # not a record copy: a word or a vector over anything (offset_audit's)
        T = s.view
        if s.view and s.store and isrec.get(s.store) and same.get((s.view, s.store)):
            s.verdict, s.detail = "SAME", T
            out.append(s)
            continue
        if s.carved:
            s.verdict, s.detail = "RAW", "%s over %s (an address computed into a buffer)" % (
                T, s.store or "?")
            out.append(s)
            continue
        U, path = s.store, ""
        if s.base and isrec.get(s.base[0]) and not (
                isrec.get(s.store) and (size.get(T) or 0) <= (size.get(s.store) or 0)):
            U, path = s.base
        if not (T and U and isrec.get(T) and isrec.get(U)):
            s.verdict, s.detail = "RAW", "%s over %s" % (T or "?", s.store or "?")
            if s.base and isrec.get(s.base[0]) and (align.get(T) or 1) >= 16:
                # a quadword record over a member that is not one: the member
                # must sit at the record's alignment
                R, mp = s.base
                key = ("align", T, R, mp)
                if key not in layout:
                    r = oa.probe_values(u, {0: "__builtin_offsetof(%s, %s) %% %d" % (R, mp, align[T]),
                                            1: "__alignof__(%s)" % R}, workdir)
                    layout[key] = r.get(0) not in (0, None) or (r.get(1) or 0) < align[T]
                if layout[key]:
                    s.verdict = "MISMATCH"
                    s.detail = "%s over %s.%s: the member is not %d-byte aligned on the host" % (
                        T, R, mp, align[T])
            out.append(s)
            continue
        s.verdict, s.detail = check(s, T, U, path)
        out.append(s)
    for d, r in pairs:
        T, U = r.store, d.store
        if not (T and U and isrec.get(T) and isrec.get(U)) or same.get((T, U)) or \
                d.carved or r.carved:
            continue
        p = Site(d.file, d.line, "pair", "%s = %s" % (d.snippet, r.snippet))
        p.var = d.var
        p.verdict, p.detail = check(p, T, U, "")
        p.detail = "through %s: %s" % (d.view, p.detail)
        out.append(p)
    return out


# --- views through `void *`: the mail data and `void *` members --------------------
# A `void *` carries no record type, so a record written through it and read
# back as another record type is invisible to the copy checks above: the climb
# mail hands `&enemy->climbOrient` to ActSendMail_WithAdditionalData and
# actCommonRopeClimbEnd1 reads `Act.intrData` whole as a ClimbEndRec. Each such `void *` is a channel:
#   - the mail data: the data argument of a sender (MAIL_SENDERS) is written,
#     a cast of `Act.intrData` or of a getter's result (MAIL_GETTERS) reads;
#   - a `void *` member R.m: `x->m = E` writes, `(T *)x->m` reads.
# A writer's storage is E's static type: a record (`&rec`, a `T *`), or a
# record from a member (`&x->f`, an array member `x->f`: R from f); a `void *`
# or `char *` local is followed to its assignments, and a `void *` that the
# function casts to one record type elsewhere (`(MotOriTarget *)s->addData`)
# has that type. Writers of scalar data (`float *`, `void **`) are not
# records and are left out.
# Pairs: a mail reader under `case N:` reads mail N's writers; any other
# reader, and every reader of a member channel, pairs with the writers whose
# record T's EE layout matches from the writer's offset (each member of T at
# the same EE offset and size: the view the EE code relied on). Each pair is
# checked on the host: every member of T at the offset of the writer's
# matching field (less the writer's member offset), T within the storage,
# and a 16-byte aligned T on a 16-byte boundary. A mail reader with no
# matching writer, and a mail writer or reader whose type is unknown, is a
# finding unless tools/template_audit_allow.txt lists it with a reason.

MAIL_SENDERS = {"ActSendMail_WithAdditionalData": (1, 3)}  # name: (mail argument, data argument)
MAIL_GETTERS = {"GetMailAdditionalData"}
MAIL_FIELDS = {("Act", "intrData")}
ALLOW_PATH = os.path.join(ROOT, "tools", "template_audit_allow.txt")


class ChanEvent:
    def __init__(self, role, file, line, snippet, src):
        self.role, self.file, self.line, self.snippet, self.src = role, file, line, snippet, src
        self.chan = None  # "mail" or ("member", R, m)
        self.T = None  # reader: the record read
        self.U = None  # writer: (record, member path) of the storage
        self.mail = None
        self.untyped = None  # why the type is not known


def call_args(u, i):
    """Argument spans of the call whose name is at token i."""
    toks = u.toks
    cl = u.match.get(i + 1)
    if cl is None:
        return []
    args = []
    st = i + 2
    depth = 0
    for k in range(i + 2, cl):
        s = toks[k].s
        if s in "([{":
            depth += 1
        elif s in ")]}":
            depth -= 1
        elif s == "," and depth == 0:
            args.append((st, k - 1))
            st = k + 1
    if st <= cl - 1:
        args.append((st, cl - 1))
    return args


def strip_byte_casts(u, a, b):
    """[a, b] without parentheses and casts to `void *` / `char *`."""
    while True:
        a, b = oa.strip_parens(u, a, b)
        c = u.cast_at(a)
        if c and c[0].endswith("*") and not c[0].endswith("* *") and \
                oa.strip_cv(c[0][:-1]) in oa.CHARLIKE and u.unary_end(c[1] + 1) == b:
            a = c[1] + 1
            continue
        return a, b


def span_text(u, a, b):
    return "".join(t.s for t in u.toks[a:b + 1])


def is_record_name(t):
    t = oa.strip_cv(t or "")
    return bool(t) and t not in oa.CHARLIKE and t not in oa.SCALAR and "*" not in t and \
        "(" not in t and "[" not in t and t not in oa.INTCAST


def case_label(u, i, fa):
    """N of the `case N:` the statement at token i is under, else None."""
    toks = u.toks
    k = i - 1
    while k > fa:
        s = toks[k].s
        if s in ("break", "return", "goto") or (s == "{" and toks[k - 1].s == ")" and
                                                toks[u.match.get(k - 1, k) - 1].s == "switch"):
            return None
        if s == "case" and toks[k + 1].k == "num" and toks[k + 2].s == ":":
            return oa.num_value(toks[k + 1].s)
        k -= 1
    return None


class Pending(Exception):
    pass


def channel_events(u, workdir):
    """The channel writers and readers of a unit (ChanEvent list)."""
    toks = u.toks
    raw = []  # (role, i0, func, data)
    for fa, fb in u.funcs:
        for i in range(fa + 1, fb):
            t = toks[i]
            if not oa.in_game(t):
                continue
            if t.k == "id" and t.s in MAIL_SENDERS and toks[i + 1].s == "(" and \
                    toks[i - 1].s not in (".", "->"):
                mi, di = MAIL_SENDERS[t.s]
                args = call_args(u, i)
                if len(args) > max(mi, di):
                    ma, mb = oa.strip_parens(u, *args[mi])
                    mail = oa.num_value(toks[ma].s) if ma == mb and toks[ma].k == "num" else None
                    raw.append(("w", i, (fa, fb), ("mail", strip_byte_casts(u, *args[di]), mail,
                                                    u.match[i + 1])))
                continue
            if t.s == "(":
                c = u.cast_at(i)
                if not c or not c[0].endswith("*") or c[0].endswith("* *"):
                    continue
                T = c[0][:-1].strip()
                if not is_record_name(T):
                    continue
                e = u.unary_end(c[1] + 1)
                if e is None or e >= fb:
                    continue
                a, b = strip_byte_casts(u, c[1] + 1, e)
                if toks[a].s in MAIL_GETTERS and toks[a + 1].s == "(" and u.match.get(a + 1) == b:
                    raw.append(("r", i, (fa, fb), ("mail", T, None, e)))
                elif oa.field_split(u, a, b) is not None or (a == b and toks[a].k == "id"):
                    raw.append(("r", i, (fa, fb), ("expr", T, (a, b), e)))
                continue
            if t.s == "=" and t.k == "punct":
                ls = u.postfix_start(i - 1, casts=False)
                fs = oa.field_split(u, ls, i - 1)
                if fs is None or toks[ls - 1].s not in (";", "{", "}", ")", ",", "else"):
                    continue
                j = i + 1
                depth = 0
                while j < fb:
                    s = toks[j].s
                    if s in "([{":
                        depth += 1
                    elif s in ")]}":
                        if depth == 0:
                            break
                        depth -= 1
                    elif depth == 0 and s in (";", ","):
                        break
                    j += 1
                if j > i + 1:
                    raw.append(("w", i, (fa, fb), ("member", (ls, i - 1), strip_byte_casts(u, i + 1, j - 1),
                                                    j - 1)))
    if not raw:
        return []
    types = {}
    need = set()
    local_cache = {}

    def ty(a, b):
        if (a, b) in types:
            return types[(a, b)]
        need.add((a, b))
        raise Pending()

    def locals_of(func):
        if func not in local_cache:
            local_cache[func] = oa.scan_func_locals(u, *func)
        return local_cache[func]

    def rec_of(base, op):
        """The record a member access's base names ('struct X *' with ->)."""
        t = ty(*base)
        r = oa.ptr_target(t) if op == "->" else oa.strip_cv(t)
        return bare(r) if r and is_record_name(r) else None

    def chan_of(a, b, func, depth=0):
        """The channel the `void *` expression [a, b] reads, or None."""
        a, b = strip_byte_casts(u, a, b)
        if toks[a].s in MAIL_GETTERS and toks[a + 1].s == "(" and u.match.get(a + 1) == b:
            return "mail"
        fs = oa.field_split(u, a, b)
        if fs is not None:
            (ba, bb), path, op = fs
            if norm_type(ty(a, b)) != "void *":
                return None
            r = rec_of((ba, bb), op)
            if r is None:
                return None
            return "mail" if (r, path) in MAIL_FIELDS else ("member", r, path)
        if a == b and toks[a].k == "id" and depth < 3:
            views, bumps = locals_of(func)
            if toks[a].s in bumps or toks[a].s not in views:
                return None
            got = set()
            for ra, rb in views[toks[a].s]:
                got.add(chan_of(ra, rb, func, depth + 1))
            return got.pop() if len(got) == 1 else None
        return None

    def cast_at_use(a, b, func):
        """Records the function casts the expression [a, b] to."""
        txt = span_text(u, a, b)
        out = set()
        fa, fb = func
        for k in range(fa + 1, fb):
            if toks[k].s != "(":
                continue
            c = u.cast_at(k)
            if not c or not c[0].endswith("*") or c[0].endswith("* *"):
                continue
            e = u.unary_end(c[1] + 1)
            if e is None:
                continue
            x, y = strip_byte_casts(u, c[1] + 1, e)
            if span_text(u, x, y) == txt and is_record_name(c[0][:-1].strip()):
                out.add(bare(c[0][:-1].strip()))
        return out

    def storage(a, b, func, depth=0):
        """(record, member path) the data expression [a, b] points into;
        ("", why) for a non-record; (None, why) when not known."""
        a, b = strip_byte_casts(u, a, b)
        if toks[a].s == "&":
            fs = oa.field_split(u, a + 1, b)
            if fs is not None:
                (ba, bb), path, op = fs
                r = rec_of((ba, bb), op)
                if r is not None:
                    return r, path
            t = oa.ptr_target(ty(a, b))
            if t and is_record_name(t):
                return bare(t), ""
            return "", "the address of %s" % (t or "?")
        t = norm_type(ty(a, b))
        pt = oa.ptr_target(t)
        fs = oa.field_split(u, a, b)
        if fs is not None and pt and not is_record_name(pt) and oa.strip_cv(pt) not in ("void", "char"):
            # an array member decays to its first element: the record from there
            (ba, bb), path, op = fs
            r = rec_of((ba, bb), op)
            if r is not None:
                return r, "@" + path  # resolved against the EE layout: array or pointer
        if pt and is_record_name(pt):
            return bare(pt), ""
        if pt and oa.strip_cv(pt) in ("void", "char", "unsigned char"):
            if a == b and toks[a].k == "id" and depth < 3:
                views, bumps = locals_of(func)
                if toks[a].s not in bumps and toks[a].s in views:
                    got = {storage(ra, rb, func, depth + 1) for ra, rb in views[toks[a].s]}
                    if len(got) == 1:
                        return got.pop()
            casts = cast_at_use(a, b, func)
            if len(casts) == 1:
                return casts.pop(), ""
            if chan_of(a, b, func) is not None:
                return "", "another channel"
            return None, "a %s the function does not cast to one record" % t
        if toks[a].k == "num" and oa.num_value(toks[a].s) == 0 and a == b:
            return "", "null"
        return "", t

    events = []
    for rnd in range(8):
        need = set()
        events = []
        for role, i0, func, data in raw:
            t0 = toks[i0]
            f = os.path.normpath(t0.f) if os.path.isabs(t0.f) else t0.f
            try:
                if role == "r":
                    kind, T, sp, end = data
                    if kind == "mail":
                        chan = "mail"
                    else:
                        chan = chan_of(sp[0], sp[1], func)
                    if chan is None:
                        continue
                    ev = ChanEvent("reader", f, t0.ln, re.sub(r"\s+", " ", u.text_of(i0, end))[:160], u.src)
                    ev.chan, ev.T = chan, bare(T)
                    if chan == "mail":
                        ev.mail = case_label(u, i0, func[0])
                    events.append(ev)
                    continue
                kind = data[0]
                if kind == "mail":
                    _, (a, b), mail, end = data
                    chan = "mail"
                else:
                    _, (la, lb), (a, b), end = data
                    fs = oa.field_split(u, la, lb)
                    if norm_type(ty(la, lb)) != "void *":
                        continue
                    r = rec_of(fs[0], fs[2])
                    if r is None:
                        continue
                    chan = "mail" if (r, fs[1]) in MAIL_FIELDS else ("member", r, fs[1])
                    mail = None
                st = storage(a, b, func)
                if st[0] == "" or (chan != "mail" and st[0] is None):
                    continue
                ev = ChanEvent("writer", f, t0.ln, re.sub(r"\s+", " ", u.text_of(i0, end))[:160], u.src)
                ev.chan, ev.mail = chan, mail
                if st[0] is None:
                    ev.untyped = st[1]
                else:
                    ev.U = st
                events.append(ev)
            except Pending:
                pass
        new = {sp for sp in need if sp not in types}
        if not new:
            break
        types.update(oa.probe_types(u, new, workdir))
    return events


def load_allow():
    """[(file, snippet text, reason)] of tools/template_audit_allow.txt."""
    out = []
    if not os.path.exists(ALLOW_PATH):
        return out
    with open(ALLOW_PATH, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 3 or not parts[2].strip():
                raise SystemExit("%s: want <file> TAB <snippet> TAB <reason>: %r" % (ALLOW_PATH, line))
            out.append(tuple(p.strip() for p in parts))
    return out


PTR_WORDS = {"ICO_WORD", "ICO_WORD_PTR", "IosMsgWord", "IosMemAddr", "intptr_t", "uintptr_t"}


def ee_elements(rec):
    """{top-level member: [(EE offset, size, kind, host path)]} of a record's
    scalar elements (arrays split), from the offset audit's EE layout. kind is
    'ptr' (a pointer or a pointer-wide word: 4 bytes on the EE), 'float',
    'int' or 'blob' (a member whose type the layout does not open)."""
    ents = [e for e in rec.entries if e[4] == "field"]
    paths = {e[0] for e in ents}
    out = {}
    for e in ents:
        p = e[0]
        if any(q.startswith(p + ".") or q.startswith(p + "[") for q in paths if q != p):
            continue  # not a leaf
        top = re.split(r"[.\[]", p)[0]
        mt = e[2]
        if mt is None:
            out.setdefault(top, []).append((e[1], e[6] or 4, "int", p))
            continue
        words, ptr, dims = mt
        key = " ".join(w for w in words if w != "signed")
        if ptr or (words and words[-1] in PTR_WORDS):
            esz, kind = 4, "ptr"
        elif key in ("float", "double"):
            esz, kind = (4 if key == "float" else 8), "float"
        else:
            esz = oa.scalar_elem(mt)
            kind = "int"
        if esz is None or not dims or None in dims:
            if dims and None in dims or e[6] is None:
                return None
            if esz is None:
                out.setdefault(top, []).append((e[1], e[6], "blob", p))
            else:
                out.setdefault(top, []).append((e[1], esz, kind, p))
            continue
        n = 1
        for d in dims:
            n *= d
        if n > 4096:
            return None
        for k in range(n):
            idx = []
            rest = k
            for j in range(len(dims)):
                inner = 1
                for d in dims[j + 1:]:
                    inner *= d
                idx.append(rest // inner)
                rest %= inner
            out.setdefault(top, []).append((e[1] + k * esz, esz, kind, p + "".join("[%d]" % x for x in idx)))
    return out


def kinds_agree(a, b):
    return a == b or {a, b} == {"int", "ptr"} or "blob" in (a, b)


def ee_match(db, T, Tfiles, U, path, Ufiles):
    """T laid at U's member path, element by element on the EE: (pairs, why)
    with pairs [(T element path, U element path)] when the EE code's view
    holds (each element of T over one of U at the same EE offset, of the same
    size and a kind that the EE stores the same way: a 4-byte int over a
    pointer is one; a union needs one member that holds), else (None, why)."""
    tr = db.lookup(T, Tfiles)
    ur = db.lookup(U, Ufiles)
    if tr is None:
        return None, "%s has no EE layout" % T
    if ur is None:
        return None, "%s has no EE layout" % U
    base = oa.ee_offset_of(ur, path)
    if base is None:
        return None, "%s.%s has no EE offset" % (U, path)
    te, ue = ee_elements(tr), ee_elements(ur)
    if not te or ue is None:
        return None, "the EE layout of %s is not complete" % (T if not te else U)
    uat = {}
    for els in ue.values():
        for o, sz, kind, p in els:
            uat.setdefault(o, []).append((sz, kind, p))
    # U's pads: the bytes a whole copy carries; a T element there reads them
    pads = [(e[1], e[6], e[0]) for e in ur.entries if e[4] == "pad" and e[6]]
    groups = {}
    why = ""
    for top, els in te.items():
        got = []
        for o, sz, kind, p in els:
            c = [x for x in uat.get(base + o, []) if x[0] == sz and kinds_agree(kind, x[1])]
            if not c:
                pc = [x for x in pads if x[0] <= base + o and base + o + sz <= x[0] + x[1]]
                if pc:
                    got.append((p, "%s[%d]" % (pc[0][2], base + o - pc[0][0]), False))
                    continue
                why = "no %s element of %d bytes like %s.%s (%s) at EE 0x%X" % (U, sz, T, p, kind, base + o)
                got = None
                break
            got.append((p, min(c, key=lambda x: (x[2].count("."), x[2]))[2], True))
        groups[top] = got
    spans = sorted((min(x[0] for x in els), max(x[0] + x[1] for x in els), top) for top, els in te.items())
    union = any(spans[k + 1][0] < spans[k][1] for k in range(len(spans) - 1))
    if union:
        ok = [g for g in groups.values() if g]
        if not ok:
            return None, why
        return [x for g in ok for x in g], ""
    if any(g is None for g in groups.values()):
        return None, why
    return [x for g in groups.values() for x in g], ""


def host_check(units, T, U, path, pairs, workdir):
    """Differences of T's host layout from U's from path."""
    ex = {0: "sizeof(%s)" % T, 1: "__alignof__(%s)" % T, 2: "sizeof(%s)" % U,
          3: "__builtin_offsetof(%s, %s)" % (U, path) if path else "0"}
    for k, (tm, um, _) in enumerate(pairs):
        ex[4 + 4 * k] = "__builtin_offsetof(%s, %s)" % (T, tm)
        ex[5 + 4 * k] = "__builtin_offsetof(%s, %s)" % (U, um)
        ex[6 + 4 * k] = "sizeof(((%s *)0)->%s)" % (T, tm)
        ex[7 + 4 * k] = "sizeof(((%s *)0)->%s)" % (U, um)
    got = {}
    for un in units:
        miss = {k: v for k, v in ex.items() if k not in got}
        if not miss:
            break
        got.update(oa.probe_values(un, miss, workdir))
    if any(k not in got for k in ex):
        return ["the host probe failed (%s)" % ", ".join(ex[k] for k in sorted(ex) if k not in got)[:200]]
    bad = []
    base = got[3]
    if got[0] > got[2] - base:
        bad.append("size %d past the end (%d left)" % (got[0], got[2] - base))
    if got[1] >= 16 and base % got[1]:
        bad.append("%s at host 0x%X, not on %s's %d-byte alignment" % (path or U, base, T, got[1]))
    for k, (tm, um, sized) in enumerate(pairs):
        to, uo, ts, us = (got[4 + 4 * k + x] for x in range(4))
        if to != uo - base:
            bad.append("%s %d/%d (%s)" % (tm, to, uo - base, um))
        elif sized and ts != us:
            bad.append("%s %d bytes over %d (%s)" % (tm, ts, us, um))
    return bad


def check_channels(events, tus, workdir):
    """Pair the channel writers and readers of every unit; returns Sites."""
    db = oa.LayoutDB()
    allow = load_allow()
    used = set()
    out = []

    def files_of(ev):
        f = os.path.realpath(ev.file if os.path.isabs(ev.file) else os.path.join(ROOT, ev.file))
        src = ev.src
        fl = []
        for p in (src, f):
            if p.startswith(oa.ICO2) and os.path.exists(p):
                db.add_source(p)
                fl.append(os.path.relpath(p, ROOT))
        return fl

    def allowed(ev):
        rel = os.path.relpath(ev.file, ROOT) if os.path.isabs(ev.file) else ev.file
        for k, (f, snip, why) in enumerate(allow):
            if f == rel and snip in ev.snippet:
                used.add(k)
                return why
        return None

    def rel(f):
        return os.path.relpath(f, ROOT) if os.path.isabs(f) else f

    def site(ev, verdict, detail):
        s = Site(ev.file, ev.line, "mail" if ev.chan == "mail" else "vptr", ev.snippet)
        s.verdict, s.detail = verdict, detail
        s.key = lambda: (s.file, s.line, s.snippet, s.detail)  # one per writer
        out.append(s)

    unit_cache = {}

    def unit(src):
        if src not in unit_cache:
            fl = tus[src]
            unit_cache[src] = oa.Unit.__new__(oa.Unit)
            unit_cache[src].src = src
            unit_cache[src].text = oa.preprocess(src, fl, audit=False)
            unit_cache[src].flags = fl
        return unit_cache[src]

    def resolve_path(U, path, files):
        """'@f': an array member f is storage from f; a pointer member is not
        U's storage (None)."""
        if not path.startswith("@"):
            return path
        r = db.lookup(U, files)
        e = oa.find_entry(r, path[1:]) if r else None
        if e is None or e[2] is None or e[2][1] or not e[2][2]:
            return None
        return path[1:]

    chans = {}
    for ev in events:
        chans.setdefault(ev.chan, []).append(ev)
    checked = {}
    for chan, evs in sorted(chans.items(), key=lambda kv: str(kv[0])):
        writers = []
        for w in (e for e in evs if e.role == "writer"):
            if w.untyped:
                if chan == "mail":
                    why = allowed(w)
                    site(w, "LISTED" if why else "UNTYPED", "mail data: %s%s" % (
                        w.untyped, ("; " + why) if why else ""))
                continue
            fl = files_of(w)
            p = resolve_path(w.U[0], w.U[1], fl)
            if p is None:
                continue
            writers.append((w, w.U[0], p, fl))
        readers = [e for e in evs if e.role == "reader"]
        numbered = {r.mail for r in readers if r.mail is not None}
        if chan != "mail" and len({(w[1], w[2]) for w in writers}) != 1:
            # a `void *` member written with several record types (an actor's
            # work, Sub15C.work) holds one per object: its readers cannot be
            # paired by type
            continue
        for r in readers:
            rf = files_of(r)
            if chan == "mail" and r.mail is not None:
                cands = [w for w in writers if w[0].mail == r.mail]
                forced = True
            else:
                cands = [w for w in writers if not (chan == "mail" and w[0].mail in numbered)]
                forced = False
            matched = 0
            for w, U, path, wf in cands:
                if U == r.T and not path:
                    matched += 1
                    if chan == "mail":
                        site(r, "SAME", "%s, written at %s:%d" % (r.T, rel(w.file), w.line))
                    continue
                pairs, why = ee_match(db, r.T, rf, U, path, wf)
                if pairs is None:
                    if forced:
                        site(r, "MISMATCH", "mail %s: %s over %s%s, written at %s:%d: %s" % (
                            r.mail, r.T, U, "." + path if path else "", rel(w.file), w.line, why))
                        matched += 1
                    continue
                matched += 1
                key = (r.T, U, path, r.src)
                if key not in checked:
                    checked[key] = host_check([unit(r.src), unit(w.src),
                                               oa.global_unit(tus[r.src], workdir)],
                                              r.T, U, path, pairs, workdir)
                bad = checked[key]
                where = "%s over %s%s (written at %s:%d)" % (r.T, U, " from ." + path if path else "",
                                                             rel(w.file), w.line)
                if bad:
                    site(r, "MISMATCH", "%s, host view/storage: %s" % (where, ", ".join(bad[:8])))
                else:
                    site(r, "OK", "%s: %d elements at their EE offsets" % (where, len(pairs)))
            if chan == "mail" and not matched:
                if r.mail is not None:
                    site(r, "LISTED", "mail %d: no sender in the tree" % r.mail)
                    continue
                why = allowed(r)
                site(r, "LISTED" if why else "UNPAIRED", "mail data read as %s: no sender's storage "
                     "matches its EE layout%s" % (r.T, ("; " + why) if why else ""))
    for k, (f, snip, why) in enumerate(allow):
        if k not in used:
            s = Site(f, 0, "allow", snip)
            s.verdict, s.detail = "STALE", "allowlist line matches no site: " + why
            out.append(s)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", required=True, help="a configured build folder (compile_commands.json)")
    ap.add_argument("--all", action="store_true", help="list every record copy (VIEW, RAW, SAME)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    tus = oa.load_tus(args.build)
    if args.files:
        want = {os.path.realpath(f) for f in args.files}
        tus = {k: v for k, v in tus.items() if k in want}
    results = []
    wd = os.path.join(args.build, "template_audit")
    os.makedirs(wd, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="work.", dir=wd) as work:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as ex:
            futs = [ex.submit(audit_unit, src, fl, work) for src, fl in sorted(tus.items())]
            events = []
            for f in concurrent.futures.as_completed(futs):
                copies, ev = f.result()
                results.extend(copies)
                events.extend(ev)
        # the pairs need every unit's writers: with a FILE list, the readers
        # and writers of the units named
        results.extend(check_channels(events, tus, work))
    seen = {}
    for s in results:
        seen.setdefault(s.key(), s)
    sites = sorted(seen.values(), key=lambda s: (s.file, s.line, s.snippet))
    bad = ("MISMATCH", "UNREGISTERED", "UNTYPED", "UNPAIRED", "STALE")
    counts = {}
    for s in sites:
        counts[s.verdict] = counts.get(s.verdict, 0) + 1
        if s.verdict in bad or args.all:
            f = os.path.relpath(s.file, ROOT) if os.path.isabs(s.file) else s.file
            print("%-12s %s:%d [%s] %s\n             %s" % (s.verdict, f, s.line, s.kind, s.detail,
                                                          s.snippet))
    nch = sum(1 for s in sites if s.kind in ("mail", "vptr"))
    print("template_audit: %d units, %d record copies, %d views through void *" % (
        len(tus), len(sites) - nch, nch))
    print("template_audit: " + ", ".join("%s %d" % (k, counts.get(k, 0)) for k in
                                         ("OK", "MISMATCH", "UNREGISTERED", "UNTYPED", "UNPAIRED",
                                          "STALE", "SAME", "RAW", "LISTED")))
    return 1 if any(counts.get(k, 0) for k in bad) else 0


if __name__ == "__main__":
    sys.exit(main())
