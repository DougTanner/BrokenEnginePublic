# Refactor: Graphics Objects Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Mechanical residue in the Graphics/Objects RAII wrappers, deduped against the pipeline-cluster live plans (registration ownership, descriptor-infos right-size, ReviewSweepQuickWins). The correctness cluster (staging RAII, dead VkDeviceMemory, index-start validation) is `Refactor_StagingBufferRaii.md`.

## Design

### Engine/Source/Graphics/Objects/Pipeline.cpp
- One comment line at `Destroy()` (:178-234) documenting that `mModelMaterialsStorageBuffer` deliberately survives (freed only by the member dtor; `WriteModelDescriptor` reuses it via the `mDeviceLocalVkBuffer == VK_NULL_HANDLE` guard, `PipelineDescriptorWriter.cpp:127`) [~5m]

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Parameterize `SetupIndirectBuffer` (:94-128) on element size/type and reuse it in `CreateComputePipeline`'s `kIndirectHostVisible` branch (:679-705) — the two blocks duplicate the slot-count/create/VMA-assert/map/zero sequence, differing only in `VkDrawIndexedIndirectCommand` vs `VkDispatchIndirectCommand` (and the target mapped-pointer member). **Sequencing:** `WindowedLightingShadowDispatch` touches the `kIndirectHostVisible | kCompute` branch — land after it or refresh its citations [~30m]
- Move the `miIndirectSlotCount` assignment (:96-97) inside the indirect-flag branches so non-indirect pipelines don't carry a nonzero count with a null buffer (`SetupIndirectBuffer` is called unconditionally from `CreateGraphicsPipeline`, :643). Complements `ReviewSweepQuickWins` item 10, which only fixes the stale comment at `Pipeline.h:163` [~5m]

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp
- Collapse the triplicated `Update{CombinedImageSampler,Sampler,StorageImage}` (:590-681 — identical except `descriptorType`, `imageLayout`, and two `VkDescriptorImageInfo` fields) into one private helper [~15m]
- Define a `kSamplerAny` combined mask next to the sampler flags (`Pipeline.h:17-24`) and replace the 7-flag OR chain at :507. Note the chain omits `kSamplerBorderWhite` — only ever used alongside `kCombinedSamplers` (`PipelineManager.cpp:391,462`), so including it in the mask is behavior-neutral (the standalone-sampler branch at :516 excludes `kCombinedSamplers`); include it and say so in a comment [~5m]
- Unify the helper completion contract: `PushCombinedImageSamplerWrite` (:73) and `WriteModelDescriptor`'s sampler/array blocks (:100, :117) push their own writes while the model-materials SSBO write is only configured (:174-178) and pushed by the outer loop (:541) — make every helper push its own writes [~15m]

### Engine/Source/Graphics/Objects/Shader.cpp
- Add the leading `Destroy()` to `Shader::Create` (:18) that all sibling `Create`s have — latent `VkShaderModule` leak if ever re-invoked (currently unreachable: shaders constructed once via `try_emplace`, `PipelineManager.cpp:76`) [~5m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.{h,cpp}`, `PipelineCreator.cpp`, `PipelineDescriptorWriter.cpp`, `Shader.cpp`

## Out of scope
- The `!bFromMultimaterial && mInfo.pDescriptorInfos[3].flags & kModel` guard (`Pipeline.cpp:137-141`) — inside `Architecture_PipelineRegistrationOwnership`'s scope (its single-material-assert item targets the same lines)
- `pDescriptorInfos[32]` sizing, registration ownership, `RecordDrawIndirectSet2` ASSERT (live plans)
- Cosmetic residue (e.g. the redundant inner cast in `WriteIndirectBuffer`'s ternary, `Pipeline.cpp:362`) — below the action bar
- `CreateMultiSetPipelineLayout`'s 8 params and the stale `uniformTextureVkDescriptorSetLayoutCreateInfo` name (optional polish; single-caller helpers in plan-cluster territory — rename when those plans touch the lines)
- The Pipeline record-prologue 6× repetition (small; entangled with the Record* family shape — accept)

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.

## Notes
- Invariant exposure: none — client/graphics-only, create/rebuild-time paths. All items sit inside the pipeline cluster's shared files: respect the Order.md pipeline-cluster sequencing (never interleave with `BindlessSlotLifecycle`; refresh after `Refactor_PipelineManagerSplit` / `WindowedLightingShadowDispatch` if they land first)
- No open decisions — mechanical

## Verification Notes
- All surviving items re-verified against source (2026-07-02); line cites current.
- Removed during verification: (a) the "dead kModel slot-3 guard" delete — the deadness claim itself verified (`DescriptorFlags::kModel` is set only at `ModelPipeline.cpp:18`, and that path always calls `Create(pipelineInfo, true)` at `ModelPipeline.cpp:101`, so `bFromMultimaterial` is true whenever `kModel` is present), but the guard is explicitly in `Architecture_PipelineRegistrationOwnership`'s Design scope, which owns its fate; (b) the redundant-cast drop at `Pipeline.cpp:362` — cosmetic-only.
