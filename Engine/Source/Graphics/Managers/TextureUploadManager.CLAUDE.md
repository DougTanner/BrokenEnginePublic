# TextureUploadManager

**Global**: `gpTextureUploadManager`

Dedicated thread and transfer queue for background GPU texture uploads, streaming one chunk per frame through a fixed-size staging buffer. The transfer queue prefers a transfer-only family but falls back to the graphics family when the device advertises no dedicated transfer queue, so transfer==graphics is a supported configuration (no ownership transfer needed in that case). Per-frame pacing via binary semaphore signaled by the render loop. Large textures split across multiple frames with persisted in-progress state. Sub-mip partial copies for compressed formats (BC4/BC7).

## Queue Family Ownership Transfer

Records release barriers on transfer queue; TextureManager records matching acquire barriers on graphics queue. Simplified path when QFOT is optional via VK_KHR_maintenance9. Handles device loss by preserving CPU data for re-upload.

`WaitIdle()` (called from Graphics teardown before `vkDeviceWaitIdle`) is a drain-probe handshake, not a bare lock: it posts a request and blocks until the upload thread acks from a quiescent point between iterations, closing the window where the thread could submit on the transfer queue concurrently with the device-wait. The steady-state per-frame wake stays the binary semaphore; the probe is teardown-only. `WaitIdle` unblocks on either the drain ack or a self-exit: every loop-exit path (shutdown, device loss) sets an exit flag and notifies under the work mutex, so device-loss recovery never deadlocks awaiting an ack the exiting thread will never send. Preserve both when the upload state machine changes — a new drain path must ack from a no-submit-in-flight point, a new exit path must set the exit flag. The teardown/wait-path releases (`WaitIdle`'s probe, `DestroyTransferResources`) drain any pending permit before releasing so the binary semaphore can't exceed its max of one (over-release is UB); the steady-state per-frame signal does not.

## Adoption-Pending Counter

Carries an atomic counter tracking chunks that have been uploaded but not yet adopted by TextureManager. The counter must live here — not on TextureManager — because TextureManager is destroyed and recreated on device loss while TextureUploadManager persists. `FileManager::ResetTextureChunkStates` adjusts the counter when re-arming chunks for re-upload; keep these in sync if the upload state machine changes.
