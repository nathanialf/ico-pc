#!/usr/bin/env python3
"""The scene ledger's gate: every place that leaves a model's draw out names its reason.

The scene ledger (port/render/rd_ledger.h, docs/port/SCENE_LEDGER.md) says, per
stage, which models were drawn and which were left out and why.  It is only as
good as its coverage: a new place that skips a draw without telling the ledger
makes the next report silent again (issue 221).  This gate fails when

  1. a reason of RdLedgerReason (rd_ledger.h) has no words in rd_ledger.c's
     kReasonWords, or the other way round (the C file's _Static_assert holds
     the same at compile time), or a reason is named by no hook call outside
     rd_ledger.c (a reason nothing can record);
  2. in a ledgered function (LEDGERED below) an early return is not preceded,
     inside its own block, by an rd__ledger_ call, or the function no longer
     calls the hook its table row names;
  3. a counted drop site (COUNTED below: the box cull's tests, the packet box
     test, the draw filter's callers, the visibility test's NOP) appears more
     or fewer times than the table says.  A new site changes the count: give
     it a reason (rd_ledger.h, rd_ledger.c's words, a hook call at the site)
     and a row here, or, for a site the ledger cannot see yet, a row that says
     so (docs/port/SCENE_LEDGER.md lists them).

    python3 -I tools/check_drop_ledger.py [REPO]
    python3 -I tools/check_drop_ledger.py --selftest
"""

import re
import sys
from pathlib import Path

# (file, function, hook its body must call): every early return inside the
# function must have an rd__ledger_ call before it in its own block
LEDGERED = [
    ("port/render/rd_mesh.c", "RdMesh rd_create_vu_mesh(const RdVuMeshDesc *d)", "rd__ledger_built("),
    ("port/render/rd_mesh.c", "void rd_draw_vu_mesh(RdMesh mesh, const RdVuDraw *d, RdKey key)",
     "rd__ledger_draw("),
]

# (file, the function's first line, the hook it must call)
CALLS = [
    ("port/render/rd_core.c",
     "void rd_note_cull_drop(const char *model, int part, int rule, int views, int oneWhy)",
     "rd__ledger_box("),
    ("port/render/rd_vis.c",
     "static void visWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)",
     "rd__ledger_cull("),
    ("port/render/rd_vis.c", "const RdFrame *rd__vis_frame(const RdFrame *f, int keep)",
     "rd__ledger_picture("),
]

# (file, pattern, count, what it is and how the ledger sees it)
COUNTED = [
    ("ico2/seki/src/RegistPacket.c", r"if \(gsb_ClipBox\(box\) == 0\) \{", 5,
     "the game's object, node and part box tests: each followed by REG_CULL_DROP "
     "(rd_note_cull_drop, RD_LEDGER_BOX_*), checked line by line below"),
    ("ico2/seki/src/RegistPacket.c", r"ret = gsb_ClipBox\(pk->box\[0\]\)", 3,
     "NOT IN THE LEDGER: the packet box test (reg_clipPacketBoundingBox), a material "
     "packet left out with no hook"),
    ("ico2/seki/src/RegistPacket.c", r"r = reg_clipPacketBoundingBox\(pkt\)", 6,
     "NOT IN THE LEDGER: the packet box test's callers"),
    ("ico2/seki/src/RegistPacket.c", r"if \(m\.id == 0\) \{", 1,
     "NOT IN THE LEDGER when the packet has no batches (Packet.c pac_hostDesc); a refused "
     "build is RD_LEDGER_NO_MESH at rd_create_vu_mesh"),
    ("ico2/seki/src/Packet.c", r"\*nb == PAC_HOST_BATCHES", 1,
     "NOT IN THE LEDGER: a packet's stream cut at the batch cap or the scratch size "
     "(pac_hostStream), the rest of the packet dropped"),
    ("port/render/rd_mesh.c", r"!rd__draw_filter_pass\(key\)", 3,
     "the draw filter in rd_draw_vu_mesh (RD_LEDGER_FILTER); NOT IN THE LEDGER: "
     "rd_draw_vu_grid and rd_draw_vu (no model name)"),
    ("port/render/rd_core.c", r"rd__draw_filter_pass\(key\)", 2,
     "NOT IN THE LEDGER: the draw filter in rd_screen_prims and pushStub "
     "(screen prims, world prims, shadow strips: no model name)"),
    ("port/render/rd_shadow.c", r"!rd__draw_filter_pass\(key\)", 1,
     "NOT IN THE LEDGER: the draw filter in rd_shadow_tris (a shadow volume, no model name)"),
    ("port/render/rd_vis.c", r"\.type = RDC_NOP;", 1,
     "the visibility test's skips (RD_LEDGER_FRUSTUM, RD_LEDGER_OCCLUDED: visWalk "
     "calls rd__ledger_cull for every skip)"),
]


def function_body(text, head):
    """The lines of the function whose first line is head, through its closing brace."""
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if line == head:
            for j in range(i + 1, len(lines)):
                if lines[j] == "}":
                    return lines[i:j + 1]
            return None
    return None


