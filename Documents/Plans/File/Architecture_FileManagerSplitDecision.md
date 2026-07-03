# Architecture: FileManager Split Decision

## Context

Source: /external-architecture-review on `Engine/Source/File`. `FileManager` fuses two modules with fully disjoint consumer sets: (1) general file I/O — `Exists`/`OpenFile`/`RemoveFile`/`WriteFileAtomically`/`BackupExistingFile` plus the `Write/ReadVersionedFile` templates (consumers: saves, settings, ClientGuid, caches); (2) the packed-asset chunk system — eager/lazy maps, atomic state machine, loading threads, memory pool, decompression (consumers: Graphics, Audio, IslandTerrain). The lazy-chunk `eState` machine's transitions are stored by three files (`FileManager.cpp`, `TextureUploadManager.cpp`, `TextureManager.cpp`) with read-only polling from a fourth (`IslandTerrainResidency.cpp`, via the mutable `GetLazyChunk()` ref), and the reset-path thread-safety contract is documented rather than asserted (`ResetTextureChunkStates` comment, `FileManager.cpp:794-799`).

## Decision (2026-07-03): chose Option C (accept + document) over A (class split) and B (narrow the chunk seam)

- **Against A**: a new `PackManager` singleton + `Engine.h` + four-vcxproj wiring + churn across ~26 TUs (`gpFileManager` call sites) buys cohesion that benefits only readers — the two consumer sets never interfere at runtime, and the oversized-TU problem is already owned by the cheaper, mechanical `File/FileManagerReduceFile.md`.
- **Against B**: the distributed `eState` machine is *deliberate, documented design* — `File/CLAUDE.md` states "the per-chunk `eState` release/acquire machine tolerates any thread performing the store". Intent APIs (e.g. `AdoptChunkGpuHandles`) would be forwarding trampolines that import texture-upload phase vocabulary into FileManager while the real contract (transfer-thread/reset ordering, `FileManager.cpp:794-799`) remains unassertable either way. B is also not behavior-neutral: `TextureUploadManager` passes `&rLazyChunk.vkImage`/`vmaAllocation` directly to `vmaCreateImage` as out-params, so an adopt-after API must stage handles in locals and copy on success — changing what state exists in the chunk mid-failure/device-loss.
- **For C**: KISS/YAGNI for a single-game engine — the fusion works, is thread-correct by design, and is nearly fully documented already; only the *deliberateness* of the two-halves fusion and of the distributed state machine is missing from the docs.

## Design (executable — documentation only, no code changes)

1. In `Engine/Source/File/CLAUDE.md`, under the `## FileManager` heading (before `### Eager vs Lazy`), add a short paragraph (2-3 sentences) stating:
	- `FileManager` deliberately hosts two consumer-disjoint halves — general file I/O + versioned/atomic writes (saves, settings, caches) and the packed-asset chunk system (Graphics, Audio, IslandTerrain) — one class, one `gpFileManager`; do not split into a separate pack/chunk manager unless the directory grows another module.
	- The lazy-chunk `eState` transitions are intentionally distributed: stores in `FileManager.cpp`, `TextureUploadManager.cpp`, `TextureManager.cpp`; read-only polling in `IslandTerrainResidency.cpp` — sanctioned by the release/acquire design (see `### Lazy Loading`), not a seam to be narrowed.
2. No other file changes. `GetLazyChunk()`'s mutable ref stays as-is (upload path writes GPU handles in place via `vmaCreateImage` out-params).

## Critical files

- `Engine/Source/File/CLAUDE.md` — the only file edited.

## Out of scope

- Any code change to `FileManager.h/.cpp`, `TextureUploadManager.cpp`, `TextureManager.cpp`, `IslandTerrain*.cpp` — every rejected option's edits included.
- `DifferenceStream` — unaffected.
- The boot/teardown-window races — `File/Architecture_LoadThreadLifecycleSafety.md` (independent; not blocked by this).
- `FileManager.cpp` size remediation — `File/FileManagerReduceFile.md` executes as planned (this decision un-gates it; its "option A obviates this plan" branch is dead).

## Notes

- Decision resolved 2026-07-03 by code analysis (this file's Decision section); no grill branch remains.
- No determinism/CRC/network/`kiVersion` exposure.
