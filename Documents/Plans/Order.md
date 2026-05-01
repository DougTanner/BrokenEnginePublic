# Plan Execution Order

All refactor and bugfix plans — debt reduction that doesn't add a new engine capability — sorted by score (lowest = highest priority).

Score = Effort − Impact + Risks (lower = higher priority)

`Tier` column is an informal size/risk descriptor (**Quick Win** / **Small** / **Medium** / **Large** / **Architectural**); it does not affect score or ordering.

Counterpart: `Documents/Features/Order.md` holds brand-new additions. See `Documents/CLAUDE.md` for the distinction. Scores are not comparable across the two files — they were originally derived from a single sort and then split.

## Debt Score (DataPacker, this run): MODERATE

Distribution: 6 Medium + 1 Architectural. No critical bugs, but `ExportScene.cpp` is approaching the `/reduce-file` threshold, `RunExportJobs<T>` duck-types a static contract, and a public header pulls tinygltf into unrelated TUs.

## Debt Score (Network, this run): MODERATE

Distribution: 7 Quick Win + 6 Medium + 2 Architectural across 15 plans. Network subtree is structurally sound — the main concentrations are (1) engine → game surgery in `ServerReceive.cpp` save/load/timespeed handlers and the `*SessionBase.cpp` CoordFrames mutation, (2) one replay-determinism bug (fleet RNG re-seeded from wall clock on load), and (3) mechanical duplication across ~20 `Send*` methods, ~14 connected-gate sites, and ~12 handshake-gate sites. `ServerFleetManager.cpp` (808 lines) and `ReconcileReplay.cpp` (668 lines) are genuine `/reduce-file` candidates.

## Debt Score (Graphics — Shaders, this run): MODERATE

60 first-party GLSL shaders reviewed via one `/glsl-review` Opus subagent per file. Original distribution: 9 Quick Win + 3 Medium across 12 subsystem plans. **Five performance-neutral bugfix plans landed this session** via parallel Opus subagents: `03_Terrain`, `05_Quads`, `07_Objects` fully executed and deleted; `09_Wind` and `12_Misc` partially executed (descriptor-set audit and `nonuniformEXT` audit deferred — both require C++ coordination). Remaining distribution: 4 Quick Win + 3 Medium across 9 subsystem plans. No `inverse()` driver-hang violations. Remaining recurring hazards: (1) unguarded `normalize()` on possibly-zero vectors (Model, Water, Particles, Shadow), (2) `pow(neg, non-integer)` → NaN (Lighting — 06), (3) implicit-LOD `texture()` in compute (Shadow, ObjectShadowsBlur), (4) missing `writeonly` on every blur output `image2D`. Standalone hard bugs: `Terrain.frag:76` (landed), `Log.vert:38` (landed), `ParticlesUpdate.comp:100` (non-atomic RMW race — still pending in `04_Particles.md`). Overview at `Graphics/ShaderReview/00_Overview.md`; per-subsystem plans at `Graphics/ShaderReview/{01,02,04,06,08,09,10,11,12}_*.md`.

## Plans

