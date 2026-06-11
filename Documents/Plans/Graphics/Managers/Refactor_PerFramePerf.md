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
