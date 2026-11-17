"""tools/test/android_syms_layout_test.py -- tools/android_syms_lines.py layout, the
packaging gate on the game data layout hash (port/state_legacy/layout.txt): a guard
report with the recorded hash passes, one with another hash (v0.11.9-rc4's
23ff42f4dbb93d76: the reserved room left out of the link) fails naming both
hashes and the checkpoint path, a report without the line fails. The reports are
written under $TMPDIR; the recorded hash is the repository's."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "android_syms_lines.py")


def recorded():
    for line in open(os.path.join(ROOT, "port", "state_legacy", "layout.txt")):
        t = line.split()
        if len(t) == 2 and t[0] == "layout":
            return t[1]
    sys.exit("android_syms_layout_test: port/state_legacy/layout.txt records no hash")


def run(report):
    fd, path = tempfile.mkstemp(suffix="-carry.txt")
    with os.fdopen(fd, "w") as f:
        f.write(report)
    try:
        p = subprocess.run([sys.executable, "-I", TOOL, "layout", path, ROOT],
                           capture_output=True, text=True)
    finally:
        os.remove(path)
    return p.returncode, p.stdout + p.stderr


def main():
    want = recorded()
    line = "  symbols units 14229 data 7185 layout %s sections 27 code e10f80 13c1810\n"
    bad = []
    rc, out = run("state_carry_check: x\n" + line % want)
    if rc != 0 or want not in out:
        bad.append("the recorded hash: exit %d: %s" % (rc, out))
    rc, out = run("state_carry_check: x\n" + line % "23ff42f4dbb93d76")
    if rc == 0 or "23ff42f4dbb93d76" not in out or want not in out or "checkpoint" not in out:
        bad.append("another hash: exit %d: %s" % (rc, out))
    rc, out = run("state_carry_check: x\n")
    if rc == 0 or "no layout hash" not in out:
        bad.append("no layout line: exit %d: %s" % (rc, out))
    for b in bad:
        print("android_syms_layout_test: FAIL " + b)
    print("android_syms_layout_test: %s" % ("ok" if not bad else "%d failed" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
