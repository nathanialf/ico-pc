#!/usr/bin/env python3
"""check_region_literals.py: port sites name disc data through the region table.

The port plays seven discs (config/regions.txt: pal, jpn, kor, ntsc, aug6,
jun26, cn) from one program.  Their tables differ in the middle: a layout, a
menu row (texProperty), a story flag, a stage or the confirm button has a
different number on another disc.  Issue 147 was this class: the New Game
screen's hand-off (la_host_new_game_go) raised gflag 382, the European disc's
start flag, on every disc, and the North American disc (349) never started.
The region table (port/core/region.h IcoRegionInfo, region_<slug>.c) holds
each such number per disc; the game code's own arms use the ICO_<SET>
predicates and PAL_<SET> selectors (ico2/common/include/region.h).

This tool finds the port sites that still write such a number as a literal:

  gflag     gflagOn/gflagOff/gflagChk(N)
  layout    texLayout[N], lt_switch_layout/lt_link_layout/lt_draw_layout(N),
            an identifier naming a layout compared with or set to N, and a
            `return N;` (N >= 2) in a port block of the layout procs' files
  property  texProperty[N], lt_mask_property/lt_default_mask_property/
            lt_property_visible/lt_reset_property_chain/lt_glow_sprite/
            lt_draw_primary_sprite/lt_texture_no_of_property(N),
            lt_current_property_item() compared with N
  stage     stageData[N], stageTable[N], stgmgrForceSwitch*(N), an
            identifier naming a stage compared with or set to N
  pad       pad[i].<field> & N (a pad bit as a number), and the port's own
            confirm/cancel spelled LT_PAD_CROSS/LT_PAD_CIRCLE outside the
            region's pad_confirm/pad_back (the Japanese, Korean and Asian
            discs confirm with Circle)

N is a decimal or hexadecimal literal; 0 and -1 are not numbers of a row in
a comparison or an assignment (they are "none"), so only the index and call
forms count them.  A line that also names ico_region_*, a PAL_<SET>
selector or an ICO_<SET> predicate is routed and not a finding; so is a
line in a function whose name ends in a disc's slug (_ntsc, _jun26,
_ntscaug6, ...: the function is that disc's own arm).

Port sites:
  * every C file under port/ but third_party/, data/gen/, the tests
    (*/test/, port/test/) and the region tables themselves
    (port/core/region_<slug>.c, region.c, region_table.c);
  * in the 17 game files that carry port hooks (GAME_FILES), the port's own
    code: a function or declaration under a top-level `PC port` comment, the
    statement or block under a `PC port` comment inside a function (to the
    next blank line, `case`, or the end of the enclosing block), a line with
    a trailing `PC port` comment, a function named la_host_*/ui_*/ico_*,
    and a line that calls a port hook (la_host_*, ui_*, ico_opt_*, ico_ach_*,
    ico_gs_signal, ico_region_*), and every line of PORT_FUNCS (game-named
    functions whose body is the port's).  There, a literal that is the
    European value of a region table field that differs between the discs
    (`value`: in a comparison, case, return, assignment or index, numbers of
    20 and up) is a finding as well: the 147 shape, read from the tables.

Each finding is `file:line: category: text`.  tools/check_region_literals_allow.txt
lists the ones checked by hand, one per line:

    <file>  <category>  <snippet>  --  <reason naming the discs' numbers>

<snippet> is a piece of the (comment-stripped, space-collapsed) line, so the
entry survives line moves; an entry that matches no finding is stale and an
error too, so the list never outlives the code.  The reason must name the
discs (a slug, "every disc" or "all seven").

usage: check_region_literals.py [--allow FILE] [--root DIR] [--list]
  --list   print every finding, allowed or not (the audit's raw list)
Exit 1 on an unlisted finding or a stale entry.  Files are read as latin-1
(several game files are EUC-JP); the tool never writes.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GAME_FILES = [
    'ico2/common/src/layout_action.c',
    'ico2/common/src/layout_texture.c',
    'ico2/common/src/kanbanBoot.c',
    'ico2/common/src/StageManager.c',
    'ico2/common/src/main.c',
    'ico2/common/src/debug.c',
    'ico2/common/src/backStage.c',
    'ico2/fumi/ios/mcard.c',
    'ico2/fumi/src/enemy_act.c',
    'ico2/fumi/src/commonact.c',
    'ico2/sugipon/src/weapon.c',
    'ico2/script/src/op.c',
    'ico2/script/src/end.c',
    'ico2/script/src/deja.c',
    'ico2/script/src/st13b.c',
    'ico2/omori/src/ebrain.c',
    'ico2/seki/src/BgAnimation.c',
]
# the files whose port blocks return layout numbers (the layout procs)
LAYOUT_PROC_FILES = {'ico2/common/src/layout_action.c', 'ico2/common/src/layout_texture.c'}

PORT_SKIP_DIRS = ('port/third_party/', 'port/data/gen/', 'port/test/')
PORT_SKIP_FILES = re.compile(r'^port/core/region(_[a-z0-9]+)?\.c$')

SLUGS = ('pal', 'jpn', 'kor', 'ntsc', 'aug6', 'jun26', 'cn')
# a function that is one disc's (or a set's) own arm: name_<slugs>
ARM_SUFFIX = re.compile(r'_(?:pal|jpn|kor|ntsc|aug6|jun26|cn)(?:pal|jpn|kor|ntsc|aug6|jun26|cn)*$')

LIT = r'(-?(?:0[xX][0-9A-Fa-f]+|\d+))\b'
ROUTED = re.compile(r'\bico_region_\w+|\bPAL_[A-Z0-9_]+\s*\(|\bICO_(?:PAL|JPN|KOR|NTSC|AUG6|JUN26|CN)[A-Z0-9]*\b'
                    r'|->\s*(?:pad_confirm|pad_back)\b|\blt_ext_(?:confirm_button|port_back_buttons|back_buttons)\s*\(')
HOOK_CALL = re.compile(r'\b(?:la_host_\w+|ui_\w+|ico_opt_\w+|ico_ach_\w+|ico_gs_signal|ico_region_\w+)\s*\(')
PORT_FN = re.compile(r'^(?:la_host_|ui_|ico_)')
# functions in the game files whose body the port wrote whole under a game
# name (the original's body is gone): every line is a port site
PORT_FUNCS = {
    'debug_TryToGetStartStage',  # debug.c: [dev] start_stage in place of the start-stage file
}

PATTERNS = [
    ('gflag', re.compile(r'\bgflag(?:On|Off|Chk)\s*\(\s*' + LIT), 'all'),
    ('layout', re.compile(r'\btexLayout\s*\[\s*' + LIT), 'all'),
    ('layout', re.compile(r'\blt_(?:switch|link|draw)_layout\s*\(\s*' + LIT), 'all'),
    ('layout', re.compile(r'\b\w*[Ll]ayout\w*\s*(?:==|!=|=(?!=))\s*' + LIT), 'row'),
    ('property', re.compile(r'\btexProperty\s*\[\s*' + LIT), 'all'),
    ('property', re.compile(r'\blt_(?:mask_property|default_mask_property|property_visible|reset_property_chain'
                            r'|glow_sprite|draw_primary_sprite|texture_no_of_property)\s*\(\s*' + LIT), 'all'),
    ('property', re.compile(r'\blt_current_property_item\s*\(\s*\)\s*(?:==|!=|<=|>=|<|>)\s*' + LIT), 'row'),
    ('stage', re.compile(r'\b(?:stageData|stageTable)\s*\[\s*' + LIT), 'all'),
    ('stage', re.compile(r'\bstgmgrForceSwitch\w*\s*\(\s*' + LIT), 'all'),
    ('stage', re.compile(r'\b\w*[Ss]tage(?:_?[Nn]o|Num|Id)?\s*(?:==|!=|<=|>=|<|>|=(?!=))\s*' + LIT), 'row'),
    ('pad', re.compile(r'\bpad\s*\[\s*\d+\s*\]\s*\.\s*\w+\s*&\s*~?\s*\(?\s*' + LIT), 'all'),
    ('pad', re.compile(r'\bLT_PAD_(?:CROSS|CIRCLE)\b()'), 'name'),
]
# identifiers that look like a stage/layout but are not a table number
NOT_IDS = re.compile(r'(?:Step|step|Status|status|Count|count|Timer|timer|Mode|mode|Flag|flag|Max|max|Stages|'
                     r'Done|done|Shader|shader|Sweep|Part|Model|Started|started)\b')


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


FN_DEF = re.compile(r'^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$')


def functions(code_lines):
    """per line: the name of the function whose body holds it (or None), and
    the line ranges of each top-level function {name: (head, end)}"""
    owner = [None] * len(code_lines)
    depth = 0
    cur = None
    pending = None
    for i, ln in enumerate(code_lines):
        if depth == 0:
            m = FN_DEF.match(ln)
            if m and not ln.lstrip().startswith(('#', 'typedef', 'struct', 'enum', 'union')) and '=' not in ln:
                pending = m.group(1)
            elif ln.strip().endswith(';'):
                pending = None
        opens = ln.count('{')
        closes = ln.count('}')
        if depth == 0 and opens and pending:
            cur = pending
            pending = None
        if cur is not None and (depth > 0 or opens):
            owner[i] = cur
        depth += opens - closes
        if depth <= 0:
            depth = 0
            if closes:
                cur = None
    return owner


def indent(s):
    return len(s) - len(s.lstrip(' \t'))


def game_port_lines(raw_lines, code_lines, owner):
    """the set of 0-based lines that are the port's own code in a game file"""
    keep = set()
    n = len(raw_lines)
    for i, ln in enumerate(code_lines):
        if HOOK_CALL.search(ln):
            keep.add(i)
        if owner[i] and (PORT_FN.match(owner[i]) or owner[i] in PORT_FUNCS):
            keep.add(i)
    # `PC port` comments: find them in the raw text
    i = 0
    while i < n:
        raw = raw_lines[i]
        pos = raw.find('PC port')
        if pos < 0:
            i += 1
            continue
        cstart = raw.rfind('/*', 0, pos)
        before = raw[:cstart] if cstart >= 0 else raw[:pos]
        if before.strip() and not before.strip().startswith('//'):
            keep.add(i)  # a trailing comment: its line
            i += 1
            continue
        # the comment's end
        j = i
        while j < n and '*/' not in raw_lines[j][(pos if j == i else 0):]:
            j += 1
        top = indent(raw) == 0 and owner[i] is None
        k = j + 1
        if top:
            # the declaration or function that follows
            depth = 0
            seen_body = False
            while k < n:
                c = code_lines[k]
                if not c.strip() and depth == 0 and not seen_body:
                    if k > j + 1:
                        break
                    k += 1
                    continue
                keep.add(k)
                depth += c.count('{') - c.count('}')
                if '{' in c:
                    seen_body = True
                if depth <= 0 and (seen_body or c.rstrip().endswith(';')):
                    if seen_body and depth <= 0:
                        break
                    if not seen_body and c.rstrip().endswith(';'):
                        # a run of declarations: keep to the blank line
                        if k + 1 < n and not code_lines[k + 1].strip():
                            break
                k += 1
        else:
            base = indent(raw)
            depth = 0
            opened = False
            while k < n:
                c = code_lines[k]
                s = c.strip()
                if depth == 0:
                    if not s:
                        break
                    if indent(c) < base or s.startswith(('case ', 'default:', 'default :')):
                        break
                    if s.startswith('}') and indent(c) <= base:
                        break
                keep.add(k)
                opens = c.count('{')
                depth += opens - c.count('}')
                if depth < 0:
                    break
                if opens:
                    opened = True
                if depth == 0 and opened:
                    break  # a compound statement: the comment is about it alone
                k += 1
        i = j + 1
    return keep


