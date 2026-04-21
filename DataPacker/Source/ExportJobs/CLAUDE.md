# `DataPacker/Source/ExportJobs/`

Asset-specific processors that convert raw files into cached binary chunks.

## ExportJob Pipeline

Abstract base owns dirty-checking and cache I/O; derived classes only implement `Export()` (and sometimes `CheckDirty()` / `CleanupOnFailure()`).

- **Cache layout**: `.chunk` = magic + version + `ChunkHeader` + aligned data. A sibling `.txt` stores the input's `last_write_time` so `CheckDirty` survives restores that preserve filesystem timestamps.
- **Version convention**: `GetVersion()` returns `rawVersion + sizeof(common::ChunkHeader)` so header-layout changes auto-invalidate caches. `ExportShader` also folds in `VK_HEADER_VERSION` to force re-export on SDK upgrades.
- **Allocation contract**: each `Export()` calls `AllocateHeaderAndData` exactly once; the base populates magic/crc/flags/path afterward.
- **Clean path**: `RunExport()` skips `Export()` and streams cached bytes back when not dirty.
- **Per-job workbuffer**: `RunExport()` constructs a `common::ThreadLocal` — jobs run on worker threads with isolated `gpThreadLocal->mWorkbuffer`.
- **Intermediate cleanup**: jobs that produce sidecar files track them and override `CleanupOnFailure()` to unlink on throw.

## Handles() and ChunkFlags

Static `Handles(directory_entry) -> optional<ChunkFlags_t>` decides whether a processor claims a path and tags the resulting chunk (stage bits for shaders, cubemap bit for `[C]` textures, raw-passthrough for files under `Raw/`).

## Two-Phase Pipeline

Scene and Islands run a pre-export phase emitting intermediates consumed by the main phase:

- **Islands**: source `.exr`/`.r32` are consumed and deleted, leaving block-compressed / `R16_UNORM` files that `ExportTexture` picks up as passthrough. The island chunk stores CRCs to those files plus a mode-of-nonzero beach elevation and a float-downsampled CPU heightmap. Intermediates may be hand-authored, or baked upstream by the Gaea pre-pass (see `../CLAUDE.md`) from a sibling `island.json` + resolved `.terrain` archetype. `Handles()` ignores island folders until an elevation intermediate exists, so a folder containing only `island.json` is a no-op until the bake produces one.
- **Scene**: a `.PreExport` marker carries its own version; stale marker forces re-running `PreExport` even when the main chunk is clean. Emits `.MODEL` geometry and per-texture block-compressed intermediates (BC4 picked when any material uses the image for occlusion). Primitives from different mesh nodes sharing one glTF material split into distinct material entries while preserving the source index for texture lookup.
- **IBL cubemaps**: do their own timestamp-vs-source dirty checks. Pre-filtered radiance data is written **face-major / mip-minor** to match `TextureUploadManager`'s iteration order.

## Texture Routing

`ExportTexture` routes on filename tags (`[C]`, `[BC4]`, `[BC7]`) and explicit format extensions (passthrough, no mip generation). Font-atlas detection strips `[...]` prefixes and cross-references `Fonts/**/*.fnt`; matches use a box-filter mipmap chain.

## Shader Dirty Tracking

`ExportShader::CheckDirty` parses Makefile-style `.d` depfiles from `glslc -MD` and re-exports when any transitively `#include`'d header is newer than the cached chunk.

## See Also
- [../CLAUDE.md](../CLAUDE.md) — DataPacker orchestration, output structure, and the shared `RunExportJobs<T>` template.
