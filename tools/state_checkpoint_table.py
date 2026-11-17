#!/usr/bin/env python3
"""tools/state_checkpoint_table.py -- where each released build kept the game's
checkpoint, for Load on a save state that cannot resume where it was saved
(port/save/state_checkpoint.h).

A save state holds the game's own save image gameSysMainSaveBuff, which the
game's gamesysMemorySave wrote at its last checkpoint, and the few words a
card load sets besides it (systemStatus, whose word 3 says a checkpoint was
made since the journey started; the slot preview IosMcPreviewInfo; the option
words optionScreenMode and girlControlMode; stage_no, which checks the place
against the state's header).  A state of version 0.11.9 or later names their
places itself (its "ckpt" section).  An older state does not: this table gives
them per released build, as offsets into the state's gdata and gbss sections,
keyed by the state header's commit text and platform tag.

  state_checkpoint_table.py build ASSET... [--out FILE]
      Reads each release asset (an Android package, a Windows or macOS zip, a
      Linux tarball, an iOS package: read as archives, nothing in them is run)
      and writes the C table (default port/save/state_checkpoint_table.c),
      each entry with the asset's name and SHA-256.  An asset whose program
      names none of the globals is listed and left out.

  state_checkpoint_table.py guard SYMBOLS
      The package's check of the program it is about to ship (an ELF program
      or library, a GNU ld map, an nm table): every global above lies inside
      the game's data ranges at its size, so the state's own record can name
      it (port/game/state_checkpoint_live.c writes the record from the
      program's own addresses; a global outside the ranges would be one no
      state holds).  Then the committed table is read back and must hold an
      entry for every release and platform of RELEASES (a table edited by
      hand that lost one fails).  Exit 0 or 1 (each failure named).

  state_checkpoint_table.py check [--table FILE]
      The second half of guard alone (ctest state_checkpoint_table).

Symbol sources per platform: the ELF's own symbol tables (Android's
libmain.so, the Linux program), the GNU ld map beside the Windows program
(x64/ico_pc_x64.map), the Mach-O symbol table of the macOS and iOS programs.
The game's ranges: ELF sections ico_gdata and ico_gbss; PE the brackets
.data$ico_a, .data$ico_n and .data$ico_z (port/platform/game_data_range.c);
Mach-O __DATA,__ico_gdata and __DATA,__ico_gbss.

The commit text a build writes into its states (port/platform/build_commit.cmake,
git describe) is read from the program itself: the release's VERSION.txt names
the full commit, and the text "<tag>-<n>-g<that commit's first digits>" (or the
bare tag, a build made after the tag) is looked for between two NUL bytes.
"""
import argparse
import hashlib
import importlib.util
import io
import os
import re
import struct
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "port", "save", "state_checkpoint_table.c")

# the globals, in port/save/state_checkpoint.h's order (ICO_CKPT_G_*), with
# their sizes (gamesys.c, main.c, mcard.c, StageManager.c)
GLOBALS = [
    ("gameSysMainSaveBuff", 25596),
    ("systemStatus", 48),
    ("IosMcPreviewInfo", 24),
    ("optionScreenMode", 4),
    ("girlControlMode", 4),
    ("stage_no", 4),
]

# every release that wrote save states (format 1 from v0.9.6, format 2 from
# v0.10.0) up to the last before the states named their own places, and the
# platforms each shipped; the macOS and iOS programs are read when their
# symbol tables name the globals (listed by build when not)
RELEASES = ["v0.9.6", "v0.9.7", "v0.10.0", "v0.10.1", "v0.10.2", "v0.10.3", "v0.10.4",
            "v0.10.5", "v0.11.0", "v0.11.1", "v0.11.2", "v0.11.3", "v0.11.4", "v0.11.5",
            "v0.11.6", "v0.11.7", "v0.11.8"]
REQUIRED_PLATFORMS = ["android-arm64", "windows-x64", "linux-x64"]


