# Architecture: Audio Include Hygiene

## Context

Source: /external-architecture-review on `Engine/Source/Audio`. The area has two include philosophies coexisting: headers are uniformly bare (PCH-reliant), while .cpps are inconsistent — `StreamingVoices.cpp` includes `File/FileManager.h` directly but `StaticVoice.cpp`/`StreamingVoice.cpp` consume the same FileManager symbols silently via the PCH. One include is outright unused, and `Engine.h`'s Audio block (`Engine.h:67-71`) is not self-contained: `sound_t` in `StaticVoice.h` resolves only because the game's `Frame/Frame.h` precedes `Engine.h` in `Pch.h` — an invisible cross-layer ordering dependency no comment documents.

## Design

### Engine/Source/Audio/AudioManager.cpp
- Remove the unused `#include "Game.h"` (line 7). No symbol declared in `Game.h` is referenced; `game::Frame` appears only as opaque pass-through pointer/reference parameters in `AudioManager::Update`/`PlayOneShot`/`PlayOneShot3d`, already covered by the forward declaration at `AudioManager.h:8-13`. [~5m]

### Engine/Source/Audio/StaticVoices.h
- Add `#include "StaticVoice.h"`. The `std::vector<StaticVoice> mVoices` member (line 80) requires the complete `StaticVoice` type; today it compiles only because `Engine.h:67` precedes `Engine.h:69` (the load-bearing order comment at `Engine.h:66` exists to protect this). A direct include makes the dependency explicit and `StaticVoices.cpp` inherits it through its own header. [~5m]

### Engine/Source/Audio/StreamingVoices.cpp
- Add `#include "StreamingVoice.h"`. The TU uses the complete `StreamingVoice` type (`make_unique` at line 274, member access throughout `DrainConsumedAndSubmitReady`/`FillReadyBuffers`), `kfCrossfadeDuration`, `SlotState`, and `kiBufferCount` — all from `StreamingVoice.h`, currently riding `Engine.h` ordering. (Keep the forward declaration in `StreamingVoices.h`; the header itself only needs `unique_ptr<StreamingVoice>` with an out-of-line destructor.) [~5m]

### Engine/Source/Audio/StaticVoice.cpp and Engine/Source/Audio/StreamingVoice.cpp
- Add `#include "File/FileManager.h"` to both. `StaticVoice::LoadXAudio2SourceVoice` uses `gpFileManager`, `LoadPriority::kHigh`, and the complete `LazyChunk` (lines 20-44); `StreamingVoice::FillSlot`/`GetRemainingTime` use `gpFileManager->ReadChunkData` and `LazyChunk` members (lines 16-67). Matches the existing precedent in `StreamingVoices.cpp:5`. [~5m]

### Engine/Source/Audio/StaticVoice.h
- Add the direct include for `sound_t` (`Engine/Source/Frame/Collections/Sounds/Sounds.h`), used by the `StaticVoice` constructor parameter and `mId` member (lines 29, 40). This removes the cross-layer PCH-ordering dependency: nothing in `Engine.h` includes `Sounds.h`/`FrameBase.h` — the Audio block currently compiles only because the game's `Frame/Frame.h` precedes `Engine.h` in `Pch.h:96-97`. Verify `Sounds.h` is includable at this point (it is a shared client/server collections header with no audio dependency); if it is itself PCH-reliant the include is documentary but harmless. [~10m]

## Critical files

- `Engine/Source/Audio/AudioManager.cpp`
- `Engine/Source/Audio/StaticVoices.h`
- `Engine/Source/Audio/StreamingVoices.cpp`
- `Engine/Source/Audio/StaticVoice.h`
- `Engine/Source/Audio/StaticVoice.cpp`
- `Engine/Source/Audio/StreamingVoice.cpp`

## Out of scope

- The three stale `#include "Memory/MemoryManager.h"` lines (`AudioManager.cpp:5`, `StaticVoices.cpp:5`, `StreamingVoices.cpp:6`) — already covered by `Engine/DeadCodeAndUnusedIncludesSweep.md` ("~12 Engine TUs", enumerated at execution).
- `StaticVoices.cpp`'s `#include "Game.h"` — kept, no action. (Verification correction: `game::gpCamera` is declared in the game `Graphics/Camera.h` and the complete `game::Frame` comes from the game `Frame/Frame.h` — both resolve via the PCH, not via `Game.h`'s direct include list (`ClientSettings.h`/`Fleet.h`/session headers), so `Game.h` may itself be removable from this TU; that removal is out of scope here.)
- Direct includes for `DestroyXAudio2SourceVoice` consumers — adding `AudioManager.h` to the voice .cpps would create a real header cycle; the structural fix is `Audio/Architecture_AudioHelperPlacement.md` (relocate the helper), after which those includes land there.
- Engine-wide PCH reliance on the game `Frame/Frame.h` for `game::Frame` member access — by design (sanctioned engine→game read direction); not an Audio-local problem.

## Acceptance criteria

- Client builds clean with no behavior change (include-only edit; compile-checked).

## Notes

- No determinism/CRC/network/`kiVersion` exposure. Client-only files.
- Co-schedule with `Architecture_AudioHelperPlacement.md` — both edit the same include blocks (see Order.md File Groups).

## Verification Notes

All five design items verified against source (2026-06-10):

- `AudioManager.cpp:7` `Game.h` confirmed unused — no `game::` symbol referenced anywhere in the TU; `game::Frame` appears only as opaque pass-through pointer/reference (forward decl at `AudioManager.h:8-13` covers it).
- `StaticVoices.h:80` `std::vector<StaticVoice> mVoices` with no `StaticVoice.h` include confirmed; `Engine.h:66-71` ordering and its load-bearing comment confirmed.
- `StreamingVoices.cpp` confirmed using complete `StreamingVoice` (`make_unique` at `:274`, `kfCrossfadeDuration` at `:63`, `SlotState`/`kiBufferCount` in `DrainConsumedAndSubmitReady`/`FillReadyBuffers`) with no `StreamingVoice.h` include.
- `StaticVoice.cpp:20-44` (`gpFileManager`, `LoadPriority::kHigh`, complete `LazyChunk`) and `StreamingVoice.cpp:16-67` (`ReadChunkData`, `LazyChunk` members) confirmed without `File/FileManager.h`; precedent at `StreamingVoices.cpp:5` confirmed.
- `sound_t` confirmed at `Engine/Source/Frame/Collections/Sounds/Sounds.h:69` (`SoundsInterpolate::id_t` alias); `Sounds.h` includes only `Collection.h` + `GridCoord.h` (no Audio dependency — no include cycle) and is `BT_CLIENT`-wrapped like `StaticVoice.h`. `Pch.h:96-97` ordering (game `Frame/Frame.h` before `Engine.h`) confirmed; nothing in `Engine.h`'s include list reaches `Sounds.h`.
- MemoryManager.h exclusion accurate: all three cited includes exist (`AudioManager.cpp:5`, `StaticVoices.cpp:5`, `StreamingVoices.cpp:6`) and `Engine/DeadCodeAndUnusedIncludesSweep.md` item 1 owns them. No other queue overlap found.
- Caveat: the Out-of-scope `Game.h` rationale was corrected during verification — `Game.h` in `StaticVoices.cpp` is likely also removable (symbols ride the PCH), left out of scope.
