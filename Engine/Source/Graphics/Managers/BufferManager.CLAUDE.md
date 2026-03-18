# BufferManager

**Global**: `gpBufferManager`

Manages all GPU buffers: vertex (terrain, water, models), uniform (per-framebuffer view/projection), and storage (dynamic game objects, particles, skinning). Model buffers indexed by CRC for lookup.

## Dynamic Buffers

Collections register storage buffers during CreatePipelines() via CRC-keyed dynamic buffer system with three categories (main, visible lights, wind deposit). Provides type-safe templated access with runtime size validation. Supports automatic resizing with deferred destruction to avoid Vulkan validation errors from in-flight command buffers.

## Skinning

Per-frame bump allocation for mesh data and joint matrices with per-command-buffer offset tracking. Auto-grows by doubling capacity and propagates descriptor updates to model pipelines. Joint matrices use a compact 3-row format (48 bytes) since the fourth row is always identity.

## Smoke Hierarchical Dispatch Buffers

Two dedicated buffers support the smoke occupancy system:

- **`mSmokeOccupancyVkBuffer`** (~200 KB) - Bit-packed buffer with one bit per 8x8 smoke tile. Written by `Smoke.frag` and both spread shaders to mark active tiles; read and zeroed each frame by `kPipelineSmokeOccupancyDilate`.
- **`mSmokeActiveTileVkBuffer`** (~6.5 MB) - Compacted flat list of active tile indices produced by the dilate+compact pass. Also serves as the indirect dispatch argument buffer for `vkCmdDispatchIndirect`.

## Swapchain Lifecycle

Partial teardown/rebuild of per-framebuffer buffers during swapchain recreation while preserving static geometry. Full destruction only on device loss.
