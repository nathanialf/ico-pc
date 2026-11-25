#!/usr/bin/env python3
"""tools/state_legacy_table.py -- the legacy tables that let this build read save
states an earlier Android version saved (format 2: port/platform/state_legacy.h).

  state_legacy_table.py build --apk APK --commit TEXT --name NAME [--kind release|rc]
                              [--record 2|3] [--out port/state_legacy]
      Writes port/state_legacy/NAME/ from a released APK (run by a maintainer;
      the outputs are committed): the APK's own assets/libmain.syms.gz as it is
      (syms.gzip: not .gz, which Android's asset packaging un-gzips and renames), its library's allocated sections from its section headers
      (sections.txt: the symbol file's "section" lines, which versions before
      v0.11.9 did not write), the text runs of its .rodata (every run of one or
      more C string literals, each its non-control bytes and a NUL: what a
      state's pointers into its read-only data can hold) as an index
      (texts.idx: per run its offset from the image start and its length,
      little-endian 32-bit pairs, raw deflate; the length's top bit LSTART is set
      when the run starts a literal: the byte before it is zero or it is .rodata's
      first; the manifest's "text_index 2" names this format, and the game refuses
      a table without it) and the runs' bytes in order
      (texts.<k>.z, raw deflate, each under the repository's 256 KiB file cap),
      every function's code hash under the rules its states' records hold and the current
      rules (hashes.z: per function, as port/platform/state_syms.c cuts the code into units,
      its offset from the image start and the two hashes, 32 + 64 + 64 bits little-endian, raw
      deflate; a state's own hashes are checked against the first at load, which proves the
      table is its build's; tools/code_hash_a64.py hashes as port/platform/code_hash.c does,
      reading an erratum patch site's stub from the library),
      every ADRP's verdict (carry.z: per function in hashes.z's order, its ADRPs' count and a
      bit each, set when a paused thread can hold the page it makes: code_hash_a64 page_carried
      on that build's own words, the C twin's ico_code_a64_page_carried; a function longer than
      16384 words is not judged),
      and manifest.txt: the commit text a state of that version carries in
      its header (checked against the library's bytes), the platform, the
      kind (release, or rc: a release candidate's table, packaged into
      release-candidate APKs only), the APK's file name and SHA-256, and
      every file's size and SHA-256.  Then rewrites port/state_legacy/index.txt.
      --record 2 (the default: versions before v0.11.9, whose records' hashes are of rules
      1) writes all of the above; --record 3 (v0.11.9 and later: a format 3 record names its
      own words, its hashes are of rules 2) writes the symbol file, the code hashes and the
      manifest alone ("record 3", "hash_verify_rules 2"), and carry.z; with --hashes-only no symbol file (the
      loader reads the code hashes alone; the gate reads the newest release table's symbol file,
      so only that one keeps it, which check enforces).
      Needs the NDK's llvm-readelf (tools/toolchain/android.env).

  state_legacy_table.py check [--root port/state_legacy]
      Exit 1 unless every table's files match its manifest (sizes, SHA-256),
      the texts inflate to the index's total, the runs lie in .rodata's
      section line and do not overlap, the index is of format 2 (text_index) with as many
      literal-start runs as the manifest's lstarts, the symbol file inflates and names
      ico_diag_init, and index.txt lists exactly the tables (the
      state_legacy_tables test).

  state_legacy_table.py apkcheck APK [--rc]
      Every file the game asks for (manifest.txt and the files each manifest lists)
      under assets/state_legacy/<table>/ by that exact name and with the repository's
      bytes (Android's packaging un-gzips a .gz asset and drops the extension:
      v0.11.9-rc5 shipped "syms" for "syms.gz"); the release tables, an rc's only
      with --rc; the APK's index lists exactly those.

  state_legacy_table.py gate --so LIB --syms SYMS.gz --map MAP (--git WT | --changed FILE)
                             [--against NAME] [--root port/state_legacy]
      The package gate on the game's code hashes (tools/package_android.sh): every game
      function (an ico2/ or ico/ file's, by the link map MAP) that can hold a paused frame
      (it calls ico_fiber_yield or makes an indirect call, directly or through the functions
      it calls by BL or B; LIB's own code) and whose name is unique in both, hashed under the
      current rules from LIB, against the newest release table's hash (or NAME's).  Exit 1,
      naming them, when one differs while its source file did not change since that release
      (git diff of the table's commit against HEAD in WT, or the file list FILE) and
      port/state_legacy/code_changes.txt records no such change: a state of that release
      saved inside it would then go to its checkpoint for a change of the build, not of the
      game (v0.11.16-rc8: subBirdBrainMain, a linker patch site).
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

HASH_RULES = 3  # port/platform/code_hash.h ICO_CODE_HASH_RULES
RECORD_RULES = {2: 1, 3: 2}  # a record format's code hash rules in the versions a table is made for

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CAP = 250000  # bytes per compressed text file (the repository's cap is 262144)
TEXT_INDEX = 2  # texts.idx's format (port/platform/state_legacy.h ICO_LEGACY_TEXT_INDEX)
LSTART = 0x80000000  # a run length's top bit: the run starts a literal (state_legacy.h)
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


def load_map(so):
    """the library's LOAD segments: (vaddr, file offset, file size)"""
    phoff, = struct.unpack_from("<Q", so, 0x20)
    phn, = struct.unpack_from("<H", so, 0x38)
    out = []
    for i in range(phn):
        t, _, off, va, _, fs, _, _ = struct.unpack_from("<IIQQQQQQ", so, phoff + 56 * i)
        if t == 1:
            out.append((va, off, fs))
    return out


