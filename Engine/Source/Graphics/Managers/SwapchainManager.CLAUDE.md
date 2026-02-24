# SwapchainManager

**Global**: `gpSwapchainManager`

Manages swap chain creation/recreation, framebuffers, depth and multisampling textures, and frame synchronization. Handles out-of-date and suboptimal swapchain gracefully via deferred recreation. Async presentation via PersistentWorker at time-critical priority.
