<!-- broken-engine-plan/v1 {"createdUtc":"2026-06-25T02:37:38.000Z","dependsOn":[]} -->
# Right-size Pipeline's PipelineInfo::pDescriptorInfos array

## Context

`engine::PipelineInfo` carries a by-value fixed-size descriptor array `DescriptorInfo pDescriptorInfos[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings]` (`Engine/Source/Graphics/Objects/Pipeline.h:113`), embedded into every `Pipeline` via `PipelineInfo mInfo;` (`Pipeline.h:156`). `kiMaxDescriptorSetLayoutBindings = 32` (`Common/DataFile.h:328`); `DescriptorInfo` is nine 8-byte members (`DescriptorFlags_t flags`, `int64_t iCount`, `int64_t iExplicitBinding`, `crc_t textureCrc`, 4 pointers, `crc_t crc` — `Pipeline.h:54-66`) = 72 bytes, so the array is 2,304 bytes ≈ **2.25 KB per `Pipeline`**, retained for the object's lifetime. Most pipelines populate only a handful of descriptor slots; the tail sits empty (`DescriptorFlags::kEmpty`).

`Pipeline` is genuinely multi-instance:

- `PipelineManager::mpPipelines[kPipelineCount]` (`Engine/Source/Graphics/Managers/PipelineManager.h:83`) + `mSpreadPipelines[shaders::kiMaxSpreadPasses]` (`PipelineManager.h:90`).
- Per-scene model materials: `ModelPipeline::mpPipelines` is `std::vector<Pipeline>(miMaterialCount)` (`Engine/Source/Graphics/Objects/ModelPipeline.cpp:70`), and one `ModelPipeline` exists per scene CRC in `DynamicPipelines::mModelPipelineMaps` (`Engine/Source/Graphics/Managers/DynamicPipelines.h:67`).

This mirrors the array-right-sizing pattern already applied to AnimationData's four arrays and `ModelPipeline::mpPipelines` — same intent (trim a per-instance worst-case inline array), but **harder** because the array is not a clean drop-in.

**Why this is NOT a clean swap (the reason it was split out rather than folded into the AnimationData plan):**

- The array is **scanned/indexed by absolute index up to the compile-time max**, not just up to a stored count:
	- `ModelPipeline::Create` scans to the max to find the first `kEmpty` slot, stamps it `kModel`, then writes the five Model follow-on descriptors into `[i + 1 .. i + 5]` past the initially-populated region (`ModelPipeline.cpp:13-47`; `kiModelAdditionalDescriptors = 5` at `:21`, headroom ASSERT at `:22`).
	- `Pipeline::Create` loops the full 32 slots to derive `mbPerCommandBuffer` from per-slot flags, with no early break at `kEmpty` (`Pipeline.cpp:104-110`).
	- `PipelineDescriptorWriter::Write` runs two loops bounded by the max that break at the first `kEmpty` entry (`PipelineDescriptorWriter.cpp:425-433` image-info sizing, `:493+` descriptor writes), and stack-sizes its write/buffer-info arrays to the max (`:473-474`).
	- `PipelineCreator` stack-sizes its reflection-derived binding/flag arrays to the max (`PipelineCreator.cpp:76, 241, 252-253, 707, 727`) but reads shader reflection, not `pDescriptorInfos` — those local arrays are per-call stack scratch, not part of the per-instance memory cost.
- The invariant "**empty trailing slots are writable up to 32**" is load-bearing: the `kModel` auto-append in `ModelPipeline::Create` writes into slots beyond the initially-populated region. Any runtime container must reserve capacity for the post-append maximum, not just the initial fill count.
- The dimension is the shared `.pack` format constant `common::ShaderHeader::kiMaxDescriptorSetLayoutBindings` (`Common/DataFile.h:328`); DataPacker's `ExportShader.cpp` asserts shader descriptor counts against it (`:53`) and sizes export scratch with it (`:256-257`). So the constant itself must stay; only `Pipeline`'s *runtime* mirror is a right-size candidate (`pDescriptorInfos` is a runtime ctor-arg struct, not serialized — confirm at execution that no disk write exists).

## Scope contract

Listed scope is **target and ceiling**: make the smallest complete change satisfying the option selected at grill (see Design), add no abstractions, configuration, refactors, or fixes to adjacent code, and touch nothing in a listed file beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope (exact regions):**

- `Engine/Source/Graphics/Objects/Pipeline.h` — the `pDescriptorInfos` declaration inside `struct PipelineInfo` (`:113`) and, for Option A, a rationale comment at that declaration only.
- Option B additionally (and only if B is selected at grill): the `pDescriptorInfos` loop bound and indexing in `Pipeline::Create` (`Pipeline.cpp:104-110`), the first-empty scan plus `[i + 1 .. i + 5]` auto-append in `ModelPipeline::Create` (`ModelPipeline.cpp:13-47`), and the two `pDescriptorInfos` loops in `PipelineDescriptorWriter::Write` (`PipelineDescriptorWriter.cpp:425-433, 493+`).
- Optional pre-decision instrumentation: a temporary Debug boot log of populated-slot counts across live pipelines (the `IslandResidentMemoryScaling` `[DEBUG-resmem]` precedent), removed before completion.

