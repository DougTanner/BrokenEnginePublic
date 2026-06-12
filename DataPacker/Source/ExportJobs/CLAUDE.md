# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw files into cached binary chunks. One subclass per asset type: Texture, Shader, Scene, Island, Model, Audio, Font, Raw. Each is matched and driven by the parent's `RunExportJobs<T>` template (see [../CLAUDE.md](../CLAUDE.md)).

## ExportJob Pipeline

Abstract base owns dirty-checking and cache I/O; derived classes only implement `Export()` (and sometimes `CheckDirty()` / `CleanupOnFailure()`).

- **Cache layout**: `.chunk` = magic + version + `ChunkHeader` + aligned data. A sibling `.txt` stores the input's `last_write_time` so `CheckDirty` survives restores that preserve filesystem timestamps.
- **Version convention**: `GetVersion()` returns `ExportJob::Version(N)` (folds in `sizeof(common::ChunkHeader)`) so header-layout changes auto-invalidate caches. Scene/Model/Font also fold in the `sizeof` of each `common::` payload struct they serialize, so payload size changes auto-dirty caches; same-size reorders still need the raw `N` bumped by hand (the `DataFile.h` static_assert beside each struct names the owning job). `ExportShader` passes `Version(14 + VK_HEADER_VERSION)` to force re-export on SDK upgrades.
- **Allocation contract**: each `Export()` calls `AllocateHeaderAndData` exactly once; the base populates magic/crc/flags/path afterward. One exception: the scene animation section is appended by growing `mHeaderAndData` after the allocate, so the scene chunk's `iSize` excludes it and the returned header pointer must not be dereferenced after the resize (it can reallocate).
- **Clean path**: `RunExport()` skips `Export()` and streams cached bytes back when not dirty.
- **Per-job workbuffer**: `RunExport()` constructs a `common::ThreadLocal` — jobs run on worker threads with isolated `gpThreadLocal->mWorkbuffer`.
- **Intermediate cleanup**: jobs that produce sidecar files track them and override `CleanupOnFailure()` to unlink on throw.

## Handles() and ChunkFlags

