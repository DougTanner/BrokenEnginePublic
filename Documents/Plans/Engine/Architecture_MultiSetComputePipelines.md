# Architecture: Multi-Set Descriptor Layouts on Compute Pipelines

## Context

The engine's shader-tree descriptor-set convention (per `Engine/Data/Shaders/CLAUDE.md`) is:

- **Set 0** — global (per-frame UBOs, bindless texture array, shared samplers; owned by `TextureDescriptors` and bound externally)
- **Set 1** — per-pipeline (SSBOs, combined image samplers, storage images)
- **Set 2** — per-material (model pipelines only)

Graphics pipelines already implement this. `CreateDescriptorSetLayouts` in `Engine/Source/Graphics/Objects/PipelineCreator.cpp:107-225` splits the SPIR-V-reflected bindings by `pVertexShader->mInfo.pDescriptorSetIndices[uiBinding]` / `pFragmentShader->mInfo.pDescriptorSetIndices[uiBinding]` (populated from spirv-cross in the DataPacker and surfaced through `engine::Shader::Info::pDescriptorSetIndices`). Set 0 bindings are skipped entirely (provided externally by `gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout`), set 1 becomes the pipeline-owned `mVkDescriptorSetLayout`, set 2 becomes `mVkDescriptorSetLayoutSet2`. `vkCreatePipelineLayout` is then called with `[external Set 0, Set 1, optional Set 2]`. `BindGraphicsDescriptorSets` in `Pipeline.cpp:23-34` binds both sets in one `vkCmdBindDescriptorSets` (`firstSet=0, count=2`).

Compute pipelines do not. `PipelineCreator::CreateComputePipeline` (`PipelineCreator.cpp:578-639`) routes through `CreateSingleSetPipelineLayout` (`PipelineCreator.cpp:48-69`), which slams every binding from the compute shader's `pDescriptorBindings` into one descriptor set layout at slot 0 — set indices from `pComputeShader->mInfo.pDescriptorSetIndices` are not consulted. The compute `vkCmdBindDescriptorSets` call sites then bind that one set at `firstSet=0, descriptorSetCount=1`:

- `Pipeline::RecordCompute` (`Pipeline.cpp:277`)
- `Pipeline::RecordComputeIndirect` (`Pipeline.cpp:292`)
- Direct bind sites in `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` at lines 236, 246 (`RecordWindSpreadPipeline`), 339, 424 (`RecordSmokeSpreadPipeline`)

As a consequence, every compute shader in `Engine/Data/Shaders` currently uses set 0 for all bindings (40 violations of the convention catalogued by a recent shader-side sweep across Wind, Smoke, Shadow, Lighting compute shaders). Source verification: a grep across `*.comp` for explicit `layout(set = 0|1, ...)` returns zero matches — all compute bindings rely on the default set 0.

The global descriptor set built by `TextureDescriptors::Create` (`Engine/Source/Graphics/Managers/TextureDescriptors.cpp:23-30`) uses stage flags `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT` (UBOs) and `VK_SHADER_STAGE_FRAGMENT_BIT` (samplers / bindless image array). Compute access would violate VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358 unless those flags include `VK_SHADER_STAGE_COMPUTE_BIT` for any binding a compute shader can actually reach. `PipelineDescriptorWriter::Write` (`Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp:261-489`) is gated on `bHasExternalSet0 = rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE`; the `RouteWritesBySet` filter that drops Set 0 writes (because the global set handles them externally) only fires when that flag is set. The compute path never attaches `mVkExternalDescriptorSetLayout` today (`Pipeline::Create` at `Pipeline.cpp:58` gates the assignment on `!(mInfo.flags & kCompute)`), so the writer treats compute bindings as a flat set 1.

This plan brings the compute path into structural parity with the graphics path so authors can move compute-shader bindings to `set = 1` (and have set 0 share the global per-frame state) without further C++ edits.

## Design

Five coordinated changes, all in `Engine/Source/Graphics/`:

### 1. Widen the global Set 0 descriptor-set-layout stage flags to include compute

In `TextureDescriptors::Create` (`Engine/Source/Graphics/Managers/TextureDescriptors.cpp:23-30`), extend `stageFlags` on every binding a compute shader may sample/load:

