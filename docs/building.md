# Building and testing

## Requirements

- A C++20 compiler and CMake 3.24 or later.
- LLVM development headers, libraries, and CMake package files for the analyzer.
  CI exercises LLVM 18 on Ubuntu 24.04; local development has also used LLVM 23.
- GLFW 3.3 or later and an OpenGL 3.2-capable driver for desktop targets.
- Python 3.9 or later and an ELF-capable C++ toolchain for integration fixtures.

Dependencies must be compatible with the selected compiler, architecture, C++
runtime, and build configuration. `LLVM_DIR` is the directory containing
`LLVMConfig.cmake`, not the LLVM installation prefix.

## Linux

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install cmake clang-18 llvm-18-dev libglfw3-dev libgl1-mesa-dev python3 xvfb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/abitur-gui
```

The custom desktop controls use X11 or XWayland. For a source-built GLFW, its X11
development dependencies must also be available. On a headless test machine:

```sh
xvfb-run -a ./build/abitur-window-smoke
```

## macOS

```sh
brew install llvm glfw cmake lld
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
cmake --build build --parallel 2
open build/abitur-gui.app
```

The application reads ELF inputs on macOS; it does not analyze Mach-O files.
Integration tests cross-compile header-free fixture sources to x86-64 Linux ELF.
They need `ld.lld` on `PATH`. For Homebrew installations that do not link the
executable into the default path:

```sh
export PATH="$(brew --prefix lld)/bin:$PATH"
ctest --test-dir build --output-on-failure
```

The fixture scripts can also use an existing toolchain's `rust-lld` as an ELF
linker fallback. It is not a dependency of the application.

## Windows

The desktop layer can be compiled with Visual Studio 2022 and a C++20 toolset:

```powershell
cmake -S . -B build-desktop -DABITUR_BUILD_ANALYZER=OFF
cmake --build build-desktop --config Release --parallel 2
ctest --test-dir build-desktop -C Release --output-on-failure
.\build-desktop\Release\abitur-window-smoke.exe
```

The window smoke test requires an OpenGL 3.2 driver. Hosted Windows and macOS CI
compile this target and run geometry tests; neither job assumes a suitable
graphics driver. The hosted macOS VM did not provide a suitable OpenGL 3.2 pixel
format. Native macOS window behavior is verified locally on a graphical machine;
Linux CI runs the window smoke test with Mesa under Xvfb.

To build the full analyzer, provide a compatible LLVM development installation:

```powershell
cmake -S . -B build -DLLVM_DIR="C:/path/to/llvm/lib/cmake/llvm"
cmake --build build --config Release --parallel 2
```

Full Windows analyzer execution is not part of the current CI matrix. Native
PE/COFF input analysis is not implemented. ELF fixture integration tests are
registered on Linux and macOS only.

## Build options and dependency sources

- `ABITUR_BUILD_GUI=OFF`: build the analyzer and CLI without GLFW, ImGui, OpenGL,
  or font embedding.
- `ABITUR_BUILD_ANALYZER=OFF`: build the shared desktop layer without LLVM. With
  `BUILD_TESTING=ON`, this includes `abitur-window-smoke` and geometry tests.
- `BUILD_TESTING=OFF`: omit test targets and the Python test dependency.

GUI builds download Dear ImGui v1.91.9b and use a system GLFW if available;
otherwise they download GLFW 3.4. The source archives are verified by SHA-256.
For an offline build, supply previously obtained source directories:

```sh
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui \
  -DFETCHCONTENT_SOURCE_DIR_GLFW=/path/to/glfw \
  -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
```

The Cousine font is part of the source distribution and is embedded at build
time. The executable does not search for platform-specific font files.

## Validation

```sh
ctest --test-dir build --output-on-failure
./build/abitur-window-smoke
python3 examples/build_demo.py --cxx clang++
./build/abitur-gui --smoke-test \
  demo-build/old/libexample.so demo-build/new/libexample.so demo-build/consumer
```

On macOS, replace the GUI executable path with
`build/abitur-gui.app/Contents/MacOS/abitur-gui`. The GUI smoke test runs a bounded
render loop and reports initialization or analysis errors separately from ABI
findings.

Integration tests create temporary ELF fixtures and never execute the consumer
binaries. `ABITUR_TEST_CXX` and `ABITUR_TEST_AR` override the fixture compiler and
archiver. Desktop geometry tests do not require a display server.
