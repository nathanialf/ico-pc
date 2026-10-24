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
    line from its neighbours, as is every change of kind;
  * a target's version block (opened by a version condition in one of the
    forms tools/check_targets.py accepts: #ifdef/#ifndef VERSION_<SLUG>,
    #if [!]defined(VERSION_<SLUG>) joined by || or &&; then its #elif arms,
    #else and #endif) holds its arms tight: no blank line after the opening
    line, an #elif or the #else, none before an #elif, the #else or the
    #endif. The items inside an arm follow the rules above, and each arm
    counts braces from the depth the block opened at.

A comment that sits directly above an item (no blank line between) belongs to
that item and moves with it. Nothing inside a function body is touched.

  tools/format_layout.py FILE...          rewrite in place
  tools/format_layout.py --check FILE...  exit 1 if any file would change
  tools/format_layout.py --self-test      the version-block layout on fixed cases
"""
import re
import sys

# The version-condition forms (the same reader as gen_data_c.version_condition,
# which takes them from upstream's tools/check_targets.py; the port carries
# neither the PS2 build nor that script, and this tool needs no pyelftools):
#     #ifdef VERSION_A                     #ifndef VERSION_A
#     #if defined(VERSION_A) || defined(VERSION_B)
#     #if !defined(VERSION_A) && !defined(VERSION_B)
#     #elif defined(VERSION_A) [|| defined(VERSION_B)]
#     #else                                #endif
_DIRECTIVE = re.compile(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")
_DEFINED = r"defined\(VERSION_([A-Z0-9_]+)\)"
_ANY_OF = re.compile(rf"{_DEFINED}(?:\s+\|\|\s+{_DEFINED})*")
_NONE_OF = re.compile(rf"!{_DEFINED}(?:\s+&&\s+!{_DEFINED})*")


def version_condition(line):
    """A preprocessor line read as a version block's directive: (kw, slugs,
    positive) for a version condition, (kw, None, None) for #else, #endif and
    a conditional naming no VERSION_, None for any other line. Raises
    ValueError for a conditional naming a VERSION_ in any other form."""
    m = _DIRECTIVE.match(line)
    if not m:
        return None
    kw, rest = m.group(1), m.group(2).split("/*", 1)[0].strip()
    if "VERSION_" not in rest:
        return (kw, None, None)
    names = re.findall(r"VERSION_([A-Z0-9_]+)", rest)
    if kw in ("ifdef", "ifndef") and re.fullmatch(r"VERSION_[A-Z0-9_]+", rest):
        return ("if", frozenset(n.lower() for n in names), kw == "ifdef")
    if kw in ("if", "elif") and _ANY_OF.fullmatch(rest):
        return (kw, frozenset(n.lower() for n in names), True)
    if kw == "if" and _NONE_OF.fullmatch(rest):
        return (kw, frozenset(n.lower() for n in names), False)
    raise ValueError(f"#{kw} {rest}: not a version condition form")


INCLUDE = re.compile(r"^#\s*include\b")
COND_OPEN = re.compile(r"^#\s*if")
# an #elif, like the #else, opens an arm that is an alternative to the one before
COND_ELSE = re.compile(r"^#\s*(else|elif)\b")
COND_END = re.compile(r"^#\s*endif\b")
PP = re.compile(r"^#")
DECL_START = re.compile(r"^(extern\b|static\s+inline\b[^{]*\)\s*;|inline\b[^{]*\)\s*;|[A-Za-z_][\w \*]*\b[A-Za-z_]\w*\s*\([^;{]*\)\s*;)")


def ver_open(line):
    """Whether a line opens a version block."""
    try:
        v = version_condition(line)
    except ValueError:
        return False
    return bool(v) and v[0] == "if" and v[1] is not None


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
    conds = []  # one entry per open top-level conditional: is it a version block
    # brace depth at each open conditional: an #else arm is an alternative to
    # the arm before it, so it starts from the depth its #if started from
    cond_depth = []

    def flush():
        nonlocal cur, cur_kind
        if cur:
            result.append((cur_kind, cur))
        cur, cur_kind = [], None

    i = 0
    while i < len(lines):
        raw = lines[i]
        code = strip_code(raw, state)
        stripped = raw.strip()
        directive = code.strip()
        if COND_OPEN.match(directive):
            cond_depth.append(depth)
        elif COND_ELSE.match(directive) and cond_depth:
            depth = cond_depth[-1]
        elif COND_END.match(directive) and cond_depth:
            cond_depth.pop()
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
            elif ver_open(stripped):
                kind = "ver_open"
                conds.append(True)
            elif COND_ELSE.match(stripped) and conds and conds[-1]:
                kind = "ver_else"
            elif COND_END.match(stripped) and conds and conds[-1]:
                kind = "ver_end"
                conds.pop()
            elif PP.match(stripped):
                kind = "pp"
                if COND_OPEN.match(stripped):
                    conds.append(False)
                elif COND_END.match(stripped) and conds:
                    conds.pop()
            elif DECL_START.match(stripped) and "{" not in code:
                kind = "decl"
            else:
                kind = "def"
            cur = pending_comment + [raw]
            pending_comment = []
            cur_kind = kind
            depth += code.count("{") - code.count("}")
            # single-line items end here unless a statement continues
            ends = (kind in ("include", "pp", "ver_open", "ver_else", "ver_end")
                    and not stripped.endswith("\\")) or \
                   (kind == "decl" and code.rstrip().endswith(";")) or \
                   (kind == "def" and depth == 0 and (code.rstrip().endswith(";") or code.rstrip().endswith("}")))
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
        if prev is not None and not (
            (kind in CONTIG and prev == kind)
            or prev in ("ver_open", "ver_else")
            or kind in ("ver_else", "ver_end")
        ):
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


SELF_TEST = (
    # (input, expected output)
    ("#ifdef VERSION_JPN\n\nint a;\n\n#else\n\nint b;\n\n#endif\n",
     "#ifdef VERSION_JPN\nint a;\n#else\nint b;\n#endif\n"),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\n\nint a;\n\n#endif\nint c;\n",
     "#if defined(VERSION_JPN) || defined(VERSION_KOR)\nint a;\n#endif\n\nint c;\n"),
    ("#if !defined(VERSION_JPN) && !defined(VERSION_KOR)\n\nint a;\n\n#endif\n",
     "#if !defined(VERSION_JPN) && !defined(VERSION_KOR)\nint a;\n#endif\n"),
    ("#ifdef VERSION_KOR\nvoid f(void)\n{\n#elif defined(VERSION_JPN)\nvoid f(int a)\n{\n"
     "#else\nvoid f(int a, int b)\n{\n#endif\n}\nint d;\n",
     "#ifdef VERSION_KOR\nvoid f(void)\n{\n#elif defined(VERSION_JPN)\nvoid f(int a)\n{\n"
     "#else\nvoid f(int a, int b)\n{\n#endif\n}\n\nint d;\n"),
    ("#ifdef VERSION_KOR\nint a;\nint f(void) { return 1; }\n#elif defined(VERSION_JPN)\nint b;\n#endif\n",
     "#ifdef VERSION_KOR\nint a;\n\nint f(void) { return 1; }\n#elif defined(VERSION_JPN)\nint b;\n#endif\n"),
    # a fourth target's block nested in the PAL arm, every arm tight
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\n\nint a;\n\n#else\n\n#ifdef VERSION_NTSC\n\nint n;\n\n"
     "#else\n\nint b;\n\n#endif\n\n#endif\nint c;\n",
     "#if defined(VERSION_JPN) || defined(VERSION_KOR)\nint a;\n#else\n#ifdef VERSION_NTSC\nint n;\n#else\nint b;\n#endif\n#endif\n\nint c;\n"),
    # a fifth target's block inside the fourth's arm, and a block of its own at the third level
    ("#if defined(VERSION_NTSC) || defined(VERSION_AUG6)\n\n#ifdef VERSION_AUG6\n\nint a;\n\n#else\n\nint n;\n\n#endif\n\n#else\n\nint p;\n\n#endif\nint c;\n",
     "#if defined(VERSION_NTSC) || defined(VERSION_AUG6)\n#ifdef VERSION_AUG6\nint a;\n#else\nint n;\n#endif\n#else\nint p;\n#endif\n\nint c;\n"),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\n\nint j;\n\n#else\n\n#ifdef VERSION_NTSC\n\nint n;\n\n#else\n\n#ifdef VERSION_AUG6\n\nint a;\n\n"
     "#else\n\nint p;\n\n#endif\n\n#endif\n\n#endif\n",
     "#if defined(VERSION_JPN) || defined(VERSION_KOR)\nint j;\n#else\n#ifdef VERSION_NTSC\nint n;\n#else\n#ifdef VERSION_AUG6\nint a;\n#else\nint p;\n#endif\n#endif\n#endif\n"),
    # a sixth target's block inside the fifth's arm of a shared block, and one of its own at the fourth level
    ("#if defined(VERSION_AUG6) || defined(VERSION_JUN26)\n\n#ifdef VERSION_JUN26\n\nint d;\n\n#else\n\nint a;\n\n#endif\n\n#else\n\nint p;\n\n#endif\nint c;\n",
     "#if defined(VERSION_AUG6) || defined(VERSION_JUN26)\n#ifdef VERSION_JUN26\nint d;\n#else\nint a;\n#endif\n#else\nint p;\n#endif\n\nint c;\n"),
    ("#if defined(VERSION_JPN) || defined(VERSION_KOR)\n\nint j;\n\n#else\n\n#ifdef VERSION_NTSC\n\nint n;\n\n#else\n\n#ifdef VERSION_AUG6\n\nint a;\n\n"
     "#else\n\n#ifdef VERSION_JUN26\n\nint d;\n\n#else\n\nint p;\n\n#endif\n\n#endif\n\n#endif\n\n#endif\n",
     "#if defined(VERSION_JPN) || defined(VERSION_KOR)\nint j;\n#else\n#ifdef VERSION_NTSC\nint n;\n#else\n#ifdef VERSION_AUG6\nint a;\n#else\n"
     "#ifdef VERSION_JUN26\nint d;\n#else\nint p;\n#endif\n#endif\n#endif\n#endif\n"),
)


def self_test():
    failed = 0
    for text, want in SELF_TEST:
        got = process(text)
        ok = got == want
        failed += not ok
        print(f"format_layout: self-test {'ok  ' if ok else 'FAIL'} {text.splitlines()[0]!r}"
              + ("" if ok else f"\n  got  {got!r}\n  want {want!r}"), file=sys.stderr)
    print(f"format_layout: self-test {'FAIL' if failed else 'OK'} ({failed} failed)", file=sys.stderr)
    return 1 if failed else 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    check = False
    if argv and argv[0] == "--check":
        check = True
        argv = argv[1:]
    bad = 0
    for path in argv:
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            text = f.read()
        new = process(text)
        if new != text:
            if check:
                print(f"format_layout: {path} would change (run tools/format.sh {path})", file=sys.stderr)
                bad = 1
            else:
                with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                    f.write(new)
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
