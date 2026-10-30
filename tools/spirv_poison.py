#!/usr/bin/env python3
"""tools/spirv_poison.py IN.spv OUT.spv [options]

The uninitialised-read probe (issues 163 and 164: vertex garbage on the
Turnip and Mali compilers that lavapipe does not show): a SPIR-V rewriter
that gives every value a module could read before writing a loud, fixed
value, so a read of one changes the picture on any driver.

  (a) every OpVariable of the Function or Private storage class without an
      initializer gets one: a constant of its pointee type whose float
      components are NaN (0x7FC00000; 16-bit 0x7E00, 64-bit the quiet NaN),
      integer components 0xDEADBEEF (16-bit 0xBEEF, 64-bit 0xDEADBEEFDEADBEEF)
      and booleans true;
  (b) every OpUndef of such a type is replaced by the same constant (at
      module scope the instruction becomes the constant under the same id;
      in a function body an OpCopyObject of the constant, the same id).

A pointee type the constant cannot be built for (a pointer, an image, a
sampler, a runtime array, an opaque type) leaves its variable or OpUndef as
it is, counted as skipped.  The constants are new instructions placed right
after their type's declaration (a composite's members are declared before
it, so each constant follows its constituents); the id bound is raised.
No external dependency: the module is read as its 32-bit word stream.

Options:
  --list            print each candidate: its index (the order of the
                    module: variables first, then OpUndefs), kind, storage
                    class, type and OpName, and for an OpUndef the opcodes
                    that use it; nothing is written (OUT may be -)
  --skip-var N      leave candidate N as it is (repeatable; bisection)
  --only-var N      poison candidate N alone (repeatable)
  --no-vars         leave the variables (poison the OpUndefs only)
  --no-undef        leave the OpUndefs (poison the variables only)
  --check           re-read OUT and check its structure (header, id bound,
                    every id defined once, every module-scope operand
                    defined before its use)

Prints one line: `IN: N variables poisoned (F function, P private), U
undef, S skipped, K constants added, bound A -> B`.  Exit status 1 on a
malformed input or a failed --check.

The build's own validator (spirv-val) is not in the container; the
structural --check, spirv-cross (tools/toolchain/deps/spirv-cross) reading
the module and the Khronos validation layer at shader module creation
(build-host/tmp/v096/poison.sh) stand in for it.
"""

import struct
import sys

MAGIC = 0x07230203

OP_NAME = 5
OP_UNDEF = 1
OP_TYPE_VOID = 19
OP_TYPE_BOOL = 20
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_VECTOR = 23
OP_TYPE_MATRIX = 24
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLER = 26
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_ARRAY = 28
OP_TYPE_RUNTIME_ARRAY = 29
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_TYPE_FUNCTION = 33
OP_CONSTANT_TRUE = 41
OP_CONSTANT = 43
OP_CONSTANT_COMPOSITE = 44
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_VARIABLE = 59
OP_LABEL = 248
OP_COPY_OBJECT = 83

SC_PRIVATE = 6
SC_FUNCTION = 7
SC_NAMES = {0: "UniformConstant", 1: "Input", 2: "Uniform", 3: "Output", 4: "Workgroup",
            5: "CrossWorkgroup", 6: "Private", 7: "Function", 9: "PushConstant",
            12: "StorageBuffer"}

# Opcodes whose first operand is the result id (no result type): the type
# declarations, OpLabel, OpExtInstImport, OpString
RESULT_ONLY = set(range(19, 40)) | {322, 327, 248, 11, 7, 73}


