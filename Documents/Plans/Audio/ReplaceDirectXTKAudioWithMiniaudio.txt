Replace DirectXTK Audio with miniaudio
=======================================

Context
-------
The engine uses DirectXTK's AudioEngine (XAudio2 wrapper) with X3DAudio for 3D spatial audio, music streaming, and sound effects. This ties audio to Windows/XAudio2. miniaudio (Public Domain / MIT-0) is a single-header cross-platform audio library with 3D spatialization support.

Why
---
- Cross-platform: Windows, Linux, macOS, iOS, Android
- Best possible license (public domain or MIT-0, no attribution required)
- Single header -- minimal build complexity
- Active development (v0.11.25, March 2026)
- Adequate 3D audio (distance attenuation, Doppler, directional emitters)

Tradeoffs
---------
- No HRTF (planned but not yet available) -- current X3DAudio also lacks HRTF
- No environmental reverb/occlusion -- current system doesn't use EFX either
- High-level ma_engine API differs from XAudio2 source voice model
- Alternative: OpenAL Soft has better 3D audio (HRTF, EFX) but LGPL-2 requires DLL shipping

Precondition
------------
Only worthwhile if cross-platform becomes a goal. Current Windows-only XAudio2 works well.

Changes
-------

1. ThirdParty/
   - Add miniaudio.h (single header)

2. Engine vcxproj (client only)
   - Add miniaudio source, remove DirectXTK audio references

3. Engine/Source/Audio/AudioManager.h/.cpp
   - Replace DirectX::AudioEngine with ma_engine
   - Replace X3DAUDIO_LISTENER with ma_engine listener positioning
   - Map voice management to ma_sound objects
   - Map 3D parameters: distance model, Doppler, attenuation

4. Engine/Source/Audio/StaticVoice.h/.cpp
   - Replace DirectX::SoundEffectInstance with ma_sound
   - Map 3D positioning from frame data to ma_sound_set_position()

5. Engine/Source/Audio/StreamingVoice.h/.cpp
   - Replace XAudio2 buffer queuing with miniaudio's data source streaming
   - Or use low-level ma_decoder for manual buffer control matching triple-buffer pattern

6. Common/ExternalHeaders.h
   - Remove DirectXTK audio includes, add miniaudio

Notes
-----
- High effort -- complete rewrite of AudioManager, StaticVoice, StreamingVoice
- DirectXTK would still be needed for input (GamePad/Mouse) unless input is also replaced
- Test spatial audio quality carefully -- miniaudio's spatialization is simpler than X3DAudio
