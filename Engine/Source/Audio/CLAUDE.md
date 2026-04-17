# `/Engine/Source/Audio/`

3D spatial audio via DirectXTK `AudioEngine` (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`). Global: `gpAudioManager`.

## Key Classes

- **AudioManager** - XAudio2 device lifecycle, focus/suspend, device-reset callbacks. Delegates to static and streaming voice subsystems.
- **StaticVoices** - 3D spatial one-shots + frame-driven looping voices. Hard-capped CRC-keyed voice pool avoids `DestroyVoice`/`CreateSourceVoice` churn. Recursive mutex (3D path re-enters 2D path). 3D sources asserted mono.
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
- 3D mix layers X3DAudio (matrix / Doppler / LPF) with a manual piecewise distance fade as a hard override past the physical attenuation floor.

## Device Reset

- Lost-device detection in `Update` calls `AudioEngine::Reset`, re-caches mastering voice channel count, then clears all voices.
- Device-reset callbacks defer static-voice clear via an atomic flag consumed at the top of the next `Update`; streaming state clears inline.
- `kAudio` log channel is off by default.
