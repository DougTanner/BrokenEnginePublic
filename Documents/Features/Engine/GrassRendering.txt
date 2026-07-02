Feature: Instanced grass rendering on terrain
===============================================

Context
-------
The engine has no vegetation system.  Grass is a visual-only system
that renders instanced blade quads across terrain, displaced to
terrain height, colored by terrain color, and animated by the existing
wind texture.  The entire system is client-only (#ifdef BT_CLIENT)
and compiles out of the server build.

Grass is NOT a collection -- it has no per-frame game state, no
sync/add/remove lifecycle, no serialization, and no dual-buffered
frames.  It is a purely GPU-driven visual effect.  A new GrassManager
singleton (like Islands) owns the grass blade mesh, storage buffer,
pipeline, and per-frame rendering.  Blade instance positions are
generated once at startup (or on island change) and stored in a
static GPU buffer.  The vertex shader samples the terrain elevation
and wind textures each frame for height displacement and sway
animation.

The Terrain.vert pattern shows how to sample the elevation texture
(set 1, binding 5) to get world-space height.  Wind textures are
RG16F render targets (mWindTextureOne/Two) already bound in the
Terrain.frag pipeline.  The grass pipeline binds elevation and wind
as combined image samplers.

Grass renders after terrain and before water in the image render pass,
using alpha-to-coverage for soft blade edges without requiring
transparency sorting.

Why
---
- Grass and ground vegetation are essential for visual quality in
  outdoor scenes; bare terrain looks flat and lifeless
- Instanced rendering with a storage buffer of blade positions is
  efficient and follows the engine's existing GPU-driven patterns
- The wind texture is already computed each frame and can drive
  grass sway at no additional simulation cost
- Alpha-to-coverage avoids the need for transparency sorting while
  still producing soft blade edges with MSAA

Changes (13 files)
------------------

1. Engine/Source/Graphics/Managers/GrassManager.h  [NEW]
   Client-only (#ifdef BT_CLIENT) grass rendering manager.  Define:

   - GrassBladeInstance struct: vec4 f4PositionRotation (XY world
     pos, Z unused, W rotation), packed into a static storage buffer
   - GrassManager class with:
     - Create() / Destroy() -- allocate/free GPU resources
     - GenerateBladePositions() -- fill storage buffer with blade
       instances distributed across terrain.  Uses a grid with jitter,
       samples IslandTerrain CPU elevation to skip water/cliff areas.
       Called on startup and when active islands change
     - PopulateUniforms() -- write per-frame grass uniform data
       (camera frustum, wind strength, time, blade dimensions)
     - BeginRender/EndRender -- called from CommandBufferManager
       image pass
     - Members: storage buffer for blade instances, uniform buffer
       for per-frame grass params, Pipeline*, instance count
   - extern GrassManager* gpGrassManager

2. Engine/Source/Graphics/Managers/GrassManager.cpp  [NEW]
   Implementation:

   - Create(): Create storage buffer for blade instances (VMA
     device-local), create uniform buffer (host-visible per
     command buffer), create Pipeline via PipelineManager
   - GenerateBladePositions(): Grid-based placement with random
     jitter.  For each cell: sample IslandTerrain::Elevation() to
     get terrain height, skip if below water level or slope too
     steep (from IslandTerrain::Normal()), assign random rotation.
     Upload positions to storage buffer via staging buffer
   - PopulateUniforms(): Write elapsed time, wind strength, blade
     height/width, camera visible area to uniform buffer
   - Destroy(): Clean up all GPU resources

3. Engine/Data/Shaders/Grass/Grass.vert  [NEW]
   Instanced grass blade vertex shader:
   - Bind global uniform (set 0, binding 0), main uniform (set 0,
     binding 1), grass instances storage buffer (set 1, binding 0),
     grass uniform (set 1, binding 1), terrain elevation sampler
     (set 1, binding 2), wind sampler (set 1, binding 3)
   - Input: f2InQuadVertex from quad vertex buffer
   - Per-instance: read blade position/rotation from storage buffer
   - Compute blade world-space base position, sample elevation
     texture to get terrain height at blade XY
   - Apply blade shape: bottom vertices at terrain height, top
     vertices offset upward by blade height
   - Sample wind texture at blade XY position (using same
     WorldToSmokeTexcoord mapping as smoke/wind systems), apply
     wind displacement to top vertices only (grass bends from base)
   - Add subtle per-blade variation using rotation for phase offset
     in wind sway
   - Transform through view * projection
   - Output: f2OutTexcoord, fOutHeightFactor (0 at base, 1 at tip)

4. Engine/Data/Shaders/Grass/Grass.frag  [NEW]
   Grass blade fragment shader:
   - Bind global uniform (set 0, binding 0), terrain color sampler
     (set 1, binding 4)
   - Input: f2OutTexcoord, fOutHeightFactor
   - Sample terrain color texture at blade base texcoord to tint
     grass to match surrounding terrain
   - Apply height-based color gradient (darker at base, lighter at
     tip) for depth
   - Alpha channel from blade texture for alpha-to-coverage edge
     softness
   - Output: f4OutColor

5. Engine/Data/Shaders/ShaderLayoutsBase.h
   Add GrassLayout struct (after BillboardLayout):

     struct GrassLayout
     {
         float fBladeHeight INIT;
         float fBladeWidth INIT;
         float fWindStrength INIT;
         float fTime INIT;
     };

6. Engine/Source/Graphics/Managers/DynamicPipelines.h
   - Add kDynamicPipelineGrass to DynamicPipelineType enum (before
     kDynamicPipelineCount)
   - Add CreatePipelineGrass method declaration

7. Engine/Source/Graphics/Managers/DynamicPipelines.cpp
   Implement CreatePipelineGrass:
   - Create Pipeline with flags: kIndirectHostVisible,
     kAlphaToCoverage, kUpdateAfterBind, kSampleShading,
     kCullNone (grass blades visible from both sides)
   - Shaders: Grass.vert + Grass.frag
   - Vertex buffer: mQuadsVertexBuffer (same quad mesh as billboards)
   - Descriptors: global uniform (binding 0), main uniform (binding 1),
     grass instances storage buffer (binding 0 set 1), grass uniform
     (binding 1 set 1), terrain elevation sampler (binding 2 set 1),
     wind sampler (binding 3 set 1), terrain color sampler (binding 4
     set 1)
   - Register in mPipelineMaps[kDynamicPipelineGrass]

8. Engine/Source/Graphics/Managers/CommandBufferManager.cpp
   In RecordMainCommandBuffer image pass, add grass rendering after
   terrain (kGpuTimerTerrain) and before water (kGpuTimerWater).
   Insert after the kGpuTimerTerrain block, before kGpuTimerWater:

     gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerGrass);
     for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineGrass])
     {
         pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
     }
     gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerGrass);

