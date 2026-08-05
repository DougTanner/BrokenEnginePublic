# Island Export Pipeline

Gaea route baking, archetype patching, region splitting, and island intermediate generation. An archetype is a reusable Gaea terrain template that an island export starts from. The parent ExportJobs hub owns generic cache and chunk rules.

## Cache Lifecycle

Route-level raw Gaea output and leaf geometry live under `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>/Gaea/Islands/`. `BakeVersion.meta` fingerprints the island configuration, resolved terrain, route identity, and the agreed inputs, outputs, and ordering of the bake step. `SplitVersion.meta` separately fingerprints region splitting, so a split-only change reuses the expensive raw bake.

`BakedDimensions.json` is written last in each accepted leaf and is the completion marker consumed by `ExportIsland::Handles()`. Rejected or incomplete leaves must not retain it. `BakeRoute` deletes stale higher-index leaf directories itself, unconditionally and before its dirty early-return, so shrinking a route's subdivision count cannot leave old markers producing chunks; kept leaves and non-numeric route metadata are untouched. Do not add a second, manual prune.

Splitting also densifies the mesh: after the raw Gaea Mesher output is loaded, `SubdivideBeachBand` recursively splits triangles whose height range overlaps the beach band — the narrow depth window straddling engine Z 0, tuned by the `kfBeachSubdivision*` constants in `BakeRoute.cpp` — until their longest horizontal edge falls under the target. Neighbors outside the band take the smallest split that absorbs an inherited midpoint without creating new ones, so the cascade stops one ring outside the band. It runs post-bake on the full mesh and the chunk crops reuse the result, so a change here bumps the split version, not the bake version.

Each accepted leaf's downsampled elevation is edge-tapered before it is written: a band in from every leaf border — interior split cut lines included — ramps water at or below halfway depth down to the per-island sea floor, so every leaf blends with the engine's constant open-ocean elevation clear, while shallower water is preserved almost unchanged so the taper cannot amputate a leaf's visible sand apron. It is a split-stage step in `ProcessBakedRegion`, so a change here bumps the split version.

With `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1`, a dirty route fails before launching Gaea. Clean route caches remain usable.

## Island Outputs

Each complete leaf produces a `kIsland` chunk plus independently routed BC texture intermediates for color, normals, ambient occlusion, and the RGBA material mask. The mask channels are rock, sand, snow, and flow. Texture filenames and formats are producer/consumer contracts; update the runtime shader and upload expectations with any change.

Masking occurs before mip generation. Underwater texels use format-specific flat values so constant regions survive through mipmaps and compression, while shoreline above `common::kfUnderwaterMaskThresholdMeters` remains available to rendering and placement. That one shared constant is the whole cut line: DataPacker bakes both the texture mask and the valid-area hull from it, and the engine publishes the same value to the shaders and to the debug hull draw. Never introduce a second local threshold — nothing would catch baked textures drifting out of step with the shader.

The island payload stores the quantized elevation field, XY mesh data, indices, and valid-area hull. Runtime terrain reconstructs Z from elevation. The hull must remain convex and counter-clockwise because deterministic client/server island placement consumes it through `common::ConvexHullsOverlap`; producer checks enforce those properties before serialization.

Large shared route inputs use persistent fingerprints rather than repeated content reads. JPEG diagnostics belong only in the parallel `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/.../Gaea/Diagnostics/` tree, never the source checkout.

## Versioning

Four stages are versioned and cached independently, so bump only the one whose behavior changed. The bake version covers raw Gaea output; the split version covers post-bake crop, split, leaf-output, or completion rules. `ExportIsland::kiTextureVersion` covers the BC texture encode alone, and `ExportIsland::GetVersion` covers the chunk payload — quantized elevation, mesh, indices, hull, and `IslandHeader` layout — which may also require the shared data-format version.

The BC outputs are tracked in the checkout, which shapes the texture stage: its version stays a plain `int64_t` outside `ExportJob::Version(...)`, so a chunk-header layout change cannot rewrite hundreds of megabytes of tracked textures for byte-identical output. Its marker fingerprints only the encode's own inputs; the processed mesh is deliberately excluded so a mesh-only rebake re-packs the chunk without touching the textures.

## See Also

- `../Texture/AGENTS.md` - texture intermediate encoding
- `../../../../Common/Math/AGENTS.md` - deterministic convex-hull contract
- `../../../../Engine/Source/Frame/AGENTS.md` - runtime island placement ownership
