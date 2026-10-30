#!/usr/bin/env python3
"""tools/spirv_header_diff.py OLD_DIR NEW_DIR

Compares two sets of SPIR-V modules (the *.spv files directly under each
directory, matched by file name) with the header's version word (word 1)
masked: every other word, the header's magic, generator, bound and schema
included, must be equal.  Written for the Vulkan 1.0 retarget (issue 158,
cmake/IcoShaders.cmake -fspv-target-env=vulkan1.0): copy a build's
CMAKE_BINARY_DIR/shaders/*.spv aside before the rebuild, rebuild, and run
this over the two directories; it proves the instruction streams are the
ones the earlier target emitted, so only the version the driver reads moved.

Prints one line per difference (`name: text`), then a summary with the
version words seen on each side; the exit status is 1 when a module differs
or is present on one side only, 0 otherwise.
"""
import os
import struct
import sys

MAGIC = 0x07230203


def words(path):
    """the module's 32-bit words, or a string saying why not"""
    data = open(path, "rb").read()
    if len(data) < 20 or len(data) % 4:
        return "not a SPIR-V module (%d bytes)" % len(data)
    w = struct.unpack("<%dI" % (len(data) // 4), data)
    if w[0] != MAGIC:
        return "not a SPIR-V module (magic 0x%08x)" % w[0]
    return w


def spv_names(d):
    return {n for n in os.listdir(d) if n.endswith(".spv") and os.path.isfile(os.path.join(d, n))}


def version_text(v):
    return "%d.%d" % ((v >> 16) & 0xFF, (v >> 8) & 0xFF)


def main(argv):
    if len(argv) != 3 or not all(os.path.isdir(d) for d in argv[1:]):
        print(__doc__)
        return 2
    old_dir, new_dir = argv[1], argv[2]
    old, new = spv_names(old_dir), spv_names(new_dir)
    findings = 0
    for n in sorted(old - new):
        print("%s: only in %s" % (n, old_dir))
        findings += 1
    for n in sorted(new - old):
        print("%s: only in %s" % (n, new_dir))
        findings += 1
    versions = ({}, {})
    same = 0
    for n in sorted(old & new):
        a = words(os.path.join(old_dir, n))
        b = words(os.path.join(new_dir, n))
        bad = [t for t in (a, b) if isinstance(t, str)]
        if bad:
            print("%s: %s" % (n, bad[0]))
            findings += 1
            continue
        for side, w in zip(versions, (a, b)):
            side[w[1]] = side.get(w[1], 0) + 1
        if len(a) != len(b):
            print("%s: %d words, now %d" % (n, len(a), len(b)))
            findings += 1
            continue
        diff = [i for i in range(len(a)) if i != 1 and a[i] != b[i]]
        if diff:
            print("%s: %d words differ outside the version word (first at word %d: "
                  "0x%08x, now 0x%08x)" % (n, len(diff), diff[0], a[diff[0]], b[diff[0]]))
            findings += 1
            continue
        same += 1
    for label, side in zip(("old", "new"), versions):
        print("%s versions: %s" % (label, ", ".join(
            "%s x%d" % (version_text(v), c) for v, c in sorted(side.items())) or "none"))
    print("spirv_header_diff: %d modules compared, %d identical with the version masked, "
          "%d findings" % (len(old & new), same, findings))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
