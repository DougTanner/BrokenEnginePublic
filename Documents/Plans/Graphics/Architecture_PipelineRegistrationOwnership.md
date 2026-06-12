# Architecture: Pipeline Registration Ownership

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). The directory's
riskiest property: registration happens in the Objects layer, un-registration in the Managers layer.
`PipelineDescriptorWriter::Write` plants raw `Pipeline*` back-references into
`gpTextureManager->mTextureDescriptors` registries, but `Pipeline::Destroy` never unregisters — the only
cleanup is `ClearTextureBindings()` during whole-`PipelineManager` reconstruction. Any future
out-of-rebuild `Pipeline::Create` leaves stale/duplicate `Pipeline*` entries that
`RewriteSamplerDescriptors` (`TextureDescriptors.cpp:334,368`) would use after free. On top of that,
`Pipeline::Create` hardcodes render-feature policy (lighting/shadow/smoke resources) that belongs to its
owner, and `ModelPipeline` configures inner pipelines by poking public members before `Create`.

## Design

### Engine/Source/Graphics/Objects/Pipeline.cpp + Managers/PipelineManager.{h,cpp}
- Move the `kModel` auto-append out of `Pipeline::Create` (`Pipeline.cpp:77-112`) into the layer that owns
  the appended resources — it binds five manager-owned objects by name:
  `gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures` (`:91-92`), `.mShadowBlurTexture`
  (`:96`), `.mSmokeTextureOne` (`:101`), `gpBufferManager->mMeshDataStorageBuffers` (`:106`),
  `gpBufferManager->mJointMatrixStorageBuffers` (`:111`). Destination (grill): `PipelineManager` (which
  already owns rebuild) or `ModelPipeline`/`DynamicPipelines` (the only consumers of `kModel`). The generic
  `Pipeline` wrapper stops knowing about lighting/shadow/smoke passes. [~1h]
- Replace the magic descriptor-slot assumption in the single-material assert —
  `mInfo.pDescriptorInfos[3].flags & kModel` (`Pipeline.cpp:114-118`) hardcodes slot 3 as "the" model slot
  while the append loop directly above scans all slots; reuse the scan result. [~15m]

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp + Managers/TextureDescriptors.{h,cpp}
- Make registration lifecycle symmetric and owned at one altitude. Options (grill): (a) move the
  registration calls (`RegisterTextureBinding` `PipelineDescriptorWriter.cpp:103,125,146,475,480,509,514`,
  `RegisterStandaloneSamplerBinding` `:68,429`, `mBindlessArrayConsumers.try_emplace` `:494-495`) up into
  `PipelineManager`'s rebuild flow, next to the `ClearTextureBindings()` that undoes them
  (`PipelineManager.cpp:62`); or (b) keep them where they are but have `Pipeline::Destroy`
  (`Pipeline.cpp:155-210`) unregister its own entries via new `TextureDescriptors::UnregisterPipeline(Pipeline*)`.
  Option (a) keeps Objects free of manager-registry writes (recommended); (b) is smaller but leaves the
  layer inversion. [~2h]

