# Right-size Pipeline's PipelineInfo::pDescriptorInfos array

## Context

`engine::PipelineInfo` carries a by-value fixed-size descriptor array `DescriptorInfo pDescriptorInfos[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings]` (`Engine/Source/Graphics/Objects/Pipeline.h:102`), embedded by value into every `Pipeline` via `PipelineInfo mInfo;` (`Pipeline.h:143`). `kiMaxDescriptorSetLayoutBindings = 32` (`Common/DataFile.h:299`); `DescriptorInfo` is ~80 bytes (`DescriptorFlags_t` + 3×`int64_t` + 3×`crc_t` + 4 pointers), so the array is ≈ **2.5 KB per `Pipeline`**, retained for the object's lifetime. Most pipelines populate only a handful of descriptor slots; the tail sits empty (`DescriptorFlags::kEmpty`).

`Pipeline` is genuinely multi-instance:
- `PipelineManager::mpPipelines[kPipelineCount]` (`PipelineManager.h:92`) + `mSpreadPipelines[kiMaxSpreadPasses]` (`PipelineManager.h:99`).
- Per-scene model materials: `ModelPipeline::mpPipelines` is `std::vector<Pipeline>(miMaterialCount)` (`ModelPipeline.cpp:45`), and one `ModelPipeline` exists per scene CRC in `DynamicPipelines::mModelPipelineMaps` (`DynamicPipelines.h:65`).

This mirrors the array-right-sizing pattern already applied to AnimationData's four arrays and `ModelPipeline::mpPipelines` — same intent (trim a per-instance worst-case inline array), but **harder** because the array is not a clean drop-in.

**Why this is NOT a clean swap (the reason it was split out rather than folded into the AnimationData plan):**
- The array is **scanned/indexed by absolute index up to the compile-time max**, not just up to a stored count: `Pipeline::Create` walks `[i+1 .. i+5]` past the populated region to auto-append the 5 Model follow-on descriptors (`Pipeline.cpp:100-134`), checks `mInfo.pDescriptorInfos[3]` by literal index (`Pipeline.cpp:136`), and loops to `kiMaxDescriptorSetLayoutBindings` to set `mbPerCommandBuffer` (`Pipeline.cpp:149`). `ModelPipeline::Create` scans to the max to find the first empty slot (`ModelPipeline.cpp:13`). `PipelineCreator` / `PipelineDescriptorWriter` also iterate to the max.
- The invariant "**empty trailing slots are writable up to 32**" is load-bearing: the `kModel` auto-append writes into slots beyond the initially-populated region. Any runtime container must reserve capacity for the post-append maximum, not just the initial fill count.
- The dimension is the shared `.pack` format constant `common::ShaderHeader::kiMaxDescriptorSetLayoutBindings` (`Common/DataFile.h:299`); DataPacker's `ExportShader.cpp` asserts shader descriptor counts against it. So the constant itself must stay; only `Pipeline`'s *runtime* mirror is a right-size candidate (`pDescriptorInfos` is a runtime ctor-arg struct, not serialized — confirm at execution).

## Design

**Investigate-then-decide.** The ~2.5 KB-per-instance win must be weighed against the index-walk semantics that make a container swap non-trivial. Present these options at grill:

- **A — Accept + document (likely right size).** Keep the fixed `[32]` array but document, at the declaration, *why* it is fixed: the create/record paths index it to the compile-time max and the `kModel` auto-append writes into the trailing slots, so the storage must be max-dimensioned. Lowest risk; closes the "is this an oversight?" question without touching GPU-setup paths. Net memory unchanged.
- **B — Right-size with a populated-count + reserved-headroom container.** Replace the array with a runtime container sized to `initialBindingCount + kModelAutoAppendReserve` (the max post-append count, not the raw fill count). All `Create`/record scan-to-max loops must change from `< kiMaxDescriptorSetLayoutBindings` to `< container.size()`, and the literal-index access (`pDescriptorInfos[3]`) must be guaranteed in-range. Captures the memory win but touches every scan site and the auto-append logic — needs a playtest pass over model + spread + every `kPipeline*` pass.
- **C — Hybrid: shrink the constant for the runtime mirror only.** If profiling shows the realistic max populated+appended count is well under 32, introduce a separate smaller runtime cap distinct from the `.pack` `kiMaxDescriptorSetLayoutBindings`. Adds a second constant to keep in lockstep — likely not worth it.

Recommend gathering the real distribution first: instrument the populated slot count across all live pipelines (a Debug boot log, like the `IslandResidentMemoryScaling` `[DEBUG-resmem]` precedent) to quantify the actual waste before committing to B. If the aggregate is small, **A** is the answer.

## Critical files

- `Engine/Source/Graphics/Objects/Pipeline.h` — `PipelineInfo::pDescriptorInfos` declaration (`:102`); `PipelineInfo mInfo` member (`:143`).
- `Engine/Source/Graphics/Objects/Pipeline.cpp` — `Pipeline::Create` scan-to-max + `[i+1..i+5]` Model auto-append + literal `[3]` access + `mbPerCommandBuffer` loop (`:100-149`).
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp` — first-empty-slot scan (`:13`); `mpPipelines` construction (`:45`).
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp` / `PipelineDescriptorWriter.cpp` — descriptor-array iterations to the max.
- `Common/DataFile.h` — `ShaderHeader::kiMaxDescriptorSetLayoutBindings = 32` (`:299`); **read-only** — the `.pack` format constant stays.

## Out of scope

- The `.pack`/DataPacker descriptor-count format and `ShaderHeader::kiMaxDescriptorSetLayoutBindings` — unchanged; this only concerns `Pipeline`'s runtime inline mirror.
- The pipeline registration/ownership rework (`Graphics/Architecture_PipelineRegistrationOwnership.md`) and the bindless lifecycle consolidation (`Graphics/Managers/Architecture_BindlessSlotLifecycle.md`) — those reshape the same `Pipeline::Create`/`Write` paths; sequence with them, do not interleave.
- Any change to descriptor *semantics* (set/binding numbers, the `kModel` auto-append behavior) — preserve exactly.

## Notes

- **Decision plan (present options).** Resolve A/B/C via `/external-grill-plan` before any edit; gather the populated-slot distribution first.
- **Invariant exposure:** client/graphics-only. The `pDescriptorInfos` array is a runtime ctor-arg struct, not serialized — no CRC / determinism / `kiVersion` / `.pack`-layout / replay exposure (confirm `pDescriptorInfos` is never written to disk at execution). Option B touches GPU pipeline-setup paths → playtest (model render, spread passes, every `kPipeline*` pass) rather than compile-check alone.
- **Sequencing:** `Pipeline.{h,cpp}` is in the `Engine/Source/Graphics/Objects/` File Group; co-schedule with the other Objects/Pipeline plans and refresh line citations (the registration-ownership and bindless plans move the same `Create`/`Write` lines).
