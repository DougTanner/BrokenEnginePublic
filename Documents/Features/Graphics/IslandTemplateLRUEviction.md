# Island Template LRU Eviction (Phase 5 of 5: Per-Frame Multi-Island)

## Context

After Phase 3, every island template that gets touched at runtime occupies a permanent slot in the bindless texture array (cap `shaders::kiMaxIslands = 64`). With only two templates in the manifest today (`kIslands01Crc`, `kIslands02Crc`) this is fine. But the whole point of the 5-phase project is to unlock a content pipeline that can ship dozens of island templates. Without eviction, VRAM grows unboundedly and the slot cap is hit after 64 distinct templates have ever been seen.

Phase 5 adds ref-counting and an LRU grace period: templates whose ref-count drops to zero get their GPU resources freed after K render frames; their CPU bytes are already retained (see below), so re-references re-upload via the existing texture-upload path without re-reading from disk. A freed slot becomes available for new templates.

**Depends on Phases 1 + 2 + 3.** (Phase 4 not strictly required — Phase 5 works with single-island cells, but the combination is the intended deployment.)

## Design

### CPU bytes — the `pData` reality

`LazyChunk::pData` is **not** a heap allocation and **not** a memory-mapped pack slice. It is a fixed-offset slot in a single `VirtualAlloc`'d pool (`mpLazyPool` in `FileManager.cpp:267-282`). Each `LazyChunk` is assigned its offset at boot by walking the hashmap of chunks once. The "nulling" of `pData` on `TextureManager.cpp:463` after adoption is purely bookkeeping for "this chunk's bytes have been consumed by the GPU upload" — it does **not** free anything. `ResetTextureChunkStates` (`FileManager.cpp:603-624`) is the canonical "make these chunks loadable again" entry point.

This means **the bytes for every island template are always resident at zero extra cost**. Phase 5 does not need new CPU-retention infrastructure. The original plan's "(a) stop nulling pData / (b) keep a `unique_ptr<uint8_t[]>` copy" choice is moot.

The right approach: when a template gets evicted, route its four texture chunks back through the existing lazy-load entry — call `ResetTextureChunkStates` for those four CRCs, then `RequestChunkLoad` on re-acquire. The texture-upload thread's existing state machine produces a fresh `vkImage` + `vmaAllocation`; `ProcessPendingTextures` adopts them and patches descriptors. **No new `Texture::ReuploadFromCpu` method is needed** — Phase 5 reuses the lazy-load path end-to-end.

There is one detail: `TextureManager.cpp:437` reads `bool bFromTransferQueue = rLazyChunk.pData != nullptr;` to decide whether a QFOT acquire barrier is needed. The upload thread sets `pData` non-null after copying from pool into staging, and `ProcessPendingTextures` nulls it after adoption. The lazy-load path itself is symmetric across first-load and re-load, so this works without modification.

### Per-template state additions

`IslandTemplate` from Phase 1 gains:

```cpp
int64_t miRefCount = 0;                  // count of active staticData.islands entries referencing this
uint64_t muiLastUsedRenderFrame = 0;     // for LRU; counts Graphics::muiFrameCounter values
bool mbGpuResident = false;              // tracks whether GPU resources are live
bool mbPinned = false;                   // never evict (e.g. kIslands01Crc menu CRC)
```

`miTextureSlot` (added in Phase 1, populated in Phase 3) keeps its meaning — once assigned, it survives GPU eviction. Only when LRU evicts the *slot itself* (all 64 in use, new template wants in) does the slot get reassigned.

`mbPinned` is set true at startup for `kIslands01Crc` and `kIslands02Crc` so the menu↔game transition is always instantaneous (no re-upload latency). Today the codebase explicitly designs for this — `TextureManager.cpp:147-154` appends both CRCs to `smPriorityTextures` at realtime priority for this reason.

### `Texture::FreeGpuResources`

`engine::Texture` gains:

```cpp
void FreeGpuResources();   // destroy vkImage + vmaAllocation; keep mInfo
```

This is the only new `Texture` method. CPU bytes live on `LazyChunk::pData` in `mpLazyPool` — `Texture` does not own them.

### Ref-counting

`IslandTerrain::AcquireTemplate(crc)` ++ `mIslands.at(crc).miRefCount`; updates `muiLastUsedRenderFrame` to `gpGraphics->muiFrameCounter`.
`IslandTerrain::ReleaseTemplate(crc)` -- `mIslands.at(crc).miRefCount`.

