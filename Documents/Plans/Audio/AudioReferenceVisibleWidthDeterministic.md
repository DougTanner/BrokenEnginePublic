# Make Audio Reference Visible Width Deterministic

Source: audit follow-up to the camera-height 3D-audio modulation change in `StaticVoices` (originating plan: `C:\Users\dougt\.claude\plans\audio-characteristics-need-to-ancient-lovelace.md`).

## Context

The `UpdateListenerPosition` member of `engine::StaticVoices` lazily captures a reference visible-area width on the first frame where `fVisibleWidth > 0.0f`:

```cpp
if (mfReferenceVisibleWidth == 0.0f && fVisibleWidth > 0.0f)
{
    mfReferenceVisibleWidth = fVisibleWidth;
}
...
float fScale = (mfReferenceVisibleWidth > 0.0f) ? (fVisibleWidth / mfReferenceVisibleWidth) : 1.0f;
mfEffectiveFadeEnd = mfManualFadeEnd * std::max(1.0f, fScale);
```

(`Engine/Source/Audio/StaticVoices.cpp` lines 276-284.)

The intent: at default zoom, audible distance equals `mfManualFadeEnd`; as the player zooms out and visible area widens, audible distance scales proportionally so distant emitters do not fall out of audible range.

The bug shape is determinism. The "first frame where visible width is positive" is *not* the first frame at the floor zoom (`Camera::kfCameraEyeHeightDefault = 150.0f`). The camera starts at `Camera::kfCameraEyeHeightInitial = kfCameraEyeHeightDefault + 48.0f = 198.0f` (`Projects/BrokenEngineSandbox/Source/Graphics/Camera.h:24-27`), which is already 32% above the floor. The reference is also seeded by whatever aspect ratio and FOV happen to be active at that exact moment, so two launches with different window sizes (or with FOV adjusted between sessions) produce different reference values.

The `std::max(1.0f, fScale)` clamp at line 284 absorbs *some* of this — it prevents the audible range from shrinking below default when the player zooms in. But the clamp anchors at "whatever happened on first capture," not at the floor. So:

- Session A: window 1920x1080 at startup → reference captured at `kfCameraEyeHeightInitial` with 16:9 visible width. Audible range at default zoom = `mfManualFadeEnd * (defaultWidth / initialWidth)` → because `defaultWidth < initialWidth`, the clamp activates and audible range = `mfManualFadeEnd`. Effectively correct *by accident of the clamp*.
- Session B: window resized to 1280x800 mid-session, audio system happens to reset, captures reference at the new aspect → different reference value, different scale ramp shape as the player zooms.

The first-launch order-of-events also matters. If audio init runs before the camera has produced a positive `f4RenderVisibleArea`, the capture is deferred to whatever frame first does — that frame's eye height may not even be `kfCameraEyeHeightInitial` if the user has already moved the zoom wheel.

The pre-existing `kfCameraEyeHeightInitial` offset is intentional — it gives a comfortable starting frame. The bug is that the audio reference uses that startup-display value instead of the *floor* value where the audible-range invariant is meant to hold.

## Out of scope

- Whether the audible-range scaling should exist at all. If the listener-on-camera plan (`Documents/Plans/Audio/ListenerOnCameraNotPlayer.md`) lands and obsoletes `mfReferenceVisibleWidth` entirely, this plan closes with no work needed. Otherwise this plan addresses determinism *of the captured reference*, not whether the capture itself is the right shape.
- The cross-channel bleed (`mfChannelBleedT`). Independent of the visible-width path; covered separately by the listener-placement plan.
- The manual fade band division at line 379 — covered separately by `Documents/Plans/Audio/HardenManualFadeBandDivision.md`.
- FOV runtime changes. The current code captures once and never recomputes; this plan changes when/how the reference is computed but does not promise to track FOV changes mid-session unless the analytical option is chosen.

## Acceptance criteria

- Reference visible width does not depend on which frame audio init happens to run on.
- Reference visible width does not depend on the camera's startup eye-height offset (`kfCameraEyeHeightInitial - kfCameraEyeHeightDefault`).
- Two launches with the same FOV / aspect ratio produce identical `mfReferenceVisibleWidth`.
- An aspect-ratio change at runtime either (a) recomputes the reference, or (b) is documented as an unsupported scenario in the audio CLAUDE.md.

