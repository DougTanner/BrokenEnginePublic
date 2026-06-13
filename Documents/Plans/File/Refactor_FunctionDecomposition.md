# Refactor: File Function Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/File`. Three functions exceed (or graze) the ~100-line guidance with cleanly separable stages; decomposition is mechanical with one load-bearing invariant to preserve.

## Design

### Engine/Source/File/FileManager.cpp — `LoadPackFiles` (lines 208-361, ~154 lines)
- Split into named private stages: manifest read + lazy-map build (per data type), pool allocation + per-chunk `pData` assignment, persistent-handle open + read-buffer allocation, and the eager-load async body (e.g. `StartEagerLoad()`). **Invariant**: pool sizing and `pData` assignment iterate `mLazyChunkMap` in identical hashmap order (the `iPoolOffset` accumulate loop and the paired `pData`-assignment loop) — keep both walks in one helper or document the pairing where they split (`ResetTextureChunkStates` is the third copy of this walk; see `File/CLAUDE.md` "Lazy Memory Pool Invariant"). [~30m]

### Engine/Source/File/FileManager.cpp — `LoadChunk` (lines 475-569, ~95 lines, 4-deep nesting)
- Extract the sector-aligned sub-read copy loop (lines 495-541) into a private helper (e.g. `ReadChunkFromDisk(hFile, iFileOffset, iOnDiskSize, pDst, bCompressed)`), flattening the deepest nesting (`while` → `else` → `if (bAligned)` → `for`). [~15m]

### Engine/Source/File/DifferenceStream.h — `DifferenceStreamReader` ctor (lines 152-259, ~107 lines)
- Decompose into private helpers mirroring the file layout: header read+validate, differences load, checksums load, full-frames load (`kbReplayFullFrames`). Straight-line extraction; convert today's early-out `return`s into an `if (!ReadX()) { return; }` chain so `mbLoaded` stays the single success latch. [~20m]

## Critical files

- `Engine/Source/File/FileManager.cpp`
- `Engine/Source/File/FileManager.h` (new private declarations)
- `Engine/Source/File/DifferenceStream.h`

## Out of scope

- Moving the animation-data parse out of `LoadPackFiles` — `File/Architecture_AnimationDataLoadPlacement.md` **has landed and been removed**; it already shrank the function by ~23 lines and removed the `BT_CLIENT` nest. The line ranges in this plan reflect the post-landing source.
- `ReadChunkData`'s three near-duplicate bounds checks (lines 655, 677, 702) — the branches differ subtly (eager header size vs lazy `iDataSize` vs on-disk size); a shared helper would obscure more than it saves (KISS).
- Behavior changes of any kind — pure extraction; byte flow, lock scopes, and state-machine transitions unchanged.

## Acceptance criteria

- Client + server build clean; chunk loading, save/load, and replay behave identically (mechanical extraction — the diff is the criterion).

## Notes

- No determinism/CRC/network/`kiVersion` exposure. The replay-reader decomposition touches replay file parsing — extraction only, no format change.
- `Architecture_AnimationDataLoadPlacement.md` (shared `LoadPackFiles` lines) has already landed; sequence alongside `Architecture_LoadThreadLifecycleSafety.md` (dtor/async edits) in one File session.

## Verification Notes

Verified against source (2026-06-10):

- Line ranges (refreshed after `Architecture_AnimationDataLoadPlacement.md` landed, which deleted the scene-parse block from `LoadPackFiles` and shifted everything below it up ~22 lines): `LoadPackFiles` 208-361 (~154 lines), `LoadChunk` ~475 onward (~95 lines, same nesting `while` → `else` → `if (bAligned)` → `for`), sub-read copy loop inside it, `DifferenceStreamReader` ctor 152-259 (~108 lines in `DifferenceStream.h`, unaffected by the FileManager deletion). All FileManager.cpp line numbers in this plan re-refreshed against the post-landing source (2026-06-12).
- Pool-walk invariant claim matches `File/CLAUDE.md`'s "Lazy Memory Pool Invariant" section exactly (single `VirtualAlloc` sized by cumulative `RoundUp` over the full map; `pData` assigned by walking the same map in the same order; "hashmap iteration order *is* the layout"). The two paired walks at 270-274/279-284 and the third copy at 607-614 (inside `ResetTextureChunkStates`) confirmed.
- `ReadChunkFromDisk` helper shape viable: `iAlignedOffset`/`iPrefix` derive from `iFileOffset` + the `miSectorSize` member, so the proposed parameter list suffices for a private member helper.
- Reader-ctor caveat for execution: the existing early-out `return`s are at lines 158, 175, 182, 210 only. The checksum stage (229-242) is **clear-and-continue** — on size mismatch it `DEBUG_BREAK`s, clears `mChecksums`, and still proceeds to set `mbLoaded = true`. The `if (!ReadX()) { return; }` conversion applies to the header/differences stages; the checksums helper (and full-frames stage) must preserve the non-failing semantics, or replay loading behavior changes.
- Helpers for `DifferenceStreamReader` are member functions of a class template — they live in `DifferenceStream.h`, no new declarations needed in `FileManager.h` for that part (the `FileManager.h` "new private declarations" note applies to the `LoadPackFiles`/`LoadChunk` stages only).
- Out-of-scope items confirmed (the three `ReadChunkData` bounds checks at 655/677/702 do differ subtly as claimed). No queue duplication.