def scan_lines(rel, code_lines, sites, owner):
    found = []
    for i in sorted(sites):
        ln = code_lines[i]
        if not ln.strip() or ln.lstrip().startswith('#include'):
            continue
        if ROUTED.search(ln):
            continue
        fn = owner[i] if owner else None
        if fn and ARM_SUFFIX.search(fn):
            continue
        for cat, rx, kind in PATTERNS:
            if kind == 'name' and ln.lstrip().startswith('#'):
                continue  # the bit's own definition
            for m in rx.finditer(ln):
                lit = m.group(1)
                if kind == 'row':
                    if lit in ('0', '-1'):
                        continue
                    ident = m.group(0).split('=')[0].split('!')[0].split('<')[0].split('>')[0].strip()
                    if NOT_IDS.search(ident):
                        continue
                    if ident.startswith(('ICO_', 'RD_', 'VK_')):
                        continue
                found.append((rel, i + 1, cat, ' '.join(ln.split())))
                break
    return found


def scan_layout_returns(rel, code_lines, sites, owner):
    found = []
    rx = re.compile(r'\breturn\s+' + LIT + r'\s*;')
    for i in sorted(sites):
        ln = code_lines[i]
        m = rx.search(ln)
        if not m or ROUTED.search(ln):
            continue
        fn = owner[i]
        if fn and ARM_SUFFIX.search(fn):
            continue
        try:
            v = int(m.group(1), 0)
        except ValueError:
            continue
        if v < 2:
            continue
        found.append((rel, i + 1, 'layout', ' '.join(ln.split())))
    return found


