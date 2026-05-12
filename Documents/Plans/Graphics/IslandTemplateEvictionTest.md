# Island Template Eviction — Test and Per-Pipeline Descriptor Fix

## Context

Phase 5 of the per-frame multi-island system (`Features/Graphics/IslandTemplateLRUEviction.md`) landed as a behavioral no-op: ref-counts, eviction sweep, restoration sweep, and canonical-slot fallback are all wired, but both extant templates (`kIslands01Crc`, `kIslands02Crc`) are `mbPinned = true` so eviction never fires today.

Two blockers prevent live eviction from working correctly. Both must be solved before a non-pinned template can ship:

1. **Per-pipeline descriptor staleness**. `TextureDescriptors::UpdateTextureArrayDescriptors` rewrites only Set 0 binding 4 (global bindless `mImageInfos` array, keyed by texture CRC). The four terrain G-buffer pipelines (`kPipelineTerrainElevation` / `Color` / `Normal` / `AmbientOcclusion`, see `PipelineManager.cpp:377-380`) use a *per-pipeline* `kCombinedSamplers` binding sourced from `gpTextureManager->mRenderTargetTextures.m*Textures` at pipeline creation, written via `TextureDescriptors::WriteArrayBindingDescriptors`. After `IslandTerrain::EvictionSweep` calls `Texture::FreeGpuResources()` and repoints `m*Textures[slot]` at canonical (slot 0), the per-pipeline descriptor set still references the destroyed `VkImageView`. Next draw → Vulkan validation error or undefined sample. Today's `AcquireTextureSlot` path is fine because it bumps `gpGraphics->meDestroyType = kPipelines` which forces full pipeline rebuild next frame (a rebuild re-registers and re-writes every per-pipeline binding); the eviction sweep does *not* bump `meDestroyType`.

2. **Binding registration is keyed by texture CRC at pipeline-creation time**. `TextureDescriptors::RegisterTextureBinding` records each binding under the CRC found in `ppTextures[k]->mInfo.crc` when the pipeline is created. At boot, `m*Textures[2..63]` all point at slot 0's Texture, so only `kIslands01Crc`'s + `kIslands02Crc`'s texture CRCs end up in `mTextureBindings`. Post-boot mints of new templates (kIslands03+) populate slot N with a different Texture*, but the binding map never gets a new entry for those texture CRCs, so `ProcessPendingTextures`' `UpdateDescriptorsForTexture(newCrc)` path can't patch the per-pipeline binding for the new template. Same root cause as (1): the pipeline-rebuild path is the only thing that currently fixes this, and the rebuild is only triggered by `AcquireTextureSlot`'s `meDestroyType` bump, not by `RestorationSweep`.

## Design

### Per-pipeline binding patch

Add a helper to `TextureDescriptors` that rewrites the four terrain pipelines' island-array bindings in place. Two reasonable shapes — pick during execution:

- **Option A (targeted)**: `void RewriteIslandPipelineArrays();` walks `mTextureBindings` for the four known terrain pipeline pointers and calls `WriteArrayBindingDescriptors` on each. Avoids touching unrelated bindings.
- **Option B (existing)**: call the already-present `RewriteSamplerDescriptors()` which rewrites every registered binding. Simpler but heavier.

Wire from both sweeps in `IslandTerrain`:
- `EvictionSweep`: after the slot-repoint loop and the existing `UpdateTextureArrayDescriptors()` call.
- `RestorationSweep`: after each `bDirty = true` patch sequence, before the existing `UpdateTextureArrayDescriptors()` call.

Both are inside the descriptor-patch safety window (post-fence-wait, pre-cmd-buffer-recording), so the writes are safe with `UPDATE_AFTER_BIND`.

### Binding registration for runtime-minted templates

When `AcquireTextureSlot` mints a new slot N for a non-pinned template, `mTextureBindings` has no entry for the new template's four texture CRCs. After the first pipeline rebuild that picks up the new `m*Textures[N]` pointers, `RegisterTextureBinding` runs and adds the entries — but only if a rebuild actually happens. Today `AcquireTextureSlot` bumps `meDestroyType` precisely for this reason. Phase 5 should preserve that behavior on first-mint of non-pinned templates (current code already does — the `meDestroyType` bump stays in the rewritten state machine).

The remaining gap: pipeline rebuild registers the canonical-fallback's texture CRCs (still pointing at slot 0 when the rebuild happens, because `RestorationSweep` only patches per-channel as chunks adopt). So `mTextureBindings` ends up keyed by slot 0's CRCs again. When the new template's chunks adopt and `RestorationSweep` patches `m*Textures[N]` to the real Texture*, the per-pipeline binding still references slot 0's `VkImageView`. The sweep-side rewrite from §1 fixes this.

### Synthetic third template for live test

Add a debug-only template (gated by a `kbIslandLruTest` constexpr or `#if defined(BT_DEBUG)`). Two options:
- Duplicate `kIslands02Crc`'s pack contents under a fresh CRC at DataPacker time and place it in a non-origin cell.
- Use the existing `kIslands03` data if/when the manifest grows.

