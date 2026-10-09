#!/usr/bin/env python3
"""check_call_types.py: the game's calls and declarations agree with the definitions.

The game was decompiled from a PS2 program where an argument is a register
the caller fills and the callee reads.  On x86-64 and arm64 a callee that
reads an argument its caller never passed gets whatever the register held
(issue 20: default_item_select called the item-select callback with no
argument; the callback indexed a table with a pointer's low half and ended
the run on a phone).  Two kinds of source finding hide this:

  (a) a function-pointer cast called with fewer arguments than the function
      it reaches defines: ((T (*)(ARGS))expr)(CALLARGS).  The target is the
      named function, or the functions ever assigned to the variable (or
      handed to the setter that stores it).
  (b) an `extern` declaration inside a .c file whose parameter count,
      integer parameter widths (char/short/int/long) or return width differ
      from the definition found elsewhere under ico2/.  Pointers all compare
      equal ("ptr"); signedness and qualifiers are ignored.

usage: check_call_types.py [--allow FILE] [ico2-dir]
Prints each finding as `file:line: message` and exits 1 when there is one
that tools/check_call_types_allow.txt (one `symbol  reason` per line) does
not list.  Files are read as latin-1: several are EUC-JP.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'else', 'do', 'sizeof', 'case', 'defined'}
INT_WORDS = {'char', 'short', 'int', 'long'}
QUALS = {'const', 'volatile', 'signed', 'unsigned', 'register', 'static', 'inline', 'extern',
         '__inline', '__inline__', 'struct', 'enum', 'union'}


def strip(text):
    """comments and string/char literals blanked, newlines kept"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == '/*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif two == '//':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + ' ' * (j - i - 1) + (c if j < n and text[j] == c else ''))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def balanced(text, i):
    """index just past the ')' matching the '(' at text[i], or -1"""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '(':
            depth += 1
        elif text[j] == ')':
            depth -= 1
            if depth == 0:
                return j + 1
    return -1


def split_args(s):
    """top-level comma split"""
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += c
    if cur.strip() or out:
        out.append(cur)
    return [a.strip() for a in out]


class Source:
    def __init__(self, rel, text):
        self.rel = rel
        self.text = strip(text)
        self.is_c = rel.endswith('.c') or rel.endswith('.c.inc')

    def line(self, pos):
        return self.text.count('\n', 0, pos) + 1


class Typedefs:
    """simple integer typedefs, resolved to their width word"""

    def __init__(self, sources):
        self.map = {}
        pat = re.compile(r'typedef\s+((?:(?:unsigned|signed|short|long|int|char|\w+)\s+)*?)(\w+)\s*;')
        for s in sources:
            for m in pat.finditer(s.text):
                self.map.setdefault(m.group(2), m.group(1).split())

    def width(self, words):
        """char/short/int/long/ll for integer type words, else None"""
        words = list(words)
        for _ in range(8):
            exp = []
            changed = False
            for w in words:
                if w in self.map and w not in INT_WORDS:
                    exp += self.map[w]
                    changed = True
                else:
                    exp.append(w)
            words = exp
            if not changed:
                break
        ints = [w for w in words if w in INT_WORDS]
        if not ints and 'unsigned' in words:
            return 'int'
        if ints.count('long') >= 2:
            return 'll'
        if ints and all(w in INT_WORDS or w in QUALS for w in words):
            for w in ('char', 'short', 'long'):
                if w in ints:
                    return w
            return 'int'
        return None


def norm_type(words, td):
    """a type's words (no declarator name) to a comparable string"""
    words = [w for w in words if w not in ('const', 'volatile', 'register', 'static', 'inline',
                                           'extern', '__inline', '__inline__')]
    w = td.width(words)
    if w:
        return w
    words = [x for x in words if x not in ('signed', 'unsigned')]
    return ' '.join(words) or 'int'


