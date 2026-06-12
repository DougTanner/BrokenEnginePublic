# Refactor: ExportJobs Quick-Win Mechanics

## Context
Source: /external-refactor-clean on `DataPacker/Source/ExportJobs` (recursive). Mechanical, low-risk in-function cleanups — each independently <15 minutes, batched into one pass.

## Design

### DataPacker/Source/ExportJobs/ExportJob.cpp
- `ExportJob` ctor input-directory prefix detection uses `mInputPath.native().find(...) != std::string::npos` (line 12) — `npos` from `std::string` against a `wstring` find, and a mid-path coincidental match selects the wrong root before the `substr`. Use `starts_with(gpFileManager->mpInputDirectories[0].native())` [~5m]

### DataPacker/Source/ExportJobs/ExportShader.cpp / ExportShader.h
- `ExportShader::Handles` calls `rDirectoryEntry.path().extension()` three times per directory entry (line 96), each constructing a fresh `std::filesystem::path` — hoist one local [~5m]
- Mark TU-internal free functions/structs (`BindingTable`, `WriteBinding`, `CollectBindings`, lines 137/145/160) `static` or wrap in an anonymous namespace — external linkage is an ODR hazard; `GaeaArchetype.cpp` shows the local pattern [~5m]
- Collapse `ExportShader.h:36-50` access-specifier churn (`protected:` → `private:` → second `protected:`); nothing derives from `ExportShader` [~5m]

### DataPacker/Source/ExportJobs/ExportScene.cpp
- `LoadGltfModel` hand-parses the extension via `rfind('.')` + `substr` (lines 190-196) — replace with `mInputPath.extension() == ".glb"` [~5m]
- Mark TU-internal free functions (`ToVkFilter`, `ToVkSamplerAddressMode`, `ComputeTextureFormats`, lines 89/113/131) `static`/anonymous-namespace [~5m]
- While there: replace the raw glTF sampler-enum integers in `ToVkFilter`/`ToVkSamplerAddressMode` (9728/9729/9984-9987, 10497/33071/33648) with tinygltf's `TINYGLTF_TEXTURE_FILTER_*`/`TINYGLTF_TEXTURE_WRAP_*` defines [~10m]

### DataPacker/Source/ExportJobs/ExportTexture.cpp
- `ProcessKtxCubemap` takes `[[maybe_unused]] VkFormat vkFormat` (line 105) and hardcodes `VK_FORMAT_R16G16B16A16_SFLOAT` at :122 — drop the dead parameter [~5m]

### DataPacker/Source/ExportJobs/ExportIsland.cpp
- Delete the no-op `reinterpret_cast<std::byte*>(dataSpan.data())` (line 446) — already `std::byte*` [~2m]
- `Handles` walks `parent_path()` in a manual loop to find an "Islands" ancestor (lines 351-365); iterate path components directly like the siblings (`ExportRaw.cpp:15-21`, `ExportScene.cpp:40-46`) [~10m]

### DataPacker/Source/ExportJobs/ExportFont.cpp
- `std::span<CharInfo>` used for in-memory iteration (line 87) — style rule 21 allows span only for mapped-Vulkan-memory storage; use pointer + count [~5m]

### DataPacker/Source/ExportJobs/Scene/SceneVerticesLoader.cpp
- `IsOcclusion`'s single-line shared-texture test repeats `map.find(name) != end() && map.at(name).TextureIndex() == iIndex` four times (line 354) — extract a `UsesTexture(map, name, iIndex)` helper [~10m]

### DataPacker/Source/ExportJobs/ExportCubemapIbl.cpp
- Replace the three per-element half↔float scalar loops (lines 44-50, 101-107, 163-171) with DirectXMath `XMConvertHalfToFloatStream`/`XMConvertFloatToHalfStream` batch calls — identical results, SIMD-batched, and drops the per-pixel bounds-checked `.at(i)`. The :163-171 instance converts up to 1024² × 11 mips × 6 faces per cubemap [~20m]
- Phase-2 prefiltered loop builds a `std::vector<std::string> faceNames` per matched `[C]` directory (lines 260-262) — use two `static constexpr const char*` C arrays and select a pointer [~5m]

