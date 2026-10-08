#!/usr/bin/env python3
"""tools/stack_overread_audit.py [--selftest] [--allow FILE] [PATH...]

Looks in the game sources (ico2/ by default) for code that only worked on the
PS2 because of how the period compiler laid out a function's locals on the
stack. The host compiler orders and pads locals as it likes, so such code
reads or writes the wrong bytes on the PC. Issue 12 was one: QueenBarrierGeo
built a 3x3 matrix in a 48-byte local, put the translation in the next local,
and copied 64 bytes from the first, which only gave the right translation
because the second local sat right after the first in the PS2's frame.

Two checks, both on locals of a function (not parameters, not globals):

  overread   a call whose callee reads or writes more bytes through a pointer
             argument than the local object it points at holds from that
             point (CALLEES below lists each callee's width per argument:
             the 64-byte matrix copies and products, the 16-byte vector
             helpers, memcpy/memmove/memset with a literal size).
  write-only a local that is only ever written (by an assignment or as the
             destination of a known writer) and never read. In decompiled
             code that is the tell-tale of a value meant to be picked up by
             an overread of its neighbour.

It is a grep-level checker, not a compiler: types come from the typedefs it
can size (QVec, QMat3, float[3][4], sceVu0FVECTOR, ...), arguments it cannot
resolve to a local are skipped. A documented false positive goes into the
allow file (tools/stack_overread_allow.txt), one per line:

    path:function:local:kind   reason

Prints one line per hit (path:line: function: kind: reason) and exits 1 when
any hit is not allowed, or when an allow entry matches nothing (so the list
cannot go stale). --selftest runs the checks on tools/test/stack_overread/
and compares the hits with the fixtures' EXPECT markers.
"""

import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_ALLOW = os.path.join(ROOT, "tools", "stack_overread_allow.txt")
FIXTURES = os.path.join(ROOT, "tools", "test", "stack_overread")

# Bytes each callee touches through each pointer argument (index: width).
# Widths are the host implementations' (port/math/libvu0.c, matrix.c,
# matrix_drive.c): the matrix routines memcpy/memmove 64 bytes, the vector
# routines read and write all four lanes.
M, V = 64, 16
CALLEES = {
    "CopyMatrix": {0: M, 1: M},
    "_CopyMatrix": {0: M, 1: M},
    "sceVu0CopyMatrix": {0: M, 1: M},
    "CopyMatrixUncached": {0: M, 1: M},
    "sceVu0MulMatrix": {0: M, 1: M, 2: M},
    "_MulMatrix": {0: M, 1: M, 2: M},
    "sceVu0TransposeMatrix": {0: M, 1: M},
    "_TransposeMatrix": {0: M, 1: M},
    "sceVu0InversMatrix": {0: M, 1: M},
    "_InversMatrix": {0: M, 1: M},
    "sceVu0UnitMatrix": {0: M},
    "_UnitMatrix": {0: M},
    "sceVu0TransMatrix": {0: M, 1: M},
    "sceVu0RotMatrixX": {0: M, 1: M},
    "sceVu0RotMatrixY": {0: M, 1: M},
    "sceVu0RotMatrixZ": {0: M, 1: M},
    "sceVu0RotMatrix": {0: M, 1: M},
    "sceVu0ApplyMatrix": {0: V, 1: M, 2: V},
    "_ApplyMatrix": {0: V, 1: M, 2: V},
    "sceVu0CopyVector": {0: V, 1: V},
    "_CopyVector": {0: V, 1: V},
    "CopyVector": {0: V, 1: V},
    "sceVu0AddVector": {0: V, 1: V, 2: V},
    "sceVu0SubVector": {0: V, 1: V, 2: V},
    "sceVu0MulVector": {0: V, 1: V, 2: V},
    "sceVu0ScaleVector": {0: V, 1: V},
    "sceVu0ScaleVectorXYZ": {0: V, 1: V},
    "sceVu0InterVector": {0: V, 1: V, 2: V},
    "sceVu0Normalize": {0: V},
    "sceVu0OuterProduct": {0: V},
    "_AddVector": {0: V, 1: V, 2: V},
    "_SubVector": {0: V, 1: V, 2: V},
    "_ScaleVector": {0: V, 1: V},
    # the game's own matrix writers (sugipon/src/geometryManager.c,
    # matrixDrive.c, quaternion.c)
    "GetRootMatrix": {0: M},
    "MatrixDrive_SetTransposeMatrix": {0: M, 1: M},
    "GetMatrixFromQuaternion": {0: M, 1: V},
    "GetMatrixFromQuaternionPos": {0: M},
}
# memcpy-like: (dst index, src index or None, size index)
SIZED = {"memcpy": (0, 1, 2), "memmove": (0, 1, 2), "memset": (0, None, 2)}

