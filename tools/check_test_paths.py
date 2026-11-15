#!/usr/bin/env python3
"""check_test_paths.py: no test may read anything from outside the repository.

The owner's rule (v0.11.7, docs/port/TESTING_FIXTURES.md): every test gets its
input from the repository.  This guard reads every test list and test script
and fails on any line that names a path outside the tree, printing the file,
the line number, the reason and the line.

Scope (the files a ctest reads or runs) and the rules each kind gets:
  - lists and CMake (every rule below): the lists under tools/ (tools/*.txt)
    and under a port/**/test/ folder (*.txt), every CMake file
    (CMakeLists.txt, cmake/*.cmake, port/**/*.cmake, port/**/CMakeLists.txt,
    the -P test scripts under port/**/test/);
  - code (the data rules only: a dump, a picture or a machine-local folder):
    the C sources under port/**/test/, everything under tools/test/, and the
    tools/*.py scripts a CMake file names (the Python tests).  A unit test
    may hold a made-up path as data ("/home/x/.config") that it never reads.

Rules:
  absolute   a token starting with '/' with two or more components, other
             than the operating system's own places (/usr/, /proc/, /sys/,
             /dev/, /etc/, /bin/, /lib: drivers and kernel files, never test
             input), or a drive path (C:\\, C:/)
  home       ~/, $ENV{HOME}, $HOME/, %USERPROFILE%
  climbs     a relative path with '..' components that, resolved against the
             file's own folder (or ${CMAKE_SOURCE_DIR} / ${CMAKE_CURRENT_
             SOURCE_DIR} / ${CMAKE_CURRENT_LIST_DIR}), leaves the tree
  local      the machine-local dump folders: ico-dumps, build-host/tmp
  (code)     only the absolute paths under the build machine's /primary/ or
             /tmp/ and the local folders, in string literals
A comment line of a registration CMake file (CMakeLists.txt, a .cmake file
outside test/) is skipped: it documents, its commands are what a test reads;
a -P test script's and a list's comments are checked like their other lines.
A line carrying the marker `test-paths: ok` is skipped (a refusal message or
a regex that spells a path in order to reject it); so is a CMake regex
(the text after MATCHES or REGEX).

tools/check_test_paths_allow.txt names the files not converted yet (one
repo-relative path a line, `#` comments).  An allow-listed file is skipped;
the guard FAILS when an allow-listed file no longer has a hit (take it off
the list: it only shrinks) or does not exist.

  python3 -I tools/check_test_paths.py [ROOT]     check the tree
  python3 -I tools/check_test_paths.py --selftest the rules on fixed lines
Exit 0 clean, 1 on any hit or stale allow-list entry.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW = "tools/check_test_paths_allow.txt"

TOK = r"(?:^|[\s\"'(=;,\[|])"
ABS_RE = re.compile(TOK + r"(/[A-Za-z0-9_.+~-]+(?:/[A-Za-z0-9_.*+~-]*)+)")
DRIVE_RE = re.compile(r"(?:^|[\s\"=(;,])([A-Z]:[\\/][A-Za-z0-9_])")
HOME_RE = re.compile(TOK + r"(~/|\$ENV\{HOME\}|\$HOME/|%USERPROFILE%)")
REL_RE = re.compile(TOK + r"((?:\$\{[A-Z_]+\}/)?(?:[A-Za-z0-9_.+-]+/)*\.\.(?:/[A-Za-z0-9_.*+-]*)*)"
                    r"(?=$|[\s\"')])")
STR_RE = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")
# code: the build machine's own folders (a unit test's made-up "/home/x" is data)
CODE_ROOTS = ("/primary/", "/tmp/")
LOCAL_RE = re.compile(r"(ico-dumps(?=[/\\\"' )]|$)|build-host[/\\]tmp)")
SYSTEM = ("/usr/", "/proc/", "/sys/", "/dev/", "/etc/", "/bin/", "/lib")
OK_MARK = "test-paths: ok"
REGEX_RE = re.compile(r"\b(MATCHES|REGEX)\b")


def resolve_climb(tok, filedir, root):
    """True when the '..' path tok leaves root; None when it cannot be told"""
    base = filedir
    m = re.match(r"\$\{([A-Z_]+)\}/(.*)$", tok)
    if m:
        var, tok = m.group(1), m.group(2)
        if var == "CMAKE_SOURCE_DIR":
            base = root
        elif var not in ("CMAKE_CURRENT_SOURCE_DIR", "CMAKE_CURRENT_LIST_DIR"):
            return None
    if "${" in tok:
        return None
    p = os.path.normpath(os.path.join(base, tok))
    return not (p == root or p.startswith(root + os.sep))


def line_hits(line, kind="list", filedir=None, root=None):
    """the reasons a line breaks the rule (empty: clean).  kind: 'list'
    (lists, CMake) or 'code' (the data rules only)"""
    if OK_MARK in line:
        return []
    r = REGEX_RE.search(line)
    if r:
        line = line[:r.start()]
    why = []
    if kind == "code":
        # code: only what its string literals name (a comment may cite where
        # evidence came from; a unit test's made-up paths are data)
        line = " ".join('"%s"' % m.group(1) for m in STR_RE.finditer(line))
    for m in ABS_RE.finditer(line):
        p = m.group(1)
        if p.startswith(SYSTEM):
            continue
        if kind == "code" and not p.startswith(CODE_ROOTS):
            continue
        why.append("absolute path " + p)
    if kind == "list":
        for m in DRIVE_RE.finditer(line):
            why.append("drive path " + m.group(1))
        for m in HOME_RE.finditer(line):
            why.append("home folder " + m.group(1))
        if filedir is not None:
            for m in REL_RE.finditer(line):
                if resolve_climb(m.group(1), filedir, root):
                    why.append("climbs out of the tree " + m.group(1))
    m = LOCAL_RE.search(line)
    if m:
        why.append("machine-local folder " + m.group(1))
    return why


def read(root, rel):
    try:
        with open(os.path.join(root, rel), "rb") as fp:
            return fp.read().decode("utf-8", errors="replace")
    except OSError:
        return ""


def scope(root):
    """{repo-relative path: kind} of every file in scope"""
    out = {}

    def walk(top):
        base = os.path.join(root, top)
        for dp, dn, fn in os.walk(base):
            dn[:] = [d for d in dn if d not in (".git", "third_party")]
            for f in fn:
                yield os.path.relpath(os.path.join(dp, f), root).replace(os.sep, "/")

    for rel in walk("port"):
        if rel.endswith(".cmake") or rel.endswith("/CMakeLists.txt"):
            out[rel] = "list"
        elif "/test/" in rel and rel.endswith(".txt"):
            out[rel] = "list"
        elif "/test/" in rel and rel.endswith((".c", ".h", ".py", ".sh")):
            out[rel] = "code"
    for rel in walk("tools/test"):
        if rel.endswith((".py", ".sh", ".c", ".txt", ".cmake")):
            out[rel] = "code"
    for rel in walk("cmake"):
        if rel.endswith(".cmake"):
            out[rel] = "list"
    if os.path.isfile(os.path.join(root, "CMakeLists.txt")):
        out["CMakeLists.txt"] = "list"
    tools = os.path.join(root, "tools")
    if os.path.isdir(tools):
        for f in sorted(os.listdir(tools)):
            if f.endswith(".txt") and "tools/" + f != ALLOW:
                out["tools/" + f] = "list"
    named = re.compile(r"tools/([A-Za-z0-9_]+\.py)")
    for rel, kind in list(out.items()):
        if kind == "list" and rel.endswith((".cmake", "CMakeLists.txt")):
            code = "\n".join(l for l in read(root, rel).splitlines()
                             if not l.lstrip().startswith("#"))
            for m in named.finditer(code):
                if os.path.isfile(os.path.join(root, "tools", m.group(1))):
                    out.setdefault("tools/" + m.group(1), "code")
    out.pop("tools/check_test_paths.py", None)
    return out


def registration(rel):
    """a CMake file that registers tests (not a -P test script under test/)"""
    return rel.endswith(("CMakeLists.txt", ".cmake")) and "/test/" not in rel


def file_hits(root, rel, kind):
    hits = []
    filedir = os.path.dirname(os.path.join(root, rel))
    for n, line in enumerate(read(root, rel).splitlines(), 1):
        if registration(rel) and line.lstrip().startswith("#"):
            continue  # a registration file's comments document; its commands are checked
        for w in line_hits(line, kind, filedir, root):
            hits.append((n, w, line.strip()))
    return hits


def allow_list(root):
    out = []
    for line in read(root, ALLOW).splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            out.append(line)
    return out


def check(root):
    root = os.path.abspath(root)
    allowed = allow_list(root)
    files = scope(root)
    bad = 0
    for rel in sorted(files):
        if rel in allowed:
            continue
        for n, why, text in file_hits(root, rel, files[rel]):
            print("%s:%d: %s: %s" % (rel, n, why, text[:160]))
            bad += 1
    for rel in allowed:
        if rel not in files:
            print("%s: on %s but it is no file in scope (gone or renamed): take it off the "
                  "list" % (rel, ALLOW))
            bad += 1
        elif not file_hits(root, rel, files[rel]):
            print("%s: on %s but it names no path outside the repository any more: take it "
                  "off the list (the list only shrinks)" % (rel, ALLOW))
            bad += 1
    print("check_test_paths: %d files in scope, %d on the allow-list (not converted yet), %d "
          "problems" % (len(files), len(allowed), bad))
    return 1 if bad else 0


def selftest():
    root = "/r"
    d = "/r/port/render/test"
    cases = [
        ("/primary/dev/ico-dumps/a.rddump 1 3", "list", True),
        ('  "-DLIST=${CMAKE_SOURCE_DIR}/tools/x.txt"', "list", False),
        ('if(NOT IS_DIRECTORY "/primary/dev/ico-dumps")', "list", True),
        ("set(x ../../../../outside/a.rddump)", "list", True),
        ("# beside each program... (pass, sample, line, ...)", "list", False),
        ('ico_diag_note("/logs/loss-hang.zip");', "code", False),
        ('/* build-host/tmp/v0117/evidence/x.log */', "code", False),
        ("add_executable(t test/t.c ../platform/x.c)", "list", False),
        ('"${CMAKE_SOURCE_DIR}/../elsewhere/a.rddump"', "list", True),
        ("fx-shadow-wide", "list", False),
        ('file(GLOB found "${l}")', "list", False),
        ('if(l MATCHES "/tools/toolchain/ccache/")', "list", False),
        ('COMMAND sh -c "x > /dev/null"', "list", False),
        ('"-DLVP=/usr/share/vulkan/icd.d/lvp_icd.json"', "list", False),
        ("C:\\dumps\\a.rddump", "list", True),
        ('"pass\'s:\\n  plain"', "list", False),
        ("$ENV{HOME}/dumps", "list", True),
        ("build-host/tmp/v0117/a.zip", "list", True),
        ("# see https://github.com/x/y", "list", False),
        ('message(FATAL_ERROR "/abs/path refused") # test-paths: ok', "list", False),
        ("/tmp/claude/a.rddump", "list", True),
        ('cfg_set("/home/x/.config/ico")', "code", False),
        ('load("/primary/dev/ico-dumps/x.rddump")', "code", True),
        ('open("build-host/tmp/corpus/a.rddump")', "code", True),
        ('for pre in ("ico-dumps-", "102-"):', "code", False),
        ("# <build>/menu_look/<lang>/ and <tag>/glslang/bin", "list", False),
    ]
    bad = 0
    for line, kind, want in cases:
        got = bool(line_hits(line, kind, d, root))
        if got != want:
            print("selftest: %r (%s): %s, expected %s" % (line, kind, got, want))
            bad += 1
    print("check_test_paths selftest: %d of %d cases right" % (len(cases) - bad, len(cases)))
    return 1 if bad else 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        return selftest()
    return check(sys.argv[1] if len(sys.argv) > 1 else ROOT)


if __name__ == "__main__":
    sys.exit(main())
