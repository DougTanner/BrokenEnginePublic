# SwapchainManager

Global: `gpSwapchainManager`

Manages swap chain creation/recreation, framebuffers, depth and multisampling textures, and frame synchronization. Handles out-of-date and suboptimal swapchain gracefully via deferred recreation. Async presentation via PersistentWorker at time-critical priority.

Owns two render passes. The scene (all default pipelines, plus the F16 multisample attachment) renders into an F16 (`R16G16B16A16_SFLOAT`) HDR target; a fullscreen resolve pipeline then tone-maps that target into the single-sample swapchain present pass, whose only client is the resolve. Preserves HDR highlight detail that direct-to-`UNORM` swapchain rendering would clamp, and gives one hook for whole-frame tone mapping and color grading.

The HDR framebuffer's attachments (HDR color/depth/MSAA) are all single images reused every frame in flight, so the incoming `EXTERNAL → 0` subpass dependency must carry depth stage + access (`EARLY|LATE_FRAGMENT_TESTS` + `DEPTH_STENCIL_ATTACHMENT_WRITE`) in its src scopes: the previous frame's depth *write* is a cross-frame write-after-write against the next frame's clear/transition and needs an availability op, whereas the color attachment's prior access is only the resolve pass's sampled *read* (a WAR already ordered by the existing `FRAGMENT_SHADER` src stage).
