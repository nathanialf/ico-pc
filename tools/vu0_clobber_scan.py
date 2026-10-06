#!/usr/bin/env python3
"""tools/vu0_clobber_scan.py --elf BASE_ELF --syms SYMBOL_ELF [--candidates F ...]

docs/port/MATH.md, "Register side effects" (DIVERGENCES.md F10): on the PS2
seki/src/Matrix.c keeps the current matrix in VU0 registers vf4-vf7, and
libvu0 and several game helpers use vf4-vf7 as scratch; the host keeps the
matrix in ico_current_matrix and its routines touch only their arguments.
The two differ only where the EE reads the current matrix after a routine
the host does not mirror wrote vf4-vf7.

This scan decides that on the EE's code. BASE_ELF is the retail ELF
(baserom/pal/baseelf.elf in the decomp repo), SYMBOL_ELF a byte-identical
build with symbols (the decomp's build/ico.syms.elf); the .text sections
must be equal. The disassembly is mips-linux-gnu-objdump -m mips:5900 (it
decodes VU0 macro instructions). Per function, a forward dataflow over the
control-flow graph (delay slots, jump tables read from .rodata) tracks each
of the 16 lanes of vf4-vf7 as a set of origins:
  E  the value on entry (the caller's),
  C  set by a current-matrix routine the host also implements (Matrix.c's
     _Init/_Unit/_Set/_SetTrans/_ClearTransCurrentMatrix, and Shadow.c's
     inline load of the volume matrix, which the host does as
     _SetCurrentMatrix),
  F  written by any other instruction (libvu0, FSqrt, the cloth helpers...),
  Y  after a thread switch (iosThreadSleep, WaitSema...: the EE kernel does
     not save VU0 registers),
  J  after an indirect call (newlib's stdio callbacks excepted: they are
     VU0-free),
  P  popped from a push made by a caller.
_Push/_PopCurrentMatrix and _Push/_PopVu0Registers save and restore lane
states on an abstract stack. A use is a current-matrix reader (_Get*, _Trans,
_Rot*, _Scale, _Mul*, _Apply*, _RotTrans*, _Transpose*, _Inverse*,
BgAnimation.c's _RotTransCurrentMatrixYXZ) or a raw read of vf4-vf7 by a
function other than the writer; a callee that reads lanes it did not write
reports them to its callers as uses. Every use whose lanes may hold F, Y, J
or P is printed. The only ones in the retail ELF are libvu0's own register
passing in sceVu0RotMatrix{X,Y,Z} (_sceVu0ecossin returns cos/sin in
vf4/vf5 and multiplies vf4.yzw by vf0.x = 0 first), listed apart.

Exit 0 when nothing outside libvu0's internal passing is found, 1 otherwise.
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict

LANES = [(r, l) for r in (4, 5, 6, 7) for l in 'xyzw']
ALL = frozenset(LANES)

SET_ALL = {'_InitCurrentMatrix', '_UnitCurrentMatrix', '_SetCurrentMatrix'}
SET_TRANS = {'_SetTransCurrentMatrix', '_ClearTransCurrentMatrix'}
# read all 16 lanes, write all 16 from them
RW_ALL = {'_RotCurrentMatrixX', '_RotCurrentMatrixY', '_RotCurrentMatrixZ',
          '_ScaleCurrentMatrix', '_MulCurrentMatrixR', '_MulCurrentMatrixL',
          '_TransposeCurrentMatrix', '_TransposeRotationCurrentMatrix',
          '_InverseCurrentMatrix', '_RotTransCurrentMatrixYXZ'}
TRANS = {'_TransCurrentMatrix'}  # reads all, writes vf7
USE_ALL = {'_GetCurrentMatrix', '_ApplyCurrentMatrix',
           '_RotTransPersCurrentMatrix', '_RotTransCurrentMatrix'}
USE_TRANS = {'_GetCurrentMatrixTrans'}
PUSH = {'_PushCurrentMatrix', '_PushVu0Registers'}
POP = {'_PopCurrentMatrix', '_PopVu0Registers'}
YIELD = {'iosThreadSleep', 'SleepThread', 'WaitSema', 'RotateThreadReadyQueue', 'ChangeThreadPriority', 'WakeupThread', 'StartThread'}
# newlib stdio: the pointers are __sread/__swrite/__sseek/__sclose/lflush/__sflush, no VU0
STDIO = {'fflush', '__sfvwrite', '_fwalk'}
HOST_CM_RAW = {'shadow_RenderVolume', 'shadow_RenderVolumeMulti'}
API = SET_ALL | SET_TRANS | RW_ALL | TRANS | USE_ALL | USE_TRANS | PUSH | POP

funcs = {}  # name -> list of (addr, mnem, ops)
order = []
addr2func = {}
RODATA = b''
RODATA_BASE = 0


def load(dis_lines):
    cur = None
    for line in dis_lines:
        m = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if m:
            cur = m.group(2)
            funcs[cur] = []
            order.append(cur)
            continue
        m = re.match(r'^\s+([0-9a-f]+):\t[0-9a-f]+ \t(\S+)\s*(.*)$', line)
        if m and cur:
            funcs[cur].append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    for f in order:
        for a, _, _ in funcs[f]:
            addr2func[a] = f


VF = re.compile(r'\$vf(\d+)([xyzw]*)')


def vu_effects(mn, ops):
    """(writes, reads) as sets of (reg, lane) restricted to vf4-vf7."""
    regs = VF.findall(ops)
    w, r = set(), set()
    base = mn.split('.')[0]
    dest = mn.split('.')[1] if '.' in mn else 'xyzw'

    def lanes(reg, suf, default):
        return {(reg, c) for c in (suf or default)}
    if base == 'lqc2' or base == 'qmtc2':
        for n, s in regs:
            w |= lanes(int(n), '', 'xyzw')
    elif base in ('sqc2', 'qmfc2'):
        for n, s in regs:
            r |= lanes(int(n), '', 'xyzw')
    elif base in ('vsqi', 'vsqd', 'vmtir', 'vrinit', 'vrxor', 'vdiv', 'vsqrt',
                  'vrsqrt', 'vclipw') or ops.startswith('$ACC'):
        for n, s in regs:
            r |= lanes(int(n), s, dest)
    elif base.startswith('v') and regs and base not in ('vnop', 'vwaitq'):
        n, s = regs[0]
        w |= lanes(int(n), s, dest)
        if base == 'vsub' and len(regs) == 3 and regs[0][0] == regs[1][0] == regs[2][0]:
            regs = regs[:1]
        for n, s in regs[1:]:
            r |= lanes(int(n), s, dest)
    keep = lambda st: {x for x in st if x[0] in (4, 5, 6, 7)}
    return keep(w), keep(r)


BR = re.compile(r'^(b\w*|j)$')


def target_of(ops):
    m = re.search(r'(?:^|,)(?:0x)?([0-9a-f]+)(?: <[^>]*>)?$', ops)
    return int(m.group(1), 16) if m else None


summ = {}  # name -> (exit: dict lane->frozenset, entry_uses: set of lanes used at E, unbalanced flag)
viol = defaultdict(set)
uses_log = defaultdict(set)
jalr_f = []


def summary(name, stack=()):
    if name in summ:
        return summ[name]
    if name in API or name not in funcs:
        return None
    if name in stack:
        return ({l: frozenset('E') for l in LANES}, set(), 'recursive')
    s = analyse(name, stack + (name,))
    summ[name] = s
    return s


def join(a, b):
    return {l: a[l] | b[l] for l in LANES}


def norm_stacks(stks):
    """merge stacks of equal depth entry by entry (lane-wise union)"""
    by = {}
    for k in stks:
        if len(k) > 40:
            k = k[-40:]
        d = len(k)
        if d not in by:
            by[d] = [list(e) for e in k]
        else:
            for i, e in enumerate(k):
                by[d][i] = [a | b for a, b in zip(by[d][i], e)]
    return frozenset(tuple(tuple(e) for e in v) for v in by.values())


def jump_table(body, j):
    import struct
    win = body[max(0, j - 14):j]
    N = hi = lo = None
    for a, mn, ops in win:
        if mn == 'sltiu':
            N = int(ops.split(',')[-1], 0)
        elif mn == 'lui':
            hi = int(ops.split(',')[-1], 0)
        elif mn == 'addiu' and hi is not None and lo is None:
            v = int(ops.split(',')[-1], 0)
            if -32768 <= v < 32768:
                lo = v
    if N is None or hi is None or lo is None:
        return None
    base = (hi << 16) + lo - RODATA_BASE
    if base < 0 or base + 4 * N > len(RODATA):
        return None
    return [struct.unpack_from('<I', RODATA, base + 4 * k)[0] for k in range(N)]


def isF(x):
    return x.startswith('F')


def analyse(name, stack):
    body = funcs[name]
    idx = {a: i for i, (a, _, _) in enumerate(body)}
    n = len(body)
    E = {l: frozenset(['E']) for l in LANES}
    instate = {0: (E, frozenset([()]))}
    exit_state = None
    entry_uses = set()
    notes = set()
    work = {0}

    def merge(i, st, stks):
        nonlocal work
        if i not in instate:
            instate[i] = (dict(st), norm_stacks(frozenset(stks)))
            work.add(i)
            return
        o, os_ = instate[i]
        ch = False
        m = {}
        for l in LANES:
            m[l] = o[l] | st[l]
            if m[l] != o[l]:
                ch = True
        ns = norm_stacks(os_ | frozenset(stks))
        if ns != os_:
            ch = True
        if ch:
            instate[i] = (m, ns)
            work.add(i)

    def use(st, lanes_used, what, at):
        uses_log[name].add((at, what, frozenset(x for l in lanes_used for x in st[l])))
        bad = sorted((l, x) for l in lanes_used for x in st[l] if isF(x) or x == 'Y' or x.startswith('J'))
        for l in lanes_used:
            if 'E' in st[l]:
                entry_uses.add(l)
        if bad:
            viol[name].add((at, what, tuple(sorted(set('%d%s' % l for l, _ in bad))),
                            tuple(sorted(set(x for _, x in bad)))))

    def apply_call(st, stks, callee, at):
        st = dict(st)
        if callee in SET_ALL:
            for l in LANES:
                st[l] = frozenset(['C:%s@%x:%s' % (name, at, callee)])
            if callee != '_SetCurrentMatrix':
                stks = frozenset([()])
        elif callee in SET_TRANS:
            for c in 'xyzw':
                st[(7, c)] = frozenset(['C:%s@%x:%s' % (name, at, callee)])
        elif callee in RW_ALL or callee in TRANS:
            use(st, ALL, callee, at)
            val = frozenset().union(*st.values())
            tgt = [(7, c) for c in 'xyzw'] if callee in TRANS else LANES
            for l in tgt:
                st[l] = val
        elif callee in USE_ALL:
            use(st, ALL, callee, at)
        elif callee in USE_TRANS:
            use(st, {(7, c) for c in 'xyzw'}, callee, at)
        elif callee in YIELD:
            for l in LANES:
                st[l] = frozenset(['Y'])
        elif callee in PUSH:
            stks = frozenset(k + (tuple(st[l] for l in LANES),) for k in stks)
        elif callee in POP:
            outs = []
            nst = {l: frozenset() for l in LANES}
            for k in stks:
                if k:
                    for l, v in zip(LANES, k[-1]):
                        nst[l] = nst[l] | v
                    outs.append(k[:-1])
                else:
                    notes.add('pop of a caller push at %x' % at)
                    for l in LANES:
                        nst[l] = nst[l] | frozenset(['P'])
                    outs.append(())
            st = nst
            stks = frozenset(outs)
        else:
            sm = summary(callee, stack)
            if sm is None:
                return st, stks
            ex, uses, flag = sm
            if flag:
                notes.add('[%s: %s]' % (callee, flag[:200]))
            if uses:
                use(st, uses, callee + ' (callee reads the matrix)', at)
            for l in LANES:
                v = set()
                for x in ex[l]:
                    if x == 'E':
                        v |= st[l]
                    elif isF(x) and not x.startswith('F:' + callee):
                        v.add('F:' + callee + '>' + x[2:] if ':' in x else x)
                    else:
                        v.add(x)
                st[l] = frozenset(v)
        return st, stks

    def raw(st, mn, ops, a, fname):
        w, r = vu_effects(mn, ops)
        if not (w or r):
            return st
        st = dict(st)
        own = 'F:' + fname + '@'
        bad = set()
        for l in r:
            if 'E' in st[l]:
                entry_uses.add(l)
            for x in st[l]:
                if (isF(x) and not x.startswith(own)) or x == 'Y' or x.startswith('J'):
                    bad.add((l, x))
        if bad:
            viol[name].add((a, 'raw read ' + mn, tuple(sorted(set('%d%s' % l for l, _ in bad))),
                            tuple(sorted(set(x for _, x in bad)))))
        for l in w:
            st[l] = frozenset(['C:%s@%x:lqc2' % (fname, a)]) if fname in HOST_CM_RAW else frozenset([own + '%x' % a])
        return st

    while work:
        i = min(work)
        work.discard(i)
        st, stks = instate[i]
        j = i
        while True:
            if j >= n:
                exit_state = st if exit_state is None else {l: exit_state[l] | st[l] for l in LANES}
                break
            if j != i and j in instate:
                merge(j, st, stks)
                break
            a, mn, ops = body[j]
            base = mn.split('.')[0]
            is_branch = bool(BR.match(base)) or base in ('jal', 'jalr', 'jr')
            if is_branch and j + 1 < n:
                st = raw(st, body[j + 1][1], body[j + 1][2], body[j + 1][0], name)
            if base == 'jal':
                t = target_of(ops)
                callee = addr2func.get(t, hex(t or 0))
                st, stks = apply_call(st, stks, callee, a)
                j += 2
                continue
            if base == 'jalr':
                notes.add('indirect call at %x' % a)
                fl = sorted(set('%d%s' % l for l in LANES if any(isF(x) for x in st[l])))
                if fl:
                    jalr_f.append((name, a, fl))
                if name not in STDIO:
                    st = dict(st)
                    for l in LANES:
                        st[l] = st[l] | frozenset(['J@%x' % a])
                j += 2
                continue
            if base == 'jr':
                if ops == 'ra':
                    exit_state = st if exit_state is None else {l: exit_state[l] | st[l] for l in LANES}
                    if any(stks):
                        notes.add('exit with pushes at %x' % a)
                else:
                    tg = jump_table(body, j)
                    if tg is None:
                        notes.add('jump table at %x' % a)
                        tg = range(n)
                    else:
                        tg = [idx[t] for t in tg if t in idx]
                    for k in tg:
                        merge(k, st, stks)
                break
            if is_branch:
                t = target_of(ops)
                if t in idx:
                    merge(idx[t], st, stks)
                elif t is not None and addr2func.get(t):
                    st2, _ = apply_call(st, stks, addr2func[t], a)
                    exit_state = st2 if exit_state is None else {l: exit_state[l] | st2[l] for l in LANES}
                if base in ('j', 'b'):
                    break
                j += 2
                if j < n:
                    merge(j, st, stks)
                break
            st = raw(st, mn, ops, a, name)
            j += 1
    if exit_state is None:
        exit_state = E
    return (exit_state, entry_uses, '; '.join(sorted(notes)))



LIBVU0_INTERNAL = {'_sceVu0ecossin', '_RotMatrixX_02', '_RotMatrixY_02', '_RotMatrixZ_02',
                   'sceVu0RotMatrixX', 'sceVu0RotMatrixY', 'sceVu0RotMatrixZ',
                   'sceVu0RotMatrix'}


def internal(f, what):
    return f in LIBVU0_INTERNAL or what.split(' ')[0] in LIBVU0_INTERNAL


def calls(f):
    out = []
    for a, mn, ops in funcs.get(f, []):
        if mn in ('jal', 'j', 'b'):
            t = target_of(ops)
            c = addr2func.get(t)
            if c and c != f and (mn == 'jal' or funcs[c][0][0] == t):
                out.append((a, c))
    return out


def tree(f, memo={}):
    if f not in memo:
        seen, todo = set(), [f]
        while todo:
            g = todo.pop()
            if g in seen:
                continue
            seen.add(g)
            if g not in API:
                todo += [c for _, c in calls(g)]
        memo[f] = seen
    return memo[f]


def triple(cand):
    """the first set, clobber, read in call order (what a name scan sees) and
    where the readers on that call actually take the matrix from"""
    def sets(c):
        t = tree(c)
        return bool(t & (SET_ALL | SET_TRANS | HOST_CM_RAW))

    def clobbers(c):
        for g in tree(c):
            if g in API or g in HOST_CM_RAW:
                continue
            if any(vu_effects(mn, ops)[0] for _, mn, ops in funcs.get(g, [])):
                return True
        return False

    def readers(c):
        return sorted(g for g in tree(c) if g in uses_log and g not in LIBVU0_INTERNAL
                      and not all(internal(g, w) for _, w, _ in uses_log[g]))
    cs = calls(cand)
    for i, (ai, ci) in enumerate(cs):
        if not sets(ci):
            continue
        for j in range(i + 1, len(cs)):
            aj, cj = cs[j]
            if not clobbers(cj):
                continue
            for k in range(j + 1, len(cs)):
                ak, ck = cs[k]
                r = readers(ck)
                if r:
                    org = set()
                    for g in r:
                        for _, _, o in uses_log[g]:
                            org |= {re.sub(r'@[0-9a-f]+', '', x) for x in o}
                    return ('set %s@%x, clobber %s@%x, read %s@%x: readers %s take the matrix '
                            'from %s' % (ci, ai, cj, aj, ck, ak, ', '.join(r),
                                         ', '.join(sorted(org))))
    return 'no set-clobber-read sequence on the EE'


def main():
    global RODATA, RODATA_BASE
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--syms', required=True)
    ap.add_argument('--objdump', default='mips-linux-gnu-objdump')
    ap.add_argument('--objcopy', default='mips-linux-gnu-objcopy')
    ap.add_argument('--candidates', nargs='*', default=[])
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as d:
        sec = {}
        for name, elf in (('base', a.elf), ('syms', a.syms)):
            subprocess.run([a.objcopy, '-O', 'binary', '-j', '.text', elf, d + '/' + name],
                           check=True)
            sec[name] = open(d + '/' + name, 'rb').read()
        if sec['base'] != sec['syms']:
            sys.exit('.text of %s and %s differ' % (a.elf, a.syms))
        subprocess.run([a.objcopy, '-O', 'binary', '-j', '.rodata', a.elf, d + '/ro'],
                       check=True)
        RODATA = open(d + '/ro', 'rb').read()
    hdr = subprocess.run([a.objdump, '-h', a.elf], capture_output=True, text=True,
                         check=True).stdout
    RODATA_BASE = int(re.search(r'\.rodata\s+\S+\s+([0-9a-f]+)', hdr).group(1), 16)
    dis = subprocess.run([a.objdump, '-d', '-m', 'mips:5900', '-j', '.text', a.syms],
                         capture_output=True, text=True, check=True).stdout
    load(dis.splitlines())
    sys.setrecursionlimit(100000)
    for f in order:
        summary(f)
    found = lib = 0
    for f in order:
        for at, what, lanes, srcs in sorted(viol.get(f, [])):
            line = '%s %x %s: lanes %s from %s' % (f, at, what, ' '.join(lanes),
                                                   ' | '.join(srcs)[:300])
            if internal(f, what):
                lib += 1
            else:
                found += 1
                print(line)
    print('%d function(s) analysed; %d use(s) of a clobbered current matrix; %d libvu0-internal '
          'register passing site(s)' % (len(order), found, lib))
    for c in a.candidates:
        print('%s: %s' % (c, triple(c)))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
