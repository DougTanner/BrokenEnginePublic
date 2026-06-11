# Refactor: File Function Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/File`. Three functions exceed (or graze) the ~100-line guidance with cleanly separable stages; decomposition is mechanical with one load-bearing invariant to preserve.

## Design

### Engine/Source/File/FileManager.cpp — `LoadPackFiles` (lines 206-383, ~177 lines)
- Split into named private stages: manifest read + lazy-map build (per data type), pool allocation + per-chunk `pData` assignment, persistent-handle open + read-buffer allocation, and the eager-load async body (e.g. `StartEagerLoad()`). **Invariant**: pool sizing and `pData` assignment iterate `mLazyChunkMap` in identical hashmap order (lines 268-282) — keep both walks in one helper or document the pairing where they split (`ResetTextureChunkStates` lines 629-636 is the third copy of this walk; see `File/CLAUDE.md` "Lazy Memory Pool Invariant"). [~30m]

### Engine/Source/File/FileManager.cpp — `LoadChunk` (lines 497-591, ~95 lines, 4-deep nesting)
- Extract the sector-aligned sub-read copy loop (lines 517-563) into a private helper (e.g. `ReadChunkFromDisk(hFile, iFileOffset, iOnDiskSize, pDst, bCompressed)`), flattening the deepest nesting (`while` → `else` → `if (bAligned)` → `for`). [~15m]

### Engine/Source/File/DifferenceStream.h — `DifferenceStreamReader` ctor (lines 152-259, ~107 lines)
- Decompose into private helpers mirroring the file layout: header read+validate, differences load, checksums load, full-frames load (`kbReplayFullFrames`). Straight-line extraction; convert today's early-out `return`s into an `if (!ReadX()) { return; }` chain so `mbLoaded` stays the single success latch. [~20m]

## Critical files

- `Engine/Source/File/FileManager.cpp`
- `Engine/Source/File/FileManager.h` (new private declarations)
- `Engine/Source/File/DifferenceStream.h`

## Out of scope

- Moving the animation-data parse out of `LoadPackFiles` — `File/Architecture_AnimationDataLoadPlacement.md` (land that first; it shrinks the function by ~25 lines and removes a `BT_CLIENT` nest).
- `ReadChunkData`'s three near-duplicate bounds checks (lines 677, 699, 724) — the branches differ subtly (eager header size vs lazy `iDataSize` vs on-disk size); a shared helper would obscure more than it saves (KISS).
- Behavior changes of any kind — pure extraction; byte flow, lock scopes, and state-machine transitions unchanged.

## Acceptance criteria

- Client + server build clean; chunk loading, save/load, and replay behave identically (mechanical extraction — the diff is the criterion).

## Notes

- No determinism/CRC/network/`kiVersion` exposure. The replay-reader decomposition touches replay file parsing — extraction only, no format change.
- Sequence after `Architecture_AnimationDataLoadPlacement.md` (shared `LoadPackFiles` lines) and alongside `Architecture_LoadThreadLifecycleSafety.md` (dtor/async edits) in one File session.

## Verification Notes

Verified against source (2026-06-10):

- Line ranges accurate: `LoadPackFiles` 206-383 (~178 lines), `LoadChunk` 497-591 (~95 lines, nesting `while`(517) → `else`(554) → `if (bAligned)`(542) → `for`(545)), sub-read copy loop 517-563, `DifferenceStreamReader` ctor 152-259 (~108 lines).
- Pool-walk invariant claim matches `File/CLAUDE.md`'s "Lazy Memory Pool Invariant" section exactly (single `VirtualAlloc` sized by cumulative `RoundUp` over the full map; `pData` assigned by walking the same map in the same order; "hashmap iteration order *is* the layout"). The two paired walks at 268-274/277-282 and the third copy at 629-636 confirmed.
- `ReadChunkFromDisk` helper shape viable: `iAlignedOffset`/`iPrefix` derive from `iFileOffset` + the `miSectorSize` member, so the proposed parameter list suffices for a private member helper.
- Reader-ctor caveat for execution: the existing early-out `return`s are at lines 158, 175, 182, 210 only. The checksum stage (229-242) is **clear-and-continue** — on size mismatch it `DEBUG_BREAK`s, clears `mChecksums`, and still proceeds to set `mbLoaded = true`. The `if (!ReadX()) { return; }` conversion applies to the header/differences stages; the checksums helper (and full-frames stage) must preserve the non-failing semantics, or replay loading behavior changes.
- Helpers for `DifferenceStreamReader` are member functions of a class template — they live in `DifferenceStream.h`, no new declarations needed in `FileManager.h` for that part (the `FileManager.h` "new private declarations" note applies to the `LoadPackFiles`/`LoadChunk` stages only).
- Out-of-scope items confirmed (the three `ReadChunkData` bounds checks at 677/699/724 do differ subtly as claimed). No queue duplication.
