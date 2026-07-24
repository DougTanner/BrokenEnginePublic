<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-18T19:00:04.000Z","dependsOn":[]} -->
# Tweaks Slider Audit Traversal

## Context

The `kbDebugInput` slider-map audit detects drift between the `TweaksSliderMap` slider registrations and the `WrapperSlider` call sites that address them: a registration with no reachable call site is an "orphan key", and a `WrapperSlider` `mapKey` absent from the map is a "missed key". The audit drives every registered section through a synthetic offscreen ImGui pass and, per `WrapperSlider` call reached, records whether its `mapKey` exists in the map.

The audit is broken: on a real open it reports every registered key as an orphan. Captured harness evidence measured **513 orphan keys, 0 missed keys against exactly 513 registrar entries**, reproduced across four launches (an earlier, smaller registry produced the same 508/0 signature). The pass never reaches a single live `WrapperSlider` call, so both `mAuditTouched` and `mAuditMissed` stay empty.

**Root cause (confirmed 2026-07-21, runtime evidence plus code inspection).** `TweaksScreenBase::RunSliderAuditFrame` (`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp:448-513`) calls `ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f)` (`:476`) *before* `ImGui::Begin("##slider-audit", nullptr, kAuditFlags)` (`:478`). ImGui sets `window->HiddenFramesCanSkipItems = 1` whenever `style.Alpha <= 0.0f` at `Begin` time (`ThirdParty/imgui/imgui.cpp:8111-8113`); that feeds `hidden_regular` and forces `skip_items = true` (`imgui.cpp:8128-8131`), so `Begin` returns `!window->SkipItems` (`imgui.cpp:8151`) — false. The `AutoFitFrames` escape does not apply because `SetNextWindowSize` was called (`TweaksScreenBase.cpp:475`). `Begin` therefore returns false on every audit frame, the section-render loop inside the `if` body (`:480-483`) never executes, no `WrapperSlider` call reaches the audit branch (`TweaksScreenBase.cpp:174-191`), and completion (`:495-511`) reports every registered key as an orphan. The window was confirmed non-minimized and producing live frames, and stale-frame replay was falsified (injected mouse moves read back; the `##slider-audit` window itself observed in `describe_ui`, which filters on `WasActive`).

The bug is pre-existing and independent of the graphics quick-wins change that first surfaced it. That change only added otherwise-correct registry entries and call sites — `Spread Decay End` and `Spread Accumulation Decay End`, both proven visible, addressable, and set/restored through the harness — which is why the orphan count grew while the audit itself was already non-functional. Correcting the audit is therefore an independent UI bugfix.

**Lifecycle correction: the audit is not one-shot per process.** `Graphics::Destroy()` at `>= DestroyType::kSwapchain` (`Engine/Source/Graphics/Graphics.cpp:723`) resets `mpImGuiManager` (`Graphics.cpp:762`) and `Graphics::Create()` rebuilds it (`Graphics.cpp:455-457`), constructing a fresh `TweaksScreen` with `miAuditFrame = 0` (`TweaksScreenBase.h:117`). Any swapchain-tier recreate — window restore, resize, a graphics-settings change — re-arms the audit. The existing "first-open" / "one-shot per process" wording in the audit comments and in the TweaksScreen `AGENTS.md` is inaccurate and is corrected as part of this fix.

## Design

Smallest fix, entirely inside `RunSliderAuditFrame`'s synthetic-window setup: make the `##slider-audit` window's `Begin` return true so the section-render loop executes and each reachable `WrapperSlider` call records into the audit.

**Mechanism (decided): delete the alpha push and its pop.** Remove `ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f)` (`:476`) and the matching `ImGui::PopStyleVar()` (`:486`). The pass stays non-visible and non-interactive on the mechanisms already present: the offscreen position (`SetNextWindowPos(-10000, -10000)`, `:474`), the 1x1 size (`:475`), and `ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus` in `kAuditFlags` (`:477`).

Rejected alternative — moving the push to after `Begin` — is not equivalent and is more invasive: `PopStyleVar` currently sits *after* `ImGui::End()` (`:485-486`), and no statement can be placed between the `if (ImGui::Begin(…))` condition and its body, so that variant forces splitting `Begin`'s return into a local and re-balancing the push/pop around the new control flow. It buys nothing the existing flags do not already provide.

Preserve the existing audit contract unchanged: map keys remain globally unique; a `WrapperSlider` `mapKey` absent from the map remains a missed key; a registration with no reachable call site remains an orphan key; the subtab cycling across `kiAuditFrameCount` frames, the `ScopedSuppressAllocationTracking` boundaries around audit-owned STL work, and the completion reporting all stay as-is.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the acceptance criteria, and add no abstractions, configuration, refactors, audit-infrastructure changes, or fixes to adjacent code encountered. Naming a file below grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

