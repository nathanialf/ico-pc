#!/usr/bin/env python3
"""tools/state_legacy_table.py -- the legacy tables that let this build read save
states an earlier Android version saved (format 2: port/platform/state_legacy.h).

  state_legacy_table.py build --apk APK --commit TEXT --name NAME [--kind release|rc]
                              [--out port/state_legacy]
      Writes port/state_legacy/NAME/ from a released APK (run by a maintainer;
      the outputs are committed): the APK's own assets/libmain.syms.gz as it is
      (syms.gzip: not .gz, which Android's asset packaging un-gzips and renames), its library's allocated sections from its section headers
      (sections.txt: the symbol file's "section" lines, which versions before
      v0.11.9 did not write), the text runs of its .rodata (every run of one or
      more C string literals, each its non-control bytes and a NUL: what a
      state's pointers into its read-only data can hold) as an index
      (texts.idx: per run its offset from the image start and its length,
      little-endian 32-bit pairs, raw deflate) and the runs' bytes in order
      (texts.<k>.z, raw deflate, each under the repository's 256 KiB file cap),
      every function's code hash under rules 1 and the current rules (hashes.z: per function,
      as port/platform/state_syms.c cuts the code into units, its offset from the image start
      and the two hashes, 32 + 64 + 64 bits little-endian, raw deflate; a state's own rules-1
      hashes are checked against the first at load, which proves the table is its build's; tools/code_hash_a64.py hashes
      as port/platform/code_hash.c does),
      and manifest.txt: the commit text a state of that version carries in
      its header (checked against the library's bytes), the platform, the
      kind (release, or rc: a release candidate's table, packaged into
      release-candidate APKs only), the APK's file name and SHA-256, and
      every file's size and SHA-256.  Then rewrites port/state_legacy/index.txt.
      Needs the NDK's llvm-readelf (tools/toolchain/android.env).

  state_legacy_table.py check [--root port/state_legacy]
      Exit 1 unless every table's files match its manifest (sizes, SHA-256),
      the texts inflate to the index's total, the runs lie in .rodata's
      section line and do not overlap, the symbol file inflates and names
      ico_diag_init, and index.txt lists exactly the tables (the
      state_legacy_tables test).

  state_legacy_table.py apkcheck APK [--rc]
      Every file the game asks for (manifest.txt and the files each manifest lists)
      under assets/state_legacy/<table>/ by that exact name and with the repository's
      bytes (Android's packaging un-gzips a .gz asset and drops the extension:
      v0.11.9-rc5 shipped "syms" for "syms.gz"); the release tables, an rc's only
      with --rc; the APK's index lists exactly those.
"""
import argparse
import gzip
import hashlib
import io
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import code_hash_a64  # noqa: E402

HASH_RULES = 2  # port/platform/code_hash.h ICO_CODE_HASH_RULES

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAP = 250000  # bytes per compressed text file (the repository's cap is 262144)
TEXT_RUN = re.compile(rb"(?:[\t\n\r\x1b\x20-\xff]+\x00)+")


def readelf():
    env = {}
    with open(os.path.join(ROOT, "tools/toolchain/android.env")) as f:
        for line in f:
            m = re.match(r'(\w+)="(.*)"', line.strip())
            if m:
                env[m.group(1)] = m.group(2)
    p = os.path.join(env["ANDROID_NDK_ROOT"], "toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf")
    if not os.access(p, os.X_OK):
        sys.exit("state_legacy_table: no llvm-readelf at " + p)
    return p


def sections(re_path, so_path):
    out = subprocess.run([re_path, "-S", "-W", so_path], capture_output=True, text=True,
                         check=True).stdout
    lines, ro = [], None
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(.*)", line)
        if not m:
            continue
        t = m.group(1).split()
        if len(t) < 7 or "A" not in t[6] or "T" in t[6]:
            continue
        addr, off, size = int(t[2], 16), int(t[3], 16), int(t[4], 16)
        if size == 0:
            continue
        lines.append("section %016x %06x %s %s" % (addr, size, t[6], t[0]))
        if t[0] == ".rodata":
            ro = (addr, off, size)
    if ro is None:
        sys.exit("state_legacy_table: the library has no .rodata")
    return lines, ro


