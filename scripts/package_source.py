#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

"""Create a source release from a committed Git tree and record its checksum."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default="HEAD", help="Committed source reference")
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if git(root, "status", "--porcelain"):
        parser.error("Commit or remove working-tree changes before creating a release archive")
    commit = git(root, "rev-parse", "--verify", f"{args.ref}^{{commit}}")
    manifest = git(root, "show", f"{commit}:CMakeLists.txt")
    match = re.search(r"project\(abitur VERSION ([0-9]+\.[0-9]+\.[0-9]+)", manifest)
    if not match:
        parser.error("Cannot determine the release version from the committed CMake project")
    version = match.group(1)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f"abitur-{version}-source.tar.gz"
    subprocess.run(["git", "-C", str(root), "archive", "--format=tar.gz",
                    f"--prefix=abitur-{version}/", f"--output={archive}", commit], check=True)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / "SHA256SUMS").write_text(f"{checksum}  {archive.name}\n", encoding="utf-8")
    print(f"Commit: {commit}\nArchive: {archive}\nSHA256: {checksum}")


if __name__ == "__main__":
    main()
