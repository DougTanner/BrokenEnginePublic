# Corrupt Pack Data Graceful Failure

## Context

Policy (established this session): an `ASSERT` does not prevent a crash — `common::Assert` throws `std::runtime_error`, and on the loading thread (a bare `std::thread`, no try/catch) that bypasses `HandleException` and `std::terminate`s. If corrupt or missing external pack data can crash or hang the process, the path must be fixed with real failure handling, not just an assert.

This pattern was just applied to `DataTypeFromFlags` in `FileManager.cpp`: a corrupt chunk-header type flag (no recognized type bit) now returns `data::kDataTypeCount` and callers degrade gracefully — `LoadChunk` logs `kError`, `DEBUG_BREAK`s, marks the chunk `kReady` with zero-filled pool data, and `NotifyChunkCompletion()`s (so `WaitForChunks` callers don't block forever); `ReadChunkData` logs `kError`, `DEBUG_BREAK`s, and returns `false`.

Three remaining crash/hang paths on bad pack data in `Engine/Source/File/FileManager.cpp` carry the same exposure. All are trust-boundary reads of `.pack`/`.manifest` files (opaque to our code — file content is not validated by us). None touch determinism/CRC/`kiVersion`/network/replay; all are process-local boot/load-thread paths.

1. **`LoadPackFiles` manifest header assert (`FileManager.cpp:228`).** After `manifestStream.read(&dataHeader, ...)`, `ASSERT(dataHeader.iMagic == kiMagic && dataHeader.iVersion == kiVersion)` throws on a corrupt manifest. A *missing* manifest also trips it: the failed `fstream::read` leaves `dataHeader` zero-initialized (`{}`), so magic/version mismatch fires for missing files too — at the main-thread boot path, before the loading thread even starts.

2. **`LoadChunk` zlib uncompress assert (`FileManager.cpp:562`).** After `uncompress(...)` of a `kZlibCompressed` chunk, `ASSERT(iZlibResult == Z_OK && uiUncompressedSize == iDataSize)` throws on the loading thread (`LoadingThread` → `LoadChunk`) when the compressed payload is corrupt or the chunk was mistagged. The loading thread has no try/catch → `std::terminate`, bypassing the `HandleException` crash-report path entirely.

3. **`LoadPackFiles` unchecked `CreateFileW` (`FileManager.cpp:311`).** A missing `.pack` file makes `CreateFileW(... OPEN_EXISTING ...)` return `INVALID_HANDLE_VALUE`, stored unchecked in `mLazyPackFileHandles[i]`. Later `ReadFile` calls on that handle fail with `uiBytesRead == 0`, and `LoadChunk`'s `while (iDataCopied < iOnDiskSize)` sub-read loop (`FileManager.cpp:507-553`) advances `iDataCopied` by `iCopySize` derived from `uiBytesRead` — which is 0 — so the loop **never advances → infinite loop (hang)** on the loading thread.

## Design

Mirror the just-landed `DataTypeFromFlags` shape at each site: log `kError` (always logged), `DEBUG_BREAK()` for debugger diagnosis, then degrade (skip / zero / fail-soft) — never throw, never hang.

### `FileManager.cpp` — `LoadPackFiles` manifest header (item 1)

The `.manifest`/`.pack` set are required assets; a missing or corrupt manifest is not a recoverable "skip this chunk" condition — it means an asset type is unusable. Limping (zero chunk-count, empty `mpChunkLocations[i]`) would defer the failure to every later `WaitForChunks`/`ReadChunkData` for that type, producing a silently broken game rather than a clear diagnosis. **Recommended:** detect the bad/missing manifest here, log `kError` with the manifest path, `DEBUG_BREAK()`, then fail loud user-facing and exit cleanly (see `## Notes` — startup-missing-asset decision, pre-staged for grill). Concretely: replace the bare `ASSERT` with an explicit `if (!manifestStream || dataHeader.iMagic != kiMagic || dataHeader.iVersion != kiVersion)` check (also covers the failed-read/missing-file case the zeroed `dataHeader` exposes) that routes to the chosen clean-exit path. [~20m]

### `FileManager.cpp` — `LoadChunk` zlib uncompress (item 2)

Replace the `ASSERT(iZlibResult == Z_OK && ...)` (`:562`) with a fail-soft branch matching the `DataTypeFromFlags` precedent a few lines above (`:493-500`): on `iZlibResult != Z_OK || static_cast<int64_t>(uiUncompressedSize) != rLazyChunk.iDataSize`, `LOG(kLoading, kError, ...)` (include the crc + zlib result), `DEBUG_BREAK()`, then mark the chunk `kReady` with its (zero-filled) pool data and `NotifyChunkCompletion()`, and `return` — so the loading thread survives and `WaitForChunks` callers unblock. The pool slot is already allocated; leaving it zero-filled yields a blank/placeholder texel block rather than a terminate. [~15m]

### `FileManager.cpp` — `CreateFileW` result + sub-read loop guard (item 3)

Two coordinated guards:
- **At the open site (`:311`):** check the `CreateFileW` result; if `INVALID_HANDLE_VALUE`, `LOG(kLoading, kError, ...)` with the pack path and `DEBUG_BREAK()`. A missing `.pack` for a required type is the same severity as item 1's missing manifest — route it to the same startup-missing-asset decision (grill), rather than storing the invalid handle and discovering it per-chunk later.
- **Defense-in-depth at the read loop (`LoadChunk`, `:507-553`):** even with the open-site check, harden the sub-read loop against a zero-progress read (truncated `.pack`, mid-read I/O failure) so a `uiBytesRead == 0` (or `iCopySize <= 0`) iteration cannot spin forever — break out, `LOG(kLoading, kError, ...)`, `DEBUG_BREAK()`, and fall through to the `kReady`/zero-filled degrade (same as item 2) instead of looping. This closes the hang for any zero-progress cause, not only the missing-file one. [~25m]

## Critical files

- `Engine/Source/File/FileManager.cpp` — `LoadPackFiles` (manifest read `:228`, `CreateFileW` `:311`), `LoadChunk` (uncompress `:562`, sub-read loop `:507-553`)

## Out of scope

- `ReadChunkData`'s corrupt-flags path (`:700-707`) and `LoadChunk`'s `DataTypeFromFlags` path (`:490-500`) — already landed this session (the pattern this plan extends).
- The eager-load path's `ASSERT(pChunkHeader->iMagic == kiMagic && pChunkHeader->crc == rChunkLocation.crc)` (`:343`) — adjacent corrupt-eager-pack assert; same policy applies but is a distinct site (client-only async eager path, different degrade shape). Mention to the user; file separately if wanted, do not fold in.
- The duplicate-CRC `DEBUG_BREAK` paths (`:258`, `:350`) — diagnostic-only, already non-throwing.
- Loading-thread *lifecycle* races (dtor join / future drain / eager-map sync) — owned by `File/Architecture_LoadThreadLifecycleSafety.md`.
- `WaitForChunks` non-reprioritization — documented accepted limitation (`File/CLAUDE.md`).
- Function decomposition of `LoadPackFiles`/`LoadChunk` — owned by `File/Refactor_FunctionDecomposition.md` (the sub-read loop this plan guards is the loop that plan extracts; co-schedule / refresh).

## Acceptance criteria

- A missing or corrupt `.manifest` produces a `kError` log + `DEBUG_BREAK` + the chosen clean user-facing failure — never an uncaught throw.
- A corrupt zlib-compressed chunk fails its single chunk load (`kError` + `DEBUG_BREAK` + `kReady`/zero-filled + completion notify) without terminating the loading thread or blocking `WaitForChunks`.
- A missing `.pack` (or any zero-progress read) cannot infinite-loop the loading thread.

## Notes

- No determinism/CRC/`kiVersion`/replay/network exposure — all paths are process-local boot / loading-thread reads of opaque external files (trust boundary; defensive handling is policy-compliant here, unlike between our own functions).
- The `kError` log + `DEBUG_BREAK` keep the debugger-diagnosis affordance the asserts provided, while removing the throw/terminate/hang.
- **Grill decision (startup-missing-asset, items 1 and 3):** `.pack`/`.manifest` are required assets. Options for "an entire required pack/manifest is missing or corrupt": **(A) recommended** — `LOG kError` + `DEBUG_BREAK` then a user-facing error (message box, consistent with the existing report-path MessageBox pattern) and clean process exit, since limping with zero-filled assets ships a broken game; **(B)** degrade to empty chunk set for that type and continue (defers failure to every consumer, no clear diagnosis); **(C)** route the failure through `HandleException` so it lands in the crash report. Resolve A-vs-B-vs-C before implementation. Items 2 (single corrupt chunk) is *not* part of this decision — a single bad chunk degrades in place; only a wholly missing/corrupt manifest or pack escalates to the process-level decision.
