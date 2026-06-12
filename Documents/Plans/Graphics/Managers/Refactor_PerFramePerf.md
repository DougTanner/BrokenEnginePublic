# Refactor: Per-Frame Hot-Path Perf (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). The directory's
allocation discipline is fully compliant (verified); the two remaining per-frame costs are a double full-map
scan and worker-thread serialization on culled particles.

## Design

### Engine/Source/Graphics/Managers/TextureManager.cpp — per-frame full `mTextureMap` scans
- `ProcessPendingTextures` (`TextureManager.cpp:615`) and `AnyAdoptionPending` (`:718`) each iterate the
  entire `mTextureMap` — every chunk flagged `kTexture` in the pack (populated unconditionally in the ctor,
  `:241-269`; plausibly hundreds to low thousands of entries) — and both run every frame
  (`Graphics.cpp:197, 204`). Each step is a node chase + `gpFileManager->GetLazyChunk` hash lookup + atomic
  acquire load; the idle frame pays both scans in full. Fix: an atomic relaxed pending-adoption counter,
  incremented at every transition into `kGpuUploadComplete`/`kDiskLoaded`
  (`TextureUploadManager.cpp:179, 188, 404, 420`), decremented at the two adoption points
  (`TextureManager.cpp:662, 684`); `AnyAdoptionPending` → `counter != 0`, `ProcessPendingTextures`
  early-returns on 0 (keep the scan for non-zero frames). Care points: the device-lost `kDiskLoaded` store
  (`TextureUploadManager.cpp:420`) and `FileManager::ResetTextureChunkStates` re-arms must maintain the
  counter; the boot spin `WaitForTextures` (`TextureManager.cpp:742-751`) must still observe it. [~1h]

### Engine/Source/Graphics/Managers/ParticleManager.cpp — culls inside the spawn mutex
- `Spawn` (`ParticleManager.cpp:29-47`) takes `mSpawnMutex` before the visible-area cull (`:38`) and
  intensity cull (`:43`), though both read only the caller-owned layout and the camera rect — so every
  *culled* particle still serializes all workers during parallel tick dispatch. Move both early-outs above
  the `lock_guard`; keep the capacity check, `GetOrAssignTextureIndex` (mutating), and the staging write
  under the lock. [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/TextureManager.cpp`, `TextureManager.h` (counter member)
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` (counter increments)
- `Engine/Source/Graphics/Managers/ParticleManager.cpp`

## Out of scope
- `UpdateTextureArrayDescriptors`' full-array descriptor flush per adoption frame
  (`TextureDescriptors.cpp:115-135`) — bounded by the adoption rate limit; a dirty-range write is only
  justified if island-churn frames show in profiles (needs runtime evidence; deliberately not planned).
- The `ProcessPendingTextures` body extraction — `Refactor_TextureDecomposition.md` (pairs naturally; the
  counter early-out lands first).

## Acceptance criteria
- Idle frames no longer iterate `mTextureMap`; the counter agrees with a full scan under churn (boot, island
  stream-in/out, device-lost recovery); culled particle spawns no longer contend `mSpawnMutex`.

## Notes
- No determinism/CRC exposure — sim RNG/state untouched; `Spawn`'s cull reordering drops the same particles
  it dropped before (cull predicates read the same inputs, just unlocked).
- The counter must be exact, not heuristic: a stuck-nonzero counter re-introduces the scan; a stuck-zero
  counter stalls adoption (visible as permanent placeholders). The device-lost and reset paths are the grill
  focus.

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run). Both items
confirmed:

- **Map scans**: `ProcessPendingTextures` iterates the full `mTextureMap` at `TextureManager.cpp:615` (function
  `:603-708`); `AnyAdoptionPending` at `:718` (function `:710-727`); ctor populates the map unconditionally
  from every `kTexture` chunk at `:241-269`; per-frame call sites `Graphics.cpp:197-199` (the gated pre-scan
  calls `AnyAdoptionPending`) and `:204`. State-transition stores confirmed at `TextureUploadManager.cpp:179,
  188, 404, 420`; adoption-side `kReady` stores at `TextureManager.cpp:662, 684`; boot spin at `:742-751`;
  `FileManager::ResetTextureChunkStates` re-arms to `kNotLoaded`/`kDiskLoaded` (both must maintain the counter,
  as the plan flags).
- **Spawn culls inside the mutex**: `ParticleManager.cpp:29` lock precedes the visible-area cull (`:38`) and
  intensity cull (`:43`); both read only the caller-owned `layout` and `game::gpCamera->f4RenderVisibleArea`.
  Worker-thread contention is real: `ParticleManager::Spawn`'s sole caller is `ExplosionsSpawn.cpp:231`, which
  runs in `ExplosionsPostRender::Spawn` — invoked from game PostRender phase code (`Missiles.cpp:285`,
  `PlayersCombat.cpp:479`, `Spaceships.cpp:271`) inside the parallel-dispatched `RunFrameTick`. The capacity
  check at `:31` reads the shared staging layout and stays under the lock as planned.
