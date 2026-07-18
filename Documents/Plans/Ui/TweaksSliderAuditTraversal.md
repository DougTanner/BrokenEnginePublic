# Tweaks Slider Audit Traversal

## Context

The `kbDebugInput` first-open audit is intended to detect drift between `TweaksSliderMap` registrations and `WrapperSlider` call sites. Runtime acceptance during the originating graphics quick-wins execution proved that its two new registrations and calls are coherent: `Spread Decay End` and `Spread Accumulation Decay End` were both visible, addressable, and set/restored through the agent harness. On the same first F3 open, the captured harness log emitted 508 `TweaksSliderMap: orphan key` warnings, including those two functional keys, and emitted no `missed key` warnings.

`TweaksScreenBase::RunSliderAuditFrame` in `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp:378-443` drives every section through a synthetic offscreen 1x1 ImGui window, forces one subtab index per frame, and relies on the section render functions reaching `WrapperSlider`. `TweaksScreenBase::WrapperSlider` records a key in `mAuditTouched` only when the synthetic render reaches that call (`TweaksScreenBase.cpp:115-138`); completion then labels every untouched registration as orphaned (`TweaksScreenBase.cpp:425-439`). The observed warning set proves that the synthetic render does not reliably traverse existing functional slider calls, leaving `mAuditTouched` incomplete and producing false-positive orphan warnings across the global registry.

This logic is pre-existing: `TweaksScreenBase.cpp` is byte-identical to baseline commit `650724598eee5c0a7133d0c0eeb7c438fa3847ee`. The originating graphics quick-wins change only exposed the false positive while adding otherwise-correct registry entries and call sites, so correcting the audit is an independent UI bugfix.

## Design

- Correct the first-open audit traversal so a coherent, unchanged `TweaksSliderMap` and its reachable `WrapperSlider` call sites produce no false-positive orphan or missed warnings.
- Confirm the exact ImGui gating that prevents the current synthetic pass from reaching registered slider calls before editing. Keep the fix within the existing audit path and use the smallest mechanism that exercises the real section/subtab call sites without changing normal TweaksScreen rendering or interaction.
- Preserve the existing audit contract: map keys remain globally unique, a call-site lookup absent from the map remains a missed key, and a registration with no reachable call site remains an orphan key.
- Preserve the one-time debug-only behavior and scoped allocation-tracking suppression around audit-owned STL work.

## Critical files

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` — `TweaksScreenBase::RunSliderAuditFrame` synthetic traversal and `TweaksScreenBase::WrapperSlider` touch/miss recording.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` — audit state (`miAuditFrame`, `mbAuditMode`, `mAuditTouched`, `mAuditMissed`) if the verified correction requires state-shape changes.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen*.cpp` and game-side TweaksScreen section implementations — read-only registry/call-site coverage references unless root-cause inspection proves a specific audit-only integration change is required.

## Out of scope

- Adding, removing, renaming, or retuning wrappers, slider-map entries, or visible controls.
- Changing normal TweaksScreen layout, section/subtab behavior, persistence, or interaction.
- The game-extensible `TweakSection` architecture owned by `Ui/Architecture_TweakSectionGameExtension.md`.
- General registry redesign, new audit infrastructure, or unrelated ImGui cleanup.
- Unit tests.

## Acceptance criteria

- Opening TweaksScreen for the first time through the agent harness completes the audit with no `TweaksSliderMap: orphan key` or `TweaksSliderMap: missed key` warnings for the coherent unchanged registry.
- Harness interaction confirms representative existing sliders, including disambiguated-label/map-key sliders such as `Spread Decay End` and `Spread Accumulation Decay End`, remain visible, addressable, and settable after the fix.
- The affected Debug client target compiles and the harness log contains no new audit, UI, or allocation-tracking errors.
- Static inspection confirms the audit still reports both mismatch classes when their conditions are present; no intentionally mismatched fixture or temporary source mutation is required.

## Notes

- Risk tier trigger: Tier 2 scoped behavior in the client-only debug TweaksScreen audit. This does not touch deterministic simulation/CRC state, replay/save compatibility, wire protocol, `kiVersion`, `.pack` layout, shaders, or client/server guard scope.
- The audit performs one-time STL work under allocation tracking; preserve the existing `ScopedSuppressAllocationTracking` boundaries or provide equivalent justification for any changed allocation path.
- Runtime verification requires `/agent-harness`; first-open behavior means each decisive run must start from a fresh client process or otherwise reset the audit lifecycle through an existing supported path.
