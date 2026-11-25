"""tools/test/state_legacy_gate_test.py -- tools/state_legacy_table.py gate, the Android
package gate on the game's code hashes, over a fake pair: a "release" library and table and
a "new" library, each a small ELF with a game function that yields (by BL to
ico_fiber_yield), one that makes an indirect call, one that can hold no frame, and a port
function, with their symbol files and lld-style link maps naming real game source files.
The same code passes; a yielding game function changed while its source file did not fails
naming it and saying what to do; the same change passes when its file is in the changed
list, or when port/state_legacy/code_changes.txt (a copy under $TMPDIR) records it with
its new hash; a change of the function that can hold no frame is not gated; the indirect
caller is gated; the new library holding the linker's erratum 843419 patch site where the
release had the load (the bird's case, v0.11.16-rc8) passes, as rules 3 hash it as the
load, while rules 2 count the branch. The files are written under $TMPDIR."""
import gzip
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "state_legacy_table.py")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hash_a64  # noqa: E402
import state_legacy_table  # noqa: E402

TEXT_LO, TEXT_HI = 0x1000, 0x4000
# the functions: name, address, the file whose object holds it (None: a port function)
FUNCS = [("ico_fiber_yield", 0x1000, None), ("gYield", 0x1ff0, "act_bird.c"),
         ("gIndirect", 0x2400, "camera-root.c"), ("gLeaf", 0x2800, "camera-root.c"),
         ("pPort", 0x2c00, None)]
STUB = 0x3800


def ins(*ws):
    return list(ws)


