#!/ usr / bin / env python3

"""python3 -I tools/vu_variant_equiv.py [--decomp DIR] [--count N] [--check FILE] [ENTRY...]

A symbolic executor for the vertex loops of the VU1 programs (cluster, mesh,
normal_c, normal_l), used to classify the hunks of tools/test/vu1/jun26_vs_pal.diff:
it runs a routine of the PAL text and of the JUN26 text on the same symbolic input
(the packet, the matrices, the constants) for N vertices, with every data-dependent
branch (the clip tests, the back-face flag) forked, and compares what each path
writes to VU memory (the output packet) and what it kicks.  Equal results on every
path mean the two texts have the same dataflow in that routine ("scheduling only");

a difference names the first memory quadword whose expression differs.

The model (what it assumes, from the VU1 manual and the programs' own timing):
  * the two slots of a line read the state before the line, then write
  * a branch has one delay slot
  * the MAC and status flags an fmand/fsand reads are those of the latest FMAC op
    issued at least 4 lines earlier (ftoi/itof/abs set none); no stalls modelled
  * the Q register has the last div's value, I the last loi's; a float is a
    string expression, +, * and max/min sorted (commutative), nothing else
    reassociated: the floating-point result is the expression's value
Unsupported mnemonics abort the routine (reported, not guessed).
"""
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vu_variant_diff as vd  # noqa: E402

TOP = 4000
COMP = "xyzw"
ENTRIES = {
    "cluster": ["START_CLUSTER_0", "START_CLUSTER_0_SPEC", "START_CLUSTER_1_SPEC"],
    "mesh": ["START_MESH_LIGHT", "START_MESH_LIGHT_SPEC", "START_MESH_NOLIGHT"],
    "normal_c": ["START_NORMAL_C", "START_NORMAL_C_NOCLIP"],
    "normal_l": ["START_NORMAL_L", "START_NORMAL_L_SPEC"],  # START_NORMAL_REF (rsqrt) has no hunk
}

VERBOSE = False


class Fork(Exception):
    def __init__(self, key):
        Exception.__init__(self, key)
        self.key = key


class Unsupported(Exception):
    pass


def split_pair(line):
    """(upper, lower) text of one normalised line."""
    t = line.split(" ")
    i = 1 if t[0].startswith("nop") else 2
    while i < len(t) and t[i - 1].endswith(","):
        i += 1
    return " ".join(t[:i]), " ".join(t[i:])


def parse(lines):
    prog, labels = [], {}

    for ln in lines:
        if ln.endswith(":"):
            labels[ln[:-1]] = len(prog)
        else:
            u, l = split_pair(ln)
            prog.append((u, l, ln))
    return prog, labels


def mn_ops(text):
    m = re.match(r"([a-z0-9\[\]]+)(?:\.([xyzw]+))?\s*(.*)$", text)
    mn = m.group(1).replace("[e]", "").replace("[i]", "")
    dest = m.group(2) or "xyzw"
    ops = [o.strip() for o in m.group(3).split(",")] if m.group(3) else []
    return mn, dest, ops


def comm(op, a, b):
    a, b = sorted((a, b))
#x + 0 and 1 * x are x(the sign of a zero is the one difference, not tracked)
    if op == "add" and a == "0":
        return b
    if op == "mul" and a == "1":
        return b
    return "%s(%s,%s)" % (op, a, b)


class Machine:
    def __init__(self, prog, labels, count, decisions):
        self.prog, self.labels, self.count, self.dec = prog, labels, count, decisions
        self.vf = {}

        self.vi = [0] * 16
        self.acc = ["acc0.%s" % c for c in COMP]
        self.q, self.i = "Q0", "I0"
        self.mem = {}

        self.kicks = []
        self.macs, self.stats = [], []
        self.slot = 0
        self.taken = []

