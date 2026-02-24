# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrates all audio playback including listener positioning, music crossfading, and voice lifecycle. Handles XAudio2 device change/reset callbacks and applies 3D spatial calculations to active voices each frame.
- **StaticVoice** - Short-lived sound effects with optional 3D positioning. Integrates with FileManager's lazy loading to defer voice creation until audio data is available. Supports fade-out for smooth removal when game objects disappear.
- **StreamingVoice** - Music playback via triple-buffered streaming from lazy-loaded chunks. Handles continuous buffer submission through XAudio2 callbacks for gapless playback.

## Architecture

### Phase Enforcement
Audio playback methods require the Frame parameter and assert PostRender phase via `FrameFlags::kPostRender`. This prevents duplicate sounds during frame interpolation. One-shot audio (`PlayOneShot`) additionally checks `FrameFlags::kRecalculated` and returns early during frame recalculation to avoid replaying sounds.

### Music System
Callback-based playlist decoupling: gamelogic owns track selection, AudioManager handles playback mechanics. Crossfade transitions overlap streams with volume fading, triggered automatically when remaining time reaches threshold.

### 3D Spatial Audio
X3DAudio integration provides distance attenuation, Doppler effect from emitter/listener velocity, and multi-channel speaker panning. Listener position updated from player frame data.

### Threading Model
- **Main Thread** - Voice creation, 3D position updates, playlist logic
- **XAudio2 Thread** - `OnBufferEnd()` callbacks trigger next buffer submission
- **Background Thread** - Lazy loading of audio chunks via FileManager

**Deferred Destruction**: StreamingVoice destruction moves streams to local storage while holding the mutex, then destroys after releasing. This prevents XAudio2 deadlocks since `DestroyVoice()` waits for `OnBufferEnd()` callbacks that also acquire the mutex.
