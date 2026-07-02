# Refactor: Command Buffer Record Dedup & Decomposition

## Context
Source: /external-refactor-clean on Engine/Source (recursive). `CommandBufferRecordMain::Record` is a ~300-line nine-pass monolith while its sibling `CommandBufferRecordGlobal::Record` is already decomposed into per-pass statics (intra-directory drift), and two idioms repeat across the record/dispatch sites.

## Design

### Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp
- Decompose `Record` (:17-317) into per-pass private statics matching the Global sibling's convention (`RecordShadowPasses`/`RecordTerrainPasses`/... — only `RecordLightingSpreadPipeline` exists on the Main side today). Nine passes: occupancy clear + lighting deposit, smoke emit, wind deposit A/B, object shadows + blur, water displacement, main render-pass body (UI prepass/models/terrain/water/particles/text). Move-only, no logic change [~1h]

### Engine/Source/Graphics/Objects/Pipeline.{h,cpp} — shared indirect-dispatch helper
- Extract `Pipeline::RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, VkBuffer, VkDeviceSize)` (or a file-local static): the "compute `iDescriptorSetIndex` from `mbPerCommandBuffer`, bind pipeline, `BindComputeDescriptorSets`, `vkCmdDispatchIndirect` from an externally owned VkBuffer" block is copied at `CommandBufferRecordGlobal.cpp:257-262, 267-272, 354-359` and the bind prologue of `CommandBufferRecordMain.cpp:383-386` (needed because `RecordComputeIndirect` only dispatches from the pipeline's own `mIndirectVkBuffer`) [~30m]

### Tile-count helper
- Add a one-line `TileCount(uint32_t)` helper (GraphicsUtils or `shaders::`) and fold the ~12 hand-expanded `(w + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize` sites: `CommandBufferRecordGlobal.cpp:43-44,48-49,80-81`; `CommandBufferRecordMain.cpp:173,176,396-397,423-424`; `BufferManager.cpp:572-573,612-613,660-661` [~30m]

## Critical files
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`, `CommandBufferRecordGlobal.cpp`, `BufferManager.cpp`
- `Engine/Source/Graphics/Objects/Pipeline.{h,cpp}` (new helper)
- `Engine/Source/Graphics/GraphicsUtils.h` (if the tile helper lands there)

## Out of scope
- Gating/windowing of the recorded passes (live plans: `DisabledPassGatingPerfAudit`, `WindowedLightingShadowDispatch`)
- The occupancy-grid deletion (`Architecture_LightingOccupancyRemoval.md` — may remove one of the dispatch sites; refresh)
- Any pass reordering or barrier change

## Notes
- Invariant exposure: none — client/graphics-only, record-once command buffers; move-only decomposition. Sequencing: `DisabledPassGatingPerfAudit` and `WindowedLightingShadowDispatch` cite line ranges in both record files (Order.md File Groups) — land this after them or refresh their citations; the `Pipeline.h` touch respects the pipeline-cluster constraint (never interleave with `BindlessSlotLifecycle`)
- No open decisions — mechanical
