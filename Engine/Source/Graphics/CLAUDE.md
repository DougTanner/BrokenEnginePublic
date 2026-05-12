# Graphics - Vulkan Rendering System

## Overview

Multi-pass deferred Vulkan renderer with lighting, shadows, GPU particles, and post-processing. Entirely client-only. `gpGraphics` owns all managers and coordinates a three-submission render loop (Global, Main, ImGui).

## Architecture Notes

- Record-once command buffers resubmitted every frame; re-recorded only on resize or settings change
- Per-framebuffer descriptor sets prevent GPU conflicts across frames in flight
- All GPU memory allocated through VMA
- Lazy texture loading: generic textures use a white placeholder at startup; islands use a permanent neutral programmatic placeholder at slot 0 (format-matched, never adopted by a real island), with background disk load via transfer queue and deferred descriptor update. Frame 0 renders no islands — visible terrain appears only as subscriptions arrive and mint slots
- Camera snaps render visible-area to the terrain/water quad grid — never render off-grid
- Terrain collision queries live in `/Frame/IslandTerrain` (shared); `Islands` here is client-only GPU rendering
- Frame boundary sits between Main-submit and the next Acquire, not at loop top
- `RenderGlobal` post-fence-wait is the descriptor-patch safety window: island LRU eviction runs immediately before `TextureManager::ProcessPendingTextures` and restoration runs immediately after, so descriptor rewrites happen while no frame-in-flight references the slots
- `muiFrameCounter` increments unconditionally each frame (not gated by `VK_EXT_memory_budget` polling); the island LRU grace clock depends on monotonic advance on every device

## Destroy / Refresh Pipeline

Settings-change detector escalates a destroy tier (`DestroyType`) monotonically via `std::max`, with fine-grained resource subsets in a flags bitmask. `Destroy()` drains all in-flight submissions and `vkDeviceWaitIdle`s before touching resources. Selective pipeline recreation avoids full rebuilds when only pipeline-affecting cvars changed; partial destroy preserves texture/buffer managers and stashes the old swapchain for seamless recreation.

## Error / Debug Helpers

- `CheckVkFailed` auto-escalates swapchain/surface-loss results into destroy tiers; `DEVICE_LOST` throws `DeviceLostException` (caught by main loop to recreate Graphics in place)
- `CHECK_VK` macro adds `DEBUG_BREAK()` at the call site
- Debug-name strings interned because Vulkan retains the `c_str()`; insertion wrapped in `ScopedSuppressAllocationTracking`
- `OneShotCommandBuffer` — RAII wrapper for init/transfer work on the graphics queue

## AnimationData

- Zero-copy: node/channel/keyframe/material pointers index directly into eagerly-loaded pack memory
- Matrix convention: DirectXMath row-major storage reinterpreted by GLSL as column-major is already the transpose — never `XMMatrixTranspose` before upload
- Topological node order invariant (`iParentIndex < i`) enables single-pass world-matrix build
- CUBICSPLINE path uses glTF-spec Hermite tangents scaled by delta; STEP/LINEAR uses a compact keyframe
- Temp TRS/world-matrix arrays come from `gpThreadLocal->mWorkbuffer`, never heap

## Islands / Screenshot / Resolution Helpers

- `Islands` bridges island state into a host-visible storage buffer; growth doubles capacity and forces command-buffer re-record. Inactive slots are zero-width quads (GPU-culled)
- Screenshot save waits the target framebuffer's fence, copies present image to host memory, then `std::async`-saves JPG with its own `ThreadLocal` (runs off the engine worker pool); a static future serializes overlapping saves
- Render-target size aligns to shadow-block boundaries expanded past framebuffer extent; smoke-sim resolution scales relative to a 3840-px reference width

## See Also

- [Managers/CLAUDE.md](Managers/CLAUDE.md) - Vulkan manager singletons and initialization order
- [Objects/CLAUDE.md](Objects/CLAUDE.md) - RAII wrappers for Vulkan resources
- [Render/CLAUDE.md](Render/CLAUDE.md) - Per-subsystem uniform buffer population
- [Debug/CLAUDE.md](Debug/CLAUDE.md) - Wireframe debug visualization (BT_DEBUG only)
- [Graphics Pipeline diagram](../../../Documents/Architecture/GraphicsPipeline.md)
