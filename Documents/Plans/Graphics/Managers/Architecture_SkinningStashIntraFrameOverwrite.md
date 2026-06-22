# Skinning Stash Intra-Frame Multi-Grow Overwrite

Investigate-then-decide plan. Establish whether `BufferManager`'s per-framebuffer skinning stash arrays can be overwritten mid-frame by a second same-framebuffer grow that frees the buffer the first grow stashed, then either fix a proven use-after-free or downgrade to a documenting comment.

## Context

`BufferManager` keeps a per-framebuffer "previous buffer" stash for the two skinning bump allocators:

- `std::optional<Buffer> mPreviousMeshDataBuffer[kiMaxFramebuffers]` and `mPreviousJointMatrixBuffer[kiMaxFramebuffers]` (declared `BufferManager.h`, private block alongside `miMeshDataOffset`/`miJointMatrixOffset`/`miMeshDataCapacity`/`miJointMatrixCapacity`).

The grow paths `BufferManager::GrowMeshDataBuffer` / `GrowJointMatrixBuffer` (private; called from `AllocateMeshData` / `AllocateJointMatrices` when the bumped offset exceeds capacity) each do:

```
mPreviousMeshDataBuffer[iCommandBuffer] = std::move(mMeshDataStorageBuffers.at(iCommandBuffer));
mMeshDataStorageBuffers.at(iCommandBuffer).Create(...);   // bigger
std::memcpy(new.mpMappedMemory, pOldData, iValidCount * sizeof(...));
gpPipelineManager->mDynamicPipelines.UpdateAllModelPipelineDescriptors(iCommandBuffer, kModelPipelineBindingMeshData, pNewBuffer);
```

The single-slot stash exists so the OLD buffer survives until end-of-frame: the prior frame's in-flight GPU submission may still reference it via the model-pipeline descriptor (binding `kModelPipelineBindingMeshData` = 15 / `kModelPipelineBindingJointMatrix` = 16, written by `UpdateAllModelPipelineDescriptors` in `DynamicPipelines.cpp`). The stash is cleared only by `BufferManager::ResetSkinningAllocations(iCommandBuffer)`, which also zeroes the bump offsets. That reset runs exactly once per frame, near the top of `RenderFrameMain(iCommandBuffer)` (`MainUniforms.cpp`, immediately after `RenderLightingMain`), BEFORE any collection `Render` call.

The suspected hazard: within ONE `RenderFrameMain` pass the stash is a single slot per framebuffer, but the collection render calls below it can grow the SAME framebuffer's buffer more than once:

- `PlayersRender.cpp` `PlayersInterpolate::Render` calls `AllocateMeshData` / `AllocateJointMatrices` once **per player** inside its per-entity loop (each skinned player can independently trip a grow).
- `SpaceshipsRender.cpp` `SpaceshipsInterpolate::Render` does one bulk `AllocateMeshData` + one `AllocateJointMatrices` **per active coord**.
- `RenderFrameMain` iterates active coords (camera coord first), invoking these `Render` methods sequentially on the main thread.

So across multiple players and/or multiple active coords, two grows for the same `iCommandBuffer` in one frame is plausible. The second grow does `mPreviousMeshDataBuffer[iCommandBuffer] = std::move(...)`, which **destroys the buffer the first same-frame grow stashed** (optional move-assign frees the held `Buffer`). Because `ResetSkinningAllocations` already ran for this frame, there is no intra-frame reset between the two grows to drain it safely.

Whether this is a live use-after-free hinges on a question NOT proven during the Graphics/Managers sweep: has the PRIOR FRAME's GPU submission that referenced the first-stashed buffer actually drained before the second same-frame grow frees it? The relevant barrier is the top-of-frame per-framebuffer fence wait in `Graphics::RenderGlobal` (`Graphics.cpp`): `vkGetFenceStatus` + `vkWaitForFences` on `rCommandBuffers.mVkFence` for the current framebuffer, run before the collection renders. If that wait guarantees the prior frame's read of framebuffer `i`'s buffer is complete before this frame's first grow on `i`, then the first-stashed buffer is only protecting THIS frame's in-flight work — but this frame has not submitted yet at grow time, so the only reader of the first-stashed buffer is a not-yet-recorded command buffer that will be pointed (by `UpdateAllModelPipelineDescriptors`) at the NEW buffer anyway. Confirming or refuting that chain is the core deliverable.