#-- - registers
    def getf(self, r):
        n = int(r[2:])
        if n == 0:
            return ["0", "0", "0", "1"]
        return self.vf.get(n) or ["vf%d.%s" % (n, c) for c in COMP]

    def memread(self, a):
        if a in self.mem:
            return list(self.mem[a])
        if a >= TOP - 100:
            return ["M[%d].%s" % (a - TOP, c) for c in COMP]
        return ["G[%d].%s" % (a, c) for c in COMP]

    def reg(self, tok):
        """(vector, component or None) of a vf operand token like vf16x or vf16."""
        m = re.match(r"vf(\d+)([xyzw]?)$", tok)
        if not m:
            raise Unsupported(tok)
        return self.getf("vf" + m.group(1)), (COMP.index(m.group(2)) if m.group(2) else None)

    def val(self, tok):
        return tok if isinstance(tok, str) and not tok.lstrip("-").isdigit() else int(tok)

    def ivalue(self, tok):
        m = re.match(r"vi(\d+)$", tok)
        return self.vi[int(m.group(1))]

    def flag_at(self, hist):
        best = None
        for s, e in hist:
            if s <= self.slot - 4:
                best = e
        return best or "flag0"

#-- - execution
    def run(self, entry):
        pc = self.labels[entry]
        delay = None
        steps = 0
        while True:
            steps += 1
            if steps > 4000:
                raise Unsupported("step limit")
            u, l, text = self.prog[pc]
            writes, branch = [], None
            self.exec_upper(u, writes)
            branch = self.exec_lower(l, writes)
            for w in writes:
                w()
            self.slot += 1
            if delay is not None:
                tgt = delay
                if tgt.startswith("BEGIN_") or tgt == "RET":
                    return
                pc = self.labels[tgt]
                delay = None
                if branch is not None:
                    raise Unsupported("branch in a delay slot")
                continue
            if branch is not None:
                delay = branch
            pc += 1

    def setv(self, writes, n, dest, vals):
        def w():
            cur = list(self.getf("vf%d" % n))
            for k, c in enumerate(COMP):
                if c in dest:
                    cur[k] = vals[k]
            if n:
                self.vf[n] = cur
        writes.append(w)

    def exec_upper(self, u, writes):
        mn, dest, ops = mn_ops(u)
        if mn == "nop":
            return
        if mn in ("ftoi0", "ftoi4", "ftoi12", "ftoi15", "itof0", "itof4", "itof12", "itof15"):
            fd = int(ops[0][2:])
            v, _ = self.reg(ops[1])
            vals = ["%s(%s)" % (mn, v[k]) for k in range(4)]
            self.setv(writes, fd, dest, vals)
            return
        if mn == "move":
            raise Unsupported("move is a lower op")
        m = re.match(r"(add|sub|mul|madd|msub|max|mini)(a)?(x|y|z|w|i|q)?$", mn)
        if not m:
            raise Unsupported(mn)
        op, toacc, bc = m.groups()
        if toacc:
            fd = None
            fs, ft = ops[1], ops[2]
        else:
            fd = int(ops[0][2:])
            fs, ft = ops[1], ops[2]
        a, _ = self.reg(fs)
        if bc in ("x", "y", "z", "w"):
            tv, tc = self.reg(ft)
            b = [tv[COMP.index(bc)]] * 4
        elif bc == "i":
            b = [self.i] * 4
        elif bc == "q":
            b = [self.q] * 4
        else:
            tv, _ = self.reg(ft)
            b = tv
        vals = []
        for k in range(4):
            if op == "add":
                r = comm("add", a[k], b[k])
            elif op == "sub":
                r = "0" if a[k] == b[k] else "sub(%s,%s)" % (a[k], b[k])
            elif op == "mul":
                r = comm("mul", a[k], b[k])
            elif op == "madd":
                r = comm("add", self.acc[k], comm("mul", a[k], b[k]))
            elif op == "msub":
                r = "sub(%s,%s)" % (self.acc[k], comm("mul", a[k], b[k]))
            elif op == "max":
                r = comm("max", a[k], b[k])
            else:
                r = comm("min", a[k], b[k])
            vals.append(r)
        slot = self.slot
        if toacc:
            def wa(vals=vals):
                for k, c in enumerate(COMP):
                    if c in dest:
                        self.acc[k] = vals[k]
            writes.append(wa)
        else:
            self.setv(writes, fd, dest, vals)
        flagexpr = "fl(%s:%s)" % (op, ",".join(vals[k] for k, c in enumerate(COMP) if c in dest))

        def wf():
            self.macs.append((slot, flagexpr))
            self.stats.append((slot, flagexpr))
        writes.append(wf)

    def addr(self, ops_off):
        m = re.match(r"(-?\d*)\((vi\d+)(\+\+|--)?\)$", ops_off)
        if not m:
            raise Unsupported(ops_off)
        off = int(m.group(1)) if m.group(1) not in ("", "-") else 0
        return off, int(m.group(2)[2:]), m.group(3)

    def cond(self, key):
        if key not in self.dec:
            raise Fork(key)
        self.taken.append((key, self.dec[key]))
        return self.dec[key]

    def exec_lower(self, l, writes):
        mn, dest, ops = mn_ops(l)
        slot = self.slot
        if mn == "nop":
            return None
        if mn in ("lq", "lqi"):
            fd = int(ops[0][2:])
            off, r, post = self.addr(ops[1])
            a = self.vi[r] + off
            self.setv(writes, fd, dest, self.memread(a))
            if post:
                def w():
                    self.vi[r] += 1
                writes.append(w)
            return None
        if mn in ("sq", "sqi"):
            fs = self.getf(ops[0])
            off, r, post = self.addr(ops[1])
            a = self.vi[r] + off

            def w():
                cur = self.memread(a)
                for k, c in enumerate(COMP):
                    if c in dest:
                        cur[k] = fs[k]
                self.mem[a] = cur
                if post:
                    self.vi[r] += 1
            writes.append(w)
            return None
        if mn == "move":
            fd = int(ops[0][2:])
            self.setv(writes, fd, dest, list(self.getf(ops[1])))
            return None
        if mn == "mfir":
            fd = int(ops[0][2:])
            s = self.ivalue(ops[1])
            self.setv(writes, fd, dest, ["int(%s)" % s] * 4)
            return None
        if mn == "xtop":
            r = int(ops[0][2:])
            writes.append(lambda: self.vi.__setitem__(r, TOP))
            return None
        if mn == "ilwr":
            r = int(ops[0][2:])
            writes.append(lambda: self.vi.__setitem__(r, self.count))
            return None
        if mn in ("iaddi", "iaddiu", "isubiu"):
            r = int(ops[0][2:])
            s = self.ivalue(ops[1])
            k = int(ops[2])
            if isinstance(s, str):
                raise Unsupported("symbolic int in " + l)
            v = s - k if mn == "isubiu" else s + k
            writes.append(lambda: self.vi.__setitem__(r, v))
            return None
        if mn == "iand":
            r = int(ops[0][2:])
            a, b = self.ivalue(ops[1]), self.ivalue(ops[2])
            if isinstance(a, str) or isinstance(b, str):
                raise Unsupported("symbolic int in " + l)
            writes.append(lambda: self.vi.__setitem__(r, a & b))
            return None
        if mn == "iadd":
            r = int(ops[0][2:])
            a, b = self.ivalue(ops[1]), self.ivalue(ops[2])
            writes.append(lambda: self.vi.__setitem__(r, a + b))
            return None
        if mn == "div":
            a, _ = self.reg(ops[1])
            ac = self.reg(ops[1])
            ft = self.reg(ops[2])
            av = ac[0][ac[1]]
            bv = ft[0][ft[1]]
            writes.append(lambda: setattr(self, "q", "div(%s,%s)" % (av, bv)))
            return None
        if mn == "waitq":
            return None
        if mn == "loi":
            v = ops[0] if ops else "0"
            writes.append(lambda: setattr(self, "i", "I(%s)" % v))
            return None
        if mn in ("fmand", "fsand"):
            r = int(ops[0][2:])
            fl = self.flag_at(self.macs if mn == "fmand" else self.stats)
            src = ops[1]
            if mn == "fsand":
                mask = int(ops[1], 0) if not src.startswith("vi") else self.ivalue(src)
                v = "S[%s&%s]" % (fl, mask)
            else:
                m = self.ivalue(src)
                prev = self.vi[int(src[2:])] if src.startswith("vi") else m
                v = "F[%s&%s]" % (fl, m) if not isinstance(prev, str) else "%s&F[%s]" % (prev, fl)
                if isinstance(prev, str):
                    v = "&".join(sorted(["F[%s]" % fl, prev]))
            writes.append(lambda: self.vi.__setitem__(r, v))
            return None
        if mn in ("ibne", "ibeq", "ibgtz"):
            if mn == "ibgtz":
                a = self.ivalue(ops[0])
                b = 0
                tgt = ops[1]
            else:
                a, b = self.ivalue(ops[0]), self.ivalue(ops[1])
                tgt = ops[2]
            if isinstance(a, str) or isinstance(b, str):
                key = "%s %s %s" % (a, {"ibne": "!=", "ibeq": "==", "ibgtz": ">"}[mn], b)
                take = self.cond(key)
            else:
                take = {"ibne": a != b, "ibeq": a == b, "ibgtz": a > b}[mn]
            return tgt if take else None
        if mn == "b":
            return ops[0]
        if mn == "xgkick":
            a = self.ivalue(ops[0])

            def w():
                self.kicks.append((a, {
k: tuple(v) for k, v in self.mem.items() if k >= a}))
            writes.append(w)
            return None
        raise Unsupported(mn)


