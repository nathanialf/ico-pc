#!/usr/bin/env python3
"""region_literals_test.py: tools/check_region_literals.py finds what it must.

A tree in a temporary folder: the seven region tables (copied from
port/core, so the `value` check reads the real per-disc numbers), a port
file and a game file (ico2/common/src/layout_action.c by name, from
tools/test/region_literals/) with planted sites.  The check must report
exactly the planted findings (every category once, issue 147's
gflagOn(382) among them), leave the game's own lines and the routed lines
alone, pass with an allow list that names them, and fail on a stale entry
and on a reason that names no disc.  Prints `region_literals_test: ok`.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOL = os.path.join(ROOT, 'tools', 'check_region_literals.py')
FIX = os.path.join(HERE, 'region_literals')

EXPECT = [
    ('ico2/common/src/layout_action.c', 'gflag', 'gflagOn(382);'),
    ('ico2/common/src/layout_action.c', 'layout', 'return 58;'),
    ('ico2/common/src/layout_action.c', 'value', 'n > 105'),
    ('port/ui/planted.c', 'gflag', 'gflagChk(349)'),
    ('port/ui/planted.c', 'layout', 'texLayout[57]'),
    ('port/ui/planted.c', 'property', 'lt_mask_property(294'),
    ('port/ui/planted.c', 'stage', 'stage_no == 60'),
    ('port/ui/planted.c', 'pad', 'pad[0].flags & 0x40'),
    ('port/ui/planted.c', 'pad', 'LT_PAD_CROSS'),
]


def run(root, allow):
    p = subprocess.run([sys.executable, '-I', TOOL, '--root', root, '--allow', allow],
                       capture_output=True, text=True)
    return p.returncode, p.stdout


def main():
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, 'port', 'core'))
        os.makedirs(os.path.join(tmp, 'port', 'ui'))
        os.makedirs(os.path.join(tmp, 'ico2', 'common', 'src'))
        for slug in ('pal', 'jpn', 'kor', 'ntsc', 'aug6', 'jun26', 'cn'):
            name = 'region_%s.c' % slug
            shutil.copy(os.path.join(ROOT, 'port', 'core', name), os.path.join(tmp, 'port', 'core', name))
        shutil.copy(os.path.join(FIX, 'planted.c'), os.path.join(tmp, 'port', 'ui', 'planted.c'))
        shutil.copy(os.path.join(FIX, 'game.c'), os.path.join(tmp, 'ico2', 'common', 'src', 'layout_action.c'))
        empty = os.path.join(tmp, 'empty.txt')
        open(empty, 'w').close()
        code, out = run(tmp, empty)
        lines = [ln for ln in out.splitlines() if not ln.startswith('check_region_literals:')]
        if code != 1:
            bad.append('no allow list: exit %d, want 1' % code)
        for rel, cat, snip in EXPECT:
            hits = [ln for ln in lines if ln.startswith(rel + ':') and (': %s: ' % cat) in ln and snip in ln]
            if len(hits) != 1:
                bad.append('%s %s "%s": %d findings, want 1' % (rel, cat, snip, len(hits)))
        if len(lines) != len(EXPECT):
            bad.append('%d findings, want %d:\n  %s' % (len(lines), len(EXPECT), '\n  '.join(lines)))
        allow = os.path.join(tmp, 'allow.txt')
        with open(allow, 'w') as f:
            for rel, cat, snip in EXPECT:
                f.write('%s  %s  %s  --  planted (pal 382, ntsc 349)\n' % (rel, cat, snip))
        code, out = run(tmp, allow)
        if code != 0:
            bad.append('full allow list: exit %d, want 0:\n%s' % (code, out))
        with open(allow, 'a') as f:
            f.write('port/ui/planted.c  gflag  gflagOn(999)  --  stale on every disc\n')
        code, out = run(tmp, allow)
        if code != 1 or 'stale' not in out:
            bad.append('a stale entry: exit %d, want 1 with "stale"' % code)
        with open(allow, 'w') as f:
            for rel, cat, snip in EXPECT:
                f.write('%s  %s  %s  --  planted\n' % (rel, cat, snip))
        code, out = run(tmp, allow)
        if code != 1 or 'names no disc' not in out:
            bad.append('a reason without a disc: exit %d, want 1 with "names no disc"' % code)
    for b in bad:
        print('region_literals_test: ' + b)
    if bad:
        return 1
    print('region_literals_test: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
