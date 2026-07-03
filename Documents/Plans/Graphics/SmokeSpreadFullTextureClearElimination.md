# Eliminate the per-frame full-texture clears in the smoke spread pass

## Context

`kGpuTimerSmokeSpread` measures **1210 µs current / 1245 avg / 1324 max** on an **idle scene** (Profile build, 120 fps,
~6.6 ms total GPU/frame, no combat, `kGpuTimerSmokeEmit` = 4 µs — nothing emitting). `kGpuTimerWindSpread`, the
structural twin running over the **same-extent, same-bytes-per-texel** textures, measures **129 µs**. The smoke sim pays
~1.2 ms/frame (~18% of the GPU frame) to simulate nothing.

**Idle-scene caveat**: these numbers are the *fixed* (content-independent) cost. Because the cost is fixed, the same
~1 µs/frame-per-megatexel-cleared tax is also paid mid-combat, on top of the content-proportional spread work.

Root cause (verified by code inspection, and by the 10× wind asymmetry): the smoke spread already implements
occupancy-driven indirect dispatch — bit-packed per-8×8-tile occupancy, dilate/compact into an active-tile list,
`vkCmdDispatchIndirect` (`CommandBufferRecordGlobal::RecordSmokeSpreadHalf`) — so the dilates, fills, and spread
dispatches cost ≈ wind's 129 µs when empty. The remaining ~1080 µs is the **two `vkCmdClearColorImage` calls per frame**
in `RecordSmokeSpreadHalf` (one per ping-pong half), each clearing a full smoke texture. At a 3840×2160 framebuffer the
smoke textures are ≈ **8192×4608 R32_SFLOAT** (`SmokeSimulationPixels()` scales `kfSmokeReferencePixels` = 8192 by
actual/reference width; default `gSmokeSimulationPixels` = 1.0) = **151 MB each, 302 MB of clear writes per frame**
(~280 GB/s effective — full-bandwidth, as expected: STORAGE-usage images typically forfeit clear/DCC metadata fast
paths). 1081 µs / 75.5 Mtexel ≈ **14.3 µs per megatexel cleared**.

Wind proves the clear is not structurally required: `CreateWindTextures` hard-clears once at creation/recreate, and the
wind field's exact-zero convergence self-extinguishes the active-tile set — there is **no per-frame wind texture clear**
(`Engine/Data/Shaders/Wind/CLAUDE.md`, "Exact-zero convergence"). Smoke already has the matching exact-zero mechanism
(`SmokeSpreadTwo.comp` constant subtraction + `kfSmokeZeroThreshold` zeroing, documented as load-bearing for the
hierarchical culling). The only gap the clear currently papers over is **stale texels**: a texel written nonzero in an
earlier frame whose tile is *not* in the current active set keeps its old value at a UV that (after camera pan/zoom
remap) corresponds to the wrong world position — ghost smoke. Wind accepts this (its field is only consumed by smoke
spread, invisible); smoke's field is directly visible, so eliminating the clear needs a correctness mechanism, below.

Relationship to `Graphics/DisabledPassGatingPerfAudit.md` (live): that audit lists `RecordSmokeSpreadPipeline` as a
measure-then-decide site with "accept is the likely outcome unless a pass measures non-trivial". **This measurement is
the missing number, and it argues the opposite for the smoke row**: 1.2 ms idle is non-trivial by any calibration.
However, edge-triggered gating (that audit's lever) only helps when smoke is *disabled*; this plan removes the fixed
cost **content-drivenly** — idle, mid-combat, and enabled-but-empty all drop to wind-class cost — which is strictly
stronger than gating for this site. The audit's smoke row then measures the ~130-250 µs residue and can land on
"accept" per its own precedent. Do not fold this plan into the audit; they compose.

## Design

Make smoke mirror wind's no-per-frame-clear structure, plus one union term wind lacks (smoke needs visible-field
correctness):

1. **Split the shared smoke occupancy buffer into a per-texture pair**, mirroring `mWindOccupancyVkBuffers[0/1]`:
   `mSmokeOccupancyVkBuffers[0]` describes `mSmokeTextureOne`'s last-written nonzero tiles, `[1]` describes
   `mSmokeTextureTwo`'s. (Semantics wind already documents: "Occupancy[i] describes Texture i's content".) The single
   `mSmokeActiveTileVkBuffer` stays shared — it is reset and rebuilt within each half and never persists across frames.
   Per-frame record order per half (B then A, as today):
   - Dilate for half X reads the *input* texture's occupancy (as today: plain 5×5 for B, scale-aware remap for A)
     **OR-united with the *output* texture's own occupancy bit at the output tile index** (new term, new binding).
   - Clear (fill) the *output* texture's occupancy buffer, then indirect-spread writes the output texture and re-marks
     its occupancy — exactly today's structure, just against the split buffers.
