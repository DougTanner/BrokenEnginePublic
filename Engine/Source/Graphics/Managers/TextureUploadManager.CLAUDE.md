# TextureUploadManager

**Global**: `gpTextureUploadManager`

Dedicated thread and transfer queue for background GPU texture uploads, streaming one chunk per frame through a fixed-size staging buffer. Per-frame pacing via binary semaphore signaled by the render loop. Large textures split across multiple frames with persisted in-progress state. Sub-mip partial copies for compressed formats (BC4/BC7).

## Queue Family Ownership Transfer

Records release barriers on transfer queue; TextureManager records matching acquire barriers on graphics queue. Simplified path when QFOT is optional via VK_KHR_maintenance9. Handles device loss by preserving CPU data for re-upload.