def norm_param(p, td):
    if p == '...':
        return '...'
    if p.startswith('ICO_WORD') or '*' in p or '[' in p or '(' in p:
        return 'ptr'
    words = re.findall(r'\w+', p)
    if len(words) > 1 and words[-1] not in INT_WORDS and words[-1] not in (
            'unsigned', 'signed', 'float', 'double', 'void'):
        # the last word is the parameter's name unless the type needs it
        if not (words[-2] in ('struct', 'enum', 'union') and len(words) == 2):
            words = words[:-1]
    return norm_type(words, td)


def parse_params(s, td):
    """None for an unspecified list `()`, else the list of normalised types"""
    s = s.strip()
    if s == '':
        return None
    if s == 'void':
        return []
    return [norm_param(p, td) for p in split_args(s)]


def norm_ret(prefix, td):
    if '*' in prefix or prefix.strip().startswith('ICO_WORD_PTR'):
        return 'ptr'
    words = re.findall(r'\w+', prefix)
    if words and words[0] == 'ICO_WORD_PTR':
        return 'ptr'
    return norm_type(words, td)


DEF = re.compile(r'^([A-Za-z_][^\n;{}()=]*?)\b([A-Za-z_]\w*)\s*\(', re.M)
EXT = re.compile(r'\bextern\s+([^;{}()=]*?)\b([A-Za-z_]\w*)\s*\(')


def find_defs(sources, td):
    """name -> list of (source, line, ret, params, static)"""
    defs = {}
    for s in sources:
        for m in DEF.finditer(s.text):
            name = m.group(2)
            prefix = m.group(1)
            if name in KEYWORDS or prefix.split()[0] in KEYWORDS or 'typedef' in prefix:
                continue
            end = balanced(s.text, m.end() - 1)
            if end < 0:
                continue
            rest = s.text[end:end + 40].lstrip()
            if not rest.startswith('{'):
                continue
            params = s.text[m.end():end - 1]
            static = 'static' in prefix.split()
            defs.setdefault(name, []).append(
                (s, s.line(m.start()), norm_ret(prefix, td), parse_params(params, td), static,
                 params))
    return defs


def check_externs(sources, defs, td, out):
    for s in sources:
        if not s.is_c:
            continue
        for m in EXT.finditer(s.text):
            name = m.group(2)
            end = balanced(s.text, m.end() - 1)
            if end < 0 or not s.text[end:].lstrip().startswith(';'):
                continue
            targets = [d for d in defs.get(name, []) if not d[4] and d[0] is not s]
            if not targets or '(*' in m.group(1):
                continue
            ret = norm_ret(m.group(1), td)
            params = parse_params(s.text[m.end():end - 1], td)
            problems = []
            for d in targets:
                p = []
                if ret != d[2]:
                    p.append('returns %s, defined as %s' % (ret, d[2]))
                if params is not None and d[3] is not None:
                    if len(params) != len(d[3]):
                        p.append('takes %d parameter(s), defined with %d' % (len(params), len(d[3])))
                    else:
                        for i, (a, b) in enumerate(zip(params, d[3])):
                            if a != b and a in INT_WORDS | {'ll'} and b in INT_WORDS | {'ll'}:
                                p.append('parameter %d is %s, defined as %s' % (i + 1, a, b))
                            elif a != b and (a == 'ptr') != (b == 'ptr'):
                                p.append('parameter %d is %s, defined as %s' % (i + 1, a, b))
                problems.append((p, d))
            if all(p for p, _ in problems):
                p, d = problems[0]
                out.append((s.rel, s.line(m.start()), name,
                            'extern %s: %s (%s:%d)' % (name, '; '.join(p), d[0].rel, d[1])))


ARG_NAME = re.compile(r'^(?:\([^()]*(?:\([^()]*\)[^()]*)*\)\s*)*&?\s*(\w+)$')
CAST = re.compile(r'\(\s*\(\s*[\w\s\*]*?\(\s*\*\s*\)\s*\(')