2. **Union term = the stale-texel guarantee.** A tile whose storage was nonzero at its last write is re-dispatched this
   frame regardless of where the input remap says smoke lives now, so the spread rewrites it with the correctly
   remapped sample — which is ≈ 0 when the smoke has moved away, i.e. the same value today's clear produces. The
   own-occupancy bit is tile-index-based (output-texture storage space), so it is immune to the coordinate-frame shift
   between frames that makes remap-only activation insufficient (the exact reason the clear exists today). When the
   rewrite lands zero, the bit self-clears — no permanent active-set growth. Steady camera: near-zero growth (old and
   new footprints coincide). Panning: active set ≈ old footprint ∪ new footprint (≤ 2× content-proportional, still ≪
   full texture).
3. **Remove both `vkCmdClearColorImage` calls** and their `kShaderReadOnly → kTransferDestination → kComputeReadWrite`
   transitions from `RecordSmokeSpreadHalf`; the spread's existing `kShaderReadOnly → kComputeReadWrite → kShaderReadOnly`
   transitions remain.
4. **Add a creation-time hard clear** of both smoke textures in `RenderTargetTextures::CreateSmokeTextures`, copying
   `CreateWindTextures`' `OneShotCommandBuffer` + `vkCmdClearColorImage` block verbatim — inactive tiles must never
   hold undefined contents across boot/recreate. (Per NVIDIA guidance, `vkCmdClearColorImage` is the right primitive
   *when* a clear is needed; this plan removes the per-frame need, not the primitive.)
