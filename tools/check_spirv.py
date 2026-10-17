#!/usr/bin/env python3
"""tools/check_spirv.py [--sizes] DIR [DIR ...]

The SPIR-V gate: every *.spv under the directories given is read and every
finding is printed as `file: gate: text`; the exit status is 1 when there is
one.  --sizes also prints one line per module: its stage, words,
instructions, basic blocks (OpLabel), OpPhi, OpLoopMerge and OpDot counts,
and the shape census: comparisons with a vector-of-booleans result, OpSelect
with a boolean or vector-of-booleans result, OpAny and OpAll.

Gates:
  shape      a vertex shader has no vector of booleans (no OpTypeVector of
             OpTypeBool), no OpAny or OpAll and no OpSelect whose result is
             a boolean or a vector of booleans: the forms of v0.5.2's
             vertex shaders, which built on every device the port has
             failed on (scalar comparisons joined with &&, || and !, which
             DXC makes branches, ifs, and ?: between numbers).  v0.7.6's VU
             vertex shaders built on a PowerVR DXT-48-1536's driver
             (driver 26.1); the next set, the same opcodes but 1,476
             comparisons of float vectors, 1,660 selects of boolean
             vectors and 116 any(), crashed its compiler (SIGSEGV in the
             driver on the first pipeline with a VU vertex shader and
             on vu_particle_vs alone).  The HLSL avoids every construct
             the optimiser turns into one (docs/port/SHADERS.md, The
             envelope): a vector chosen after it is computed becomes a
             select on a vector of booleans, so a vector is chosen value
             by value.
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
             VS_BUDGET for a vertex shader, PS_BUDGET for a pixel shader
             (the largest at the gate's introduction, font_sheet_ps_dstrov's
             71,418, plus 10 percent).  A phone's driver compiles the
             start-up set in a time about proportional to the size: v0.7.6's
             VU vertex shaders were 81k to 93k words (the cut copied three
             times as branch chains), minutes on a phone.  The vertex budget
             is a tripwire about a tenth above the largest VU vertex shader
             in the forms above (vu_lit_vs, 51,005: the cut once for the
             blended camera, the two ticks written out with a z cut each
             and their picture and frame rows, the attribute planes);
             v0.5.2's, without the cut, the ticks and the planes, were up
             to 16,268.
  size per variant (issue 107)  the VU vertex shaders' plain variants
             (vu_*_plain_vs, ICO_VU_PLAIN: no card test, no tick cameras;
             port/shaders/vu_common.hlsli) have their own budget,
             VS_PLAIN_BUDGET, a tenth above the largest of them at its
             introduction (vu_lit_plain_vs, 23,305).  The extended
             variants keep VS_BUDGET.  An Adreno 740 (driver 512.676.53)
             built every VU vertex shader of v0.6.1 (at most 32,709 words)
             and hung the whole device compiling those of v0.7.5 to v0.7.7
             (46,470 words and up): the plain variant is what such a
             device compiles, so it stays well inside the size that
             built (docs/port/SHADERS.md, The size history).
             The vertex pre-pass's entries (vu_*_pre_vs: a program's
             vertex function alone, no triangle, no card test, no tick
             cameras; docs/port/RENDER.md, The vertex pre-pass) are held
             to VS_PLAIN_BUDGET too: every device compiles them.
             The VU vertex shaders by release (git archive <tag>
             port/shaders, this DXC and flags; words, and vu_prelit_vs's
             instructions / blocks), and the Adreno 740 with them:
               v0.5.2  5,448 to 16,268 words (3,870 / 604)     built (issue 93)
               v0.6.1  8,200 to 32,709 (7,699 / 1,180)         built (issue 107)
               v0.7.5  10,840 to 49,256 (11,826 / 1,837)       hung (issue 93)
               v0.7.6  10,840 to 93,407 (19,920 / 2,431)       hung (issue 107)
               v0.7.7  10,000 to 51,005 (10,692 / 861)         hung (issue 107)
  variants   every VU vertex shader vu_X_vs in the directories given has
             its plain variant vu_X_plain_vs beside it (only when a
             directory, not single files, is given); a pre-pass entry
             vu_X_pre_vs has none (the variants differ only in the triangle
             rule, which it does not hold).
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
OP_TYPE_BOOL = 20
OP_TYPE_VECTOR = 23
OP_ANY = 154
OP_ALL = 155
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
VS_BUDGET = 56000
VS_PLAIN_BUDGET = 25700
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
    bools = {ops[0] for op, ops in insts if op == OP_TYPE_BOOL}
    bvecs = {ops[0] for op, ops in insts if op == OP_TYPE_VECTOR and ops[1] in bools}
    vcmp = sum(1 for op, ops in insts if 164 <= op <= 191 and ops and ops[0] in bvecs)
    bsel = sum(1 for op, ops in insts if op == OP_SELECT and ops[0] in bools | bvecs)
    anyall = census.get(OP_ANY, 0) + census.get(OP_ALL, 0)
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
    if vertex and (bvecs or bsel or anyall):
        out.append("shape: %d vector-of-boolean types, %d vector comparisons, %d boolean "
                   "selects, %d OpAny/OpAll" % (len(bvecs), vcmp, bsel, anyall))
    if vertex:
        denied = ", ".join("%d %s" % (census[op], DENIED[op]) for op in sorted(DENIED)
                           if op in census)
        if denied:
            out.append("envelope: a vertex shader with " + denied)
        other = ", ".join("%d %s" % (census[op], OTHER_NAMES.get(op, "opcode %d" % op))
                          for op in sorted(census) if op not in VS_OPCODES and op not in DENIED)
        if other:
            out.append("envelope: opcodes outside the vertex-shader set: " + other)
    plain = os.path.basename(path).endswith("_plain_vs.spv")
    pre = os.path.basename(path).endswith("_pre_vs.spv")
    budget = (VS_PLAIN_BUDGET if plain or pre else VS_BUDGET) if vertex else PS_BUDGET
    if nwords > budget:
        out.append("size: %d words, over the %s budget of %d" %
                   (nwords, "plain vertex" if (plain or pre) and vertex else
                    "vertex" if vertex else "pixel", budget))
    if sizes is not None:
        sizes.append("%-32s %-2s %7d words %6d insts %5d blocks %5d phis %3d loops %3d dot "
                     "%4d vcmp %4d bsel %3d any" %
                     (os.path.basename(path), "vs" if vertex else "ps", nwords, len(insts),
                      census.get(OP_LABEL, 0), census.get(OP_PHI, 0),
                      census.get(OP_LOOP_MERGE, 0), census.get(OP_DOT, 0), vcmp, bsel, anyall))
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
    # every VU vertex shader has its plain variant (only over directories)
    if all(not os.path.isfile(d) for d in args):
        names = {os.path.basename(f) for f in files}
        for n in sorted(names):
            if (n.startswith("vu_") and n.endswith("_vs.spv") and
                    not n.endswith("_plain_vs.spv") and not n.endswith("_pre_vs.spv") and
                    n[:-len("_vs.spv")] + "_plain_vs.spv" not in names):
                print("%s: variants: no plain variant %s" %
                      (n, n[:-len("_vs.spv")] + "_plain_vs.spv"))
                findings += 1
    if show:
        print("\n".join(sizes))
    print("check_spirv: %d modules, %d findings" % (len(files), findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
