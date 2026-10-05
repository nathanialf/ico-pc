#!/usr/bin/env python3
"""Top-level blank-line layout for the repo's C (runs after clang-format).

clang-format settles braces, wrapping and runs of blank lines, but not WHICH
top-level items sit together. This pass normalises, at brace depth 0 only:

  * `#include` lines form one block with no blank lines inside it;
  * declarations (`extern ...;`, prototypes, `static inline` prototypes)
    form contiguous blocks with no blank lines inside;
  * preprocessor lines (#define, #if...) form contiguous blocks;
  * every other item (function, struct/union/enum/typedef definition,
    data definition, standalone comment) is separated by exactly one blank
    line from its neighbours, as is every change of kind.

A comment that sits directly above an item (no blank line between) belongs to
that item and moves with it. Nothing inside a function body is touched.

  tools/format_layout.py FILE...          rewrite in place
  tools/format_layout.py --check FILE...  exit 1 if any file would change
"""
import re, sys

INCLUDE = re.compile(r"^#\s*include\b")
PP = re.compile(r"^#")
COND = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b")
DECL_START = re.compile(r"^(extern\b|static\s+inline\b[^{]*\)\s*;|inline\b[^{]*\)\s*;|[A-Za-z_][\w \*]*\b[A-Za-z_]\w*\s*\([^;{]*\)\s*;)")


def strip_code(line, state):
    """Return the line with comments and string/char literals blanked, tracking
    an open block comment across lines in `state['blk']`."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if state["blk"]:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out)
            state["blk"] = False
            i = j + 2
            continue
        c = line[i]
        if line.startswith("/*", i):
            state["blk"] = True
            i += 2
            continue
        if line.startswith("//", i):
            break
        if c in "\"'":
            q = c
            i += 1
            while i < n and line[i] != q:
                if line[i] == "\\":
                    i += 1
                i += 1
            i += 1
            out.append(" ")
            continue
        out.append(c)
        i += 1
    return "".join(out)


def items(lines):
    """Split the file into top-level items: (kind, [lines]) with blank lines
    dropped; blank-line policy is re-applied by `render`."""
    result = []
    depth = 0
    state = {"blk": False}
    cur = []
    cur_kind = None
    pending_comment = []
    in_comment_item = False

    def flush():
        nonlocal cur, cur_kind
        if cur:
            result.append((cur_kind, cur))
        cur, cur_kind = [], None

    # brace depth through #if arms: each arm starts at the depth of its #if,
    # and #endif leaves the first arm's depth (an #ifdef ICO_HOST arm and its
    # #else may each open the same function)
    conds = []

    i = 0
    while i < len(lines):
        raw = lines[i]
        code = strip_code(raw, state)
        stripped = raw.strip()
        cond = COND.match(code)
        if cond:
            word = cond.group(1)
            if word in ("if", "ifdef", "ifndef"):
                conds.append([depth, None])
            elif conds and word in ("elif", "else"):
                if conds[-1][1] is None:
                    conds[-1][1] = depth
                depth = conds[-1][0]
            elif conds and word == "endif":
                at_if, first = conds.pop()
                if first is not None:
                    depth = first
        if depth == 0 and not cur:
            if stripped == "":
                # a blank line ends a standalone comment
                if pending_comment:
                    result.append(("comment", pending_comment))
                    pending_comment = []
                i += 1
                continue
            is_comment_only = (code.strip() == "" and stripped != "")
            if is_comment_only:
                pending_comment.append(raw)
                i += 1
                continue
            # start of an item; attach any pending comment
            if INCLUDE.match(stripped):
                kind = "include"
            elif PP.match(stripped):
                kind = "pp"
            elif DECL_START.match(stripped) and "{" not in code:
                kind = "decl"
            else:
                kind = "def"
            cur = pending_comment + [raw]
            pending_comment = []
            cur_kind = kind
            depth += code.count("{") - code.count("}")
            # single-line items end here unless a statement continues
            ends = (kind in ("include", "pp") and not stripped.endswith("\\")) or \
                   (kind == "decl" and code.rstrip().endswith(";")) or \
                   (kind == "def" and depth == 0 and (code.rstrip().endswith(";") or code.rstrip().endswith("}")))
            if kind == "def" and depth == 0 and not ends and not code.strip():
                ends = False
            if ends:
                flush()
            i += 1
            continue
        # continuing an item
        cur.append(raw)
        d = code.count("{") - code.count("}")
        depth += d
        if cur_kind in ("include", "pp"):
            if not stripped.endswith("\\"):
                flush()
        elif cur_kind == "decl":
            if code.rstrip().endswith(";"):
                flush()
        else:
            if depth == 0 and (code.rstrip().endswith(";") or code.rstrip().endswith("}")):
                flush()
        i += 1
    if pending_comment:
        result.append(("comment", pending_comment))
    flush()
    return result


CONTIG = {"include", "decl", "pp"}


def render(its):
    out = []
    prev = None
    for kind, ls in its:
        # strip trailing blank lines inside multi-line items (bodies keep theirs)
        while ls and ls[-1].strip() == "":
            ls = ls[:-1]
        if prev is not None:
            if kind in CONTIG and prev == kind:
                pass
            else:
                out.append("")
        out.extend(ls)
        prev = kind
    return out


def process(text):
    lines = text.split("\n")
    trailing_nl = text.endswith("\n")
    if trailing_nl:
        lines = lines[:-1]
    its = items(lines)
    new = render(its)
    res = "\n".join(new) + "\n"
    return res


def main(argv):
    check = False
    if argv and argv[0] == "--check":
        check = True
        argv = argv[1:]
    bad = 0
    for path in argv:
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        new = process(text)
        if new != text:
            if check:
                print(f"format_layout: {path} would change (run tools/format.sh {path})", file=sys.stderr)
                bad = 1
            else:
                open(path, "w", encoding="utf-8", errors="surrogateescape").write(new)
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
