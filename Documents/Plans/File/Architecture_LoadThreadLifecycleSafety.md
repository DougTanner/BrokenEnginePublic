# Architecture: File Load-Thread Lifecycle Safety

## Context

Source: /external-architecture-review on `Engine/Source/File`. The chunk state machine's acquire/release discipline is sound, but the eager-load `std::async` task and the maps it populates carry three latent races — all boot-window or teardown-window, all process-local (no determinism/CRC exposure), all cheap to close.

## Design

### Engine/Source/File/FileManager.cpp — destructor ordering
- `~FileManager` (lines 45-53) joins `mLoadingThread`, but `mLoadingThread` is assigned *on the async task* (line 381), and nothing drains `mLoadingFuture` before teardown on the server (no server-side caller of `GetEagerChunkMap()`). If destruction races the still-running async task, `join()` hits a not-yet-joinable thread (throws → terminate) or races the assignment itself. Fix: drain the future first — `if (mLoadingFuture.valid()) { mLoadingFuture.get(); }` at the top of the dtor, before the shutdown/notify/join sequence. [~5m]
- Optional simplification while there (grill): on `BT_SERVER` the async task body is entirely compiled out except `mLoadingThread = std::thread(...)` — a task spawned solely to spawn a thread. Starting `mLoadingThread` directly and launching the async only under `BT_CLIENT` removes the server's pointless task, at the cost of a second code path; the dtor drain alone already closes the race. [~10m]

### Engine/Source/File/FileManager.cpp — unsynchronized eager-map reads during boot
- `ReadChunkData` (line 670, comment claims "read-only after initialization") and `IsChunkReady`'s `ASSERT` (line 404) read `mEagerChunkMap` unsynchronized while the async task populates it via `try_emplace` (line 344) — concurrent `find` vs rehash is UB. Window: boot-time only. Not reachable today — the off-main-thread readers (`StreamingVoice::FillSlot` → `ReadChunkData` on the `kThreadStreamingVoiceFill` worker; `StaticVoice` → `IsChunkReady`) are driven from `AudioManager::Update` in the main loop, which starts only after the client Graphics boot has drained `mLoadingFuture` via `GetEagerChunkMap()`, and the server never populates the map (`BT_CLIENT` block compiled out). The invariant is enforced solely by that cross-subsystem boot ordering, documented nowhere — this item makes it explicit and cheap to keep true. Fix shape (grill): a `std::atomic<bool> mbEagerLoadComplete` set at the end of the async task; `ReadChunkData`/the `IsChunkReady` ASSERT skip the eager-map lookup until it reads true (acquire) — audio/lazy consumers never need eager chunks mid-boot. Avoid having worker threads call `mLoadingFuture.get()`: `std::future` is single-consumer, which is also why `GetEagerChunkMap()`'s `valid()`+`get()` (lines 387-390) is itself main-thread-only today; the same flag can short-circuit it. [~20m]

### Engine/Source/File/FileManager.cpp — misleading lock in `ReadChunkData`
- The resident-copy path takes `mQueueMutex` around the `eState` check + memcpy (lines 695-707), but the writer of that data (`LoadChunk`) holds no lock — actual visibility comes from the `eState` release/acquire pair (store line 588, load line 696). The mutex implies a protocol that doesn't exist. Meanwhile `ResetTextureChunkStates` rewrites `pData`/`iDataSize` for every lazy chunk with no lock (lines 630-636), formally racing this reader (benign today: idempotent values; the documented precondition at lines 618-627 covers only the upload thread). Resolution (grill): drop the misleading lock and document the acquire-pair contract at both sites, or extend the `ResetTextureChunkStates` precondition comment to name the audio fill worker. Documentation-first is acceptable; do not add new locking to the loading thread's hot path. [~15m]

## Critical files

- `Engine/Source/File/FileManager.cpp`
- `Engine/Source/File/FileManager.h` (new atomic member; `mLoadingFuture` may be private if `Architecture_FileManagerEncapsulation.md` lands first)

## Out of scope

- `WaitForChunks` non-reprioritization of already-queued chunks — documented, accepted limitation (`File/CLAUDE.md`).
- Replacing the hand-rolled thread + priority-queue + CV with `PersistentWorker` — justified as-is: multi-producer priority semantics don't fit the one-Wake-per-Wait contract.
- `mLoadingFuture` visibility — `File/Architecture_FileManagerEncapsulation.md` (same function; co-schedule).

## Acceptance criteria

- Server shutdown immediately after construction cannot terminate (dtor drains the future before join).
- No unsynchronized `mEagerChunkMap` access is reachable before the async population completes.

## Notes

- No determinism/CRC/network/`kiVersion` exposure — all races are process-local boot/teardown windows.
- Grill decisions pre-staged: (1) dtor-drain only vs server direct-start; (2) atomic-flag shape for the eager-map gate; (3) lock-removal vs documentation for the `ReadChunkData` mutex.

## Verification Notes

Verified against source (2026-06-10); one claim corrected:

- Dtor race confirmed: `~FileManager` joins `mLoadingThread` at `FileManager.cpp:53` with no future drain; `mLoadingThread` is assigned at line 381 *inside* the `std::async` task (line 316). Repo grep confirms zero server-side `GetEagerChunkMap()` callers (all 13 call sites are client-only Graphics/`*Render` TUs), so on the server nothing ever drains `mLoadingFuture` — a fast teardown can `join()` a not-yet-joinable (or mid-assignment) thread. This is the plan's strongest item.
- Eager-map race structurally confirmed: `ReadChunkData` (line 670) and the `IsChunkReady` `ASSERT` (line 404) read `mEagerChunkMap` with no synchronization; the async task `try_emplace`s at line 344. **Correction applied to the Design text**: the original "reachable from the streaming-audio fill worker" overstated it — `FillSlot` does run `ReadChunkData` on the `kThreadStreamingVoiceFill` `PersistentWorker` (StreamingVoice.cpp:53; worker contract per `Audio/CLAUDE.md`), but it is woken only from main-loop `AudioManager::Update`, which starts after the client Graphics boot drains the future; the server never populates the map. The item is invariant-hardening of an ordering-implied contract, not an observed bug — kept because the fix is one atomic flag and the comment at line 669 ("read-only after initialization") is otherwise enforced by nothing.
- Misleading-lock claim confirmed: `ReadChunkData` takes `mQueueMutex` at 695-707 around an `eState` acquire-load + memcpy, but `LoadChunk` writes chunk bytes and release-stores `eState` (581/588) with no lock, and `ResetTextureChunkStates` rewrites `pData`/`iDataSize` for every lazy chunk lockless (630-636) under a documented-not-asserted precondition (618-627) that names only the upload thread. Documentation-first resolution is consistent with the existing precedent in that comment block.
- `std::future` single-consumer note accurate; `GetEagerChunkMap()`'s `valid()`+`get()` (387-390) callers are all main-thread boot/render paths today.
- No queue duplication: `Engine/SingletonPublishGlobalGuardSweep.md` also edits `~FileManager` but only wraps the `gpFileManager = nullptr` at line 71 — disjoint edit, line-refresh only if co-scheduled.