def b_to(pc, t, link=False):
    return (0x94000000 if link else 0x14000000) | (((t - pc) // 4) & 0x3FFFFFF)


def library(patch=False, change=None):
    """the ELF bytes; gYield's ADRP x9 at page offset 0xff8 and the load through it 8 bytes
    later (lld's erratum sequence), patched into a B to a stub when patch"""
    img = bytearray(TEXT_HI)
    code = {
        "ico_fiber_yield": ins(0xD503201F, 0xD65F03C0),
        # stp x29, x30; mov x29, sp; adrp x9 (0x1ff8); ldr x8, [sp, #16] (0x1ffc);
        # ldr x9, [x9, #0x448] (0x2000); bl ico_fiber_yield; ldr x0, [x9]; ldp; ret
        "gYield": ins(0xA9BF7BFD, 0x910003FD, 0x90000009, 0xF9400BE8, 0xF9422529, None, 0xF9400120,
                      0xA8C17BFD, 0xD65F03C0),
        "gIndirect": ins(0xA9BF7BFD, 0x910003FD, 0xD63F0100, 0xF9400420, 0xA8C17BFD, 0xD65F03C0),
        "gLeaf": ins(0xF9400420, 0xD65F03C0),
        "pPort": ins(0xF9400820, 0xD65F03C0),
    }
    for name, at, _ in FUNCS:
        ws = list(code[name])
        for k, w in enumerate(ws):
            if w is None:
                ws[k] = b_to(at + 4 * k, 0x1000, link=True)
        if patch and name == "gYield":
            ws[4] = b_to(at + 16, STUB)
            struct.pack_into("<II", img, STUB, 0xF9422529, b_to(STUB + 4, at + 20))
        if change and change[0] == name:
            ws[change[1]] = change[2]
        for k, w in enumerate(ws):
            struct.pack_into("<I", img, at + 4 * k, w)
    # the ELF header and one LOAD segment over the whole file
    struct.pack_into("<4sBBBBB7xHHIQQQIHHHHHH", img, 0, b"\x7fELF", 2, 1, 1, 0, 0, 3, 183, 1, 0x1000,
                     64, 0, 0, 64, 56, 1, 64, 0, 0)
    struct.pack_into("<IIQQQQQQ", img, 64, 1, 5, 0, 0, 0, TEXT_HI, TEXT_HI, 0x1000)
    return bytes(img)


def syms():
    lines = ["%016x T %s" % (at, name) for name, at, _ in FUNCS]
    lines.append("0000000000001000 T ico_diag_init")
    lines.append("section %016x %06x AX .text" % (TEXT_LO, TEXT_HI - TEXT_LO))
    return "\n".join(lines) + "\n"


def link_map():
    out = ["             VMA              LMA     Size Align Out     In      Symbol"]
    for name, at, src in FUNCS:
        obj = "libico_game.a(%s.o)" % src if src else "libico_platform.a(fiber.c.o)"
        out.append("%16x %16x %8x %5d         %s:(.text.%s)" % (at, at, 16, 4, obj, name))
        out.append("%16x %16x %8x %5d                 %s" % (at, at, 16, 1, name))
    return "\n".join(out) + "\n"


def write(path, data):
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)


def table(root, so):
    """a record 3 table "v9.9.9-android" of the release library so, as build --record 3 makes it"""
    d = os.path.join(root, "v9.9.9-android")
    os.makedirs(d)
    sy = syms()
    sec = [l for l in sy.splitlines() if l.startswith("section ")]
    hs = state_legacy_table.unit_hashes(sy, sec, so, 2)
    write(os.path.join(d, "syms.gzip"), gzip.compress(sy.encode()))
    write(os.path.join(d, "hashes.z"), state_legacy_table.deflate(b"".join(struct.pack("<IQQ", *h) for h in hs)))
    write(os.path.join(d, "manifest.txt"), "commit v9.9.8-1-g0123456789\nplatform android-arm64\nkind release\n"
          "record 3\nhash_rules %d\nhash_verify_rules 2\nhashes %d\n" % (state_legacy_table.HASH_RULES, len(hs)))
    write(os.path.join(root, "index.txt"), "android-arm64 release v9.9.9-android v9.9.8-1-g0123456789\n")


def gate(tmp, root, so, changed=()):
    write(os.path.join(tmp, "new.so"), so)
    write(os.path.join(tmp, "new.syms.gz"), gzip.compress(syms().encode()))
    write(os.path.join(tmp, "new.map"), link_map())
    write(os.path.join(tmp, "changed.txt"), "\n".join(changed) + "\n")
    p = subprocess.run([sys.executable, "-I", TOOL, "gate", "--so", os.path.join(tmp, "new.so"),
                        "--syms", os.path.join(tmp, "new.syms.gz"), "--map", os.path.join(tmp, "new.map"),
                        "--changed", os.path.join(tmp, "changed.txt"), "--root", root],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def main():
    bad = []
    tmp = tempfile.mkdtemp(prefix="state_legacy_gate-")
    try:
        root = os.path.join(tmp, "legacy")
        rel = library()
        table(root, rel)
        files = {}
        for dp, _, fs in os.walk(os.path.join(ROOT, "ico2")):
            for f in fs:
                if f in ("act_bird.c", "camera-root.c"):
                    files[f] = os.path.relpath(os.path.join(dp, f), ROOT)
        if len(files) != 2:
            sys.exit("state_legacy_gate_test: the game sources act_bird.c and camera-root.c are not in ico2/")
        rc, out = gate(tmp, root, rel)
        if rc != 0 or "2 game functions that can hold a paused frame, 0 differ" not in out:
            bad.append("the same code: exit %d: %s" % (rc, out))
        # gYield's ldr x0, [x9] -> [x9, #8]: a real change, its file unchanged
        changed = library(change=("gYield", 6, 0xF9400520))
        rc, out = gate(tmp, root, changed)
        if rc == 0 or "FAIL gYield" not in out or files["act_bird.c"] not in out or "code_changes.txt" not in out:
            bad.append("a changed yielding function, its file unchanged: exit %d: %s" % (rc, out))
        rc, out = gate(tmp, root, changed, [files["act_bird.c"]])
        if rc != 0 or "changed with its source %s: gYield" % files["act_bird.c"] not in out:
            bad.append("the same, its file changed: exit %d: %s" % (rc, out))
        new = [h for h in state_legacy_table.unit_hashes(syms(), [l for l in syms().splitlines()
                                                                   if l.startswith("section ")], changed, 3)
               if h[0] == 0x1ff0][0][2]
        write(os.path.join(root, "code_changes.txt"), "# made on purpose\nchanged v9.9.9-android gYield %016x\n" % new)
        rc, out = gate(tmp, root, changed)
        if rc != 0 or "recorded in code_changes.txt: gYield" not in out:
            bad.append("the same, recorded: exit %d: %s" % (rc, out))
        write(os.path.join(root, "code_changes.txt"), "changed v9.9.9-android gYield %016x\n" % (new ^ 1))
        rc, out = gate(tmp, root, changed)
        if rc == 0 or "FAIL gYield" not in out:
            bad.append("the same, recorded with another hash: exit %d: %s" % (rc, out))
        os.remove(os.path.join(root, "code_changes.txt"))
        # the indirect caller is gated, the leaf is not
        rc, out = gate(tmp, root, library(change=("gIndirect", 3, 0xF9400820)))
        if rc == 0 or "FAIL gIndirect" not in out:
            bad.append("a changed indirect caller: exit %d: %s" % (rc, out))
        rc, out = gate(tmp, root, library(change=("gLeaf", 0, 0xF9400820)))
        if rc != 0:
            bad.append("a changed function that can hold no frame: exit %d: %s" % (rc, out))
        # the linker's patch site: rules 3 hash it as the load (the gate passes), rules 2 do not
        patched = library(patch=True)
        rc, out = gate(tmp, root, patched)
        if rc != 0 or "0 differ" not in out:
            bad.append("the new library with an erratum patch site: exit %d: %s" % (rc, out))
        t_rel = code_hash_a64.Text(rel[TEXT_LO:TEXT_HI], TEXT_LO)
        t_new = code_hash_a64.Text(patched[TEXT_LO:TEXT_HI], TEXT_LO)
        r2 = [code_hash_a64.code_hash(lib[0x1ff0:0x2014], 0x1ff0, 2, t) for lib, t in ((rel, t_rel), (patched, t_new))]
        r3 = [code_hash_a64.code_hash(lib[0x1ff0:0x2014], 0x1ff0, 3, t) for lib, t in ((rel, t_rel), (patched, t_new))]
        if r2[0] == r2[1] or r3[0] != r3[1]:
            bad.append("the patch site: rules 2 %s, rules 3 %s (want apart, alike)" % (r2, r3))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for b in bad:
        print("state_legacy_gate_test: FAIL " + b)
    print("state_legacy_gate_test: %s" % ("ok" if not bad else "%d failures" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
