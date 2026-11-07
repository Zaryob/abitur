# Abitur

Abitur compares the exported interfaces of Linux ELF libraries and reports
structural ABI differences. It provides a command-line interface for automated
checks and a Dear ImGui desktop interface for inspecting findings.

**Status:** experimental, version 0.1.0. A successful comparison means that no
differences were found in the supported ABI surface. It does not establish that
an existing application will run correctly with a replacement library.

## Capabilities

- Compare two shared ELF libraries (`.so`) or two regular archives (`.a`) with
  embedded DWARF information.
- Report removed and added exports, changed symbol properties, function
  signatures, and reachable record, array, and bitfield layouts.
- Associate findings with a consumer's direct dynamic imports.
- Export text, JSON, and SARIF 2.1.0 reports with structural evidence.
- Inspect findings in a desktop application with shared window controls and an
  embedded monospace font.

Static archive comparison concerns **relinking**. Replacing a `.a` file does not
update an executable that already contains its code. Consumer import analysis
is available for shared-library comparisons only.

## Build

The full application requires C++20, CMake 3.24 or later, LLVM development
libraries, GLFW 3.3 or later, and an OpenGL 3.2-capable driver. Python 3 is used
for integration tests and examples. See [build instructions](docs/building.md)
for platform-specific dependencies and desktop-only builds.

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install cmake clang-18 llvm-18-dev libglfw3-dev libgl1-mesa-dev python3
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The command-line interface can be built without desktop dependencies by adding
`-DABITUR_BUILD_GUI=OFF`.

## Usage

```sh
./build/abitur old/libexample.so new/libexample.so \
  --consumer ./application --format json --output report.json
./build/abitur old/libexample.a new/libexample.a --format sarif --output report.sarif
./build/abitur --version
./build/abitur --license
```

Exit codes are `0` for no supported differences, `1` for structural differences,
`2` for input or tool errors, and `3` for changes or incomplete coverage requiring
review. Consumer evidence does not waive a library-level finding. See the
[usage guide](docs/usage.md) and [report reference](docs/reports.md).

Start `abitur-gui` to select libraries and inspect findings. On macOS, the desktop
target is `abitur-gui.app`. Linux desktop window controls require X11 or XWayland;
native Wayland is not supported by the current window layer.

## Example

```sh
python3 examples/build_demo.py --cxx clang++-18
./build/abitur demo-build/old/libexample.so demo-build/new/libexample.so \
  --consumer demo-build/consumer --format text
```

The example changes record member offsets and a function return type, removes
an export, and adds another export. The expected comparison exit code is `1`.
The generated consumer is inspected and is not executed.

## Scope

The current analyzer does not fully implement the C++ ABI. Inheritance, virtual
tables, special member functions, exported symbol versions, and unsupported or
incomplete type information require review. SONAME resolution, indirect consumer
use, runtime behavior, and exception/RTTI interoperability are not checked.
Stripped and split debug information, thin archives, Mach-O, and PE/COFF inputs
are not supported. See [analysis limitations](docs/limitations.md).

The desktop layer is shared across Windows, macOS, and Linux. Host platform
support does not imply support for that platform's native binary format.
Libabigail is not integrated in this release.

## Documentation

- [Building and testing](docs/building.md)
- [CLI and desktop usage](docs/usage.md)
- [Report format and rules](docs/reports.md)
- [Architecture](docs/architecture.md)
- [Limitations](docs/limitations.md)
- [Development roadmap](docs/roadmap.md)
- [Contributing](CONTRIBUTING.md)
- [Release process](docs/releasing.md)
- [Changelog](CHANGELOG.md)

## License

Abitur's original source code and documentation are licensed under the
**GNU General Public License, version 3 or later** (`GPL-3.0-or-later`).
See [LICENSE](LICENSE). Third-party components retain their own licenses;
attributions and license texts are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
