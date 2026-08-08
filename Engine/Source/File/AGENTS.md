# File - Runtime I/O and Packed Assets

`FileManager` (`gpFileManager`) owns platform paths, versioned and atomic files, packed-asset access, and deterministic replay streams. `PackChunks` is its private packed-asset implementation, not a manager or aggregation-header surface.

## File Contracts

- Asset data comes from the validated absolute `--data-directory` or the executable-sibling `Data` directory; the process working directory never selects assets. Client and server launches must use the same data root.
- Versioned files share `WriteVersionHeader` / `ReadAndValidateVersionHeader`. Raw trivially-copyable payloads validate size; streamed payload types own their serialization. Change an on-disk layout and its owning version together.
- One-shot writes are atomic through a sibling temporary and rename. Direct write streams opt out only with `kStreaming`; backup failure is reported but does not weaken the atomic main-file write.

## Packed Assets

- Client Scene, Model, Shader, and Raw packs are eager. Audio, Islands, and Texture packs are lazy; the server opens only its accepted lazy types. Keep the server/eager split aligned with DataPacker output ownership.
- Lazy requests run on background loaders and publish chunk state with release/acquire ordering. `WaitForChunks` may block for readiness, but an already queued request is not reprioritized.
- Each `LazyChunk` owns state for at most one asynchronous reload of an uncompressed subrange. Publish its exact range before the pending release-store; consumers acquire the terminal state and only reset their own ready or failed range, never a pending one.
- Audio performs random-access reads without making the whole chunk resident. Texture chunks continue from disk load through `TextureUploadManager`. Device loss and island eviction reset GPU state and also rewrite the pool pointer and size of every lazy chunk, because pool offsets are cumulative; those values land unchanged for chunks that are not being evicted, which is the only reason a racing lock-free audio read stays safe. The caller, not the reset code, must guarantee that no other thread touches a chunk being reset — the texture upload thread and the audio fill worker are the two racing readers. Keep both reset callers' logic in step before adding a third.
- Startup hashes the ordered Islands chunk table into the pack integrity token the client sends in its connection handshake; a server whose token differs refuses the connection (`../../../Documents/Architecture/Network.md`). Sorting, filtering, or re-emitting that table changes the token, so every client fails to connect while the reported error points at the data build instead.
- `EagerChunk.iDataSize` is the raw on-disk extent taken from the chunk table, not `ChunkHeader::iSize`: a scene chunk's header size excludes its appended animation section. Bound eager reads against `iDataSize`, or animation data silently truncates with no crash.
- Pack and manifest files are trust boundaries. Invalid required boot structure is fatal; per-chunk read or decompression corruption reports the failure, publishes completion, and leaves the worker and waiters live.

## Lazy-Pool Invariants

- One reserved virtual-memory pool is laid out by cumulative aligned size over the complete lazy-chunk map. Reset code must walk the same complete map order; do not compact pointers or recompute offsets from a subset.
- Compressed chunks reserve their uncompressed size. Reclamation decommits only page-aligned interiors so pointers, boundary pages, and cumulative offsets remain stable; use it only without concurrent readers. Before any raw read or decompression write targets a reclaimed range, recommit that range first; whole texture loads recommit their full pool allocation, while uncompressed island subranges reload directly from disk.

## Replay Streams

`DifferenceStream` writes full boundaries, per-frame deltas, and checksums. Publishing succeeds only when every sibling file succeeds; any failure removes the complete sibling set. Replay manifests authenticate their expected component bytes and sizes with SHA-256; hash only ordinary, non-reparse files, and treat a hash/read failure as an invalid replay generation. Optional full-frame diagnostics remain gated by `kbReplayFullFrames`; malformed or stale `.fullframes` data permanently disables those diagnostics for that reader without changing authoritative replay or checksum validation, and mismatch reporting follows the operand labeling owned by Log ([`Common/Log/AGENTS.md`](../../../Common/Log/AGENTS.md)).

## See Also

- Game persistence (`../../../Projects/BrokenEngineSandbox/Source/Save/AGENTS.md`)
