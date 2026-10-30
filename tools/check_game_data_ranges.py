#!/usr/bin/env python3
"""check_game_data_ranges.py: the game's .data/.bss ranges in a linked program.

Save states copy and restore the game's writable globals as two address
ranges (port/platform/game_data_range.h), which the build makes by putting
every game object's .data and .bss in sections of their own
(cmake/IcoStateSections.cmake). This reads the program's symbol table with
the toolchain's nm and checks, without running it, that the ranges hold the
game's globals (static ones too, which the program's own start-up check
cannot name) and none of the port's:

    check_game_data_ranges.py --nm NM PROGRAM
    check_game_data_ranges.py --selftest

The ranges are read from the linker's symbols: ELF __start_/__stop_ico_gdata
and __start_/__stop_ico_gbss; PE the brackets gdata_bracket_a, _n and _z
(port/platform/game_data_range.c): data [a, n), bss [n, z). A symbol is
checked by its address (nm gives no sizes on PE). Exit 0 when every check
passes, 1 when one fails (each failure named), 2 for a bad call.
"""
import subprocess
import sys

# The game's (ico2/, port/data/gen/table_defs.c): inside one of the ranges.
# texTable and cdvdThreadStack are file statics, which only this check sees.
GAME = [
    "systemStatus",  # ico2/common/src/main.c, initialised
    "stage_no",
    "stagePreLoadBuff",  # ico2/common/src/StageManager.c, zero-filled
    "gameSysMemoryFuncList",  # ico2/common/src/gamesys.c, function pointers
    "texTable",  # ico2/seki/src/Texture.c, static
    "objLayout",  # port/data/gen/table_defs.c
    "cdvdThreadStack",  # ico2/common/src/main.c, static
    "gFlagGameClear",  # ico2/script/src/gflag.c
    "gameParam",  # port/data/gen/table_defs.c
    "ico_scratchpad",  # ico2/seki/src/Basic.c
]
# The port's: outside both.
PORT = [
    "ico_iop_ram",  # port/data/iop_ram.c
    "ico_hw_eeio",  # port/platform/hwregs.c
    "ico_mv_active",  # port/game/options.c
    "ico_region",  # port/core
]

ELF_BOUNDS = ("__start_ico_gdata", "__stop_ico_gdata", "__start_ico_gbss", "__stop_ico_gbss")
PE_BOUNDS = ("gdata_bracket_a", "gdata_bracket_n", "gdata_bracket_z")


def parse_nm(text):
    """name -> list of addresses (a static name can occur more than once)."""
    syms = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, kind, name = parts
        if kind in ("U", "w", "v"):
            continue
        try:
            a = int(addr, 16)
        except ValueError:
            continue
        syms.setdefault(name, []).append(a)
    return syms


def ranges_of(syms):
    """[(lo, hi) data, (lo, hi) bss], the scheme's name; or (None, why)."""

    def one(name):
        v = syms.get(name)
        return v[0] if v and len(v) == 1 else None

    if all(one(n) is not None for n in ELF_BOUNDS):
        b = [one(n) for n in ELF_BOUNDS]
        r = [(b[0], b[1]), (b[2], b[3])]
        scheme = "ELF"
    elif all(one(n) is not None for n in PE_BOUNDS):
        b = [one(n) for n in PE_BOUNDS]
        r = [(b[0], b[1]), (b[1], b[2])]
        scheme = "PE"
    else:
        return None, "no game data sections in this program (no __start_ico_gdata or gdata_bracket_a symbols)"
    if not (r[0][0] < r[0][1] and r[1][0] <= r[1][1]):
        return None, "%s ranges out of order: data %#x..%#x, bss %#x..%#x" % (
            scheme, r[0][0], r[0][1], r[1][0], r[1][1])
    if r[1][0] < r[1][1] and r[0][0] < r[1][1] and r[1][0] < r[0][1]:
        return None, "%s ranges overlap" % scheme
    return (r, scheme), ""


