# Refactor: Graphics Objects Quick Wins

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Mechanical residue in the Graphics/Objects RAII wrappers, deduped against the pipeline-cluster live plans (registration ownership, descriptor-infos right-size, ReviewSweepQuickWins). The correctness cluster (staging RAII, dead VkDeviceMemory, index-start validation) is `Refactor_StagingBufferRaii.md`.

## Design

### Engine/Source/Graphics/Objects/Pipeline.cpp
- Delete the dead `!bFromMultimaterial && mInfo.pDescriptorInfos[3].flags & kModel` guard (:137-141) — `DescriptorFlags::kModel` is set only at `ModelPipeline.cpp:18`, whose path always calls `Create(pipelineInfo, true)` (`ModelPipeline.cpp:101`), so `bFromMultimaterial` is true whenever `kModel` is present; the magic `[3]` also contradicts the scan-all-slots loop above (:102). Sits in the `kModel` auto-append region `Architecture_PipelineRegistrationOwnership` reshapes — land as a rider or refresh citations [~5m]
- Drop the redundant inner cast in the ternary at :362 [~5m]
- One comment line at `Destroy()` (:178-234) documenting that `mModelMaterialsStorageBuffer` deliberately survives (freed only by the member dtor; guarded via `mDeviceLocalVkBuffer == VK_NULL_HANDLE` in `WriteModelDescriptor`) [~5m]

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Parameterize `SetupIndirectBuffer` (:99-123) on element size/type and reuse it in `CreateComputePipeline` (:679-705) — the two blocks duplicate the slot-count/create/VMA-assert/map/zero sequence, differing only in `VkDrawIndexedIndirectCommand` vs `VkDispatchIndirectCommand`. **Sequencing:** `WindowedLightingShadowDispatch` touches the `kIndirectHostVisible | kCompute` branch — land after it or refresh its citations [~30m]
- Move the `miIndirectSlotCount` assignment (:96-97) inside the indirect-flag branches so non-indirect pipelines don't carry a nonzero count with a null buffer [~5m]

### Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp
- Collapse the triplicated `Update{CombinedImageSampler,Sampler,StorageImage}` (:590-681 — identical except `descriptorType` + two `VkDescriptorImageInfo` fields) into one private helper [~15m]
- Define a `kSamplerAny` combined mask next to the sampler flags (`Pipeline.h:17-24`) and replace the 7-flag OR chain at :507 [~5m]
- Unify the helper completion contract: `:100,117,73` push their own writes while the model-materials SSBO write is only configured (:174-178) and pushed by the outer loop (:541) — make every helper push its own writes [~15m]

### Engine/Source/Graphics/Objects/Shader.cpp
- Add the leading `Destroy()` to `Shader::Create` (:18) that all sibling `Create`s have — latent `VkShaderModule` leak if ever re-invoked (currently unreachable: shaders constructed once via `try_emplace`, `PipelineManager.cpp:76`) [~5m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.{h,cpp}`, `PipelineCreator.cpp`, `PipelineDescriptorWriter.cpp`, `Shader.cpp`

## Out of scope
- `pDescriptorInfos[32]` sizing, registration ownership, `RecordDrawIndirectSet2` ASSERT (live plans)
- `CreateMultiSetPipelineLayout`'s 8 params and the stale `uniformTextureVkDescriptorSetLayoutCreateInfo` name (optional polish; single-caller helpers in plan-cluster territory — rename when those plans touch the lines)
- The Pipeline record-prologue 6× repetition (small; entangled with the Record* family shape — accept)

## Notes
- Invariant exposure: none — client/graphics-only, create/rebuild-time paths. All items sit inside the pipeline cluster's shared files: respect the Order.md pipeline-cluster sequencing (never interleave with `BindlessSlotLifecycle`; refresh after `Refactor_PipelineManagerSplit` / `WindowedLightingShadowDispatch` if they land first)
- No open decisions — mechanical
