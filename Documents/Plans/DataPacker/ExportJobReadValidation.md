<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:13.687Z","dependsOn":[]} -->
# Validate export-job file reads before publication

## Context

A verified `/external-deep-analysis` run over `DataPacker/Source` (baseline `3cb5e9a6`) found seven export-job read sites that consume file bytes without checking that the read completed. Because destination buffers are value-initialized, a short or failed read silently publishes zero-filled output as a successful export. Root `AGENTS.md` requires validation of anything opaque to the current code unit, including file reads.

Verified sites (reviewer-confirmed against source, reports retained by the analysis session):

- `DataPacker/Source/ExportJobs/ExportScene.cpp:519-522` and `:568-573` — material count and material records from the cached `.MODEL` file are read unchecked; a header-preserving truncation leaves zero-filled tails that continue into `FillMaterialShaderDatas` and chunk publication.
- `DataPacker/Source/ExportJobs/ExportModel.cpp:20-57` — material count, seek, index/vertex counts, and payloads are all unchecked before the vectors are copied into the model chunk.
- `DataPacker/Source/ExportJobs/ExportRaw.cpp:25-32` — size is measured, payload allocated, then read unchecked; a shrink or failure between the two publishes a partly unwritten payload.
- `DataPacker/Source/ExportJobs/ExportIsland.cpp:275-281` and `:314-316` — mesh counts (negative values cast to `size_t`) and `Elevation.r32` payload are read unchecked, then consumed by hull construction, quantization, and serialization.
- `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp:318-338` (`LoadElevationMeters`) and `:340-352` (`LoadAmbientOcclusion`) — `file_size` is checked but the `read` calls are not; partial data can reach `ProcessBakedRegion` and be blessed by the `SplitVersion.meta` stamp at `:609-613`.
- `DataPacker/Source/ExportJobs/Texture/MigrateLegacyIntermediates.cpp:137` and `:161` — header and payload reads are unchecked; for a legacy raw file whose payload size equals the expected raw size, the partially populated buffer is accepted and rewritten over the tracked source.
- `DataPacker/Source/ExportJobs/Texture/Texture.cpp:114-134` (`Texture::LoadUint16Raw`) — the buffer from `common::ReadEntireFile` is cast and dereferenced one `uint16_t` per declared pixel without verifying `data.size() == miWidth * miHeight * sizeof(uint16_t)`; island AO dimensions come from `BakedDimensions.json` while route cleanliness checks only file existence, so a truncated AO cache file reaches an out-of-bounds read.

## Design

At each listed site, reject the input before any allocation-driving value is consumed and before any output, sidecar, or marker is written: require the stream to be open and each read to complete (or, for `LoadUint16Raw`, require the exact byte count with checked size arithmetic). Failure follows each job's existing failure path (`CleanupOnFailure()` and aggregate diagnostics), so prior outputs are never replaced. No format, ordering, version constant, or success-path behavior changes.

## Critical files

- `DataPacker/Source/ExportJobs/ExportScene.cpp`
- `DataPacker/Source/ExportJobs/ExportModel.cpp`
- `DataPacker/Source/ExportJobs/ExportRaw.cpp`
- `DataPacker/Source/ExportJobs/ExportIsland.cpp`
- `DataPacker/Source/ExportJobs/Island/BakeRoute.cpp`
- `DataPacker/Source/ExportJobs/Texture/MigrateLegacyIntermediates.cpp`
- `DataPacker/Source/ExportJobs/Texture/Texture.cpp`

## In scope

- Read-completion and exact-size validation at the seven listed read sites only: `ExportScene::MainExport` material reads and `ReadMaterialInfosFromModel`, `ExportModel::Export`, `ExportRaw::Export`, `ReadProcessedMesh`/`ExportIslandData` mesh and elevation reads, `LoadElevationMeters`, `LoadAmbientOcclusion`, `MigrateLegacyIntermediate` header/payload reads, and `Texture::LoadUint16Raw`.
- Routing each new failure through the owning job's existing failure disposition.

## Out of scope

- The documented nontransactional in-place rewrite in `MigrateLegacyIntermediates` (`Texture/AGENTS.md` Migration) — validation ordering only, no staging redesign.
- Any change to chunk layout, version constants, export ordering, or success-path bytes.
- Defensive validation between internal callers; new validation applies only to file-read trust boundaries.

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: the changed functions produce the `.pack`/`.manifest` bytes the runtime loads. Invariants: exported bytes identical for unchanged valid inputs; version constants untouched; export deterministic; failed reads must not write any output, sidecar, or version marker.

## Acceptance criteria

- For each listed site, an injected truncation or short read fails the owning job before any output or marker is written, and prior outputs remain byte-identical.
- A full export from unchanged valid inputs produces byte-identical `.pack` and `.manifest` outputs.

## Notes

`std::ifstream` short-read semantics (failbit, partial buffer) are standard behavior; no external verification is required.