def check(syms, game=GAME, port=PORT):
    """The failures, [] when none."""
    got, why = ranges_of(syms)
    if got is None:
        return [why]
    r, _ = got
    fails = []

    def inside(a):
        return any(lo <= a < hi for lo, hi in r)

    for name in game:
        addrs = syms.get(name)
        if not addrs:
            fails.append("%s: not in the symbol table" % name)
        elif not all(inside(a) for a in addrs):
            fails.append("%s is outside the game's data" % name)
    for name in port:
        addrs = syms.get(name)
        if not addrs:
            fails.append("%s: not in the symbol table" % name)
        elif any(inside(a) for a in addrs):
            fails.append("%s is inside the game's data" % name)
    return fails


def selftest():
    fails = []

    def nm(lines):
        return parse_nm("\n".join(lines))

    elf = [
        "0000000000600000 D __start_ico_gdata", "0000000000600100 D __stop_ico_gdata",
        "0000000000700000 B __start_ico_gbss", "0000000000780000 B __stop_ico_gbss",
        "0000000000600010 D g1", "0000000000700020 b g2", "0000000000600200 D p1",
        "0000000000690000 B p2", "0000000000780000 B p3", "                 U und",
    ]
    if check(nm(elf), ["g1", "g2"], ["p1", "p2", "p3"]):
        fails.append("ELF: a good table fails")
    if check(nm(elf), ["p1"], []) != ["p1 is outside the game's data"]:
        fails.append("ELF: a port symbol counted as the game's")
    if check(nm(elf), [], ["g2"]) != ["g2 is inside the game's data"]:
        fails.append("ELF: a game symbol counted as the port's")
    if check(nm(elf), ["und"], []) != ["und: not in the symbol table"]:
        fails.append("ELF: an undefined symbol counted")
    # a static name twice, one copy outside
    if not check(nm(elf + ["0000000000690010 b g2"]), ["g2"], []):
        fails.append("ELF: a second copy outside passed")
    pe = [
        "0000000140008000 d gdata_bracket_a", "0000000140008400 d gdata_bracket_n",
        "0000000140600000 d gdata_bracket_z", "0000000140008010 D g1", "0000000140008500 D g2",
        "0000000140007ff0 D p1", "0000000140600000 d p2",
    ]
    if check(nm(pe), ["g1", "g2"], ["p1", "p2"]):
        fails.append("PE: a good table fails")
    if check(nm(pe), [], ["g1"]) != ["g1 is inside the game's data"]:
        fails.append("PE: a game symbol counted as the port's")
    if not check(nm(["0000000000600010 D g1"]), ["g1"], [])[0].startswith("no game data sections"):
        fails.append("no brackets: not refused")
    swapped = ["0000000140008400 d gdata_bracket_a", "0000000140008000 d gdata_bracket_n",
               "0000000140600000 d gdata_bracket_z"]
    if "out of order" not in check(nm(swapped), [], [])[0]:
        fails.append("PE: brackets out of order not refused")
    for f in fails:
        print("check_game_data_ranges: selftest FAIL: %s" % f)
    print("check_game_data_ranges: selftest %s" % ("ok" if not fails else "%d failures" % len(fails)))
    return 1 if fails else 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if len(argv) != 3 or argv[0] != "--nm":
        print(__doc__, file=sys.stderr)
        return 2
    nm, program = argv[1], argv[2]
    try:
        out = subprocess.run([nm, program], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print("check_game_data_ranges: %s %s: %s" % (nm, program, e), file=sys.stderr)
        return 2
    syms = parse_nm(out)
    fails = check(syms)
    got, _ = ranges_of(syms)
    if got is not None:
        (d, b), scheme = got
        print("check_game_data_ranges: %s data %#x..%#x (%d bytes), bss %#x..%#x (%d bytes)" % (
            scheme, d[0], d[1], d[1] - d[0], b[0], b[1], b[1] - b[0]))
    for f in fails:
        print("check_game_data_ranges: FAIL: %s" % f)
    if not fails:
        print("check_game_data_ranges: ok (%d game, %d port symbols)" % (len(GAME), len(PORT)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
