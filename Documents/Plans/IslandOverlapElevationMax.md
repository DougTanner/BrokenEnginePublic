# Island Overlap: MAX Elevation Compositing + Submerged-Mesh Sink

## Context

The new `IslandChainPlacement` packing lets island **bounding rectangles** overlap (their true valid-area
hulls never do; the overlap is meant to be hidden underwater). Two elevation paths resolve that overlap
incorrectly, plus a third secondary artifact the mesh now exposes:

1. **GPU composite elevation** (`mTerrainElevationTexture`, R32_SFLOAT, client-only). The elevation prepass
   draws one axis-aligned quad per island into the RTT with **no blend → last-island-drawn wins**
   (`kPipelineTerrainElevation`, `PipelineManager.cpp:475`; RTT clears to `mfSeaFloorElevation`,
   `RenderTargetTextures.cpp:340`). The terrain mesh (`Terrain.vert:103`), water, and shadow all sample
   this composite. **This is the visible "picks one island randomly" symptom.**
2. **CPU `GlobalElevation()`** (`IslandTerrain.cpp:212-269`, **shared** client+server). Walks the cell's
   placement list and **returns the first** island whose footprint rectangle contains the point (`:265`).
   Consumed by AI terrain-following / steering (`TerrainUtils`) and `GlobalNormal` (4 finite-diff taps that
   route through `GlobalElevation`). Collision does **not** use elevation; NavData already composites
   correctly via Clipper2 `Union` on per-island contours (`NavBuild.cpp:477`).
3. **Submerged-mesh rise.** Once the composite is MAX'd, island B's Gaea mesh still samples the composite
   for its vertex Z. In an overlap, B's underwater region would be lifted to island A's land height and draw
   B's (baked-black, RGB 0,0,0) terrain there. B's mesh must instead drop to the flat sea floor wherever
   B itself is underwater.

By bake construction, `color RGB == (0,0,0)` ⇔ heightmap `< kfUnderwaterMaskThresholdMeters` (**-2.5 m**,
`Common/DataFile.h:22`) — the same threshold the valid-area hull is baked from. Detecting the sink off the
**per-island heightmap sample** (rather than color) is the robust signal and is what this plan uses.

## Locked decisions

- **Sink scope: all sub-threshold verts.** Every terrain-mesh vertex whose own island heightmap sample is
  below the -2.5 m zero-out line drops to the flat sea floor (`mfSeaFloorElevation`), on every island — not
  only overlap regions. Simplest rule; matches the user's wording. (Supersedes, for the deep `< -2.5 m`
  band, the earlier choice to keep the full underwater skirt drawn at real bathymetry; the visible shallow
  band `-2.5..0 m` still renders from the composite as today.)
- **MAX everywhere overlap is resolved:** GPU terrain-elevation prepass, GPU shadow-elevation prepass, and
  the shared CPU `GlobalElevation`.
- **Sink target = `mfSeaFloorElevation`** (`globalLayout.fSeaFloorElevation`), the same value the RTT clears
  to and `GlobalElevation` returns for open ocean — keeps mesh, water, shadow, and sim agreeing on the floor.

## Constraints (hard)

- **Determinism**: only `GlobalElevation` is shared (client+server). MAX over the cell's placements is
  commutative → order-independent, *more* deterministic than today's first-match. All GPU/shader/uniform
  changes are client-only (Graphics is client-only) and never feed the shared CRC.
- No `kiNavDataVersion` bump (NavData format unchanged). Replays/saves predating this diverge in overlap
  regions (steering reads different heights) — same incidental class as the placement overhaul, not a
  format break.
- No new error handling/validation; no unit tests. KISS/YAGNI/DRY.

## Grill resolutions
- **Root cause confirmed by code reading** (not hypothesized): no-blend last-writer-wins prepass, first-match
  `GlobalElevation`, composite-Z mesh rise. Existing-library gate N/A (blend-state / query fix, engine-glue).
- **Sink scope = all sub-threshold verts** (resolved with user; supersedes the deep-skirt preservation for `< -2.5 m`).
- **Both elevation RTTs clear to `mfSeaFloorElevation`** (terrain `RenderTargetTextures.cpp:342`, shadow `:85`),
  so `kMax` is safe on both: single-island pixels unchanged (`max(seafloor, value) == value`).
- **Determinism**: only `GlobalElevation` is shared; MAX is commutative → order-independent; not in the CRC;
  no `kiNavDataVersion` bump. All GPU/uniform/shader edits are client-only and never feed the shared CRC.
- **Client/server**: `GlobalElevation` compiles unchanged on both with no `#ifdef`; Graphics edits are
  client-only by location. **Memory/threading**: `GlobalElevation` stays read-only, non-allocating, no new
  shared state. **Build wiring**: no new files, no vcxproj/filter changes.
- **GPU↔CPU consistency**: seeding the CPU max at `mfSeaFloorElevation` matches `TerrainElevation.frag`'s own
  clamp (a sample deeper than the floor clamps to `fSeaFloorElevation`), so it is not a behavior regression.

---

## Implementation

