<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:16.864Z","dependsOn":[]} -->
# Remove orphaned IBL cubemap intermediates before texture discovery

## Context

Verified by `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`): the IBL pre-pass producers (`GenerateIrradianceCubemaps`, `GeneratePreFilteredCubemaps`, `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp:133-156`, `:267-289`, `:322-376`) enumerate only current inputs and write `_Irradiance.R16G16B16A16_SFLOAT` / `_Prefiltered.R16G16B16A16_SFLOAT` intermediates beside them, with no stale-output reconciliation. When a source KTX or six-face directory is deleted or renamed, the derived intermediate stays on disk; `ExportTexture::Handles()` accepts every `.R16G16B16A16_SFLOAT` file (`ExportTexture.cpp:8-17`), and `Main.cpp:517-520` runs the producers immediately before recursive texture discovery, so a stale texture chunk keeps being published under the old path. `ExportJobs/AGENTS.md` (Cubemap Pre-pass) documents these as producer-generated intermediates consumed through raw texture routing; scene pre-export already removes its orphaned texture intermediates, so this producer is the outlier.

## Design

During the existing pre-pass, each producer collects its expected output paths from current inputs; after generation, producer-owned outputs and their metadata not in that expected set are removed, before `RunExportJobs<ExportTexture>` discovery. Ownership is proven through the mirrored `CubemapIbl/<input-root>/...meta` cache entries, and reconciliation distinguishes irradiance from prefiltered outputs so one pass cannot delete the other's valid files. Interrupted-producer state must be handled: `BeginOutputUpdate()` removes `.meta` and leaves a `.meta.dirty` marker (`ExportCubemapIbl.cpp:96-102`), so `.meta.dirty` also proves producer ownership. Unchanged inputs are compared, never rewritten, so surviving bytes, deterministic ordering, and version constants are untouched.

## Critical files

- `DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp`

## In scope

- Expected-output inventory and orphan removal for both IBL producer variants, including `.meta` / `.meta.dirty` sidecar handling, running before ordinary texture export discovery.

## Out of scope

- Convolution, filtering, fingerprinting, and output formats; exception-path resource cleanup (owned by the IBL resource-cleanup plan).
- Generic reconciliation for other producers.

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: changes which files the `.pack` texture producer chain discovers. Invariants: with unchanged inputs, no file is rewritten and export output is byte-identical; removal is limited to outputs whose producer ownership is proven via the mirrored metadata; irradiance and prefiltered ownership never cross.

## Acceptance criteria

- Deleting or renaming a source KTX or six-face cubemap removes only its producer-owned intermediates and metadata before texture discovery; the next export publishes no chunk under the old path.
- With unchanged inputs, no IBL output is rewritten and a full export is byte-identical.