### Engine/Source/Graphics/Objects/ModelPipeline.cpp + Pipeline.h
- Formalize the pre-`Create` external-layout protocol: `ModelPipeline::Create` writes
  `mpPipelines[i].mVkExternalDescriptorSetLayout` (`ModelPipeline.cpp:73`, redundantly re-assigned inside
  `Pipeline::Create` at `Pipeline.cpp:72-75`) and `mVkExternalDescriptorSetLayoutSet1`
  (`ModelPipeline.cpp:78`, consumed `PipelineCreator.cpp:193,220`, `PipelineDescriptorWriter.cpp:274`)
  before calling `Create`, and the contract only works because `Destroy`-at-top-of-`Create` deliberately
  skips clearing these externals. Pass both through `PipelineInfo` (or `Create` parameters) instead of
  member poking; delete the redundant re-assign. [~30m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.h`, `Pipeline.cpp`
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp`
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp`
- `Engine/Source/Graphics/Managers/PipelineManager.h`, `PipelineManager.cpp`
- `Engine/Source/Graphics/Managers/TextureDescriptors.h`, `TextureDescriptors.cpp`

## Out of scope
- Deleting the dead `Recreate`/`ReCreate` APIs — `Graphics/Architecture_ObjectsDeadRecreateApis.md`
  (land it first; it removes the only would-be violators).
- `WriteModelDescriptor`'s copy-paste decomposition — `Graphics/Refactor_WriteDescriptorDecomposition.md`.
- The set-index resolution quadruplication — `Graphics/Architecture_DescriptorSetIndexDedup.md`.
- The bindings-15/16 constant unification — `Graphics/Architecture_ObjectsShaderCpuConsistency.md`.
- Any behavior change to descriptor contents, binding numbers, or the NVIDIA-hang workaround.

## Acceptance criteria
- No `gpTextureManager->mTextureDescriptors` registration write remains in `Objects/` (option a), or every
  registration has a symmetric unregistration on `Pipeline::Destroy` (option b); `Pipeline::Create` contains
  no named lighting/shadow/smoke/mesh-data resources; client builds clean and a settings-recreate cycle
  (multisampling toggle) renders identically.

## Notes
- Architectural ownership move, client-only: no determinism/CRC, `kiVersion`, replay, or network exposure.
  Cold path (boot / settings recreate / device loss), but needs a settings-recreate smoke test.
- Grill decisions pre-staged: (1) destination layer for the `kModel` append, (2) registration option (a)
  vs (b).
- Land after `Architecture_ObjectsDeadRecreateApis.md` and before/with the two Refactor decomposition
  plans (they restructure the same functions; co-schedule or refresh citations).
- Cross-queue sequencing: `Graphics/Managers/Architecture_BindlessSlotLifecycle.md` (Large) consolidates
  the same `TextureDescriptors.{h,cpp}` registration surface — sequence the two plans, never run them
  concurrently; whichever lands second re-targets the reshaped registry API.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Registration sites exact: `RegisterTextureBinding` at `PipelineDescriptorWriter.cpp:103,125,146,475,480,509,514`
  (all seven), `RegisterStandaloneSamplerBinding` at `:68,429`, `mBindlessArrayConsumers.try_emplace` at
  `:494-495`. `Pipeline::Destroy` (`Pipeline.cpp:155-210`) frees sets/layouts/pipeline/indirect buffer and
  never touches `gpTextureManager->mTextureDescriptors`; the only cleanup is `ClearTextureBindings()`
  (`PipelineManager.cpp:62`, `TextureDescriptors.cpp:390-395`). `RewriteSamplerDescriptors` dereferences the
  raw `Pipeline*` back-references at `TextureDescriptors.cpp:334,368` (and `:385` for bindless consumers) —
  the use-after-free path described.
- `kModel` auto-append resource citations exact (`Pipeline.cpp:91-92,96,101,106,111` inside `:77-112`);
  `ModelPipeline::Create` is the only producer of `kModel` descriptors (repo grep), so the proposed
  destinations are complete.
- Slot-3 assert exact (`Pipeline.cpp:114-118` reads `pDescriptorInfos[3]` while the loop above scans all slots).
- External-layout protocol citations exact: `ModelPipeline.cpp:73` (Set 0 external — redundant with the
  unconditional re-assign in `Pipeline::Create` `Pipeline.cpp:72-75`), `:78` (Set 1 external — load-bearing,
  consumed at `PipelineCreator.cpp:193,220` and `PipelineDescriptorWriter.cpp:274`); `Pipeline::Destroy`
  clears owned layouts but never the `mVkExternal*` pointers, confirming the skip-clearing contract.
- Scoring nuance for the orchestrator: once `Architecture_ObjectsDeadRecreateApis.md` lands (dead APIs gone
  + invariant documented), no live code path violates the lifecycle — the asymmetry is a latent hazard for
  future out-of-rebuild creates, not an active bug. Impact 3 assumes valuing that future-proofing; Impact 2
  is defensible.