def assigned_targets(name, sources, defs):
    """function names a variable is ever assigned (directly or through a setter)"""
    found = set()
    asg = re.compile(r'\b%s\s*=\s*([A-Za-z_]\w*)\s*;' % re.escape(name))
    for s in sources:
        for m in asg.finditer(s.text):
            v = m.group(1)
            if v in defs:
                found.add(v)
                continue
            # a parameter of the function that stores it: its callers' arguments
            fn = None
            for dn, dl in defs.items():
                for d in dl:
                    if d[0] is s and d[1] <= s.line(m.start()):
                        if fn is None or d[1] > fn[1]:
                            fn = (dn, d[1], d[5])
            if fn and re.search(r'\b%s\b' % re.escape(v), fn[2]):
                idx = [i for i, a in enumerate(split_args(fn[2]))
                       if re.findall(r'\w+', a)[-1:] == [v]]
                if not idx:
                    continue
                call = re.compile(r'\b%s\s*\(' % re.escape(fn[0]))
                for s2 in sources:
                    for cm in call.finditer(s2.text):
                        e = balanced(s2.text, cm.end() - 1)
                        args = split_args(s2.text[cm.end():e - 1]) if e > 0 else []
                        if idx[0] >= len(args):
                            continue
                        # the argument, minus casts in front of the name
                        am = ARG_NAME.match(args[idx[0]])
                        if am and am.group(1) in defs:
                            found.add(am.group(1))
    return found


def check_casts(sources, defs, td, out):
    for s in sources:
        for m in CAST.finditer(s.text):
            # ((T (*)(ARGS)) EXPR) (CALLARGS)
            open_inner = m.end() - 1
            end_args = balanced(s.text, open_inner)
            if end_args < 0:
                continue
            cast_args = s.text[open_inner + 1:end_args - 1]
            rest = s.text[end_args:]
            mm = re.match(r'\s*\)\s*(\w+)\s*\)\s*\(', rest)
            if not mm:
                continue
            expr = mm.group(1)
            call_open = end_args + mm.end() - 1
            call_end = balanced(s.text, call_open)
            if call_end < 0:
                continue
            call_args = split_args(s.text[call_open + 1:call_end - 1])
            cast_params = parse_params(cast_args, td)
            have = len(call_args)
            if cast_params is not None:
                have = min(have, len(cast_params))
            targets = {expr} if expr in defs else assigned_targets(expr, sources, defs)
            for t in sorted(targets):
                for d in defs.get(t, []):
                    if d[3] is not None and len(d[3]) > have:
                        out.append((s.rel, s.line(m.start()), t,
                                    'call through (%s) passes %d argument(s) but %s (%s:%d) '
                                    'takes %d' % (expr, have, t, d[0].rel, d[1], len(d[3]))))
                        break


def load_allow(path):
    allowed = set()
    if os.path.exists(path):
        for ln in open(path, encoding='latin-1'):
            ln = ln.strip()
            if ln and not ln.startswith('#'):
                allowed.add(ln.split()[0])
    return allowed


def main(argv):
    allow = os.path.join(ROOT, 'tools', 'check_call_types_allow.txt')
    args = argv[1:]
    if args[:1] == ['--allow']:
        allow = args[1]
        args = args[2:]
    root = args[0] if args else os.path.join(ROOT, 'ico2')
    sources = []
    for d, _, files in os.walk(root):
        for f in sorted(files):
            if f.endswith(('.c', '.h', '.inc')):
                p = os.path.join(d, f)
                with open(p, 'rb') as fh:
                    sources.append(Source(os.path.relpath(p, os.path.dirname(root)),
                                          fh.read().decode('latin-1')))
    sources.sort(key=lambda s: s.rel)
    td = Typedefs(sources)
    defs = find_defs(sources, td)
    found = []
    check_casts(sources, defs, td, found)
    check_externs(sources, defs, td, found)
    allowed = load_allow(allow)
    bad = [f for f in sorted(set(found)) if f[2] not in allowed]
    for rel, line, sym, msg in bad:
        print('%s:%d: %s' % (rel, line, msg))
    if bad:
        print('check_call_types: %d finding(s); fix the call or list the symbol in '
              'tools/check_call_types_allow.txt with a reason' % len(bad))
        return 1
    print('check_call_types: ok (%d functions, %d allowed)' % (len(defs), len(found)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