| # | Plan | Tier | Effort | Impact | Risks | Score | Notes |
|---|------|------|--------|--------|-------|-------|-------|
| 1 | [Graphics/ReverseZDepth.txt](Graphics/ReverseZDepth.txt) | Small | 2 | 4 | 1 | -1 | Flip min/max depth, GREATER_EQUAL compare, clear to 0. Eliminates z-fighting. Contained change. |
| 2 | [Network/Architecture_FleetReloadStateOrdering.md](Network/Architecture_FleetReloadStateOrdering.md) | Medium | 2 | 4 | 2 | 0 | Investigate whether `ResetState`'s `mFleets`/`mPlayerToGuid`/`mGuidToClientId` clears wipe just-loaded fleet data after `ReadFleetData`/`OnResetForLoad`. Trace one Quickload, decide intentional pattern vs bug, fix if needed |
| 3 | [Network/Refactor_DeadCode.md](Network/Refactor_DeadCode.md) | Quick Win | 1 | 2 | 1 | 0 | Delete dead `Client.cpp` ctor locals, `PollLANDiscovery` bool→void, `[[maybe_unused]] spawnCoord` audit |
| 4 | [Network/Refactor_WorkbufferRaii.md](Network/Refactor_WorkbufferRaii.md) | Medium | 2 | 3 | 1 | 0 | Add `common::ScopedWorkbufferFrame` RAII + migrate 26 manual Push/Pop pairs (precursor to SendBoilerplate) |
| 5 | [Network/Refactor_GuardConsolidation.md](Network/Refactor_GuardConsolidation.md) | Quick Win | 2 | 3 | 1 | 0 | `Client::CanSend()` + `Server::FindHandshakenClient()` + delete 5 unreachable `iSize < 1` checks |
| 6 | [Network/Refactor_AllocationTrackingAudit.md](Network/Refactor_AllocationTrackingAudit.md) | Quick Win | 1 | 2 | 1 | 0 | Add missing `// Heap:` comments at ~25 sites, remove cosmetic guards, standardize `suppressAllocationTracking` name |
| 7 | [Network/Refactor_CoordScratchFlags.md](Network/Refactor_CoordScratchFlags.md) | Quick Win | 1 | 2 | 1 | 0 | `CoordScratch` 5 bools → `common::Flags<ReconcileScratchFlags>` (1 byte vs 5 bytes) |
| 8 | [Graphics/ShaderReview/01_Model.md](Graphics/ShaderReview/01_Model.md) | Quick Win | 2 | 3 | 1 | 0 | `Model.frag` tangent Gram-Schmidt normalize guards (lines 171, 176), dead `fSunDot`, `pow(x,5)` expand, prefer `ReadLighting` helper |
| 9 | [Graphics/ShaderReview/08_Smoke.md](Graphics/ShaderReview/08_Smoke.md) | Quick Win | 1 | 2 | 1 | 0 | `pow` defensive clamps (`Smoke.frag:31`, `SmokeSpreadTwo.comp:69`), `readonly` on `SmokeOccupancyDilate` buffer, replace magic `/8` with `kiComputeTileSize` |
| 10 | [Graphics/ShaderReview/11_DebugUi.md](Graphics/ShaderReview/11_DebugUi.md) | Quick Win | 1 | 1 | 0 | 0 | `DebugRenderBillboard.vert` redundant normalize, `ProfileText.frag` dead varyings, `UiDepthPrepass.vert` add `invariant gl_Position` if any pass relies on depth-equal |
| 11 | [Graphics/ShaderReview/12_Misc.md](Graphics/ShaderReview/12_Misc.md) | Quick Win | 1 | 1 | 0 | 0 | **Partial**: `Log.vert` w=0 + vertex-gate landed; `DebugTexture.frag` uninit output + P-S1 + f4Scaled guards landed. Remaining: `Log.vert` unwritten flat outputs decision (assign or remove), `DebugTexture.frag` `nonuniformEXT` audit, minor perf cleanups |
| 12 | [Engine/WindSmokeAdvection.txt](Engine/WindSmokeAdvection.txt) | Small | 1 | 1 | 0 | 0 | Fix: Wind doesn't push low-value smoke — add direct wind advection offset to base sampling coord. |
| 13 | [Graphics/StaleManagerForwardDecls.txt](Graphics/StaleManagerForwardDecls.txt) | Quick Win | 1 | 1 | 0 | 0 | Sweep `Engine/Source/Graphics/Managers/*.h` for unused forward declarations. `CommandBufferManager.h` `RenderFrame` already removed; `BufferManager.h:7` flagged as a probable case. |
| 14 | [Misc/VcxprojFiltersGameTweaksScreenPaths.md](Misc/VcxprojFiltersGameTweaksScreenPaths.md) | Quick Win | 1 | 1 | 0 | 0 | Fix wrong include paths and filter classification for game-side `TweaksScreen<Tab>.cpp` group in `BrokenEngineSandbox.vcxproj.filters` (lines ~1107-1151). Build unaffected, IDE-view only. |
| 15 | [DataPacker/Architecture_IncludeGraph.md](DataPacker/Architecture_IncludeGraph.md) | Medium | 3 | 4 | 2 | 1 | Move `tiny_gltf.h` out of public headers (4 files), delete unused skeleton include, delete duplicate `<crtdbg.h>` block |
| 16 | [DataPacker/Architecture_ExportJobContract.md](DataPacker/Architecture_ExportJobContract.md) | Medium | 3 | 4 | 2 | 1 | `Version()` helper on base, delete dead `utils::ChunkHeader` fwd decl, `IsExportJob` concept, remove empty `AddToHeader` hook, default moves |
| 17 | [Network/Refactor_FileSizeTriage.md](Network/Refactor_FileSizeTriage.md) | Quick Win | 2 | 2 | 1 | 1 | Run `/reduce-file` on `ServerFleetManager.cpp` (808) and `ReconcileReplay.cpp` (668); defer other borderline files |
| 18 | [Network/Refactor_ClientReceiveStateMachine.md](Network/Refactor_ClientReceiveStateMachine.md) | Medium | 2 | 3 | 2 | 1 | Extract `ClassifyFullState` + 3 subscribe-accept helpers from two 55-60-line handlers |
| 19 | [Network/Architecture_HeaderSplits.md](Network/Architecture_HeaderSplits.md) | Medium | 2 | 2 | 1 | 1 | Split `NetworkDiscovery.{h,cpp}` into Responder/Scanner files (BT_CLIENT/BT_SERVER vcxproj rule) |
| 20 | [Network/Refactor_SendBoilerplate.md](Network/Refactor_SendBoilerplate.md) | Medium | 3 | 4 | 2 | 1 | Variadic `SendSimplePacket` template collapses 12 `Client::Send*` + 6 `ClientSession::Send*` methods; ~300 lines deleted |
| 21 | [Misc/FlatContainersOptimization.txt](Misc/FlatContainersOptimization.txt) | Small | 3 | 3 | 1 | 1 | Replace std::unordered_map with std::flat_map in hot paths. **BLOCKED**: MSVC 14.50 lacks \<flat_map\>. |
| 22 | [Misc/LinterToolingForStyleGuide.txt](Misc/LinterToolingForStyleGuide.txt) | Medium | 3 | 2 | 0 | 1 | clang-format and clang-tidy for style enforcement. |
| 23 | [Misc/TweaksScreenSliderMapPerTabRegistrars.md](Misc/TweaksScreenSliderMapPerTabRegistrars.md) | Medium | 3 | 3 | 1 | 1 | Move ~175 string-literal slider-map entries from `TweaksScreen.cpp` constructor to per-tab registrars (`TweaksScreen<Tab>.cpp`) so each file owns its labels. Co-locates registration with use-site. |
| 24 | [Misc/TweaksSliderMapDebugAudit.md](Misc/TweaksSliderMapDebugAudit.md) | Small | 2 | 2 | 1 | 1 | First-frame audit (gated on `kbDebugInput`) of `TweaksSliderMap` to warn on orphan/missed keys; sentinel-driven dry-run pass. **Depends on Plan 23 (per-tab registrars).** |
| 25 | [Graphics/ShaderReview/09_Wind.md](Graphics/ShaderReview/09_Wind.md) | Medium | 2 | 2 | 2 | 2 | **Partial**: textureLod swaps + denormal epsilon landed. Remaining: descriptor-set `set=1` audit (coordinate with `02_Water` + `06_Lighting` + C++ pipeline-layout side) |
| 26 | [Graphics/ShaderReview/02_Water.md](Graphics/ShaderReview/02_Water.md) | Medium | 3 | 3 | 2 | 2 | `Water.frag` descriptor-set audit (set 1 vs set 0 for global samplers), `normalize` zero-guards (lines 110, 133), drop unused `Fresnel()`, reduce per-fragment `pow` + single smoke sample; `Water.vert` replace `gl_Position.w=0` kill pattern and use explicit `textureLod` |
| 27 | [Graphics/ShaderReview/04_Particles.md](Graphics/ShaderReview/04_Particles.md) | Medium | 4 | 5 | 3 | 2 | Billboard singularity on top-down RTS camera (`SquareParticlesRender.vert:50`, `LongParticlesRender.vert:53`); `ParticlesUpdate.comp:100` non-atomic RMW race on allocation bitmap (make `atomicAnd`); `ParticlesSpawn.comp` uint8/uint16 header mismatch; `ParticlesRender.frag:36` `pow` base clamp; double-integration on terrain bounce |
| 28 | [Graphics/ShaderReview/10_Shadow.md](Graphics/ShaderReview/10_Shadow.md) | Medium | 4 | 4 | 2 | 2 | `Shadow.comp` `acos` clamp, `normalize(vec3(x,0,0))` fix, `textureLod` in compute, signed-vs-unsigned loop counter with negative increment; shrink 512-thread blur workgroups to 64-256; add `writeonly` to every blur output image; fix inverted `fInvSigma` semantics in `ShadowBlurV` |
| 29 | [Network/ReconcileWasteAndInvariant.txt](Network/ReconcileWasteAndInvariant.txt) | Architectural | 4 | 5 | 3 | 2 | Add `iHighWaterValidatedTick` invariant + `DEBUG_BREAK`, cap catch-up to `serverUpdates.begin()->first - 1`, preserve validated frames across writeback; fixes ~37-tick re-sim loop on kChina |
| 30 | [Network/TransferReconciliationBarrierFixB.txt](Network/TransferReconciliationBarrierFixB.txt) | Architectural | 5 | 5 | 3 | 3 | Phase B per-tick barrier + client transfer sort (gated on Phase A logs) |
| 31 | [DataPacker/Architecture_MainOrchestration.md](DataPacker/Architecture_MainOrchestration.md) | Medium | 4 | 4 | 3 | 3 | Extract `DataTypes.h` + `Data.h` generators, unify three parallel lists, single-MessageBox failure path, IBL file split |
| 32 | [DataPacker/Refactor_DedupLoops.md](DataPacker/Refactor_DedupLoops.md) | Medium | 4 | 4 | 3 | 3 | `CollectBindings` helper (6 SPIRV-Cross blocks), flatten O(n²) stage_inputs, TEXCOORD dedup, optional `common::ReadFile` |
| 33 | [Network/Architecture_LayerViolations.md](Network/Architecture_LayerViolations.md) | Architectural | 4 | 4 | 3 | 3 | Reclass save/load/reset/replay/pause/timespeed packets as game-layer; virtuals on `ClientSessionBase`/`ServerSessionBase` for CoordFrames mutation and Frame factory |
| 34 | [Network/Refactor_HotPathAllocations.md](Network/Refactor_HotPathAllocations.md) | Medium | 4 | 4 | 3 | 3 | Migrate ~10 per-tick `std::vector`/`std::unordered_map`/`ostringstream` allocations to `gpThreadLocal->mWorkbuffer` |
| 35 | [Graphics/ShaderReview/06_Lighting.md](Graphics/ShaderReview/06_Lighting.md) | Medium | 4 | 4 | 3 | 3 | `AreaLight`/`PointLight` missing scalar qualifier on `lightOccupancyBuffer` + `+0.4f` rounding-bias bug (should be `+0.5f` or flat int varying); `LightingSpread.frag` NaN/Inf guards on `fNorm`/`fRingFalloff` + per-fragment cos/sin hot path; `LightingBlur{H,V}` NaN at radius=0 + `writeonly` + `textureLod` + inverted sigma semantics in V pass; `LightCombine.comp` `pow` + `fPassCount` guards |
| 36 | [Frame/FrameRelativePositions.txt](Frame/FrameRelativePositions.txt) | Large | 5 | 5 | 3 | 3 | Convert world-absolute to frame-relative positions. Fixes float precision degradation at distance (~1cm jitter at 140 frames from origin). |
| 37 | [DataPacker/Refactor_ExportSceneDecompose.md](DataPacker/Refactor_ExportSceneDecompose.md) | Medium | 5 | 4 | 3 | 4 | Decompose `PreExport` (~265 lines) and `MainExport` (~280 lines) + one redundant-check simplification |
| 38 | [Network/Refactor_OversizedFunctions.md](Network/Refactor_OversizedFunctions.md) | Architectural | 5 | 4 | 3 | 4 | Decompose `ReconcileCoord` (207 lines), `WriteFleetData`/`ReadFleetData`, `ResetClientsForLoad`, `NewClients`, `Client::Poll`/`Server::Poll` sim/direct dedup |
| 39 | [DataPacker/Refactor_LongParamsAndFlags.md](DataPacker/Refactor_LongParamsAndFlags.md) | Medium | 4 | 3 | 3 | 4 | `Texture` bools → `Flags<TextureOptions>`, `LoadVertices`/`LoadAnimations`/`WriteBinding` param structs |
| 40 | [DataPacker/Architecture_CohesionSplits.md](DataPacker/Architecture_CohesionSplits.md) | Architectural | 5 | 4 | 4 | 5 | Extract `CopyThirdPartyLicenses` → `Attribution.{h,cpp}`, move Vulkan SDK path to `ExportShader.cpp`, rename `ExportScene*` helpers to `Scene/` subdir |

