# Dynamic Island Loading — Three Follow-ups

## Context

The Phase-5 dynamic subscription-driven island texture loading landed this
session. Step-9 review surfaced three out-of-scope concerns around
`IslandTerrain::AcquireTextureSlot`, `TextureManager` lifecycle, and the
hot-path allocation discipline. All three are narrow, related to slot lifecycle
state, and worth resolving together.

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

### Follow-up 3 — `AcquireTextureSlot` first-mint kPipelines escalation may be unnecessary

**Severity: NIT (performance).**

`AcquireTextureSlot` sets
`gpGraphics->meDestroyType = std::max(DestroyType::kPipelines, gpGraphics->meDestroyType)`
on every first-mint. Under the old eager-load architecture this fired
exclusively during boot (`gpGraphics == nullptr`), so the escalation was a
no-op. Under the new subscription-driven design, first-mint happens at
runtime whenever the player encounters a new island CRC — each one triggers a
full pipeline rebuild.

Today the game ships with one island manifest so this fires at most once. As
soon as the content pipeline produces a second template, every traversal into
a cell with the new template rebuilds all terrain pipelines.

The bindless texture arrays are pre-initialized to the slot-0 placeholder at
TextureManager construction, and runtime descriptor patches go through
`UpdateTextureArrayDescriptors` (the canonical sink). Pipeline rebuild is
probably not needed for a slot-pointer change. Verify by:

- Reading why the `kPipelines` escalation exists at all in
  `AcquireTextureSlot`. Is it for `mImageInfos` resize, for descriptor-set
  layout changes, for shader specialization? Trace by removing the line and
  observing whether anything actually breaks (specifically: validation
  errors, pipeline-cache misses, descriptor mismatches).
- If genuinely unnecessary, delete the escalation and the surrounding `if
  (gpGraphics != nullptr)` block.
- If needed for a real reason, narrow the trigger condition (e.g., only fire
  when the slot count actually grows past a threshold, not on every mint) and
  document the reason in a comment.

## Critical files

- `Engine/Source/Frame/IslandTerrain.h` — add `ResetTextureSlots()` declaration
- `Engine/Source/Frame/IslandTerrain.cpp` — implement reset; investigate
  `kPipelines` escalation in `AcquireTextureSlot`
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

- All three follow-ups are narrow and localized to the TextureManager /
  IslandTerrain interaction. Land them in one session; the diffs share
  context and reviewing them together is cheaper than three separate passes.
- Follow-up 1 is the only one with user-visible behavior (device-lost
  recovery rendering). Follow-up 2 is correctness-of-instrumentation.
  Follow-up 3 is latency. Prioritize Follow-up 1.
- Verification for Follow-up 1 needs a deliberate device-lost trigger; if no
  reliable trigger exists, settle for code-walk confirmation of the reset path.