def execute(prog, labels, entry, count, decisions):
    m = Machine(prog, labels, count, decisions)
    m.run(entry)
    return m


def explore(prog, labels, entry, count, prefix):
    """All leaves under a decision prefix: [(decisions, machine)]."""
    out = []
    stack = [dict(prefix)]
    while stack:
        d = stack.pop()
        try:
            out.append((d, execute(prog, labels, entry, count, d)))
        except Fork as f:
            for v in (True, False):
                nd = dict(d)
                nd[f.key] = v
                stack.append(nd)
    return out


def result(m):
    return {"mem": dict((k, tuple(v)) for k, v in m.mem.items()), "kicks": m.kicks}

def first_diff(a, b):
    ka, kb = a["mem"], b["mem"]
    for k in sorted(set(ka) | set(kb)):
        if ka.get(k) != kb.get(k):
            x, y = ka.get(k), kb.get(k)
            if x is None or y is None:
                return "quad %d: only in %s" % (k - TOP, "PAL" if y is None else "JUN26")
            return "output quad %d, component %s" % (k - TOP, "".join(COMP[c] for c in range(4) if x[c] != y[c]))
    if [x[0] for x in a["kicks"]] != [x[0] for x in b["kicks"]]:
        return "kick address %s / %s" % ([x[0] - TOP for x in a["kicks"]], [x[0] - TOP for x in b["kicks"]])
    for x, y in zip(a["kicks"], b["kicks"]):
        if x[1] != y[1]:
            return "kick contents differ"
    return None


