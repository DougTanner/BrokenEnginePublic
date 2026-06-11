# Architecture: FileManager Split Decision

## Context

Source: /external-architecture-review on `Engine/Source/File`. **Decision plan (present options).** `FileManager` fuses two modules with fully disjoint consumer sets: (1) general file I/O — `Exists`/`OpenFile`/`RemoveFile`/`WriteFileAtomically`/`BackupExistingFile` plus the `Write/ReadVersionedFile` templates (consumers: saves, settings, ClientGuid, caches); (2) the packed-asset chunk system — eager/lazy maps, atomic state machine, loading thread, memory pool, decompression (consumers: Graphics, Audio, IslandTerrain). Related integration risk: the lazy-chunk state machine's transitions are co-owned by four files (FileManager, TextureUploadManager, TextureManager, IslandTerrain) through `GetLazyChunk()`'s mutable `LazyChunk&` and raw `eState` polling, with a thread-safety contract that is documented rather than asserted (`FileManager.cpp:618-627` says so explicitly).

## Design

Present options; resolve via /external-grill-plan before any edit:

- **Option A — split the class.** `FileManager` keeps general file I/O + versioned/atomic writes; a new `PackManager` (name at grill) owns pack files, chunk maps, loading thread, and pool. Two deep modules, each with a small interface. Cost: a new `gp*` singleton, `Engine.h` + vcxproj wiring, call-site churn across ~22 TUs; zero behavior change. [~2-3d]
- **Option B — keep one class, narrow the chunk seam.** Leave the fusion, but replace `GetLazyChunk()` mutable-ref leakage with intent-named APIs (e.g. `AdoptChunkGpuHandles(crc, ...)` for the upload thread's writes, a read-only state poll for IslandTerrain) so state transitions are owned by FileManager alone. Smaller diff; addresses the four-file state machine without the rename ripple. [~1d]
- **Option C — accept and document.** A `File/CLAUDE.md` note naming the two halves and the distributed state machine; revisit if the directory grows.

## Critical files

- `Engine/Source/File/FileManager.h` / `.cpp`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp`, `TextureManager.cpp`, `Engine/Source/Frame/IslandTerrain.cpp` (chunk-seam consumers)
- `Engine/Source/Engine.h` + client/server vcxproj (Option A only)

## Out of scope

- `DifferenceStream` — unaffected by every option.
- The boot/teardown-window races — `File/Architecture_LoadThreadLifecycleSafety.md` (land it first; this decision must not block those fixes).
- Behavior or performance changes of any kind — every option is interface reshaping only.

## Notes

- Decision plan (present options). KISS lens: Option B (or C) is likely the right size for a single-game engine — Option A is the textbook shape but pays a ~22-TU rename for cohesion that mostly bothers readers, not the compiler.
- No determinism/CRC/network/`kiVersion` exposure under any option.

## Verification Notes

Verified against source (2026-06-10):

- Two-module fusion confirmed: general file I/O consumers (`GameSaveLoad`, `ProfileScreens`, `ClientSend`/`ClientReceive` ClientGuid, settings) never touch chunk APIs; chunk consumers (Graphics managers/objects, Audio voices, `IslandTerrain`, game `*Render`) never touch `OpenFile`/`WriteFileAtomically`. Repo grep counts ~23 engine+game TUs referencing `gpFileManager` (matching the "~22 TUs" Option-A cost claim; DataPacker's `gpFileManager` is a separate class in a separate binary).
- Four-file chunk state machine confirmed: `GetLazyChunk()` mutable-ref + raw `eState` polling at `TextureUploadManager.cpp:105/173/419`, `TextureManager.cpp:617/720/735/918`, `IslandTerrain.cpp:514/654/665/701/798/812` — plus FileManager's own transitions. The documented-not-asserted thread-safety contract exists verbatim at `FileManager.cpp:618-627`.
- Out-of-scope ordering confirmed: `Architecture_LoadThreadLifecycleSafety.md` exists and should land first as stated.
- Option B's narrower seam is consistent with the existing `RequestTextureChunkLoad(crc)` free-function precedent (forward-declared in `Collection.h` per `File/CLAUDE.md`).
- Scoring caveat: E4/I3/R3 prices the Option-A worst case; the plan's own KISS lens recommends B/C (~1d, compile-checked interface reshaping, no determinism exposure), which would score nearer E3/I2/R2. Worst-case pricing keeps it low in the queue, which matches the recommendation — acceptable, but flagged.
