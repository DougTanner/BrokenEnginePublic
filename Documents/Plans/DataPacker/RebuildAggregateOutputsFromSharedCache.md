# Rebuild Aggregate Outputs from Shared Cache

## Context

The Local worktree data workflow exposed a DataPacker recovery gap: deleting or omitting worktree-local `Output/Data` should require only final `.pack`/`.manifest`/generated-header assembly when `%TEMP%/DataPacker/<Project>` still contains valid chunks. `RunExportJobs<T>` already separates aggregate dirtiness from per-job dirtiness (`DataPacker/Source/Main.cpp:80-135`): missing final files set the aggregate dirty flag, while every job still runs `ExportJob::CheckDirty`, and clean jobs load their cached `.chunk` in `ExportJob::RunExport` (`ExportJob.cpp:93-203`).

The remaining gap is orchestration. `MainThread` unconditionally enters preparatory scheduling around aggregate assembly: `BakeIslandIntermediates()` at `Main.cpp:351` and `GenerateIrradianceCubemaps()` / `GeneratePreFilteredCubemaps()` at `:354-355`. Those producers use their own fingerprints, but their generated intermediates can be absent in a fresh worktree even when the final per-job chunks are valid in the shared temp cache. Missing aggregate output can therefore lead to an unexpected Gaea or texture/IBL regeneration instead of a cache-only assembly. The user explicitly requires missing final output alone never to authorize expensive regeneration.

## Design

1. Persist an atomically-written aggregate-rebuild receipt under `%TEMP%/DataPacker/<Project>` after a complete successful normal export. Version the private receipt independently from `.pack`; store no absolute checkout paths. Record the ordered output families and, per job, the logical relative path, flags, CRC, export version, cache-chunk relative path, exact chunk size/content fingerprint, and canonical source/dependency fingerprint. Prep-derived jobs must record their canonical producer dependency token rather than depending on a worktree-local generated intermediate: Scene source/version, island bake+split fingerprints, and IBL source+operation fingerprints.
2. At `MainThread` startup, classify aggregate-output absence separately from source/cache dirtiness before calling any preparatory producer. When `mbCleanExport` is false and final aggregates are missing or incomplete, validate the receipt, current exporter/prep versions, current canonical source dependencies, and every referenced shared chunk. If all inputs are unchanged and all cache records are valid, reconstruct every `.pack`, `.manifest`, per-family CRC header, `DataTypes.h`, and `Data.h` directly from the ordered cached chunks; skip Scene pre-export, Gaea bake/split work, IBL convolution, and texture encoding.
3. If a canonical input, dependency, job version, bake/split version, or IBL operation version genuinely changed, enter the existing incremental pipeline. Schedule only the affected producer/job: an island change may dirty its route, an IBL source change may dirty its derived cubemap, and a texture/source change may dirty its own job; unrelated cached chunks remain reusable. Missing final aggregates must not broaden that dirty set.
4. Treat the receipt and shared cache as opaque disk input. Reject malformed versions, absolute/traversing cache paths, duplicate case-folded logical paths or CRCs, impossible sizes/offsets, mismatched flags/versions/fingerprints, truncated reads, and content-hash mismatches before publishing anything. If inputs are unchanged but a required cache record is missing/corrupt, fail fast with the exact cache path and repair command instead of silently launching Gaea/compression. Add an optional trailing `--repair-cache` flag for both default-path and explicit-three-path invocation forms; it authorizes regeneration of missing/corrupt caches, while normal changed-input/version invalidation remains automatic.
5. Stage the complete reconstructed output set outside `Output/Data`, close and verify every stream, then publish with replace-existing/write-through file moves. Keep a rollback journal/backups for the multi-file publish and update the completion receipt last; any validation, write, or publish failure restores the previous complete set where one existed and leaves incomplete new output unusable. Normal exports update the cache receipt only after their atomic aggregate publication succeeds.
6. Log one summary distinguishing `aggregate cache reconstruction`, `incremental source rebuild`, and explicit `cache repair`, including reused/dirty/repaired job counts and Gaea/IBL/texture producer counts. This provides verification evidence without adding unit tests.

## Critical files

- `DataPacker/Source/Main.cpp` — `MainThread`, `RunExportJobs<T>`, aggregate staging/publication, and prep scheduling.
- `DataPacker/Source/ExportJobs/ExportJob.{h,cpp}` — cache-record validation and exact cached-chunk loading contract.
- `DataPacker/Source/ExportJobs/Island/BakeIslandIntermediates.cpp` and `BakeRoute.cpp` — read-only island prep dependency/fingerprint reporting without executing Gaea.
- `DataPacker/Source/ExportJobs/ExportCubemapIbl.{h,cpp}` and `ExportScene.{h,cpp}` — canonical producer dependency tokens independent of generated intermediates.
- `DataPacker/Source/FileManager.{h,cpp}` — receipt path and optional trailing `--repair-cache` parsing.

## Out of scope

- Moving, pruning, sharing across project names, or globally cleaning `%TEMP%/DataPacker`.
- Changing asset payloads, manifest/pack layout, `DataHeader::kiVersion`, or any exporter output bytes.
- Making Gaea/BC/IBL output cross-machine reproducible.
- Changing worktree build-mode selection or runtime data-directory handling.
- Automatically repairing an invalid unchanged-input cache without explicit `--repair-cache` authorization.

## Acceptance criteria

- After one successful normal export, delete final `.pack`, `.manifest`, and generated headers but retain the shared temp cache. A normal DataPacker run regenerates byte-identical aggregate files from cache; logs show zero Gaea invocations, zero CMFT IBL convolutions, and zero texture encode/compression jobs.
- Repeat from a fresh worktree lacking ignored generated intermediates. Valid shared receipt/chunks still take the aggregate-cache path without writing intermediates into source data directories.
- Change one ordinary texture input: only that texture's required producer/export work runs. Change one island JSON/archetype: only affected route prep/jobs run. Unrelated cached jobs remain reused.
- Truncate a cached chunk or corrupt the receipt while sources remain unchanged: normal invocation fails before prep/publish and names the invalid cache; `--repair-cache` regenerates only the invalid entries and then publishes complete output.
- Force a staging/publish failure and verify no partial aggregate set is accepted and any previous complete output remains intact.
- Build DataPacker and exercise the manual cache-reconstruction/invalidation matrix above; do not add unit tests or invoke client/server runtime verification.

## Notes

- Offline DataPacker/cache behavior only. No simulation determinism/CRC, replay, wire protocol, client/server guard, shader-runtime, or allocation-tracked-path exposure.
- Rebuilt `.pack`/`.manifest`/headers must be byte-identical to normal assembly from the same cached chunks. The receipt has its own cache-format version; it does not require `DataHeader::kiVersion` or per-export `GetVersion()` bumps unless payload semantics independently changed.
- This plan overlaps `Refactor_BakeIslandLegacyCacheMigration.md` in `BakeIslandIntermediates.cpp`; changes are behaviorally independent, but the later lander must reconcile the moved legacy block and refresh the prep-fingerprint hook.
