Feature: Projected decal system for terrain marks
====================================================

Context
-------
The engine has no decal system.  Terrain marks (explosions, tire
tracks, scorch marks, impact craters) require projecting textures
onto the terrain surface.  The terrain renders as a fullscreen quad
compositing G-buffer textures (Terrain.frag), with elevation, color,
normal, and AO render targets already available.

Decals are projected quads positioned in world space, oriented to
the terrain surface normal, and rendered after terrain but before
water in the main render pass.  Each decal samples the terrain
elevation texture to conform to terrain height and blends its
color/normal contribution onto the existing terrain output.

The Billboards collection is the closest pattern: a client-only
Sync-based collection with SOA layout, dynamic GPU storage buffer,
BeginRender/Render/EndRender three-phase pipeline, and a
DynamicPipelines entry.  Decals differ in that they are world-space
projected (using view/projection matrices) rather than screen-space,
and they sample the terrain elevation texture to drape onto the
surface.

CAVEAT: Billboards source files were NOT found under
Engine/Source/Frame/Collections/ (only referenced from
FrameBase.h:78,189) -- verify the Billboards source location before
using it as a template.

Reference: https://www.youtube.com/watch?v=wwLW6CjswxM&list=WL&index=2

Why
---
- Explosions, projectile impacts, and vehicle tracks need visible
  terrain marks for gameplay feedback and visual quality
- The terrain G-buffer (elevation, normal) is already rendered and
  available for decal projection at minimal additional cost
- Following the existing Billboards collection pattern keeps the
  implementation consistent with the codebase architecture

Changes (15 files)
------------------

1. Engine/Source/Frame/Collections/Decals/Decals.h  [NEW]
   Client-only (#ifdef BT_CLIENT) collection header following the
   Billboards pattern.  Define:

   - DecalsType struct: crc (texture), fSizeX/fSizeY (world-space
     dimensions), fAlpha
   - DecalsInterpolate: Collection<DecalsInterpolate, CollectionFlags::kIdToIndex>
     with TypeRegistry<DecalsType>.  SOA members: puiTypeIndices (uint8_t*),
     pfRotations (float*), pfAlpha (float*), pVecPositions (XMVECTOR*).
     SyncData struct with vecPosition, uiTypeIndex, fRotation, fAlpha.
     Static methods: Register, AllocateAndCopy, Sync, Update,
     GraphicsResources, BeginRender, Render, EndRender
   - DecalsPostRender: Collection<DecalsPostRender> with puiIds member.
     Static methods: AllocateAndCopy, Update, PreCollision, Add, Remove,
     PostCollision, AreaDamage, Transfer, Destroy, Spawn
   - extern template declarations for both collections
   - using decal_t = DecalsInterpolate::id_t

2. Engine/Source/Frame/Collections/Decals/Decals.cpp  [NEW]
   Core lifecycle following Billboards.cpp pattern:
   - Template instantiations for both collections
   - Register() -- empty initially, types registered by game layer
   - AllocateAndCopy for both structs (Allocate + memcpy for
     Interpolate, AllocateAndCopyIds for PostRender)
   - Spawn/Transfer/Destroy stubs

3. Engine/Source/Frame/Collections/Decals/DecalsUpdate.cpp  [NEW]
   Following BillboardsUpdate.cpp pattern:
   - Update() -- empty stub
   - Sync() -- copy SyncData fields to SOA arrays by IdToIndex
   - Add() -- GrowPairedCollections + AddVisualIndexableElement,
     set type index
   - Remove() -- RemoveIndexableElement, clear id
   - PreCollision/PostCollision/AreaDamage -- empty stubs

4. Engine/Source/Frame/Collections/Decals/DecalsRender.cpp  [NEW]
   Following BillboardsRender.cpp pattern:
   - GraphicsResources() -- CreateDynamicBuffer with DecalLayout size,
     CreatePipelineDecals (new DynamicPipelines method)
   - BeginRender() -- AccumulateRenderCapacity across active coords,
     ResizeDynamicBufferIfNeeded, update storage buffer descriptor
   - Render() -- For each decal: load SOA fields, look up type,
     compute world-space quad corners from position + rotation + size,
     transform to clip space via view*projection, populate DecalLayout
     in storage buffer (f4Position as world pos, fSizeX/fSizeY,
     fRotation, fTextureIndex via CrcToIndex, fAlpha)
   - EndRender() -- SetCount for profiling, WriteIndirectBuffer

5. Engine/Data/Shaders/ShaderLayoutsBase.h
   Add DecalLayout struct (after BillboardLayout):

     struct DecalLayout
     {
         vec4 f4Position INIT;    // world-space XYZ, W=1
         float fSizeX INIT;
         float fSizeY INIT;
         float fTextureIndex INIT;
         float fRotation INIT;
         float fAlpha INIT;
         float fPadding1 INIT;
         float fPadding2 INIT;
         float fPadding3 INIT;
     };

6. Engine/Data/Shaders/Decals/Decals.vert  [NEW]
   World-space projected decal vertex shader:
   - Bind global uniform (set 0, binding 0), main uniform (set 0,
     binding 1), decals storage buffer (set 1, binding 2), terrain
     elevation sampler (set 1, binding 3)
   - Input: f2InQuadVertex (quad vertex buffer)
   - Per-instance: read DecalLayout, compute world-space quad corner
     from position + size + rotation, sample terrain elevation texture
     to get Y height at the quad corner's XZ, set vertex Y to terrain
     height, transform through view * projection
   - Output: iOutInstanceIndex, f2OutTexcoord

7. Engine/Data/Shaders/Decals/Decals.frag  [NEW]
   Decal fragment shader:
   - Bind decals storage buffer (set 1, binding 2), bindless textures
     (set 0, binding 4), sampler (set 0, binding 12)
   - Sample decal texture using nonuniformEXT indexing (same pattern
     as Billboards.frag)
   - Multiply output alpha by per-instance fAlpha
   - Output: f4OutColor with alpha blending

8. Engine/Source/Graphics/Managers/DynamicPipelines.h
   - Add kDynamicPipelineDecals to DynamicPipelineType enum (before
     kDynamicPipelineCount)
   - Add CreatePipelineDecals method declaration with same signature
     as CreatePipelineBillboards (crc, name, iBufferSize)

9. Engine/Source/Graphics/Managers/DynamicPipelines.cpp
   Implement CreatePipelineDecals following CreatePipelineBillboards:
   - Guard against duplicate crc
   - CreateDynamicBuffer for storage
   - Create Pipeline with flags: kIndirectHostVisible, kAlphaBlend,
     kUpdateAfterBind, kDepthTestOnly (read depth, no write)
   - Shaders: Decals.vert + Decals.frag
   - Vertex buffer: mQuadsVertexBuffer
   - Descriptors: global uniform (binding 0), main uniform (binding 1),
     storage buffer (binding 2), terrain elevation sampler (binding 3),
     sampler + bindless textures
   - Register in mPipelineMaps[kDynamicPipelineDecals]

10. Engine/Source/Graphics/Managers/CommandBufferManager.cpp
    In RecordMainCommandBuffer, add decal rendering after terrain
    (kPipelineTerrain) and before water (kPipelineWater).  Insert
    between the terrain and water draws (the old 703/705 line
    anchors are stale):

      gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerDecals);
      for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineDecals])
      {
          pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
      }
      gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerDecals);

