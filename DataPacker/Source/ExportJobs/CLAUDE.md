# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw files into cached binary chunks. One subclass per asset type: Texture, Shader, Scene, Island, Model, Audio, Font, Raw. Each is matched and driven by the parent's `RunExportJobs<T>` template (see [../CLAUDE.md](../CLAUDE.md)).

## ExportJob Pipeline

Abstract base owns dirty-checking and cache I/O; derived classes only implement `Export()` (and sometimes `CheckDirty()` / `CleanupOnFailure()`).

- **Cache layout**: `.chunk` = magic + version + `ChunkHeader` + aligned data. A sibling `.txt` stores the input's `last_write_time` so `CheckDirty` survives restores that preserve filesystem timestamps.
- **Version convention**: `GetVersion()` returns `ExportJob::Version(N)` (folds in `sizeof(common::ChunkHeader)`) so header-layout changes auto-invalidate caches. `ExportShader` passes `Version(14 + VK_HEADER_VERSION)` to force re-export on SDK upgrades.
- **Allocation contract**: each `Export()` calls `AllocateHeaderAndData` exactly once; the base populates magic/crc/flags/path afterward.
- **Clean path**: `RunExport()` skips `Export()` and streams cached bytes back when not dirty.
- **Per-job workbuffer**: `RunExport()` constructs a `common::ThreadLocal` — jobs run on worker threads with isolated `gpThreadLocal->mWorkbuffer`.
- **Intermediate cleanup**: jobs that produce sidecar files track them and override `CleanupOnFailure()` to unlink on throw.

## Handles() and ChunkFlags

Static `Handles(directory_entry) -> optional<ChunkFlags_t>` decides whether a processor claims a path and tags the resulting chunk (stage bits for shaders, cubemap bit for `[C]` textures, raw-passthrough for files under `Raw/`).

## Island Chunk Ingest

The Gaea bake, archetype patching, crop/downsample, route split, and two-stage `BakeVersion.txt` / `SplitVersion.txt` dirty sentinels all live in the parent's `BakeIslandIntermediates.cpp` / `GaeaArchetype.cpp` — see [../CLAUDE.md](../CLAUDE.md). `ExportIsland` only ingests each baked chunk leaf (`<Islands>/<island>/<route>/<index>/`) into one independent `kIsland` chunk. `Handles()` claims a directory carrying `Intermediates/BakedDimensions.json` under an `Islands` ancestor — exactly the leaves the bake produced (sparse indices are normal; too-low leaves are deleted at split time). A leaf's `BakedDimensions.json` drives every downstream size: anisotropic post-crop world dimensions, the source crop rect, and a leaf-relative `textureSourceDir` pointing at the route's shared full-res Gaea sources.