Both called from `Islands::UpdateActiveIslands` on the **diff** between the prior frame's active placement set and the new one — not on every placement every frame (or every Acquire would be `++` and there'd be no Release). Maintain a `prevActiveCrcs` flat-vector on `Islands` (size-bounded by `kiMaxIslands`).

### Eviction sweep — correct point in the render loop

`UpdateActiveIslands` runs during Main-command-buffer recording (after `RenderGlobal`). Performing descriptor patches there is unsafe: the command buffer currently being recorded will reference those descriptors. `UPDATE_AFTER_BIND` allows in-use descriptors to be patched, but only between command buffers — not mid-record.

**Eviction sweep must run at the same point as `ProcessPendingTextures`**: inside `RenderGlobal`, *after* the framebuffer-fence wait (`Graphics.cpp:124-162`) and *before* any command-buffer recording that references the descriptors. This is the canonical descriptor-patch safety window.

Inside that sweep:

1. Walk `mIslands`; for any template where `!mbPinned && miRefCount == 0 && (gpGraphics->muiFrameCounter - muiLastUsedRenderFrame) > kuiGraceRenderFrames`:
   - For each of the four texture CRCs (elevation, color, normals, AO): `Texture::FreeGpuResources()` on the corresponding `mTextureMap` entry.
   - **Do not patch the descriptor to the white placeholder.** White is `R8G8B8A8_UNORM`; the terrain shaders sample `R16_UNORM` (elevation), BC7/BC4/BC5 (color, AO, normals) — putting a white view there triggers Vulkan validation format-compatibility errors. Instead, leave the descriptor pointed at the now-destroyed `VkImageView` and immediately re-issue `ResetTextureChunkStates` + `RequestChunkLoad` for the four CRCs so they're already in-flight at lazy priority for next re-acquire. The descriptor is stale only between the `FreeGpuResources` call and the next `ProcessPendingTextures` adoption — both run inside the same `RenderGlobal` window, before any draw command samples it.
   - Set `mbGpuResident = false`.
2. After the sweep, `mTextureDescriptors.UpdateTextureArrayDescriptors()` once.

`kuiGraceRenderFrames` configurable; default ~300 (≈ 5 s at 60 Hz, ≈ 2.5 s at 120 Hz). Document the wall-clock semantics — render-rate-dependent.

### Re-reference path

`AcquireTemplate(crc)` checks `mbGpuResident`. If false, the chunks were already nudged back into a loadable state during eviction; just bump priority to `kRealtime` via `RequestChunkLoad`. The next `ProcessPendingTextures` adopts and patches the descriptor with the real view.

Between eviction and re-adoption, the slot's descriptor is stale, but no draw command samples it because the template's `miRefCount == 0` until the re-Acquire — and the re-Acquire happens before the renderer emits any quad with that template's slot. The asymmetry holds: ref-count > 0 ⇒ GPU resident before any rendering. Verify this invariant in code; if it can be violated by ordering, fall back to a typed-placeholder scheme (one placeholder texture per format).

### Slot eviction (when all 64 in use)

If `AcquireTextureSlot(crc)` (from Phase 3) is called for a new CRC with no free slots: find the LRU template (`!mbPinned && miRefCount == 0`, oldest `muiLastUsedRenderFrame`); evict it (free GPU + clear its slot reservation by setting `miTextureSlot = -1`); reassign the slot to the new CRC. If no eligible LRU template exists, **all 64 slots are pinned or active** — assert-and-clamp at the renderer (drop later placements). Tunable: bump `kiMaxIslands` to 128 if this becomes a real constraint (requires shader recompile).

### Descriptor flow

The four terrain G-buffer pipelines (and `ShadowElevation`) use the per-pipeline (set=1 binding=2) `textureSampler[kiMaxIslands]` array, constructed with `kUpdateAfterBind` at `PipelineManager.cpp:388`. `UpdateTextureArrayDescriptors` is the single sink for slot changes.

### Determinism

LRU is a client-only GPU concern. No CRC impact, no network protocol change. Server build never instantiates GPU textures; this phase compiles in `#if defined(BT_CLIENT)` scopes wherever it touches Vulkan resources.

## Critical files