**In scope:**

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp`, `RunSliderAuditFrame()` only (`:448-513`): the `PushStyleVar(ImGuiStyleVar_Alpha, 0.0f)` / `Begin("##slider-audit", …)` / `PopStyleVar` ordering at `:476-486`, per Design. Do not change the subtab-cycling loop, `mbAuditMode` gating, allocation suppression, or completion reporting.
- Lifecycle-wording correction only (meaning-preserving), at the three sites asserting the inaccurate "first-open" / "one-shot per process" lifecycle:
  - `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp`, the `WrapperSlider` audit-branch comment (`:178`).
  - `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h`, the audit-state comment (`:112`).
  - `Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md`, the "first-open debug audit" sentence under Registration and Layout Contracts.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h`, audit-state members (`miAuditFrame`, `mbAuditMode`, `mAuditTouched`, `mAuditMissed`, `mPreAuditSubtab`): touch only if the fix requires a state-shape change. The confirmed root cause does not, so no state-shape change is expected beyond the wording correction above.

**Read-only reference (no edits) — coverage and verification only:**

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreen*.cpp` (e.g. `TweaksScreenLighting.cpp`), the game-side section implementations, and the `TweaksSliderMap` registrars.

## Critical files

- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.cpp` — `RunSliderAuditFrame()` (`:448-513`), the sole edit site for the fix; the `WrapperSlider` audit branch (`:174-191`) whose comment (`:178`) carries the lifecycle wording correction.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenBase.h` — audit-state members and the lifecycle comment (`:112`).
- `Engine/Source/Ui/Screens/TweaksScreen/AGENTS.md` — the "first-open debug audit" sentence (`:12`) under Registration and Layout Contracts.
- `ThirdParty/imgui/imgui.cpp` — root-cause evidence only, never edited: `style.Alpha <= 0.0f` sets `HiddenFramesCanSkipItems` (`:8112-8113`), which feeds `hidden_regular` (`:8116`) and forces `skip_items` (`:8128-8131`), so `Begin` returns false (`:8151`).
- `Engine/Source/Graphics/Graphics.cpp` — audit re-arm lifecycle evidence only, never edited: `Destroy()` resets `mpImGuiManager` (`:762`) at `>= DestroyType::kSwapchain` (`:723`) and `Create()` rebuilds it (`:455-457`).

## Risk tier and invariants

**Tier 2.** Trigger: scoped behavior change in the client-only, `kbDebugInput`-gated TweaksScreen audit. Invariants to hold:

- Normal TweaksScreen rendering, layout, section/subtab behavior, persistence, and interaction are unchanged — the audit pass stays non-visible and non-interactive.
- The audit contract is unchanged: map keys stay globally unique, a `WrapperSlider` `mapKey` absent from the map stays a missed key, a registration with no reachable call site stays an orphan key, and the subtab cycling across `kiAuditFrameCount` frames and the completion reporting stay as-is.
- The existing `ScopedSuppressAllocationTracking` boundaries around audit-owned STL work are preserved; no new allocation path enters the tracked main loop.
- No exposure to deterministic simulation/CRC state, replay/save compatibility, wire protocol, `kiVersion`, `.pack` layout, shaders, or client/server guard scope.

**Out of scope:**

- Adding, removing, renaming, or retuning wrappers, slider-map entries, or visible controls.
- Changing normal TweaksScreen layout, section/subtab behavior, persistence, or interaction.
- The game-extensible `TweakSection` section-registry architecture, which has landed as a separate completed change. Sections register at startup and dispatch through `TweakSectionDesc::pfnRender`; do not redesign that mechanism here.
- General registry redesign, new audit infrastructure, or unrelated ImGui cleanup.
- Unit tests.

## Acceptance criteria

- Opening TweaksScreen through the agent harness completes the audit having actually traversed live `WrapperSlider` calls: `mAuditTouched` is non-empty, and the orphan-key warning count drops from the captured 513-of-513 baseline to a residual set that is a small fraction of the registry. The pre-fix signature (orphan count equal to the registrar entry count, `mAuditTouched` empty) must not reproduce.
- Any orphan or missed keys still reported after the fix are enumerated in the change report and triaged as either genuine registry drift or registrations gated behind runtime state / a section with more subtabs than `kiAuditFrameCount`. Fixing them is out of scope: a residual orphan set does not fail this plan, but an untriaged one does. Zero orphans is not required and is not asserted — the audit only visits `kiAuditFrameCount` subtabs per section.
- Harness interaction confirms representative existing sliders — including the disambiguated label/map-key sliders whose keys are `Spread Decay End` and `Spread Accumulation Decay End` (`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp:55-56,177-178`) — remain visible, addressable, and settable after the fix.
- The affected Debug client target compiles, and the harness log contains no new audit, UI, or allocation-tracking errors.
- Static inspection confirms the audit still reports both mismatch classes when their conditions are present; no intentionally mismatched fixture or temporary source mutation is required.

## Notes

- Runtime verification uses `/agent-harness`. Because the audit re-arms on any swapchain-tier `Graphics` recreate and starts at `miAuditFrame = 0` on a fresh `TweaksScreen`, begin each decisive run from a fresh client process — the simplest reliable reset of the audit lifecycle.