Per leaf, `Export()` emits four standalone BC texture chunks (each with its own CRC) plus one `kIsland` chunk:
- **Color** (PNG8 sRGB) → BC7, **Normals** (EXR linear) → BC5, **AmbientOcclusion** (`.r16`) → BC4, and a packed **material mask** BC7 RGBA (R=rock, G=sand, B=snow, A=flow) built from four grayscale PNGs. Color/normals/masks are cropped in-memory to this leaf's rect; the mask is additionally 4×-downsized to match the heightmap footprint. A JPEG sidecar is written next to each for visual diagnosis. `Texture::sEncodeMutex` serializes the BC encoder across textures.
- **Underwater flattening**: `Texture::MaskByHeightmap` runs *before* `MakeMipmaps` on each texture, overwriting texels below `common::kfUnderwaterMaskThresholdMeters` with a per-format flat value (color RGB 0 with alpha pinned 255 so BC7 keeps no-alpha mode; normals → tangent (0,0,1); AO/masks → 0). Flattening pre-mip lets the constant runs propagate down every mip for free, maximizing RDO + zlib compression. Camera-visible shallow water above the threshold is preserved for clean beaches.
- **`kIsland` payload**: `[heightmap floats][mesh XY pairs][mesh indices][valid-area hull]`. The downsampled `R32_SFLOAT` meter heightmap is read once and reused for both the masks and the payload (no separate elevation chunk). The mesh (`MeshProcessed.bin`, float3 XYZ) is stripped to float2 XY — `Terrain.vert` re-derives Z from the elevation sampler. `BuildValidAreaHull` produces the CCW convex hull (Andrew's monotone chain, O(H) via per-row extremes) of pixels at or above the same threshold, in island-local meters; producer-side asserts verify CCW + convexity since the runtime SAT (`common::ConvexHullsOverlap`) requires it. Consumed client-only by debug render and overlap tests (see [Engine/Source/Frame/CLAUDE.md](../../../Engine/Source/Frame/CLAUDE.md)).

`CheckDirty` extends the base mtime check: a leaf directory's mtime doesn't propagate from edits to files inside it or to the route's shared textures, so it re-exports when any regular file in the leaf's own `Intermediates/` or the route's `Intermediates/` (one level up) is newer than the chunk.

## Scene Two-Phase

A `.PreExport` marker carries its own version; a stale marker forces re-running `PreExport` even when the main chunk is clean. `PreExport` emits `.MODEL` geometry and per-texture block-compressed intermediates routed by material usage (BC4 occlusion, BC5 normals, BC7 otherwise); `MainExport` assembles the scene chunk and optional animation section. Primitives from different mesh nodes sharing one glTF material split into distinct material entries while preserving the source index for texture lookup.

## IBL Cubemaps

`GenerateIrradianceCubemaps` / `GeneratePreFilteredCubemaps` are free functions (not `ExportJob`s) run as a pre-pass over `[C]`-tagged `.ktx` files and `[C]` face-image directories, doing their own timestamp-vs-source dirty checks via cmft. Pre-filtered radiance is written **face-major / mip-minor** to match `TextureUploadManager`'s iteration order. The `.R16G16B16A16_SFLOAT` outputs are later picked up by `ExportTexture`'s raw path.

## Texture Routing

`ExportTexture::Export` first resolves a `VkFormat` from filename tags (`[BC4]`, `[BC5]`, `[BC7]`, `[C]` cubemap) and explicit format extensions, then dispatches to one of four paths:
- **KTX cubemap** (`.ktx`): gli-loaded, zlib-compressed, kept as `R16G16B16A16_SFLOAT`.
- **Raw passthrough** (BCn / R16 / R32_SFLOAT / R16G16B16A16_SFLOAT intermediates): bytes copied straight through with no mip generation. BCn/R16 intermediates from `Texture::Save` are already zlib streams; the IBL `.R16G16B16A16_SFLOAT` (6 packed faces) is the one raw input compressed here, sized from its on-disk payload rather than 2D mip math.
- **Live cubemap** (`[C]` directory of 6 face PNGs/JPGs): each face encoded, concatenated, zlib-compressed.
- **Regular texture** (PNG/TGA/JPG): full mip chain then zlib. Font-atlas detection strips `[...]` prefixes and cross-references `Fonts/**/*.fnt`; atlas matches use a box-filter chain instead of the default linear one.

All non-passthrough paths set `kZlibCompressed`; `ChunkHeader` carries both the compressed-on-disk and uncompressed sizes, and the runtime `FileManager` decompresses at chunk-load (see [Engine/Source/File/CLAUDE.md](../../../Engine/Source/File/CLAUDE.md)).

## Texture Intermediate Format

The on-disk intermediate the raw-passthrough reader ingests starts with an optional 8-byte magic sentinel (`kiTextureIntermediateMagic`) then 3× `int64_t` width/height/mipcount and a zlib stream of concatenated mips. The magic distinguishes current `Texture::Save` output from legacy (pre-magic / pre-zlib) files; the reader tolerates both shapes. Producing `Texture::Save`, the BC7 RDO knobs, and the startup `MigrateLegacyIntermediates` upgrade pass all live in the parent (see [../CLAUDE.md](../CLAUDE.md)); IBL `.R16G16B16A16_SFLOAT` outputs stay in the legacy pre-magic shape since their writer is the separate cubemap path above.

## Shader Dirty Tracking

`ExportShader::CheckDirty` parses Makefile-style `.d` depfiles from `glslc -MD` and re-exports when any transitively `#include`'d header is newer than the cached chunk.

## See Also
- [../CLAUDE.md](../CLAUDE.md) — DataPacker orchestration, output structure, and the shared `RunExportJobs<T>` template.
