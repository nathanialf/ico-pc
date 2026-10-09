#!/usr/bin/env python3
"""Rename port/ symbols from prefix_CamelCase to prefix_snake_case.

port/ names its functions and file-scope symbols with a lowercase module
prefix: rd_, rd__, ui_, ui__, rhi_, vkr_, d3dp_, dx_, texpack_ and so on.
Older code follows the prefix with CamelCase (rd_ReplayFrame), newer code
with snake_case. This tool moves every CamelCase one to snake_case
(rd_replay_frame) in three steps:

    tools/rename_port_symbols.py --table tools/rename_table.txt
        scan the tree and write the table, one "old new" pair per line,
        sorted; exit 1 (and write nothing) on a collision
    tools/rename_port_symbols.py --apply tools/rename_table.txt [--root DIR]
        replace every table name, as a whole word, in the files the rename
        owns (below), comments and strings included; print a summary
    tools/rename_port_symbols.py --check tools/rename_table.txt [--root DIR]
        list what still matches the pattern after an apply, each name with
        the reason it stays; exit 1 if a table name or a port-declared
        CamelCase name is left

Which names. An identifier is a candidate when it reads
<lowercase prefix>_<Upper...> or <prefix>__<Upper...> (the prefix is one
or more lowercase words, [a-z][a-z0-9]* joined by "_": rd, lt_ext,
rhi_vk; the rest may hold more underscores). It enters the table
when port/ declares it at file scope (a function definition or prototype,
a file-scope variable, a function-like or object-like #define, a
prototype a port/ Python generator writes on a line of its own) and the
game (ico2/, sce/) does not define it. Never renamed: types and struct,
union and enum tags (RdFrame, typedef names), enum constants, struct
members, locals and parameters, uppercase macros (RD_*), Java_* JNI names,
port/third_party, the generated files (port/data/gen, port/test/
layout_asserts.c, the shader headers under port/rhi/test/shaders), the
newlib constants of port/math/newlib (ico_ksin_S1: newlib's own names) and
every name the game defines (game code keeps its names; only its calls to
renamed port functions change), including the host hooks the game files
define for port/ (gif_Host*, pac_Host*, mc_Host*, tex_HostTextureId).

Pasted names. rhi_backend.h builds the backend entry points by pasting a
bare suffix (X(Init) -> rhi_##n, RHI__NAME(Init) -> rhi_<backend>_Init) and
names the dispatch table's fields after it (be()->Init). PASTE_SITES lists
those places; --apply rewrites the suffix there too (X(init), be()->init).

How a name converts. The prefix and its one or two underscores stay. Each
underscore-separated part of the rest is split at a lower-to-upper step
and before the last capital of a capital run that a lowercase letter
follows; digits stay with the word before them. The parts are lowercased
and joined with "_":

    SetSpaceOverride -> set_space_override    GsNamedBlock -> gs_named_block
    ZWrite -> z_write    TexA -> tex_a    PABE -> pabe    D3D12 -> d3d12
    UVOffset -> uv_offset    AA1Expand -> aa1_expand

Collisions refuse the table: two old names that convert to one new name,
or a new name that already appears as a word anywhere in the scanned
trees (port, tools, android, ico2, sce, cmake, the CMake files, docs,
.github). --accept NEW, once per name, lets a new name through that is
already a word only outside C code (a file name such as rd_present.c, a
CTest name); a new name that is already a C identifier always refuses.

Files --apply rewrites: port/, tools/, android/, cmake/, CMakeLists.txt,
CMakePresets.json, .github/workflows/ci.yml, docs/BUILDING.md, and the
ico2/ and sce/ files that use a table name. Skipped: the keep-list paths
above, this tool and the table, and binary files. Bytes that are not
UTF-8 (the game's EUC-JP working-tree files) pass through unchanged. Run
tools/format.sh afterwards (longer names rewrap some lines).
"""

import argparse
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

IDENT = re.compile(r"(?<![A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]*")
CAMEL = re.compile(r"^([a-z][a-z0-9]*(?:_[a-z0-9]+)*)(__?)([A-Z][A-Za-z0-9_]*)$")

# A C prototype on a line of its own, as a Python generator writes one.
C_PROTO = re.compile(r"^\s*[A-Za-z_][\w \t*]*\b[a-z][a-z0-9_]*_[A-Z]\w*\([^()]*\);\s*$")

C_EXTS = (".c", ".h", ".inc", ".hlsl", ".hlsli")
ASM_EXTS = (".s", ".vsm", ".dsm")

