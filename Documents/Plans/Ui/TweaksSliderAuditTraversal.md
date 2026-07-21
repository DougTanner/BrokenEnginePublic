# Tweaks Slider Audit Traversal

## Context

The `kbDebugInput` first-open audit is intended to detect drift between `TweaksSliderMap` registrations and `WrapperSlider` call sites. Runtime acceptance during the originating graphics quick-wins execution proved that its two new registrations and calls are coherent: `Spread Decay End` and `Spread Accumulation Decay End` were both visible, addressable, and set/restored through the agent harness. On the same first F3 open, the captured harness log emitted 508 `TweaksSliderMap: orphan key` warnings, including those two functional keys, and emitted no `missed key` warnings.

`TweaksScreenBase::RunSliderAuditFrame` in `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp:378-443` drives every section through a synthetic offscreen 1x1 ImGui window, forces one subtab index per frame, and relies on the section render functions reaching `WrapperSlider`. `TweaksScreenBase::WrapperSlider` records a key in `mAuditTouched` only when the synthetic render reaches that call (`TweaksScreenBase.cpp:115-138`); completion then labels every untouched registration as orphaned (`TweaksScreenBase.cpp:425-439`). The observed warning set proves that the synthetic render does not reliably traverse existing functional slider calls, leaving `mAuditTouched` incomplete and producing false-positive orphan warnings across the global registry.

This logic is pre-existing: `TweaksScreenBase.cpp` is byte-identical to baseline commit `650724598eee5c0a7133d0c0eeb7c438fa3847ee`. The originating graphics quick-wins change only exposed the false positive while adding otherwise-correct registry entries and call sites, so correcting the audit is an independent UI bugfix.

**Root cause confirmed (2026-07-21, runtime evidence — the Design's "confirm the exact ImGui gating before editing" step is satisfied).** `RunSliderAuditFrame` calls `ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f)` *before* `ImGui::Begin("##slider-audit", nullptr, kAuditFlags)`. ImGui sets `window->HiddenFramesCanSkipItems = 1` when `style.Alpha <= 0.0f` at `Begin` time (`ThirdParty/imgui/imgui.cpp:8111-8113`); that feeds `hidden_regular` → `skip_items = true` (`:8128-8131`), and `Begin` returns `!window->SkipItems` (`:8151`). The `AutoFitFrames` escape does not apply because `SetNextWindowSize` was called. So `Begin` returns **false on every audit frame**, the section-render loop inside the `if` body never executes, no `WrapperSlider` call ever reaches the audit branch, and both `mAuditTouched` and `mAuditMissed` stay empty — every registered key is then reported as an orphan. Measured signature: **513 orphan keys, 0 missed keys, against exactly 513 registrar entries**, reproduced across four launches, with the window confirmed non-minimized and producing live frames (stale-frame replay was falsified by injecting mouse moves and reading them back; the `##slider-audit` window itself was observed in `describe_ui`, which filters on `WasActive`). The earlier 508/0 reading is the same signature at a smaller registry. Likely fix: move the `PushStyleVar` after `Begin`, or drop it entirely and rely on the offscreen position plus `ImGuiWindowFlags_NoBackground`.

Also corrected: the audit is **not** one-shot per process. `Graphics::Destroy()` at `>= DestroyType::kSwapchain` resets `mpImGuiManager` (`Engine/Source/Graphics/Graphics.cpp:756`) and `Create()` rebuilds it (`:451`), constructing a fresh `TweaksScreen` with `miAuditFrame = 0`. Any swapchain-tier recreate — window restore, resize, a graphics settings change — re-arms it. Plan text and `Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md` both describe it as "first-open"; that wording is inaccurate and should be corrected by whichever change fixes the traversal.

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
- The game-extensible `TweakSection` section-registry architecture, which has since landed as a separate completed change. Sections are now registered at startup and dispatched through `TweakSectionDesc::pfnRender`; do not redesign that mechanism here.
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
