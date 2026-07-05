# Architecture: Shader/CPU Constant Single-Sourcing

## Summary
**What this plan does:** Routes three hand-mirrored C++↔GLSL storage-image format families and two literal compute-dispatch divisors through the shared dual-language constants in `Engine/Data/Shaders/ShaderLayoutsBase.h`. Adds `keShadowFormat` (R16_UNORM), `keCombineFormat` (R8G8B8A8_UNORM), `keWaterDisplacementFormat` (R16G16B16A16_SFLOAT) following the existing `ke*Format` convention and references them from the shadow / combine-history / water-displacement `VkImageCreateInfo` sites (GLSL `layout(...)` qualifiers stay literal, gaining a pointer comment); swaps the two lighting-blur `(x + 7) / 8` dispatch divisors for `shaders::kiComputeTileSize`; swaps two literal `VK_FORMAT_R16_SFLOAT` sites for `keElevationFormat`; and deletes three dead `ShaderLayoutsBase.h` constants. Client/graphics-only, shader repack, no behavior change.

**Why it's good for the codebase:** Closes a latent silent-UB hazard — the shadow, combine, and water-displacement storage-image formats are currently hand-mirrored between the C++ `VkImageCreateInfo.format` and the shader's `layout(r16/rgba8/rgba16f, ...)` qualifier with nothing tying the two sides together, so a one-sided future edit silently yields a storage-image format mismatch (undefined behavior) or an under-dispatch. It extends the established single-sourcing pattern (`keSmokeFormat`/`keWindFormat`/`keElevationFormat`/`keLightingFormat` and `kiComputeTileSize` already work this way; ~14 other dispatch sites already use the constant) to the last three format families and the two outlier dispatches, and removes three constants (`kiLightingTextures`, `kiBillboardTexturesCount`, `kiMaxAlphaMesh`) that are confirmed dead repo-wide and mislead readers into trusting them as live sizing values.

## Context
- Source: `Documents/Plans/Graphics/Architecture_ShaderCpuSingleSourcing.md` (claimed; removed with its Order.md row after execution completes)
- Order.md row: Tier Small / Effort 2 / Impact 2 / Risks 1 / Score 1 (marked `[CLAIMED]` in Step 2)
- Notes: Add `keShadowFormat`/`keCombineFormat`/`keWaterDisplacementFormat` dual-language constants (three hand-mirrored storage-image families — one-sided edit is silent UB), use `kiComputeTileSize` at the two literal-8 blur dispatches, `keElevationFormat` at two literal sites, delete three dead `ShaderLayoutsBase.h` constants. Shader repack; water session in-flight — refresh cites
- Relevance: **Fully** — every cited literal, dead constant, and shader qualifier still exists with the exact value/format the plan assumes; no literal has already been converted, no format changed, and all three "dead" constants have zero repo-wide uses (verified against current source).
- Dependency resolution: switched from the two higher-priority table rows — `Network/AuditSweepQuickWins.md` (blocked: prerequisite `DeadMachinerySweep` is `[CLAIMED]`) and `Network/Refactor_DrainContractUnification.md` (blocked: prerequisite `SessionBaseCollapse` is `[CLAIMED]`); the lighting-occupancy-removal plan was itself `[CLAIMED]` at the time (since landed and removed). This plan has no `Order.md` dependency edges.
- Changes since the plan was written (line drift from the in-flight water session + path corrections — no logic impact):
  - Lighting-blur dispatches: `TextureManager.cpp` 792/799 → **817/824**
  - rgba8 lighting-blur intermediate/result placeholders: `TextureManager.cpp` 730/752 → **759/777**
  - Elevation literal: `TextureManager.cpp` 125 → **126**; `IslandTerrainResidency.cpp` 81 → **89**
  - Dead constant `kiMaxAlphaMesh`: `ShaderLayoutsBase.h` 184 → **189**
  - **Path corrections:** `RenderTargetTextures.cpp` and `RenderTargetTexturesLighting.cpp` live under `Engine/Source/Graphics/Managers/`, not `.../Render/`. Shaders live in subdirectories: `Engine/Data/Shaders/Lighting/`, `.../Shadow/`, `.../Water/`.

## Execution steps

1. **`Engine/Source/Graphics/Managers/TextureManager.cpp` — lighting-blur dispatch divisors.** In `BlurLightingTexture`, replace the literal `(uiWidth + 7) / 8` / `(uiHeight + 7) / 8` divisors at `:817` (`rBlurH.RecordCompute`) and `:824` (`rBlurV.RecordCompute`) with the `shaders::kiComputeTileSize` form used by every other dispatch site (`(uiWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize`, etc.). The shaders `Engine/Data/Shaders/Lighting/LightingBlurH.comp:5` and `LightingBlurV.comp:5` declare `local_size = kiComputeTileSize` (`ShaderLayoutsBase.h:185`, `= 8`), so the value is unchanged.

2. **`keElevationFormat` at the two literal `VK_FORMAT_R16_SFLOAT` sites.** Replace the literal with `shaders::keElevationFormat` (`ShaderLayoutsBase.h:35`, `= VK_FORMAT_R16_SFLOAT`) at:
   - `Engine/Source/Graphics/Managers/TextureManager.cpp:126` (`CreatePlaceholderTexture(mIslandPlaceholderElevation, …)`)
   - `Engine/Source/Frame/IslandTerrainResidency.cpp:89` (`rTemplate.mElevationTexture.Create`, `.format = VK_FORMAT_R16_SFLOAT`)

