"""tools/test/android_syms_layout_test.py -- tools/android_syms_lines.py layout, the
packaging gate on the game data layout hash (port/state_legacy/layout.txt): a guard
report with the recorded hash passes, one with another hash (v0.11.9-rc4's
23ff42f4dbb93d76: the reserved room left out of the link) fails naming both
hashes and the checkpoint path, a report without the line fails; the same for
the Windows program's recorded hash (layout-windows-x64), and the arm64 Linux
program's "none" passes printing the hash. tools/android_syms_lines.py gamesec:
a map of the game's objects and the reserved room passes, one with no object in
the game's sections or without the room fails. The files are written under
$TMPDIR; the recorded hashes are the repository's."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "android_syms_lines.py")


def recorded(key="layout"):
    for line in open(os.path.join(ROOT, "port", "state_legacy", "layout.txt")):
        t = line.split()
        if len(t) == 2 and t[0] == key:
            return t[1]
    sys.exit("android_syms_layout_test: port/state_legacy/layout.txt records no %s hash" % key)


def run(report, *extra, verb="layout", suffix="-carry.txt"):
    fd, path = tempfile.mkstemp(suffix=suffix)
    with os.fdopen(fd, "w") as f:
        f.write(report)
    try:
        p = subprocess.run([sys.executable, "-I", TOOL, verb, path, ROOT] + list(extra),
                           capture_output=True, text=True)
    finally:
        os.remove(path)
    return p.returncode, p.stdout + p.stderr


# GNU ld's map lines for the game's data sections (" <section> 0xADDR 0xSIZE <object>")
GAME_OBJ = " ico_gdata      0x0000000000a00000      0x1200 port/libico_game.a(layout_action.c.o)\n"
ROOM_OBJ = " ico_gbss       0x0000000000b00000       0x600 port/CMakeFiles/ico_data.dir/data/reserved/tables.c.o\n"


def gamesec_cases(bad):
    rc, out = run(GAME_OBJ + ROOM_OBJ, verb="gamesec", suffix=".map")
    if rc != 0:
        bad.append("gamesec, the game's objects and the room: exit %d: %s" % (rc, out))
    rc, out = run("Linker script and memory map\n .text 0x1000 0x20 main.o\n", verb="gamesec",
                  suffix=".map")
    if rc == 0 or "no object" not in out:
        bad.append("gamesec, no object in the game's sections: exit %d: %s" % (rc, out))
    rc, out = run(GAME_OBJ, verb="gamesec", suffix=".map")
    if rc == 0 or "reserved room" not in out:
        bad.append("gamesec, without the room: exit %d: %s" % (rc, out))


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
    # the Windows program's recorded hash, and another
    win = recorded("layout-windows-x64")
    rc, out = run("state_carry_check: x\n" + line % win, "windows-x64")
    if rc != 0 or win not in out:
        bad.append("the Windows hash: exit %d: %s" % (rc, out))
    rc, out = run("state_carry_check: x\n" + line % want, "windows-x64")
    if want != win and (rc == 0 or "checkpoint" not in out):
        bad.append("another Windows hash: exit %d: %s" % (rc, out))
    # the arm64 Linux program: none recorded, the hash printed
    if recorded("layout-linux-arm64") == "none":
        rc, out = run("state_carry_check: x\n" + line % "6cdc283877a6e39f", "linux-arm64")
        if rc != 0 or "6cdc283877a6e39f" not in out or "record it" not in out:
            bad.append("the arm64 Linux none: exit %d: %s" % (rc, out))
    gamesec_cases(bad)
    for b in bad:
        print("android_syms_layout_test: FAIL " + b)
    print("android_syms_layout_test: %s" % ("ok" if not bad else "%d failed" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
