# Bugfix: Bindless-array slots orphaned across `kPipelines`-tier destroy

## Context

Surfaced while fixing `gWaterUnderseaCompression` (see `~/.claude/plans/gwaterunderseacompression-breaks-the-isl-groovy-hedgehog.md`). That fix removed the only frequently-touched trigger but the underlying bug remains for every other setting that escalates to `kPipelines` destroy — sampler-recreate, MSAA toggle, sample-count changes, shadow texture resolution, smoke simulation pixels, terrain-elevation texture multiplier, etc. (≈10 trigger sites in `Engine/Source/Graphics/Graphics.cpp`).

## Symptom

Any `kPipelines`-tier destroy after islands have become resident leaves the `kPipelineTerrainElevation` pipeline's bindless heightmap array (`mElevationTextures`) pointing at the slot-0 placeholder for every slot. The elevation prepass samples the placeholder → vertex Z collapses to ~0 in `Terrain.vert` → flat grey islands until full app restart. Visible immediately if the user touches sampler/MSAA/shadow-size sliders during play; otherwise silent for the rest of the session.

## Root cause

`Engine/Source/Frame/IslandTerrain.cpp:536` (`RestorationSweep`):

```cpp
if (rTemplate.mbGpuResident || rTemplate.miTextureSlot < 0) { continue; }
```

`UpdateArrayBindingsForKey(rCrc)` (line 570) only fires on the non-resident → resident transition. After `mpPipelineManager.reset()` + reconstruct, every already-resident template silently skips re-patching even though its descriptor set is a freshly-allocated set whose array slots still point at the placeholder.

The same pattern likely applies to color / normals / AO / masks bindless arrays on `kPipelineTerrain` — they're populated by `ProcessPendingTextures`'s `UpdateDescriptorsForTexture` path on first kReady transition, with no re-fire on pipeline recreate.

## Approach options

- **A. Force a re-patch sweep on pipeline recreate.** After `mpPipelineManager = std::make_unique<PipelineManager>()` returns, call a new `IslandTerrain::RepatchAllBindlessSlots()` that walks all resident templates and calls `UpdateArrayBindingsForKey(rCrc)` unconditionally. Cheapest fix, minimal new state. Risk: relies on hooking the right call site after every PipelineManager reconstruct.
- **B. Generation counter on the bindless-array binding.** Mirror the existing per-`Texture` generation pattern: bump a generation on PipelineManager construct, snapshot it alongside each registered array consumer; `RestorationSweep` compares snapshots and re-patches stale entries regardless of residency. Aligns with the existing descriptor-staleness invariant (`PipelineManager::VerifyAllDescriptorGenerations`).
- **C. Stop placeholder-initialising bindless array slots and instead seed PipelineManager construction with live per-slot pointers from `IslandTerrain`.** Eliminates the gap entirely but reshuffles the lazy-mint contract and likely breaks the slot-0 fallback that islands rely on during first-mint.

Recommend **B** — extends an already-documented invariant, no new code path needed at the recreate site, catches future bindless-array consumers automatically.

## Verification

1. Drop a temp `kDebug` log in `RestorationSweep` printing per-island slot binding state.
2. Run the sandbox, let an island become resident.
3. Toggle Multisampling (Graphics.cpp:325) or move the shadow-texture-resolution slider to fire `kPipelines` destroy without touching compression.
4. Without the fix: terrain flattens to grey as soon as the next frame draws.
5. With the fix: terrain redraws correctly; log shows re-patch fired for already-resident slots.

## Out of scope

- The bbox-edge clear-value discontinuity that the original compression-bake was working around. Tracked separately if it ever becomes visually objectionable.
