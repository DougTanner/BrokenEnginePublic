# /Engine/Source/Audio/

The `/Engine/Source/Audio/` directory contains the 3D spatial audio system implementation.

## Core Files

### AudioManager.h & AudioManager.cpp

3D spatial audio system using DirectXTK's AudioEngine (XAudio2).

**Global Singleton**: `gpAudioManager`

**Key Components**:
- `Voice` - Represents an active sound with 3D position, volume, pitch, and fade properties
- `VoiceFlags` - Tracks voice state (e.g., fading out)
- Implements `DirectX::IVoiceNotify` for music playlist callbacks

**Main Functions**:
- `Update(Frame&)` - Processes frame-based sounds, updates 3D positions, manages music crossfading
- `PlayOneShot(crc, b3d, volume, pitch)` - Fire-and-forget sound playback (2D or 3D)
- `PlayOneShot(crc, position, volume, pitch)` - 3D positioned one-shot sound
- `LoadVoice()` - Internal voice creation from CRC-indexed audio chunks
- `Apply3d()` - Calculates 3D audio effects (distance attenuation, doppler, panning)
- `OnBufferEnd()` - Handles music playlist advancement

**Music System**:
- Separate voices for menu and game music
- Automatic crossfading based on `game::gpGame->mbMainMenuMusic`
- Playlists defined in static vectors (`sMenuMusics`, `sGameMusics`)

**3D Audio Features**:
- Custom distance attenuation with manual fade ranges
- Doppler effect calculation
- Multi-channel output matrix calculation
- Listener position/velocity tracking from player frame data

**Technical Details**:
- ADPCM compressed audio format
- Voice pooling and lifecycle management
- Frame-based sound tracking via unique IDs
- Volume squared for perceptually linear curves