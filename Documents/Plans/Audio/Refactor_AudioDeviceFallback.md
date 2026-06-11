# Refactor: AudioManager Device-Enumeration Fallback Robustness

## Context

Source: /external-refactor-clean on `Engine/Source/Audio`. `Audio/CLAUDE.md` documents the constructor as "enumerates render endpoints to bind the OS default (**falls back to first active**)", but the code only reaches the first-active fallback (`AudioManager.cpp:84-101`) when the default-endpoint lookup fully succeeds: the early-returns at `AudioManager.cpp:30-33` (`GetDefaultAudioEndpoint` failed — **no log at all**), `:38-42` (`GetId` returned null), and `:52-56` (`EnumAudioEndpoints` returned null) abandon construction entirely, leaving `mpAudioEngine` null — silent no-audio on a machine that may have active devices.

Compounding it: the failure logs that do exist are `LOG(kAudio, kDebug, ...)`, and `kAudio` is compile-time thresholded at `kWarning` in the game `Pch.h` — so every failure path in this constructor is compiled out of default builds. A no-audio report is undiagnosable from logs.

OS API results are a trust boundary — validation/fallback here is policy-compliant (root CLAUDE.md "Error handling at trust boundaries only").

## Design

### Engine/Source/Audio/AudioManager.cpp — AudioManager::AudioManager()
- Restructure the endpoint discovery so the fallback always runs: make the default-endpoint id lookup best-effort (empty id on any failure), always `EnumAudioEndpoints` (its own failure → log + return, nothing else possible), match the default id when known, and enter the existing first-active fallback block whenever `mpAudioEngine` is still null and `uiCount > 0`. The fallback block itself (`:88-101`) is reused as-is. [~30m]
- Promote the constructor's failure-path logs from `kDebug` to `kWarning` (the compile-time `kAudio` threshold) and add one to the currently-silent `GetDefaultAudioEndpoint` failure return — so "constructed without an audio device" is always visible in logs. Success-path logs stay at their current levels. [~10m]

## Critical files

- `Engine/Source/Audio/AudioManager.cpp`

## Out of scope

- Hot-plug / late device arrival — the existing device-reset path in `Update` (`AudioManager.cpp:248-268`) already owns runtime recovery; this plan is constructor-only.
- The `kAudio` compile-time threshold itself (`Pch.h`) — unchanged; the fix is using the right level, not moving the threshold.
- The exception handlers at `:138-147` — already log at `kError`; untouched.

## Acceptance criteria

- With a failing `GetDefaultAudioEndpoint` (or null `GetId`) but active render devices present, the constructor still creates `mpAudioEngine` from the first active device.
- Every constructor path that ends with `mpAudioEngine == nullptr` emits at least one `kWarning`-or-higher log.

## Notes

- Invariant exposure: none — startup-only, client-only, no determinism/CRC/network exposure. Hard to playtest the failure paths (needs a device-less VM or forced failure); code review + normal-boot regression is the practical bar.

## Verification Notes

All items verified against source (2026-06-10):

- Early-return paths confirmed: `AudioManager.cpp:30-33` (`GetDefaultAudioEndpoint != S_OK`, no log of any kind), `:38-42` (`GetId` null → `kDebug`), `:52-56` (`EnumAudioEndpoints` null collection → `kDebug`) all abandon construction before the first-active fallback at `:84-101`, leaving `mpAudioEngine` null. `Audio/CLAUDE.md`'s "falls back to first active" claim only holds when the default lookup fully succeeds — discrepancy confirmed.
- Compile-out confirmed: `keLogLevelAudio = keLogLevelDefault = kWarning` at game `Pch.h:84-85`, so the constructor's `kDebug` failure logs are compiled out of default builds.
- Scope caveat: `CHECK_HRESULT`-wrapped failures (negative HRESULTs from `CoCreateInstance`/`GetId`/`EnumAudioEndpoints`/`GetCount`/`Item`) throw and land in the `:138-147` handlers, which already log `kError` — so the "silent/undiagnosable" claim applies specifically to the three soft-failure paths cited above (non-S_OK default lookup, null out-params), which is exactly what the design addresses. The restructure should keep the `CHECK_HRESULT` throw-path behavior unchanged.
- Out-of-scope claims confirmed: runtime device-reset recovery lives at `AudioManager.cpp:248-268`; the exception handlers already log at `kError`.