def va_bytes(so, loads, a, e):
    """the library's bytes of addresses [a, e) (b"" when no LOAD segment holds them whole)"""
    for va, off, fs in loads:
        if va <= a and e <= va + fs:
            return so[off + a - va:off + e - va]
    return b""


def code_range(sec_lines):
    code = [l.split() for l in sec_lines if "X" in l.split()[3]]
    return (min(int(t[1], 16) for t in code), max(int(t[1], 16) + int(t[2], 16) for t in code))


def unit_starts(syms_text, lo, hi):
    """state_syms.c build_units for an nm table with section lines: the code symbols inside the
    code sections' span, sorted by address (the file's first at one address), each up to the
    next or the span's end: [(start, end, name)]"""
    items, seen = [], set()
    for seq, line in enumerate(syms_text.splitlines()):
        t = line.split()
        if len(t) == 3 and len(t[1]) == 1 and t[1] in "TtWwDdBbRrVvSsGg":
            a = int(t[0], 16)
            if a and lo <= a < hi:
                items.append((a, seq, t[2]))
        elif len(t) in (4, 5) and t[0] != "section" and len(t[2]) == 1 and t[2] in "TtWwDdBbRrVvSsGg":
            a = int(t[0], 16)
            if a and lo <= a < hi:
                items.append((a, seq, t[3]))
    items.sort()
    starts = []
    for a, _, nm in items:
        if a not in seen:
            seen.add(a)
            starts.append((a, nm))
    out = []
    for i, (a, nm) in enumerate(starts):
        e = starts[i + 1][0] if i + 1 < len(starts) else hi
        if e > a:
            out.append((a, e, nm))
    return out


def unit_hashes(syms_text, sec_lines, so, verify_rules=1, names=None):
    """every unit's (start, hash under verify_rules, hash under HASH_RULES); names: only those
    units (by name), for the gate"""
    lo, hi = code_range(sec_lines)
    loads = load_map(so)
    text = code_hash_a64.Text(va_bytes(so, loads, lo, hi), lo, hi)
    out = []
    for a, e, nm in unit_starts(syms_text, lo, hi):
        if names is not None and nm not in names:
            continue
        c = va_bytes(so, loads, a, e)
        out.append((a, code_hash_a64.code_hash(c, a, verify_rules, text),
                    code_hash_a64.code_hash(c, a, HASH_RULES, text)))
    return out


