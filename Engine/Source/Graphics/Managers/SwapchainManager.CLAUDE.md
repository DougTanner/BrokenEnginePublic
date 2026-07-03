# SwapchainManager

**Global**: `gpSwapchainManager`

Manages swap chain creation/recreation, framebuffers, depth and multisampling textures, and frame synchronization. Handles out-of-date and suboptimal swapchain gracefully via deferred recreation. Async presentation via PersistentWorker at time-critical priority.

Owns two render passes. The scene (all default pipelines, plus the F16 multisample attachment) renders into an F16 (`R16G16B16A16_SFLOAT`) HDR target; a fullscreen resolve pipeline then tone-maps that target into the single-sample swapchain present pass, whose only client is the resolve. Preserves HDR highlight detail that direct-to-`UNORM` swapchain rendering would clamp, and gives one hook for whole-frame tone mapping and color grading.
