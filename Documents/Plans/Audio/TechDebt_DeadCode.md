# Tech Debt: Dead Code

Source: /external-tech-debt on Engine/Source/Audio

## Changes

### Engine/Source/Audio/AudioManager.h
- Remove unused `LOG_STATIC_VOICES` macro definition (line 5). `LOG_STREAMING_VOICES` is used but `LOG_STATIC_VOICES` has zero callers in the entire codebase [~2m]

### Engine/Source/Audio/StaticVoice.h
- Remove `common::crc_t uiCrc` parameter from StaticVoice constructor declaration (line 21) [~2m]

### Engine/Source/Audio/StaticVoice.cpp
- Remove `[[maybe_unused]] common::crc_t uiCrc` parameter from StaticVoice constructor definition (line 53) [~2m]

### Engine/Source/Audio/AudioManager.cpp
- Update StaticVoice construction call to remove the `uiCrc` argument (line 533: `mStaticVoices.push_back(StaticVoice(pVoice, id, uiCrc, fVolume, fPitch, fFadeOutTime, vecPosition, vecVelocity))`) [~2m]

## Verification Notes
- All line numbers verified accurate against source (2026-03-18)
- LOG_STATIC_VOICES has zero callers codebase-wide; LOG_STREAMING_VOICES (line 6) IS used and must be kept
- uiCrc parameter is [[maybe_unused]] and never stored — no downstream consumers
- Single call site for StaticVoice constructor at AudioManager.cpp:533
