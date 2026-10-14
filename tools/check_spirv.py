#!/usr/bin/env python3
"""tools/check_spirv.py DIR [DIR ...]

The SPIR-V gate: every *.spv under the directories given is read and every
finding is printed as `file: gate: text`; the exit status is 1 when there is
one.

Gates:
  aggregate  no instruction produces or stores a whole array: no OpLoad,
             OpStore, OpCompositeConstruct, OpCompositeInsert, OpPhi,
             OpSelect, OpCopyObject or OpCopyLogical whose type is an array
             (an array in a constant buffer or a storage buffer is read
             element by element, through OpAccessChain, and that is fine).
             A shader built around array-typed values (a local array copied
             whole, built from a list, or passed through a loop) was the
             first pipeline two drivers' compilers crashed on while the
             shaders without one built, so the HLSL writes such code out
             over scalars (port/shaders/vu_common.hlsli, vu_clip_corners).
  localarray no vertex shader keeps an array in the Function storage class
             (a local array indexed at run time is memory the compiler lays
             out). Pixel shaders may (font_sheet_ps, yuv_ps: built fine on
             the same drivers).

The modules are the build's port/shaders output (CMAKE_BINARY_DIR/shaders);
the ctest spirv_check runs this over it. No SPIR-V tool is needed: the
binary form is decoded here.
"""
import os
import struct
import sys

OP_ENTRY_POINT = 15
OP_TYPE_ARRAY = 28
OP_TYPE_RUNTIME_ARRAY = 29
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_VARIABLE = 59
OP_LOAD = 61
OP_STORE = 62
OP_ACCESS_CHAIN = 65
OP_COMPOSITE_CONSTRUCT = 80
OP_COMPOSITE_INSERT = 82
OP_COPY_OBJECT = 83
OP_PHI = 245
OP_SELECT = 169
OP_COPY_LOGICAL = 400
STORAGE_FUNCTION = 7
MODEL_VERTEX = 0
# opcodes whose first two operands are <result type> <result id>
TYPED_VALUE_OPS = {OP_LOAD, OP_COMPOSITE_CONSTRUCT, OP_COMPOSITE_INSERT, OP_COPY_OBJECT, OP_PHI,
                   OP_SELECT, OP_COPY_LOGICAL}
NAMES = {OP_LOAD: "OpLoad", OP_STORE: "OpStore", OP_COMPOSITE_CONSTRUCT: "OpCompositeConstruct",
         OP_COMPOSITE_INSERT: "OpCompositeInsert", OP_COPY_OBJECT: "OpCopyObject", OP_PHI: "OpPhi",
         OP_SELECT: "OpSelect", OP_COPY_LOGICAL: "OpCopyLogical"}


def check(path):
    """the findings of one module, as strings"""
    data = open(path, "rb").read()
    if len(data) < 20 or len(data) % 4 or struct.unpack("<I", data[:4])[0] != 0x07230203:
        return ["not a SPIR-V module"]
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    arrays = set()      # type ids that are arrays
    pointee = {}        # pointer type id -> pointed type id
    value_type = {}     # result id -> result type id (for the typed instructions)
    models = set()
    i = 5
    insts = []
    while i < len(words):
        wc = words[i] >> 16
        op = words[i] & 0xFFFF
        if wc == 0:
            return ["malformed: zero word count at word %d" % i]
        ops = words[i + 1:i + wc]
        insts.append((op, ops))
        if op == OP_ENTRY_POINT:
            models.add(ops[0])
        elif op == OP_TYPE_ARRAY:
            arrays.add(ops[0])
        elif op == OP_TYPE_POINTER:
            pointee[ops[0]] = ops[2]
        elif op in TYPED_VALUE_OPS or op == OP_ACCESS_CHAIN or op == OP_VARIABLE:
            value_type[ops[1]] = ops[0]
        i += wc
    out = []
    counts = {}
    local = 0
    for op, ops in insts:
        if op in TYPED_VALUE_OPS and ops[0] in arrays:
            counts[op] = counts.get(op, 0) + 1
        elif op == OP_STORE and value_type.get(ops[1]) in arrays:
            counts[op] = counts.get(op, 0) + 1
        elif op == OP_VARIABLE and ops[2] == STORAGE_FUNCTION and MODEL_VERTEX in models:
            if pointee.get(ops[0]) in arrays:
                local += 1
    agg = ", ".join("%d %s" % (counts[op], NAMES[op]) for op in sorted(counts))
    if agg:
        out.append("aggregate: array-typed values: " + agg)
    if local:
        out.append("localarray: %d local arrays in a vertex shader" % local)
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    files = []
    for d in argv[1:]:
        if os.path.isfile(d):
            files.append(d)
            continue
        for root, _, names in os.walk(d):
            files += [os.path.join(root, n) for n in names if n.endswith(".spv")]
    if not files:
        print("check_spirv: no .spv files under " + " ".join(argv[1:]))
        return 1
    findings = 0
    for f in sorted(files):
        for text in check(f):
            print("%s: %s" % (f, text))
            findings += 1
    print("check_spirv: %d modules, %d findings" % (len(files), findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
