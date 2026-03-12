# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper). Entirely client-only (`#ifdef BT_CLIENT`).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrates playback: listener positioning, music crossfading, voice lifecycle, and XAudio2 device reset handling. Owns a time-seeded `RandomEngine` for pitch randomization, keeping audio variance out of the Frame's deterministic random engine.
- **StaticVoice** - Short-lived sound effects with optional 3D positioning. Integrates with FileManager's lazy loading and supports fade-out for smooth removal.
- **StreamingVoice** - Music playback via triple-buffered streaming from lazy-loaded chunks. Handles continuous buffer submission through XAudio2 callbacks.

## Architecture

### Phase Enforcement
Audio playback asserts `FrameFlags::kPostRender` to prevent duplicate sounds during frame interpolation. One-shot audio also checks `FrameFlags::kRecalculated` and returns early to avoid replaying sounds during reconciliation.

### Music System
Callback-based playlist decoupling: game logic owns track selection, AudioManager handles playback. Crossfade transitions overlap streams with volume fading, triggered when remaining time reaches a threshold.

### 3D Spatial Audio
X3DAudio integration provides distance attenuation, Doppler effect, and multi-channel speaker panning. Listener position updated from player frame data each tick.

### Threading
- **Main thread** - Voice creation, 3D position updates, playlist logic
- **XAudio2 thread** - `OnBufferEnd()` callbacks trigger next buffer submission
- **Background thread** - Lazy loading of audio chunks via FileManager

StreamingVoice destruction moves streams to local storage while holding the mutex, then destroys after releasing, preventing XAudio2 deadlocks from `DestroyVoice()` waiting on `OnBufferEnd()` callbacks.
