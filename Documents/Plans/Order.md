# Plan Execution Order

All refactor and bugfix plans — debt reduction that doesn't add a new engine capability — sorted by score (lowest = highest priority).

Score = Effort − Impact + Risks (lower = higher priority)

`Tier` column is an informal size/risk descriptor (**Quick Win** / **Small** / **Medium** / **Large** / **Architectural**); it does not affect score or ordering.

Counterpart: `Documents/Features/Order.md` holds brand-new additions. See `Documents/CLAUDE.md` for the distinction. Scores are not comparable across the two files — they were originally derived from a single sort and then split.

## Debt Score (DataPacker, this run): MODERATE

Distribution: 6 Medium + 1 Architectural. No critical bugs, but `ExportScene.cpp` is approaching the `/reduce-file` threshold and a public header pulls tinygltf into unrelated TUs.

## Debt Score (Network, this run): MODERATE

Distribution: 7 Quick Win + 6 Medium + 1 Architectural across 14 plans. Network subtree is structurally sound — the main concentrations are (1) engine → game surgery in `ServerReceive.cpp` save/load/timespeed handlers and the `*SessionBase.cpp` CoordFrames mutation, (2) one replay-determinism bug (fleet RNG re-seeded from wall clock on load), and (3) mechanical duplication across ~20 `Send*` methods, ~14 connected-gate sites, and ~12 handshake-gate sites. `ServerFleetManager.cpp` (808 lines) and `ReconcileReplay.cpp` (668 lines) are genuine `/reduce-file` candidates.

## Debt Score (Graphics — Shaders, this run): MODERATE

60 first-party GLSL shaders reviewed via one `/glsl-review` Opus subagent per file. Original distribution: 9 Quick Win + 3 Medium across 12 subsystem plans. **Six performance-neutral bugfix plans landed this session** via parallel Opus subagents: `03_Terrain`, `05_Quads`, `07_Objects`, `04_Particles` fully executed and deleted; `09_Wind` and `12_Misc` partially executed (descriptor-set audit and `nonuniformEXT` audit deferred — both require C++ coordination). `08_Smoke` landed in a follow-up session (full execution, deleted). The particles sweep landed cross-cutting fixes folded in: 5 `writeonly` Shadow image2D sites, 2 `writeonly` Lighting blur sites, 4 `writeonly` Smoke/Wind spread sites, `LightCombine.comp` Uchimura `pow` base clamps, and the redundant outer `normalize` drop in `Model.frag`. Remaining distribution: 1 Medium across 1 subsystem plan (11_Water). **`06_Lighting` landed this session**: its correctness/consistency items were either already mitigated by existing slider clamps or executed (ring-count slider min raised to 2 to guard the `LightingSpread` `fNorm` divide; `scalar` qualifier added to `lightOccupancyBuffer` in `AreaLight`/`PointLight`); the deferred performance items were split into the new `06_LightingPerf` plan, which **landed in a follow-up session** (incremental rotation recurrence for the `LightingSpread` direction-loop `cos`/`sin`, hue-preserving Reinhard dedup, `LightCombine` Uchimura skip at `fHuePreserve>=0.999`, blur reciprocal hoist, `AreaLight`/`PointLight` early-alpha discard; plus folded-in Pattern-3 pow-skip guards in `TerrainElevation.frag`, `Water.frag`, and `ShaderFunctions.h`). No `inverse()` driver-hang violations. Remaining recurring hazards: (1) unguarded `normalize()` on possibly-zero vectors (Model, Water — Particles now guarded), (2) `pow(neg, non-integer)` → NaN in `LightingSpread.frag` (LightCombine clamped), (3) implicit-LOD `texture()` in compute (Shadow, ObjectShadowsBlur). Standalone hard bugs: `Terrain.frag:76` (landed), `Log.vert:38` (landed), `ParticlesUpdate.comp` non-atomic RMW race (landed). Overview at `Graphics/ShaderReview/00_Overview.md`; per-subsystem plans at `Graphics/ShaderReview/{02,09,11,12}_*.md`.

## Plans

| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
|---|------|------|--------|--------|-------|-------|-------|
| 1 | [Engine/IslandColorBc7SrgbBlock.md](Engine/IslandColorBc7SrgbBlock.md) | Large | 4 | 3 | 2 | 3 | After the May 2026 Color/Normals color-space cleanup, island `Color.BC7_UNORM_BLOCK` files contain sRGB-encoded bytes fed straight into linear-space lighting math. Switch DataPacker output + sampler view to `VK_FORMAT_BC7_SRGB_BLOCK` so the hardware decodes sRGB→linear on every tap. Touches `ExportIsland.{cpp,h}`, `DataPacker/Main.cpp`, `Texture.cpp`, `TextureUploadManager.cpp`, plus a shader-side appearance pass to re-tune `Terrain.frag`'s hardcoded color thresholds against the now-linear samples; `TerrainColor.frag`'s composite RTT may need a parallel format switch |
| 3 | [Frame/ScaleEngineToMeters.md](Frame/ScaleEngineToMeters.md) | Large | 4 | 3 | 2 | 3 | Three-batch pass to bring camera, gameplay, and audio tunables into the "1 unit = 1 m" convention introduced when `kfMetersToUnits = 1.0f` and `kfCellWidth/kfCellHeight = 600` landed. Catalog: ~15 literals across `Camera.{h,cpp}`, `CameraBase.cpp`, `Game.{cpp,h}`, `FrameBase.h`, `Frame.cpp`, `TerrainUtils.cpp`, `Players{,Navigation}.{cpp,h}`, `Spaceships.cpp`, `SoundSettingsWrappersBase.cpp`, `WrapperBase.cpp`. Final cleanup deletes `engine::kfMetersToUnits` and simplifies the three `IslandTerrain.cpp` multiplications |
| 4 | [Graphics/WaterDisplacementIndirectCompute.md](Graphics/WaterDisplacementIndirectCompute.md) | Medium | 3 | 2 | 2 | 3 | Two deferred perf items in the Gerstner-displacement pre-compute. Item 1: replace the `ASSERT(false)` for `kIndirectHostVisible|kCompute` at `PipelineCreator.cpp:624` with the symmetric host-visible-indirect branch, add `Pipeline::WriteIndirectComputeBuffer`, wire `kPipelineWaterDisplacement` to indirect dispatch from `MainUniforms.cpp` (saves ~64x workgroup launches at LOD3). Item 2: extend the existing `fShoreAmplitude` check in `WaterDisplacement.comp` so `fTerrainElevation >= 0.0f` early-returns over land, skipping both Gerstner loops |
| 5 | [Graphics/IslandNavContourResidency.md](Graphics/IslandNavContourResidency.md) | Medium | 3 | 2 | 2 | 3 | Server-side per-template `IslandTemplate::mNavContour` (~5-50 KB per template × ~65 templates) is permanently resident — built once in `WaitForElevationMaps`, freed only at `~IslandTerrain`. Surfaced by the `IslandResidentMemoryScaling` sibling-sweep. Adopt whatever lifecycle the strategy plan picks, or close if measurement shows the aggregate cost is negligible. Depends on `IslandResidentMemoryScalingStrategy.md` for the lifecycle decision |
| 6 | [Graphics/IslandResidentMemoryScalingStrategy.md](Graphics/IslandResidentMemoryScalingStrategy.md) | Large | 4 | 3 | 3 | 4 | Predecessor `IslandResidentMemoryScaling.md` landed step 1 only: `[DEBUG-resmem]` boot-time LOG instrumentation at the end of `IslandTerrain::CreateClientMeshBuffers` reporting per-template + aggregate mesh CPU / mesh GPU / heightmap / hull bytes. Once a Debug client boot log is captured, pick a strategy (a2 = LRU mesh + zero indirect entry; b = lazy `kIsland` chunk load; c = trim route table; d = compress/share; e = close with justification) and implement. Remove the `[DEBUG-resmem]` block when done |

### Reference / Index Documents

Meta/overview docs that are never executed as plans — excluded from the priority walk and orphan scans.

