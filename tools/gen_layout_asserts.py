#!/usr/bin/env python3
"""Generate port/test/layout_asserts.c: the game's struct layouts as asserts.

The game's headers (ico2/*/include/*.h) carry the offsets the reconstruction
recovered from the ROM as `/* 0xNN */` comments on the struct members. This
script reads every header, finds each struct or union with at least one such
comment, and writes one `_Static_assert(offsetof(T, m) == 0xNN)` per commented
member, plus one per `pad<HEX>` member (the padding names carry their offset),
and a `sizeof` assert where a size is known: from config/struct_classes.txt
(`size=`), or from the struct's leading comment ("the 64-byte node",
"the 0x108 bytes at ...").

Every struct the asserts touch needs a line in config/struct_classes.txt:

    <name> <class> [size=0xNN] [base=0xNN] [pending=<package>] [nosize] [# evidence]

class is `runtime` (natural host layout, real pointers), `overlay` (read
directly from disc or ELF bytes: frozen) or `save` (serialised into the save
image or the card files: frozen). On the host (x64) only overlay and save
structs are asserted. The runtime structs' asserts are documentation of the
EE layout, compiled only with -DICO_LAYOUT_EE=1 (on an EE-layout build; the
32-bit host that ran them every build was retired at Phase 2 exit, commit
36a1d73e). `pending=<package>` holds a frozen struct's 64-bit asserts back
until that package has converted its pointer fields (define
ICO_LAYOUT_PENDING to compile them anyway). `nosize` drops a size taken from
the struct's comment (when the comment means something else); `base=0xNN`
says the struct's comments give offsets within an enclosing record where it
sits at 0xNN.

Usage:
    tools/gen_layout_asserts.py            # write port/test/layout_asserts.c
    tools/gen_layout_asserts.py --check    # exit 1 if it is out of date
    tools/gen_layout_asserts.py --list     # print the structs and their asserts
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "port", "test", "layout_asserts.c")
CLASSES = os.path.join(ROOT, "config", "struct_classes.txt")
CLASS_NAMES = ("runtime", "overlay", "save")
# The preprocessor symbols the headers are read with (the host build's).
DEFINED = {"ICO_HOST"}
# Headers that do not compile on their own (they lean on a type their .c
# defines first); none of them holds an asserted struct.
SKIP_HEADERS = {"ico2/sugipon/include/girlForceField.h"}

OFF_RE = re.compile(r"^\s*(0x[0-9A-Fa-f]+)\b")
PAD_RE = re.compile(r"^_?pad([0-9A-Fa-f]+)$")
SIZE_RES = [
    re.compile(r"\b(0x[0-9A-Fa-f]+|\d+)-byte\b"),
    re.compile(r"\b(0x[0-9A-Fa-f]+|\d+) bytes\b"),
]


def num(s):
    return int(s, 16) if s.lower().startswith("0x") else int(s)


# --- tokenizer ---------------------------------------------------------------
class Tok:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind, text, line):
        self.kind = kind  # 'id', 'num', 'punct', 'comment', 'str'
        self.text = text
        self.line = line

    def __repr__(self):
        return "%s:%r@%d" % (self.kind, self.text, self.line)


TOKEN_RE = re.compile(
    r"""
    (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<str>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
  | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<num>0[xX][0-9A-Fa-f]+[uUlL]*|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuUlL]*)
  | (?P<punct>\.\.\.|->|<<|>>|[{}()\[\];,:*=&|^~!<>?+\-/%.\#])
  | (?P<nl>\n)
  | (?P<ws>[ \t\r\f\v]+|\\\n)
    """,
    re.S | re.X,
)


def strip_preprocessor(text):
    """Blank out preprocessor lines, keeping only the branches a host build
    with DEFINED takes. Line numbers are kept."""
    out = []
    stack = []  # (taking, any_taken)
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        # join continuations
        full = line
        n = 1
        while full.endswith("\\") and i + n < len(lines):
            full = full[:-1] + lines[i + n]
            n += 1
        s = full.strip()
        active = all(t for t, _ in stack)
        if s.startswith("#"):
            d = s[1:].strip()
            m = re.match(r"(\w+)\s*(.*)", d)
            word, rest = (m.group(1), m.group(2)) if m else ("", "")
            rest = re.sub(r"/\*.*?\*/|//.*", "", rest).strip()
            if word in ("ifdef", "ifndef"):
                sym = rest.split()[0] if rest else ""
                if sym in DEFINED or sym.startswith("ICO_"):
                    val = sym in DEFINED
                else:
                    val = True  # include guards
                if word == "ifndef" and (sym in DEFINED or sym.startswith("ICO_")):
                    val = not val
                stack.append((val, val))
            elif word == "if":
                val = rest not in ("0",)
                if re.fullmatch(r"defined\s*\(?\s*(\w+)\s*\)?", rest):
                    sym = re.fullmatch(r"defined\s*\(?\s*(\w+)\s*\)?", rest).group(1)
                    val = sym in DEFINED or not sym.startswith("ICO_")
                stack.append((val, val))
            elif word == "elif":
                t, any_t = stack.pop()
                val = (not any_t) and rest not in ("0",)
                stack.append((val, any_t or val))
            elif word == "else":
                t, any_t = stack.pop()
                stack.append((not any_t, True))
            elif word == "endif":
                stack.pop()
            out.extend([""] * n)
        else:
            out.extend(lines[i : i + n] if active else [""] * n)
        i += n
    return "\n".join(out)


def tokenize(text):
    toks = []
    line = 1
    pos = 0
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m:
            pos += 1
            continue
        kind = m.lastgroup
        s = m.group(kind)
        if kind not in ("ws", "nl"):
            toks.append(Tok(kind, s, line))
        line += s.count("\n")
        pos = m.end()
    return toks


# --- parser ------------------------------------------------------------------
class Member:
    def __init__(self):
        self.names = []  # declarator names, first is the one the comment is for
        self.bitfield = False
        self.agg = None  # nested Aggregate for an inline struct/union
        self.offset = None  # from a comment
        self.line = 0
        self.comment = None


class Aggregate:
    def __init__(self, kind, tag):
        self.kind = kind  # 'struct' or 'union'
        self.tag = tag
        self.members = []
        self.names = []  # typedef names
        self.comment = None  # the comment right before it
        self.file = None
        self.line = 0
        self.typedef = False


class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0

    def peek(self, k=0):
        j = self.i
        n = 0
        while j < len(self.t):
            if self.t[j].kind != "comment":
                if n == k:
                    return self.t[j]
                n += 1
            j += 1
        return None

    def next_code(self):
        while self.i < len(self.t) and self.t[self.i].kind == "comment":
            self.i += 1
        if self.i >= len(self.t):
            return None
        tok = self.t[self.i]
        self.i += 1
        return tok

    def skip_balanced(self, open_, close):
        depth = 1
        while depth and self.i < len(self.t):
            tok = self.t[self.i]
            self.i += 1
            if tok.kind == "punct":
                if tok.text == open_:
                    depth += 1
                elif tok.text == close:
                    depth -= 1

    def prev_comment(self, idx):
        """The comment directly before token idx (only comments between)."""
        j = idx - 1
        if j >= 0 and self.t[j].kind == "comment":
            return self.t[j]
        return None

    def trailing_comment(self, idx, line):
        """The first comment after token idx that starts on `line`."""
        j = idx + 1
        if j < len(self.t) and self.t[j].kind == "comment" and self.t[j].line == line:
            return self.t[j]
        return None

    def parse_body(self, agg):
        """Parse members after `{` up to the matching `}`."""
        while True:
            tok = self.peek()
            if tok is None:
                return
            if tok.kind == "punct" and tok.text == "}":
                self.next_code()
                return
            self.parse_member(agg)

    def parse_member(self, agg):
        mem = Member()
        # index of the member's first code token
        while self.i < len(self.t) and self.t[self.i].kind == "comment":
            self.i += 1
        start = self.i
        first = self.t[start]
        mem.line = first.line
        lead = self.prev_comment(start)
        # a leading comment counts when it is on the member's first line and
        # not the trailing comment of the previous member on that line
        if lead is not None and lead.line == first.line:
            k = start - 2
            if not (k >= 0 and self.t[k].kind == "punct" and self.t[k].text == ";"
                    and self.t[k].line == lead.line):
                mem.comment = lead
        depth = 0
        names_at_depth0 = []
        last_id = None
        in_bits = False
        while True:
            tok = self.next_code()
            if tok is None:
                return
            if tok.kind == "id" and tok.text in ("struct", "union", "enum") and depth == 0:
                nxt = self.peek()
                tag = None
                if nxt is not None and nxt.kind == "id":
                    tag = nxt.text
                    self.next_code()
                    nxt = self.peek()
                if nxt is not None and nxt.kind == "punct" and nxt.text == "{":
                    self.next_code()
                    if tok.text == "enum":
                        self.skip_balanced("{", "}")
                    else:
                        sub = Aggregate(tok.text, tag)
                        sub.line = tok.line
                        self.parse_body(sub)
                        mem.agg = sub
                continue
            if tok.kind == "id" and tok.text == "__attribute__":
                # skip ((...))
                if self.peek() is not None and self.peek().text == "(":
                    self.next_code()
                    self.skip_balanced("(", ")")
                continue
            if tok.kind == "punct":
                if tok.text in ("(", "["):
                    # function pointer declarator: (*name)(...) ; arrays: [n]
                    if tok.text == "(":
                        # look for (*name)
                        save = self.i
                        a = self.next_code()
                        b = self.next_code()
                        c = self.next_code()
                        if a is not None and a.text == "*" and b is not None and b.kind == "id" and c is not None and c.text == ")":
                            if not in_bits:
                                names_at_depth0.append(b.text)
                            last_id = None
                            continue
                        self.i = save
                        self.skip_balanced("(", ")")
                    else:
                        if last_id is not None and not in_bits:
                            names_at_depth0.append(last_id)
                            last_id = None
                        self.skip_balanced("[", "]")
                    continue
                if tok.text == ":":
                    mem.bitfield = True
                    if last_id is not None:
                        names_at_depth0.append(last_id)
                        last_id = None
                    in_bits = True
                    continue
                if tok.text == ",":
                    if last_id is not None and not in_bits:
                        names_at_depth0.append(last_id)
                    last_id = None
                    in_bits = False
                    continue
                if tok.text == ";":
                    if last_id is not None and not in_bits:
                        names_at_depth0.append(last_id)
                    semi = self.i - 1
                    trail = self.trailing_comment(semi, tok.line)
                    if trail is not None:
                        mem.comment = trail
                    break
                continue
            if tok.kind == "id":
                if tok.text not in ("const", "volatile", "signed", "unsigned", "short", "long",
                                    "int", "char", "float", "double", "void", "register"):
                    last_id = tok.text
                else:
                    last_id = None
                continue
            if tok.kind == "num" and in_bits:
                continue
        # unique names, order kept
        seen = []
        for n in names_at_depth0:
            if n not in seen:
                seen.append(n)
        mem.names = seen
        if mem.comment is not None:
            m = OFF_RE.match(mem.comment.text[2:])
            if m:
                mem.offset = int(m.group(1), 16)
        agg.members.append(mem)

    def parse_file(self):
        aggs = []
        while self.i < len(self.t):
            tok = self.t[self.i]
            if tok.kind == "comment":
                self.i += 1
                continue
            if tok.kind == "id" and tok.text in ("struct", "union"):
                start = self.i
                is_typedef = False
                # typedef immediately before?
                j = start - 1
                while j >= 0 and self.t[j].kind == "comment":
                    j -= 1
                first = start
                if j >= 0 and self.t[j].kind == "id" and self.t[j].text == "typedef":
                    is_typedef = True
                    first = j
                self.i += 1
                tag = None
                nxt = self.peek()
                if nxt is not None and nxt.kind == "id" and nxt.text != "__attribute__":
                    tag = nxt.text
                    self.next_code()
                    nxt = self.peek()
                while nxt is not None and nxt.kind == "id" and nxt.text == "__attribute__":
                    self.next_code()
                    self.next_code()
                    self.skip_balanced("(", ")")
                    nxt = self.peek()
                if nxt is None or not (nxt.kind == "punct" and nxt.text == "{"):
                    continue
                self.next_code()
                agg = Aggregate(tok.text, tag)
                agg.typedef = is_typedef
                agg.line = self.t[first].line
                c = self.prev_comment(first)
                if c is not None and c.line + c.text.count("\n") >= self.t[first].line - 1:
                    agg.comment = c
                self.parse_body(agg)
                # declarators after `}` up to `;`
                names = []
                while True:
                    t2 = self.next_code()
                    if t2 is None or (t2.kind == "punct" and t2.text == ";"):
                        break
                    if t2.kind == "id" and t2.text == "__attribute__":
                        self.next_code()
                        self.skip_balanced("(", ")")
                        continue
                    if t2.kind == "id":
                        names.append(t2.text)
                    if t2.kind == "punct" and t2.text == "[":
                        self.skip_balanced("[", "]")
                if is_typedef:
                    agg.names = names
                aggs.append(agg)
                continue
            self.i += 1
        return aggs


# --- collection --------------------------------------------------------------
def headers():
    out = []
    base = os.path.join(ROOT, "ico2")
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames.sort()
        rel = os.path.relpath(dirpath, ROOT)
        if rel.startswith(os.path.join("ico2", "vusrc")):
            continue
        for f in sorted(filenames):
            if f.endswith(".h"):
                p = os.path.join(rel, f)
                if p not in SKIP_HEADERS:
                    out.append(p)
    return out


def type_name(agg):
    """The spelling an assert uses, and the names the classification may use."""
    names = []
    if agg.typedef:
        names = [n for n in agg.names if not n.startswith("*")]
    if names:
        return names[0], set(names) | ({"struct " + agg.tag, agg.tag} if agg.tag else set())
    if agg.tag:
        return "%s %s" % (agg.kind, agg.tag), {agg.tag, "%s %s" % (agg.kind, agg.tag)}
    return None, set()


def collect_checks(agg):
    """(path, offset, line, source) for a top-level aggregate. Nested inline
    aggregates: a member path `a.b`; their comments are absolute when the
    value is at least the enclosing member's comment, else relative to it."""
    checks = []

    def walk(members, prefix, base_known, base):
        for mem in members:
            names = mem.names
            if mem.agg is not None and not names:
                # anonymous member: its fields are the outer struct's own
                walk(mem.agg.members, prefix, base_known, base)
                continue
            if not names:
                continue
            path = prefix + names[0]
            if mem.offset is not None and not mem.bitfield:
                off = mem.offset
                if prefix and base_known and off < base:
                    off = base + off
                checks.append((path, off, mem.line, "comment"))
            elif not mem.bitfield:
                m = PAD_RE.match(names[0])
                if m and len(names) == 1 and not prefix:
                    checks.append((path, int(m.group(1), 16), mem.line, "pad"))
            if mem.agg is not None:
                if mem.offset is not None:
                    o = mem.offset
                    if prefix and base_known and o < base:
                        o = base + o
                    walk(mem.agg.members, path + ".", True, o)
                else:
                    walk(mem.agg.members, path + ".", False, 0)

    walk(agg.members, "", True, 0)
    return checks


def comment_size(agg):
    """The first "N-byte" or "N bytes" in the struct's leading comment that is
    not an alignment ("16-byte aligned", "64-byte alignment")."""
    if agg.comment is None:
        return None
    text = agg.comment.text
    best = None
    for r in SIZE_RES:
        for m in r.finditer(text):
            if re.match(r"\s*(align|boundar)", text[m.end():]):
                continue
            if best is None or m.start() < best.start():
                best = m
            break
    return num(best.group(1)) if best else None


def read_classes():
    classes = {}
    order = []
    if not os.path.exists(CLASSES):
        return classes, order
    with open(CLASSES, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            body = line.split("#", 1)[0].strip()
            if not body:
                continue
            parts = body.split()
            name = parts[0]
            # `struct Foo` spellings
            if name in ("struct", "union") and len(parts) > 1:
                name = parts[0] + " " + parts[1]
                parts = [name] + parts[2:]
            if len(parts) < 2 or parts[1] not in CLASS_NAMES:
                sys.exit("%s:%d: expected `<name> <%s>`" % (CLASSES, ln, "|".join(CLASS_NAMES)))
            ent = {"class": parts[1], "size": None, "pending": None, "nosize": False, "base": 0,
                   "line": ln}
            for opt in parts[2:]:
                if opt.startswith("size="):
                    ent["size"] = num(opt[5:])
                elif opt.startswith("pending="):
                    ent["pending"] = opt[8:]
                elif opt.startswith("base="):
                    ent["base"] = num(opt[5:])
                elif opt == "nosize":
                    ent["nosize"] = True
                else:
                    sys.exit("%s:%d: unknown option %r" % (CLASSES, ln, opt))
            if name in classes:
                sys.exit("%s:%d: %s listed twice" % (CLASSES, ln, name))
            classes[name] = ent
            order.append(name)
    return classes, order


def gather():
    structs = []  # dicts
    seen = {}
    others = {}  # every named aggregate: alias -> (name, header, line)
    for h in headers():
        with open(os.path.join(ROOT, h), encoding="latin-1") as f:
            text = f.read()
        toks = tokenize(strip_preprocessor(text))
        for agg in Parser(toks).parse_file():
            name, aliases = type_name(agg)
            checks = collect_checks(agg)
            if name is not None:
                for a in aliases | {name}:
                    others.setdefault(a, (name, h, agg.line))
            if not any(src == "comment" for _, _, _, src in checks):
                continue
            if name is None:
                print("%s:%d: anonymous aggregate with offset comments, skipped" % (h, agg.line),
                      file=sys.stderr)
                continue
            if name in seen:
                print("%s:%d: %s already defined at %s, skipped" % (h, agg.line, name, seen[name]),
                      file=sys.stderr)
                continue
            seen[name] = "%s:%d" % (h, agg.line)
            structs.append({
                "name": name,
                "aliases": aliases,
                "header": h,
                "line": agg.line,
                "checks": checks,
                "comment_size": comment_size(agg),
            })
    return structs, others


def render(structs, others, classes, order):
    missing = []
    for s in structs:
        ent = None
        for a in [s["name"]] + sorted(s["aliases"]):
            if a in classes:
                ent = classes[a]
                break
        if ent is None:
            missing.append(s)
        s["cls"] = ent
    if missing:
        for s in missing:
            print("%s:%d: %s has no line in config/struct_classes.txt" % (s["header"], s["line"], s["name"]),
                  file=sys.stderr)
        sys.exit(1)

    # classification entries for structs without offset comments: size only
    touched = set()
    for s in structs:
        touched |= s["aliases"] | {s["name"]}
    for cname in order:
        if cname in touched:
            continue
        ent = classes[cname]
        if cname not in others:
            sys.exit("%s:%d: %s is not a struct in any header" % (CLASSES, ent["line"], cname))
        if ent["size"] is None:
            sys.exit("%s:%d: %s has no offset comments; give it size=" % (CLASSES, ent["line"], cname))
        name, h, line = others[cname]
        structs.append({"name": name, "aliases": {cname}, "header": h, "line": line, "checks": [],
                        "comment_size": None, "cls": ent})

    hdrs = []
    for s in structs:
        if s["header"] not in hdrs:
            hdrs.append(s["header"])
    counts = {c: [0, 0, 0] for c in CLASS_NAMES}  # structs, fields, sizes
    out = []
    w = out.append
    w("/* port/test/layout_asserts.c: GENERATED by tools/gen_layout_asserts.py from")
    w(" * the ico2/ headers' offset comments and config/struct_classes.txt. Do not")
    w(" * edit; rerun the script.")
    w(" *")
    w(" * On the host (x64) only the frozen structs (overlay: read from disc or ELF")
    w(" * bytes; save: serialised into the save image) are asserted, except the ones")
    w(" * still pending a conversion (ICO_LAYOUT_PENDING compiles those too). The")
    w(" * runtime structs' asserts are documentation of the EE layout: they hold on a")
    w(" * build with the EE's ILP32 layout and compile only with -DICO_LAYOUT_EE=1.")
    w(" * The 32-bit host build that checked them every build was retired at Phase 2")
    w(" * exit (commit 36a1d73e). */")
    w("")
    w("#include <stddef.h>")
    w("")
    w('#include "typedef.h"')
    for h in hdrs:
        b = os.path.basename(h)
        if b != "typedef.h":
            w('#include "%s"' % b)
    w("")
    w("#ifndef ICO_LAYOUT_EE")
    w("#define ICO_LAYOUT_EE 0")
    w("#endif")
    w("#ifndef ICO_LAYOUT_PENDING")
    w("#define ICO_LAYOUT_PENDING 0")
    w("#endif")
    w("")
    w("#define OFF(T, m, o) _Static_assert(offsetof(T, m) == (o), #T \".\" #m \" at \" #o)")
    w("#define SIZE(T, n) _Static_assert(sizeof(T) == (n), \"sizeof(\" #T \") == \" #n)")
    w("")
    for s in structs:
        ent = s["cls"]
        cls = ent["class"]
        size = ent["size"]
        size_src = "config"
        if size is None and not ent["nosize"] and s["comment_size"] is not None:
            size = s["comment_size"]
            size_src = "comment"
        if cls == "runtime":
            cond = "ICO_LAYOUT_EE"
        elif ent["pending"]:
            cond = "ICO_LAYOUT_EE || ICO_LAYOUT_PENDING"
        else:
            cond = None
        T = s["name"]
        note = "%s, %s:%d" % (cls, s["header"], s["line"])
        if ent["pending"]:
            note += ", 64-bit pending %s" % ent["pending"]
        w("/* %s: %s */" % (T, note))
        if cond:
            w("#if %s" % cond)
        n_f = 0
        done = set()
        for path, off, line, src in s["checks"]:
            if path in done:
                continue
            done.add(path)
            if src == "comment":
                off -= ent["base"]
            w("OFF(%s, %s, 0x%X);" % (T, path, off))
            n_f += 1
        if size is not None:
            w("SIZE(%s, 0x%X); /* %s */" % (T, size, size_src))
        if cond:
            w("#endif")
        w("")
        counts[cls][0] += 1
        counts[cls][1] += n_f
        counts[cls][2] += 1 if size is not None else 0
    w("int main(void)")
    w("{")
    w("    return 0;")
    w("}")
    return "\n".join(out) + "\n", counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if the output is out of date")
    ap.add_argument("--list", action="store_true", help="list structs and their checks")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    structs, others = gather()
    if args.list:
        for s in structs:
            print("%s %s:%d size=%s" % (s["name"], s["header"], s["line"], s["comment_size"]))
            for c in s["checks"]:
                print("    %s 0x%X %s:%d" % (c[0], c[1], c[3], c[2]))
        return 0
    classes, order = read_classes()
    text, counts = render(structs, others, classes, order)
    if args.check:
        try:
            with open(args.out, encoding="utf-8") as f:
                cur = f.read()
        except OSError:
            cur = None
        if cur != text:
            print("%s is out of date: run tools/gen_layout_asserts.py" % os.path.relpath(args.out, ROOT),
                  file=sys.stderr)
            return 1
        return 0
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)
    tot = [0, 0, 0]
    for c in CLASS_NAMES:
        print("%-8s %4d structs %5d offsets %4d sizes" % (c, *counts[c]))
        tot = [a + b for a, b in zip(tot, counts[c])]
    print("%-8s %4d structs %5d offsets %4d sizes" % ("total", *tot))
    return 0


if __name__ == "__main__":
    sys.exit(main())
