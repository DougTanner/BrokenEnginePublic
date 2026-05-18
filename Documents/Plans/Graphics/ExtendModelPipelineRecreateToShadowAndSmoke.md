# Extend Model Pipeline Recreate To Shadow And Smoke Textures

## Context

Fix A (this session) wired `ModelPipeline::Recreate()` into the `kLightingTextures` stage of `PipelineManager::RecreatePipelineGroups` to fix terrain going flat after dragging *Tweaks → Lighting → Write → Texture Multiplier Start*. Root cause: the `kModel` descriptor flag in `Engine/Source/Graphics/Objects/Pipeline.cpp:64-98` auto-appends three follow-on sampler bindings on every model pipeline:

```
[i+1]  combined sampler array  ← mppLightingFinalTextures   (= mpCombineTextures)
[i+2]  combined sampler        ← mShadowBlurTexture
[i+3]  combined sampler        ← mSmokeTextureOne
```

When the underlying texture's `Texture::Create` runs again, the `VkImageView` handle is replaced (Texture::Create internally calls `Destroy()` first per `Engine/Source/Graphics/Objects/Texture.cpp:169`), but the descriptor sets on every `kModel`-flagged pipeline cache the old `VkImageView` at write time. The driver eventually returns `VK_ERROR_SURFACE_LOST_KHR` from sampling undefined memory.

Fix A only addresses the `mppLightingFinalTextures` half. The same bug class still exists for the other two textures:

- **`kShadowTextures`** recreates `mShadowBlurTexture` via `CreateShadowTextures` (referenced from `Graphics::RecreateResources` at `Graphics.cpp:487-493`). The `kShadowTextures` flag fires when the shadow texture multiplier or related shadow sizing cvars change.
- **`kSmokeTextures`** recreates `mSmokeTextureOne` via `CreateSmokeTextures`. The `kSmokeTextures` flag fires when `gSmokeSimulationPixels`, `gSmokeSimulationArea`, `gSmokeTrailPower`, or `gSmokeTrailAlpha` change (`Graphics.cpp:465-476`).

Both paths use `bSelectiveRecreation` (only `kObjectShadows` opts out per `Graphics.cpp:591`), so they currently leave `kModel` pipelines with stale descriptors. The user has not reported visible breakage on those sliders, but the same `SURFACE_LOST` failure mode is reachable from them.

## Design

Mirror the Fix A pattern in two additional stages:

1. **Stage 2 (`CreatePipelineShadows`)** at `PipelineManager.cpp:775-779` — when `kShadowTextures` fires (note: `kObjectShadows` is excluded from `bSelectiveRecreation`, so it already triggers a full pipeline-manager rebuild and needs no extra work). After `CreatePipelineShadows()`, add the same `for (DynamicModelPipelineType eType : {…}) { for (auto& […]) { rpModelPipeline->Recreate(); } }` loop guarded on `(flags & kShadowTextures)`.

2. **Smoke stage** — `kSmokeTextures` currently lands in the dynamic smoke/wind deposit pipeline section at `PipelineManager.cpp:797-813`. Add the same recreation loop guarded on `(flags & kSmokeTextures)`.

To avoid three near-identical copy-pasted blocks, factor the recreate loop into a `private` helper on `PipelineManager` (or a free function in the same TU):

```cpp
// Recreate kModel-flagged pipelines because their cached descriptor sets reference
// VkImageView handles for mppLightingFinalTextures / mShadowBlurTexture / mSmokeTextureOne
// (auto-appended at Pipeline.cpp:64-98). Any flag that destroys+recreates one of those
// textures must call this, or kModel pipelines sample destroyed memory → SURFACE_LOST.
void PipelineManager::RecreateAllModelPipelines(std::string_view triggerName)
{
    int64_t iCount = 0;
    for (DynamicModelPipelineType eType : {kDynamicModelPipelineModel, kDynamicModelPipelineModelShadow})
    {
        for (auto& [rCrc, rpModelPipeline] : mDynamicPipelines.mModelPipelineMaps[eType])
        {
            rpModelPipeline->Recreate();
            ++iCount;
        }
    }
    LOG(kGraphics, kDebug, "RecreateAllModelPipelines: trigger={} count={}", triggerName, iCount);
}
```

Then call `RecreateAllModelPipelines("kLightingTextures" / "kShadowTextures" / "kSmokeTextures")` at each of the three stages. Net diff is one new helper plus three call sites; the helper is the single place a future maintainer touches if a fourth flag joins this family.

Ordering: each call must come **after** the corresponding `Create*Textures()` finishes so the helper reads the freshly-rebuilt `mppLightingFinalTextures` / `mShadowBlurTexture` / `mSmokeTextureOne` slots, not the dangling pre-recreate handles.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.h` — declare `RecreateAllModelPipelines(std::string_view)` private member.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` `PipelineManager::RecreatePipelineGroups` — replace the inline kLightingTextures loop with the helper call, add two more call sites for kShadowTextures (after `CreatePipelineShadows`) and kSmokeTextures (after the smoke-deposit dynamic-pipeline loop).
- `Engine/Source/Graphics/Objects/Pipeline.cpp:64-98` — comment site only; add a one-line cross-reference to `RecreateAllModelPipelines` so future kModel-flag readers know about the recreate contract.

## Out of scope

- The `kModel` auto-append model itself. Replacing it with explicit per-pipeline descriptor declarations would eliminate the need for any recreate-side bookkeeping but is a larger refactor and not justified by this bug alone.
- `TextureDescriptors::UpdateArrayBindingsForKey`-style in-place descriptor patching. Fix A chose full pipeline recreate to match the existing Stage 1 idiom; this plan continues that choice for consistency.
- `kObjectShadows` — already excluded from `bSelectiveRecreation`, so it triggers a full `PipelineManager` rebuild and naturally rebuilds every kModel pipeline.
- Removing the diagnostic `kTemp` logs added during Fix A diagnosis — orthogonal cleanup, tracked separately.
- Auditing whether the `kModel` shadow variant's fragment shader actually samples lighting / smoke (it does not need to), since the stale descriptor is illegal regardless of whether the shader reads it.

## Acceptance criteria

- After this plan lands, dragging any cvar that fires `kShadowTextures` (shadow texture multiplier sliders) or `kSmokeTextures` (`gSmokeSimulationPixels`, `gSmokeSimulationArea`, `gSmokeTrailPower`, `gSmokeTrailAlpha`) produces **no `VK_ERROR_SURFACE_LOST_KHR`** and **no visual corruption on models** (units / ships / etc.).
- A grep for `mModelPipelineMaps` in `PipelineManager.cpp` returns exactly one iteration site (the helper), not three copies.
- The helper's `LOG(kGraphics, kDebug, …)` emits the correct trigger name and a count > 0 each time a relevant flag fires.

## Notes

- Test path: in *Tweaks → Lighting → Write*, change *Deposit Texture Multiplier* (still in the kLightingTextures family — should already work post-Fix A); then change any shadow-texture-multiplier slider in *Tweaks → Shadow*; then change *Smoke Simulation Pixels* in *Tweaks → Smoke*. All three should leave the scene visually intact.
- Re-verify the kTemp logs from the diagnosis session show: (a) the matching `Create*Textures` call, (b) the new `RecreateAllModelPipelines` log with non-zero count, (c) no follow-up `Graphics::Destroy: meDestroyType=5` (kSurface) within the next ~150 frames.
- If the user reports the same flat-terrain symptom on a non-multiplier cvar that nonetheless fires `kLightingTextures`, that's already covered by Fix A — this plan does not add new coverage there.
