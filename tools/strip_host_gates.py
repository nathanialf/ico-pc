#!/usr/bin/env python3
"""Resolve every ICO_HOST preprocessor conditional in the game sources.

ico2/ and sce/ are the port's own source (docs/BUILDING.md, "The game
code"): they are only ever compiled for the host, with ICO_HOST defined, so a
conditional on ICO_HOST has one live arm. This tool keeps that arm and drops
the other, together with the #if/#else/#endif lines themselves:

  #ifdef ICO_HOST  A #else B #endif   -> A
  #ifndef ICO_HOST A #endif           -> (nothing)
  #ifndef ICO_HOST A #else B #endif   -> B
  #if defined(ICO_HOST) || X          -> the #if arm
  #if defined(ICO_HOST) && X          -> #if X (the directive is rewritten)
  #if !defined(ICO_HOST) ...          -> resolved the same way

Conditionals on anything else (ICO_RD, ICO_HEADLESS, ...) are kept, also when
nested inside or around an ICO_HOST one. An #elif in an ICO_HOST conditional
is not handled: the tool stops and names the line.

The files are EUC-JP, so the tool works on bytes and never decodes; every
byte outside the resolved conditionals is kept. Where removing a directive
would leave two blank lines in a row, one is dropped. A comment on a removed
directive line that belongs to the kept arm is kept as a line of its own; a
whole-line comment that only said "PC port:" is dropped once it is next to
no conditional.

  tools/strip_host_gates.py            rewrite the files, print sites per file
  tools/strip_host_gates.py --dry-run  print sites per file, change nothing
  tools/strip_host_gates.py --check    exit 1 if any ICO_HOST conditional
                                       remains under ico2/ or sce/
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("ico2", "sce")
EXTS = (".c", ".h", ".inc", ".s", ".S")

DIRECTIVE = re.compile(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", re.S)
HOST_COND = re.compile(rb"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif)\b.*\bICO_HOST\b(?!_)")
ORPHAN = re.compile(rb"^[ \t]*(/\*[ \t]*(PC port|port)[ \t]*:?[ \t]*\*/|//[ \t]*(PC port|port)[ \t]*:?)[ \t]*\r?\n?$")


class StripError(Exception):
    pass


def split_lines(data):
    lines = data.split(b"\n")
    out = [l + b"\n" for l in lines[:-1]]
    if lines[-1]:
        out.append(lines[-1])
    return out


def comment_state(lines):
    """For each line: True when the line starts inside a block comment."""
    states = []
    in_comment = False
    for line in lines:
        states.append(in_comment)
        i = 0
        n = len(line)
        quote = None
        while i < n:
            c = line[i:i + 1]
            if in_comment:
                if line.startswith(b"*/", i):
                    in_comment = False
                    i += 2
                    continue
                i += 1
                continue
            if quote:
                if c == b"\\":
                    i += 2
                    continue
                if c == quote:
                    quote = None
                i += 1
                continue
            if line.startswith(b"/*", i):
                in_comment = True
                i += 2
                continue
            if line.startswith(b"//", i):
                break
            if c in (b'"', b"'"):
                quote = c
            i += 1
    return states


def logical_directives(lines, states):
    """Yield (start, end, text) for each preprocessor line (end exclusive),
    joining backslash continuations."""
    i = 0
    n = len(lines)
    while i < n:
        if not states[i] and lines[i].lstrip(b" \t").startswith(b"#"):
            j = i
            text = lines[i].rstrip(b"\r\n")
            while text.endswith(b"\\") and j + 1 < n:
                j += 1
                text = text[:-1] + b" " + lines[j].rstrip(b"\r\n")
            yield i, j + 1, text
            i = j + 1
        else:
            i += 1


def split_comment(text):
    """Split a directive's text into (code, trailing comment or b"")."""
    m = re.search(rb"(/\*.*\*/|//.*)[ \t]*$", text)
    if not m:
        return text, b""
    return text[: m.start()].rstrip(), m.group(1)


