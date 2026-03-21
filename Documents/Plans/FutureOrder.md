# Future Plans

Brand new features and eventual/maybe plans. Sorted by score (lowest = highest priority).

Score = Effort - Impact + Risks (lower = higher priority)

## Plans Ready to Execute

Ideas evaluated from Engine.txt and triaged against the current codebase (2026-03-21).

| # | Plan File | Scope | Description | Effort | Impact | Risks | Score |
|---|-----------|-------|-------------|--------|--------|-------|-------|
| 1 | Graphics/SkyboxRenderPass.txt | Small | Fullscreen skybox draw (cubemap exists, only used for reflections). Star field at night, sun/moon disc using existing fSunAngle. | 1 | 4 | 0 | -3 |
| 2 | Graphics/HdrResolveAndColorGrading.txt | Medium | Render to F16 intermediate, ACES tone mapping resolve (dead code exists in Model.frag), 3D LUT color grading. Unlocks all HDR effects. | 3 | 5 | 1 | -1 |
| 3 | Graphics/ReverseZDepth.txt | Small | Flip min/max depth, GREATER_EQUAL compare, clear to 0. Eliminates z-fighting. Contained change. | 2 | 4 | 1 | -1 |
| 4 | Graphics/HeatDistortionAndShockwave.txt | Small | Screen-space UV distortion near heat/explosions. Displacement-map shockwave for explosions. | 2 | 3 | 0 | -1 |
| 5 | Engine/GradientParticlesAndCurlNoise.txt | Small | Lifetime color gradient for particles (white->orange->smoke). Curl noise in smoke spread shader. | 2 | 3 | 0 | -1 |
| 6 | Graphics/TerrainSnowInGrooves.txt | Small | Curvature-aware snow in Terrain.frag using AO/normal derivatives instead of color-based detection. | 1 | 2 | 0 | -1 |
| 7 | Misc/SpirvOptIntegration.txt | Small | Add spirv-opt pass to DataPacker shader compilation. Reduces shader size, improves driver compile. | 1 | 2 | 0 | -1 |
| 8 | Engine/AddImPlot.txt | Small | Add real-time plotting extension for performance visualization. | 1 | 2 | 0 | -1 |
| 9 | Misc/AddMeshoptimizer.txt | Medium | Replace custom vertex dedup with meshoptimizer library. | 2 | 4 | 1 | -1 |
| 10 | Graphics/WaterFoamAndRefraction.txt | Medium | Foam/whitecaps from wave steepness, refraction via UV-distorted scene sampling. | 3 | 4 | 1 | 0 |
| 11 | Audio/AdaptiveMusic.txt | Small | GetNextMusicTrack considers game state (combat, location). Crossfade infrastructure exists. | 2 | 2 | 0 | 0 |
| 12 | Engine/WindSmokeAdvection.txt | Small | Add wind advection parameter for smoke simulation. | 1 | 1 | 0 | 0 |
| 13 | Network/NetworkMetricsAndAdaptiveBuffering.txt | Medium | Packet loss/jitter metrics, adaptive reconciliation buffering. | 3 | 3 | 0 | 0 |
| 14 | Network/RemoteEntityInterpolation.txt | Medium | Snapshot-based interpolation for remote entities. | 3 | 4 | 1 | 0 |
| 15 | Network/SoftDesyncRecovery.txt | Medium | Soft recovery via full state re-download instead of disconnect. | 3 | 4 | 1 | 0 |
| 16 | Misc/FlatContainersOptimization.txt | Small | Replace std::unordered_map with std::flat_map in hot paths. **BLOCKED**: MSVC 14.50 lacks \<flat_map\>. | 3 | 3 | 1 | 1 |
| 17 | Graphics/DecalSystem.txt | Medium | Projected texture rendering for terrain marks (bullet holes, scorches, tracks). New pipeline pass. | 3 | 3 | 1 | 1 |
| 18 | Engine/AddTracyProfiler.txt | Medium | Integrate Tracy profiler with existing profiling system. | 3 | 3 | 1 | 1 |
| 19 | Misc/LinterToolingForStyleGuide.txt | Medium | clang-format and clang-tidy for style enforcement. | 3 | 2 | 0 | 1 |
| 20 | Graphics/FlipbookSpriteAnimations.txt | Medium | Sprite-sheet animation for explosions (texture atlas with frame indexing). | 3 | 2 | 1 | 2 |
| 21 | Graphics/DestructionBuffer.txt | Medium | Per-entity damage accumulation, fragment shader peels/blends sub-surface based on hit positions. | 3 | 3 | 2 | 2 |
| 22 | Network/ForwardErrorCorrection.txt | Medium | XOR-based FEC for unreliable coord updates. | 4 | 3 | 1 | 2 |
| 23 | Engine/GrassRendering.txt | Large | Instanced grass patches, vertex shader height, noise textures for dryness/height. New collection + shaders. | 4 | 3 | 2 | 3 |
| 24 | Engine/TreePlacementAndRendering.txt | Large | Tree/bush placement with LOD. New collection and instanced rendering. | 4 | 3 | 2 | 3 |
| 25 | Frame/FrameRelativePositions.txt | Large | Convert world-absolute to frame-relative positions. | 5 | 5 | 3 | 3 |
| 26 | Audio/ReplaceDirectXTKAudioWithMiniaudio.txt | Large | Replace audio engine with miniaudio. | 5 | 3 | 2 | 4 |

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
