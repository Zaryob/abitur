#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build real debug ELF fixtures. No Linux runtime is needed to inspect them."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(args, **kwargs):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}\n{result.stderr}")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--cxx", required=True)
    args = parser.parse_args()
    cxx = os.environ.get("ABITUR_TEST_CXX", args.cxx)
    ar = os.environ.get("ABITUR_TEST_AR") or shutil.which("llvm-ar")
    if not ar and Path("/opt/homebrew/opt/llvm/bin/llvm-ar").exists():
        ar = "/opt/homebrew/opt/llvm/bin/llvm-ar"
    ar = ar or shutil.which("ar")
    assert ar, "ar required for archive fixtures"
    cross = sys.platform == "darwin"
    extra = ["--target=x86_64-unknown-linux-gnu"] if cross else []
    linker = shutil.which("ld.lld")
    link_env = os.environ.copy()
    if cross and not linker:
        rustc = shutil.which("rustc")
        if rustc:
            sysroot = Path(run([rustc, "--print", "sysroot"]).stdout.strip())
            linker = next(iter(sysroot.glob("lib/rustlib/*/bin/rust-lld")), None)
            if linker:
                link_env["DYLD_LIBRARY_PATH"] = str(sysroot / "lib")
        assert linker, "ELF linker required on macOS: install lld or Rust's rust-lld"
    checks = 0
    with tempfile.TemporaryDirectory(prefix="abitur-test-") as directory:
        root = Path(directory)

        def fixture(label, source, debug=True, flags=()):
            folder = root / label
            folder.mkdir()
            cpp, obj = folder / "api.cpp", folder / "api.o"
            cpp.write_text(source)
            run([cxx, *extra, "-std=c++20", "-fPIC", "-O0", *( ["-g", "-gdwarf-4"] if debug else []), *flags, "-c", cpp, "-o", obj])
            archive, shared = folder / "libexample.a", folder / "libexample.so"
            run([ar, "rcs", archive, obj])
            if cross:
                run([linker, "-flavor", "gnu", "-shared", "-soname", "libexample.so", "-o", shared, obj], env=link_env)
            else:
                run([cxx, "-shared", "-Wl,-soname,libexample.so", obj, "-o", shared])
            return shared, archive

        def compare(old, new, expected, *options):
            nonlocal checks
            proc = subprocess.run([args.cli, str(old), str(new), "--format", "json", *map(str, options)], capture_output=True, text=True)
            assert proc.returncode == expected, (proc.returncode, expected, proc.stdout, proc.stderr)
            checks += 1
            return json.loads(proc.stdout) if expected != 2 else proc.stderr

        baseline = 'struct Item { int x; int y; }; extern "C" int inspect(Item* p) { return p->x; } extern "C" int removed() { return 1; }'
        old = fixture("old", baseline)
        same = fixture("same", baseline)
        added = fixture("added", baseline + ' extern "C" int added() { return 0; }')
        reordered = fixture("reordered", baseline.replace("int x; int y;", "int y; int x;"))
        removed = fixture("removed", baseline.replace(' extern "C" int removed() { return 1; }', ""))
        signature = fixture("signature", baseline.replace('int inspect(Item* p)', 'long inspect(Item* p)'))
        no_debug = fixture("no-debug", baseline, debug=False)

        for kind in (0, 1):
            report = compare(old[kind], same[kind], 0)
            assert report["decision"] == "pass", report
            report = compare(old[kind], added[kind], 0)
            assert any(f["rule_id"] == "ABI004" for f in report["findings"])
            report = compare(old[kind], removed[kind], 1)
            assert any(f["rule_id"] == "ABI002" and f["symbol"] == "removed" for f in report["findings"])
            report = compare(old[kind], reordered[kind], 1)
            changed = [f for f in report["findings"] if f["rule_id"] == "ABI005"]
            assert changed and any("member x offset: 0 -> 4" in e for f in changed for e in f["evidence"]), report
            report = compare(old[kind], signature[kind], 1)
            assert any("return.size: 4 -> 8" in e for f in report["findings"] for e in f["evidence"]), report
            assert "DWARF" in compare(no_debug[kind], old[kind], 2)

        # Self-referential type graph terminates and still finds nested layout changes.
        recursive = 'struct Link { Link* next; int value; }; extern "C" int walk(Link* n) { return n->value; }'
        rec1 = fixture("recursive-old", recursive)
        rec2 = fixture("recursive-new", recursive.replace("int value;", "long value;"))
        compare(rec1[0], rec1[0], 0)
        compare(rec1[0], rec2[0], 1)

        # C++ mangled signatures and namespaces, including return types absent from mangling.
        cpp1 = fixture("cpp-old", 'namespace api { struct Data { int a; int b; }; int read(Data* p) { return p->a; } }')
        cpp2 = fixture("cpp-new", 'namespace api { struct Data { int b; int a; }; int read(Data* p) { return p->a; } }')
        compare(cpp1[0], cpp1[0], 0)
        report = compare(cpp1[0], cpp2[0], 1)
        assert any(f["symbol"].startswith("_ZN3api") for f in report["findings"])

        # Private/internal types do not leak into exported ABI comparisons.
        private1 = fixture("private-old", baseline + ' namespace { struct Secret { int n; }; int local(Secret* p) { return p->n; } }')
        private2 = fixture("private-new", baseline + ' namespace { struct Secret { long n; }; int local(Secret* p) { return p->n; } }')
        compare(private1[0], private2[0], 0)

        inherited = fixture("inheritance", 'struct Base { int a; }; struct Child : Base { int b; }; extern "C" int use(Child* p) { return p->b; }')
        report = compare(inherited[0], inherited[0], 3)
        assert report["decision"] == "review"
        assert any("inheritance" in e for f in report["findings"] for e in f["evidence"])

        # Globals, arrays, bitfields and indirect function signatures.
        global1 = fixture("global-old", 'struct State { int x; int y; }; State state = {1, 2};')
        global2 = fixture("global-new", 'struct State { long x; int y; }; State state = {1, 2};')
        compare(global1[0], global2[0], 1)
        array1 = fixture("array-old", 'struct Buffer { int values[3]; }; extern "C" int sum(Buffer* p) { return p->values[0]; }')
        array2 = fixture("array-new", 'struct Buffer { int values[4]; }; extern "C" int sum(Buffer* p) { return p->values[0]; }')
        report = compare(array1[1], array2[1], 1)
        assert any("dimension[0]" in e for f in report["findings"] for e in f["evidence"])
        bits1 = fixture("bits-old", 'struct Bits { unsigned a:3; unsigned b:5; }; extern "C" int bits(Bits* p) { return p->a; }')
        bits2 = fixture("bits-new", 'struct Bits { unsigned a:4; unsigned b:4; }; extern "C" int bits(Bits* p) { return p->a; }')
        report = compare(bits1[0], bits2[0], 1)
        assert any("bit-size" in e for f in report["findings"] for e in f["evidence"])
        callback1 = fixture("callback-old", 'extern "C" int invoke(int (*cb)(int)) { return cb(1); }')
        callback2 = fixture("callback-new", 'extern "C" int invoke(int (*cb)(double)) { return cb(1); }')
        compare(callback1[0], callback2[0], 1)

        # DWARF 5 relocations in archives and signature changes without C linkage.
        dwarf5 = fixture("dwarf5", baseline, flags=("-gdwarf-5",))
        compare(dwarf5[0], dwarf5[0], 0)
        compare(dwarf5[1], dwarf5[1], 0)
        overload1 = fixture("overload-old", 'int api(int x) { return x; }')
        overload2 = fixture("overload-new", 'int api(double x) { return (int)x; }')
        report = compare(overload1[0], overload2[0], 1)
        assert any(f["rule_id"] == "ABI002" for f in report["findings"])

        hidden1 = fixture("hidden-old", baseline + ' extern "C" __attribute__((visibility("hidden"))) int hidden() { return 1; }')
        compare(hidden1[0], old[0], 0)

        # Archive member renames must not be interpreted as symbol removal.
        renamed_obj = root / "renamed.o"
        shutil.copy(root / "old/api.o", renamed_obj)
        renamed_ar = root / "renamed.a"
        run([ar, "rcs", renamed_ar, renamed_obj])
        compare(old[1], renamed_ar, 0)

        # Real consumer ELF, linked to the baseline without a C runtime.
        app_cpp, app_obj, app = root / "consumer.cpp", root / "consumer.o", root / "consumer"
        app_cpp.write_text('extern "C" int removed(); extern "C" void _start() { volatile int result = removed(); (void)result; }')
        run([cxx, *extra, "-fPIC", "-c", app_cpp, "-o", app_obj])
        if cross:
            run([linker, "-flavor", "gnu", "-e", "_start", "-o", app, app_obj, old[0]], env=link_env)
        else:
            run([cxx, "-nostdlib", "-Wl,-e,_start", "-o", app, app_obj, old[0]])
        report = compare(old[0], removed[0], 1, "--consumer", app)
        assert report["consumers"][0]["affected_symbols"] == ["removed"], report
        assert any(f["consumers"] == [str(app)] for f in report["findings"] if f["symbol"] == "removed")
        report = compare(old[0], reordered[0], 1, "--consumer", app)
        assert report["consumers"][0]["affected_symbols"] == []
        assert report["decision"] == "break", "unused direct imports must not waive library breaks"
        compare(old[1], same[1], 2, "--consumer", app)
        compare(old[0], same[1], 2)
        # EI_OSABI is byte 7 of the endian-independent ELF identification.
        # Keep target detection covered when supporting older LLVM readers.
        osabi_changed = root / "osabi.so"
        contents = bytearray(old[0].read_bytes())
        contents[7] = 3 if contents[7] != 3 else 0
        osabi_changed.write_bytes(contents)
        report = compare(old[0], osabi_changed, 1)
        assert any(f["rule_id"] == "ABI001" for f in report["findings"])
        bad = root / "bad.so"
        bad.write_bytes(b"\x7fELF\x00")
        compare(bad, old[0], 2)
        compare(root / "missing.so", old[0], 2)

        output = root / "report.sarif"
        proc = subprocess.run([args.cli, str(old[0]), str(removed[0]), "--format", "sarif", "--output", str(output)], capture_output=True, text=True)
        assert proc.returncode == 1 and not proc.stdout
        data = json.loads(output.read_text())
        assert data["version"] == "2.1.0"
        assert any(f["ruleId"] == "ABI002" and f["level"] == "error" for f in data["runs"][0]["results"])
        # Deterministic serialization for the same inputs.
        first = run([args.cli, str(old[0]), str(old[0]), "--format", "json"]).stdout
        second = run([args.cli, str(old[0]), str(old[0]), "--format", "json"]).stdout
        assert first == second
        print(f"{checks} ABI integration checks + SARIF export + deterministic serialization passed")


if __name__ == "__main__":
    main()
