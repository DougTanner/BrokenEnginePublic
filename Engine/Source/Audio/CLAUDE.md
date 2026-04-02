# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper). Entirely client-only (`#if defined(BT_CLIENT)`).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrates playback: listener positioning, music crossfading, voice lifecycle, and XAudio2 device reset handling. Owns a time-seeded `RandomEngine` for pitch randomization, keeping audio variance out of the Frame's deterministic random engine.
- **StaticVoice** - Frame-driven 3D sound effects managed by AudioManager. Each voice is tracked by ID, synced with frame sound data (volume, pitch, position, velocity) each update, and faded out when no longer present in the frame. Also provides `LoadXAudio2SourceVoice` for fire-and-forget one-shot playback.
- **StreamingVoice** - Music playback via triple-buffered streaming from lazy-loaded chunks. Buffers are fixed-size arrays (no heap allocation per stream). Signals the main thread via an atomic counter when buffers are consumed. Non-copyable and non-movable; always owned via `unique_ptr`.

## Architecture

### Voice Lifecycle
Two playback modes: **one-shot** (fire-and-forget via `PlayOneShot`/`PlayOneShot3d`, asserts `kPostRender`, skips reconciliation frames, thread-safe via `mOneShotRecursiveMutex`) and **managed** (persistent voices in a flat vector, capped at `kiMaxStaticVoices`, synced from `SoundsInterpolate`/`SoundsPostRender` frame data each tick with swap-and-pop removal after fade-out). `LoadXAudio2SourceVoice` creates XAudio2 source voices; the caller is responsible for any required thread synchronization.

### Music System
Callback-based playlist decoupling: game logic owns track selection, AudioManager handles playback. Crossfade transitions overlap streams with volume fading, triggered when remaining time reaches a threshold.

### 3D Spatial Audio
X3DAudio integration provides distance attenuation, Doppler effect, and multi-channel speaker panning. Listener position updated from player frame data each tick. Custom manual fade applies additional distance-based volume attenuation.

### Logging

Silent failure points (max static voice limit, music playback issues, voice creation failures) emit `Log(kLogAudio, ...)` messages. `kLogAudio` is deactivated by default in `guiLogEnabledCategories` — enable it when debugging audio issues.

### Threading
- **Main thread** - 3D position updates, playlist logic, static voice cleanup, and all buffer filling/submission for streaming voices
- **Worker threads** - One-shot playback (`PlayOneShot`/`PlayOneShot3d`) serialized via `mOneShotRecursiveMutex`
- **XAudio2 thread** - `OnBufferEnd()` only increments an atomic counter (`miBuffersConsumed`); no file I/O or buffer submission occurs on this thread
- **Background thread** - Lazy loading of audio chunks via FileManager

`AudioManager::SubmitStreamingBuffers()` is called from `UpdateMusicStreams()` on the main thread; it polls each stream's `miBuffersConsumed` counter and performs `FillBuffer` + `SubmitSourceBuffer` for any consumed slots. This keeps the XAudio2 callback thread free of I/O and mutex acquisition.

Voice cleanup uses thread-safe handoff: XAudio2 callbacks set an atomic flag; `Update()` on the main thread checks the flag and clears static voices, avoiding data races. `TransitionCurrentToPrevious()` promotes the current music stream to the fading-out previous list. Streams pending destruction are moved into a persistent `mStreamsToDestroy` member (reused each frame to avoid per-frame heap allocation) while holding the mutex, then destroyed after releasing, preventing XAudio2 deadlocks from `DestroyVoice()` waiting on `OnBufferEnd()` callbacks.
