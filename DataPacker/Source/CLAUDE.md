# `DataPacker/Source`

Asset preprocessing tool that converts raw assets (textures, models, shaders, audio, fonts) into optimized binary formats for runtime loading.

## Architecture

Main.cpp orchestrates a multi-phase pipeline: pre-export (Scene, then a Gaea bake of island intermediates, then Islands) generates intermediates, offline IBL cubemap convolution runs next, main export processes asset types in parallel, then header generation and ThirdParty attribution collection. The Gaea bake is a separate step (not an `ExportJob`) that shells out to `Gaea.Swarm.exe` — all Gaea-version-specific logic is confined to that one translation unit for easier migration. Single-instance guard is a named Win32 mutex. Outside the debugger, top-level `try/catch` shows a MessageBox; under the debugger it runs unwrapped so exceptions break in.

FileManager (`gpFileManager`) owns input/output/temp directories and the Vulkan SDK path (`VK_SDK_PATH` + `Bin/` required for glslc / glslangValidator / spirv-opt). CLI accepts zero args (sandbox defaults) or exactly three (engine-data, project-data, output-dir).

Texture utilities keep internal pixels as RGBA float at 0–255 scale (not 0..1). BC4/BC5/BC7 mip chains terminate once width or height drops below 4 or stops being divisible by 4.

Pch.h sets `kbIsDataPacker = true` (distinct from engine/game PCH); log categories default to `kVerbose`.

## Design Patterns

- **Template-based processing**: `RunExportJobs<T>()` provides type-safe job management with dirty checking. Parallelism is one `std::async` task per matched asset — no central thread pool. `T::Handles(entry)` returns `std::optional<ChunkFlags_t>` to accept-and-tag.
- **Atomic writes**: Outputs written to temp directory first, renamed on success. On any job failure, temp files removed and run returns non-zero without touching prior outputs.
- **Content-based updates**: Generated headers only overwritten when content differs, preventing unnecessary recompilation.
- **Deterministic output**: Assets sorted by relative path before processing for consistent chunk ordering.
- **Per-type header hook**: Only `ExportTexture` contributes extra header content (selected via `if constexpr`). Adding another requires extending the template.

## Output Structure

Each asset type produces `.manifest` (CRC-to-chunk-location), `.pack` (binary data), `.h` (C++ CRC constants). Also generates `DataTypes.h` (enum/names only), `Data.h` (aggregates CRC headers), and sibling `Attribution/` directory. Split headers let files needing only the enum avoid recompilation when asset CRCs change.

Manifest layout: `common::DataHeader` then aligned array of `common::ChunkLocation`. Attribution: `<output-parent>/Attribution/<LibraryName>/`; license search tries `LICENSE*` then `copying*` / `readme*` / `manual.md`. `Prebuilts` skipped; a ThirdParty dir with no recognizable license ASSERTs.

## See Also
- [ExportJobs/CLAUDE.md](ExportJobs/CLAUDE.md) - Individual asset type processors and base class pipeline
