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

60 first-party GLSL shaders reviewed via one `/glsl-review` Opus subagent per file. Original distribution: 9 Quick Win + 3 Medium across 12 subsystem plans. **Five performance-neutral bugfix plans landed this session** via parallel Opus subagents: `03_Terrain`, `05_Quads`, `07_Objects` fully executed and deleted; `09_Wind` and `12_Misc` partially executed (descriptor-set audit and `nonuniformEXT` audit deferred — both require C++ coordination). `08_Smoke` landed in a follow-up session (full execution, deleted). Remaining distribution: 2 Quick Win + 5 Medium across 7 subsystem plans. No `inverse()` driver-hang violations. Remaining recurring hazards: (1) unguarded `normalize()` on possibly-zero vectors (Model, Water, Particles, Shadow), (2) `pow(neg, non-integer)` → NaN (Lighting — 06), (3) implicit-LOD `texture()` in compute (Shadow, ObjectShadowsBlur), (4) missing `writeonly` on every blur output `image2D`. Standalone hard bugs: `Terrain.frag:76` (landed), `Log.vert:38` (landed), `ParticlesUpdate.comp:100` (non-atomic RMW race — still pending in `04_Particles.md`). Overview at `Graphics/ShaderReview/00_Overview.md`; per-subsystem plans at `Graphics/ShaderReview/{02,04,06,09,10,11,12}_*.md`.

## Plans

| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
|---|------|------|--------|--------|-------|-------|-------|
| 1 | [Graphics/ExtendModelPipelineRecreateToShadowAndSmoke.md](Graphics/ExtendModelPipelineRecreateToShadowAndSmoke.md) | Small | 2 | 3 | 1 | 0 | Fix A (this session) added `ModelPipeline::Recreate()` calls to the `kLightingTextures` stage of `RecreatePipelineGroups`. The same stale-`VkImageView` bug class exists for `kShadowTextures` (auto-appended `mShadowBlurTexture`) and `kSmokeTextures` (auto-appended `mSmokeTextureOne`). Extend the recreate to both flags via a single private helper. |
| 2 | [Graphics/ShaderReview/04_Particles.md](Graphics/ShaderReview/04_Particles.md) | Medium | 4 | 5 | 3 | 2 | Billboard singularity on top-down RTS camera (`SquareParticlesRender.vert:50`, `LongParticlesRender.vert:53`); `ParticlesUpdate.comp:100` non-atomic RMW race on allocation bitmap (make `atomicAnd`); `ParticlesSpawn.comp` uint8/uint16 header mismatch; `ParticlesRender.frag:36` `pow` base clamp; double-integration on terrain bounce |
| 3 | [Graphics/ShaderReview/10_Shadow.md](Graphics/ShaderReview/10_Shadow.md) | Medium | 4 | 4 | 2 | 2 | `Shadow.comp` `acos` clamp, `normalize(vec3(x,0,0))` fix, `textureLod` in compute, signed-vs-unsigned loop counter with negative increment; shrink 512-thread blur workgroups to 64-256; add `writeonly` to every blur output image; fix inverted `fInvSigma` semantics in `ShadowBlurV` |
| 4 | [WrapperArrayCollapse.md](WrapperArrayCollapse.md) | Medium | 3 | 3 | 2 | 2 | Collapse 6 files of 4× per-target wrapper duplications into `Wrapper[kRenderTargetCount]` arrays indexed by enum. |
| 5 | [Engine/HeightLerpWrapperQuartet.md](Engine/HeightLerpWrapperQuartet.md) | Medium | 3 | 2 | 1 | 2 | Collapse 5 height-lerped `Wrapper` quartets (4 audio + 1 lighting = 20 globals) into `HeightLerpWrapperQuartet` struct + free `LerpAtHeight`; supersedes orphan `Graphics/CameraHeightLerpHelper.md`. |
| 6 | [Graphics/DynamicIslandLoadingFollowups.md](Graphics/DynamicIslandLoadingFollowups.md) | Medium | 3 | 3 | 2 | 2 | Two IslandTerrain/TextureManager follow-ups from Phase-5 dynamic-loading session: (1) reset `miTextureSlot`/`mbGpuResident` on device-lost TextureManager recreate — silently strands BOTH `kPipelineTerrainElevation` and `kPipelineShadowElevation` on placeholder otherwise; (2) wrap or document `AcquireTextureSlot` cold-path in `Islands::UpdateActiveIslands` for allocation tracker. |
| 7 | [Graphics/CoLocatePerSlotDescriptorRegistration.md](Graphics/CoLocatePerSlotDescriptorRegistration.md) | Medium | 3 | 3 | 2 | 2 | Co-locate per-slot bindless-array descriptor registration with the pipeline declaration in `PipelineManager.cpp` instead of hand-authoring it in `IslandTerrain::AcquireTextureSlot`. Eliminates the split-across-files bug class that caused commit `09fb128` to silently drop `kPipelineShadowElevation` registration; the next pipeline added against `mElevationTextures` will not need a new hand-authored line. |
| 8 | [Frame/Refactor_NavBuildSplit.md](Frame/Refactor_NavBuildSplit.md) | Medium | 3 | 3 | 2 | 2 | Split 1262-line `NavBuild.cpp` into contour / polygon / visibility translation units; fix oversized `ContourEdge` reserve (currently `width*height` instead of perimeter-sized) while down there. Pre-existing debt surfaced by the auto-crop islands review. |
| 9 | [Graphics/ShaderReview/DescriptorSetComputeSweep.md](Graphics/ShaderReview/DescriptorSetComputeSweep.md) | Medium | 2 | 2 | 2 | 2 | Add explicit `set = N` qualifiers to ~14 compute shaders (~50 bindings) across Wind/Smoke/Shadow/Lighting so they match the documented descriptor-set convention. Mechanical pure-GLSL sweep. **Blocked on `Engine/Architecture_MultiSetComputePipelines.md`.** |
| 10 | [Engine/Architecture_MultiSetComputePipelines.md](Engine/Architecture_MultiSetComputePipelines.md) | Architectural | 4 | 4 | 3 | 3 | Bring compute pipelines into structural parity with graphics: split SPIR-V-reflected set 0 / set 1 in `CreateComputePipeline`, share `TextureDescriptors`' global Set 0 with compute (widen stage flags), update the 5 compute bind sites via a new `BindComputeDescriptorSets` helper. Unblocks `set = 1` migration of ~50 compute-shader bindings. |
| 11 | [DataPacker/Refactor_BakeIslandIntermediatesSplit.md](DataPacker/Refactor_BakeIslandIntermediatesSplit.md) | Medium | 3 | 2 | 2 | 3 | `BakeIslandIntermediates.cpp` grew to 1221 lines after the beach-band mesh subdivision pass landed (over the 1000-line `/reduce-file` threshold). Extract the ~300-line subdivision block to a new `SubdivideBeachBand.{cpp,h}` TU as `BeachSubdivider` struct + `SubdivideBeachBand(...)` free function. Pure code motion (no algorithm change, no `kiBakeVersion` bump); MeshProcessed.bin byte-identical for fixed-seed standard archetype. |
| 12 | [DataPacker/Architecture_MainOrchestration.md](DataPacker/Architecture_MainOrchestration.md) | Medium | 4 | 4 | 3 | 3 | Extract `DataTypes.h` + `Data.h` generators, unify three parallel lists, single-MessageBox failure path, IBL file split |
| 13 | [DataPacker/Refactor_DedupLoops.md](DataPacker/Refactor_DedupLoops.md) | Medium | 4 | 4 | 3 | 3 | `CollectBindings` helper (6 SPIRV-Cross blocks), flatten O(n²) stage_inputs, TEXCOORD dedup, optional `common::ReadFile` |
| 14 | [Network/Architecture_LayerViolations.md](Network/Architecture_LayerViolations.md) | Architectural | 4 | 4 | 3 | 3 | Reclass save/load/reset/replay/pause/timespeed packets as game-layer; virtuals on `ClientSessionBase`/`ServerSessionBase` for CoordFrames mutation and Frame factory |
| 15 | [Network/Refactor_HotPathAllocations.md](Network/Refactor_HotPathAllocations.md) | Medium | 4 | 4 | 3 | 3 | Migrate ~10 per-tick `std::vector`/`std::unordered_map`/`ostringstream` allocations to `gpThreadLocal->mWorkbuffer` |
| 16 | [Graphics/ShaderReview/06_Lighting.md](Graphics/ShaderReview/06_Lighting.md) | Medium | 4 | 4 | 3 | 3 | `AreaLight`/`PointLight` missing scalar qualifier on `lightOccupancyBuffer` + `+0.4f` rounding-bias bug (should be `+0.5f` or flat int varying); `LightingSpread.frag` NaN/Inf guards on `fNorm`/`fRingFalloff` + per-fragment cos/sin hot path; `LightingBlur{H,V}` NaN at radius=0 + `writeonly` + `textureLod` + inverted sigma semantics in V pass; `LightCombine.comp` `pow` + `fPassCount` guards |
| 17 | [Graphics/WaterFragmentSetAudit.md](Graphics/WaterFragmentSetAudit.md) | Medium | 3 | 2 | 2 | 3 | Audit + move the 9 global samplers (`pLightingSamplers`, `shadowTextureSampler`, `objectShadowsTextureSampler`, `elevationTextureSampler`, `skyboxSampler`, `noiseTextureSampler`, `depthLutSampler`, `smokeSampler`, `ambientLightingSampler`) in `Water.frag` from `set = 1` to `set = 0` to match the documented graphics-pipeline convention; update sibling fragment shaders (`Terrain.frag`, `Model.frag`) that share these bindings + the C++ pipeline-layout / write sites. `pWaterNormalSamplers[17]` stays per-pipeline. Split off from `02_Water.md`. |
| 18 | [Frame/FrameRelativePositions.txt](Frame/FrameRelativePositions.txt) | Large | 5 | 5 | 3 | 3 | Convert world-absolute to frame-relative positions. Fixes float precision degradation at distance (~1cm jitter at 140 frames from origin). |
| 19 | [DataPacker/Refactor_ExportSceneDecompose.md](DataPacker/Refactor_ExportSceneDecompose.md) | Medium | 5 | 4 | 3 | 4 | Decompose `PreExport` (~265 lines) and `MainExport` (~280 lines) + one redundant-check simplification |
| 20 | [Network/Refactor_OversizedFunctions.md](Network/Refactor_OversizedFunctions.md) | Architectural | 5 | 4 | 3 | 4 | Decompose `ReconcileCoord` (207 lines), `WriteFleetData`/`ReadFleetData`, `ResetClientsForLoad`, `NewClients`, `Client::Poll`/`Server::Poll` sim/direct dedup |
| 21 | [DataPacker/Refactor_LongParamsAndFlags.md](DataPacker/Refactor_LongParamsAndFlags.md) | Medium | 4 | 3 | 3 | 4 | `Texture` bools → `Flags<TextureOptions>`, `LoadVertices`/`LoadAnimations`/`WriteBinding` param structs |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `DataPacker/Refactor_ExportSceneDecompose.md` should precede `DataPacker/Refactor_LongParamsAndFlags.md` — both touch `ExportJobs/Scene/SceneVerticesLoader.cpp`/`SceneAnimationLoader.cpp` (renamed from `ExportSceneVertices.cpp`/`ExportSceneAnimation.cpp` in the IncludeGraph + CohesionSplits session), and the decomposition will shift line numbers that the long-params plan references.
- `Network/Refactor_FileSizeTriage.md` defers `ClientSession.cpp` / `ClientReceive.cpp` / `ServerReceive.cpp` pending upstream refactors; run only after `SendBoilerplate` lands, then re-measure.
- `Network/Architecture_LayerViolations.md` touches `ServerReceive.cpp`, `ClientSessionBase.cpp`, `ServerSessionBase.cpp`, `Client.cpp`, `Server.cpp`, `ClientReceive.cpp` — small-scale refactors on those files have already landed (Guard, DeadCode, WorkbufferRaii) so its diff can focus on the semantic change.
- `Graphics/ShaderReview/DescriptorSetComputeSweep.md` depends on `Engine/Architecture_MultiSetComputePipelines.md` — the sweep adds `set = 1` qualifiers to ~50 compute-shader bindings, but `CreateComputePipeline` in `PipelineCreator.cpp:608-623` currently hard-codes a single-set layout and the 5 compute `vkCmdBindDescriptorSets` call sites only bind 1 set. Running the sweep without the engine plan landed would cause `VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358` validation errors. Land the engine plan first; the sweep then becomes pure-GLSL.
- The remaining descriptor-set audit items in `Graphics/ShaderReview/02_Water.md` (fragment-shader set 0 vs set 1 question) and `Graphics/ShaderReview/06_Lighting.md` (lightOccupancyBuffer scalar qualifier + compute blur set assignments) are independent of the `DescriptorSetComputeSweep`. The Lighting blur `.comp` files are owned by the sweep; everything else in `06_Lighting.md` (correctness, `writeonly`, `textureLod`, NaN guards, sigma algebra) stays with that plan.
- `Graphics/SampledNormalsSnapStepAudit.md` may touch `Water.frag` (comment-only update to the precision-pact block at lines 103-107) and `Engine/Source/Graphics/Render/GlobalUniforms.cpp` (modulus widening for the two narrow sliders if Option 3 is chosen in the grill). If the comment update lands, coordinate with `Graphics/ShaderReview/02_Water.md` since both edit the same fragment shader; pure UI-side opt-in (no modulus change) has no shader overlap.
- Per-frame multi-island system (5 phases) has LANDED. Phase 5 (LRU eviction, dynamic subscription-driven loading, neutral slot-0 placeholder) executed in this session — no remaining follow-ups. See `Engine/Source/Frame/CLAUDE.md` for the current architecture description.

