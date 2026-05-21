# Water displacement compute: indirect-host-visible dispatch + over-land skip

## Context

The Gerstner-displacement pre-compute pass (`Engine/Data/Shaders/Water/WaterDisplacement.comp`, added in the session that pulled Gerstner math out of `Water.vert`) currently dispatches a constant `ceil(iFullX/8) × ceil(iFullY/8)` workgroups at LOD0 size every frame, regardless of which water LOD will actually draw. An in-shader `if (i2Coord.x > iWaterActiveQuadX || i2Coord.y > iWaterActiveQuadY) return;` early-returns out-of-bounds threads, but every workgroup still launches and pays scheduling cost.

This file tracks two perf opportunities that were deferred from the initial implementation because each requires engine-side plumbing.

## Item 1 — Host-visible indirect-compute dispatch

`Engine/Source/Graphics/Objects/PipelineCreator.cpp:611-614` currently `ASSERT(false)`s on `kIndirectHostVisible | kCompute`:
```cpp
if (rPipeline.mInfo.flags & kIndirectHostVisible)
{
    ASSERT(false);
}
else if (rPipeline.mInfo.flags & kIndirectDeviceLocal) { ... }
```

The graphics-side equivalent is implemented at the same file's `SetupIndirectBuffer(...)` (line ~71). Plan:

1. Add the symmetric branch for compute: allocate a host-visible `VkDispatchIndirectCommand` buffer sized `iCommandBufferCount * sizeof(VkDispatchIndirectCommand)` with `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | HOST_VISIBLE_BIT | HOST_COHERENT_BIT`. Persistent-map and store the pointer alongside `mIndirectVkBuffer` / `mIndirectVmaAllocation`.
2. Add `Pipeline::WriteIndirectComputeBuffer(int64_t iCommandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)` next to the existing `WriteIndirectBuffer` (graphics) at `Engine/Source/Graphics/Objects/Pipeline.cpp:303-327`.
3. Change `kPipelineWaterDisplacement`'s flags in `PipelineManager.cpp` from `{kCompute}` to `{kCompute, kIndirectHostVisible}`.
4. In `CommandBufferRecordMain.cpp` (where the current direct `RecordCompute(..., ceil(...), ceil(...))` lives), replace with `pPipelines[kPipelineWaterDisplacement].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer)`.
5. In `MainUniforms.cpp` next to the existing `iWaterActiveQuadX/Y` populate block, write `WriteIndirectComputeBuffer(iCommandBuffer, ceil((rWaterLod.iQuadCountX + 1) / kiComputeTileSize), ceil((rWaterLod.iQuadCountY + 1) / kiComputeTileSize), 1)`.

The shader's bounds-check stays in place as a defensive guard (cheap, covers any future dispatch-size mismatch).

**Expected saving**: at LOD3 (1/8 dim per axis = 1/64 area), workgroup count drops ~64x. Translates to fewer wavefront launches; absolute saving is small in nanoseconds but proportional.

**Critical files**:
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp` (line 611-614 — add the branch)
- `Engine/Source/Graphics/Objects/Pipeline.h/.cpp` (new `WriteIndirectComputeBuffer`)
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` (pipeline flags)
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` (RecordCompute → RecordComputeIndirect)
- `Engine/Source/Graphics/Render/MainUniforms.cpp` (write indirect dims)

## Item 2 — Skip Gerstner sum over land

`WaterDisplacement.comp` currently runs the full low-band + medium-band Gerstner loops for every active-LOD texel, including those whose corresponding water vertex is over land. `Water.vert`'s over-land early-out then discards the displacement entirely.

The compute shader already samples elevation at line 49 (to compute `fShoreAmplitude`). Extending the existing `fShoreAmplitude` check to also short-circuit when `fTerrainElevation >= 0.0f`:

```glsl
if (fTerrainElevation >= 0.0f)
{
    imageStore(displacementImage, i2Coord, vec4(0.0f));
    imageStore(normalImage,       i2Coord, vec4(0.0f, 0.0f, 1.0f, 0.0f));
    return;
}
```

Saves both Gerstner loops for over-land texels. Win scales with how much of the active visible area is occupied by island interiors.

**Critical files**:
- `Engine/Data/Shaders/Water/WaterDisplacement.comp` — single early-return after the `fShoreAmplitude` line.

## Verification (both items)

- Build client, run, visually confirm waves still animate identically.
- Open the GPU profile overlay; compare `Water Displacement` timer at full-zoom (LOD0) vs zoomed-out (LOD3). Indirect dispatch should produce a measurable drop at LOD3; current direct dispatch is flat across LODs.
- Walk camera over a large island; over-land skip should drop the timer further.
- Validation layer: no new STORAGE→SAMPLED hazards (texture layout cycle is unchanged).
