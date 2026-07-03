Feature: Tree and Bush Placement with LOD Rendering
=====================================================

Context
-------
The engine renders terrain islands with elevation, color, normal, and AO
textures but has no vegetation.  Trees and bushes would add visual depth
and break up terrain flatness.  The existing ModelPipeline/DynamicPipelines
pattern (used by Spaceships, Players, Missiles) supports instanced model
rendering with per-instance storage buffers and indirect draw.  Islands
already provide CPU heightmap data via IslandTerrain for elevation queries.

Tree positions should be pre-baked by the DataPacker during island export
using Poisson disk sampling on the heightmap -- this avoids runtime
placement cost, guarantees determinism, and lets artists control density
per biome via a placement mask texture.  At runtime, trees are
client-only static visuals (no physics, no server simulation).

LOD strategy:
- Near: full 3D model via ModelPipeline (existing instanced path)
- Far: billboard impostor (camera-facing quad with pre-rendered texture)
- Very far: culled entirely

Decision (2026-07-03)
---------------------
Trees are rendered by a client-only TreeRenderer singleton owned by
Graphics (the Islands/gpIslands pattern), NOT a frame Collection<T>.
Rationale: no deterministic simulation code reads tree state (no
collision, no damage, no server relevance -- placement is baked offline
by the DataPacker), so Collection<T>'s machinery (dual-buffered frame
state, CRC/serialization, Collections() tuple registration, phase-hook
stubs, deterministic capacity growth) is all dead weight for a static
per-island dataset whose count never changes.  Islands is the direct
precedent: static world visuals live in Graphics, not the frame.
FrameBase.h is untouched.  Revisit only if gameplay later requires
sim-visible trees (e.g. destructible or blocking), in which case they
become a shared collection per the add-collection skill.

Why
---
- Pre-baked placement avoids runtime Poisson sampling and keeps the
  server build unaffected (trees are purely visual)
- Reusing ModelPipeline and DynamicPipelines means no new rendering
  infrastructure -- TreeRenderer drives the same instanced-model path
  Spaceships uses, plus a billboard pipeline
- Billboard impostors at distance keep draw call and vertex counts low
  while maintaining visual coverage
- The DataPacker already processes islands and can embed placement data
  in the island chunk with a version bump

Changes (15 files)
------------------

1. DataPacker/Source/ExportJobs/ExportIsland.h
   Add a placement mask texture constant (e.g. "PlacementMask.r32")
   alongside the existing elevation/color/normal/AO constants.  Bump
   GetVersion() to force re-export.

2. DataPacker/Source/ExportJobs/ExportIsland.cpp
   After exporting elevation data, add a tree placement phase:
   - Load the placement mask (grayscale density map, same resolution as
     elevation divided by a placement divisor)
   - Load the elevation heightmap (already available from earlier in
     Export())
   - Run Poisson disk sampling over the island area:
     - Skip positions below beach elevation (no underwater trees)
     - Skip positions where the placement mask is below a threshold
     - Skip positions where terrain slope (from elevation gradient)
       exceeds a steepness limit
     - Each accepted position stores: world X/Y, terrain elevation,
       a random rotation angle, a random scale factor, and a type
       index (tree vs bush, selected by elevation band or mask value)
   - Write the placement array into the chunk:
     - int32_t count
     - Per tree: float x, float y, float elevation, float rotation,
       float scale, uint8_t typeIndex
   The Poisson disk sampler should be a simple utility function in
   ExportIsland.cpp (not a separate file) using a grid-accelerated
   dart-throwing approach with a fixed seed for determinism.

3. Common/ChunkHeader.h  (or wherever ChunkFlags is defined)
   No change needed -- island chunks already have a flags field.  The
   placement data is appended after the existing heightmap data in the
   island chunk.  IslandTerrain reads it by position.

4. Engine/Source/Frame/IslandTerrain.h
   Add members to store loaded tree placement data per island:
     struct TreePlacement
     {
         float fX;
         float fY;
         float fElevation;
         float fRotation;
         float fScale;
         uint8_t uiTypeIndex;
     };
     std::vector<TreePlacement> mTreePlacements;

5. Engine/Source/Frame/IslandTerrain.cpp
   After reading the heightmap from the island chunk, read the tree
   placement count and array from the chunk data.  Populate
   mTreePlacements.

6. Engine/Data/Shaders/ShaderLayoutsBase.h
   TreeLayout is not needed -- trees use the existing ModelLayout struct
   (same as Spaceships: position, 3x4 transform, normal transform,
   color add, mesh data base).

   For billboard impostors, add a TreeBillboardLayout after ModelLayout:
     struct TreeBillboardLayout
     {
         vec4 f4PositionAndSize INIT;   // xyz = world pos, w = size
         vec4 f4TexcoordAndAlpha INIT;  // xy = atlas offset, zw = atlas size
     };

7. Engine/Data/Shaders/Trees/TreeBillboard.vert  (new file)
   Vertex shader for billboard impostors.  Takes TreeBillboardLayout
   from a storage buffer, expands a camera-facing quad per instance.
   Uses the existing QuadsFullscreen vertex input but overrides the
   position to be a world-space billboard anchored at the tree base.

8. Engine/Data/Shaders/Trees/TreeBillboard.frag  (new file)
   Fragment shader sampling the impostor atlas texture.  Alpha test
   discard for tree silhouette edges.  Apply basic directional
   lighting from globalLayout sun direction.