- `Engine/Source/Graphics/Objects/Texture.h` / `Texture.cpp` — `FreeGpuResources`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` — verify re-issued `RequestChunkLoad` paths work for already-once-loaded chunks (today they do via `ResetTextureChunkStates`)
- `Engine/Source/File/FileManager.cpp` — `ResetTextureChunkStates` already exists; called from eviction
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp:113` — slot patch + `UpdateTextureArrayDescriptors`
- `Engine/Source/Frame/IslandTerrain.h` / `IslandTerrain.cpp` — `IslandTemplate::miRefCount`/`muiLastUsedRenderFrame`/`mbGpuResident`/`mbPinned`, `AcquireTemplate`/`ReleaseTemplate`, eviction sweep entry-point
- `Engine/Source/Graphics/Graphics.cpp:124-162` (`RenderGlobal` post-fence-wait region) — invoke eviction sweep alongside `ProcessPendingTextures`
- `Engine/Source/Graphics/Islands.h` / `Islands.cpp` — `prevActiveCrcs` flat-vector, per-frame diff producing Acquire/Release calls; **does not invoke eviction sweep** (eviction lives in `RenderGlobal`, not in mid-render-recording)
- `Engine/Source/Graphics/Managers/TextureManager.cpp:147-154` (`smPriorityTextures`) — keep both menu CRC chunks pinned permanently at realtime priority via `mbPinned`

## Out of scope

- More than `kiMaxIslands` templates *concurrently active* — that's a separate dial (bump the constant + recompile shaders). Phase 5 unblocks dozens of templates *over time*, not at once.
- Streaming-from-disk replacement: this phase keeps CPU bytes resident in `mpLazyPool` (they were anyway). A separate plan can layer "shrink `mpLazyPool` by paging unused chunk slots to disk" on top.
- Per-island world dimensions in the renderer — owned by Phase 4 prerequisite.
- Heightmap eviction from RAM — only GPU resources evict. Sim queries read `mpfHeightmapData` directly and need it resident; the `mpLazyPool` slot stays valid.
- Typed-placeholder textures (one per format) — only needed if the "no draw between FreeGpuResources and re-adoption" invariant turns out to be violable in practice. Defer until proven necessary.

## Acceptance criteria

- VRAM stays bounded as the camera traverses 100s of cells when > 64 unique templates eventually ship.
- Today (only 01 and 02 present): with `mbPinned = true` on both, no eviction triggers, no regressions versus pre-Phase-5 behavior.
- With a third (synthetic) test template added: when it falls out of the active set for `kuiGraceRenderFrames` frames, it evicts; re-entering its cell re-uploads within a few frames.
- No Vulkan validation errors during evict/re-upload cycles, including format-compatibility errors on the bindless terrain texture array.
- No flicker on menu↔game transitions (verified by `mbPinned` on 01 and 02).
- Save/load and reconciliation CRC remain stable (GPU eviction has no sim-side impact).

## Notes

- The dominant fragility in this plan is the "no draw between `FreeGpuResources` and re-adoption" invariant. The placement-list iteration guarantees `miRefCount > 0` for everything sampled by an emitted quad, but the eviction sweep runs *before* `UpdateActiveIslands` recomputes the active set, so a careless implementation could free the GPU resources for a template that will be Acquired this same frame. **Always Release-then-Acquire** the diff before sweeping, never sweep-first.
- Eviction sweep runs at the same point as `ProcessPendingTextures`, inside `RenderGlobal` post-fence-wait. Same descriptor-patch safety window.
- Pinned templates (`mbPinned = true` on 01 and 02) preserve the codebase's existing "menu↔game switch is instant" property. The current `smPriorityTextures` mechanism becomes redundant once Phase 5 is in — both effects are achieved by the pin flag.
- The plan is the largest, riskiest, and lowest-priority of the five phases. Land Phases 1–4 first, then assess whether real content pressure justifies Phase 5. If two templates are all the game ever ships, Phase 5 stays on the shelf.
- **Scoring revision:** with the `pData` reality and descriptor-patch placement clarified, Effort drops slightly (no new `ReuploadFromCpu`); Risks stay High due to GPU-resource lifecycle correctness. Recommended: Effort 4, Impact 3 (Real — bounded VRAM, but only after real content ships), Risks 3 (High), Score 4. Down-rank in `Order.md` accordingly.
