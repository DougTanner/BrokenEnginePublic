# Architecture: StaticVoices One-Shot Path Cleanup

## Context

Source: /external-architecture-review on `Engine/Source/Audio`. The one-shot emit path has three related defects sharing one root — the awkward `PlayOneShot3d` → `PlayOneShot` re-entry boundary:

1. **Replay-guard deviation (verified):** `Audio/CLAUDE.md` documents "replay ticks must not mutate audio state in either direction", but `StaticVoices::PlayOneShot3d` advances `mRandomEngine` (pitch randomization, `StaticVoices.cpp:99-102`) and takes the lock *before* the inner `PlayOneShot`'s `kRecalculated` early-out (`StaticVoices.cpp:25-28`). Replay ticks therefore shift the presentation RNG stream. Presentation-only (never touches gameplay RNG/CRC), but it contradicts the documented invariant.
2. **Recursive mutex is avoidable:** `mOneShotRecursiveMutex` (`StaticVoices.h:75`) is `std::recursive_mutex` solely because `PlayOneShot3d` locks (`:97`) then re-enters `PlayOneShot` which locks again (`:42`). The duplicated pitch-randomization blocks (`:59-62` vs `:99-102`) are a symptom of the same boundary. The mutex itself is genuinely needed (one-shots arrive concurrently from per-coord Dispatch workers), only the recursion is a smell.
3. **Leaky return type:** `PlayOneShot` returns raw `IXAudio2SourceVoice*` (`AudioManager.h:27`, `StaticVoices.h:41`). Grep-verified: no game caller captures it (e.g. `PlayersCombat.cpp:227` discards). A captured pointer would dangle on device reset (`AudioEngine::Reset` destroys all source voices) with no invalidation contract.

## Design

### Engine/Source/Audio/StaticVoices.h / StaticVoices.cpp
- Introduce a private unlocked `PlayOneShotLocked(const game::Frame&, common::crc_t, bool b3d, float fVolume, float fPitch, float fPitchRange)` containing the current `PlayOneShot` body minus the lock; it keeps returning `IXAudio2SourceVoice*` for internal use and owns the single pitch-randomization block. [~20m]
- Public `StaticVoices::PlayOneShot` becomes: guards (`kPostRender` assert, `kRecalculated`, `mbSuspended`, silent-cull) → lock → `PlayOneShotLocked` → return `void`.
- `StaticVoices::PlayOneShot3d` hoists the `kRecalculated` and `mbSuspended` early-outs to the top (before the distance cull, the lock, and any RNG advance), locks once, and calls `PlayOneShotLocked` — deleting its duplicated pitch-randomization block. This restores the documented replay invariant. [~10m]
- Replace `std::recursive_mutex mOneShotRecursiveMutex` with `std::mutex` (rename accordingly, e.g. `mOneShotMutex`); update the comment at `StaticVoices.h:75`. [~5m]

### Engine/Source/Audio/AudioManager.h / AudioManager.cpp
- `AudioManager::PlayOneShot` (`AudioManager.h:27`, forwarding at `AudioManager.cpp:215-218`) returns `void` to match. [~5m]

## Critical files

- `Engine/Source/Audio/StaticVoices.h`, `StaticVoices.cpp`
- `Engine/Source/Audio/AudioManager.h`, `AudioManager.cpp`

## Out of scope

- `UpdateLifecycle` decomposition and pass-level perf — `Audio/Refactor_UpdateLifecycleDecomposition.md`.
- The new-voice start-volume click discipline — `Audio/Refactor_VoiceStartClickDiscipline.md`.
- Any change to the cull/hysteresis thresholds or pitch-randomization behavior on non-replay ticks — ranges and draw ordering on the normal path are preserved (one draw per emitted one-shot, under the lock, exactly as today).

## Acceptance criteria

- Replay ticks (`FrameFlags::kRecalculated`) reach neither the RNG nor the lock in either one-shot path.
- No `std::recursive_mutex` remains in Audio; single pitch-randomization site.
- `PlayOneShot` public surface returns `void` on both `AudioManager` and `StaticVoices`; client + game build clean (no caller uses the return value).

## Notes

- Invariant exposure: replay-path behavior only, and only the presentation RNG stream (time-seeded, never feeds gameplay random streams or CRC — `StaticVoices.cpp:17`). No determinism/CRC/network exposure. Client-only.
- Pre-staged grill decision: hoisting `mbSuspended` into `PlayOneShot3d` means a suspended client also skips the distance computation — strictly less work, no behavior difference (the inner check already rejected). Confirm no caller relies on the (current) RNG advance while suspended; none found.

## Verification Notes

All three defects verified against source (2026-06-10):

- Replay-guard deviation confirmed: `PlayOneShot3d` has no `kRecalculated` or `mbSuspended` early-out of its own — it locks at `:97` and advances `mRandomEngine` at `:99-102` (when `fPitchRange > 0`) before the inner `PlayOneShot`'s `kRecalculated` early-out at `:25-28`.
- Recursive mutex confirmed solely re-entry-driven: the only two lock sites are `PlayOneShot` (`:42`) and `PlayOneShot3d` (`:97`), and the only re-entry is the `:104` inner call. No other recursive acquisition exists.
- Return-capture grep re-confirmed repo-wide: the single external caller is `PlayersCombat.cpp:227` (discards); the only capture is `StaticVoices.cpp:104` (internal, subsumed by `PlayOneShotLocked`). `AudioManager.h:27` / `StaticVoices.h:41` / forwarding `AudioManager.cpp:215-218` all as cited.
- Design caveat (resolve at grill/implementation): `PlayOneShotLocked` owning the single pitch-randomization block must surface the *final randomized pitch* back to `PlayOneShot3d`, whose follow-up `Apply3dVolume` call sets `SetFrequencyRatio(DopplerFactor * fPitch)` (`StaticVoices.cpp:616`) and would otherwise overwrite the randomized ratio with the un-randomized one. Pass pitch by reference (in/out) or return it alongside the voice pointer.
