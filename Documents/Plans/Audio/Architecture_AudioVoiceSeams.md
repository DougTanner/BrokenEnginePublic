# Architecture: Audio Voice Ownership & Seam Cleanup

## Context
Source: /external-architecture-review + /external-refactor-clean on Engine/Source (recursive). The Audio subsystem is healthy overall (closely matches its CLAUDE.md contracts), but carries one real behavior gap (music never resumes after any voice clear), a split suspend-state representation, shallow-struct/feature-envy seams between the singular/plural voice classes, and several small duplications.

## Design

### Music resume dead-end (the one behavior item)
- `StreamingVoices.cpp:63` — `CheckTrackTransition` is gated on `mpCurrentStream != nullptr`, and `Clear` (device reset `AudioManager.cpp:293`, deferred clear `:269-272`, `Suspend` `:220`) nulls it while `mGetNextTrack` survives but is never consulted again. Static voices self-heal from frame SOA state every frame (`StaticVoices.cpp:253`); music does not — unplugging headphones mid-game silences music until the next menu transition. Let `CheckTrackTransition` fire when `mpCurrentStream == nullptr && mGetNextTrack`, or document music-does-not-resume as intended [~15m]

### Suspend state & access surface
- Two atomics track one suspend fact (`AudioManager.h:59` + `StaticVoices.h:91`, synced only at `AudioManager.cpp:212-213/230-231`; StreamingVoices has neither — a third behavior for the same concept). Single owner [~15m]
- `AudioManager.h:50-55` exposes `mStaticVoices`/`mStreamingVoices`/`mpAudioEngine`/`mRealTime` publicly alongside one-line forwarders; grep shows zero external member accesses — privatize [~5m]

### StaticVoice invariant ownership
- The `kFadingOut` ↔ `miFadeOutCount` invariant is hand-synced at five sites (`StaticVoices.cpp:233-235, :356-357, :438-440, :466-469, :559`; header admits fragility at `StaticVoices.h:96`). Fold `StaticVoice` into `StaticVoices` as a private struct (verified: no external users) or add `Set/ClearFadingOut` helpers owning the counter; rename `mfFadeOutVolume` (it also serves as the fade-*in* ramp, `StaticVoices.cpp:489-492, :341`) [~30m]

### StreamingVoice feature envy
- All slot-state transitions live in `StreamingVoices::DrainConsumedAndSubmitReady` (`StreamingVoices.cpp:174-238` — touches ~10 members of the passed stream, zero of `StreamingVoices`) and the `fillOne` lambda (`:244-264`); move both onto `StreamingVoice`, plural class keeps mutex/worker orchestration [~30m]
- Rename `StreamingVoice::mbFillFailed` (`StreamingVoice.h:78`) — it doubles as the normal EOF signal (`StreamingVoice.cpp:49` vs `:59`); `mbFillDone` or split flags [~5m]

### Mechanical dedup/cleanup
- Extract `CacheMasteringVoiceChannels()` — ctor `AudioManager.cpp:143-155` vs device-reset re-cache `:280-289` duplicate the `GetMasterVoice`/`GetVoiceDetails`/`min(nChannels)` core and have already drifted (reset path null-guards, ctor doesn't) [~15m]
- Decompose the ~160-line `AudioManager` ctor (`AudioManager.cpp:12-171`) — extract `SelectAudioEndpoint(...)`, folding the duplicated `GetId`/`ScopedLambda`/`CoTaskMemFree` blocks (:35-49 vs :108-123) and the two `AudioEngine` creation sites (:94, :118) [~30m]
- Delete stale `[[maybe_unused]]` on used `rFrame` params (`StaticVoices.cpp:20,78` — both bodies read it unconditionally) [~5m]
- Drop the genuinely unused `rFrame` param from `UpdateListenerPosition` (`StaticVoices.cpp:496`, `StaticVoices.h:47`; sole caller `AudioManager.cpp:310`) [~5m]
- Extract `AcquireOrLoadVoice(crc)` — the acquire-with-load-fallback block is duplicated in `PriorityPass` (`StaticVoices.cpp:332-339` vs `:387-394`) [~15m]

## Critical files
- `Engine/Source/Audio/AudioManager.{h,cpp}`, `StaticVoices.{h,cpp}`, `StaticVoice.{h,cpp}`, `StreamingVoices.{h,cpp}`, `StreamingVoice.{h,cpp}`
- `Engine/Source/Audio/CLAUDE.md` (suspend/resume + voice-ownership contract updates)

## Out of scope
- `PriorityPass` full decomposition (~150 lines — coherent and heavily commented; optional, not filed)
- The O(slots × voices) inner scan (cheap in practice; profile first if ever)
- DirectXTK/XAudio2 layer changes; audio asset pipeline

## Notes
- Invariant exposure: none — client-only subsystem (`BT_CLIENT`), no determinism/CRC/wire. The voice classes have documented thread contracts (strict Wait/Wake alternation, `OnBufferEnd` deadlock avoidance) — the ownership moves are code motion that must not change lock scopes
- Grill decision: music resume — fix (recommended) vs document-as-intended; StaticVoice — private-struct fold vs invariant helpers (recommend helpers: smaller diff)
