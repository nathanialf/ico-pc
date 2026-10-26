#!/usr/bin/env python3
"""tools/check_docs.py [--root DIR] [--allow FILE] [--summary] [PATH ...]

The documentation gate: README.md, docs/*.md and docs/port/*.md are read
line by line and every finding is printed as `path:line: gate: text`.

Gates:
  links    every relative `[text](path[#anchor])` names a file git tracks
           (a directory: one with a tracked file in it) and the anchor is a
           heading slug of the target (GitHub's rule: lowercase, spaces to
           `-`, punctuation dropped, a repeated heading gets -1, -2, ...);
           https://github.com/nathanialf/ico-pc/blob/main/<path> URLs are
           checked as paths. Fenced code blocks and code spans are skipped.
  history  release numbers and words that compare the tree with an earlier
           one (previously, no longer, ...). docs/RELEASE_NOTES_v*.md are
           exempt from this gate only.
  codes    work-package and review codes (R2a, P3, AN-12, VIS-RULES, ...).
  issues   issue numbers, in the player set.
  jargon   the port's internal words (rd, RHI, GS, replay, fiber, ...), in
           the player set.
  todo     TODO, FIXME, XXX.
  package  every name in tools/package_docs_lib.sh `pkg_player_docs` exists
           as docs/<name>.md, and every docs/*.md README.md links is in that
           list (LEGAL, THIRD_PARTY, BUILDING and the release notes aside).
The player set is README.md and the docs that pkg_player_docs names. Gates
other than links read code blocks and code spans too: a player sees them.

The allow-list (tools/check_docs_allow.txt) holds one regex per line, `#`
starting a comment line. `GLOB: REGEX` limits a regex to the files that
match the glob (the path from the repository root; `*` and `?` stay within
one folder, `**` crosses folders). A finding
is dropped when an allowed regex matches its whole matched text, or is
found anywhere in its line.

With PATH arguments only those files are read (they must be in scope) and
the package gate is skipped. --summary prints the counts per file and gate
after the findings.

Exit 0 when there are no findings, 1 when there are, 2 for a bad setup.
"""
import argparse
import os
import re
import subprocess
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_URL = "https://github.com/nathanialf/ico-pc"

# (pattern, flags) per gate; each match is one finding
HISTORY = [
    r"\bv?0\.[0-9]+\.[0-9]+\b",
    r"\brc[0-9]+\b",
    r"\bpreviously\b",
    r"\bused to\b",
    r"\bno longer\b",
    r"\bnew in\b",
    r"\bolder (version|build)s?\b",
    r"\bearlier versions?\b",
    r"\bformer(ly)?\b",
    r"\bchanged in\b",
    r"\bsince v",
    r"\bbefore version\b",
    r"\bthis version\b",
    r"\bwas (added|fixed|changed)\b",
    r"\bearlier rule\b",
    r"\bbefore this job\b",
]
CODES = [
    r"\b(R[0-9][a-z]?|P[0-9]|AN-[0-9]+[a-z]?|I[0-9]+[a-z]|B[0-9]|Y[0-9]|AA[0-9])\b",
    r"VIS-WINDOW",
    r"VIS-RULES",
    r"EXACT-RASTER",
    r"GS-BLEND",
    r"\breview [A-Z][0-9]\b",
]
ISSUES = [r"issues/[0-9]+", r"\bissue #?[0-9]+\b"]
# Acronyms and identifiers are matched as written; words in any case.
JARGON_CASE = [
    r"\brd:",
    r"\brd\b",
    r"\bRHI\b",
    r"\bGS\b",
    r"TEX0",
    r"TEXA",
    r"\bVU[01]\b",
    r"SPU2",
    r"\bEE\b",
    r"\bIOP\b",
    r"\bSIF\b",
    r"\bFIFO\b",
    r"\bICO_[A-Z0-9_]+\b",
    r"HLSL|SPIR-V|DXIL",
]
JARGON_ANYCASE = [
    r"\breplay",
    r"present mode",
    r"\bfiber",
    r"\bo2r\b",
    r"Main tick",
    r"libcdvd|libmc|libpad",
    r"\bheadless\b",
    r"\bshaders?\b",
    r"\bmailbox\b",
    r"\bswapchain\b",
    r"\bctest\b",
    r"\bworktree\b",
    r"\bdecomp",
]
TODO = [r"\b(TODO|FIXME|XXX)\b"]