# Trees scanned for collisions and for --check.
SCAN_TREES = ("port", "tools", "android", "ico2", "sce", "cmake", "docs", ".github")
SCAN_FILES = ("CMakeLists.txt", "CMakePresets.json")

# What --apply rewrites outright; ico2/ and sce/ files are added when they
# use a table name.
APPLY_TREES = ("port", "tools", "android", "cmake")
APPLY_FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    ".github/workflows/ci.yml",
    "docs/BUILDING.md",
)
GAME_TREES = ("ico2", "sce")

# This tool and its table: never rewritten, and their words (the docstring's
# examples, an earlier table) never count as collisions.
SELF_FILES = ("tools/rename_port_symbols.py", "tools/rename_table.txt")

# Never rewritten and never scanned for declarations.
KEEP_PREFIXES = ("port/third_party/", "port/data/gen/")
KEEP_FILES = (
    "port/test/layout_asserts.c",
    "port/rhi/test/shaders/rhi_test_spv.h",
    "port/rhi/test/shaders/rhi_test_dxil.h",
) + SELF_FILES

# Rewritten, but their declarations never enter the table: newlib's own
# constant names (S1, C1, PIo2, Zero) behind an ico_k*_ prefix, kept so
# the code still reads against newlib.
KEEP_DECL_PREFIXES = ("port/math/newlib/",)

# Names a macro pastes from a bare suffix (rhi_##n, RHI__NAME(n)): in these
# files the suffix W of a table name prefix+W is rewritten too, where the
# pattern (W in its group) finds it. The backend table's fields carry the
# same suffixes.
PASTE_SITES = (
    ("port/rhi/rhi_backend.h", "rhi_", r"\bX\(([A-Z]\w*)\)"),
    ("port/rhi/rhi_backend_names.h", "rhi_", r"\bRHI__NAME\(([A-Z]\w*)\)"),
    ("port/rhi/rhi_backend.c", "rhi_", r"\bbe\(\)->([A-Z]\w*)\("),
)

# Directories a plain walk (no git) skips.
SKIP_DIRS = {".git", ".gradle", ".cxx", "build", "build-host", ".venv", "__pycache__"}


def is_keep(rel):
    return rel.startswith(KEEP_PREFIXES) or rel in KEEP_FILES


def snake(part):
    """Convert one CamelCase run (no underscores) to snake_case."""
    out = []
    n = len(part)
    for i, c in enumerate(part):
        if i > 0 and c.isupper():
            prev = part[i - 1]
            nxt = part[i + 1] if i + 1 < n else ""
            if prev.islower() or (nxt.islower() and (prev.isupper() or prev.isdigit())):
                out.append("_")
        out.append(c.lower())
    return "".join(out)


def convert(name):
    """The snake_case form of a candidate name, or None if it is not one."""
    m = CAMEL.match(name)
    if not m:
        return None
    prefix, sep, rest = m.groups()
    return prefix + sep + "_".join(snake(p) if p else p for p in rest.split("_"))


# ---------------------------------------------------------------- files


def list_files(root, trees, files):
    """Relative paths of the regular files under trees plus files, sorted.

    In a git checkout the tracked files; elsewhere (a scratch copy) a walk
    that skips symlinks and build directories.
    """
    out = set()
    if (root / ".git").exists():
        res = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--", *trees, *files],
            check=True,
            capture_output=True,
        )
        for rel in res.stdout.decode().split("\0"):
            if rel and (root / rel).is_file() and not (root / rel).is_symlink():
                out.add(rel)
        return sorted(out)
    for t in trees:
        base = root / t
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(
                d
                for d in dirnames
                if d not in SKIP_DIRS and not os.path.islink(os.path.join(dirpath, d))
            )
            for f in filenames:
                p = Path(dirpath) / f
                if p.is_file() and not p.is_symlink():
                    out.add(p.relative_to(root).as_posix())
    for f in files:
        p = root / f
        if p.is_file() and not p.is_symlink():
            out.add(f)
    return sorted(out)


def read_text(root, rel):
    """The file's text, or None for a binary file (a NUL byte).

    Bytes that are not UTF-8 (the game's ISO-8859 comments, its EUC-JP
    working-tree files) decode as surrogates and write back unchanged
    (write_text).
    """
    data = (root / rel).read_bytes()
    if b"\0" in data:
        return None
    return data.decode("utf-8", "surrogateescape")


def write_text(root, rel, text):
    (root / rel).write_bytes(text.encode("utf-8", "surrogateescape"))


# ---------------------------------------------------------------- C scan


