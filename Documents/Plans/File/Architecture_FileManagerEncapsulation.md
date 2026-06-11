# Architecture: FileManager Encapsulation

## Context

Source: /external-architecture-review on `Engine/Source/File`. `FileManager`'s implementation is genuinely deep (sector-aligned unbuffered I/O, pool layout, priority scheduling), but two members are public with zero external users, and one const accessor launders mutation through the global pointer. Tightening these keeps the interface honest without touching behavior.

## Design

### Engine/Source/File/FileManager.h
- Move `mpChunkLocations` (line 142) and `mLoadingFuture` (line 143) into the private section. Grep confirms both are referenced only from `FileManager.cpp` (`mpChunkLocations`: `LoadPackFiles`, `GetMemoryStats`; `mLoadingFuture`: `LoadPackFiles`, `GetEagerChunkMap`). [~5m]

### Engine/Source/File/FileManager.cpp — `GetEagerChunkMap`
- `GetEagerChunkMap() const` (lines 385-395) bypasses its own constness by routing through `gpFileManager->mLoadingFuture.get()` (and `gpProfileManager` boot timers). Replace with an honest shape: extract a private `EnsureEagerLoadComplete()` (non-const member, or a `mutable` future) that drains `mLoadingFuture` + boot-timer bookkeeping, called directly instead of through the global. Pick the simplest form that drops the `gpFileManager->` self-reference. [~15m]

## Critical files

- `Engine/Source/File/FileManager.h`
- `Engine/Source/File/FileManager.cpp`

## Out of scope

- Making the future-drain safe for concurrent callers — owned by `File/Architecture_LoadThreadLifecycleSafety.md` (same function; co-schedule).
- The wider chunk-API surface (`GetLazyChunk` mutable ref, raw `eState` polling by IslandTerrain/TextureUploadManager) — owned by the `File/Architecture_FileManagerSplitDecision.md` decision plan.

## Acceptance criteria

- `mpChunkLocations`/`mLoadingFuture` private; client + server build clean (compile-checked: any hidden external user fails the build).

## Notes

- No determinism/CRC/network/`kiVersion` exposure; visibility/const-correctness change only.

## Verification Notes

Verified against source (2026-06-10):

- Repo-wide grep confirms `mpChunkLocations` and `mLoadingFuture` are referenced only in `Engine/Source/File/FileManager.h`/`.cpp` (`mpChunkLocations`: h:142, cpp:229/230/239/337/779; `mLoadingFuture`: h:143, cpp:316/387/390). `DataPacker/Source/FileManager.*` is a different class sharing the `gpFileManager` global name; it has zero members by either name, so it does not block the move.
- Original plan text mis-listed `GetEagerStats` as a referencing function (it reads `mPackFileData`/`mEagerChunkMap` only); corrected in place to `GetMemoryStats`/`GetEagerChunkMap`.
- `GetEagerChunkMap() const` at lines 385-395 confirmed laundering mutation through `gpFileManager->mLoadingFuture.get()` plus `gpProfileManager` boot timers. Shape caveat for execution: a const member cannot call a non-const private helper on `this` — the "non-const `EnsureEagerLoadComplete()`" option implies making `GetEagerChunkMap` itself non-const (compiles at all call sites: every caller goes through the non-const `gpFileManager` pointer — all in client Graphics/`*Render` TUs); the alternative keeping it const is `mutable std::future`. Either satisfies "drops the `gpFileManager->` self-reference".
- Co-scheduling with `Architecture_LoadThreadLifecycleSafety.md` confirmed sensible — both edit `GetEagerChunkMap`/`mLoadingFuture`; no queue duplication found.
