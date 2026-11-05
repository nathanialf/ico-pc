#!/usr/bin/env python3
"""tools/unused_content_audit.py: what each retail disc holds that the game never reaches.

For one disc (--slug, a row of config/regions.txt) this reads the disc image
(an .iso: its DATA.DF directory, the packs' member directories, the root files
and the boot ELF's tables, through port/data/gen/SLUG/table_desc.c) and reports

  (a) the DATA.DF entries that no table row and no program string names;
  (b) the stage rows whose pack (or movie) is on the disc and that no exit
      row of a reachable stage leads to, with the title's own switches
      (ico2/script/src/op.c: the attract movie, 57 and in the 50 Hz mode on the
      European disc 58) and the boot (kanbanBoot.c: the logo, 1) as the roots;
      the stage rows whose packs the disc does not hold, counted by kind;
      the movie rows and who plays them;
  (c) the models (p2o members) of the logo pack and of the two test-room packs
      that no stage a player can reach uses: used by an unreached stage only (the
      rooms' own models), or by nothing at all (the early show's title art);
  (d) the loose items: the stream motions and the sound streams the tables name
      and nothing plays, the files the disc holds outside the game's data, and
      the subtitle file path the Japanese-family subtitle reader opens.

The page docs/port/UNUSED_CONTENT_AUDIT.md holds one generated section per
disc.  Regenerate it (every image, the page whole) with

    python3 -I tools/unused_content_audit.py --write

or one section (the page's other sections kept) with `--slug SLUG --write`.
`--check --slug SLUG` regenerates that disc's section and compares it with
the committed page (exit 1 on a difference); without the image it exits 77.
The image: --disc, else ICO_DISC_IMAGE (pal) or ICO_DISC_IMAGE_<SLUG>, else
baserom/<image name>.iso in this checkout (`--root DIR` points at another
checkout's tree for a worktree without the discs).  Reads the directories and
the boot ELF only: no member's data, no image read whole.  Python 3 only.
Exit: 0 done, 1 a failure or a difference, 2 usage, 77 no image.
"""
import argparse
import os
import posixpath
import re
import struct
import sys
import zlib

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import content_table_check as ctc  # noqa: E402  (Disc, elf_reader, table_range, region_row)

ROOT = ctc.ROOT
PAGE = os.path.join(ROOT, "docs", "port", "UNUSED_CONTENT_AUDIT.md")
EXIT_SKIP = 77
SLUGS = ["pal", "jpn", "kor", "ntsc", "aug6", "jun26", "cn"]
MEMBER = 0x224  # one pack member's directory record
# the loose files a retail disc keeps beside the game: debug leftovers by name
DEBUG_FILES = ["MAIN.MAP", "SRCFILE.TXT", "TRFILE.TXT", "TRTABLE.BIN", "DUMMY.TXT"]
# the ordinary root files of the game's folder: anything else is listed
ROOT_OK = re.compile(r"(\.IRX|\.IMG|\.CNF)$|^DFDATAS$", re.I)
SRC = os.path.join(ROOT, "ico2")


def say(msg):
    print("unused_content_audit: " + msg, file=sys.stderr, flush=True)


# --------------------------------------------------------------------- the disc rows

def disc_row(slug):
    """The row of port/data/discs.c for SLUG: game_dir, elf_path, df_path, image_base."""
    src = open(ctc.DISCS_C, encoding="utf-8").read()
    m = re.search(r'\[ICO_REGION_%s\]\s*=\s*\{(.*?"Ico_\w+".*?)\},' % slug.upper(), src, re.S)
    if not m:
        raise SystemExit("unused_content_audit: no ico_discs row for %s in %s" % (slug, ctc.DISCS_C))
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    strs = re.findall(r'"([^"]*)"', body)
    # id, slug, boot, game_dir, elf_path, df_path, [iso sha1, elf sha1], ..., archive, image
    image = [s for s in strs if re.fullmatch(r"Ico_\w+", s)][-1]
    return dict(id=strs[0], boot=strs[2], game_dir=strs[3], elf=strs[4], df=strs[5], image=image)


def image_path(slug, row, args):
    if args.disc:
        return args.disc
    env = "ICO_DISC_IMAGE" if slug == "pal" else "ICO_DISC_IMAGE_%s" % slug.upper()
    if os.environ.get(env):
        return os.environ[env]
    return os.path.join(args.root or ROOT, "baserom", row["image"] + ".iso")