VALUE_MIN = 20  # below it a number is too common to say anything
VALUE_SKIP = {'pad_confirm', 'pad_back'}  # the pad patterns cover these
VLIT = r'(0[xX][0-9A-Fa-f]+|\d+)(?![\w.])'
VALUE_CTX = [
    re.compile(r'(?:==|!=|<=|>=|<|>)\s*' + VLIT),
    re.compile(r'(?<![\w.])' + VLIT + r'\s*(?:==|!=|<=|>=|<(?!<)|>(?!>))'),
    re.compile(r'\bcase\s+' + VLIT),
    re.compile(r'\breturn\s+' + VLIT + r'\s*;'),
    re.compile(r'(?<![=!<>])=\s*' + VLIT + r'\s*;'),
    re.compile(r'(?<!\w)\w+\s*\[\s*' + VLIT + r'\s*\]'),
]
DECL = re.compile(r'^\s*(?:(?:static|const|unsigned|signed|extern|volatile)\s+)*'
                  r'(?:char|int|short|float|double|long|[A-Za-z_]\w*_t|[A-Z]\w*)\s+\**\w+\s*\[')


def region_values(root):
    """{number: [field, ...]}: the European disc's value of every region
    table field whose value is not the same on all seven discs"""
    vals = {}
    for slug in SLUGS:
        path = os.path.join(root, 'port', 'core', 'region_%s.c' % slug)
        if not os.path.exists(path):
            return {}
        t = strip(read(path))
        vals[slug] = {m.group(1): int(m.group(2), 0)
                      for m in re.finditer(r'\.(\w+)\s*=\s*(-?(?:0[xX][0-9a-fA-F]+|\d+))\s*[,}]', t)}
    out = {}
    for field, v in vals['pal'].items():
        if field in VALUE_SKIP or v < VALUE_MIN:
            continue
        if len({vals[s].get(field) for s in SLUGS}) > 1:
            out.setdefault(v, []).append(field)
    return out


