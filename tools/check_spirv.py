#!/usr/bin/env python3
"""tools/check_spirv.py [--sizes] DIR [DIR ...]

The SPIR-V gate: every *.spv under the directories given is read and every
finding is printed as `file: gate: text`; the exit status is 1 when there is
one.  --sizes also prints one line per module: its stage, words,
instructions, basic blocks (OpLabel), OpPhi, OpLoopMerge and OpDot counts.

Gates:
  envelope   a vertex shader uses only the opcodes of the vertex shaders
             proven on every device the port has failed on (VS_OPCODES):
             the union over v0.5.2's eleven VU vertex shaders and the 2D
             vertex shaders, compiled with the pinned DXC and the flags of
             cmake/IcoShaders.cmake, less OpUndef.  v0.6.0 added to every VU
             vertex shader OpLoopMerge (loops with run-time trip counts),
             OpDot, OpLogicalNotEqual and OpUndef, which no vertex shader
             that builds on an NVIDIA GeForce 10-series card or a Qualcomm
             Adreno 740 has; those drivers crashed or hung on the first
             pipeline with one (docs/port/SHADERS.md, The envelope).  The
             four are named in the finding (DENIED); any other opcode
             outside the set fails as well, so a new construct is a
             decision, not an accident.  (v0.5.2's three grid entries
             carried one OpUndef each; the gate allows none.)
  size       no module is longer than its stage's budget in 32-bit words:
             VS_BUDGET for a vertex shader (v0.5.2's largest was 16,268,
             v0.7.6's 93,407: a phone compiles the start-up set in minutes
             at that size), PS_BUDGET for a pixel shader (the largest at
             the gate's introduction, font_sheet_ps_dstrov's 71,418, plus 10
             percent)
  aggregate  no instruction produces or stores a whole array: no OpLoad,
             OpStore, OpCompositeConstruct, OpCompositeInsert, OpPhi,
             OpSelect, OpCopyObject or OpCopyLogical whose type is an array
             (an array in a constant buffer or a storage buffer is read
             element by element, through OpAccessChain, and that is fine).
             A shader built around array-typed values (a local array copied
             whole, built from a list, or passed through a loop) was the
             first pipeline two drivers' compilers crashed on while the
             shaders without one built, so the HLSL writes such code out
             over scalars (port/shaders/vu_common.hlsli, the cut).
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

OP_UNDEF = 1
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
OP_DOT = 148
OP_LOGICAL_NOT_EQUAL = 165
OP_SELECT = 169
OP_PHI = 245
OP_LOOP_MERGE = 246
OP_LABEL = 248
OP_COPY_LOGICAL = 400
STORAGE_FUNCTION = 7
MODEL_VERTEX = 0
# opcodes whose first two operands are <result type> <result id>
TYPED_VALUE_OPS = {OP_LOAD, OP_COMPOSITE_CONSTRUCT, OP_COMPOSITE_INSERT, OP_COPY_OBJECT, OP_PHI,
                   OP_SELECT, OP_COPY_LOGICAL}
NAMES = {OP_LOAD: "OpLoad", OP_STORE: "OpStore", OP_COMPOSITE_CONSTRUCT: "OpCompositeConstruct",
         OP_COMPOSITE_INSERT: "OpCompositeInsert", OP_COPY_OBJECT: "OpCopyObject", OP_PHI: "OpPhi",
         OP_SELECT: "OpSelect", OP_COPY_LOGICAL: "OpCopyLogical"}

# The vertex-shader envelope: every opcode of v0.5.2's vu_*_vs (git archive
# v0.5.2 port/shaders, DXC v1.9.2609 with ICO_DXC_SPIRV_FLAGS and
# ICO_DXC_COMMON_FLAGS, -T vs_6_0, the entries of that tree's
# port/shaders/CMakeLists.txt) and of the 2D vertex shaders (blend_int_vs,
# blit_vs, crt_vs, fx_rect_vs, sprite_*_vs, yuv_vs), OpUndef taken out.
# The OpExtInst instructions (GLSL.std.450) are not told apart.
VS_OPCODES = {
    3: "Source", 5: "Name", 6: "MemberName", 11: "ExtInstImport", 12: "ExtInst",
    14: "MemoryModel", 15: "EntryPoint", 17: "Capability", 19: "TypeVoid", 20: "TypeBool",
    21: "TypeInt", 22: "TypeFloat", 23: "TypeVector", 24: "TypeMatrix", 28: "TypeArray",
    29: "TypeRuntimeArray", 30: "TypeStruct", 32: "TypePointer", 33: "TypeFunction",
    41: "ConstantTrue", 42: "ConstantFalse", 43: "Constant", 44: "ConstantComposite",
    46: "ConstantNull", 54: "Function", 56: "FunctionEnd", 59: "Variable", 61: "Load",
    62: "Store", 65: "AccessChain", 71: "Decorate", 72: "MemberDecorate", 79: "VectorShuffle",
    80: "CompositeConstruct", 81: "CompositeExtract", 82: "CompositeInsert", 83: "CopyObject",
    109: "ConvertFToU", 110: "ConvertFToS", 111: "ConvertSToF", 112: "ConvertUToF",
    124: "Bitcast", 127: "FNegate", 128: "IAdd", 129: "FAdd", 130: "ISub", 131: "FSub",
    132: "IMul", 133: "FMul", 134: "UDiv", 136: "FDiv", 137: "UMod", 142: "VectorTimesScalar",
    154: "Any", 168: "LogicalNot", 169: "Select", 170: "IEqual", 171: "INotEqual",
    174: "UGreaterThanEqual", 175: "SGreaterThanEqual", 176: "ULessThan", 180: "FOrdEqual",
    182: "FOrdNotEqual", 184: "FOrdLessThan", 186: "FOrdGreaterThan", 188: "FOrdLessThanEqual",
    190: "FOrdGreaterThanEqual", 191: "FUnordGreaterThanEqual", 194: "ShiftRightLogical",
    196: "ShiftLeftLogical", 197: "BitwiseOr", 199: "BitwiseAnd", 245: "Phi",
    247: "SelectionMerge", 248: "Label", 249: "Branch", 250: "BranchConditional", 251: "Switch",
    253: "Return",
}
# the four constructs v0.6.0 brought into the vertex shaders, named in the finding
DENIED = {OP_LOOP_MERGE: "OpLoopMerge", OP_DOT: "OpDot", OP_LOGICAL_NOT_EQUAL: "OpLogicalNotEqual",
          OP_UNDEF: "OpUndef"}
# names for the opcodes most likely to show up outside the set
OTHER_NAMES = {77: "OpVectorExtractDynamic", 78: "OpVectorInsertDynamic", 126: "OpSNegate",
               135: "OpSDiv", 145: "OpMatrixTimesVector", 155: "OpAll", 164: "OpLogicalEqual",
               166: "OpLogicalOr", 167: "OpLogicalAnd", 172: "OpUGreaterThan",
               173: "OpSGreaterThan", 177: "OpSLessThan", 178: "OpULessThanEqual",
               179: "OpSLessThanEqual", 195: "OpShiftRightArithmetic", 198: "OpBitwiseXor",
               200: "OpNot", 252: "OpKill"}
VS_BUDGET = 16000
PS_BUDGET = 78560


def decode(path):
    """(words, [(op, operands)]) of one module, or a string saying why not"""
    data = open(path, "rb").read()
    if len(data) < 20 or len(data) % 4 or struct.unpack("<I", data[:4])[0] != 0x07230203:
        return "not a SPIR-V module"
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    i = 5
    insts = []
    while i < len(words):
        wc = words[i] >> 16
        if wc == 0:
            return "malformed: zero word count at word %d" % i
        insts.append((words[i] & 0xFFFF, words[i + 1:i + wc]))
        i += wc
    return len(words), insts


def check(path, sizes=None):
    """the findings of one module, as strings; sizes, a list, receives its size line"""
    d = decode(path)
    if isinstance(d, str):
        return [d]
    nwords, insts = d
    arrays = set()      # type ids that are arrays
    pointee = {}        # pointer type id -> pointed type id
    value_type = {}     # result id -> result type id (for the typed instructions)
    models = set()
    census = {}
    for op, ops in insts:
        census[op] = census.get(op, 0) + 1
        if op == OP_ENTRY_POINT:
            models.add(ops[0])
        elif op == OP_TYPE_ARRAY:
            arrays.add(ops[0])
        elif op == OP_TYPE_POINTER:
            pointee[ops[0]] = ops[2]
        elif op in TYPED_VALUE_OPS or op == OP_ACCESS_CHAIN or op == OP_VARIABLE:
            value_type[ops[1]] = ops[0]
    vertex = MODEL_VERTEX in models
    out = []
    counts = {}
    local = 0
    for op, ops in insts:
        if op in TYPED_VALUE_OPS and ops[0] in arrays:
            counts[op] = counts.get(op, 0) + 1
        elif op == OP_STORE and value_type.get(ops[1]) in arrays:
            counts[op] = counts.get(op, 0) + 1
        elif op == OP_VARIABLE and ops[2] == STORAGE_FUNCTION and vertex:
            if pointee.get(ops[0]) in arrays:
                local += 1
    agg = ", ".join("%d %s" % (counts[op], NAMES[op]) for op in sorted(counts))
    if agg:
        out.append("aggregate: array-typed values: " + agg)
    if local:
        out.append("localarray: %d local arrays in a vertex shader" % local)
    if vertex:
        denied = ", ".join("%d %s" % (census[op], DENIED[op]) for op in sorted(DENIED)
                           if op in census)
        if denied:
            out.append("envelope: a vertex shader with " + denied)
        other = ", ".join("%d %s" % (census[op], OTHER_NAMES.get(op, "opcode %d" % op))
                          for op in sorted(census) if op not in VS_OPCODES and op not in DENIED)
        if other:
            out.append("envelope: opcodes outside the vertex-shader set: " + other)
    budget = VS_BUDGET if vertex else PS_BUDGET
    if nwords > budget:
        out.append("size: %d words, over the %s budget of %d" %
                   (nwords, "vertex" if vertex else "pixel", budget))
    if sizes is not None:
        sizes.append("%-32s %-2s %7d words %6d insts %5d blocks %5d phis %3d loops %3d dot" %
                     (os.path.basename(path), "vs" if vertex else "ps", nwords, len(insts),
                      census.get(OP_LABEL, 0), census.get(OP_PHI, 0),
                      census.get(OP_LOOP_MERGE, 0), census.get(OP_DOT, 0)))
    return out


def main(argv):
    show = "--sizes" in argv
    args = [a for a in argv[1:] if a != "--sizes"]
    if not args:
        print(__doc__)
        return 2
    files = []
    for d in args:
        if os.path.isfile(d):
            files.append(d)
            continue
        for root, _, names in os.walk(d):
            files += [os.path.join(root, n) for n in names if n.endswith(".spv")]
    if not files:
        print("check_spirv: no .spv files under " + " ".join(args))
        return 1
    findings = 0
    sizes = [] if show else None
    for f in sorted(files):
        for text in check(f, sizes):
            print("%s: %s" % (f, text))
            findings += 1
    if show:
        print("\n".join(sizes))
    print("check_spirv: %d modules, %d findings" % (len(files), findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
