# `/Engine/Source/Audio/`

3D spatial audio using DirectXTK AudioEngine (XAudio2).

**Global**: `gpAudioManager`

## Core Components

### Voice Management
- **StaticVoice** - Simple struct for sound effects
  - POD struct with position, volume, pitch, fade properties
  - Contains `pVoice` pointer to XAudio2 source voice
  - No special methods (just data members)
  - Used for short-lived, non-streaming audio (explosions, impacts, etc.)
- **StreamingVoice** - Class for music streaming
  - Chunk location, streaming buffers, block alignment
  - Self-contained streaming operations via member methods
  - Move-only class (contains `unique_ptr` members)
  - Destructor cleans up XAudio2 voice and buffers
  - Used for long-lived, streaming audio (background music)
- **VoiceFlags** - State tracking for StaticVoice (fading out, etc.)
- Frame-based sound tracking via unique IDs for StaticVoice
- Voice pooling and lifecycle management

### StreamingVoice Methods
- `GetRemainingTime()` - Calculates remaining playback time for music streams
- `FillBuffer()` - Fills streaming buffer from chunk with block alignment
- `ProcessNextBuffer()` - Handles buffer completion and queues next buffer
- `InitializeMusicStream()` - Initializes streaming data and allocates buffers
- `CreateMusicStream()` - Static factory method to create and initialize music streaming voice
- `CalculateSoundVolume()` - Static helper for sound effect volume calculation (master * sound * local)
- `CalculateMusicVolume()` - Static helper for music volume calculation (master * music)
- `SetCrossFadeVolume()` - Apply cross-fade volume with cosine/sine interpolation
- `SetMusicVolume()` - Set music volume on this voice

### AudioManager Functions
- `Update(Frame&)` - Process sounds, update 3D positions, manage music cross-fading
- `PlayOneShot(crc, b3d, volume, pitch)` - 2D or 3D fire-and-forget playback
- `PlayOneShot(crc, position, volume, pitch)` - 3D positioned one-shot
- `SetMusicPlaylist(playlist)` - Set music tracks to play (thread-safe)
  - Accepts vector of music CRCs to play in sequence
  - Locks mutex for thread safety during state changes
  - Resets all music state and streams
  - Sets music index to 0 to start from beginning
  - Called by game code to configure music tracks
- `Apply3d()` - Calculate distance attenuation, doppler, panning
- `LoadVoice()` - Sound effect voice creation and buffer submission
- `LoadMusicVoice()` - Wrapper that delegates to StreamingVoice::CreateMusicStream factory method
  - Simplified implementation using StreamingVoice static factory
  - Returns true if voice created successfully
- `UpdateCrossFade(deltaTime)` - Manages cross-fade state transitions
  - Uses StreamingVoice::SetMusicVolume() for non-fading playback
  - Uses StreamingVoice::SetCrossFadeVolume() during active cross-fade

### Music System
- **Cross-fading**: Dual music voices enable smooth 2-second transitions between tracks
- **Cross-fade Timing**: Starts when current track has ≤2 seconds remaining (kfCrossfadeDuration)
- **Cross-fade Interpolation**: Cosine/sine curves for perceptually smooth volume transitions
- **Separated Design**: StaticVoice for sound effects, StreamingVoice for music streaming
- **Stream Management**: `mpCurrentMusicStream` and `mpNextMusicStream` (both std::unique_ptr<StreamingVoice>) for overlapping playback
- **Playlist Management**: Dynamic playlist via `SetMusicPlaylist()` function
  - Playlist stored in `mMusicPlaylist` member variable
  - Protected by `mMusicStreamMutex` for thread safety
  - Empty playlist check prevents crashes if no music configured
  - Game code responsible for setting playlist on startup
- Playlist advancement via `DirectX::IVoiceNotify` callbacks
- **Streaming**: Music uses 3-buffer streaming system (16KB each, rounded to block alignment)
- **StreamingVoice**: Tracks chunk location, position, block alignment, streaming buffers
- Buffer size rounded down to ADPCM block boundaries (e.g., 65536 → 65280 for 256-byte blocks)
- Each 65KB buffer provides ~370ms audio at 44.1kHz stereo ADPCM (~1.1 seconds total)
- Volume control: Master/music volume settings squared for perceptual linearity

### 3D Audio Features
- Custom distance attenuation with manual fade ranges (0-150 units)
- Doppler effect calculation via X3DAudio
- Multi-channel output matrix calculation
- Listener position/velocity from player frame data

### Technical Details
- **Format**: ADPCM compressed audio (Microsoft ADPCM, ~4:1 compression)
- **Loading**: Lazy background loading, skips if not ready
- **Priority**: Music chunks load with high priority (LoadPriority::kHigh)
- **Volume**: Squared for perceptually linear curves
- **Memory Layout**: Audio metadata stored in ChunkHeader's AudioHeader union member
- **Preprocessing**: WAV files compressed via Windows SDK `adpcmencode3.exe` in DataPacker

## DirectXTK AudioEngine Interface