def scan_values(rel, code_lines, sites, owner, values):
    """a literal that is the European value of a per-disc field, in a
    comparison, case, return, assignment or index, at a game file's port site"""
    found = []
    for i in sorted(sites):
        ln = code_lines[i]
        if ROUTED.search(ln) or ln.lstrip().startswith('#') or DECL.match(ln):
            continue
        fn = owner[i]
        if fn and ARM_SUFFIX.search(fn):
            continue
        hit = None
        for rx in VALUE_CTX:
            for m in rx.finditer(ln):
                if int(m.group(1), 0) in values:
                    hit = m.group(1)
                    break
            if hit:
                break
        if hit:
            found.append((rel, i + 1, 'value', '%s (pal %s) %s' % (
                hit, '/'.join(sorted(values[int(hit, 0)])), ' '.join(ln.split()))))
    return found


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode('latin-1')


def port_files(root):
    out = []
    base = os.path.join(root, 'port')
    for d, dirs, files in os.walk(base):
        dirs.sort()
        for f in sorted(files):
            if not f.endswith(('.c', '.h', '.cpp', '.m', '.mm')):
                continue
            rel = os.path.relpath(os.path.join(d, f), root).replace(os.sep, '/')
            if rel.startswith(PORT_SKIP_DIRS) or '/test/' in rel or PORT_SKIP_FILES.match(rel):
                continue
            out.append(rel)
    return out


