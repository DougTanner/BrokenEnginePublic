# Move X3DAudio Listener Onto Camera (Not Player)

Source: audit follow-up to the camera-height 3D-audio modulation change in `StaticVoices` (originating plan: `C:\Users\dougt\.claude\plans\audio-characteristics-need-to-ancient-lovelace.md`).

## Context

The X3DAudio listener position is set in the `UpdateListenerPosition` member of `engine::StaticVoices` from the client player's interpolated XY:

```cpp
vecListenerPos = rFrame.interpolate.pPlayers->pVecPositions[iClientIndex];
```

(`Engine/Source/Audio/StaticVoices.cpp` line 262.)

The camera, however, lifts off the ground as the player zooms out — `mfCameraEyeHeight` ranges from `kfCameraEyeHeightDefault = 150.0f` up to roughly `2 * kfCameraEyeHeightDefault` and beyond. As altitude grows, the geometry X3DAudio uses to compute panning, attenuation, and Doppler diverges from what the player visually perceives: a unit on screen four kilometers away in screen-space is still treated as "right next to" the listener because the listener is glued to the player's XY at z=0.

That divergence is *why* the recent change had to introduce two manual kludges in the `Apply3dVolume` member (line 359) and `UpdateListenerPosition` (line 282-284):

1. Cross-channel bleed up to 50% as eye height rises from `kfCameraEyeHeightDefault` to `2 * kfCameraEyeHeightDefault` (`mfChannelBleedT`), to keep stereo from feeling "pinned" once the camera is far above the player.
2. Audible-distance scaling proportional to visible-area width vs. a captured reference (`mfEffectiveFadeEnd = mfManualFadeEnd * std::max(1.0f, fScale)`), to extend the manual fade so distant emitters do not vanish at high altitude.

Both kludges exist to compensate for X3DAudio's natural falloff/panning model being wrong for this listener placement. Lifting the listener onto the camera (or some hybrid) lets X3DAudio's own falloff replace the manual matrix mix, which is the cleaner long-term shape.

## Out of scope

- Changing `mfCurveDistanceScaler` defaults broadly, except as a calibrated companion to the listener move (see Trade-offs).
- Removing the manual piecewise distance fade in `Apply3dVolume` lines 370-381. That fade is a hard floor below X3DAudio's curve and is independent of where the listener sits — it stays unless a separate plan removes it.
- Static-voice 2D one-shots (`b3d == false`). Their volume path bypasses `Apply3dVolume` and is unaffected.
- Streaming voices (music). They never feed X3DAudio.
- Any change to listener orientation (`OrientFront`, `OrientTop`). The camera does not yaw the audio frame today; out of scope.

## Acceptance criteria

- One concrete listener-placement option chosen (Cleanest / Hybrid / Hybrid+Scaler — see Approach) and documented in `Engine/Source/Audio/CLAUDE.md`.
- After the move, the cross-channel bleed (`mfChannelBleedT` and the Apply3dVolume 359-366 block) is either deleted or its existence justified in a new comment near `Apply3dVolume`.
- `mfReferenceVisibleWidth` / `mfEffectiveFadeEnd` either deleted (if the X3DAudio curve now does the work) or kept with a comment naming what they layer on top of.
- A subjective playtest confirms: high-altitude pan feels natural; mid-altitude near-emitter panning is preserved; low-altitude (near `kfCameraEyeHeightDefault`) is unchanged or improved.

## Approach

Three placements, in increasing intervention cost:

### Option 1 — Cleanest: listener = camera position

Set `mX3dAudioListener.Position` directly from `game::gpCamera->mVecPosition` (or the equivalent camera-space "eye"). Listener velocity becomes camera velocity — meaningful for Doppler only if the camera is treated as the "ear in motion" model, which is consistent with first-person audio practice.

- Pros: collapses the entire kludge surface. X3DAudio's matrix computation now matches what the player sees; pan, attenuation, and LPF behave naturally. The bleed code in `Apply3dVolume` and the visible-width scaling in `UpdateListenerPosition` can be deleted.
- Cons: Doppler semantics change. Today Doppler reflects player motion vs. emitter motion; under Option 1 it reflects camera motion vs. emitter motion. For a top-down RTS where the camera generally tracks the player, the difference is small — but a free-look camera or a snap-to-unit transition will produce audible Doppler artifacts that the current scheme avoids. May also surface bugs where `gpCamera->mVecPosition` is updated at a different cadence than `pPlayers->pVecPositions` (verify before landing).

