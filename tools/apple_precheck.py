#!/usr/bin/env python3
"""apple_precheck.py: the Apple builds' Vulkan backend, checked on Linux.

The macOS and iOS builds link the Vulkan backend to MoltenVK directly
(ICO_RHI_MOLTENVK, port/rhi/CMakeLists.txt): every vk* entry point is a
declared function there, where every other build reaches it through volk's
function pointers (VK_NO_PROTOTYPES). Code that is fine with a pointer can
fail only on Apple: `vkFoo != NULL` is an always-true comparison of a
function (-Werror,-Wtautological-pointer-compare under clang), `vkFoo = NULL`
does not compile, and an entry point MoltenVK does not export fails the link.
No Linux build sees these; the Apple CI jobs do, an hour or more later.

This script compiles the backend's sources (the `_ico_vk_sources` list of
port/rhi/CMakeLists.txt, Android's own file left out) the way the Apple
builds do, with clang on this host: ICO_RHI_MOLTENVK=1, no VK_NO_PROTOTYPES,
the vendored Vulkan-Headers (tools/toolchain/deps/vulkan-headers; MoltenVK
ships the Khronos headers too) and the port's warnings as errors
(cmake/IcoFlags.cmake ICO_PORT_WARNINGS, ICO_SEMANTIC_OPTIONS). It needs no
Apple SDK and runs no CMake.

  apple_precheck.py [--root DIR] [--clang CLANG]
      -fsyntax-only on each source; exit 1 on any error.
  apple_precheck.py --symbols-against REF [--root DIR] [--clang CLANG]
      also compiles each source to an object in a scratch folder (under
      $TMPDIR) and lists the vk* entry points the backend calls directly
      that the tree at git REF (a release whose Apple packages linked) did
      not: each is a function MoltenVK must export, or the Apple link fails.
      Listing one is not a failure; check it against MoltenVK's exports.

What it does not cover: Apple's own clang (its warnings can differ from
this host's clang), code under __APPLE__ or ICO_IOS (it needs Apple's
headers), the Objective-C file, the link and the packaging. The Apple CI
jobs (.github/workflows/apple-build.yml) are the gate; this is the early look.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PORT_WARNINGS = ["-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror",
                 "-Werror=return-type", "-Werror=implicit-function-declaration",
                 "-Werror=strict-prototypes"]
SEMANTIC = ["-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off", "-fno-fast-math",
            "-fsigned-char", "-fno-common", "-fgnu89-inline"]
# Apple-only by construction or another platform's: not compiled for Apple
SKIP = {"vk/vk_surface_android.c"}


def vk_sources(root):
    text = open(os.path.join(root, "port/rhi/CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"set\(_ico_vk_sources\s+([^)]*)\)", text)
    if not m:
        sys.exit("apple_precheck: no _ico_vk_sources list in port/rhi/CMakeLists.txt")
    return [s for s in m.group(1).split() if s not in SKIP]


def find_clang(name):
    if name:
        return name
    for c in ["clang", "clang-21", "clang-20", "clang-19", "clang-18"]:
        if shutil.which(c):
            return c
    sys.exit("apple_precheck: no clang on PATH (--clang names one)")


def flags(root, deps):
    inc = [os.path.join(root, "port/rhi"), os.path.join(deps, "vulkan-headers/include")]
    sdl = os.path.join(deps, "sdl3/linux-x64/include")
    out = ["-std=gnu11", "-DICO_RHI_HAVE_SDL=1", "-DRHI_BACKEND_PREFIX=vk",
           "-DICO_RHI_MOLTENVK=1"] + SEMANTIC
    for i in inc:
        if not os.path.isdir(i):
            sys.exit(f"apple_precheck: {i} is missing (tools/fetch_toolchain.sh)")
        out.append("-I" + i)
    if os.path.isdir(sdl):
        out.append("-I" + sdl)
    return out


def check(root, clang, deps):
    bad = 0
    srcs = vk_sources(root)
    for s in srcs:
        path = os.path.join(root, "port/rhi", s)
        p = subprocess.run([clang, "-fsyntax-only"] + flags(root, deps) + PORT_WARNINGS + [path],
                           capture_output=True, text=True)
        if p.returncode != 0:
            bad += 1
            sys.stdout.write(p.stderr)
            print(f"apple_precheck: FAIL {s}")
    print(f"apple_precheck: {len(srcs) - bad} of {len(srcs)} sources of the Vulkan backend "
          f"compile as the Apple builds compile them ({clang})")
    return bad == 0


def entry_points(root, clang, deps, scratch, nm):
    names = set()
    obj = os.path.join(scratch, "o.o")
    for s in vk_sources(root):
        path = os.path.join(root, "port/rhi", s)
        if not os.path.exists(path):
            continue
        p = subprocess.run([clang, "-c", "-w", "-o", obj] + flags(root, deps) + [path],
                           capture_output=True, text=True)
        if p.returncode != 0:
            sys.stdout.write(p.stderr)
            sys.exit(f"apple_precheck: {path} does not compile")
        out = subprocess.run([nm, "-u", obj], capture_output=True, text=True, check=True).stdout
        for line in out.split():
            if re.fullmatch(r"_?vk[A-Z]\w*", line):
                names.add(line.lstrip("_"))
    return names


def tree_at(ref, dest):
    tar = os.path.join(dest, "t.tar")
    with open(tar, "wb") as f:
        subprocess.run(["git", "-C", ROOT, "archive", ref, "port/rhi", "port/include"],
                       stdout=f, check=True)
    with tarfile.open(tar) as t:
        t.extractall(os.path.join(dest, "tree"), filter="data")
    return os.path.join(dest, "tree")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=ROOT, help="the tree to check (default: this one)")
    ap.add_argument("--clang", help="the clang to run (default: the first on PATH)")
    ap.add_argument("--symbols-against", metavar="REF",
                    help="list the vk* entry points called directly that REF's tree did not call")
    a = ap.parse_args()
    clang = find_clang(a.clang)
    # the vendored headers come from this checkout's toolchain (a worktree's
    # tools/toolchain may be a link to the main checkout's)
    deps = os.path.join(ROOT, "tools/toolchain/deps")
    ok = check(a.root, clang, deps)
    if a.symbols_against:
        nm = shutil.which("llvm-nm") or shutil.which("nm")
        if not nm:
            sys.exit("apple_precheck: no nm on PATH")
        with tempfile.TemporaryDirectory(prefix="apple_precheck.") as scratch:
            now = entry_points(a.root, clang, deps, scratch, nm)
            was = entry_points(tree_at(a.symbols_against, scratch), clang, deps, scratch, nm)
        added = sorted(now - was)
        print(f"apple_precheck: {len(now)} vk* entry points called directly; "
              f"{len(added)} not called at {a.symbols_against}" + (":" if added else ""))
        for n in added:
            print("  " + n)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
