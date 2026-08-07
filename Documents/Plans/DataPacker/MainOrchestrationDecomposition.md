<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:10:33.778Z","dependsOn":[]} -->
# Decompose Main.cpp orchestration and Attribution license collection

## Context

`DataPacker/Source/Main.cpp` carries the second-largest hot complexity mass in the corpus (908; structuralErosion 92.3% at the flagging snapshot) and `Attribution.cpp` the area's highest erosion (96.3%). A verified `/external-deep-analysis` run over `DataPacker/Source` (baseline `3cb5e9a6`) confirmed real whole-phase seams in three functions, with the seams checked against source by a fresh reviewer:

- `RunExportJobs<T>()` (`Main.cpp:82-428`, cyclomatic complexity 57) — published-output validation into `iManifestChunkCount`/`manifestChunkLocations` (`:91-161`), discovery and dirty state (`:173-192`), pack-layout validation (`:216-283`), and execution plus three-file publication (`:297-424`) exchange only explicit values.
- `main` (`Main.cpp:541-638`, cyclomatic complexity 17) — the existing `runOnce` lambda already isolates command dispatch (`:554-592`); debugger-sensitive diagnostic execution (`:594-634`) is separate; mutex lifetime and exit conversion stay in `main` (`:543-552`, `:636-637`).
- `CopyThirdPartyLicenses` (`Attribution.cpp:24-156`, cyclomatic complexity 27) — read-only discovery/selection building `pendingCopies` (`:44-138`), the clean gate (`:140-143`), and publication from `EnsureLocal` (`:144-155`).

This work was owned by the removed wrapper plan `DataPackerDeepAnalysis.md`; this plan records its concrete, analysis-proven remainder.

## Design

Extract file-local, behavior-preserving helpers at exactly the seams above, retaining phase order, future draining, temporary-file commit order, deterministic ordering, version constants, emitted bytes, debugger exception behavior, CLI results, and the Attribution clean gate. Acceptance target from the analysis pipeline: no resulting function exceeds cyclomatic complexity ten. Verifier caveat, binding: that number is not a repository authority — whole-phase seams only, never cosmetic micro-helpers or split command branches to chase it; if the target and the seams conflict, record a named residual and surface the conflict.

## Critical files

- `DataPacker/Source/Main.cpp`
- `DataPacker/Source/Attribution.cpp`

## In scope

- Behavior-preserving decomposition of `RunExportJobs<T>()`, `main`, and `CopyThirdPartyLicenses` at the listed seams, plus the file-local helpers those extractions require.

## Out of scope

- Any behavior, ordering, CLI, diagnostic, or output change; mutex validation (owned by `DataPackerMutexValidation.md`); Attribution stale-output reconciliation (owned by `AttributionStaleOutputReconciliation.md`); other functions in these files.

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: these functions assemble and publish the `.pack`/`.manifest` bytes the runtime loads. Invariants: a full export from unchanged inputs is byte-identical (`.pack`, `.manifest`, generated headers) across clean, dirty, and failed-export scenarios; version constants untouched; export deterministic; every supported and malformed CLI form keeps its current exit code and diagnostic behavior.

## Acceptance criteria

- Clean and dirty exports produce byte-identical `.pack`, `.manifest`, and generated header outputs; CLI and debugger behavior unchanged.
- Each named function is decomposed to the stated target or carries a named residual explaining the remaining seam conflict.
