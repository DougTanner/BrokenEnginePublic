<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T16:27:17.589Z","dependsOn":["Documents/Plans/Graphics/WaterShorelineFoamBands.md"]} -->
# Baked per-island shore distance/direction field for direction-correct foam

## Context

Contingency/upgrade prototype for the depth-driven foam of `WaterShorelineFoamBands.md`. That plan uses water depth as the shore coordinate, which is correct only where the seabed slopes smoothly away from the beach. Explicit trigger condition — build this plan only if `/agent-harness` screenshots from Plans 1+2 show either: (a) foam bands detaching from the coastline or collapsing into a single hard edge at cliff faces, where the artist-curved depth ramps too fast to hold multiple bands; or (b) band motion or spacing that reads visibly wrong in narrow bays and channels between islands, where the depth gradient direction diverges from the true shore direction. If neither artifact is visible at gameplay altitude, reject this plan rather than building it.

The upgrade replaces depth with a baked shore field, the Anno 1800 approach (GDC talk: shore waves driven by precomputed shore distance and direction): DataPacker computes, per island at pack time, the distance to the waterline and the 2D shore-normal direction (central-difference gradient of the heightmap), stores them as one extra per-island texture, and the runtime composites them into a new visible-area target exactly the way `TerrainElevation.frag` composites elevation. `Water.frag` then reads shore proximity for the band coordinate and the direction for noise scroll, giving direction-correct inward crawl regardless of seabed shape. Islands are static baked heightmaps, so the field never changes at runtime — precedent: the existing per-island elevation splat path.

Risk tier: Tier 3 — this spans DataPacker output format (a new pack chunk/texture per island), texture and render-target management, pipeline creation, and shaders across independently owned subsystems. It requires the full Tier-3 workflow (execution card, `/plan-audit` plus `/external-grill-plan`, `/adversarial-review`). Still zero simulation/CRC exposure: everything runtime-side is client-only rendering.

## Scope contract

The listed scope is both target and ceiling: smallest complete change satisfying the acceptance criteria plus the mechanical necessities the named regions require. Prototype for visual interest — no generalized distance-field library, no reuse by other systems.

## In scope

Only these regions (refresh exact line anchors from the then-current files; function names, not planning-time line numbers, define the regions):