def load_recover():
    spec = importlib.util.spec_from_file_location(
        "state_recover", os.path.join(ROOT, "tools", "state_recover.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


SR = load_recover()


class Skip(Exception):
    pass


# --- the programs inside the assets ------------------------------------------------


def members(path):
    """(platform, program bytes, map text or None, VERSION.txt text or "")"""
    name = os.path.basename(path)
    if name.endswith(".apk"):
        with zipfile.ZipFile(path) as z:
            return ("android-arm64", z.read("lib/arm64-v8a/libmain.so"), None,
                    z.read("assets/VERSION.txt").decode("utf-8", "replace")
                    if "assets/VERSION.txt" in z.namelist() else "")
    if name.endswith(".ipa"):
        with zipfile.ZipFile(path) as z:
            # the program is named as its bundle (Payload/ICO.app/ICO)
            prog = [n for n in z.namelist() if re.match(r"Payload/([^/]+)\.app/\1$", n)]
            if len(prog) != 1:
                raise Skip("no one program in Payload/*.app")
            return ("ios-arm64", z.read(prog[0]), None, "")
    if name.endswith("-win.zip"):
        with zipfile.ZipFile(path) as z:
            names = z.namelist()
            exe = [n for n in names if n.endswith("/x64/ico_pc_x64.exe")]
            mp = [n for n in names if n.endswith("/x64/ico_pc_x64.map")]
            ver = [n for n in names if n.endswith("/VERSION.txt")]
            if not exe or not mp:
                raise Skip("no x64/ico_pc_x64.exe with its map")
            return ("windows-x64", z.read(exe[0]), z.read(mp[0]).decode("utf-8", "replace"),
                    z.read(ver[0]).decode("utf-8", "replace") if ver else "")
    if name.endswith(".zip") and "macos" in name:
        with zipfile.ZipFile(path) as z:
            prog = [n for n in z.namelist() if re.match(r"[^/]+\.app/Contents/MacOS/[^/]+$", n)]
            if len(prog) != 1:
                raise Skip("no one program in *.app/Contents/MacOS")
            return ("macos-arm64", z.read(prog[0]), None, "")
    if name.endswith(".tar.gz"):
        plat = "linux-arm64" if "arm64" in name else "linux-x64"
        with tarfile.open(path) as t:
            prog = mp = ver = None
            for m in t.getmembers():
                if not m.isfile():
                    continue
                base = m.name.split("/")[-1]
                if base == "ico_pc":
                    prog = t.extractfile(m).read()
                elif base == "ico_pc.map":
                    mp = t.extractfile(m).read().decode("utf-8", "replace")
                elif base == "VERSION.txt":
                    ver = t.extractfile(m).read().decode("utf-8", "replace")
            if prog is None:
                raise Skip("no ico_pc program")
            return (plat, prog, mp, ver or "")
    raise Skip("not an asset this tool reads")


# --- the game's ranges and the globals' places ----------------------------------------


def elf_sections(blob):
    (shoff,) = struct.unpack_from("<Q", blob, 40)
    (shentsize, shnum, shstrndx) = struct.unpack_from("<HHH", blob, 58)
    secs = []
    for i in range(shnum):
        (nm, stype, _fl, addr, off, size) = struct.unpack_from("<IIQQQQ", blob,
                                                               shoff + i * shentsize)
        secs.append((nm, addr, off, size))
    stroff = secs[shstrndx][2]
    out = {}
    for (nm, addr, _off, size) in secs:
        end = blob.index(b"\0", stroff + nm)
        out[blob[stroff + nm:end].decode("utf-8", "replace")] = (addr, size)
    return out


def ranges_elf(blob):
    s = elf_sections(blob)
    if "ico_gdata" not in s:
        raise Skip("the ELF has no ico_gdata section")
    gb = s.get("ico_gbss", (s["ico_gdata"][0] + s["ico_gdata"][1], 0))
    return [s["ico_gdata"], gb]


MAP_INPUT = re.compile(r"^ (\.data\$ico_[anz])\s*$|^ (\.data\$ico_[anz])\s+0x([0-9a-fA-F]+)\s+"
                       r"0x([0-9a-fA-F]+)")
MAP_ADDR = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)")


def ranges_map(text):
    """GNU ld's map: ELF output sections ico_gdata / ico_gbss, or the PE
    brackets (the first input section of each name: game_data_range.c's)"""
    out = {}
    pend = None
    for line in text.splitlines():
        m = re.match(r"^(ico_gdata|ico_gbss)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)", line)
        if m and m.group(1) not in out:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
            continue
        if pend is not None:
            a = MAP_ADDR.match(line)
            if a and pend not in out:
                out[pend] = (int(a.group(1), 16), int(a.group(2), 16))
            pend = None
            continue
        m = MAP_INPUT.match(line)
        if m:
            if m.group(1):
                pend = m.group(1)
            elif m.group(2) not in out:
                out[m.group(2)] = (int(m.group(3), 16), int(m.group(4), 16))
    if "ico_gdata" in out:
        gd = out["ico_gdata"]
        gb = out.get("ico_gbss", (gd[0] + gd[1], 0))
        return [gd, gb]
    if all(k in out for k in (".data$ico_a", ".data$ico_n", ".data$ico_z")):
        a, n, z = out[".data$ico_a"][0], out[".data$ico_n"][0], out[".data$ico_z"][0]
        return [(a, n - a), (n, z - n)]
    raise Skip("the map names neither ico_gdata nor the .data$ico_a/n/z brackets")


def macho(blob):
    """(sections {(seg, sect): (addr, size)}, symbols {name: addr}) of a
    64-bit little-endian Mach-O (the arm64 slice of a universal file)"""
    magic = struct.unpack_from(">I", blob, 0)[0]
    if magic == 0xCAFEBABE:
        (n,) = struct.unpack_from(">I", blob, 4)
        for i in range(n):
            (cpu, _sub, off, size, _al) = struct.unpack_from(">iiIII", blob, 8 + i * 20)
            if cpu == 0x0100000C:
                return macho(blob[off:off + size])
        raise Skip("no arm64 slice")
    if struct.unpack_from("<I", blob, 0)[0] != 0xFEEDFACF:
        raise Skip("not a 64-bit Mach-O program")
    (ncmds,) = struct.unpack_from("<I", blob, 16)
    at = 32
    secs = {}
    order = []
    symoff = nsyms = stroff = 0
    for _ in range(ncmds):
        (cmd, size) = struct.unpack_from("<II", blob, at)
        if cmd == 0x19:  # LC_SEGMENT_64
            (nsects,) = struct.unpack_from("<I", blob, at + 64)
            for k in range(nsects):
                s = at + 72 + k * 80
                sect = blob[s:s + 16].split(b"\0")[0].decode()
                seg = blob[s + 16:s + 32].split(b"\0")[0].decode()
                (addr, ssize) = struct.unpack_from("<QQ", blob, s + 32)
                secs[(seg, sect)] = (addr, ssize)
                order.append((seg, sect))
        elif cmd == 0x2:  # LC_SYMTAB
            (symoff, nsyms, stroff, _strsize) = struct.unpack_from("<IIII", blob, at + 8)
        at += size
    syms = {}
    for i in range(nsyms):
        (strx, ntype, nsect, _desc, value) = struct.unpack_from("<IBBHQ", blob, symoff + i * 16)
        if (ntype & 0xE0) != 0 or (ntype & 0x0E) != 0x0E or nsect == 0:
            continue  # stabs, or not defined in a section
        end = blob.index(b"\0", stroff + strx)
        name = blob[stroff + strx:end].decode("utf-8", "replace")
        if name.startswith("_"):
            syms.setdefault(name[1:], value)
    return secs, syms


def places_macho(blob):
    secs, syms = macho(blob)
    if ("__DATA", "__ico_gdata") not in secs:
        raise Skip("the program has no __DATA,__ico_gdata section")
    gd = secs[("__DATA", "__ico_gdata")]
    gb = secs.get(("__DATA", "__ico_gbss"), (gd[0] + gd[1], 0))
    starts = sorted(set(syms.values()))
    found = {}
    for name, size in GLOBALS:
        if name not in syms:
            continue
        a = syms[name]
        # the next symbol bounds it (Mach-O symbols carry no size)
        k = starts.index(a)
        if k + 1 < len(starts) and starts[k + 1] - a < size:
            raise Skip("%s has %d bytes before the next symbol, %d wanted"
                       % (name, starts[k + 1] - a, size))
        found[name] = (a, size)
    return [gd, gb], found


def places_table(table, rng):
    found = {}
    for name, size in GLOBALS:
        ent = table.syms.get(name)
        if ent is None:
            continue
        addr, have = ent[0], ent[1]
        bound = ent[2] if len(ent) > 2 else None
        if have is not None and have != size:
            raise Skip("%s is %d bytes, %d wanted" % (name, have, size))
        if have is None and bound is not None and bound - addr < size:
            raise Skip("%s has %d bytes before the next symbol, %d wanted"
                       % (name, bound - addr, size))
        found[name] = (addr, size)
    return found


def in_range(rng, addr, size):
    for k, (lo, n) in enumerate(rng):
        if lo <= addr and addr + size <= lo + n:
            return k, addr - lo
    return None


def commit_text(blob, version_txt, tag):
    """the commit text the build writes into its states' headers"""
    m = re.search(r"^commit ([0-9a-f]{40})$", version_txt, re.M)
    if m:
        full = m.group(1)
        pat = re.compile(rb"\0(v\d+\.\d+\.\d+(?:-rc\d+)?-\d+-g" + full[:7].encode() +
                         rb"[0-9a-f]{0,33}(?:-dirty)?)\0")
        hits = sorted(set(x.group(1).decode() for x in pat.finditer(blob)))
        hits = [h for h in hits if full.startswith(h.split("-g")[-1].split("-")[0])]
        if len(hits) == 1:
            return hits[0], "the program's own text (VERSION.txt's commit)"
    # a build made after its tag: describe gives the bare tag
    pat = re.compile(rb"\0(v\d+\.\d+\.\d+-\d+-g[0-9a-f]{7,40}(?:-dirty)?)\0")
    hits = sorted(set(x.group(1).decode() for x in pat.finditer(blob)))
    if len(hits) == 1:
        return hits[0], "the program's one describe text"
    if (b"\0" + tag.encode() + b"\0") in blob and not hits:
        return tag, "the bare tag (a build made at the tag)"
    return "", "not found (%d candidates)" % len(hits)


def read_asset(path):
    tag = re.search(r"ico-pc-(v\d+\.\d+\.\d+)", os.path.basename(path))
    tag = tag.group(1) if tag else ""
    with open(path, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    plat, prog, mp, ver = members(path)
    if prog[:4] == b"\x7fELF":
        rng = ranges_elf(prog)
        found = places_table(SR.read_elf(prog, path), rng)
        if mp is not None and not found:
            found = places_table(SR.read_map(mp, path), rng)
    elif mp is not None:
        rng = ranges_map(mp)
        found = places_table(SR.read_map(mp, path), rng)
    else:
        rng, found = places_macho(prog)
    if "gameSysMainSaveBuff" not in found:
        raise Skip("its symbols do not name gameSysMainSaveBuff")
    places = []
    for name, size in GLOBALS:
        if name not in found:
            places.append((-1, 0, 0))
            continue
        where = in_range(rng, found[name][0], size)
        if where is None:
            raise Skip("%s at %#x is outside the game's ranges" % (name, found[name][0]))
        places.append((where[0], where[1], size))
    commit, how = commit_text(prog, ver, tag)
    return {"tag": tag, "platform": plat, "commit": commit, "commit_how": how,
            "gdata": rng[0][1], "gbss": rng[1][1], "places": places,
            "asset": os.path.basename(path), "sha256": sha}


# --- the C table ---------------------------------------------------------------


def c_table(entries, skipped):
    out = ["/*", " * port/save/state_checkpoint_table.c (generated by "
           "tools/state_checkpoint_table.py build; do not edit)", " *",
           " * Where each released build kept the game's checkpoint "
           "(state_checkpoint.h): per",
           " * build (the state header's commit text and platform), its game ranges' sizes and",
           " * each global's range (0 gdata, 1 gbss, -1 not in the program) and offset, read",
           " * from the release asset named with its SHA-256.", " *"]
    for s in skipped:
        out.append(" * not in the table: %s" % s)
    out += [" */", '#include "state_checkpoint.h"', "",
            "const IcoCkptLayout ico_ckpt_table[] = {"]
    for e in entries:
        pl = ", ".join("{%d, 0x%xu, %du}" % p for p in e["places"])
        out.append('    {"%s", "%s", "%s", 0x%xu, 0x%xu,' % (e["tag"], e["commit"], e["platform"],
                                                           e["gdata"], e["gbss"]))
        out.append("     {%s}," % pl)
        out.append('     "%s",' % e["asset"])
        out.append('     "%s"},' % e["sha256"])
    out += ["};", "", "const int ico_ckpt_table_count = (int)(sizeof ico_ckpt_table / "
            "sizeof ico_ckpt_table[0]);", ""]
    return "\n".join(out)


# an entry's head as C text, whatever its line breaks (tools/format.sh lays the
# generated table out again: one field a line)
ENTRY_RE = re.compile(r'\{\s*"(v[^"]*)",\s*"([^"]*)",\s*"([a-z0-9-]+)",\s*0x([0-9a-f]+)u,\s*'
                      r'0x([0-9a-f]+)u,')
SHA_RE = re.compile(r'"[0-9a-f]{64}"')


def read_table(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    entries = [(m.group(1), m.group(2), m.group(3)) for m in ENTRY_RE.finditer(text)]
    # every entry carries one SHA-256: a count that differs is an entry this
    # reader did not see (a layout it does not know), never a smaller table
    shas = len(SHA_RE.findall(text))
    if shas != len(entries):
        raise SystemExit("state_checkpoint_table: %s: %d entries read but %d SHA-256 texts: the "
                         "reader does not know this file's layout" % (path, len(entries), shas))
    return entries


def check_table(path):
    bad = []
    have = read_table(path)
    keys = {(t, p) for (t, _c, p) in have}
    for t in RELEASES:
        for p in REQUIRED_PLATFORMS:
            if (t, p) not in keys:
                bad.append("the table has no %s %s entry" % (t, p))
    for (t, c, p) in have:
        if not c:
            print("state_checkpoint_table: note: %s %s has no commit text (its layout still "
                  "serves a state its bytes confirm)" % (t, p))
    print("state_checkpoint_table: %s: %d entries, %d releases x %d platforms required"
          % (os.path.relpath(path, ROOT), len(have), len(RELEASES), len(REQUIRED_PLATFORMS)))
    return bad


def guard(symbols):
    bad = []
    with open(symbols, "rb") as f:
        blob = f.read()
    try:
        if blob[:4] == b"\x7fELF":
            rng = ranges_elf(blob)
            found = places_table(SR.read_elf(blob, symbols), rng)
        else:
            text = (SR.gzip.decompress(blob) if blob[:2] == b"\x1f\x8b" else blob).decode(
                "utf-8", "replace")
            if "Linker script and memory map" in text:
                rng = ranges_map(text)
                found = places_table(SR.read_map(text, symbols), rng)
            else:
                t = SR.read_nm(text, symbols)
                lo = {k: v for k, v in t.anchor.items()}
                ends = {}
                for line in text.splitlines():
                    f = line.split()
                    if len(f) >= 3 and f[-1] in ("__stop_ico_gdata", "__stop_ico_gbss"):
                        ends[f[-1][11:]] = int(f[0], 16)
                if "gdata" not in lo or "gdata" not in ends:
                    raise Skip("the nm table names no __start_/__stop_ico_gdata")
                rng = [(lo["gdata"], ends["gdata"] - lo["gdata"]),
                       (lo.get("gbss", ends["gdata"]),
                        ends.get("gbss", ends["gdata"]) - lo.get("gbss", ends["gdata"]))]
                found = places_table(t, rng)
    except Skip as e:
        return ["%s: %s" % (symbols, e)]
    for name, size in GLOBALS:
        if name not in found:
            bad.append("%s names no %s" % (symbols, name))
            continue
        if in_range(rng, found[name][0], size) is None:
            bad.append("%s: %s (%d bytes at %#x) is outside the game's data ranges"
                       % (symbols, name, size, found[name][0]))
    if not bad:
        print("state_checkpoint_table: %s: the %d checkpoint globals lie in the game's data "
              "ranges at their sizes" % (symbols, len(GLOBALS)))
    return bad


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("assets", nargs="+")
    b.add_argument("--out", default=TABLE)
    g = sub.add_parser("guard")
    g.add_argument("symbols")
    g.add_argument("--table", default=TABLE)
    c = sub.add_parser("check")
    c.add_argument("--table", default=TABLE)
    a = ap.parse_args(argv)
    if a.cmd == "build":
        entries, skipped = [], []
        for path in a.assets:
            try:
                e = read_asset(path)
            except (Skip, KeyError, zipfile.BadZipFile, tarfile.TarError, struct.error,
                    ValueError, SR.Refused) as ex:
                skipped.append("%s (%s)" % (os.path.basename(path), ex))
                print("state_checkpoint_table: %s: left out: %s" % (path, ex))
                continue
            print("state_checkpoint_table: %s: %s %s commit %r (%s), gdata %#x gbss %#x, image "
                  "%s" % (e["asset"], e["tag"], e["platform"], e["commit"], e["commit_how"],
                          e["gdata"], e["gbss"], e["places"][0]))
            entries.append(e)
        order = {t: i for i, t in enumerate(RELEASES)}
        entries.sort(key=lambda e: (order.get(e["tag"], len(order)), e["platform"]))
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(c_table(entries, skipped))
        fmt = os.path.join(ROOT, "tools", "format.sh")
        if os.path.abspath(a.out).startswith(os.path.join(ROOT, "port") + os.sep) and \
                os.path.exists(fmt):
            import subprocess
            subprocess.run([fmt, a.out], check=True)
        print("state_checkpoint_table: wrote %s (%d entries, %d left out)"
              % (a.out, len(entries), len(skipped)))
        return 0
    bad = []
    if a.cmd == "guard":
        bad += guard(a.symbols)
    bad += check_table(a.table)
    for x in bad:
        print("state_checkpoint_table: FAIL: %s" % x)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
