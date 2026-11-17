#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a release's assets from a tag checkout: runs o3-sdk's tools/gate.py on this tree, and only on a CANDIDATE verdict writes
<out>/o3-303-<tag>.ptf, <out>/SHA256SUMS and <out>/notes.md (docs/release-notes/<tag>.md with its @PLACEHOLDERS@ filled).

    python3 -I tools/release_assets.py <tag> --sdk DIR --arm DIR [--out DIR] [--jobs N]

The host ctest is not part of this run (--skip-host): the release CI's ci (host) job runs it on the same commit before this one.
Never flashes and never uploads; .github/workflows/release-ci.yml publishes what it writes.
"""
import argparse
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREFIX = "build-gate-release"


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), check=True, stdout=subprocess.PIPE, text=True).stdout.strip()


def main(argv):
    parser = argparse.ArgumentParser(prog="release_assets.py")
    parser.add_argument("tag", help="the release tag, v<major>.<minor>.<patch>, checked out at HEAD")
    parser.add_argument("--sdk", required=True, help="the o3-sdk checkout at tools/o3sdk.ref")
    parser.add_argument("--arm", required=True, help="the Arm GNU Toolchain 15.3.Rel1 directory")
    parser.add_argument("--out", default="release")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args(argv)

    if not re.match(r"^v[0-9]+\.[0-9]+\.[0-9]+$", args.tag):
        print("release_assets.py: the tag must read v<major>.<minor>.<patch>", file=sys.stderr)
        return 2
    head = git("rev-parse", "HEAD")
    if git("rev-parse", args.tag + "^{commit}") != head:
        print("release_assets.py: %s is not the commit checked out" % args.tag, file=sys.stderr)
        return 2
    template = os.path.join(ROOT, "docs", "release-notes", args.tag + ".md")
    if not os.path.isfile(template):
        print("release_assets.py: no %s" % os.path.relpath(template, ROOT), file=sys.stderr)
        return 2
    sdk = os.path.abspath(args.sdk)
    pinned = open(os.path.join(ROOT, "tools", "o3sdk.ref"), encoding="ascii").read().strip()
    sdk_head = subprocess.run(["git", "-C", sdk, "rev-parse", "HEAD"], check=True, stdout=subprocess.PIPE, text=True).stdout.strip()
    if sdk_head != pinned:
        print("release_assets.py: o3-sdk is at %s, tools/o3sdk.ref names %s" % (sdk_head, pinned), file=sys.stderr)
        return 2

    spec = importlib.util.spec_from_file_location("o3sdk_gate", os.path.join(sdk, "tools", "gate.py"))
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    gate.ROOT = ROOT
    rc = gate.main(["o3_303", "--sdk", sdk, "--arm", os.path.abspath(args.arm), "--prefix", PREFIX, "--jobs", str(args.jobs),
                    "--skip-host"])
    build = os.path.join(ROOT, PREFIX + "-a")
    with open(os.path.join(build, "CANDIDATE.md"), encoding="utf-8") as f:
        manifest = f.read()
    if rc != 0 or "Verdict: **CANDIDATE**" not in manifest:
        print("release_assets.py: the gate did not give a CANDIDATE (rc=%d); nothing written" % rc, file=sys.stderr)
        return 1

    label = gate.image_label(os.path.join(build, "app", "o3_303.bin"))
    if not label or not label.startswith(args.tag + " ("):
        print("release_assets.py: the image's label %r does not start with %s" % (label, args.tag), file=sys.stderr)
        return 1
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    name = "o3-303-%s.ptf" % args.tag
    ptf = os.path.join(out, name)
    shutil.copyfile(os.path.join(build, "app", "o3_303.ptf"), ptf)
    shutil.copyfile(os.path.join(build, "CANDIDATE.md"), os.path.join(out, "CANDIDATE.md"))
    with open(ptf, "rb") as f:
        data = f.read()
    digest = hashlib.sha256(data).hexdigest()
    with open(os.path.join(out, "SHA256SUMS"), "w", encoding="ascii") as f:
        f.write("%s  %s\n" % (digest, name))

    fields = {
        "TAG": args.tag,
        "LABEL": label,
        "COMMIT": head[:12],
        "SDK": pinned[:12],
        "SIZE": "{:,}".format(len(data)),
        "SHA256": digest,
        "CRC32": "%08X" % (zlib.crc32(data) & 0xFFFFFFFF),
    }
    with open(template, encoding="utf-8") as f:
        notes = f.read()
    for key, value in fields.items():
        notes = notes.replace("@%s@" % key, value)
    left = re.findall(r"@[A-Z0-9]+@", notes)
    if left:
        print("release_assets.py: unknown placeholder(s) %s in the notes" % ", ".join(sorted(set(left))), file=sys.stderr)
        return 1
    with open(os.path.join(out, "notes.md"), "w", encoding="utf-8") as f:
        f.write(notes)
    for key in ("LABEL", "SIZE", "SHA256", "CRC32"):
        print("release_assets.py: %s %s" % (key, fields[key]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
