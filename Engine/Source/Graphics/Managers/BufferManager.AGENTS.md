# BufferManager

Global: `gpBufferManager`

Manages all GPU buffers: vertex (terrain, water, models), uniform (per-framebuffer view/projection), and storage (dynamic game objects, particles, skinning). Model buffers indexed by CRC for lookup.

## Dynamic Buffers

Collections register storage buffers during CreatePipelines() via CRC-keyed dynamic buffer system with three categories (main, visible lights, wind deposit). Provides type-safe templated access with runtime size validation. Supports automatic resizing with deferred destruction to avoid Vulkan validation errors from in-flight command buffers.

## Skinning

Per-frame bump allocation for mesh data and joint matrices with per-command-buffer offset tracking. Auto-grows by doubling capacity and propagates descriptor updates to model pipelines. Joint matrices use a compact 3-row format (48 bytes) since the fourth row is always identity. Multiple grows within the same frame are safe: buffers are per-framebuffer instances, the top-of-frame per-framebuffer fence wait drains any prior submission before a grow runs, and record-once command buffers read skinning data only through descriptor sets (which the grow repoints at the new buffer).

## Hierarchical Dispatch Buffers

Both smoke and wind use the same occupancy + active tile list pattern for hierarchical indirect dispatch.

Smoke pairs one bit-packed occupancy buffer with each ping-pong texture and shares one compact active-tile list between both spread halves. Deposits mark the first texture's occupancy; each spread reads its input occupancy, consumes its output texture's prior occupancy as a stale-storage union term, resets the output occupancy, and re-marks nonzero output. Both occupancy buffers start at zero.

Wind keeps a separate occupancy and active-tile-list pair for each ping-pong texture. Both spread variants remain recorded, with runtime state and indirect dispatch selecting useful work.

## Swapchain Lifecycle

Partial teardown/rebuild of per-framebuffer buffers during swapchain recreation while preserving static geometry. Full destruction only on device loss.
