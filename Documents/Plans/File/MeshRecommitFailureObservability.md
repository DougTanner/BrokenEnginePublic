# Make RecommitAndReloadChunkRange Failure Observable to Its Caller

## Context

`FileManager::RecommitAndReloadChunkRange` (`Engine/Source/File/FileManager.cpp:927`) soft-fails a `VirtualAlloc(MEM_COMMIT)` failure — logs and returns, per its "fail the recommit soft rather than crash the recovery path" comment. But its only caller, `CreateClientMeshBuffers` (`Engine/Source/Frame/IslandTerrainResidency.cpp:38`), has no way to observe the failure and immediately `memcpy`s from `mpuiMeshIndices` / `mpfMeshPositions`, which point into the still-decommitted pages → access violation inside the `Buffer::Create` fill lambda during device-loss recovery. The soft-fail contract crashes anyway one call later — worse than failing loud, because the crash site obscures the cause.

Requires a double fault (device loss + OS commit failure) to trigger, so severity is low; the fix is contract hygiene.

## Design

Return `bool` from `RecommitAndReloadChunkRange` — `false` on every soft-fail exit (MEM_COMMIT failure, pack-open failure, short read), `true` on success. On `false`, `CreateClientMeshBuffers` fails loud: log `kError` naming the chunk CRC and the recommit as the cause, then throw `std::runtime_error` with a message naming the chunk and failure (the same type `ASSERT` throws; routed to `engine::HandleException` for a crash report outside the debugger) — do **not** proceed to `Buffer::Create`, and do **not** silently skip the template.

Why not skip-and-log: the record-once terrain CB unconditionally binds every template's mesh buffer at record time (`CommandBufferRecordMain.cpp:255-256` — `ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE)` then `RecordBindVertexBuffer`), so a skipped upload leaves a null vertex-buffer bind in the CB — the "renders as evicted" story only applies to textures, not the mesh bind. Skipping would trade one crash for another (or invalid Vulkan), plus extra machinery to survive to a "later recovery" that record-once CBs don't support. Fail-loud at the true cause site converts the obscured downstream access violation into a diagnosable crash report; per repo ASSERT discipline this is the graceful-recovery-impossible branch, where loud-and-attributable is correct.

## Critical files

- `Engine/Source/File/FileManager.h`, `FileManager.cpp` — `RecommitAndReloadChunkRange` signature + return sites
- `Engine/Source/Frame/IslandTerrainResidency.cpp` — `CreateClientMeshBuffers`

## Out of scope

- The lazy-pool decommit/recommit design itself (live plans `Graphics/TextureChunkCpuPoolReclaim.md`, `File/EagerPackBufferReclaim.md`).
- The FileManager split decision (`File/Architecture_FileManagerSplitDecision.md`).

## Notes

- Client-only path (device-loss recovery); no determinism/CRC/`kiVersion`/wire exposure.
- Shares `FileManager.{h,cpp}` with the live File plans — resolve `Architecture_FileManagerSplitDecision` first or refresh citations; small enough to co-schedule with any of them.
- Decision (2026-07-03): **fail-loud** (log `kError` + throw at the call site). Skip-and-log was refuted against current code: the record-once CB binds every template's mesh unconditionally (`CommandBufferRecordMain.cpp:255-256`), so a skipped upload is a null-buffer bind, not a graceful degrade. Rationale detail in Design.