This plan is entangled with the just-landed `ResizeDynamicBuffer` documentation. That sibling single-slot `mPreviousBuffer` stash was documented safe via a three-point per-framebuffer fence chain (`BufferManager::ResizeDynamicBuffer` comment: per-framebuffer instances + RenderGlobal top-of-frame fence wait + immediate descriptor rewrite), and its comment explicitly cites THESE per-framebuffer stash arrays as the "fix template" for the multi-grow case ("The skinning path uses per-framebuffer stash arrays -- mPreviousMeshDataBuffer -- instead."). If the arrays are themselves unsafe under multi-grow, that reasoning is partly circular and the `ResizeDynamicBuffer` comment may need a correction too.

## Design

Three investigation gates, then a decision:

(a) **Can multiple grows for the same framebuffer index occur in one frame?** Read the call structure precisely: `RenderFrameMain` active-coord loop → `PlayersInterpolate::Render` per-entity `AllocateMeshData`/`AllocateJointMatrices`, `SpaceshipsInterpolate::Render` bulk allocations. Determine whether the bump-allocator's doubling growth (`miMeshDataCapacity *= 2`) realistically fires more than once per framebuffer per frame given default capacities and entity counts. If two grows on one `iCommandBuffer` per frame is impossible (e.g. first grow always lands a capacity that covers all remaining same-frame allocations), the hazard is moot and the outcome is gate (c)-documentation only.

(b) **Has the prior-frame GPU read drained at the second grow?** Trace the lifetime: which submission last read framebuffer `i`'s mesh-data/joint-matrix buffer through the model-pipeline descriptor, and whether `Graphics::RenderGlobal`'s top-of-frame per-framebuffer fence wait on `mVkFence` for framebuffer `i` (`Graphics.cpp`) completes that read before the first grow of the frame. Critically establish: at the moment of the SECOND same-frame grow, is the only live reader of the first-stashed buffer (i) the prior frame (already drained by the fence wait), or (ii) this frame's not-yet-submitted command buffer (whose descriptor was just repointed at the buffer the first grow created, not the first-stashed one)? If both readers are accounted for, the first-stashed buffer has no live GPU reference at the second grow and the overwrite is safe.

(c) **Decide:**
- If a live use-after-free is possible (some submission still references the first-stashed buffer when the second grow frees it): propose a fix. Candidate shapes — (i) replace the single-slot stash with a per-framebuffer deferred-destroy list (`std::vector<Buffer>` per framebuffer, drained in `ResetSkinningAllocations`) so every same-frame grow's old buffer survives to end of frame; or (ii) an `ASSERT` that at most one grow per framebuffer per frame occurs (cheap if gate (a) shows multi-grow is not expected in practice, converting a silent UAF into a loud failure). Pick the simplest that closes the hole; surface the (i)-vs-(ii) choice for `/external-grill-plan` if both remain viable after investigation.
- If proven safe: downgrade to a documenting comment on `GrowMeshDataBuffer`/`GrowJointMatrixBuffer` (and/or the array declarations in `BufferManager.h`) modeled on the `ResizeDynamicBuffer` three-point comment, stating exactly why a same-frame second grow that overwrites the stash cannot free a still-referenced buffer. Also correct the `ResizeDynamicBuffer` comment's "fix template" reference if the investigation changes its accuracy.

## Critical files

