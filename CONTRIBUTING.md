# Contributing

Abitur is an experimental ABI analysis project. Changes should keep supported
behavior, uncertain coverage, and runtime compatibility claims distinct.

## Development

Follow [building.md](docs/building.md), build the affected targets, and run CTest.
For desktop changes, run `abitur-window-smoke` on a graphical session and verify
the affected interaction. ABI analysis changes should include small C/C++
fixtures compiled into real ELF files. Do not add proprietary binaries or
unnecessary binary fixtures to the repository.

Format first-party C++ sources with the checked-in `.clang-format`. Keep public
report fields documented. If a report contract changes, update its schema,
reference documentation, and relevant checks together. Do not change an ABI
decision merely to satisfy a fixture without documenting the analysis reason.

## Issues and pull requests

For incorrect findings, describe the expected result, compiler version, target,
build flags, and a minimal reproducible source example. Include text/JSON output
when appropriate, with private paths and identifiers removed. Report missing
coverage separately from confirmed false-positive or false-negative results.

Pull requests should explain the observed problem, the resulting behavior, and
validation. Keep refactoring and behavior changes separate when practical. New
platform support should state which build and runtime paths were tested.

## License

First-party source and documentation are GPL-3.0-or-later unless a file states
otherwise. By contributing, you agree to distribute your contribution under
those terms. Keep third-party copyright and license notices intact. No copyright
assignment or contributor license agreement is required by this repository.
