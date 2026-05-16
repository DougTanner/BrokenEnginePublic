# Dynamic Island Loading — Follow-ups

## Context

The Phase-5 dynamic subscription-driven island texture loading landed this
session. Step-9 review surfaced two out-of-scope concerns around
`IslandTerrain::AcquireTextureSlot`, `TextureManager` lifecycle, and the
hot-path allocation discipline. Both are narrow, related to slot lifecycle
state, and worth resolving together. (A third follow-up — investigating the
`kPipelines` destroy-tier escalation in `AcquireTextureSlot` — was removed
after commit `09fb128` deleted that escalation entirely.)

## Design

### Follow-up 1 — Device-lost recovery leaves all islands on placeholder

**Severity: CRITICAL latent.**

After a `DeviceLostException` the main loop catches and recreates `Graphics`
(see `Engine/Source/Main.cpp` + `Graphics.cpp` swap-tier `kSurface` path).
`mpTextureManager.reset()` destroys the old TextureManager; the fresh one runs
its ctor and re-initializes all `kiMaxIslands` slots in the four bindless
arrays to point at the new placeholder Textures.

However `engine::gpIslandTerrain` lives on `gpGame`, not on `Graphics`. It
survives device loss with `miNextTextureSlot >= 1` and every previously-minted
template still has `miTextureSlot >= 1` and `mbGpuResident = true`. The hot
path in `AcquireTextureSlot` early-returns when `mbGpuResident == true`, so
slots are never re-bound — the bindless array's slot N points at the new
TextureManager's placeholder, not the real island Texture. Result: after
device-lost recovery, every island silently renders as the neutral placeholder
(flat sea-level, mid-gray, up-normals, full AO) forever.

**Cross-coupling with boot-time mesh creation (added post-record-once refactor):**
`Islands` ctor now also calls `IslandTerrain::CreateClientMeshBuffers()` and
re-binds every template's mesh buffer at recreate time. That side of the device-
loss path already works (CPU mesh pointers survive device loss because they live
in the kIsland chunk payload, not VMA). The fix below only needs to address
texture-slot residency state — meshes are no longer in scope.

**Fix:** reset the IslandTerrain slot-assignment state when TextureManager is
recreated. Two viable shapes:

- **Option A (recommended):** add `IslandTerrain::ResetTextureSlots()` that
  walks `mIslands`, sets `miTextureSlot = -1` and `mbGpuResident = false` for
  every template, and resets `miNextTextureSlot = 1`. Call it from the new
  `TextureManager` ctor (right before the slot-0 placeholder fan-out) when
  `gpIslandTerrain != nullptr`. Subscriptions already in `mCoordFrames` cause
  `Islands::UpdateActiveIslands` to re-call `AcquireTextureSlot` on the next
  frame, re-minting slots and re-requesting chunk loads naturally.
- **Option B:** add the reset to `Graphics::Destroy(kSurface)` instead. Same
  effect; slightly different ownership.

Verification: synthetic device-lost test (force `vkDeviceWaitIdle` failure or
window-resize storm), confirm islands re-appear after the new Graphics tier
finishes initializing.

**Scope widened by shadow-elevation registration fix (this session).**
`AcquireTextureSlot` first-mint now calls `RegisterTextureBinding` twice — once
for `kPipelineTerrainElevation`, once for `kPipelineShadowElevation` — because
both pipelines bind `mElevationTextures` and each owns its own descriptor that
must be patched in lockstep by `UpdateArrayBindingsForKey(islandCrc)`. After
device-loss the fresh `TextureDescriptors` ctor clears `mTextureBindings`, so
the `miTextureSlot >= 0` early-return strands BOTH the terrain elevation and
the shadow elevation pipelines on slot-0 placeholder. The recommended
`ResetTextureSlots` fix must re-execute both `RegisterTextureBinding` calls
(or — preferred — fully reset `miTextureSlot`/`mbGpuResident` so the natural
re-mint cycle in `AcquireTextureSlot` re-registers both). Code-walk both call
sites after the reset lands to confirm parity.

### Follow-up 2 — `Islands::UpdateActiveIslands` re-acquire path may trip allocation DEBUG_BREAK

**Severity: IMPORTANT.**

`Islands::UpdateActiveIslands` calls `AcquireTextureSlot(islandCrc)` per active
placement every frame. On the hot path (`miTextureSlot >= 0 && mbGpuResident`)
this returns early with no allocations.

The cold path inside `AcquireTextureSlot` — when an evicted slot is
re-referenced — calls `gpFileManager->RequestChunkLoad(textureCrcs, kRealtime)`
and emits a `LOG`. `UpdateActiveIslands` is the main-loop hot path; main-loop
heap allocations trigger `DEBUG_BREAK()`.

Two paths to resolve:

- **Path A:** audit `FileManager::RequestChunkLoad` — confirm whether the
  realtime priority queue or any internal containers can grow in this call.
  If always allocation-free on the steady-state hot path, document the
  invariant inline.
- **Path B:** wrap the AcquireTextureSlot call site at
  `Engine/Source/Graphics/Islands.cpp:155` with a narrow
  `ScopedSuppressAllocationTracking` + `// Heap:` comment if Path A finds
  unavoidable allocations.

The `ClientDataReceiver::ApplyReceivedStaticData` hook is already wrapped in
`ScopedSuppressAllocationTracking` and is therefore unaffected.

## Critical files

- `Engine/Source/Frame/IslandTerrain.h` — add `ResetTextureSlots()` declaration
- `Engine/Source/Frame/IslandTerrain.cpp` — implement reset (touches both
  `kPipelineTerrainElevation` and `kPipelineShadowElevation` registration
  paths via the natural re-mint cycle)
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — hook
  `gpIslandTerrain->ResetTextureSlots()` from ctor (or from
  `Graphics::Destroy(kSurface)`)
- `Engine/Source/Graphics/Islands.cpp:155` — optional
  `ScopedSuppressAllocationTracking` wrap for Follow-up 2 Path B
- `Engine/Source/File/FileManager.cpp` — audit `RequestChunkLoad` allocation
  behavior for Follow-up 2 Path A

## Out of scope

- The placeholder Texture creation pattern (4 near-identical Create calls in
  TextureManager.cpp) — flagged by style review but intentional; do not
  consolidate.
- The repeated 4-channel array assignments in `AcquireTextureSlot` /
  `EvictionSweep` / `RestorationSweep` — same pattern across `RenderTargetTextures`,
  consolidation would require restructuring that struct, out of scope here.

## Notes

- Both follow-ups are narrow and localized to the TextureManager /
  IslandTerrain interaction. Land them in one session; the diffs share
  context and reviewing them together is cheaper than two separate passes.
- Follow-up 1 is the only one with user-visible behavior (device-lost
  recovery rendering). Follow-up 2 is correctness-of-instrumentation.
  Prioritize Follow-up 1.
- Verification for Follow-up 1 needs a deliberate device-lost trigger; if no
  reliable trigger exists, settle for code-walk confirmation of the reset path
  re-registers BOTH `kPipelineTerrainElevation` and `kPipelineShadowElevation`.