3. **`Engine/Data/Shaders/ShaderLayoutsBase.h` — delete three dead constants.** Remove `kiLightingTextures = 11` (`:121`), `kiBillboardTexturesCount = 3` (`:122`), and `kiMaxAlphaMesh = 16` (`:189`). All three are confirmed to have **zero** references anywhere in Engine, Projects, Common, DataPacker, or Data/Shaders (repo-wide grep; only the definition lines and these plan docs match).

4. **`Engine/Data/Shaders/ShaderLayoutsBase.h` — add three new `ke*Format` constants and single-source them.** Following the existing pattern (`inline constexpr VkFormat ke…Format = VK_FORMAT_…;`, e.g. `keElevationFormat:35`, `keLightingFormat:37`, `keSmokeFormat:39`, `keWindFormat:40`), add:
   - `keShadowFormat` = `VK_FORMAT_R16_UNORM`
   - `keCombineFormat` = `VK_FORMAT_R8G8B8A8_UNORM`
   - `keWaterDisplacementFormat` = `VK_FORMAT_R16G16B16A16_SFLOAT`

   Then reference them from the C++ `VkImageCreateInfo.format` sites, and add a pointer comment (e.g. `// keShadowFormat`) next to the matching GLSL `layout(...)` qualifier (the qualifier itself stays a literal GLSL keyword — this is the same comment-level convention the plan's Critical files note describes):
   - **Shadow (`keShadowFormat`, R16_UNORM / GLSL `r16`):** C++ `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` — `CreateShadowTextures()` `:107,122,137,152` and `CreateObjectShadowsTextures()` `:296,317,332`. GLSL: `Engine/Data/Shaders/Shadow/Shadow.comp:14`, `Shadow/ShadowBlurH.comp:14`, `Shadow/ShadowBlurV.comp:14`, `Shadow/ShadowTemporal.comp:15`, `Shadow/ObjectShadowsBlurH.comp:14`, `Shadow/ObjectShadowsBlurV.comp:14`.
   - **Combine / history (`keCombineFormat`, R8G8B8A8_UNORM / GLSL `rgba8`):** C++ `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp:309,325,347,363` and `TextureManager.cpp:759,777` (lighting-blur intermediate/result placeholders — same rgba8 family). GLSL: `Engine/Data/Shaders/Lighting/LightCombine.comp:16-19`, `Lighting/LightingTemporal.comp:19-22`, `Lighting/LightingBlurH.comp:13`, `Lighting/LightingBlurV.comp:13`.
   - **Water displacement (`keWaterDisplacementFormat`, R16G16B16A16_SFLOAT / GLSL `rgba16f`):** C++ `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` — `CreateWaterDisplacementTextures()` (`:26`) at `:37,52`. GLSL: `Engine/Data/Shaders/Water/WaterDisplacement.comp:19-20`.

   Note: `ShaderLayoutsBase.h` and `Water/WaterDisplacement.comp` have in-flight water-session edits — re-verify these exact lines at implementation time before editing.

## Additional candidate locations
**No additional candidates found.** The Step 6 codebase sweep (Opus, across `Engine/Source/Graphics/`, `Engine/Source/Frame/`, `Engine/Data/Shaders/*.comp`, and the game layer) confirmed the plan already lists every hand-mirrored instance of all three patterns:
- **Pattern A (storage-image format, no `ke*Format` constant):** the complete set of compute storage-image format families is exactly five — `rg16f`/wind and `r32f`/smoke already single-source via `keWindFormat`/`keSmokeFormat`; shadow/combine/water-displacement are precisely the three this plan adds. No sixth family exists.
- **Pattern B (literal tile divisor):** every tiled dispatch except the two lighting-blur ones already uses the `shaders::kiComputeTileSize` form (shadow/object-shadow blurs, combine, lighting-temporal, wind/smoke occupancy dilate, and the water-displacement dispatch at `MainUniforms.cpp:457`). The two `TextureManager.cpp:817,824` divisors are the only literals.
- **Pattern C (literal `VK_FORMAT` duplicating a constant):** both `VK_FORMAT_R16_SFLOAT` sites are plan-covered; no others exist.

Four coincidental value-matches were checked and correctly **excluded** (same VkFormat value, but not a hand-mirrored storage image — adopting the constant would wrongly couple unrelated code): `TextureCache.cpp:125` (PBR BRDF LUT, R16G16_SFLOAT), `RenderTargetTextures.cpp:174` (`mSmokeGradientTexture`, R16_UNORM sampled gradient), `RenderTargetTextures.cpp:353` (`kbDebugPrintf`-gated `mLogTexture` color attachment, R8G8B8A8_UNORM), `SwapchainManager.cpp:34,387,405` (HDR scene target, R16G16B16A16_SFLOAT).

## Out of scope
- The `[256]` Gerstner wave arrays' missing shared count constant — water session in-flight; noted, not filed.
- Any behavioral shader change (formats, dispatch counts, and outputs are all bit-for-bit unchanged — the divisor and format values are identical after substitution).
- Substituting the GLSL `layout(...)` format qualifiers themselves — those are GLSL keywords, not `VkFormat` enums; single-sourcing on the GLSL side is a pointer comment only.

## Notes
- **Invariant exposure:** client/graphics-only. Deleting the dead constants and adding new ones changes `ShaderLayoutsBase.h` → **shader repack required**. No determinism/CRC/`.pack`-layout/`kiVersion`/wire exposure; not an allocation-tracked path.
- **Grill decision:** none material — mechanical once the `ke*Format` naming matches the existing family convention.
- **Sequencing:** `ShaderLayoutsBase.h` and `Water/WaterDisplacement.comp` may carry in-flight water-session edits; re-verify the water-displacement and `kiMaxAlphaMesh` lines at execution.
