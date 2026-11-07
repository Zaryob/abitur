# Third-party components

Abitur's original code and documentation are GPL-3.0-or-later. The following
components retain their upstream licenses; they are not relicensed as original
Abitur code.

## Distributed asset

**Cousine Regular** — digitized data copyright (c) 2010 Google Corporation.
Licensed under Apache License 2.0. The font is stored in `assets/fonts`, obtained
from Dear ImGui v1.91.9b's `misc/fonts/Cousine-Regular.ttf`, and embedded in desktop
executables. License and provenance are in [assets/fonts](assets/fonts/README.md).

## Build dependencies

- **LLVM:** Apache License 2.0 with LLVM exceptions and additional notices for
  included third-party material. See [licenses/LLVM.txt](licenses/LLVM.txt) and
  [upstream licensing](https://llvm.org/docs/DeveloperPolicy.html#copyright-license-and-patents).
- **Dear ImGui v1.91.9b:** MIT License, copyright (c) 2014–2025 Omar Cornut and
  Dear ImGui contributors. See [licenses/ImGui-MIT.txt](licenses/ImGui-MIT.txt).
- **GLFW 3.4 fallback:** zlib/libpng license. See
  [licenses/GLFW-Zlib.txt](licenses/GLFW-Zlib.txt). A system installation may have
  a different version and must retain its applicable notices.

Dear ImGui's OpenGL backend includes an OpenGL loader with its own upstream
notices in `backends/imgui_impl_opengl3_loader.h`. GLFW and LLVM installations can
contain additional dependencies and notices. The source release does not bundle
their compiled libraries or replace those upstream notices.

## Distribution

The initial release publishes first-party sources, the embedded font, its build
configuration, and notices. It does not provide prebuilt application binaries.
Binary distributors must also account for the actual dependencies linked or
bundled by their build, provide their notices and required source, and include
Abitur's GPL license and corresponding source as applicable. This dependency
summary is not a complete binary-build manifest.
