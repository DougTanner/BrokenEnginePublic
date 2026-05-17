# Plan: Clean Up `Set3dSettings` After Listener Height-Lerp Landed

## Context

The Sound > Tweaks sub-tab landed in `~/.claude/plans/sound-needs-a-tweaks-wondrous-kite.md`: `mfEffectiveFadeStart`, `mfEffectiveFadeEnd`, and `mfCurveDistanceScaler` are now overwritten every frame by `StaticVoices::UpdateListenerPosition` reading the 12 listener wrappers in `Engine/Source/Ui/SoundSettingsWrappersBase.cpp`.

That leaves three loose ends the original plan deferred as out-of-scope:

1. `StaticVoices::Set3dSettings(float fCurveDistanceScaler, float fManualFadeStart, float fManualFadeEnd, float fManualFadeVolume)` — three of the four arguments are now inert (overridden each frame). Only `fManualFadeVolume` still matters.
2. `Projects/BrokenEngineSandbox/Source/Game.cpp:58` calls `engine::gpAudioManager->Set3dSettings(10.0f, 0.0f, 300.0f, 0.15f)` once at startup — the first three literals are dead.
3. `mfManualFadeStart` and `mfManualFadeEnd` member fields on `StaticVoices` (h:88-89) are written by `Set3dSettings` but never read on the consumer path. `mfCurveDistanceScaler` (h:87) is read once in `Apply3dVolume` but the initialiser `= 10.0f` is now only relevant for the brief window between `Init` and the first `UpdateListenerPosition` call.

`mfManualFadeVolume` itself is inconsistent with the rest of the listener block — it's a single hard-coded constant while the three sibling quantities are user-tunable height-lerped wrappers. Promoting it to the same four-wrapper shape would close the inconsistency.

## Out of scope

- Any change to the X3DAudio integration shape (curve, listener orientation, emitter properties).
- Promoting `mfManualFadeVolume` to a height-lerped wrapper if the user prefers it stay a constant — that's a design call to confirm in `/external-grill-plan`, not a foregone conclusion.
- Removing the `StaticVoices` startup defaults wholesale — only the now-redundant members.

## Design

Two cleanup paths; pick one in the grill:

**Path A (minimal):** Collapse `Set3dSettings` to `SetManualFadeVolume(float fManualFadeVolume)`. Delete `mfManualFadeStart` / `mfManualFadeEnd` members. Keep `mfCurveDistanceScaler` (still read by `Apply3dVolume`, its default just becomes initial-frame-only and can stay 10.0f). Update the one caller in `Game.cpp:58` to `engine::gpAudioManager->SetManualFadeVolume(0.15f)`.

**Path B (consistent):** Promote `mfManualFadeVolume` to four wrappers in the same shape as the others (`gListenerFadeVolumeStartHeight/EndHeight/Low/High`). Defaults match the current `0.15f` constant at both Low and High. Add the four sliders to the Sound > Tweaks sub-tab under a new `WrapperSeparatorText("Listener Audible Floor")` block. Delete `Set3dSettings` and the `Game.cpp:58` caller entirely; remove `mfManualFadeStart`/`mfManualFadeEnd` members. `mfManualFadeVolume` now reads from the wrapper-driven lerp in `UpdateListenerPosition` alongside the existing three quantities.

Path B is more consistent and removes a one-shot API; Path A is two-file cleanup with zero new UI surface.

## Critical files

- `Engine/Source/Audio/StaticVoices.h` — member field set, `Set3dSettings` declaration
- `Engine/Source/Audio/StaticVoices.cpp` — `Set3dSettings` body, `UpdateListenerPosition` (Path B adds a fourth `LerpAtHeight` call), `Apply3dVolume` (mfManualFadeVolume read site, unchanged either path)
- `Engine/Source/Audio/AudioManager.h` / `.cpp` — pass-through `Set3dSettings` API
- `Projects/BrokenEngineSandbox/Source/Game.cpp:58` — single caller
- Path B only: `Engine/Source/Ui/SoundSettingsWrappersBase.h/cpp` (+4 wrappers), `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenSound.cpp` (+4 sliders + registrar entries)
- `Engine/Source/Audio/CLAUDE.md` — the "Fade band shape" bullet currently notes `Set3dSettings` argument inertness; rewrite to reflect whichever path lands
- `Documents/Plans/Frame/ScaleEngineToMeters.md` — Audio section currently mentions the `Set3dSettings` callsite as a future cleanup target; remove that line once this lands

## Notes

- Same-day follow-up to the Sound > Tweaks sub-tab change. No replay/CRC implications (audio is client-only and not in the simulation state).
- Verify after change: build client Debug, open Sound > Tweaks, confirm slider moves still affect listener distance/curve in real time.