def eval_host(kind, rest, where):
    """Resolve a conditional mentioning ICO_HOST with ICO_HOST defined.
    Returns (value, rewritten) where value is True/False when the condition
    is decided, or None with `rewritten` the directive's new condition."""
    rest, _ = split_comment(rest)
    rest = rest.strip()
    if kind == b"ifdef":
        if rest != b"ICO_HOST":
            raise StripError("%s: unexpected #ifdef %r" % (where, rest))
        return True, None
    if kind == b"ifndef":
        if rest != b"ICO_HOST":
            raise StripError("%s: unexpected #ifndef %r" % (where, rest))
        return False, None
    if kind != b"if":
        raise StripError("%s: #%s on ICO_HOST: resolve by hand" % (where, kind.decode()))
    expr = re.sub(rb"defined[ \t]*\([ \t]*ICO_HOST[ \t]*\)|defined[ \t]+ICO_HOST\b", b"1", rest)
    expr = re.sub(rb"\bICO_HOST\b", b"1", expr)
    expr = re.sub(rb"![ \t]*1\b", b"0", expr)
    expr = expr.strip()
    while expr.startswith(b"(") and expr.endswith(b")"):
        expr = expr[1:-1].strip()
    if expr in (b"1", b"0"):
        return expr == b"1", None
    if b"(" in expr.replace(b"defined(", b"") or (b"||" in expr and b"&&" in expr):
        raise StripError("%s: #if %r: resolve by hand" % (where, rest))
    if b"||" in expr:
        terms = [t.strip() for t in expr.split(b"||")]
        if b"1" in terms:
            return True, None
        terms = [t for t in terms if t != b"0"]
        return None, b" || ".join(terms) if terms else None
    if b"&&" in expr:
        terms = [t.strip() for t in expr.split(b"&&")]
        if b"0" in terms:
            return False, None
        terms = [t for t in terms if t != b"1"]
        return None, b" && ".join(terms)
    raise StripError("%s: #if %r: resolve by hand" % (where, rest))


def strip(data, path):
    """Return (new bytes, number of ICO_HOST conditionals resolved, notes)."""
    lines = split_lines(data)
    states = comment_state(lines)
    directives = {s: (e, t) for s, e, t in logical_directives(lines, states)}

    out = []           # list of [line bytes, junction flag]
    stack = []         # frames: dict(host, keep_arm, value)
    sites = 0
    notes = []
    junction = False   # a directive was just removed

    def live():
        return all(f["keep"] for f in stack if f["host"])

    def emit(line):
        nonlocal junction
        blank = line.strip() == b""
        if junction and blank and (not out or out[-1].strip() == b""):
            return
        if junction and not blank and ORPHAN.match(line):
            return
        if not blank:
            junction = False
        out.append(line)

    def removed():
        nonlocal junction
        # a "PC port:" comment left with nothing to introduce
        k = len(out) - 1
        while k >= 0 and out[k].strip() == b"":
            k -= 1
        if k >= 0 and ORPHAN.match(out[k]):
            del out[k]
            notes.append("dropped orphan comment before line")
        junction = True

    i = 0
    n = len(lines)
    while i < n:
        if i in directives:
            end, text = directives[i]
            where = "%s:%d" % (path, i + 1)
            m = DIRECTIVE.match(text)
            if m:
                kind, rest = m.group(1), m.group(2)
                is_host = HOST_COND.match(text) is not None
                if kind in (b"if", b"ifdef", b"ifndef"):
                    if is_host:
                        value, rewritten = eval_host(kind, rest, where)
                        if value is None:
                            # partially decided: keep a rewritten #if
                            stack.append({"host": False})
                            if live():
                                indent = re.match(rb"^[ \t]*#[ \t]*", text).group(0)
                                _, cmt = split_comment(rest)
                                line = indent + b"if " + rewritten
                                if cmt:
                                    line += b" " + cmt
                                emit(line + b"\n")
                                notes.append("%s: rewritten to #if %s" % (where, rewritten.decode()))
                            sites += 1
                            i = end
                            continue
                        sites += 1
                        outer = live()
                        stack.append({"host": True, "keep": value, "value": value})
                        if outer:
                            removed()
                            _, cmt = split_comment(rest)
                            if cmt and value:
                                emit(re.match(rb"^[ \t]*", text).group(0) + cmt + b"\n")
                                notes.append("%s: directive comment kept as a line" % where)
                        i = end
                        continue
                    stack.append({"host": False})
                elif kind == b"elif":
                    if not stack:
                        raise StripError("%s: #elif without #if" % where)
                    if stack[-1]["host"] or is_host:
                        raise StripError("%s: #elif in an ICO_HOST conditional: resolve by hand" % where)
                elif kind == b"else":
                    if not stack:
                        raise StripError("%s: #else without #if" % where)
                    f = stack[-1]
                    if f["host"]:
                        f["keep"] = not f["value"]
                        if all(g["keep"] for g in stack[:-1] if g["host"]):
                            removed()
                            _, cmt = split_comment(rest)
                            if cmt and f["keep"] and not re.search(rb"ICO_HOST", cmt):
                                emit(re.match(rb"^[ \t]*", text).group(0) + cmt + b"\n")
                        i = end
                        continue
                elif kind == b"endif":
                    if not stack:
                        raise StripError("%s: #endif without #if" % where)
                    f = stack.pop()
                    if f["host"]:
                        if live():
                            removed()
                        i = end
                        continue
            if live():
                for k in range(i, end):
                    emit(lines[k])
            i = end
            continue
        if live():
            emit(lines[i])
        i += 1
    if stack:
        raise StripError("%s: unterminated conditional" % path)
    # a conditional that ended the file leaves no blank last line behind
    while junction and len(out) > 1 and out[-1].strip() == b"" and not data.endswith(b"\n\n"):
        out.pop()
    return b"".join(out), sites, notes