## Dependencies

Plans that must be executed in order due to shared files or stale line numbers:

- `Network/ReconcileWasteAndInvariant.txt` depends on `Network/TransferReconciliationBarrierFixB.txt` — barrier fix removes the dominant CRC-mismatch source; without it the high-water mark advances too slowly to benefit from the catch-up cap. Also touches `ReconcileReplay.cpp`, `ReconcileReplayCrc.cpp`, `ClientReconciler.{h,cpp}` — run the invariant/waste fix first since it adds a new field to `CoordReconcileWork` and `CoordFrames`, then the `Refactor_OversizedFunctions.md` / `Refactor_CoordScratchFlags.md` / `Refactor_FileSizeTriage.md` work on the same files.
- `DataPacker/Refactor_ExportSceneDecompose.md` should precede `DataPacker/Refactor_LongParamsAndFlags.md` — both touch `ExportSceneVertices.cpp`/`ExportSceneAnimation.cpp`, and the decomposition will shift line numbers that the long-params plan references.
- `DataPacker/Architecture_CohesionSplits.md` (Scene-helpers rename) should run AFTER any plan that touches `ExportSceneAnimation/Skeleton/Vertices.{h,cpp}` — the rename invalidates paths referenced elsewhere.
- `DataPacker/Architecture_IncludeGraph.md` (tinygltf forward-declare moves) and `DataPacker/Architecture_CohesionSplits.md` (Scene rename) both touch the same four Scene helper headers — do them in the same session.
- `Network/Refactor_WorkbufferRaii.md` must precede `Network/Refactor_SendBoilerplate.md` — the Send template uses `ScopedWorkbufferFrame`; landing Send first would force a two-pass migration.
- `Network/Refactor_GuardConsolidation.md` and `Network/Refactor_DeadCode.md` both delete the 5 unreachable `iSize < 1` checks in `ServerReceive.cpp` — de-dupe at execution time (do whichever lands first).
- `Network/Refactor_FileSizeTriage.md` defers `ClientSession.cpp` / `ClientReceive.cpp` / `ServerReceive.cpp` pending upstream refactors; run only after `SendBoilerplate`, `ClientReceiveStateMachine`, and `GuardConsolidation` land, then re-measure.
- `Network/Architecture_LayerViolations.md` touches `ServerReceive.cpp`, `ClientSessionBase.cpp`, `ServerSessionBase.cpp`, `Client.cpp`, `Server.cpp`, `ClientReceive.cpp` — run AFTER the small-scale refactors on those files (Guard, DeadCode, WorkbufferRaii) so its diff focuses on the semantic change.
- `Graphics/ShaderReview/09_Wind.md`, `Graphics/ShaderReview/02_Water.md`, and `Graphics/ShaderReview/06_Lighting.md` each include a descriptor-set (`set=0` vs `set=1`) audit — coordinate in one session so the convention lands consistently across Wind compute pipelines, Water samplers, and `lightOccupancyBuffer`. Confirm the C++ pipeline-layout side (`Engine/Source/Graphics/...`) before touching GLSL so both sides move together.
- `Graphics/ShaderReview/09_Wind.md` edits `WindSpreadCommon.h` — that header is included by both `WindSpreadOne.comp` and `WindSpreadTwo.comp`, so the `pow(max(fDecayRate, 0), ...)` fix and the switch to `textureLod` inside the divergent-branch sampling land for both shaders from a single header edit. No follow-up edits to the per-file `.comp` files are required for those fixes.
- `Misc/TweaksSliderMapDebugAudit.md` depends on `Misc/TweaksScreenSliderMapPerTabRegistrars.md` — audit warning text and per-file ownership change with the registrar refactor; landing audit first would force a re-tune. Run registrar first, then audit, in one session.