# docs/*.md that README.md may link without being a packaged player guide
NOT_PLAYER = {"LEGAL", "THIRD_PARTY", "BUILDING"}


def compile_all(pats, flags=0):
    return [re.compile(p, flags) for p in pats]


GATES = {
    "history": compile_all(HISTORY, re.I),
    "codes": compile_all(CODES),
    "issues": compile_all(ISSUES, re.I),
    "jargon": compile_all(JARGON_CASE) + compile_all(JARGON_ANYCASE, re.I),
    "todo": compile_all(TODO),
}

FENCE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
CODE_SPAN = re.compile(r"(`+)(.+?)\1")
LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\]]*\])*\]\(\s*(<[^>]*>|[^)\s]+)(?:\s+\"[^\"]*\")?\s*\)")
HEADING = re.compile(r"^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
HTML_ANCHOR = re.compile(r"<a\s+(?:name|id)=\"([^\"]+)\"", re.I)


def tracked_files(root):
    """Paths git tracks, from the root; None when git cannot say."""
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True,
                             check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {p for p in out.decode("utf-8", "replace").split("\0") if p}


def player_docs(root):
    """The names in package_docs_lib.sh's pkg_player_docs=(...)."""
    path = os.path.join(root, "tools", "package_docs_lib.sh")
    with open(path, encoding="utf-8") as f:
        m = re.search(r"^pkg_player_docs=\(([^)]*)\)", f.read(), re.M)
    if not m:
        raise SystemExit("check_docs: no pkg_player_docs=(...) in tools/package_docs_lib.sh")
    return m.group(1).split()


def doc_files(root):
    out = []
    if os.path.isfile(os.path.join(root, "README.md")):
        out.append("README.md")
    for d in ("docs", "docs/port"):
        full = os.path.join(root, d)
        if os.path.isdir(full):
            out += sorted(d + "/" + n for n in os.listdir(full) if n.endswith(".md"))
    return out


def read_lines(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read().split("\n")


def prose_mask(lines):
    """For each line, True when it is inside a fenced code block (the fence
    lines included)."""
    mask = []
    fence = None
    for line in lines:
        m = FENCE.match(line)
        if fence is None and m:
            fence = m.group(1)[0] * len(m.group(1))
            mask.append(True)
        elif fence is not None:
            mask.append(True)
            if line.strip().startswith(fence) and line.strip().strip(fence[0]) == "":
                fence = None
        else:
            mask.append(False)
    return mask


def slug(text):
    """GitHub's anchor for a heading's text."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links: their text
    text = re.sub(r"<[^>]+>", "", text)  # inline HTML
    text = text.replace("`", "").replace("*", "")
    text = text.strip().lower()
    text = "".join(c for c in text if c.isalnum() or c in " -_")
    return text.replace(" ", "-")


def anchors(lines):
    """The anchors a document defines: heading slugs, -1, -2 for repeats,
    and <a name/id> anchors."""
    out = set()
    seen = {}
    code = prose_mask(lines)
    prev = None
    for i, line in enumerate(lines):
        if code[i]:
            prev = None
            continue
        for a in HTML_ANCHOR.findall(line):
            out.add(a)
        text = None
        m = HEADING.match(line)
        if m:
            text = m.group(2)
        elif prev and prev.strip() and re.match(r"^\s{0,3}(=+|-+)\s*$", line) \
                and not HEADING.match(prev) and not prev.lstrip().startswith(("-", "*", "|", ">")):
            text = prev.strip()
        if text is not None:
            s = slug(text)
            n = seen.get(s, 0)
            out.add(s if n == 0 else "%s-%d" % (s, n))
            seen[s] = n + 1
        prev = line
    return out


def glob_regex(glob):
    """A glob as a regex: `**` any text, `*` and `?` within one folder."""
    out = ""
    i = 0
    while i < len(glob):
        if glob.startswith("**", i):
            out += ".*"
            i += 2
        elif glob[i] == "*":
            out += "[^/]*"
            i += 1
        elif glob[i] == "?":
            out += "[^/]"
            i += 1
        else:
            out += re.escape(glob[i])
            i += 1
    return re.compile(out + r"\Z")


class Allow:
    def __init__(self, path):
        self.rules = []  # (glob or None, regex)
        if not os.path.isfile(path):
            return
        with open(path, encoding="utf-8") as f:
            for raw in f:
                line = raw.rstrip("\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                m = re.match(r"^(\S*(?:\.md|[*?])\S*):\s+(.*)$", line)
                glob, rx = (m.group(1), m.group(2)) if m else (None, line.strip())
                self.rules.append((glob_regex(glob) if glob else None, re.compile(rx)))

    def allows(self, rel, text, line):
        for glob, rx in self.rules:
            if glob is not None and not glob.match(rel):
                continue
            if rx.fullmatch(text) or rx.search(line):
                return True
        return False


class Checker:
    def __init__(self, root, allow):
        self.root = root
        self.allow = allow
        self.findings = []  # (path, line, gate, text)
        self.tracked = tracked_files(root)
        self.anchor_cache = {}

    def add(self, rel, n, gate, text, line):
        if not self.allow.allows(rel, text, line):
            self.findings.append((rel, n, gate, text))

    def is_file(self, rel):
        if self.tracked is not None:
            return rel in self.tracked
        return os.path.isfile(os.path.join(self.root, rel))

    def is_dir(self, rel):
        rel = rel.rstrip("/")
        if self.tracked is not None:
            return rel == "" or any(p.startswith(rel + "/") for p in self.tracked)
        return os.path.isdir(os.path.join(self.root, rel))

    def anchors_of(self, rel):
        if rel not in self.anchor_cache:
            self.anchor_cache[rel] = anchors(read_lines(self.root, rel))
        return self.anchor_cache[rel]

    def check_target(self, rel, n, line, target, doc_dir, shown):
        """One link target: a path from the repository root, or None when
        the link is not checked; reports a finding when it is wrong."""
        path, _, frag = target.partition("#")
        path = urllib.parse.unquote(path)
        frag = urllib.parse.unquote(frag)
        if path == "":
            dest = rel
        else:
            dest = os.path.normpath(os.path.join(doc_dir, path)).replace(os.sep, "/")
            if dest.startswith("../") or dest == "..":
                self.add(rel, n, "links", shown + " (outside the repository)", line)
                return
            if dest == ".":
                dest = ""
            if not self.is_file(dest):
                if self.is_dir(dest):
                    if frag:
                        self.add(rel, n, "links", shown + " (anchor on a folder)", line)
                    return
                self.add(rel, n, "links", shown + " (no such tracked file)", line)
                return
        if frag and dest.endswith(".md") and frag not in self.anchors_of(dest):
            self.add(rel, n, "links", shown + " (no such heading)", line)

    def check_links(self, rel, lines, code):
        doc_dir = os.path.dirname(rel)
        for i, line in enumerate(lines):
            if code[i]:
                continue
            text = CODE_SPAN.sub(lambda m: " " * len(m.group(0)), line)
            for m in LINK.finditer(text):
                target = m.group(1)
                if target.startswith("<"):
                    target = target[1:-1]
                if target.startswith(REPO_URL):
                    tail = target[len(REPO_URL):]
                    m2 = re.match(r"^/(?:blob|tree)/main/([^#?]*)(#.*)?$", tail)
                    if m2:
                        self.check_target(rel, i + 1, line, m2.group(1) + (m2.group(2) or ""),
                                          "", target)
                    elif tail.startswith("#"):
                        self.check_target(rel, i + 1, line, "README.md" + tail, "", target)
                    continue
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                    continue  # another site, mailto:
                self.check_target(rel, i + 1, line, target, doc_dir, target)

    def check_words(self, rel, lines, gates):
        for i, line in enumerate(lines):
            for gate in gates:
                seen = set()  # one finding per place: `rd:` is not also `rd`
                for rx in GATES[gate]:
                    for m in rx.finditer(line):
                        if m.start() not in seen:
                            seen.add(m.start())
                            self.add(rel, i + 1, gate, m.group(0), line)

    def check_file(self, rel, player):
        lines = read_lines(self.root, rel)
        self.check_links(rel, lines, prose_mask(lines))
        gates = ["codes", "todo"]
        if not re.match(r"docs/RELEASE_NOTES_v[^/]*\.md\Z", rel):
            gates.insert(0, "history")
        if rel in player:
            gates += ["issues", "jargon"]
        self.check_words(rel, lines, gates)

    def check_package(self, names):
        for name in names:
            if not self.is_file("docs/%s.md" % name):
                self.add("tools/package_docs_lib.sh", 1, "package",
                         "docs/%s.md (in pkg_player_docs, not tracked)" % name, "")
        lines = read_lines(self.root, "README.md")
        code = prose_mask(lines)
        for i, line in enumerate(lines):
            if code[i]:
                continue
            text = CODE_SPAN.sub(lambda m: " " * len(m.group(0)), line)
            for m in LINK.finditer(text):
                t = m.group(1).strip("<>").partition("#")[0]
                if t.startswith(REPO_URL + "/blob/main/"):
                    t = t[len(REPO_URL + "/blob/main/"):]
                mm = re.match(r"^(?:\./)?docs/([^/]+)\.md$", t)
                if not mm:
                    continue
                name = mm.group(1)
                if name in NOT_PLAYER or name.startswith("RELEASE_NOTES_") or name in names:
                    continue
                self.add("README.md", i + 1, "package",
                         "docs/%s.md (README links it, not in pkg_player_docs)" % name, line)


def main():
    ap = argparse.ArgumentParser(description="The documentation gate (see the docstring).")
    ap.add_argument("--root", default=ROOT, help="the repository root (default: this tree)")
    ap.add_argument("--allow", default=None,
                    help="the allow-list (default: tools/check_docs_allow.txt under the root)")
    ap.add_argument("--summary", action="store_true",
                    help="also print the number of findings per file and gate")
    ap.add_argument("paths", nargs="*", help="only these docs (paths from the root)")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    allow = Allow(args.allow or os.path.join(root, "tools", "check_docs_allow.txt"))
    names = player_docs(root)
    player = {"README.md"} | {"docs/%s.md" % n for n in names}
    scope = doc_files(root)
    files = scope
    if args.paths:
        files = []
        for p in args.paths:
            rel = os.path.relpath(os.path.abspath(p), root).replace(os.sep, "/")
            if rel not in scope:
                print("check_docs: %s is not in scope (README.md, docs/*.md, docs/port/*.md)" % p,
                      file=sys.stderr)
                return 2
            files.append(rel)

    c = Checker(root, allow)
    for rel in files:
        c.check_file(rel, player)
    if not args.paths:
        c.check_package(names)

    for rel, n, gate, text in c.findings:
        print("%s:%d: %s: %s" % (rel, n, gate, text))
    if args.summary:
        counts = {}
        for rel, _, gate, _ in c.findings:
            counts[(rel, gate)] = counts.get((rel, gate), 0) + 1
        for (rel, gate), k in sorted(counts.items()):
            print("summary: %s: %s -> %d" % (rel, gate, k))
    if c.findings:
        print("check_docs: %d finding(s) in %d file(s)"
              % (len(c.findings), len({f[0] for f in c.findings})), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
