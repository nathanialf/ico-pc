#!/ usr / bin / env python3

"""python3 -I tools/vu_variant_diff.py [--decomp DIR] [--check FILE] [--hunks]

The June 2001 demo (VERSION_JUN26) ships its own VU1 programs for cluster, mesh,
normal_c and normal_l (particle is shared).  The decomp carries them as
VERSION_JUN26 blocks in ico2/vusrc/*.vsm.  This tool runs each .vsm through cpp
twice, as tools/gen_ninja.py of the decomp does (-DVERSION_<SLUG>, -Ivusrc, and
the LN(pal,jpn,kor,ntsc,aug6,jun26,cn) selector, here the PAL column or the
JUN26 column), normalises the text and writes the difference:

  * comments (';' to the end of the line), blank lines, line markers dropped
  * runs of blanks collapsed to one space
  * integer operands in decimal (0xd0 and 208 are one spelling)
  * the loi operand as the float's bit pattern in hex (255 and 0x437f0000)

A line of the normalised text is one VU instruction pair (upper and lower
slot) or a label.  The output is a unified diff with two lines of context,
one "@@" header per hunk naming the program, the hunk's number, the normalised
line ranges and the label the hunk sits under.  The hunk numbers are the keys of
tools/test/vu1/jun26_vs_pal.md.

  --decomp DIR   the decomp checkout (default: $ICO_DECOMP_DIR); without it the
                 tool exits 77 (skipped)
  --check FILE   compare the output with FILE byte for byte, exit 1 on a
                 difference (ctest vu1_variant_diff)
  --check-table  no decomp needed: every hunk of tools/test/vu1/jun26_vs_pal.diff
                 has exactly one row in jun26_vs_pal.md with a classification
                 ("scheduling only" or "semantic: ..."), the rows are all hunks, the
                 number of semantic rows is VU1_JUN26_SEMANTIC_DIFFS in
                 port/render/vu1_ref/vu1_ref.h, and VU_F_JUN26 in
                 port/shaders/vu_common.hlsli is ICO_VU_JUN26 in
                 port/shaders/shader_consts.h (ctest vu1_variant_table)

The decomp's own gate proves each text is the JUN26 ELF's (and the PAL ELF's),
so the committed diff is byte-anchored to the discs.
"""
import difflib
import os
import re
import shutil
import struct
import subprocess
import sys

PROGRAMS = ("cluster", "mesh", "normal_c", "normal_l", "particle")
CONTEXT = 2

# LN(pal,jpn,kor,ntsc,aug6,jun26,cn): the column each variant takes
VARIANTS = (("pal", "a", []), ("jun26", "f", ["-DVERSION_JUN26"]))


def cpp_text(decomp, program, column, defs):
    """The program's text after cpp for one variant (gen_ninja.py's vucpp)."""
    vusrc = os.path.join(decomp, "ico2", "vusrc")
    cpp = os.environ.get("CPP") or shutil.which("cpp")
    if not cpp:
        raise RuntimeError("no cpp on the path (set CPP)")
    cmd = [cpp, "-P", "-traditional-cpp", "-undef", "-x", "assembler-with-cpp"] + defs + [
        "-DLN(a,b,c,d,e,f,g)=" + column, "-I" + vusrc, os.path.join(vusrc, program + ".vsm")]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if r.returncode != 0:
        raise RuntimeError("cpp failed on %s: %s" % (program, r.stderr.strip()))
    return r.stdout


def _num(m):
    return str(int(m.group(0), 16))


def _loi(m):
    tok = m.group(1)
    if tok.lower().startswith("0x"):
        bits = int(tok, 16)
    else:
        bits = struct.unpack("<I", struct.pack("<f", float(tok)))[0]
    return "loi 0x%08x" % bits


def normalise(text):
    out = []
    for line in text.split("\n"):
        line = line.split(";", 1)[0]
        line = re.sub(r"\s+", " ", line).strip()
        if not line or line.startswith("#"):
            continue
        line = re.sub(r"loi\s+(\S+)", _loi, line)
        line = re.sub(r"\b0[xX][0-9a-fA-F]+\b", _num, line)
        out.append(line)
    return out


def nearest_label(lines, i):
    """The label at or above normalised line i (0-based)."""
    for k in range(min(i, len(lines) - 1), -1, -1):
        if lines[k].endswith(":"):
            return lines[k][:-1]
    return ""