def compare(decomp, program, entry, count):
    texts = [vd.normalise(vd.cpp_text(decomp, program, col, defs)) for _, col, defs in vd.VARIANTS]
    (pp, pl), (jp, jl) = parse(texts[0]), parse(texts[1])
    try:
        pal = explore(pp, pl, entry, count, {})
    except Unsupported as e:
        return "unsupported in PAL: %s" % e
    n = 0
    for d, m in pal:
        try:
            jun = explore(jp, jl, entry, count, d)
        except Unsupported as e:
            return "unsupported in JUN26: %s" % e
        for d2, m2 in jun:
            n += 1
            diff = first_diff(result(m), result(m2))
            if diff:
                return "DIFFERENT: %s%s" % (diff, VERBOSE and "\n    PAL " + str(result(m)["mem"]) or "")
    return "same on %d paths (%d vertices)" % (n, count)


def report(decomp, count, only):
    lines = []
    for program, entries in ENTRIES.items():
        for e in entries:
            if only and e not in only:
                continue
            lines.append("%-9s %-24s %s\n" % (program, e, compare(decomp, program, e, count)))
    return "".join(lines)


def main(argv):
    decomp = os.environ.get("ICO_DECOMP_DIR", "")
    count = 4
    check = None
    only = []
    args = list(argv)
    while args:
        t = args.pop(0)
        if t == "--decomp":
            decomp = args.pop(0)
        elif t == "--count":
            count = int(args.pop(0))
        elif t == "--check":
            check = args.pop(0)
        else:
            only.append(t)
    if not (os.environ.get("CPP") or shutil.which("cpp")):
        sys.stderr.write("vu_variant_equiv: no cpp on the path (set CPP): skipped\n")
        return 77
    if not decomp or not os.path.isfile(os.path.join(decomp, "ico2", "vusrc", "mesh.vsm")):
        sys.stderr.write("vu_variant_equiv: no decomp (ICO_DECOMP_DIR / --decomp): skipped\n")
        return 77
    text = report(decomp, count, only)
    if check:
        with open(check, "rb") as f:
            want = f.read()
        if want != text.encode("utf-8"):
            sys.stderr.write("vu_variant_equiv: %s differs from this run:\n%s" % (check, text))
            return 1
        sys.stdout.write("vu_variant_equiv: %s matches\n" % check)
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