| Document | Purpose |
|----------|---------|
| [Graphics/ShaderReview/00_Overview.md](Graphics/ShaderReview/00_Overview.md) | Shader-review effort overview and cross-cutting-findings index; the per-subsystem plans it references were either executed-and-deleted or never authored |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `Network/Refactor_FileSizeTriage.md` defers `ClientSession.cpp` / `ClientReceive.cpp` / `ServerReceive.cpp` pending upstream refactors; run only after `SendBoilerplate` lands, then re-measure.
- `Network/Architecture_LayerViolations.md` **Plan A has landed and been removed** (the six one-way debug save/load/reset/replay/pause packets are now game-layer `GamePacketType` handled in `game::ServerSession::ParseReceivedGamePackets`). **Plan B has landed and been removed** — the timespeed flow (`kClientTimespeedRequest` + `kServerTimespeedUpdate`) is now reclassed to `GamePacketType`; handlers and broadcast live in `game::ClientSession::PollNetwork` and `game::ServerSession::{ParseReceivedGamePackets, BroadcastTimespeedIfChanged, SendTimespeedToNewClient}`; engine `GameBase::ServerUpdate` and `Server::ClientHello` invoke the game-session helpers. CoordFrames / Frame factory / Frame version / BufferFullFrame items were dropped per the user-confirmed layer-violation policy (engine reading game globals to access engine-base members is by design, not a violation).
- `Engine/Architecture_GameSaveLoadRelocation.md` **has landed and been removed** — `GameSaveLoad` relocated to the game layer (`game::GameSaveLoad` in `Projects/BrokenEngineSandbox/Source/Save/`, owned by `game::Game`); engine `GameBase::ServerUpdate` now drives it via `game::gpGame->mGameSaveLoad.X()`, and the `friend` + engine-side member were removed.
- `Graphics/SampledNormalsSnapStepAudit.md` may touch `Water.frag` (comment-only update to the precision-pact block at lines 103-107) and `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (modulus widening for the two narrow sliders if Option 3 is chosen in the grill). If the comment update lands, coordinate with `Graphics/ShaderReview/02_Water.md` since both edit the same fragment shader; pure UI-side opt-in (no modulus change) has no shader overlap.
- Per-frame multi-island system (5 phases) has LANDED. Phase 5 (LRU eviction, dynamic subscription-driven loading, neutral slot-0 placeholder) executed in this session — no remaining follow-ups. See `Engine/Source/Frame/CLAUDE.md` for the current architecture description.
- `Graphics/IslandNavContourResidency.md` depends on `Graphics/IslandResidentMemoryScalingStrategy.md`. The NavContour residency lifecycle should mirror whatever strategy the predecessor picks (lazy chunk-load / LRU / cap / close). Do not pick a NavContour strategy before the predecessor lands. Both plans are gated on captured `[DEBUG-resmem]` boot-log data from a Debug client run.

### Cross-directory dependencies (plans in `Documents/Features/`)

- `Graphics/ShaderReview/02_Water.md` should land BEFORE any `Documents/Features/Graphics/ocean-phase-*.md` plan — both edit `Water.frag`, and landing defensive fixes first keeps ocean-phase diffs focused on new shading terms.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`Engine/Source/Frame/NavBuild.{h,cpp}` / `NavQuery.cpp` + `Projects/.../Players/PlayersNavigation.cpp`**: `Frame/NavBuildDebugCrossingCheckPerf.md` (gates/accelerates the `BuildCellNavData` crossing loop, fixes the `NavBuild.cpp` LOG-float sites + the `ComputeNavigation` dead local), `Frame/EvaluateRecastDetourNavmesh.md` (read-only eval of the same `BuildCellNavData`/`NavQueryDirection`/`NavQuerySnapToNavigable` interfaces — no edits land, so it never conflicts; if it ever returns "go", the integration follow-up would supersede the crossing-check plan since Detour owns the builder)
- **`ExportJobs/Scene/Scene{Animation,Skeleton,Vertices}Loader.{h,cpp}`**: `Refactor_LongParamsAndFlags.md` (paths reflect post-rename locations from the IncludeGraph + CohesionSplits session; `Refactor_DedupLoops.md` landed and was removed)
- **`Main.cpp`**: `Architecture_MainOrchestration.md` (extract generators), `Refactor_MigratorElevationFormat.md` (orphaned-elevation skip/delete in `MigrateLegacyIntermediate`), `Fix_ExportJobSortTotalOrder.md` (`RunExportJobs<T>` sort comparator)
- **`BakeIslandIntermediates.{cpp,h}`**: `DataPacker/Refactor_SplitBakeIslandIntermediates.md` (split into three TUs — `BakeIslandIntermediates.cpp` orchestration, `BakeRoute.cpp`, `ProcessBakedRegion.cpp` — sharing a private `BakeIslandIntermediatesInternal.h` for the `IslandBakeContext`/`RegionBounds`/`LeafTarget`/`BakeOutput` structs and shared constants), `DataPacker/GuardEmptyRouteAllChunksRejected.md` (insert `iWrittenLeaves == 0` throw in `BakeRoute` before the `kpcSplitVersionFile` stamp; bump `kiSplitVersion` by 1)
- **`ExportShader.cpp`**: `Refactor_LongParamsAndFlags.md` (WriteBinding struct)
- **`Texture.{h,cpp}` (DataPacker)**: `Refactor_LongParamsAndFlags.md` (Flags), `Engine/IslandColorBc7SrgbBlock.md` (`VK_FORMAT_BC7_SRGB_BLOCK` added alongside `VK_FORMAT_BC7_UNORM_BLOCK` in `:274`/`:393`/`:410`/`:514-515`)
- **`DataPacker/Source/ExportJobs/ExportIsland.{cpp,h}`**: `Engine/IslandColorBc7SrgbBlock.md` (`kpcIslandColor` extension + `Save` format → SRGB, `GetVersion()` bump), `Frame/IslandValidAreaHullConvexityAssert.md` (CCW + convexity assert after `BuildValidAreaHull`)
- **`Projects/.../Graphics/Camera.cpp`**: `Bugfix_ZoomStutterDiagFloatSpecs.md` (TEMP zoom-stutter diagnostic block — delete or convert float specs to `common::Wb`), `Frame/ScaleEngineToMeters.md` (`kfEyeHeightMin`/`kfEyeHeightMax` literals)
- **`Engine/Data/Shaders/ShaderLayoutsBase.h` + `RenderTargetTexturesLighting.cpp`**: `Graphics/DebugTextureSlotRegistry.md` (replace `kiMaxDebugTextures = kiMaxSpreadPasses + 16` magic with derived constant via `DebugSlotCategory` enum)
- **`Engine/Source/Graphics/Objects/PipelineCreator.cpp` + `Pipeline.{h,cpp}` + `Managers/CommandBufferRecordMain.cpp` + `Render/MainUniforms.cpp` + `Engine/Data/Shaders/Water/WaterDisplacement.comp`**: `Graphics/WaterDisplacementIndirectCompute.md` (add `kIndirectHostVisible|kCompute` branch, `WriteIndirectComputeBuffer` helper, and `fTerrainElevation >= 0.0f` over-land skip)
- **`ExportJob.{h,cpp}`**: `Architecture_ExportJobContract.md` (Version helper + concept + default moves)
- **`Network/Client/ClientSend.cpp`**: `Refactor_SendBoilerplate.md`
- **`Network/Client/ClientSession.cpp` (game)**: `Refactor_SendBoilerplate.md`, `Refactor_HotPathAllocations.md`, `Refactor_OversizedFunctions.md`, `Refactor_ClientSessionCanSendDelegation.md` (touches sibling `ClientSession.h` only — co-located here so a `ClientSession`-area session can absorb it)
- **`Network/Server/ServerFleetManager.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ClientReceive.cpp`**: `Refactor_HotPathAllocations.md`
- **`Network/Client/Client.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/Server.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ReconcileReplay.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/ServerSession.cpp` (game)**: `Refactor_OversizedFunctions.md`, `Refactor_AllocationTrackingAudit.md`
- **`TweaksScreen.cpp`/`TweaksScreen<Tab>.cpp`**: `TweaksSliderMapDebugAudit.md`
- **`Engine/Source/Frame/IslandTerrain.cpp`**: `Frame/ScaleEngineToMeters.md` (drop the three `kfMetersToUnits` multiplications at `:49`/`:50`/`:147`/`:395`/`:818` as part of the final-cleanup batch), `Graphics/IslandResidentMemoryScalingStrategy.md` (extend `EvictionSweep`/`RestorationSweep` to per-template mesh buffers under strategy (a2); remove the `[DEBUG-resmem]` instrumentation block at the end of `CreateClientMeshBuffers`), `Graphics/IslandNavContourResidency.md` (server-side `mNavContour` lifecycle in `WaitForElevationMaps` / `~IslandTerrain`)
- **`Engine/Source/Graphics/Managers/PipelineManager.{h,cpp}`**: `Graphics/TerrainPipelineBindingConstants.md` (add `TerrainPipelineBindings` namespace in the header; annotate the `kPipelineTerrain` descriptor-info table in the cpp), `Graphics/ElevationMaxBlendFormatGuard.md` (reads the two `kMax` elevation pipelines that introduced the R16_SFLOAT blend dependency — referenced, not modified)
- **`Engine/Source/Graphics/GraphicsUtils.{h,cpp}` + `Engine/Source/Graphics/Managers/InstanceManager.cpp` + `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` (engine)**: `Graphics/ElevationMaxBlendFormatGuard.md` (add `SupportsColorAttachmentBlend` mirroring `SupportsLinearFilter`; call it at device-format setup; the elevation RTT `.Create` calls are the protected consumer)
- **`Engine/Source/Graphics/Objects/Texture.{h,cpp}` + `Engine/Source/Graphics/Managers/InstanceManager.cpp` (engine)**: `Graphics/ClearEmptyTexturesToSafeValues.md` (clear no-data sampled color images in `Texture::Create`'s null-`rDataFunction` branch + remove the `TransitionUndefinedToReadOnly` suppression in `DebugUtilsCallback`)
