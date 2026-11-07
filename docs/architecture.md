# Architecture

The CLI and desktop application share the same analyzer and report models:

```text
Baseline ELF/archive + candidate ELF/archive
                    |
          LLVM Object + DWARF readers
                    |
           ABI graph per object
                    |
        Export and structural comparisons
                    |
       Direct consumer import associations
                    |
           Shared report model
              /           \
      CLI / JSON / SARIF    ImGui desktop
```

## Analyzer

`src/analyzer.cpp` reads object files and regular archive members using LLVM.
DWARF relocations are processed by LLVM, including those in relocatable archive
members. The graph builder reserves node identities before following references
so recursive types can be represented.

Exported function and variable symbols form graph roots. Type nodes store a kind,
structural properties, labeled edges, and unresolved coverage reasons. Typedef
and CV-qualified wrappers are normalized. Definitions may be resolved through
DWARF specification/origin references and record declarations.

Comparison uses paired-node visitation to terminate on recursive graphs. Record
members are matched by names, with positional labels for anonymous members.
The comparator records property and edge changes as evidence paths. Separate
export rules detect removed, added, or changed ELF symbols.

Input and parsing failures throw `std::runtime_error`. Coverage gaps that are
represented by the analyzer become warning findings. A target difference or an
error-level structural finding takes precedence in the final decision.

## Consumers and reports

Consumer analysis reads direct undefined dynamic symbol names. It intersects
them with baseline exports and associates matching names with findings. The
library-level decision is retained even when supplied consumers have no matching
affected imports.

`include/abitur/analyzer.hpp` defines the shared API and report structures.
`src/report.cpp` serializes reports to text, JSON, and SARIF. JSON schema version
`1` is independent of the application version; the contract is documented in
[reports.md](reports.md).

## Desktop

`src/gui.cpp` contains application panels, file selection, result filtering,
report export, and the background analysis job. A `std::future` transfers the
completed report or exception back to the UI thread.

`src/desktop/window.cpp` owns GLFW, OpenGL, the ImGui context/backends, shared
window chrome, theme, and embedded font. Application panels receive a content
rectangle and do not call platform window APIs. Window drag/resize consumes the
same event stream as ImGui controls. Geometry calculations and monitor selection
are isolated in `include/abitur/desktop/geometry.hpp`.

## Build targets

- `abitur_core`: LLVM-based analysis and report serialization.
- `abitur`: command-line executable.
- `abitur_imgui`: ImGui and its GLFW/OpenGL backends.
- `abitur_desktop`: shared window implementation and embedded font.
- `abitur-gui`: desktop executable using the analyzer and window layer.
- `abitur-window-geometry`: display-independent geometry checks.
- `abitur-window-smoke`: native window initialization and state checks.

The core and desktop targets are internal project modules. Their C++ APIs and
binary interfaces are not currently published as stable library contracts.
