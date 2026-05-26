# Elevation Descriptor Restoration Decoupling

## Context

Surfaced during the island-lockup audit. When an island slot is minted (or re-minted after an eviction reclaimed it), its template-owned elevation `VkImage` is created and valid immediately — `AcquireTextureSlot` calls `CreateElevationTextureFromHeightmap` (`Engine/Source/Frame/IslandTerrain.cpp:253`/`:328`) which uploads the in-memory heightmap synchronously. But the slot's per-pipeline Set-1 elevation descriptor is only patched in `IslandTerrain::RestorationSweep` (`:556`), via the `UpdateArrayBindingsForKey` call (`:604`), which is gated on `mbGpuResident` — set only after **all 4 chunk channels** (color / normals / AO / masks) reach `ChunkState::kReady` (the `bAllReady` loop, `:586-595`).

So the freshly-minted slot's elevation descriptor stays on the slot-0 placeholder (flat sea-level) until the 4 disk-loaded channels finish resolving — producing a visible terrain-elevation pop-in during island load (the island appears flat, then snaps to real relief once color/normals/AO/masks land).

This is correctness-**safe**: the slot-0 placeholder elevation is a valid `VkImageView` (no UAF — that concern was the eviction lockup, already fixed). It is purely load-time visual latency.

## Design

- Patch the elevation array descriptor independently, as soon as the slot's elevation upload completes — decoupled from the 4-channel `bAllReady` gate. Because `CreateElevationTextureFromHeightmap` is synchronous at first-mint, the elevation image is already valid by the time `RestorationSweep` first sees the slot.
- Track a per-template elevation-patched flag (e.g. on `IslandTemplate`, paralleling `mbGpuResident`) so `RestorationSweep` patches the elevation binding exactly once per mint, on the first sweep after the slot exists, without re-issuing the write every frame.
- All descriptor writes must stay inside `RenderGlobal`'s drained post-fence-wait window. `RestorationSweep` already runs there, so implement this as a dedicated elevation-restoration step within `RestorationSweep` (its own flag / early-patch branch) rather than the keypress / `AcquireTextureSlot` path. Do NOT write descriptors from `AcquireTextureSlot`.
- The existing `mbGpuResident` transition still drives the 4-channel color/normals/AO/masks patch — only the elevation `UpdateArrayBindingsForKey(rCrc)` is hoisted ahead of it.

## Critical files

- `Engine/Source/Frame/IslandTerrain.cpp` — `RestorationSweep` (`:556`), the `UpdateArrayBindingsForKey(rCrc)` elevation patch (`:604`), the `bAllReady` 4-channel gate (`:586-596`), `CreateElevationTextureFromHeightmap` (`:253`) and its first-mint call (`:328`).
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate` (`mbGpuResident`, `miTextureSlot`); add the per-template elevation-patched flag here.
- `Engine/Source/Graphics/Managers/TextureDescriptors.{h,cpp}` — `UpdateArrayBindingsForKey` (the array-only CRC-keyed patch); no signature change expected, referenced for the elevation write.

## Out of scope

- The 4-channel color / normals / AO / masks residency gating (`bAllReady`, `mbGpuResident`) — unchanged; only the elevation descriptor is decoupled.
- Any use-after-free / freed-view concern — that was the eviction lockup and is already fixed. The slot-0 elevation placeholder is a valid view.
- The eviction path (slot teardown patching elevation back to placeholder) — already correct via the eviction symmetry invariant; not touched.

## Acceptance criteria

- A freshly-minted (or re-minted) island shows real elevation relief without waiting on its color/normals/AO/masks chunks to finish loading — no flat-then-snap elevation pop-in.
- The elevation descriptor is patched exactly once per mint (no per-frame re-write).
- Every descriptor write still occurs inside `RenderGlobal`'s drained post-fence-wait window (inside `RestorationSweep`); none originate from the keypress / `AcquireTextureSlot` path.
