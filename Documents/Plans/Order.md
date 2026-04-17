# Plan Execution Order

All plans — bugfixes, changes, enhancements, and future features — in a single list sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

`Tier` column is an informal size/risk descriptor (**Quick Win** / **Small** / **Medium** / **Large** / **Architectural**); it does not affect score or ordering.

## Debt Score (DataPacker, this run): MODERATE

Distribution: 1 Quick Win + 6 Medium + 1 Architectural. No critical bugs, but `ExportScene.cpp` is approaching the `/reduce-file` threshold, `RunExportJobs<T>` duck-types a static contract, and a public header pulls tinygltf into unrelated TUs.

## Debt Score (Network, this run): MODERATE

Distribution: 7 Quick Win + 6 Medium + 2 Architectural across 15 plans. Network subtree is structurally sound — the main concentrations are (1) engine → game surgery in `ServerReceive.cpp` save/load/timespeed handlers and the `*SessionBase.cpp` CoordFrames mutation, (2) one replay-determinism bug (fleet RNG re-seeded from wall clock on load), and (3) mechanical duplication across ~20 `Send*` methods, ~14 connected-gate sites, and ~12 handshake-gate sites. `ServerFleetManager.cpp` (808 lines) and `ReconcileReplay.cpp` (668 lines) are genuine `/reduce-file` candidates.

## Debt Score (Graphics — Shaders, this run): MODERATE

60 first-party GLSL shaders reviewed via one `/glsl-review` Opus subagent per file. Original distribution: 9 Quick Win + 3 Medium across 12 subsystem plans. **Five performance-neutral bugfix plans landed this session** via parallel Opus subagents: `03_Terrain`, `05_Quads`, `07_Objects` fully executed and deleted; `09_Wind` and `12_Misc` partially executed (descriptor-set audit and `nonuniformEXT` audit deferred — both require C++ coordination). Remaining distribution: 4 Quick Win + 3 Medium across 9 subsystem plans. No `inverse()` driver-hang violations. Remaining recurring hazards: (1) unguarded `normalize()` on possibly-zero vectors (Model, Water, Particles, Shadow), (2) `pow(neg, non-integer)` → NaN (Lighting — 06), (3) implicit-LOD `texture()` in compute (Shadow, ObjectShadowsBlur), (4) missing `writeonly` on every blur output `image2D`. Standalone hard bugs: `Terrain.frag:76` (landed), `Log.vert:38` (landed), `ParticlesUpdate.comp:100` (non-atomic RMW race — still pending in `04_Particles.md`). Overview at `Graphics/ShaderReview/00_Overview.md`; per-subsystem plans at `Graphics/ShaderReview/{01,02,04,06,08,09,10,11,12}_*.md`.

## Plans

| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
|---|------|------|--------|--------|-------|-------|-------|
| 1 | Graphics/SkyboxRenderPass.txt | Small | 1 | 4 | 0 | -3 | Fullscreen skybox draw (cubemap exists, only used for reflections). Star field at night, sun/moon disc using existing fSunAngle. |
| 2 | Graphics/HdrResolveAndColorGrading.txt | Medium | 3 | 5 | 1 | -1 | Render to F16 intermediate, ACES tone mapping resolve (dead code exists in Model.frag), 3D LUT color grading. Unlocks all HDR effects. |
| 3 | Graphics/ReverseZDepth.txt | Small | 2 | 4 | 1 | -1 | Flip min/max depth, GREATER_EQUAL compare, clear to 0. Eliminates z-fighting. Contained change. |
| 4 | Graphics/HeatDistortionAndShockwave.txt | Small | 2 | 3 | 0 | -1 | Screen-space UV distortion near heat/explosions. Displacement-map shockwave for explosions. |
| 5 | Engine/GradientParticlesAndCurlNoise.txt | Small | 2 | 3 | 0 | -1 | Lifetime color gradient for particles (white->orange->smoke). Curl noise in smoke spread shader. |
| 6 | Graphics/TerrainSnowInGrooves.txt | Small | 1 | 2 | 0 | -1 | Curvature-aware snow in Terrain.frag using AO/normal derivatives instead of color-based detection. |
| 7 | Misc/SpirvOptIntegration.txt | Small | 1 | 2 | 0 | -1 | Add spirv-opt pass to DataPacker shader compilation. Reduces shader size, improves driver compile. |
| 8 | Graphics/ocean-phase-3-foam.md | Small | 2 | 4 | 1 | -1 | Height+shore+slope foam factor modulated by noise pattern, blended pre-shadow in `Water.frag`; ~10 new floats + `TweaksScreenWaterFoam.cpp`. Depends on Phase 1. |
| 9 | Graphics/ocean-phase-4-sun-glitter.md | Small | 1 | 3 | 1 | -1 | Noise-jittered micro-normal sparkle via NdotH smoothstep threshold added to Ward specular; ~5 new floats. Depends on Phase 1. |
| 10 | Graphics/ocean-phase-6-sss.md | Small | 1 | 2 | 0 | -1 | Wave-height SSS with view-dependent sun alignment (`pow(1-VdotL)`) additive turquoise tint; ~8 new floats. Depends on Phase 1. |
| 11 | [Network/TransferReconciliationBarrier.txt](Network/TransferReconciliationBarrier.txt) | Medium | 2 | 3 | 1 | 0 | Phase A diagnostic logs for Transfer-correlated CRC mismatches on kChina |
| 12 | [DataPacker/Refactor_QuickWins.md](DataPacker/Refactor_QuickWins.md) | Quick Win | 1 | 2 | 1 | 0 | Dead constants, unreachable branch, empty loop body, OPTIMIZE_SHADERS → `if constexpr`, indent fix, redundant dtor |
| 13 | [Network/Architecture_FleetRngDeterminism.md](Network/Architecture_FleetRngDeterminism.md) | Medium | 3 | 5 | 2 | 0 | Thread seed through save/load so replay playback matches record (ResetState currently wall-clock-seeds) |
| 14 | [Network/Architecture_UnusedIncludes.md](Network/Architecture_UnusedIncludes.md) | Quick Win | 1 | 2 | 1 | 0 | Delete Blasters/Missiles/Spaceships includes from `ReconcileReplay.cpp` (only Players.h is used) |
| 15 | [Network/Refactor_DeadCode.md](Network/Refactor_DeadCode.md) | Quick Win | 1 | 2 | 1 | 0 | Delete dead `Client.cpp` ctor locals, `PollLANDiscovery` bool→void, `[[maybe_unused]] spawnCoord` audit |
| 16 | [Network/Refactor_WorkbufferRaii.md](Network/Refactor_WorkbufferRaii.md) | Medium | 2 | 3 | 1 | 0 | Add `common::ScopedWorkbufferFrame` RAII + migrate 26 manual Push/Pop pairs (precursor to SendBoilerplate) |
| 17 | [Network/Refactor_GuardConsolidation.md](Network/Refactor_GuardConsolidation.md) | Quick Win | 2 | 3 | 1 | 0 | `Client::CanSend()` + `Server::FindHandshakenClient()` + delete 5 unreachable `iSize < 1` checks |
| 18 | [Network/Refactor_AllocationTrackingAudit.md](Network/Refactor_AllocationTrackingAudit.md) | Quick Win | 1 | 2 | 1 | 0 | Add missing `// Heap:` comments at ~25 sites, remove cosmetic guards, standardize `suppressAllocationTracking` name |
| 19 | [Network/Refactor_CoordScratchFlags.md](Network/Refactor_CoordScratchFlags.md) | Quick Win | 1 | 2 | 1 | 0 | `CoordScratch` 5 bools → `common::Flags<ReconcileScratchFlags>` (1 byte vs 5 bytes) |
| 20 | [Graphics/ShaderReview/01_Model.md](Graphics/ShaderReview/01_Model.md) | Quick Win | 2 | 3 | 1 | 0 | `Model.frag` tangent Gram-Schmidt normalize guards (lines 171, 176), dead `fSunDot`, `pow(x,5)` expand, prefer `ReadLighting` helper |
| 21 | [Graphics/ShaderReview/08_Smoke.md](Graphics/ShaderReview/08_Smoke.md) | Quick Win | 1 | 2 | 1 | 0 | `pow` defensive clamps (`Smoke.frag:31`, `SmokeSpreadTwo.comp:69`), `readonly` on `SmokeOccupancyDilate` buffer, replace magic `/8` with `kiComputeTileSize` |
| 22 | [Graphics/ShaderReview/09_Wind.md](Graphics/ShaderReview/09_Wind.md) | Medium | 2 | 2 | 2 | 2 | **Partial**: textureLod swaps + denormal epsilon landed. Remaining: descriptor-set `set=1` audit (coordinate with `02_Water` + `06_Lighting` + C++ pipeline-layout side) |
| 23 | [Graphics/ShaderReview/11_DebugUi.md](Graphics/ShaderReview/11_DebugUi.md) | Quick Win | 1 | 1 | 0 | 0 | `DebugRenderBillboard.vert` redundant normalize, `ProfileText.frag` dead varyings, `UiDepthPrepass.vert` add `invariant gl_Position` if any pass relies on depth-equal |
| 24 | [Graphics/ShaderReview/12_Misc.md](Graphics/ShaderReview/12_Misc.md) | Quick Win | 1 | 1 | 0 | 0 | **Partial**: `Log.vert` w=0 + vertex-gate landed; `DebugTexture.frag` uninit output + P-S1 + f4Scaled guards landed. Remaining: `Log.vert` unwritten flat outputs decision (assign or remove), `DebugTexture.frag` `nonuniformEXT` audit, minor perf cleanups |
| 25 | Graphics/WaterFoamAndRefraction.txt | Medium | 3 | 4 | 1 | 0 | Foam/whitecaps from wave steepness, refraction via UV-distorted scene sampling. |
| 26 | Audio/AdaptiveMusic.txt | Small | 2 | 2 | 0 | 0 | GetNextMusicTrack considers game state (combat, location). Crossfade infrastructure exists. |
| 27 | Engine/WindSmokeAdvection.txt | Small | 1 | 1 | 0 | 0 | Add wind advection parameter for smoke simulation. |
| 28 | Network/RemoteEntityInterpolation.txt | Medium | 3 | 4 | 1 | 0 | Snapshot-based interpolation for remote entities. |
| 29 | Graphics/ocean-phase-5-caustics.md | Small | 2 | 3 | 1 | 0 | Shallow-water two-layer noise `min()` cellular caustics modulated by depth smoothstep, added to sea-floor albedo; ~9 new floats. Depends on Phase 1. |
| 30 | Graphics/ocean-phase-7-legacy-recovery.md | Small | 2 | 3 | 1 | 0 | Re-integrate noise color-zone modulation, depth-LUT blend slider, and ambient-floor clamp on top of Phase 1 PBR; ~9 new floats. Depends on Phase 1. |
| 31 | [DataPacker/Architecture_IncludeGraph.md](DataPacker/Architecture_IncludeGraph.md) | Medium | 3 | 4 | 2 | 1 | Move `tiny_gltf.h` out of public headers (4 files), delete unused skeleton include, delete duplicate `<crtdbg.h>` block |
| 32 | [DataPacker/Architecture_ExportJobContract.md](DataPacker/Architecture_ExportJobContract.md) | Medium | 3 | 4 | 2 | 1 | `Version()` helper on base, delete dead `utils::ChunkHeader` fwd decl, `IsExportJob` concept, remove empty `AddToHeader` hook, default moves |
| 33 | [Network/Refactor_FileSizeTriage.md](Network/Refactor_FileSizeTriage.md) | Quick Win | 2 | 2 | 1 | 1 | Run `/reduce-file` on `ServerFleetManager.cpp` (808) and `ReconcileReplay.cpp` (668); defer other borderline files |
| 34 | [Network/Refactor_ClientReceiveStateMachine.md](Network/Refactor_ClientReceiveStateMachine.md) | Medium | 2 | 3 | 2 | 1 | Extract `ClassifyFullState` + 3 subscribe-accept helpers from two 55-60-line handlers |
| 35 | [Network/Architecture_HeaderSplits.md](Network/Architecture_HeaderSplits.md) | Medium | 2 | 2 | 1 | 1 | Split `NetworkDiscovery.{h,cpp}` into Responder/Scanner files (BT_CLIENT/BT_SERVER vcxproj rule) |
| 36 | [Network/Refactor_SendBoilerplate.md](Network/Refactor_SendBoilerplate.md) | Medium | 3 | 4 | 2 | 1 | Variadic `SendSimplePacket` template collapses 12 `Client::Send*` + 6 `ClientSession::Send*` methods; ~300 lines deleted |
| 37 | Misc/FlatContainersOptimization.txt | Small | 3 | 3 | 1 | 1 | Replace std::unordered_map with std::flat_map in hot paths. **BLOCKED**: MSVC 14.50 lacks \<flat_map\>. |
| 38 | Graphics/DecalSystem.txt | Medium | 3 | 3 | 1 | 1 | Projected texture rendering for terrain marks (bullet holes, scorches, tracks). New pipeline pass. |
| 39 | Engine/AddTracyProfiler.txt | Medium | 3 | 3 | 1 | 1 | Integrate Tracy profiler with existing profiling system. |
| 40 | Misc/LinterToolingForStyleGuide.txt | Medium | 3 | 2 | 0 | 1 | clang-format and clang-tidy for style enforcement. |
| 41 | Graphics/ocean-phase-2-slope-mapping.md | Medium | 3 | 4 | 2 | 1 | 4-octave slope cascade (1077/154/22/3.1m) replacing 6 `SampleNormal()` calls; slope-variance → Ward roughness; noise-UV anti-tiling; ~10 new floats. Depends on Phase 1. |
| 42 | [Graphics/ShaderReview/02_Water.md](Graphics/ShaderReview/02_Water.md) | Medium | 3 | 3 | 2 | 2 | `Water.frag` descriptor-set audit (set 1 vs set 0 for global samplers), `normalize` zero-guards (lines 110, 133), drop unused `Fresnel()`, reduce per-fragment `pow` + single smoke sample; `Water.vert` replace `gl_Position.w=0` kill pattern and use explicit `textureLod` |
| 43 | [Graphics/ShaderReview/04_Particles.md](Graphics/ShaderReview/04_Particles.md) | Medium | 4 | 5 | 3 | 2 | Billboard singularity on top-down RTS camera (`SquareParticlesRender.vert:50`, `LongParticlesRender.vert:53`); `ParticlesUpdate.comp:100` non-atomic RMW race on allocation bitmap (make `atomicAnd`); `ParticlesSpawn.comp` uint8/uint16 header mismatch; `ParticlesRender.frag:36` `pow` base clamp; double-integration on terrain bounce |
| 44 | [Graphics/ShaderReview/10_Shadow.md](Graphics/ShaderReview/10_Shadow.md) | Medium | 4 | 4 | 2 | 2 | `Shadow.comp` `acos` clamp, `normalize(vec3(x,0,0))` fix, `textureLod` in compute, signed-vs-unsigned loop counter with negative increment; shrink 512-thread blur workgroups to 64-256; add `writeonly` to every blur output image; fix inverted `fInvSigma` semantics in `ShadowBlurV` |
| 45 | [Network/ReconcileWasteAndInvariant.txt](Network/ReconcileWasteAndInvariant.txt) | Architectural | 4 | 5 | 3 | 2 | Add `iHighWaterValidatedTick` invariant + `DEBUG_BREAK`, cap catch-up to `serverUpdates.begin()->first - 1`, preserve validated frames across writeback; fixes ~37-tick re-sim loop on kChina |
| 46 | [Graphics/ocean-phase-1-pbr-foundation.md](Graphics/ocean-phase-1-pbr-foundation.md) | Architectural | 4 | 5 | 3 | 2 | Rewrite `Water.frag` main() to Beer-Lambert + Schlick + Ward BRDF + MeanFresnel; ~12 new `GlobalLayout` floats + new `TweaksScreenWaterPBR.cpp`; establishes shading model Phases 2-7 build on |
| 47 | Graphics/FlipbookSpriteAnimations.txt | Medium | 3 | 2 | 1 | 2 | Sprite-sheet animation for explosions (texture atlas with frame indexing). |
| 48 | Graphics/DestructionBuffer.txt | Medium | 3 | 3 | 2 | 2 | Per-entity damage accumulation, fragment shader peels/blends sub-surface based on hit positions. |
| 49 | Network/ForwardErrorCorrection.txt | Medium | 4 | 3 | 1 | 2 | XOR-based FEC for unreliable coord updates. |
| 50 | Frame/Future_TagBasedGenericIteration.txt | Medium | 3 | 2 | 1 | 2 | C++ concepts for generic cross-collection operations. Revisit when 3+ more game collections added. |
| 51 | Frame/Future_EventMessageBus.txt | Medium | 3 | 3 | 2 | 2 | Frame-scoped event queue decoupling collection communication. Revisit when fan-out exceeds 3-4 consumers. |
| 52 | [Network/TransferReconciliationBarrierFixB.txt](Network/TransferReconciliationBarrierFixB.txt) | Architectural | 5 | 5 | 3 | 3 | Phase B per-tick barrier + client transfer sort (gated on Phase A logs) |
| 53 | [DataPacker/Architecture_MainOrchestration.md](DataPacker/Architecture_MainOrchestration.md) | Medium | 4 | 4 | 3 | 3 | Extract `DataTypes.h` + `Data.h` generators, unify three parallel lists, single-MessageBox failure path, IBL file split |
| 54 | [DataPacker/Refactor_DedupLoops.md](DataPacker/Refactor_DedupLoops.md) | Medium | 4 | 4 | 3 | 3 | `CollectBindings` helper (6 SPIRV-Cross blocks), flatten O(n²) stage_inputs, TEXCOORD dedup, optional `common::ReadFile` |
| 55 | [Network/Architecture_LayerViolations.md](Network/Architecture_LayerViolations.md) | Architectural | 4 | 4 | 3 | 3 | Reclass save/load/reset/replay/pause/timespeed packets as game-layer; virtuals on `ClientSessionBase`/`ServerSessionBase` for CoordFrames mutation and Frame factory |
| 56 | [Network/Refactor_HotPathAllocations.md](Network/Refactor_HotPathAllocations.md) | Medium | 4 | 4 | 3 | 3 | Migrate ~10 per-tick `std::vector`/`std::unordered_map`/`ostringstream` allocations to `gpThreadLocal->mWorkbuffer` |
| 57 | [Graphics/ShaderReview/06_Lighting.md](Graphics/ShaderReview/06_Lighting.md) | Medium | 4 | 4 | 3 | 3 | `AreaLight`/`PointLight` missing scalar qualifier on `lightOccupancyBuffer` + `+0.4f` rounding-bias bug (should be `+0.5f` or flat int varying); `LightingSpread.frag` NaN/Inf guards on `fNorm`/`fRingFalloff` + per-fragment cos/sin hot path; `LightingBlur{H,V}` NaN at radius=0 + `writeonly` + `textureLod` + inverted sigma semantics in V pass; `LightCombine.comp` `pow` + `fPassCount` guards |
| 58 | Engine/GrassRendering.txt | Large | 4 | 3 | 2 | 3 | Instanced grass patches, vertex shader height, noise textures for dryness/height. New collection + shaders. |
| 59 | Engine/TreePlacementAndRendering.txt | Large | 4 | 3 | 2 | 3 | Tree/bush placement with LOD. New collection and instanced rendering. |
| 60 | Frame/FrameRelativePositions.txt | Large | 5 | 5 | 3 | 3 | Convert world-absolute to frame-relative positions. |
| 61 | Frame/Future_SharedBehaviorTraits.txt | Large | 4 | 3 | 2 | 3 | Reusable SOA trait structs composed into collections via tuple_cat with shared logic functions. Revisit at 20-25 collections. |
| 62 | [DataPacker/Refactor_ExportSceneDecompose.md](DataPacker/Refactor_ExportSceneDecompose.md) | Medium | 5 | 4 | 3 | 4 | Decompose `PreExport` (~265 lines) and `MainExport` (~280 lines) + one redundant-check simplification |
| 63 | [Network/Refactor_OversizedFunctions.md](Network/Refactor_OversizedFunctions.md) | Architectural | 5 | 4 | 3 | 4 | Decompose `ReconcileCoord` (207 lines), `WriteFleetData`/`ReadFleetData`, `ResetClientsForLoad`, `NewClients`, `Client::Poll`/`Server::Poll` sim/direct dedup |
| 64 | [DataPacker/Refactor_LongParamsAndFlags.md](DataPacker/Refactor_LongParamsAndFlags.md) | Medium | 4 | 3 | 3 | 4 | `Texture` bools → `Flags<TextureOptions>`, `LoadVertices`/`LoadAnimations`/`WriteBinding` param structs |
| 65 | Audio/ReplaceDirectXTKAudioWithMiniaudio.txt | Large | 5 | 3 | 2 | 4 | Replace audio engine with miniaudio. |
| 66 | Frame/Future_CollectionVariants.txt | Large | 4 | 2 | 2 | 4 | Optional sparse SOA extensions for subset-only fields. No evidence of need currently. |
| 67 | [DataPacker/Architecture_CohesionSplits.md](DataPacker/Architecture_CohesionSplits.md) | Architectural | 5 | 4 | 4 | 5 | Extract `CopyThirdPartyLicenses` → `Attribution.{h,cpp}`, move Vulkan SDK path to `ExportShader.cpp`, rename `ExportScene*` helpers to `Scene/` subdir |

