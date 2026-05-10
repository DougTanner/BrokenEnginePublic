# `/Engine/Source/Audio/`

3D spatial audio via DirectXTK `AudioEngine` (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`). Global: `gpAudioManager`.

## Key Classes

- **AudioManager** - XAudio2 device lifecycle, focus/suspend, device-reset callbacks. Delegates to static and streaming voice subsystems. `Update` refreshes the listener position before driving voice lifecycle so the priority pass sees current-frame listener and fade state.
- **StaticVoices** - 3D spatial one-shots + frame-driven looping voices. Hard-capped voice pool keyed by `crc`. Recursive mutex (3D path re-enters 2D path). 3D sources asserted mono.
- **StreamingVoices** - Music streaming with triple-buffered crossfade. Buffer refill on main thread; XAudio2 callback only bumps an atomic counter so file I/O stays off the callback thread.

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
- Fade band shape: full voice volume inside the visible footprint, narrow fade beyond. `mfEffectiveFadeStart = sqrt(visibleHalfWidth² + eyeHeight²)` (listener-to-screen-edge distance). `mfEffectiveFadeEnd = sqrt((1.5*visibleHalfWidth)² + eyeHeight²)` (1.5× off-screen sits at the audible floor). Both grow with zoom-out and shrink with zoom-in. Audible floor is **relative**: `mfManualFadeVolume × fVolume` (not absolute), so a full-voice sound floors at ~−16 dB after `VolumeToPower` squaring while a `fVolume=0` voice stays silent regardless of distance. Per-category wrappers (e.g., `gExplosionVolume`) default 0 in `SoundWrappers.cpp`; without this relative-floor design they would leak at ~0.15 absolute past fade-end. The cross-channel bleed and per-voice volume scale layered on top are currently neutralized (`kfMaxChannelBleedFactor = 0.0f`, `kfMinHeightVolumeScale = 1.0f`) — kludge structure kept so playtest can re-tune if pan still feels off.
- Audio reads `game::gpCamera` directly for eye position / height / visible area — established cross-layer pattern in `Engine/Source/`, not a `game::gpGame` violation.

## Voice Prioritization

- Static voice slots are scarce; allocation is closest/loudest-wins, not first-come. Per-frame priority pass scores entries by attenuated volume and reclaims XAudio2 voices from the lowest-scoring sounds when over budget.
- One-shots are hard-culled at submission when their attenuated volume falls below the cull threshold — they never enter the pool.
- Looping/persistent entries out of audible range release their XAudio2 voice but keep the bookkeeping entry (inactive state); they re-acquire a voice and fade in via the existing fade-volume on re-entry. Activation/deactivation use a hysteresis band around the cull threshold to prevent flicker at the boundary.

## Device Reset

- Lost-device detection in `Update` calls `AudioEngine::Reset`, re-caches mastering voice channel count, then clears all voices.
- Device-reset callbacks defer static-voice clear via an atomic flag consumed at the top of the next `Update`; streaming state clears inline.
- `kAudio` log channel is off by default.