- `DataPacker/Source/ExportJobs/ExportIsland.cpp`: a new bake step next to the existing `Elevation.r32` read (the region around lines 307-320 that fills `cpuHeightmapData`) computing the shore field from `cpuHeightmapData` per Design, plus emitting it as a new per-island packed texture alongside the existing color/normals/AO/masks texture emission, and, in `DataPacker/Source/ExportJobs/ExportIsland.h`, the new intermediate-filename constant beside `kpcIslandMasks` plus the version bumps the new output requires (`ExportIsland::GetVersion`'s raw version for the changed island chunk header, `kiTextureVersion` for the added BC encode).
- `Common/DataFile.h`: the `IslandHeader` struct (~line 274) gains `shoreFieldCrc` beside the four existing texture CRCs, with the `static_assert(sizeof(IslandHeader) == 72, ...)` size updated and `DataHeader::kiVersion` bumped exactly as that assert's message requires; plus the new `kfShoreFieldMaxMeters` constant beside `kfUnderwaterMaskThresholdMeters`.
- `Engine/Source/Graphics/Managers/RenderTargetTextures.h`/`.cpp`: a new `mShoreFieldTexture` visible-area target created beside `mTerrainElevationTexture` (sized via the same `TextureManager::DetailTextureSize` path, `RenderTargetTextures.cpp` ~line 380).
- `Engine/Source/Graphics/Managers/PipelineManager.h`/`.cpp`: a new `kPipelineShoreField` enum slot and creation mirroring `kPipelineTerrainElevation` (creation site ~lines 538-553), including the per-island bindless shore-field texture array descriptor.
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp`: record the shore-field pass immediately after the terrain-elevation pass (~lines 110-114), same instance source.
- Engine-side per-island texture registration. The shore field is chunk-backed (a lazy texture chunk with its own CRC), so it follows the color/normals/AO/masks path, not the template-owned `mElevationTexture` path, and the fixed-size CRC arrays that path uses grow from 4 to 5 entries:
	- `Engine/Source/Frame/IslandTerrainResidency.cpp` (client-gated island loading and residency; the sim half of `IslandTerrain` lives in `IslandTerrain.cpp` and is untouched): the `common::crc_t textureCrcs[4]` literal in `IslandTerrain::AcquireTextureSlot`, `residencyCrcs[4]` in the file-local `IsTextureRestorationPending`, and `evictCrcs[4]` in `IslandTerrain::EvictTemplate` each gain the shore-field CRC, so chunk load requests, the four-channel-ready transition, and eviction all cover it.
	- `Engine/Source/Frame/IslandTerrain.h`: the `(&textureCrcs)[4]` parameter of `AcquireTextureSlot`'s private helper `FirstMintTextureSlot`, and the comments naming "five channels"/"four chunk-backed channels", updated for the added channel.
	- `Engine/Source/Graphics/Managers/TextureDescriptors.h`/`.cpp`: `IslandSlot::textureCrcs[4]`, `MintIslandSlot`, `EvictIslandSlot` (both `(&textureCrcs)[4]` signatures), the placeholder-init loop that fills every slot at startup, and the `ppTextures == rTargets.m*Textures.data()` binding-key dispatch chain each gain the shore-field array. `RestoreIslandSlot` needs no change — it restores only the template-owned elevation texture.
	- `Engine/Source/Graphics/Managers/RenderTargetTextures.h`: a `std::vector<Texture*> mShoreFieldTextures` beside `mMasksTextures`; `Engine/Source/Graphics/Managers/TextureManager.h`/`.cpp`: an `mIslandPlaceholderShoreField` beside `mIslandPlaceholderMasks`, created in the same `CreatePlaceholderTexture` block and sized in the same `resize(shaders::kiMaxIslands)` block.
- `Engine/Data/Shaders/Terrain/ShoreField.frag`: new fragment shader, sibling of `TerrainElevation.frag`, splatting per-island shore-field textures into the target (blend rule per Design); reuses the existing `QuadsAxisAlignedVisibleArea.vert`.
- `Engine/Data/Shaders/Water/Water.frag`: one new `set = 1` sampler binding for the shore-field target; replace the foam block's depth-derived `fShoreT` with sampled shore proximity and add the direction-based noise scroll per Design. No other part of the foam block changes.
- `Engine/Data/Shaders/ShaderLayoutsBase.h`: the new water binding constant and one float `fWaterShoreFoamRescale` (maps sampled proximity to the foam band coordinate); `Engine/Source/Ui/WaterWrappersBase.h`/`.cpp`, `TweaksScreenWater.cpp`, `WaterUniforms.cpp`: one wrapper/slider/upload `gWaterShoreFoamRescale` in the existing Foam block.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026` project/filter membership for any new C++ files, via `/update-vcxproj`.

## Out of scope

- Any change to simulation behavior: the CRC'd elevation/normal sim path (`Engine/Source/Frame/IslandTerrain.cpp` — `GlobalElevation`, `FrameElevation`, `FrameNormal`, `BuildElevationGrid`, `MakeFrameElevationSampler`), collision, pathing, and every other CRC-relevant read or write. The client-gated residency regions named in `## In scope` are the only `IslandTerrain` code this plan touches, and no sim code may read the shore field.
- Changing the existing elevation bake, `TerrainElevation.frag`, the elevation target, or its consumers; the shore field is strictly additive alongside them.
- Foam appearance changes beyond swapping the shore coordinate and scroll direction — band function, noise contract, sliders, and blend point stay as `WaterShorelineFoamBands.md` landed them.
- Runtime distance-field recomputation, dynamic waterlines, server builds, and reuse of the field by lighting/shadow/gameplay systems.

## Design

- Bake (DataPacker, per island, from the already-loaded `cpuHeightmapData` at heightmap resolution — its values are engine-meters above beach, so beach/sea level is exactly `0.0f`): waterline = the `0.0f` elevation contour. `common::kfUnderwaterMaskThresholdMeters` is `-2.5f`, not sea level; it is the texture-masking and valid-area cut only, so the bake references it solely where it must match the existing island texture footprint (texels masked out of the shipped textures), never as the shore contour. Distance = per-texel distance in island-local meters to that contour, computed by breadth-first ring expansion seeded from every texel adjacent to a sign change of `elevation - 0.0f` and expanded outward over the heightmap grid; ring expansion is chosen over jump flood because bake cost is irrelevant offline and the BFS is materially simpler. Direction = normalized central-difference gradient of a 1-texel-blurred heightmap, pointing up-gradient (toward land). Encode RGBA8: `R = 1 - clamp(distance / kfShoreFieldMaxMeters, 0, 1)` (shore *proximity*, 1 at the waterline), `G/B = direction * 0.5 + 0.5`, `A` unused 0. `kfShoreFieldMaxMeters` is `50.0f`, declared in `Common/DataFile.h` beside `kfUnderwaterMaskThresholdMeters`. Derivation: 50 m is the narrowest shipped island-chunk footprint (`widthMeters` 400 in `Engine/Data/Islands/01/Island.json` divided by that island's 8-column route), so the field spans the whole width of the smallest island chunk, and the 8-bit proximity step 50/255 = 0.196 m matches the 0.195 m heightmap texel spacing (400 m over 8192 / `kiElevationDivisor` = 2048 texels), so the encoding loses no resolution the bake actually has.
- Proximity (not distance) is stored so the runtime composite can use the same MAX blending as the elevation target with a clear value of 0: a texel covered by one island passes through unchanged, and overlapping island footprints — which per the Terrain contract occur only underwater at packed-mip bounds, where the breaker mask has already zeroed foam — produce harmless per-channel MAX garbage in unobservable pixels. Document that acceptance explicitly at the blend-state creation.
- Runtime composite: `ShoreField.frag` mirrors `TerrainElevation.frag`'s structure (bindless per-island sampler indexed by instance slot, `nonuniformEXT`), minus the undersea pow curve — the field is already in its final encoding. Target format RGBA8, visible-area extent from the same detail-size path as elevation, pass recorded right after elevation in the Global command buffer, participating in the same recreation tiers.
- `Water.frag` foam block changes only two inputs: `fShoreT = clamp((1.0f - f4ShoreField.x) * globalLayout.fWaterShoreFoamRescale, 0.0f, 1.0f)` replaces the depth-derived coordinate, and the foam-noise UV gains an inward scroll `f2NoiseUv += (f4ShoreField.yz * 2.0f - 1.0f) * globalLayout.fWaterFoamPhase * <foam noise scale>` so the ragged cut travels along the shore normal. `fBreakMask` stays depth-based (it gates physical breaking, which really is depth).
- Sampling the shore-field target uses the plain visible-area texcoord (`f2VisibleAreaTexcoord`, already computed) — same addressing as `elevationTextureSampler`, no precision concerns.

## Critical files

- `DataPacker/Source/ExportJobs/ExportIsland.cpp`/`.h` — bake and chunk emission.
- `Common/DataFile.h` — `IslandHeader` texture CRCs, its size `static_assert`, and `kfShoreFieldMaxMeters`.
- `Engine/Source/Frame/IslandTerrainResidency.cpp`, `Engine/Source/Frame/IslandTerrain.h`, `Engine/Source/Graphics/Managers/TextureDescriptors.h`/`.cpp`, `TextureManager.h`/`.cpp` — the per-island channel set growing from four chunk-backed textures to five (load, mint, restore-gate, evict, placeholders).
- `Engine/Source/Graphics/Managers/RenderTargetTextures.h`/`.cpp`, `PipelineManager.h`/`.cpp`, `CommandBufferRecordGlobal.cpp` — target, pipeline, and pass recording, all mirroring the elevation path.
- `Engine/Data/Shaders/Terrain/ShoreField.frag` — new composite shader.
- `Engine/Data/Shaders/Water/Water.frag`, `ShaderLayoutsBase.h`, `WaterWrappersBase.*`, `TweaksScreenWater.cpp`, `WaterUniforms.cpp` — consumption and the one new tunable.
- `Engine/Data/Shaders/Terrain/TerrainElevation.frag` and `Engine/Data/Shaders/Terrain/AGENTS.md` — read-only precedent: MAX-blend/clear-value contract the new target imitates.

## Risk triggers and invariants

- Tier 3: new pack chunk format (repack required; no backward compatibility — one current format only), a new render target participating in recreation/destroy tiers, bindless per-island descriptor lifecycle (writes only inside the bindless write epoch, per the Graphics churn-scan contract), and cross-subsystem span (DataPacker + Managers + shaders).
- CRC safety: render-only data and client-only code; simulation terrain untouched. Any temptation to read the shore field from sim code is out of scope by definition.
- Elevation-composite contract: the new target must copy the clear-value/blend pairing discipline (proximity 0 clear + MAX) — a wrong clear value silently corrupts every non-island pixel.
- Island texture lifecycle: shore-field textures must ride the existing adoption/eviction path; a separately managed lifetime would race the descriptor epoch.
- Water precision contracts and the cross-product-normal/prepass-dampener rules remain binding for the `Water.frag` edit.

## Acceptance criteria

- DataPacker repack emits the shore-field texture for every island; a spot-check dump of one island shows proximity 1 along the coastline and direction vectors pointing inland (sign check against the heightmap gradient).
- `/compile` builds DataPacker Release and client Debug|x64; `/update-vcxproj` verifies membership for new files.
- `/agent-harness` at gameplay altitude: at a location that exhibited the recorded trigger artifact under Plans 1+2 (cliff face or narrow bay — capture the same viewpoint), a screenshot shows evenly spaced foam bands hugging the actual coastline, and a two-screenshot pair shows the ragged pattern crawling inland along the shore normal; open-ocean and smooth-slope beaches remain visually equivalent to the Plan-2 result.
- Debug texture display (or `Screenshot.cpp` render-target capture) of the new shore-field target shows island coastlines as bright proximity ridges with no bleed outside island footprints above water.
- No Vulkan validation errors across a window resize and a device-recreation smoke pass (the new target participates in recreation correctly).

## Notes

- Reference: Anno 1800 (GDC) shore-wave rendering driven by precomputed shore distance/direction fields.
- This plan intentionally leaves `fBreakMask` and the `WaterShoalingAmplitude.md` prepass untouched: shoaling physics keys on depth; only the foam's *pattern space* switches to the shore field.