### Reference / Index Documents (not independently scheduled)

| Plan File | Purpose |
|-----------|---------|
| Graphics/ocean-fragment-shader-overview.md | Index/meta doc for ocean shader rewrite; links all 7 phase plans + Tweaks screen plan. Keep as reference. |
| Graphics/ocean-phase-tweaks-screen.md | UI-only consolidation spec (single `kWaterPBR` TweakSection with 7 tabs); subsumed by per-phase plans — each phase creates its own tab. Do not schedule standalone. |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `Network/TransferReconciliationBarrierFixB.txt` depends on `Network/TransferReconciliationBarrier.txt` — Phase A logs must be captured and analyzed before Phase B is implemented.
- `Network/ReconcileWasteAndInvariant.txt` depends on `Network/TransferReconciliationBarrier.txt` (Phase A) and `Network/TransferReconciliationBarrierFixB.txt` — barrier fix removes the dominant CRC-mismatch source; without it the high-water mark advances too slowly to benefit from the catch-up cap. Also touches `ReconcileReplay.cpp`, `ReconcileReplayCrc.cpp`, `ClientReconciler.{h,cpp}` — run the invariant/waste fix first since it adds a new field to `CoordReconcileWork` and `CoordFrames`, then the `Refactor_OversizedFunctions.md` / `Refactor_CoordScratchFlags.md` / `Refactor_FileSizeTriage.md` work on the same files.
- `DataPacker/Refactor_ExportSceneDecompose.md` should precede `DataPacker/Refactor_LongParamsAndFlags.md` — both touch `ExportSceneVertices.cpp`/`ExportSceneAnimation.cpp`, and the decomposition will shift line numbers that the long-params plan references.
- `DataPacker/Architecture_CohesionSplits.md` (Scene-helpers rename) should run AFTER any plan that touches `ExportSceneAnimation/Skeleton/Vertices.{h,cpp}` — the rename invalidates paths referenced elsewhere.
- `DataPacker/Architecture_IncludeGraph.md` (tinygltf forward-declare moves) and `DataPacker/Architecture_CohesionSplits.md` (Scene rename) both touch the same four Scene helper headers — do them in the same session.
- `Network/Refactor_WorkbufferRaii.md` must precede `Network/Refactor_SendBoilerplate.md` — the Send template uses `ScopedWorkbufferFrame`; landing Send first would force a two-pass migration.
- `Network/Refactor_GuardConsolidation.md` and `Network/Refactor_DeadCode.md` both delete the 5 unreachable `iSize < 1` checks in `ServerReceive.cpp` — de-dupe at execution time (do whichever lands first).
- `Network/Refactor_FileSizeTriage.md` defers `ClientSession.cpp` / `ClientReceive.cpp` / `ServerReceive.cpp` pending upstream refactors; run only after `SendBoilerplate`, `ClientReceiveStateMachine`, and `GuardConsolidation` land, then re-measure.
- `Network/Architecture_FleetRngDeterminism.md` touches `WriteFleetData` / `ReadFleetData` — coordinate with `Network/Refactor_OversizedFunctions.md` (which splits those functions). Run FleetRngDeterminism first so the seed-threading lands before the split.
- `Network/Architecture_LayerViolations.md` touches `ServerReceive.cpp`, `ClientSessionBase.cpp`, `ServerSessionBase.cpp`, `Client.cpp`, `Server.cpp`, `ClientReceive.cpp` — run AFTER the small-scale refactors on those files (Guard, DeadCode, WorkbufferRaii) so its diff focuses on the semantic change.
- `Graphics/ShaderReview/09_Wind.md`, `Graphics/ShaderReview/02_Water.md`, and `Graphics/ShaderReview/06_Lighting.md` each include a descriptor-set (`set=0` vs `set=1`) audit — coordinate in one session so the convention lands consistently across Wind compute pipelines, Water samplers, and `lightOccupancyBuffer`. Confirm the C++ pipeline-layout side (`Engine/Source/Graphics/...`) before touching GLSL so both sides move together.
- `Graphics/ShaderReview/09_Wind.md` edits `WindSpreadCommon.h` — that header is included by both `WindSpreadOne.comp` and `WindSpreadTwo.comp`, so the `pow(max(fDecayRate, 0), ...)` fix and the switch to `textureLod` inside the divergent-branch sampling land for both shaders from a single header edit. No follow-up edits to the per-file `.comp` files are required for those fixes.
- `Graphics/ocean-phase-{2,3,4,5,6,7}` all depend on `Graphics/ocean-phase-1-pbr-foundation.md` (need Ward BRDF + Beer-Lambert + MeanFresnel to exist). Phases 2-7 are independent of each other; recommended visual-impact order: 2, 3, 4, 5, 6, 7.
- `Graphics/ocean-phase-tweaks-screen.md` is subsumed — each phase plan creates its tab inside the single `kWaterPBR` section; do not execute standalone.
- `Graphics/ocean-fragment-shader-overview.md` is an index/meta document, not an executable plan.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`ExportScene.cpp`**: `Refactor_ExportSceneDecompose.md`, `Architecture_IncludeGraph.md` (adds tinygltf include), `Architecture_CohesionSplits.md` (updates includes for Scene rename)
- **`ExportSceneAnimation/Skeleton/Vertices.{h,cpp}`**: `Architecture_IncludeGraph.md`, `Architecture_CohesionSplits.md`, `Refactor_DedupLoops.md`, `Refactor_LongParamsAndFlags.md`
- **`Main.cpp`**: `Architecture_IncludeGraph.md` (delete duplicate crtdbg), `Architecture_ExportJobContract.md` (remove AddToHeader dispatch), `Architecture_MainOrchestration.md` (extract generators)
- **`ExportShader.cpp`**: `Refactor_DedupLoops.md` (CollectBindings + O(n²) fix), `Refactor_LongParamsAndFlags.md` (WriteBinding struct), `Refactor_QuickWins.md` (OPTIMIZE_SHADERS → `if constexpr`), `Architecture_CohesionSplits.md` (Vulkan SDK path move)
- **`Texture.{h,cpp}`**: `Refactor_LongParamsAndFlags.md` (Flags), `Refactor_QuickWins.md` (redundant dtor)
- **`FileManager.{h,cpp}`**: `Architecture_CohesionSplits.md` (split), `Refactor_QuickWins.md` (delete unreachable block)
- **`ExportJob.{h,cpp}`**: `Architecture_ExportJobContract.md` (Version helper + concept + default moves)
- **`Network/Client/ClientSend.cpp`**: `Refactor_WorkbufferRaii.md`, `Refactor_SendBoilerplate.md`, `Refactor_GuardConsolidation.md`
- **`Network/Client/ClientSession.cpp` (game)**: `Refactor_WorkbufferRaii.md`, `Refactor_SendBoilerplate.md`, `Refactor_HotPathAllocations.md`, `Refactor_OversizedFunctions.md`
- **`Network/Server/ServerReceive.cpp`**: `Refactor_GuardConsolidation.md`, `Refactor_DeadCode.md`, `Architecture_LayerViolations.md`
- **`Network/Server/ServerFleetManager.cpp`**: `Refactor_OversizedFunctions.md`, `Architecture_FleetRngDeterminism.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`, `Refactor_DeadCode.md`
- **`Network/Client/ClientReceive.cpp`**: `Refactor_ClientReceiveStateMachine.md`, `Refactor_HotPathAllocations.md`, `Architecture_LayerViolations.md`
- **`Network/Client/Client.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Refactor_DeadCode.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/Server.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ReconcileReplay.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_CoordScratchFlags.md`, `Refactor_AllocationTrackingAudit.md`, `Architecture_UnusedIncludes.md`, `ReconcileWasteAndInvariant.txt`
- **`Network/Client/ClientReconciler.{h,cpp}`**: `Refactor_CoordScratchFlags.md`, `ReconcileWasteAndInvariant.txt`
- **`Network/Server/ServerSession.cpp` (game)**: `Refactor_WorkbufferRaii.md`, `Refactor_OversizedFunctions.md`, `Architecture_FleetRngDeterminism.md`, `Refactor_AllocationTrackingAudit.md`
- **`Engine/Data/Shaders/Water/Water.frag`**: all seven ocean phase plans (Phase 1 rewrites main(); Phases 2-7 add/modify terms).
- **`Engine/Data/Shaders/ShaderLayoutsBase.h` + `GlobalUniforms.cpp` + `WrapperBase.{h,cpp}` + `TweaksSliderMap.cpp`**: all seven ocean phase plans (each adds 5-12 floats; align ordering to minimize diff churn; single `kWaterPBR` TweakSection with 7 tabs).

