Add: Flipbook sprite-sheet animations for explosions and VFX
=============================================================

Context
-------
Explosions currently use GPU compute particles and point lights but have no
large-scale fireball / shockwave visual. Flipbook (sprite-sheet) animations
are the standard solution: a single texture atlas containing a grid of
animation frames, rendered on a world-space camera-facing quad that advances
through frames over time. Free flipbook sheets are available from EmberGen
(https://jangafx.com/software/embergen/download/free-vdb-animations/) and
Unity (https://blog.unity.com/technology/free-vfx-image-sequences-flipbooks).

The engine already has two collection patterns to build on:
  - Billboards: screen-space quads with per-instance storage buffer, bindless
    texture lookup, alpha blending, and the kDynamicPipelineBillboards pipeline
  - Puffs: world-space fire-and-forget quads using the Controller pattern with
    keyframe animation, visibility culling, and auto-destroy on expiry

Flipbooks combine the world-space rendering of Puffs with the bindless texture
sampling of Billboards, adding UV sub-rect animation driven by elapsed time.

CAVEAT: Billboards source files were NOT found under
Engine/Source/Frame/Collections/ (only referenced from FrameBase.h:78,189) --
verify the Billboards source location before using it as a pattern.

Texture atlases are just regular textures loaded through the existing
TextureDescriptors system -- no DataPacker changes are needed.

Why
---
- Explosions lack a fireball visual; particles alone look thin
- Flipbooks are GPU-cheap (one textured quad) and artist-friendly
- The Controller pattern already handles fire-and-forget lifetime
- A random start frame plus vertical gradient fade produces varied results
  with a single flipbook sheet (per the Engine.txt notes)

Changes (11 files)
------------------

1. Engine/Data/Shaders/ShaderLayoutsBase.h
   Add a new FlipbookLayout struct after BillboardLayout:
     struct FlipbookLayout
     {
         vec4 f4Position INIT;       // World-space position (xyz) + w=1
         float fSize INIT;           // World-space half-extent
         float fTextureIndex INIT;   // Bindless texture array index
         float fAlpha INIT;          // Overall alpha multiplier
         float fRotation INIT;       // Z-axis rotation (radians)
         vec4 f4UvRect INIT;         // xy = UV offset, zw = UV size (one cell)
     };

2. Engine/Data/Shaders/Particles/Flipbooks.vert  [NEW]
   Vertex shader for world-space camera-facing flipbook quads.
   - Read FlipbookLayout from storage buffer via gl_InstanceIndex
   - Billboard the quad toward the camera using globalLayout view vectors
     (same approach as SquareParticlesRender.vert)
   - Apply fSize for world-space scaling and fRotation for spin
   - Remap f2InQuadVertex (0..1) through f4UvRect to produce sub-rect
     texcoords: f2OutTexcoord = f4UvRect.xy + f2InQuadVertex * f4UvRect.zw

3. Engine/Data/Shaders/Particles/Flipbooks.frag  [NEW]
   Fragment shader for flipbook quads.
   - Sample bindless texture at the sub-rect texcoord
   - Multiply alpha by fAlpha from the layout
   - Discard fully transparent fragments

4. Engine/Source/Frame/Collections/Flipbooks/Flipbooks.h  [NEW]
   New client-only collection following the Puffs pattern.
   - FlipbookType: crc (texture atlas CRC), uiColumns, uiRows,
     uiFrameCount, fFrameRate, fSize
   - FlipbookControllerType: uiBaseTypeIndex, uiKeyframeCount,
     pfTimes[], FlipbookKeyframe keyframes[] (fSize, fAlpha, fRotation).
     bDestroysSelf = true
   - FlipbooksInterpolate: Collection<FlipbooksInterpolate> with
     ControllerTypeRegistry. SOA members: puiTypeIndices,
     pVecPositions, pfAlphas, pfSizes, pfRotations,
     puiControllerTypeIndices, pfStartTimes, puiStartFrameOffsets
   - Members() returns std::tie of all SOA members
   - Standard render interface: GraphicsResources, BeginRender, Render,
     EndRender
   - FlipbooksPostRender: Collection<FlipbooksPostRender> with
     AddControlled() (same signature as PuffsPostRender::AddControlled
     plus a random start frame offset), Destroy (auto-expiry), empty
     Members()

5. Engine/Source/Frame/Collections/Flipbooks/Flipbooks.cpp  [NEW]
   - Template instantiations for both collections
   - Register(): empty (game registers types)
   - AllocateAndCopy: Allocate + memcpy all SOA arrays
   - FlipbooksPostRender: AllocateAndCopyIds, empty Spawn/Transfer/Destroy
     stubs

6. Engine/Source/Frame/Collections/Flipbooks/FlipbooksUpdate.cpp  [NEW]
   - FlipbooksInterpolate::Update(): call InterpolateKeyframes to animate
     fSize, fAlpha, fRotation from controller keyframes
   - FlipbooksPostRender::AddControlled(): grow paired collections, fill
     type index, start time, random start frame offset (from random engine)
   - FlipbooksPostRender::Destroy(): auto-remove expired controlled elements
   - Empty PreCollision/PostCollision/AreaDamage stubs

7. Engine/Source/Frame/Collections/Flipbooks/FlipbooksRender.cpp  [NEW]
   - GraphicsResources(): CreateDynamicBuffer + CreatePipelineFlipbooks
   - BeginRender(): AccumulateRenderCapacity, ResizeDynamicBufferIfNeeded
   - Render(): For each flipbook instance:
       a. Visibility cull via IsPointVisible
       b. Compute elapsed time = fCurrentTime - pfStartTimes[i]
       c. Compute frame index = (startFrameOffset + floor(elapsed * frameRate))
          % frameCount
       d. Compute UV rect from frame index, columns, rows:
          uvOffset.x = (frameIndex % columns) / columns
          uvOffset.y = (frameIndex / columns) / rows
          uvSize = (1/columns, 1/rows)
       e. Fill FlipbookLayout: projected position, size, texture index via
          CrcToIndex, alpha, rotation, UV rect
   - EndRender(): SetCount profile counters, WriteIndirectBuffer

8. Engine/Source/Graphics/Managers/DynamicPipelines.h
   Add kDynamicPipelineFlipbooks to the DynamicPipelineType enum (before
   kDynamicPipelineCount). Add declaration:
     void CreatePipelineFlipbooks(common::crc_t crc, std::string_view name,
                                  int64_t iBufferSize);

9. Engine/Source/Graphics/Managers/DynamicPipelines.cpp
   Implement CreatePipelineFlipbooks, modeled on CreatePipelineBillboards:
   - Same pattern: skip if exists, CreateDynamicBuffer, create Pipeline with
     kIndirectHostVisible | kSampleShading | kAlphaBlend | kUpdateAfterBind
   - Reference new Flipbooks.vert/frag shader CRCs
   - Quad vertex buffer, global+main uniform buffers, storage buffer,
     sampler, bindless textures
   - Register in mPipelineMaps[kDynamicPipelineFlipbooks]

10. Engine/Source/Graphics/Managers/CommandBufferManager.cpp
    Add flipbook draw recording in the main render pass, after the
    kGpuTimerBillboards block:
      gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer,
                                  kGpuTimerFlipbooks);
      for (const auto& [rCrc, pPipeline] :
           gpPipelineManager->mDynamicPipelines
               .mPipelineMaps[kDynamicPipelineFlipbooks])
      {
          pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
      }
      gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer,
                                 kGpuTimerFlipbooks);

