# `/Engine/Source/Audio/`

3D spatial audio via DirectXTK `AudioEngine` (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`). Global: `gpAudioManager`.

## Key Classes

- **AudioManager** - XAudio2 device lifecycle, focus/suspend, device-reset callbacks. Delegates to static and streaming voice subsystems. `Update` refreshes the listener position before driving voice lifecycle so the priority pass sees current-frame listener and fade state.
- **StaticVoices** - 3D spatial one-shots + frame-driven looping voices. Hard-capped voice pool keyed by `crc`. Recursive mutex (3D path re-enters 2D path). 3D sources asserted mono.
- **StreamingVoices** - Music streaming with triple-buffered crossfade. File I/O runs on a dedicated `common::PersistentWorker` (`kThreadStreamingVoiceFill`); per-slot atomic state machine (`SlotState::kEmpty/kFilling/kReady/kSubmitted`) lets the worker fill slots while the main thread submits ready ones. Main-thread `Update` is consumer-only against pre-filled slots; XAudio2 callback only bumps an atomic counter. Strict alternation: every public method (`Update`, `Play`, `CheckTrackTransition`, `Clear`) calls `mFillWorker.Wait()` at entry; only `Update` calls `mFillWorker.Wake(...)` at exit. Worker takes `mMutex` to walk the streams list. First buffer submission also calls `mpVoice->Start()` (deferred from the constructor).

## Frame-Phase Invariants

- `AudioManager::Update` runs post-render on the main thread and owns all XAudio2 pumps and buffer submission.
- One-shot emit and static voice lifecycle both assert post-render phase and early-out during reconciliation replay — replay ticks must not mutate audio state in either direction.
- Static voice lifecycle reads post-render sound ids against the interpolation SOA; listener position comes from the client player row.

## Voice Lifetime

- Two teardown modes: XAudio2-already-destroyed (post-reset/device-loss — just null pointers) vs. we-still-own-the-voices (explicit `DestroyVoice` with >100ms teardown warning).
- `Suspend` stops the XAudio2 processing thread *first* so `DestroyVoice` returns instantly. `Resume` lets normal flow recreate voices on demand.
- Faded-out streaming voices are parked in a deferred-destruction list drained *after* the mutex releases — `DestroyVoice` must wait for `OnBufferEnd` without deadlocking the callback.

## Volume & 3D

- Combined volume squared for a perceptual curve; sfx and music use separate global sliders.
- 3D mix layers X3DAudio (matrix / Doppler / LPF) with a manual piecewise distance fade as a hard override past the physical attenuation floor. The natural attenuation curve drives the priority pass; the audible-floor only clamps the final mix after prioritization, so it never makes a distant sound look loud to the prioritizer.
- Two listener points, decoupled by purpose. `mVecListenerPosition` (camera eye, full XYZ) drives the manual fade distance — altitude inflates 3D listener-to-emitter distance so high zoom pulls ground emitters into the fade band. `mX3dAudioListener.Position` (camera look-at on the world plane at `gBaseHeight`) drives X3DAudio pan/Doppler — pinning it to the world plane keeps the listener-to-ground-emitter vector XY-only, so pan azimuth reflects 'left-of-screen → left-ear' instead of being collapsed near-center by altitude. Listener velocity is zero; Doppler still applies from emitter motion.
- Fade band shape: `mfEffectiveFadeStart` / `mfEffectiveFadeEnd`, the X3DAudio `mfCurveDistanceScaler`, and the audible-floor multiplier `mfManualFadeVolume` are camera-height-lerped per the canonical "Camera-Height-Conditional Uniforms" pattern (see `Engine/Source/Graphics/Render/CLAUDE.md`). Each of the four quantities is a `HeightLerpWrapperQuartet` (`gListenerDistanceStart`, `gListenerDistanceEnd`, `gListenerCurve`, `gListenerAudibleFloor`) in `Engine/Source/Ui/SoundSettingsWrappersBase.cpp` and is exposed in the **Sound > Tweaks** sub-tab as four sliders (`StartHeight`, `EndHeight`, `Low`, `High`); `UpdateListenerPosition` calls `quartet.Resolve(fEyeHeight)` and writes the resolved floats directly. Distances on the consumer side (`Apply3dVolume` / `ComputeAttenuatedVolume`) are 3D against `mVecListenerPosition` (camera eye), so altitude naturally pushes ground emitters into the fade band as the camera climbs. Audible floor is **relative**: `mfManualFadeVolume × fVolume` (not absolute), so a full-voice sound floors at ~−16 dB after `VolumeToPower` squaring while a `fVolume=0` voice stays silent regardless of distance. Per-category wrappers (e.g., `gExplosionVolume`) default 0 in `SoundWrappers.cpp`; without this relative-floor design they would leak at ~0.15 absolute past fade-end.
- Audio reads `game::gpCamera` directly for eye position / height — established cross-layer pattern in `Engine/Source/`, not a `game::gpGame` violation.

## Voice Prioritization

- Static voice slots are scarce; allocation is closest/loudest-wins, not first-come. Per-frame priority pass scores entries by attenuated volume and reclaims XAudio2 voices from the lowest-scoring sounds when over budget.
- One-shots are hard-culled at submission when their attenuated volume falls below the cull threshold — they never enter the pool.
- Looping/persistent entries out of audible range release their XAudio2 voice but keep the bookkeeping entry (inactive state); they re-acquire a voice and fade in via the existing fade-volume on re-entry. Activation/deactivation use a hysteresis band around the cull threshold to prevent flicker at the boundary.
- Pre-emption (budget eviction and range-fall-out) does not instant-stop. The displaced voice enters a fade-out state and ramps down over the sound's configured fade-out time; on completion it returns to the per-CRC pool and the entry goes inactive. A bounded fade-out pool sits above the main voice cap so the freed slot can be handed to the incoming sound immediately while the outgoing one finishes its tail. Overflow `DEBUG_BREAK`s and the deactivation retries next frame.
- Re-entry mid-fade is click-free: if a fading-out entry becomes audible again, the fade is cancelled in place and the regular fade-in ramp resumes from the current volume — no restart, no zero-volume blip.
- Lifecycle is a fixed sequence of single-purpose passes per frame: invalidation marking, priority (handles both inactive and fading-out re-entry), deactivation marking, fade-out advance + retirement, fade-in ramp. Mark-only passes never mutate XAudio2 state directly; the dedicated advance pass owns the transitions.

## Device Reset

- Lost-device detection in `Update` calls `AudioEngine::Reset`, re-caches mastering voice channel count, then clears all voices.
- Device-reset callbacks defer both static-voice and streaming-voice clear via atomic flags consumed at the top of the next `Update`. The streaming flag was added when file I/O moved to the worker thread: `Wake`/`Wait` are designed for single-threaded use (`mbDispatched` is "Only accessed by calling thread"), so XAudio2-callback-thread calls into `StreamingVoices::Clear` would race with main-thread Wake. Deferring restores main-thread exclusivity over the worker.
- `kAudio` log channel is off by default.