def indent(line):
    return len(line) - len(line.lstrip(" "))


def early_returns_unledgered(body):
    """The returns of body not preceded in their own block by an rd__ledger_ call.

    A return is early unless it is the function's last statement.  Its block
    is the lines from the nearest line before it that ends in '{' with less
    indentation."""
    bad = []
    last = max(i for i, l in enumerate(body) if l.strip() and l != "}")
    for i, line in enumerate(body):
        if not re.search(r"\breturn\b", line) or i == last:
            continue
        ind = indent(line)
        ok = False
        for k in range(i - 1, 0, -1):
            prev = body[k]
            if "rd__ledger_" in prev:
                ok = True
                break
            if prev.rstrip().endswith("{") and indent(prev) < ind:
                break
        if not ok:
            bad.append(line.strip())
    return bad


def check(root):
    errors = []
    checked = 0
    hdr = (root / "port/render/rd_ledger.h").read_text(encoding="latin-1")
    src = (root / "port/render/rd_ledger.c").read_text(encoding="latin-1")
    enum = re.search(r"typedef enum RdLedgerReason \{(.*?)RD_LEDGER_REASONS", hdr, re.S)
    reasons = re.findall(r"^\s*(RD_LEDGER_[A-Z_]+)", enum.group(1), re.M) if enum else []
    words = re.search(r"kReasonWords\[\] = \{(.*?)\};", src, re.S)
    nwords = len(re.findall(r'"[^"]*",', words.group(1))) if words else -1
    if not reasons:
        errors.append("rd_ledger.h: RdLedgerReason not found")
    if nwords != len(reasons):
        errors.append(f"rd_ledger.c: {nwords} reason words for {len(reasons)} reasons")
    users = ""
    for p in sorted((root / "port/render").glob("*.c")):
        if p.name != "rd_ledger.c":
            users += p.read_text(encoding="latin-1")
    for r in reasons:
        checked += 1
        if not re.search(r"\b" + r + r"\b", users):
            errors.append(f"{r}: no hook call outside rd_ledger.c names it (a reason nothing records)")

    for path, head, hook in LEDGERED:
        checked += 1
        text = (root / path).read_text(encoding="latin-1")
        body = function_body(text, head)
        if body is None:
            errors.append(f"{path}: the ledgered function '{head}' is gone (update the gate's table)")
            continue
        if not any(hook in l for l in body):
            errors.append(f"{path}: '{head}' no longer calls {hook}")
        for r in early_returns_unledgered(body):
            errors.append(f"{path}: '{head}': '{r}' leaves a draw out with no rd__ledger_ call "
                          "before it (give it a reason, rd_ledger.h)")

    for path, head, hook in CALLS:
        checked += 1
        body = function_body((root / path).read_text(encoding="latin-1"), head)
        if body is None or not any(hook in l for l in body):
            errors.append(f"{path}: '{head}' must call {hook}")

    for path, pat, want, what in COUNTED:
        checked += 1
        text = (root / path).read_text(encoding="latin-1")
        got = len(re.findall(pat, text))
        if got != want:
            errors.append(f"{path}: '{pat}' {got} times, the table says {want} ({what}): a drop "
                          "site was added or removed; give a new one a ledger reason and update "
                          "tools/check_drop_ledger.py and docs/port/SCENE_LEDGER.md")

    # every box test of the game's cull reports its drop on the next line
    rp = (root / "ico2/seki/src/RegistPacket.c").read_text(encoding="latin-1").split("\n")
    for i, line in enumerate(rp):
        if re.search(r"if \(gsb_ClipBox\(box\) == 0\) \{", line):
            checked += 1
            if i + 1 >= len(rp) or "REG_CULL_DROP(" not in rp[i + 1]:
                errors.append(f"ico2/seki/src/RegistPacket.c:{i + 1}: a box test without "
                              "REG_CULL_DROP on its next line")
    return errors, checked


SELF_BAD = """void f(int a)
{
    if (a) {
        return;
    }
    g();
}"""

SELF_GOOD = """void f(int a)
{
    if (a) {
        rd__ledger_draw_out(0, 0, 1);
        return;
    }
    g();
    return;
}"""


def selftest():
    bad = early_returns_unledgered(SELF_BAD.split("\n"))
    good = early_returns_unledgered(SELF_GOOD.split("\n"))
    if bad != ["return;"] or good != []:
        print(f"check_drop_ledger: selftest FAILED (bad {bad}, good {good})")
        return 1
    print("check_drop_ledger: selftest ok (an unledgered early return found, a ledgered one passed)")
    return 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--selftest":
        return selftest()
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    errors, checked = check(root)
    for e in errors:
        print("check_drop_ledger: " + e)
    if errors:
        print(f"check_drop_ledger: FAILED ({len(errors)} of {checked} checks)")
        return 1
    print(f"check_drop_ledger: ok ({checked} checks: reasons, ledgered functions, counted drop sites)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
