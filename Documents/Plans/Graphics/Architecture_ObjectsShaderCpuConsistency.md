# Architecture: Objects Shader/CPU Consistency

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). Three
shader↔C++ pairings in the pipeline path are synchronized by comment or not at all: the model bindings
15/16 exist as literals and named constants in separate sites, the push-constant material index has a live
channel and a dead decoy doubling the push range, and a pipeline-layout size knob is dead config with a
latent mismatch.

## Design

### Bindings 15/16 single-sourcing — Pipeline.h / Pipeline.cpp / ModelPipeline.h
- Move `kModelPipelineBindingMeshData`/`kModelPipelineBindingJointMatrix` (`ModelPipeline.h:8-9`) to
  `Pipeline.h` (no cycle: `ModelPipeline.h` already includes `Pipeline.h`) and use them at the `kModel`
  auto-append site, which today writes bare literals `iExplicitBinding = 15` / `= 16`
  (`Pipeline.cpp:105,110`); keep the existing consumers (`BufferManager.cpp:502,523`). GLSL counterparts:
  `set = 1, binding = 15/16` (`Engine/Data/Shaders/Model/ModelCommon.h:62,75`). Update the comment block at
  `Pipeline.cpp:78-108` to cite the constants. [~10m]
- Fix the wrong set number in the comment at `ModelPipeline.cpp:152` — says "Set 0 bindings (15, 16) are
  shared"; the shaders declare set = 1 (`ModelCommon.h:62,75`). Behavior is correct; comment only. [~5m]

### Dead `f4Material` push-constant channel — Pipeline.cpp / Engine/Data/Shaders/ShaderLayoutsBase.h
- Shaders read the material index from `f4Pipeline.w` (`Model.frag:217`, `ModelSkinned.vert:11`), written
  per-material by `ModelPipeline::RecordDrawIndirect` (`ModelPipeline.cpp:120`). But the anonymous-namespace
  `RecordPushConstants` helper (`Pipeline.cpp:15-21`) also writes `f4Material.x = uiMaterialIndex` (`:19`), and
  `ShaderLayoutsBase.h:169` documents `f4Material // x: Material index` — no shader reads `f4Material`
  (re-grep all of `Engine/Data/Shaders` at execution). Remove the dead write + fix the layout comment;
  then decide (grill): drop the `f4Material` member from `shaders::PushConstantsLayout` (halves the push
  range 32→16 bytes; requires a DataPacker shader recompile since the dual-language header changes) or
  keep the slot reserved with an honest comment. [~30m]

### Dead `uiPushConstantSize` knob — Pipeline.h / PipelineCreator.cpp
- The pipeline-layout push range comes from `mInfo.uiPushConstantSize` (`PipelineCreator.cpp:76,158,689`)
  but `RecordPushConstants` always pushes `sizeof(shaders::PushConstantsLayout)` (`Pipeline.cpp:20`), and
  no call site overrides the default — the first smaller override produces a validation failure at record
  time. Delete the knob and use `sizeof(shaders::PushConstantsLayout)` at the three layout sites (YAGNI),
  or keep it with a creation-time `ASSERT(uiPushConstantSize >= sizeof(shaders::PushConstantsLayout))`.
  Deletion recommended. [~15m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.h`, `Pipeline.cpp`
- `Engine/Source/Graphics/Objects/ModelPipeline.h`, `ModelPipeline.cpp`
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp`
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (only if the `f4Material` member is dropped)

## Out of scope
- Relocating the `kModel` auto-append itself — `Graphics/Architecture_PipelineRegistrationOwnership.md`
  (that plan moves the code these constants land in; co-schedule or land this first as the smaller diff).
- The top-level shared-constant items — `Graphics/Architecture_SharedConstantDuplication.md`.
- Any change to binding numbers, set layout, or push-constant *values* — every item is naming/dead-code
  with bit-identical GPU behavior (except the optional layout shrink, which is gated on the grill).

## Acceptance criteria
- Bindings 15/16 exist as exactly one constant pair referenced by C++ append site and BufferManager; no
  code writes `f4Material`; `uiPushConstantSize` is gone or asserted; client builds clean; if the layout
  member is dropped, DataPacker shader recompile passes and a model render smoke-test looks identical.

## Notes
- The `f4Material` member drop edits `shaders::PushConstantsLayout` in the dual-language header — pipeline
  layouts and every shader using the push-constant block change together; DataPacker recompile required.
  No `.pack` `kiVersion` or determinism/CRC exposure (render-path only).
- Grill decisions pre-staged: drop vs keep the `f4Material` slot; delete vs assert `uiPushConstantSize`.
- Cross-plan awareness for the `f4Material` drop:
  `Graphics/Managers/Architecture_ShaderCpuContractConstants.md`'s LightCombine item — `CombineData`
  (`CommandBufferRecordMain.cpp:402-406`) is memcpy'd into a `shaders::PushConstantsLayout` (`:412`) with the
  two uints riding `f4Pipeline.x/.y`, so dropping `f4Material` does not corrupt it (first 16 bytes
  untouched), but both plans edit the same struct's consumers — co-schedule or land in either order with a
  citation refresh.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Bindings 15/16: bare literals at `Pipeline.cpp:105,110`; constants at `ModelPipeline.h:8-9`; sole existing
  consumers `BufferManager.cpp:502,523` (repo grep — no other uses); GLSL `set = 1, binding = 15/16` at
  `ModelCommon.h:62,75`; no include cycle (`ModelPipeline.h:3` includes `Pipeline.h`). Comment anchors:
  `Pipeline.cpp:78` (count), `:103`/`:108` (per-binding).
- `ModelPipeline.cpp:152` comment confirmed wrong ("Set 0 bindings (15, 16) are shared") — shaders declare
  set = 1; behavior (updating only the first pipeline's Set 1 in multi-set mode) is correct.
- `f4Material` dead-channel claim re-grepped repo-wide: the only writer is `Pipeline.cpp:19`; the only other
  occurrence is the declaration `ShaderLayoutsBase.h:169`; zero reads in `Engine/Data/Shaders/**` and zero
  shader sources under `Projects/` (none exist). Live channel `f4Pipeline.w` confirmed at `Model.frag:217`,
  `ModelSkinned.vert:11` (LightingBlur{H,V}.comp also read `f4Pipeline.w` — unrelated to the material index).
  `PushConstantsLayout` is exactly `{vec4 f4Pipeline; vec4 f4Material;}` (`ShaderLayoutsBase.h:166-170`), so
  the 32→16 byte claim holds.
- `uiPushConstantSize` re-grepped: only the default at `Pipeline.h:89` and the three consumption sites
  `PipelineCreator.cpp:76,158,689` exist — no call site overrides it; `RecordPushConstants` always pushes
  `sizeof(pushConstantsLayout)` (`Pipeline.cpp:20`). Dead-knob claim exact.
- Corrected attribution: `RecordPushConstants` is an anonymous-namespace free function, not a `Pipeline`
  member.
