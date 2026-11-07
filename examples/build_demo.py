#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

"""Generate Linux ELF demo libraries even when developing on macOS."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--output", type=Path, default=Path("demo-build"))
    args = parser.parse_args()
    source = Path(__file__).parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cross = sys.platform == "darwin"
    extra = ["--target=x86_64-unknown-linux-gnu"] if cross else []
    ar = shutil.which("llvm-ar")
    if not ar and Path("/opt/homebrew/opt/llvm/bin/llvm-ar").exists():
        ar = "/opt/homebrew/opt/llvm/bin/llvm-ar"
    ar = ar or shutil.which("ar")
    linker, link_env = shutil.which("ld.lld"), os.environ.copy()
    if cross and not linker:
        sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip())
        linker = next(iter(sysroot.glob("lib/rustlib/*/bin/rust-lld")), None)
        link_env["DYLD_LIBRARY_PATH"] = str(sysroot / "lib")
    if cross and not linker:
        parser.error("On macOS, install lld or a Rust toolchain for the ELF linker")
    for name in ("old", "new"):
        folder = output / name
        folder.mkdir(exist_ok=True)
        obj = folder / "example.o"
        run([args.cxx, *extra, "-std=c++20", "-g", "-gdwarf-4", "-O0", "-fPIC", "-c", source / f"{name}.cpp", "-o", obj])
        run([ar, "rcs", folder / "libexample.a", obj])
        if cross:
            run([linker, "-flavor", "gnu", "-shared", "-soname", "libexample.so", "-o", folder / "libexample.so", obj], env=link_env)
        else:
            run([args.cxx, "-shared", "-Wl,-soname,libexample.so", obj, "-o", folder / "libexample.so"])
    run([args.cxx, *extra, "-fPIC", "-c", source / "consumer.cpp", "-o", output / "consumer.o"])
    if cross:
        run([linker, "-flavor", "gnu", "-e", "_start", "-o", output / "consumer", output / "consumer.o", output / "old/libexample.so"], env=link_env)
    else:
        run([args.cxx, "-nostdlib", "-Wl,-e,_start", "-o", output / "consumer", output / "consumer.o", output / "old/libexample.so"])
    print(f"Demo ELF files: {output}")


if __name__ == "__main__":
    main()