def unit_hashes(syms_text, sec_lines, so):
    """state_syms.c build_units for an nm table with section lines: the code symbols inside the
    code sections' span, sorted by address (the file's first at one address), each up to the
    next or the span's end; and each one's code hash"""
    code = [l.split() for l in sec_lines if "X" in l.split()[3]]
    lo = min(int(t[1], 16) for t in code)
    hi = max(int(t[1], 16) + int(t[2], 16) for t in code)
    items, seen = [], set()
    for seq, line in enumerate(syms_text.splitlines()):
        t = line.split()
        if len(t) == 3 and len(t[1]) == 1 and t[1] in "TtWwDdBbRrVvSsGg":
            a = int(t[0], 16)
            if a and lo <= a < hi:
                items.append((a, seq))
        elif len(t) in (4, 5) and t[0] != "section" and len(t[2]) == 1 and t[2] in "TtWwDdBbRrVvSsGg":
            a = int(t[0], 16)
            if a and lo <= a < hi:
                items.append((a, seq))
    items.sort()
    starts = []
    for a, _ in items:
        if a not in seen:
            seen.add(a)
            starts.append(a)
    out = []
    for i, a in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else hi
        if e > a:
            out.append((a, code_hash_a64.code_hash(so[a:e], a, 1), code_hash_a64.code_hash(so[a:e], a, HASH_RULES)))
    return out


def deflate(b):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(b) + c.flush()


def inflate(b):
    return zlib.decompress(b, -15)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def write_index(root):
    names = sorted(d for d in os.listdir(root) if os.path.isfile(os.path.join(root, d, "manifest.txt")))
    rows = []
    for d in names:
        m = manifest(os.path.join(root, d))
        rows.append("%s %s %s %s" % (m["platform"], m["kind"], d, m["commit"]))
    with open(os.path.join(root, "index.txt"), "w") as f:
        f.write("# platform kind table commit (tools/state_legacy_table.py writes it)\n")
        f.write("".join(r + "\n" for r in rows))


def manifest(d):
    m = {"files": {}}
    for line in open(os.path.join(d, "manifest.txt")):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition(" ")
        if k == "file":
            name, size, digest = v.split()
            m["files"][name] = (int(size), digest)
        else:
            m[k] = v
    return m


