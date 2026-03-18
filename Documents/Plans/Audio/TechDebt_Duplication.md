# Tech Debt: Duplication

Source: /external-tech-debt on Engine/Source/Audio

## Changes

### Engine/Source/Audio/StaticVoice.cpp, Engine/Source/Audio/StreamingVoice.cpp
- Extract a shared voice cleanup helper to eliminate the 3x duplicated Stop/Flush/DestroyVoice pattern. The identical sequence (Stop(0, XAUDIO2_COMMIT_NOW) → FlushSourceBuffers() → DestroyVoice()) appears at StaticVoice.cpp:72-79, StaticVoice.cpp:101-109, and StreamingVoice.cpp:51-59. A free function like `DestroyXAudio2SourceVoice(IXAudio2SourceVoice*& rpVoice)` in AudioManager.h would centralize this [~15m]

### Engine/Source/Audio/AudioManager.cpp
- Extract a music stream creation helper to deduplicate the voice-allocation + StreamingVoice construction sequence. The identical pattern (GetLazyChunkMap().at(crc) → AllocateVoice → make_unique<StreamingVoice>) appears at lines 265-272 (PlayMusic) and lines 422-429 (Update transition). A private method like `CreateMusicStream(common::crc_t)` would centralize this [~15m]

## Verification Notes
- All line numbers verified accurate against source (2026-03-18)
- Voice cleanup: All three sites are structurally identical including the mpAudioEngine null check
- Music stream creation: Both sites identical; helper should return unique_ptr<StreamingVoice>
- Mastering voice channel extraction was removed during verification — only 2 occurrences in different contexts where the constructor site feeds surrounding log statements; extracting violates KISS/YAGNI
