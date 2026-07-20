# TextureUploadManager

**Global**: `gpTextureUploadManager`

Dedicated thread and transfer queue for background GPU texture uploads, streaming one chunk per frame through a fixed-size staging buffer. Queue selection prefers a transfer-only family, but resolves transfer to graphics when the selected family is graphics or is also the distinct present family. With transfer==graphics, chunks return to `kDiskLoaded` for foreground adoption instead of being submitted by the upload thread, avoiding concurrent submissions on the shared queue. Per-frame pacing via binary semaphore signaled by the render loop. Large textures split across multiple frames with persisted in-progress state. Sub-mip partial copies for compressed formats (BC4/BC7).

## Queue Family Ownership Transfer

Records release barriers on transfer queue; TextureManager records matching acquire barriers on graphics queue. Simplified path when QFOT is optional via VK_KHR_maintenance9. Handles device loss by preserving CPU data for re-upload.

`WaitIdle()` (called from Graphics teardown before `vkDeviceWaitIdle`) is a drain-probe handshake, not a bare lock: it posts a request and blocks until the upload thread acks from a quiescent point between iterations, closing the window where the thread could submit on the transfer queue concurrently with the device-wait. The steady-state per-frame wake stays the binary semaphore; the probe is teardown-only. `WaitIdle` unblocks on either the drain ack or a self-exit: every loop-exit path (shutdown, device loss, fatal upload error) sets an exit flag and notifies under the work mutex, so teardown never deadlocks awaiting an ack the exiting thread will never send. Preserve both when the upload state machine changes — a new drain path must ack from a no-submit-in-flight point, a new exit path must set the exit flag. Every `mFrameSignal` release site — the per-frame render-loop signal, `WaitIdle`'s probe, `DestroyTransferResources`, and `TextureManager::WaitForTextures` — drains any pending permit before releasing so the binary semaphore can't exceed its max of one (over-release is UB); the per-frame signal included, because the upload thread may lag a frame mid-iteration and leave a permit still pending.

Texture dimension validation is the only per-chunk soft-fail tier: corrupt dimensions complete as zero-filled before image creation. Any other upload-thread exception is published before the exit flag, leaves the active chunk and image untouched, and is rethrown by the main loop or a boot texture wait; boot failure first performs full Graphics teardown and idles the device. The fatal mailbox stays pending across transfer-resource teardown and recreation: recreation consumes any pending failure before rearming the thread state, so lifecycle reset code must not clear it. Device loss remains a distinct re-upload recovery path.

## Adoption-Pending Counter

Carries an atomic counter tracking chunks that have been uploaded but not yet adopted by TextureManager. The counter must live here — not on TextureManager — because TextureManager is destroyed and recreated on device loss while TextureUploadManager persists. `FileManager::ResetTextureChunkStates` adjusts the counter when re-arming chunks for re-upload; keep these in sync if the upload state machine changes.
