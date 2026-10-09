#!/usr/bin/env python3
"""tools/offset_audit.py: find raw EE-offset accesses that are wrong on the host.

The decompiled game reached its records through offsets the ROM's loads and
stores used: `*(T *)((char *)p + 0x230)`, `((int *)p)[9]`, `f[81]` over a
`float *` view of a record, `ICO_RAW(T, p, 0x230, field)`. On the EE those
offsets are the records' layout. On a 64-bit host every pointer field before
the offset moves what follows, so a raw offset can land on another field.
This audit looks at every site the host compiles, whether it runs or not.

Method:

1. Every ico2/ translation unit of the game libraries (compile_commands.json
   of a configured build) is preprocessed with its host flags and
   ICO_OFFSET_AUDIT, which makes ICO_RAW/ICO_RAWP leave a marker with their
   EE offset (ico2/fumi/include/ee_view.h).
2. In the preprocessed text, within function bodies of ico2/ sources:
     - a pointer cast followed by a constant: `(char *)x + 0xNN`,
       `(int *)x + N`, `(int)x + 0xNN` (a pointer through `int` is TRUNC);
     - an index into a cast, a word read through one: `((float *)x)[N]`,
       `*(int *)x` (a 4-byte access to a field that is 8 bytes on the host
       is a MISMATCH);
     - a scalar view of a record: `f[N]`, `q + N`, where `f` is assigned
       `(float *)x` (or `(char *)x + K`) in the function, or is a parameter
       every call in the unit passes such an address for;
     - a record laid over another: `((V *)x)->m`, and `s->m` where the local
       `V *s` is assigned another record (`(LadderMotWork *)GOBJ_ACT(self)`);
       V's EE layout comes from its comments, its pad<HEX> members or its
       members' EE sizes; a record kept whole in a byte-buffer member
       (`(ClipColReq *)act->flyClip`) is only checked to fit;
     - ICO_RAW / ICO_RAWP markers: the field must be the one at the EE offset
       (counted from p, which can itself point into the record);
     - `pad<HEX>` / `unk<HEX>` members (named by their EE offset), unless
       copied or cleared whole (memcpy, memset, sizeof);
     - a variable stride (`base + j * 0x50 + 0x10`, `q[i * 20 + 9]`):
       checked at its first step, every variable at 1;
     - memset / memcpy / memmove / bzero / bcopy with a size made of
       literals over a record, a span from a member, or a whole array of
       pointers: the EE span must be the host's (PARTIAL when it stops short
       of the members the EE cleared, OVERRUN when it runs into the next);
     - the initialisers of file-scope declarations as well as function
       bodies;
     - a `void *` table element or member (`sys[2]`, `w->obj`) the function
       casts to exactly one record type elsewhere: viewed as that record.
3. The operand's type comes from the compiler: each operand is wrapped in a
   statement expression that initialises a `struct __icoA_<k> *` from it, and
   gcc's diagnostic names the operand's type. Two untyped words have their
   pointee from the game's macros (POINTEE: GObj.act is an Act, Act.work an
   ActWork).
4. The record's EE layout comes from the headers' (and the .c files') offset
   comments, read with tools/gen_layout_asserts.py's parser; members without
   a comment follow the previous one at their EE size where that is known.
   The access's EE offset gives the named field there (a member path, with an
   index into an array of scalars).
5. A probe appended to the unit compiles `offsetof(R, field)` (and sizeof) on
   the host. A site is a MISMATCH when the offset it uses on the host is not
   the named field's host offset, NOFIELD when no named field is at that EE
   offset (a gap without a comment, or a pad), TRUNC when a pointer goes
   through `int`.
6. Two passes compile each unit again: at -O1 for gcc's SSA dump (NARROW, a
   pointer-wide value in 32 bits) and as gnu23, where `()` means `(void)`,
   for the calls through unprototyped declarations (UNPROTO unless every
   argument is an int or UNPROTO_OK says what the callees read).

Usage:
    tools/offset_audit.py --build build-host/<preset>    # report, exit 1 on findings
    tools/offset_audit.py --build DIR --all              # list every site, not only findings
    tools/offset_audit.py --build DIR FILE.c...          # only these units
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_layout_asserts as gla  # noqa: E402

ICO2 = os.path.join(ROOT, "ico2") + os.sep

# --- C tokens of the preprocessed text ----------------------------------------
TOK_RE = re.compile(
    r"""
    (?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<num>0[xX][0-9A-Fa-f]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fFlL]?|\.\d+(?:[eE][-+]?\d+)?[fFlL]?|\d+(?:[eE][-+]?\d+)?[fFuUlL]*)
  | (?P<punct>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|[{}()\[\];,:*=&|^~!<>?+\-/%.\#])
  | (?P<ws>\s+)
    """,
    re.X,
)
MARKER_RE = re.compile(r'^#\s*(\d+)\s+"((?:\\.|[^"])*)"')


class T:
    __slots__ = ("k", "s", "a", "b", "f", "ln")

    def __init__(self, k, s, a, b, f, ln):
        self.k, self.s, self.a, self.b, self.f, self.ln = k, s, a, b, f, ln

    def __repr__(self):
        return self.s


def tokenize_i(text):
    toks = []
    pos = 0
    cur_file = ""
    cur_line = 0
    for line in text.split("\n"):
        end = pos + len(line)
        m = MARKER_RE.match(line)
        if m:
            cur_file = m.group(2)
            cur_line = int(m.group(1))
            pos = end + 1
            continue
        if line.startswith("#"):
            pos = end + 1
            cur_line += 1
            continue
        i = 0
        while i < len(line):
            m = TOK_RE.match(line, i)
            if not m:
                i += 1
                continue
            kind = m.lastgroup
            if kind != "ws":
                toks.append(T(kind, m.group(kind), pos + m.start(), pos + m.end(), cur_file, cur_line))
            i = m.end()
        pos = end + 1
        cur_line += 1
    return toks


def num_value(s):
    s = s.rstrip("uUlL")
    try:
        return int(s, 0)
    except ValueError:
        return None


TYPE_WORDS = {"char", "short", "int", "long", "float", "double", "void", "signed", "unsigned",
              "const", "volatile", "struct", "union", "enum", "_Bool", "__int128",
              "__signed__", "__const", "__volatile__", "__restrict", "restrict"}
CHARLIKE = {"char", "u_char", "u8", "s8", "uchar", "unsigned char", "signed char", "void", "u_int8_t",
            "uint8_t", "int8_t"}
# scalar element sizes of the views (the same on the EE and the host)
SCALAR = {"char": 1, "u_char": 1, "u8": 1, "s8": 1, "uchar": 1, "unsigned char": 1, "signed char": 1,
          "uint8_t": 1, "int8_t": 1, "void": 1,
          "short": 2, "u_short": 2, "s16": 2, "u16": 2, "unsigned short": 2, "short int": 2,
          "short unsigned int": 2, "uint16_t": 2, "int16_t": 2,
          "int": 4, "u_int": 4, "s32": 4, "u32": 4, "unsigned int": 4, "unsigned": 4, "float": 4,
          "uint32_t": 4, "int32_t": 4, "IntFloat": 4,
          "long long": 8, "unsigned long long": 8, "long long int": 8, "long long unsigned int": 8,
          "u_long": 8, "s64": 8, "u64": 8, "uint64_t": 8, "int64_t": 8}
INTCAST = {"int", "unsigned int", "u_int", "unsigned", "s32", "u32", "long", "long int",
           "unsigned long", "long unsigned int", "intptr_t", "uintptr_t", "__intptr_t",
           "size_t", "ICO_WORD", "IosMemAddr", "IosMsgWord"}


class Unit:
    """One preprocessed translation unit."""

    def __init__(self, src, text, flags):
        self.src = src
        self.text = text
        self.flags = flags
        self.toks = tokenize_i(text)
        self.match = self._match()
        self.typedefs = self._typedefs()
        self.funcs = self._funcs()
        self.inits = self._inits()

    def _match(self):
        m = {}
        st = []
        for i, t in enumerate(self.toks):
            if t.k != "punct":
                continue
            if t.s in "([{":
                st.append(i)
            elif t.s in ")]}":
                if st:
                    j = st.pop()
                    m[j] = i
                    m[i] = j
        return m

    def _typedefs(self):
        names = set()
        toks = self.toks
        i = 0
        n = len(toks)
        while i < n:
            if toks[i].s == "typedef":
                depth = 0
                last = None
                j = i + 1
                while j < n:
                    s = toks[j].s
                    if s in "({[":
                        if s == "(" and j + 2 < n and toks[j + 1].s == "*" and toks[j + 2].k == "id":
                            last = toks[j + 2].s
                        depth += 1
                    elif s in ")}]":
                        depth -= 1
                    elif depth == 0 and toks[j].k == "id" and s not in TYPE_WORDS and \
                            s not in ("__attribute__", "__attribute"):
                        last = s
                    elif depth == 0 and s in (";", ","):
                        if last:
                            names.add(last)
                        last = None
                        if s == ";":
                            break
                    j += 1
                i = j
            i += 1
        names |= {"__builtin_va_list", "_Float128", "__int128_t"}
        return names

    def _funcs(self):
        """[start, end] token index pairs of function bodies."""
        out = []
        depth = 0
        for i, t in enumerate(self.toks):
            if t.k != "punct":
                continue
            if t.s == "{":
                if depth == 0:
                    j = i - 1
                    # skip __attribute__((...)) and asm labels
                    while j >= 0 and self.toks[j].s == ")" and j in self.match:
                        k = self.match[j] - 1
                        if k >= 0 and self.toks[k].s in ("__attribute__", "__asm__", "asm"):
                            j = k - 1
                            continue
                        break
                    if j >= 0 and self.toks[j].s == ")":
                        out.append((i, self.match.get(i, len(self.toks) - 1)))
                depth += 1
            elif t.s == "}":
                depth -= 1
        return out

    def _inits(self):
        """[start, end] token index pairs of the initialisers of file-scope
        declarations (`T x = { ... };`), outside every function body."""
        out = []
        toks = self.toks
        n = len(toks)
        bodies = {a: b for a, b in self.funcs}
        depth = 0
        i = 0
        while i < n:
            t = toks[i]
            if i in bodies:
                i = bodies[i] + 1
                continue
            if t.k == "punct":
                if t.s in "{([":
                    depth += 1
                elif t.s in "})]":
                    depth -= 1
                elif t.s == "=" and depth == 0:
                    j = i + 1
                    d = 0
                    while j < n:
                        s = toks[j].s
                        if s in "{([":
                            d += 1
                        elif s in "})]":
                            d -= 1
                        elif d == 0 and s in (";", ","):
                            break
                        j += 1
                    if j > i + 1:
                        out.append((i + 1, j - 1))
                    i = j
                    continue
            i += 1
        return out

    def func_of(self, i):
        for a, b in self.funcs:
            if a <= i <= b:
                return a, b
        return None

    # --- expression helpers ---
    def is_type_start(self, i):
        t = self.toks[i]
        return t.k == "id" and (t.s in TYPE_WORDS or t.s in self.typedefs or t.s == "__typeof__"
                                or t.s == "typeof")

    def cast_at(self, i):
        """If toks[i] is `(` opening a cast, (type string, close index)."""
        toks = self.toks
        if toks[i].s != "(" or i + 1 >= len(toks) or not self.is_type_start(i + 1):
            return None
        j = self.match.get(i)
        if j is None:
            return None
        parts = []
        k = i + 1
        while k < j:
            t = toks[k]
            if t.k == "id" and (t.s in TYPE_WORDS or t.s in self.typedefs):
                parts.append(t.s)
            elif t.s == "*":
                parts.append("*")
            elif t.s in ("__typeof__", "typeof"):
                return None
            elif t.k == "id" and k > i + 1 and toks[k - 1].s in ("struct", "union", "enum"):
                parts.append(t.s)
            else:
                return None
            k += 1
        return " ".join(parts), j

    def unary_end(self, i):
        """End token index (inclusive) of the cast/unary expression at i."""
        toks = self.toks
        n = len(toks)
        if i >= n:
            return None
        t = toks[i]
        if t.k == "punct" and t.s in ("&", "*", "-", "+", "!", "~", "++", "--"):
            return self.unary_end(i + 1)
        if t.s == "sizeof":
            if i + 1 < n and toks[i + 1].s == "(":
                return self.match.get(i + 1)
            return self.unary_end(i + 1)
        if t.s == "(":
            c = self.cast_at(i)
            if c is not None:
                j = c[1]
                if j + 1 < n and toks[j + 1].s == "{":  # compound literal
                    return self.postfix_end(self.match.get(j + 1, j + 1))
                return self.unary_end(j + 1)
            j = self.match.get(i)
            if j is None:
                return None
            return self.postfix_end(j)
        if t.k in ("id", "num", "str"):
            return self.postfix_end(i)
        return None

    def postfix_end(self, j):
        toks = self.toks
        n = len(toks)
        while j + 1 < n:
            s = toks[j + 1].s
            if s in ("[", "("):
                k = self.match.get(j + 1)
                if k is None:
                    return j
                j = k
            elif s in ("->", ".") and j + 2 < n and toks[j + 2].k == "id":
                j += 2
            elif s in ("++", "--"):
                j += 1
            else:
                return j
        return j

    def postfix_start(self, j, casts=True):
        """Start of the postfix expression ending at j (inclusive); with casts,
        a cast right before a parenthesised start is part of it."""
        toks = self.toks
        while True:
            t = toks[j]
            if t.s in (")", "]"):
                k = self.match.get(j)
                if k is None:
                    return j
                if t.s == ")" and k > 0 and toks[k - 1].k == "id" and toks[k - 1].s not in (
                        "return", "sizeof", "if", "while", "switch", "for") and \
                        toks[k - 1].s not in TYPE_WORDS:
                    j = k - 1  # a call
                    continue
                if t.s == "]":
                    j = k - 1
                    continue
                # a parenthesised expression; a cast before it binds to it
                if casts and k > 0 and toks[k - 1].s == ")":
                    kk = self.match.get(k - 1)
                    if kk is not None and self.cast_at(kk) is not None:
                        return kk
                return k
            if t.k in ("id", "num", "str"):
                if j >= 2 and toks[j - 1].s in ("->", "."):
                    j -= 2
                    continue
                return j
            return j

    def text_of(self, a, b):
        return self.text[self.toks[a].a:self.toks[b].b]


def const_terms(u, j):
    """`+ N - M ...` after token j: (sum, end index) of the constant terms that
    are not scaled by what follows them."""
    toks = u.toks
    total = 0
    end = j
    seen = False
    while end + 2 < len(toks) and toks[end + 1].s in ("+", "-") and toks[end + 2].k == "num":
        v = num_value(toks[end + 2].s)
        if v is None:
            break
        nxt = toks[end + 3].s if end + 3 < len(toks) else ""
        if nxt in ("*", "/", "%", "<<", ">>", "[", "(", "."):
            break
        total += v if toks[end + 1].s == "+" else -v
        end += 2
        seen = True
    return (total, end) if seen else (None, j)


def linear(u, k, signed):
    """Terms `[+-] t [+-] t ...` from token k (with signed, toks[k] is the
    first sign; else the first term has none). A term is a constant or a
    variable scaled by one (`i * S`, `S * i`, `(e) * S`, `i << K`). Returns
    (the sum with every variable at 1, end index, whether a variable term
    was seen); end is k - 1 when nothing was read."""
    toks = u.toks
    n = len(toks)

    def operand(q):
        if q >= n:
            return None, None
        if toks[q].k == "id" and toks[q].s not in TYPE_WORDS and toks[q].s not in (
                "sizeof", "__builtin_offsetof", "__alignof__", "_Alignof"):
            e = u.postfix_end(q)
            if e is not None and e < n:
                return e, "var"
        if toks[q].s == "(" and u.match.get(q) is not None and u.cast_at(q) is None:
            return u.match[q], "var"
        if toks[q].k == "num" and num_value(toks[q].s) is not None:
            return q, "num"
        return None, None

    total = 0
    end = k - 1
    var = False
    q = k
    first = True
    while True:
        if signed or not first:
            if q + 1 >= n or toks[q].s not in ("+", "-"):
                break
            sign = 1 if toks[q].s == "+" else -1
            q += 1
        else:
            sign = 1
        first = False
        e1, k1 = operand(q)
        if e1 is None:
            break
        if e1 + 2 < n and toks[e1 + 1].s in ("*", "<<"):
            e2, k2 = operand(e1 + 2)
            if e2 is None or k1 == k2 or (toks[e1 + 1].s == "<<" and (k1, k2) != ("var", "num")):
                break
            c = num_value(toks[e1 + 2].s if k2 == "num" else toks[q].s)
            v = (1 << c) if toks[e1 + 1].s == "<<" else c
            vterm = True
            te = e2
        elif k1 == "num":
            v = num_value(toks[q].s)
            vterm = False
            te = e1
        else:
            break
        if te + 1 < n and toks[te + 1].s in ("*", "/", "%", "<<", ">>", "[", "(", ".", "->", "&",
                                              "|", "^"):
            break
        total += sign * v
        var = var or vterm
        end = te
        q = te + 1
    return total, end, var


def var_terms(u, j):
    """`+ N + i * S ...` after token j with at least one variable term: (the
    sum at 1, end index), else (None, j). A variable stride is checked at its
    first step, as ICO_RAW's index offsets are."""
    v, e, var = linear(u, j + 1, True)
    if e + 1 < len(u.toks) and u.toks[e + 1].s in ("+", "-"):
        return None, j  # a term it cannot read follows: not the whole offset
    return (v, e) if var and e > j else (None, j)


def bracket_linear(u, k):
    """`[ i * S + N ]` from the `[` at token k: (the index at i = 1, the
    `]`), or (None, k)."""
    c = u.match.get(k)
    if c is None:
        return None, k
    v, e, var = linear(u, k + 1, False)
    if not var or e != c - 1:
        return None, k
    return v, c


def bracket_const(toks, k):
    """`[ N ]`, `[ N / M ]` or `[ N * M ]` from the `[` at token k: (value, index
    of the `]`), or (None, k). The quotient form spells a byte offset as an
    element index (`q[0x500 / 4]`)."""
    n = len(toks)
    if k + 2 < n and toks[k + 1].k == "num" and toks[k + 2].s == "]":
        return num_value(toks[k + 1].s), k + 2
    if k + 4 < n and toks[k + 1].k == "num" and toks[k + 2].s in ("/", "*") and \
            toks[k + 3].k == "num" and toks[k + 4].s == "]":
        a = num_value(toks[k + 1].s)
        b = num_value(toks[k + 3].s)
        if a is None or b is None or (toks[k + 2].s == "/" and (b == 0 or a % b)):
            return None, k
        return (a // b if toks[k + 2].s == "/" else a * b), k + 4
    return None, k


BAD_PREV = {"*", "/", "%", "-", "+", "&", "!", "~", ".", "->", "sizeof", "<<", ">>", "++", "--", ")",
            "]"}


def strip_cv(s):
    s = re.sub(r"\b(const|volatile|__restrict|restrict)\b", "", s)
    return re.sub(r"\s+", " ", s).strip()


def ptr_target(typestr):
    """'Act *' -> 'Act'; 'struct GObj *' -> 'struct GObj'; else None."""
    s = strip_cv(typestr)
    if s.endswith("*") and not s.endswith("**") and "(" not in s:
        return s[:-1].strip()
    return None


# --- the records' EE layout from the offset comments ----------------------------
EE_SIZES = {"char": (1, 1), "u_char": (1, 1), "u8": (1, 1), "s8": (1, 1), "uchar": (1, 1),
            "short": (2, 2), "u_short": (2, 2), "s16": (2, 2), "u16": (2, 2),
            "int": (4, 4), "u_int": (4, 4), "s32": (4, 4), "u32": (4, 4), "float": (4, 4),
            "unsigned": (4, 4), "signed": (4, 4),
            "long": (8, 8), "u_long": (8, 8), "s64": (8, 8), "u64": (8, 8),
            "u_long128": (16, 16), "u128": (16, 16),
            "ICO_WORD": (4, 4), "ICO_WORD_PTR": (4, 4), "IosMsgWord": (4, 4), "IosMemAddr": (4, 4),
            "IntFloat": (4, 4), "sceVu0FVECTOR": (16, 16), "sceVu0IVECTOR": (16, 16),
            "sceVu0FMATRIX": (64, 16), "sceVu0IMATRIX": (64, 16), "ICO_EEW": (4, 4),
            "ICO_EEPTR": (4, 4)}


class AggParser(gla.Parser):
    """gen_layout_asserts' parser, keeping each member's tokens."""

    def parse_member(self, agg):
        while self.i < len(self.t) and self.t[self.i].kind == "comment":
            self.i += 1
        start = self.i
        super().parse_member(agg)
        if agg.members:
            agg.members[-1].toks = [t for t in self.t[start:self.i] if t.kind != "comment"]


# the libvu0 vector and matrix typedefs as their arrays
VEC_TYPEDEFS = {"sceVu0FVECTOR": ("float", [4]), "sceVu0IVECTOR": ("int", [4]),
                "sceVu0FMATRIX": ("float", [4, 4]), "sceVu0IMATRIX": ("int", [4, 4])}


def member_type(mem, name=None):
    """(base type words, pointer depth, array dims) of a member's declarator
    (the first one by default)."""
    toks = getattr(mem, "toks", [])
    if not mem.names:
        return None
    if name is None:
        name = mem.names[0]
    words = []
    ptr = 0
    dims = []
    k = 0
    while k < len(toks):
        t = toks[k]
        if t.text == name:
            k += 1
            while k + 2 < len(toks) and toks[k].text == "[":
                v = None
                if toks[k + 1].kind == "num" and toks[k + 2].text == "]":
                    v = num_value(toks[k + 1].text)
                dims.append(v)
                k += 3 if v is not None else len(toks)
            break
        if t.text == "*":
            ptr += 1
        elif t.text == "(" and words and words[-1] in ("ICO_WORD_PTR",):
            # ICO_WORD_PTR(T): a 4-byte word on the EE
            depth = 1
            k += 1
            while k < len(toks) and depth:
                if toks[k].text == "(":
                    depth += 1
                elif toks[k].text == ")":
                    depth -= 1
                k += 1
            continue
        elif t.text == "(":
            ptr += 1  # function pointer
        elif t.text == ",":
            # an earlier declarator of the same member: its stars and dims
            ptr = 0
        elif t.kind == "id" and t.text not in ("const", "volatile", "struct", "union", "enum",
                                                "__attribute__") and t.text not in mem.names:
            words.append(t.text)
        elif t.text == "[":
            # dims of an earlier declarator
            depth = 1
            k += 1
            while k < len(toks) and depth:
                if toks[k].text == "[":
                    depth += 1
                elif toks[k].text == "]":
                    depth -= 1
                k += 1
            continue
        k += 1
    if not ptr and words and words[-1] in VEC_TYPEDEFS:
        base, vd = VEC_TYPEDEFS[words[-1]]
        words = words[:-1] + [base]
        dims = dims + vd
    return words, ptr, dims


def ee_size(mtype, sizes):
    """EE (size, align) of a member type, or None."""
    words, ptr, dims = mtype
    if ptr:
        base = (4, 4)
    else:
        w = [x for x in words if x not in ("signed",)]
        key = " ".join(w)
        if key in ("unsigned char", "char"):
            base = (1, 1)
        elif key in ("unsigned short", "short", "short int", "unsigned short int"):
            base = (2, 2)
        elif key in ("unsigned int", "unsigned", "int", "long int") and "long" not in w[:-1]:
            base = (4, 4) if key != "long int" else (8, 8)
        elif key in ("long long", "unsigned long long", "long long int"):
            base = (8, 8)
        elif key in ("unsigned long", "long"):
            base = (8, 8)
        elif len(w) == 1 and w[0] in EE_SIZES:
            base = EE_SIZES[w[0]]
        elif len(w) >= 1 and w[-1] in sizes:
            base = sizes[w[-1]]
        else:
            return None
    n = 1
    for d in dims:
        if d is None:
            return None
        n *= d
    return base[0] * n, base[1]


def scalar_elem(mtype):
    """The element size of a scalar member (array of scalars), else None."""
    words, ptr, dims = mtype
    if ptr:
        return None
    key = " ".join(w for w in words if w != "signed")
    if key in SCALAR:
        return SCALAR[key]
    if key in ("unsigned long", "long"):
        return None
    if key in EE_SIZES and key not in ("ICO_WORD", "ICO_WORD_PTR", "IosMsgWord", "IosMemAddr",
                                       "ICO_EEW", "ICO_EEPTR"):
        sz, al = EE_SIZES[key]
        if key.startswith("sceVu0"):
            return 4
        return sz
    return None


class Record:
    """A record's EE fields: entries (path, ee_off, member type or None, depth, kind)."""

    def __init__(self, name, where, entries, size, cls):
        self.name = name
        self.where = where
        self.entries = entries
        self.size = size
        self.cls = cls


def build_entries(agg, sizes, base_off=0):
    """Walk an aggregate: commented members at their offsets, uncommented ones
    after the previous member at their EE size where it is known. Returns
    (entries, end offset or None)."""
    entries = []

    def walk(members, prefix, base_known, base, union):
        cur = base if base_known else None
        end_max = cur
        for mem in members:
            names = mem.names
            mtype = member_type(mem) if names else None
            if mem.agg is not None and not names:
                if mem.offset is not None:
                    o = mem.offset
                    cur = o if not (prefix and base_known and o < base) else base + o
                e = walk(mem.agg.members, prefix, cur is not None, cur or 0, mem.agg.kind == "union")
                if not union:
                    cur = e
                elif e is not None and (end_max is None or e > end_max):
                    end_max = e
                continue
            if not names or mem.bitfield:
                if mem.bitfield and names and mem.offset is None and cur is not None and \
                        cur == (base if base_known else None) and not entries:
                    # the bit-field word a record starts with
                    entries.append((prefix + names[0], cur, None, (prefix + names[0]).count("."),
                                    "bits", "layout", None))
                if mem.bitfield:
                    if mem.offset is not None and names:
                        o = mem.offset
                        if prefix and base_known and o < base:
                            o = base + o
                        entries.append((prefix + names[0], o, None, (prefix + names[0]).count("."),
                                        "bits", "comment", None))
                    cur = None if not union else cur
                continue
            path = prefix + names[0]
            off = None
            src = None
            if mem.offset is not None:
                off = mem.offset
                if prefix and base_known and off < base:
                    off = base + off
                src = "comment"
            else:
                m = gla.PAD_RE.match(names[0])
                if m and len(names) == 1 and not prefix:
                    off = int(m.group(1), 16)
                    src = "pad"
            if union:
                cur_m = base if base_known else None
            else:
                cur_m = cur
            sz = None
            if mem.agg is not None:
                o = off if off is not None else cur_m
                e = walk(mem.agg.members, path + ".", o is not None, o or 0, mem.agg.kind == "union")
                if o is not None and e is not None:
                    sz = (e - o, 4)
            else:
                sz = ee_size(mtype, sizes) if mtype else None
            if off is None and cur_m is not None and sz is not None:
                al = sz[1]
                off = (cur_m + al - 1) // al * al
                src = "layout"
            if off is not None:
                kind = "pad" if gla.PAD_RE.match(names[0]) and not prefix else "field"
                if names[0].startswith("unk") and re.match(r"^unk[0-9A-Fa-f]+$", names[0]):
                    kind = "pad"
                entries.append((path, off, mtype, path.count("."), kind, src,
                                sz[0] if sz else None))
                nxt = off + sz[0] if sz is not None else None
            else:
                nxt = None
            # `float x, y, z, w;`: the other declarators, at their EE layout
            for extra in names[1:]:
                mt2 = member_type(mem, extra)
                sz2 = ee_size(mt2, sizes) if mt2 else None
                c2 = (base if base_known else None) if union else nxt
                if c2 is None or sz2 is None:
                    nxt = None
                    break
                o2 = (c2 + sz2[1] - 1) // sz2[1] * sz2[1]
                entries.append((prefix + extra, o2, mt2, (prefix + extra).count("."), "field",
                                "layout", sz2[0]))
                nxt = o2 + sz2[0]
                if union and (end_max is None or nxt > end_max):
                    end_max = nxt
            if union:
                if nxt is not None and (end_max is None or nxt > end_max):
                    end_max = nxt
            else:
                cur = nxt
        return end_max if union else cur

    end = walk(agg.members, "", True, base_off, agg.kind == "union")
    return entries, end


class LayoutDB:
    def __init__(self):
        self.headers = {}  # alias -> Record
        self.local = {}  # (file, alias) -> Record
        self.sizes = {}
        classes, _ = gla.read_classes()
        self.classes = classes
        hdr_aggs = []
        for h in gla.headers():
            hdr_aggs.append((h, self._parse(h)))
        # EE sizes known for named records: classification size=, comment size
        for h, aggs in hdr_aggs:
            for agg in aggs:
                name, aliases = gla.type_name(agg)
                if name is None:
                    continue
                cls = None
                for a in [name] + sorted(aliases):
                    if a in classes:
                        cls = classes[a]
                        break
                size = cls["size"] if cls and cls["size"] else None
                if size is None and not (cls and cls["nosize"]):
                    size = gla.comment_size(agg)
                if size is not None:
                    for a in aliases | {name}:
                        self.sizes[a.split()[-1]] = (size, 4)
        # EE sizes of the records every member of which has a known EE size,
        # until nothing new is learnt (a record of records)
        for _ in range(6):
            new = 0
            for h, aggs in hdr_aggs:
                for agg in aggs:
                    name, aliases = gla.type_name(agg)
                    if name is None or name.split()[-1] in self.sizes:
                        continue
                    if any(m.bitfield for m in agg.members):
                        continue
                    entries, end = build_entries(agg, self.sizes)
                    named = sum(len(m.names) for m in agg.members if m.names)
                    named += sum(1 for m in agg.members if m.agg is not None and not m.names)
                    top = [e for e in entries if e[3] == 0]
                    if end is not None and len(top) >= named:
                        for a in aliases | {name}:
                            self.sizes[a.split()[-1]] = (end, 4)
                        new += 1
            if not new:
                break
        for h, aggs in hdr_aggs:
            for agg in aggs:
                self._add(self.headers, None, h, agg)
        self.cfiles = set()
        self.lock = threading.RLock()

    def _parse(self, path):
        with open(os.path.join(ROOT, path), encoding="latin-1") as f:
            text = f.read()
        toks = gla.tokenize(gla.strip_preprocessor(text))
        try:
            return AggParser(toks).parse_file()
        except Exception:
            return []

    def _add(self, table, file, where, agg):
        name, aliases = gla.type_name(agg)
        if name is None:
            return
        entries, end = build_entries(agg, self.sizes)
        # EE offsets come from comments, or from pad<HEX> names (the views
        # of other records the .c files define: `char pad0[1620]; int limit;`),
        # or, for a record without either, from the EE layout of every member
        # (all of known EE size)
        named = sum(len(m.names) for m in agg.members if m.names and not m.bitfield) + sum(
            1 for m in agg.members if m.agg is not None and not m.names)
        top = [e for e in entries if e[3] == 0]
        complete = end is not None and len(top) >= named and not any(m.bitfield for m in agg.members)
        if not any(e[5] in ("comment", "pad") for e in entries) and not complete and \
                not (entries and all(e[4] == "bits" and e[1] == 0 for e in entries)):
            return
        # a member's EE size, where its type's is not known: up to the next
        # member at its level
        fixed = []
        for e in entries:
            if e[6] is None:
                par = e[0].rsplit(".", 1)[0] if "." in e[0] else ""
                nxt = [x[1] for x in entries if x[1] > e[1] and x[3] == e[3] and
                       (x[0].rsplit(".", 1)[0] if "." in x[0] else "") == par]
                if nxt:
                    e = e[:6] + (min(nxt) - e[1],)
            fixed.append(e)
        entries = fixed
        cls = None
        for a in [name] + sorted(aliases):
            if a in self.classes:
                cls = self.classes[a]
                break
        base = cls["base"] if cls else 0
        if base:
            entries = [(p, o - base if s == "comment" else o, mt, d, k, s, sz)
                       for (p, o, mt, d, k, s, sz) in entries]
        size = cls["size"] if cls and cls["size"] else None
        if size is None:
            size = self.sizes.get(name.split()[-1], (end, 4))[0]
        rec = Record(name, "%s:%d" % (where, agg.line), entries, size, cls["class"] if cls else None)
        for a in aliases | {name}:
            key = a if file is None else (file, a)
            if key not in table:
                table[key] = rec
            if " " in a:
                k2 = a.split()[-1]
                key = k2 if file is None else (file, k2)
                table.setdefault(key, rec)

    def learn_sizes(self, aggs, table):
        """EE sizes of the records every member of which has a known EE
        size, until nothing new is learnt (a record of records)."""
        for _ in range(6):
            new = 0
            for agg in aggs:
                name, aliases = gla.type_name(agg)
                if name is None or name.split()[-1] in table:
                    continue
                if any(m.bitfield for m in agg.members):
                    continue
                sizes = dict(self.sizes)
                sizes.update(table)
                entries, end = build_entries(agg, sizes)
                named = sum(len(m.names) for m in agg.members if m.names)
                named += sum(1 for m in agg.members if m.agg is not None and not m.names)
                top = [e for e in entries if e[3] == 0]
                if end is not None and len(top) >= named:
                    for a in aliases | {name}:
                        table[a.split()[-1]] = (end, 4)
                    new += 1
            if not new:
                break

    def add_source(self, path):
        rel = os.path.relpath(path, ROOT)
        with self.lock:
            if rel in self.cfiles:
                return
            self.cfiles.add(rel)
            aggs = self._parse(rel)
            local = {}
            self.learn_sizes(aggs, local)
            saved = self.sizes
            self.sizes = dict(saved)
            self.sizes.update(local)
            try:
                for agg in aggs:
                    self._add(self.local, rel, rel, agg)
            finally:
                self.sizes = saved

    def expand(self, rec, files, depth=0):
        """The record's entries with each member of a commented record type
        replaced by that record's fields (offsets added), arrays of such
        records element by element where the element's EE size is known."""
        key = (id(rec), tuple(files))
        cache = self.__dict__.setdefault("_exp", {})
        if key in cache:
            return cache[key]
        out = list(rec.entries)
        if depth < 4:
            for (p, o, mt, d, k, src, sz) in rec.entries:
                if mt is None or mt[1] or k != "field":
                    continue
                words = mt[0]
                if not words:
                    continue
                sub = self.lookup(words[-1], files, expand=False)
                if sub is None or sub is rec:
                    continue
                subent = self.expand(sub, files, depth + 1)
                dims = mt[2]
                if dims:
                    if sub.size is None or None in dims:
                        continue
                    count = 1
                    for x in dims:
                        count *= x
                    if count > 64:
                        continue
                    idxs = []
                    for n in range(count):
                        sub_i = []
                        rest = n
                        for kk in range(len(dims)):
                            inner = 1
                            for x in dims[kk + 1:]:
                                inner *= x
                            sub_i.append(rest // inner)
                            rest %= inner
                        idxs.append(("".join("[%d]" % x for x in sub_i), n * sub.size))
                else:
                    idxs = [("", 0)]
                for suffix, eo in idxs:
                    for (p2, o2, mt2, d2, k2, s2, sz2) in subent:
                        out.append((p + suffix + "." + p2, o + eo + o2, mt2, d + d2 + 1, k2, s2, sz2))
                    if suffix:
                        out.append((p + suffix, o + eo, (words, 0, []), d + 1, "field", "layout",
                                    sub.size))
        cache[key] = out
        return out

    def lookup(self, name, files, expand=True):
        rec = self._lookup(name, files)
        if rec is None or not expand:
            return rec
        r = Record(rec.name, rec.where, self.expand(rec, files), rec.size, rec.cls)
        return r

    def _lookup(self, name, files):
        name = strip_cv(name)
        keys = [name]
        if name.startswith(("struct ", "union ")):
            keys.append(name.split()[-1])
        for f in files:
            for k in keys:
                r = self.local.get((f, k))
                if r is not None:
                    return r
        for k in keys:
            r = self.headers.get(k)
            if r is not None:
                return r
        return None


# --- sites -------------------------------------------------------------------
class Site:
    def __init__(self, unit, kind, i0, file, line, snippet):
        self.unit = unit
        self.kind = kind
        self.i0 = i0
        self.file = file
        self.line = line
        self.snippet = snippet
        self.verdict = None
        self.detail = ""
        self.operand = None  # (a, b) token span of the address expression
        self.const = 0  # bytes added to the address (the same on the EE and the host)
        self.cast = None
        self.field = None  # ico_raw: (p span, field span); pad: (base span, name, op)
        self.vtype = None  # recast: the view record's type and the member path
        self.func = None

    def key(self):
        return (self.file, self.line, self.kind, self.snippet)


def in_game(t):
    f = os.path.normpath(t.f) if os.path.isabs(t.f) else t.f
    return f.startswith(ICO2) or f.startswith("ico2/")


def scan_func_locals(u, fa, fb):
    """IDENT -> [rhs spans] of `IDENT = rhs` in a function, and the idents
    moved by ++, --, += or -=."""
    toks = u.toks
    views = {}
    bumps = set()
    for i in range(fa + 1, fb):
        t = toks[i]
        if t.k != "id":
            continue
        if toks[i - 1].s in (".", "->"):
            continue
        nx = toks[i + 1].s
        if nx == "=":
            j = i + 2
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
            if j > i + 2:
                views.setdefault(t.s, []).append((i + 2, j - 1))
        elif nx in ("++", "--", "+=", "-="):
            bumps.add(t.s)
        if toks[i - 1].s in ("++", "--"):
            bumps.add(t.s)
    return views, bumps


def find_sites(u):
    toks = u.toks
    sites = []
    n = len(toks)

    def mk(kind, i0, a, b, func):
        t = toks[i0]
        snippet = re.sub(r"\s+", " ", u.text_of(a, b))[:200]
        f = os.path.normpath(t.f) if os.path.isabs(t.f) else t.f
        s = Site(u, kind, i0, f, t.ln, snippet)
        s.func = func
        return s

    # function bodies, then the initialisers of file-scope declarations (a
    # static initialiser can hold an address computed from an EE offset)
    regions = [(fa, fb, (fa, fb)) for fa, fb in u.funcs]
    regions += [(a - 1, b + 1, None) for a, b in u.inits]
    for fa, fb, func in regions:
        if func is not None:
            has_game = any(in_game(toks[k]) for k in range(fa, min(fb, fa + 2)))
            if not has_game and not in_game(toks[fb]):
                continue
            views, bumps = scan_func_locals(u, fa, fb)
            u.locals[func] = (views, bumps)
        else:
            if not in_game(toks[fa + 1]):
                continue
            views, bumps = {}, set()
        firsts = {}
        for i in range(fa + 1, fb):
            t = toks[i]
            if not in_game(t):
                continue
            # memset / memcpy / memmove with a constant size over a record or
            # a span from a member: the size is the EE's
            if t.k == "id" and t.s in CLEAR_FUNCS and toks[i + 1].s == "(" and \
                    toks[i - 1].s not in (".", "->"):
                ptrs, si = CLEAR_FUNCS[t.s]
                args = []
                c = u.match.get(i + 1)
                depth = 0
                st = i + 2
                for k in range(i + 2, c):
                    s_ = toks[k].s
                    if s_ in "([{":
                        depth += 1
                    elif s_ in ")]}":
                        depth -= 1
                    elif s_ == "," and depth == 0:
                        args.append((st, k - 1))
                        st = k + 1
                args.append((st, c - 1))
                if len(args) > max(ptrs + (si,)):
                    n_ = const_expr(u, *args[si])
                    if n_ is not None and n_ > 0:
                        for pi in ptrs:
                            a_, b_ = strip_parens(u, *args[pi])
                            s = mk("clear", i, i, c, func)
                            s.operand = (a_, b_)
                            s.size = n_
                            s.call = t.s
                            sites.append(s)
            # ICO_RAW / ICO_RAWP markers: __ico_audit_raw ( p , off ) , [&] ( field )
            if t.s == "__ico_audit_raw" and toks[i + 1].s == "(":
                c = u.match.get(i + 1)
                args = []
                depth = 0
                st = i + 2
                for k in range(i + 2, c):
                    s = toks[k].s
                    if s in "([{":
                        depth += 1
                    elif s in ")]}":
                        depth -= 1
                    elif s == "," and depth == 0:
                        args.append((st, k - 1))
                        st = k + 1
                args.append((st, c - 1))
                k = c + 1
                if toks[k].s != ",":
                    continue
                k += 1
                amp = toks[k].s == "&"
                if amp:
                    k += 1
                if toks[k].s != "(":
                    continue
                fe = u.match[k]
                s = mk("ico_raw", i, i, fe, func)
                off = None
                if len(args) == 2:
                    txt = u.text_of(args[1][0], args[1][1]).strip("() ")
                    try:
                        off = int(txt, 0)
                    except ValueError:
                        off = None
                s.const = off
                s.off_text = u.text_of(args[1][0], args[1][1]) if len(args) == 2 else ""
                s.field = (args[0], (k + 1, fe - 1), amp)
                sites.append(s)
                continue
            # pad / unk members, named by their EE offset
            if t.k == "id" and toks[i - 1].s in ("->", ".") and toks[i - 2].s not in ("{", ",") and (
                    re.match(r"^_?pad[0-9A-Fa-f]+$", t.s) or re.match(r"^unk[0-9A-Fa-f]+$", t.s)):
                bstart = u.postfix_start(i - 2)
                s = mk("pad", i, bstart, i, func)
                s.field = ((bstart, i - 2), t.s, toks[i - 1].s)
                # the pad as a whole block (memcpy, memset, sizeof): its bytes
                # carried along, not read as a field
                s.opaque = False
                if toks[bstart - 1].s in ("(", ",") and toks[i + 1].s in (",", ")"):
                    k = bstart - 1
                    depth = 0
                    while k > fa:
                        if toks[k].s in (")", "]"):
                            depth += 1
                        elif toks[k].s in ("(", "["):
                            if depth == 0:
                                break
                            depth -= 1
                        k -= 1
                    if toks[k].s == "(" and toks[k - 1].s in ("memcpy", "memmove", "memset", "sizeof",
                                                               "__builtin_memcpy", "memcmp"):
                        s.opaque = True
                sites.append(s)
                continue
            # local views: IDENT [ N ], IDENT + N
            if t.k == "id" and t.s in views and toks[i - 1].s not in (".", "->"):
                nxt = toks[i + 1].s
                v = None
                stride = False
                if nxt == "[" and bracket_const(toks, i + 1)[0] is not None:
                    v, end = bracket_const(toks, i + 1)
                    kind = "view_index"
                elif nxt == "[" and bracket_linear(u, i + 1)[0] is not None:
                    v, end = bracket_linear(u, i + 1)
                    kind = "view_index"
                    stride = True
                elif nxt in ("+", "-") and i + 2 < n and toks[i + 2].k == "num" and \
                        toks[i - 1].s not in BAD_PREV:
                    v, end = const_terms(u, i)
                    kind = "view_plus"
                elif nxt in ("+", "-") and toks[i - 1].s not in BAD_PREV and \
                        var_terms(u, i)[0] is not None:
                    v, end = var_terms(u, i)
                    kind = "view_plus"
                    stride = True
                if v is not None:
                    s = mk(kind, i, i, end, func)
                    s.operand = (i, i)
                    s.const = v
                    s.stride = stride
                    sites.append(s)
                    continue
                # IDENT->member, IDENT a local of a record type assigned another
                # record: a view (LadderMotWork *s = (LadderMotWork *)GOBJ_ACT(self))
                if nxt == "->" and toks[i + 2].k == "id":
                    k = i + 2
                    path = []
                    while k < n:
                        if toks[k].k == "id" and (not path or path[-1] == "."):
                            path.append(toks[k].s)
                            k += 1
                        elif toks[k].s == "." and path and toks[k + 1].k == "id":
                            path.append(".")
                            k += 1
                        elif toks[k].s == "[" and toks[k + 1].k == "num" and toks[k + 2].s == "]":
                            path.append("[%d]" % num_value(toks[k + 1].s))
                            k += 3
                        else:
                            break
                    first = firsts.setdefault(t.s, i)
                    s = mk("lview", i, i, k - 1, func)
                    s.operand = (i, i)
                    s.first = first
                    s.vtype = (None, "".join(path))
                    sites.append(s)
                    continue
            if t.s != "(":
                continue
            c = u.cast_at(i)
            if c is None:
                continue
            ctype, ce = c
            if ce + 1 < n and toks[ce + 1].s == "{":
                continue
            prev = toks[i - 1].s
            ptr = ctype.endswith("*")
            base = ctype[:-1].strip() if ptr else ctype
            if ptr and base.endswith("*"):
                continue
            oe = u.unary_end(ce + 1)
            if oe is None:
                continue
            grouped = prev == "(" and not (toks[i - 2].k == "id" and toks[i - 2].s not in (
                "return", "case", "else", "do")) and toks[i - 2].s not in (")", "]") and \
                toks[oe + 1].s == ")" and u.match.get(oe + 1) == i - 1
            scalar = ptr and (base in CHARLIKE or base in SCALAR)
            # ((V *)x)->member: a record laid over another
            if ptr and not scalar and grouped and toks[oe + 2].s == "->":
                k = oe + 3
                path = []
                while k < n:
                    if toks[k].k == "id" and (not path or path[-1] in (".",)):
                        path.append(toks[k].s)
                        k += 1
                    elif toks[k].s == "." and path and toks[k + 1].k == "id":
                        path.append(".")
                        k += 1
                    elif toks[k].s == "[" and toks[k + 1].k == "num" and toks[k + 2].s == "]":
                        path.append("[%d]" % num_value(toks[k + 1].s))
                        k += 3
                    else:
                        break
                if path:
                    s = mk("recast", i, i - 1, k - 1, func)
                    s.operand = (ce + 1, oe)
                    s.vtype = (base, "".join(path))
                    s.cast = ctype
                    sites.append(s)
                continue
            kind = None
            if scalar:
                kind = "cast"
            elif not ptr and base in INTCAST:
                kind = "intcast"
            if kind is None:
                continue
            scale = 1 if (not ptr or base in CHARLIKE) else SCALAR.get(base, 1)
            # ((T *)x)[N]
            if ptr and grouped and oe + 2 < n and toks[oe + 2].s == "[" and (
                    bracket_const(toks, oe + 2)[0] is not None or
                    bracket_linear(u, oe + 2)[0] is not None):
                v, be = bracket_const(toks, oe + 2)
                stride = v is None
                if v is None:
                    v, be = bracket_linear(u, oe + 2)
                s = mk("index", i, i - 1, be, func)
                s.operand = (ce + 1, oe)
                s.const = v * scale
                s.cast = ctype
                s.stride = stride
                sites.append(s)
                continue
            if prev == "*" and scalar and (toks[i - 2].k != "id" or toks[i - 2].s in (
                    "return", "case", "else", "do")) and toks[i - 2].s not in (")", "]") and \
                    toks[i - 2].k not in ("num", "str"):
                # *(T *)x: the word at the start of what x points to
                s = mk("deref", i, i - 1, oe, func)
                s.operand = (ce + 1, oe)
                s.const = 0
                s.cast = ctype
                sites.append(s)
                continue
            if prev in BAD_PREV:
                continue
            v, end = const_terms(u, oe)
            stride = False
            if v is None:
                v, end = var_terms(u, oe)
                stride = True
            if v is None:
                continue
            s = mk(kind, i, i, end, func)
            s.operand = (ce + 1, oe)
            s.const = v * scale
            s.cast = ctype
            s.stride = stride
            sites.append(s)
    return sites


# the clears and copies whose constant size is checked against the record's
# span: name -> (pointer arguments, size argument)
CLEAR_FUNCS = {"memset": ((0,), 2), "memcpy": ((0, 1), 2), "memmove": ((0, 1), 2),
               "__builtin_memset": ((0,), 2), "__builtin_memcpy": ((0, 1), 2),
               "__builtin_memmove": ((0, 1), 2), "bzero": ((0,), 1), "bcopy": ((0, 1), 2)}


def const_expr(u, a, b):
    """The value of [a, b] when it is an integer constant expression of
    literals only (`0x38`, `16 * 4`), else None: a `sizeof`, an offsetof or
    a variable makes the size the host's own."""
    txt = []
    k = a
    while k <= b:
        t = u.toks[k]
        if t.s == "(" and u.cast_at(k) is not None:
            ct = strip_cv(u.cast_at(k)[0])
            if ct not in INTCAST and ct not in SCALAR:
                return None
            k = u.cast_at(k)[1] + 1  # (size_t)0x38: the integer cast drops out
            continue
        if t.k == "num":
            v = num_value(t.s)
            if v is None:
                return None
            txt.append(str(v))
        elif t.k == "punct" and t.s in ("+", "-", "*", "/", "(", ")", "<<", ">>", "|", "&"):
            txt.append("//" if t.s == "/" else t.s)
        else:
            return None
        k += 1
    try:
        return int(eval("".join(txt), {"__builtins__": {}}))
    except Exception:
        return None


def strip_parens(u, a, b):
    while a < b and u.toks[a].s == "(" and u.match.get(a) == b:
        a += 1
        b -= 1
    return a, b


def field_split(u, a, b, subst=None):
    """A member access expression [a, b] -> ((base span), path, op): the base is
    what the last top-level `->` (else the first `.`) applies to; None when the
    expression is not a member access or its path has a variable index."""
    toks = u.toks
    a, b = strip_parens(u, a, b)
    arrow = None
    depth = 0
    for k in range(a, b + 1):
        s = toks[k].s
        if s in "([":
            depth += 1
        elif s in ")]":
            depth -= 1
        elif depth == 0 and s == "->":
            arrow = k
    if arrow is None:
        depth = 0
        for k in range(a, b + 1):
            s = toks[k].s
            if s in "([":
                depth += 1
            elif s in ")]":
                depth -= 1
            elif depth == 0 and s == ".":
                arrow = k
                break
        if arrow is None:
            return None
    # the base must be a postfix expression ending right before the operator,
    # starting at a (a cast or a prefix operator in front means it is not)
    if u.postfix_start(arrow - 1, casts=False) != a:
        return None
    parts = []
    k = arrow + 1
    while k <= b:
        t = toks[k]
        if t.k == "id" or t.s == ".":
            parts.append(t.s)
            k += 1
        elif t.s == "[" and toks[k + 1].k == "num" and toks[k + 2].s == "]" and k + 2 <= b:
            parts.append("[%d]" % num_value(toks[k + 1].s))
            k += 3
        elif t.s == "[" and subst and toks[k + 1].s in subst and toks[k + 2].s == "]" and k + 2 <= b:
            parts.append("[%d]" % subst[toks[k + 1].s])
            k += 3
        else:
            return None
    return (a, arrow - 1), "".join(parts), toks[arrow].s


# --- probing -------------------------------------------------------------------
PROBE_PRE = "({ __auto_type __icov = ("
PROBE_POST = "); struct __icoA_%d *__icoa = __icov; (void)__icoa; __icov; })"
DIAG_RE = re.compile(r"'struct __icoA_(\d+) \*'.*?(?:from|using type) (?:incompatible pointer type )?'([^']*)'")


def run(cmd, cwd=None, inp=None):
    env = dict(os.environ, LC_ALL="C", LANG="C")
    return subprocess.run(cmd, cwd=cwd, input=inp, capture_output=True, text=True, env=env,
                          encoding="latin-1")


PROBE_PRE_ADDR = "({ __auto_type __icov = &("
PROBE_POST_ADDR = "); struct __icoA_%d *__icoa = __icov; (void)__icoa; *__icov; })"


def probe_types(u, spans, workdir, addr=False):
    """{span: type string}; a span with no diagnostic converts to a struct
    pointer silently, so it is `void *`. With addr, the type of the span's
    address (`T *(*)[2]` for an array of two pointers)."""
    pre, post = (PROBE_PRE_ADDR, PROBE_POST_ADDR) if addr else (PROBE_PRE, PROBE_POST)
    spans = sorted(set(spans))
    inserts = []
    tail = []
    for pid, (a, b) in enumerate(spans):
        if u.func_of(a) is None:
            # a file-scope initialiser: no statement expression there, so the
            # operand (which names only file-scope objects) is typed in a
            # function appended to the unit
            tail.append("void __icoT_%d(void) { struct __icoA_%d *__icoa = (%s); (void)__icoa; }" % (
                pid, pid, u.text_of(a, b)))
            continue
        ln = u.toks[b].b - u.toks[a].a
        inserts.append((u.toks[a].a, 1, -ln, pre))
        inserts.append((u.toks[b].b, 0, ln, post % pid))
    inserts.sort()
    out = []
    last = 0
    for pos, _, _, txt in inserts:
        out.append(u.text[last:pos])
        out.append(txt)
        last = pos
    out.append(u.text[last:])
    if tail:
        out.append("\n" + "\n".join(tail) + "\n")
    path = os.path.join(workdir, "%s.%d.probe.i" % (os.path.basename(u.src), threading.get_ident()))
    with open(path, "w", encoding="latin-1") as f:
        f.write("".join(out))
    r = run([u.flags["cc"], "-x", "cpp-output", "-fsyntax-only", "-fdiagnostics-plain-output",
             "-fmax-errors=0", "-Wno-error"] + u.flags["opts"] + [path])
    res = {}
    for line in r.stderr.splitlines():
        if "__icoA_" not in line:
            continue
        m = DIAG_RE.search(line)
        if m:
            res.setdefault(int(m.group(1)), m.group(2))
    return {sp: res.get(pid, "void *") for pid, sp in enumerate(spans)}


def probe_values(u, exprs, workdir):
    """{key: int} of constant expressions compiled at the end of the unit."""
    exprs = dict(exprs)
    vals = {}
    for _ in range(6):
        if not exprs:
            break
        keys = list(exprs)
        lines = ["", '# 1 "__ico_audit__"']
        for k in keys:
            lines.append("const unsigned long __icoV_%d = (unsigned long)(%s);" % (k, exprs[k]))
        path = os.path.join(workdir, "%s.%d.vals.i" % (os.path.basename(u.src), threading.get_ident()))
        with open(path, "w", encoding="latin-1") as f:
            f.write(u.text + "\n".join(lines) + "\n")
        r = run([u.flags["cc"], "-x", "cpp-output", "-S", "-O0", "-g0", "-o", "-", "-w",
                 "-fno-asynchronous-unwind-tables", "-fmax-errors=0"] + u.flags["opts"] + [path])
        if r.returncode == 0:
            cur = None
            for line in r.stdout.splitlines():
                m = re.match(r"^_?__icoV_(\d+):", line.strip())
                if m:
                    cur = int(m.group(1))
                    continue
                # the 8-byte data directive: .quad (x86-64), .xword (aarch64
                # gcc), .dword, .8byte; .long where long is 4 bytes
                m = re.match(r"^\s*\.(quad|xword|dword|long|8byte)\s+(-?\d+)", line)
                if m and cur is not None:
                    vals[cur] = int(m.group(2))
                    cur = None
                m = re.match(r"^\s*\.zero\s+8", line)
                if m and cur is not None:
                    vals[cur] = 0
                    cur = None
            break
        bad = set()
        for line in r.stderr.splitlines():
            m = re.match(r"^__ico_audit__:(\d+):", line)
            if m and "error" in line:
                idx = int(m.group(1)) - 1
                if 0 <= idx < len(keys):
                    bad.add(keys[idx])
        if not bad:
            sys.stderr.write("%s: value probe failed:\n%s\n" % (u.src, r.stderr[-2000:]))
            break
        for k in bad:
            del exprs[k]
    return vals


# --- per unit ------------------------------------------------------------------
def load_tus(build):
    with open(os.path.join(build, "compile_commands.json")) as f:
        cc = json.load(f)
    seen = {}
    for e in cc:
        src = os.path.realpath(e["file"])
        if not src.startswith(ICO2):
            continue
        args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        try:
            out = args[args.index("-o") + 1]
        except ValueError:
            out = ""
        if not re.search(r"CMakeFiles/ico_[a-z]+\.dir/", out):
            continue
        if src in seen:
            continue
        opts = []
        pp = []
        skip = False
        for x in args[1:]:
            if skip:
                skip = False
                continue
            if x in ("-o", "-c"):
                skip = True
                continue
            if x == src or os.path.realpath(os.path.join(e["directory"], x)) == src:
                continue
            if x.startswith(("-I", "-D", "-U", "-include", "-imacros", "-isystem", "-fmacro-prefix")):
                pp.append(x)
            elif x.startswith(("-O", "-g", "-W")) and not x.startswith("-Wno-error"):
                continue
            else:
                opts.append(x)
        seen[src] = {"cc": args[0], "dir": e["directory"], "pp": pp, "opts": opts}
    return seen


_GLOBAL = {}
_GLOBAL_LOCK = threading.Lock()


def global_unit(fl, workdir):
    """A unit that includes every game header (port/test/layout_asserts.c's
    list), for the probes of a record a unit only declares."""
    with _GLOBAL_LOCK:
        if "u" in _GLOBAL:
            return _GLOBAL["u"]
        incs = []
        with open(os.path.join(ROOT, "port", "test", "layout_asserts.c")) as f:
            for line in f:
                if line.startswith("#include \""):
                    incs.append(line.strip())
        path = os.path.join(workdir, "all_headers.c")
        with open(path, "w") as f:
            f.write("#include <stddef.h>\n" + "\n".join(incs) + "\n")
        u = Unit.__new__(Unit)
        u.src = path
        u.text = preprocess(path, fl, audit=False)
        u.flags = fl
        _GLOBAL["u"] = u
        return u


def preprocess(src, fl, audit=True):
    defs = ["-DICO_OFFSET_AUDIT=1"] if audit else []
    r = run([fl["cc"], "-E"] + defs + fl["pp"] + fl["opts"] + [src], cwd=fl["dir"])
    if r.returncode != 0:
        raise RuntimeError("%s: preprocessing failed:\n%s" % (src, r.stderr[-2000:]))
    return r.stdout


def find_entry(rec, path):
    for e in rec.entries:
        if e[0] == path:
            return e
    return None


def ee_offset_of(rec, path):
    """EE offset of a member path (with constant indices) from the comments:
    the path itself, else its longest listed prefix and the indices into an
    array of scalars after it."""
    if path == "":
        return 0
    e = find_entry(rec, path)
    if e is not None:
        return e[1]
    m = re.match(r"^(.*?)((?:\[\d+\])+)$", path)
    if m:
        e = find_entry(rec, m.group(1))
        if e is None or e[2] is None:
            return None
        elem = scalar_elem(e[2])
        dims = e[2][2]
        idx = [int(x) for x in re.findall(r"\[(\d+)\]", m.group(2))]
        if elem is None or len(idx) > len(dims) or None in dims:
            return None
        off = 0
        for k, x in enumerate(idx):
            inner = 1
            for d in dims[k + 1:]:
                inner *= d
            off += x * inner * elem
        return e[1] + off
    return None


def resolve(rec, N):
    """The EE field at offset N: (path, byte delta inside it, entry) or
    (None, why, None)."""
    if rec.size and N >= rec.size:
        return None, "past the record's EE size 0x%X" % rec.size, None
    exact = [e for e in rec.entries if e[1] == N]
    if exact:
        fields = [e for e in exact if e[4] == "field"]
        if fields:
            e = max(fields, key=lambda e: e[3])
            return e[0], 0, e
        if any(e[4] == "bits" for e in exact):
            if N == 0:
                # the word of the bit-fields that start the record
                return "", 0, None
            return None, "the bit-field word of %s" % exact[0][0], None
        return None, "pad %s" % exact[0][0], None
    before = [e for e in rec.entries if e[1] < N]
    if not before:
        return None, "before the first field", None
    o = max(e[1] for e in before)
    found = None
    for e in rec.entries:
        size = e[6]
        if size is None or not (e[1] <= N < e[1] + size):
            continue
        if found is None or e[3] > found[3] or (e[3] == found[3] and e[1] > found[1]):
            found = e
    if found is None:
        return None, "no field covers EE 0x%X (last at 0x%X)" % (N, o), None
    if found[4] == "pad":
        return None, "inside %s" % found[0], None
    delta = N - found[1]
    mtype = found[2]
    elem = scalar_elem(mtype) if mtype else None
    if elem is None or delta % elem:
        return found[0], delta, found
    words, ptr, dims = mtype
    if dims and None not in dims:
        subs = []
        rest = delta // elem
        for k in range(len(dims)):
            inner = 1
            for d in dims[k + 1:]:
                inner *= d
            subs.append(rest // inner)
            rest %= inner
        return found[0] + "".join("[%d]" % x for x in subs), 0, found
    return found[0], delta, found


# Untyped words whose pointee the game's macros give: GOBJ_ACT casts GObj.act
# to Act * (common/include/typedef.h), GOBJ_WORK casts Act.work to ActWork *
# (fumi/include/act-game.h).
POINTEE = {
    ("GObj", "act"): "Act",
    ("Act", "work"): "ActWork",
}


def own_type(ad, vname):
    """The address is a member whose declared type is vname (or the record
    itself is): a typed pointer to it, not a view."""
    v = strip_cv(vname).split()[-1]
    if ad.rec is not None and ad.rec.name.split()[-1] == v and not ad.path:
        return True
    if ad.path:
        e = find_entry(ad.rec, re.sub(r"\[\d+\]$", "", ad.path))
        if e is not None and e[2] is not None and e[2][0] and e[2][0][-1] == v:
            return True
    return False


class Pending(Exception):
    pass


class Addr:
    """An address: record type spelling, record, member path, bytes after it."""

    def __init__(self, tname, rec, path="", extra=0, note=""):
        self.tname = tname
        self.rec = rec
        self.path = path
        self.extra = extra
        self.note = note


class Resolver:
    def __init__(self, u, db, files):
        self.u = u
        self.db = db
        self.files = files
        self.types = {}
        self.need = set()

    def type(self, a, b):
        sp = (a, b)
        if sp in self.types:
            return self.types[sp]
        self.need.add(sp)
        raise Pending()

    def record(self, typestr, pointer=True):
        tn = ptr_target(typestr) if pointer else strip_cv(typestr)
        if tn is None or tn in SCALAR or tn in CHARLIKE or "(" in tn or "[" in tn:
            return None, tn
        return self.db.lookup(tn, self.files), tn

    def addr(self, a, b, func, depth=0, follow=False):
        """Resolve the address expression [a, b] to an Addr, or None when it is
        not a view of a record with offset comments. Raises Pending."""
        u = self.u
        toks = u.toks
        a, b = strip_parens(u, a, b)
        if depth > 10:
            return None
        # (T) x: the same address
        if toks[a].s == "(":
            c = u.cast_at(a)
            if c is not None and c[1] < b:
                ce = c[1]
                oe = u.unary_end(ce + 1)
                if oe == b:
                    return self.addr(ce + 1, b, func, depth + 1)
                if oe is not None and oe < b:
                    v, end = const_terms(u, oe)
                    if v is None or end != b:
                        v, end = var_terms(u, oe)  # a variable stride: its first step
                    if v is not None and end == b:
                        ct = c[0]
                        if ct.endswith("*"):
                            cb = ct[:-1].strip()
                            if cb in CHARLIKE:
                                sc = 1
                            elif cb in SCALAR:
                                sc = SCALAR[cb]
                            else:
                                return None
                        elif ct in INTCAST:
                            sc = 1
                        else:
                            return None
                        r = self.addr(ce + 1, oe, func, depth + 1)
                        if r is None:
                            return None
                        return Addr(r.tname, r.rec, r.path, r.extra + v * sc, r.note)
        # ICO_RAWP(T, p, off, field) in the audit's text: the field
        if toks[a].s == "__ico_audit_raw" and toks[a + 1].s == "(":
            c = u.match.get(a + 1)
            if c is not None and c + 1 < b and toks[c + 1].s == ",":
                return self.addr(c + 2, b, func, depth + 1, follow)
        # x + K: the address x views, K elements on
        oe = u.unary_end(a)
        if oe is not None and oe < b:
            v, end = const_terms(u, oe)
            if v is None or end != b:
                v, end = var_terms(u, oe)
            if v is not None and end == b:
                sc = self.elem_of(a, oe)
                if sc is None:
                    return None
                r = self.addr(a, oe, func, depth + 1, follow)
                if r is None:
                    return None
                return Addr(r.tname, r.rec, r.path, r.extra + v * sc, r.note)
        if toks[a].s == "&":
            fs = field_split(u, a + 1, b)
            ia, ib = strip_parens(u, a + 1, b)
            # &(P)[N]: N elements from what P points to
            if fs is None and toks[ib].s == "]" and u.match.get(ib) is not None:
                lb = u.match[ib]
                if lb == ib - 2 and toks[lb + 1].k == "num" and lb > ia and \
                        u.postfix_start(lb - 1, casts=False) == ia:
                    sc = self.elem_of(ia, lb - 1)
                    r = self.addr(ia, lb - 1, func, depth + 1, follow)
                    if r is None or sc is None:
                        return None
                    return Addr(r.tname, r.rec, r.path, r.extra + num_value(toks[lb + 1].s) * sc,
                                r.note)
            if fs is not None:
                (ba, bb), path, op = fs
                # &((V *)x)->m with m at the start of V: the address x is
                # (a union view such as ActStatusWord over a word pair)
                ca, cb = strip_parens(u, ba, bb)
                c = u.cast_at(ca) if toks[ca].s == "(" else None
                if op == "->" and c is not None and c[0].endswith("*") and \
                        u.unary_end(c[1] + 1) == cb:
                    vrec = self.db.lookup(c[0][:-1].strip(), self.files)
                    if vrec is not None and ee_offset_of(vrec, path) == 0:
                        r = self.addr(c[1] + 1, cb, func, depth + 1, follow)
                        if r is not None and r.rec is not None and \
                                r.rec.name.split()[-1] != vrec.name.split()[-1]:
                            return Addr(r.tname, r.rec, r.path, r.extra,
                                        "%s over it" % vrec.name)
                ty = self.type(ba, bb)
                rec, tn = self.record(ty, op == "->")
                if rec is None:
                    return None
                return Addr(tn, rec, path)
            ty = self.type(a, b)
            rec, tn = self.record(ty)
            return Addr(tn, rec) if rec is not None else None
        fs = field_split(u, a, b)
        if fs is not None:
            (ba, bb), path, op = fs
            ty = self.type(ba, bb)
            rec, tn = self.record(ty, op == "->")
            if rec is not None:
                e = find_entry(rec, path)
                if e is not None and e[2] is not None and not e[2][1] and e[2][2]:
                    return Addr(tn, rec, path, 0, "array member")
                pt = POINTEE.get((rec.name.split()[-1], path))
                if pt is not None:
                    prec = self.db.lookup(pt, self.files)
                    if prec is not None:
                        return Addr(pt, prec, "", 0, "%s.%s" % (rec.name, path))
            ty = self.type(a, b)
            rec, tn = self.record(ty)
            if rec is None:
                return self.cast_view(a, b, ty, func)
            return Addr(tn, rec)
        if a == b and toks[a].k == "id":
            ty = self.type(a, b)
            rec, tn = self.record(ty)
            if rec is not None and not follow:
                return Addr(tn, rec)
            if rec is None and tn is not None and tn not in SCALAR and tn not in CHARLIKE:
                return None
            views, bumps = u.locals.get(func, ({}, set()))
            name = toks[a].s
            if name in bumps:
                return None
            if name not in views:
                return self.param(name, func, depth) if func else None
            got = None
            for (ra, rb) in views[name]:
                r = self.addr(ra, rb, func, depth + 1)
                if r is None:
                    return None
                k = (r.tname, r.path, r.extra)
                if got is not None and (got.tname, got.path, got.extra) != k:
                    return None
                got = r
            if got is not None:
                got = Addr(got.tname, got.rec, got.path, got.extra, "view %s" % name)
            return got
        ty = self.type(a, b)
        rec, tn = self.record(ty)
        if rec is None:
            return self.cast_view(a, b, ty, func)
        return Addr(tn, rec)

    def cast_view(self, a, b, ty, func):
        """A `void *` (or byte pointer) that the function casts to exactly
        one record type with an EE layout (`(ChainNode *)w->chains[k]`): that
        record, the type the table's element is known by."""
        if func is None or ptr_target(ty) is None or strip_cv(ptr_target(ty)) not in CHARLIKE:
            return None
        u = self.u
        toks = u.toks
        # a table element or member (`sys[2]`, `w->obj`), not a call's result
        # (a call can return a different record each time: bga_objPtr)
        if any(toks[k].k == "id" and toks[k + 1].s == "(" for k in range(a, b)):
            return None
        key = (func, "".join(t.s for t in toks[a:b + 1]))
        cache = self.__dict__.setdefault("_cv", {})
        if key not in cache:
            got = set()
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
                x, y = strip_parens(u, c[1] + 1, e)
                while True:
                    cc = u.cast_at(x)
                    if cc and cc[0].endswith("*") and strip_cv(cc[0][:-1]) in CHARLIKE and \
                            u.unary_end(cc[1] + 1) == y:
                        x, y = strip_parens(u, cc[1] + 1, y)
                        continue
                    break
                if "".join(t.s for t in toks[x:y + 1]) != key[1]:
                    continue
                vt = strip_cv(c[0][:-1])
                if vt in CHARLIKE or vt in SCALAR:
                    continue
                got.add(vt)
            cache[key] = got
        got = cache[key]
        if len(got) != 1:
            return None
        vt = next(iter(got))
        rec = self.db.lookup(vt, self.files)
        if rec is None:
            return None
        return Addr(vt, rec, "", 0, "a void * cast to %s in the function" % vt)

    def param(self, name, func, depth):
        """A parameter that views a record: the record every call in the unit
        passes for it."""
        u = self.u
        toks = u.toks
        fa, fb = func
        j = fa - 1
        while j >= 0 and toks[j].s == ")" and j in u.match and \
                toks[u.match[j] - 1].s in ("__attribute__", "__asm__", "asm"):
            j = u.match[j] - 2
        if toks[j].s != ")":
            return None
        po = u.match.get(j)
        if po is None or toks[po - 1].k != "id":
            return None
        fname = toks[po - 1].s
        # the parameter's position
        idx = None
        n = 0
        depth_ = 0
        last = None
        for k in range(po + 1, j + 1):
            t = toks[k]
            if t.s in "([":
                depth_ += 1
            elif t.s in ")]" and k != j:
                depth_ -= 1
            if depth_ == 0 and (t.s == "," or k == j):
                if last == name:
                    idx = n
                n += 1
                last = None
            elif depth_ == 0 and t.k == "id":
                last = t.s
        if idx is None:
            return None
        got = None
        for k in range(len(toks) - 1):
            if toks[k].s != fname or toks[k + 1].s != "(" or k == po - 1:
                continue
            if k > 0 and toks[k - 1].s in (".", "->"):
                continue
            c = u.match.get(k + 1)
            if c is None:
                continue
            args = []
            depth_ = 0
            st = k + 2
            for q in range(k + 2, c):
                t = toks[q].s
                if t in "([{":
                    depth_ += 1
                elif t in ")]}":
                    depth_ -= 1
                elif t == "," and depth_ == 0:
                    args.append((st, q - 1))
                    st = q + 1
            args.append((st, c - 1))
            if idx >= len(args):
                return None
            cf = u.func_of(k)
            if cf is None:
                continue
            r = self.addr(args[idx][0], args[idx][1], cf, depth + 1)
            if r is None:
                return None
            key = (r.tname, r.path, r.extra)
            if got is not None and (got.tname, got.path, got.extra) != key:
                return None
            got = r
        if got is None:
            return None
        return Addr(got.tname, got.rec, got.path, got.extra, "parameter %s of %s" % (name, fname))

    def type_addr(self, a, b):
        """The type of &[a, b] (an array keeps its bounds)."""
        sp = (a, b, "addr")
        if sp in self.types:
            return self.types[sp]
        self.need.add(sp)
        raise Pending()

    def elem_of(self, a, b):
        ty = strip_cv(self.type(a, b))
        pt = ptr_target(ty)
        if pt is None:
            return 1 if ty in INTCAST or ty in ("int", "long int", "unsigned int") else None
        if pt in CHARLIKE:
            return 1
        return SCALAR.get(pt)


def audit_unit(src, fl, db, workdir):
    text = preprocess(src, fl)
    u = Unit(src, text, fl)
    u.locals = {}
    sites = find_sites(u)
    files = []
    for f in {s.file for s in sites} | {src}:
        p = os.path.realpath(f if os.path.isabs(f) else os.path.join(fl["dir"], f))
        if p.startswith(ICO2) and os.path.exists(p):
            db.add_source(p)
            files.append(os.path.relpath(p, ROOT))
    files.sort(key=lambda f: 0 if os.path.realpath(os.path.join(ROOT, f)) == src else 1)
    rv = Resolver(u, db, files)
    want = {}

    def need(expr):
        if expr not in want:
            want[expr] = len(want)
        return want[expr]

    def interpret(s):
        """Fill s.plan = (Addr of the record start view, EE offset, host access
        offset expression or None for a raw offset) or a verdict."""
        toks = u.toks
        if s.kind == "clear":
            return plan_clear(s)
        if s.kind == "pad":
            (a, b), name, op = s.field
            ty = rv.type(a, b)
            rec, tn = rv.record(ty, op == "->")
            m = re.search(r"([0-9A-Fa-f]+)$", name)
            if s.opaque:
                s.verdict = "OK"
                s.detail = "%s.%s copied or cleared as a block" % (tn, name)
                return
            if rec is None:
                s.verdict = "NOFIELD"
                s.detail = "%s.%s: a member named by its EE offset" % (tn, name)
                return
            s.verdict = "NOFIELD"
            s.detail = "%s.%s: a member named by its EE offset" % (tn, name)
            return
        if s.kind == "ico_raw":
            (pa, pb), (fa_, fb_), amp = s.field
            subst = None
            if not hasattr(s, "const0"):
                s.const0 = s.const
            s.const = s.const0
            if s.const is None:
                # an offset in one index variable (8 + i * 8): checked at i = 1
                txt = s.off_text
                ids = set(re.findall(r"[A-Za-z_]\w*", re.sub(r"\b0[xX][0-9A-Fa-f]+|\b\d+", "", txt)))
                if len(ids) == 1:
                    v = ids.pop()
                    e = re.sub(r"\b%s\b" % v, "1", txt)
                    if re.fullmatch(r"[\s\d()+*xXa-fA-F-]+", e):
                        try:
                            s.const = int(eval(e, {"__builtins__": {}}))
                            subst = {v: 1}
                        except Exception:
                            pass
            if s.const is None:
                s.verdict = "UNRESOLVED"
                s.detail = "ICO_RAW offset is not a constant"
                return
            a, b = strip_parens(u, fa_, fb_)
            # *(T *)&x->f, (T)x->f, &x->f: the field is x->f
            while True:
                if toks[a].s in ("*", "&"):
                    a += 1
                    continue
                if toks[a].s == "(":
                    c = u.cast_at(a)
                    if c is not None:
                        a = c[1] + 1
                        continue
                    if u.match.get(a) == b:
                        a, b = a + 1, b - 1
                        continue
                break
            shift = 0
            # (unsigned short)(m->flags >> 16): the upper half of the word
            if toks[b].k == "num" and toks[b - 1].s == ">>":
                v = num_value(toks[b].s)
                if v is not None and v % 8 == 0:
                    shift = v // 8
                    a, b = strip_parens(u, a, b - 2)
            fs = field_split(u, a, b, subst)
            if fs is not None and not shift and subst is None:
                (ba0, bb0), _, op0 = fs
                ca, cb = strip_parens(u, ba0, bb0)
                cc = u.cast_at(ca) if toks[ca].s == "(" else None
                if cc is not None and toks[cc[1] + 1].s == "&":
                    fs = None  # ((V *)&x->f)->m, a view over a member: the general route
            if fs is not None and shift:
                (ba, bb), path, op = fs
                ty = rv.type(ba, bb)
                rec, tn = rv.record(ty, op == "->")
                if rec is None:
                    s.verdict = "SKIP"
                    s.detail = "ICO_RAW on %s (no EE layout)" % tn
                    return
                s.addr = Addr(tn, rec)
                s.ee = s.const
                fe = ee_offset_of(rec, path)
                if fe is None or fe + shift != s.ee:
                    s.verdict = "MISMATCH"
                    s.detail = "%s: the ICO_RAW field %s >> %d is not at EE 0x%X" % (
                        tn, path, shift * 8, s.ee)
                    return
                s.host_expr = need("__builtin_offsetof(%s, %s)" % (tn, path))
                s.host_add = shift
                return
            if fs is None and subst is None:
                # the field's address through the general resolver:
                # &((float *)x->root.lookPos)[1], (T *)((char *)x + K), ...
                fspan = (fa_ - 1, fb_ + 1) if amp else (fa_, fb_)
                if amp:
                    fspan = (fa_ - 2, fb_ + 1)  # & ( field )
                fad = rv.addr(fspan[0], fspan[1], s.func, follow=True)
                if fad is None or fad.rec is None:
                    s.verdict = "UNRESOLVED"
                    s.detail = "ICO_RAW field is not a member access"
                    return
                fe = ee_offset_of(fad.rec, fad.path)
                base_ee = 0
                pad = rv.addr(pa, pb, s.func, follow=True)
                if pad is not None and pad.rec is not None and \
                        pad.rec.name.split()[-1] == fad.rec.name.split()[-1]:
                    pe = ee_offset_of(pad.rec, pad.path)
                    if pe is not None:
                        base_ee = pe + pad.extra
                if fe is None:
                    s.verdict = "UNRESOLVED"
                    s.detail = "%s.%s has no EE offset" % (fad.tname, fad.path)
                    return
                s.addr = Addr(fad.tname, fad.rec)
                s.ee = base_ee + s.const
                if fe + fad.extra != s.ee:
                    s.verdict = "MISMATCH"
                    s.detail = "%s: the ICO_RAW field is at EE 0x%X, its offset says 0x%X" % (
                        fad.tname, fe + fad.extra, s.ee)
                    return
                s.host_expr = need("__builtin_offsetof(%s, %s)" % (fad.tname, fad.path)) \
                    if fad.path else None
                s.host_add = fad.extra
                return
            if fs is None:
                s.verdict = "UNRESOLVED"
                s.detail = "ICO_RAW field is not a member access"
                return
            (ba, bb), path, op = fs
            ty = rv.type(ba, bb)
            rec, tn = rv.record(ty, op == "->")
            if rec is None:
                s.verdict = "SKIP"
                s.detail = "ICO_RAW on %s (no offset comments)" % tn
                return
            # off counts from p, which can itself point into the record
            # (char *geo = (char *)GOBJ_SUB(o) + 0xA0)
            base_ee = 0
            pad = rv.addr(pa, pb, s.func, follow=True)
            if pad is not None and pad.rec is not None and \
                    pad.rec.name.split()[-1] == rec.name.split()[-1]:
                pe = ee_offset_of(pad.rec, pad.path)
                if pe is not None:
                    base_ee = pe + pad.extra
            s.addr = Addr(tn, rec)
            s.ee = base_ee + s.const
            s.host_expr = need("__builtin_offsetof(%s, %s)" % (tn, path))
            s.host_add = 0
            return
        if s.kind in ("view_index", "view_plus"):
            a, b = s.operand
            ty = rv.type(a, b)
            rec, tn = rv.record(ty)
            if rec is not None or ptr_target(ty) is None and strip_cv(ty) not in INTCAST and \
                    strip_cv(ty) not in ("int", "long int", "unsigned int"):
                s.verdict = "SKIP"
                s.detail = "not a scalar view (%s)" % ty
                return
            elem = rv.elem_of(a, b)
            if elem is None:
                s.verdict = "SKIP"
                s.detail = "not a scalar view (%s)" % ty
                return
            ad = rv.addr(a, b, s.func)
            if ad is None:
                s.verdict = "SKIP"
                s.detail = "not a view of a record (%s)" % ty
                return
            s.addr = ad
            s.const = s.const * elem
        elif s.kind == "lview":
            ty = rv.type(s.first, s.first)
            vname = ptr_target(ty)
            if vname is None:
                s.verdict = "SKIP"
                s.detail = "not a record pointer (%s)" % ty
                return
            vrec = db.lookup(vname, files)
            a, b = s.operand
            if vrec is None:
                ad = rv.addr(a, b, s.func, follow=True)
                if ad is not None and ad.rec is not None and not own_type(ad, vname) and \
                        ad.rec.name.split()[-1] != strip_cv(vname).split()[-1]:
                    s.verdict = "UNRESOLVED"
                    s.detail = "%s, which has no EE layout, views %s" % (vname, ad.tname)
                    return
                s.verdict = "SKIP"
                s.detail = "%s has no EE layout" % vname
                return
            ad = rv.addr(a, b, s.func, follow=True)
            if ad is not None and own_type(ad, vname):
                ad = None
            if ad is None or ad.rec.name == vrec.name:
                s.verdict = "SKIP"
                s.detail = "not a view of another record"
                return
            vpath = s.vtype[1]
            ve = ee_offset_of(vrec, vpath)
            if ve is None:
                s.verdict = "UNRESOLVED"
                s.detail = "%s.%s has no EE offset" % (vname, vpath)
                return
            s.kind = "recast"
            s.vtype = (vname, vpath)
            s.addr = ad
            s.const = ve
            s.vhost = need("__builtin_offsetof(%s, %s)" % (vname, vpath))
        elif s.kind == "recast":
            a, b = s.operand
            vname, vpath = s.vtype
            vrec = db.lookup(vname, files)
            # ((V *)&p)->m[N], V a union of pointers over the pointer slot p:
            # the N-th element of what p points to
            if toks[a].s == "&" and vrec is not None:
                m = re.match(r"^(\w+)\[(\d+)\]$", vpath)
                e = find_entry(vrec, m.group(1)) if m else None
                if m and e is not None and e[2] is not None and e[2][1] == 1 and not e[2][2]:
                    key = " ".join(e[2][0])
                    if key in SCALAR or key in CHARLIKE:
                        ia, ib = strip_parens(u, a + 1, b)
                        ad = rv.addr(ia, ib, s.func)
                        if ad is not None:
                            s.kind = "slot"
                            s.addr = ad
                            s.const = int(m.group(2)) * SCALAR.get(key, 1)
                            return finish(s)
            ad = rv.addr(a, b, s.func, follow=True)
            # a record kept whole in a byte-buffer member (Act.flyClip holds a
            # ClipColReq): storage, so only its size matters
            if ad is not None and ad.path and not ad.extra:
                e = find_entry(ad.rec, ad.path)
                if e is not None and e[2] is not None and not e[2][1] and e[2][2] and \
                        scalar_elem(e[2]) == 1:
                    s.kind = "storage"
                    s.store = (need("sizeof(%s)" % vname),
                               need("sizeof(((%s *)0)->%s)" % (ad.tname, ad.path)))
                    s.verdict = "STORE"
                    s.detail = "%s kept in %s.%s" % (vname, ad.tname, ad.path)
                    return
            if ad is not None and own_type(ad, vname):
                s.verdict = "SKIP"
                s.detail = "%s is the member's own type" % vname
                return
            if ad is None:
                # ((V *)&p)->m[N]: a union of pointers over a pointer slot
                if toks[a].s == "&" and vrec is not None:
                    m = re.match(r"^(\w+)\[(\d+)\]$", vpath)
                    e = find_entry(vrec, m.group(1)) if m else None
                    if m and e is not None and e[2] is not None and e[2][1] == 1 and not e[2][2]:
                        key = " ".join(e[2][0])
                        if key in SCALAR or key in CHARLIKE:
                            ad = rv.addr(a + 1, b, s.func)
                            if ad is not None:
                                s.kind = "slot"
                                s.addr = ad
                                s.const = int(m.group(2)) * SCALAR.get(key, 1)
                                s.host_add = None
                                return finish(s)
                s.verdict = "SKIP"
                s.detail = "%s over an address that is not a record" % vname
                return
            if vrec is None:
                s.verdict = "UNRESOLVED"
                s.detail = "view %s, which has no EE layout, over %s" % (vname, ad.tname)
                return
            if vrec.name == ad.rec.name:
                s.verdict = "SKIP"
                s.detail = "%s over itself" % vname
                return
            ve = ee_offset_of(vrec, vpath)
            if ve is None:
                s.verdict = "UNRESOLVED"
                s.detail = "%s.%s has no EE offset" % (vname, vpath)
                return
            s.addr = ad
            s.const = ve
            be = find_entry(vrec, vpath)
            if be is not None and be[4] == "bits" and be[1] == 0:
                s.vhost = "zero"
            else:
                s.vhost = need("__builtin_offsetof(%s, %s)" % (vname, vpath))
        else:
            a, b = s.operand
            if s.kind == "intcast":
                ty = rv.type(a, b)
                if ptr_target(ty) is None and "*" not in ty:
                    s.verdict = "SKIP"
                    s.detail = "integer operand (%s)" % ty
                    return
                if strip_cv(s.cast) in ("int", "unsigned int", "u_int", "s32", "u32", "unsigned"):
                    s.verdict = "TRUNC"
                    s.detail = "pointer %s through (%s)" % (ty, s.cast)
                    return
            ad = rv.addr(a, b, s.func)
            if ad is None:
                s.verdict = "SKIP"
                s.detail = "not a view of a record"
                return
            s.addr = ad
        finish(s)

    def plan_clear(s):
        """A constant-size clear or copy over a record: the EE span it
        covers, and the host bounds that span must fall between (the end of
        the last member it covers, the start of the next)."""
        toks = u.toks
        a, b = s.operand
        ad = rv.addr(a, b, s.func)
        if ad is None or ad.rec is None:
            # a whole array of pointers (`WayPoint *wpA[2]`): 4 bytes an
            # element on the EE
            x, y = a, b
            while True:
                x, y = strip_parens(u, x, y)
                c = u.cast_at(x)
                if c is not None and u.unary_end(c[1] + 1) == y:
                    x = c[1] + 1
                    continue
                break
            if toks[x].s == "&" and y == x + 1:
                x += 1
            if x == y and toks[x].k == "id":
                at = rv.type_addr(x, y)
                m = re.match(r"^(.*\*)\s*\(\*\)((?:\[\d+\])+)$", strip_cv(at))
                if m:
                    n = 1
                    for d in re.findall(r"\[(\d+)\]", m.group(2)):
                        n *= int(d)
                    s.ee = 0
                    s.addr = Addr(m.group(1) + " " + m.group(2), None)
                    s.arr = (n, toks[x].s, m.group(1).strip())
                    s.span_start = None
                    s.span_lo = s.span_hi = need("%d * sizeof(void *)" % n)
                    s.span_what = "%s, %d pointers (%d bytes on the EE)" % (toks[x].s, n, 4 * n)
                    s.verdict = "SPAN" if s.size == 4 * n else "SKIP"
                    if s.verdict == "SKIP":
                        s.detail = "%s of %d bytes over %s, %d pointers: not the EE size" % (
                            s.call, s.size, toks[x].s, n)
                    return
            s.verdict = "SKIP"
            s.detail = "%s of %d bytes: not a view of a record with an EE layout (%s)" % (
                s.call, s.size, rv.type(a, b))
            return
        rec, T = ad.rec, ad.tname
        base = ee_offset_of(rec, ad.path)
        if base is None:
            s.verdict = "SKIP"
            s.detail = "%s.%s has no EE offset" % (T, ad.path)
            return
        ee0 = base + ad.extra
        ee1 = ee0 + s.size
        s.ee = ee0
        s.addr = ad
        s.span_start = need("__builtin_offsetof(%s, %s)" % (T, ad.path)) if ad.path else None
        size = rec.size
        if size and not ad.path and not ad.extra and ee1 > size and s.size % size == 0:
            n = s.size // size
            s.span_lo = need("%d * sizeof(%s)" % (n, T))
            s.span_hi = s.span_lo
            s.span_what = "%d %s records" % (n, T)
            s.verdict = "SPAN"
            return
        if size and ee1 > size:
            s.verdict = "NOFIELD"
            s.detail = "%s of 0x%X bytes from %s EE 0x%X: past the record's EE size 0x%X" % (
                s.call, s.size, T, ee0, size)
            return
        ents = [e for e in rec.entries if e[4] in ("field", "pad")]
        last = [e for e in ents if e[6] and e[1] < ee1 and e[1] + e[6] == ee1 and e[1] >= ee0]
        nxt = [e for e in ents if e[1] == ee1]
        if last:
            f = min(last, key=lambda e: e[3])
            s.span_lo = need("__builtin_offsetof(%s, %s) + sizeof(((%s *)0)->%s)" % (T, f[0], T, f[0]))
            s.span_what = "to the end of %s" % f[0]
            if size and ee1 == size:
                s.span_hi = need("sizeof(%s)" % T)
            elif nxt:
                g = min(nxt, key=lambda e: e[3])
                s.span_hi = need("__builtin_offsetof(%s, %s)" % (T, g[0]))
                s.span_what += ", before %s" % g[0]
            else:
                s.span_hi = None
            s.verdict = "SPAN"
            return
        tgt = resolve(rec, ee1)
        if tgt[0] and tgt[1] == 0:
            s.span_lo = need("__builtin_offsetof(%s, %s)" % (T, tgt[0]))
            s.span_hi = s.span_lo
            s.span_what = "to %s" % tgt[0]
            s.verdict = "SPAN"
            return
        # the end inside a member (a pad, a doubleword): the same byte on the
        # host when the member's host size is its EE size
        inside = [e for e in ents if e[6] and e[1] < ee1 < e[1] + e[6]]
        if inside:
            f = max(inside, key=lambda e: e[3])
            d = ee1 - f[1]
            s.span_lo = need("__builtin_offsetof(%s, %s) + %d" % (T, f[0], d))
            s.span_hi = s.span_lo
            s.span_guard = (need("sizeof(((%s *)0)->%s)" % (T, f[0])), f[6])
            s.span_what = "to %s+0x%X" % (f[0], d)
            s.verdict = "SPAN"
            return
        s.verdict = "UNRESOLVED"
        s.detail = "%s of 0x%X bytes from %s EE 0x%X ends inside a member (EE 0x%X: %s)" % (
            s.call, s.size, T, ee0, ee1, tgt[1] if tgt[0] is None else tgt[0])

    def finish(s):
        ad = s.addr
        if ad.rec is None:
            s.verdict = "SKIP"
            s.detail = "record %s has no offset comments" % ad.tname
            return
        base_ee = ee_offset_of(ad.rec, ad.path)
        if base_ee is None:
            s.verdict = "UNRESOLVED"
            s.detail = "%s.%s has no EE offset" % (ad.tname, ad.path)
            return
        s.ee = base_ee + ad.extra + s.const
        s.host_expr = need("__builtin_offsetof(%s, %s)" % (ad.tname, ad.path)) if ad.path else None
        s.host_add = ad.extra + (s.const if s.kind != "recast" else 0)

    todo = [s for s in sites]
    for rnd in range(16):
        rv.need = set()
        later = []
        for s in todo:
            try:
                interpret(s)
            except Pending:
                s.verdict = None
                later.append(s)
        if not later:
            break
        new = {sp for sp in rv.need if sp not in rv.types}
        if not new:
            break
        plain = {sp for sp in new if len(sp) == 2}
        if plain:
            rv.types.update(probe_types(u, plain, workdir))
        addrs = {sp[:2] for sp in new if len(sp) == 3}
        if addrs:
            rv.types.update({sp + ("addr",): t for sp, t in probe_types(u, addrs, workdir, True).items()})
        todo = later
    for s in sites:
        if s.verdict is None and not hasattr(s, "ee"):
            s.verdict = "UNRESOLVED"
            s.detail = "the operand's type was not found"
    # the named fields at the EE offsets
    for s in sites:
        if s.verdict is not None:
            continue
        s.elem = 0
        ee = s.ee
        rsz = s.addr.rec.size
        # past the record, from its start: an array of records (EE stride)
        if rsz and ee >= rsz and not s.addr.path and s.kind != "ico_raw":
            s.elem = ee // rsz
            ee = ee % rsz
            s.elem_size = need("sizeof(%s)" % s.addr.tname)
        tgt = resolve(s.addr.rec, ee)
        if tgt[0] is None:
            s.verdict = "NOFIELD"
            s.detail = "%s EE 0x%X: %s" % (s.addr.tname, s.ee, tgt[1])
            continue
        s.target = tgt
        if tgt[0] == "":
            s.tgt_expr = None
            s.tgt_size = None
            continue
        s.tgt_expr = need("__builtin_offsetof(%s, %s)" % (s.addr.tname, tgt[0]))
        s.tgt_size = need("sizeof(((%s *)0)->%s)" % (s.addr.tname, tgt[0]))
    if want:
        plain = Unit.__new__(Unit)
        plain.src = src
        plain.text = preprocess(src, fl, audit=False)
        plain.flags = fl
        got = probe_values(plain, {v: k for k, v in want.items()}, workdir)
        miss = {v: k for k, v in want.items() if v not in got}
        if miss:
            got.update(probe_values(global_unit(fl, workdir), miss, workdir))
    else:
        got = {}
    for s in sites:
        if s.verdict != "SPAN":
            continue
        T = s.addr.tname
        st = 0 if s.span_start is None else got.get(s.span_start)
        lo = got.get(s.span_lo)
        hi = got.get(s.span_hi) if s.span_hi is not None else None
        if getattr(s, "arr", None):
            what = "%s of 0x%X bytes over %s" % (s.call, s.size, s.span_what)
        else:
            what = "%s of 0x%X bytes from %s%s (EE 0x%X..0x%X, %s)" % (
                s.call, s.size, T, "." + s.addr.path if s.addr.path else "", s.ee, s.ee + s.size,
                s.span_what)
        if st is None or lo is None or (s.span_hi is not None and hi is None):
            s.verdict = "UNRESOLVED"
            s.detail = what + ": the host probe failed"
            continue
        st += s.addr.extra
        end = st + s.size
        g = getattr(s, "span_guard", None)
        if g is not None and got.get(g[0]) != g[1]:
            s.verdict = "UNRESOLVED"
            s.detail = what + ": the member it ends in is 0x%s bytes on the host, 0x%X on the EE" % (
                "%X" % got[g[0]] if got.get(g[0]) is not None else "?", g[1])
            continue
        if end < lo:
            s.verdict = "PARTIAL"
            s.detail = what + ": the host span is 0x%X bytes" % (lo - st)
        elif hi is not None and end > hi:
            s.verdict = "OVERRUN"
            s.detail = what + ": the host span is 0x%X bytes" % (hi - st)
        else:
            s.verdict = "OK"
            s.detail = what
    for s in sites:
        if s.verdict == "STORE":
            vs, ms = (got.get(x) for x in s.store)
            if vs is None or ms is None:
                s.verdict = "UNRESOLVED"
                s.detail += ": the size probe failed"
            elif vs <= ms:
                s.verdict = "OK"
                s.detail += " (0x%X of 0x%X bytes)" % (vs, ms)
            else:
                s.verdict = "MISMATCH"
                s.detail += ": 0x%X bytes in 0x%X" % (vs, ms)
    for s in sites:
        if s.verdict is not None:
            continue
        T = s.addr.tname
        if s.kind == "ico_raw":
            host = got.get(s.host_expr) if s.host_expr is not None else 0
            if host is not None:
                host += getattr(s, "host_add", 0) or 0
        else:
            hb = 0 if s.host_expr is None else got.get(s.host_expr)
            host = None if hb is None else hb + s.host_add
            if s.kind == "recast" and host is not None:
                vh = 0 if s.vhost == "zero" else got.get(s.vhost)
                host = None if vh is None else host + vh
        tg = 0 if s.tgt_expr is None else got.get(s.tgt_expr)
        path, delta, ent = s.target
        if host is None or tg is None:
            s.verdict = "UNRESOLVED"
            s.detail = "%s: the host probe failed (%s)" % (T, path)
            continue
        name = path + ("+0x%X" % delta if delta else "")
        if delta:
            sz = got.get(s.tgt_size)
            if sz is not None and delta >= sz:
                s.verdict = "NOFIELD"
                s.detail = "%s EE 0x%X: past %s on the host" % (T, s.ee, name)
                continue
            # inside a member that is not an array of scalars: the same bytes
            # only when the member's host size is its EE size
            if ent is not None and ent[6] is not None and sz is not None and sz != ent[6]:
                s.verdict = "UNRESOLVED"
                s.detail = "%s EE 0x%X: inside %s, whose host size 0x%X is not its EE size 0x%X" % (
                    T, s.ee, path, sz, ent[6])
                continue
        tg += delta
        if s.elem:
            es = got.get(s.elem_size)
            if es is None:
                s.verdict = "UNRESOLVED"
                s.detail = "%s: the host probe failed (sizeof)" % T
                continue
            tg += s.elem * es
            name = "[%d].%s" % (s.elem, name)
        s.host = host
        s.host_target = tg
        via = (" via " + s.addr.note) if s.addr.note else ""
        if getattr(s, "stride", False):
            via += " (a variable stride, at its first step)"
        acc = None
        if s.kind in ("index", "deref") and s.cast and s.cast.endswith("*"):
            acc = SCALAR.get(s.cast[:-1].strip())
        hsz = got.get(s.tgt_size)
        if host == tg and acc and not delta and ent is not None and ent[6] == acc and \
                hsz is not None and hsz != acc:
            # the EE word is a pointer-wide field on the host
            s.verdict = "MISMATCH"
            s.detail = "%s EE 0x%X = %s: a %d-byte access to a %d-byte host field%s" % (
                T, s.ee, name, acc, hsz, via)
        elif host == tg:
            s.verdict = "OK"
            s.detail = "%s EE 0x%X = %s (host 0x%X)%s" % (T, s.ee, name, tg, via)
        else:
            s.verdict = "MISMATCH"
            s.detail = "%s EE 0x%X = %s: host 0x%X, the access uses 0x%X%s" % (
                T, s.ee, name, tg, host, via)
    if UNPROTO_PASS[0]:
        sites.extend(unproto_sites(u, workdir))
    return sites


UNPROTO_PASS = [None]  # set by main: the -std= of the unprototyped-call pass




# --- Pointer-wide values held in 32 bits -----------------------------------
# A pointer, an ICO_WORD (intptr_t) or a pointer-wide integer converted to
# int, unsigned int or narrower loses its upper 32 bits on x64; on the EE
# both are 32 bits wide, so nothing in the source marks it (BoxWork.colData). gcc's early SSA dump (-fdump-tree-ssa-lineno) spells
# every conversion, implicit or cast, with its operand's type; on an LP64
# host ICO_WORD is `long int`, the EE's doublewords `long long int`, so the
# two do not mix. Reported: a conversion to a 32-bit-or-narrower integer of
# a pointer, or of a `long` that a load, a call or a pointer cast produced.
# Differences, shifts, masks and divisions of addresses are quantities
# (a size, a quadword count) and are not reported.

NARROW_DST = ("int", "unsigned int", "short int", "short unsigned int", "char",
              "unsigned char", "signed char")
NARROW_WIDE = re.compile(r"^(long (unsigned )?int|.*\*)$")
NARROW_DECL = re.compile(r"^  (.+?) ([A-Za-z_.][A-Za-z0-9_.]*)(\[.*\])?;$")
NARROW_DEF = re.compile(r"^\s*(?:\[[^\]]+\] )+([A-Za-z_.][A-Za-z0-9_.]*) = (.*);$")
NARROW_CONV = re.compile(r"^\s*\[([^\]]+?)(?: discrim \d+)?\] (?:\[[^\]]+\] )*[^=]+ = \(([^()]+)\) "
                         r"([A-Za-z_.][A-Za-z0-9_.]*)(?:\(D\))?;$")
NARROW_LOC = re.compile(r"\[[^\]]*\] ")
# reviewed: (file, the source line stripped) -> why it is a 32-bit quantity
NARROW_OK = {
    ("ico2/common/src/debug.c", "return (int)n;"): "strtol of the start-stage number, 1..105",
    ("ico2/fumi/ios/thread.c", "return buf[0];"): "the join message, an int iosThreadMessage sent",
    ("ico2/seki/src/DisplayList.c", "unsigned int end = (unsigned int)entry->cur;"):
        "debug quadword count: only the difference of the two words is used",
    ("ico2/seki/src/DisplayList.c", "unsigned int start = (unsigned int)entry->tag;"):
        "debug quadword count: only the difference of the two words is used",
    ("ico2/sugipon/src/a_p_1.c", "p->layout = arg->obj;"): "SceneObj.obj holds a spiderDef row here",
    ("ico2/sugipon/src/box.c", None): "SceneObj.obj holds a layout kind here",
    ("ico2/sugipon/src/enemy.c", "int kind = param->obj;"): "SceneObj.obj holds an enemy kind here",
    ("ico2/sugipon/src/girl.c", "kind = csv->obj;"): "SceneObj.obj holds a kind here",
    ("ico2/sugipon/src/rotObject.c", "p->kind = src->obj;"): "SceneObj.obj holds a kind here",
    ("ico2/sugipon/src/spider.c", "k = lay->obj;"): "SceneObj.obj holds a spiderDef row here",
    ("ico2/sugipon/src/stormTest.c", "int v = lay->obj;"): "SceneObj.obj holds a number here",
    ("ico2/sugipon/src/torch.c", "p->flags = lay->obj & ~1;"): "SceneObj.obj holds flags here",
    ("ico2/sugipon/src/weapon.c", None): "SceneObj.obj holds a weapon kind here",
    ("ico2/seki/src/Packet.c", None):
        "a packet address minus a tag address, a quadword count (PacAddr is pointer-wide)",
}


def narrow_ok(rel, srcline):
    for (f, text), why in NARROW_OK.items():
        if f == rel and (text is None or text == srcline):
            if text is None and "->obj" not in srcline and "cursor.addr" not in srcline:
                continue
            return why
    return None



# --- Calls through unprototyped declarations ------------------------------
# A declaration `T f()` or `T (*fn)()` takes any arguments: the call passes
# them after the default promotions, a pointer as 8 bytes, an int as 4, a
# float as a double. On the EE an int and a pointer are the same 4 bytes, so a
# callee that reads an int where the caller passed a pointer (or the reverse)
# worked there and reads the wrong bytes here. C23 reads `()` as `(void)`, so
# compiling the unit as gnu23 makes every such call with arguments an error
# ("too many arguments"). A call whose arguments are all int-sized integers
# cannot pass a pointer or a float and is OK; any other must be in
# UNPROTO_OK, which says what the callees read, or it is UNPROTO.
UNPROTO_RE = re.compile(r"^(.+?):(\d+):\d+: error: too many arguments to function '([^']+)'")
# reviewed: (file, the called expression) -> what the callees read
UNPROTO_OK = {
    ("ico2/common/src/debug.c", "fn"):
        "the memory-card menu passes &mc: debug_mcLoadMainBlock, SaveMainBlock and DeleteFile read "
        "the McMgr *, debug_mcFormat and debug_mcUnformat take an int port they never read, "
        "debug_mcTest takes nothing",
    ("ico2/fumi/ios/mcard.c", "e->save"):
        "iOSMcSaveList's writers take (McMgr *, const IconFile * or void *) or (McMgr *): pointers",
    ("ico2/fumi/ios/mcard.c", "e->load"):
        "iOSMcSaveList's readers take (McMgr *) or (McMgr *, void *): pointers",
    ("ico2/fumi/ios/thread.c", "obj->func"):
        "a thread's body gets its void * argument (a process's GObj): the bodies read a pointer or "
        "an ICO_WORD; those declared with an int (subBoyBrainMain, subAP1Control, actSt07aChanWay1/2, "
        "actSt07aGirlWay, actSt10rGirlWay, the IntrMail motion slots' int) never read it",
    ("ico2/fumi/isys/obj_manager.c", "p->func"):
        "a thread-less process's body gets its GObj: the same bodies as obj->func in thread.c",
    ("ico2/fumi/sound/s_init.c", "self->proc"):
        "the stageSEProc.c procs take the slot as a SeSlot *, an int * or an ICO_WORD, or nothing",
}


def unproto_sites(u, workdir):
    """UNPROTO / OK sites of the unit's calls through `()` declarations."""
    path = os.path.join(workdir, "%s.%d.c23.i" % (os.path.basename(u.src), threading.get_ident()))
    with open(path, "w", encoding="latin-1") as f:
        f.write(u.text)
    opts = [UNPROTO_PASS[0] if o.startswith("-std=") else o for o in u.flags["opts"]]
    if UNPROTO_PASS[0] not in opts:
        opts.append(UNPROTO_PASS[0])
    r = run([u.flags["cc"], "-x", "cpp-output", "-fsyntax-only", "-w", "-fmax-errors=0"] + opts + [path])
    toks = u.toks
    calls = []
    seen = set()
    for line in r.stderr.splitlines():
        m = UNPROTO_RE.match(line)
        if not m:
            continue
        f, ln, callee = m.group(1), int(m.group(2)), m.group(3)
        if callee == "__ico_audit_raw":
            continue  # the audit's own ICO_RAW marker (ee_view.h)
        key = (f, ln, callee)
        if key in seen:
            continue
        seen.add(key)
        last = re.findall(r"[A-Za-z_]\w*", callee)[-1]
        for k, t in enumerate(toks):
            if t.ln == ln and t.f == f and t.s == last and toks[k + 1].s == "(":
                rel = os.path.relpath(os.path.realpath(os.path.join(u.flags["dir"], f)), ROOT)
                calls.append((rel, ln, callee, k))
                break
    if not calls:
        return []
    spans = {}
    for rel, ln, callee, k in calls:
        c = u.match[k + 1]
        args = []
        st = k + 2
        depth = 0
        for q in range(k + 2, c):
            s_ = toks[q].s
            if s_ in "([{":
                depth += 1
            elif s_ in ")]}":
                depth -= 1
            elif s_ == "," and depth == 0:
                args.append((st, q - 1))
                st = q + 1
        args.append((st, c - 1))
        spans[k] = args
    types = probe_types(u, [sp for a in spans.values() for sp in a], workdir)
    out = []
    for rel, ln, callee, k in calls:
        tys = [strip_cv(types[sp]) for sp in spans[k]]
        risky = [t for t in tys if "*" in t or "[" in t or t in ("float", "double") or
                 t in INTCAST and t not in ("int", "unsigned int", "u_int", "unsigned", "s32", "u32")]
        snippet = re.sub(r"\s+", " ", u.text_of(k, u.match[k + 1]))[:160]
        s = Site(None, "unproto", 0, os.path.join(ROOT, rel), ln, snippet)
        why = UNPROTO_OK.get((rel, callee))
        if not risky:
            s.verdict = "OK"
            s.detail = "%s() through a () declaration: int arguments only (%s)" % (callee, ", ".join(tys))
        elif why:
            s.verdict = "OK"
            s.detail = "%s(%s) through a () declaration; %s" % (callee, ", ".join(tys), why)
            s.reviewed = (rel, callee)
        else:
            s.verdict = "UNPROTO"
            s.detail = "%s(%s) through a () declaration: a pointer, long or float argument the callee " \
                       "may read with another width" % (callee, ", ".join(tys))
        out.append(s)
    return out


def gnu23(tus):
    """The -std= under which the build's gcc reads `()` as `(void)` and says
    so in the diagnostic the pass reads (gnu23, gcc 13's gnu2x), else None."""
    for fl in tus.values():
        for std in ("-std=gnu23", "-std=gnu2x"):
            r = run([fl["cc"], std, "-dM", "-E", "-x", "c", "/dev/null"])
            if r.returncode != 0 or "__clang__" in r.stdout or "#define __GNUC__" not in r.stdout:
                continue
            r = run([fl["cc"], std, "-fsyntax-only", "-x", "c", "-"],
                    inp="void f();\nvoid g(void) { f(1); }\n")
            if any(UNPROTO_RE.match(x.replace("<stdin>", "s.c")) for x in r.stderr.splitlines()):
                return std
        return None
    return None


def lp64(tus):
    """True when the build's compiler has a 64-bit long (ICO_WORD is `long`)."""
    for fl in tus.values():
        r = run([fl["cc"], "-dM", "-E", "-x", "c", "/dev/null"])
        return "#define __SIZEOF_LONG__ 8" in r.stdout and \
            "#define __INTPTR_TYPE__ long int" in r.stdout
    return False


def narrow_unit(src, fl, workdir):
    """The NARROW sites of one unit (see above)."""
    sub = tempfile.mkdtemp(prefix="narrow.", dir=workdir)
    r = run([fl["cc"], "-O1", "-fdump-tree-ssa-lineno", "-c", "-o", os.path.join(sub, "u.o"),
             "-dumpdir", sub + os.sep] + fl["pp"] + fl["opts"] + [src], cwd=fl["dir"])
    if r.returncode != 0:
        raise RuntimeError("%s: the narrowing pass did not compile:\n%s" % (src, r.stderr[-2000:]))
    sites = []
    lines_of = {}
    for name in os.listdir(sub):
        if not name.endswith(".ssa"):
            continue
        types, defs = {}, {}
        with open(os.path.join(sub, name), encoding="latin-1") as f:
            for line in f:
                if line and not line.startswith(" ") and "(" in line:
                    types, defs = {}, {}
                    continue
                m = NARROW_DECL.match(line)
                if m and "=" not in line:
                    types[m.group(2)] = m.group(1).strip()
                    continue
                m = NARROW_DEF.match(line)
                if m:
                    defs[m.group(1)] = NARROW_LOC.sub("", m.group(2))
                m = NARROW_CONV.match(line)
                if not m:
                    continue
                loc, dst, opnd = m.group(1), m.group(2).strip(), m.group(3)
                if dst not in NARROW_DST:
                    continue
                st = types.get(opnd) or types.get(re.sub(r"_\d+$", "", opnd))
                if st is None or not NARROW_WIDE.match(st):
                    continue
                if not st.endswith("*"):
                    d = defs.get(opnd, "")
                    ptrcast = re.match(r"^\((long (unsigned )?int)\) ([A-Za-z_.][A-Za-z0-9_.]*)$", d)
                    if ptrcast:
                        t2 = types.get(ptrcast.group(3)) or types.get(
                            re.sub(r"_\d+$", "", ptrcast.group(3)))
                        if not (t2 or "").endswith("*"):
                            continue
                    elif not re.match(r"^([A-Za-z_][\w.]*(\(D\))?(->|\.)[\w.\[\]>-]+|\*[\w.]+|"
                                      r"[A-Za-z_][\w.]*|[A-Za-z_]\w* \(.*\))$", d) or \
                            re.match(r"^\d+$", d):
                        continue
                    if st == "long unsigned int" and re.match(r"^[A-Za-z_]\w* \(", d):
                        continue  # a size_t result (strlen): a count
                fm = re.match(r"(.+):(\d+):\d+$", loc)
                if not fm:
                    continue
                path, ln = os.path.realpath(os.path.join(fl["dir"], fm.group(1))), int(fm.group(2))
                if not path.startswith(ICO2):
                    continue
                rel = os.path.relpath(path, ROOT)
                if path not in lines_of:
                    with open(path, encoding="latin-1") as g:
                        lines_of[path] = g.read().split("\n")
                text = lines_of[path][ln - 1].strip() if ln <= len(lines_of[path]) else ""
                s = Site(None, "narrow", 0, path, ln, text)
                why = narrow_ok(rel, text)
                s.verdict = "OK" if why else "NARROW"
                s.detail = "(%s) of %s %s%s" % (dst, st, opnd, ("; " + why) if why else "")
                sites.append(s)
    return sites


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", required=True, help="a configured build folder (compile_commands.json)")
    ap.add_argument("--all", action="store_true", help="list every site")
    ap.add_argument("--skipped", action="store_true", help="also list the skipped sites")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--dump", help="print a record's EE fields as the audit reads them")
    ap.add_argument("--no-narrow", action="store_true",
                    help="skip the pointer-to-32-bit conversion pass (X4)")
    args = ap.parse_args()
    if args.dump:
        db = LayoutDB()
        r = db.lookup(args.dump, [])
        if r is None:
            print("no record %s" % args.dump)
            return 2
        print("%s %s size=%s" % (r.name, r.where, r.size))
        for e in sorted(r.entries, key=lambda e: (e[1], e[3])):
            print("  0x%04X %-6s %-7s size=%s %s" % (e[1], e[4], e[5], e[6], e[0]))
        return 0
    tus = load_tus(args.build)
    if args.files:
        want = {os.path.realpath(f) for f in args.files}
        tus = {k: v for k, v in tus.items() if k in want}
    db = LayoutDB()
    results = []
    # the narrowing pass needs an LP64 host: there ICO_WORD is `long`
    # (port/test/CMakeLists.txt runs it on the native Linux build)
    narrow = not args.no_narrow and lp64(tus)
    UNPROTO_PASS[0] = gnu23(tus)
    os.makedirs(os.path.join(args.build, "offset_audit"), exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="work.", dir=os.path.join(args.build, "offset_audit")) as work:
        _GLOBAL.clear()
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as ex:
            futs = {ex.submit(audit_unit, src, fl, db, work): src for src, fl in sorted(tus.items())}
            if narrow:
                futs.update({ex.submit(narrow_unit, src, fl, work): src
                             for src, fl in sorted(tus.items())})
            for f in concurrent.futures.as_completed(futs):
                results.extend(f.result())
    seen = {}
    for s in results:
        k = s.key()
        if k not in seen or (seen[k].verdict in ("SKIP", "UNRESOLVED") and s.verdict not in (
                "SKIP", "UNRESOLVED")):
            seen[k] = s
    sites = sorted(seen.values(), key=lambda s: (s.file, s.line, s.snippet))
    counts = {}
    for s in sites:
        counts[s.verdict] = counts.get(s.verdict, 0) + 1
    bad = ("MISMATCH", "NOFIELD", "TRUNC", "UNRESOLVED", "NARROW", "PARTIAL", "OVERRUN", "UNPROTO")
    for s in sites:
        if s.verdict in bad or args.all or (args.skipped and s.verdict == "SKIP"):
            print("%-10s %s:%d [%s] %s\n           %s" % (s.verdict, os.path.relpath(s.file, ROOT)
                                                       if os.path.isabs(s.file) else s.file, s.line,
                                                       s.kind, s.detail, s.snippet))
    kinds = {}
    for s in sites:
        kinds[s.kind] = kinds.get(s.kind, 0) + 1
    print("offset_audit: %d units, %d sites (%s)" % (
        len(tus), len(sites), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    print("offset_audit: " + ", ".join("%s %d" % (k, counts.get(k, 0)) for k in
                                       ("OK", "MISMATCH", "NOFIELD", "TRUNC", "UNRESOLVED", "NARROW",
                                        "PARTIAL", "OVERRUN", "UNPROTO", "SKIP")))
    if not narrow:
        print("offset_audit: the narrowing pass did not run (not an LP64 host, or --no-narrow)")
    if not UNPROTO_PASS[0]:
        print("offset_audit: the unprototyped-call pass did not run (needs gcc 13 or later)")
    elif not args.files:
        used = {getattr(s, "reviewed", None) for s in sites}
        stale = [k for k in UNPROTO_OK if k not in used]
        for k in stale:
            print("STALE      %s: UNPROTO_OK entry %r matches no call" % (k[0], k[1]))
        if stale:
            return 1
    return 1 if any(counts.get(k, 0) for k in bad) else 0


if __name__ == "__main__":
    sys.exit(main())
