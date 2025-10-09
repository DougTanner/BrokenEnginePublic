# `/Engine/Source/Audio/`

3D spatial audio using DirectXTK AudioEngine (XAudio2).

**Global**: `gpAudioManager`

## Core Components

### Voice Management
- **StaticVoice** - Class for sound effects
  - Constructor takes AudioEngine*, SoundInfo, and Sound parameters
  - Automatically loads and initializes XAudio2 source voice
  - Static LoadVoice() helper for raw voice creation and buffer submission
  - Contains position, volume, pitch, fade properties
  - Move-only class (owns IXAudio2SourceVoice* pointer)
  - Used for short-lived, non-streaming audio (explosions, impacts, etc.)
- **StreamingVoice** - Class for music streaming
  - Chunk location, streaming buffers, block alignment
  - Self-contained streaming operations via member methods
  - Move-only class (contains `unique_ptr` members)
  - Destructor cleans up XAudio2 voice and buffers
  - Used for long-lived, streaming audio (background music)
- **StaticVoiceFlags** - State tracking for StaticVoice (loaded, fading out, etc.)
- **StreamingVoiceFlags** - State tracking for StreamingVoice (stream active, last buffer submitted)
- Frame-based sound tracking via unique IDs for StaticVoice
- Voice pooling and lifecycle management

### StreamingVoice Methods
- `GetRemainingTime()` - Calculates remaining playback time for music streams
- `FillBuffer()` - Fills streaming buffer from chunk with block alignment
- `ProcessNextBuffer()` - Handles buffer completion and queues next buffer
- `UpdateVolume()` - Update volume with fade in/out based on target volume
  - Fades in using sine curve when target is 1.0
  - Fades out using cosine curve when target is 0.0
  - Returns true when fade out is complete
- `SetMusicVolume()` - Set music volume on this voice

### StaticVoice Methods
- `StaticVoice(AudioEngine*, SoundInfo, Sound)` - Constructor that initializes all members and loads voice
  - Initializes frame ID, volume, pitch, fade-out parameters from SoundInfo and Sound
  - Calls static LoadVoice to create and load XAudio2 source voice
  - Voice is ready to start playback after construction (if loading succeeded)
- `LoadVoice(AudioEngine*, IXAudio2SourceVoice*&, crc, bOneShot, b3d)` - Static helper for voice creation
  - Loads audio chunk from FileManager with lazy loading support
  - Allocates XAudio2 source voice with appropriate format
  - Submits audio buffer to voice (one-shot or looping)
  - Used by constructor and by AudioManager::PlayOneShot for direct voice creation

### AudioManager Functions
- `Update(Frame&)` - Process sounds, update 3D positions, manage music cross-fading
- `PlayOneShot(crc, b3d, volume, pitch)` - 2D or 3D fire-and-forget playback
- `PlayOneShot(crc, position, volume, pitch)` - 3D positioned one-shot
- `SetNextTrackCallback(callback)` - Set callback for querying next music track (thread-safe)
  - Accepts std::function<common::crc_t()> callback
  - Called by AudioManager when track ends to get next CRC
  - Gamelogic maintains playlist and index
- `PlayMusic(crc)` - Play specific music track (thread-safe)
  - Immediately transitions to specified track
  - Moves current stream to previous for fade out
  - Called by gamelogic when switching contexts (menu/game)
- `Apply3d()` - Calculate distance attenuation, doppler, panning
- `UpdateMusicStreams(deltaTime)` - Updates all music stream volumes
  - Calls UpdateVolume() on current stream (fade in)
  - Calls UpdateVolume() on all previous streams (fade out)
  - Removes previous streams when fade out complete

### Music System
- **Cross-fading**: Multiple overlapping streams enable smooth 2-second transitions between tracks
- **Cross-fade Timing**: Starts when current track has ≤2 seconds remaining
- **Cross-fade Interpolation**: Sine curve for fade in (0.0→1.0), cosine curve for fade out (1.0→0.0)
- **Separated Design**: StaticVoice for sound effects, StreamingVoice for music streaming
- **Stream Management**:
  - `mpCurrentMusicStream` - Current playing track (fades in from 0.0 to 1.0)
  - `mPreviousStreams` - Vector of previous tracks (each fades out from current volume to 0.0)
  - Streams moved to previous vector when new track starts
  - Automatic cleanup when fade out completes
