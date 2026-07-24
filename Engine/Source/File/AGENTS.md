# File - Runtime I/O and Packed Assets

`FileManager` (`gpFileManager`) owns platform paths, versioned and atomic files, packed-asset access, and deterministic replay streams. `PackChunks` is its private packed-asset implementation, not a manager or aggregation-header surface.

## File Contracts

- Asset data comes from the validated absolute `--data-directory` or the executable-sibling `Data` directory; the process working directory never selects assets. Client and server launches must use the same data root.
- Versioned files share `WriteVersionHeader` / `ReadAndValidateVersionHeader`. Raw trivially-copyable payloads validate size; streamed payload types own their serialization. Change an on-disk layout and its owning version together.
- One-shot writes are atomic through a sibling temporary and rename. Direct write streams opt out only with `kStreaming`; backup failure is reported but does not weaken the atomic main-file write.

## Packed Assets

- Client Scene, Model, Shader, and Raw packs are eager. Audio, Islands, and Texture packs are lazy; the server opens only its accepted lazy types. Keep the server/eager split aligned with DataPacker output ownership.
- Lazy requests run on background loaders and publish chunk state with release/acquire ordering. `WaitForChunks` may block for readiness, but an already queued request is not reprioritized.
- Audio performs random-access reads without making the whole chunk resident. Texture chunks continue from disk load through `TextureUploadManager`; device loss and island eviction reset GPU state without changing the lazy-pool layout.
- Pack and manifest files are trust boundaries. Invalid required boot structure is fatal; per-chunk read or decompression corruption reports the failure, publishes completion, and leaves the worker and waiters live.

## Lazy-Pool Invariants

- One reserved virtual-memory pool is laid out by cumulative aligned size over the complete lazy-chunk map. Reset code must walk the same complete map order; do not compact pointers or recompute offsets from a subset.
- Compressed chunks reserve their uncompressed size. Decommit/reload may reclaim only page-aligned interiors so pointers, boundary pages, and cumulative offsets remain stable; use it only without concurrent readers.

## Replay Streams

`DifferenceStream` writes full boundaries, per-frame deltas, and checksums. Publishing succeeds only when every sibling file succeeds; any failure removes the complete sibling set. Optional full-frame diagnostics remain gated by `kbReplayFullFrames`.

## See Also

- Game persistence (`../../../Projects/BrokenEngineSandbox/Source/Save/AGENTS.md`)
