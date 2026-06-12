# Architecture: Shader/CPU Contract Constants (Graphics/Managers)

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Managers` (non-recursive). Six places where a
value that must agree between two or more sites (C++↔GLSL, or two C++ files) is synchronized by comment or
duplication instead of a single definition. Sibling plan `Graphics/Architecture_SharedConstantDuplication.md`
covers the top-level-Graphics constants; this plan covers the Managers directory. Positives for calibration:
`kiComputeTileSize`/`kiOccupancyDilateGroupSize`/`kiMaxSpreadPasses`/`kiMaxIslands` are already correctly shared
from `shaders::` and `RegisterTextureBinding` ASSERTs bindings against the reflected layout
(`TextureDescriptors.cpp:253`).

## Design

### Engine/Source/Graphics/Managers/PipelineManager.h — dead `TerrainPipelineBindings`
- The `TerrainPipelineBindings` constants `kiElevation/kiColor/kiNormals/kiAmbientOcclusion/kiMasks`
  (`PipelineManager.h:68-75`) appear **only in comments** (`PipelineManager.cpp:251, 392-394, 413, 533`); the
  header claim that "Naming the indices keeps the three sites in lockstep" (`PipelineManager.h:64-67`) is
  false — actual bindings come from shader reflection + positional `DescriptorInfo` order, and
  `IslandTerrain::AcquireTextureSlot` registers via the `mBindlessArrayConsumers` registry without them
  (`Engine/Source/Frame/IslandTerrain.cpp:585-590`). Either wire the constants into the GLSL (via
  `ShaderLayoutsBase.h`) and the registration code, or delete them and fix the header comment.
  Recommendation: delete (reflection is already the source of truth). Grill decision: delete vs wire. [~15m]

### Global Set-0 binding numbers → `shaders::` constants
- The global set's binding numbers (0/1/3/4/12) are bare integers in `TextureDescriptors.cpp:25-31` (layout
  creation) and `:107-108, 125` (descriptor writes), duplicated by dozens of shaders (e.g.
  `Engine/Data/Shaders/Model/Model.frag:46-47`, `Engine/Data/Shaders/Particles/Billboards.frag:14-15`), held
  in lockstep by comment only. Add `kiGlobalBinding*` constants to the dual-language
  `Engine/Data/Shaders/ShaderLayoutsBase.h` and use them in `TextureDescriptors.cpp`. Shader-side adoption
  (layout qualifiers referencing the constants) is a larger sweep requiring a DataPacker shader rebuild —
  grill decision whether to take C++-side-only first or both sides at once. [~1h]

### Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp — format literal
- The spread / spread-only textures are created with literal `VK_FORMAT_R16G16B16A16_SFLOAT`
  (`RenderTargetTexturesLighting.cpp:206, 222`) while the render pass targeting them uses
  `shaders::keLightingFormat` (`:244`, defined at `Engine/Data/Shaders/ShaderLayoutsBase.h:37`). Currently
  equal, so latent only — replace the two literals with `shaders::keLightingFormat` (the pattern smoke / wind /
  elevation already follow at `RenderTargetTextures.cpp:91, 214, 242, 363`). [~5m]

### Engine/Source/Graphics/Managers/PipelineManager.cpp — hand-mirrored water descriptor tables
- The Water (`PipelineManager.cpp:434-455`) and WaterSkyboxOne (`:476-494`) pipelines duplicate ~14 positional
  `DescriptorInfo` entries; the comment at `:463-467` itself warns that divergence silently yields flat
  (un-displaced) water because the shared `Water.vert` reads elevation at set=1 binding=5. Extract the shared
  prefix into one file-local table (or builder helper) both pipelines consume, so the invariant is structural
  rather than comment-enforced. [~30m]

### Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp — LightCombine push-constant type pun
- `RecordLightingSpreadPipeline` builds an ad-hoc local `struct CombineData { uint32_t uiWidth, uiHeight; }`
  (`CommandBufferRecordMain.cpp:402-406`) and memcpys it into `shaders::PushConstantsLayout`
  (`:412`) — whose members are `vec4 f4Pipeline; vec4 f4Material;` (all floats,
  `Engine/Data/Shaders/ShaderLayoutsBase.h:166-170`) — while the shader declares a *different* block
  (`layout(push_constant) uniform PushConstants { uint uiWidth; uint uiHeight; }`,
  `Engine/Data/Shaders/Lighting/LightCombine.comp:21-25`). The two uints ride as bit patterns in
  `f4Pipeline.x/.y`; a reorder of either side breaks silently. Add
  `struct CombinePushConstantsLayout { uint32_t uiWidth INIT; uint32_t uiHeight INIT; }` to the dual-language
  `ShaderLayoutsBase.h`, declare the shader block from it, and populate/push that type directly (deleting the
  local struct, the memcpy, and the float/uint pun). Requires a DataPacker shader recompile. [~30m]

### Duplicated `kBlurSalt`
- `0x424C5552` is defined twice: `TextureManager.cpp:907` and `TextureDescriptors.cpp:417`. Single-source it
  (e.g. `static constexpr` on `TextureDescriptors` in `TextureDescriptors.h`, used by both). [~5m]

### Engine/Source/Graphics/Managers/PipelineManager.h — magic `17` for `kiWaterNormalCount`
- `Texture* mppWaterNormalTextures[17]` (`PipelineManager.h:102-105`) duplicates
  `TextureManager::kiWaterNormalCount` as a literal because `TextureManager.h` is included after
  `PipelineManager.h` in `Engine.h` (`Engine.h:48` vs `:51`). Since the ordering is the other way around,
  define the constant in `PipelineManager.h` (or `ShaderLayoutsBase.h` if shader-relevant) and have
  `TextureManager` reference that single definition; delete the comment workaround. Fallback if single-sourcing
  is rejected: `TextureManager.h` *is* visible in `PipelineManager.cpp`, so at minimum add
  `static_assert(TextureManager::kiWaterNormalCount == std::size(mppWaterNormalTextures))` at the top of
  `CreateLightingShadowDependentPipelines` (`PipelineManager.cpp:365` writes `kiWaterNormalCount` entries into
  the literal-sized array). [~15m]

## Critical files
- `Engine/Source/Graphics/Managers/PipelineManager.h`, `PipelineManager.cpp`
- `Engine/Source/Graphics/Managers/TextureDescriptors.h`, `TextureDescriptors.cpp`
- `Engine/Source/Graphics/Managers/TextureManager.h`, `TextureManager.cpp`
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp`
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (new `kiGlobalBinding*`, possibly `kiWaterNormalCount`)
- (Optional shader sweep) `Engine/Data/Shaders/**` set-0 `layout(...)` declarations

