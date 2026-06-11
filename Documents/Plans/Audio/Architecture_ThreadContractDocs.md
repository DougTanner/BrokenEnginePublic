# Architecture: Audio Thread-Contract Documentation

## Context

Source: /external-architecture-review on `Engine/Source/Audio`. The area's threading contracts verified clean, but two load-bearing exclusion arguments live nowhere in the code — they exist only in main-loop ordering — and two `Audio/CLAUDE.md` claims are subtly inaccurate against the verified code. Comment/doc-only plan; zero behavior.

## Design

### Engine/Source/Audio/StaticVoices.h
- Document the temporal-exclusion contract on the lock-free listener/fade fields: `mVecListenerPosition`, `mfEffectiveFadeStart`, `mfEffectiveFadeEnd`, `mfCurveDistanceScaler`, `mfManualFadeVolume` are written by main-thread `UpdateListenerPosition` (audio step) and read lock-free by worker-thread `PlayOneShot3d` (`StaticVoices.cpp:91-92`) and `ComputeAttenuatedVolume`. Race-free **only** because the main loop strictly sequences ClientUpdate (per-coord dispatch joins) → Render → `AudioManager::Update` (`Main.cpp:285-304`); same for `UpdateLifecycle`/`UpdateVolumes`/`Clear` mutating `mVoices`/`mPooledVoices` without the one-shot mutex. A member-block comment naming that sequencing is the deliverable. [~10m]

### Engine/Source/Audio/StreamingVoices.cpp
- Document `StreamingVoices::GetStreamCount` (lines 160-163) as a deliberate off-contract read: it takes neither `mFillWorker.Wait()` nor `mMutex` (called at `AudioManager.cpp:291` right after `Update` wakes the worker). Safe because the fill worker never mutates the stream *containers* (only stream internals, under `mMutex`); say so at the function. [~5m]

### Engine/Source/Audio/CLAUDE.md
- Fix "Mark-only passes never mutate XAudio2 state directly": the invalidation pass's already-silent shortcut calls `ReturnVoiceToPool` → `pVoice->Stop` (`StaticVoices.cpp:209-213`). Reword to scope the claim to the fade-transition sources, or name the exception. [~5m]
- Nuance the deferred-destruction claim ("drained after the mutex releases — DestroyVoice must wait for OnBufferEnd without deadlocking the callback"): true for `Update` (`StreamingVoices.cpp:120-122`), but `Clear` destroys streams *while holding* `mMutex` (`StreamingVoices.cpp:129-157`) — safe because `OnBufferEnd` never takes `mMutex`. Note the asymmetry so the invariant reads as path-specific, not class-wide. [~5m]

## Critical files

- `Engine/Source/Audio/StaticVoices.h`
- `Engine/Source/Audio/StreamingVoices.cpp`
- `Engine/Source/Audio/CLAUDE.md`

## Out of scope

- Adding runtime asserts for the temporal exclusion (e.g. a main-thread check in `UpdateLifecycle`) — comment-only by choice; an assert would need a thread-identity utility decision that isn't worth a dependency here.
- The CLAUDE.md "replay must not mutate audio state" claim — the *code* fix in `Audio/Architecture_OneShotPathCleanup.md` restores that claim; do not weaken the doc to match the bug.
- Any locking/code change to `GetStreamCount` or the listener fields.

## Notes

- Comment/doc-only; Risks 0. No determinism/CRC/network exposure.
- If co-scheduled with the other Audio plans, land this last (or refresh line citations) — the code plans move the cited lines.

## Verification Notes

All items verified against source (2026-06-10):

- Lock-free listener/fade reads confirmed: `PlayOneShot3d` reads `mVecListenerPosition` (`StaticVoices.cpp:91`) and the fade fields via `ComputeAttenuatedVolume` (`:92`, fields written at `:502-505`) with no lock. Main-loop sequencing confirmed at `Main.cpp:285-304` exactly as cited (`ClientUpdate` `:285` → `Render` `:293` → `pAudioManager->Update` `:304`).
- `GetStreamCount` (`StreamingVoices.cpp:160-163`) confirmed lockless; called at `AudioManager.cpp:291` after `Update` wakes the worker. The fill worker (`FillReadyBuffers`, under `mMutex`) mutates only stream internals, never the three containers. Note `AudioManager::Suspend` (`AudioManager.cpp:192`) also calls it — likewise main-thread, same argument; worth covering in the same comment.
- CLAUDE.md "mark-only passes" exception confirmed: invalidation's already-silent shortcut calls `ReturnVoiceToPool` (`StaticVoices.cpp:209-213`), which calls `pVoice->Stop` (`:113`).
- Deferred-destruction asymmetry confirmed: `Update` drains `mStreamsToDestroy` after the lock scope (`StreamingVoices.cpp:120-122`); `Clear` resets/clears all three containers while holding `mMutex` (`:129-157`); `StreamingVoice::OnBufferEnd` (`StreamingVoice.cpp:96-99`) is a bare atomic `fetch_add` — never takes `mMutex` — so no deadlock.
