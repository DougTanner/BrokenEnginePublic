# Refactor: Command Buffer Record Dedup & Decomposition

## Context
Source: /external-refactor-clean on Engine/Source (recursive). `CommandBufferRecordMain::Record` is a ~300-line nine-pass monolith while its sibling `CommandBufferRecordGlobal::Record` is already decomposed into per-pass statics (intra-directory drift), and two idioms repeat across the record/dispatch sites.

## Design

### Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp
- Decompose `Record` (:17-317) into per-pass private statics matching the Global sibling's convention (`RecordShadowPasses`/`RecordTerrainPasses`/... — only `RecordLightingSpreadPipeline` exists on the Main side today). Nine passes: lighting deposit, smoke emit, wind deposit A/B, object shadows + blur, water displacement, main render-pass body (UI prepass/models/terrain/water/particles/text). Move-only, no logic change [~1h]

### Engine/Source/Graphics/Objects/Pipeline.{h,cpp} — shared indirect-dispatch helper
- Extract `Pipeline::RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, VkBuffer, VkDeviceSize)` (or a file-local static): the "compute `iDescriptorSetIndex` from `mbPerCommandBuffer`, bind pipeline, `BindComputeDescriptorSets`, `vkCmdDispatchIndirect` from an externally owned VkBuffer" block is copied at `CommandBufferRecordGlobal.cpp:257-262, 267-272, 354-359` and the bind prologue of `CommandBufferRecordMain.cpp:383-386` (needed because `RecordComputeIndirect` only dispatches from the pipeline's own `mIndirectVkBuffer`) [~30m]

### Tile-count helper
- Add a one-line `TileCount(uint32_t)` helper (GraphicsUtils or `shaders::`) and fold the ~16 hand-expanded `(w + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize` expressions: `CommandBufferRecordGlobal.cpp:43-44,48-49,80-81`; `CommandBufferRecordMain.cpp:173,176,396-397,423-424`; `BufferManager.cpp:572-573,612-613`; `Render/WindUniforms.cpp:54-55`; `Render/SmokeUniforms.cpp:37-38`. Do NOT touch `Render/MainUniforms.cpp:353` (a `+ 1` variant — fold only if it reads cleanly as `TileCount(iQuadCountX + 1)`) [~30m]

## Critical files
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`, `CommandBufferRecordGlobal.cpp`, `BufferManager.cpp`
- `Engine/Source/Graphics/Render/WindUniforms.cpp`, `SmokeUniforms.cpp` (tile-count fold)
- `Engine/Source/Graphics/Objects/Pipeline.{h,cpp}` (new helper)
- `Engine/Source/Graphics/GraphicsUtils.h` (if the tile helper lands there)

## Out of scope
- Gating/windowing of the recorded passes (live plans: `DisabledPassGatingPerfAudit`, `WindowedLightingShadowDispatch`)
- Any pass reordering or barrier change

## Coordination

- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory reciprocal pipeline-cluster exclusion; never interleave because BindlessSlotLifecycle executes alone.

## Notes
- Invariant exposure: none — client/graphics-only, record-once command buffers; move-only decomposition. Sequencing: `DisabledPassGatingPerfAudit` and `WindowedLightingShadowDispatch` cite line ranges in both record files (Order.md File Groups) — land this after them or refresh their citations; the `Pipeline.h` touch respects the pipeline-cluster constraint (never interleave with `BindlessSlotLifecycle`)
- No open decisions — mechanical

## Verification Notes

- Re-verified 2026-07-02: `Record` spans `:17-317` and `RecordLightingSpreadPipeline` is the only per-pass static on the Main side (Global has five); `Pipeline::RecordComputeIndirect` (`Pipeline.cpp:324-345`) dispatches only from the pipeline's own `mIndirectVkBuffer`, confirming the three externally-buffered Global blocks are hand-copied. Note the Main `:382-400` combine site shares only the bind prologue — it pushes its own `CombinePushConstantsLayout` and dispatches direct, so the helper must not swallow the push/dispatch.
- Original tile-count cite `BufferManager.cpp:660-661` was wrong (different formula) — corrected above; Render-side sites added.