5. **Deposits**: `Smoke.frag` renders into `mSmokeTextureOne` (Main CB) and already atomicOrs the occupancy buffer —
   bind it to `mSmokeOccupancyVkBuffers[0]` (TextureOne's). Deposited tiles then enter both B's input dilation and A's
   own-union term next frame, unchanged in spirit.
6. **`gbSmokeClear` edge path unchanged**: `kPipelineSmokeClearA/B` (indirect-gated full-texture fragment clears fired
   by `RenderSmokeGlobal` on the enable/disable latch) remain the force-clear. Stale occupancy bits after a force-clear
   cost one wasted rewrite frame and then self-clear — no action needed.

Researched alternatives, judged against the record-once CB constraint (per-frame variation only via host-visible
buffers / indirect dispatch — `WriteIndirectComputeBuffer` / `RecordComputeIndirect` exist):

- **Indirect-dispatched "clear previously-active tiles" pass** per half: correct, but needs per-half persistent
  written-tile lists *plus* a deposit-tile feed — strictly more machinery than the union term for the same result.
- **Half/quarter-rate temporal updating**: implementable (convert dilates to indirect, zero the groups on skip frames),
  but after this plan the residue is wind-class (~150-250 µs); rate-halving that saves ~100 µs for visible smoke-motion
  judder at 120 fps. Rejected (YAGNI).
- **Lower sim resolution + upsample**: already a runtime lever — `gSmokeSimulationPixels` (0.5-1.5, default 1.0),
  quadratic savings; a soft density field needs no bilateral upsample (bilinear consumers already). Orthogonal; no change.
- **In-shader early-out on empty texels**: inferior to the existing tile-level culling; still pays dispatch + fetch.

**Invariant exposure**: client/graphics-only (smoke/wind are render-only, wall-clock driven, never in deterministic
frame state — no CRC / replay / wire / `kiVersion` exposure). **Shader repack required** (dilate `.comp` edits: new
binding + union term). New buffer split touches `BufferManager` create/destroy and `PipelineManager` descriptor writes.
No allocation-tracked-path or client/server-guard changes (`CommandBufferRecordGlobal.cpp` is already
whole-file `BT_CLIENT`).

**Visual impact statement (mandatory)**: **(a) no visual change.** Every texel a consumer can sample is either zero
(never written / self-extinguished — identical to today's cleared state) or freshly written by the spread this frame
with the same remapped sample the current code produces; former-footprint tiles get an explicit ≈0 rewrite instead of a
transfer clear. The residual risk is an implementation bug (ghost smoke under fast pan/zoom), not a designed tradeoff —
playtest by panning/zooming hard across an active smoke field. No quality slider warranted (pure win); the existing
`gSmokeSimulationPixels` slider remains the resolution/cost lever.

Expected saving: **~1.0-1.1 ms GPU/frame at 4K idle** (1210 µs → wind-class ~150-250 µs residue: dilates + fills +
barriers, plus one extra occupancy read per dilate thread); mid-combat saves the same fixed clear cost, with the
stale-footprint rewrite costing bandwidth proportional to smoke coverage instead (worst case ≈ today's clear, typical
case ≪).

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — `RecordSmokeSpreadHalf` (delete the
  `vkCmdClearColorImage` block + its transitions; re-point occupancy fills/barriers at the per-half buffer) and
  `RecordSmokeSpreadPipeline` (inter-half barrier now covers the split buffers).
- `Engine/Source/Graphics/Managers/BufferManager.cpp` / `.h` — `mSmokeOccupancyVkBuffer` → `mSmokeOccupancyVkBuffers[2]`
  (+ sizes/allocations, create/destroy), mirroring `mWindOccupancyVkBuffers`.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `CreateSmokeWindPipelines`: descriptor writes for the two
  dilate pipelines (new own-occupancy binding), spread pipelines and `Smoke.frag` deposit re-pointed at the split buffers.
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilate.comp` / `SmokeOccupancyDilateRemap.comp` — add the own-occupancy
  binding and the OR-union activation term.
- `Engine/Data/Shaders/Smoke/SmokeSpreadOne.comp` / `SmokeSpreadTwo.comp` — occupancy binding now the output texture's
  buffer (binding index unchanged if only the bound buffer changes; verify).
- `Engine/Data/Shaders/Smoke/Smoke.frag` — `smokeOccupancyBuffer` binding → TextureOne's buffer.
- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` — `CreateSmokeTextures`: add the creation-time hard clear
  (copy the `CreateWindTextures` block).
- `Engine/Source/Graphics/Render/SmokeUniforms.cpp` — `RenderSmokeGlobal` (no code change expected; the `gbSmokeClear`
  latch and `kPipelineSmokeClearA/B` gating are load-bearing context).
- `Engine/Data/Shaders/Smoke/CLAUDE.md`, `Engine/Source/Graphics/Render/CLAUDE.md` — update the "smoke clears every
  frame / wind clears once" contrast once it inverts.

## Out of scope

- `Graphics/DisabledPassGatingPerfAudit.md` — not edited; it stays the measure-then-decide audit for the *other* sites
  (wind, object-shadows, wind deposit). Its smoke measurement should be re-taken after this lands (expected: accept the
  residue).
- Enable/disable edge gating of the remaining smoke fixed cost (dilates/fills) — that is exactly the audit's question;
  the residue is wind-class small.
- Wind-side changes (`RecordWindSpreadPipeline` is already clear-free; its 129 µs residue is the audit's row).
- Half-rate temporal updating, sim-resolution default changes, occupancy-word-granularity tuning (rejected above).
- The smoke texture format change — separate plan `Graphics/SmokeSimTextureFormatReduction.md` (composes with this one;
  co-schedule, same files).
- `kfSmokeConstantDecay` / `kfSmokeZeroThreshold` retuning (only relevant under the format plan).

## Notes

- Pre-staged grill decision: **union-term dilate (recommended) vs indirect clear-pass over previously-written tiles** —
  both correct; the union term reuses the existing dilate structure and adds no new pass, the clear-pass variant keeps
  activation semantics untouched but adds per-half persistent lists + a deposit feed. Recommend union term (KISS).
- The union term makes smoke's staleness handling *stronger* than wind's (wind tolerates stale texels by consumer
  invisibility). Do not back-port it to wind without a measured reason.
- Co-scheduling: `CommandBufferRecordGlobal.cpp` is shared with `Graphics/DisabledPassGatingPerfAudit.md` and (per
  the existing Dependencies entry) `Graphics/WindowedLightingShadowDispatch.md`; `Graphics/Refactor_CommandBufferRecordDedup.md`
  plans to decompose the sibling Main record file. Co-schedule or refresh citations.
- External references from the technique survey: NVIDIA "Advanced API Performance: Vulkan Clearing and Presenting"
  (clear-path guidance, batch clears, compute-clear tradeoffs) — https://developer.nvidia.com/blog/advanced-api-performance-vulkan-clearing-and-presenting/ ;
  sparse "bricking"/active-brick fluid sim with indirect dispatch (the pattern smoke already implements) — e.g.
  "Fast Fluid Simulations with Sparse Volumes on the GPU" https://people.csail.mit.edu/kuiwu/gvdb_sim.html .
- Measurement provenance: built-in GPU timestamp profiler (`kGpuTimerSmokeSpread`, `ProfileManagerBase.h`), Profile
  build, idle scene, 120 fps. Re-measure at 1080p too when executing (clear cost scales with `SmokeSimulationPixels()`²,
  i.e. framebuffer-width²).
