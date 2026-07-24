# Island Export Pipeline

Gaea route baking, archetype patching, region splitting, and island intermediate generation. The parent ExportJobs hub (`../AGENTS.md`) owns generic cache and chunk rules.

## Cache Lifecycle

Route-level raw Gaea output and leaf geometry live under `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/<project>/Gaea/Islands/`. `BakeVersion.meta` fingerprints the island configuration, resolved terrain, route identity, and bake contract. `SplitVersion.meta` separately fingerprints region splitting, so a split-only change reuses the expensive raw bake.

`BakedDimensions.json` is written last in each accepted leaf and is the completion sentinel consumed by `ExportIsland::Handles()`. Rejected or incomplete leaves must not retain it. When route subdivision or pruning changes, remove stale higher-index leaf directories so their old sentinels cannot produce chunks.

With `BT_DATAPACKER_FORBID_EXPENSIVE_EXPORT=1`, a dirty route fails before launching Gaea. Clean route caches remain usable.

## Island Outputs

Each complete leaf produces a `kIsland` chunk plus independently routed BC texture intermediates for color, normals, ambient occlusion, and the RGBA material mask. The mask channels are rock, sand, snow, and flow. Texture filenames and formats are producer/consumer contracts; update the runtime shader and upload expectations with any change.

Masking occurs before mip generation. Underwater texels use format-specific flat values so constant regions survive through mipmaps and compression while the above-threshold shoreline remains available to rendering and placement.

The island payload stores the quantized elevation field, XY mesh data, indices, and valid-area hull. Runtime terrain reconstructs Z from elevation. The hull must remain convex and counter-clockwise because deterministic client/server island placement consumes it through `common::ConvexHullsOverlap`; producer checks enforce those properties before serialization.

Large shared route inputs use persistent fingerprints rather than repeated content reads. JPEG diagnostics belong only in the parallel `%LOCALAPPDATA%/BrokenEngine/DataPackerCache/.../Gaea/Diagnostics/` tree, never the source checkout.

## Versioning

Four stages are versioned and cached independently, so bump only the one whose behavior changed. The bake version covers raw Gaea output; the split version covers post-bake crop, split, leaf-output, or completion rules. `ExportIsland::kiTextureVersion` covers the BC texture encode alone, and `ExportIsland::GetVersion` covers the chunk payload — quantized elevation, mesh, indices, hull, and `IslandHeader` layout — which may also require the shared data-format version.

The BC outputs are tracked in the checkout, which shapes the texture stage: its version stays a plain `int64_t` outside `ExportJob::Version(...)`, so a chunk-header layout change cannot rewrite hundreds of megabytes of tracked textures for byte-identical output. Its marker fingerprints only the encode's own inputs; the processed mesh is deliberately excluded so a mesh-only rebake re-packs the chunk without touching the textures.

## See Also

- `../Texture/AGENTS.md` - texture intermediate encoding
- `../../../../Common/Math/AGENTS.md` - deterministic convex-hull contract
- `../../../../Engine/Source/Frame/AGENTS.md` - runtime island placement ownership