def strip_c(text):
    """Blank comments and literal contents, keeping quotes and newlines."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j, n - 1)
            out.append(c + re.sub(r"[^\n]", " ", text[i + 1 : j]) + text[j])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|\d[\w.]*|\"[^\"\n]*\"|'[^'\n]*'|\S")


def logical_lines(text):
    """Join backslash continuations; yield (is_directive, line)."""
    buf = []
    for line in text.split("\n"):
        if line.endswith("\\"):
            buf.append(line[:-1])
            continue
        buf.append(line)
        full = " ".join(buf)
        buf = []
        yield full.lstrip().startswith("#"), full


def scan_decls(text):
    """Declared names in one C file: {name: set of kinds}.

    Kinds: func (a definition, with body), proto (a prototype), var (a
    file-scope variable), macro (#define), typedef, tag, enum, member.
    Conditional branches restart from the brace state at their #if so
    that #if/#else pairs that each open a brace stay balanced.
    """
    decls = defaultdict(set)
    stack = []  # open braces: extern, struct, enum, block
    cond = []  # [stack at #if, (stack, paren) after the first branch, paren at #if]
    paren = 0
    typedef = False
    prev = prev2 = ""
    pending = ""  # the last prototype at depth 0, a definition if "{" follows
    for directive, line in logical_lines(strip_c(text)):
        if directive:
            d = line.strip()[1:].strip()
            word = d.split(None, 1)[0] if d else ""
            if word == "define":
                m = re.match(r"define\s+([A-Za-z_]\w*)", d)
                if m:
                    decls[m.group(1)].add("macro")
            elif word in ("if", "ifdef", "ifndef"):
                cond.append([list(stack), None, paren])
            elif word in ("elif", "else") and cond:
                if cond[-1][1] is None:
                    cond[-1][1] = (list(stack), paren)
                stack = list(cond[-1][0])
                paren = cond[-1][2]
            elif word == "endif" and cond:
                top = cond.pop()
                if top[1] is not None:
                    stack, paren = list(top[1][0]), top[1][1]
            continue
        for tok in TOKEN.findall(line):
            depth = sum(1 for k in stack if k != "extern")
            # the token after a candidate decides what the candidate was
            if CAMEL.match(prev):
                name, before = prev, prev2
                inner = stack[-1] if stack else ""
                declarator = IDENT.fullmatch(before) or before in ("*", ")", ",")
                if depth == 0 and paren == 0:
                    if before in ("struct", "union", "enum"):
                        decls[name].add("tag")
                    elif typedef:
                        decls[name].add("typedef")
                    elif tok == "(" and declarator:
                        decls[name].add("proto")
                        pending = name
                    elif tok in (";", "=", "[", ",") and declarator:
                        decls[name].add("var")
                elif depth == 0 and paren == 1 and before == "*" and tok == ")":
                    decls[name].add("typedef" if typedef else "var")
                elif depth == 0 and paren == 1 and before == "*" and tok == "(" and not typedef:
                    # a function returning a pointer to an array: T (*f(args))[n]
                    decls[name].add("proto")
                    pending = name
                elif inner == "enum" and tok in (",", "=", "}"):
                    decls[name].add("enum")
                elif inner == "struct" and tok in (";", "[", ",", ":", ")") and declarator:
                    decls[name].add("member")
            if tok == "{":
                if prev.startswith('"') and prev2 == "extern":
                    stack.append("extern")
                elif "enum" in (prev, prev2):
                    stack.append("enum")
                elif "struct" in (prev, prev2) or "union" in (prev, prev2):
                    stack.append("struct")
                else:
                    if depth == 0 and paren == 0 and pending and prev in (")", "]"):
                        decls[pending].discard("proto")
                        decls[pending].add("func")
                    stack.append("block")
                pending = ""
            elif tok == "}":
                if stack:
                    stack.pop()
            elif tok == "(":
                paren += 1
            elif tok == ")":
                paren = max(0, paren - 1)
            elif tok == ";" and depth == 0 and paren == 0:
                typedef, pending = False, ""
            elif tok == "typedef" and depth == 0:
                typedef = True
            prev2, prev = prev, tok
    return decls


# ---------------------------------------------------------------- table


class Scan:
    """Everything the table, the collision check and --check need."""

    def __init__(self, root):
        self.root = root
        self.port_decls = defaultdict(set)  # name -> kinds declared in port
        self.port_where = defaultdict(set)  # name -> port files declaring it
        self.game_defs = set()  # names ico2/sce define (or use in asm)
        self.keep_decls = set()  # names declared in keep-list C files
        self.words = Counter()  # every identifier word in the scanned trees
        self.code_words = set()  # candidates seen in C code (not comments)
        self.c_idents = set()  # every identifier in C code, all trees
        files = list_files(root, SCAN_TREES, SCAN_FILES)
        self.texts = {}
        for rel in files:
            text = read_text(root, rel)
            if text is None:
                continue
            self.texts[rel] = text
            if rel in SELF_FILES:
                continue
            self.words.update(IDENT.findall(text))
            top = rel.split("/", 1)[0]
            if rel.endswith(C_EXTS):
                self.c_idents.update(IDENT.findall(strip_c(text)))
            if top == "port" and rel.endswith(C_EXTS):
                decls = scan_decls(text)
                if is_keep(rel) or rel.startswith(KEEP_DECL_PREFIXES):
                    self.keep_decls.update(n for n in decls if CAMEL.match(n))
                    continue
                for name, kinds in decls.items():
                    if CAMEL.match(name):
                        self.port_decls[name] |= kinds
                        self.port_where[name].add(rel)
                self.code_words.update(
                    w for w in IDENT.findall(strip_c(text)) if CAMEL.match(w)
                )
            elif top == "port" and rel.endswith(".py") and not is_keep(rel):
                # a generator's C prototypes, one per line (embed_shaders.py)
                protos = "\n".join(l for l in text.split("\n") if C_PROTO.match(l))
                for name, kinds in scan_decls(protos).items():
                    if CAMEL.match(name) and "proto" in kinds:
                        self.port_decls[name].add("proto")
                        self.port_where[name].add(rel)
            elif top in GAME_TREES and rel.endswith(C_EXTS):
                for name, kinds in scan_decls(text).items():
                    if kinds & {"func", "var", "macro"}:
                        self.game_defs.add(name)
            elif top in GAME_TREES and rel.endswith(ASM_EXTS):
                self.game_defs.update(w for w in IDENT.findall(text) if CAMEL.match(w))

    def table(self):
        """{old: new} for the renamed names."""
        out = {}
        for name, kinds in self.port_decls.items():
            if not kinds & {"func", "proto", "var", "macro"}:
                continue
            if name in self.game_defs or name in self.keep_decls:
                continue
            out[name] = convert(name)
        return out

    def reason(self, name):
        """Why a candidate name stays (for --check)."""
        kinds = self.port_decls.get(name, set())
        if name in self.game_defs:
            return "game (ico2/sce defines it)"
        if name in self.keep_decls:
            return "vendored or generated (third_party, math/newlib, gen)"
        if kinds & {"func", "proto", "var", "macro"}:
            return "PORT-DECLARED"
        if kinds & {"typedef", "tag"}:
            return "type or tag"
        if "enum" in kinds:
            return "enum constant"
        if "member" in kinds:
            return "struct member"
        if name in self.code_words:
            return "no port declaration (local, parameter, a library's member)"
        return "text only (comments, strings, file names)"


def collisions(scan, table, accept):
    """Lines describing every collision; empty when there is none.

    A name in accept (--accept) may already be a word in the trees, as a
    file or test name, but never as an identifier in C code: that stays a
    collision whatever accept says.
    """
    errs = []
    by_new = defaultdict(list)
    for old, new in table.items():
        by_new[new].append(old)
    for new, olds in sorted(by_new.items()):
        if len(olds) > 1:
            errs.append(f"{new}: from {' and '.join(sorted(olds))}")
    for old, new in sorted(table.items()):
        n = scan.words.get(new)
        if not n:
            continue
        if new in accept and new not in scan.c_idents:
            continue
        where = "a C identifier" if new in scan.c_idents else "a word"
        errs.append(f"{new} (from {old}): already used as {where} ({n}x)")
    for new in sorted(set(accept) - set(table.values())):
        errs.append(f"{new}: --accept names no new name of the table")
    return errs


def write_table(path, table):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for old in sorted(table):
            f.write(f"{old} {table[old]}\n")


def read_table(path):
    table = {}
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            parts = line.split()
            if not parts:
                continue
            if len(parts) != 2 or parts[0] in table:
                sys.exit(f"rename: {path}:{n}: expected one new 'old new' pair")
            table[parts[0]] = parts[1]
    return table


# ---------------------------------------------------------------- apply


def apply_files(root, table):
    """The relative paths --apply rewrites."""
    files = [
        rel
        for rel in list_files(root, APPLY_TREES, APPLY_FILES)
        if not is_keep(rel)
    ]
    for rel in list_files(root, GAME_TREES, ()):
        text = read_text(root, rel)
        if text is not None and any(w in table for w in IDENT.findall(text)):
            files.append(rel)
    return sorted(set(files))


def paste_sub(rel, text, table):
    """Rewrite the pasted suffixes PASTE_SITES names in one file."""
    total = 0
    for path, prefix, pattern in PASTE_SITES:
        if rel != path:
            continue

        def sub(m):
            nonlocal total
            new = table.get(prefix + m.group(1))
            if new is None or not new.startswith(prefix):
                return m.group(0)
            total += 1
            a, b = m.span(1)
            s0 = m.start(0)
            return m.group(0)[: a - s0] + new[len(prefix) :] + m.group(0)[b - s0 :]

        text = re.sub(pattern, sub, text)
    return text, total


def do_apply(root, table):
    total = 0
    changed = []
    per_name = Counter()
    for rel in apply_files(root, table):
        text = read_text(root, rel)
        if text is None:
            continue
        count = 0

        def sub(m):
            nonlocal count
            new = table.get(m.group(0))
            if new is None:
                return m.group(0)
            count += 1
            per_name[m.group(0)] += 1
            return new

        out = IDENT.sub(sub, text)
        out, n = paste_sub(rel, out, table)
        count += n
        if count:
            write_text(root, rel, out)
            changed.append((rel, count))
            total += count
    trees = Counter()
    for rel, count in changed:
        trees[rel.split("/", 1)[0] if "/" in rel else rel] += 1
    print(f"rename: {total} replacements in {len(changed)} files")
    for tree, n in sorted(trees.items()):
        print(f"  {tree}: {n} files")
    game = [(rel, c) for rel, c in changed if rel.split("/", 1)[0] in GAME_TREES]
    if game:
        print("  game files:")
        for rel, c in game:
            print(f"    {rel}: {c}")
    unused = sorted(set(table) - set(per_name))
    if unused:
        print(f"  table names with no occurrence: {' '.join(unused)}")
    return 0


# ---------------------------------------------------------------- check


def do_check(root, table):
    scan = Scan(root)
    left = defaultdict(Counter)  # name -> files
    for rel in apply_files(root, table):
        text = scan.texts.get(rel) or read_text(root, rel)
        if text is None:
            continue
        for w in IDENT.findall(text):
            if CAMEL.match(w):
                left[w][rel] += 1
    bad = 0
    for path, prefix, pattern in PASTE_SITES:
        text = read_text(root, path) if (root / path).is_file() else ""
        for m in re.finditer(pattern, text or ""):
            if prefix + m.group(1) in table:
                print(f"OLD PASTED SUFFIX: {path}: {m.group(1)}")
                bad += 1
    families = defaultdict(list)
    for name in sorted(left):
        reason = "OLD TABLE NAME" if name in table else scan.reason(name)
        if reason in ("OLD TABLE NAME", "PORT-DECLARED"):  # the table is stale
            bad += 1
        families[reason].append(name)
    for reason in sorted(families):
        names = families[reason]
        print(f"{reason}: {len(names)}")
        for name in names:
            files = left[name]
            where = ", ".join(sorted(files)[:3]) + (" ..." if len(files) > 3 else "")
            print(f"  {name}  {sum(files.values())}x  {where}")
    if bad:
        print(f"rename: {bad} names left that the rename owns", file=sys.stderr)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n", 1)[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--table", metavar="OUT", help="scan and write the table")
    mode.add_argument("--apply", metavar="TABLE", help="rename the table's names")
    mode.add_argument("--check", metavar="TABLE", help="list what is left")
    ap.add_argument(
        "--accept",
        metavar="NEW",
        action="append",
        default=[],
        help="with --table: a new name that may already be a word (a file or "
        "test name) but not a C identifier; repeat per name",
    )
    ap.add_argument("--root", default=str(ROOT), help="tree to work on (default: this repo)")
    args = ap.parse_args()
    root = Path(args.root).resolve()

    if args.table:
        scan = Scan(root)
        table = scan.table()
        errs = collisions(scan, table, set(args.accept))
        if errs:
            print(f"rename: {len(errs)} collisions, no table written:", file=sys.stderr)
            for e in errs:
                print(f"  {e}", file=sys.stderr)
            return 1
        write_table(args.table, table)
        prefixes = Counter(CAMEL.match(old).group(1) + CAMEL.match(old).group(2) for old in table)
        print(f"rename: {len(table)} names in {args.table}")
        for p, n in sorted(prefixes.items()):
            print(f"  {p}: {n}")
        return 0
    if args.apply:
        return do_apply(root, read_table(args.apply))
    return do_check(root, read_table(args.check))


if __name__ == "__main__":
    sys.exit(main())