def hunks(program, a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    res = []
    for n, group in enumerate(sm.get_grouped_opcodes(CONTEXT), 1):
        i1, i2 = group[0][1], group[-1][2]
        j1, j2 = group[0][3], group[-1][4]
        res.append((n, i1, i2, j1, j2, group))
    return res


def render(decomp):
    out = []
    texts = {}
    for program in PROGRAMS:
        texts[program] = [normalise(cpp_text(decomp, program, col, defs)) for _, col, defs in VARIANTS]
    for program in PROGRAMS:
        a, b = texts[program]
        out.append("--- %s (PAL)\n+++ %s (JUN26)\n" % (program, program))
        hs = hunks(program, a, b)
        if not hs:
            out.append("# %s: identical in both variants\n" % program)
        for n, i1, i2, j1, j2, group in hs:
            out.append("@@ %s #%d -%d,%d +%d,%d @@ %s\n" % (
                program, n, i1 + 1, i2 - i1, j1 + 1, j2 - j1, nearest_label(a, i1)))
            for tag, x1, x2, y1, y2 in group:
                if tag == "equal":
                    out.extend(" " + s + "\n" for s in a[x1:x2])
                    continue
                out.extend("-" + s + "\n" for s in a[x1:x2])
                out.extend("+" + s + "\n" for s in b[y1:y2])
    return "".join(out)


def check_table():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

    def read(*p):
        with open(os.path.join(root, *p), encoding="utf-8") as f:
            return f.read()

    errors = []
    diff = read("tools", "test", "vu1", "jun26_vs_pal.diff")
    md = read("tools", "test", "vu1", "jun26_vs_pal.md")
    hunk_ids = re.findall(r"^@@ (\w+) #(\d+) ", diff, re.M)
    rows = {}
    for m in re.finditer(r"^\| (\w+) #(\d+) \|.*\| ([^|]*) \|$", md, re.M):
        key = (m.group(1), m.group(2))
        if key in rows:
            errors.append("%s #%s has two rows" % key)
        rows[key] = m.group(3).strip()
    for key in hunk_ids:
        if key not in rows:
            errors.append("%s #%s has no row in jun26_vs_pal.md" % key)
    for key in rows:
        if key not in hunk_ids:
            errors.append("%s #%s is not a hunk of jun26_vs_pal.diff" % key)
    semantic = 0
    for key, cls in rows.items():
        if cls.startswith("semantic:"):
            semantic += 1
        elif not cls.startswith("scheduling only"):
            errors.append("%s #%s: classification '%s'" % (key + (cls[:40],)))
    m = re.search(r"#define VU1_JUN26_SEMANTIC_DIFFS (\d+)", read("port", "render", "vu1_ref", "vu1_ref.h"))
    if not m or int(m.group(1)) != semantic:
        errors.append("VU1_JUN26_SEMANTIC_DIFFS is %s, the table has %d semantic rows" % (m and m.group(1), semantic))
    a = re.search(r"#define VU_F_JUN26 (\d+)u", read("port", "shaders", "vu_common.hlsli"))
    b = re.search(r"ICO_VU_JUN26 = (\d+)", read("port", "shaders", "shader_consts.h"))
    if not a or not b or a.group(1) != b.group(1):
        errors.append("VU_F_JUN26 and ICO_VU_JUN26 differ")
    for e in errors:
        sys.stderr.write("vu_variant_diff --check-table: %s\n" % e)
    if not hunk_ids:
        errors.append("no hunks")
    if not errors:
        sys.stdout.write("vu_variant_diff --check-table: %d hunks, %d semantic, ok\n" % (len(hunk_ids), semantic))
    return 1 if errors else 0


def main(argv):
    decomp = os.environ.get("ICO_DECOMP_DIR", "")
    check = None
    table = False
    args = list(argv)
    while args:
        t = args.pop(0)
        if t == "--decomp":
            decomp = args.pop(0)
        elif t == "--check":
            check = args.pop(0)
        elif t == "--check-table":
            table = True
        else:
            sys.stderr.write(__doc__)
            return 2
    if table:
        return check_table()
    if not (os.environ.get("CPP") or shutil.which("cpp")):
        sys.stderr.write("vu_variant_diff: no cpp on the path (set CPP): skipped\n")
        return 77
    if not decomp or not os.path.isfile(os.path.join(decomp, "ico2", "vusrc", "mesh.vsm")):
        sys.stderr.write("vu_variant_diff: no decomp (ICO_DECOMP_DIR / --decomp): skipped\n")
        return 77
    text = render(decomp)
    if check:
        with open(check, "rb") as f:
            want = f.read()
        if want != text.encode("utf-8"):
            sys.stderr.write("vu_variant_diff: %s differs from the decomp's text; regenerate with "
                             "tools/vu_variant_diff.py > %s\n" % (check, check))
            return 1
        sys.stdout.write("vu_variant_diff: %s matches (%d hunks)\n" % (check, text.count("\n@@ ") + text.startswith("@@ ")))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
