# Extract Island Legacy-Cache Migration

## Context

`DataPacker/Source/ExportJobs/Island/BakeIslandIntermediates.cpp` is 528 lines after the shared-Gaea-cache work. Its first large block now owns a separate, one-time responsibility: compare, merge, validate, and remove legacy source-tree `Intermediates/` directories and JPEG diagnostics before normal route baking starts. The migration helpers (`FilesEqual`, `DirectoryCanMerge`, `MigrateLegacyDirectory`, `MigrateLegacyFile`, and `MigrateLegacyIslandCache`) occupy roughly 180 lines, while the rest of the file owns route definitions, `BakeOne`, cache-path mapping, and top-level island orchestration.

## Design

- Move `MigrateLegacyIslandCache` and its file/directory comparison, merge, and conflict-marker helpers into a focused sibling implementation, `LegacyIslandCacheMigration.{h,cpp}`.
- Keep the existing call at the start of `BakeOne` and pass the current island folder and selected `RouteSubdivision` entries unchanged. Reuse `GetIslandCachePath` and `GetIslandDiagnosticsPath`; do not duplicate cache-root derivation.
- Preserve migration semantics byte-for-byte: prefer rename into an empty destination, validate copy/merge contents before deleting the source, retain conflicting source data, write `LegacyMigrationConflict.meta`, and invalidate all bake/split metadata on a conflict.
- Leave `RouteSubdivision`, route selection, stale-route pruning, Gaea launch/staging, and cache-path APIs with the bake orchestration.
- Add the new source/header to `DataPacker.vcxproj` and its filters, then run the DataPacker compile and a migration fixture covering both a clean move and a conflicting destination.

## Critical files

- `DataPacker/Source/ExportJobs/Island/BakeIslandIntermediates.cpp` — `BakeOne`, `MigrateLegacyIslandCache`, and its private migration helpers.
- `DataPacker/Source/ExportJobs/Island/BakeIslandIntermediatesInternal.h` — private `RouteSubdivision` contract shared by the island bake TUs.
- `DataPacker/Source/ExportJobs/Island/BakeIslandIntermediates.h` — existing `GetIslandCachePath` / `GetIslandDiagnosticsPath` APIs reused by the extracted implementation.
- `DataPacker/Platforms/VisualStudio2026/DataPacker.vcxproj` and `.filters` — new TU membership.

## Out of scope

- Changing the `%TEMP%/DataPacker/<Project>/Gaea` layout, migration policy, conflict resolution, or metadata format.
- Changing route subdivision, Gaea invocation, raw-output staging, leaf splitting, or `ExportIsland` packing.
- General filesystem-helper extraction outside the island migration path.

## Acceptance criteria

- `BakeIslandIntermediates.cpp` retains cohesive route/cache orchestration and no longer contains legacy merge/copy implementation details.
- A legacy source-tree route cache migrates to the same destination and is removed only after content validation.
- A conflicting destination preserves source data, writes the same conflict marker, and invalidates route metadata.
- DataPacker builds with the new TU registered in the project and filters.

## Notes

- This is a move-only refactor; preserve log levels/messages and exception behavior unless compilation requires a mechanical qualification change.
- Invariant exposure: offline DataPacker only. No runtime determinism/CRC, replay, client/server guard, allocation-tracked path, `.pack` layout, `kiVersion`, or cache-format change.

