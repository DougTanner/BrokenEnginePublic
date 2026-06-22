# TextureUploadManager

**Global**: `gpTextureUploadManager`

Dedicated thread and transfer queue for background GPU texture uploads, streaming one chunk per frame through a fixed-size staging buffer. The transfer queue prefers a transfer-only family but falls back to the graphics family when the device advertises no dedicated transfer queue, so transfer==graphics is a supported configuration (no ownership transfer needed in that case). Per-frame pacing via binary semaphore signaled by the render loop. Large textures split across multiple frames with persisted in-progress state. Sub-mip partial copies for compressed formats (BC4/BC7).

## Queue Family Ownership Transfer

Records release barriers on transfer queue; TextureManager records matching acquire barriers on graphics queue. Simplified path when QFOT is optional via VK_KHR_maintenance9. Handles device loss by preserving CPU data for re-upload.

`WaitIdle()` (called from Graphics teardown before `vkDeviceWaitIdle`) is a drain-probe handshake, not a bare lock: it posts a request and blocks until the upload thread acks from a quiescent point between iterations, closing the window where the thread could submit on the transfer queue concurrently with the device-wait. The steady-state per-frame wake stays the binary semaphore; the probe is teardown-only. If the upload state machine changes, the thread must still reach a no-submit-in-flight point to ack.

## Adoption-Pending Counter

Carries an atomic counter tracking chunks that have been uploaded but not yet adopted by TextureManager. The counter must live here — not on TextureManager — because TextureManager is destroyed and recreated on device loss while TextureUploadManager persists. `FileManager::ResetTextureChunkStates` adjusts the counter when re-arming chunks for re-upload; keep these in sync if the upload state machine changes.
