#!/usr/bin/env python3
"""check_rc_text.py: the rc-only tooling stays out of every release.

The release candidates' tooling (CMake ICO_RC_TOOLS, Gradle icoRcTools;
docs/BUILDING.md, "Release candidate tools") is compiled for a package
label ending in -rc<N> and for nothing else. Its mark is the English text of
the Main row it adds, "Share a bug report" (port/ui/strings_en.c
UI_STR_OPT_SHARE_REPORT). The package scripts (tools/package_*.sh) take
their switch from here and check what they built with it:

    check_rc_text.py --cmake-flag LABEL     prints -DICO_RC_TOOLS=ON or =OFF
    check_rc_text.py --gradle-flag LABEL    prints -PicoRcTools=true or =false
    check_rc_text.py --label LABEL FILE...  an rc label: every FILE holds the
                                            text; any other label (or ""):
                                            no FILE holds it
    check_rc_text.py --expect present|absent FILE...
    check_rc_text.py --selftest             this file's own cases, and the
                                            package scripts, CMakeLists.txt
                                            and app/build.gradle using it

A FILE that is a zip (an APK, an IPA, a zip) is read entry by entry, each
inflated; any other file is read as bytes. "Holds" is the text in UTF-8,
or, for the absent check, in UTF-16 (either byte order) too. Exit 0 when
the check passes, 1 when it fails (each file named), 2 for a bad call.
"""
import os
import re
import subprocess
import sys
import tempfile
import zipfile

TEXT = "Share a bug report"
RC_LABEL = re.compile(r"-rc[0-9]+$")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def is_rc(label):
    return bool(RC_LABEL.search(label or ""))


def needles(utf16):
    out = [TEXT.encode("utf-8")]
    if utf16:
        out += [TEXT.encode("utf-16-le"), TEXT.encode("utf-16-be")]
    return out


def hits(path, utf16):
    """The places in path that hold the text: [path] or [path!entry, ...]."""
    found = []
    keys = needles(utf16)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                data = z.read(info)
                if any(k in data for k in keys):
                    found.append("%s!%s" % (path, info.filename))
        return found
    with open(path, "rb") as f:
        data = f.read()
    return [path] if any(k in data for k in keys) else []


def check(expect, files):
    bad = 0
    for path in files:
        if not os.path.isfile(path):
            print("check_rc_text: no file %s" % path)
            bad += 1
            continue
        h = hits(path, utf16=expect == "absent")
        if expect == "absent" and h:
            for where in h:
                print("check_rc_text: FAIL: %s holds \"%s\": the rc-only tooling is in a "
                      "build that is not a release candidate's" % (where, TEXT))
            bad += 1
        elif expect == "present" and not h:
            print("check_rc_text: FAIL: %s lacks \"%s\": an rc package without its rc "
                  "tooling (ICO_RC_TOOLS not on)" % (path, TEXT))
            bad += 1
        else:
            print("check_rc_text: %s: \"%s\" %s" % (path, TEXT, expect))
    return 1 if bad else 0


