# CommandBufferManager

**Global:** `gpCommandBufferManager`

Owns per-framebuffer command buffers and the submission graph. Global and Main command buffers are immutable after their first recording and are rebuilt only through renderer recreation; their record helpers own compute/world preparation and scene rendering respectively. ImGui records separately each frame.

## Submission and Synchronization

- Global may prepend texture queue-family ownership-acquire barriers, then signals Main. Main waits on both Global completion and swapchain image availability; swapchain acquisition therefore gates Main rather than Global.
- Main signals the particle semaphore consumed by the next frame's Global submission. Preserve this cross-frame edge when changing particle compute or rendering order.
- Submission workers publish work through `PersistentWorker` wake/wait edges: Main waits for Global, and UI waits for Main. Data read by a worker must be written before its wake.
- Main resets the framebuffer fence but submits without it. The inseparable following ImGui submission signals that fence; every path that submits Main must also submit ImGui.

The per-framebuffer recorded flag is an idempotence guard, not a runtime re-record mechanism.
