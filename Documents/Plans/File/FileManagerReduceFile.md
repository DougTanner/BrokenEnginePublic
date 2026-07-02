# Reduce FileManager.cpp (crossed 1000-line threshold)

`FileManager.cpp` is 1023 lines, over the 1000-line `/reduce-file` threshold. `repo-code-review` flags it as a REQUIRED split; per the C++ Code Change Process this is a follow-up plan rather than an inline split.

## Context

`FileManager.cpp` (`Engine/Source/File/FileManager.cpp`, 1023 lines) is the packed-asset I/O TU: eager/lazy pack loading, the background loading thread + atomic chunk state machine, the single-`VirtualAlloc` lazy pool, streaming reads, versioned/atomic writes, and memory-stats getters. The `repo-code-review` identified cohesive whole-function groups that relocate cleanly (no body fragmentation):
- **boot / ctor-dtor + `LoadPackFiles`** (pool sizing, pack-file open, eager load kickoff) — orchestration; likely stays as the "core" TU.
- **lazy-loading engine**: `LoadingThread` / `LoadChunk` / `RequestChunkLoad` / `WaitForChunks` / `IsChunkReady` / `NotifyChunkCompletion` / `ReadChunkData`.
- **pool sub-range decommit/recommit**: `DecommitChunkRange` / `RecommitAndReloadChunkRange`.
- **`ResetTextureChunkStates`** (both overloads).
- **`MemoryStats` getters** (`GetEagerStats` / `GetLazyStats` / `GetMemoryStats`).

## Design

`/reduce-file` **whole-function relocation only** — no body fragmentation, no class/API change. Move cohesive groups into new sibling TUs (e.g. `FileManagerLoading.cpp` for the loading thread + chunk state machine + `ReadChunkData` + the decommit/recommit pair; keep ctor/dtor/`LoadPackFiles`/pool-setup + versioned/atomic writes + stats in `FileManager.cpp`, or split stats out too). `FileManager.h` and `gpFileManager` unchanged. New TUs are shared (both builds) — add to all four vcxproj/.filters under `Engine\File`. Move-only: no `kiVersion`/`.pack`/CRC/wire/determinism exposure.

The exact split lines are decided by `/reduce-file` at execution (run the skill, take its plan).

## Critical files
- `Engine/Source/File/FileManager.cpp` (1023 lines) — the split source.
- `Engine/Source/File/FileManager.h` — unchanged (declarations stay).
- The four BrokenEngineSandbox vcxproj/.filters (client + server, `.vcxproj` + `.filters`) — wire the new TU(s) under `Engine\File`.

## Out of scope
- Any class/API change or member-visibility change — that is the separate `File/Architecture_FileManagerSplitDecision.md` decision (split the *class* into disjoint modules). This plan is a mechanical *file* split of the existing single class.
- The behavior of any relocated function.

## Notes
- **Coordinate with / gate on `File/Architecture_FileManagerSplitDecision.md`.** That plan decides whether to split `FileManager` into two *classes* (general I/O vs the chunk system). If it lands **option A (class split)**, that split likely already carves `FileManager.cpp` below 1000 lines and **obviates this plan** (close it). If it lands **option B (narrow seam)** or **option C (accept + document)**, the single class stays > 1000 lines and this mechanical `/reduce-file` executes as the concrete size remediation. Resolve the decision plan first; re-check the line count before running this.
- Move-only, both builds, no `kiVersion`/`.pack`/CRC/wire/determinism/`kbProfiling` exposure.
- Plan only — `/reduce-file` runs at execution; grill: which functions land in which TU (or accept-and-document if the decision plan's class-split is imminent).