## Approach

Two options.

### Option A — Recompute on FOV / aspect-ratio change

Keep the lazy-capture shape but additionally re-capture whenever the camera's projection changes. Plumb a "projection dirty" notification from `CameraBase::CalculateMatricesAndVisibleArea` to `StaticVoices::UpdateListenerPosition`, and in `UpdateListenerPosition` re-capture *only when the camera is at* `kfCameraEyeHeightDefault` — i.e., wait for the player to zoom all the way in once before fixing the reference.

- Pros: minimal API surface change; reuses the existing field.
- Cons: still order-dependent — the player may never zoom all the way in. Forces the reference to drift across sessions if FOV changes mid-game (depending on what "changes" the reference). Adds a cross-subsystem signal for a small thing.

### Option B (recommended) — Derive analytically at startup

Compute the reference width directly from projection math at `kfCameraEyeHeightDefault`, evaluated once per FOV / aspect-ratio configuration. The same math `CameraBase::CalculateMatricesAndVisibleArea` runs is available — extract it into a pure helper that takes `(fEyeHeight, fFovY, fAspect)` and returns the visible-area XZ width, then call it at `Init` time and on aspect change.

- Pros: deterministic by construction; no frame-order dependency; no "wait for zoom-in" race; same result every launch for a given FOV/aspect; trivially extends to FOV changes (recompute on FOV change, no need for a "is the camera at the floor right now" gate).
- Cons: requires extracting / refactoring the visible-area math out of `CalculateMatricesAndVisibleArea`. Modest extra code in `Camera`. Audio now depends on a Camera helper rather than reading a runtime field.

### Recommendation

**Option B**. Determinism is the entire point of this fix and Option A leaves an order-of-events tail. The extraction of the visible-area helper is small (`CameraBase::CalculateMatricesAndVisibleArea` is one function) and is justified independently — a pure "what would the visible area be at eye height H?" helper has obvious utility outside audio. Option A trades complexity (cross-subsystem dirty signal) for less determinism, which is the wrong direction.

Replace `mfReferenceVisibleWidth = fVisibleWidth` capture in `UpdateListenerPosition` with a call to the new `CameraBase::ComputeVisibleAreaWidth(kfCameraEyeHeightDefault, currentFovY, currentAspect)` (exact name TBD) at audio init and on FOV/aspect change. The `mfReferenceVisibleWidth` field stays; it is just sourced differently.

## Critical files

- `Engine/Source/Graphics/CameraBase.h` / `CameraBase.cpp` — add a static / member helper that computes visible-area width given eye height, FOV, and aspect. Extract from the body of `CalculateMatricesAndVisibleArea` if the math is already there in extractable form.
- `Engine/Source/Audio/StaticVoices.cpp` — `UpdateListenerPosition` (lines 276-281): replace the lazy capture with a call to the helper. Or move the seed entirely to `Init`.
- `Engine/Source/Audio/StaticVoices.h` — `mfReferenceVisibleWidth` (line 78): keep as a cached value, possibly mark `static_assert` or comment that it is derived from `kfCameraEyeHeightDefault`.
- `Engine/Source/Audio/CLAUDE.md` — the "audible distance scales with current visible area vs. a per-listener-update reference width" sentence should be updated to "vs. the analytically-derived visible width at `kfCameraEyeHeightDefault`."
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.h` — `kfCameraEyeHeightDefault`, `kfCameraEyeHeightInitial` (lines 24, 27): no edits, just the constants the helper consumes.

## Notes

- This plan is a no-op if `Documents/Plans/Audio/ListenerOnCameraNotPlayer.md` lands first with Option 2/3, which obsoletes `mfReferenceVisibleWidth`. Coordinate ordering: do the listener-placement plan first if both are queued.
- The `std::max(1.0f, fScale)` clamp at line 284 should stay regardless of which option lands here — it is a separate invariant ("audible range never shrinks below default"), not a bandaid for the capture-determinism bug.
- A unit test is not required (project rule: do not add unit tests), but a one-time `LOG(kAudio, kDebug, ...)` at init that prints the computed reference width is cheap and helps catch future regressions where FOV-change recomputation breaks.