def scan(root):
    found = []
    values = region_values(root)
    for rel in port_files(root):
        text = read(os.path.join(root, rel))
        code = strip(text).split('\n')
        owner = functions(code)
        found += scan_lines(rel, code, range(len(code)), owner)
    for rel in GAME_FILES:
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            if os.path.abspath(root) == ROOT:
                print('%s: missing (GAME_FILES is out of date)' % rel)
                found.append((rel, 0, 'missing', ''))
            continue  # a test tree (--root) holds only some of them
        text = read(path)
        raw = text.split('\n')
        code = strip(text).split('\n')
        owner = functions(code)
        sites = game_port_lines(raw, code, owner)
        found += scan_lines(rel, code, sites, owner)
        found += scan_values(rel, code, sites, owner, values)
        if rel in LAYOUT_PROC_FILES:
            found += scan_layout_returns(rel, code, sites, owner)
    # a line a category names is not named again by its value
    named = {(f[0], f[1]) for f in found if f[2] != 'value'}
    return sorted({f for f in found if f[2] != 'value' or (f[0], f[1]) not in named})


REASON_DISCS = re.compile(r'\b(?:%s)\b|every disc|all seven|each disc' % '|'.join(SLUGS))


def load_allow(path):
    entries = []
    if not os.path.exists(path):
        return entries
    with open(path, encoding='utf-8') as f:
        for no, ln in enumerate(f, 1):
            s = ln.rstrip('\n')
            if not s.strip() or s.lstrip().startswith('#'):
                continue
            if '  --  ' not in s:
                entries.append((no, None, None, None, 'no "  --  " before the reason'))
                continue
            head, reason = s.split('  --  ', 1)
            parts = head.split(None, 2)
            if len(parts) < 3:
                entries.append((no, None, None, None, 'want <file>  <category>  <snippet>'))
                continue
            err = None if REASON_DISCS.search(reason) else 'the reason names no disc'
            entries.append((no, parts[0], parts[1], ' '.join(parts[2].split()), err))
    return entries


def main(argv):
    allow = os.path.join(ROOT, 'tools', 'check_region_literals_allow.txt')
    root = ROOT
    show_all = False
    args = argv[1:]
    while args:
        if args[0] == '--allow' and len(args) > 1:
            allow, args = args[1], args[2:]
        elif args[0] == '--root' and len(args) > 1:
            root, args = args[1], args[2:]
        elif args[0] == '--list':
            show_all, args = True, args[1:]
        else:
            print(__doc__)
            return 2
    found = scan(root)
    entries = load_allow(allow)
    bad = 0
    used = set()
    for no, f, cat, snip, err in entries:
        if err:
            print('%s:%d: %s' % (os.path.basename(allow), no, err))
            bad += 1
    unlisted = []
    for rel, line, cat, text in found:
        hit = None
        for idx, (no, f, c, snip, err) in enumerate(entries):
            if f == rel and c == cat and snip and snip in text:
                hit = idx
                break
        if hit is None:
            unlisted.append((rel, line, cat, text))
        else:
            used.add(hit)
        if show_all:
            print('%s:%d: %s: %s%s' % (rel, line, cat, text, '' if hit is None else '  [allowed]'))
    for rel, line, cat, text in unlisted:
        if not show_all:
            print('%s:%d: %s: %s' % (rel, line, cat, text))
        bad += 1
    for idx, (no, f, c, snip, err) in enumerate(entries):
        if f and not err and idx not in used:
            print('%s:%d: stale: no %s finding in %s matches "%s"' % (os.path.basename(allow), no, c, f, snip))
            bad += 1
    if bad:
        print('check_region_literals: %d problem(s): route each number through ico_region_cur() '
              '(port/core/region.h) or a PAL_<SET> selector, or list it in '
              'tools/check_region_literals_allow.txt with the discs\' numbers' % bad)
        return 1
    print('check_region_literals: ok (%d port files, %d game files, %d findings allowed)'
          % (len(port_files(root)), len(GAME_FILES), len(found)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