### std::array → C arrays (style rule 21)
- `Texture/MigrateLegacyIntermediates.cpp:43, 49, 60, 72` and `Island/SubdivideBeachBand.cpp:48-49, 70, 92, 191` — replace `std::array` with C arrays (for the `std::array<int32_t, 2>` edge-slot map value, a tiny two-member struct) [~15m]

### DataPacker/Source/ExportJobs/Island/SubdivideBeachBand.cpp
- `BeachSubdivider` ctor takes 7 parameters, 4 of which are `SubdivisionConfig` fields unpacked by the wrapper at line 386 — pass the config struct through (line 13) [~10m]

## Critical files
- All files listed above.

## Out of scope
- `ExportTexture` format-sniffing/`Handles`/font-rescan rework — `Refactor_ExportTextureMechanics.md`.
- Large-function decompositions — `Refactor_IslandFunctionDecomposition.md` / `Refactor_SceneFunctionDecomposition.md`.
- Unused includes / `using enum` removals — `Architecture_ExportJobsIncludeHygiene.md`.

## Notes
- No determinism/CRC/pack-layout exposure: every item is behavior-preserving (the half↔float stream conversions produce bit-identical results to the scalar intrinsics). Compile + a normal bake verifies.
- File-group overlaps: `ExportCubemapIbl.cpp` (with `Architecture_OrchestrationDedup.md`), `ExportShader.cpp` (with `Architecture_ShaderReflectionGuards.md`, `Refactor_ShaderToolRunnerDedup.md`), `SceneVerticesLoader.cpp` (with the Scene plans) — co-schedule where convenient.

## Verification Notes
- All line/symbol citations verified exact (ExportJob.cpp:12; ExportShader.cpp:96/:137/:145/:160 and ExportShader.h:36-50 with nothing deriving from `ExportShader`; ExportScene.cpp:190-196/:89/:113/:131 with the three free functions used nowhere outside the TU; ExportTexture.cpp:105/:122; ExportIsland.cpp:446/:351-365; ExportFont.cpp:87; SceneVerticesLoader.cpp:354; ExportCubemapIbl.cpp:44-50/:101-107/:163-171/:260-262; MigrateLegacyIntermediates.cpp:43/:49/:60/:72; SubdivideBeachBand.cpp:48-49/:70/:92/:191/:13 with the wrapper at :386).
- Style-guide claims confirmed verbatim: rule 21 (`Documents/C++StyleGuide.txt:110`) bans `std::array`/`std::span`/`std::bitset` with the sole exception "std::span is allowed when storing pointer + size for mapped Vulkan memory" — the ExportFont span and all eight `std::array` sites are violations as claimed.
- Half↔float stream claim verified at the strongest reading: `XMConvertHalfToFloatStream`/`XMConvertFloatToHalfStream` exist in DirectXMath's PackedVector (the scalar `XMConvertHalfToFloat` is already used at `ExportCubemapIbl.cpp:48`), and because the project bans AVX (`ExternalHeaders.h:4-5`), `_XM_F16C_INTRINSICS_` is off — both scalar and stream paths run the same software round-to-nearest-even conversion, so results are bit-identical by construction.
- TINYGLTF sampler defines confirmed present in the imported tinygltf (`tiny_gltf.h:99+`: `TINYGLTF_TEXTURE_FILTER_NEAREST` = 9728 etc.).
- One behavior caveat added for the `ExportIsland::Handles` item: the sibling pattern iterates the *full* path's components, but `Handles` checks ancestors only (it starts at `parent_path()`). To stay behavior-preserving, iterate `rDirectoryEntry.path().parent_path()`'s components — a claimed leaf directory itself named "Islands" must not start matching.
- Wording nit (no action needed): the ExportJob.cpp item says "`npos` from `std::string` against a `wstring` find" — `std::string::npos` and `std::wstring::npos` have the same value, so that mismatch is cosmetic; the substantive defect is the mid-path substring match, which is real.