### Cross-directory dependencies (plans in `Documents/Features/`)

- `Graphics/ShaderReview/02_Water.md` should land BEFORE any `Documents/Features/Graphics/ocean-phase-*.md` plan — both edit `Water.frag`, and landing defensive fixes first keeps ocean-phase diffs focused on new shading terms.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`ExportScene.cpp`**: `Refactor_ExportSceneDecompose.md`
- **`ExportJobs/Scene/Scene{Animation,Skeleton,Vertices}Loader.{h,cpp}`**: `Refactor_DedupLoops.md`, `Refactor_LongParamsAndFlags.md` (paths reflect post-rename locations from the IncludeGraph + CohesionSplits session that landed both plans together; consuming plans must refresh their references on next pickup)
- **`Main.cpp`**: `Architecture_MainOrchestration.md` (extract generators), `Refactor_MigratorElevationFormat.md` (orphaned-elevation skip/delete in `MigrateLegacyIntermediate`)
- **`ExportShader.cpp`**: `Refactor_DedupLoops.md` (CollectBindings + O(n²) fix), `Refactor_LongParamsAndFlags.md` (WriteBinding struct)
- **`Texture.{h,cpp}`**: `Refactor_LongParamsAndFlags.md` (Flags)
- **`ExportJob.{h,cpp}`**: `Architecture_ExportJobContract.md` (Version helper + concept + default moves)
- **`Network/Client/ClientSend.cpp`**: `Refactor_SendBoilerplate.md`
- **`Network/Client/ClientSession.cpp` (game)**: `Refactor_SendBoilerplate.md`, `Refactor_HotPathAllocations.md`, `Refactor_OversizedFunctions.md`, `Refactor_ClientSessionCanSendDelegation.md` (touches sibling `ClientSession.h` only — co-located here so a `ClientSession`-area session can absorb it)
- **`Network/Server/ServerReceive.cpp`**: `Architecture_LayerViolations.md`
- **`Network/Server/ServerFleetManager.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ClientReceive.cpp`**: `Refactor_HotPathAllocations.md`, `Architecture_LayerViolations.md`
- **`Network/Client/Client.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/Server.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ReconcileReplay.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/ServerSession.cpp` (game)**: `Refactor_OversizedFunctions.md`, `Refactor_AllocationTrackingAudit.md`
- **`TweaksScreen.cpp`/`TweaksScreen<Tab>.cpp`**: `TweaksSliderMapDebugAudit.md`
- **`Engine/Source/Frame/IslandTerrain.cpp`**: `Graphics/DynamicIslandLoadingFollowups.md`, `Graphics/CoLocatePerSlotDescriptorRegistration.md` (the architectural fix; land the device-lost reset from Followups Follow-up 1 first, then this plan collapses the `AcquireTextureSlot` hand-authored registration block)