### AudioEngine Initialization
- Device enumeration via Windows Multimedia Device API (MMDevice)
- Prefers default audio endpoint, falls back to first available
- Creation flags: `AudioEngine_Default`
- Audio category: `AudioCategory_GameEffects`
- Supports XAudio2 debug configuration (disabled by default)

### AudioEngine Methods Used
- `IsAudioDevicePresent()` - Check if audio device available
- `GetInterface()` - Direct access to IXAudio2 for debug config
- `GetMasterVoice()` - Access mastering voice for output config
- `GetOutputFormat()` - Returns WAVEFORMATEXTENSIBLE
- `GetOutputChannels()` - Channel count for output
- `GetChannelMask()` - Speaker configuration mask
- `GetOutputSampleRate()` - Sample rate of output device
- `Get3DHandle()` - X3DAUDIO_HANDLE for spatial calculations
- `AllocateVoice()` - Create IXAudio2SourceVoice instances
- `DestroyVoice()` - Clean up source voices
- `Reset()` - Handle device loss/change
- `Update()` - Process audio engine per frame

### XAudio2 Voice Interface
- **Voice Creation**: Uses ADPCMWAVEFORMAT from lazy-loaded chunks
- **Voice Flags**: `SoundEffectInstance_Default` for standard voices
- **One-shot vs Looping**: Controlled by bOneShot parameter
- **Initial State**: Volume set to 0.0f until properly configured

### XAudio2 Buffer Submission
```cpp
XAUDIO2_BUFFER structure:
- Flags: XAUDIO2_END_OF_STREAM (effects) or 0 (music)
- AudioBytes: Size from chunk header
- pAudioData: Pointer to ADPCM data
- LoopCount: 0 (one-shot/music) or XAUDIO2_LOOP_INFINITE
- pContext: 'this' for music (enables callbacks), nullptr for effects
```

### IVoiceNotify Callback System
- AudioManager inherits from `IVoiceNotify`
- Registered with AudioEngine via `RegisterNotify(this, false)`
- `OnBufferEnd()` - Triggered when any voice buffer completes
- **Streaming Logic**:
  - Delegates to `StreamingVoice::ProcessNextBuffer()` for both current and next streams
  - Handles both music streams during cross-fade
  - StreamingVoice::ProcessNextBuffer() performs:
    - Voice nullptr safety check
    - Next buffer calculation in circular pool
    - Buffer filling with block alignment via StreamingVoice::FillBuffer()
    - XAUDIO2_BUFFER submission
    - XAUDIO2_END_OF_STREAM flag on final buffer
    - Stream state updates

### 3D Audio Calculations
- **X3DAUDIO_LISTENER**: Position offset by +5.0f Z from player
- **X3DAUDIO_EMITTER**: Per-voice position and velocity
- **X3DAudioCalculate Flags**:
  - `X3DAUDIO_CALCULATE_MATRIX` - Speaker panning
  - `X3DAUDIO_CALCULATE_LPF_DIRECT` - Low-pass filter (unused)
  - `X3DAUDIO_CALCULATE_DOPPLER` - Pitch shift from velocity
- **Output Matrix**: Maps mono sources to multi-channel output
- **Distance Scalers**: 10.0f for both curve and doppler

### Voice Lifetime Management
- StaticVoices tracked in `mVoices` vector with frame-based IDs
- Fade out system for smooth voice removal
- Automatic cleanup when sounds disappear from frame
- StreamingVoices: Contain both voice and streaming data
- StreamingVoice destructor handles voice cleanup automatically
- Voice pooling reduces allocation overhead

### Cross-fade State Management
- **CrossFadeState enum**: `kNone`, `kStarting`, `kActive`
- **kStarting**: Next voice created, about to start playback at 0 volume
- **kActive**: Both voices playing, volumes adjusting over 2 seconds
- **Completion**: Current voice stopped, next becomes current, playlist advances

### Error Handling
- Device presence checked before all operations
- HRESULT checking on all XAudio2 calls
- Graceful fallback if no audio device available
- Error 0x88960001 indicates format mismatch (mono/stereo)
- **Device Reset Handling**: Correctly nulls voice pointers after Reset() since voices are already destroyed
- **Streaming Failures**: Falls back to silence if next track fails to load
- **Callback Thread**: Simple error logging without allocation for thread safety

### Recent Fixes
- **Removed Memory Allocation in Callback**: Replaced CHECK_HRESULT with simple FAILED() check and LOG
- **Added Null Safety**: ProcessStreamingBuffer() checks voice pointer before operations
- **Eliminated Code Duplication**: Refactored OnBufferEnd() to use helper function
- **Fixed Playlist Indexing**: Standardized to modulo operation

### Streaming System Details
- **StreamingVoice Members**:
  - `mpVoice`: XAudio2 source voice (owned by StreamingVoice, destroyed in ~StreamingVoice())
  - `mchunkLocation`: File offset and size from FileManager
  - `miCurrentPosition`: Track read position in audio data
  - `miDataChunkSize`: Total audio data size
  - `miBlockAlign`: ADPCM block alignment from WAVEFORMAT
  - `mbuffers`: Pool of 3 streaming buffers (16KB each, rounded to block alignment)
  - `miActiveBuffer`: Currently playing buffer index
  - `mbStreamActive`: Whether streaming is currently active
  - `mbLastBufferSubmitted`: Track when final buffer was queued
