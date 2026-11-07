# Reports and compatibility rules

JSON reports use schema version `1`. The reference schema is
[`report.schema.json`](report.schema.json). The executable version is reported
separately in `tool`; consumers should not infer schema versions from it.

## Top-level fields

- `schema_version`: integer `1`.
- `tool`: application identifier and version.
- `old`, `new`: input paths as supplied to the analyzer.
- `scope`: `shared-library` or `static-relink`.
- `decision`: `pass`, `break`, or `review`.
- `decision_reason`: explanation of the library-level decision.
- `findings`: structural and coverage findings.
- `consumers`: optional direct import associations.
- `limitations`: coverage statements attached to the report.

Paths and symbol names are evidence, not commands. Avoid depending on the exact
wording of human-readable messages or evidence strings. Those may change as the
analysis is refined.

## Findings

Each finding contains `rule_id`, `severity`, `symbol`, `message`, `evidence`, and
`consumers`. Severity is `error`, `warning`, or `note`. An empty symbol means the
finding concerns the input as a whole or coverage rather than a single interface.
Consumer paths identify a matching direct import; they do not prove a failing
call site.

Current rules:

- **ABI001 — TargetChanged:** ELF target architecture/ABI metadata differs.
- **ABI002 — SymbolRemoved:** an exported symbol is absent from the candidate.
- **ABI003 — SymbolPropertiesChanged:** ELF symbol kind or binding differs.
- **ABI004 — SymbolAdded:** an export was added; this is a note.
- **ABI005 — SignatureOrLayoutChanged:** structural properties of a function,
  variable, or reachable type differ.
- **ABI006 — IncompleteTypeCoverage:** an interface reaches unsupported or
  incomplete type information.
- **ABI007 — CoverageRequiresReview:** an input-wide coverage warning or missing
  interface description requires review.

Decision precedence is error → `break`, otherwise warning → `review`, otherwise
`pass`. There is no consumer-specific override in this release.

## Consumers

Each consumer entry contains its path, `imported_symbols` matching baseline
exports, and `affected_symbols` matching non-note findings. Empty affected
symbols mean no direct association was observed within this analysis.

## Example evidence

The included demo reports paths such as:

```text
checksum.return.size: 4 -> 8
process.parameter[0].pointee.member sequence offset: 0 -> 4
```

These describe the structural comparison, not a reconstructed execution trace.

## SARIF

SARIF output uses version 2.1.0, includes rule definitions and results, and stores
the decision and input paths in run properties. Symbol and consumer information
are result properties. Findings currently have no source locations or call-flow
traces because the analyzer does not reconstruct those locations. Do not assume
that every SARIF consumer will display location-free binary findings identically.

Input/tool errors use process exit code `2` and standard error. They are not
encoded as successful JSON/SARIF reports.