def words_of(data):
    if len(data) % 4 != 0:
        raise ValueError("not a whole number of words")
    w = list(struct.unpack("<%dI" % (len(data) // 4), data))
    if not w or w[0] != MAGIC:
        raise ValueError("not a little-endian SPIR-V module")
    return w


def split(w):
    """the instructions after the header: (opcode, operands) each"""
    out = []
    i = 5
    while i < len(w):
        n = w[i] >> 16
        op = w[i] & 0xFFFF
        if n == 0 or i + n > len(w):
            raise ValueError("bad instruction length at word %d" % i)
        out.append([op, w[i + 1:i + n]])
        i += n
    return out


def join(header, insts):
    w = list(header)
    for op, ops in insts:
        w.append(((len(ops) + 1) << 16) | op)
        w.extend(ops)
    return struct.pack("<%dI" % len(w), *w)


def string_of(ops):
    b = struct.pack("<%dI" % len(ops), *ops)
    return b.split(b"\0", 1)[0].decode("utf-8", "replace")


# Opcodes that define a result id with a result type (operand 0 the type,
# 1 the id): OpUndef, OpExtInst, the constants, the function, its
# parameters and calls, the memory, composite, image, conversion,
# arithmetic, relational, bit, derivative and atomic instructions that
# have a result (not OpImageWrite), OpPhi.  Every other opcode outside
# RESULT_ONLY defines no id.  The set covers what DXC emits; an opcode
# missing here only weakens --check and the use census.
TYPE_RESULT = ({1, 12, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 54, 55, 57, 59, 60, 61, 65, 66,
                67, 68, 69, 70, 245}
               | set(range(77, 85)) | (set(range(86, 108)) - {99}) | set(range(109, 125))
               | set(range(126, 153)) | set(range(154, 192)) | set(range(194, 206))
               | set(range(207, 216)) | ({227} | set(range(229, 243))))


def has_type_and_result(op):
    return op in TYPE_RESULT


class Module:
    def __init__(self, data):
        w = words_of(data)
        self.header = w[:5]
        self.insts = split(w)
        self.bound = w[3]
        self.types = {}      # id -> (op, operands)
        self.type_at = {}    # id -> instruction index
        self.consts = {}     # id -> (type id, value words)
        self.names = {}
        self.first_function = len(self.insts)
        for k, (op, ops) in enumerate(self.insts):
            if op == OP_FUNCTION and self.first_function == len(self.insts):
                self.first_function = k
            if 19 <= op <= 39 or op in (322, 327):
                self.types[ops[0]] = (op, ops)
                self.type_at[ops[0]] = k
            elif op == OP_CONSTANT:
                self.consts[ops[1]] = (ops[0], ops[2:])
            elif op == OP_NAME:
                self.names[ops[0]] = string_of(ops[1:])

    def new_id(self):
        i = self.bound
        self.bound += 1
        return i

    def describe(self, t):
        op, ops = self.types.get(t, (None, None))
        if op == OP_TYPE_BOOL:
            return "bool"
        if op == OP_TYPE_INT:
            return ("int%d" if ops[2] else "uint%d") % ops[1]
        if op == OP_TYPE_FLOAT:
            return "float%d" % ops[1]
        if op == OP_TYPE_VECTOR:
            return "%s vec%d" % (self.describe(ops[1]), ops[2])
        if op == OP_TYPE_MATRIX:
            return "%sx%d" % (self.describe(ops[1]), ops[2])
        if op == OP_TYPE_ARRAY:
            n = self.consts.get(ops[2], (0, [0]))[1][0]
            return "%s[%d]" % (self.describe(ops[1]), n)
        if op == OP_TYPE_STRUCT:
            name = self.names.get(t, "")
            return "struct %s{%s}" % (name + " " if name else "",
                                       ", ".join(self.describe(m) for m in ops[1:]))
        if op == OP_TYPE_POINTER:
            return "ptr(%s) %s" % (SC_NAMES.get(ops[1], ops[1]), self.describe(ops[2]))
        return "op%s" % op


class Poisoner:
    def __init__(self, mod):
        self.m = mod
        self.cache = {}       # type id -> constant id
        self.inserts = {}     # instruction index -> [new instructions after it]
        self.added = 0

    def emit_after(self, type_id, inst):
        k = self.m.type_at[type_id]
        self.inserts.setdefault(k, []).append(inst)
        self.added += 1

    def constant(self, t):
        """the poison constant of type t, or None when it cannot be built"""
        if t in self.cache:
            return self.cache[t]
        op, ops = self.m.types.get(t, (None, None))
        c = None
        if op == OP_TYPE_BOOL:
            c = self.m.new_id()
            self.emit_after(t, [OP_CONSTANT_TRUE, [t, c]])
        elif op == OP_TYPE_FLOAT:
            width = ops[1]
            val = {16: [0x7E00], 32: [0x7FC00000], 64: [0x00000000, 0x7FF80000]}.get(width)
            if val is not None:
                c = self.m.new_id()
                self.emit_after(t, [OP_CONSTANT, [t, c] + val])
        elif op == OP_TYPE_INT:
            width = ops[1]
            val = {16: [0xBEEF], 32: [0xDEADBEEF], 64: [0xDEADBEEF, 0xDEADBEEF]}.get(width)
            if val is not None:
                c = self.m.new_id()
                self.emit_after(t, [OP_CONSTANT, [t, c] + val])
        elif op in (OP_TYPE_VECTOR, OP_TYPE_MATRIX):
            e = self.constant(ops[1])
            if e is not None:
                c = self.m.new_id()
                self.emit_after(t, [OP_CONSTANT_COMPOSITE, [t, c] + [e] * ops[2]])
        elif op == OP_TYPE_ARRAY:
            n = self.m.consts.get(ops[2])
            e = self.constant(ops[1])
            if e is not None and n is not None:
                c = self.m.new_id()
                self.emit_after(t, [OP_CONSTANT_COMPOSITE, [t, c] + [e] * n[1][0]])
        elif op == OP_TYPE_STRUCT:
            members = [self.constant(mt) for mt in ops[1:]]
            if members and all(x is not None for x in members):
                c = self.m.new_id()
                self.emit_after(t, [OP_CONSTANT_COMPOSITE, [t, c] + members])
        self.cache[t] = c
        return c


def candidates(mod):
    """the variables without an initializer (Function, Private) and the
    OpUndefs, in module order: (kind, index of the instruction, type id)"""
    out = []
    for k, (op, ops) in enumerate(mod.insts):
        if op == OP_VARIABLE and len(ops) == 3 and ops[2] in (SC_FUNCTION, SC_PRIVATE):
            pt = mod.types.get(ops[0])
            if pt and pt[0] == OP_TYPE_POINTER:
                out.append(("var", k, pt[1][2]))
    for k, (op, ops) in enumerate(mod.insts):
        if op == OP_UNDEF:
            out.append(("undef", k, ops[0]))
    return out


def undef_users(mod, uid):
    users = {}
    for op, ops in mod.insts:
        start = 2 if has_type_and_result(op) else (1 if op in RESULT_ONLY else 0)
        if uid in ops[start:]:
            users[op] = users.get(op, 0) + 1
    return users


OPNAMES = {245: "OpPhi", 80: "OpCompositeConstruct", 82: "OpCompositeInsert",
           79: "OpVectorShuffle", 81: "OpCompositeExtract", 62: "OpStore", 169: "OpSelect",
           57: "OpFunctionCall", 12: "OpExtInst", 254: "OpReturnValue"}


def poison(mod, skip, only, do_vars, do_undef, listing):
    p = Poisoner(mod)
    cands = candidates(mod)
    nv = nf = np_ = nu = ns = 0
    in_function = False
    for idx, (kind, k, t) in enumerate(cands):
        op, ops = mod.insts[k]
        if listing:
            name = mod.names.get(ops[1], "")
            if kind == "var":
                print("  %3d var   %-8s %-40s %s" % (idx, SC_NAMES.get(ops[2], ops[2]),
                                                     mod.describe(t), name))
            else:
                users = undef_users(mod, ops[1])
                print("  %3d undef %-8s %-40s used by %s" % (
                    idx, "", mod.describe(t),
                    ", ".join("%s x%d" % (OPNAMES.get(o, "op%d" % o), n)
                              for o, n in sorted(users.items())) or "nothing"))
            continue
        if idx in skip or (only and idx not in only):
            continue
        if (kind == "var" and not do_vars) or (kind == "undef" and not do_undef):
            continue
        c = p.constant(t)
        if c is None:
            ns += 1
            continue
        if kind == "var":
            ops.append(c)
            nv += 1
            if ops[2] == SC_FUNCTION:
                nf += 1
            else:
                np_ += 1
        else:
            if k < mod.first_function:
                # at module scope (no OpCopyObject there) the OpUndef goes
                # and its uses are renamed to the constant
                mod.insts[k] = None
                rename(mod, ops[1], c)
            else:
                mod.insts[k] = [OP_COPY_OBJECT, [t, ops[1], c]]
            nu += 1
    if listing:
        return None
    # module-scope OpUndefs removed (their uses renamed): drop the holes,
    # then place the constants after their types (indices before the drop)
    out = []
    for k, inst in enumerate(mod.insts):
        if inst is not None:
            out.append(inst)
        for extra in p.inserts.get(k, []):
            out.append(extra)
    mod.insts = out
    return nv, nf, np_, nu, ns, p.added


def rename(mod, old, new):
    for inst in mod.insts:
        if inst is None:
            continue
        op, ops = inst
        start = 2 if has_type_and_result(op) else (1 if op in RESULT_ONLY else 0)
        for i in range(start, len(ops)):
            if ops[i] == old:
                ops[i] = new


def check(data):
    """structural check of a written module; the list of problems"""
    problems = []
    try:
        w = words_of(data)
        insts = split(w)
    except ValueError as e:
        return [str(e)]
    bound = w[3]
    defined = {}
    for k, (op, ops) in enumerate(insts):
        rid = None
        if has_type_and_result(op) and len(ops) >= 2:
            rid = ops[1]
        elif op in RESULT_ONLY and ops:
            rid = ops[0]
        if rid is not None:
            if rid >= bound or rid == 0:
                problems.append("id %d outside the bound %d" % (rid, bound))
            if rid in defined:
                problems.append("id %d defined twice" % rid)
            defined[rid] = k
    # module scope: a constant's or variable's operands are defined before it
    for k, (op, ops) in enumerate(insts):
        if op == OP_FUNCTION:
            break
        if op in (OP_CONSTANT_COMPOSITE, OP_VARIABLE):
            refs = ops[2:] if op == OP_CONSTANT_COMPOSITE else ops[3:]
            refs = [ops[0]] + list(refs)
            for r in refs:
                if r not in defined or defined[r] >= k:
                    problems.append("instruction %d (op %d) uses id %d before its definition"
                                    % (k, op, r))
    # function-scope variables: an initializer is a constant defined at
    # module scope
    first_fn = next((k for k, (op, _) in enumerate(insts) if op == OP_FUNCTION), len(insts))
    for k, (op, ops) in enumerate(insts):
        if op == OP_VARIABLE and len(ops) == 4 and k > first_fn:
            if ops[3] not in defined or defined[ops[3]] >= first_fn:
                problems.append("function variable %d: initializer %d not a module constant"
                                % (ops[1], ops[3]))
    return problems


def main(argv):
    args = list(argv[1:])
    skip, only = set(), set()
    do_vars = do_undef = True
    listing = do_check = False
    pos = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--skip-var":
            skip.add(int(args[i + 1]))
            i += 2
            continue
        if a == "--only-var":
            only.add(int(args[i + 1]))
            i += 2
            continue
        if a == "--no-vars":
            do_vars = False
        elif a == "--no-undef":
            do_undef = False
        elif a == "--list":
            listing = True
        elif a == "--check":
            do_check = True
        elif a.startswith("--"):
            sys.stderr.write("spirv_poison.py: unknown option %s\n" % a)
            return 2
        else:
            pos.append(a)
        i += 1
    if len(pos) != 2:
        sys.stderr.write(__doc__)
        return 2
    src, dst = pos
    with open(src, "rb") as f:
        data = f.read()
    try:
        mod = Module(data)
    except ValueError as e:
        sys.stderr.write("%s: %s\n" % (src, e))
        return 1
    if listing:
        print("%s:" % src)
        poison(mod, skip, only, do_vars, do_undef, True)
        return 0
    before = mod.bound
    nv, nf, np_, nu, ns, added = poison(mod, skip, only, do_vars, do_undef, False)
    mod.header[3] = mod.bound
    out = join(mod.header, mod.insts)
    with open(dst, "wb") as f:
        f.write(out)
    print("%s: %d variables poisoned (%d function, %d private), %d undef, %d skipped, "
          "%d constants added, bound %d -> %d" % (src, nv, nf, np_, nu, ns, added, before,
                                                 mod.bound))
    if do_check:
        problems = check(out)
        for pr in problems:
            print("%s: check: %s" % (dst, pr))
        if problems:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
