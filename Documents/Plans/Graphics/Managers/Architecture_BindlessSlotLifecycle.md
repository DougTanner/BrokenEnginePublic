# Architecture: Bindless Slot Lifecycle — Single Owner

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). The island bindless
slot lifecycle invariant — "a recycled slot must never sample a destroyed view" — currently has no single
owner. Understanding (or safely changing) it requires five places: `TextureDescriptors`' patch/unregister
functions, `IslandTerrain`'s mint/evict/restore sweeps, `TextureManager`'s slot-0 placeholder arrays +
pointer-stability rule, the RenderGlobal fence-drain window, and a deliberate carve-out in the descriptor
staleness verifier. The eviction-symmetry invariant (`Engine/Source/Graphics/Managers/AGENTS.md:29`) is
enforced nowhere — `VerifyAllDescriptorGenerations` explicitly *exempts* the array elements where this bug
class lives (`PipelineManager.cpp:856-862`). This was the architecture review's single most impactful
recommendation: it targets the directory's only documented GPU use-after-free bug class and removes the one
place the staleness verifier must look away.

## Design

### Consolidate into a slot registry owned by TextureDescriptors
- Extract the mint/evict/restore descriptor patching — `WriteArrayElementFromLive` and
  `UnregisterBindingsForKey` (`TextureDescriptors.cpp:210-246, 320-326`), `UpdateArrayBindingsForKey`, the
  `mBindlessArrayConsumers` consumer registry, and the slot-0 placeholder arrays + pointer-stability rule
  (`TextureManager.cpp:278-309`) — into one object (e.g. `BindlessSlotRegistry` inside `TextureDescriptors`)
  exposing `MintSlot` / `EvictSlot` / `RestoreSlot(channel)`. [~1h design + ~2h move]
- Convert the island sweeps to call those three methods instead of patching five parallel arrays and a
  binding map (mint in `AcquireTextureSlot`; `EvictionSweep`/`RestorationSweep` live in
  `Engine/Source/Frame/IslandTerrainResidency.cpp` — the client-only residency TU split out of
  `IslandTerrain.cpp`). [~1h]
- Single-source the RenderGlobal gating predicates with the sweep bodies: `AnyEvictionPending` /
  `AnyRestorationPending` (`IslandTerrainResidency.cpp:239` area) each hand-mirror their sweep's
  qualification logic (including the 4-CRC all-ready walk verbatim), so a drift silently breaks the
  RenderGlobal churn gating. Derive predicate and sweep from one shared qualification helper inside the
  registry. [~30m]
- Enforce eviction symmetry inside the registry (one code path mints/evicts *all* channels of a slot
  together), turning the `Managers/AGENTS.md:29` doc invariant into code. [~30m]
- Narrow or remove the `VerifyAllDescriptorGenerations` carve-out (`PipelineManager.cpp:856-862`): with a
  single owner stamping generations, the verifier can cover bindless array elements instead of exempting them.
  [~30m]
- Keep the RenderGlobal fence-drain window (`Engine/Source/Graphics/Graphics.cpp:197-206`) as the sole
  phase where the registry may patch live sets — ASSERT it inside the registry rather than relying on caller
  discipline. [~30m]

## Critical files
- `Engine/Source/Graphics/Managers/TextureDescriptors.h`, `TextureDescriptors.cpp`
- `Engine/Source/Graphics/Managers/TextureManager.h`, `TextureManager.cpp`
- `Engine/Source/Frame/IslandTerrain.h`, `IslandTerrain.cpp`, `IslandTerrainResidency.cpp` (sweeps + gating predicates)
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (verifier carve-out)
- `Engine/Source/Graphics/Graphics.cpp` (read-only context: RenderGlobal window)
- `Engine/Source/Graphics/Managers/AGENTS.md` (invariant doc moves from prose to enforced)

## Out of scope
- Changing the slot allocation policy, slot counts, or eviction heuristics — the *who-patches-what* moves;
  the *when-and-which-slot* decisions stay in `IslandTerrain`.
- The lighting-blur slot path (`CrcToBlurredIndex` fallback) beyond what the registry move touches — its
  synchronization contract (bindless-index phase exclusion) is documented in `Managers/AGENTS.md` and stays.
- The RenderGlobal phase ordering itself (fence drain → sweeps → adoption → restoration) — unchanged.

## Acceptance criteria
- One code path performs every bindless array element write; `IslandTerrain` no longer touches descriptor
  arrays directly.
- Eviction of a slot provably evicts all channels (single function), and `VerifyAllDescriptorGenerations`
  covers bindless elements (carve-out removed or reduced to slot-0 placeholders only).
- Client builds and renders identically; island stream-in/out (eviction + restoration) works under texture
  memory pressure.

## Coordination

- Execute alone; never interleave with `Documents/Plans/Graphics/Architecture_PipelineRegistrationOwnership.md`, `Documents/Plans/Graphics/PipelineDescriptorInfosRightSize.md`, `Documents/Plans/Graphics/Managers/Refactor_PipelineManagerSplit.md`, `Documents/Plans/Graphics/Managers/CorruptTextureChunkLifecycleHardening.md`, `Documents/Plans/Graphics/WindowedLightingShadowDispatch.md`. The structured dependency on CorruptTextureChunkLifecycleHardening preserves its required first landing.

## Notes
- No determinism/CRC exposure (client render path), but **high blast radius within rendering**: this is the
  machinery that prevents GPU use-after-free during island slot recycling; regressions are timing-dependent
  visual corruption or device loss. Execute alone, not co-scheduled.
- Pre-staged grill decisions: (a) registry as inner class of `TextureDescriptors` vs sibling manager;
  (b) whether `RestoreSlot` keeps the per-channel granularity the restoration sweep currently uses.
- The `CrcToIndex` phase-exclusion contract is settled (documented in `Managers/AGENTS.md`); the registry
  inherits it unchanged.
- `RegisterTextureBinding` now takes a designated-initializer struct — refresh call-signature citations at
  execution. The small `WriteArrayElementFromLive` null-view-fallback fix in
  `CorruptTextureChunkLifecycleHardening.md` targets the same function — land it first or fold it into the
  registry move; never interleave.
