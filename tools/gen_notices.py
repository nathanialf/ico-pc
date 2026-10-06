#!/usr/bin/env python3
"""tools/gen_notices.py --platform {linux,windows} --out NOTICES.txt [--root DIR]

Writes the third-party notices file a package ships beside the program:
every component in tools/notices/manifest.json for that platform, with its
version, licence, what it is used for and its full licence text(s). The
program's own licence is LICENSE, shipped next to it.

Fails (exit 1) if any text the manifest names is missing, so a package
cannot ship without a notice. docs/THIRD_PARTY.md lists the
components and why each is there.
"""

import argparse
import json
import os
import re
import sys

RULE = "=" * 78
SUB = "-" * 78


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def extract(root, spec):
    lines = read(root, spec["file"]).split("\n")
    start = next((i for i, l in enumerate(lines) if spec["from"] in l), None)
    if start is None:
        raise ValueError(f"{spec['file']}: no line with {spec['from']!r}")
    end = next((i for i in range(start, len(lines)) if spec["to"] in lines[i]), None)
    if end is None:
        raise ValueError(f"{spec['file']}: no line with {spec['to']!r} after {spec['from']!r}")
    out = [re.sub(r"^\s*\*( |$)", "", l).rstrip() for l in lines[start : end + 1]]
    return "\n".join(out).strip("\n") + "\n"


def text_of(root, t):
    if "file" in t:
        path = os.path.join(root, t["file"])
        if not os.path.isfile(path):
            raise FileNotFoundError(t["file"])
        return read(root, t["file"])
    if "any_of" in t:
        for rel in t["any_of"]:
            if os.path.isfile(os.path.join(root, rel)):
                return read(root, rel)
        raise FileNotFoundError(" or ".join(t["any_of"]))
    if "extract" in t:
        return extract(root, t["extract"])
    raise ValueError(f"unknown text kind: {t}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", required=True, choices=["linux", "windows"])
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--manifest", default=None)
    a = ap.parse_args()

    root = os.path.abspath(a.root)
    manifest = a.manifest or os.path.join(root, "tools", "notices", "manifest.json")
    with open(manifest, encoding="utf-8") as f:
        comps = json.load(f)["components"]
    comps = [c for c in comps if a.platform in c.get("platforms", ["linux", "windows"])]

    parts = [
        "ICO PC port: third-party notices\n",
        "The program is distributed under the MIT licence in LICENSE. It contains",
        "or ships with the following third-party components, whose licences",
        "require (or whose authors ask) that these notices go with it.\n",
    ]
    for c in comps:
        parts.append(f"  - {c['name']} ({c['licence']})")
    parts.append("")
    try:
        for c in comps:
            parts += [
                RULE,
                c["name"],
                f"Version: {c['version']}",
                f"Licence: {c['licence']}",
                f"Used for: {c['used_for']}",
                RULE,
                "",
            ]
            for i, t in enumerate(c["texts"]):
                if i:
                    parts += [SUB, ""]
                parts.append(text_of(root, t).rstrip("\n"))
                parts.append("")
    except (OSError, ValueError) as e:
        print(f"gen_notices: missing licence text: {e}", file=sys.stderr)
        return 1

    with open(a.out, "w", encoding="utf-8", newline="\r\n" if a.platform == "windows" else "\n") as f:
        f.write("\n".join(parts))
    print(f"gen_notices: {len(comps)} components -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