def build(a):
    apk = open(a.apk, "rb").read()
    with zipfile.ZipFile(io.BytesIO(apk)) as z:
        so = z.read("lib/arm64-v8a/libmain.so")
        syms = z.read("assets/libmain.syms.gz")
    fd, tmp = tempfile.mkstemp(suffix="-libmain.so")  # under $TMPDIR
    with os.fdopen(fd, "wb") as f:
        f.write(so)
    try:
        sec_lines, (ro_addr, ro_off, ro_size) = sections(readelf(), tmp)
    finally:
        os.remove(tmp)
    if a.commit.encode() + b"\x00" not in so:
        sys.exit("state_legacy_table: the library does not hold the commit text %r" % a.commit)
    if b" T ico_diag_init\n" not in gzip.decompress(syms):
        sys.exit("state_legacy_table: the APK's symbol file names no ico_diag_init")
    ro = so[ro_off:ro_off + ro_size]
    runs = [(m.start(), m.end()) for m in TEXT_RUN.finditer(ro) if m.end() - m.start() >= 4]
    blob = b"".join(ro[s:e] for s, e in runs)
    idx = b"".join(struct.pack("<II", ro_addr + s, e - s) for s, e in runs)
    out = os.path.join(a.out, a.name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    hashes = unit_hashes(gzip.decompress(syms).decode(), sec_lines, so)
    files = {"syms.gzip": syms, "sections.txt": ("".join(l + "\n" for l in sec_lines)).encode(),
             "texts.idx": deflate(idx),
             "hashes.z": deflate(b"".join(struct.pack("<IQQ", a, h1, h) for a, h1, h in hashes))}
    k, at, step = 0, 0, 400000
    while at < len(blob):
        n = min(step, len(blob) - at)
        z = deflate(blob[at:at + n])
        while len(z) > CAP:
            n //= 2
            z = deflate(blob[at:at + n])
        files["texts.%d.z" % k] = z
        k, at = k + 1, at + n
    for name, b in files.items():
        if len(b) > CAP + 12000:
            sys.exit("state_legacy_table: %s is %d bytes" % (name, len(b)))
        with open(os.path.join(out, name), "wb") as f:
            f.write(b)
    with open(os.path.join(out, "manifest.txt"), "w") as f:
        f.write("# tools/state_legacy_table.py build from %s\n" % os.path.basename(a.apk))
        f.write("commit %s\nplatform android-arm64\nkind %s\n" % (a.commit, a.kind))
        f.write("apk %s\napk_sha256 %s\n" % (os.path.basename(a.apk), sha(apk)))
        f.write("texts %d %d\n" % (len(runs), len(blob)))
        f.write("text_files %d\n" % k)
        f.write("hash_rules %d\nhashes %d\n" % (HASH_RULES, len(hashes)))
        for name in sorted(files):
            f.write("file %s %d %s\n" % (name, len(files[name]), sha(files[name])))
    write_index(a.out)
    print("state_legacy_table: %s: %d text runs, %d bytes; %d files, %d bytes" %
          (out, len(runs), len(blob), len(files), sum(len(b) for b in files.values())))


def check(a):
    root = a.root
    bad = []
    names = sorted(d for d in os.listdir(root) if os.path.isfile(os.path.join(root, d, "manifest.txt")))
    listed = []
    for line in open(os.path.join(root, "index.txt")):
        if line.strip() and not line.startswith("#"):
            listed.append(line.split()[2])
    if sorted(listed) != names:
        bad.append("index.txt lists %s, the tables are %s" % (listed, names))
    for d in names:
        p = os.path.join(root, d)
        m = manifest(p)
        for name, (size, digest) in m["files"].items():
            b = open(os.path.join(p, name), "rb").read()
            if len(b) != size or sha(b) != digest:
                bad.append("%s/%s does not match its manifest" % (d, name))
        gz = [f for f in m["files"] if f.endswith(".gz")]
        if gz:
            bad.append("%s: %s end in .gz, which Android's asset packaging un-gzips and renames" % (d, gz))
        extra = set(os.listdir(p)) - set(m["files"]) - {"manifest.txt"}
        if extra:
            bad.append("%s: files the manifest does not list: %s" % (d, sorted(extra)))
        try:
            nruns, nbytes = map(int, m["texts"].split())
            idx = inflate(open(os.path.join(p, "texts.idx"), "rb").read())
            runs = [struct.unpack_from("<II", idx, i) for i in range(0, len(idx), 8)]
            blob = b"".join(inflate(open(os.path.join(p, "texts.%d.z" % k), "rb").read())
                            for k in range(int(m["text_files"])))
            ro = [l.split() for l in open(os.path.join(p, "sections.txt"))
                  if l.split()[-1] == ".rodata"][0]
            lo, hi = int(ro[1], 16), int(ro[1], 16) + int(ro[2], 16)
            if len(runs) != nruns or sum(n for _, n in runs) != nbytes or len(blob) != nbytes:
                bad.append("%s: the texts do not hold what the manifest says" % d)
            prev = 0
            for a0, n in runs:
                if a0 < lo or a0 + n > hi or a0 < prev:
                    bad.append("%s: a text run outside .rodata or out of order" % d)
                    break
                prev = a0 + n
            if b" T ico_diag_init\n" not in gzip.decompress(open(os.path.join(p, "syms.gzip"), "rb").read()):
                bad.append("%s: the symbol file names no ico_diag_init" % d)
            hz = inflate(open(os.path.join(p, "hashes.z"), "rb").read())
            if len(hz) != 20 * int(m["hashes"]) or int(m["hash_rules"]) != HASH_RULES:
                bad.append("%s: the code hashes do not hold what the manifest says" % d)
            offs = [struct.unpack_from("<I", hz, i)[0] for i in range(0, len(hz), 20)]
            if any(x >= y for x, y in zip(offs, offs[1:])):
                bad.append("%s: the code hashes are not in address order" % d)
        except (OSError, KeyError, ValueError, IndexError, zlib.error) as e:
            bad.append("%s: %s" % (d, e))
    for b in bad:
        print("state_legacy_table: " + b)
    print("state_legacy_table: %d tables, %s" % (len(names), "ok" if not bad else "%d problems" % len(bad)))
    sys.exit(1 if bad else 0)


def apkcheck(a):
    """every file the game asks for (state_legacy.c: manifest.txt and the files the manifest
    lists) in the APK's assets/state_legacy/<table>/ under that exact name, with the
    repository's bytes; the release tables always, the rc tables only in an rc APK, the APK's
    index naming exactly those"""
    root = os.path.join(ROOT, "port/state_legacy")
    bad, want = [], []
    for line in open(os.path.join(root, "index.txt")):
        t = line.split()
        if len(t) == 4 and not line.startswith("#") and (t[1] != "rc" or a.rc):
            want.append(t)
    with zipfile.ZipFile(a.apk) as z:
        have = {i.filename: i for i in z.infolist()}
        idx = "assets/state_legacy/index.txt"
        if idx not in have:
            bad.append("the APK lacks %s" % idx)
        else:
            listed = sorted(l.split()[2] for l in z.read(idx).decode().splitlines()
                            if len(l.split()) == 4 and not l.startswith("#"))
            if listed != sorted(t[2] for t in want):
                bad.append("the APK's index lists %s, expected %s" % (listed, sorted(t[2] for t in want)))
        for t in want:
            m = manifest(os.path.join(root, t[2]))
            names = ["manifest.txt"] + sorted(m["files"])
            for f in names:
                path = "assets/state_legacy/%s/%s" % (t[2], f)
                src = open(os.path.join(root, t[2], f), "rb").read()
                if path not in have:
                    bad.append("the APK lacks %s (the game asks for it by this name)" % path)
                elif z.read(path) != src:
                    bad.append("the APK's %s is not the repository's bytes" % path)
        for n in have:
            if n.startswith("assets/state_legacy/") and n.split("/")[2] not in [t[2] for t in want] \
                    and n != idx:
                bad.append("the APK holds %s, which no table it lists has" % n)
    for b in bad:
        print("state_legacy_table: apkcheck: " + b)
    print("state_legacy_table: apkcheck %s: %d tables, %s" % (os.path.basename(a.apk), len(want),
                                                            "ok" if not bad else "%d problems" % len(bad)))
    sys.exit(1 if bad else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--apk", required=True)
    b.add_argument("--commit", required=True)
    b.add_argument("--name", required=True)
    b.add_argument("--kind", default="release", choices=("release", "rc"))
    b.add_argument("--out", default=os.path.join(ROOT, "port/state_legacy"))
    c = sub.add_parser("check")
    c.add_argument("--root", default=os.path.join(ROOT, "port/state_legacy"))
    k = sub.add_parser("apkcheck")
    k.add_argument("apk")
    k.add_argument("--rc", action="store_true", help="a release candidate's APK: its tables too")
    a = ap.parse_args()
    {"build": build, "check": check, "apkcheck": apkcheck}[a.cmd](a)


if __name__ == "__main__":
    main()
