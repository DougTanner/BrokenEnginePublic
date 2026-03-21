# Future Plans

Brand new features and eventual/maybe plans. Sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

## Plan Ideas

Ideas evaluated from Engine.txt and triaged against the current codebase (2026-03-21).
These are plan ideas — plan files have not yet been written for them.

| # | Plan Idea | Effort | Impact | Risks | Score | Scope | Description |
|---|-----------|--------|--------|-------|-------|-------|-------------|
| 1 | SkyboxRenderPass | 1 | 4 | 0 | -3 | Small | Fullscreen skybox draw (cubemap exists, only used for reflections). Star field at night, sun/moon disc using existing fSunAngle. |
| 2 | HdrResolveAndColorGrading | 3 | 5 | 1 | -1 | Medium | Render to F16 intermediate, ACES tone mapping resolve (dead code exists in Model.frag), 3D LUT color grading. Unlocks all HDR effects. |
| 3 | ReverseZDepth | 2 | 4 | 1 | -1 | Small | Flip min/max depth, GREATER_EQUAL compare, clear to 0. Eliminates z-fighting. Contained change. |
| 4 | HeatDistortionAndShockwave | 2 | 3 | 0 | -1 | Small | Screen-space UV distortion near heat/explosions. Displacement-map shockwave for explosions. |
| 5 | GradientParticlesAndCurlNoise | 2 | 3 | 0 | -1 | Small | Lifetime color gradient for particles (white->orange->smoke). Curl noise in smoke spread shader. |
| 6 | TerrainSnowInGrooves | 1 | 2 | 0 | -1 | Small | Curvature-aware snow in Terrain.frag using AO/normal derivatives instead of color-based detection. |
| 7 | SpirvOptIntegration | 1 | 2 | 0 | -1 | Small | Add spirv-opt pass to DataPacker shader compilation. Reduces shader size, improves driver compile. |
| 8 | WaterFoamAndRefraction | 3 | 4 | 1 | 0 | Medium | Foam/whitecaps from wave steepness, refraction via UV-distorted scene sampling, mesh LOD clipmap. |
| 9 | FlatContainersOptimization | 2 | 3 | 1 | 0 | Small | Replace std::unordered_map with std::flat_map in hot paths (Collection::idToIndexMap, render-state maps). |
| 10 | AdaptiveMusic | 2 | 2 | 0 | 0 | Small | GetNextMusicTrack considers game state (combat, location). Crossfade infrastructure exists. |
| 11 | DecalSystem | 3 | 3 | 1 | 1 | Medium | Projected texture rendering for terrain marks (bullet holes, scorches, tracks). New pipeline pass. |
| 12 | FlipbookSpriteAnimations | 3 | 2 | 1 | 2 | Medium | Sprite-sheet animation for explosions (texture atlas with frame indexing). |
| 13 | DestructionBuffer | 3 | 3 | 2 | 2 | Medium | Per-entity damage accumulation, fragment shader peels/blends sub-surface based on hit positions. |
| 14 | GrassRendering | 4 | 3 | 2 | 3 | Large | Instanced grass patches, vertex shader height, noise textures for dryness/height. New collection + shaders. |
| 15 | TreePlacementAndRendering | 4 | 3 | 2 | 3 | Large | Tree/bush placement with LOD. New collection and instanced rendering. |

## Existing Plans Ready to Execute

| # | Plan File | Description | Effort | Impact | Risks | Score |
|---|-----------|-------------|--------|--------|-------|-------|
| 1 | Engine/AddImPlot.txt | Add real-time plotting extension for performance visualization | 1 | 2 | 0 | -1 |
| 2 | Misc/AddMeshoptimizer.txt | Replace custom vertex dedup with meshoptimizer library | 2 | 4 | 1 | -1 |
| 3 | Engine/WindSmokeAdvection.txt | Add wind advection parameter for smoke simulation | 1 | 1 | 0 | 0 |
| 4 | Network/NetworkMetricsAndAdaptiveBuffering.txt | Packet loss/jitter metrics, adaptive reconciliation buffering | 3 | 3 | 0 | 0 |
| 5 | Network/RemoteEntityInterpolation.txt | Snapshot-based interpolation for remote entities | 3 | 4 | 1 | 0 |
| 6 | Network/SoftDesyncRecovery.txt | Soft recovery via full state re-download instead of disconnect | 3 | 4 | 1 | 0 |
| 7 | Engine/AddTracyProfiler.txt | Integrate Tracy profiler with existing profiling system | 3 | 3 | 1 | 1 |
| 8 | Misc/LinterToolingForStyleGuide.txt | clang-format and clang-tidy for style enforcement | 3 | 2 | 0 | 1 |
| 9 | Network/ForwardErrorCorrection.txt | XOR-based FEC for unreliable coord updates | 4 | 3 | 1 | 2 |
| 10 | Frame/FrameRelativePositions.txt | Convert world-absolute to frame-relative positions | 5 | 5 | 3 | 3 |
| 11 | Audio/ReplaceDirectXTKAudioWithMiniaudio.txt | Replace audio engine with miniaudio | 5 | 3 | 2 | 4 |

## Declined / Not Applicable

Ideas from Engine.txt evaluated and rejected (already implemented, out of scope, or declined):

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
- New Weapon Types — declined
