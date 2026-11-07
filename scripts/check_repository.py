#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Suleyman Poyraz
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check documentation links, first-party licensing, and optional JSON reports."""
import argparse
import json
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, action="append", default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    errors = []
    documents = [*root.glob("*.md"), *root.glob("docs/*.md"), root / "assets/fonts/README.md"]
    for document in documents:
        for target in re.findall(r"\]\(([^)]+)\)", document.read_text(encoding="utf-8")):
            url = urlsplit(target)
            if url.scheme or not url.path:
                continue
            if not (document.parent / unquote(url.path)).exists():
                errors.append(f"{document.relative_to(root)}: missing link target {target}")
    sources = [root / "CMakeLists.txt"]
    for folder in ("src", "include", "tests", "examples", "scripts", "cmake"):
        sources.extend(p for p in (root / folder).rglob("*") if p.suffix in (".cpp", ".hpp", ".py", ".in"))
    for source in sources:
        text = source.read_text(encoding="utf-8")
        if "SPDX-License-Identifier: GPL-3.0-or-later" not in text:
            errors.append(f"{source.relative_to(root)}: missing first-party GPL identifier")
    license_text = (root / "LICENSE").read_text(encoding="utf-8")
    if "GNU GENERAL PUBLIC LICENSE" not in license_text or "Version 3, 29 June 2007" not in license_text:
        errors.append("LICENSE: missing GPL version 3 text")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    version = re.search(r"project\(abitur VERSION ([0-9]+\.[0-9]+\.[0-9]+)", cmake).group(1)
    if f"## {version} " not in (root / "CHANGELOG.md").read_text(encoding="utf-8"):
        errors.append("CHANGELOG.md: missing current project version")
    schema = json.loads((root / "docs/report.schema.json").read_text(encoding="utf-8"))
    if args.report:
        from jsonschema import Draft202012Validator
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)
        for path in args.report:
            data = json.loads(path.read_text(encoding="utf-8"))
            for error in validator.iter_errors(data):
                errors.append(f"{path}: {error.json_path}: {error.message}")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"Checked {len(documents)} documents, {len(sources)} first-party source files, and {len(args.report)} reports")


if __name__ == "__main__":
    main()
