# Device-Id Null-Guard Consistency in AudioManager Constructor

## Context

The `AudioManager` constructor (`Engine/Source/Audio/AudioManager.cpp`) enumerates render endpoints to bind the OS default device, falling back to the first active device. Each candidate device's id is obtained via `IMMDevice::GetId(&pcDeviceId)`, which returns an `LPWSTR` the caller frees with `CoTaskMemFree`.

The constructor has three `GetId` call sites:

- **Default-endpoint path** (`pDefaultAudioEndpoint->GetId`, ~line 36): after a recent change this site null-checks `pcDefaultDeviceId` before constructing `defaultAudioEndpointId` from it, logging a `kWarning` and falling through on null.
- **Active-endpoint match loop** (`pMMDevice->GetId`, ~line 77): constructs `std::wstring audioEndpointId(pcDeviceId);` directly, no null check.
- **First-active fallback block** (`pMMDevice->GetId`, ~line 104): constructs `std::wstring audioEndpointId(pcDeviceId);` directly, no null check.

`GetId` is wrapped in `CHECK_HRESULT`, which throws on a negative HRESULT — so a hard failure is handled. But a returned `S_OK` with a null out-pointer would make `std::wstring(nullptr)` undefined behavior. The two unguarded sites are therefore inconsistent with the now-guarded default-endpoint path.

`GetId` returning `S_OK` with a null pointer is effectively impossible in practice (Core Audio OS API), so this is a latent/defensive consistency fix surfaced during a session-final audit, not an active bug. `GetId`'s out-pointer is opaque to our code (an OS-API result), so a null-guard here is trust-boundary-policy-compliant (validate results opaque to the current code unit).

## Design

Bring the two unguarded sites in line with the default-endpoint path: after the `CHECK_HRESULT(... GetId(&pcDeviceId))` and the `CoTaskMemFree` `ScopedLambda`, null-check `pcDeviceId` before constructing the `std::wstring`.

- **Match loop** (~line 77): on null, log a `kWarning` (consistent with the default path's wording, e.g. "GetId returned nullptr") and `continue` to the next enumerated device rather than constructing from null.
- **First-active fallback** (~line 104): on null, log a `kWarning` and skip the `make_unique<AudioEngine>` for this device (the block is already inside `if (uiCount > 0)`; with a single candidate there is nothing further to try, so the engine simply stays null — the existing `if (mpAudioEngine != nullptr)` configuration block below already handles that case).

Keep the `CoTaskMemFree` `ScopedLambda` placement as-is at each site so the buffer is freed regardless of the null result (matching the default-endpoint path, where the free lambda is registered before the null check).

This is a single-file mechanical change with compile-checked semantics; no signatures, headers, or data layout change.

## Critical files

- `Engine/Source/Audio/AudioManager.cpp` — the `AudioManager::AudioManager()` constructor; specifically the active-endpoint match loop (`std::wstring audioEndpointId(pcDeviceId);` ~line 78) and the first-active fallback block (`std::wstring audioEndpointId(pcDeviceId);` ~line 109). Model the guard on the existing default-endpoint null-check at ~lines 41-49.

## Out of scope

- The default-endpoint `GetId` path — already guarded; do not touch.
- Any change to `CHECK_HRESULT` behavior, the `CoTaskMemFree` `ScopedLambda` pattern, or the `EnumAudioEndpoints` / `GetCount` flow.
- Device-reset, voice-prioritization, fade, or any other `AudioManager` behavior.
- Broadening the guard into a general OS-API-null helper or refactoring the three sites into a shared id-fetch helper — that would be a larger consolidation; this plan only adds the two missing checks for consistency.
- Log-category / threshold changes (the `kAudio` category is `kWarning`-thresholded in game `Pch.h` by design).

## Notes

State-invariant exposure: none. Client-only (`AudioManager.cpp` is fully `#if defined(BT_CLIENT)`), runs once at startup outside the allocation-tracked main loop, and touches no determinism / CRC sim path, `kiVersion`/`.pack` layout, replay, or network surface. No grill decisions: the guard shape is fully determined by the existing default-endpoint path. Quick Win defensive-consistency fix.

Trust-boundary policy: the `GetId` out-pointer is an OS-API result opaque to our code, so null-guarding it is policy-compliant (validate anything opaque to the current code unit; no defensive validation is being added between our own functions).
