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

### Engine/Source/Graphics/Objects/Pipeline.cpp + ModelPipeline.cpp
- Move the `kModel` auto-append out of `Pipeline::Create` (`Pipeline.cpp:100-135`) into
  `ModelPipeline::Create`. Destination decided (see Decision note): `ModelPipeline::Create` is the sole
  producer of the `kModel` flag (`ModelPipeline.cpp:18` — sets it into the first empty descriptor slot;
  no other engine or game site sets `kModel`, and all model pipelines flow through
  `DynamicPipelines::CreateModelPipeline` → `ModelPipeline::Create`). Append the five follow-on
  `DescriptorInfo` entries in `ModelPipeline::Create` immediately after setting the flag, into its local
  `pipelineInfo` copy — the same five manager-owned objects `Pipeline::Create` binds today:
  `gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures` (`Pipeline.cpp:114-115`),
  `.mShadowBlurTexture` (`:119`), `.mSmokeTextureOne` (`:122-124` incl. `kSamplerSmoke`),
  `gpBufferManager->mMeshDataStorageBuffers` at `shaders::kiModelBindingMeshData` (`:127-129`),
  `gpBufferManager->mJointMatrixStorageBuffers` at `shaders::kiModelBindingJointMatrix` (`:132-134`).
  Preserve the two ASSERTs (slot headroom, next-slot-empty) and the explanatory comments at the new site.
  Delete the whole append loop from `Pipeline::Create`; the generic `Pipeline` wrapper stops knowing about
  lighting/shadow/smoke passes. Do NOT move `PipelineDescriptorWriter`'s `kModel` descriptor *write*
  handling (`WriteModelDescriptor`, dispatched at `PipelineDescriptorWriter.cpp:508`) — only the
  info-append policy moves. `PipelineManager` was rejected as destination: it never builds model
  descriptor lists, and model-pipeline creation happens at game collection init via `DynamicPipelines`
  (e.g. `MissilesRender.cpp:23`), outside `PipelineManager`'s constructor enumeration. [~1h]
- Delete the single-material assert block in `Pipeline::Create`
  (`if (!bFromMultimaterial && mInfo.pDescriptorInfos[3].flags & kModel)`, `Pipeline.cpp:137-141`).
  It is unreachable dead code: the only `kModel` producer is `ModelPipeline::Create`, which always calls
  the inner `Create(pipelineInfo, true)` (`ModelPipeline.cpp:101`), so `bFromMultimaterial` is always true
  whenever a `kModel` descriptor exists. Material-count validation already lives at `ModelPipeline`'s
  trust boundary (`ModelPipeline.cpp:36-40`). Do not re-home the assert. [~15m]

### Engine/Source/Graphics/Objects/Pipeline.cpp + Managers/TextureDescriptors.{h,cpp}
- Make registration lifecycle symmetric via option (b), decided (see Decision note): keep the
  registration calls where they are (inside the `PipelineDescriptorWriter` write walk — the
  `ShouldRegisterBinding` gate `PipelineDescriptorWriter.cpp:51-54` and its call sites `:76-78`,
  `:103-105`, `:219-221`, `:531-533`; `RegisterCombinedSamplerBindings` `:270-315` including the
  `mBindlessArrayConsumers.try_emplace` at `:294`) and add
  `TextureDescriptors::UnregisterPipeline(Pipeline* pPipeline)` that erases every record whose
  `pPipeline` matches, from all three registries (`TextureDescriptors.h:112-114`):
  `mTextureBindings` (erase matching `TextureBinding` elements; erase a CRC's map entry when its vector
  empties), `mBindlessArrayConsumers` (same per-vector erase + empty-entry cleanup), and
  `mStandaloneSamplerBindings` (erase matching elements). Mirror the existing keyed-unregister precedent
  `UnregisterBindingsForKey` (`TextureDescriptors.h:45`). Call it from `Pipeline::Destroy`
  (`Pipeline.cpp:178`), after the early-return guard so no-op destroys stay no-op — this also covers the
  `Destroy()` at the top of `Pipeline::Create`, so any out-of-rebuild recreate purges its stale entries
  before re-registering. Keep `ClearTextureBindings()` at `PipelineManager` rebuild
  (`PipelineManager.cpp:84`) as belt-and-suspenders; do not remove it. [~2h]