def selftest():
    fails = []

    def ok(cond, what):
        if not cond:
            fails.append(what)

    for label, want in [("v0.8.5-rc1", True), ("v0.8.5-rc12", True), ("v0.8.2-rc0", True),
                        ("v0.8.5", False), ("v0.8.5-rc", False), ("test-rc1-x", False),
                        ("", False), (None, False), ("v0.4.4-handprobe", False)]:
        ok(is_rc(label) == want, "is_rc(%r) is %s" % (label, not want))
    with tempfile.TemporaryDirectory() as d:
        def put(name, data):
            p = os.path.join(d, name)
            with open(p, "wb") as f:
                f.write(data)
            return p

        plain = put("plain.bin", b"\x7fELF" + b"\0" * 64 + b"Save a picture for a bug report\0")
        marked = put("marked.bin", b"\x7fELF" + b"\0" * 64 + TEXT.encode() + b"\0")
        wide = put("wide.bin", b"\0" * 16 + TEXT.encode("utf-16-le") + b"\0\0")
        zin = os.path.join(d, "marked.apk")
        with zipfile.ZipFile(zin, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("classes.dex", b"dex\n035\0" + b"x" * 100)
            z.writestr("lib/arm64-v8a/libmain.so", b"\x7fELF" + b"y" * 4000 + TEXT.encode())
        zout = os.path.join(d, "plain.apk")
        with zipfile.ZipFile(zout, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("classes.dex", b"dex\n035\0" + b"x" * 100)
            z.writestr("resources.arsc", "Share a bug".encode("utf-16-le"))
        zwide = os.path.join(d, "wide.apk")
        with zipfile.ZipFile(zwide, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("resources.arsc", TEXT.encode("utf-16-le"))
        ok(hits(plain, True) == [], "a file without the text")
        ok(hits(marked, False) == [marked], "a file with the text")
        ok(hits(wide, True) == [wide] and hits(wide, False) == [],
           "UTF-16 is looked for in the absent check alone")
        ok(hits(zin, False) == [zin + "!lib/arm64-v8a/libmain.so"], "an inflated zip entry")
        ok(hits(zout, True) == [], "a zip without the text")
        ok(hits(zwide, True) == [zwide + "!resources.arsc"], "a UTF-16 zip entry")
        ok(check("absent", [plain, zout]) == 0, "absent passes on the plain files")
        ok(check("absent", [plain, marked]) == 1, "absent fails on a marked file")
        ok(check("absent", [zwide]) == 1, "absent fails on a UTF-16 entry")
        ok(check("present", [marked, zin]) == 0, "present passes on the marked files")
        ok(check("present", [plain]) == 1, "present fails on a plain file")
        ok(check("absent", [os.path.join(d, "missing")]) == 1, "a missing file fails")
        me = os.path.abspath(__file__)
        for args, rc in [(["--label", "v0.8.5", plain], 0), (["--label", "v0.8.5", marked], 1),
                         (["--label", "v0.8.5-rc2", marked], 0),
                         (["--label", "v0.8.5-rc2", plain], 1), (["--label", "", marked], 1)]:
            r = subprocess.run([sys.executable, "-I", me] + args, capture_output=True)
            ok(r.returncode == rc, "%s exits %d (%d)" % (args[:2], rc, r.returncode))
    for flag, label, want in [("--cmake-flag", "v0.8.5-rc1", "-DICO_RC_TOOLS=ON"),
                              ("--cmake-flag", "v0.8.5", "-DICO_RC_TOOLS=OFF"),
                              ("--cmake-flag", "", "-DICO_RC_TOOLS=OFF"),
                              ("--gradle-flag", "v0.8.5-rc3", "-PicoRcTools=true"),
                              ("--gradle-flag", "v0.8.5", "-PicoRcTools=false")]:
        r = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), flag, label],
                           capture_output=True, text=True)
        ok(r.returncode == 0 and r.stdout.strip() == want, "%s %r prints %s (%r)"
           % (flag, label, want, r.stdout.strip()))

    # the row's English text is this file's mark
    def read(rel):
        with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
            return f.read()

    en = read("port/ui/strings_en.c")
    m = re.search(r'\[UI_STR_OPT_SHARE_REPORT\]\s*=\s*"([^"]*)"', en)
    ok(m is not None and m.group(1) == TEXT,
       "strings_en.c's UI_STR_OPT_SHARE_REPORT is \"%s\"" % TEXT)
    ok(re.search(r"#if ICO_RC_TOOLS\s*\n\s*\[UI_STR_OPT_SHARE_REPORT\]", en) is not None,
       "strings_en.c's row text is inside #if ICO_RC_TOOLS")
    # the switch is off unless asked for, and asked for by the label alone
    cm = read("CMakeLists.txt")
    ok(re.search(r'option\(ICO_RC_TOOLS "[^"]*" OFF\)', cm) is not None,
       "CMakeLists.txt: option(ICO_RC_TOOLS ... OFF)")
    ok('MATCHES "-rc[0-9]+$"' in cm, "CMakeLists.txt refuses ICO_RC_TOOLS with a release label")
    gr = read("android/app/build.gradle")
    ok("findProperty('icoRcTools') ?: 'false'" in gr, "build.gradle: icoRcTools false by default")
    ok("/.*-rc[0-9]+/" in gr, "build.gradle refuses icoRcTools with a release label")
    ok('android:enabled="${icoRcTools}"' in read("android/app/src/main/AndroidManifest.xml"),
       "the manifest's report provider is enabled by icoRcTools alone")
    for script, flag in [("package_linux.sh", "--cmake-flag"), ("package_win.sh", "--cmake-flag"),
                         ("package_macos.sh", "--cmake-flag"), ("package_ios.sh", "--cmake-flag"),
                         ("package_android.sh", "--gradle-flag")]:
        s = read("tools/" + script)
        ok("check_rc_text.py" in s and flag in s and "--label" in s,
           "tools/%s takes %s from check_rc_text.py and checks its program with --label"
           % (script, flag))
        ok(re.search(r"ICO_RC_TOOLS=(ON|1)|icoRcTools=true", s) is None,
           "tools/%s never turns the rc tooling on by itself" % script)
    for f in fails:
        print("check_rc_text: selftest FAIL: %s" % f)
    print("check_rc_text: selftest %s" % ("ok" if not fails else "%d failures" % len(fails)))
    return 1 if fails else 0


def main(argv):
    if argv[:1] == ["--selftest"] and len(argv) == 1:
        return selftest()
    if len(argv) == 2 and argv[0] == "--cmake-flag":
        print("-DICO_RC_TOOLS=%s" % ("ON" if is_rc(argv[1]) else "OFF"))
        return 0
    if len(argv) == 2 and argv[0] == "--gradle-flag":
        print("-PicoRcTools=%s" % ("true" if is_rc(argv[1]) else "false"))
        return 0
    if len(argv) >= 3 and argv[0] == "--label":
        return check("present" if is_rc(argv[1]) else "absent", argv[2:])
    if len(argv) >= 3 and argv[0] == "--expect" and argv[1] in ("present", "absent"):
        return check(argv[1], argv[2:])
    print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
