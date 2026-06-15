# Refactor: ModelPipeline Member Cleanups

## Context

Source: /external-refactor-clean + /external-architecture-review on `Engine/Source/Graphics/Objects`
(non-recursive). `ModelPipeline`'s member block accumulates two independent cleanups: a textbook bool→Flags
cluster, and a by-value inner-pipeline array sized to a 128-material maximum never approached.

## Design

### Engine/Source/Graphics/Objects/ModelPipeline.h / ModelPipeline.cpp
- Convert the scalar bools to `common::Flags<ModelPipelineFlags>`, absorbing `Create`'s two bool parameters
  (`bool bAddModelDescriptors, bool bIsShadow`, `ModelPipeline.h:20`). Members today: `mbTexturesRequested`
  (`:34`) and `mbHasTransparentMaterials` (`:36`) — convert these two survivors. (The former Recreate-snapshot
  pair `mbAddModelDescriptors` / `mbIsShadow` has **already been deleted** by
  `Graphics/Architecture_ObjectsDeadRecreateApis.md` — landed and removed — since their sole reader was the
  dead `Recreate()`. NB: `Create`'s `bAddModelDescriptors` / `bIsShadow` *parameters* survive the deletion
  — they still drive the kModel-append and alpha-blend logic in `Create`'s body — so they remain to absorb
  into the `Flags` parameter.) `mpbTransparentMaterials` stays a C array —
  `Flags` wraps a named-enumerator bitmask over one integer (128 dynamic slots don't fit) and `std::bitset`
  is banned (style rule 21). Call-site surface: the single `ModelPipeline::Create` call at
  `DynamicPipelines.cpp:22`, fed by the `ModelPipelineSpec` bool fields `bAddModelDescriptors` /
  `bIsPipelineShadow` (`DynamicPipelines.h:11-12`) — convert the spec fields to the same `Flags` type to
  avoid a bool→flag translation at the call site. [~30m]
- Right-size `mpPipelines` — `Pipeline mpPipelines[common::SceneHeader::kiMaxMaterials]`
  (`ModelPipeline.h:30`, `kiMaxMaterials = 128` at `Common/DataFile.h:86`) stores 128 by-value `Pipeline`s,
  mostly empty: each `Pipeline` embeds a full `PipelineInfo` whose
  `pDescriptorInfos[32]` (`kiMaxDescriptorSetLayoutBindings = 32`, `DataFile.h:290`) alone is 32 × 72 B ≈
  2.3 KB, so `sizeof(Pipeline)` is roughly 2.5–3 KB and each `ModelPipeline` carries ~320–380 KB of mostly
  zeroed inline storage. `DynamicPipelines` heap-allocates one `ModelPipeline` per model/pass combination
  (`mModelPipelines` / `mModelPipelineMaps[kDynamicModelPipelineCount]`, `DynamicPipelines.h:64-67`),
  multiplying the waste. First measure (`sizeof(Pipeline)`, instance count — log or `static_assert` at
  execution), then either allocate to the actual material count at `Create` (startup path;
  `Graphics::Create`'s blanket `ScopedSuppressAllocationTracking` (`Graphics.cpp:264-268`) already covers
  settings recreates — add a local suppression + `// Heap:` comment only if a tracked path appears), or
  close with a documented justification if the measured footprint is negligible. Container constraint:
  `Pipeline` is non-copyable and non-movable, so right-sizing means a non-relocating allocation
  (`std::vector<Pipeline> v(n)` constructed once and never resized, or `std::unique_ptr<Pipeline[]>`). [~45m]

## Critical files
- `Engine/Source/Graphics/Objects/ModelPipeline.h`, `ModelPipeline.cpp`
- `Engine/Source/Graphics/Managers/DynamicPipelines.h`, `DynamicPipelines.cpp` (spec fields + Create call site)

## Out of scope
- `Pipeline`'s own bool members (`mbPerCommandBuffer`, `mbTexturesRequested` — `Pipeline.h:138,163`) — a
  future Pipeline-side Flags consolidation; coordinate if both ever land together.
- The demand-load latch in `ModelPipeline::WriteIndirectBuffer` (`ModelPipeline.cpp:134-140`) vs
  `Pipeline::WriteIndirectBuffer`'s (`Pipeline.cpp:322-326`) — dropped at verification; see Verification
  Notes.
- `kiMaxMaterials` itself or any `.pack`/`SceneHeader` layout change.
- Registration/lifecycle semantics — `Graphics/Architecture_PipelineRegistrationOwnership.md`.

## Acceptance criteria
- `ModelPipeline` has no scalar bool members; `Create` takes a `Flags` parameter; if the array is
  right-sized, a boot log confirms the measured savings and rendering is identical.

## Notes
- Client-only; no determinism/CRC, `kiVersion`, replay, or network exposure (`mpbTransparentMaterials` and
  friends are render-side only; `kiMaxMaterials` untouched).
- Grill decision pre-staged: dynamic sizing vs documented-accept for `mpPipelines` (measure first).
- Sequencing: `Graphics/Architecture_ObjectsDeadRecreateApis.md` has **landed and been removed** — the
  snapshot-bool pair is already deleted, so this item converts only `mbTexturesRequested` /
  `mbHasTransparentMaterials`.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- **Dropped: the demand-load latch dedup.** The two latches share only a three-line shape
  (`if (count > 0 && !mbTexturesRequested) { latch; request; }`) with different payloads:
  `Pipeline::WriteIndirectBuffer` requests the pipeline's own `mTextureCrcs` (collected at descriptor-write
  time, `PipelineDescriptorWriter.cpp:476,510`), while `ModelPipeline::WriteIndirectBuffer` requests the
  scene chunk's full texture-CRC span read out of pack memory (`ModelPipeline.cpp:137-139`). The two are
  not redundant (model textures ride the bindless array and never enter the inner pipelines'
  `mTextureCrcs`), and neither "delegating to the Pipeline latch" nor a shared helper fits without
  parameterizing away more than the duplication saves — extracting it is over-abstraction (KISS).
- Bool cluster citations exact (`ModelPipeline.h:20,34,36`; the snapshot pair at the former `:42-43` is now
  deleted); corrected the original "four bools + update DynamicPipelines call sites" to the precise surface:
  two surviving bools, one call site (`DynamicPipelines.cpp:22`) via `ModelPipelineSpec`
  (`DynamicPipelines.h:11-12`).
- `mpPipelines` footprint claims grounded: `DescriptorInfo` is nine 8-byte members (72 B,
  `Pipeline.h:41-53`); `kiMaxDescriptorSetLayoutBindings = 32` (`DataFile.h:290`);
  `kiMaxMaterials = 128` (`DataFile.h:86`); per-instance inline storage ≈ 128 × ~2.5–3 KB. Instance
  multiplier confirmed: `DynamicPipelines` holds one heap `ModelPipeline` per model/pass
  (`DynamicPipelines.h:64-67`); measurement-first framing retained. Added the non-copyable/non-movable
  container constraint (`Pipeline.h:111` deleted copy ctor; no moves; by-value `Buffer` member).