9. Engine/Source/Graphics/Trees/TreeRenderer.h  (new file)
   Client-only singleton class managing tree rendering (no frame
   collection, no FrameBase.h change -- see Decision above).  Follows
   the Islands pattern: whole-file client affinity via client-vcxproj
   membership; the .cpp is #if defined(BT_CLIENT)-wrapped like
   Islands.cpp:

     class TreeRenderer
     {
     public:
         TreeRenderer();
         ~TreeRenderer();

         void LoadPlacements(const IslandTerrain& rTerrain);
         void GraphicsResources();
         void BeginRender(int64_t iCommandBuffer, ...);
         void Render(int64_t iCommandBuffer);
         void EndRender(int64_t iCommandBuffer);

         // Per-island tree data
         struct IslandTrees
         {
             std::vector<IslandTerrain::TreePlacement> placements;
         };
         std::vector<IslandTrees> mIslandTrees;
     };

     inline TreeRenderer* gpTreeRenderer = nullptr;

   Register tree/bush model types (tree model CRCs from data constants)
   during construction or GraphicsResources().

10. Engine/Source/Graphics/Trees/TreeRenderer.cpp  (new file)
    Constructor/destructor, LoadPlacements, and the render
    implementation, reusing the Spaceships instanced-model machinery:

    GraphicsResources():
    - CreateDynamicBuffer for model LOD (sizeof ModelLayout)
    - CreateModelPipeline + CreateModelPipelineShadow for the tree model
    - CreateDynamicBuffer for billboard LOD (sizeof TreeBillboardLayout)
    - Create a DynamicPipeline for billboard rendering (new pipeline type
      or use CreatePipelineBillboards variant)

    BeginRender():
    - Accumulate total tree count from all active islands
    - ResizeDynamicBufferIfNeeded for both model and billboard buffers
    - Update storage buffer descriptors if resized

    Render():
    - For each tree placement in the current island:
      - Compute distance to camera
      - If within near LOD threshold:
        - Build model transform matrix from placement (position on
          terrain, rotation, scale)
        - Write ModelLayout to the model storage buffer
        - Increment model instance count
      - Else if within far LOD threshold:
        - Write TreeBillboardLayout to the billboard storage buffer
        - Increment billboard instance count
      - Else: culled (too far)
    - Use multithreading dispatch for large tree counts

    EndRender():
    - WriteIndirectBuffer for model pipeline (near LOD count)
    - WriteIndirectBuffer for billboard pipeline (far LOD count)

11. Engine/Source/Graphics/Graphics.h
    Add #include for TreeRenderer.h and forward declaration.
    Add TreeRenderer member to Graphics class (owned, created after
    Islands in the initialization order).

12. Engine/Source/Graphics/Graphics.cpp
    Create gpTreeRenderer after gpIslands in the constructor.
    Call LoadPlacements() after island terrain data is loaded.
    Destroy gpTreeRenderer before gpIslands in the destructor.
    Call TreeRenderer GraphicsResources/BeginRender/Render/EndRender
    from the appropriate Graphics render orchestration points.

13. Engine/Source/Graphics/Managers/CommandBufferManager.cpp
    Add RecordDrawIndirect calls for tree model and billboard
    pipelines in RecordMainCommandBuffer.  Insert after existing
    model pipeline draws (the shadow and opaque passes).  Billboard
    trees render after opaque objects but before transparent objects.

14. Engine/Source/Graphics/Managers/DynamicPipelines.h
    Add kDynamicPipelineTreeBillboard to the DynamicPipelineType enum.
    Add a CreatePipelineTreeBillboard method, or reuse an existing
    billboard pipeline creation path if the vertex format matches.

15. Engine/Source/Profile/ProfileManagerBase.h
    Add GPU and CPU timers for tree rendering:
      kGpuTimerTreeModels
      kGpuTimerTreeBillboards
      kCpuTimerRenderTrees

Notes
-----
- Trees are entirely client-only -- no server simulation, no frame
  state, no serialization, no CRC.  This keeps the server build
  completely unaffected
- The TreeRenderer singleton pattern matches Islands (gpIslands) rather
  than the collection pattern (Spaceships, Players) because trees are
  static world decoration, not dynamic game entities
- The DataPacker placement phase uses a fixed random seed per island
  so tree positions are deterministic across builds
- Billboard impostor textures should be pre-rendered offline (or at
  load time via a one-shot render-to-texture pass) showing the tree
  model from multiple angles.  A simple approach: render the tree model
  to a small atlas texture from 8 directions; at runtime select the
  two nearest angles and blend or just pick the closest
- LOD distance thresholds should be exposed as Wrapper tweakables in
  TweaksScreen for tuning
- The placement mask texture is optional -- if absent, the DataPacker
  places trees everywhere above beach elevation with default density.
  This allows islands without masks to still get vegetation
- Wind animation: vertex shader can apply a simple sine-based sway
  using the wind direction from globalLayout.  This is a visual-only
  effect with no gameplay impact
- Future work: Simplygon or meshoptimizer for automatic LOD mesh
  generation, SpeedTree integration, seasonal color variation, falling
  leaves particle effect
- Shadow pass support: tree models cast shadows via the existing
  ModelPipelineShadow path.  Billboard trees do not cast shadows (too
  low quality at that distance to matter)
- The existing island chunk format appends placement data after the
  heightmap.  Reading code in IslandTerrain.cpp checks remaining chunk
  size to handle islands exported before the version bump (no placement
  data = zero trees, graceful fallback)