# Callees whose argument 0 is only written (for the write-only check).
WRITERS = {n for n, a in CALLEES.items() if 0 in a and n not in (
    "sceVu0AddVector", "sceVu0SubVector", "sceVu0MulVector")}
WRITERS |= {"sceVu0AddVector", "sceVu0SubVector", "sceVu0MulVector",
            "memset", "memcpy", "memmove", "UnitMatrix33", "_UnitRotation",
            "UnitRotation", "_UnitVector", "GetRootMatrix", "GetRootPosition",
            "MatrixDrive_SetTransposeMatrix", "GetMatrixFromQuaternion",
            "GetMatrixFromQuaternionPos"}
# The decomp's local helpers that write a 48-byte 3x3 through argument 0.
CALLEES["UnitMatrix33"] = {0: 48}

BUILTIN = {
    "char": (1, 1), "short": (2, 2), "int": (4, 4), "float": (4, 4),
    "double": (8, 8), "long": (8, 8),
    "u_char": (1, 1), "u_short": (2, 2), "u_int": (4, 4), "u_long": (8, 8),
    "u_long128": (16, 16), "u_long64": (8, 8),
    "uint8_t": (1, 1), "int8_t": (1, 1), "uint16_t": (2, 2), "int16_t": (2, 2),
    "uint32_t": (4, 4), "int32_t": (4, 4), "uint64_t": (8, 8), "int64_t": (8, 8),
    "u8": (1, 1), "s8": (1, 1), "u16": (2, 2), "s16": (2, 2),
    "u32": (4, 4), "s32": (4, 4), "u64": (8, 8), "s64": (8, 8),
    "sceVu0FVECTOR": (16, 16), "sceVu0IVECTOR": (16, 16),
    "sceVu0FMATRIX": (64, 16), "sceVu0IMATRIX": (64, 16),
}
PTR = (8, 8)
KEYWORDS = {"return", "if", "else", "while", "for", "do", "switch", "case",
            "goto", "break", "continue", "sizeof", "default"}


