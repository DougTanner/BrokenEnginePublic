# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper).

**Global**: `gpAudioManager`

## Key Classes

- **AudioManager** - Orchestrates all audio playback. Manages listener position, music crossfading, and voice lifecycle. Implements `IVoiceNotify` for XAudio2 device change callbacks. Static voices stored in `unordered_map<sound_t, StaticVoice>` for O(1) lookup by sound ID.
- **StaticVoice** - Short-lived sound effects with optional 3D positioning. Static `LoadXAudio2SourceVoice()` factory method integrates with FileManager's lazy loading: checks `IsChunkReady()`, requests chunk load with kHigh priority if not ready, and returns false to defer voice creation. Supports fade-out for smooth removal when game objects disappear. Move-only type with proper XAudio2 voice lifecycle (old voice destroyed on move-assign).
- **StreamingVoice** - Music playback with triple-buffered streaming via `FileManager::ReadChunkData()`. Implements `IVoiceNotify::OnBufferEnd()` for continuous buffer submission. Reads audio data at sequential offsets from a `LazyChunk`, supporting both in-memory and direct-from-disk reads transparently.

## Architecture

### Phase Enforcement
Audio playback methods require the Frame parameter and assert PostRender phase. This prevents duplicate sounds during frame interpolation.

### Music System
Callback-based playlist decoupling: gamelogic owns track selection, AudioManager handles playback mechanics. Crossfade transitions overlap streams with volume fading, triggered automatically when remaining time reaches threshold.

### 3D Spatial Audio
X3DAudio integration provides distance attenuation, Doppler effect from emitter/listener velocity, and multi-channel speaker panning. Listener position updated from player frame data.

### Threading Model
- **Main Thread** - Voice creation, 3D position updates, playlist logic
- **XAudio2 Thread** - `OnBufferEnd()` callbacks trigger next buffer submission
- **Background Thread** - Lazy loading of audio chunks via FileManager

**Deferred Destruction**: StreamingVoice destruction moves streams to local storage while holding the mutex, then destroys after releasing. This prevents XAudio2 deadlocks since `DestroyVoice()` waits for `OnBufferEnd()` callbacks that also acquire the mutex.