- `Engine/Source/Graphics/Managers/BufferManager.h` — `mPreviousMeshDataBuffer[kiMaxFramebuffers]` / `mPreviousJointMatrixBuffer[kiMaxFramebuffers]` declarations; `miMeshDataOffset`/`miJointMatrixOffset`/`miMeshDataCapacity`/`miJointMatrixCapacity`; `AllocateMeshData`/`AllocateJointMatrices`/`ResetSkinningAllocations`/`GrowMeshDataBuffer`/`GrowJointMatrixBuffer` decls.
- `Engine/Source/Graphics/Managers/BufferManager.cpp` — `GrowMeshDataBuffer` / `GrowJointMatrixBuffer` (the `mPreviousX[iCommandBuffer] = std::move(...)` overwrite + `UpdateAllModelPipelineDescriptors` repoint); `AllocateMeshData` / `AllocateJointMatrices`; `ResetSkinningAllocations`; the sibling `ResizeDynamicBuffer` comment that cites these arrays as its template.
- `Engine/Source/Graphics/Render/MainUniforms.cpp` — `RenderFrameMain`'s single per-frame `gpBufferManager->ResetSkinningAllocations(iCommandBuffer)` call site (after `RenderLightingMain`, before the active-coord render loop).
- `Engine/Source/Graphics/Graphics.cpp` — `Graphics::RenderGlobal` top-of-frame per-framebuffer fence wait (`vkGetFenceStatus` / `vkWaitForFences` on `rCommandBuffers.mVkFence`), the barrier gate (b) turns on.
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp` — `UpdateAllModelPipelineDescriptors`, the descriptor repoint to the new buffer (bindings `kModelPipelineBindingMeshData` = 15 / `kModelPipelineBindingJointMatrix` = 16 from `Objects/ModelPipeline.h`).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersRender.cpp` — `PlayersInterpolate::Render` per-entity `AllocateMeshData` / `AllocateJointMatrices`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsRender.cpp` — `SpaceshipsInterpolate::Render` bulk `AllocateMeshData` / `AllocateJointMatrices`.

## Out of scope

- The two already-queued skinning bump-allocator grow bugs (post-bump-offset memcpy OOB read; single-doubling under-size on bulk allocations) — owned by `Graphics/Managers/Refactor_BufferGrowthAndBounds`. This plan touches the same two functions, so co-schedule, but it does NOT re-fix or duplicate those items.
- The `ResizeDynamicBuffer` single-slot `mPreviousBuffer` mechanism itself (documented safe this session) — only its "fix template" cross-reference is in scope, and only if this investigation invalidates it.
- Any change to the bump-allocator doubling-growth policy, capacities, or the model-pipeline descriptor layout/binding numbers.
- The broader bindless slot lifecycle / island eviction use-after-free class (`Graphics/Managers/Architecture_BindlessSlotLifecycle`) — unrelated GPU-lifetime surface.
- Threading model of the render thread / `kbRenderThread` submission workers — not touched; the investigation is about CPU-side stash overwrite vs GPU drain, not the submit thread.

## Notes

- Invariant exposure: **client/graphics-only**. No determinism/CRC, no `kiVersion`/`.pack` layout, no replay, no network. The skinning buffers are host-visible GPU upload targets written from the render path; nothing here feeds the simulation CRC. GPU-lifetime (use-after-free) correctness only.
- Allocation-tracked path: the grow `Buffer::Create` runs in the main loop; existing code already lives there (the recreate path suppresses up-stack per Managers conventions). A deferred-destroy-list fix (gate c-i) would allocate a per-framebuffer `std::vector<Buffer>` — size it at boot (`InitializePerCommandBufferBuffers`) so no per-frame heap churn occurs, or reserve once; flag for the allocation tracker if any growth can happen in-loop.
- Tag: **investigate-then-decide**. Verify the cited root cause (gates a + b) before any edit, per Diagnosis Discipline — "a same-frame second grow overwrites the stash" is the suspected mechanism, not yet proven to free a still-referenced buffer.
- Single open decision pre-staged for `/external-grill-plan`: if gate (c) lands on a fix, choose between the per-framebuffer deferred-destroy list (robust, slightly more state) vs the at-most-one-grow-per-frame `ASSERT` (minimal, only valid if gate (a) shows multi-grow is not a real workload). If gate (b) proves safety, no decision — comment only.
