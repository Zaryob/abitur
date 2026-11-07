# Usage

## Prepare inputs

Compile both library versions with complete, embedded DWARF information, normally
using `-g`. Use comparable compiler, target, visibility, optimization, and ABI
settings. Separate debug files and split DWARF are not supported.

Inputs are read as files. Abitur does not load libraries with the dynamic loader
and does not execute the analyzed library or consumer. Resource limits for
hostile or unusually large inputs have not been established.

## Command-line interface

```text
abitur OLD.so NEW.so [--consumer APP]... [--format text|json|sarif] [--output PATH]
abitur OLD.a NEW.a [--format text|json|sarif] [--output PATH]
abitur --help
abitur --version
abitur --license
```

`OLD` is the baseline and `NEW` is the proposed replacement. Compare inputs of
the same kind. Archive members are matched through their exported symbols,
rather than member filenames. Archives must contain relocatable ELF members
with embedded DWARF.

`--consumer` may be repeated for executable or shared-library ELF consumers with
the same target as the baseline. Only direct undefined dynamic symbols matching
baseline exports are considered. This option is not valid for archives.

The default format is `text`. Reports go to standard output unless `--output`
is supplied. The output directory must exist. An existing output file is
overwritten. Input and tool failures are written to standard error and do not
produce a structured report.

## Exit codes

- `0` — `pass`: no structural differences or coverage warnings were found in the
  supported surface. Added exports alone do not fail the check.
- `1` — `break`: an error-level structural finding was found. This takes
  precedence over review warnings.
- `2` — the invocation, input, or analysis failed.
- `3` — `review`: no error-level finding was found, but coverage or a change
  requires review.

The default gate is conservative. It does not suppress a library finding because
the supplied consumers have no matching direct imports. Configurable policy and
consumer-scoped gates are planned, not implemented.

When collecting a report in CI, preserve the analyzer's exit code:

```sh
set +e
./build/abitur old/libexample.so new/libexample.so --format json --output report.json
status=$?
set -e
# Upload or inspect the report here when it exists.
exit "$status"
```

## Desktop interface

Run `abitur-gui` or pass startup paths:

```text
abitur-gui OLD NEW [CONSUMER]
```

1. Select the baseline and candidate. A single optional consumer can be supplied
   in the desktop interface.
2. Start **Analyze compatibility**. Input controls are disabled while analysis
   runs; the interface continues to render.
3. Filter findings by text or severity and select a row.
4. Inspect **Evidence**, **Consumers**, and **Coverage & limits**.
5. Enter an output path prefix and select **JSON** or **SARIF**. The corresponding
   extension is appended. The directory must exist; existing reports are
   overwritten.

Dropping two files sets the baseline and candidate. Dropping one file fills the
baseline when empty, otherwise the candidate. The file chooser is implemented in
ImGui and accepts a typed directory and filename.

The title bar supports dragging and double-click maximize/restore. Window buttons
minimize, maximize/restore, and close the application. Window borders and corners
resize it subject to minimum size limits. Maximization uses the current
monitor's work area. Dragging a maximized window restores its previous size.

The desktop interface uses an embedded Cousine monospace font and the same
ImGui controls on supported host platforms. Its Linux backend requires X11 or
XWayland. Use **About Abitur** for version, license, and project information.
