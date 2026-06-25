# BufferManager

**Global**: `gpBufferManager`

Manages all GPU buffers: vertex (terrain, water, models), uniform (per-framebuffer view/projection), and storage (dynamic game objects, particles, skinning). Model buffers indexed by CRC for lookup.

## Dynamic Buffers

Collections register storage buffers during CreatePipelines() via CRC-keyed dynamic buffer system with three categories (main, visible lights, wind deposit). Provides type-safe templated access with runtime size validation. Supports automatic resizing with deferred destruction to avoid Vulkan validation errors from in-flight command buffers.

## Skinning

Per-frame bump allocation for mesh data and joint matrices with per-command-buffer offset tracking. Auto-grows by doubling capacity and propagates descriptor updates to model pipelines. Joint matrices use a compact 3-row format (48 bytes) since the fourth row is always identity. Multiple grows within the same frame are safe: buffers are per-framebuffer instances, the top-of-frame per-framebuffer fence wait drains any prior submission before a grow runs, and record-once command buffers read skinning data only through descriptor sets (which the grow repoints at the new buffer).

## Hierarchical Dispatch Buffers

Both smoke and wind use the same occupancy + active tile list pattern for hierarchical indirect dispatch.

**Smoke** (single pair):
- **`mSmokeOccupancyVkBuffer`** - Bit-packed buffer with one bit per 8x8 smoke tile. Written by `Smoke.frag` and both spread shaders; read and zeroed each frame by `kPipelineSmokeOccupancyDilate`.
- **`mSmokeActiveTileVkBuffer`** - Compacted flat list of active tile indices produced by the dilate+compact pass. Also serves as the indirect dispatch argument buffer.

**Wind** (two pairs A/B, one per ping-pong texture index):
- **`mWindOccupancyVkBuffers[2]`** - Per-index bit-packed occupancy. Written by `WindDeposit.frag` and the spread compute shaders.
- **`mWindActiveTileVkBuffers[2]`** - Per-index active tile list with indirect dispatch args prefix. Enables record-once command buffers: both spread pipelines are always dispatched, and each returns early if its index is inactive.

**Lighting Spread**:
- **`mLightOccupancyVkBuffers[]`** - Declared per cascade level (`kiMaxCascadeLevels`) but only index 0 is created. Bound to the two lighting deposit pipelines (area/point lights) as bit-packed tile occupancy, zeroed each frame in the Main record; nothing reads it back yet (write-only). Created/destroyed alongside lighting texture recreation via `DestroyFlags::kLightingTextures`.

## Swapchain Lifecycle

Partial teardown/rebuild of per-framebuffer buffers during swapchain recreation while preserving static geometry. Full destruction only on device loss.
