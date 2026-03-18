# Architecture: Update() Function Split

Source: /external-architecture-review on Engine/Source/Audio

## Changes

### Engine/Source/Audio/AudioManager.h
- Add private method declarations for the extracted subfunctions [~5m]:
  - `void UpdateStaticVoiceLifecycle(const game::Frame& rFrame, float fDeltaTime)` — fade-out, removal, new voice creation, position sync
  - `void UpdateListenerPosition(const game::Frame& rFrame)` — human player position/velocity → X3DAUDIO_LISTENER

### Engine/Source/Audio/AudioManager.cpp
- Extract static voice fade-out and swap-and-pop removal (lines 443-485) plus new voice creation and sync (lines 487-535) into `UpdateStaticVoiceLifecycle()` [~10m]
- Extract listener position update from human player (lines 537-567) into `UpdateListenerPosition()` [~10m]
- The remaining `Update()` body becomes: clear voices check, device reset, music transition check, UpdateMusicStreams, UpdateStaticVoiceLifecycle, UpdateListenerPosition, 3D volume loop, profiling — approximately 40-50 lines [~5m]

## Verification Notes
- All line numbers verified accurate against source (2026-03-18)
- UpdateStaticVoiceLifecycle needs access to rSoundsInterpolate and rSoundsPostRender (lines 439-440); compute these inside the extracted method from rFrame
- fDeltaTime is already in the proposed signature
- The 3D volume loop (lines 570-573) and profiling (line 575) remain in Update()
- Update() takes const game::Frame* (nullable); extracted methods take const game::Frame& since they're called inside the null check
