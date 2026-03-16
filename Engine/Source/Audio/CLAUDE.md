# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrates playback: listener positioning, music crossfading, voice lifecycle, and XAudio2 device reset handling. Owns a time-seeded `RandomEngine` for pitch randomization, keeping audio variance out of the Frame's deterministic random engine.
- **StaticVoice** - Frame-driven 3D sound effects managed by AudioManager. Each voice is tracked by ID, synced with frame sound data (volume, pitch, position, velocity) each update, and faded out when no longer present in the frame. Also provides `LoadXAudio2SourceVoice` for fire-and-forget one-shot playback.
- **StreamingVoice** - Music playback via triple-buffered streaming from lazy-loaded chunks. Handles continuous buffer submission through XAudio2 callbacks.

## Architecture

### Voice Lifecycle
Two playback modes: **one-shot** (fire-and-forget via `PlayOneShot`/`PlayOneShot3d`, asserts `kPostRender`, skips reconciliation frames, thread-safe via `mOneShotRecursiveMutex`) and **managed** (persistent voices in a flat vector, capped at `kiMaxStaticVoices`, synced from `SoundsInterpolate`/`SoundsPostRender` frame data each tick with swap-and-pop removal after fade-out). `LoadXAudio2SourceVoice` creates XAudio2 source voices; the caller is responsible for any required thread synchronization.

### Music System
Callback-based playlist decoupling: game logic owns track selection, AudioManager handles playback. Crossfade transitions overlap streams with volume fading, triggered when remaining time reaches a threshold.

### 3D Spatial Audio
X3DAudio integration provides distance attenuation, Doppler effect, and multi-channel speaker panning. Listener position updated from player frame data each tick. Custom manual fade applies additional distance-based volume attenuation.

### Threading
- **Main thread** - 3D position updates, playlist logic, static voice cleanup
- **Worker threads** - One-shot playback (`PlayOneShot`/`PlayOneShot3d`) serialized via `mOneShotRecursiveMutex`
- **XAudio2 thread** - `OnBufferEnd()` callbacks trigger next buffer submission
- **Background thread** - Lazy loading of audio chunks via FileManager

Voice cleanup uses thread-safe handoff: XAudio2 callbacks clear streaming voices (mutex-protected) and set an atomic flag; `Update()` on the main thread checks the flag and clears static voices, avoiding data races. StreamingVoice destruction moves streams to local storage while holding the mutex, then destroys after releasing, preventing XAudio2 deadlocks from `DestroyVoice()` waiting on `OnBufferEnd()` callbacks.
