# `/Engine/Source/Audio/`

3D spatial audio system using DirectXTK AudioEngine (XAudio2 wrapper).

**Global**: `gpAudioManager`

## Architecture Overview

The audio system separates sound effects from music through distinct voice classes:

- **StaticVoice** - Short-lived sound effects (explosions, impacts, etc.)
  - Fire-and-forget playback with 3D positioning
  - Frame-based tracking with unique IDs
  - Automatic fade-out and cleanup when removed from game state

- **StreamingVoice** - Long-playing music with streaming from disk
  - Multi-buffer streaming (3 buffers, 16KB each) to prevent audio dropouts
  - Self-contained streaming operations via callback system
  - Owns and manages its XAudio2 voice lifetime via RAII

Both voice types follow a two-step initialization pattern: static `LoadXAudio2SourceVoice()` methods handle voice creation (can fail), then constructors accept pre-created voice pointers (assumes success).

## Key Systems

### Music Crossfading

Smooth transitions between music tracks using overlapping streams with volume fading:

- **Current Stream** - Active track fading in (sine curve interpolation)
- **Previous Streams** - Vector of older tracks fading out (cosine curve interpolation)
- Crossfade begins automatically when remaining time ≤ crossfade duration
- RAII ownership enables automatic cleanup when fade completes

### Playlist Management

Callback-based system decouples audio playback from playlist logic:

- AudioManager queries gamelogic for next track via `mGetNextMusicTrack` callback
- Gamelogic maintains context-specific playlists (menu vs game)
- Gamelogic can manually trigger track changes via `PlayMusic(crc)`
- Thread-safe via `mMusicStreamRecursiveMutex` (callback runs on XAudio2 thread)

### 3D Spatial Audio

Custom spatial audio using X3DAudio for positioned sound effects:

- Distance attenuation with configurable fade ranges
- Doppler effect calculation from emitter/listener velocity
- Multi-channel output matrix calculation for speaker panning
- Listener position updated from player frame data each update

### Voice Management

Lifecycle management for sound effect voices:

- Frame-based tracking correlates voices with game objects via unique IDs
- Fade-out system for smooth removal when game objects disappear
- Voice pooling in vector for efficient memory reuse
- Automatic cleanup of finished voices

## AudioManager Core Functions

- `Update(Frame&)` - Updates 3D listener position, manages music crossfading, cleans up finished voices
- `PlayOneShot()` / `PlayOneShot3d()` - Fire-and-forget sound effect playback (2D or 3D positioned)
- `PlayMusic(crc)` - Immediately transitions to specified music track
- `SetNextMusicTrackCallback(callback)` - Sets callback for playlist advancement
- `UpdateMusicStreams(deltaTime)` - Handles fade in/out for all active music streams

## Technical Details

### DirectXTK AudioEngine Integration

- Device enumeration via Windows MMDevice API (prefers default endpoint)
- X3DAudio integration for spatial calculations
- `IVoiceNotify` callback interface for streaming buffer management
- Voice allocation/destruction through AudioEngine wrapper

### Threading Model

- **Main Thread** - Voice creation, 3D position updates, playlist logic
- **XAudio2 Thread** - Buffer callbacks (`OnBufferEnd()`) trigger next buffer submission
- **Background Thread** - Lazy loading of audio chunks from FileManager
- Mutex protection for music stream state accessed from multiple threads

### Data Format

- 16-bit PCM audio (uncompressed WAV data)
- Lazy loading with high priority for music, normal priority for sound effects
- Audio metadata stored in `ChunkHeader::AudioHeader` union member
- Volume control uses squared values for perceptually linear curves

## Design Rationale

**Separated Voice Classes**: StaticVoice and StreamingVoice handle fundamentally different lifetime and memory patterns. Sound effects are small, fully-loaded chunks with simple playback. Music requires streaming, buffer management, and crossfading. Separate classes provide type safety and appropriate RAII semantics for each use case.

**Vector-Based Previous Streams**: Using a vector instead of a single previous stream enables future support for complex multi-layer crossfades and simplifies automatic cleanup through standard container operations.

**Callback-Based Playlists**: Gamelogic owns playlist state (menu music vs game music, current track index) while AudioManager focuses purely on playback mechanics. This separation allows context-specific music without coupling audio to game state.

**RAII for Streaming**: StreamingVoice owns its XAudio2 voice pointer and automatically handles cleanup in the destructor. This eliminates manual synchronization and enables safe removal during crossfades by simply removing from the vector.

**Thread-Safe Design**: Music streaming callbacks run on XAudio2's thread while playlist logic runs on main thread. Recursive mutex allows nested locking when callbacks trigger during updates without deadlock risk.