## Out of scope
- Changing any constant's *value* — every item is single-sourcing with bit-identical behavior.
- The Set-0 per-framebuffer *indexing* question — that is `Graphics/PipelineGraphicsGlobalSet0Indexing.md`
  (bind-time index choice in `Pipeline.cpp`), unrelated to the binding-number constants here.
- Top-level Graphics constant pairings — `Graphics/Architecture_SharedConstantDuplication.md`.
- Reworking shader reflection / `DescriptorInfo` positional convention itself.

## Acceptance criteria
- Each constant has exactly one definition; all former duplicate sites reference it; client (and DataPacker,
  if the shader sweep is taken) build clean; no behavioral change.

## Notes
- No determinism/CRC exposure — client render path only. The shader-side Set-0 sweep, if taken, requires a
  DataPacker recompile and renders identically (values unchanged).
- Two grill decisions staged: (a) delete-vs-wire for `TerrainPipelineBindings`; (b) C++-only vs both-sides for
  the Set-0 binding constants.
- Touches `PipelineManager.cpp` alongside `Architecture_LayerSeams.md` and `TextureManager.cpp`/`.h` alongside
  `Architecture_IncludeHygiene.md` — co-schedule (File Groups).

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run). All items
confirmed; no removals:

- **`TerrainPipelineBindings` comment-only**: constants defined at `PipelineManager.h:68-75` (header claim at
  `:63-67`); repo-wide grep finds them ONLY in trailing comments at `PipelineManager.cpp:251, 392-394, 413,
  533` — zero code uses. `IslandTerrain::AcquireTextureSlot` registers via `RegisterTextureBinding` with the
  consumer registry's stored binding numbers (`IslandTerrain.cpp:589`), not these constants.
- **LightCombine pun**: local `struct CombineData { uint32_t uiWidth, uiHeight; }` at
  `CommandBufferRecordMain.cpp:402-406`, memcpy'd into `shaders::PushConstantsLayout` at `:412`
  (`vec4 f4Pipeline` at `ShaderLayoutsBase.h:166-168`); shader block `uint uiWidth; uint uiHeight;` at
  `LightCombine.comp:21-25`. Exactly as described.
- **`kBlurSalt` duplicated**: `0x424C5552` at `TextureManager.cpp:907` and `TextureDescriptors.cpp:417`.
- **Magic `17`**: `mppWaterNormalTextures[17]` at `PipelineManager.h:105` (workaround comment `:102-104`);
  `TextureManager::kiWaterNormalCount = 17` at `TextureManager.h:45`. Include-order claim verified:
  `Engine.h:48` (PipelineManager.h) precedes `:51` (TextureManager.h), and `TextureManager.h` IS visible from
  `PipelineManager.cpp` via the PCH (the constant is already used there at `:365,444,486`), so both the
  single-sourcing direction and the `static_assert` fallback are viable.
- **Format literal**: `VK_FORMAT_R16G16B16A16_SFLOAT` at `RenderTargetTexturesLighting.cpp:206, 222`;
  `shaders::keLightingFormat` (same value, `ShaderLayoutsBase.h:37`) used by the render pass at `:244`.
- **Set-0 binding numbers**: bare 0/1/3/4/12 at `TextureDescriptors.cpp:25-31` (layout) and `:107-109, 125`
  (writes), lockstep-by-comment with shader `layout(set = 0, binding = N)` declarations.
- **Water tables**: duplicated positional entries at `PipelineManager.cpp:434-455` / `:476-494`; divergence
  warning comment at `:463-467`.
- Out-of-scope cross-reference verified: `Graphics/PipelineGraphicsGlobalSet0Indexing.md` is about bind-time
  set *indexing* in `Pipeline.cpp` — genuinely distinct from the binding-number constants here.