**Out of scope:**

- The `.pack`/DataPacker descriptor-count format and `ShaderHeader::kiMaxDescriptorSetLayoutBindings` (`Common/DataFile.h:328`) — unchanged; this only concerns `Pipeline`'s runtime inline mirror. `ExportShader.cpp` untouched.
- `PipelineCreator.cpp` — its max-sized arrays are reflection-driven stack scratch, not `pDescriptorInfos` mirrors; do not resize them.
- Any change to descriptor *semantics* (set/binding numbers, the `kModel` auto-append behavior in `ModelPipeline::Create`) — preserve exactly.
- Everything else in the listed files: `Pipeline` record/destroy paths, `DescriptorInfo`/`DescriptorFlags` definitions, `ModelPipeline` trust-boundary validation and material loop, `PipelineDescriptorWriter` cursor/registration logic beyond the two named loop bounds.

## Design

**Investigate-then-decide.** The ~2.25 KB-per-instance win must be weighed against the index-walk semantics that make a container swap non-trivial. Present these options at grill:

- **A — Accept + document (likely right size).** Keep the fixed `[32]` array but document, at the declaration (`Pipeline.h:113`), *why* it is fixed: the create/write paths bound their loops at the compile-time max and the `kModel` auto-append writes into the trailing slots, so the storage must be max-dimensioned. Lowest risk; closes the "is this an oversight?" question without touching GPU-setup paths. Net memory unchanged.
- **B — Right-size with a populated-count + reserved-headroom container.** Replace the array with a runtime container sized to `initialBindingCount + kiModelAdditionalDescriptors` headroom (the max post-append count, not the raw fill count). The scan sites named in the scope contract change from `< kiMaxDescriptorSetLayoutBindings` bounds to container-size bounds, and the `ModelPipeline::Create` `[i + 1 .. i + 5]` writes must be guaranteed in-range. Captures the memory win but touches every scan site and the auto-append logic — needs a playtest pass over model + spread + every `kPipeline*` pass.
- **C — Hybrid: shrink the constant for the runtime mirror only.** If profiling shows the realistic max populated+appended count is well under 32, introduce a separate smaller runtime cap distinct from the `.pack` `kiMaxDescriptorSetLayoutBindings`. Adds a second constant to keep in lockstep — likely not worth it.

Recommend gathering the real distribution first: instrument the populated slot count across all live pipelines (Debug boot log, `[DEBUG-resmem]` precedent) to quantify the actual waste before committing to B. If the aggregate is small, **A** is the answer.

## Critical files

- `Engine/Source/Graphics/Objects/Pipeline.h` — `DescriptorInfo` (`:54-66`); `PipelineInfo::pDescriptorInfos` declaration (`:113`); `PipelineInfo mInfo` member (`:156`).
- `Engine/Source/Graphics/Objects/Pipeline.cpp` — `Pipeline::Create` full-32 `mbPerCommandBuffer` loop (`:104-110`).
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp` — first-empty-slot scan + `kModel` auto-append into `[i + 1 .. i + 5]` (`:13-47`); `mpPipelines` construction (`:70`).
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp` — max-bounded `pDescriptorInfos` loops breaking at first `kEmpty` (`:425-433`, `:493+`); max-sized stack write arrays (`:473-474`).
- `Common/DataFile.h` — `ShaderHeader::kiMaxDescriptorSetLayoutBindings = 32` (`:328`); **read-only** — the `.pack` format constant stays.

## Risk tier and invariants

- **Tier 2** (scoped client/graphics runtime behavior, one subsystem). Option A alone is Tier 1 (comment-only).
- Client-only: every listed file is whole-file `BT_CLIENT`. `pDescriptorInfos` is a runtime ctor-arg struct, not serialized — no CRC / determinism / `kiVersion` / `.pack`-layout / replay exposure (confirm no disk write at execution).
- Load-bearing invariant for Option B: trailing slots up to the post-append maximum stay writable; the `ModelPipeline::Create` headroom ASSERT (`ModelPipeline.cpp:22`) must remain valid under the new capacity rule.
- No allocation in the render loop: any Option B container is populated during pipeline creation (startup / recreate), never per-frame.

## Acceptance criteria

- **Option A:** rationale comment present at `Pipeline.h:113`; client compiles; no behavior change (comment-only diff is decisive).
- **Option B:** client compiles; playtest boots and renders model, spread, and every `kPipeline*` pass without validation errors or missing descriptors; `ModelPipeline` scenes (model + shadow) draw correctly; measured per-`Pipeline` size reduction reported.
- **Instrumentation (if run):** temporary log removed before completion.

## Coordination

- Both previously named exclusion partners — `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md` and `Documents/Plans/Graphics/Architecture_PipelineRegistrationOwnership.md` — no longer exist in the plans tree (verified 2026-07-24). Their exclusions are moot unless successors reappear; re-check for plans touching `Pipeline::Create` / `PipelineDescriptorWriter::Write` at claim time and do not interleave with any found.

## Notes

- **Decision plan (present options).** Resolve A/B/C via `/external-grill-plan` before any edit; gather the populated-slot distribution first.
- Line citations verified 2026-07-24; refresh if `Engine/Source/Graphics/Objects/` files move underneath this plan.