CARRY_MAX_WORDS = 16384  # a longer unit (the code after the last symbol: the PLT) is not judged
UNJUDGED = 0xFFFF         # state_legacy.h ICO_LEGACY_UNJUDGED


def carry_rows(syms_text, sec_lines, so):
    """carry.z's bytes before deflate (state_legacy.h): per unit, in hashes.z's order, its ADRPs'
    count (16 bits) and one bit each, set when a paused thread can hold its page (code_hash_a64
    page_carried on this build's own words, patch sites replaced)"""
    lo, hi = code_range(sec_lines)
    loads = load_map(so)
    text = code_hash_a64.Text(va_bytes(so, loads, lo, hi), lo, hi)
    out = bytearray()
    for a, e, nm in unit_starts(syms_text, lo, hi):
        c = va_bytes(so, loads, a, e)
        ws = [code_hash_a64.rd32(c, 4 * k) for k in range(len(c) // 4)]
        ks = [k for k, w in enumerate(ws) if w & 0x9F000000 == 0x90000000]
        if len(ws) > CARRY_MAX_WORDS or len(ks) >= UNJUDGED:
            out += struct.pack("<H", UNJUDGED)
            continue
        ws = code_hash_a64.unpatch(ws, a, text)[0]
        bits = bytearray((len(ks) + 7) // 8)
        for i, k in enumerate(ks):
            if code_hash_a64.page_carried(ws, k):
                bits[i // 8] |= 1 << (i % 8)
        out += struct.pack("<H", len(ks)) + bits
    return bytes(out)


def carry_count(raw):
    """carry.z's rows (check)"""
    at, n = 0, 0
    while at + 2 <= len(raw):
        c, = struct.unpack_from("<H", raw, at)
        at += 2 + (0 if c == UNJUDGED else (c + 7) // 8)
        n += 1
    return n if at == len(raw) else -1


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
    vrules = RECORD_RULES[a.record]
    hashes = unit_hashes(gzip.decompress(syms).decode(), sec_lines, so, vrules)
    out = os.path.join(a.out, a.name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    hz = deflate(b"".join(struct.pack("<IQQ", a0, h1, h) for a0, h1, h in hashes))
    cz = deflate(carry_rows(gzip.decompress(syms).decode(), sec_lines, so))
    if a.record == 3:
        files = {"hashes.z": hz, "carry.z": cz}
        if not a.hashes_only:
            files["syms.gzip"] = syms
        for name, b in files.items():
            if len(b) > CAP + 12000:
                sys.exit("state_legacy_table: %s is %d bytes" % (name, len(b)))
            with open(os.path.join(out, name), "wb") as f:
                f.write(b)
        with open(os.path.join(out, "manifest.txt"), "w") as f:
            f.write("# tools/state_legacy_table.py build --record 3 from %s\n" % os.path.basename(a.apk))
            f.write("commit %s\nplatform android-arm64\nkind %s\nrecord 3\n" % (a.commit, a.kind))
            f.write("apk %s\napk_sha256 %s\n" % (os.path.basename(a.apk), sha(apk)))
            f.write("hash_rules %d\nhash_verify_rules %d\nhashes %d\n" % (HASH_RULES, vrules, len(hashes)))
            for name in sorted(files):
                f.write("file %s %d %s\n" % (name, len(files[name]), sha(files[name])))
        write_index(a.out)
        print("state_legacy_table: %s: %d code hashes (rules %d and %d); %d files, %d bytes" %
              (out, len(hashes), vrules, HASH_RULES, len(files), sum(len(b) for b in files.values())))
        return
    ro = so[ro_off:ro_off + ro_size]
    runs = [(m.start(), m.end()) for m in TEXT_RUN.finditer(ro) if m.end() - m.start() >= 4]
    blob = b"".join(ro[s:e] for s, e in runs)
    if any(e - s >= LSTART for s, e in runs):
        sys.exit("state_legacy_table: a text run of 2 GiB or more")
    lstarts = sum(1 for s, _ in runs if s == 0 or ro[s - 1] == 0)
    idx = b"".join(struct.pack("<II", ro_addr + s, (e - s) | (LSTART if s == 0 or ro[s - 1] == 0 else 0))
                   for s, e in runs)
    files = {"syms.gzip": syms, "sections.txt": ("".join(l + "\n" for l in sec_lines)).encode(),
             "texts.idx": deflate(idx), "hashes.z": hz, "carry.z": cz}
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
        f.write("text_index %d\nlstarts %d\n" % (TEXT_INDEX, lstarts))
        f.write("text_files %d\n" % k)
        f.write("hash_rules %d\nhashes %d\n" % (HASH_RULES, len(hashes)))
        if vrules != 1:
            f.write("hash_verify_rules %d\n" % vrules)
        for name in sorted(files):
            f.write("file %s %d %s\n" % (name, len(files[name]), sha(files[name])))
    write_index(a.out)
    print("state_legacy_table: %s: %d text runs (%d start a literal), %d bytes; %d files, %d bytes" %
          (out, len(runs), lstarts, len(blob), len(files), sum(len(b) for b in files.values())))


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
            record = int(m.get("record", "2"))
            rules, vrules = int(m["hash_rules"]), int(m.get("hash_verify_rules", "1"))
            if record not in RECORD_RULES or vrules != RECORD_RULES[record]:
                bad.append("%s: record %s with hashes to check its states by of rules %d" % (d, record, vrules))
                continue
            if rules != HASH_RULES and not (2 <= rules < HASH_RULES and "hash_rules_kept" in m):
                bad.append("%s: its code hashes are of rules %d, this build's of %d (rebuild the table, or "
                           "say why it cannot be in a hash_rules_kept line)" % (d, rules, HASH_RULES))
            elif rules != HASH_RULES:
                print("state_legacy_table: %s: kept at rules %d (%s)" % (d, rules, m["hash_rules_kept"]))
            if "syms.gzip" in m["files"] and \
                    b" T ico_diag_init\n" not in gzip.decompress(open(os.path.join(p, "syms.gzip"), "rb").read()):
                bad.append("%s: the symbol file names no ico_diag_init" % d)
            hz = inflate(open(os.path.join(p, "hashes.z"), "rb").read())
            if len(hz) != 20 * int(m["hashes"]) or not hz:
                bad.append("%s: the code hashes do not hold what the manifest says" % d)
            offs = [struct.unpack_from("<I", hz, i)[0] for i in range(0, len(hz), 20)]
            if "carry.z" not in m["files"]:
                bad.append("%s: no carry.z (its ADRPs' verdicts: rebuild the table)" % d)
            elif carry_count(inflate(open(os.path.join(p, "carry.z"), "rb").read())) != len(offs):
                bad.append("%s: carry.z does not hold a row per code hash" % d)
            if any(x >= y for x, y in zip(offs, offs[1:])):
                bad.append("%s: the code hashes are not in address order" % d)
            if record == 3:
                # the loader reads hashes.z alone; the newest release's also keeps its symbol
                # file, which the package gate names the functions by
                want = ["carry.z", "hashes.z", "syms.gzip"] if d == newest_release(root) else \
                    ["carry.z", "hashes.z"]
                if sorted(m["files"]) != want:
                    bad.append("%s: a record 3 table holds %s, not %s (the newest release's keeps its "
                               "symbol file for the gate, the others drop it: build --hashes-only)" %
                               (d, ", ".join(want), sorted(m["files"])))
                continue
            nruns, nbytes = map(int, m["texts"].split())
            if int(m.get("text_index", "1")) != TEXT_INDEX:
                bad.append("%s: its texts.idx is of format %s, not %d (rebuild the table)" %
                           (d, m.get("text_index", "1"), TEXT_INDEX))
                continue
            idx = inflate(open(os.path.join(p, "texts.idx"), "rb").read())
            raw = [struct.unpack_from("<II", idx, i) for i in range(0, len(idx), 8)]
            runs = [(a0, n & ~LSTART) for a0, n in raw]
            flags = [n & LSTART != 0 for _, n in raw]
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
            if sum(flags) != int(m["lstarts"]):
                bad.append("%s: %d runs start a literal, the manifest says %s" % (d, sum(flags), m["lstarts"]))
            if runs and runs[0][0] == lo and not flags[0]:
                bad.append("%s: the run at .rodata's start starts a literal, its flag is clear" % d)
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


CHANGES = "code_changes.txt"  # port/state_legacy: the game code changes made on purpose
MAP_SECTION = re.compile(r"^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s{9}(\S.*)$")
MAP_SYMBOL = re.compile(r"^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+\d+\s{17}(\S+)$")
GAME_MEMBER = re.compile(r"\(([^()/]+\.c)\.o\):\(\.text[.$]")


def game_functions(map_path, root):
    """the game's functions by lld's link map: {name: source file} for the symbols of each code
    input section of an archive member that names one ico2/ or ico/ file of the repository (lld
    gives a member its file name alone; a name two such files share is left out)"""
    files = {}
    for d in ("ico2", "ico"):
        for dp, _, fs in os.walk(os.path.join(root, d)):
            for f in fs:
                if f.endswith(".c"):
                    files.setdefault(f, []).append(os.path.relpath(os.path.join(dp, f), root))
    out, cur = {}, None
    for line in open(map_path, errors="replace"):
        m = MAP_SECTION.match(line)
        if m:
            g = GAME_MEMBER.search(m.group(1))
            cur = files[g.group(1)][0] if g and len(files.get(g.group(1), ())) == 1 else None
            continue
        m = MAP_SYMBOL.match(line)
        if m and cur is not None and not m.group(1).startswith("$"):
            out[m.group(1)] = cur
    return out


def plt_names(so):
    """{address of a .plt entry: the dynamic symbol it calls} from the library's section headers
    (lld's PLT: a 32-byte header, then one 16-byte entry per .rela.plt entry in its order)"""
    shoff, = struct.unpack_from("<Q", so, 0x28)
    shn, shstr = struct.unpack_from("<HH", so, 0x3C)
    if shoff == 0 or shn == 0:
        return {}
    sh = [struct.unpack_from("<IIQQQQIIQQ", so, shoff + 64 * i) for i in range(shn)]
    strtab = sh[shstr][4]

    def name(off):
        return so[strtab + off:so.index(b"\0", strtab + off)].decode(errors="replace")

    by = {name(h[0]): h for h in sh}
    if not all(k in by for k in (".plt", ".rela.plt", ".dynsym", ".dynstr")):
        return {}
    plt, rela, dsym, dstr = by[".plt"], by[".rela.plt"], by[".dynsym"], by[".dynstr"]
    out = {}
    for i in range(rela[5] // 24):
        _, info, _ = struct.unpack_from("<QQq", so, rela[4] + 24 * i)
        st_name, = struct.unpack_from("<I", so, dsym[4] + 24 * (info >> 32))
        out[plt[3] + 32 + 16 * i] = so[dstr[4] + st_name:so.index(b"\0", dstr[4] + st_name)].decode(
            errors="replace")
    return out


def frame_holders(units, so, loads):
    """the units' names that can hold a paused frame: ico_fiber_yield, every unit with an
    indirect call (BLR, BLRAA...), and every unit that reaches one by BL or B (the library's own
    words: a call through the PLT by the symbol it binds, through a linker thunk by the thunk's
    target; a call through a pointer to a function that yields is the indirect call's)"""
    starts = [a for a, _, _ in units]
    import bisect
    plt = plt_names(so)
    callers, seeds = {}, set()
    for a, e, nm in units:
        c = va_bytes(so, loads, a, e)
        for k in range(len(c) // 4):
            w = code_hash_a64.rd32(c, 4 * k)
            if w & 0xFE000000 == 0xD6000000 and (w >> 21) & 3 == 1:
                seeds.add(nm)
            elif w & 0x7C000000 == 0x14000000:
                t = a + 4 * k + code_hash_a64.sext(w & 0x3FFFFFF, 26) * 4
                if a <= t < e:
                    continue
                i = bisect.bisect_right(starts, t) - 1
                if t in plt:
                    callee = plt[t]
                elif i >= 0 and units[i][0] <= t < units[i][1]:
                    callee = re.sub(r"^__AArch64(ADRP|AbsLong)Thunk_", "", units[i][2])
                else:
                    continue
                callers.setdefault(callee, set()).add(nm)
        if nm == "ico_fiber_yield":
            seeds.add(nm)
    seen, work = set(seeds), list(seeds)
    while work:
        for c in callers.get(work.pop(), ()):
            if c not in seen:
                seen.add(c)
                work.append(c)
    return seen


def recorded_changes(root, table):
    """port/state_legacy/code_changes.txt: {(table, function): hash} the maintainer recorded"""
    out = {}
    p = os.path.join(root, CHANGES)
    if os.path.isfile(p):
        for line in open(p):
            t = line.split()
            if len(t) == 4 and t[0] == "changed" and t[1] == table:
                out[t[2]] = int(t[3], 16)
    return out


def newest_release(root):
    best = None
    for line in open(os.path.join(root, "index.txt")):
        t = line.split()
        if len(t) == 4 and not line.startswith("#") and t[1] == "release" and t[0] == "android-arm64":
            v = tuple(int(x) for x in re.findall(r"\d+", t[2].split("-")[0]))
            if best is None or v > best[0]:
                best = (v, t[2])
    return best[1] if best else None


def gate(a):
    root = a.root
    name = a.against or newest_release(root)
    if name is None:
        sys.exit("state_legacy_table: gate: port/state_legacy/index.txt names no release table")
    m = manifest(os.path.join(root, name))
    if int(m["hash_rules"]) != HASH_RULES:
        sys.exit("state_legacy_table: gate: the table %s holds code hashes of rules %s, this build's are "
                 "%d: rebuild it (README.md)" % (name, m["hash_rules"], HASH_RULES))
    commit = m["commit"]
    # the release's functions: unique names, their current-rules hashes
    old_syms = gzip.decompress(open(os.path.join(root, name, "syms.gzip"), "rb").read()).decode()
    if os.path.isfile(os.path.join(root, name, "sections.txt")):
        # a format 2 table: the versions before v0.11.9 wrote no section lines into the file
        old_syms += open(os.path.join(root, name, "sections.txt")).read()
    hz = inflate(open(os.path.join(root, name, "hashes.z"), "rb").read())
    old_hash = {off: h for off, _, h in struct.iter_unpack("<IQQ", hz)}
    old_lo = min(int(l.split()[1], 16) for l in old_syms.splitlines()
                 if l.startswith("section ") and "X" in l.split()[3])
    old_hi = max(int(l.split()[1], 16) + int(l.split()[2], 16) for l in old_syms.splitlines()
                 if l.startswith("section ") and "X" in l.split()[3])
    old_units = unit_starts(old_syms, old_lo, old_hi)
    old_count = {}
    for _, _, nm in old_units:
        old_count[nm] = old_count.get(nm, 0) + 1
    old_by = {nm: old_hash.get(st) for st, _, nm in old_units if old_count[nm] == 1}
    # this build's
    so = open(a.so, "rb").read()
    syms = gzip.decompress(open(a.syms, "rb").read()).decode()
    sec_lines = [l for l in syms.splitlines() if l.startswith("section ")]
    lo, hi = code_range(sec_lines)
    units = unit_starts(syms, lo, hi)
    count = {}
    for _, _, nm in units:
        count[nm] = count.get(nm, 0) + 1
    loads = load_map(so)
    game = game_functions(a.map, ROOT if a.git is None else a.git)
    holders = frame_holders(units, so, loads)
    want = {nm for nm in game if nm in holders and count.get(nm) == 1 and old_by.get(nm) is not None}
    if not want:
        sys.exit("state_legacy_table: gate: no game function to compare (the map names none, or the table "
                 "%s shares none)" % name)
    now = {}
    by_start = {st: nm for st, _, nm in units}
    for st, _, h in unit_hashes(syms, sec_lines, so, HASH_RULES, want):
        now[by_start[st]] = h
    # the game sources changed since the release
    if a.git is not None:
        g = re.search(r"-g([0-9a-f]{7,40})$", commit)
        base = g.group(1) if g else commit
        r = subprocess.run(["git", "-C", a.git, "diff", "--name-only", base, "HEAD", "--", "ico2", "ico"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("state_legacy_table: gate: git diff %s HEAD failed in %s: %s" % (base, a.git, r.stderr.strip()))
        changed = set(r.stdout.split())
    else:
        changed = set(open(a.changed).read().split())
    rec = recorded_changes(root, name)
    differ = sorted(nm for nm in want if now[nm] != old_by[nm])
    explained = [nm for nm in differ if game[nm] in changed]
    noted = [nm for nm in differ if nm not in explained and rec.get(nm) == now[nm]]
    bad = [nm for nm in differ if nm not in explained and nm not in noted]
    print("state_legacy_table: gate against %s (%s): %d game functions that can hold a paused frame, %d "
          "differ (%d in a changed source file, %d recorded in %s, %d unexplained)" %
          (name, commit, len(want), len(differ), len(explained), len(noted), CHANGES, len(bad)))
    for nm in explained:
        print("state_legacy_table: gate:   changed with its source %s: %s" % (game[nm], nm))
    for nm in noted:
        print("state_legacy_table: gate:   recorded in %s: %s" % (CHANGES, nm))
    for nm in bad:
        print("state_legacy_table: gate: FAIL %s (%s, unchanged since %s): hash %016x, the release's %016x" %
              (nm, game[nm], commit, now[nm], old_by[nm]))
    if bad:
        print("state_legacy_table: gate: %s's states saved inside these functions would go to their checkpoint "
              "for a change of the build, not of the game. Find what differs (code_hash_a64.py masks only what "
              "the link moves: a new linker patch or an instruction its decoder misreads is a gap in the hash "
              "rules, fixed in port/platform/code_hash.c and tools/code_hash_a64.py together, with the tables "
              "rebuilt). Only when the game code changed on purpose (a compiler or flag change), add a line "
              "'changed %s <function> <hash>' for each to port/state_legacy/%s, knowing those states then "
              "take the checkpoint path." % (name, name, CHANGES))
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--apk", required=True)
    b.add_argument("--commit", required=True)
    b.add_argument("--name", required=True)
    b.add_argument("--kind", default="release", choices=("release", "rc"))
    b.add_argument("--record", type=int, default=2, choices=(2, 3),
                   help="the format of the version's states' records (3: v0.11.9 and later)")
    b.add_argument("--hashes-only", action="store_true",
                   help="record 3: no symbol file (every table but the newest release's: the gate reads it)")
    b.add_argument("--out", default=os.path.join(ROOT, "port/state_legacy"))
    c = sub.add_parser("check")
    c.add_argument("--root", default=os.path.join(ROOT, "port/state_legacy"))
    k = sub.add_parser("apkcheck")
    k.add_argument("apk")
    k.add_argument("--rc", action="store_true", help="a release candidate's APK: its tables too")
    g = sub.add_parser("gate")
    g.add_argument("--so", required=True, help="the unstripped libmain.so")
    g.add_argument("--syms", required=True, help="its libmain.syms.gz")
    g.add_argument("--map", required=True, help="its link map (lld's)")
    src = g.add_mutually_exclusive_group(required=True)
    src.add_argument("--git", help="the work tree whose HEAD the library is of")
    src.add_argument("--changed", help="a file listing the game source files changed since the release")
    g.add_argument("--against", help="the table to compare with (default: the newest release's)")
    g.add_argument("--root", default=os.path.join(ROOT, "port/state_legacy"))
    a = ap.parse_args()
    {"build": build, "check": check, "apkcheck": apkcheck, "gate": gate}[a.cmd](a)


if __name__ == "__main__":
    main()
