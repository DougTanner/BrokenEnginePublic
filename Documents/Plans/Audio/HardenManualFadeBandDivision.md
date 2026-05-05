# Harden Manual-Fade Band Division Against Zero Denominator

Source: audit follow-up to the camera-height 3D-audio modulation change in `StaticVoices` (originating plan: `C:\Users\dougt\.claude\plans\audio-characteristics-need-to-ancient-lovelace.md`). Pre-existing latent issue, not introduced by the recent change — surfaced during the audit because `mfManualFadeEnd` now feeds into `mfEffectiveFadeEnd` which is the actual divisor.

## Context

The `Apply3dVolume` member of `engine::StaticVoices` interpolates the manual distance fade with:

```cpp
else if (fDistance >= mfManualFadeStart)
{
    float fPercent = std::clamp((fDistance - mfManualFadeStart) / (mfEffectiveFadeEnd - mfManualFadeStart), 0.0f, 1.0f);
    fDistanceVolume = (1.0f - fPercent) * fVolume + fPercent * mfManualFadeVolume;
}
```

(`Engine/Source/Audio/StaticVoices.cpp` line 379.)

The denominator `mfEffectiveFadeEnd - mfManualFadeStart` divides by zero if `Set3dSettings` is ever called with `fManualFadeStart == fManualFadeEnd`. Today the field defaults are `0.0f` and `150.0f` respectively (`StaticVoices.h:74-75`) and only `AudioManager` calls `Set3dSettings`, so the situation does not arise in practice. But:

- `Set3dSettings` is a public member that accepts arbitrary floats with no validation (project rule: "DO NOT add error handling or validation - assume parameters to functions are valid"). Future tweaks-screen wiring or in-game tuning could produce equal values.
- `mfEffectiveFadeEnd` is computed each frame from `mfManualFadeEnd * std::max(1.0f, fScale)` (line 284). The `std::max(1.0f, fScale)` clamp prevents the multiplier from going below 1, so `mfEffectiveFadeEnd >= mfManualFadeEnd`; the only way to reach `mfEffectiveFadeEnd == mfManualFadeStart` is `mfManualFadeEnd == mfManualFadeStart` (which would also produce a zero denom even before the recent change).

A NaN result here propagates into `fDistanceVolume`, then through `VolumeToPower` into `SetVolume`, which would either crash, set NaN volume, or clamp silently inside XAudio2 — all of those are bad outcomes for an unobservable condition that costs one line of code to prevent.

## Out of scope

- Adding general parameter validation to `Set3dSettings`. Project rule against validation; the hardening is at the *use site*, not the *setter*.
- Auditing every other potential div-by-zero in the audio subsystem. This plan addresses the one site flagged by the audit. A broader audit is a separate plan.
- Changing the semantics of `mfManualFadeStart` / `mfManualFadeEnd` defaults. They stay `0.0f` / `150.0f`.
- Replacing the manual fade band with X3DAudio's natural curve. Out of scope; covered by `Documents/Plans/Audio/ListenerOnCameraNotPlayer.md` if at all.

## Acceptance criteria

- Calling `Set3dSettings(_, X, X, _)` (equal start and end) no longer produces NaN in `fDistanceVolume`.
- Behaviour at strictly-greater fade band (the only currently-reachable case) is bit-identical to today.
- The fix is one line plus a comment.

## Approach

Floor the denominator at a small epsilon. Two equivalent forms:

```cpp
float fDenom = std::max(0.0001f, mfEffectiveFadeEnd - mfManualFadeStart);
float fPercent = std::clamp((fDistance - mfManualFadeStart) / fDenom, 0.0f, 1.0f);
```

or inline:

```cpp
float fPercent = std::clamp((fDistance - mfManualFadeStart) / std::max(0.0001f, mfEffectiveFadeEnd - mfManualFadeStart), 0.0f, 1.0f);
```

Either form works. The named-temporary form is preferred for readability and matches the style guide's mild bias toward intermediate names over deeply nested expressions.

The epsilon `0.0001f` is chosen so that:
- For the legitimate min-band case (start = end - epsilon-ish), the resulting percent saturates the `std::clamp` to 1.0 quickly, which is the correct behaviour: a degenerate band means "everything past start is at fade volume."
- It is well above `FLT_EPSILON` so denormals are not a concern.
- It is well below `1.0f` so it does not affect any reasonable real-world band width.

Optional alternative: a debug-only `ASSERT(mfEffectiveFadeEnd > mfManualFadeStart)` at the top of the `else if` branch. Catches misconfiguration in dev builds without runtime cost in release. Recommend doing both — the assert documents the precondition; the floor handles release.

## Critical files

- `Engine/Source/Audio/StaticVoices.cpp` — `Apply3dVolume`, line 379. Single-line change.
- `Engine/Source/Audio/StaticVoices.h` — no edits; defaults stay.

## Notes

- Pre-existing issue, not caused by the recent camera-height change. The recent change made `mfEffectiveFadeEnd` the variable that participates in the division (previously it was `mfManualFadeEnd` directly), but the underlying division-by-equal-bounds vulnerability has been there since the manual fade band was first added. Audit caught it now because `mfEffectiveFadeEnd` is recomputed every frame and the surface area for "could it equal `mfManualFadeStart`?" widened slightly.
- The fix is mechanical and independent of the listener-placement and reference-width plans; can land in any order relative to them.
- Adding the assert (per "Optional alternative") does not contradict the project's "DO NOT add error handling or validation" rule — assertions are diagnostic aids, not validation that runs in release builds. They are used throughout the codebase (e.g., `ASSERT(rFrame.interpolate.frameFlags & FrameFlags::kPostRender)` two lines up the call stack in `UpdateLifecycle`).