def files():
    for d in DIRS:
        top = os.path.join(ROOT, d)
        if not os.path.isdir(top):
            continue
        for dirpath, dirnames, filenames in os.walk(top):
            dirnames.sort()
            for f in sorted(filenames):
                if f.endswith(EXTS):
                    yield os.path.join(dirpath, f)


def remaining(data):
    lines = split_lines(data)
    states = comment_state(lines)
    hits = []
    for s, _, text in logical_directives(lines, states):
        if HOST_COND.match(text):
            hits.append(s + 1)
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="fail if an ICO_HOST conditional remains")
    ap.add_argument("--dry-run", action="store_true", help="count the sites, change nothing")
    args = ap.parse_args()

    if args.check:
        bad = 0
        for p in files():
            with open(p, "rb") as fh:
                for ln in remaining(fh.read()):
                    print("%s:%d: ICO_HOST conditional" % (os.path.relpath(p, ROOT), ln))
                    bad += 1
        if bad:
            print("strip_host_gates: %d ICO_HOST conditional(s) remain; run tools/strip_host_gates.py" % bad)
            return 1
        print("strip_host_gates: no ICO_HOST conditional under %s" % ", ".join(DIRS))
        return 0

    per_dir = {}
    total = 0
    failed = False
    for p in files():
        rel = os.path.relpath(p, ROOT)
        with open(p, "rb") as fh:
            data = fh.read()
        if b"ICO_HOST" not in data:
            continue
        try:
            new, sites, notes = strip(data, rel)
        except StripError as e:
            print("error: %s" % e)
            failed = True
            continue
        if sites == 0:
            continue
        print("%5d  %s" % (sites, rel))
        for note in notes:
            print("       note: %s" % note)
        total += sites
        top = rel.split(os.sep)[0]
        per_dir[top] = per_dir.get(top, 0) + sites
        if not args.dry_run and new != data:
            with open(p, "wb") as fh:
                fh.write(new)
    for d in sorted(per_dir):
        print("%5d  %s/ total" % (per_dir[d], d))
    print("%5d  sites" % total)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
