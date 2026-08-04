<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-03T22:03:08.553Z","dependsOn":[]} -->
# Fix: AgentHarness ImGui tab item addressing

## Context

The existing AgentHarness label-addressing contract cannot select a Dear ImGui tab item. `AgentUiRegistry` records a widget label only when its test-engine `ItemInfo` hook finds an earlier `ItemAdd` record (`Engine/Source/Agent/AgentUiRegistry.cpp:90-124`). Dear ImGui's `BeginTabItem` reports `IMGUI_TEST_ENGINE_ITEM_INFO(id, label, ...)` before its real tab rectangle is submitted with `ItemAdd(bb, id)` (`ThirdParty/imgui/imgui_widgets.cpp:10447-10454,10553-10564`), so the tab's label is dropped and the zero-size layout placeholder is not associated with the eventual rectangle. `BuildDescribeUi` omits the resulting empty-label record (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:950-956`), and the existing `ResolveLabel` path cannot address it.

The observed request was `click {"label":"Low","window":"Water"}`. It returned `no widget matches label`, listing only Specular controls as candidates. A raw-coordinate click returned `ok:true`, but it did not establish that the tab was selected; the following `describe_ui` result still exposed Specular controls and no `Low` tab. The symptom is that the tab label reported before its real rectangle is submitted is not addressable by label.

The user chose to test the water change manually and authorized this independent tooling follow-up. This Plan uses the baseline-stable `Low Max` control only to verify labeled-widget coverage. The generic commands already exist (`describe_ui`, `click`, and `set_slider`), so this restores their intended labeled-widget coverage rather than adding a new command or player-facing feature.

## Design

Repair the client-side registry's hook-order handling and reuse the current `label` + optional `window` addressing path. Keep normal widgets on their current immediate `ItemAdd`/`ItemInfo` path. For metadata that arrives before an `ItemAdd` (the Dear ImGui tab path), retain a bounded pending record keyed by `ImGuiID` whose pending state stores the label only, never a pre-`ItemAdd` visibility/status value. Attach the label to the first real, non-empty tab rectangle in that frame's layout pass. Do not publish the zero-size tab-layout placeholder; at a non-empty `ItemAdd`, derive `Visible` from the same rectangle/clip-overlap predicate used for ordinary records and carry only status validly derivable from that rectangle. Clear stale pending entries at the frame boundary. Derive the real tab's remaining status from its submitted rectangle/hook data so `AgentInput::StabilizeTarget` can use the existing center-click path. Preserve the existing window pseudo-item exclusion.

Keep the registry double-buffered, fixed-capacity, and allocation-free in steady state. Leave `ResolveLabel`'s exact-full-label, exact-display-label, then case-insensitive-substring tiers and its `window` filter unchanged. Cross-window duplicate display labels resolve by the existing `window` filter; distinct full labels that share a display label resolve by exact full label; identical full labels in one window remain ambiguous. Verify these cases with existing ordinary widgets and no new screen fixtures. Do not add coordinate-based fallback. The existing `BuildDescribeUi`, `CommandClick`, and `CommandSetSlider` handlers should consume the repaired records without a new JSON field or command schema.

## Critical files

- `Engine/Source/Agent/AgentUiRegistry.h` — `AgentUiItem` and the fixed-capacity registry state/hooks; add only the bounded pending metadata needed for pre-`ItemAdd` labels.
- `Engine/Source/Agent/AgentUiRegistry.cpp` — `HookItemAdd`, `HookItemInfo`, `Swap`, and the `ImGuiTestEngineHook_ItemAdd`/`ImGuiTestEngineHook_ItemInfo` bridge; preserve normal-item pairing, window pseudo-item filtering, completed-frame publication, and allocation-free behavior.
- `Engine/Source/Agent/AgentInput.cpp` — read-only verification site for `StabilizeTarget` (`:114-175`), including the existing visibility gate and center-coordinate injection.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — read-only verification sites `BuildDescribeUi` (`:929-973`), `BeginScriptAndDefer`/`CommandClick` (`:1092-1165`), and `CommandSetSlider` (`:1180-1192`); the existing command schema remains the consumer.
- `ThirdParty/imgui/imgui_widgets.cpp` — read-only hook-order evidence at `BeginTabItem`/`TabItemEx` (`:10447-10454,10553-10564`); do not modify vendored ImGui.

## In scope

- `Engine/Source/Agent/AgentUiRegistry.h`: fixed-capacity pending metadata required to pair a pre-`ItemAdd` tab label with its real rectangle.
- `Engine/Source/Agent/AgentUiRegistry.cpp`: the hook-pairing, placeholder suppression, visibility/status propagation, and frame-boundary cleanup in `HookItemAdd`, `HookItemInfo`, `Swap`, and the two test-engine hook bridge functions named above.
- The existing `describe_ui`/`click`/`set_slider` command paths only as compile-checked consumers of the repaired registry; no new command or JSON field.

## Out of scope

- All water shader, uniform, wrapper, Tweaks-screen, and screenshot behavior in `Documents/Plans/Graphics/WaterShoalingAmplitude.md`; the manual `Break Depth` test remains that Plan's acceptance work.
- Changes to `AgentInput` script phases, `ResolveLabel` matching tiers, duplicate-label semantics, or window naming beyond preserving their current behavior.
- New coordinate APIs, coordinate guessing, a broad UI refactor, or stable-ID changes to game screens.
- `Tools/AgentHarness` framing, ownership/lock protocol, transport, or executable command schema; the existing client command channel remains unchanged.
- `ThirdParty/imgui` source/header edits, simulation state, determinism/CRC, serialization, replay, wire protocol, and any other game behavior.
- Unit tests.

## Risk tier and invariants

Tier 2 — scoped client AgentUiRegistry behavior. The change stays in one engine automation subsystem and reuses the existing command schema; it does not touch simulation/CRC, serialization, replay, wire framing, threading, trust-boundary validation, or build/bootstrap coordination. The fixed-capacity registry remains allocation-free in steady state, publishes only completed-frame records, never exposes a zero-size tab placeholder, and keeps window pseudo-items unaddressable. Existing ordinary-widget records and duplicate-label ambiguity remain unchanged.

## Acceptance criteria

- In a live client run, `click {"label":"Low","window":"Water"}` succeeds through label/window addressing without raw coordinates, and its post-click `describe_ui` result shows the `Low` tab selected with the baseline-stable `Low Max` item in window `Water`.
- `describe_ui` exposes visible, non-empty tab labels and non-zero rectangles for the active tab bar while retaining the existing window and ordinary-widget fields.
- Addressing ambiguity is verified without new screen fixtures: cross-window duplicate display labels resolve by `window`; distinct full labels with the same display label resolve by exact full label; identical full labels in one window remain ambiguous. `ResolveLabel` and existing ordinary-widget clicks remain unchanged.
- The registry's fixed-capacity/allocation-free and completed-frame behavior remains intact, including no addressable `Begin` window pseudo-item and no stale pending label after a tab disappears.
- `/compile`'s BrokenEngineSandbox client Debug|x64 build passes for the changed registry path; selectively invalidate affected client translation units if appropriate. Runtime verification requires only that the provisioned AgentHarness and server executables exist; this Plan does not rebuild AgentHarness or Tool targets. No unit tests are added.
- `/agent-harness` reproduces the scenario above with the provisioned client/server executables and no coordinate guessing, confirming labeled `Low`/`Low Max` addressing.
- WorktreeCli `plan validate` exits `0` with `status:valid` and `code:ok`, and its `plans` inventory contains the exact path `Documents/Plans/Agents/AgentHarnessImGuiTabSelection.md`.

## Notes

This Plan has no dependency on `WaterShoalingAmplitude.md`; the tab-addressing gap is independently landable. The active water Plan retains ownership of all water behavior and its manual slider verification.