### Part A — MAX blend on both elevation prepasses (client GPU)
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`:
  - `kPipelineTerrainElevation` (`:475`): add `kMax` to `.flags` (`{kRenderTarget, kPushConstants, kMax, kUpdateAfterBind}`).
  - `kPipelineShadowElevation` (`:215`): add `kMax` likewise, so shadow-cast heights match terrain in overlaps.
- `kMax` is already wired to `VK_BLEND_OP_MAX` (`Pipeline.h:70`, `PipelineCreator.cpp:499-501`); for MIN/MAX
  the Vulkan blend factors are ignored, giving exactly `max(src, dst)`. Clear value is the most-negative
  `mfSeaFloorElevation`, so MAX keeps the highest land per pixel; single-island pixels are unchanged
  (`max(seafloor, value) == value`). The per-island curved value from `TerrainElevation.frag` is monotonic,
  so MAX of curved values is order-consistent.

### Part B — Bind per-island heightmap array to the terrain pipeline + Terrain.vert sink (client GPU)
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`, `kPipelineTerrain` descriptor list (after the
  masks array at `:372`): append the per-island heightmap bindless array, mirroring the masks entry:
  `{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands,
  .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}` → set=1 **binding 21**.
  `kSamplerElevation` matches the per-slot `RegisterTextureBinding` flag used for the elevation array on the
  prepasses, so descriptor writes line up.
- `Engine/Data/Shaders/Terrain/Terrain.vert`:
  - Add `#extension GL_EXT_nonuniform_qualifier : require` (currently only Terrain.frag has it).
  - Declare `layout (set = 1, binding = 21) uniform sampler2D ownHeightmapSamplers[kiMaxIslands];`.
  - After `fVertexZ` is sampled from the composite (`:103`), sample this island's **own** heightmap at the
    island-local UV already computed for `f2OutIslandTexcoord`, and sink:
    ```glsl
    float fOwnElevation = textureLod(ownHeightmapSamplers[nonuniformEXT(uiOutTextureSlot)], f2OutIslandTexcoord, 0.0f).x;
    if (fOwnElevation < globalLayout.fUnderwaterMaskThreshold)
        fVertexZ = globalLayout.fSeaFloorElevation;
    ```
    Own heightmap stores **raw** meters relative to beach (no undersea curve), so compare the raw sample to
    the raw threshold. Land and the shallow `-2.5..0 m` band keep the composite Z (water-shoreline alignment
    preserved); only the deep band sinks.
- Per-slot registration is automatic: `IslandTerrain::AcquireTextureSlot` already `Register`s
  `mElevationTextures.data()` for **all** consumers of that array pointer, and `RestorationSweep`'s
  `UpdateArrayBindingsForKey` patches them. Adding `kPipelineTerrain` as a third consumer is picked up by
  the same machinery. **Verify the eviction symmetry path** rewrites the new binding-21 slot back to the
  slot-0 placeholder on evict (it iterates consumers per array pointer; confirm binding 21 is covered).

### Part C — Expose the zero-out threshold to the shader (client GPU, DRY)
- `Engine/Data/Shaders/ShaderLayoutsBase.h`: add `float fUnderwaterMaskThreshold INIT;` to `GlobalLayout`
  (append after `fDebugTerrainElevationHigh`, `:422`).
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (beside `:558`):
  `rGlobalLayout.fUnderwaterMaskThreshold = common::kfUnderwaterMaskThresholdMeters * kfMetersToUnits;`
  (single source of truth = `Common/DataFile.h:22`; `kfMetersToUnits == 1.0f` today).

### Part D — `GlobalElevation` MAX over all islands (shared CPU)
- `Engine/Source/Frame/IslandTerrain.cpp:212-269`: replace the first-match early `return` (`:265`) with a
  running max. Seed `float fMaxElevation = mfSeaFloorElevation;`, keep the footprint-rectangle `continue`
  test, and inside the loop `fMaxElevation = std::max(fMaxElevation, rTemplate.mpfHeightmapData[...]);`;
  return `fMaxElevation` after the loop (covers the no-containing-island case, since the seed equals the
  open-ocean fallback). Order-independent and deterministic. `GlobalNormal` inherits the fix unchanged.

---

## Critical files
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `kMax` on both elevation prepasses; heightmap array on `kPipelineTerrain`.
- `Engine/Data/Shaders/Terrain/Terrain.vert` — nonuniform ext, binding-21 array, sub-threshold sink.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `fUnderwaterMaskThreshold` field.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — populate the threshold from `common::kfUnderwaterMaskThresholdMeters`.
- `Engine/Source/Frame/IslandTerrain.cpp` — `GlobalElevation` first-match → max.

## Risks / notes
- **R16_SFLOAT color-blend support**: `VK_BLEND_OP_MAX` on `mTerrainElevationTexture` / `mShadowElevationTexture`
  needs `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` for `keElevationFormat` (= R16_SFLOAT; R32_SFLOAT is only
  the *sampled* per-island source, never blended). Near-universal on desktop GPUs; confirm via validation layers
  at run. Audit confirmed this is currently UNGUARDED — routed to a follow-up plan
  (`Documents/Plans/Graphics/ElevationMaxBlendFormatGuard.md`).
- **Eviction symmetry** (Part B): the elevation array is template-owned (no `mTextureMap` entry) and patched
  in `RestorationSweep`; ensure adding `kPipelineTerrain` as a consumer keeps both restoration and eviction
  rewrites correct for binding 21 — call out for review.
- All shader/pipeline/uniform edits are client-only; no `BT_SERVER` impact except the shared `GlobalElevation`.
- **Deep-skirt drop-off (accepted)**: sinking all sub-threshold verts turns every island's `< -2.5 m` skirt into
  a cliff down to the flat floor, which may read as a subtle drop-off ring through shallow water. Accepted per
  the locked sink-scope decision; if disliked, it's a tuning follow-up (deepen the threshold or go surgical).