### Engine/Source/Graphics/Objects/ModelPipeline.cpp + Pipeline.h
- Formalize the pre-`Create` external-layout protocol: `ModelPipeline::Create` writes
  `mpPipelines[i].mVkExternalDescriptorSetLayout` (`ModelPipeline.cpp:93`, redundantly re-assigned inside
  `Pipeline::Create` at `Pipeline.cpp:94-98`) and `mVkExternalDescriptorSetLayoutSet1`
  (`ModelPipeline.cpp:98`, consumed `PipelineCreator.cpp:196,223`, `PipelineDescriptorWriter.cpp:387`)
  before calling `Create`, and the contract only works because `Destroy`-at-top-of-`Create` deliberately
  skips clearing these externals. Pass both through `PipelineInfo` (or `Create` parameters) instead of
  member poking; delete the redundant re-assign. [~30m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.h`, `Pipeline.cpp`
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp` (external-layout protocol consumers only; registration calls stay put)
- `Engine/Source/Graphics/Objects/ModelPipeline.cpp`
- `Engine/Source/Graphics/Managers/TextureDescriptors.h`, `TextureDescriptors.cpp`

## Out of scope
- Deleting the dead `Recreate`/`ReCreate` APIs (the dead APIs and their snapshot members are gone; the
  rebuild-only lifecycle invariant is documented in `Objects/AGENTS.md` + the `PipelineDescriptorWriter.cpp`
  registration block; this plan makes that documented invariant structurally enforced).
- `WriteModelDescriptor`'s copy-paste decomposition — `Graphics/Refactor_WriteDescriptorDecomposition.md`.
- The set-index resolution quadruplication — `Graphics/Architecture_DescriptorSetIndexDedup.md`.
- The bindings-15/16 constant unification — `Graphics/Architecture_ObjectsShaderCpuConsistency.md`.
- Any behavior change to descriptor contents, binding numbers, or the NVIDIA-hang workaround.

## Acceptance criteria
- Every registration a `Pipeline` plants (`mTextureBindings`, `mBindlessArrayConsumers`,
  `mStandaloneSamplerBindings`) is removed by `Pipeline::Destroy` via
  `TextureDescriptors::UnregisterPipeline`; `Pipeline::Create` contains no named
  lighting/shadow/smoke/mesh-data resources and no `kModel` scan (the append and its ASSERTs live in
  `ModelPipeline::Create`); the dead slot-3 single-material assert is gone; client builds clean and a
  settings-recreate cycle (multisampling toggle) renders identically.

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.
- `Documents/Plans/Graphics/PipelineDescriptorInfosRightSize.md`: never interleave changes to the shared scan-to-max `Pipeline::Create` / `PipelineDescriptorWriter::Write` paths.
- `Documents/Plans/Graphics/Managers/Refactor_PipelineManagerSplit.md`: never interleave with its PipelineManager file-shape change; refresh citations after it lands.

## Notes
- Architectural ownership move, client-only: no determinism/CRC, `kiVersion`, replay, or network exposure.
  Cold path (boot / settings recreate / device loss), but needs a settings-recreate smoke test.
- Decision (2026-07-03): both pre-staged grill decisions resolved by code analysis; no open decisions
  remain — execute as written. (1) `kModel` append destination is `ModelPipeline::Create`: it is the sole
  producer of the flag (`ModelPipeline.cpp:18`; no other engine/game site sets `kModel`), and
  model-pipeline creation runs at game collection init through `DynamicPipelines`, not inside
  `PipelineManager`'s constructor — so `PipelineManager` was never the creation choke point and is the
  wrong altitude. (2) Registration lifecycle is option (b) (symmetric `UnregisterPipeline` from
  `Pipeline::Destroy`): option (a) was rejected because the registration calls are computed per-binding
  inside the descriptor-write walk (reflection-resolved binding indices, set-0 exclusion, CRC vs pointer
  vs bindless-array shapes — `ShouldRegisterBinding` / `RegisterCombinedSamplerBindings`), so hoisting
  them into `PipelineManager` would duplicate that walk or require collect-and-replay machinery; and (a)'s
  "keep Objects free of manager-registry writes" premise is not a repo layering rule — Objects write
  managers through `gp*` globals pervasively, the Frame layer registers into the same registry
  (`IslandTerrainResidency.cpp:162`), and the codebase's layering violation criterion is engine *types*
  naming game concepts, not call direction. (b) is also the smaller diff and directly enforces the
  documented rebuild-only invariant against any future out-of-rebuild recreate.
- The dead recreate APIs are already gone, so no live code path violates the lifecycle; land this before/with
  the two Refactor decomposition plans (they restructure the same functions; co-schedule or refresh citations).
- Cross-queue sequencing: `Graphics/Managers/Architecture_BindlessSlotLifecycle.md` (Large) consolidates
  the same `TextureDescriptors.{h,cpp}` registration surface — sequence the two plans, never run them
  concurrently; whichever lands second re-targets the reshaped registry API.
- Line cites refreshed against source on 2026-07-03 (same pass as the Decision note). `Objects/AGENTS.md`
  carries a "Rebuild-only pipeline lifecycle" section stating this plan's target invariant; update that
  section when `UnregisterPipeline` lands (the "`Pipeline::Destroy` does **not** unregister" sentence
  becomes false).