## Declined / Not Applicable

Future ideas from Engine.txt evaluated and rejected (already implemented, out of scope, or declined):

- PBR / glTF 2.0 — fully implemented
- Hex Shields — fully implemented
- Particle System — fully implemented (GPU compute)
- Islands (most ideas) — implemented (transfer queue, bindless, AO, mipmaps, tiling, visibility)
- Data Packer (most ideas) — implemented (CRCs, re-export, mesh headers, index buffers, BC4/BC7, lazy tiers)
- Vulkan (most ideas) — implemented (VMA, pipeline cache, timeline semaphores, MSAA 4x, transfer queue)
- Serialization — custom SOA tuple system sufficient
- Optimization (many) — implemented (aligned floats, denormals, XMVector3Rotate)
- Hydraulic/thermal erosion — art pipeline tool, not engine code
- Procedural noise for terrain — contradicts pre-baked architecture
- Robot voice creation — asset creation task
- AI-generated music — content/tooling concern
- CSAA — obsolete NVIDIA-specific
- FFT Ocean — overkill for island-focused game
- Log-LUV encoding — F16 already used
- Async compute queue — defer until profiling
- Subpass merging — minimal desktop GPU benefit
- Multithreaded pipeline creation — pipeline cache sufficient
- _mm_prefetch / PGO — defer until Tracy exists
- Bloom & Lens Flares — declined
- Screen-Space Reflections — declined
- Dynamic Cubemap Rendering — declined
- Bounced & Rim Lighting — declined
- Ambient Wind & Gusts — declined
- Network Metrics & Adaptive Buffering — fully implemented (packet loss, jitter tracking, adaptive replay throttle)
- Soft Desync Recovery — fully implemented (resync request, full state re-download)
- New Weapon Types — declined
