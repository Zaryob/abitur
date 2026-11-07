# Development roadmap

This document describes proposed work, not implemented features or release
commitments. Priorities favor analysis coverage and interpretable evidence before
additional presentation features.

## Priority 1: comparison coverage and decision quality

1. **Libabigail adapter.** Integrate `abidiff` and `abicompat` behind an explicit
   engine interface, retain engine diagnostics, and compare native findings with
   engine results. Consumer compatibility checking already exists in libabigail;
   the integration should make its evidence and coverage explicit.
2. **ELF loader metadata.** Compare SONAME, needed libraries, version definitions
   and requirements, symbol visibility, and relevant symbol attributes.
3. **Finding classification.** Separate confirmed export/layout incompatibility,
   structural differences, and unknown coverage. Reduce false positives from
   member renaming, equivalent DWARF forms, and type declaration resolution.
4. **Validation corpus.** Add versioned GCC/Clang fixtures across optimization
   levels and targets, C++ inheritance/special-member cases, and differential
   testing against established ABI tools.

## Priority 2: consumer evidence and CI policy

5. **Consumer expectations.** Inspect consumer DWARF and relocations, associate
   versioned imports with providers, and report dependency paths. Explicitly
   retain unknown coverage for dynamic lookup and inlined layouts.
6. **Policy configuration.** Add reviewed suppressions, baselines, severity
   thresholds, and consumer-scoped policies with auditable reasons. The default
   must not equate missing evidence with compatibility.
7. **Debug information discovery.** Support separate debug files, build IDs,
   debug links, and eventually debuginfod with explicit network controls.
8. **CI integration.** Provide an action or reusable workflow with stable report
   contracts, report retention, source locations where available, and semantic
   versioning guidance based on declared policy.

## Priority 3: operation and inspection

9. **Scale and robustness.** Benchmark memory/time, add parser fuzzing and
   resource limits, and introduce cached ABI snapshots and incremental scans.
10. **Desktop inspection.** Add side-by-side record layouts, navigable evidence
    paths, multiple consumers, cancellation, and project/session files.
11. **Distribution.** Provide tested packages, software bills of materials,
    dependency/license manifests, and signed release artifacts where supported.
12. **Platform extensions.** Evaluate native Wayland window operations and other
    binary formats only after ELF/C++ coverage and host testing are established.
