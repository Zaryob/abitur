# Release process

1. Update the CMake project version and `CHANGELOG.md`. Report schema versions
   are maintained separately and should change only for report contract changes.
2. Check first-party SPDX identifiers, the GPL license, embedded asset notices,
   and dependency versions/checksums. Keep actual coverage limitations documented.
3. Build the full application and desktop-only configuration. Run CTest, the
   native window smoke test, and the demo analysis/render test where supported.
4. Commit the intended release source and wait for the relevant CI jobs. Inspect
   failures; do not describe unexecuted platforms as runtime-validated.
5. Generate the source archive from the exact commit with
   `python3 scripts/package_source.py --ref HEAD`. This writes a source archive
   and SHA-256 manifest to `dist/`.
6. Tag the verified commit as `vVERSION` and publish the source archive, checksum
   manifest, and release notes. Mark experimental versions as prereleases.
7. Verify the published tag, assets, checksums, license detection, and repository
   visibility. Never include local build trees, credentials, analysis inputs, or
   test-generated binaries in the source package.

The initial distribution is source-only. Prebuilt binaries need separate
platform packaging, dependency/source/license manifests, and runtime validation.
