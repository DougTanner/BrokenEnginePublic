<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:14.743Z","dependsOn":[]} -->
# Stage texture intermediates and clean up failed scene texture tasks

## Context

Two verified findings from `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`) break the documented atomic-output rule (`DataPacker/Source/AGENTS.md`: jobs write temporary files and rename only on complete success; failed outputs are discarded without replacing prior outputs):

- `DataPacker/Source/ExportJobs/Texture/Texture.cpp:667-678` — `Texture::Save` calls `std::filesystem::remove(rPath)` and then writes the destination directly, checking `fileStreamOut.good()` only at the end. A disk or stream failure destroys the last known-good intermediate and can leave a truncated file that same-run texture discovery consumes, while the prior scene/island pack remains published. Callers include parallel `ExportScene` and `ExportIsland` jobs, and `Main.cpp:513-520` continues into `RunExportJobs<ExportTexture>` after a producer fails.
- `DataPacker/Source/ExportJobs/ExportScene.cpp:248-270` — `ProcessTextures` launches all texture tasks, then records each path in `mIntermediateFiles` only after its index-ordered `future::get()` succeeds. If an early `get()` throws, later tasks finish during future destruction but were never registered, so `CleanupOnFailure()` (`ExportScene.cpp:781-787`, invoked from `ExportJob::RunExport` at `ExportJob.cpp:191-198`) cannot remove them; the orphans remain discoverable by the subsequent texture export as a partial generation.

## Design

- `Texture::Save` writes to a non-discoverable sibling temporary path, verifies the complete write and close, and publishes by rename only on success; on failure it removes the temporary and leaves the prior destination untouched. Serialization stays byte-identical, so successfully published bytes are unchanged. The `MigrateLegacyIntermediates` caller (`MigrateLegacyIntermediates.cpp:211-215`) keeps its documented validate-before-rewrite ordering and gains only the staged write; no migration redesign.
- `ProcessTextures` precomputes and registers non-discoverable staging paths before launching tasks, drains every future while retaining the first exception, removes everything this attempt created on failure, and publishes only after all tasks succeed. Preregistering final paths is explicitly rejected: cleanup could otherwise delete pre-existing good intermediates this attempt never modified.

## Critical files

- `DataPacker/Source/ExportJobs/Texture/Texture.cpp`
- `DataPacker/Source/ExportJobs/ExportScene.cpp`

## In scope

- `Texture::Save` staging, verification, cleanup, and rename publication.
- `ExportScene::ProcessTextures` staging-path registration, full future drain with first-failure retention, failure cleanup, and success publication, plus the matching `CleanupOnFailure()` bookkeeping.

## Out of scope

- The nontransactional in-place rewrite policy of `MigrateLegacyIntermediates` (`Texture/AGENTS.md` Migration).
- Stale-intermediate reconciliation for deleted sources (owned by the IBL reconciliation plan).
- Any change to encoded bytes, chunk formats, or version constants.

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: intermediate publication feeds the `.pack`/`.manifest` producer chain, and `ProcessTextures` changes threading-adjacent failure flow. Invariants: successful-run intermediates, packs, and manifests byte-identical; a failed attempt leaves prior outputs byte-identical and no discoverable partial files; version constants untouched.

## Acceptance criteria

- Injected open/write/flush failure in `Texture::Save` leaves the prior destination byte-identical and no temporary discoverable by `ExportTexture`.
- With one texture task forced to fail, every launched task is observed, all artifacts created by the attempt are removed, prior intermediates remain byte-identical, and publication does not run.
- A full export from unchanged inputs is byte-identical.

## Notes

`std::async` future destructors blocking until completion is standard documented behavior; no external verification is required.
