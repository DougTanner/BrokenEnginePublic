# Architecture: Bindless Slot Lifecycle — Single Owner

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). The island bindless
slot lifecycle invariant — "a recycled slot must never sample a destroyed view" — currently has no single
owner. Understanding (or safely changing) it requires five places: `TextureDescriptors`' patch/unregister
functions, `IslandTerrain`'s mint/evict/restore sweeps, `TextureManager`'s slot-0 placeholder arrays +
pointer-stability rule, the RenderGlobal fence-drain window, and a deliberate carve-out in the descriptor
staleness verifier. The eviction-symmetry invariant (`Engine/Source/Graphics/Managers/CLAUDE.md:29`) is
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
- Convert `IslandTerrain`'s sweeps to call those three methods instead of patching five parallel arrays and a
  binding map (`Engine/Source/Frame/IslandTerrain.cpp:525-595, 819-829`). [~1h]
- Enforce eviction symmetry inside the registry (one code path mints/evicts *all* channels of a slot
  together), turning the `Managers/CLAUDE.md:29` doc invariant into code. [~30m]
- Narrow or remove the `VerifyAllDescriptorGenerations` carve-out (`PipelineManager.cpp:856-862`): with a
  single owner stamping generations, the verifier can cover bindless array elements instead of exempting them.
  [~30m]
- Keep the RenderGlobal fence-drain window (`Engine/Source/Graphics/Graphics.cpp:197-206`) as the sole
  phase where the registry may patch live sets — ASSERT it inside the registry rather than relying on caller
  discipline. [~30m]

## Critical files
- `Engine/Source/Graphics/Managers/TextureDescriptors.h`, `TextureDescriptors.cpp`
- `Engine/Source/Graphics/Managers/TextureManager.h`, `TextureManager.cpp`
- `Engine/Source/Frame/IslandTerrain.h`, `IslandTerrain.cpp`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (verifier carve-out)
- `Engine/Source/Graphics/Graphics.cpp` (read-only context: RenderGlobal window)
- `Engine/Source/Graphics/Managers/CLAUDE.md` (invariant doc moves from prose to enforced)

## Out of scope
- Changing the slot allocation policy, slot counts, or eviction heuristics — the *who-patches-what* moves;
  the *when-and-which-slot* decisions stay in `IslandTerrain`.
- The lighting-blur slot path (`CrcToBlurredIndex` fallback) beyond what the registry move touches — its
  synchronization is `Architecture_ThreadAndLifetimeGuards.md`.
- The RenderGlobal phase ordering itself (fence drain → sweeps → adoption → restoration) — unchanged.

## Acceptance criteria
- One code path performs every bindless array element write; `IslandTerrain` no longer touches descriptor
  arrays directly.
- Eviction of a slot provably evicts all channels (single function), and `VerifyAllDescriptorGenerations`
  covers bindless elements (carve-out removed or reduced to slot-0 placeholders only).
- Client builds and renders identically; island stream-in/out (eviction + restoration) works under texture
  memory pressure.

## Notes
- No determinism/CRC exposure (client render path), but **high blast radius within rendering**: this is the
  machinery that prevents GPU use-after-free during island slot recycling; regressions are timing-dependent
  visual corruption or device loss. Execute alone, not co-scheduled.
- Pre-staged grill decisions: (a) registry as inner class of `TextureDescriptors` vs sibling manager;
  (b) whether `RestoreSlot` keeps the per-channel granularity the restoration sweep currently uses.
- Builds on (and should land after) `Architecture_ThreadAndLifetimeGuards.md`'s `CrcToIndex` decision, which
  touches the same file.
