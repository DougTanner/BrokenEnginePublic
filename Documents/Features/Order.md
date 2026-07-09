# Feature Execution Order

All brand-new additions — new render passes, new effects, new systems, new collections, new network/audio capabilities, new dev tooling that ships in the binary — sorted by score (lowest = highest priority).

Score = Effort − Impact + Risks (lower = higher priority)

`Tier` is an informal size/risk descriptor (**Quick Win** / **Small** / **Medium** / **Large** / **Architectural**); it does not affect score or ordering.

Counterpart: `Documents/Plans/Order.md` holds refactor/bugfix plans. See `Documents/CLAUDE.md` for the distinction. Scores are not comparable across the two files — they were originally derived from a single sort and then split.

## Plans

| Plan | Tier | Effort | Impact | Risks | Score | Notes |
|------|------|--------|--------|-------|-------|-------|
| [Graphics/SkyboxRenderPass.txt](Graphics/SkyboxRenderPass.txt) | Small | 1 | 4 | 0 | -3 | Fullscreen sky draw (Kloofendal IBL cubemap loaded but no sky pass; water reflections use a separate Ryfjallet map). Star field at night, sun/moon disc using existing fSunAngle. |
| [Engine/GradientParticlesAndCurlNoise.txt](Engine/GradientParticlesAndCurlNoise.txt) | Small | 2 | 3 | 0 | -1 | Lifetime color gradient for particles (white->orange->smoke). Curl noise in smoke spread shader. |
| [Graphics/WaterFoam.md](Graphics/WaterFoam.md) | Small | 2 | 4 | 1 | -1 | Whitecaps + shore foam as add-on to current water shader: Jacobian foam baked into WaterDisplacement.comp's free W channel, shore smoothstep, noise breakup; ~7 floats. |
| [Graphics/WaterSunGlitter.md](Graphics/WaterSunGlitter.md) | Small | 1 | 3 | 1 | -1 | Noise-jittered micro-normal sparkle added into the existing skybox specular; inherits shadow/height-darken; ~4 floats. |
| [Graphics/WaterSSS.md](Graphics/WaterSSS.md) | Small | 1 | 2 | 0 | -1 | Wave-crest additive turquoise tint (height × view-sun alignment), pure ALU; ~7 floats. |
| [Graphics/TerrainSnowCurvature.md](Graphics/TerrainSnowCurvature.md) | Small | 1 | 2 | 0 | -1 | Modulate the Gaea snow mask by the per-island AO texture (concavity proxy): snow gains in grooves, scours off ridges. Zero extra fetches; 1 float. |
| [Audio/AdaptiveMusic.txt](Audio/AdaptiveMusic.txt) | Small | 2 | 2 | 0 | 0 | GetNextMusicTrack considers game state (combat, location). Crossfade infrastructure exists. |
| [Network/RemoteEntityInterpolation.txt](Network/RemoteEntityInterpolation.txt) | Medium | 3 | 4 | 1 | 0 | Snapshot-based interpolation for remote entities. |
| [Graphics/WaterCaustics.md](Graphics/WaterCaustics.md) | Small | 2 | 3 | 1 | 0 | Two-layer noise min() cellular caustics added to the depth-LUT color in shallow water; ~8 floats. |
| [Agent/AgentQueryGlobalState.md](Agent/AgentQueryGlobalState.md) | Small | 2 | 3 | 1 | 0 | Read-only `query_fleets`/`query_clients`/`query_session` JSON arms over server-global (non-Frame) state — fleets, client slots, session/timestep. Extends the documented agent command surface; updates the agent-harness skill as a rider |
| [Graphics/DecalSystem.txt](Graphics/DecalSystem.txt) | Medium | 3 | 3 | 1 | 1 | Projected texture rendering for terrain marks (bullet holes, scorches, tracks). New pipeline pass. |
| [Engine/AddTracyProfiler.txt](Engine/AddTracyProfiler.txt) | Medium | 3 | 3 | 1 | 1 | Integrate Tracy profiler with existing profiling system. |
| [Graphics/FlipbookSpriteAnimations.txt](Graphics/FlipbookSpriteAnimations.txt) | Medium | 3 | 2 | 1 | 2 | Sprite-sheet animation for explosions (texture atlas with frame indexing). |
| [Graphics/DestructionBuffer.txt](Graphics/DestructionBuffer.txt) | Medium | 3 | 3 | 2 | 2 | Per-entity damage accumulation, fragment shader peels/blends sub-surface based on hit positions. |
| [Graphics/HeatDistortionAndShockwave.txt](Graphics/HeatDistortionAndShockwave.txt) | Medium | 3 | 3 | 2 | 2 | Screen-space UV distortion near heat/explosions; shockwave rings. Prerequisite: re-add pfSizePercents SOA member (CRC bump). Shares scene-color copy with WaterRefraction. |
| [Graphics/WaterRefraction.md](Graphics/WaterRefraction.md) | Medium | 3 | 3 | 2 | 2 | Scene-color copy + mid-render-pass split; wave-distorted sampling of the scene beneath shallow water. |
| [Network/ForwardErrorCorrection.txt](Network/ForwardErrorCorrection.txt) | Medium | 4 | 3 | 1 | 2 | XOR-based FEC for unreliable coord updates. |
| [Frame/Future_TagBasedGenericIteration.txt](Frame/Future_TagBasedGenericIteration.txt) | Medium | 3 | 2 | 1 | 2 | C++ concepts for generic cross-collection operations. Revisit when 3+ more game collections added (still 5). |
| [Frame/Future_EventMessageBus.txt](Frame/Future_EventMessageBus.txt) | Medium | 3 | 3 | 2 | 2 | Frame-scoped event queue decoupling collection communication. Revisit when fan-out exceeds 3-4 consumers. |
| [Engine/GrassRendering.txt](Engine/GrassRendering.txt) | Large | 4 | 3 | 2 | 3 | Instanced grass patches, vertex shader height, noise textures for dryness/height. New collection + shaders. |
| [Engine/TreePlacementAndRendering.txt](Engine/TreePlacementAndRendering.txt) | Large | 4 | 3 | 2 | 3 | Tree/bush placement with LOD. New collection and instanced rendering. |
| [Frame/Future_SharedBehaviorTraits.txt](Frame/Future_SharedBehaviorTraits.txt) | Large | 4 | 3 | 2 | 3 | Reusable SOA trait structs composed into collections via tuple_cat with shared logic functions. Revisit at 20-25 collections (currently 16, trending down). |
| [Frame/FrameRelativePositions.md](Frame/FrameRelativePositions.md) | Architectural | 5 | 5 | 3 | 3 | Convert world-absolute Frame positions to frame-relative (centered ±450 local) + camera-frame-relative render rebase at the `mRenderInterpolates` copy. Fixes float precision degradation at distance (~1cm jitter at 90 cells from origin); enables larger maps. Requires `Frame::kiVersion` bump. |
| [Audio/ReplaceDirectXTKAudioWithMiniaudio.txt](Audio/ReplaceDirectXTKAudioWithMiniaudio.txt) | Large | 5 | 3 | 2 | 4 | Replace audio engine with miniaudio. Enables cross-platform (Linux/macOS/iOS/Android). Deferred until cross-platform is a goal. |
| [Frame/Future_CollectionVariants.txt](Frame/Future_CollectionVariants.txt) | Large | 4 | 2 | 2 | 4 | Optional sparse SOA extensions for subset-only fields. No evidence of need currently. |
| [Engine/RmlUiPlayerFacingUi.md](Engine/RmlUiPlayerFacingUi.md) | Architectural | 5 | 3 | 2 | 4 | Adopt RmlUi (MIT, HTML/CSS) for the ~8 player-facing screens (~125 ImGui call sites): vendor lib (+FreeType decision), Vulkan `RenderInterface`, Wrapper data binding, per-screen `.rml` documents; TweaksScreen/ImPlot stay ImGui. Revisit-When gated (art direction outgrows the ImGui facelift). |

