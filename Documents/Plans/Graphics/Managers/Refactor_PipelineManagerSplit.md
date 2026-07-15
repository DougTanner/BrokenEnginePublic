# Refactor: PipelineManager.cpp Size — Accept and Document (Graphics/Managers)

## Context

`Engine/Source/Graphics/Managers/PipelineManager.cpp` was flagged as an **OPTIONAL, pre-existing**
`/reduce-file` candidate (883 lines at time of flagging). A 2-file whole-function split (lighting/shadow
cluster + terrain/smoke/particle/debug cluster) was drafted as an option; the old 2-file bucketing existed
only as that draft, never executed.

**Decision (2026-07-03): accept-and-document, do not split.** A re-analysis (846 lines, down from 883)
confirmed the file is COHESIVE: the eight `Create*` functions are mechanically separable but each fills a
disjoint slot of one `mpPipelines` array via a load-bearing ctor boot order that must stay in the home TU
regardless of any split; every `Create*` follows a uniform declarative `.Create({...})` shape with zero
inter-function calls, so no responsibility boundary exists to split along. A split now would also churn
line citations in two in-flight plans (`Architecture_PipelineRegistrationOwnership.md`,
`Architecture_BindlessSlotLifecycle.md`) for no structural gain.

## Design

Add a brief comment at the top of `PipelineManager.cpp` noting the file's size is accepted-cohesive per
this 2026-07-03 analysis, so future `/reduce-file` passes don't re-flag it.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — add the size-accepted note near the top

## Out of scope

- Any function-body or ctor-order change — this plan is a comment-only note.
- Splitting `PipelineManager.h` or any other Graphics/Managers file.

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.
- Never interleave the PipelineManager split with `Documents/Plans/Graphics/Architecture_PipelineRegistrationOwnership.md`, `Documents/Plans/Graphics/Managers/CorruptTextureChunkLifecycleHardening.md`, `Documents/Plans/Graphics/DataTextureSamplerBias.md`; refresh citations between landings.

## Notes

- Invariant exposure: none — comment-only.