- **Playlist Management**: Callback-based system
  - AudioManager queries gamelogic via `mGetNextMusicTrack` callback when track ends
  - Gamelogic maintains playlist and index (separate for menu/game)
  - Gamelogic calls `PlayMusic(crc)` when switching contexts
  - Protected by `mMusicStreamMutex` for thread safety
  - Decouples audio system from playlist logic
- Playlist advancement via callback when track nears end (≤2 seconds remaining)
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
  - Delegates to `StreamingVoice::ProcessNextBuffer()` for current and all previous streams
  - Handles all music streams during cross-fade
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
- **Volume State in StreamingVoice**:
  - `mfCurrentVolume` - Current volume level (0.0 to 1.0)
  - `mfTargetVolume` - Target volume (1.0 for fade in, 0.0 for fade out)
  - `mfFadeProgress` - Fade progress from 0.0 to 1.0
- **Transition Flow**:
  - Current stream starts at target 1.0, fades in using sine curve
  - When track ending, current moved to previous vector with target 0.0
  - New track loaded as current with target 1.0
  - Previous streams fade out using cosine curve, removed when complete
  - No state machine required - all state in individual streams

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
  - `mFlags`: StreamingVoiceFlags_t tracking stream active state and last buffer submission
  - `mchunkLocation`: File offset and size from FileManager
  - `miCurrentPosition`: Track read position in audio data
  - `miDataChunkSize`: Total audio data size
  - `miBlockAlign`: ADPCM block alignment from WAVEFORMAT
  - `mbuffers`: Pool of 3 streaming buffers (16KB each, rounded to block alignment)
  - `miActiveBuffer`: Currently playing buffer index
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
- Protected members: current stream, previous streams vector, next track callback
- Lock held during: Update(), OnBufferEnd(), SetNextTrackCallback(), PlayMusic(), destructor
- Prevents race conditions between main thread updates and audio callbacks
- **Stream Destructor Safety**: Stops voice and flushes buffers before destruction
- **Callback Safety**: Callback set with mutex protection, called from Update() with mutex held

### Design Improvements
- **Separated Voice Classes**: StaticVoice and StreamingVoice handle different audio types
  - StaticVoice: Class with constructor for sound effects initialization
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
- **Self-Contained Volume Control**: Volume state and fading logic in StreamingVoice
  - `mfCurrentVolume`, `mfTargetVolume`, `mfFadeProgress` members track fade state
  - `UpdateVolume()`: Handles both fade in (sine) and fade out (cosine) based on target
  - Returns completion status to enable automatic cleanup
  - No external state machine required
- **Encapsulated Voice Creation**: Voice loading moved to respective classes
  - `StaticVoice::LoadVoice()`: Static helper for sound effect voice creation and buffer submission
  - `StaticVoice` constructor: Initializes all members and calls LoadVoice internally
- **RAII Ownership**: StreamingVoice owns its XAudio2 voice pointer
  - Automatic cleanup via RAII destructor (~StreamingVoice())
  - Simplifies cross-fade logic
  - No manual synchronization needed
- **Vector-Based Stream Management**: Previous streams stored in vector for multi-layer fading
  - Enables multiple overlapping fades (future-proof for complex transitions)
  - Automatic cleanup via vector removal when fade complete
  - Simpler than state machine approach
- **Callback-Based Playlist Management**: Decouples audio from gamelogic
  - AudioManager queries next track via callback instead of owning playlist
  - Gamelogic maintains playlist and index (context-specific: menu vs game)
  - `PlayMusic(crc)` allows gamelogic to manually trigger track changes
  - Separation of concerns: AudioManager handles playback, gamelogic handles sequencing
- **Clearer Intent**: Function and method names explicitly indicate their purpose
- **Simplified Logic**: Each component handles only its specific responsibilities
  - StaticVoice: Simple data container for sound effects
  - StreamingVoice: Data loading, streaming, and self-contained volume management
  - AudioManager: Orchestration, stream lifecycle, and 3D positioning
  - Gamelogic: Playlist management and track selection

### Current Limitations
- No environmental reverb effects
- Low-pass filter not enabled (requires XAUDIO2_VOICE_USEFILTER)
- Fixed distance attenuation curve