11. Engine/Source/Frame/FrameBase.h
    - Add #include "Frame/Collections/Flipbooks/Flipbooks.h" in the
      BT_CLIENT include block
    - Add FlipbooksInterpolate flipbooks {}; to FrameInterpolateBase
      (inside BT_CLIENT, alphabetical order after explosions)
    - Add FlipbooksPostRender flipbooks {}; to FramePostRenderBase
      (same position)
    - Add rSelf.flipbooks to both Collections() tuples (both Interpolate
      and PostRender)
    - Increment kCollectionCount by 1 (client: 11 -> 12, server stays 2)
    - Add FlipbooksInterpolate to the InterpolateRenderTypes TypeList

Additional updates (not counted as separate changes):
    - Engine/Source/Profile/ProfileManagerBase.h: add kCpuCounterFlipbooks,
      kCpuCounterFlipbooksRendered, kGpuTimerFlipbooks
    - Add new .cpp files to the vcxproj filter for both client and server
      projects
    - Engine/Source/Frame/Collections/Flipbooks/AGENTS.md: standard
      collection AGENTS.md

Notes
-----
- The new SOA members on FlipbooksInterpolate/FlipbooksPostRender require the
  add-collection-member checklist
- Flipbook textures are loaded as regular textures via TextureDescriptors --
  no DataPacker or atlas packing changes needed
- The random start frame offset prevents all simultaneous explosions from
  looking identical; it is set at spawn time from the frame's random engine
  to maintain client/server sync (the offset is client-only state, so it
  does not affect determinism)
- Vertical gradient fade (alpha decreasing toward bottom of quad) can be
  added later in the fragment shader as a simple 1-texcoord.y multiplier
- Explosions.cpp Spawn() is the integration point: register a flipbook
  controller type per explosion type, and call
  FlipbooksPostRender::AddControlled() alongside the existing
  PointLights/Puffs/SmokeTrails spawns
- The collection is client-only (#ifdef BT_CLIENT) -- no shared CRC or
  SharedMembers/ClientMembers split is needed
- Frame rate and frame count are per-type, so different flipbook sheets
  (fire, smoke, shockwave) can run at different speeds