def list_dir(disc, path):
    """[(name, is_dir, size)] of an ISO directory ('' is the root), '.' and '..' left out."""
    lsn, size = disc.root
    for comp in [c for c in path.split("/") if c]:
        hit = [e for e in list_dir_at(disc, lsn, size) if e[0].upper() == comp.upper() and e[1]]
        if not hit:
            raise ValueError("%s: no %s on the disc" % (path, comp))
        lsn, size = hit[0][3], hit[0][2]
    return [(n, d, s) for n, d, s, _l in list_dir_at(disc, lsn, size)]


def list_dir_at(disc, lsn, size):
    d = disc.sectors(lsn, (size + 2047) // 2048)
    at, out = 0, []
    while at < len(d):
        ln = d[at]
        if ln == 0:
            at = (at // 2048 + 1) * 2048
            continue
        raw = d[at + 33:at + 33 + d[at + 32]]
        if raw not in (b"\0", b"\1"):
            name = raw.decode("latin-1").split(";")[0].rstrip(".") if not d[at + 25] & 2 \
                else raw.decode("latin-1")
            if name.endswith(".") and not d[at + 25] & 2:
                name = name[:-1]
            out.append((name, bool(d[at + 25] & 2), struct.unpack_from("<I", d, at + 10)[0],
                        struct.unpack_from("<I", d, at + 2)[0]))
        at += ln
    return out


class Img:
    """One disc: the ISO, its DATA.DF directory, the boot ELF and its tables."""

    def __init__(self, slug, path):
        self.slug = slug
        self.row = disc_row(slug)
        self.path = path
        self.disc = ctc.Disc(path, self.row["elf"], ctc.disc_df_shift(slug), self.row["df"])
        self.elf = self.disc.elf()
        self.read = ctc.elf_reader(self.elf)
        self.entries = self.disc.entries
        self.by_name = {}
        for n, (off, size) in self.entries:
            self.by_name.setdefault(n, []).append((off, size))
        self._desc = open(ctc.table_desc(slug), encoding="utf-8").read()
        self._fields = {}
        self.region_name = re.search(r'"%s",\s*"[^"]*",\s*"[^"]*",\s*"([^"]*)"' % slug,
                                     self._desc[self._desc.rindex("const IcoTableSet"):]).group(1)

    # ---- tables
    def table(self, name):
        """(lo, hi, count) of a table of the disc's table_desc.c."""
        m = re.search(r'\{"[^"]*",\s*"%s",\s*\w+,\s*(0x[0-9A-Fa-f]+)u,\s*(0x[0-9A-Fa-f]+)u,\s*'
                      r'0x[0-9A-Fa-f]+u,\s*\d+,\s*(\d+)' % name, self._desc)
        if not m:
            raise SystemExit("unused_content_audit: %s not in %s" % (name, ctc.table_desc(self.slug)))
        return int(m.group(1), 16), int(m.group(2), 16), int(m.group(3))

    def has_table(self, name):
        return re.search(r'\{"[^"]*",\s*"%s",' % name, self._desc) is not None

    def rows(self, name):
        """[bytes] one per record of the table."""
        lo, hi, n = self.table(name)
        size = (hi - lo) // n
        blob = self.read(lo, size * n)
        return [blob[i * size:(i + 1) * size] for i in range(n)]

    def fields(self, rec):
        if rec not in self._fields:
            m = re.search(r"fields_%s\[\] = \{(.*?)\n\};" % rec, self._desc, re.S)
            out = {}
            for f in re.finditer(r'\{"([^"]*)",\s*(.*?),\s*(\d+),\s*(\d+),\s*(\d+),\s*ICO_TF_(\w+),'
                                 r'\s*(\d+),\s*(\d+),\s*(\d+)', m.group(1), re.S):
                out[f.group(1)] = (int(f.group(3)), int(f.group(4)), int(f.group(5)), f.group(6),
                                   int(f.group(7)), int(f.group(8)), int(f.group(9)))
            self._fields[rec] = out
        return self._fields[rec]

    def get(self, rec, blob, field, idx=0):
        off, size, count, kind, signed, bit, width = self.fields(rec)[field]
        at = off + idx * size
        if kind == "BITS":
            v = int.from_bytes(blob[off:off + size], "little") >> bit
            return v & ((1 << width) - 1)
        return int.from_bytes(blob[at:at + size], "little", signed=bool(signed))

    def cstr(self, rec, blob, field):
        off, size, count, _k, _s, _b, _w = self.fields(rec)[field]
        return blob[off:off + size * count].split(b"\0")[0].decode("latin-1")

    def strings(self, name):
        """Every printable NUL-ended string of a table's records."""
        out = []
        for r in self.rows(name) if self.has_table(name) else []:
            for s in r.split(b"\0"):
                if len(s) >= 3 and all(32 <= c < 127 for c in s):
                    out.append(s.decode("ascii"))
        return out

    # ---- the packs
    def pack_members(self, off, size):
        """[(path, id, size)] of a pack: only the head of its deflate stream is read."""
        dec = zlib.decompressobj(-15)
        buf, pos = b"", 0
        n = None
        while pos < size:
            buf += dec.decompress(self.disc.read_df(off + pos, min(1 << 16, size - pos)))
            pos += 1 << 16
            if n is None and len(buf) >= 16:
                n = struct.unpack_from("<I", buf, 0)[0]
            if n is not None and len(buf) >= 16 + n * MEMBER:
                break
        out = []
        for k in range(n or 0):
            e = buf[16 + k * MEMBER:16 + (k + 1) * MEMBER]
            mid, _kind, _w, sz = struct.unpack_from("<IIII", e, 0)
            out.append((e[16:].split(b"\0")[0].decode("latin-1"), mid, sz))
        return out

    def packs(self):
        """{pack name: [(path, id, size)]}"""
        out = {}
        for n, (off, size) in self.entries:
            if n.upper().endswith(".DF") and n not in out:
                out[n] = self.pack_members(off, size)
        return out

    def pack_data(self, pack):
        """{member path: bytes} of one pack (the whole pack is inflated)."""
        off, size = self.by_name[pack][0]
        data = zlib.decompressobj(-15).decompress(self.disc.read_df(off, size))
        n = struct.unpack_from("<I", data, 0)[0]
        at = 16 + n * MEMBER
        out = {}
        for k in range(n):
            e = data[16 + k * MEMBER:16 + (k + 1) * MEMBER]
            sz = struct.unpack_from("<I", e, 12)[0]
            out.setdefault(posixpath.normpath(e[16:].split(b"\0")[0].decode("latin-1")), data[at:at + sz])
            at += sz
        return out


# ------------------------------------------------------------------- the analysis

def attract_roots(img):
    """The stages the game switches to by number (not by an exit row), with their source.

    kanbanBoot.c boots into the logo stage 1; op.c's attract switches to the stage at the
    European table's row 57 (the table's own number on the demo disc, which has no memoryPAL
    row) and, on the European disc in the 50 Hz mode, to 58.  The sources are read here so
    a change of those calls stops the audit."""
    boot = open(os.path.join(SRC, "common", "src", "kanbanBoot.c"), encoding="latin-1").read()
    op = open(os.path.join(SRC, "script", "src", "op.c"), encoding="latin-1").read()
    if "stgmgrForceSwitchWithFade(1, 255.0f" not in boot:
        raise SystemExit("unused_content_audit: kanbanBoot.c no longer boots into stage 1 by a literal")
    for need in ("stgmgrForceSwitchWithFade(57, 256.0f", "systemStatus[0] != 0 ? 58 : 57"):
        if need not in op:
            raise SystemExit("unused_content_audit: op.c no longer holds `%s`" % need)
    roots = [(1, "boot: the logo (kanbanBoot.c)")]
    row57 = 58 if img.slug == "jun26" else 57  # region.c ico_region_stage(57)
    roots.append((row57, "the title's attract (op.c, stgmgrForceSwitchWithFade 57)"))
    if img.slug == "pal":
        roots.append((58, "the title's attract in the 50 Hz mode (op.c, systemStatus[0] != 0 ? 58 : 57)"))
    return roots


SWITCH_CALL = re.compile(r"\b(stgmgrForceSwitch\w*|RequestStageChangeSimple|RequestStageChangeDirect)"
                         r"\s*\(([^;]*?)[,)]", re.S)


def switch_literals():
    """{stage number: [file:line]} of the script and stage-manager calls that name a stage by a
    literal: stgmgrForceSwitch*(N, ..), RequestStageChangeSimple(N, ..), RequestStageChangeDirect(
    girl, N, ..), the literal either bare or any alternative of a region selector PAL_xxx(a, b, ..)
    (op.c, whose attract is the roots, and e3.c, whose numbers are the early show's own on
    each disc and which only early-show stages run, are left out).  RequestStageChange(N, ..)
    takes an exit slot of the current stage (boyact.c: exitData[ent[N - 1]]) and is covered by
    the exit rows, not here."""
    out = {}
    for sub in ("script/src", "common/src"):
        base = os.path.join(SRC, sub)
        for f in sorted(os.listdir(base)):
            if not f.endswith(".c") or f in ("op.c", "e3.c"):
                continue  # op.c's attract is the roots above; e3.c's numbers are the early show's own
            text = open(os.path.join(base, f), encoding="latin-1").read()
            for m in re.finditer(r"\b(stgmgrForceSwitch\w*|RequestStageChangeSimple|RequestStageChangeDirect)"
                                 r"\s*\(([^;]*)", text):
                if m.group(1) == "stgmgrForceSwitch" and "{" in text[max(0, m.start() - 6):m.start()]:
                    continue
                args, depth, cur = [], 0, ""
                for ch in m.group(2):
                    if ch == "(":
                        depth += 1
                    elif ch == ")":
                        if depth == 0:
                            break
                        depth -= 1
                    if ch == "," and depth == 0:
                        args.append(cur)
                        cur = ""
                    else:
                        cur += ch
                args.append(cur)
                a = args[1 if m.group(1) == "RequestStageChangeDirect" and len(args) > 1 else 0]
                nums = [int(x) for x in re.findall(r"(?<![\w.])(\d+)(?![\w.])", a)] if (
                    re.fullmatch(r"\s*(\d+|PAL\w*\([\d,\s]+\)|systemStatus\[0\] != 0 \? \d+ : \d+)\s*", a)) else []
                line = text.count("\n", 0, m.start()) + 1
                for n in nums:
                    out.setdefault(n, []).append("%s/%s:%d" % (sub.split("/")[0], f, line))
    return out


def stream_readers():
    """{streamMotion index: [source file:count]} from the `streamMotion[N]` literals."""
    out = {}
    for dp, _dn, fn in os.walk(SRC):
        for f in sorted(fn):
            if not f.endswith(".c"):
                continue
            path = os.path.join(dp, f)
            for m in re.finditer(r"streamMotion\[(\d+)\]", open(path, encoding="latin-1").read()):
                rel = os.path.relpath(path, SRC).replace(os.sep, "/")
                out.setdefault(int(m.group(1)), {}).setdefault(rel, 0)
                out[int(m.group(1))][rel] += 1
    return out


def analyse(img):
    R = {}
    packs = img.packs()
    R["packs"] = packs
    entry_names = set(n for n, _ in img.entries)
    stage_rows = img.rows("stageData")
    movie = [r.split(b"\0")[0].decode("latin-1") for r in img.rows("movieFile")]
    exits = [img.get("ExitData", r, "nextStage") for r in img.rows("exitData")]
    stages = []
    for i, r in enumerate(stage_rows):
        df = img.cstr("StgPre", r, "dataFile")
        mp = img.get("StgPre", r, "mpegNo")
        ent = [img.get("StgPre", r, "ent", k) for k in range(15)]
        mdl = [img.get("StgPre", r, "mdl", k) for k in range(4)]
        stages.append(dict(row=i, key=img.cstr("StgPre", r, "key"), name=img.cstr("StgPre", r, "name"),
                           df=df, mpeg=mp, ent=ent, mdl=mdl,
                           lay=(img.get("StgPre", r, "labelTop"), img.get("StgPre", r, "labelEnd"))))
    for s in stages:
        if s["mpeg"]:
            f = movie[s["mpeg"]] if s["mpeg"] < len(movie) else ""
            s["file"] = f.rsplit("/", 1)[-1]
            s["on_disc"] = s["file"] in entry_names
            s["kind"] = "movie"
        elif s["df"] in ("NOTUSE", ""):
            s["file"], s["on_disc"], s["kind"] = "", False, "none"
        else:
            s["file"] = s["df"] + ".DF"
            s["on_disc"] = s["file"] in entry_names
            s["kind"] = ("development" if s["df"].startswith("STGNOCD_") else
                         "early show" if s["df"].startswith("STGONLYSAMPLE_") else "stage")
    R["stages"] = stages
    R["movie"] = movie
    # reachability: the closure of the roots over the exit rows of each stage's ent[]
    roots = attract_roots(img)
    lits = switch_literals()
    R["lits"] = lits
    reach = {}
    todo = list(roots) if stages[1]["on_disc"] else []
    if stages[1]["on_disc"]:
        todo += [(n, "a script switch (%s)" % sites[0]) for n, sites in sorted(lits.items())
                 if 0 < n < len(stages) and stages[n]["on_disc"]]
    while todo:
        st, how = todo.pop()
        if st in reach or not 0 < st < len(stages):
            continue
        reach[st] = how
        for e in stages[st]["ent"]:
            if 0 < e < len(exits):
                nxt = exits[e]
                if 0 < nxt < len(stages) and nxt not in reach:
                    todo.append((nxt, "an exit of stage %d" % st))
    R["roots"] = roots
    R["reach"] = reach
    # exit rows that lead into a stage: the entrance rows of a stage that names itself
    R["exit_targets"] = sorted(set(x for x in exits if 0 < x < len(stages)))
    R["exit_count"] = len(exits)
    # the names the tables hold
    named = set()
    for t in ("movieFile", "adpcmFile", "streamMotion", "iconFile", "jimakuFileName"):
        named |= set(s.rsplit("/", 1)[-1] for s in img.strings(t))
    for s in stages:
        if s["df"] not in ("NOTUSE", "", "#REF!"):
            named.add(s["df"] + ".DF")
    prog = set()
    for n in entry_names:
        if n.upper().endswith(".DF"):
            pat = rb"(?<![A-Za-z0-9_])" + re.escape(n[:-3].encode()) + rb"(?![A-Za-z0-9_])"
        else:
            pat = rb"(?:\0|/)" + re.escape(n.encode()) + rb"\0"
        if n not in named and re.search(pat, img.elf):
            prog.add(n)
    R["named"], R["prog"] = named, prog
    # member paths the tables name
    paths = set()
    for t in ("texFile", "modelData", "stageTable", "particleEffectFile", "seFile", "jimakuFileName",
              "streamMotion"):
        for s in img.strings(t):
            paths.add(posixpath.normpath(s))
    R["paths"] = paths
    return R


def model_use(img, R):
    """(c): the p2o members of the logo pack and the test rooms' packs by who uses their ids."""
    stages = R["stages"]
    reach = R["reach"]
    packs = R["packs"]
    layouts = img.rows("objLayout")
    lay_mdl = [img.get("GenGeo", r, "mdl") for r in layouts]
    scene = [img.get("StgObjDat", r, "kind") for r in img.rows("objTableScene")]
    # a model is used by a scene animation when the animation's file is in a pack and one of
    # its nodes carries the model's name (the stage animation table's object range only lists
    # the models the file may name: the retail title's range also holds the early show's art)
    cand = {}
    for pk in ("STGLOG.DF", "STGBOSS_TEST.DF", "STG8TEST.DF"):
        for path, mid, _sz in packs.get(pk, []):
            if path.endswith(".p2o"):
                cand.setdefault(mid, set()).add(posixpath.splitext(posixpath.basename(path))[0])
    where = {}
    for pk, ms in packs.items():
        for m in ms:
            where.setdefault(posixpath.normpath(m[0]), pk)
    need = {}  # pack -> {bga path: set of candidate ids its row range holds}
    scene_absent = set()
    for r in img.rows("stageTable"):
        path = img.cstr("StageAnimDef", r, "path")
        first, last = img.get("StageAnimDef", r, "objFirst"), img.get("StageAnimDef", r, "objLast")
        if path == "NULL" or last <= first:
            continue
        ids = set(scene[k] for k in range(first, min(last, len(scene))))
        key = posixpath.normpath(path)
        if key in where:
            hit = ids & set(cand)
            if hit:
                need.setdefault(where[key], {}).setdefault(path, set()).update(hit)
        else:
            scene_absent.update(ids)
    scene_used = set()
    for pk, files in need.items():
        data = img.pack_data(pk)
        for path, ids in files.items():
            names = set(d["name"] for d in ctc.bga_nodes(data.get(posixpath.normpath(path), b"")))
            scene_used.update(i for i in ids if cand[i] & names)
    by_stage = {}  # model id -> [stage rows whose layout range holds it]
    for s in stages:
        lo, hi = s["lay"]
        for k in range(max(lo, 0), min(hi, len(lay_mdl))):
            by_stage.setdefault(lay_mdl[k], set()).add(s["row"])
        for m in s["mdl"]:
            by_stage.setdefault(m, set()).add(s["row"])
    used_reach = set(i for i, ss in by_stage.items() if ss & set(reach)) | scene_used
    out = {}
    for pk in ("STGLOG.DF", "STGBOSS_TEST.DF", "STG8TEST.DF"):
        if pk not in packs:
            continue
        rec = dict(p2o=0, reach=0, rooms=[], nowhere=[], e3=[])
        for path, mid, size in packs[pk]:
            if not path.endswith(".p2o"):
                if path.startswith("object/sdf/e3/"):
                    rec["e3"].append((path, None, size, "texture"))
                continue
            rec["p2o"] += 1
            if mid in used_reach:
                cls = "reachable content"
                rec["reach"] += 1
            elif mid in by_stage:
                cls = "unreached stages only"
                rec["rooms"].append((path, mid, sorted(by_stage[mid])))
            else:
                cls = "no row names it"
                rec["nowhere"].append((path, mid, size))
            if path.startswith("object/sdf/e3/"):
                rec["e3"].append((path, mid, size, cls))
        out[pk] = rec
    out["_scene_absent"] = len(scene_absent)
    return out


# ------------------------------------------------------------------------- the page

def section(img):
    R = analyse(img)
    M = model_use(img, R)
    stages, reach, packs = R["stages"], R["reach"], R["packs"]
    L = []
    w = L.append
    w("## %s: %s (%s)" % (img.slug, img.row["id"], img.region_name))
    w("")
    w("The image holds %d DATA.DF entries (%d packs), %d stage rows and %d exit rows." % (
        len(img.entries), len(packs), len(stages), R["exit_count"]))
    w("")
    # ---- (a)
    w("### DATA.DF entries no table or program string names")
    w("")
    free = [(n, s) for n, (o, s) in img.entries
            if n not in R["named"] and n not in R["prog"]]
    if not free:
        w("None.")
    else:
        w("| entry | bytes (compressed) | members | members a table names by path |")
        w("| --- | --- | --- | --- |")
        seen = set()
        for n, size in free:
            if n in seen:
                continue
            seen.add(n)
            ms = packs.get(n)
            if ms is None:
                w("| %s | %d | - | - |" % (n, size))
            else:
                w("| %s | %d | %d | %d |" % (n, size, len(ms),
                                              sum(1 for m in ms if posixpath.normpath(m[0]) in R["paths"])))
    w("")
    if R["prog"]:
        w("Named by a string in the program, not by a table: %s." % ", ".join(sorted(R["prog"])))
        w("")
    dup = sorted(set(n for n, c in ((n, len(v)) for n, v in img.by_name.items()) if c > 1))
    if dup:
        w("Names listed twice in the directory: %s." % ", ".join(dup))
        w("")
    # ---- (b)
    w("### Stage rows")
    w("")
    on = [s for s in stages if s["row"] and s["on_disc"]]
    unreached = [s for s in on if s["row"] not in reach]
    boot_ok = stages[1]["on_disc"]
    if boot_ok:
        w("%d of the %d stage rows have their pack or movie on the disc; %d of those are reached "
          "(a root below, or an exit row of a reached stage)." % (len(on), len(stages) - 1,
                                                                 len(on) - len(unreached)))
        w("")
        w("Roots: " + "; ".join("stage %d, %s" % r for r in R["roots"]) + ".")
        w("")
        sw = [(n, sites) for n, sites in sorted(R["lits"].items())
              if 0 < n < len(stages) and stages[n]["on_disc"]]
        w("Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, "
          "RequestStageChangeDirect; the numbers are the source's own, every alternative of a "
          "region selector counted), first call site each:")
        w("")
        w("| row | name | call site |")
        w("| --- | --- | --- |")
        for n, sites in sw:
            w("| %d | %s | %s |" % (n, stages[n]["name"], sites[0]))
        w("")
        w("Reached by nothing, pack on the disc:")
    else:
        w("%d of the %d stage rows have their pack or movie on the disc, and the boot stage (row 1, "
          "%s) is not one of them: the game's tables are whole but the disc's packs are the early "
          "show's samples only, so no verdict on reached rows is made.  The rows with a pack:" % (
              len(on), len(stages) - 1, stages[1]["file"]))
        unreached = on
    w("")
    if unreached:
        w("| row | name | file | what leads to it |")
        w("| --- | --- | --- | --- |")
        for s in unreached:
            w("| %d | %s | %s | %s |" % (s["row"], s["name"], s["file"],
                                       leads_to(img, R, s["row"]) if boot_ok else "-"))
    else:
        w("None.")
    w("")
    miss = {}
    for s in stages[1:]:
        if not s["on_disc"]:
            miss.setdefault(s["kind"], []).append(s["row"])
    w("Rows whose pack or movie the disc does not hold: " + (", ".join(
        "%d %s (%s)" % (len(v), k, rows_text(v)) for k, v in sorted(miss.items())) or "none") + ".")
    w("")
    w("Movie rows:")
    w("")
    w("| movie | file | on the disc | stage rows playing it | reached |")
    w("| --- | --- | --- | --- | --- |")
    entry_names = set(n for n, _ in img.entries)
    for k, f in enumerate(R["movie"]):
        if f == "NULL":
            continue
        base = f.rsplit("/", 1)[-1]
        who = [s["row"] for s in stages if s["mpeg"] == k]
        w("| %d | %s | %s | %s | %s |" % (k, base, "yes" if base in entry_names else "no",
                                        ", ".join(map(str, who)) or "none",
                                        ", ".join("yes" if r in reach else "no" for r in who) or "-"))
    w("")
    # ---- (c)
    w("### Models in the logo pack and the test-room packs")
    w("")
    w("A model is used by reachable content when the layout rows of a reached stage or a reached "
      "stage's model list hold its id, or an animation file in a pack names it in a node (the "
      "animation table's object ranges alone do not decide it: the retail title's range also holds "
      "the early show's art).")
    w("")
    w("| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | "
      "no row names it |")
    w("| --- | --- | --- | --- | --- | --- |")
    for pk in ("STGLOG.DF", "STGBOSS_TEST.DF", "STG8TEST.DF"):
        if pk not in M:
            w("| %s | no | | | | |" % pk)
            continue
        r = M[pk]
        w("| %s | %d | %d | %d | %d | %d |" % (pk, img.by_name[pk][0][1], r["p2o"], r["reach"],
                                              len(r["rooms"]), len(r["nowhere"])))
    w("")
    w("`No row names it` is every model no layout row, model list or animation names; the weapons and "
      "effects the scripts create by id are among them, so it is a list to read, not a verdict.  "
      "The early show's art has a folder of its own, listed after each pack.")
    w("")
    for pk in ("STGLOG.DF", "STGBOSS_TEST.DF", "STG8TEST.DF"):
        if pk not in M:
            continue
        r = M[pk]
        if r["nowhere"]:
            w("No row names, in %s: %s." % (pk, ", ".join(
                "%s (id %d)" % (p.rsplit("/", 1)[-1], i) for p, i, _sz in r["nowhere"])))
            w("")
        if r["rooms"]:
            w("Only unreached stages use, in %s: %s." % (pk, "; ".join(
                "%s (id %d, stage rows %s)" % (p.rsplit("/", 1)[-1], i, ", ".join(map(str, ss)))
                for p, i, ss in r["rooms"])))
            w("")
        if r["e3"]:
            w("The early show's title art, in %s (the folder object/sdf/e3):" % pk)
            w("")
            w("| member | model id | bytes | used by |")
            w("| --- | --- | --- | --- |")
            for p, i, sz, cls in r["e3"]:
                w("| %s | %s | %d | %s |" % (p.rsplit("/", 1)[-1], "-" if i is None else i, sz, cls))
            w("")
    w("Scene-animation rows whose animation file is in no pack on this disc name %d model ids."
      % M["_scene_absent"])
    w("")
    # ---- (d)
    w("### Loose items")
    w("")
    sm = [r.split(b"\0")[0].decode("latin-1") for r in img.rows("streamMotion")]
    rd = stream_readers()
    w("| stream motion | file | on the disc | read by |")
    w("| --- | --- | --- | --- |")
    for k, p in enumerate(sm):
        if p == "NULL":
            continue
        base = p.rsplit("/", 1)[-1]
        who = rd.get(k)
        w("| %d | %s | %s | %s |" % (k, base, "yes" if base in entry_names else "no",
                                     ", ".join(sorted(who)) if who else "nothing"))
    w("")
    ad = [r.split(b"\0")[0].decode("latin-1") for r in img.rows("adpcmFile")]
    absent = [(k, p) for k, p in enumerate(ad) if p != "NULL" and p.rsplit("/", 1)[-1] not in entry_names]
    shown = ", ".join("%d %s" % (k, p.rsplit("/", 1)[-1]) for k, p in absent[:8])
    w("Sound streams the table names that are not on the disc: %s." % (
        "none" if not absent else "%d (%s%s)" % (len(absent), shown, ", and more" if len(absent) > 8 else "")))
    w("")
    jim = [n for n in entry_names if n.lower() == "data.jim"]
    has_str = b"Tex_jimaku/data.jim" in img.elf
    w("Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that "
      "carry it): %s by the program, %s as a DATA.DF entry." % (
          "named" if has_str else "not named", "present" if jim else "not present"))
    w("")
    top = list_dir(img.disc, img.row["game_dir"])
    names = {n.upper(): s for n, d, s in top if not d}
    dbg = [(n, names[n]) for n in DEBUG_FILES if n in names]
    other = [(n, s) for n, d, s in top if not d and not ROOT_OK.search(n) and n.upper() not in
             set(DEBUG_FILES) and n.upper() != img.row["elf"].rsplit("/", 1)[-1].upper()]
    w("Debug files beside the game: %s." % (", ".join("%s (%d bytes)" % x for x in dbg) or "none"))
    w("")
    if other:
        w("Other files in the game's folder: %s." % ", ".join("%s (%d bytes)" % x for x in other))
        w("")
    return "\n".join(L).rstrip() + "\n"


def rows_text(v):
    if len(v) <= 3:
        return ", ".join(map(str, v))
    runs, i = [], 0
    while i < len(v):
        j = i
        while j + 1 < len(v) and v[j + 1] == v[j] + 1:
            j += 1
        runs.append("%d" % v[i] if i == j else "%d-%d" % (v[i], v[j]))
        i = j + 1
    return ", ".join(runs)


def leads_to(img, R, row):
    """What an exit row says about an unreached stage: the rows that name it as a destination."""
    st = R["stages"][row]
    if st["mpeg"]:
        same = [s["row"] for s in R["stages"] if s["row"] in R["reach"] and s["file"] == st["file"]]
        if same:
            return "no exit row; plays the file of reached row %s" % rows_text(same)
    exits = [img.get("ExitData", r, "nextStage") for r in img.rows("exitData")]
    rows = [i for i, x in enumerate(exits) if x == row]
    named_by = []
    for s in R["stages"]:
        for e in s["ent"]:
            if e in rows and s["row"] != row:
                named_by.append(s["row"])
    if named_by:
        return "an exit of stage %s (that stage is unreached)" % ", ".join(map(str, sorted(set(named_by))))
    if rows:
        return "entrance row %s names it; no stage lists that row as an exit" % rows_text(rows)
    return "no exit row"


HEAD = """# Unused content audit

What each retail disc holds that the game never reaches, read from the disc images by
`tools/unused_content_audit.py` (the directories and the boot program's tables only).  This
page is generated: one section per disc, and the ctests `unused_content_audit_<disc>` compare each
section with the disc.  To regenerate it, with every image at hand:

    python3 -I tools/unused_content_audit.py --write

or one disc's section (the others kept): `python3 -I tools/unused_content_audit.py --slug jpn --write`.

How each part is decided is in the script's header.  A stage row is *reached* when it is a root
(the boot's logo; the title's attract movie, by `ico2/script/src/op.c`) or an exit row of a
reached stage leads to it; the debug Stage Select reaches every row whose pack is on the disc,
and is not counted.  An exit row no stage lists is an entrance row (where a forced switch lands:
`getExitIndexOfStage` in `ico2/fumi/src/act-game.c`), so it does not make its stage reached.  The
page reports what the tables show: a stage the program enters by a number it computes would be
listed too, and the page does not decide which of the listed rows the game really never enters.
"""


def build_page(sections):
    return HEAD + "\n" + "\n".join(sections[s] for s in SLUGS if s in sections)


def split_sections(text):
    out = {}
    for m in re.finditer(r"^## (\w+): .*?(?=^## |\Z)", text, re.S | re.M):
        out[m.group(1)] = m.group(0)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--slug", help="one disc (a row of config/regions.txt); default every disc")
    p.add_argument("--disc", help="the disc image (.iso) of --slug")
    p.add_argument("--root", help="a checkout whose baserom/ holds the images")
    p.add_argument("--check", action="store_true", help="compare with the committed page")
    p.add_argument("--write", action="store_true", help="write the page (or --slug's section)")
    p.add_argument("--page", default=PAGE)
    args = p.parse_args()
    slugs = [args.slug] if args.slug else SLUGS
    if args.slug and args.slug not in SLUGS:
        print("unused_content_audit: unknown slug %s" % args.slug, file=sys.stderr)
        return 2
    if args.disc and not args.slug:
        print("unused_content_audit: --disc needs --slug", file=sys.stderr)
        return 2
    if not args.check and not args.write:
        args.write = False
    new = {}
    for slug in slugs:
        path = image_path(slug, disc_row(slug), args)
        if not path.lower().endswith(".iso") or not os.path.exists(path):
            say("SKIP %s: no disc image (.iso) at %s" % (slug, path))
            if args.check:
                return EXIT_SKIP
            if args.write:
                return 1
            continue
        new[slug] = section(Img(slug, path))
    if args.check:
        have = split_sections(open(args.page, encoding="utf-8").read()) if os.path.exists(args.page) else {}
        rc = 0
        for slug, text in new.items():
            old = have.get(slug)
            if old is None or old.rstrip() != text.rstrip():
                say("FAIL %s: the committed section differs from the disc (regenerate: "
                    "tools/unused_content_audit.py --slug %s --write)" % (slug, slug))
                if old is not None:
                    import difflib
                    for ln in list(difflib.unified_diff(old.splitlines(), text.splitlines(), "page",
                                                       "disc", lineterm=""))[:40]:
                        print(ln)
                rc = 1
            else:
                say("%s: ok" % slug)
        return rc
    if args.write:
        have = split_sections(open(args.page, encoding="utf-8").read()) if os.path.exists(args.page) else {}
        have.update(new)
        missing = [s for s in SLUGS if s not in have]
        if missing:
            say("the page lacks %s: run --write with those images too" % ", ".join(missing))
        with open(args.page, "w", encoding="utf-8") as f:
            f.write(build_page(have))
        say("wrote %s" % os.path.relpath(args.page, ROOT))
        return 0
    for slug in slugs:
        print(new.get(slug, ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