def strip(text):
    """Comments and string/char literals blanked, newlines kept."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r"[^\n]", " ", text[i + 1:j - 1]) + c)
            i = j
        elif c == "#" and (i == 0 or text[i - 1] == "\n"):
            j = i
            while True:
                k = text.find("\n", j)
                if k < 0:
                    k = n
                    break
                if text[k - 1] == "\\":
                    j = k + 1
                    continue
                break
            out.append(re.sub(r"[^\n]", " ", text[i:k]))
            i = k
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_top(s, sep=","):
    parts, depth, cur = [], 0, []
    for c in s:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    return parts


def match_close(s, i, o="(", c=")"):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == o:
            depth += 1
        elif s[j] == c:
            depth -= 1
            if depth == 0:
                return j
    return -1


def eval_dim(expr, consts):
    e = expr.strip()
    if not e:
        return None
    e = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(consts.get(m.group(1), "X")), e)
    e = re.sub(r"(\d+)[uUlL]+", r"\1", e)
    if not re.fullmatch(r"[\d\s+\-*/()x]+", e) or "X" in e:
        return None
    try:
        return int(eval(e, {"__builtins__": {}}))  # noqa: S307 - digits and operators only
    except Exception:
        return None


class Types:
    """Sizes and field layouts of the typedefs it can resolve."""

    def __init__(self):
        self.size = dict(BUILTIN)   # name -> (size, align)
        self.fields = {}            # name -> [(field, offset, size, (elem type, dims))]

    def copy(self):
        t = Types()
        t.size = dict(self.size)
        t.fields = dict(self.fields)
        return t

    def base(self, words):
        """(size, align) of a type spelled by its words, or None."""
        w = [x for x in words if x not in ("const", "volatile", "static", "register",
                                           "extern", "struct", "union", "signed")]
        if not w:
            return None
        if w[0] == "unsigned":
            w = w[1:] or ["int"]
        if w[-1] == "int" and len(w) > 1:
            w = w[:-1]
        name = " ".join(w)
        if name == "long long":
            return (8, 8)
        return self.size.get(name)

    def learn(self, text, consts):
        """Every `typedef struct {..} Name;`, `struct Tag {..};` and array typedef in text."""
        for m in re.finditer(r"\b(typedef\s+)?(struct|union)\s*(\w*)\s*\{", text):
            j = match_close(text, m.end() - 1, "{", "}")
            if j < 0:
                continue
            body = text[m.end():j]
            tail = re.match(r"\s*(?:__attribute__\s*\(\(((?:[^()]|\([^()]*\))*)\)\)\s*)?(\w*)\s*(?:__attribute__\s*\(\(((?:[^()]|\([^()]*\))*)\)\)\s*)?;",
                            text[j + 1:], re.S)
            layout = self._layout(body, m.group(2) == "union", consts)
            if layout is None:
                continue
            size, align, fields = layout
            attrs = ""
            if tail:
                attrs = (tail.group(1) or "") + (tail.group(3) or "")
            am = re.search(r"aligned\s*\(\s*(\d+)\s*\)", attrs)
            if am:
                align = max(align, int(am.group(1)))
            size = (size + align - 1) // align * align
            names = []
            if m.group(3):
                names.append(m.group(3))
            if m.group(1) and tail and tail.group(2):
                names.append(tail.group(2))
            for nm in names:
                self.size[nm] = (size, align)
                self.fields[nm] = fields
        for m in re.finditer(r"\btypedef\s+([\w\s]+?)\s+(\w+)\s*((?:\[[^\]]*\])+)\s*(?:__attribute__\s*\(\(((?:[^()]|\([^()]*\))*)\)\))?\s*;", text):
            b = self.base(m.group(1).split())
            if b is None:
                continue
            dims = [eval_dim(d, consts) for d in re.findall(r"\[([^\]]*)\]", m.group(3))]
            if None in dims:
                continue
            n = 1
            for d in dims:
                n *= d
            align = b[1]
            am = re.search(r"aligned\s*\(\s*(\d+)\s*\)", m.group(4) or "")
            if am:
                align = max(align, int(am.group(1)))
            self.size[m.group(2)] = (b[0] * n, align)

    def _layout(self, body, union, consts):
        off, align, fields = 0, 1, []
        for decl in body.split(";"):
            decl = decl.strip()
            if not decl:
                continue
            if "{" in decl or "(" in decl or ":" in decl:
                return None
            dm = re.match(r"((?:\w+\s+)*\w+)\b\s*(\**\s*\w+\s*(?:\[[^\]]*\]\s*)*"
                          r"(?:,\s*\**\s*\w+\s*(?:\[[^\]]*\]\s*)*)*)$", decl, re.S)
            if not dm:
                return None
            words = dm.group(1).split()
            b = self.base(words)
            for d in split_top(dm.group(2)):
                dd = re.match(r"\s*(\**)\s*(\w+)\s*((?:\[[^\]]*\])*)\s*$", d)
                if not dd:
                    return None
                if dd.group(1):
                    fs, fa = PTR
                else:
                    if b is None:
                        return None
                    fs, fa = b
                dims = [eval_dim(x, consts) for x in re.findall(r"\[([^\]]*)\]", dd.group(3))]
                if None in dims:
                    return None
                n = 1
                for x in dims:
                    n *= x
                elem = fs
                fs *= n
                o = 0 if union else (off + fa - 1) // fa * fa
                fields.append((dd.group(2), o, fs, (" ".join(words) if not dd.group(1) else None, dims, elem)))
                off = max(off, o + fs) if union else o + fs
                align = max(align, fa)
        return off, align, fields


def consts_of(text):
    c = {}
    for m in re.finditer(r"^\s*#\s*define\s+(\w+)\s+\(?\s*(\d+)\s*\)?\s*$", text, re.M):
        c[m.group(1)] = int(m.group(2))
    for m in re.finditer(r"\benum\b[^{;]*\{([^}]*)\}", text):
        v = 0
        for item in m.group(1).split(","):
            item = item.strip()
            if not item:
                continue
            im = re.match(r"(\w+)\s*(?:=\s*(.+))?$", item, re.S)
            if not im:
                break
            if im.group(2) is not None:
                ev = eval_dim(im.group(2), c)
                if ev is None:
                    break
                v = ev
            c[im.group(1)] = v
            v += 1
    return c


class Local:
    def __init__(self, name, typ, size, elem, dims, pos, end):
        self.name, self.typ, self.size = name, typ, size
        self.elem, self.dims, self.pos, self.end = elem, dims, pos, end


def block_end(body, p):
    """End (body offset) of the innermost brace block holding body offset p."""
    depth = 0
    for i in range(p - 1, -1, -1):
        if body[i] == "}":
            depth += 1
        elif body[i] == "{":
            if depth == 0:
                j = match_close(body, i, "{", "}")
                return len(body) if j < 0 else j
            depth -= 1
    return len(body)


def visible(locs, pos):
    """name -> the Local whose declaration is in scope at absolute offset pos."""
    out = {}
    for l in locs:
        if l.pos <= pos < l.end and (l.name not in out or out[l.name].pos < l.pos):
            out[l.name] = l
    return out


def functions(s):
    """(name, body start, body end) of each top-level function definition."""
    depth, last, i = 0, 0, 0
    while i < len(s):
        c = s[i]
        if c == "{":
            if depth == 0:
                head = s[last:i]
                hm = re.search(r"(\w+)\s*\(([^;{}]*)\)\s*(?:__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)\s*)?$", head, re.S)
                if hm and hm.group(1) not in KEYWORDS and not re.search(r"=\s*$", head):
                    j = match_close(s, i, "{", "}")
                    if j < 0:
                        return
                    yield hm.group(1), i + 1, j
                    i = j + 1
                    last = i
                    continue
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                last = i + 1
        elif c == ";" and depth == 0:
            last = i + 1
        i += 1


DECL = re.compile(
    r"(?:(?<=[;{}])|^)(\s*)((?:(?:const|volatile|register|unsigned|signed|struct|union|long|short)\s+)*)"
    r"([A-Za-z_]\w*)\s+([^;{}()]*?(?:=[^;{}]*)?)(?=;)", re.M)


def locals_of(body, base, types):
    """Every local declared in body (block-scoped ones too), shadowing kept apart."""
    out = []
    for m in DECL.finditer(body):
        words = m.group(2).split() + [m.group(3)]
        if m.group(3) in KEYWORDS or "static" in words:
            continue
        b = types.base(words)
        if b is None:
            continue
        typ = " ".join(w for w in words if w not in ("const", "volatile", "register", "struct", "union"))
        decls = m.group(4)
        for d in split_top(decls):
            d = d.split("=", 1)[0]
            dd = re.match(r"\s*(\**)\s*([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*$", d)
            if not dd or dd.group(2) in KEYWORDS:
                continue
            name = dd.group(2)
            rp = m.start(4) + m.group(4).find(d) + d.find(name)
            end = base + block_end(body, rp)
            if dd.group(1):
                out.append(Local(name, typ, None, None, [], base + rp, end))
                continue
            dims = [eval_dim(x, {}) for x in re.findall(r"\[([^\]]*)\]", dd.group(3))]
            if None in dims:
                continue
            n = 1
            for x in dims:
                n *= x
            out.append(Local(name, typ, b[0] * n, b[0], dims, base + rp, end))
    return out


CAST = re.compile(r"^\(\s*(?:const\s+|volatile\s+|struct\s+|union\s+|unsigned\s+)*[A-Za-z_]\w*\s*(?:\*\s*)+\)\s*")


def resolve(arg, locs, types):
    """(local, bytes available from the pointer onward) for &x, x[], &x.f..., or None."""
    a = arg.strip()
    while True:
        a0 = a
        a = CAST.sub("", a).strip()
        if a.startswith("(") and match_close(a, 0) == len(a) - 1:
            a = a[1:-1].strip()
        if a == a0:
            break
    amp = a.startswith("&")
    if amp:
        a = a[1:].strip()
    m = re.match(r"([A-Za-z_]\w*)((?:\s*(?:\.\s*\w+|\[[^\]]*\]))*)\s*(?:\+\s*(\d+))?$", a)
    if not m or m.group(1) not in locs:
        return None
    loc = locs[m.group(1)]
    if loc.size is None:
        return None
    acc = re.findall(r"\.\s*(\w+)|\[([^\]]*)\]", m.group(2))
    if not amp and not acc and not loc.dims:
        return None              # a struct passed by value, not a pointer
    off, size, typ, dims, elem = 0, loc.size, loc.typ, list(loc.dims), loc.elem
    for field, idx in acc:
        if field:
            fl = types.fields.get(typ)
            if fl is None or dims:
                return None
            f = [x for x in fl if x[0] == field]
            if not f:
                return None
            _, fo, fs, (ftyp, fdims, felem) = f[0]
            off += fo
            size, typ, dims, elem = fs, ftyp, list(fdims), felem
        else:
            if not dims:
                return None
            row = size // dims[0]
            iv = eval_dim(idx, {})
            if iv is None:
                iv = dims[0] - 1         # unknown index: the last row is the tightest
            off += iv * row
            size = row
            dims = dims[1:]
    plus = int(m.group(3)) if m.group(3) else 0
    if plus:
        if not dims:
            return None
        off += plus * (size // dims[0])
    if amp or not acc or dims:
        avail = loc.size - off
        return loc, avail, (off, size)
    return None


def calls(body):
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
        if m.group(1) in KEYWORDS:
            continue
        j = match_close(body, m.end() - 1)
        if j < 0:
            continue
        yield m.group(1), m.start(), m.end(), j, split_top(body[m.end():j])


def audit_file(path, rel, base_types, consts):
    raw = open(path, encoding="utf-8", errors="replace").read()
    s = strip(raw)
    types = base_types.copy()
    types.learn(s, consts)
    hits = []

    def line_of(pos):
        return s.count("\n", 0, pos) + 1

    for fname, b0, b1 in functions(s):
        body = s[b0:b1]
        locs = locals_of(body, b0, types)
        if not locs:
            continue
        arg_spans = []
        for callee, cs, ce, cj, args in calls(body):
            pos = ce
            spans = []
            for a in args:
                spans.append((pos, pos + len(a), a))
                pos += len(a) + 1
            arg_spans.append((callee, spans))
            need = {}
            if callee in CALLEES:
                need = dict(CALLEES[callee])
            elif callee in SIZED and len(args) > SIZED[callee][2]:
                n = eval_dim(args[SIZED[callee][2]], consts)
                if n is not None:
                    need = {SIZED[callee][0]: n}
                    if SIZED[callee][1] is not None:
                        need[SIZED[callee][1]] = n
            for k, width in need.items():
                if k >= len(args):
                    continue
                r = resolve(args[k], visible(locs, b0 + ce), types)
                if r is None:
                    continue
                loc, avail, _ = r
                if avail < width:
                    hits.append((rel, line_of(b0 + cs), fname, loc.name, "overread",
                                 "%s argument %d touches %d bytes, %s (%s, %d bytes) holds %d from there"
                                 % (callee, k + 1, width, loc.name, loc.typ, loc.size, avail)))
        # write-only locals
        for loc in locs:
            if loc.size is None:
                continue
            name = loc.name
            reads = writes = 0
            for m in re.finditer(r"\b%s\b" % re.escape(name), body):
                p = m.start()
                if b0 + p == loc.pos or visible(locs, b0 + p).get(name) is not loc:
                    continue
                prev = body[:p].rstrip()
                if prev.endswith(".") or prev.endswith("->"):
                    continue            # a field of something else with the same name
                after = body[m.end():]
                am = re.match(r"((?:\s*(?:\.\s*\w+|\[[^\]]*\]))*)\s*=(?!=)", after)
                if am:                  # name, name.f, name[i] followed by '=' (not '==')
                    writes += 1
                    continue
                w = False
                for callee, spans in arg_spans:
                    if callee not in WRITERS or not spans:
                        continue
                    a0, a1, a = spans[0]
                    if a0 <= p < a1 and resolve(a, {name: loc}, types):
                        w = True
                        break
                if w:
                    writes += 1
                else:
                    reads += 1
            if writes and not reads:
                hits.append((rel, line_of(loc.pos), fname, name, "write-only",
                             "%s (%s, %d bytes) is written and never read" % (name, loc.typ, loc.size)))
    return hits


def header_types(root):
    t = Types()
    consts = {}
    texts = []
    for d, _, files in os.walk(root):
        for f in sorted(files):
            if f.endswith(".h"):
                txt = open(os.path.join(d, f), encoding="utf-8", errors="replace").read()
                consts.update(consts_of(txt))
                texts.append(strip(txt))
    for _ in range(2):              # twice, so a typedef built on a later one resolves
        for txt in texts:
            t.learn(txt, consts)
    return t, consts


def sources(paths):
    for p in paths:
        if os.path.isfile(p):
            yield p
            continue
        for d, _, files in os.walk(p):
            for f in sorted(files):
                if f.endswith(".c"):
                    yield os.path.join(d, f)


def run(paths, type_roots):
    types, consts = Types(), {}
    for r in type_roots:
        t, c = header_types(r)
        types.size.update(t.size)
        types.fields.update(t.fields)
        consts.update(c)
    hits = []
    for p in sorted(set(sources(paths))):
        rel = os.path.relpath(p, ROOT)
        c = dict(consts)
        c.update(consts_of(open(p, encoding="utf-8", errors="replace").read()))
        hits += audit_file(p, rel, types, c)
    return hits


def read_allow(path):
    allow = {}
    if not os.path.exists(path):
        return allow
    for n, ln in enumerate(open(path, encoding="utf-8"), 1):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        key, _, reason = ln.partition(" ")
        parts = key.split(":")
        if len(parts) != 4 or not reason.strip():
            sys.exit("%s:%d: want 'path:function:local:kind reason'" % (path, n))
        allow[tuple(parts)] = reason.strip()
    return allow


def selftest():
    hits = run([FIXTURES], [FIXTURES])
    got = {(h[0], h[1], h[4]) for h in hits}
    want = set()
    for p in sources([FIXTURES]):
        rel = os.path.relpath(p, ROOT)
        for n, ln in enumerate(open(p, encoding="utf-8"), 1):
            for k in re.findall(r"EXPECT:\s*([\w-]+)", ln):
                want.add((rel, n, k))
    ok = True
    for w in sorted(want - got):
        print("selftest: missed %s:%d: %s" % w)
        ok = False
    for g in sorted(got - want):
        print("selftest: unexpected %s:%d: %s" % g)
        ok = False
    if not want:
        print("selftest: no EXPECT markers under %s" % FIXTURES)
        ok = False
    print("stack_overread_audit selftest: %s (%d expected hits)" % ("PASS" if ok else "FAIL", len(want)))
    return 0 if ok else 1


def main(argv):
    allow_path = DEFAULT_ALLOW
    paths = []
    it = iter(argv)
    for a in it:
        if a == "--selftest":
            return selftest()
        if a == "--allow":
            allow_path = next(it)
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            paths.append(a)
    if not paths:
        paths = [os.path.join(ROOT, "ico2")]
    hits = run(paths, [os.path.join(ROOT, "ico2")])
    allow = read_allow(allow_path)
    used = set()
    bad = 0
    for rel, line, fn, local, kind, why in hits:
        key = (rel, fn, local, kind)
        if key in allow:
            used.add(key)
            continue
        print("%s:%d: %s: %s: %s" % (rel, line, fn, kind, why))
        bad += 1
    full = not argv or all(a.startswith("--") for a in argv)
    stale = [k for k in allow if k not in used] if full else []
    for k in stale:
        print("%s: allow entry %s matches nothing" % (os.path.relpath(allow_path, ROOT), ":".join(k)))
    if bad or stale:
        return 1
    print("stack_overread_audit: OK (%d allowed)" % len(used))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
