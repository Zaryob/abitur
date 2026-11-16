# Changelog

## Unreleased

### Analysis

- macOS Mach-O support: `.dylib`, static `.a` archives, and Universal/Fat
  binaries, with automatic `.dSYM` companion DWARF resolution.
- Standalone binary inspection when no candidate is given: headers, segments,
  sections, exported/imported symbols, DWARF signatures, and struct memory
  layouts with padding detection.
- ABI008 reports SONAME / install name and direct dependency changes.
- Finding suppressions with `--suppress RULE[:SYMBOL]` and
  `--suppression-file PATH`. Suppressed findings do not fail the gate.
- LLVM 18 ELF metadata support.

### CI

- Reusable GitHub composite action (`.github/actions/abitur-check`) with a job
  step summary.

### Desktop

- Multi-tab inspection view for single binaries, a suppression file input, and
  one-click copying of suppression flags or suppression-file lines.
- Separate search per tab, severity counts, a rule column, and arrow-key
  navigation through findings.
- Input validation with clear buttons and a reason when the run button is
  disabled; `Cmd/Ctrl+Enter` runs the analysis and `Cmd/Ctrl+F` focuses search.
- Text, JSON, and SARIF export in both modes. The default output path is placed
  next to the baseline binary.
- File browser with Up/Home navigation, typed paths, and cached listings.
  Dropped `.supp` files fill the suppression input.

## 0.1.0 — 2026-11-07

Initial experimental source release.

- ELF shared-library and regular-archive comparison using LLVM Object/DWARF.
- Export, function signature, and reachable type-layout findings.
- Coverage warnings and conservative library-level exit decisions.
- Direct consumer import associations.
- Text, JSON schema version 1, and SARIF 2.1.0 reports.
- Shared Dear ImGui desktop window controls and an embedded Cousine font.
- CLI version/license information and desktop project information.
- Integration fixtures, desktop geometry/window checks, and CI definitions.
- GPL-3.0-or-later licensing with preserved third-party notices.

This release does not integrate libabigail or implement complete C++ ABI,
loader-resolution, or runtime compatibility analysis.
