# Refactor: StaticVoices UpdateLifecycle Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Audio`. `StaticVoices::UpdateLifecycle` (`StaticVoices.cpp:144-473`) is ~330 lines — five documented passes (invalidation, priority/cull, deactivation marking, fade-out advance + retirement, fade-in ramp) in one function. The pass structure is legible via comments but each pass also re-derives shared state inefficiently: the `FadeOutCount()` O(n) lambda (`:170-181`) is re-evaluated inside per-voice loops (call sites `:216`, `:370`, `:417` — O(n²) worst case at the 256-entry cap), and the deactivation pass does an O(voices × activated) linear membership scan against a workbuffer id array (`:406-414`).

## Design

### Engine/Source/Audio/StaticVoices.cpp / StaticVoices.h

- Extract the five passes of `StaticVoices::UpdateLifecycle` into private methods named after the CLAUDE.md pass vocabulary (e.g. `InvalidationPass`, `PriorityPass`, `DeactivationPass`, `AdvanceFadeOut`, `AdvanceFadeIn`), with `UpdateLifecycle` reduced to the guard block (`kPostRender` assert, `kRecalculated` early-out, `mbSkipNextInvalidation` consumption) plus the pass sequence. Pass shared state (`rSoundsInterpolate`, `rSoundsPostRender`, `fDeltaTime`) as parameters. Mechanical extraction; no ordering change. [~45m]
- Replace the `FadeOutCount()` lambda with a maintained `int64_t miFadeOutCount` member: increment at the two `kFadingOut` set sites (`:223`, `:425`), decrement at the clear sites (`:347` fade-cancel, `:449` retirement), reset in `Clear`. Replaces all three call sites; turns the budget checks O(1). [~15m]
- Replace the `pActivatedIds` workbuffer array + linear membership scan with a transient per-voice flag (`StaticVoiceFlags::kActivatedThisFrame = 0x04`): the priority pass sets it on the voices it activates/syncs (current array writes at `:356`, `:387`); the deactivation pass — which already iterates all voices unconditionally — consumes and clears it. Removes the scratch allocation (`:250-252`) and the O(n×m) scan. Note the flag is purely transient (never persists across the deactivation pass), and both setter and consumer are skipped together on `kRecalculated` ticks, so no stale-flag path exists. [~20m]

## Critical files

- `Engine/Source/Audio/StaticVoices.h` (flag enum, `miFadeOutCount`, private pass method declarations)
- `Engine/Source/Audio/StaticVoices.cpp`
- `Engine/Source/Audio/StaticVoice.h` (`StaticVoiceFlags` enum value)

## Out of scope

- The existing-voice-by-id linear scan in the priority pass (`:298-306`, O(slots × voices) ≤ 128×256 id compares) — bounded and simple; explicitly accepted, do not add an id→index map.
- Any threshold/behavior change (cull volume, hysteresis band, fade times, pass ordering) — this is a pure decomposition + bookkeeping refactor; audible behavior must be unchanged.
- One-shot path changes — `Audio/Architecture_OneShotPathCleanup.md`.

## Acceptance criteria

- `UpdateLifecycle` body is the guard block + five named pass calls; client builds clean.
- No `FadeOutCount` recomputation loop remains; no workbuffer scratch in the lifecycle path.
- Pass execution order and per-pass logic identical (diff review per pass; audible behavior unchanged in a playtest).

## Notes

- Invariant exposure: none for determinism/CRC (audio is presentation-only; reads `const game::Frame` state, mutates only XAudio2 + private members). Audible lifecycle behavior — needs a playtest. Client-only.
- `StaticVoiceFlags` is `uint8_t` with `0x01`/`0x02` used — `0x04` is free.
- Touches the same lines as `Architecture_OneShotPathCleanup.md`'s file; co-schedule or sequence (see Order.md File Groups).

## Verification Notes

All cited sites verified against source (2026-06-10): `UpdateLifecycle` spans `StaticVoices.cpp:144-473` (~330 lines, five passes matching the CLAUDE.md vocabulary); `FadeOutCount` lambda at `:170-181` with call sites `:216`/`:370`/`:417` (the latter two inside per-voice/per-slot loops — O(n²) worst case at the 256 reserve confirmed); `kFadingOut` set at `:223`/`:425`, cleared at `:347`/`:449`; `pActivatedIds` scratch at `:250-252`, writes `:356`/`:387`, linear scan `:406-414`; `StaticVoiceFlags` is `uint8_t` with `0x01`/`0x02` used.

- `miFadeOutCount` bookkeeping checked exhaustive: the invalidation pass's else-if ordering means erased entries are never `kFadingOut` (inactive/silent entries erase, fading ones are left for pass 4), and swap-with-back erase preserves counts — so the four set/clear sites plus the `Clear()` reset cover every transition.
- `kActivatedThisFrame` stale-flag audit passed: `kRecalculated` early-outs at `:154-157` skip setter and consumer together; the deactivation pass runs unconditionally otherwise (including `iSoundCount==0`); a marked voice is never `kInactive`/`kFadingOut` at consumption time (marking requires `fAttenuated >= kfCullVolume`, which first clears those flags via reactivation/fade-cancel).
- Implementation caveat: that last invariant is load-bearing — clear the flag unconditionally at the top of each deactivation-pass iteration (before the `kInactive`/`kFadingOut` `continue` guards) rather than only on the voices the pass fully processes, so the design stays correct if the priority pass's marking conditions ever change.
