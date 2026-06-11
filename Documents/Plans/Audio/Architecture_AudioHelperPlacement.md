# Architecture: Audio Helper Placement (DestroyXAudio2SourceVoice / VolumeToPower)

## Context

Source: /external-architecture-review on `Engine/Source/Audio`. Two shared helpers live in the wrong headers, creating the area's only structural defects:

1. `DestroyXAudio2SourceVoice` is an inline free function in the **top-level** header (`AudioManager.h:67-85`) consumed by the **bottom-level** files (`StaticVoices.cpp:139,533`, `~StreamingVoice` at `StreamingVoice.cpp:23`). This is a semantic cycle (AudioManager → voices → AudioManager) invisible at the include level only because the upward edges ride the PCH — and it blocks the voice .cpps from ever declaring their dependency directly. It also reaches through the `gpAudioManager` global, creating a destruction-order brittleness: `~AudioManager` nulls `gpAudioManager` at the end of its body (`AudioManager.cpp:165`) *before* members destruct; the voice destructors are safe today only because `Clear(false)` (`AudioManager.cpp:157-158`) already emptied every voice list. Any future voice surviving to member destruction would deref a null `gpAudioManager`.
2. `VolumeToPower` is defined in `StaticVoice.h:8-13` but consumed by `StreamingVoice.cpp:91` — a streaming-voice file silently depending on the static-voice header via `Engine.h:67-68` ordering.

## Design

### New file: Engine/Source/Audio/AudioUtility.h
- Fully `#if defined(BT_CLIENT)`-wrapped header holding both helpers. [~20m]
  - Move `VolumeToPower` here unchanged (from `StaticVoice.h:8-13`).
  - Move `DestroyXAudio2SourceVoice` here, re-signatured to take the engine explicitly: `inline void DestroyXAudio2SourceVoice(AudioEngine* pAudioEngine, IXAudio2SourceVoice*& rpVoice)`. Body identical except `gpAudioManager->mpAudioEngine` becomes the `pAudioEngine` parameter (the `nullptr` check transfers to the parameter; the >100ms teardown warning LOG stays). This removes the global back-reference and the destruction-order brittleness.
  - `AudioEngine`/`IXAudio2SourceVoice` complete types arrive via the PCH (`ExternalHeaders.h:174` DirectXTK `Audio.h`), same as the current `AudioManager.h` inline relies on.

### Engine/Source/Audio/StaticVoices.cpp
- `ClearPool` (line 139) and `Clear` (line 533) pass the existing `mpAudioEngine` member (set in `StaticVoices::Init`). Add `#include "AudioUtility.h"` (also covers the `VolumeToPower` uses at lines 66, 614). [~10m]

### Engine/Source/Audio/StreamingVoice.h / .cpp
- `StreamingVoice` holds no engine pointer today. Add `AudioEngine* mpAudioEngine` (passed through the constructor — `StreamingVoices::CreateStream` at `StreamingVoices.cpp:271-274` has it on hand); `~StreamingVoice` passes it to the relocated helper. Add `#include "AudioUtility.h"` to the .cpp (also covers `VolumeToPower` at line 91, removing the cross-sibling dependency on `StaticVoice.h`). [~15m]

### Engine/Source/Audio/AudioManager.h
- Delete the inline `DestroyXAudio2SourceVoice` (lines 67-85). `gpAudioManager` stays where it is. [~5m]

### Engine/Source/Audio/StaticVoice.h / .cpp
- Remove `VolumeToPower` from the header; add `#include "AudioUtility.h"` to `StaticVoice.cpp` (use at line 70). [~5m]

### Engine/Source/Engine.h
- Add `AudioUtility.h` at the top of the Audio block (before `StaticVoice.h`, `Engine.h:67`); extend the load-bearing order comment at `Engine.h:66`. [~5m]

### Build wiring
- Add `AudioUtility.h` to the **client** vcxproj/filters only (file is fully `BT_CLIENT`-wrapped, per the VisualStudio2026 CLAUDE.md rule), alongside the existing Audio headers. [~5m]

## Critical files

- `Engine/Source/Audio/AudioUtility.h` (new)
- `Engine/Source/Audio/AudioManager.h`
- `Engine/Source/Audio/StaticVoice.h`, `StaticVoice.cpp`
- `Engine/Source/Audio/StaticVoices.cpp`
- `Engine/Source/Audio/StreamingVoice.h`, `StreamingVoice.cpp`
- `Engine/Source/Engine.h`
- Client `.vcxproj` / `.vcxproj.filters`

## Out of scope

- Making `AudioManager`'s public `mStaticVoices`/`mStreamingVoices`/`mpAudioEngine` members private (facade enforcement) — no current abuser; separate concern, not filed.
- Any change to teardown *behavior* (two teardown modes, `Clear(bool)` semantics) — this is a pure relocation/parameterization; the null-engine check semantics are preserved (subsystem `mpAudioEngine` mirrors the manager's `unique_ptr`, which outlives all subsystem use).
- The remaining missing direct includes in the area — `Audio/Architecture_IncludeHygiene.md`.

## Acceptance criteria

- Client builds clean; `AudioManager.h` no longer defines any free function; no `gpAudioManager` read remains inside voice teardown paths.

## Notes

- No determinism/CRC/network/`kiVersion` exposure. Client-only.
- Co-schedule with `Architecture_IncludeHygiene.md` (same include blocks; see Order.md File Groups). Land this one first or together — it relocates symbols the hygiene plan would otherwise cite.

## Verification Notes

All items verified against source (2026-06-10):

- `DestroyXAudio2SourceVoice` inline free function confirmed at `AudioManager.h:67-85`, reading `gpAudioManager->mpAudioEngine`; consumers confirmed at `StaticVoices.cpp:139` (`ClearPool`), `:533` (`Clear`), `StreamingVoice.cpp:23` (`~StreamingVoice`).
- Destruction-order brittleness confirmed: `~AudioManager` nulls `gpAudioManager` at `AudioManager.cpp:165` inside the dtor body; `mStaticVoices`/`mStreamingVoices` destruct after the body. Safe today only because `Clear(false)` at `:157-158` empties everything; `~StreamingVoice` would deref null `gpAudioManager` if a stream survived (`~StaticVoice` only ASSERTs `mpVoice == nullptr`, so the live exposure is the streaming side).
- `VolumeToPower` confirmed at `StaticVoice.h:8-13`; consumers at `StreamingVoice.cpp:91`, `StaticVoices.cpp:66`/`:614`, `StaticVoice.cpp:70`. `StreamingVoice` confirmed to hold no `AudioEngine*`; `StreamingVoices::CreateStream` (`:267-280`) has `mpAudioEngine` in scope for the ctor pass-through. DirectXTK `Audio.h` confirmed at `ExternalHeaders.h:174` (client-gated).
- Null-engine semantics preserved: `StaticVoices::Init`/`StreamingVoices::Init` run only when the manager created an engine (`AudioManager.cpp:134-135`), and no voice can exist without an engine, so the parameter-null check is equivalent to the current global check.
- Caveats: (1) `Engine/SingletonPublishGlobalGuardSweep.md` also edits `AudioManager.cpp`'s ctor/dtor publish sites (`:17`/`:165`) — different lines, no semantic conflict, but co-schedule or refresh lines. (2) `StreamingVoice` is move-deleted and held by `unique_ptr`, so adding the `AudioEngine*` member is mechanical — no move-assignment update needed (unlike `StaticVoice`).