### Reference / Index Documents (not independently scheduled)

None currently.

## Dependencies

- `Graphics/HeatDistortionAndShockwave.txt` and `Graphics/WaterRefraction.md` both need a scene-color-copy texture in `RenderTargetTextures` — whichever lands first creates it, the other reuses it (verify the two copy points can share one texture). HDR resolve has landed: source scene copies from `gpSwapchainManager->mHdrTexture` (F16, pre-resolve), not the post-tonemap swapchain; HeatDistortion's `GetResolveImage` assumption is stale.
- `Frame/FrameRelativePositions.md` bumps the `Frame::kiVersion` base constant; `Documents/Plans/Frame/CrcVersionGateBump.md` bumps the same constant (115→116). Either order works, but land them as separate bumps — FrameRelativePositions takes whatever base it finds +1.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`Engine/Data/Shaders/Water/Water.frag` + `ShaderLayoutsBase.h` + `WaterWrappersBase.{h,cpp}` + `TweaksScreenWater.cpp` + `GlobalUniforms.cpp`**: `Graphics/WaterFoam.md`, `Graphics/WaterSunGlitter.md`, `Graphics/WaterCaustics.md`, `Graphics/WaterSSS.md`, `Graphics/WaterRefraction.md` — each adds 2-8 `GlobalLayout` floats and a slider block; align ordering to minimize diff churn. All five are independent add-ons to the current water renderer (no shared foundation, any order).
