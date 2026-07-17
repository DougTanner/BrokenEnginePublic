# Terrain Instance Area Cull (visible-first SSBO packing)

## Context

`kGpuTimerTerrain` measures **998 µs current / 987 avg / 1027 max** at 120 fps, Profile build, ~6.6 ms total GPU/frame — third-biggest GPU cost. Caveat: **idle scene near the spawn islands**; this plan's win scales with how much of the subscribed ring sits off-screen, so it is largest exactly in that idle/zoomed-in case and shrinks when zoomed out.

Every island placement in every subscribed coord is drawn by the terrain mesh pass. `game::Game` (the `UpdateActiveIslands` call site in `Game.cpp`) filters coords only by `iConfirmedTick >= 0`; `Islands::UpdateActiveIslands` (`Engine/Source/Graphics/Islands.cpp`) then writes an `AxisAlignedQuadLayout` SSBO slot + `instanceCount` for **every** placement — no spatial test. At gameplay zoom the visible area covers a fraction of the ~9-cell subscribed ring (a cell can place ~100 islands per `Islands.h`'s `kiMaxPlacementsPerTemplate` rationale), so most instances run full vertex shading — with **two `textureLod` fetches per vertex** on meshes of 10⁵-10⁶ triangles (`IslandResidentMemoryScaling_Overview.md`) — only to be frustum-clipped.

The instances cannot simply be dropped: the elevation and shadow-elevation prepasses draw the full fixed slot count from the same SSBO (zero-width-quad culled), and off-screen islands legitimately contribute to the shadow-area composite (`mShadowElevationTexture` → `Shadow.comp` ray-march: an off-screen mountain casts onto visible water/terrain) and the lighting/smoke areas. Only the **mesh pass** (this hotspot) draws nothing useful for instances outside the camera frustum. The pass's indirect draws read dense per-template SSBO ranges `[firstInstance, firstInstance + instanceCount)`, which permits a two-tier split with zero new buffers:

## Design

Two-tier cull inside `Islands::UpdateActiveIslands`, using the existing `CameraBase::InVisibleArea` point-in-rect helper with per-side adjust margins (`CameraBase.h:47`):

- **Tier 1 — SSBO population cull (prepass-safe):** skip placements whose footprint bounding circle (radius `0.5f * hypot(mfQuadFootprintX, mfQuadFootprintY)` — rotation-safe) misses the **widest consuming area**: the union of the camera visible area (`f4RenderVisibleArea`), shadow area, lighting area, and smoke area (all camera-centered rectangles; the shadow/lighting areas are the 1.5x-headroom world-sized-texel rects from `GlobalUniforms`/`LightingUniforms` — read the previous frame's latched values, which is safe because they are rate-limited/latched and the margin below covers drift). Skipped placements produce no SSBO write and no `instanceCount` — they vanish from mesh pass *and* prepasses, which is correct because they were outside every consuming RTT's world rectangle.
- **Tier 2 — mesh-pass ordering:** among surviving placements, write each template's range **visible-first**: placements intersecting `f4RenderVisibleArea` (+ margin) first, then area-only (shadow/lighting/smoke contributors) after. Set the indirect `instanceCount` to the visible count only; record the *total* written count in `mLastWrittenCounts` (the stale-slot clear must cover both groups). The prepasses draw the full fixed slot count and therefore still consume the area-only slots; the mesh pass draws only the visible prefix. One extra per-template counter in the existing workbuffer scratch.
- **Margin:** the visible-area test must be conservative against tall terrain leaning into frame under perspective — a peak of height `h` at eye height `E` shifts on-screen by up to `~(h/E) * edgeDistance`. Expose a single `gIslandCullMargin` Wrapper (meters, added to all four `InVisibleArea` adjusts), default generous (e.g. `IslandHeader::fMaxHeightMeters`-scale, ~512 m; grill), max large enough to act as an effective disable. `FrameStaticData` placements are deterministic and static per cell, but the cull is recomputed per frame anyway (it already rewrites the SSBO per frame), so camera motion needs no extra invalidation.

**Visual impact: (a) no visual change** with the default margin — culled instances were outside the frustum (tier 2) or outside every consuming RTT rectangle (tier 1). Misconfigured (too-small) margin degrades to (c): islands/shadows popping at screen edges — which is why the margin is a slider with a conservative default rather than a constant. Slider: `gIslandCullMargin`, suggested range 0-4096 m, default ~512 m.

**Expected saving:** proportional to the culled fraction of mesh-pass vertex work. Idle at spawn with a ~9-cell ring and a sub-cell visible area, the visible prefix is a small fraction of total instances — estimated **150-400 µs** off `kGpuTimerTerrain` (bounded by the pass's geometry-bound share; the fragment share is already screen-limited). Also trims the per-frame SSBO memsets/writes and prepass vertex work as a side effect.

## Critical files

- `Engine/Source/Graphics/Islands.cpp` / `Islands.h` — `UpdateActiveIslands` two-tier packing; `mLastWrittenCounts` covers total (visible + area-only) counts; comment updates.
- `Engine/Source/Graphics/CameraBase.h` — `InVisibleArea` (consumed, not modified); `f4RenderVisibleArea`.
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` / `LightingUniforms.cpp` — source of the latched shadow/lighting area rectangles (`f4ShadowArea`/`f4LightingArea` equivalents); expose the latched values for the union rect (read-only accessors or cached copies on `gpGraphics` — pick the smallest seam).
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTemplate::mfQuadFootprintX/Y` (bounding-circle radius inputs).
- `Engine/Source/Ui/TerrainWrappersBase.{h,cpp}` + Terrain Tweaks section — `gIslandCullMargin` (`TweaksSliderMap` pattern).
- `Projects/BrokenEngineSandbox/Source/Game.cpp` — `UpdateActiveIslands` call site (unchanged; cited for the subscribed-coords contract).

## Out of scope

- Culling the elevation/shadow prepass draw count itself (fixed slot count, zero-width-quad culled — cheap at 34 µs; not worth touching).
- Mesh LOD (`TerrainMeshLodChain.md`) and fragment sample reduction (`TerrainDetailSampleTrim.md`).
- Per-template `miRefCount`/LRU semantics: the residency ref count must keep counting **all** placements in subscribed coords (tier 1 skips must still ref-count their template, or eviction churn changes) — preserve the existing loop's ref-count/`muiLastUsedRenderFrame` writes for every placement, culled or not.
- Any change to `FrameStaticData`, placements, or the deterministic island-chain generation (sim-side; CRC exposure).

## Acceptance criteria

- With default margin: no visible difference panning/zooming at any height (including shadows from off-screen terrain), and `kGpuTimerTerrain` drops in the idle-at-spawn scene.
- Margin maxed out reproduces today's draw counts exactly (effective disable).

## Notes

- **Invariant exposure:** client/graphics-only. No `.pack`/`kiVersion`, no shader change (no repack), no CRC/wire/determinism — placements are read, never written; ref-counting behavior preserved per Out of scope. Allocation-tracked path: the extra counter uses the existing `gpThreadLocal->mWorkbuffer` scratch, no heap.
- **Sequencing:** shares `Islands.cpp`/`Islands.h` with `TerrainMeshLodChain.md` (co-schedule; the per-frame indirect writes compose) and with `IslandPlacementSsboResidency.md` (SSBO arena decision — land this first or refresh its citations; tier-2 packing is compatible with its option A dense arena). The shadow/lighting-area accessor seam now starts at `ComputeWorldSizedTexelArea` in `Engine/Source/Graphics/Render/Render.h`; reuse that result rather than duplicating the area calculation.
- **Grill decisions:** margin default value; whether the union rect reads previous-frame latched areas (recommended — simplest, drift covered by margin) or recomputes areas pre-`UpdateActiveIslands`.
