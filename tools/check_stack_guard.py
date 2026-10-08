#!/usr/bin/env python3
"""check_stack_guard.py: no arm64 object stores onto its stack protector's slot.

The Android build compiles with -fstack-protector-strong (the NDK's default
flags) where the desktop builds have no stack protector: a function with a
local array keeps a guard word next to it and calls __stack_chk_fail, which
ends the process ("stack corruption detected"), when the word changed by the
time it returns. A game function that writes one element past a local array
(harmless on the EE, where the next stack word was another local) then ends
the run on a phone and only there. This finds the cases the compiler can
see: a store at a constant offset from sp or x29 that overlaps the guard's
slot, which the prologue fills from TPIDR_EL0 + 0x28.

usage: check_stack_guard.py llvm-objdump object...
Prints each such store and exits 1 when there is one.
"""
import re
import subprocess
import sys

MEM = re.compile(r'\[(sp|x29)(?:, #(-?0x[0-9a-f]+|-?\d+))?\](!?)')
FUNC = re.compile(r'\n(?=[0-9a-f]+ <[^>]+>:\n)')


def num(s):
    return int(s, 0) if s else 0


def width(op, reg):
    """the bytes a store writes"""
    per = {'x': 8, 'w': 4, 'd': 8, 's': 4, 'q': 16, 'h': 2, 'b': 1}.get(reg[:1], 8)
    if op.startswith('stp'):
        return 2 * per
    if op in ('strb', 'sturb'):
        return 1
    if op in ('strh', 'sturh'):
        return 2
    return per


def scan(objdump, path):
    text = subprocess.run([objdump, '-d', '--no-show-raw-insn', path], capture_output=True,
                          text=True, check=True).stdout
    hits = []
    for f in FUNC.split(text):
        m = re.match(r'([0-9a-f]+) <([^>]+)>:', f)
        if not m:
            continue
        fp = None  # x29 = sp + fp after the prologue
        tp = set()  # registers holding TPIDR_EL0
        guard = None  # the register the guard was loaded into
        slot = None  # the guard's slot, as an offset from sp
        for line in f.split('\n')[1:]:
            parts = line.split('\t')
            if len(parts) < 3:
                continue
            op, args = parts[1].strip(), parts[2].strip()
            if op == 'add' and args.startswith('x29, sp, #'):
                fp = num(args.split('#')[1])
            elif op == 'mov' and args == 'x29, sp':
                fp = 0
            elif op == 'mrs' and 'TPIDR_EL0' in args:
                tp.add(args.split(',')[0])
            if guard is None and op == 'ldr':
                g = re.match(r'(x\d+), \[(x\d+), #0x28\]', args)
                if g and g.group(2) in tp:
                    guard = g.group(1)
                    continue
            if not op.startswith('st'):
                continue
            mm = MEM.search(args)
            if not mm:
                continue
            base, off = mm.group(1), num(mm.group(2))
            if base == 'x29':
                if fp is None:
                    continue
                off += fp
            first = args.split(',')[0].strip()
            if slot is None:
                if guard is not None and first == guard:
                    slot = off  # the prologue's store of the guard
                continue
            if off < slot + 8 and off + width(op, first) > slot:
                hits.append('%s: %s: %s %s (the guard is at sp+%#x)' % (path, m.group(2), op, args,
                                                                       slot))
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    hits = []
    for o in sys.argv[2:]:
        hits += scan(sys.argv[1], o)
    for h in hits:
        print(h)
    if hits:
        print('check_stack_guard: %d store(s) on a stack protector slot' % len(hits))
        return 1
    print('check_stack_guard: no store on a stack protector slot in %d objects' %
          (len(sys.argv) - 2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
