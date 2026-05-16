# Remove Island Loading `kTemp` Instrumentation

## Context

While diagnosing the island-loading-lag bug this session, `LOG(kTemp, ...)` calls were added across five files to trace the chunk-load → upload → residency pipeline. Per `Common/CLAUDE.md:36`, *"`kTemp` category is reserved for transient agent diagnostics"* — these are explicit one-shot instrumentation that ships out the moment the investigation closes. The lag bug is now fixed (see "Fix Island Loading Lag" plan); the trace points are dead weight and violate the `kTemp`-is-transient contract if left in.

## Design

Pure deletion. Remove each `LOG(kTemp, ...)` site and any temporary local variables they constructed (timing captures, formatted strings, etc.) but leave the surrounding logic intact.

### Files

- **`Engine/Source/File/FileManager.cpp`** — remove the `LOG(kTemp, ...)` lines added in `RequestChunkLoad` (chunk-queued trace) and `LoadChunk` (disk-read start / done with Ready transition).
- **`Engine/Source/Frame/IslandTerrain.cpp`** — remove `kTemp` logs from:
  - `AcquireTextureSlot` first-mint (slot assignment + chunk-queue trace)
  - `AcquireTextureSlot` cold re-acquire path (the spammy per-frame "Island re-acquire" line)
  - Mesh upload path
  - Elevation upload path (added during the lag-fix session for the direct-from-RAM upload)
  - `EvictionSweep` eviction line
  - `RestorationSweep` residency-transition line
- **`Engine/Source/Graphics/Managers/TextureUploadManager.cpp`** — remove the upload-start and upload-done `kTemp` lines.
- **`Engine/Source/Graphics/Managers/TextureManager.cpp`** — remove `kTemp` logs from `ProcessPendingTextures` (adoption + throttle trace).
- **`Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp`** — remove the "Static data received" `kTemp` log added when investigating boot-time subscription timing.

### Sweep

After per-file removal, do a repo-wide search for `kTemp` and `LOG(common::kTemp` to confirm zero remaining references in first-party source. The `kTemp` enum value itself stays in `Common/Log.h` — only its uses are scrubbed.

## Critical files

- `Engine/Source/File/FileManager.cpp` — `RequestChunkLoad`, `LoadChunk`
- `Engine/Source/Frame/IslandTerrain.cpp` — `AcquireTextureSlot`, `EvictionSweep`, `RestorationSweep`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp` — upload start/done
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — `ProcessPendingTextures`
- `Projects/BrokenEngineSandbox/Source/Network/Client/ClientDataReceiver.cpp` — `ApplyReceivedStaticData` hook

## Out of scope

- `kTemp` instrumentation elsewhere in the codebase that was not added during this session — only the island-loading-lag investigation's trace is in scope.
- Demoting any of these lines to `kVerbose`/`kDebug` instead of deleting — if the information becomes useful later, add a proper persistent log at the right level. `kTemp` is not a graduation candidate.
- The `kTemp` enum value in `Common/Log.h` — leave it in place for future agent sessions.

## Notes

- Mechanical deletion; no compile-affecting changes beyond the log lines themselves and any unused local timing/format helpers they referenced.
- If `Temp/IslandPop.txt` or similar capture files are still on disk, they're untouched by this plan — leave them as historical evidence of the bug; the user will clean up.