9. Engine/Source/Profile/ProfileManagerBase.h
   - Add kGpuTimerGrass to GpuTimers enum (after kGpuTimerTerrain,
     before kGpuTimerWater)
   - Add corresponding entry in mGpuTimers initializer:
     {.name = "    Grass"}

10. Engine/Source/Graphics/Graphics.cpp
    In Graphics::Create(), construct GrassManager after Islands and
    PipelineManager (grass needs terrain data and pipeline creation).
    In Graphics::Destroy(), destroy before PipelineManager.  Add
    GrassManager::GenerateBladePositions() call after islands are
    loaded.

11. Engine/Source/Graphics/Render/RenderMain.cpp (or equivalent)
    In the per-frame main render population, call
    gpGrassManager->PopulateUniforms() to write the grass uniform
    buffer with current frame time and wind parameters.

12. Engine/Source/Graphics/Graphics.h (or Engine.h)
    Add #include for GrassManager.h in the BT_CLIENT block.
    Declare extern GrassManager* gpGrassManager.

13. BrokenEngineSandbox.vcxproj + BrokenEngineSandboxServer.vcxproj
    Add new .h/.cpp files to appropriate filters.  Server vcxproj
    does not need GrassManager files (BT_CLIENT gated).

Notes
-----
- Blade instance positions are static -- generated once per island
  configuration.  Dynamic grass density or LOD is a future extension
- Alpha-to-coverage requires MSAA to be enabled (the engine already
  uses sample shading via kSampleShading)
- Wind displacement uses the same texcoord mapping as the smoke/wind
  systems (WorldToSmokeTexcoord) so grass sway matches particle and
  smoke movement
- Grass density and blade dimensions should be exposed in the UI
  settings (UiManager) for runtime tuning
- Distance-based LOD (fade out grass beyond a threshold) can be
  added in the vertex shader by comparing blade position to camera
  eye position and scaling blade height to zero
- The terrain color sampler gives grass a natural color match to the
  surrounding ground without needing separate grass color textures
- Consider adding a noise-based height variation and dryness/color
  variation texture in a follow-up pass for visual richness
- The IslandTerrain elevation/normal accessor names used above
  (Elevation()/Normal()) are speculative -- verify against the
  current IslandTerrain header at execution time
