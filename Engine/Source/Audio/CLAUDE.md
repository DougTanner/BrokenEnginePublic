# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper).

**Global**: `gpAudioManager`

## Files

### AudioManager.h/cpp
Orchestrates audio playback including sound effects and music streaming.

- `Update(Frame&)` - Updates 3D listener, manages music crossfading, pumps AudioEngine
- `PlayOneShot()` / `PlayOneShot3d()` - Fire-and-forget sound effects (2D or 3D positioned)
- `PlayMusic(crc)` - Transitions to specified music track with crossfade
- `SetNextMusicTrackCallback(callback)` - Gamelogic provides next track for playlist advancement

Implements `IVoiceNotify` for AudioEngine callbacks (critical error, reset, device changes).

### StaticVoice.h/cpp
Short-lived sound effects with optional 3D positioning.

Static `LoadXAudio2SourceVoice()` handles voice creation with lazy loading (can fail). Constructor takes pre-created voice pointer. Supports frame-based tracking via unique ID for correlating with game objects. Fade-out system for smooth removal when game objects disappear.

### StreamingVoice.h/cpp
Music playback with multi-buffer streaming from disk.

Uses triple-buffering (3 buffers × 16KB) to prevent audio dropouts. Implements `IVoiceNotify::OnBufferEnd()` for buffer callbacks - when XAudio2 finishes a buffer, the next is filled and submitted on the XAudio2 thread. RAII ownership of XAudio2 voice enables automatic cleanup.

## Architecture

### Music Crossfading
Smooth transitions via overlapping streams with volume fading:
- Current stream fades in (linear interpolation)
- Previous streams vector holds tracks fading out (removed when fade completes)
- Crossfade triggers automatically when remaining time ≤ 1 second

### Playlist System
Callback-based decoupling of playback from playlist logic. Gamelogic owns playlist state (menu vs game music, track index) and provides next track via callback. AudioManager focuses purely on playback mechanics. Thread-safe via recursive mutex since callbacks run on XAudio2 thread.

### 3D Spatial Audio
X3DAudio integration for positioned sound effects:
- Distance attenuation with configurable fade ranges
- Doppler effect from emitter/listener velocity
- Multi-channel output matrix for speaker panning
- Listener position updated from player frame data

### Threading Model
- **Main Thread** - Voice creation, 3D position updates, playlist logic
- **XAudio2 Thread** - `OnBufferEnd()` callbacks trigger next buffer submission
- **Background Thread** - Lazy loading of audio chunks via FileManager

## Design Rationale

**Separated Voice Classes**: StaticVoice and StreamingVoice handle fundamentally different lifetime and memory patterns. Sound effects are small, fully-loaded chunks with simple fire-and-forget playback. Music requires streaming, buffer management, and crossfading.

**Callback-Based Playlists**: Gamelogic owns playlist context while AudioManager handles playback mechanics. This separation allows context-specific music without coupling audio to game state.

**Vector-Based Previous Streams**: Enables multiple overlapping crossfades and simplifies cleanup through standard container operations with RAII.
