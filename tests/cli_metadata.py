#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check standalone CLI metadata without analysis inputs."""
import subprocess
import sys

cli, version = sys.argv[1:]
for option in ("--help", "--version", "--license"):
    result = subprocess.run([cli, option], capture_output=True, text=True)
    if result.returncode or result.stderr:
        raise AssertionError((option, result.returncode, result.stderr))
    if option == "--version" and result.stdout.strip() != f"abitur {version}":
        raise AssertionError(result.stdout)
    if option == "--license":
        for expected in (version, "GPL-3.0-or-later", "NO WARRANTY", "THIRD_PARTY_NOTICES.md"):
            if expected not in result.stdout:
                raise AssertionError((expected, result.stdout))
    if option == "--help" and "--license" not in result.stdout:
        raise AssertionError(result.stdout)
print("Standalone CLI help, version and license metadata passed")
