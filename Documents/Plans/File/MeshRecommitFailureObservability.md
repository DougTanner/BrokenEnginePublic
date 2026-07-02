# Make RecommitAndReloadChunkRange Failure Observable to Its Caller

## Context

`FileManager::RecommitAndReloadChunkRange` (`Engine/Source/File/FileManager.cpp:927`) soft-fails a `VirtualAlloc(MEM_COMMIT)` failure — logs and returns, per its "fail the recommit soft rather than crash the recovery path" comment. But its only caller, `CreateClientMeshBuffers` (`Engine/Source/Frame/IslandTerrainResidency.cpp:38`), has no way to observe the failure and immediately `memcpy`s from `mpuiMeshIndices` / `mpfMeshPositions`, which point into the still-decommitted pages → access violation inside the `Buffer::Create` fill lambda during device-loss recovery. The soft-fail contract crashes anyway one call later — worse than failing loud, because the crash site obscures the cause.

Requires a double fault (device loss + OS commit failure) to trigger, so severity is low; the fix is contract hygiene.

## Design

Return `bool` from `RecommitAndReloadChunkRange`. On `false`, `CreateClientMeshBuffers` skips the GPU upload for that template and logs `kError` — the template renders as evicted (zero-instance indirect draw; inactive slots are GPU-culled by design) until a later recovery. Alternative if skipping fights the record-once-CB template-count invariant at execution time: fail loud at the call site (log + rethrow `DeviceLostException` equivalent). Recommend skip-and-log.

## Critical files

- `Engine/Source/File/FileManager.h`, `FileManager.cpp` — `RecommitAndReloadChunkRange` signature + return sites
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers`

## Out of scope

- The lazy-pool decommit/recommit design itself (live plans `Graphics/TextureChunkCpuPoolReclaim.md`, `File/EagerPackBufferReclaim.md`).
- The FileManager split decision (`File/Architecture_FileManagerSplitDecision.md`).

## Notes

- Client-only path (device-loss recovery); no determinism/CRC/`kiVersion`/wire exposure.
- Shares `FileManager.{h,cpp}` with the live File plans — resolve `Architecture_FileManagerSplitDecision` first or refresh citations; small enough to co-schedule with any of them.
- Grill decision (single): skip-and-log (recommended) vs fail-loud, per Design.
