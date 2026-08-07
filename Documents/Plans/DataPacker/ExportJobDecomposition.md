<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-06T22:03:19.085Z","dependsOn":[]} -->
# Decompose DeclipChannel, ParseDependencyFile, and LoadAnimations

## Context

Verified by `/external-deep-analysis` over `DataPacker/Source` (baseline `3cb5e9a6`), with seams confirmed against source by a fresh reviewer. `Documents/Plans/DataPacker/MainOrchestrationDecomposition.md` owns the Main.cpp and Attribution decompositions from the same analysis; these three export-job functions are recorded here:

- `DeclipChannel` (`DataPacker/Source/ExportJobs/AudioRepair.cpp:126-294`, cyclomatic complexity 35) — rail detection and run construction (`:134-182`), repair-policy classification (`:195-204`), and per-run support gathering, spline solving, reconstruction, and statistics (`:213-284`) are distinct phases reached per channel from `RepairAudio` (`:528-530`).
- `ParseDependencyFile` (`DataPacker/Source/ExportJobs/ExportShaderDependencies.cpp:89-200`, cyclomatic complexity 27) — syntax loading/validation (`:91-113`), root-prefix construction and matching (`:117-152`), root-delimited tokenization (`:154-184`), and the separate whitespace-fallback grammar (`:186-199`) are concrete semantic seams.
- `LoadAnimations` (`DataPacker/Source/ExportJobs/Scene/SceneAnimationLoader.cpp:40-186`, cyclomatic complexity 18) — channel filtering/mapping (`:62-107`), accessor selection (`:109-116`), cubic keyframe emission (`:118-145`), ordinary keyframe emission (`:147-175`), and clip publication (`:177-184`); the useful boundary is the two keyframe-emission branches plus the mapping needed to keep those interfaces cohesive.

## Design

Extract file-local helpers at the verified seams only, preserving thresholds, pass order, sample traversal, dependency ordering, diagnostics, vector append order, and packed values. Acceptance target from the analysis pipeline: no resulting function exceeds cyclomatic complexity ten. Verifier caveat, binding: that number is not a repository authority — whole-phase seams only, no forwarding micro-helpers to chase it; if they conflict, record a residual and surface the conflict.

## Critical files

- `DataPacker/Source/ExportJobs/AudioRepair.cpp`
- `DataPacker/Source/ExportJobs/ExportShaderDependencies.cpp`
- `DataPacker/Source/ExportJobs/Scene/SceneAnimationLoader.cpp`

## In scope

- Behavior-preserving decomposition of the three named functions plus the file-local helpers those decompositions require.

## Out of scope

- Any repair-policy, parsing-grammar, or serialization behavior change; version constants; other functions in these files.
- `AccessorFloats` input handling (owned by the glTF loader input-handling plan).

## Risk tier and invariants

Expected Change Workflow Tier 3. Trigger: these functions produce audio, shader-dependency, and animation bytes in the `.pack`/`.manifest` chain. Invariants: repaired PCM, dependency metadata, and animation-bearing scene chunk bytes byte-identical for identical inputs; malformed-input disposition unchanged; export deterministic.

## Acceptance criteria

- A full export from unchanged inputs produces byte-identical `.pack` and `.manifest` outputs.
- Each named function is decomposed to the stated target or carries a named residual explaining the remaining seam conflict.
