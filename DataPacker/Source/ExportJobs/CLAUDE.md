# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw files into cached binary chunks.

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

## Two-Phase Pipeline

Scene and Islands run a pre-export phase emitting intermediates consumed by the main phase:

- **Islands**: source `.exr`/`.r32` are consumed and deleted, leaving block-compressed / `R16_UNORM` files that `ExportTexture` picks up as passthrough. The island chunk stores CRCs to those files plus a mode-of-nonzero beach elevation and a float-downsampled CPU heightmap. Intermediates may be hand-authored, or baked upstream by the Gaea pre-pass (see `../CLAUDE.md`) from a sibling `island.json` + resolved `.terrain` archetype. `Handles()` ignores island folders until an elevation intermediate exists, so a folder containing only `island.json` is a no-op until the bake produces one. Pre-bake island normal intermediates predating BC5 are auto-migrated on first run.
- **Scene**: a `.PreExport` marker carries its own version; stale marker forces re-running `PreExport` even when the main chunk is clean. Emits `.MODEL` geometry and per-texture block-compressed intermediates routed three ways by material usage: BC4 for occlusion, BC5 for normals, BC7 otherwise. Primitives from different mesh nodes sharing one glTF material split into distinct material entries while preserving the source index for texture lookup.
- **IBL cubemaps**: do their own timestamp-vs-source dirty checks. Pre-filtered radiance data is written **face-major / mip-minor** to match `TextureUploadManager`'s iteration order.

## Texture Routing

`ExportTexture` routes on filename tags (`[C]`, `[BC4]`, `[BC5]`, `[BC7]`) and explicit format extensions (passthrough, no mip generation). Font-atlas detection strips `[...]` prefixes and cross-references `Fonts/**/*.fnt`; matches use a box-filter mipmap chain.

## Texture Chunk Format

Texture intermediates start with an 8-byte magic sentinel (`kiTextureIntermediateMagic = 0x00000000_BC7E_DA7A`, "BC7E DATA") followed by 3× `int64_t` width/height/mipcount and a zlib stream of all mips concatenated. The magic distinguishes "produced by the current `Texture::Save`" from legacy (pre-magic and/or pre-zlib) files; readers (`LoadIntermediate`, `ExportTexture` raw-passthrough) tolerate both shapes. `MigrateLegacyIntermediates()` in `Main.cpp` upgrades legacy files in place at startup — for BCn it decodes mip 0 and re-encodes with current RDO knobs (one generation of BC7-roundtrip quality loss for islands whose source `.exr` is gone), for R16 it just zlib-wraps. BCn block bytes are RDO-tuned with a small lookback window (4 KB, 256 BC7 blocks) so the byte stream is LZ-friendly without making ERT's O(blocks × window) inner loop blow past the L3-cache cliff on 2K² mips; deflate's own 32 KB window catches longer-range matches. `ChunkHeader` carries the compressed-on-disk size and an uncompressed-size sibling; the runtime `FileManager` decompresses payloads at chunk-load (see [Engine/Source/File/CLAUDE.md](../../../Engine/Source/File/CLAUDE.md)). `.R16G16B16A16_SFLOAT` cubemap intermediates from the IBL convolution path stay in the legacy pre-magic shape — that writer is a separate code path.

## Shader Dirty Tracking

`ExportShader::CheckDirty` parses Makefile-style `.d` depfiles from `glslc -MD` and re-exports when any transitively `#include`'d header is newer than the cached chunk.

## See Also
- [../CLAUDE.md](../CLAUDE.md) — DataPacker orchestration, output structure, and the shared `RunExportJobs<T>` template.
