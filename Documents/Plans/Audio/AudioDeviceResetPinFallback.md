# Fallback When the Pinned 48 kHz Mastering Format Fails on Device Reset

## Context

The mastering voice is pinned to 48 kHz: `AudioManager` stores `mPinnedOutputFormat` (startup device's native channel count + 48000 Hz) and the device-lost path in `Update` calls `AudioEngine::Reset(&mPinnedOutputFormat, nullptr)` so the pin survives reset (previously `Reset()` would have silently reverted to the new device's default rate).

Adversarial review found a reachable failure this pin introduces: `Reset(..., nullptr)` targets the **new default device**, but the pinned `nChannels` came from the **startup** device. On a hot-swap to a device with fewer channels (e.g. 7.1 → stereo), `CreateMasteringVoice(8ch, ...)` fails; the `Reset` return value is ignored (pre-existing), leaving the engine dead until the next `IsAudioDevicePresent()` retry loop — a graceful silent-retry, no crash, but audio stays off where the pre-pin code would have recovered. Pre-change, the reset passed device-default channels (0), which always matched.

## Design

In the `AudioManager::Update` device-reset path:

1. Capture the `bool` result of `Reset(&mPinnedOutputFormat, nullptr)`.
2. On failure, fall back to `Reset(nullptr, nullptr)` (device-default format — audio returns at the new device's native rate/channels; only the SRC bypass is lost). Log one `kWarning` naming the fallback.
3. On either successful path, the existing post-reset re-cache of `miMasteringVoiceChannels` (reads the actual voice) keeps the 3D mix self-consistent.
4. ~~Optional re-pin rider~~ — **dropped by decision, do not implement** (see Notes). `mPinnedOutputFormat` stays immutable after the constructor; after a fallback, audio runs at device-default format until the next app launch re-pins.

## Critical files

- `Engine/Source/Audio/AudioManager.cpp` — the `Reset(&mPinnedOutputFormat, nullptr)` call in `Update`'s device-lost handling; the constructor migration path is fine (it targets the bound endpoint, channels always match) and is not touched.

## Out of scope

- The constructor construct-then-migrate sequence (verified correct).
- Re-pinning mid-session on hot-swap beyond the optional item above; suspend/resume paths.
- Any DataPacker/audio-format change.

## Acceptance criteria

- Simulated failure (e.g. temporarily corrupt the pinned format under debugger) recovers audio via the fallback `Reset` instead of staying silent.

## Notes

- Client-only (`Audio/` is client-only); no CRC/determinism/wire/pack exposure.
- Shares `AudioManager.cpp` with `Audio/Architecture_AudioVoiceSeams.md` (ctor decompose + mastering-channel cache dedup) — co-schedule or refresh citations.
- Decision (2026-07-03): **minimal fallback only — no re-pin rider (Design item 4 dropped).** YAGNI: the rider only pays off after a channel-count-changing hot-swap followed by a *second* device reset in the same session, and its payoff is merely restoring the SRC-bypass micro-optimization; meanwhile it makes `mPinnedOutputFormat` mutable mid-session, complicating the invariant that the pin is the startup capture (`AudioManager.cpp:143-149`). Losing the bypass until next launch is a negligible cost; audio itself is restored by the fallback `Reset(nullptr, nullptr)`.
