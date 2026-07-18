# Agent Tweaks UI Automation

## Context

Runtime verification of the Tweaks quick-win refactor on 2026-07-18 exposed an agent-harness capability gap. F3 visibly opened and closed the Tweaks windows, but `describe_ui` and `describe_scene` reported `uiState: "kNone"`, and `click {"label":"Low","window":"Water"}` failed because no matching widget was registered. The current acceptance had to use a persisted-settings fixture instead of driving the tabs; the user approved that replacement and explicitly requested this follow-up.

Two verified boundaries cause the gap:

- `BuildDescribeUi` reads `gpGame->meUiState` (`Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp:466`), and `CommandDescribeScene` does the same (`Projects/BrokenEngineSandbox/Source/Agent/AgentScene.cpp:192`). F3 instead toggles `Game::mbShowImGui` (`Projects/BrokenEngineSandbox/Source/Game.cpp:771-774`). `UiState::kTweaks` already exists (`Game.h:28-37`) but is not reached by this overlay path. `Game::ShouldShowInGameUi` (`Game.cpp:539-542`) establishes that `mbShowImGui` takes precedence while the Tweaks overlay is visible.
- The vendored ImGui `TabItemEx` reports `IMGUI_TEST_ENGINE_ITEM_INFO` before its later `ItemAdd` (`ThirdParty/imgui/imgui_widgets.cpp:10451` and `:10564`). `AgentUiRegistry::HookItemInfo` (`Engine/Source/Agent/AgentUiRegistry.cpp:107`) searches only items already added to the current write table, so it drops the early label and status flags; the later tab geometry remains unlabeled and cannot be resolved by `click`.

These are distinct defects at the two sides of one automation contract: the harness must both identify the active Tweaks overlay and address the overlay's tab controls. They should land together because the same end-to-end scenario verifies the complete capability and either half alone still leaves Tweaks tab automation unusable.

## Design

1. Make `AgentUiRegistry` tolerate both hook orders. Preserve the existing completed-frame double buffer and resolve an item's label/status and geometry into one record whether `HookItemInfo` or `HookItemAdd` arrives first. A bounded pending/merge representation inside the existing registry boundary is the likely shape, but execution must choose the smallest design supported by the actual ImGui hook sequences rather than prescribing an unverified exact algorithm.
2. Preserve the registry contract: fixed capacity, zero steady-state heap allocation, completed-frame publication, existing label resolution precedence, disabled/status flags, window association, and best-effort bounded overflow behavior. Do not modify vendored ImGui.
3. Give the game agent reporting path one consistent effective UI-state rule: while `Game::mbShowImGui` is true, both `describe_ui` and `describe_scene` report `kTweaks`; otherwise they report the existing `meUiState`. Ground the precedence in `Game::ShouldShowInGameUi` and avoid mutating the production UI state machine merely to satisfy reporting.
4. Keep `click` on the existing `AgentUiRegistry`/`AgentInput` label-addressing path. Tab selection must use normal synthetic ImGui input and return the existing `found: true` response, without screenshots, pixel guessing, or Tweaks-specific command arms.
5. Update `.agents/skills/agent-harness/SKILL.md` so the `describe_ui`, `describe_scene`, and label-addressable `click` contracts document the effective Tweaks state and tab-item support.

This is a Tier 3 change because it changes the agent JSON command behavior across the engine-owned UI registry and game-owned reporting layer at the loopback command trust boundary.

## Critical files

- `Engine/Source/Agent/AgentUiRegistry.h` — fixed-capacity item/pending representation and zero-allocation contract.
- `Engine/Source/Agent/AgentUiRegistry.cpp` — `HookItemAdd`, `HookItemInfo`, completed-frame merge/publication, and label resolution.
- `Engine/Source/Agent/AgentInput.cpp` — existing label-driven tab click path; preserve rather than specialize.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommandsClient.cpp` — `BuildDescribeUi` effective `uiState` reporting and `CommandClick` integration.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentScene.cpp` — `CommandDescribeScene` effective `uiState` reporting.
- `Projects/BrokenEngineSandbox/Source/Game.h` and `Game.cpp` — `UiState::kTweaks`, `mbShowImGui`, and `ShouldShowInGameUi` authority for overlay precedence.
- `ThirdParty/imgui/imgui_widgets.cpp` — hook-order reference only; do not edit.
- `.agents/skills/agent-harness/SKILL.md` — public automation command contract.

## Out of scope

- Production UI state-machine changes or assigning `meUiState = kTweaks` for F3.
- The current `BeginSubtab` Tweaks refactor or other Tweaks implementation changes.
- Generic screenshot vision, pixel-coordinate discovery, or pixel guessing.
- Modifications to vendored ImGui.
- Unrelated Tweaks slider-map warnings.
- New agent commands or Tweaks-specific command parameters.

## Acceptance criteria

- Debug client and server compile through `/compile`.
- In a harness-driven Debug client, F3 opens Tweaks and both `describe_ui` and `describe_scene` report `uiState: "kTweaks"`; F3 closes it and both return to the underlying `meUiState` value.
- With the Water window open, `describe_ui` publishes label-addressable `Low` and `Depth` tab items in window `Water` with valid rectangles and status flags.
- `click {"label":"Low","window":"Water"}` returns `found: true`, selects Low, and exposes Low-only content such as `Low Max`. `click {"label":"Depth","window":"Water"}` returns `found: true`, selects Depth, and exposes Depth-only content such as `Water Terrain Height`.
- After selecting Low, cleanly quitting, and relaunching, the Water window restores Low as selected and exposes its Low-only content; clicking Depth still changes the content normally.
- The most populated Tweaks view remains within the registry's fixed capacity, repeated tab discovery/clicks remain stable, and the Debug run shows no allocation-tracking break or registry-overflow regression. Static review confirms early metadata storage and merging use fixed-capacity member storage with no steady-state heap allocation.
- C++ correctness, affected-code, and style reviews cover the engine/game changes; the changed skill passes `/validate-skill` and its fresh coherence review.

## Notes

- Agent loopback JSON behavior changes: `uiState` more accurately reports an already-visible overlay, and tab items become addressable through the existing item schema. Gameplay network/wire protocol is unchanged.
- Client-only UI and agent reporting paths do not touch deterministic simulation, CRC state, replay/save compatibility, `kiVersion`, `.pack` data, serialization layout, shaders, or server runtime behavior. The server build remains an acceptance check because shared sources and guards compile in both targets.
- Registry hooks run in the allocation-tracked render loop. The existing fixed-capacity, zero-steady-state-heap invariant is mandatory.
- No new source files are expected; project membership should remain unchanged.