- **StreamingVoice Methods**:
  - `~StreamingVoice()`: Destructor cleans up music voice and buffers
  - `GetRemainingTime()`: Calculates remaining playback time in seconds from FileManager data
  - `FillBuffer()`: Fills streaming buffer with audio data from chunk
    - **Critical**: Ensures reads are aligned to ADPCM block boundaries
    - Rounds read size down to nearest block multiple (prevents corruption)
    - Updates current position after successful read
    - Returns false when no more data available
    - Sets `rbLastBuffer` flag for final buffer
  - `ProcessNextBuffer()`: Handles buffer completion and submission
    - Called from AudioManager::OnBufferEnd() callback
    - Includes nullptr safety check for voice pointer
    - Finds next buffer in circular pool
    - Fills buffer via FillBuffer()
    - Submits buffer to XAudio2 with appropriate flags
    - Updates stream state (active buffer index, last buffer submitted)
  - `InitializeMusicStream()`: Initializes streaming voice with chunk data
    - Sets up chunk location, data size, block alignment
    - Calculates starting position (8 seconds from end)
    - Allocates 3 streaming buffers (16KB each, rounded to block alignment)
    - Fills and submits first buffer to begin playback
- **Buffer Management**:
  - Triple buffering prevents audio dropouts
  - Buffers reused in circular fashion
  - OnBufferEnd() callback triggers StreamingVoice::ProcessNextBuffer()
  - Automatically submits next buffer when one completes

### Thread Safety
- **mMusicStreamMutex**: Protects all music streaming member variables
- Required because `OnBufferEnd()` callback runs on XAudio2 thread
- Protected members: music streams (with embedded voices), cross-fade state, music index, playlist
- Lock held during: Update(), LoadMusicVoice(), OnBufferEnd(), SetMusicPlaylist(), destructor
- Prevents race conditions between main thread updates and audio callbacks
- **Stream Destructor Safety**: Stops voice and flushes buffers before destruction
- **SetMusicPlaylist Safety**: Resets all music state while holding mutex

### Design Improvements
- **Separated Voice Classes**: StaticVoice and StreamingVoice handle different audio types
  - StaticVoice: Simple POD struct for sound effects
  - StreamingVoice: Full class with RAII for music streaming
  - Clear separation of concerns and responsibilities
  - Type safety prevents mixing sound effects with music streams
- **Self-Contained Streaming**: StreamingVoice manages its own streaming operations
  - `FillBuffer()`: StreamingVoice fills its own buffers from chunk data
  - `ProcessNextBuffer()`: StreamingVoice handles buffer cycling and submission
  - `InitializeMusicStream()`: StreamingVoice initializes its own streaming state
  - AudioManager focuses on orchestration, StreamingVoice handles data loading
- **Factory Method Pattern**: Music voice creation encapsulated in StreamingVoice class
  - `StreamingVoice::CreateMusicStream()`: Static factory method creates and initializes music voices
  - Returns `unique_ptr<StreamingVoice>` or nullptr on failure
  - Encapsulates XAudio2 voice allocation and streaming buffer setup
  - AudioManager's `LoadMusicVoice()` simplified to wrapper calling factory
- **Volume Calculation Helpers**: Centralized volume math in StreamingVoice class
  - `StreamingVoice::CalculateSoundVolume()`: Combines master, sound, and local volume (all squared)
  - `StreamingVoice::CalculateMusicVolume()`: Combines master and music volume (both squared)
  - Eliminates repeated `std::pow()` calculations throughout AudioManager
  - Ensures consistent volume curves across all audio
- **Cross-Fade Encapsulation**: Volume interpolation logic in StreamingVoice
  - `StreamingVoice::SetCrossFadeVolume()`: Applies cosine/sine interpolation based on progress
  - `StreamingVoice::SetMusicVolume()`: Simple music volume setter
  - AudioManager's `UpdateCrossFade()` simplified to call StreamingVoice methods
  - Clearer separation: AudioManager manages state, StreamingVoice manages volume
- **Function Separation**: LoadVoice() split into two specialized functions
  - `LoadVoice()`: Handles sound effects with immediate buffer submission
  - `LoadMusicVoice()`: Creates XAudio2 voice, delegates initialization to StreamingVoice
- **RAII Ownership**: StreamingVoice owns its XAudio2 voice pointer
  - Automatic cleanup via RAII destructor (~StreamingVoice())
  - Simplifies cross-fade logic
  - No manual synchronization needed
- **Clearer Intent**: Function and method names explicitly indicate their purpose
- **Simplified Logic**: Each component handles only its specific responsibilities
  - StaticVoice: Simple data container for sound effects
  - StreamingVoice: Data loading, streaming, and voice-specific operations (volume, cross-fade)
  - AudioManager: Orchestration, cross-fade state management, and 3D positioning

### Current Limitations
- No environmental reverb effects
- Low-pass filter not enabled (requires XAUDIO2_VOICE_USEFILTER)
- Fixed distance attenuation curve