# Refactor: Pipeline & Device Decomposition (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Boot-path functions in
the instance/device/pipeline files with extractable self-contained blocks or table-drivable repetition. The
review explicitly cleared the big declarative pipeline tables (`PipelineManager` ctor,
`CreateLightingShadowDependentPipelines`) as cohesive-as-is — they are not in this plan.

## Design

### Engine/Source/Graphics/Managers/InstanceManager.cpp
- Ctor (`InstanceManager.cpp:111-374`, ~264 lines): extract (a) the RenderDoc force-load registry scan
  (`:226-267`, nests 7 levels, fully self-contained) as `static void TryLoadRenderDocDll()`; (b) the
  `VK_EXT_layer_settings` build (`:135-212`) as a second helper. [~30m]
- `SelectPhysicalDevice` (`:376-496`, ~121 lines): the five copy-pasted required-feature blocks (`:447-471`)
  differ only in feature member + message — table-drive with `{&rFeatures.field, "name", "reason"}` rows;
  drops the function under 100 lines. [~15m]

### Engine/Source/Graphics/Managers/DeviceManager.cpp
- Ctor (`DeviceManager.cpp:8-383`, ~375 lines): linear boot narrative tolerated by KISS, but extract the two
  fully self-contained blocks — pipeline-cache load+validate (`:336-382`) and the maintenance9 QFOT probe
  (`:269-287`). Leave the feature-chain pNext assembly inline (pointer plumbing reads better linearly). [~45m]

### Engine/Source/Graphics/Managers/DynamicPipelines.cpp
- `CreateDepositPipeline` (`:258`): 9 parameters; its six wrapper call sites (`:295-344`) wrap arguments
  across lines. Introduce a `DepositPipelineSpec` struct mirroring the existing `ModelPipelineSpec` pattern;
  call sites become single designated-initializer literals. [~30m]
- The `CreatePipeline*` family duplicates the allocate/register tail 7× (`:128-130, 162-164, 198-200,
  235-237, 270-272, 359-361, 390-392`: index variable + `push_back` + `Create` + `insert_or_assign`) — use
  `mPipelines.back()` or a private `AddPipeline(...)` helper. `CreateModelPipeline`/`CreateModelPipelineShadow`
  also duplicate the chunk-lookup + vertex-shader-select preamble (`:39-44` vs `:79-84`) — extract a 5-line
  helper. [~30m]

### Optional (take only if the file is being touched anyway)
- `CreateSmokeWindPipelines` (`PipelineManager.cpp:539-696`, ~158 lines): four A/B near-duplicate pairs
  differing in 2-4 fields; legitimate ~80-line compression via the file's existing desc-table style
  (`pShadowBlurDescs`, `:296-302`), but a flat table is also acceptable per KISS. [~30m, optional]
- `DeviceManager.h:26-29`: four correlated capability bools → `common::Flags<DeviceCapability>` per root
  CLAUDE.md; mechanical but touches external readers — low value relative to churn. [~30m, optional]

## Critical files
- `Engine/Source/Graphics/Managers/InstanceManager.cpp` (+ `.h` for helper declarations)
- `Engine/Source/Graphics/Managers/DeviceManager.cpp` (+ `.h`)
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp`, `DynamicPipelines.h`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (optional item only)

## Out of scope
- The Water/WaterSkyboxOne descriptor-table duplication (`PipelineManager.cpp:434-494`) — **intentional**
  binding mirroring; its structural fix is `Architecture_ShaderCpuContractConstants.md` item 4, not a DRY
  pass here.
- `PipelineManager` ctor and `CreateLightingShadowDependentPipelines` — cleared as cohesive declarative
  tables; do not split.
- Behavior changes — pure extraction/table-driving; the boot sequence order is preserved exactly.

## Acceptance criteria
- Client builds clean; instance/device/pipeline creation logs and behavior identical; extracted helpers are
  file-local (`static` or private members).

## Notes
- No determinism/CRC exposure — boot/recreate paths only.
- Shares `PipelineManager.cpp`/`DynamicPipelines.cpp` with `Architecture_LayerSeams.md`,
  `Architecture_ShaderCpuContractConstants.md`, and `Refactor_DeadCodeRemoval.md` — co-schedule (File Groups).

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run). All items
confirmed:

- `InstanceManager` ctor begins `:111`; `ReadLayerProperties` call at `:121`; the five copy-pasted
  required-feature blocks land in the `:447-471` range (each a feature check + `MessageBox` using
  `game::kGameName`).
- `CreateDepositPipeline` at `DynamicPipelines.cpp:258` with 9 parameters; six thin wrappers at `:293-344`
  (`CreatePipelineSmokeAxisAligned`/`Smoke`/`WindDepositA`/`WindDepositB`/`WindDepositAxisAlignedA`/
  `WindDepositAxisAlignedB`). The allocate/register tail appears 7× — `push_back` + `Create` +
  `insert_or_assign` pairs at `:129/150, :163/183, :199/220, :236/255, :271/290, :360/378, :391/410`. The
  chunk-lookup + vertex-shader-select preamble duplication confirmed (`:39-44`-area vs `:79-84`).
- `CreateSmokeWindPipelines` starts at `PipelineManager.cpp:539` (previous function ends `:537`), matching the
  cited range.