11. Engine/Source/Frame/FrameBase.h
    - Add #include for Decals/Decals.h in the BT_CLIENT include block
    - Add DecalsInterpolate decals {} member to FrameInterpolateBase
      (in the BT_CLIENT block, alphabetically after billboards)
    - Add DecalsPostRender decals {} member to FramePostRenderBase
      (in the BT_CLIENT block, alphabetically after billboards)
    - Add rSelf.decals to both Collections() tuples (after billboards)
    - Increment kCollectionCount from 11 to 12 in both structs
    - Add DecalsInterpolate to InterpolateRenderTypes type list

12. Engine/Source/Profile/ProfileManagerBase.h
    - Add kCpuCounterDecals and kCpuCounterDecalsRendered to
      EngineCpuCounters enum (after the billboards entries)
    - Add kGpuTimerDecals to the GPU timer enum (after
      kGpuTimerTerrain, before kGpuTimerWater, matching render order)

13. Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj
    Add the four new Decals source files (Decals.h, Decals.cpp,
    DecalsUpdate.cpp, DecalsRender.cpp) and two shader files
    (Decals.vert, Decals.frag) to the project.

14. Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj.filters
    Create filter "Engine\Frame\Collections\Decals" with a new GUID.
    Add the four source files under that filter.  Add the two shader
    files under "Engine\Data\Shaders\Decals" filter.

15. Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandboxServer.vcxproj
    and BrokenEngineSandboxServer.vcxproj.filters
    Add the same source files.  The #ifdef BT_CLIENT guards in Decals.h
    will compile them to empty translation units on the server build.

Notes
-----
- Decals render after terrain, before water, so they appear on the
  terrain surface but under water when submerged
- The terrain elevation sampler in the decal vertex shader lets quads
  conform to terrain height without CPU terrain queries
- Alpha blending with depth-test-only (no depth write) prevents decals
  from interfering with subsequent depth-tested geometry (water, objects)
- Decal lifetime management is handled by parent collections (e.g.,
  Explosions spawn a decal on impact, remove it after a fade timer)
- Future enhancements: normal-map decals that modify terrain normals,
  decal fade-out over time via alpha animation, terrain-aware UV
  projection for non-flat surfaces