- Binding 0 (`globalUniform`, UBO) — add `VK_SHADER_STAGE_COMPUTE_BIT`. Compute shaders (e.g., `WindSpread*`, `LongParticlesUpdate`, smoke spread) already pass the per-frame `GlobalLayout` UBO as a per-pipeline binding; once they migrate to set 0, they will read it through the global set.
- Binding 1 (`mainUniform`, UBO) — add `VK_SHADER_STAGE_COMPUTE_BIT` for symmetry (camera / view state used by some compute spread passes).
- Binding 3 (`samplerRepeat`) — add `VK_SHADER_STAGE_COMPUTE_BIT` (compute particle/wind/smoke shaders sample with this sampler).
- Binding 4 (bindless `pTextures[]`) — add `VK_SHADER_STAGE_COMPUTE_BIT` (any compute shader that wants the bindless array — e.g., particle update reading an elevation texture).
- Binding 12 (`samplerClamp`) — add `VK_SHADER_STAGE_COMPUTE_BIT`.

These widenings are validation-only: at the device level they just permit compute access. Existing graphics consumers are unaffected.

The companion bindings-flags array (`pBindingFlags` at `TextureDescriptors.cpp:32-39`) is unchanged — `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` on binding 4 continues to apply. No new descriptor sets are allocated; compute shares the same per-framebuffer `mGlobalDescriptorSets[iFramebuffer]` already populated for graphics.

(Investigated alternative: building a parallel `mGlobalDescriptorSetsCompute` with compute-only stage flags. Rejected — doubles descriptor-pool consumption and forces a second write path in `TextureDescriptors::WriteGlobalDescriptorSets`, `UpdateTextureArrayDescriptors`, and `RewriteSamplerDescriptors`. The stage-flag widening is a one-line edit per binding with no runtime overhead.)

### 2. Compute path: split bindings by set index in `CreateComputePipeline`

Refactor `PipelineCreator::CreateComputePipeline` (`PipelineCreator.cpp:578-639`) to mirror `CreateDescriptorSetLayouts` (`PipelineCreator.cpp:107-225`):

- Walk `pComputeShader->mInfo.pDescriptorBindings[0..iDescriptorSetLayoutBindings]` filtering out empty (`descriptorCount == 0`) gaps, exactly as the existing compute path already does at `PipelineCreator.cpp:611-621`. Additionally consult `pComputeShader->mInfo.pDescriptorSetIndices[uiBinding]` for each kept binding to partition into `pVkSet1Bindings[]` (set 1) and a discard bucket (set 0 — provided by the global set).
- If the global set is available (mirror the graphics check `rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE`), build only the set-1 layout into `rPipeline.mVkDescriptorSetLayout`, then call `vkCreatePipelineLayout` with `pSetLayouts = [mVkExternalDescriptorSetLayout, mVkDescriptorSetLayout]`, `setLayoutCount = 2`.
- If the global set is NOT available (e.g., during early init before `TextureDescriptors::Create` has run, or in a test harness), fall back to `CreateSingleSetPipelineLayout` exactly as today — preserves the lifecycle of any pre-`TextureDescriptors` compute pipelines and gives a safe degenerate path.
- A compute shader that still declares everything at set 0 (the current state of every `.comp` in the tree) will produce `iSet1Count == 0`. The plan must handle this without breaking: when set 1 has zero bindings and the global set is attached, either (a) create an empty set-1 layout (`bindingCount = 0`) so the pipeline layout shape is uniform, or (b) drop set 1 entirely and emit a one-set pipeline layout that contains only the global set. Recommended: option (a) — keeps the call-site code in `Pipeline.cpp` and `CommandBufferRecordGlobal.cpp` uniform (always bind 2 sets), and `PipelineDescriptorWriter` already allocates the per-pipeline set unconditionally. The descriptor pool already supports zero-binding layouts.
- The set-2 (`kMultiSet`) branch from the graphics path does not apply to compute; assert if a compute pipeline ever sets `kMultiSet`. The existing kMultiSet model-pipeline plumbing in `Pipeline.cpp` does not appear on the compute paths.
- Push constants: continue passing `VK_SHADER_STAGE_COMPUTE_BIT` to the helper. Compute push constants are already routed correctly by `RecordPushConstants` in `Pipeline.cpp:15-21`.

