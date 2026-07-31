# TextureUploadManager

Global: `gpTextureUploadManager`

Runs a dedicated upload thread with a fixed staging budget, persisting large-texture progress across frames. That budget caps size, not rate; pacing is separate — the render loop signals the thread once per frame and the thread waits for a signal before each chunk, so uploads spread across frames instead of bursting. Block-compressed partial copies support BC4, BC5, and BC7. Queue selection prefers transfer-only work, but resolves to foreground adoption when transfer aliases graphics or the distinct present family so concurrent submissions do not share that queue.

## Ownership and Teardown

Distinct transfer queues release image ownership for the matching graphics acquire; maintenance9-capable devices may use the simplified path. Device loss preserves CPU data for re-upload.

`WaitIdle` is a teardown drain handshake: the upload thread acknowledges only from a no-submit-in-flight point, and every exit path publishes exit state so a waiter cannot deadlock. Frame permits remain binary and must be drained before release. Fatal upload failures are published for the main thread and survive transfer-resource recreation; device loss remains a separate recovery path.

## Chunk Completion

Pack dimensions and derived copy ranges are validated before image allocation. Corrupt dimensions soft-fail the chunk to ready without creating an image, preserving its existing placeholder. Other upload failures leave active ownership intact for the published fatal path.

The pending-adoption counter belongs to this manager because it outlives `TextureManager` during device recreation. Keep file-state rearming and adoption completion synchronized with that counter.
