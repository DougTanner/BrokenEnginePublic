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
- Camera-zoom couples into the 3D mix: audible distance scales with current visible area vs. a per-listener-update reference width (then doubled so visible-edge sounds sit mid-fade-band, not at the floor), stereo channels cross-bleed (capped at 25% to preserve some directionality — a 50% cap would collapse to pure mono), and a global per-voice volume scale lerps from 1.0 at default eye height down to 0.75 at 2x default and beyond. All three factors share the same height-derived 0..1 parameter and recompute once per `UpdateListenerPosition`.
- Audio reads `game::gpCamera` directly for eye height / visible area — established cross-layer pattern in `Engine/Source/`, not a `game::gpGame` violation.

## Voice Prioritization

- Static voice slots are scarce; allocation is closest/loudest-wins, not first-come. Per-frame priority pass scores entries by attenuated volume and reclaims XAudio2 voices from the lowest-scoring sounds when over budget.
- One-shots are hard-culled at submission when their attenuated volume falls below the cull threshold — they never enter the pool.
- Looping/persistent entries out of audible range release their XAudio2 voice but keep the bookkeeping entry (inactive state); they re-acquire a voice and fade in via the existing fade-volume on re-entry. Activation/deactivation use a hysteresis band around the cull threshold to prevent flicker at the boundary.

## Device Reset

- Lost-device detection in `Update` calls `AudioEngine::Reset`, re-caches mastering voice channel count, then clears all voices.
- Device-reset callbacks defer static-voice clear via an atomic flag consumed at the top of the next `Update`; streaming state clears inline.
- `kAudio` log channel is off by default.