### 3. Attach `mVkExternalDescriptorSetLayout` to compute pipelines

In `Pipeline::Create` (`Engine/Source/Graphics/Objects/Pipeline.cpp:57-61`), remove the `!(mInfo.flags & kCompute)` gate so that compute pipelines also pick up `gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout` when it exists.

This is the trigger for the new branch in step 2 and the new `bHasExternalSet0` branch in `PipelineDescriptorWriter::RouteWritesBySet` (step 4) to kick in for compute.

### 4. Route compute descriptor writes by reflected set index

`PipelineDescriptorWriter::RouteWritesBySet` (`PipelineDescriptorWriter.cpp:225-257`) already consults `pVertexShader` / `pFragmentShader` to find the set index for each binding. Generalize so compute pipelines use `ppShaders[0]` (the compute shader) instead:

- Branch on `rPipeline.mInfo.flags & kCompute` in `PipelineDescriptorWriter::Write` (`PipelineDescriptorWriter.cpp:261-489`) and call a compute-flavored variant of `RouteWritesBySet` that consults only `ppShaders[0]->mInfo.pDescriptorSetIndices`.
- Same Set 0 drop policy: writes whose reflected set index is 0 are discarded (the global set already handles them via `TextureDescriptors::WriteGlobalDescriptorSets`). Writes with index 1 are kept and re-targeted to `rPipeline.mVkDescriptorSets[iFramebuffer]`.
- `BindingExistsInShaderLayout` (`PipelineDescriptorWriter.cpp:15-29`) currently short-circuits `return true` when `kCompute` is set, because the existing code path treats compute as one flat set with no sparse-binding worries. Extend this so the helper validates against the compute shader's actual binding count too — otherwise `FilterWritesByShaderLayout` (currently skipped for compute at `PipelineDescriptorWriter.cpp:476-479`) must also start running for compute, since once we route by set, dangling per-pipeline `pDescriptorInfos` entries that don't exist in the shader will mis-bind. The simplest approach: extend `FilterWritesByShaderLayout` to use `ppShaders[0]` for compute, and stop the kCompute early-out on `BindingExistsInShaderLayout`. (The existing `BindingIsInSet0` already returns `false` when `mVkExternalDescriptorSetLayout == VK_NULL_HANDLE`, so today's behavior is preserved for pipelines without the global set.)

### 5. Update compute bind sites to bind both sets

Five call sites must bind the global set 0 + per-pipeline set 1 (`firstSet=0, descriptorSetCount=2`), conditional on `mVkExternalDescriptorSetLayout != VK_NULL_HANDLE` for symmetry with `BindGraphicsDescriptorSets`:

- `Pipeline::RecordCompute` (`Pipeline.cpp:266-279`) — the existing `iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;` already mirrors the graphics-side logic in `Pipeline.cpp:207, 222`. Build a 2-set `VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iCommandBuffer], rPipeline.mVkDescriptorSets[iDescriptorSetIndex]};` and bind with count 2. Note: the global set is indexed by command-buffer (per-framebuffer), the per-pipeline set continues to follow the `mbPerCommandBuffer` rule.
- `Pipeline::RecordComputeIndirect` (`Pipeline.cpp:281-301`) — same shape.
- `CommandBufferRecordGlobal::RecordWindSpreadPipeline` at `CommandBufferRecordGlobal.cpp:234-238` (`SpreadB`) and `:243-248` (`SpreadA`) — both inline a manual `vkCmdBindPipeline` + `vkCmdBindDescriptorSets` pair instead of going through `RecordCompute`/`RecordComputeIndirect`. They must build the same 2-set array. Cleanest: extract a `BindComputeDescriptorSets` helper into `Pipeline.cpp` (twin of `BindGraphicsDescriptorSets` at `Pipeline.cpp:23-34`) and call it from all 5 sites. That centralizes the conditional-on-external-layout logic and removes the duplication.
- `CommandBufferRecordGlobal::RecordSmokeSpreadPipeline` at `CommandBufferRecordGlobal.cpp:336-341` (`SpreadB`) and `:421-426` (`SpreadA`) — same shape, same helper.

The new helper signature:

```cpp
void BindComputeDescriptorSets(VkCommandBuffer vkCommandBuffer, VkPipelineLayout vkPipelineLayout, VkDescriptorSetLayout vkExternalLayout, int64_t iCommandBuffer, int64_t iDescriptorSetIndex, const std::vector<VkDescriptorSet>& rDescriptorSets);
```

— two index parameters because the global set is indexed by framebuffer (`iCommandBuffer`) and the per-pipeline set follows the `mbPerCommandBuffer` rule (`iDescriptorSetIndex`).

### Per-command-buffer index for compute

Today the compute paths already compute `iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0` (`Pipeline.cpp:275, 290` and `CommandBufferRecordGlobal.cpp:234, 244, 337, 422`). This mirrors `Pipeline::RecordDraw` (`Pipeline.cpp:207`) and `RecordBindPipelineAndDescriptors` (`Pipeline.cpp:222`), which feed `BindGraphicsDescriptorSets` (`Pipeline.cpp:23-34`). The new compute helper preserves that exact rule for the per-pipeline set; the global set always uses `iCommandBuffer` because `mGlobalDescriptorSets` is sized per-framebuffer (`TextureDescriptors.cpp:62`). No change to the underlying per-command-buffer policy is in scope — this plan brings parity but does not redesign it.

## Critical files

- `Engine/Source/Graphics/Objects/PipelineCreator.cpp` — refactor `CreateComputePipeline` (lines 578-639) to mirror the set-splitting in `CreateDescriptorSetLayouts` (lines 107-225). Either extract the shared splitting logic into a stage-agnostic helper or duplicate the loop with `ppShaders[0]` as the only shader source. Recommended: a `SplitBindingsBySet` helper that takes a `Shader*` array and a count.
- `Engine/Source/Graphics/Objects/Pipeline.cpp` — drop the `!(mInfo.flags & kCompute)` gate at line 58 (so compute also attaches `mVkExternalDescriptorSetLayout`); add a `BindComputeDescriptorSets` helper modeled on `BindGraphicsDescriptorSets` at lines 23-34; update `RecordCompute` (line 277) and `RecordComputeIndirect` (line 292) to call it.
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp` — generalize `RouteWritesBySet` (lines 225-257), `FilterWritesByShaderLayout` (lines 202-223), and `BindingExistsInShaderLayout` / `BindingIsInSet0` (lines 15-50) to consult `ppShaders[0]` when the pipeline is compute, and stop the compute early-outs at lines 17-20 and 476-479. The bMultiSet / Set 2 branches remain graphics-only.
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp` — widen `stageFlags` on the global Set 0 bindings at lines 23-30 to include `VK_SHADER_STAGE_COMPUTE_BIT` on bindings 0, 1, 3, 4, 12. Update the comment header at lines 17-22 to reflect the new accessibility.
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — replace the four inline `vkCmdBindDescriptorSets(... VK_PIPELINE_BIND_POINT_COMPUTE ...)` calls in `RecordWindSpreadPipeline` (lines 236, 246) and `RecordSmokeSpreadPipeline` (lines 339, 424) with the new `BindComputeDescriptorSets` helper so all 5 compute bind sites stay symmetric.

## Out of scope

- **Moving any compute shader's bindings from set 0 to set 1.** All currently-deployed `.comp` files keep their bindings at the implicit set 0. This plan only enables the C++ side to handle set 1 once shaders adopt it. The follow-on plan `Documents/Plans/Graphics/ShaderReview/DescriptorSetComputeSweep.md` will migrate shaders once this lands.
- **Compute push-constant changes.** `RecordPushConstants` at `Pipeline.cpp:15-21` and the existing `VK_SHADER_STAGE_COMPUTE_BIT` paths already work; the push-constant range stage on the pipeline layout continues to be `VK_SHADER_STAGE_COMPUTE_BIT` for compute pipelines.
- **Set-2 / multi-material support for compute.** The graphics-side `kMultiSet` branch in `CreateDescriptorSetLayouts` (lines 194-204) and `RecordDrawIndirectSet2` (`Pipeline.cpp:249-264`) has no compute analog; this plan asserts `kMultiSet` is unset on compute pipelines.
- **Building a parallel compute-only global descriptor set.** Investigated and rejected — see Design step 1. Sticking with shared `mGlobalDescriptorSets` and widened stage flags.
- **Touching the descriptor pool size or the descriptor pool's `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT` flag.** Both compute pipelines today and the new layout shape use the same descriptor types (UBO / SSBO / SAMPLER / COMBINED_IMAGE_SAMPLER / STORAGE_IMAGE) that the pool already provisions for graphics.
- **`PipelineDescriptorWriter::UpdateStorageBuffer` / `UpdateCombinedImageSampler` / `UpdateSampler` / `UpdateStorageImage` (`PipelineDescriptorWriter.cpp:491-602`).** These already operate on `rPipeline.mVkDescriptorSets` directly; they are correct for the new layout because the per-pipeline set is what they target (set 1, by convention). No changes needed unless a future shader-side migration moves a binding back to set 0 — but then the call sites would be obviously wrong (writing to the wrong set) and would be fixed alongside the shader edit.
- **`PipelineManager.cpp` compute pipeline registrations** (e.g., `WindSpreadComputeA/B` at lines 539-570, `LongParticlesUpdate` at line 575). The `DescriptorInfo` lists are unchanged; the framework relayers them. Authors authoring a new compute pipeline keep using the same `pDescriptorInfos` shape — descriptor-info-list shape is independent of the underlying set layout because the writer is the layer that routes by reflected set index.

## Acceptance criteria

- A compute shader with all bindings at default set 0 (current state of every `.comp`) builds and runs unchanged. Smoke test: launch the client with `WindSpread*` and `SmokeSpread*` and `LongParticlesUpdate` exercising; expect zero validation errors and identical visuals.
- After step 1 lands, the Vulkan validation layer reports no `VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358` (stage-flag mismatch) for any compute bind site even though the global set 0 is now bound to compute.
- A test compute shader with one binding moved to `layout(set = 1, binding = N) ...` (any one of the existing `.comp` files, edited only on a scratch branch) builds, links, runs, and reads the correct data — both bindings come through cleanly.
- The 5 compute bind sites all go through the new `BindComputeDescriptorSets` helper (grep for `vkCmdBindDescriptorSets` + `VK_PIPELINE_BIND_POINT_COMPUTE` should return only the helper definition).
- No regression on the dual-pipeline-creation-site invariant called out in `Engine/Source/Graphics/Managers/CLAUDE.md` — `PipelineManager.cpp`'s two construction sites do not change shape.

## Notes

- The compute shader's `mInfo.pDescriptorSetIndices` is already populated by the DataPacker side via spirv-cross. No data-format change is needed to surface compute set indices to runtime; the data was always there, just ignored by `CreateComputePipeline`.
- The `BindingExistsInShaderLayout` early-out at `PipelineDescriptorWriter.cpp:17-20` was added because compute previously had no per-binding shader-layout filter. Once compute participates in the set-routed flow, that filter becomes meaningful — but it should consult only `ppShaders[0]` (the compute shader), not the vertex/fragment pair. Same for `BindingIsInSet0` at lines 32-50.
- The 5-site compute bind-site list above accounts for every `vkCmdBindDescriptorSets` with `VK_PIPELINE_BIND_POINT_COMPUTE` in the engine. Grep was used to confirm — there are no compute bind sites outside `Pipeline.cpp` and `CommandBufferRecordGlobal.cpp`.
- Per `Engine/Source/Graphics/Managers/CLAUDE.md`'s "single descriptor pool serves all pipelines; global Set 0 is shared, Sets 1/2 are per-pipeline" — this plan extends "shared global Set 0" to the compute pipeline bind point, matching the docs' stated intent.
- This plan is a prerequisite for the descriptor-set audit in `Graphics/ShaderReview/DescriptorSetComputeSweep.md`. Once this plan lands, the shader-side audit becomes a pure-GLSL change; the C++ side is ready.