Make the test template `mbPinned = false`. Drive camera traversal across its placement, watch logs (`kGraphics`/`kLoading` `kVerbose` lines already in `IslandTerrain.cpp`).

## Acceptance criteria

- Camera moves into a cell with the test template → all 4 chunks reach `kReady` → `RestorationSweep` patches the slot per-channel → `kGraphics` log emits "Fully restored islandCrc=… slot=…" within a few frames of entry → terrain renders correctly with no Vulkan validation errors.
- Camera leaves the cell, ref count drops to 0, grace window (`kuiGraceRenderFrames = 300`) elapses → `EvictionSweep` emits "Evicting islandCrc=… slot=… (refCount=0, framesSinceUse=…)" → GPU resources free → slot shows canonical (visually wrong texture briefly in the cell, but no validation error).
- Camera re-enters the cell → `AcquireTextureSlot` re-issues `RequestChunkLoad` → restoration completes within a few frames → no validation errors during the entire evict/re-upload cycle, including format-compatibility on the bindless terrain texture array.
- Pinned templates (`kIslands01Crc` + `kIslands02Crc`) remain unaffected — no eviction logs for them, menu↔game stays instant.
- `RewriteSamplerDescriptors` / new helper executes only inside the safety window; no in-flight cmd buffer references the patched descriptors.

## Critical files

- `Engine/Source/Graphics/Managers/TextureDescriptors.h` / `TextureDescriptors.cpp` — add `RewriteIslandPipelineArrays` (or reuse `RewriteSamplerDescriptors`).
- `Engine/Source/Frame/IslandTerrain.cpp` — invoke the per-pipeline rewrite from `EvictionSweep` and `RestorationSweep` post-patch.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — verify `RegisterTextureBinding` semantics for the terrain pipelines; consider registering by slot-index rather than texture CRC if cleaner.
- `Engine/Source/Frame/IslandTerrain.cpp` — debug-only synthetic-template hook (gated).
- `Engine/Data/Islands/...` — third template asset for live test (or reuse existing `03` if shipped).
- `Documents/Architecture/FrameUpdatePipeline.md` — no diagram update needed (sweeps live in `RenderGlobal`, not the frame tick).

### Additional correctness items flagged by Phase 5 multi-agent audit

These are dormant under the no-op contract (both extant templates pinned), but must be solved before eviction actually fires:

- **`EvictionSweep` ordering and `mImageInfos` staleness in `IslandTerrain.cpp`**. After `FreeGpuResources()` destroys the evicted Texture's `mVkImageView`, `TextureDescriptors::mImageInfos[CrcToIndex(evicted_crc)]` still retains the now-destroyed handle (because `mImageInfos` snapshots imageViews at adoption time and is not re-fetched on demand). The final `UpdateTextureArrayDescriptors()` at end of `EvictionSweep` then writes that destroyed handle into Set 0 binding 4. Dormant today because terrain pipelines sample island textures through the per-pipeline binding rather than the global bindless array, so no shader actually reads the corrupted bindless slot. When eviction goes live: either (a) patch `mImageInfos[CrcToIndex(evicted_crc)]` to a placeholder/canonical view before `UpdateTextureArrayDescriptors`, or (b) reorder `EvictionSweep` to call `ResetTextureChunkStates(span)` first (atomic state transition to `kNotLoaded`) and only then call `FreeGpuResources`. Pick during execution.
- **Data race in `FileManager::ResetTextureChunkStates(std::span)`**. The pool-offset-restoration walk writes `pData` and `iDataSize` for *every* chunk in `mLazyChunkMap` (idempotent same-value writes for chunks not in the target span). The upload thread may concurrently read those fields for unrelated chunks. Strict UB even though the write is value-preserving. Fix: restrict the `pData`/`iDataSize` write to chunks in the target span (still walk all chunks to compute cumulative offsets, but only write at target indices). Keep the wholesale no-arg version untouched — it's only called under `vkDeviceWaitIdle` during device-loss recovery so the race doesn't apply there.

Both fixes are local (a few lines each) and should land alongside the per-pipeline descriptor work in this plan, before exercising any synthetic third template.

## Out of scope

- Slot-LRU when all 64 slots are taken. Still deferred to a separate plan when real content approaches the cap.
- Heightmap RAM eviction.
- Streaming-from-disk replacement for `mpLazyPool`.
- Performance tuning of `kuiGraceRenderFrames` — leave at 300 until profiling data suggests otherwise.
- Removing `smPriorityTextures` (now redundant with `mbPinned`); separate cleanup plan.

## Notes

- The `RegisterTextureBinding` keying-by-CRC quirk is the deeper architectural smell. If the per-pipeline rewrite turns out to be invasive, consider re-shaping the registration to key by slot index — cleaner for the bindless terrain array but a wider blast radius (other call sites pass `pTexture` not `ppTextures`).
- Phase 5's main plan deliberately landed as a no-op so this follow-up can be sequenced when real content pressure justifies the verification work.