The flags `Handles()` returns tag the chunk (cubemap bit for `[C]` textures, `kRaw` for files under a `Raw/` path component); processors may add flags after matching (shader stage bits in `ExportShader`'s constructor, `kZlibCompressed` during texture export). `ExportScene` claims `.gltf` but skips paths containing an `Intermediates` component — Gaea's Mesher emits `Mesh.gltf` bake artifacts there that must not become standalone scenes.

## Island Chunk Ingest

The Gaea bake, archetype patching, crop/downsample, and route split all live in the `Island/` bake TUs — orchestration documented in [../CLAUDE.md](../CLAUDE.md). The two-stage `BakeVersion.txt` / `SplitVersion.txt` dirty sentinels gate bake/split re-runs per route; leaf-level completion proof and rejection are covered below. `ExportIsland` only ingests each baked chunk leaf (`<Islands>/<island>/<route>/<index>/`) into one independent `kIsland` chunk. `Handles()` claims a directory carrying `Intermediates/BakedDimensions.json` under an `Islands` ancestor — that file is written last per leaf, so its presence proves the bake completed (sparse indices are normal; too-low leaves are deleted at split time). `BakedDimensions.json` drives every downstream size: anisotropic post-crop world dimensions, the source crop rect, and a leaf-relative `textureSourceDir` pointing at the route's shared full-res Gaea sources.

Per leaf, `Export()` emits four BC texture intermediates — each later chunked independently via ExportTexture's raw path and referenced from the `kIsland` chunk by CRC — plus the `kIsland` chunk itself:
- **Color** (sRGB) → BC7, **Normals** (EXR) → BC5, **AmbientOcclusion** (`.r16`) → BC4, and a packed **material mask** BC7 RGBA (R=rock, G=sand, B=snow, A=flow) built from four grayscale PNGs, 4×-downsized to match the heightmap footprint.
- **Underwater flattening**: `Texture::MaskByHeightmap` runs *before* `MakeMipmaps`, overwriting texels below `common::kfUnderwaterMaskThresholdMeters` with per-format flat values (color RGB 0 with alpha pinned 255 so BC7 keeps no-alpha mode; normals → tangent (0,0,1); AO/masks → 0). Flattening pre-mip lets the constant runs propagate down every mip, maximizing RDO + zlib compression while preserving camera-visible shallow water for clean beaches.
- **`kIsland` payload**: `[heightmap floats][mesh XY pairs][mesh indices][valid-area hull]`. The downsampled `R32_SFLOAT` meter heightmap is read once and reused for the masks and the payload (no separate elevation chunk). The mesh is stripped to float2 XY — `Terrain.vert` re-derives Z from the elevation sampler. `BuildValidAreaHull` produces the CCW convex hull (Andrew's monotone chain, O(H) via per-row extremes) of above-threshold pixels in island-local meters; producer-side asserts verify CCW + convexity since the runtime SAT (`common::ConvexHullsOverlap`) requires both. Consumed by `IslandChainPlacement` hull-overlap packing on both client and server, plus client-only debug render (see [Engine/Source/Frame/CLAUDE.md](../../../Engine/Source/Frame/CLAUDE.md)).

`CheckDirty` extends the base mtime check: a leaf directory's mtime doesn't propagate from edits to files inside it or to the route's shared textures, so it also re-exports when any file in the leaf's own `Intermediates/` or the route's `Intermediates/` (one level up) is newer than the chunk.

## Scene Two-Phase

A `.PreExport` marker stamped with the job's version forces re-running `PreExport` when stale, even when the main chunk is clean. `PreExport` emits `.MODEL` geometry and per-texture block-compressed intermediates routed by material usage (BC4 occlusion, BC5 normals, BC7 otherwise) — both later chunked independently by `ExportModel` and ExportTexture's raw path and referenced from the scene chunk by relative-path CRC. `MainExport` assembles the scene chunk and optional animation section. Primitives from different mesh nodes sharing one glTF material split into distinct material entries while preserving the source index for texture lookup.

## IBL Cubemaps

`GenerateIrradianceCubemaps` / `GeneratePreFilteredCubemaps` are free functions (not `ExportJob`s) run as a pre-pass over `[C]`-tagged `.ktx` files and `[C]` face-image directories, doing their own timestamp-vs-source dirty checks via cmft. Pre-filtered radiance is written **face-major / mip-minor** to match `TextureUploadManager`'s iteration order. The `.R16G16B16A16_SFLOAT` outputs are later picked up by ExportTexture's raw path.

## Texture Routing

`ExportTexture::Export` resolves a `VkFormat` from filename tags (`[BC4]`, `[BC5]`, `[BC7]`, `[C]` cubemap) and explicit format extensions, then dispatches to one of four paths:
- **KTX cubemap** (`.ktx`): gli-loaded, zlib-compressed, kept as `R16G16B16A16_SFLOAT`.
- **Raw passthrough** (BCn / R16 / R16G16B16A16_SFLOAT intermediates): bytes copied through with no mip generation. BCn/R16 intermediates from `Texture::Save` are already zlib streams; the IBL `.R16G16B16A16_SFLOAT` (6 packed faces) is the one raw input compressed here, sized from its on-disk payload rather than 2D mip math.
- **Live cubemap** (`[C]` directory of 6 face PNGs/JPGs): each face encoded, concatenated, zlib-compressed.
- **Regular texture** (PNG/TGA/JPG): full mip chain then zlib. Font-atlas detection strips `[...]` prefixes and cross-references `Fonts/**/*.fnt`; atlas matches use a box-filter mip chain instead of the default linear one.

All four paths set `kZlibCompressed` (the passthrough's BCn/R16 bytes are already zlib streams); `ChunkHeader` carries both compressed-on-disk and uncompressed sizes, and the runtime `FileManager` decompresses at chunk load (see [Engine/Source/File/CLAUDE.md](../../../Engine/Source/File/CLAUDE.md)). The raw-passthrough intermediate starts with an optional 8-byte `kiTextureIntermediateMagic`, then 3× `int64_t` width/height/mipcount, then the payload; the reader tolerates legacy pre-magic files (IBL outputs stay legacy-shaped since their writer is the cubemap pre-pass above). The producing `Texture::Save` and the startup legacy-migration pass live in `Texture/` (documented in [../CLAUDE.md](../CLAUDE.md)).

## Shader Dirty Tracking

`ExportShader::CheckDirty` parses Makefile-style `.d` depfiles from `glslc -MD` and re-exports when any transitively `#include`'d header is newer than the cached chunk.

## See Also
- [../CLAUDE.md](../CLAUDE.md) — DataPacker orchestration, output structure, and the shared `RunExportJobs<T>` template.