### Cross-directory dependencies (plans in `Documents/Features/`)

- `Graphics/ShaderReview/02_Water.md` should land BEFORE any `Documents/Features/Graphics/ocean-phase-*.md` plan — both edit `Water.frag`, and landing defensive fixes first keeps ocean-phase diffs focused on new shading terms.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`ExportScene.cpp`**: `Refactor_ExportSceneDecompose.md`, `Architecture_IncludeGraph.md` (adds tinygltf include), `Architecture_CohesionSplits.md` (updates includes for Scene rename)
- **`ExportSceneAnimation/Skeleton/Vertices.{h,cpp}`**: `Architecture_IncludeGraph.md`, `Architecture_CohesionSplits.md`, `Refactor_DedupLoops.md`, `Refactor_LongParamsAndFlags.md`
- **`Main.cpp`**: `Architecture_IncludeGraph.md` (delete duplicate crtdbg), `Architecture_ExportJobContract.md` (remove AddToHeader dispatch), `Architecture_MainOrchestration.md` (extract generators)
- **`ExportShader.cpp`**: `Refactor_DedupLoops.md` (CollectBindings + O(n²) fix), `Refactor_LongParamsAndFlags.md` (WriteBinding struct), `Architecture_CohesionSplits.md` (Vulkan SDK path move)
- **`Texture.{h,cpp}`**: `Refactor_LongParamsAndFlags.md` (Flags)
- **`FileManager.{h,cpp}`**: `Architecture_CohesionSplits.md` (split)
- **`ExportJob.{h,cpp}`**: `Architecture_ExportJobContract.md` (Version helper + concept + default moves)
- **`Network/Client/ClientSend.cpp`**: `Refactor_WorkbufferRaii.md`, `Refactor_SendBoilerplate.md`, `Refactor_GuardConsolidation.md`
- **`Network/Client/ClientSession.cpp` (game)**: `Refactor_WorkbufferRaii.md`, `Refactor_SendBoilerplate.md`, `Refactor_HotPathAllocations.md`, `Refactor_OversizedFunctions.md`
- **`Network/Server/ServerReceive.cpp`**: `Refactor_GuardConsolidation.md`, `Refactor_DeadCode.md`, `Architecture_LayerViolations.md`
- **`Network/Server/ServerFleetManager.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_AllocationTrackingAudit.md`, `Refactor_DeadCode.md`, `Architecture_FleetReloadStateOrdering.md`
- **`Network/Client/ClientReceive.cpp`**: `Refactor_ClientReceiveStateMachine.md`, `Refactor_HotPathAllocations.md`, `Architecture_LayerViolations.md`
- **`Network/Client/Client.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Refactor_DeadCode.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Server/Server.cpp` (engine)**: `Refactor_OversizedFunctions.md`, `Architecture_LayerViolations.md`, `Refactor_AllocationTrackingAudit.md`
- **`Network/Client/ReconcileReplay.cpp`**: `Refactor_OversizedFunctions.md`, `Refactor_FileSizeTriage.md`, `Refactor_CoordScratchFlags.md`, `Refactor_AllocationTrackingAudit.md`, `ReconcileWasteAndInvariant.txt`
- **`Network/Client/ClientReconciler.{h,cpp}`**: `Refactor_CoordScratchFlags.md`, `ReconcileWasteAndInvariant.txt`
- **`Network/Server/ServerSession.cpp` (game)**: `Refactor_WorkbufferRaii.md`, `Refactor_OversizedFunctions.md`, `Refactor_AllocationTrackingAudit.md`, `Architecture_FleetReloadStateOrdering.md`
- **`TweaksScreen.cpp`/`TweaksScreen<Tab>.cpp`**: `TweaksScreenSliderMapPerTabRegistrars.md`, `TweaksSliderMapDebugAudit.md` — both touch these files; do them in one session (registrar first).
