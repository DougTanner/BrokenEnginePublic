# Architecture: Chunk Payload Layouts — Single Source for Writer/Reader Offset Math

## Context
Source: /external-architecture-review on `DataPacker/Source/ExportJobs` (recursive). Chunk **headers** are single-sourced in `Common/DataFile.h` with static_asserts, but chunk **payload** layouts exist as duplicated hand-rolled offset math on the DataPacker writer side and the Engine reader side, tied together only by copy-pasted comments. Any drift ships a pack the engine misreads.

## Design

### Shader chunk payload `[bindings ALIGN16][setIndices ALIGN16][attrs ALIGN16][SPIR-V]`
- Writer `ExportShader.cpp:408-426` and reader `Engine/Source/Graphics/Managers/PipelineManager.cpp:41-54` duplicate identical `RoundUp<int64_t, kiAlignmentBytes>` expressions. Add constexpr offset helpers on `common::ShaderHeader` (in `Common/DataFile.h`, e.g. `BindingsOffset()/SetIndicesOffset()/AttributesOffset()/SpirvOffset(counts...)`) and use them on both sides [~45m]

### Scene chunk payload `[textureCrcs ALIGN16][indexStarts ALIGN16][MaterialShaderData]`
- Writer `ExportScene.cpp:530-537` and **two** readers: `Engine/Source/Graphics/Objects/ModelPipeline.cpp:38-43` (layout comment literally copy-pasted between the files) and `Engine/Source/File/FileManager.cpp:363-367` (animation-offset computation — duplicates the same `RoundUp` chains plus a 16-aligned `MaterialShaderData` block term). Same approach on `common::SceneHeader`; an `AnimationSectionOffset(textureCount, materialCount)` helper covers the FileManager site [~45m]

### Animation section walk
- Writer `ExportScene.cpp:780-829` (sequential memcpys with `RoundUp<int64_t, 4>` at :781) and reader `Engine/Source/Graphics/AnimationData.cpp` pointer walk (same `RoundUp<int64_t, 4>` at :21). Share the per-array offset/alignment step through the same helper family [~30m]

### `.MODEL` intermediate format
- One writer (`ExportScene.cpp:435-518`), three readers (`ExportScene::MainExport` :523-528, `ExportScene::ReadMaterialInfosFromModel` :586-598, `ExportModel::Export` `ExportModel.cpp:22-42`). The 16-vs-32-bit index decision is duplicated between writer (`ExportScene.cpp:479`) and reader (`ExportModel.cpp:30`) with nothing tying them. Add a shared constant for the index-width threshold and named section-order constants in a DataPacker-local header both TUs include [~30m]

### Font chunk payload `[ids ALIGN16][characters]`
- Writer `ExportFont.cpp:105-116` computes the characters block at `RoundUp<int64_t, kiAlignmentBytes>(iIdsBytes)`; reader `Engine/Source/Graphics/Managers/TextManager.cpp:32` hand-mirrors the same `RoundUp<int64_t, kiAlignmentBytes>(iCharacters * sizeof(uint32_t))` offset — tied only by the convention that the id count equals the character count. Add an offset helper on `common::FontHeader` (e.g. `CharactersOffset(idCount)`) and use it on both sides [~20m]

### Texture intermediate format `[kiTextureIntermediateMagic][w][h][mips][zlib payload]`
- One writer (`Texture::Save`, `Texture.cpp:600-621`), three hand-rolled readers each re-implementing the magic-vs-legacy branch: `ExportTexture.cpp:132-150`, `Main.cpp` `LoadBc7AsFloatPixelsMip0` :216-241, `MigrateLegacyIntermediates.cpp:131-165`. Extract one shared read helper (e.g. beside `Texture::Save` in `Texture/Texture.h`). Legacy pre-magic tolerance must remain — the IBL writers intentionally emit the legacy shape (`ExportCubemapIbl.cpp:109-120, 174-185`) [~30m]

## Critical files
- `Common/DataFile.h` (offset helpers beside `ShaderHeader`/`SceneHeader`)
- `DataPacker/Source/ExportJobs/ExportShader.cpp`, `ExportScene.cpp`, `ExportModel.cpp`, `Texture/Texture.{h,cpp}`, `Texture/MigrateLegacyIntermediates.cpp`, `ExportTexture.cpp`; `DataPacker/Source/Main.cpp`
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`, `Engine/Source/Graphics/Objects/ModelPipeline.cpp`, `Engine/Source/Graphics/AnimationData.cpp`, `Engine/Source/File/FileManager.cpp` (scene animation-offset math)

## Acceptance criteria
- Pack bytes are identical before/after (pure refactor — same offsets computed through shared helpers).
- Writer and reader of each payload reference the same named offset functions/constants; no duplicated `RoundUp` chains remain at the listed sites.

## Out of scope
- Changing any payload layout, alignment, or `kiVersion` — bytes must not change.
- The `kIsland` payload (`ExportIsland.cpp:444-450` vs `IslandHeader` comment contract) — engine slices via header counts, no duplicated offset math to unify.
- The chunk-cache versioning hole — separate plan (`Architecture_ChunkCacheVersioning.md`).

## Notes
- Touches the engine read path for shaders/scenes/animations — mechanical, but a mistake breaks asset loading; verify with a full bake + game load. No runtime-determinism/CRC-sim exposure (load-time only).
- Pre-staged grill question: helpers as constexpr member functions on the header structs vs free functions in a new `Common/ChunkLayout.h` — member functions keep DataFile.h the single contract file (recommended).

## Verification Notes
- All writer/reader line citations verified exact on both sides: `ExportShader.cpp:408-426` ↔ `PipelineManager.cpp:41-54`; `ExportScene.cpp:530-537` ↔ `ModelPipeline.cpp:38-43` (comment copy-paste confirmed verbatim); `ExportScene.cpp:780-829` (`RoundUp<int64_t, 4>` at :781) ↔ `AnimationData.cpp` (`RoundUp<int64_t, 4>` at :21); `.MODEL` writer :435-518, readers :523-528 / :586-598 / `ExportModel.cpp:22-42`; index-width threshold writer `ExportScene.cpp:479` ↔ reader `ExportModel.cpp:30`; texture-intermediate writer `Texture.cpp:600-621`, readers `ExportTexture.cpp:133-150` / `Main.cpp:216-241` / `MigrateLegacyIntermediates.cpp:131-166`; IBL legacy-shape writers `ExportCubemapIbl.cpp:109-120` and :174-185 (inside `convertAndWrite`).
- A third scene-layout reader (`Engine/Source/File/FileManager.cpp:363-367`) was missing from the original draft and has been added — it computes the animation-section offset with the same duplicated `RoundUp` chains and is exactly the site whose math `ExportScene.cpp:793`'s sanity check currently fails to mirror (see `Architecture_ChunkCacheVersioning.md`).
- Caveat for the texture-intermediate read helper: `MigrateLegacyIntermediates.cpp`'s reader has different semantics from the other two — it must *skip* magic-prefixed files (idempotence), validates header plausibility (:148-151), and returns silently on corrupt zlib rather than asserting. The shared helper needs to expose header info + payload + a was-magic flag rather than forcing one error policy.
