# IslandTemplate mesh buffer device-loss lifecycle

## Context

`IslandTemplate::mMeshBuffer` (added with per-island Gaea Mesher terrain meshes) is allocated through VMA inside `IslandTerrain::AcquireTextureSlot` on first-mint and never destroyed by IslandTerrain. `IslandTerrain` is a Frame singleton (game-owned), but its VMA handles belong to `gpDeviceManager`'s allocator (Graphics-owned). On `DeviceLostException` recovery, Graphics is destroyed and recreated — including the VMA allocator. The mesh buffers' `VkBuffer` / `VmaAllocation` handles are now stale references inside live `IslandTemplate` objects.

The next first-mint after recovery enters the `miTextureSlot < 0` branch (eviction marked `miTextureSlot = -1`? — actually no, only `mbGpuResident = false`). On re-acquisition, `mMeshBuffer.Create()` is called, but the buffer's `mDeviceLocalVkBuffer` is a stale (now-invalid) handle. The new `ASSERT` in `AcquireTextureSlot` will fire under a debugger; in release the second `Create()` leaks the prior allocation in VMA bookkeeping and may VUID-error on Vulkan.

Severity: bounded — only fires on actual device loss, which is rare. But VMA allocator destruction without releasing allocations is a real leak risk.

## Design

Mirror the existing pattern used for `Islands::mIslandsStorageBuffer` (Graphics-owned, destroyed with Graphics; recreated on Graphics restart). Two options:

**Option A (recommended): Have IslandTerrain destroy mesh buffers on Graphics shutdown.**

Add an `IslandTerrain::ReleaseGpuResources()` method called from `Graphics::Destroy()` (just before `mpDeviceManager.reset()`):

```cpp
void IslandTerrain::ReleaseGpuResources()
{
    for (auto& [rCrc, rTemplate] : mIslands)
    {
        rTemplate.mMeshBuffer.Destroy();
        rTemplate.miTextureSlot = -1;
        rTemplate.mbGpuResident = false;
    }
    miNextTextureSlot = 1;
}
```

`Graphics::Create()` after recovery would then see virgin templates and re-mint via the normal AcquireTextureSlot first-mint path. The textures (owned by TextureManager) are already correctly recreated; this extends the recovery to mesh buffers too.

**Option B: Move mesh buffer ownership to a Graphics-owned IslandMeshManager.**

Cleaner architecturally — keeps GPU-resource ownership Graphics-side, matches the existing TextureManager pattern. `IslandTemplate` would hold an `int64_t miMeshSlot` or `Buffer*` pointing at a Graphics-side `std::unordered_map<crc_t, Buffer>`. On Graphics restart the map is empty and rebuilt lazily on first-mint.

Larger surgery; Option A is the KISS fix for the immediate bug.

## Critical files

- `Engine/Source/Frame/IslandTerrain.h` — declare `void ReleaseGpuResources()` member.
- `Engine/Source/Frame/IslandTerrain.cpp` — define the method; iterate `mIslands`, destroy each `mMeshBuffer`, reset slot state.
- `Engine/Source/Graphics/Graphics.cpp` — call `gpIslandTerrain->ReleaseGpuResources()` early in the destroy path (before `mpDeviceManager.reset()`), guarded by `gpIslandTerrain != nullptr`.

## Out of scope

- Move mesh ownership to a Graphics-owned manager (Option B above).
- Hot-restart mesh eviction (mesh remains LRU-immune by design).
- Texture descriptor recovery (already covered by `DynamicIslandLoadingFollowups.md`).

## Acceptance criteria

- Trigger a device-lost recovery (e.g., via the `kSurface` destroy path in dev tooling or actual TDR).
- After recovery, terrain renders correctly for all active islands without `ASSERT` fire or VMA leak warnings.
- `vmaCalculateStatistics` post-recovery shows no orphan allocations relative to a clean boot.
