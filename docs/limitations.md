# Analysis limitations

Abitur 0.1.0 is an experimental structural analyzer. Its results are evidence
about the inspected interface, not a complete implementation of the C++ ABI or
a guarantee of runtime compatibility.

## Supported surface

The analyzer follows exported functions and variables through their DWARF types.
It compares fundamental type size/encoding, function parameters and return
types, available calling-convention metadata, pointers/references, record sizes
and member offsets, arrays, and bitfields. It handles recursive type graphs.

Typedef names and CV qualifiers are normalized away. Enumeration values are not
compared because this analysis concerns physical layout and calling interfaces.
These choices do not imply source or behavioral compatibility.

## Conservative findings

Record members are matched by names. Member renaming can therefore produce a
structural finding even when the physical layout is unchanged. Differences in
the presence or representation of DWARF attributes can also produce findings.
ABI005 is an error-level structural difference, not a separately validated proof
that every consumer will fail.

The following cause review findings where detected:

- Inheritance, virtual functions/vtables and thunks.
- Special member functions that can affect C++ value passing.
- Exported symbol version definitions; native symbol matching uses base names.
- Unsupported types, unresolved member expressions, unknown array bounds,
  missing interface descriptions, and incomplete type information.
- Different DWARF compiler producer sets or multiple definitions of an exported
  interface across archive members.

These checks are not a complete detector of every ABI extension. Missing debug
information can leave relevant details unrepresented, particularly with
optimization, LTO, generated code, or incomplete compilation units.

## Inputs

- Libraries must be shared ELF objects or regular archives containing
  relocatable ELF objects.
- Embedded DWARF is required in each inspected object. Stripped, split-debug,
  BTF-only, and CTF-only inputs are unsupported.
- Thin archives, mixed input kinds, and archives containing unsupported members
  are rejected.
- Target changes are reported, but no cross-target ABI equivalence analysis is
  performed.
- Mach-O and PE/COFF analysis are not implemented.

## Consumer evidence

The consumer view intersects direct undefined dynamic symbol names with baseline
exports and associates those names with findings. It does not establish which
provider the runtime loader will select, inspect call sites or relocations, or
reconstruct the application's expected type layouts.

No absence-of-impact claim is made for `dlsym`, plugins, indirect dependencies,
inline code, copied layouts, copy relocations, or other dynamic lookup. Global
coverage warnings can have no symbol-specific consumer association. A consumer
with no affected direct imports may still be incompatible.

## Runtime and operational coverage

SONAME/dependency resolution, loader paths, symbol interposition, symbol version
binding, exception/RTTI interoperability, runtime behavior, and semantic changes
are outside the analysis. Files are inspected without executing them, but parsing
untrusted inputs is not sandboxed by Abitur itself.

Performance targets, including operation within 8 GB RAM, have not been
benchmarked. Large-corpus scalability and parser fuzzing remain development
work. Libabigail and `abicompat` are not integrated; see the
[roadmap](roadmap.md) for planned analysis extensions.
