# `/Engine/Source/Audio/`

3D spatial audio using DirectXTK AudioEngine (XAudio2).

**Global**: `gpAudioManager`

## Core Components

### Voice Management
- **Voice** - Active sound with position, volume, pitch, fade properties
- **VoiceFlags** - State tracking (fading out, etc.)
- Frame-based sound tracking via unique IDs
- Voice pooling and lifecycle management

### Key Functions
- `Update(Frame&)` - Process sounds, update 3D positions, manage music cross-fading
- `PlayOneShot(crc, b3d, volume, pitch)` - 2D or 3D fire-and-forget playback
- `PlayOneShot(crc, position, volume, pitch)` - 3D positioned one-shot
- `Apply3d()` - Calculate distance attenuation, doppler, panning
- `LoadVoice()` - Sound effect voice creation and buffer submission
- `LoadMusicVoice()` - Music streaming voice setup with associated stream object
- `FillStreamBuffer()` - Fills streaming buffer from chunk with block alignment
- `UpdateCrossFade(deltaTime)` - Manages cross-fade state transitions and volume curves
- `GetMusicRemainingTime(stream)` - Calculates remaining playback time for cross-fade timing
  - Simple calculation: remaining blocks × samples per block ÷ sample rate
  - Ignores partial blocks since FillStreamBuffer enforces block alignment

### Music System
- **Cross-fading**: Dual music streams enable smooth 2-second transitions between tracks
- **Cross-fade Timing**: Starts when current track has ≤4 seconds remaining
- **Cross-fade Interpolation**: Cosine/sine curves for perceptually smooth volume transitions
- **Unified Design**: `MusicStream` objects contain both voice and streaming data
- **Stream Management**: `mpCurrentMusicStream` and `mpNextMusicStream` for overlapping playback
- Playlist advancement via `DirectX::IVoiceNotify` callbacks
- Combined playlist: `sAllMusic` contains all menu and game tracks
- **Streaming**: Music uses 3-buffer streaming system (65536 bytes each)
- **MusicStream**: Tracks chunk location, position, block alignment
- Buffer size rounded down to ADPCM block boundaries (e.g., 65536 → 65280 for 256-byte blocks)
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
- **Memory Layout**: ADPCMWAVEFORMAT at 0x14, data size at 0x4A, audio data at 0x4E
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
  - Handles both current and next music streams during cross-fade
  - Checks active music streams for buffers needing refill
  - Fills next buffer in circular pool when one completes
  - Submits filled buffers back to voice queue
  - Sets XAUDIO2_END_OF_STREAM flag on final buffer
  - Cross-fade completion swaps next stream to current

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
- Voices tracked in `mVoices` vector with frame-based IDs
- Fade out system for smooth voice removal
- Automatic cleanup when sounds disappear from frame
- Music streams: Contain both voice and streaming data
- Stream destructor handles voice cleanup automatically
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
- **Device Reset Handling**: Nulls voice pointers before stream cleanup to prevent crashes
- **Streaming Failures**: Falls back to silence if next track fails to load
- **Callback Thread**: CHECK_HRESULT used in OnBufferEnd may allocate (potential glitch source)

### Streaming System Details
- **MusicStream Structure**:
  - `pVoice`: Associated XAudio2 source voice (owned by stream)
  - `chunkLocation`: File offset and size from FileManager
  - `uiCurrentPosition`: Track read position in audio data  
  - `uiDataChunkSize`: Total audio data size from offset 0x4A
  - `uiBlockAlign`: ADPCM block alignment from WAVEFORMAT
  - `buffers`: Pool of 3 streaming buffers (65536 bytes each)
  - `iActiveBuffer`: Currently playing buffer index
  - `bStreamActive`: Whether streaming is currently active
  - `bLastBufferSubmitted`: Track when final buffer was queued
- **FillStreamBuffer()**:
  - Reads audio data from chunk at current position + 0x4E offset
  - **Critical**: Ensures reads are aligned to ADPCM block boundaries
  - Rounds read size down to nearest block multiple (prevents corruption)
  - Updates current position after successful read
  - Returns false when no more data available
  - Sets `rbLastBuffer` flag for final buffer
- **Buffer Management**:
  - Triple buffering prevents audio dropouts
  - Buffers reused in circular fashion
  - OnBufferEnd() checks stream states to refill buffers
  - Automatically submits next buffer when one completes

### Thread Safety
- **mMusicStreamMutex**: Protects all music streaming member variables
- Required because `OnBufferEnd()` callback runs on XAudio2 thread
- Protected members: music streams (with embedded voices), cross-fade state, music index
- Lock held during: Update(), LoadMusicVoice(), OnBufferEnd(), destructor
- Prevents race conditions between main thread updates and audio callbacks
- **Stream Destructor Safety**: Stops voice and flushes buffers before destruction

### Design Improvements
- **Function Separation**: LoadVoice() split into two specialized functions
  - `LoadVoice()`: Handles sound effects with immediate buffer submission
  - `LoadMusicVoice()`: Handles music streaming with triple-buffer setup
- **Eliminated Parameter Coupling**: LoadMusicVoice() takes only stream reference
- **Voice Integration**: MusicStream now contains its associated voice
  - Eliminates manual synchronization between voices and streams
  - Automatic cleanup via RAII destructor
  - Simplifies cross-fade logic
- **Clearer Intent**: Function names explicitly indicate their purpose
- **Simplified Logic**: Each function handles only its specific use case

### Current Limitations
- No environmental reverb effects
- Low-pass filter not enabled (requires XAUDIO2_VOICE_USEFILTER)
- Fixed distance attenuation curve