### Option 2 — Hybrid: listener XY = player, listener Z = camera height

Keep the listener anchored at the player's XY (so left/right and forward/back continue to feel emitter-relative-to-player), but lift the listener vertically to match `gpCamera->mfCameraEyeHeight`. X3DAudio's matrix calculation now sees the correct distance (player + altitude) and pan stays player-anchored.

- Pros: preserves player-relative panning the player is used to. Distance grows correctly, so X3DAudio's natural falloff replaces the visible-width scaling. Doppler unchanged (listener still moves with the player).
- Cons: vertical-only lift produces a subtly different sense of space than a true camera-aligned listener — emitters far from the player but visible on screen still feel "off-axis" from the listener even when they are central to the camera frame. The bleed kludge may still be partially needed at extreme altitudes; verify in playtest.

### Option 3 — Hybrid + curve-distance-scaler ramp

Same as Option 2 but additionally scale `mfCurveDistanceScaler` proportionally to camera height (e.g., grow from `mfCurveDistanceScaler` at `kfCameraEyeHeightDefault` to `2 * mfCurveDistanceScaler` at `2 * kfCameraEyeHeightDefault`). This compensates for the fact that X3DAudio attenuates more aggressively when the listener is far away — without compensation, distant emitters under Option 2 will feel quieter than today.

- Pros: lets the artist re-tune the falloff curve to match the new geometry without resorting to manual matrix mixes. Most flexible knob for matching the existing perceived behaviour.
- Cons: introduces a new artist-facing parameter (the ramp curve) that needs tuning. May feel like trading one kludge for a smaller, better-justified one.

### Recommendation

**Option 2 (hybrid)**. Cheapest move that obsoletes the visible-width scaling for free, preserves Doppler behaviour, and keeps player-anchored panning. If post-playtest the manual bleed code at lines 359-366 still has audible value, that is information about needing Option 3's curve-scaler ramp; if not, delete the bleed code in the same change.

Defer Option 1 (full camera-listener) until a free-look or snap-to-unit camera mode exists — at that point the current player-anchored model breaks differently and a full move becomes the right shape.

## Critical files

- `Engine/Source/Audio/StaticVoices.cpp` — `UpdateListenerPosition` (lines 254-285): change listener Z source. `Apply3dVolume` (lines 328-385): delete or justify the bleed block (359-366) and the `mfEffectiveFadeEnd` consumption (373-381).
- `Engine/Source/Audio/StaticVoices.h` — fields `mfChannelBleedT`, `mfReferenceVisibleWidth`, `mfEffectiveFadeEnd` (lines 78-80): delete if Option 2 obsoletes them; keep with a justifying comment otherwise.
- `Engine/Source/Audio/CLAUDE.md` — the "Camera-zoom couples into the 3D mix" bullet under "Volume & 3D" must be rewritten to reflect the new listener placement.
- `Projects/BrokenEngineSandbox/Source/Graphics/Camera.{h,cpp}` — read-only; verify `mVecPosition` and `mfCameraEyeHeight` cadence aligns with `UpdateListenerPosition`'s post-render call site.

## Notes

- The existing `gpCamera` cross-layer access pattern is documented in `Engine/Source/Audio/CLAUDE.md` ("Audio reads `game::gpCamera` directly for eye height / visible area — established cross-layer pattern in `Engine/Source/`, not a `game::gpGame` violation"). Option 1 and Option 2/3 are equally compatible with that pattern.
- The originating change's `mfReferenceVisibleWidth` capture (lines 278-281) is also flagged separately in `Documents/Plans/Audio/AudioReferenceVisibleWidthDeterministic.md`. If Option 2 is chosen here and the visible-width scaling goes away, that other plan becomes moot — close it in the same session.
- The originating change's manual-fade band is also flagged separately in `Documents/Plans/Audio/HardenManualFadeBandDivision.md`. That hardening is independent of which listener placement is chosen — both plans can land in either order.
