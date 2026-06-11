# Refactor: StaticVoice Start Click Discipline + Load Flags

## Context

Source: /external-refactor-clean on `Engine/Source/Audio`. Two small items on the voice-creation seam between `StaticVoice` and `StaticVoices`:

1. **Inconsistent click discipline (audible):** the reactivation path deliberately does `SetVolume(0.0f)` before `Start()` to mask the click (`StaticVoices.cpp:334-339`, with an explanatory comment), but the **new-voice** path starts hot: the `StaticVoice` constructor (`StaticVoice.cpp:70-71`) sets the *unattenuated 2D* volume (`VolumeToPower(gMasterVolume, gSoundVolume, mfVolume)` — no distance attenuation, no audible-floor logic) and calls `Start()`. Until `UpdateVolumes` → `Apply3dVolume` corrects it later the same `AudioManager::Update` (`AudioManager.cpp:285-288`), XAudio2's processing thread can render up to a quantum of the sound at the wrong (typically louder) volume — an audible pop for distant-but-above-cull spawns.
2. **Bool-pair parameters:** `StaticVoice::LoadXAudio2SourceVoice(AudioEngine*, IXAudio2SourceVoice*&, common::crc_t, bool bOneShot, bool b3d)` (`StaticVoice.h:26`) — call sites read as bare bool pairs (`true, b3d` at `StaticVoices.cpp:54`; `false, true` at `:326`, `:380`), the pattern `common::Flags` exists to fix (root CLAUDE.md "Flags over booleans").

## Design

### Engine/Source/Audio/StaticVoice.cpp
- `StaticVoice` constructor: replace the 2D-volume `SetVolume` (line 70) with `SetVolume(0.0f)` before `Start()`, matching the reactivation discipline. [~5m]

### Engine/Source/Audio/StaticVoices.cpp
- New-voice creation site (`StaticVoices.cpp:386-387`): immediately after `mVoices.push_back(...)`, call `Apply3dVolume(pVoice, vecPosition, vecVelocity, fSoundVolume, fPitch)` so the correct attenuated 3D mix is established in the same pass with zero gap (the priority pass already has every argument on hand). The same-frame `UpdateVolumes` then maintains it as today. [~10m]

### Engine/Source/Audio/StaticVoice.h / StaticVoice.cpp + call sites
- Replace the `bool bOneShot, bool b3d` pair on `LoadXAudio2SourceVoice` with `common::Flags<LoadVoiceFlags>` (`kOneShot = 0x01`, `k3d = 0x02`; enum lives next to `StaticVoiceFlags`). Update the three call sites and the `LoopCount` / mono-assert consumers in the function body (`StaticVoice.cpp:29`, `:49`). [~15m]

## Critical files

- `Engine/Source/Audio/StaticVoice.h`, `StaticVoice.cpp`
- `Engine/Source/Audio/StaticVoices.cpp`

## Out of scope

- The reactivation path — already correct; untouched.
- One-shot 2D volume handling in `PlayOneShot` (`StaticVoices.cpp:64-67`) — one-shots set their final volume *before* `Start()`, so they have no wrong-volume window; no change.
- `Clear(bool bNullVoicesBeforeDestroy)`'s single descriptive bool — not proliferation; left as is.

## Acceptance criteria

- A newly spawned persistent voice is never audible at unattenuated 2D volume (its first audible samples are already 3D-mixed).
- No bare bool pair remains at `LoadXAudio2SourceVoice` call sites; client builds clean.

## Notes

- Invariant exposure: audible behavior only — playtest spawn-heavy combat for pops/regressions. No determinism/CRC/network exposure. Client-only.
- If `Audio/Architecture_AudioHelperPlacement.md` lands first, `VolumeToPower` references cited here move to `AudioUtility.h` — symbol names are stable, refresh lines at execution.

## Verification Notes

All items verified against source (2026-06-10):

- Click-discipline asymmetry confirmed: `StaticVoice` ctor sets the unattenuated 2D volume then `Start()` (`StaticVoice.cpp:70-71`); the reactivation path does `SetVolume(0.0f)` before `Start()` with an explanatory comment (`StaticVoices.cpp:334-339`); `UpdateVolumes` corrects later in the same `AudioManager::Update` (`AudioManager.cpp:285`/`:288`). Note both voice sources feeding the ctor have a wrong-volume hazard: freshly-loaded voices are zeroed inside `LoadXAudio2SourceVoice` (`StaticVoice.cpp:38`) but the ctor then *raises* them to the 2D volume pre-`Start`; pooled voices additionally retain their last `Apply3dVolume` level. `SetVolume(0.0f)` in the ctor fixes both.
- New-voice site confirmed: `mVoices.push_back` at `StaticVoices.cpp:386`; `vecPosition`/`vecVelocity`/`fSoundVolume`/`fPitch` all in scope (`:293-296`); `Apply3dVolume` is a private member of the same class, callable there; the proposed arguments match what the same-frame `UpdateVolumes` computes (`mfFadeOutVolume` initializes to 1.0, so `mfFadeOutVolume * mfVolume == fSoundVolume`) — no ramp-behavior change for new voices.
- Bool pair confirmed: `StaticVoice.h:26` signature; exactly three call sites (`StaticVoices.cpp:54` `true, b3d`; `:326` and `:380` `false, true`); body consumers at `StaticVoice.cpp:29` (mono assert) and `:49` (`LoopCount`). `common::Flags` conversion matches the root-CLAUDE.md "Flags over booleans" pattern; no overlap with `Common/Flags.md` (that plan fixes `Flags.h` internals only).
